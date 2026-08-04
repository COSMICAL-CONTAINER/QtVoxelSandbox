#ifndef ENTITYMANAGER_H
#define ENTITYMANAGER_H

#include <QObject>
#include <QString>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <vector>

// 统一实体管理器（t95；Entities 层）。t239 扩展为「生物基类（AI/物理/血量/受击/死亡）」。
//
// 每个 Mob 实体 = {世界坐标 pos, 半径 radius, 可推动标志 pushable, 渲染外观（kind/color/mobType/blockId）,
// 物理态（vy/resting）, **AI 态（yawRad/wanderTimer/wanderSpeed/moveSpeed）, 生命态（maxHealth/health/
// dead/hurtFlash/deathTimer）**}。为猪/牛/羊（t240）+ 后续 mob 统一基类（spec「为猪牛羊 + 后续 mob 统一基」）。
// 持有一类测试生物（pushable=true，玩家走碰可推动，swept 碰撞解析玩家位移传给实体）+ t117 沙子重力方块
// （pushable=false，下落到着地转 setBlock + 移除）。掉落物（src/Game 的 ItemEntityManager）机制等价
// 「pushable=false、被拾取」的实体变体——本轮不迁移既有的掉落系统（已深度集成 t35-t64：拾取 / 丢弃 /
// 重力 / 数量 / 免拾取窗），仅在此确立统一基类形态，为后续把掉落物并入统一 EntityManager 留形。
//
// 物理：
//   - 重力 + 地面静止（tick(dt, world)）：未 resting 的实体 vy -= g*dt（钳 -kMaxFall），按 dy 下移并
//     扫实体所在列首个实体方块 → 落到其顶面停下（resting=true，pos.y = solidCellY + 1 + halfH）。
//     resting 实体复探支撑格，失支撑则续落（防挖空悬空）。机制与 ItemEntityManager::tick 同源（向下
//     只读 World::isSolid，PLAN §2 合规）。
//   - 玩家推动（resolvePlayerPush）：对每个 pushable 实体，用「玩家 AABB（XZ 矩形）vs 实体圆（XZ）」
//     求穿透，把实体沿「AABB 最近点 → 实体中心」方向推出穿透量（玩家位移传给实体）。仅在实体与玩家
//     AABB 垂直区间重叠时推动（防跨层误推——玩家从实体头顶跳过不应推开它）。推动后做世界碰撞钳制：
//     扫 mob AABB footprint 覆盖的所有格子（仿 player aabbHitsSolid；非旧版「只查中心格」），任一实体
//     方块 → 撤回该轴推动（防穿墙 + 消除斜推角落 jitter——旧版单格检查致 mob 入墙反复跳变）。
//
// t239 生物基类（AI / 物理 / 血量 / 受击 / 死亡）+ t241 行走动画 + 羊吃草：
//   - **AI wander 自主移动**（tick 内 aiWander）：时间片倒计时 wanderTimer；到 0 随机选新朝向 yawRad +
//     随机决定 idle（speed=0，~25%）/ 行走（speed=kWalkSpeed），重置 timer∈[kWanderMin, kWanderMax]。
//     行走时按 yaw 算水平位移（与 player 同 yaw 约定：dir = (-sin, 0, -cos) → QML eulerRotation.y = yawDeg
//     使模型 -Z 前）正对行走方向），逐轴（X 后 Z）做世界边界 clamp + 方块碰撞撤回（复用 mobAabbHitsSolid）。
//     撞墙（两轴都未动）→ 缩短 timer 下帧大概率换向离开墙角。机制等价 MC passive mob 的「游荡 + 停驻」
//     循环（随机选向 + 时间片），非确定性（生物 AI 非世界生成，不涉 §2-K）。
//   - **t241 行走动画相位 walkPhase**：tick 内 moveSpeed>0（行走）时推进 walkPhase（fmod 2π）；idle / 吃草 /
//     死亡 → 冻结（腿停于上次相位）。walkPhaseAt(i) 供 QML 驱动 MobModel 腿摆（每 active 帧 bump revision 让
//     绑定刷新；idle 时 EntityManager 返回同一 float → MobModel.setWalkPhase 早退不重建）。
//   - **t241 羊吃草 AI**（仅 mobType==MobSheep）：idle 且扫描冷却到 → 检测前方一格草丛（TallGrass），找到则
//     进入吃草周期（eatTimer=kEatDuration，期间强制 idle 站立 + 头部俯仰动画）。周期推进到 apply 阈值时
//     消耗草丛：草丛→空气 + 其下草方块→泥土（机制等价 MC 羊吃草：草丛消失、下方草地变泥土）。写入走
//     World::setWaterSilent（通用静默 state 写入口；非玩家破块 → 不发 broken/placed，免粒子 / 音 / 掉落噪音，
//     同水流蔓延 / 作物生长模式）。headPitchAt(i) 据吃草进度返 sin(πp) 包络（负值=低头），供 QML 驱动羊头俯仰。
//   - **血量 / 受击 / 死亡态**：maxHealth/health（默认 10 = MC 1.0 猪/牛/羊 5 心）；damageEntity(i, amount)
//     扣血 + 设 hurtFlash=kHurtFlashTime（QML 据红闪显红）；health≤0 → dead=true + deathTimer=kDeathTime +
//     emit mobDied（坐标 + mobType，t242 据它掉落猪排/皮革/羊毛）。dead 期间冻结 AI / 重力（仅 deathTimer
//     倒计时），到 0 移除（给 QML 播死亡动画窗口）。resolvePlayerPush 跳过 dead mob（尸体不被推）。
//
// 分层（PLAN §2）：本层属 Entities（位于 Game/Physics 之下、World 之上）。向下读写 World（读 isSolid/
// blockAt；t241 羊吃草写 setWaterSilent —— 系统模拟静默写栅格，仍只向下依赖 World，不反向依赖 Renderer/
// Physics/QtQuick3D）。tick / resolvePlayerPush / damageEntity 由 PlayerController（Game/Physics 层）每帧 /
// 攻击时调（C++ 直调，非 Q_INVOKABLE 优先——避开 moc 对 World* 前向类型的 metatype 处理，同
// ItemEntityManager 先例；damageEntity 兼 Q_INVOKABLE 供调试 / t242 攻击路径双入口）。呈现层（Main.qml
// 的 Repeater）只读 count/posAt/colorAt/yawAt/walkPhaseAt/headPitchAt/healthAt/...，自发渲染，绝不反向写
// （PLAN §2 分层：呈现层只消费 Entities 数据，同 blockBroken→粒子 / spawnItem→掉落物 模式）。
class World; // 前向声明（tick / resolvePlayerPush / aiWander 只读 World::isSolid/blockAt；完整定义在 .cpp include）
class EntityManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EntityManager)
    // count：当前实体数（Repeater 作 int model → 生成 0..count-1 delegate）。NOTIFY entitiesChanged
    // 驱动 spawn 后 Repeater 追加新 delegate（不重建已有 → 后续 Mob/AI 动画连续不被打断）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：实体集版本号（随 spawn / 推动位移 / 重力下落 / AI 行走 / 受击红闪 / 死亡移除 自增）。供
    // 「触碰」绑定作 NOTIFY 触发器（同 Hotbar.slotRevision / ItemEntityManager.revision 模式）——
    // posAt/colorAt/yawAt/healthAt 是 Q_INVOKABLE 不被 NOTIFY 自动跟踪，需 { revision; posAt(i) } 显式建依赖，
    // push/下落/AI 移动/红闪/死亡后绑定才重算。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit EntityManager(QObject *parent = nullptr);

    int count() const { return int(m_entities.size()); }
    int revision() const { return m_revision; }
    // t256：当前**活体**实体数（不含已释放的空槽）。F3 draw-call 估算用它（空槽 delegate 已 visible=false
    //   不参与绘制，count 会高估）。spawn 上限判定（kCap）也读它（空槽可复用，不算满）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // t256：第 i 个槽位是否活体（= 已分配未释放）。呈现层 delegate 据它 visible：空槽 → 隐藏整棵 delegate
    //   （slot 复用保 Repeater count 单调不降、delegate 永不销毁，空槽仅隐藏不重建）。越界 → false。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 实体外观种类（Q_ENUM 供 QML 渲染分流：Mob=纯色立方 / Item=掉落物（vestigial，实际由 ItemEntityManager
    // 管）/ FallingBlock=贴图方块）。
    enum Kind { Mob, Item, FallingBlock };
    Q_ENUM(Kind)

    // t240 mob 子类 id（与 Entity.mobType 同值；Q_ENUM 供 QML 据 mobTypeAt 选 MobModel 比例 + 贴图）。
    //   MobTest=0 通用测试生物（t239，M 键生成；QML 仍走 UnitCube 旧路径，不进 MobModel）；
    //   MobPig/MobCow/MobSheep = 猪/牛/羊（t240 各自方块化原创 3D 模型 + 各自贴图，Main.qml 走 MobModel）。
    //   t242 据本 enum 选死亡掉落（猪:生猪排 / 牛:皮革+牛肉 / 羊:羊毛）；t243 spawn egg 据本 enum 选生成类型。
    //   机制等价 MC 1.0 passive mob 三种（猪/牛/羊），名称 / 模型 / 贴图全原创（PLAN §9 区隔，不照搬 MC 美术）。
    //   t280 黑暗刷怪：MobShambler=4 / MobBones=5 = 敌对生物（机制等价 MC 1.0 僵尸 / 骷髅；PLAN §9 区隔改名：
    //     Zombie→Shambler「蹒跚者」、Skeleton→Bones「骸骨」—— 名称 / 模型 / 贴图全原创，仅机制对齐「黑暗刷怪 /
    //     白天燃烧」）。Entity.hostile=true → 走 tickHostileLife 的燃烧 + 黑暗刷怪调度（白天暴露日光 → 着火扣血 →
    //     消失；夜间 / 洞穴低光处由 PlayerController.updateMobSpawning 周期 spawn）。QML 据 mobTypeAt 走对应贴图 /
    //     颜色（Shambler 暗绿、Bones 灰白 UnitCube，机制等价，非照搬 MC 美术）。
    enum MobType { MobTest = 0, MobPig = 1, MobCow = 2, MobSheep = 3, MobShambler = 4, MobBones = 5 };
    Q_ENUM(MobType)

    // 生成默认测试生物（mobType=0、#ff5555、满血 kDefaultMaxHealth）。t239 调试入口（M 键）；t243 spawn eggs
    //   落地后由 spawnMobTyped 直接生成猪/牛/羊。位置存该格中心 (x+0.5, y+0.5, z+0.5)；从高处生成时由重力
    //   tick 落到地表。radius=0.5、pushable=true。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnMob(int x, int y, int z);
    // t239 生物基类统一生成入口（猪/牛/羊 + 后续 mob）：mobType=子类 id（0=通用测试生物；t240 pig/cow/sheep
    //   各自 id；掉落/模型据它分流）；color=渲染配色（QML delegate baseColor）；maxHealth=血量上限（≤0 用默认）。
    //   生成即满血、未死、AI wanderTimer=0（tick 首帧即选第一次向）。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnMobTyped(int x, int y, int z, int mobType, const QString &color, int maxHealth);
    // t280 黑暗刷怪：敌对生物生成入口（Shambler/Bones）。委托 spawnMobTyped（设 hostile=true / 配色 / 血量）
    //   后再翻 Entity.hostile（spawnMobTyped 是通用入口，不知哪些 mobType 是敌对；本入口收口敌对语义）。
    //   达 kCap → 委托内静默跳过。mobType 仅 MobShambler/MobBones 合法（其余当敌对调是非语义，但仍生成不崩）。
    Q_INVOKABLE void spawnHostileMob(int x, int y, int z, int mobType);
    // t280 当前**活体**敌对生物数（hostile=true 且非 dead 的 Mob）。供刷怪调度判总数上限（kHostileMobCap）。
    //   含 Shambler/Bones；不含 passive（pig/cow/sheep/test）与 FallingBlock/Item。
    Q_INVOKABLE int hostileCount() const;
    // t280 第 i 个实体是否**敌对**（hostile=true 的活体 Mob）。QML 据它对 Shambler/Bones 显燃烧火焰 Model
    //   （passive 永不燃烧 → 火焰仅敌对会显）。越界 / 非 hostile → false。
    Q_INVOKABLE bool isHostileAt(int i) const;
    // t280 第 i 个敌对生物是否**正在燃烧**（暴露在日光下、skyBrightness 超过燃烧门）。tickHostileLife 每 tick
    //   重算并写入 Entity.burning 缓存；QML 据 isBurningAt 显火焰动画 + baseColor 偏橙。越界 / 非敌对 → false。
    Q_INVOKABLE bool isBurningAt(int i) const;
    // t117 沙子重力方块：在方块格 (x,y,z) 生成一个下落方块实体（携带 blockId）。位置存该格中心
    // (x+0.5, y+0.5, z+0.5)；pushable=false（不被玩家推动，同掉落物变体）；kind=FallingBlock；
    // blockId 存实体携带的方块 id（着地放置用它）。重力 tick 下落，着地时 world->setBlockFromEntity
    // 放置 blockId 并移除自身。链式塌落由调用方先把沙格置 air（经 World::setBlock → blockBroken →
    // 呈现层 onBlockBroken 递归触发上方沙）实现。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnFallingBlock(int x, int y, int z, int blockId);
    // t176 存档：清空所有实体（切世界 / 退出存档前调，防上一世界的 mob / 下落方块残留进新世界）。
    //   emit entitiesChanged → count=0 → QML Repeater 清空 delegate。t256：同步清空槽位 free list +
    //   live 计数（slot 复用模型见 acquireSlot/releaseSlot）。
    Q_INVOKABLE void clearAll() { m_entities.clear(); m_freeSlots.clear(); m_liveCount = 0; emit entitiesChanged(); }

    // 第 i 个实体的渲染数据（呈现层 Repeater delegate 绑它摆位 + 配色）。越界返回安全默认。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // XZ 碰撞半宽（旧名 radius；t252 拆分 XZ/Y：halfW=XZ 圆碰撞半径 + footprint 格扫 X/Z 范围）。
    //   pig/cow/sheep 0.45（0.9 宽）、MobTest/FallingBlock 0.5（1×1×1）。越界 → 0。
    Q_INVOKABLE float radiusAt(int i) const;
    // t252 Y 碰撞半高（F3+B hitbox 高度读；QML WireCube scale.y = 2*halfHeightAt）。
    //   pig/sheep 0.45（0.9 高）、cow 0.70（1.4 高，机制等价 MC 1.0 牛）、MobTest/FallingBlock 0.5。越界 → 0。
    Q_INVOKABLE float halfHeightAt(int i) const;
    Q_INVOKABLE bool pushableAt(int i) const;
    Q_INVOKABLE int kindAt(int i) const;
    Q_INVOKABLE QString colorAt(int i) const;
    // t117：第 i 个实体携带的方块 id（FallingBlock 着地 setBlock 用；呈现层据它设 BlockCube.blockId
    // 贴图渲染）。非 FallingBlock 实体返回 0。越界返回 0。
    Q_INVOKABLE int blockIdAt(int i) const;
    // t239 mob 朝向 / 行走（呈现层 / t241 腿摆动画读）：
    //   yawAt = 朝向度数（QML delegate Node eulerRotation.y → 模型 -Z 正对行走方向；与 player.yaw 同约定）。
    //   moveSpeedAt = 当前水平速度（blocks/s；行走非零、idle/撞墙/死亡=0；t241 据它驱动腿摆频率）。
    //   越 mob（非 Mob kind）/ 越界 → 安全默认（yaw 0 / speed 0）。
    Q_INVOKABLE float yawAt(int i) const;
    Q_INVOKABLE float moveSpeedAt(int i) const;
    // t241 行走动画相位（弧度；行走时每帧推进，idle/吃草/死亡冻结）。QML 据 it 驱动 MobModel 腿摆
    //   （绑 `walkPhase: {revision; walkPhaseAt(i)}`）。非 Mob / 越界 → 0。
    Q_INVOKABLE float walkPhaseAt(int i) const;
    // t241 羊头部俯仰（弧度，负=低头吃草）：仅 mobType==MobSheep 且处于吃草周期时返 sin(πp) 包络（中段
    //   最深、起末归零）；其余 mob / 非吃草态 → 0（MobModel 头走轴对齐快路径不旋转）。QML 据 it 驱动
    //   MobModel 头俯仰（绑 `headPitch: {revision; headPitchAt(i)}`）。
    Q_INVOKABLE float headPitchAt(int i) const;
    // t239 mob 子类 id（t240 pig/cow/sheep；t242 据它选掉落物、t243 spawn egg 据 it 选生成类型）。越界→0。
    Q_INVOKABLE int mobTypeAt(int i) const;
    // t239 mob 血量 / 受击 / 死亡态（呈现层心条 / 红闪 / 死亡动画；t242 攻击 HUD 读）：
    //   healthAt / maxHealthAt = 当前 / 上限血量（供心条 / 攻击反馈）；deadAt = 死亡态（QML 播死亡动画）；
    //   hurtFlashAt = 受击红闪剩余比 0..1（>0 → QML baseColor 红，机制等价 MC mob 受击 10 tick 红闪）。
    //   非 Mob / 越界 → 安全默认。
    Q_INVOKABLE int healthAt(int i) const;
    Q_INVOKABLE int maxHealthAt(int i) const;
    Q_INVOKABLE bool deadAt(int i) const;
    Q_INVOKABLE float hurtFlashAt(int i) const;

    // t239 受击（Q_INVOKABLE 兼调试 + t242 攻击路径双入口）：第 i 个 mob 受 amount 伤害。clamp health 到
    //   [0, maxHealth]；hurtFlash = kHurtFlashTime（QML 红闪）。health≤0 且未 dead → dead=true + deathTimer=
    //   kDeathTime + emit mobDied（t242 据它掉落）。dead / 非 Mob / 越界 / amount≤0 → 静默早退。bump revision。
    Q_INVOKABLE void damageEntity(int i, int amount);
    // t242 攻击射线 vs mob AABB 命中测试（C++ 直调；PlayerController::beginMining 左键攻击路径调）。
    //   返回沿射线 (origin, dir) maxDist 内**最近**的活体 mob 索引；无命中 → -1。
    //   outDist（若非 null）写入命中距离（起点到 AABB 表面欧氏距离）。
    //   命中判定：slab-based ray-AABB（mob AABB = pos ± radius 的 1×1×1 立方；dir 须归一）。
    //   跳过：非 Mob（掉落物 / 下落方块）、dead（尸体不可再打，防鞭尸重复扣血 / 触发多次掉落）。
    //   分层（PLAN §2）：EntityManager 自持实体数据，做几何测试最自然；只读自身数据、无向下依赖。
    // t253 攻击单体选中：findMobHit 返回**单个** mob 索引（nearest-along-ray，非 AoE 全打）；丢弃返回值
    //   = 单体选中结果未用 = bug（调了选体却不据此攻击 / 高亮）。[[nodiscard]] 在编译期强约束（同项目
    //   lessons-learned 的 [[nodiscard]] fallible-call 纪律：检查 + 用返回值，不 (void) 吞）。
    [[nodiscard]] int findMobHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist = nullptr) const;
    // t249 受击击退（spec「受击往攻击方向小跳击退」；C++ 直调，PlayerController::attackMob 命中后调）：
    //   给第 i 个 mob 一个水平方向 (dirX,dirZ) 的击退冲量（vx/vz=kKnockbackHoriz 沿方向）+ 小跳垂直速度
    //   （vy=kKnockbackUp 向上）；解除 resting 让 tick 重力分支处理上跳→减速→下落→着地（小弹起观感）。
    //   方向 (dirX,dirZ) 由 caller 传「玩家→mob」水平向量（knockback 内再归一 + 零向量防御）。机制等价
    //   MC 1.0 knockback：受击实体沿攻击方向被推开 + 小幅上弹（不旋转、无受击硬直打断 AI，仅速度叠加）。
    //   非 Mob / dead（尸体不被推，同 resolvePlayerPush）/ 越界 → 静默早退。bump revision → QML 位置绑定刷新。
    //   分层（PLAN §2）：与 damageEntity 同层（Entities），只改自身数据，无向下依赖；由 Game/Physics 层调。
    void knockback(int i, float dirX, float dirZ);

    // 玩家推动解析（C++ 直调；PlayerController::tick 每帧调，captured 时）。
    //   playerFeet=玩家脚底中心，halfW=玩家 AABB 半宽，height=玩家 AABB 高，world=只读世界（钳制穿墙用）。
    //   对每个 pushable 实体：垂直区间与玩家 AABB 重叠时，按「AABB(XZ) vs 实体圆(XZ)」求穿透，把实体
    //   沿 (实体中心 − AABB 最近点) 方向推出穿透量；实体陷入实体方块时撤回该轴推动（防穿墙）。
    //   任一实体 pos 真变 → dirty=true，末尾统一 bump revision + emit（驱动 QML 位置绑定重算）。
    //   t239：dead mob 跳过（尸体不被推）。
    void resolvePlayerPush(const QVector3D &playerFeet, float halfW, float height, World *world);

    // 重力 + AI wander + 地面静止（C++ 直调；PlayerController::tick 每帧调，独立于捕获态——菜单/暂停时
    //   实体仍模拟）。机制同 ItemEntityManager::tick（向下只读 World::isSolid/blockAt）。world=null / 无实体
    //   → 早 return。Mob：AI 行走（aiWander）+ 重力；dead Mob：仅 deathTimer 倒计时（冻结）；FallingBlock：
    //   t117/t220 着地放置 / 变掉落物 + 移除。
    //   t250 环境音：listener = 玩家脚底位置（听者），用于 proximity 门控 mob idle/step 叫声 —— 仅听者
    //   kAudioRange 半径内的活体 mob 才 emit mobAmbient/mobStep（远场静默，防多 mob 同步吵闹）。PlayerController
    //   传 m_pos（菜单态仍有效）。listener 无关物理 / AI，仅参与音频门控（不写入实体态）。
    void tick(qreal dt, World *world, const QVector3D &listener);
    // t280 黑暗刷怪调度 + 敌对生物日光燃烧（C++ 直调；PlayerController::tickImpl 每 tick 调，与 tick 同级）。
    //   独立于玩家捕获态（菜单 / 暂停时仍推进 —— 夜晚照样刷怪、白天照样燃烧，世界模拟连续）。机制等价 MC 1.0
    //   「黑暗刷怪 + 白天燃烧」：周期 spawn（light<7 + 距玩家>24 + 总数上限）+ 敌对暴露日光 → 扣血 → 死亡消失。
    //   三个职责（独立模块、单一方法收口敌对生命周期）：
    //     (a) **燃烧**：每个活体 hostile mob，所在格 skyLightAt>=15（直接见天，无遮挡）且 skyBrightness>门槛 →
    //         标 burning=true（QML 显火焰）+ 累加 burnTimer，每 kBurnDamageInterval 秒扣 1HP（复用 damageEntity →
    //         hurtFlash 红闪 + health 归零 mobDied 死亡链 → 死亡动画后 releaseSlot 自然消失 = spec「燃烧消失」）。
    //         夜间 / 洞穴 / 树荫下不燃烧（机制等价 MC 1.0 僵尸 / 骷髅日光燃烧）。
    //     (b) **远距消失**：敌对生物距玩家 > kFarDespawn（且不在 burning）→ 直接 releaseSlot 移除（防世界塞满
    //         远处 mob；机制等价 MC 即时消失半径）。
    //     (c) **spawn 调度**：内部 m_spawnAccum 节流（kSpawnInterval 秒一次），满 → 若 hostileCount<kHostileMobCap，
    //         在玩家周围 [kSpawnMinDist, kSpawnMaxDist] 环内做 kSpawnAttempts 次随机选点（地表 or 洞穴），
    //         各点查「air 格 + 下方 solid + 有效光 < kSpawnLightThreshold」三条件，首个合格点 spawn 一个敌对
    //         （Shambler/Bones 等概率）。有效光 = max(skyLightAt * skyBrightness, blockLightAt)（0..15）；夜间地表
    //         天光乘子→0、洞穴 skyLightAt=0 → 二者均<阈值可刷；白天地表 skyBrightness≈1 + 见天 skyLightAt=15 →
    //         有效光 15 远超阈值不刷（机制等价 MC「夜晚地表 + 洞穴均可刷、白天仅暗洞穴」）。
    //   分层（PLAN §2）：Entities 层（同 tick）只读 World（blockAt/isSolid/skyLightAt/blockLightAt/heightAt/width/
    //   depth/height）+ 自身实体数据；写 EntityManager 自身（spawn / releaseSlot / damageEntity）。skyBrightness /
    //   playerPos 由 Game 层（PlayerController）传入 —— Game 层读 WorldClock.skyLight（Game→World 向下）+ m_pos。
    //   无向上依赖。world==null / 无实体 → 早 return。
    void tickHostileLife(qreal dt, World *world, const QVector3D &playerPos, float skyBrightness);

signals:
    void entitiesChanged(); // spawn / 推动位移 / 重力下落 / AI 行走 / 受击红闪 / 死亡移除 触发；驱动 count/revision + QML 绑定刷新
    // t250 mob 环境 idle 叫声（牛叫/羊叫/猪叫）：tick 内 ambientTimer 周期倒计时（随机 8-16s）到 + 玩家
    //   听者范围内 → emit mobAmbient(mobType)。mobType = 子类 id（0=通用 / 1=猪 / 2=牛 / 3=羊）→ 呈现层
    //   （Main.qml）Connections 路由到 AudioManager.playMobAmbient 据 mobType 选 mob_idle clip（机制等价
    //   MC 1.0 被动生物偶发 idle call；§9 原创）。分层（PLAN §2）：Entities 层发语义事件，呈现层只消费。
    void mobAmbient(int mobType);
    // t250 mob 走路声：tick 内 walkPhase 每累积半步（π=一次脚落）+ 听者范围内 → emit mobStep(mobType,
    //   blockId=脚下方块 id)。mobType 当前保留语义对齐；blockId 供 AudioManager 按材质组选 step clip。
    //   呈现层 Connections 路由到 AudioManager.playMobStep（机制等价 MC 生物走路脚步声；§9 原创）。
    void mobStep(int mobType, int blockId);
    // t117/t220 FallingBlock 遇不完整方块失撑 → 变掉落物。沙下落途中首个「非 air/水」方块为**不完整方块**
    //   （火把 / 半砖 / 栅栏 / ...，即非完整立方）时发本信号：坐标 = 不完整方块**上方一格**（= 沙应掉落位）、
    //   blockId = 实体携带的方块 id（机制等价 MC「沙落火把上 → 沙碎成掉落物」；仅完整立方可支撑沙）。
    //   呈现层（Main.qml）Connections 转发到 ItemEntityManager.spawnItem 生成掉落实体（同
    //   PlayerController.spawnItem 模式；分层：Entities 层发语义事件，呈现层只消费，绝不反向写栅格）。
    void fallingBlockDropped(int x, int y, int z, int blockId);
    // t239 mob 死亡一次性事件（damageEntity 把 health 首次扣到 ≤0 时发）。坐标 = 死亡格 floor(pos)，
    //   mobType = 子类 id（0=通用 / t240 pig/cow/sheep）。t242 据它 + mobType 决定掉落物（猪:生猪排 /
    //   牛:皮革+牛肉 / 羊:羊毛）→ 呈现层转发 ItemEntityManager.spawnItem（同 fallingBlockDropped 模式）。
    //   分层（PLAN §2）：Entities 层发语义事件，呈现层只消费，绝不反向写栅格。
    void mobDied(int x, int y, int z, int mobType);

private:
    struct Entity {
        // t256：槽位占用标志（slot-reuse 模型）。true = 已分配的活体实体；false = 已释放的空槽（在
        //   m_freeSlots 中待复用）。与 mob 的 dead（死亡动画期）正交：濒死 mob 仍 alive=true（占槽播
        //   死亡动画），deathTimer 到才 releaseSlot → alive=false（空槽，delegate 隐藏）。呈现层 delegate
        //   绑 visible:aliveAt(index) → 空槽隐藏；C++ 各遍历（tick/findMobHit/resolvePlayerPush）跳过空槽。
        bool alive = true;
        QVector3D pos;
        // t252 碰撞箱缩小：XZ 半宽（halfW）与 Y 半高（halfH）分离（旧版单一 radius=0.5 致所有 mob
        //   碰撞感「整立方大」1×1×1）。按 mobType 设：MobTest 0.5/0.5（保 t95 旧路径）；pig/sheep
        //   0.45/0.45（0.9×0.9）；cow 0.45/0.70（0.9×1.4，机制等价 MC 1.0 牛）。FallingBlock 0.5/0.5
        //   （1×1×1 立方，同地形方块外观）。findMobHit / resolvePlayerPush / mobAabbHitsSolid / aiWander /
        //   resting 均读它们（XZ 用 halfW、Y 用 halfH），不再共用单一 radius。
        float halfW = 0.5f;      // XZ 碰撞半宽（圆碰撞半径 + footprint 格扫 X/Z 范围）
        float halfH = 0.5f;      // Y 碰撞半高（垂直区间 + footprint 格扫 Y 范围 + resting 贴地偏移）
        bool pushable = true;    // 玩家是否可推动（掉落物变体 pushable=false，统一基类预留）
        int kind = Mob;          // 渲染分流（Mob/Item/FallingBlock；Q_ENUM）
        int blockId = 0;         // t117 FallingBlock 携带的方块 id（着地 setBlock 用；其余 kind=0）
        QString color = QStringLiteral("#ff5555"); // 渲染配色（醒目纯色）
        float vy = 0.0f;         // 垂直速度（blocks/s；向下为负）；落地后归 0；t249 击退小跳设正值（向上）
        bool resting = false;    // 是否已落在实体方块顶面（resting 跳过重力，仅复探支撑格）
        // t249 受击击退水平速度（XZ 分量，blocks/s）：knockback() 受击瞬间设置，tick 指数衰减到 0。
        //   机制等价 MC 1.0 knockback 冲量（受击实体被沿攻击方向推开）；与 AI wander 的「直接位移」分离，
        //   作为独立速度层叠加（knockback 期间 AI 仍可走，二者位移相加，同 MC 受击时实体既有动量又有击退）。
        float vx = 0.0f;         // 击退水平速度 X（默认 0；仅 knockback 后非零）
        float vz = 0.0f;         // 击退水平速度 Z（默认 0；仅 knockback 后非零）
        // t239 生物基类（AI / 血量 / 受击 / 死亡）——仅 Mob kind 使用（FallingBlock/Item 留默认 0/false）：
        int mobType = 0;         // mob 子类 id（0=通用测试；t240 pig/cow/sheep；t280 Shambler/Bones；drop/模型据它分流）
        int maxHealth = 0;       // 血量上限（满血）；takeDamage clamp 到 [0, maxHealth]
        int health = 0;          // 当前血量；<=0 → dead（spawnMobTyped 设满血）
        bool dead = false;       // 死亡态（health<=0 → true；dead 期间冻结 AI/重力，deathTimer 到 0 移除）
        float hurtFlash = 0.0f;  // 受击红闪剩余秒数（damageEntity 设 kHurtFlashTime；tick 衰减；>0 → QML 红）
        float deathTimer = 0.0f; // 死亡到移除倒计时（dead 翻 true 时设 kDeathTime；给 QML 播死亡动画窗口）
        // t280 黑暗刷怪（敌对生物 Shambler/Bones 专用；passive / FallingBlock 留默认 false/0 不触发）：
        //   hostile=true 的 Mob 走 tickHostileLife 的燃烧 + 远距消失 + spawn 调度逻辑。passive（pig/cow/sheep/
        //   test）hostile=false → 不燃烧 / 不计入敌对上限 / 不远距消失（passive 永驻世界，机制等价 MC 被动生物
        //   不燃烧、不自然消失）。FallingBlock / Item 不走 Mob 分支故 hostile 字段不读。
        bool  hostile   = false; // 是否敌对生物（Shambler/Bones=true；passive=false）。spawnHostileMob 设 true。
        bool  burning   = false; // 当前是否在日光下燃烧（tickHostileLife 每 tick 重算缓存；QML isBurningAt 读）。
        float burnTimer = 0.0f;  // 燃烧扣血累积（秒；暴露日光时累加 dt，每 kBurnDamageInterval 扣 1HP）。
        float suffocationTimer = 0.0f; // t254 窒息累积计时（头部嵌实体方块时累加，每 kSuffocationInterval 秒扣 1HP；机制同玩家 t160）
        float yawRad = 0.0f;     // 朝向 + 行走方向（弧度）；AI wander 随机选；dir=(-sin,0,-cos)，QML yawDeg
        float wanderTimer = 0.0f;// 到下次选向倒计时（秒）；<=0 → 新 yawRad + 新 wanderSpeed + 重置 timer
        float wanderSpeed = 0.0f;// 当前 AI 行走速度（blocks/s；0=idle 停驻 / kWalkSpeed=行走）；time-slice 随机
        float moveSpeed = 0.0f;  // 当前有效水平速度（= wanderSpeed 行走时；撞墙/idle/死亡=0；expose 供 t241 腿摆）
        float walkPhase = 0.0f;  // t241 行走动画相位（弧度）：moveSpeed>0 时推进（fmod 2π），余冻结；QML 腿摆读
        // t241 羊吃草态（仅 mobType==MobSheep 用；其余 mob 留默认 0/false 不触发）：
        float eatTimer = 0.0f;   // >0 = 正处吃草周期（秒，倒数到 0 结束）；周期内强制 idle 站立 + 头部俯仰
        bool  eatApplied = false;// 本周期是否已消耗草丛（apply 阈值到达时置 true，防重复消耗）
        float eatCooldown = 0.0f;// 到下次扫描草丛的倒计时（秒）；吃完后 kEatCooldown、空扫描后 kEatScanInterval
        // t250 环境音态（仅 Mob kind 用；FallingBlock/Item 留默认不触发）：
        float stepAccum = 0.0f;  // walkPhase 半步累加器（弧度）；行走时累加 moveSpeed*dt*kWalkFreq，≥π → emit mobStep
        float ambientTimer = 0.0f; // 到下次 idle 叫声的倒计时（秒）；≤0 → emit mobAmbient + 重置随机周期
    };
    std::vector<Entity> m_entities;
    int m_revision = 0;

    // t256 slot-reuse（修掉落沙 delegate 泄漏）：实体移除（着地 / 死亡 / 跌出）不再 erase-shift，而把槽位
    //   标 alive=false + 入 m_freeSlots；下次 spawn 优先复用空槽。于是 m_entities.size()（=count 属性 = QML
    //   Repeater model）在游玩期**单调不降** → Repeater 永不需要销毁已 reparent 的 3D delegate。根因：
    //   lessons-learned t170「reparent 后的 3D delegate 在 Repeater count 减小时不被销毁」——掉落沙频繁
    //   spawn/land 使 count 上下抖动，每次「升」新建的 delegate（BlockCube + 材质 + 子 Model）在「降」时
    //   不回收 → 10min 累积数千孤儿 delegate → ~2GB / 卡顿；重启清零（症状吻合）。slot 复用让 count 不降
    //   → delegate 一次创建后稳定复用（空槽仅 visible=false 隐藏，不销毁不新建）→ 无累积。高水位受 kCap
    //   钳制（≤64 槽），与既有「峰值并发实体数」同量级，无额外常驻开销。
    std::vector<int> m_freeSlots; // 已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体实体数（= m_entities.size() − 空槽数）；spawn 上限 + F3 draw 估算读它
    // t280 黑暗刷怪 spawn 节流累积器（秒）：tickHostileLife 每 tick 累加 dt，达 kSpawnInterval 才尝试一次 spawn
    //   周期（kSpawnAttempts 次选点）。独立于物理 / AI 的 tick（tick 每 16ms 跑、spawn 每 kSpawnInterval 秒跑一次，
    //   节流避免每帧扫几千次 blockAt）。PlayerController 唯一调 tickHostileLife → 累加器随其 60Hz tick 推进。
    float m_spawnAccum = 0.0f;

    // 把构造好的实体放入槽位（优先复用空槽，否则追加）。move 入槽后 alive=true（Entity 默认）。++m_liveCount。
    int acquireSlot(Entity &&e)
    {
        int slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_entities[size_t(slot)] = std::move(e);
        } else {
            m_entities.push_back(std::move(e));
            slot = int(m_entities.size()) - 1;
        }
        ++m_liveCount;
        return slot;
    }
    // 释放槽位：alive=false + 入 free list + --m_liveCount。不 erase → count 不降 → Repeater delegate 稳定。
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_entities.size())) return;
        m_entities[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }

    // t239 AI wander 自主移动（tick 内 Mob 分支调）：时间片倒计时到 → 随机选向 + idle/行走；行走按 yaw 逐轴
    //   （X 后 Z）世界边界 clamp + 方块碰撞撤回。返回是否真位移（驱动 dirty + moveSpeed）。worldW/worldD =
    //   世界宽/深（边界 clamp 防 mob 走出世界坠虚空）。
    bool aiWander(Entity &e, float dt, World *world, float worldW, float worldD);

    // t241 羊吃草：检测/消耗 entity 前方一格草丛。consume=false 仅检测（决定是否进入吃草周期）；
    //   consume=true 则写入（草丛→空气 + 其下草方块→泥土，走 World::setWaterSilent 静默写，非玩家破块
    //   → 不发 broken/placed，免粒子/音/掉落噪音）。返回是否在前方找到草丛（TallGrass）。
    //   目标格 = 沿 yaw 朝向 reach=0.7 前方列、y=身体格（草丛所在）+ 其下地表格（草方块）；OOB → 安全 false。
    bool sheepEatGrass(Entity &e, World *world, float worldW, float worldD, bool consume);

    static constexpr int kCap = 64;            // 实体数上限（测试用，防溢出）
    static constexpr float kGravity = 28.0f;   // 重力加速度（blocks/s²；与玩家/掉落物同值，世界手感一致）
    static constexpr float kMaxFall = 78.4f;   // 终端下落速度（blocks/s；防无限加速）
    // t252：kRestOffset 移除 —— resting 贴地偏移现按 per-entity halfH（底面贴支撑方块顶面 = top + halfH），
    //   不再是固定 0.5（cow halfH=0.70 → 比 1×1 高 0.2，单一常量无法表达）。
    // t239 生物基类常量：
    static constexpr int kDefaultMaxHealth = 10;  // 默认 mob 血量（猪/牛/羊 MC 1.0 = 10 = 5 心）
    static constexpr float kWalkSpeed = 1.0f;     // AI 行走速度（blocks/s；慢于玩家 4.3，wander 观感）
    static constexpr float kWanderMin = 2.0f;     // 选向时间片下限（秒）
    static constexpr float kWanderMax = 5.0f;     // 选向时间片上限（秒）
    static constexpr float kIdleChance = 0.25f;   // 每次选向进入 idle（speed=0 停驻）的概率
    static constexpr float kHurtFlashTime = 0.5f; // 受击红闪持续秒数（机制等价 MC mob 受击 10 tick = 0.5s）
    static constexpr float kDeathTime = 0.5f;     // 死亡到移除窗口（给 QML 播死亡动画；机制等价 MC 死亡动画）
    // t254 mob 窒息扣血间隔（机制同玩家 t160 的 kSuffocationInterval）：mob 头部（AABB 顶格）嵌实体可碰撞方块
    //   （被沙 / 方块埋住）时，每本间隔秒扣 1HP。机制等价 MC 1.0 窒息 1HP/s（每秒半心）；复用 damageEntity 链
    //   （扣血 + hurtFlash 红闪 + 血量归零 mobDied 死亡掉落），同玩家 fallDamageTaken(1)→takeDamage 链。
    static constexpr float kSuffocationInterval = 1.0f; // t254 窒息扣血间隔（秒；每秒 1HP，机制等价 MC 窒息 1/秒，同玩家 t160）
    // t241 行走动画 / 羊吃草常量：
    static constexpr float kWalkFreq = 6.2831853f; // 行走相位推进系数（=2π → 每 block 行走完成一个完整腿摆周期；
                                                   //   moveSpeed * dt * kWalkFreq；机制等价 MC mob 每步一摆）
    static constexpr float kEatDuration = 1.2f;    // 吃草周期总时长（秒；头低→嚼→抬 包络）；期间强制 idle 站立
    static constexpr float kEatApplyAt  = 0.5f;    // 周期内消耗草丛的时刻（秒；近 sin(πp) 包络峰 → 头最低时嚼）
    static constexpr float kEatCooldown = 2.0f;    // 吃完一棵后到下次扫描的冷却（秒；防连续吃完一片）
    static constexpr float kEatScanInterval = 1.0f; // 空扫描（前方无草）后到下次扫描的间隔（秒；节流扫描开销）
    static constexpr float kEatReach = 0.7f;       // 检测前方草丛的水平距离（block；头部前方 ~ 半格多）
    static constexpr float kEatHeadPitch = -0.6f;  // 吃草头部俯仰峰值（弧度，负=低头；headPitchAt 据 sin(πp) 调制）
    // t249 受击击退常量（机制对齐 MC 1.0 knockback 量级，spec「小跳击退」）：
    //   kKnockbackHoriz：击退水平初速（blocks/s）。受击瞬间设 vx/vz=本值×方向；与 kWalkSpeed=1.0 相比明显
    //     快（≈4.5×走速），但远低于玩家 kWalk=4.3 + 疾跑 → 玩家可追上被击退的 mob（不会打飞到追不上）。
    //   kKnockbackUp：击退小跳垂直初速（blocks/s，向上为正）。峰值高 = v²/(2g) = 20.25/56 ≈ 0.36 格 = 小弹起
    //     （机制等价 MC knockback 的小幅上弹，非大跳）。重力 28 把它拉回，着地走原 tick 路径。
    //   kKnockbackDrag：水平速度指数衰减率（1/s）。每帧 vx *= (1 - drag*dt)；时间常数 1/drag=0.25s → ~0.5s
    //     衰减到 ~13%、~1s 近停。总位移 ≈ v0/drag ≈ 1.1 格（小击退，对齐 spec「小跳击退」而非打飞）。
    static constexpr float kKnockbackHoriz = 4.5f;  // 击退水平初速（blocks/s）
    static constexpr float kKnockbackUp    = 4.5f;  // 击退小跳垂直初速（blocks/s；峰值 ~0.36 格）
    static constexpr float kKnockbackDrag  = 4.0f;  // 击退水平衰减率（1/s；~0.5s 基本停下）
    // t250 环境音常量（机制对齐 MC 1.0 被动生物偶发 idle call + 走路声；§9 原创合成音色在 build_sounds.py）：
    //   kStepHalfStride：走路声半步阈值（弧度）。walkPhase 每完整 stride=2π 含两次脚落（左+右），故每累积
    //     π → 一次脚落 → emit mobStep（同 player QML 端「Δphase≥π 播一次脚步音」语义，搬进 C++ 避逐 mob 追踪）。
    //   kAmbientMin/Max：idle 叫声随机周期（秒）。MC 1.0 被动生物偶发 idle call 间隔量级 ~ 数秒到十余秒；
    //     取 8-16s 偏稀疏（环境氛围、不抢前景；密集多 mob 场景也不吵）。
    //   kAudioRange：听者范围（blocks）。仅此半径内的活体 mob emit idle/step 叫声（远场静默，防满屏 mob
    //     同步吵闹 + 无意义远场计算）。
    static constexpr float kStepHalfStride = 3.14159265f; // 半步阈值（弧度；=π）
    static constexpr float kAmbientMin = 8.0f;            // idle 叫声间隔下限（秒）
    static constexpr float kAmbientMax = 16.0f;           // idle 叫声间隔上限（秒）
    static constexpr float kAudioRange = 24.0f;           // 听者范围（blocks；近 mob 才发声）
    // t280 黑暗刷怪常量（机制对齐 MC 1.0「light≤7 刷怪 / 距玩家>24 / 总数上限 / 白天燃烧」；数值为本工程小世界
    //   160×160×64 量身调，非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）。dev-spec t280 验收阈值在常量名注：
    //   kSpawnLightThreshold=7（spec「light<阈值(7)」）、kSpawnMinDist=24（spec「距玩家>N 格(24)」）。
    //   - kSpawnInterval / kSpawnAttempts：周期 spawn 节流。每 2s 一周期、每周期 8 次随机选点 → 平均 ~1 mob/2s
    //     增长（受 cap 钳制），夜里 10 分钟周期可堆 ~30 只（= kHostileMobCap），白天燃烧削掉。MC 自身约每 tick
    //     尝试 spawn，本工程节流到 2s 周期 + 8 attempts 是「世界小、mob 密度够」与「扫描开销省」的平衡。
    //   - kSpawnMinDist/Max：spawn 环 [24, 40]（spec 24 为下界）。下界 24 = 玩家附近不刷（避免凭空冒脸贴脸）；
    //     上界 40 = 不刷太远（玩家走过去前 mob 不动 = 浪费槽，且 kFarDespawn=56 会清掉）。
    //   - kHostileMobCap：敌对总数上限（不含 passive / FallingBlock / Item）。30 = 小世界合理密度（MC 1.0 自然
    //     spawn cap 70，本工程世界小取一半）。达上限 → 跳过 spawn（passive / FallingBlock 仍可 spawn，走 kCap）。
    //   - kSpawnLightThreshold：刷怪所需有效光上界（< 此才刷，spec 7）。有效光 = max(skyLightAt*skyBrightness,
    //     blockLightAt)（0..15）；夜间地表 / 洞穴均 < 7 可刷、白天地表 > 7 不刷（机制等价 MC「light level ≤ 7」）。
    //   - kBurnSkyBrightness / kBurnDamageInterval：日光燃烧。skyBrightness > kBurnSkyBrightness（白天，>0.55
    //     = 太阳在地平线足够高）且 mob 直接见天（skyLightAt>=15 = 无树叶 / 顶棚遮挡）→ burning=true，每
    //     kBurnDamageInterval 秒扣 1HP（机制等价 MC 僵尸 / 骷髅日光燃烧 1HP/s）。shade（skyLightAt<15）不燃烧。
    //   - kFarDespawn：敌对远距消失半径（blocks）。距玩家 > 此的 hostile 直接 releaseSlot（防世界塞满远处 mob；
    //     MC 即时消失半径 128，本工程小世界取 56 ≈ spawn 上界 40 + 缓冲 → spawn 环边沿 mob 不被立即清）。
    //   - kHostileDefaultHealth：Shambler/Bones 满血（机制等价 MC 1.0 僵尸 / 骷髅 20HP=10 心；本工程取 20 同
    //     passive kDefaultMaxHealth=10 ×2 —— 敌对略肉以体现威胁，仍可几剑打死，对齐 t265 攻击力）。
    static constexpr float kSpawnInterval        = 2.0f;  // spawn 周期（秒；每周期 kSpawnAttempts 次选点）
    static constexpr int   kSpawnAttempts        = 8;     // 每 spawn 周期的随机选点尝试次数
    static constexpr float kSpawnMinDist         = 24.0f; // spawn 环下界（blocks；spec「距玩家>24」）
    static constexpr float kSpawnMaxDist         = 40.0f; // spawn 环上界（blocks）
    static constexpr int   kHostileMobCap        = 30;    // 敌对生物总数上限（不含 passive / FallingBlock / Item）
    static constexpr float kSpawnLightThreshold  = 7.0f;  // 刷怪有效光上界（< 此才刷；spec「light<7」）
    static constexpr float kBurnSkyBrightness    = 0.55f; // 燃烧所需 skyBrightness 门（白天；>0.55 = 日间）
    static constexpr float kBurnDamageInterval   = 1.0f;  // 燃烧扣血间隔（秒/HP；机制等价 MC 日光燃烧 1HP/s）
    static constexpr float kFarDespawn           = 56.0f; // 敌对远距消失半径（blocks）
    static constexpr int   kHostileDefaultHealth = 20;    // Shambler/Bones 满血（机制等价 MC 1.0 僵尸 / 骷髅 20HP）
};

#endif // ENTITYMANAGER_H
