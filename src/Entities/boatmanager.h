#ifndef BOATMANAGER_H
#define BOATMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <vector>

// 船实体管理器（t469；Entities 层）。机制等价 MC 1.0 boat。
//
// 船是新实体类型：浮在水面（Water 上方），玩家右键船实体 → 骑乘（steer），WASD 控制方向 + 前进，
// 水上快速移动（明显快于游泳）；冰面（Ice/PackIce/BlueIce）速度倍增（蓝冰最快，复用 BlockRegistry::isIce
// 判冰面 + 按冰类型给船速递增倍率）；高速撞硬墙 → 船损坏掉落船物品（可重放）。
//
// 数据形态：每个船实体 = {世界坐标 pos（船中心，浮水面）、水平速度 vx/vz（动量 / 冰上惯性）、
// 朝向 yawDeg（船头方向，呈现层船 Model 据它定向）、变体 boatType（Oak=0 浅色 / Spruce=1 深色）、
// 槽位 alive（slot-reuse 模型）}。呈现层（Main.qml 的 boatHost Repeater）经 count/posAt/boatTypeAt/yawAt
// 读数据，自发渲染船 Model（橡木 / 云杉贴图，NoLighting），绝不反向写。
//
// 骑乘（steer）：单机唯一玩家 → BoatManager 持 m_riderBoat（玩家当前骑的船索引，-1 = 未骑）。
//   tryMount(origin,dir,maxDist) 跑独立 boat 命中射线（findBoatHit，slab ray-AABB，同 EntityManager::findMobHit
//   模式）命中船 → m_riderBoat = idx + 返 true；dismount(world,...) 清 m_riderBoat + 把玩家摆到船侧安全位。
//   PlayerController.step 骑乘分支调 tickRiddenBoat 推进船物理（WASD 输入由 PlayerController 算 wish 传入），
//   并据返回的船位把玩家 m_pos 同步到船座位（玩家随船位移、骑乘期禁用玩家自身移动）。
//
// 物理（tick，未骑的船）：浮水（pos.y 向水面 lerp，机制等价 MC 船浮水）+ 水平速度摩擦衰减（空船不动）。
//   tickRiddenBoat（被骑的船）：据 wish + 当前所在格冰类型算目标速度（kBoatSpeed × iceMul；冰面加速），
//   船速向目标 lerp（动量），按速度积分水平位移 + 逐轴碰撞（撞可碰撞方块则该轴不动）。
//   撞墙且速度 > kBoatCrashSpeed → 视为高速撞毁（outCrashed=true，机制等价 MC 高速撞墙船损坏）；
//   低速轻撞（speed < kBoatCrashSpeed）只停下不坏。
//
// 撞毁掉落：breakRiddenBoat() 移除被骑的船 + 清 m_riderBoat + emit boatBroken(x,y,z,boatType) →
//   呈现层 Main.qml 转发到 ItemEntityManager.spawnItem 生成船物品掉落实体（机制等价 MC 船撞坏掉船物品）。
//   PlayerController 骑乘期撞毁后把玩家 m_pos 摆到末位船位（不坠虚空），下一 tick 回正常物理。
//
// 分层（PLAN §2）：本层属 Entities（位于 Game/Physics 之下、World 之上）。向下只读 World
// （blockAt / stateAt / isCollidable，判水面 / 冰面 / 碰撞），不依赖 Renderer / Physics / QtQuick3D。
//   tick / tickRiddenBoat / tryMount / dismount 由 PlayerController（Game/Physics 层）每帧 / 右键时调
//   （C++ 直调，非 Q_INVOKABLE 优先 —— 避开 moc 对 World* 前向类型的 metatype 处理，同 ItemEntityManager /
//   EntityManager 先例）。spawnBoat 兼 Q_INVOKABLE 供 QML / PlayerController placeBlock 双入口。
class World; // 前向声明（tick / tickRiddenBoat 只读 World；完整定义在 .cpp include）
class BoatManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BoatManager)
    // count：当前船槽数（含已释放空槽；slot-reuse 模型下单调不降）。Repeater 作 int model → 生成 0..count-1 delegate。
    //   NOTIFY entitiesChanged 驱动 spawn 后 Repeater 追加新 delegate（不重建已有）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：船集版本号（随 spawn / 撞毁 / 骑乘物理推进 / 浮水 自增）。供「触碰」绑定作 NOTIFY 触发器
    //   （同 Hotbar.slotRevision / ItemEntityManager.revision 模式）—— posAt/boatTypeAt/yawAt 是 Q_INVOKABLE
    //   不被 NOTIFY 自动跟踪，需 { revision; posAt(i) } 显式建依赖。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit BoatManager(QObject *parent = nullptr);

    int count() const { return int(m_boats.size()); }
    int revision() const { return m_revision; }
    // 当前活体船数（不含已释放空槽）。F3 draw-call 估算用它（空槽 delegate visible=false 不绘制）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // 第 i 个槽位是否活体。呈现层 delegate 据它 visible（空槽隐藏，slot 复用保 Repeater count 单调不降）。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 船变体（Q_ENUM 供 QML 据 boatTypeAt 选橡木 / 云杉贴图）。
    enum BoatType { Oak = 0, Spruce = 1 };
    Q_ENUM(BoatType)

    // 在方块格 (x,y,z)（整数坐标，水面格）生成一个 boatType 的船实体。位置存该格中心略上（浮在水面）。
    //   达 kCap → 跳过 + qWarning（防溢出）。
    Q_INVOKABLE void spawnBoat(int x, int y, int z, int boatType);

    // 玩家当前骑的船索引（-1 = 未骑）。PlayerController.step 据它判骑乘分支。
    Q_INVOKABLE int ridingIndex() const { return m_riderBoat; }

    // 第 i 个船的世界坐标（呈现层 delegate 绑它摆位）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个船的变体（Oak/Spruce；呈现层据它选贴图）。越界返回 Oak。
    Q_INVOKABLE int boatTypeAt(int i) const;
    // 第 i 个船的朝向（度；船头方向，呈现层船 Model eulerRotation.y）。越界返回 0。
    Q_INVOKABLE float yawAt(int i) const;

    // t176 存档：清空所有船（切世界 / 退出存档前调，防上一世界的船残留进新世界）。t437 模式：释放全部活体槽
    //   （保 slot-reuse 单调不变量：count 不降、Repeater 不销毁 delegate、无孤儿）。仅释放活体槽（幂等）。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_boats.size(); ++i)
            if (m_boats[i].alive) releaseSlot(int(i));
        m_riderBoat = -1; // 切世界清骑乘态
        emit entitiesChanged();
    }

    // ── C++ 直调（PlayerController 用；非 Q_INVOKABLE 避 moc 对 World* 前向类型的 metatype 处理）──

    // 跑独立 boat 命中射线（slab ray-AABB，同 EntityManager::findMobHit 模式）：从 origin 沿 dir（单位向量）
    //   maxDist 内命中首个活体船 → 返其索引（outDist 写命中距离）；无命中 → -1。供 PlayerController 右键骑乘 /
    //   放船前判「是否瞄着已有船」。
    int findBoatHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const;

    // 尝试骑乘：跑 findBoatHit 命中船 → 设 m_riderBoat + 返 true；未命中 → false（不改态）。由 PlayerController
    //   placeBlock 船段调（右键瞄船 → 上船）。已骑乘（m_riderBoat>=0）→ 不重复上（返 false）。
    bool tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 下船：清 m_riderBoat + 把玩家摆到船侧安全位（outPlayerFeet 写玩家脚底 m_pos）。由 PlayerController
    //   Shift 下船 / 撞毁 / 切世界调。船保留在世界（不下船不消失）。返 false = 无骑乘（no-op）。
    bool dismount(World *world, QVector3D &outPlayerFeet);

    // 推进所有船物理（常开，由 PlayerController.tick 每帧调；菜单 / 暂停时世界照常模拟）：
    //   未骑的船 → 浮水（pos.y 向水面 lerp）+ 水平速度摩擦衰减（空船渐停）。被骑的船的「操控位移」由
    //   tickRiddenBoat 单独推进（骑乘期 wish 驱动），tick 只做浮水（与 tickRiddenBoat 同帧先后跑，浮水把船
    //   Y 钉水面、操控位移改 XZ）。world null / 无船 → 早 return。
    void tick(qreal dt, World *world);

    // 推进被骑船的操控物理（PlayerController.step 骑乘分支调）：wishX/wishZ = 玩家 WASD 据 yaw 算出的水平
    //   单位意图向量（已含 W 前 / S 后 / A/D 横移）。据船当前格冰类型算目标速度（kBoatSpeed × iceMul），
    //   船速向目标 lerp（动量）→ 积分水平位移 + 逐轴碰撞。outBoatPos 写新船中心位（PlayerController 据它把
    //   玩家 m_pos 同步到船座位）。outCrashed=true = 高速撞硬墙（PlayerController 据 it 调 breakRiddenBoat
    //   掉船物品 + 下船）。无骑乘（m_riderBoat<0）→ no-op（outBoatPos 不变 / outCrashed=false）。
    void tickRiddenBoat(qreal dt, World *world, float wishX, float wishZ,
                        QVector3D &outBoatPos, bool &outCrashed);

    // 撞毁被骑的船：移除该船（releaseSlot）+ 清 m_riderBoat + emit boatBroken（呈层掉船物品）。
    //   由 PlayerController 骑乘期 outCrashed=true 时调。无骑乘 → no-op。
    void breakRiddenBoat();

signals:
    void entitiesChanged();                       // spawn / 撞毁 / 浮水 / 骑乘物理触发；驱动 count/revision + QML 绑定刷新
    void boatBroken(int x, int y, int z, int boatType); // 船撞毁 → 呈现层据它 spawnItem 掉船物品（机制等价 MC 船损坏掉船）

private:
    struct Boat {
        QVector3D pos;       // 船中心世界坐标（浮水面；PlayerController 据它把玩家摆船座位）
        float vx = 0.0f;     // 水平速度 X（blocks/s；动量 / 冰上惯性）
        float vz = 0.0f;     // 水平速度 Z
        float yaw = 0.0f;    // 船头朝向（度；呈现层船 Model eulerRotation.y）
        int boatType = Oak;  // 变体（Oak 浅色 / Spruce 深色）
        bool alive = true;   // slot-reuse 槽位占用标志（放末位：聚合初始化尾字段缺省取 default member init）
    };
    std::vector<Boat> m_boats;
    int m_revision = 0;
    int m_riderBoat = -1;   // 玩家当前骑的船索引（-1 = 未骑）
    std::vector<int> m_freeSlots; // slot-reuse：已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体船数

    int acquireSlot(Boat &&b)
    {
        int slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_boats[size_t(slot)] = std::move(b);
        } else {
            m_boats.push_back(std::move(b));
            slot = int(m_boats.size()) - 1;
        }
        ++m_liveCount;
        return slot;
    }
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_boats.size())) return;
        m_boats[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    void notifyChanged() { ++m_revision; emit entitiesChanged(); }

    // 船 footprint（half = kBoatHalfW）覆盖的格中是否有「可碰撞方块」（挡船）。y 为船中心所在格。
    //   可碰撞 = World::isCollidable（含实体方块 / 门 / 活版门；不含水 / 火把 / 空气）。
    bool boatFootprintBlocked(World *world, float px, float py, float pz) const;
    // 算船当前 XZ 列的水面 Y（找最顶水格 + 1 - kBoatDraft；无水 → 返当前 py 不浮）。用于浮水 lerp 目标。
    float waterSurfaceY(World *world, float px, float pz, float fallbackY) const;
    // 船当前所在格（脚下）的方块 id（判冰面加速 / 水面）。返脚下格 blockAt。
    quint8 blockBelowBoat(World *world, const QVector3D &boatPos) const;

    static constexpr int kCap = 64;            // 船数上限（防溢出；船相对稀有，cap 取 mob 量级）
    // 船几何 / 物理常量（机制等价 MC 1.0 boat；手感可玩）：
    static constexpr float kBoatHalfW    = 0.6f;   // 船 footprint 半宽 / 半长（约 1.2×1.2；碰撞 + 命中盒）
    static constexpr float kBoatHalfH    = 0.5f;   // 船命中盒半高（1.0 高，略大于视觉船舱 0.5；放宽右键骑乘命中）
    static constexpr float kBoatDraft    = 0.25f;  // 船吃水（船中心距水面下沉量；浮在水面略没入）
    //   kBoatSpeed：水上基础船速（blocks/s；明显快于游泳 kSwimUp=4.5 / 走 kWalk=4.3，机制等价 MC 船水上快）。
    //   kBoatAccel：船速向目标 lerp 接近率（1/s；越小越滑有惯性 —— 冰上叠加速时惯性明显）。
    //   kBoatFriction：空船 / 松键水平摩擦衰减率（1/s）。
    //   kBoatCrashSpeed：撞墙损坏速度阈值（blocks/s；高速撞硬墙才坏，低速轻撞只停）。
    //   kBoatTurnRate：船头转向速率（度/s；船头平滑转向意图方向，机制等价 MC 船缓转）。
    static constexpr float kBoatSpeed    = 8.0f;
    static constexpr float kBoatAccel    = 4.0f;
    static constexpr float kBoatFriction = 3.0f;
    static constexpr float kBoatCrashSpeed = 7.0f;
    static constexpr float kBoatTurnRate = 360.0f;
};

#endif // BOATMANAGER_H
