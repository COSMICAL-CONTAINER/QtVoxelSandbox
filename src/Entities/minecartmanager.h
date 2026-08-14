#ifndef MINECARTMANAGER_H
#define MINECARTMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <vector>

// 矿车实体管理器（t565；Entities 层）。机制等价 MC 1.0 minecart。
//
// 矿车是新实体类型：放在铁轨上（Rail 方块），玩家右键矿车实体 → 骑乘，W/A/D 控制沿轨道前后 /
// 拐弯（轨连接位导向 —— 矿车从当前轨格沿「玩家意图方向最近的连接向」推进到相邻轨格，拐角自动转弯；
// 机制等价 MC 1.0 矿车沿轨行驶 + 弯道自动转向），轨上快速移动（明显快于步行）；离轨（下一格无轨）→ 停。
// 左键挖矿车 → 矿车实体消失 + 掉矿车物品（可重放）。
//
// 数据形态：每个矿车实体 = {世界坐标 pos（矿车中心，轨面上 0.15）、水平行进方向 dirX/dirZ（单位向量，
//   轨向四向之一）、朝向 yawDeg（呈现层 Model 据它定向）、速度 speed（blocks/s，W 前进加速 / 松键摩擦衰减）、
//   槽位 alive（slot-reuse 模型）}。呈现层（Main.qml 的 cartHost Repeater）经 count/posAt/yawAt 读数据，
//   自发渲染矿车 Model（原创斗形几何，NoLighting），绝不反向写。
//
// 骑乘（steer）：单机唯一玩家 → MinecartManager 持 m_riderCart（玩家当前骑的矿车索引，-1 = 未骑）。
//   tryMount(origin,dir,maxDist) 跑独立矿车命中射线（findCartHit，slab ray-AABB，同 BoatManager::findBoatHit
//   模式）命中矿车 → m_riderCart = idx + 返 true；dismount(world,...) 清 m_riderCart + 把玩家摆到矿车侧
//   安全位。PlayerController.step 骑乘分支调 tickRiddenCart 推进矿车物理（WASD 输入由 PlayerController 算
//   wish 传入），并据返回的矿车位把玩家 m_pos 同步到车座位（玩家随车位移、骑乘期禁用玩家自身移动）。
//
// 物理（tickRiddenCart，被骑的矿车）：据 wish 在矿车行进方向上的投影算目标速度（前进 / 后退），
//   速度向目标 lerp（动量）→ 沿「轨连接位」逐格推进（advanceAlongTrack：从当前轨格的 4 向连接位中选
//   与行进方向点积最大的连接向 → 矿车向该邻轨格移动；跨格中心时更新行进方向 = 该连接向（拐角自动转向）；
//   无连接（轨尽头）→ 停）。矿车 Y 钉轨面（轨格 cell 顶 + kCartRideH）。
//
// 分层（PLAN §2）：本层属 Entities（位于 Game/Physics 之下、World 之上）。向下只读 World
// （blockAt / stateAt，判轨 / 连接位），不依赖 Renderer / Physics / QtQuick3D。tickRiddenCart /
// tryMount / dismount / hitCartFromRay 由 PlayerController（Game/Physics 层）每帧 / 右键 / 左键时调
// （C++ 直调，非 Q_INVOKABLE —— 同 BoatManager / ItemEntityManager 先例）。spawnCart 兼 Q_INVOKABLE
// 供 QML / PlayerController placeBlock 双入口。
class World; // 前向声明（tickRiddenCart 只读 World；完整定义在 .cpp include）
class MinecartManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MinecartManager)
    // count：当前矿车槽数（含已释放空槽；slot-reuse 模型下单调不降）。Repeater 作 int model → 生成
    //   0..count-1 delegate。NOTIFY entitiesChanged 驱动 spawn 后 Repeater 追加新 delegate（不重建已有）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：矿车集版本号（随 spawn / 挖毁 / 骑乘物理推进 自增）。供「触碰」绑定作 NOTIFY 触发器
    //   （同 BoatManager.revision 模式）—— posAt/yawAt 是 Q_INVOKABLE 不被 NOTIFY 自动跟踪，需
    //   { revision; posAt(i) } 显式建依赖。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit MinecartManager(QObject *parent = nullptr);

    int count() const { return int(m_carts.size()); }
    int revision() const { return m_revision; }
    // 当前活体矿车数（不含已释放空槽）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // 第 i 个槽位是否活体。呈现层 delegate 据它 visible（空槽隐藏，slot 复用保 Repeater count 单调不降）。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 在铁轨格 (x,y,z)（整数坐标，Rail 方块格）生成一个矿车实体。位置 = 该格中心、轨面上 kCartRideH。
    //   目标格非 Rail → 不生成（防御；placeBlock 已守）。达 kCap → 跳过 + qWarning（防溢出）。
    Q_INVOKABLE void spawnCart(int x, int y, int z);

    // 玩家当前骑的矿车索引（-1 = 未骑）。PlayerController.step 据它判骑乘分支。
    Q_INVOKABLE int ridingIndex() const { return m_riderCart; }

    // 第 i 个矿车的世界坐标（呈现层 delegate 绑它摆位）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个矿车的朝向（度；车头方向，呈现层矿车 Model eulerRotation.y）。越界返回 0。
    Q_INVOKABLE float yawAt(int i) const;

    // 切世界清空（同 BoatManager.clearAll 模式）：释放全部活体槽（保 slot-reuse 单调不变量）+ 清骑乘态。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_carts.size(); ++i)
            if (m_carts[i].alive) releaseSlot(int(i));
        m_riderCart = -1;
        emit entitiesChanged();
    }

    // ── C++ 直调（PlayerController 用；非 Q_INVOKABLE 避 moc 对 World* 前向类型的 metatype 处理）──

    // 跑独立矿车命中射线（slab ray-AABB，同 BoatManager::findBoatHit 模式）：从 origin 沿 dir（单位向量）
    //   maxDist 内命中首个活体矿车 → 返其索引（outDist 写命中距离）；无命中 → -1。
    int findCartHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const;

    // 挖矿车（攻击）：跑 findCartHit 命中矿车 → 移除该矿车（releaseSlot）+ emit cartBroken（呈层据它
    //   spawnItem 掉矿车物品 MinecartId）。若命中的是被骑的矿车（idx == m_riderCart）→ 同步清 m_riderCart。
    //   由 PlayerController.beginMining 调（左键瞄矿车 → 挖矿车，优先于破块路径，同船 hitBoatFromRay 分流模式）。
    //   返 true = 命中并移除（caller 据此发 swingArm + 不再走破块）；false = 未命中（caller 落回破块路径）。
    bool hitCartFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 尝试骑乘：跑 findCartHit 命中矿车 → 设 m_riderCart + 返 true；未命中 / 命中当前骑的 → false。
    //   由 PlayerController placeBlock 矿车段调（右键瞄矿车 → 上车，优先于放矿车）。
    bool tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 下车：清 m_riderCart + 把玩家摆到矿车侧安全位（outPlayerFeet 写玩家脚底 m_pos）。
    //   由 PlayerController Shift 下车 / 切世界调。矿车保留在世界（不下车不消失）。返 false = 无骑乘（no-op）。
    bool dismount(World *world, QVector3D &outPlayerFeet);

    // 推进被骑矿车的操控物理（PlayerController.step 骑乘分支调）：wishX/wishZ = 玩家 WASD 据 yaw 算出的
    //   水平单位意图向量。沿「当前行进方向」的投影算目标速度（前进 / 后退），速度 lerp 接近 → 沿轨连接位
    //   逐格推进（advCell 向邻轨格插值移动；跨格时按行进方向选下一连接向 —— 拐角自动转弯；轨尽头停）。
    //   outCartPos 写新矿车中心位（PlayerController 据它把玩家 m_pos 同步到车座位）。
    //   无骑乘（m_riderCart<0）→ no-op。
    void tickRiddenCart(qreal dt, World *world, float wishX, float wishZ, QVector3D &outCartPos);

signals:
    void entitiesChanged();                        // spawn / 挖毁 / 骑乘物理推进触发；驱动 count/revision + QML 绑定刷新
    void cartBroken(int x, int y, int z);          // 矿车被「挖」（攻击）→ 呈层据它 spawnItem 掉 MinecartId 物品

private:
    struct Cart {
        QVector3D pos;       // 矿车中心世界坐标（轨面上；PlayerController 据它把玩家摆车座位）
        float dirX = 0.0f;   // 行进方向 X（单位向量，轨向四向之一；拐角跨格时更新）
        float dirZ = 1.0f;   // 行进方向 Z
        float speed = 0.0f;  // 沿行进方向速度（blocks/s；W 加速 / 松键摩擦衰减 / 轨尽头停）
        float yaw = 0.0f;    // 车头朝向（度；呈现层矿车 Model eulerRotation.y）
        bool alive = true;   // slot-reuse 槽位占用标志（放末位：聚合初始化尾字段缺省取 default member init）
    };
    std::vector<Cart> m_carts;
    int m_revision = 0;
    int m_riderCart = -1;   // 玩家当前骑的矿车索引（-1 = 未骑）
    std::vector<int> m_freeSlots; // slot-reuse：已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体矿车数

    int acquireSlot(Cart &&c)
    {
        int slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_carts[size_t(slot)] = std::move(c);
        } else {
            m_carts.push_back(std::move(c));
            slot = int(m_carts.size()) - 1;
        }
        ++m_liveCount;
        return slot;
    }
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_carts.size())) return;
        m_carts[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    void notifyChanged() { ++m_revision; emit entitiesChanged(); }

    // 从矿车所在轨格出发，沿「与 (wantX,wantZ) 点积最大」的连接向找邻轨格位移 (outDx,outDz)。
    //   返 false = 无可用连接（轨尽头 / 轨连接位为空）。拐角（如行进 +Z、连接 +X）自动选中 → 矿车转弯。
    //   排除「来路」（-dir 反向连接点积为负自然排后；完全掉头仅在末端无前进连接时允许 —— 简化：允许，
    //   MC 矿车在尽头轨可反向推回）。
    bool pickTrackStep(World *world, const QVector3D &cartPos, float wantX, float wantZ,
                       int &outDx, int &outDz) const;

    static constexpr int kCap = 64;             // 矿车数上限（防溢出；同船 cap 量级）
    // 矿车几何 / 物理常量（机制等价 MC 1.0 minecart；手感可玩）：
    //   矿车斗形外观 ~0.9×0.5×1.0（长轴沿行进方向 Z）；碰撞盒半宽 0.45 / 半长 0.5 / 半高 0.45。
    static constexpr float kCartHalfW  = 0.45f;  // 矿车 footprint 半宽（X；footprint 宽 0.9，匹配车斗）
    static constexpr float kCartHalfL  = 0.50f;  // 矿车 footprint 半长（Z；footprint 长 1.0）
    static constexpr float kCartHalfH  = 0.45f;  // 矿车命中盒半高（0.9 高，含车帮）
    // 矿车中心距轨面（轨格 cell 顶）的骑乘高度：车底板贴轨面（铁轨薄板 y=1/16 之上）→ 中心 = 轨顶 + 0.3。
    static constexpr float kCartRideH  = 0.30f;
    // 轨上矿车速度（blocks/s）：明显快于步行 4.3（机制等价 MC 1.0 矿车轨上 8 blocks/s）。
    static constexpr float kCartSpeed  = 8.0f;
    // 速度 lerp 接近率（1/s；动量感：松键后滑行一段渐停）。
    static constexpr float kCartAccel  = 3.0f;
    // 空车 / 松键摩擦衰减率（1/s）。
    static constexpr float kCartFriction = 2.0f;
};

#endif // MINECARTMANAGER_H
