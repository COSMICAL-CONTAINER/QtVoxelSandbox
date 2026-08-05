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
    // 管）/ FallingBlock=贴图方块 / Arrow=箭矢投射物（t283 骷髅弓箭手远程射出，细长杆定向 Model））。
    enum Kind { Mob, Item, FallingBlock, Arrow };
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
    //   t284 Stalker（潜行者）= MobStalker(6)：机制等价 MC 1.0 苦力怕（Creeper）—— 近距蓄力膨胀 → 爆炸（破坏
    //     方块 + 伤害玩家 + 音效）。PLAN §9 区隔改名 Creeper→Stalker「潜行者」；名称 / 模型 / 贴图全原创（仅
    //     机制对齐「黑暗刷怪 / 近距自爆」）。Entity.hostile=true → 走 tickHostileLife 燃烧 / 远距消失 / spawn 调度
    //     （同 Shambler/Bones）；tick Mob 分支据 mobType==MobStalker 路由到 aiStalker（蓄力 fuse → detonateStalker
    //     爆炸：球形破坏方块 + 距离衰减伤害玩家 + emit explosion 音效）。inflateAt 暴露 fuse 进度供 QML 膨胀动画。
    enum MobType { MobTest = 0, MobPig = 1, MobCow = 2, MobSheep = 3, MobShambler = 4, MobBones = 5, MobStalker = 6, MobSpider = 7 };
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
    // t283 箭矢投射物（骷髅弓箭手 MobBones 远程攻击）：在 origin 处生成一支携带初速度 vel（blocks/s，含 vy 抛物）
    //   的箭实体。kind=Arrow、pushable=false（玩家走碰不推箭）、halfW/halfH=0.06（细长杆视觉 + 碰撞最小）。
    //   tick 内 Arrow 分支：重力改 vy（抛物）+ 速度位移 + 方块碰撞（命中即移除）+ 玩家 AABB 碰撞（命中发
    //   mobAttackedPlayer(kArrowDamage, MobBones) + 移除）+ 寿命 / 边界超界兜底移除。呈现层 mobHost delegate 据
    //   kindAt==Arrow 走细长杆 Model + arrowYawAt/arrowPitchAt 定向。机制等价 MC 1.0 骷髅射箭（箭抛物 + 命中伤害）；
    //   名称 / 视觉全原创（§9 区隔，不照搬 MC 美术）。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnArrow(const QVector3D &origin, const QVector3D &vel);
    // t304 玩家弓射出的箭（spec「松开射箭（抛物+伤害 mobs）」）：与 spawnArrow（骷髅射出，命中玩家）对称，
    //   差异在 arrowFromPlayer=true（命中 mob 而非玩家）+ arrowDamage 由弓蓄力决定（1..6 HP，caller 传）。
    //   命中 mob 走 damageEntity（扣血 + 红闪 + 归零 mobDied 死亡掉落）+ emit mobAttacked(mobType, false)
    //   （呈现层 playMobHurt；同玩家近战 attackMob 路径）。机制等价 MC 1.0 玩家弓箭打怪（敌我判别由发射者定）。
    //   origin = 玩家眼位 + 视线前移 0.5（防贴墙 spawn 入墙即没）；vel = 视线方向 × 蓄力速度（含抛物 vy）。
    //   达 kCap → 跳过 + 告警（防溢出，同 spawnArrow）。
    Q_INVOKABLE void spawnArrowPlayer(const QVector3D &origin, const QVector3D &vel, int damage);
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
    // t300 第 i 只 mob 是否**已被剪羊毛**（仅 mobType==MobSheep 用；其余 mob 永远 false）。QML delegate 据它切换
    //   羊的「毛茸」外观 vs 「裸」外观（sheared=true → 裸粉色身；false → mob_sheep 贴图毛茸身）。越界 / 非 sheep → false。
    //   revision 在剪羊毛 / 重新长毛时 bump 让 QML 绑定刷新（同 hurtFlash / chasing 模式）。
    Q_INVOKABLE bool shearedAt(int i) const;
    // t300 剪羊毛（spec「玩家右键羊 + 持剪刀 → 羊变裸 + 掉羊毛物品」）：第 i 只**未剪羊毛的活体 sheep** → 翻
    //   sheared=true + 设 regrowCooldown（吃草重新长毛前的冷却，防刚剪完立即吃草长回）+ emit sheepSheared(坐标)
    //   让呈现层 Connections 转发到 ItemEntityManager.spawnItem 生成羊毛物品掉落实体（同 mobDied→spawnItem 模式；
    //   单向事件流，分层：Entities 层发语义事件、呈现层只消费）。已剪羊毛 / 非 sheep / dead / 越界 → 静默早退
    //   （机制等价 MC 1.0：剪羊毛只对有毛的羊生效，已裸的羊右键无反应）。bump revision → QML 翻羊为裸外观。
    //   Q_INVOKABLE 兼调试 + playercontroller placeBlock shears 分支双入口（playercontroller 是 C++ 直调）。
    Q_INVOKABLE void shearSheep(int i);
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
    // t283 箭矢定向（呈现层 mobHost delegate Arrow 分支读）：arrowYawAt/arrowPitchAt 据 vel 算水平朝向 + 俯仰
    //   （度）。yaw 用 player 同约定（dir=(-sin,-cos)，yaw=atan2(-vx,-vz)）→ QML eulerRotation.y=yaw 使杆本地 -Z
    //   正对飞行方向；pitch=atan2(vy,h)（正=上扬）。非 Arrow / 越界 → 0。velAt 返箭 3D 速度（F3/调试）。
    Q_INVOKABLE float arrowYawAt(int i) const;
    Q_INVOKABLE float arrowPitchAt(int i) const;
    Q_INVOKABLE QVector3D velAt(int i) const;
    // t323 箭嵌入态访问（PlayerController::arrowPickupScan 近距拾取读）：isArrowStuckAt = 箭是否已嵌入方块
    //   （仅嵌入箭可拾，飞行中不拾 —— 免误拾飞行箭）。arrowFromPlayerAt = 是否玩家射出（仅玩家箭可拾；
    //   骷髅箭防刷不拾，spec「SKELETON 箭不可拾取」）。非 Arrow / 越界 → false。
    //   removeEntityAt = 释放槽位（拾取全入后销毁嵌入箭；同 ItemEntityManager.removeAt 拾取销毁语义）。
    Q_INVOKABLE bool isArrowStuckAt(int i) const;
    Q_INVOKABLE bool arrowFromPlayerAt(int i) const;
    Q_INVOKABLE void removeEntityAt(int i);
    // t284 Stalker 蓄力膨胀进度（0..1）：fuseTimer>0（正在蓄力）时返 clamp(fuseTimer/kFuseTime,0,1)，供 QML
    //   delegate 据 it 对 Model 做 scale（1+inflate·0.5）+ baseColor 蓄力发白（机制等价 MC 苦力怕近距蓄力膨胀
    //   发白）。非 Stalker / 未蓄力 / 越界 → 0（模型静态）。revision 在蓄力期每帧 bump（tick Mob 分支）让绑定刷新。
    Q_INVOKABLE float inflateAt(int i) const;
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
    //   → 早 return。Mob：AI 行走（aiWander / aiHostile / aiArcher）+ 重力；dead Mob：仅 deathTimer 倒计时
    //   （冻结）；FallingBlock：t117/t220 着地放置 / 变掉落物 + 移除；Arrow（t283）：抛物 + 方块 / 玩家命中 + 移除。
    //   t250 环境音：listener = 玩家脚底位置（听者），用于 proximity 门控 mob idle/step 叫声 —— 仅听者
    //   kAudioRange 半径内的活体 mob 才 emit mobAmbient/mobStep（远场静默，防多 mob 同步吵闹）。PlayerController
    //   传 m_pos（菜单态仍有效）。listener 无关物理 / AI，仅参与音频门控（不写入实体态）。
    //   t283 箭命中玩家：listenerHalfW/listenerHeight = 玩家当前 AABB 半宽 / 高（PlayerController 传 kHalfW /
    //   m_height，蹲下时随之缩小 → 箭命中盒正确随蹲下收缩）。Arrow 分支据它判 point-in-AABB 命中。
    //   t290 观察者交互门控：playerTargetable = 玩家是否可被敌对生物锁定为仇恨目标（PlayerController 传
    //   mode==Survival —— 创造/观察者玩家不可锁定）。false 时：敌对 Mob 不 detect/chase/attack/shoot（回退
    //   wander，且清残留追踪态 + Stalker 熄火，防 Survival→Creative/Spectator 切换后仍贴脸/射）；飞行中的箭
    //   不再判定命中玩家（穿过）。机制等价 MC 1.0 创造/观察者无敌且不被仇恨。tickHostileLife（刷怪/燃烧/远距
    //   消失）不读本参数 —— 那是世界模拟，独立于玩家模式（夜间照样刷怪、白天照样燃烧）。分层（PLAN §2）：
    //   玩家模式标志由 Game/Physics 层（PlayerController）持有并据此派生 bool 向下传（Game→Entities 向下依赖，
    //   同 listener/playerPos 先例），Entities 层不反查玩家模式（不反向依赖 Game）。
    void tick(qreal dt, World *world, const QVector3D &listener, float listenerHalfW, float listenerHeight,
              bool playerTargetable);
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
    // t250 mob 环境 idle 叫声（被动 牛叫/羊叫/猪叫 + 敌对 idle）：tick 内 ambientTimer 周期倒计时（随机
    //   8-16s）到 + 玩家听者范围内 → emit mobAmbient(mobType)。mobType = 子类 id（0=通用 / 1=猪 / 2=牛 /
    //   3=羊 / 4=Shambler / 5=Bones / 6=Stalker / 7=Spider，全 8 子类均周期偶发叫 —— 敌对亦走此路径，非仅
    //   被动）→ 呈现层（Main.qml）Connections 路由到 AudioManager.playMobAmbient 据 mobType 选 mob_idle
    //   clip（t294：敌对 4-7 各有独立音色，旧兜底通用已补全；机制等价 MC 1.0 生物偶发 idle call；§9 原创，
    //   零 MC 资产）。分层（PLAN §2）：Entities 层发语义事件，呈现层只消费。
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
    // t281 敌对 mob 近战攻击命中玩家（spec「attack」）：hostile mob（Shambler/Bones/Spider）在 aiHostile 内检测到
    //   玩家处于攻击范围（XZ<=kAttackRange + 垂直同层）且攻击冷却（kAttackCooldown）到时发本信号。amount = 单次伤害 HP
    //   （kAttackDamage=3，MC 简单难度僵尸）；mobType = 子类 id（Shambler/Bones/Stalker/Spider）。呈现层（Main.qml）
    //   Connections 据它路由到 PlayerState.takeDamage —— 仅 Survival 应用（Creative/Spectator 无伤跳过，机制等价 MC
    //   创造/观察者无敌）；同 fallDamageTaken→takeDamage 模式（Game/Entities 层发语义事件、呈现层只消费，PLAN §2 分层）。
    //   attackCooldown 由 EntityManager 自管（防同帧多 mob 连抽；mobType 供呈现层选攻击音 / 反馈）。
    // t296 玩家受击击退方向（kbX,kbZ）= 欲把玩家推开的水平**单位**方向（XZ）：
    //   - 近战（aiHostile attack）/ 爆炸（detonateStalker）：(玩家脚位 − mob 中心) XZ 归一 → 把玩家推开 mob。
    //   - 箭（Arrow tick 命中）：箭飞行速度 (vx,vz) 归一 → 沿箭去向推玩家（机制等价 MC 箭动量传递）。
    //   呈现层据它调 PlayerController.applyHitKnockback（仅 Survival 生效；创造/观察者无敌不弹）。零向量由 caller
    //   兜底（mob 正上方等退化情形），此处不再归一。mobAttackedPlayer 经 t290 门控仅在玩家可锁定（Survival）时发，
    //   故击退天然只作用于 Survival 玩家。
    void mobAttackedPlayer(int amount, int mobType, float kbX, float kbZ);
    // t284 Stalker 爆炸（detonateStalker 内发）：坐标 = 爆炸中心格 floor(pos)。呈现层（Main.qml）Connections
    //   据它路由到 AudioManager.playExplosion（爆炸音）+ BlockParticles.burstExplosion（白色迸发视觉）。
    //   方块破坏走 setWaterSilent（直写 + worldChanged 重建 mesh，**不**发 blockBroken → 免每块破块粒子/音 spam），
    //   故本信号是爆炸的**唯一**音/视反馈入口（同 fallDamageTaken→takeDamage 语义事件模式；PLAN §2 分层）。
    //   玩家伤害复用 mobAttackedPlayer（Survival 门控应用 takeDamage）。
    void explosion(int x, int y, int z);
    // t297 爆炸掉落（detonateStalker 内发，每破坏块按 kExplosionDropChance 概率）：坐标 = 被破坏方块格
    //   floor，itemId = BlockRegistry::dropId(原方块)（Stone→Cobble 等，同玩家挖掘掉落，非原方块 id）。
    //   呈现层（Main.qml）Connections 转发到 ItemEntityManager.spawnItem 生成掉落实体（机制等价 MC 爆炸
    //   把被毁方块的物品弹出来；同 fallingBlockDropped 模式）。分层（PLAN §2）：Entities 层发语义事件，
    //   呈现层只消费，绝不反向写栅格。
    void explosionDroppedItem(int x, int y, int z, int itemId);
    // t304 玩家箭命中 mob（spec「抛物+伤害 mobs」的命中反馈）：玩家弓射出的箭（spawnArrowPlayer）在 tick 内
    //   命中 mob 时发。damageEntity 已扣血 + 红闪 + 归零 mobDied 死亡掉落；本信号额外驱动命中音（呈现层 →
    //   AudioManager.playMobHurt，同近战 attackMob→PlayerController.mobAttacked 模式）。mobType = 被命中 mob 子类 id。
    void arrowHitMob(int mobType);
    // t300 羊被剪羊毛（shearSheep 内发，仅未剪羊毛的活体 sheep 首次翻 sheared=true 时发）。坐标 = 羊当前格
    //   floor(pos)（与 spawnItem 整数格约定一致，便于 ItemEntityManager 落在羊身旁）。呈现层（Main.qml）Connections
    //   据它转发 ItemEntityManager.spawnItem(0x20E=羊毛 ×1)（同 mobDied→spawnItem 模式；单向事件流，PLAN §2 分层：
    //   Entities 层发语义事件、呈现层只消费，绝不反向写栅格）。机制等价 MC 1.0 剪羊毛掉落羊毛物品。
    void sheepSheared(int x, int y, int z);

private:
    struct Entity {
        // t256：槽位占用标志（slot-reuse 模型）。true = 已分配的活体实体；false = 已释放的空槽（在
        //   m_freeSlots 中待复用）。与 mob 的 dead（死亡动画期）正交：濒死 mob 仍 alive=true（占槽播
        //   死亡动画），deathTimer 到才 releaseSlot → alive=false（空槽，delegate 隐藏）。呈现层 delegate
        //   绑 visible:aliveAt(index) → 空槽隐藏；C++ 各遍历（tick/findMobHit/resolvePlayerPush）跳过空槽。
        bool alive = true;
        QVector3D pos;
        // t252 碰撞箱缩小：XZ 半宽（halfW）与 Y 半高（halfH）分离（旧版单一 radius=0.5 致所有 mob
        //   碰撞感「整立方大」1×1×1）。t293 进一步收紧贴合 MobModel 身体（旧值「大一圈」）。按 mobType 设：
        //   MobTest 0.5/0.5（保 t95 旧路径）；pig/sheep 0.40/0.45；cow 0.40/0.50；敌对（Shambler/Bones/
        //   Stalker）0.30/0.90（机制等价 MC 1.0 敌对 0.6 宽）；spider 0.45/0.30。FallingBlock 0.5/0.5
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
        // t283 Arrow（箭矢投射物）专用：vx/vy/vz 复用作 3D 速度（Arrow 不走 Mob 击退衰减分支，无冲突），
        //   arrowLife = 寿命倒计时（秒；tick Arrow 分支递减，<=0 或命中 / 越界 → releaseSlot 移除）。
        //   非 Arrow 实体 arrowLife=0 不读。
        float arrowLife = 0.0f;  // 箭寿命倒计时（秒；仅 kind==Arrow 用）
        // t304 玩家射出的箭（spawnArrowPlayer）专用：arrowFromPlayer=true 的箭命中 **mob**（damageEntity +
        //   mobAttacked 语义事件）；false（骷髅 spawnArrow 射出）命中 **玩家**（mobAttackedPlayer，t283 旧路径）。
        //   机制等价 MC 1.0「玩家箭打怪、怪箭打玩家」（敌我判别由发射者决定，非箭本身阵营）。非 Arrow 默认 false。
        //   arrowDamage = 本箭命中时造成的伤害 HP（骷髅箭恒 kArrowDamage=2；玩家箭由弓蓄力 1..6 决定，spawnArrowPlayer 传）。
        bool arrowFromPlayer = false; // 是否玩家射出（命中目标分流：true→mob / false→玩家）
        int arrowDamage = 0;          // 命中伤害（HP；仅 kind==Arrow 用；骷髅箭 = kArrowDamage）
        // t323 箭嵌入态（命中方块后冻结物理）：arrowStuck=true → tick Arrow 分支仅推进 despawn 倒计时，不再
        //   重力 / 位移 / 命中判定。vx/vy/vz 保留作定向（arrowYawAt/arrowPitchAt 据 vel 算 → 嵌入箭仍朝命中
        //   飞行方向）。命中瞬间 e.pos 钉到入射面（半嵌可见）+ arrowLife 重置 kStuckArrowLifetime（~60s despawn）。
        //   玩家箭（arrowFromPlayer）嵌入后可被 PlayerController::arrowPickupScan 近距拾取；骷髅箭嵌入不可拾取
        //   （防刷箭，spec）。非 Arrow / 飞行中默认 false。
        bool arrowStuck = false; // 箭是否已嵌入方块（仅 kind==Arrow 用；spawn 默认 false，acquireSlot 覆写新实体）
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
        // t281 敌对 AI 态（仅 hostile=true 的 Mob 用；passive / FallingBlock 留默认不触发）：
        //   detect→pathfind→attack 三段（spec t281「敌对生物基类（AI/寻路）」）。chasing 在 aiHostile 内据
        //   玩家距离（<=kDetectRange）翻 true 并刷新 chaseTimer；脱离后记忆期内仍追，过则回退到 wander。
        //   attackCooldown 每自然秒递减，<=0 且在攻击范围内 → emit mobAttackedPlayer + 重置（防每帧抽血）。
        bool  chasing = false;       // 是否正追踪玩家（detect 范围内或记忆期内）；非追踪时回退 wander
        float chaseTimer = 0.0f;     // 追踪记忆倒计时（秒；脱离侦测后仍追 kChaseMemory 秒，机制等价 MC 短期记忆）
        float attackCooldown = 0.0f; // 攻击冷却倒计时（秒；<=0 可攻击；命中后置 kAttackCooldown，防连抽）
        float yawRad = 0.0f;     // 朝向 + 行走方向（弧度）；AI wander 随机选；dir=(-sin,0,-cos)，QML yawDeg
        float wanderTimer = 0.0f;// 到下次选向倒计时（秒）；<=0 → 新 yawRad + 新 wanderSpeed + 重置 timer
        float wanderSpeed = 0.0f;// 当前 AI 行走速度（blocks/s；0=idle 停驻 / kWalkSpeed=行走）；time-slice 随机
        float moveSpeed = 0.0f;  // 当前有效水平速度（= wanderSpeed 行走时；撞墙/idle/死亡=0；expose 供 t241 腿摆）
        float walkPhase = 0.0f;  // t241 行走动画相位（弧度）：moveSpeed>0 时推进（fmod 2π），余冻结；QML 腿摆读
        // t241 羊吃草态（仅 mobType==MobSheep 用；其余 mob 留默认 0/false 不触发）：
        float eatTimer = 0.0f;   // >0 = 正处吃草周期（秒，倒数到 0 结束）；周期内强制 idle 站立 + 头部俯仰
        bool  eatApplied = false;// 本周期是否已消耗草丛（apply 阈值到达时置 true，防重复消耗）
        float eatCooldown = 0.0f;// 到下次扫描草丛的倒计时（秒）；吃完后 kEatCooldown、空扫描后 kEatScanInterval
        // t300 剪羊毛 + 吃草重新长毛态（仅 mobType==MobSheep 用；其余 mob 留默认 false/0 不触发）：
        //   sheared=true → 已被剪羊毛（mob_sheep 贴图被遮为「裸粉色」外观，QML delegate 据 shearedAt 切换）；
        //   剪羊毛后羊**站在草方块上** + regrowCooldown 到 → 重新长毛（sheared=false）+ 脚下草方块→泥土
        //   （机制等价 MC 1.0「羊吃草方块重新长毛」）。regrowCooldown 由 shearSheep 设 kRegrowCooldown（防刚剪完
        //   即长回；spec「加一个重新长毛冷却，免得刷屏」）。未剪羊毛的羊永远 sheared=false（默认状态）。
        bool  sheared = false;       // 是否已被剪羊毛（QML delegate 据它切换毛茸 vs 裸外观）
        float regrowCooldown = 0.0f; // 剪羊毛后到能吃草方块重新长毛的冷却倒计时（秒；仅 sheared=true 时推进 / 触发）
        // t250 环境音态（仅 Mob kind 用；FallingBlock/Item 留默认不触发）：
        float stepAccum = 0.0f;  // walkPhase 半步累加器（弧度）；行走时累加 moveSpeed*dt*kWalkFreq，≥π → emit mobStep
        float ambientTimer = 0.0f; // 到下次 idle 叫声的倒计时（秒）；≤0 → emit mobAmbient + 重置随机周期
        // t284 Stalker 蓄力 / 爆炸态（仅 mobType==MobStalker 用；其余 mob 留默认不触发）：
        //   fuseTimer：蓄力计时（秒）。aiStalker 在 kFuseRange 内每 tick 累加 dt；达 kFuseTime → detonateStalker
        //     爆炸 + 标 exploded。玩家逃出 kDefuseRange → 归零（机制等价 MC 苦力怕近距蓄力 / 远距熄火）。
        //     inflateAt 据 it 返 0..1（fuseTimer/kFuseTime）驱动 QML 膨胀动画。
        //   exploded：本 tick 已引爆（detonateStalker 置 true）。tick Mob 分支据此把实体入 toRemove（releaseSlot），
        //     并 continue 跳过后续重力 / resting（尸体即除，不再模拟）。爆炸当帧生效。
        float fuseTimer = 0.0f;   // 蓄力计时（秒；>0 = 正在蓄力膨胀；仅 MobStalker 用）
        bool  exploded  = false;  // 本 tick 已引爆（仅 MobStalker 用；detonateStalker 置 true 后当帧移除）
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
    // t321 玩家受击全局节流倒计时（秒；<=0 = 玩家可被命中；>0 = 节流无敌帧内，mob 攻击 / 骷髅箭命中不触发）。
    //   任一 mob 经 mobAttackedPlayer 命中玩家时置 kPlayerHitThrottle；tick 每帧扣 dt。详见 kPlayerHitThrottle 注释。
    //   解决「多 mob 围攻各独立冷却叠加 → 满血瞬死」：把 N 只 mob 的并行冷却串行化为单线节拍（轮替出手）。
    float m_playerHitCooldown = 0.0f;

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
    // speedScale：水平位移缩放（t298 水中减速；1.0 陆地、kWaterSpeedMul 水中）。透传给 mob 的水平移动，
    //   使在水中时既减位移又同步降低 moveSpeed（t241 腿摆频率随 moveSpeed，故水中腿也变慢 = 视觉上「挣扎」）。
    bool aiWander(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale = 1.0f);
    // t281 敌对生物 AI（detect→pathfind→attack 三段；tick 内 hostile Mob 分支调，替代 aiWander）。
    //   spec t281「敌对生物基类（AI/寻路）：detect player（4-5 格 or MC 规则）+ 寻路（向玩家走 + 跳/绕障，简化 A*）
    //   + attack」。机制对齐 MC 1.0 僵尸 / 骷髅近战 AI；标识符 / 美术全原创（§9 区隔）。
    //   (1) detect：XZ 距离 playerPos <= kDetectRange → chasing=true + 刷新 chaseTimer（脱离后记忆期内仍追）。
    //   (2) 非追踪 → 委托 aiWander（随机游荡，同 passive），追踪态在此之外独立处理。
    //   (3) 追踪：yaw 朝玩家 + 逐轴 AABB 碰撞撤回的水平移动（向玩家走）+ resting 且前方 1 格墙顶可落 → 跳（kJumpSpeed）。
    //       「简化 A*」= 贪心方向（直线朝玩家）+ 越障跳；非完整 A*（每帧多 mob 跑 A* 开销过大，且近战 mob 直线 + 跳够用）。
    //   (4) attack：XZ <= kAttackRange + |dy|<=kAttackVertRange + 冷却到 → emit mobAttackedPlayer(dmg, mobType)
    //       + 重置冷却（防每帧抽血）。mobType = Shambler/Bones（呈现层据此选攻击音 / 反馈，机制等价 MC）。
    //   返回是否真位移（驱动 dirty + moveSpeed）。worldW/worldD = 世界宽/深（边界 clamp 防 mob 走出世界）。
    //   playerPos = 玩家脚位（tick 的 listener = PlayerController::m_pos）。分层（PLAN §2）：只读 World::isSolid +
    //   自身数据；attack 走语义信号（mobAttackedPlayer）让呈现层路由到 PlayerState（同 fallDamageTaken 模式）。
    // speedScale 见 aiWander（t298 水中减速；追踪速度 / 内部回退 wander 一并缩放）。
    bool aiHostile(Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                   float speedScale = 1.0f);
    // t283 骷髅弓箭手 AI（detect→keep-distance→shoot 三段；tick 内 hostile mob 且 mobType==MobBones 分支调，
    //   替代 aiHostile 的近战 attack）。spec t283「远程射箭（arrow 实体 + 抛物 + 命中伤害；保持距离）」。
    //   机制对齐 MC 1.0 骷髅射手：检测玩家 → 在 [kArcherKeepMin, kArcherKeepMax] 距离带维持（近则退 / 远则进）→
    //   距离 + 视线 + 冷却满足 → 朝玩家解抛物初速射箭（fireArrow）；不进 aiHostile 的近战范围攻击。
    //   (1) detect + chase memory：同 aiHostile（kDetectRange 进追踪、脱离 kChaseMemory 秒放弃）。
    //   (2) 非追踪 → 委托 aiWander（随机游荡）。
    //   (3) 朝向：yaw 朝玩家（射箭方向 + 行走方向）。
    //   (4) 保持距离：distXZ<kArcherKeepMin → 朝远离方向走（kChaseSpeed）；>kArcherKeepMax → 朝玩家走；
    //       其间 → 原地持弓（不水平位移，仅朝向）。越障跳（同 aiHostile：前方 1 格墙 + 墙顶 2 格空气 → 跳）。
    //   (5) shoot：distXZ<=kArcherShootRange + |dy|<=kShootVertRange + 视线清（lineOfSightClear）+ 冷却到 →
    //       fireArrow + 重置冷却（防每帧连发）。
    //   返回是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。playerPos = 玩家脚位（tick 的 listener）。
    //   分层（PLAN §2）：只读 World::isSolid + 自身数据；shoot 走 spawnArrow（箭实体）+ 命中由 Arrow 分支发
    //   mobAttackedPlayer 语义信号让呈现层路由 PlayerState（同 aiHostile 的 attack 模式）。
    bool aiArcher(Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                  float speedScale = 1.0f);
    // t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕）AI（detect→chase→fuse→detonate；tick 内 hostile mob 且
    //   mobType==MobStalker 分支调，替代 aiHostile/aiArcher）。spec t284「近距蓄力膨胀动画 → 爆炸」。
    //   机制对齐 MC 1.0 苦力怕：检测玩家 → 缓慢逼近 → 进 kFuseRange 开始蓄力（站立不动 + 膨胀，机制等价 MC
    //   苦力怕近距嘶嘶蓄力）→ 蓄满 kFuseTime 引爆；玩家逃出 kDefuseRange → 熄火（fuseTimer 归零）。
    //   (1) detect + chase memory：同 aiHostile（kDetectRange 进追踪、脱离 kChaseMemory 秒放弃）。
    //   (2) 非追踪 → 委托 aiWander（随机游荡）。
    //   (3) 追踪：yaw 朝玩家；蓄力中（fuseTimer>0）→ 站立不动（moveSpeed=0，机制等价 MC 苦力怕蓄力时停步）；
    //       否则缓慢朝玩家走（kStalkerChaseSpeed，慢于玩家走速 → 可甩脱但有威胁）+ 越障跳（同 aiHostile）。
    //   (4) fuse：distXZ<=kFuseRange → fuseTimer+=dt（蓄力进度推进，inflateAt 据 it 驱动 QML 膨胀）；
    //       distXZ>kDefuseRange → fuseTimer=0（熄火）。蓄力中仍朝玩家（yaw 更新）但不位移。
    //   (5) detonate：fuseTimer>=kFuseTime → 调 detonateStalker（球形破坏方块 + 距离衰减伤害玩家 + emit
    //       explosion + 标 exploded）+ 本实体当帧移除（tick Mob 分支据 exploded 入 toRemove）。
    //   返回是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。playerPos = 玩家脚位（tick 的 listener）。
    //   分层（PLAN §2）：只读 World::isSolid/blockAt + 自身数据；爆炸破坏方块走 World::setWaterSilent（向下
    //   写栅格 + worldChanged 重建 mesh）；伤害玩家走 mobAttackedPlayer 语义信号（呈现层路由 PlayerState）。
    bool aiStalker(Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                   float speedScale = 1.0f);
    // t284 Stalker 爆炸（aiStalker fuse 满时调）：以 e.pos 为中心、kExplosionRadius 为半径的球内破坏方块
    //   （setWaterSilent 写 Air，跳过 Bedrock / Water / Air）+ 距离衰减伤害玩家（emit mobAttackedPlayer）+
    //   emit explosion（呈现层播爆炸音 / 迸发）+ 标 e.exploded=true（tick 当帧移除）。机制等价 MC 苦力怕爆炸。
    //   破坏方块走 setWaterSilent（静默写 + worldChanged 重建 mesh，**不**发 blockBroken → 免球形内每块破块
    //   粒子 / 音 spam；爆炸的音 / 视反馈由 explosion 信号单一入口驱动）。
    void detonateStalker(Entity &e, World *world, const QVector3D &playerPos);
    // t283 朝 target 解抛物初速并发射一支箭（aiArcher shoot 段调）。origin = shooter 中心 + 朝 target 前移
    //   0.5 格（避免贴墙时箭 spawn 入墙即没）。水平速度固定 kArrowSpeed → 飞行时间 t=d/vH；据 target 高度差
    //   反解 vy=(Δy+0.5·g·t²)/t（命中 target 高度的抛物解）；vy 钳到 ±kArrowMaxVert 防极端弧。三轴加 ±kArrowSpread
    //   随机抖动（MC 骷髅非 100% 精准；spread ≪ vH 不改飞行时间量级）。d 太小（<0.01）→ 安全早退（防除零）。
    void fireArrow(const Entity &shooter, const QVector3D &target);
    // t283 视线清查（aiArcher shoot 前调，防穿墙盲射）：从 from 到 to 沿连线 0.5 格步进采样，任一采样点所在
    //   格 isSolid → 视线被挡返 false。0.5 格步进足以抓 1 格墙（箭速 ~14 blocks/s、每帧 0.22 格，墙厚 ≥1）。
    //   分层：只读 World::isSolid（同 tick / aiHostile 越障查），不向下加依赖。
    bool lineOfSightClear(World *world, const QVector3D &from, const QVector3D &to) const;

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
    // t300 剪羊毛后吃草方块重新长毛的冷却 / 扫描常量（机制等价 MC 1.0「羊吃草方块重新长毛」；数值为本工程小
    //   世界量身调，非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）：
    //   - kRegrowCooldown：剪羊毛后到能开始吃草方块重新长毛的硬冷却（秒）。spec「加一个重新长毛冷却，免得刷屏」
    //     → 取 6.0（明显长于 eatCooldown=2.0，玩家剪完有充足窗口看到裸羊 + 拾取羊毛，6s 后才开始尝试长回）。
    //   - kRegrowScanInterval：sheared 羊扫描脚下草方块的间隔（秒，节流扫描开销）。脚下方块每秒扫一次足够
    //     （草方块到泥土的转换非瞬态，玩家肉眼可见的「重新长毛」事件）。
    static constexpr float kRegrowCooldown   = 6.0f;  // 剪羊毛后到能吃草方块重新长毛的硬冷却（秒；防刚剪即长回）
    static constexpr float kRegrowScanInterval = 1.0f; // sheared 羊扫描脚下草方块的间隔（秒；节流 blockAt 扫描开销）
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
    // t281 敌对 AI 常量（spec「detect player（4-5 格 or MC 规则）+ 寻路（向玩家走 + 跳/绕障，简化 A*）+ attack」；
    //   机制对齐 MC 1.0 僵尸 / 骷髅近战 AI：detect→pathfind→attack；数值为本工程小世界量身调，非 MC 精确复刻 ——
    //   PLAN §4「机制对标」非数值 1:1）。detect 取 MC 追踪距离量级（16）、attack 取 MC 简单难度僵尸伤害（3HP）。
    //   - kDetectRange：玩家侦测范围（blocks；XZ 距离）。MC 1.0 僵尸追踪 16-40 格；取下界 16（小世界不致满屏涌来）。
    //   - kChaseMemory：脱离侦测后仍追踪的秒数（机制等价 MC mob 短期记忆 —— 玩家短暂绕墙后 mob 不立即放弃）。
    //   - kChaseSpeed：追踪行走速度（blocks/s）。略慢于玩家走速 4.3 → 玩家可甩脱但具威胁；快于 wander kWalkSpeed=1.0。
    //   - kAttackRange / kAttackVertRange：近战攻击 XZ 距离 + 垂直容差。XZ 1.6 = 相邻一格可达；垂直 2.0 防跨层隔空打
    //     （mob 中心 vs 玩家脚位同层差 ~0.9，跳跃 / 上坡仍命中）。
    //   - kAttackCooldown：攻击间隔（秒；机制等价 MC 僵尸 ~1s/击）；kAttackDamage：单次伤害 HP（MC 正常难度僵尸 4 = 2 心）。
    //   - kJumpSpeed：越障跳跃初速（blocks/s；同 player jump 8.4 → 峰值 ~1.25 格，刚好翻 1 格墙；复用重力 kGravity 拉回）。
    static constexpr float kDetectRange     = 16.0f; // 玩家侦测范围（blocks；XZ）
    static constexpr float kChaseMemory     = 6.0f;  // 脱离侦测后追踪记忆（秒）
    static constexpr float kChaseSpeed      = 2.8f;  // 追踪行走速度（blocks/s；慢于玩家走速、快于 wander）
    static constexpr float kAttackRange     = 1.6f;  // 近战攻击 XZ 距离（blocks；mob 中心到玩家脚位）
    static constexpr float kAttackVertRange = 2.0f;  // 攻击垂直容差（blocks；|mobY - playerFeetY|；防跨层）
    static constexpr float kAttackCooldown  = 1.0f;  // 单 mob 攻击间隔（秒）
    static constexpr int   kAttackDamage    = 4;     // 单次攻击伤害（HP；MC 正常难度僵尸 4 = 2 心）
    // t321 玩家受击全局节流（秒；详见 kPlayerHitThrottle 注释）。单 mob 冷却只防自己连抽，多 mob 围攻时各 mob
    //   独立冷却叠加 → DPS 倍乘（4 只 × 4HP/s = 16HP/s ≈ 满血瞬死，玩家无反应窗口）。本节流在 EntityManager 全局
    //   层串行化「玩家被命中」：任一 mob（近战 attack / 骷髅箭命中）经 mobAttackedPlayer 命中后置 m_playerHitCooldown
    //   = kPlayerHitThrottle；其间其它 mob 攻击 / 命中不触发（机制等价「玩家受击无敌帧」+ 强制围攻 mob 轮替出手）。
    //   单只 mob 不受影响（其 1s 冷却 > 0.5s 节流，节流总先归零）；多只围攻时受击频率上限 = 1/0.5 = 2 次/s（与只数无关）
    //   → DPS 封顶 2×kAttackDamage = 8HP/s → 满血 10HP 至少 1.25s 反应窗口（走速 4.3 → 可移动 ~5 格脱离）。
    static constexpr float kPlayerHitThrottle = 0.5f; // 玩家受击全局节流（秒；防多 mob 围攻秒杀）
    static constexpr float kJumpSpeed       = 8.4f;  // 越障跳跃初速（blocks/s；同 player jump，翻 1 格墙）
    // t283 骷髅弓箭手远程 AI 常量（spec「远程射箭（arrow 实体 + 抛物 + 命中伤害；保持距离）」；机制对齐
    //   MC 1.0 骷髅射手：远距 + 抛物箭 + 保持距离 + 命中伤害；数值为本工程小世界量身调，非 MC 精确复刻 ——
    //   PLAN §4「机制对标」非数值 1:1）。
    //   - kArcherKeepMin / kArcherKeepMax：保持距离带（blocks；XZ）。玩家近于 min → 弓手后退；远于 max → 前进；
    //     其间 → 原地射击。MC 骷髅理想射距 ~10、近战时后退；取 [5, 9] 让玩家可逼近（弓手不会无限风筝）。
    //   - kArcherShootRange：开火 XZ 上界（blocks；<= 才射，且 < kDetectRange 16）。MC 骷髅射程 ~15；本工程取
    //     12 略小于 detect（贴脸到 12 都射，过 12 仅追不射 = 接近 MC「远距拉弓近距退」节律）。
    //   - kShootVertRange：开火垂直容差（blocks；|mobY - playerFeetY|；防跨层穿地板盲射）。复用近战同名量级。
    //   - kShootCooldown：射箭间隔（秒；机制等价 MC 骷髅 ~1.5-3s 拉弓间隔）。
    //   - kArrowSpeed：箭水平速度（blocks/s）。MC 箭速 ~ 存活 60 tick ≈ 3s 飞行；本工程取 14（约 1s 飞 14 格）
    //     让玩家有反应时间侧身躲避（spec「命中伤害」隐含可规避）。
    //   - kArrowMaxVert：vy 钳（blocks/s；防 fireArrow 抛物解在大水平距 / 大高差时解出极端弧 → 箭飞几秒才落）。
    //   - kArrowSpread：三轴初速随机抖动（blocks/s；MC 骷髅非 100% 精准）。取 1.2 ≪ vH=14 → 命中率 ~ 中近距高 / 远距低。
    //   - kArrowGravity：箭重力（复用 kGravity=28 → 与世界重力一致、抛物弧自然；非独立常量）。
    //   - kArrowLifetime：箭最长存活（秒；飞行未命中 / 未碰方块时兜底移除，防永久滞留堆积）。
    //   - kArrowDamage：命中玩家伤害（HP；MC 简单难度骷髅 1-2，取 2 = 1 心，对齐 t265 玩家攻击力量级）。
    //   - kArrowHitHalfW：箭 vs 玩家 AABB 命中检测的 XZ 外扩（blocks；箭是点，玩家 AABB 外扩此值做命中盒，
    //     提升近距命中率 / 玩家不致「贴脸箭穿过」）。
    static constexpr float kArcherKeepMin    = 5.0f;   // 保持距离下界（blocks；近则退）
    static constexpr float kArcherKeepMax    = 9.0f;   // 保持距离上界（blocks；远则进）
    static constexpr float kArcherShootRange = 12.0f;  // 开火 XZ 上界（blocks）
    static constexpr float kShootVertRange   = 4.0f;   // 开火垂直容差（blocks；防跨层盲射）
    static constexpr float kShootCooldown    = 1.6f;   // 射箭间隔（秒）
    static constexpr float kArrowSpeed       = 14.0f;  // 箭水平速度（blocks/s）
    static constexpr float kArrowMaxVert     = 18.0f;  // vy 钳（blocks/s；防极端弧）
    static constexpr float kArrowSpread      = 1.2f;   // 三轴初速随机抖动（blocks/s）
    static constexpr float kArrowLifetime    = 5.0f;   // 箭最长存活（秒；兜底移除）
    static constexpr int   kArrowDamage      = 2;      // 命中伤害（HP）
    static constexpr float kArrowHitHalfW    = 0.4f;   // 箭 vs 玩家命中盒 XZ 外扩（blocks）
    // t324 玩家自身箭自伤的发射者忽略窗口（spec「玩家自身箭下落伤害」；机制对齐 MC 1.0 箭出膛短时不伤发射者）。
    //   玩家射出的箭（arrowFromPlayer）飞行此秒数后才「武装」可命中玩家自己（防贴脸出膛误伤 —— 箭 spawn 在玩家
    //   外扩命中盒内，未武装前穿过不触发）。取 0.2s：远大于箭飞出玩家命中盒所需（~0.01s @ 14 blocks/s）、远小于
    //   最低蓄力朝天箭往返时间（~0.7s）→ 既不漏 point-blank 自伤、也不漏「朝天落箭砸自己」。
    static constexpr float kArrowSelfArmDelay = 0.2f;  // 玩家箭自伤武装延迟（秒；发射者忽略窗口）
    // t323 箭嵌入方块常量（spec「箭嵌在命中面（半嵌可见）+ 玩家箭可拾 + 嵌入箭 ~60s 消失」；机制对齐
    //   MC 1.0 箭命中方块嵌入可回收）。数值为本工程量身调，非 MC 精确复刻（PLAN §4 机制对标非数值 1:1）。
    //   - kStuckArrowLifetime：嵌入箭最长存活（秒；命中方块瞬间重置 → ~60s 后 despawn，防永久滞留堆积）。
    //   - kArrowEmbed：嵌入时箭心相对入射面的回退（blocks；正 → 心在面外侧、杆沿飞行方向伸入面内半嵌可见；
    //     箭杆长 ~0.55，取 0.2 → 尖入面内 ~0.35、杆尾露面外 ~0.2，半嵌观感）。
    static constexpr float kStuckArrowLifetime = 60.0f; // 嵌入箭存活（秒；despawn）
    static constexpr float kArrowEmbed         = 0.20f; // 嵌入回退（blocks；心在面外、尖入面内）
    // t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕）AI / 爆炸常量（spec t284「近距蓄力膨胀动画 → 爆炸（破坏方块
    //   + 伤害玩家 + 音效）」；机制对齐 MC 1.0 苦力怕：缓慢逼近 + 近距蓄力 + 球形爆炸 + 距离衰减伤害；数值为
    //   本工程小世界量身调，非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）。
    //   - kStalkerChaseSpeed：追踪行走速度（blocks/s）。慢于玩家走速 4.3 → 玩家可甩脱但具威胁；略慢于 Shambler
    //     kChaseSpeed=2.8（苦力怕移动迟缓是其标志性弱点）。
    //   - kFuseRange：开始蓄力的 XZ 距离上界（blocks；<= 才蓄力）。MC 苦力怕贴近 1-2 格引爆；取 1.8（mob 半宽
    //     0.45 + 玩家半宽 0.3 + 余量 → 中心距 ~1.8 时已贴脸）。
    //   - kDefuseRange：熄火的 XZ 距离（blocks；> 则 fuseTimer 归零）。MC 苦力怕玩家逃远即熄火；取 7（远于蓄力
    //     近距、近于侦测 16 → 玩家中距拉扯可熄火保命）。
    //   - kFuseTime：蓄力到引爆的时长（秒）。MC 苦力怕 ~1.5s 嘶嘶蓄力；取 1.5（玩家有反应时间侧身 / 后撤）。
    //   - kExplosionRadius：球形爆炸半径（blocks）。MC 苦力怕爆炸威力 3（半径 ~3）；取 3.0。
    //   - kExplosionDamageMax：贴脸（距离 0）爆炸伤害（HP）。MC 苻力怕正常难度贴脸 ~ 减护甲后仍致命；取 24
    //     （= 12 心，机制等价「贴脸必死、远距可存活」），随距离线性衰减到 0（半径边缘）。
    static constexpr float kStalkerChaseSpeed   = 2.6f;  // 追踪行走速度（blocks/s；慢于 Shambler，苦力怕迟缓）
    static constexpr float kFuseRange           = 1.8f;  // 开始蓄力的 XZ 距离上界（blocks）
    static constexpr float kDefuseRange         = 7.0f;  // 熄火的 XZ 距离（blocks；> 则 fuseTimer 归零）
    static constexpr float kFuseTime            = 1.5f;  // 蓄力到引爆的时长（秒；MC 苦力怕 ~1.5s）
    static constexpr float kExplosionRadius     = 3.0f;  // 球形爆炸半径（blocks）
    static constexpr int   kExplosionDamageMax  = 24;    // 贴脸爆炸伤害（HP；随距离线性衰减到 0）
    // t297 爆炸掉落：每个被爆炸破坏的方块以此概率掉落其物品实体（机制等价 MC 爆炸弹毁方块掉物；
    //   spec「~50% 成掉落物」）。掉落 id 走 BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落）。
    static constexpr float kExplosionDropChance = 0.5f;  // 破坏块掉落概率（~50%；MC 实为 1/radius≈33%，spec 取 50%）
    // t298 怪物受水流影响（spec「怪在水中正常走（错）→减速/浮（同玩家水中物理）」；机制等价玩家水中物理
    //   t174 浮力缓沉 + t159 水下减速 + t211 流水推动 —— mobs 不按空格故无 kSwimUp 上浮，仅被动缓沉）。
    //   数值与玩家同源（PlayerController kUnderwaterSpeedMul/kWaterGravity/kWaterSinkMax/kWaterFlowPush），保世界
    //   手感一致；非 MC 精确复刻（PLAN §4「机制对标」非数值 1:1）。分层（PLAN §2）：Entities 层只读
    //   World::blockAt/stateAt（脚位格是否水 / 流水 state），写自身实体态；无向上依赖。
    //   - kWaterSpeedMul：水中水平速度倍数（脚位在水格 → AI 行走 / 追踪位移 ×此值）。0.4 = 陆地的 40%。
    //   - kWaterGravity：水中等效重力（缓沉；远小于 kGravity=28 → mob 在水中缓慢下沉而非自由落体）。
    //   - kWaterSinkMax：水中最大下沉速度（钳制，防加速穿水底；远小于 kMaxFall=78.4）。
    //   - kWaterFlowPush：流水水平推力速度（脚位在流水格 state>0 时沿离源方向叠入水平位移，同玩家 t211）。
    static constexpr float kWaterSpeedMul = 0.4f;  // 水中水平速度倍数（同玩家 kUnderwaterSpeedMul）
    static constexpr float kWaterGravity  = 6.0f;  // 水中重力（缓沉；同玩家 kWaterGravity ≈ kGravity×0.21）
    static constexpr float kWaterSinkMax  = 3.0f;  // 水中最大下沉速度（钳制；同玩家 kWaterSinkMax）
    static constexpr float kWaterFlowPush = 4.0f;  // 流水水平推力（blocks/s；同玩家 kWaterFlowPush）
};

#endif // ENTITYMANAGER_H
