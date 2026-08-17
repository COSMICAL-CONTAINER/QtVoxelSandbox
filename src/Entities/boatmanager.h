#ifndef BOATMANAGER_H
#define BOATMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <vector>

// 船实体管理器（t469；Entities 层）。机制等价 MC 1.0 boat。
//
// 船是新实体类型：浮在水面（Water 上方），玩家右键船实体 → 骑乘（steer），WASD 控制方向 + 前进。
// t584 三档介质速度：陆地最慢（能开但贴地挪）/ 水中第二（明显快于游泳）/ 冰面最快（Ice/PackIce/BlueIce
// 倍率递增 + iceSlipApproach 低接近率 → 松键长滑行惯性 + 转向迟钝，难操作才是对的）。水中船碰岸边实心
// 方块（哪怕与水面同高）→ 整船速度清零停住（机制等价 MC 1.0 船撞岸受阻；速度 ≥ 撞毁阈值才毁）；
// 直接放陆地的船不触发该停止（靠陆地高摩擦自己慢）。高速撞硬墙 → 船损坏掉落散件（可重放）。
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
    //   达 kCap → 跳过 + qWarning（防溢出）；落点与既有活体船重叠（< kBoatMinSpawnDist）→ 拒绝。
    //   review rv2-A2：返 true = 已生成；false = 被拒（cap / 重叠，caller 据此不消耗船物品 —— 旧版静默
    //   void 返 + caller 无条件扣物品 →「下船后在脚下重放船：船没生成、物品也没了」）。Q_INVOKABLE bool
    //   对 QML 调用方无害（QML 可读 bool 返回值；当前 QML 无调用点，PlayerController 直调）。
    Q_INVOKABLE bool spawnBoat(int x, int y, int z, int boatType);

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
    //   单位意图向量（已含 W 前 / S 后 / A/D 横移）。t584 三档：先判介质（waterSurfaceY 有水 = Water 档 /
    //   无水 + 支撑面冰族 = Ice 档 / 其余 = Land 档），按档取速度上限 + lerp 接近率 + 转向速率 → 积分水平
    //   位移 + 逐轴碰撞 + 水中碰岸整停（Water 档 footprint 前向层探到实心方块 → 双轴速度清零）。
    //   outBoatPos 写新船中心位（PlayerController 据它把玩家 m_pos 同步到船座位）。outCrashed=true =
    //   高速撞硬墙（PlayerController 据 it 调 breakRiddenBoat 掉船物品 + 下船）。无骑乘（m_riderBoat<0）→
    //   no-op（outBoatPos 不变 / outCrashed=false）。
    void tickRiddenBoat(qreal dt, World *world, float wishX, float wishZ,
                        QVector3D &outBoatPos, bool &outCrashed);

    // 撞毁被骑的船（t535 高速撞墙损坏）：移除该船（releaseSlot）+ 清 m_riderBoat + emit boatWrecked（呈层掉
    //   木板 + 木棍，非完整船；机制等价 MC 1.0 船撞毁散件）。由 PlayerController 骑乘期 outCrashed=true 时调。无骑乘 → no-op。
    void breakRiddenBoat();

    // t508 玩家位置注入（pushable 用）：PlayerController.tick 每帧 tick 前调一次，写玩家 AABB 中心（脚底 +
    //   半高）。tick 内据此判玩家 ↔ 船重叠 → 把船推开（机制等价 MC 1.0 船可被玩家 / 实体推开）。无玩家时
    //   （菜单 / 暂停）传 NaN 坐标 → 不推（实现 .cpp，用 std::isfinite 判有效）。
    void setPlayerCenter(const QVector3D &center);

signals:
    void entitiesChanged();                       // spawn / 撞毁 / 浮水 / 骑乘物理触发；驱动 count/revision + QML 绑定刷新
    void boatBroken(int x, int y, int z, int boatType); // 船被「挖」（攻击 / 破坏）→ 呈层据它 spawnItem 掉**完整船物品**
    // t535 船撞坏掉木板 + 木棍（非船本身；机制等价 MC 1.0 船高速撞毁 → 掉落 3 木板 + 2 木棍，非完整船）：
    //   breakRiddenBoat（高速撞墙）发本信号 → 呈层 spawnItem 掉 Planks×3 + Stick×2。区别 boatBroken（挖 → 完整船）：
    //   MC 1.0 船被「攻击 / 挖」掉完整船物品；被「高速撞墙」撞毁则掉散件（木板 + 木棍）—— 两路径语义不同，故两信号。
    //   boatType 暂不影响掉落物（两变体撞坏都掉木板 + 木棍；预留变体差异化）。
    void boatWrecked(int x, int y, int z, int boatType);
    // t630 船撞碎荷叶（用户：船应能撞碎荷叶掉落物；机制等价 MC 1.0 船高速碾过 lily pad → 叶碎掉物品）：
    //   smashLilyPads（tick / tickRiddenBoat 速度 > 阈值时的 footprint 扫）发本信号 → 呈层 spawnItem 掉
    //   LilyPad 方块物品（同 boatBroken→spawnItem 模式，单向事件流）。坐标 = 被撞碎的睡莲格。
    void lilyPadSmashed(int x, int y, int z);

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
    //   review L10：ignoreIce=true（仅水档碰岸探测传）把冰族视作可通行（冰顶与水面同层，船应能滑上冰面；
    //   见 .cpp 实现处注释）；默认 false —— 位移 / 推船 / 支撑等实际碰撞仍把冰当实体。
    bool boatFootprintBlocked(World *world, float px, float py, float pz, bool ignoreIce = false) const;
    // t630 船 footprint 水域覆盖率（0..1）：footprint 覆盖格中「该列有水柱」的格数占比（列从支撑层
    //   probeY 向上扫 kWaterProbeDepth 格内有 Water 即算水列 —— 覆盖浅水 / 深水，取覆盖即可）。采样同
    //   boatFootprintBlocked（floor(±半宽/半长) 格扫）。t630「2/3 支撑阈值」用：覆盖率 ≥ 2/3 才判「船浮
    //   在水里」（foundWater 置真）；岸沿驶入时 1/3 仍在陆上（覆盖率 < 2/3）→ 走陆档重力贴地（不掉进
    //   水岸夹缝），机制等价 MC 船大部分船身离开岸才落水。无 world → 返 0。
    float boatFootprintWaterFraction(World *world, float px, float pz, float probeY) const;
    // 算船当前 XZ 列的水面 Y（找最顶水格 + 1 - kBoatDraft；无水 → 返当前 py 不浮）。用于浮水 lerp 目标。
    //   t508 二轮复盘：outFoundWater（可空）写真是否找到水柱 —— tick 据此判「浮水」vs「无水重力落地」
    //     （旧版用船中心格 == Water 判 hasWater，但船浮水面时中心格常是水上空气格 → 误判无水 → 重力把船拽下水，
    //     用户报②「放水上直接飞到水下」真因）。waterSurfaceY 内已扫水柱，复用其结论更准。
    float waterSurfaceY(World *world, float px, float pz, float fallbackY, bool *outFoundWater = nullptr) const;    // 船当前所在「支撑面」的方块 id（判冰面加速）：从船中心所在格向下找首个**可踩实体方块**（含船中心格本身）。
    //   t508 修：旧版固定读 floor(pos.y) − 1（船中心格的下方一格），但船中心本就浮在水面 / 冰面格内
    //   （pos.y = surfaceY + 1 − draft → floor = surface 格），「−1」跳过了船实际踩着的表面格 → 永远读到
    //   水底 / 冰下，冰面加速从未生效。改：自船中心格向下扫（含本格）首个可踩实体（isCollidable）→ 船在
    //   冰面格时直接读到 Ice / PackIce / BlueIce。船在水面（水非 collidable）→ 跳过水继续向下到水底实体。
    //   t584：介质档判定（tickRiddenBoat）先看 waterSurfaceY 的扫柱结论（有水 → Water 档，浅水船读到水底
    //   沙 / 石不误判陆档），无水才读本返回值判 Ice / Land 档。
    quint8 blockBelowBoat(World *world, const QVector3D &boatPos) const;
    // t630 撞碎荷叶：船速 > kBoatLilySmashSpeed 时扫 footprint 覆盖格（船中心层 + 下一层，匹配
    //   boatFootprintBlocked 的两层采样），命中 LilyPad → setWaterSilent 清为 Air（静默写：非玩家破块，
    //   同 EntityManager 留雪路径）+ emit lilyPadSmashed（呈层掉睡莲物品）+ 返 true（caller 标 changed）。
    //   撞碎**先于**位移碰撞判定（tick / tickRiddenBoat 调用顺序）→ 高速船碾碎叶后本帧可继续前进（叶
    //   isCollidable=true 若不清会把船挡停在叶前 =「撞不动」）；低速（< 阈值）不碎 → 叶仍挡船（绕行，
    //   机制等价 MC 慢速船被 lily pad 阻挡、高速碾碎）。无 world / 无命中 → false（零开销早退）。
    bool smashLilyPads(World *world, float px, float py, float pz);

    static constexpr int kCap = 64;            // 船数上限（防溢出；船相对稀有，cap 取 mob 量级）
    // 船几何 / 物理常量（机制等价 MC 1.0 boat；手感可玩）：
    //   t556「碰撞箱太大」（用户报③）：旧 kBoatHalfW=0.8（footprint 1.6×1.6，整格还大）+ 视觉船 1.6×1.0×0.65
    //     → 碰撞盒远超船体（X 向 1.6 宽 vs 船体仅 1.0 宽）。改「矩形碰撞盒匹配船体」：X 半宽 kBoatHalfW=0.5
    //     （footprint X 1.0，对齐船体左右舷 x∈[-0.5,0.5]）+ Z 半长 kBoatHalfLen=0.7（footprint Z 1.4，对齐船头/尾
    //     z∈[-0.7,0.7]），半高 kBoatHalfH=0.35（高度 0.7，包住船底甲板 -0.2 ~ 舷顶 +0.25，机制等价 MC 1.0 船
    //     ~1.4×0.45×1.4 碰撞箱量级）。F3+B hitbox scale 同步（2·半宽 / 2·半高 / 2·半长）。骑乘命中盒随碰撞盒
    //     变小 → 右键上船需更贴近船舷（近身即中，同 t530 前行为；船舱内宽 0.8 仍足容 0.6 宽玩家，不回归「坐不下」）。
    static constexpr float kBoatHalfW    = 0.5f;   // 船 footprint 半宽（X；footprint 宽 1.0，对齐船体左右舷）
    static constexpr float kBoatHalfLen  = 0.7f;   // 船 footprint 半长（Z；footprint 长 1.4，对齐船头 / 尾舷）
    static constexpr float kBoatHalfH    = 0.35f;  // 船命中盒半高（0.7 高，包住船舱底 -0.2 ~ 舷顶 +0.25）
    //   t508 二轮复盘修「整个悬浮在水中」（用户报⑦）：旧 kBoatDraft=0.25 → 船中心 Y = 水面顶 - 0.25（船吃水 0.25），
    //     而船舷顶（model +0.225）恰与水面平齐 → 整条船视觉沉在水面下、只露舷沿 =「悬浮在水中」。改为 0.0：
    //     船中心 Y = 水源格 cell 顶（= 水面顶），船底（model -0.2）略入水 0.2、舷顶（+0.225）出水 0.225 →
    //     船大部分浮在水面之上、仅船底没入，视觉等同 MC 船浮水。spawnBoat / waterSurfaceY / dismount 共用本常量。
    static constexpr float kBoatDraft    = 0.0f;   // 船吃水（船中心距水面下沉量；0 = 船中心贴水面顶）
    // t508 二轮复盘：船底相对船中心的下沉量（视觉船底甲板底面 ≈ 中心 - 0.2）。无水重力落地稳态 Y =
    //   支撑方块顶 + kBoatHullBottom（船底贴支撑顶）。与 boats delegate 船底甲板 Model（position.y=-0.15、
    //   scale.y=0.1 → 底面 -0.2）对齐 —— 改几何时同步改本常量（两处一致才不沉地 / 不悬空）。
    static constexpr float kBoatHullBottom = 0.2f;
    //   kBoatSpeed：水中基础船速（blocks/s；明显快于游泳 kSwimUp=4.5 / 走 kWalk=4.3，机制等价 MC 船水上快）。
    //   kBoatAccel：水档船速向目标 lerp 接近率（1/s；中等动量）。
    //   kBoatFriction：空船 / 松键水平摩擦衰减率（1/s）。
    //   kBoatCrashSpeed：撞墙损坏速度阈值（blocks/s；高速撞硬墙才坏，低速轻撞只停）。
    //   kBoatTurnRate：船头转向速率（度/s；船头平滑转向意图方向，机制等价 MC 船缓转）。
    static constexpr float kBoatSpeed    = 8.0f;
    static constexpr float kBoatAccel    = 4.0f;
    // t556「船太轻」（用户报②）：空船水平摩擦衰减率 3.0 → 5.0（被推后更快停住，配合 kBoatPushImpulse 0.08
    //   → 推船滑行 <0.01 格、肉眼不动，机制对齐 MC「船重得像浸水木头」）。
    static constexpr float kBoatFriction = 5.0f;
    // t584 三档速度：水档满速 8 < 旧阈值 7 会「水档常速碰岸即毁」不对 → 提到 12（水档碰岸只停、冰档
    //   16~21.6 撞墙仍毁 = 冰上危险）。
    static constexpr float kBoatCrashSpeed = 12.0f;
    static constexpr float kBoatTurnRate = 360.0f;
    // t584 三档介质参数（陆地 / 冰面；机制等价 MC 1.0 船：陆地几乎开不动 / 冰面最快 + 惯性大难操作）：
    //   陆档：kBoatSpeed × kBoatLandSpeedMul（2.4 blocks/s，最慢 —— 比走路还慢但非零：用户要求「直接放
    //     陆地上开不会停」= 陆地能开只是慢）；kBoatLandAccel 接近率高（松手即停无滑行，贴地挪动手感）。
    //   冰档：速度倍率按冰类型 2.0 / 2.4 / 2.7（t611 微升自 1.8/2.2/2.5：16~21.6 blocks/s ≈ 水面 2~2.7
    //     倍，机制等价 MC 冰面船速 ~ 水面 2.5 倍）；接近率读 BlockRegistry::iceSlipApproach（单一权威，
    //     越小越滑 → 松键长滑行 = 冰面惯性）；转向速率 × kBoatIceTurnMul（迟钝 = 难操作才是对的，用户原话）。
    static constexpr float kBoatLandSpeedMul = 0.3f;
    static constexpr float kBoatLandAccel    = 10.0f;
    static constexpr float kBoatIceTurnMul   = 0.45f;
    // t611 碰岸方向探测步长（blocks）：tickRiddenBoat 水档碰岸检测对 ±X / ±Z 四向分别前探本距离后查
    //   footprint 是否被挡 → 只清朝岸方向的速度分量（背向分量保留 → 撞岸后可倒退）。取 0.15：贴岸时
    //   朝岸侧必命中、背岸侧（水）不命中；过大会把「还离岸半格」误判撞上（提前锁速），过小贴岸瞬间
    //   探不到（船头已越线）→ 0.15 ≈ 一帧满速位移（8×0.016）+ 裕度，手感与 t584 旧「贴岸即停」等价。
    static constexpr float kShoreProbe = 0.15f;
    // t508 玩家推船给的水平速度冲量（blocks/s；每次接触分离时叠入 vx/vz）。机制等价 MC 1.0 船被实体撞开
    //   后会滑一小段。
    //   t531「船太轻（身体撞就明显动）」复盘：旧值 2.0 → 玩家碰一下船就给 2 blocks/s 初速，肉眼明显被弹开 + 滑行
    //     （机制不等价 MC —— MC 1.0 船被实体撞只会被「推开」一小段、几乎不动，船重得像浸水木头）。改为 0.3：
    //     接触分离仍把船推出重叠区（防 AABB 穿叠），但给的水平冲量极小（0.3 blocks/s）→ 摩擦（kBoatFriction=3）
    //     ~0.1s 即停 → 玩家撞船后船几乎不动（仅接触分离的瞬时位移、无明显滑行），机制对齐 MC「撞船几乎推不动」。
    //     注：接触分离本身（push 到接触距离外）仍保留（防船被玩家挤进墙里），只是不再给冲量 → 视觉上船不会被
    //     「打飞」只会被「挤开半步」，符合「船重」直觉。
    //   t556「船还太轻（随便推就走，还能推上岸）」（用户报②）：0.3 仍轻 —— 玩家持续顶着船走（按住 W 贴船）时
    //     接触分离每帧把船「挤出重叠区」= 每帧推一点点，累积起来肉眼「推着走 / 推上岸」。再降冲量到 0.08 +
    //     kBoatFriction 提到 5.0（空船水平摩擦衰减更快）→ 单次接触只给 ~0.08 blocks/s 冲量、摩擦 0.2s 内停
    //     （滑行 <0.01 格，肉眼不动）；持续顶推虽仍每帧挤开一点，但速率大幅下降 → 推船「像推浸水木头」。
    static constexpr float kBoatPushImpulse = 0.08f;
    // t508 二轮复盘修「陆地悬空 + 卡住沉底」（用户报③⑥）：船在无水格（陆地 / 冰面）应有重力 —— 旧 tick 浮水
    //   段无水时 waterSurfaceY 返 fallback（= 当前 pos.y）→ dy=0，船 Y 永不动 → 放陆地悬在放置点（pos.y = 放置
    //   格顶 + 0.75，悬空半格），且骑乘下船摆位算错。加常速重力让陆地船落到支撑面（boatFootprintBlocked 挡实块
    //   即停），与 MC 船放陆地会落地一致。取值 8.0（blocks/s²，明显但不过猛，落半格 ~0.3s 即贴地）。
    static constexpr float kBoatGravity = 8.0f;
    // review L12 放船点最小间距（blocks）：spawnBoat 拒与既有活体船中心距 < 本值的落点（防两船同格叠加；
    //   取 1.4 = 船身全长，两船中心至少隔一船身不嵌位）。见 spawnBoat 实现处注释。
    static constexpr float kBoatMinSpawnDist = 1.4f;
    // t630 水域覆盖列向上探测深度（格）：boatFootprintWaterFraction 自支撑层参考格向上扫本深度内有 Water
    //   即算水列（覆盖浅 1 格水到深水；岸边水底常在水面下 ≥2 格 → 3 格够）。
    static constexpr int kWaterProbeDepth = 3;
    // t630「2/3 支撑阈值」（用户：船身 2/3 过去了再掉，1/3 还在岸上时不掉不卡）：footprint 水域覆盖率
    //   ≥ 本值才判「浮在水里」（foundWater 置真 → Y 钉水面 / 水档推进）；< 本值（≥1/3 船身仍在岸上）→ 走
    //   无水陆档（重力贴支撑面）。旧版 waterSurfaceY 只看**中心列**（中心格一入水岸沿即判有水 → Y 钉水面
    //   把仍压岸的半船拽下沉 → 与岸块嵌入互卡 =「一半在水一半卡方块」根因）。0.67 ≈ 4/6 格（footprint 6 格）。
    static constexpr float kBoatWaterFraction = 0.67f;
    // t630 船撞碎荷叶速度阈值（blocks/s）：船速 > 本值时碾过 LilyPad → 叶碎掉物品（机制等价 MC 1.0 船
    //   高速撞碎 lily pad）；低于阈值叶挡船（绕行）。取 3.0（水档满速 8 的 ~1/3：轻推不碎、正常行驶碾碎，
    //   陆档 2.4 顶速 < 3.0 → 陆上推船不误碎岸边叶）。
    static constexpr float kBoatLilySmashSpeed = 3.0f;
    // t630 身体推船旋转参数（用户：人撞船应有旋转效果）：玩家偏侧推船 → 力臂叉积产生 yawRate（度/s）。
    //   kBoatPushTurnRate：叉积（力臂[格] × 推开量[格]）→ 度/s 的转换系数。接触分离量本身很小（每帧挤出
    //   重叠区 ~0.01-0.05 格），系数取 1200 使偏侧推持续顶船时 yawRate 到可观值（~5-15 度/s 慢偏转，
    //   「撞船船头被别转」手感，非甩头）；对心推（力臂≈0）叉积≈0 → 纯平移不转（力矩物理直觉）。
    //   kBoatPushTurnMax：yawRate 钳制上限（度/s）防深穿叠瞬间（如出生重叠 1.4 内）大叉积甩头。
    static constexpr float kBoatPushTurnRate = 1200.0f;
    static constexpr float kBoatPushTurnMax  = 25.0f;
};

#endif // BOATMANAGER_H
