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
    //   放船前判「是否瞄着已有船」+ 左键挖船判目标。
    int findBoatHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const;

    // t508 挖船掉落（spec「攻击 / 挖船 → 船实体消失 + 掉落船物品」；机制等价 MC 1.0 攻击船 → 船破坏掉船物品）。
    //   跑 findBoatHit 命中船 → 移除该船（releaseSlot）+ emit boatBroken（呈层据它 spawnItem 掉船物品）。
    //   若命中的是被骑的船（idx == m_riderBoat）→ 同步清 m_riderBoat（船没了玩家自然没骑）。
    //   由 PlayerController.beginMining 调（左键瞄船 → 挖船，优先于破块 / 挥空手路径，同 mob 攻击分流模式）。
    //   返 true = 命中并移除（caller 据此发 swingArm + 不再走破块）；false = 未命中（caller 落回破块路径）。
    bool hitBoatFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 尝试骑乘：跑 findBoatHit 命中船 → 设 m_riderBoat + 返 true；未命中 → false（不改态）。由 PlayerController
    //   placeBlock 船段调（右键瞄船 → 上船）。t508 换船：已骑乘时命中**另一艘**船（idx != m_riderBoat）→ 直接切到
    //   新船（spec「骑船时右键另一艘船来坐上去」；旧船释放骑乘态自然浮水）；命中当前骑的船 → no-op（返 false）。
    bool tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 下船：清 m_riderBoat + 把玩家摆到船侧安全位（outPlayerFeet 写玩家脚底 m_pos）。由 PlayerController
    //   Shift 下船 / 撞毁 / 切世界调。船保留在世界（不下船不消失）。返 false = 无骑乘（no-op）。
    bool dismount(World *world, QVector3D &outPlayerFeet);

    // 推进所有船物理（常开，由 PlayerController.tick 每帧调；菜单 / 暂停时世界照常模拟）：
    //   未骑的船 → 浮水（pos.y 向水面 lerp）+ 水平速度摩擦衰减（空船渐停）+ 玩家碰撞推开（pushable，机制等价
    //   MC 1.0 船可被实体推开）。被骑的船的「操控位移」由 tickRiddenBoat 单独推进（骑乘期 wish 驱动），tick 只做
    //   浮水（与 tickRiddenBoat 同帧先后跑，浮水把船 Y 钉水面、操控位移改 XZ）。world null / 无船 → 早 return。
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

    // t508 玩家位置注入（pushable 用）：PlayerController.tick 每帧 tick 前调一次，写玩家 AABB 中心（脚底 +
    //   半高）。tick 内据此判玩家 ↔ 船重叠 → 把船推开（机制等价 MC 1.0 船可被玩家 / 实体推开）。无玩家时
    //   （菜单 / 暂停）传 NaN 坐标 → 不推（实现 .cpp，用 std::isfinite 判有效）。
    void setPlayerCenter(const QVector3D &center);

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
    // t508 玩家中心（pushable 用）：每帧由 PlayerController.setPlayerCenter 写入。tick 内读它判玩家 ↔ 船重叠
    //   → 推船。m_playerValid=false（NaN / 未设）时不推（菜单 / 暂停态）。
    QVector3D m_playerCenter{0, 0, 0};
    bool m_playerValid = false;

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
    //   t508 二轮复盘：outFoundWater（可空）写真是否找到水柱 —— tick 据此判「浮水」vs「无水重力落地」
    //     （旧版用船中心格 == Water 判 hasWater，但船浮水面时中心格常是水上空气格 → 误判无水 → 重力把船拽下水，
    //     用户报②「放水上直接飞到水下」真因）。waterSurfaceY 内已扫水柱，复用其结论更准。
    float waterSurfaceY(World *world, float px, float pz, float fallbackY, bool *outFoundWater = nullptr) const;
    // 船当前所在「支撑面」的方块 id（判冰面加速）：从船中心所在格向下找首个**可踩实体方块**（含船中心格本身）。
    //   t508 修：旧版固定读 floor(pos.y) − 1（船中心格的下方一格），但船中心本就浮在水面 / 冰面格内
    //   （pos.y = surfaceY + 1 − draft → floor = surface 格），「−1」跳过了船实际踩着的表面格 → 永远读到
    //   水底 / 冰下，冰面加速从未生效。改：自船中心格向下扫（含本格）首个可踩实体（isCollidable）→ 船在
    //   冰面格时直接读到 Ice / PackIce / BlueIce。船在水面（水非 collidable）→ 跳过水继续向下到水底实体。
    quint8 blockBelowBoat(World *world, const QVector3D &boatPos) const;

    static constexpr int kCap = 64;            // 船数上限（防溢出；船相对稀有，cap 取 mob 量级）
    // 船几何 / 物理常量（机制等价 MC 1.0 boat；手感可玩）：
    //   boat 三轮「船太小坐不下」（用户报④）：旧 kBoatHalfW=0.6（footprint 1.2×1.2）、视觉船 1.4×0.7，
    //     船舱内宽 ~0.58 < 玩家半宽 0.3×2=0.6 → 玩家模型塞不进船（肉眼「坐不下」）。放大碰撞盒至 0.8
    //     （footprint 1.6×1.6，对齐视觉船长 1.6）→ F3+B 盒覆盖整船 + 骑乘命中更宽容。kBoatHalfH 0.5→0.55
    //     （命中盒 1.1 高，包住 0.4 高船舷 + 坐姿大腿），右键上船仍近身即中。
    static constexpr float kBoatHalfW    = 0.8f;   // 船 footprint 半宽 / 半长（约 1.6×1.6；碰撞 + 命中盒）
    static constexpr float kBoatHalfH    = 0.55f;  // 船命中盒半高（1.1 高，略大于视觉船舱 0.65；放宽右键骑乘命中）
    //   t508 二轮复盘修「整个悬浮在水中」（用户报⑦）：旧 kBoatDraft=0.25 → 船中心 Y = 水面顶 - 0.25（船吃水 0.25），
    //     而船舷顶（model +0.225）恰与水面平齐 → 整条船视觉沉在水面下、只露舷沿 =「悬浮在水中」。改为 0.0：
    //     船中心 Y = 水源格 cell 顶（= 水面顶），船底（model -0.2）略入水 0.2、舷顶（+0.225）出水 0.225 →
    //     船大部分浮在水面之上、仅船底没入，视觉等同 MC 船浮水。spawnBoat / waterSurfaceY / dismount 共用本常量。
    static constexpr float kBoatDraft    = 0.0f;   // 船吃水（船中心距水面下沉量；0 = 船中心贴水面顶）
    // t508 二轮复盘：船底相对船中心的下沉量（视觉船底甲板底面 ≈ 中心 - 0.2）。无水重力落地稳态 Y =
    //   支撑方块顶 + kBoatHullBottom（船底贴支撑顶）。与 boats delegate 船底甲板 Model（position.y=-0.15、
    //   scale.y=0.1 → 底面 -0.2）对齐 —— 改几何时同步改本常量（两处一致才不沉地 / 不悬空）。
    static constexpr float kBoatHullBottom = 0.2f;
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
    // t508 玩家推船给的水平速度冲量（blocks/s；每次接触分离时叠入 vx/vz）。机制等价 MC 1.0 船被实体撞开
    //   后会滑一小段；取 2.0 使船明显被弹开但摩擦很快停（kBoatFriction=3 → ~0.3s 基本停）。
    static constexpr float kBoatPushImpulse = 2.0f;
    // t508 二轮复盘修「陆地悬空 + 卡住沉底」（用户报③⑥）：船在无水格（陆地 / 冰面）应有重力 —— 旧 tick 浮水
    //   段无水时 waterSurfaceY 返 fallback（= 当前 pos.y）→ dy=0，船 Y 永不动 → 放陆地悬在放置点（pos.y = 放置
    //   格顶 + 0.75，悬空半格），且骑乘下船摆位算错。加常速重力让陆地船落到支撑面（boatFootprintBlocked 挡实块
    //   即停），与 MC 船放陆地会落地一致。取值 8.0（blocks/s²，明显但不过猛，落半格 ~0.3s 即贴地）。
    static constexpr float kBoatGravity = 8.0f;
};

#endif // BOATMANAGER_H
