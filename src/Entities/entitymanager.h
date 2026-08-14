#ifndef ENTITYMANAGER_H
#define ENTITYMANAGER_H

#include <QObject>
#include <QString>
#include <QVector3D>
#include <QElapsedTimer>
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
    // 管）/ FallingBlock=贴图方块 / Arrow=箭矢投射物（t283 骷髅弓箭手远程射出，细长杆定向 Model）/
    // Snowball=雪球投射物（t482 雪傀儡远程攻击，白色小球定向 Model，低伤害 + 减速））。
    enum Kind { Mob, Item, FallingBlock, Arrow, Snowball };
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
    //   t399 鱿鱼（squid）= MobSquid(9)：机制等价 MC 1.0 squid —— 水生被动生物，水里游（aiSquid：周期喷水推进上浮 +
    //   水平漂游，离水则慢爬搁浅）。passive（hostile=false），死亡掉墨囊（InkSacId；呈现层 onMobDied 据本 enum 分流）。
    //   tick Mob 分支据 mobType==MobSquid 路由到 aiSquid（替代 aiWander）；水中物理复用通用 mob 水物理（speedScale
    //   减速 + kWaterGravity 缓沉 + 流水推动），aiSquid 在其上叠加周期 vy 上冲量 → 「喷水上浮 → 缓沉」节律性游动。
    //   t480 狼（wolf）= MobWolf(10)：机制等价 MC 1.0 狼 —— 驯服战斗伙伴。森林/针叶林群系生成（biomeIdAt==3 Forest
    //   / ==4 Snowy），中性 non-hostile（hostile=false → 不参与黑暗刷怪 / 日光燃烧 / 远距消失，生命周期同 passive）。
    //   未驯服狼攻击玩家（aiWolf 敌对分支，机制等价 MC 1.0 野狼攻击）；骨头右键概率驯服（~33%，kWolfTameChance）→
    //   驯服狼跟随主人 + 防御（主人攻击 / 主人受击来源的 mob → 狼追击咬击）+ 坐/站切换（右键坐留守 / 再右键站跟随）；
    //   喂生/熟肉 → love mode 繁殖产幼崽（复用 t400 框架，MobWolf 入 isBreedableType + 食物匹配表）。死亡不掉落
    //   （机制等价 MC 1.0 狼无常规掉落；仍掉少量 XP，见 Main.qml onMobDied）。§9 原创：名称 / 模型（方块化犬科 +
    //   立耳 + 尾巴）/ 贴图（程序生成灰狼毛皮）全原创，仅机制对齐「驯服 + 跟随 + 防御 + 繁殖」。尾巴角度示血量
    //   （QML 独立尾巴 Model 据 healthAt/maxHealthAt 旋转 —— 满血竖起、残血下垂，机制等价 MC 狼尾随血量升降）。
    //   t481 豹猫/猫（ocelot/cat）= MobOcelot(11)：机制等价 MC 1.0 豹猫 —— 丛林驯服伙伴。丛林群系生成
    //   （biomeIdAt==6 Jungle），中性 non-hostile（hostile=false → 不参与黑暗刷怪 / 日光燃烧 / 远距消失，生命周期
    //   同 passive）。未驯服豹猫被动游荡（aiOcelot 未驯服分支 → aiWander，不攻击玩家）；生鱼右键概率驯服
    //   （~33%，kOcelotTameChance）→ **变猫**：随机毛色变体（ocelotVariant 0..2，QML 据 it 切 3 色猫贴图）。
    //   驯服猫跟随主人 + 坐/站切换（空手右键坐留守 / 再右键站跟随，同狼模式）；**不防御**（机制等价 MC 1.0 猫
    //   不攻击怪物，与驯服狼防御咬击区分）。生鱼喂食 → love mode 繁殖产幼崽（复用 t400 框架，MobOcelot 入
    //   isBreedableType + tamed 守卫 + 幼崽继承毛色变体）。**驱赶 Stalker**：豹猫/猫在 kStalkerFleeRange 内 →
    //   Stalker 逃离（aiStalker 侧据 nearestOcelot 背离最近猫走 + 熄火不蓄力，机制等价 MC 1.0 苦力怕被猫吓跑）。
    //   死亡不掉落（机制等价 MC 1.0 豹猫/猫无常规掉落；仍掉少量 XP，见 Main.qml onMobDied）。§9 原创：名称 /
    //   模型（方块化猫科 + 尖耳 + 长尾）/ 贴图（程序生成斑点橙棕豹猫 + 3 色猫）全原创，仅机制对齐「丛林生成 +
    //   驯服变猫 + 跟随坐站 + 驱赶苦力怕 + 繁殖」。
    //   t482 雪傀儡（SnowGolem）= MobSnowGolem(12) / t483 铁傀儡（IronGolem）= MobIronGolem(13)：**防御造物**
    //   （机制等价 MC 1.0 雪傀儡 / 铁傀儡）—— 非 spawn 而是**玩家摆放方块触发生成**（placeBlock 放南瓜检测下方
    //   排列 → spawnMobTyped + 静默移除结构方块）。neutral non-hostile（hostile=false → 不参与黑暗刷怪 / 日光
    //   燃烧 / 远距消失，生命周期同 passive）。**攻击敌对 mob**（非玩家）：
    //     - 雪傀儡（aiSnowGolem）：抛雪球远程攻击（kind=Snowball 弹丸；低伤害 1HP + 轻微减速 slowTimer →
    //       mob 水平移动减速 kSnowSlowMul，QML isSlowedAt 显蓝调）；行走留雪层（走过的 air 格地面放 SnowLayer）；
    //       沙漠群系（热）/ 降水（雨雪）→ 融化（damageEntity 大伤害 → 死亡粒子链，机制等价 MC 雪傀儡沙漠/雨天
    //       融化消失）。
    //     - 铁傀儡（aiIronGolem）：大力攻击敌对（高伤害 kIronGolemAttackDamage + 击退 knockback，机制等价 MC
    //       铁傀儡重拳）；死亡掉落铁锭 / 罂粟（呈现层 onMobDied 据 mobType 分流，机制等价 MC 铁傀儡掉落铁锭 + 花）。
    //   §9 原创：名称 / 模型（南瓜头 + 方块身，Main.qml 用 BlockCube 贴图堆叠）/ 贴图全原创，仅机制对齐「搭建
    //   造物 + 防御攻击敌对」。计入实体槽（spawnMobCore 走 kCap 上限，勿超 64）。
    //   t487 银鱼（Silverfish）= MobSilverfish(14)：机制等价 MC 1.0 银鱼——小型虫类敌对生物，要塞（Stronghold）
    //   银鱼刷怪笼周期刷出。hostile=true → 走 tickHostileLife（白天暴露日光燃烧 / 黑暗刷怪调度）+ 默认 aiHostile
    //   （detect→chase→melee attack，近战追击玩家，区别于 Shambler 仅为小体型 + 快速）。§9 原创：名称 / 模型
    //   （MobModel 小型虫形：分节躯干 + 前伸小头 + 多对短腿）/ 贴图（程序生成灰白甲壳 + 体节纹）全原创，仅机制
    //   对齐「小虫群涌追击」。要塞 placeStronghold 在传送门房放银鱼刷怪笼（Spawner state 带 SpawnerStateSilverfishFlag）→
    //   tickSpawners 据该 flag 刷 Silverfish（区别于地牢 Shambler/Bones）。Entity hostilesCount / hostileNearby 均含 Silverfish。
    enum MobType { MobTest = 0, MobPig = 1, MobCow = 2, MobSheep = 3, MobShambler = 4, MobBones = 5, MobStalker = 6, MobSpider = 7, MobChicken = 8, MobSquid = 9, MobWolf = 10, MobOcelot = 11, MobSnowGolem = 12, MobIronGolem = 13, MobSilverfish = 14, MobTnt = 15 }; // t494：MobTnt=15 哨兵 mobType（非真实 mob —— 仅 TNT 爆炸 mobAttackedPlayer 传它区分死因「被 TNT 炸死」vs 潜行者自爆）
    Q_ENUM(MobType)

    // 生成默认测试生物（mobType=0、#ff5555、满血 kDefaultMaxHealth）。t239 调试入口（M 键）；t243 spawn eggs
    //   落地后由 spawnMobTyped 直接生成猪/牛/羊。位置存该格中心 (x+0.5, y+0.5, z+0.5)；从高处生成时由重力
    //   tick 落到地表。radius=0.5、pushable=true。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnMob(int x, int y, int z);
    // t239 生物基类统一生成入口（猪/牛/羊 + 后续 mob）：mobType=子类 id（0=通用测试生物；t240 pig/cow/sheep
    //   各自 id；掉落/模型据它分流）；color=渲染配色（QML delegate baseColor）；maxHealth=血量上限（≤0 用默认）。
    //   生成即满血、未死、AI wanderTimer=0（tick 首帧即选第一次向）。达 kCap → 跳过 + 告警（防溢出）。
    //   返槽索引（t529：caller 可据此设生成时朝向，见 spawnMobTypedYaw；QML 调用忽略返回值无妨）。达 kCap → -1。
    Q_INVOKABLE int spawnMobTyped(int x, int y, int z, int mobType, const QString &color, int maxHealth);
    // t529 spawnMobTyped 的「带生成时朝向」变体（spec「生成时固定朝」）：spawnMobCore 生成 → 设 yawRad
    //   yawRad（朝 caller 指定方向，典型 atan2(-dx,-dz) 朝玩家）→ 一次 emit（同 spawnMobTyped）。供 playercontroller
    //   build 雪傀儡 / 铁傀儡生成时面朝玩家用（让玩家初次见南瓜脸正脸；之后 aiWander 随机选向）。
    //   返槽索引（同 spawnMobTyped）；达 kCap → -1。
    Q_INVOKABLE int spawnMobTypedYaw(int x, int y, int z, int mobType, const QString &color, int maxHealth, float yawRad);
    // t280 黑暗刷怪：敌对生物生成入口（Shambler/Bones）。委托 spawnMobTyped（设 hostile=true / 配色 / 血量）
    //   后再翻 Entity.hostile（spawnMobTyped 是通用入口，不知哪些 mobType 是敌对；本入口收口敌对语义）。
    //   达 kCap → 委托内静默跳过。mobType 仅 MobShambler/MobBones 合法（其余当敌对调是非语义，但仍生成不崩）。
    Q_INVOKABLE void spawnHostileMob(int x, int y, int z, int mobType);
    // t374 被动生物群系化生成类型选取：据群系 id（World::biomeIdAt 编码：0=Plains, 1=Hills, 2=Desert,
    //   3=Forest）按 kPassiveSpawnWeights 加权随机返 MobPig/MobCow/MobSheep/MobChicken 之一。机制等价 MC 1.0
    //   群系化被动刷怪池（平原牛羊富集、森林猪富集；非排斥，仅概率差异）。群系 id 越界 → 兜底按 Plains。const 只读。
    //   分层（PLAN §2）：Entities 层，纯函数于入参（biome id）+ RNG 采样，不读 World / 不改实体数据。
    Q_INVOKABLE int pickPassiveMobType(int biomeId) const;
    // t280 当前**活体**敌对生物数（hostile=true 且非 dead 的 Mob）。供刷怪调度判总数上限（kHostileMobCap）。
    //   含 Shambler/Bones；不含 passive（pig/cow/sheep/test）与 FallingBlock/Item。
    Q_INVOKABLE int hostileCount() const;
    // t388 睡觉机制「床周有敌对即拒绝」判定：给定中心 center 与半径 radius（blocks），是否任一**活体敌对 mob**
    //   （alive && kind==Mob && hostile && !dead）在其 3D 球内。机制等价 MC 1.0 床周 8 格内有敌对生物即不能睡。
    //   const 只读自身数据；无实体 / 无命中 → false。由 PlayerController::trySleepAt（placeBlock useBlock 床分支）调。
    Q_INVOKABLE bool hostileNearby(const QVector3D &center, float radius) const;
    // t280 第 i 个实体是否**敌对**（hostile=true 的活体 Mob）。QML 据它对 Shambler/Bones 显燃烧火焰 Model
    //   （passive 永不燃烧 → 火焰仅敌对会显）。越界 / 非 hostile → false。
    Q_INVOKABLE bool isHostileAt(int i) const;
    // t280 第 i 个 mob 是否**正在燃烧**（火焰视觉）：hostile 暴露日光（tickHostileLife 写 Entity.burning）OR
    //   t344 火烧态（fireTimer>0，岩浆 / 火点燃；ALL mobs 含 passive）。QML 据 isBurningAt 显火焰 Model +
    //   baseColor 偏橙。t344 扩展：passive 着火亦显火焰（机制等价 MC 动物着火视觉）。越界 / 非 Mob → false。
    Q_INVOKABLE bool isBurningAt(int i) const;
    // t482 第 i 个 mob 是否**被雪球减速**（slowTimer>0）：雪傀儡雪球命中后短暂减速（水平移动 ×kSnowSlowMul，
    //   ~kSnowSlowDuration 秒）。QML 据 isSlowedAt 给 mob baseColor 叠蓝调（机制等价 MC 雪球减速可观察反馈）。
    //   越界 / 非 Mob / 未减速 → false。
    Q_INVOKABLE bool isSlowedAt(int i) const;
    // t117 沙子重力方块：在方块格 (x,y,z) 生成一个下落方块实体（携带 blockId）。位置存该格中心
    // (x+0.5, y+0.5, z+0.5)；pushable=false（不被玩家推动，同掉落物变体）；kind=FallingBlock；
    // blockId 存实体携带的方块 id（着地放置用它）。重力 tick 下落，着地时 world->setBlockFromEntity
    // 放置 blockId 并移除自身。链式塌落由调用方先把沙格置 air（经 World::setBlock → blockBroken →
    // 呈现层 onBlockBroken 递归触发上方沙）实现。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnFallingBlock(int x, int y, int z, int blockId);
    // t527 携带 state 的下落方块实体（积雪层专用）：与 spawnFallingBlock 同（位置 / halfW / halfH / kind=FallingBlock），
    //   额外写 e.blockState = state（着地 setBlockFromEntity(...,state) 写回雪层保留层数；呈现层 blockStateAt 据它缩放
    //   薄板高度）。caller = Main.qml onSnowLayerFell（World.snowLayerFell 信号转）传 blockId=SnowLayer + state=layers-1。
    //   达 kCap 跳过 + 告警（防溢出，同 spawnFallingBlock）。
    Q_INVOKABLE void spawnFallingBlockState(int x, int y, int z, int blockId, int state);
    // t283 箭矢投射物（骷髅弓箭手 MobBones 远程攻击）：在 origin 处生成一支携带初速度 vel（blocks/s，含 vy 抛物）
    //   的箭实体。kind=Arrow、pushable=false（玩家走碰不推箭）、halfW/halfH=0.06（细长杆视觉 + 碰撞最小）。
    //   tick 内 Arrow 分支：重力改 vy（抛物）+ 速度位移 + 方块碰撞（命中即移除）+ 玩家 AABB 碰撞（命中发
    //   mobAttackedPlayer(kArrowDamage, MobBones) + 移除）+ 寿命 / 边界超界兜底移除。呈现层 mobHost delegate 据
    //   kindAt==Arrow 走细长杆 Model + arrowYawAt/arrowPitchAt 定向。机制等价 MC 1.0 骷髅射箭（箭抛物 + 命中伤害）；
    //   名称 / 视觉全原创（§9 区隔，不照搬 MC 美术）。达 kCap → 跳过 + 告警（防溢出）。
    //   t480：返值 = 新箭槽索引（fireArrow 用它设 arrowShooter —— 骷髅箭命中玩家时驯服狼据发射者反击；
    //   无 QML 消费，仅 C++ fireArrow 用）；达 kCap → -1。
    Q_INVOKABLE int spawnArrow(const QVector3D &origin, const QVector3D &vel);
    // t304 玩家弓射出的箭（spec「松开射箭（抛物+伤害 mobs）」）：与 spawnArrow（骷髅射出，命中玩家）对称，
    //   差异在 arrowFromPlayer=true（命中 mob 而非玩家）+ arrowDamage 由弓蓄力决定（1..6 HP，caller 传）。
    //   命中 mob 走 damageEntity（扣血 + 红闪 + 归零 mobDied 死亡掉落）+ emit mobAttacked(mobType, false)
    //   （呈现层 playMobHurt；同玩家近战 attackMob 路径）。机制等价 MC 1.0 玩家弓箭打怪（敌我判别由发射者定）。
    //   origin = 玩家眼位 + 视线前移 0.5（防贴墙 spawn 入墙即没）；vel = 视线方向 × 蓄力速度（含抛物 vy）。
    //   达 kCap → 跳过 + 告警（防溢出，同 spawnArrow）。
    Q_INVOKABLE void spawnArrowPlayer(const QVector3D &origin, const QVector3D &vel, int damage);
    // t482/t505 雪球投射物（雪傀儡 aiSnowGolem 远程攻击 / t505 玩家右键抛掷）：在 origin 处生成一个携带初速度 vel
    //   （blocks/s，含 vy 抛物）的雪球实体。kind=Snowball、pushable=false（玩家走碰不推）、halfW/halfH=0.10
    //   （白色小球视觉 + 碰撞最小）。tick 内 Snowball 分支：重力改 vy（抛物）+ 速度位移 + 方块碰撞（命中即碎 →
    //   emit snowballBreak 粒子 + 移除）+ **活体 mob** AABB 碰撞（命中发 damageEntity(damage) + 设目标 mob
    //     slowTimer=kSnowSlowDuration（轻微减速，QML isSlowedAt 显蓝调）+ 击退 + 移除）。
    //   **damage 按发射者分流**（t505 机制对标 MC 1.0：雪傀儡雪球对敌对有伤害 / 玩家雪球对 mob 0 伤害只触发红闪 +
    //     击退）。caller 传：雪傀儡 fireSnowball 传 kSnowballDamage（敌对伤害）；玩家右键抛（playercontroller）
    //     传 0（0 伤害但 damageEntity 仍发 hurtFlash 红闪 + knockback 击退）。
    //   **t553 命中所有活体 mob**（含猪牛羊等被动 —— 机制对标 MC 1.0 雪球打任意生物都击退；仅敌对扣血 / 红闪，
    //   被动 0 伤害 0 红闪）+ 击退强度 kSnowballKnockbackStrength（自 1.0 提至 2.0 修「不击退」）。发射者自身排除
    //   （thrower 参数 = 雪傀儡槽 idx / 玩家 -1，防首帧误击自己）。玩家（PlayerController）非 Mob 实体穿过。
    //   机制等价 MC 1.0 雪傀儡抛雪球（远程弹丸 + 伤害 + 减速）/ MC 1.0 玩家抛雪球（无伤 + 击退 + 红闪）；
    //   名称 / 视觉全原创（§9 区隔）。达 kCap → 跳过 + 告警（防溢出）。返新雪球槽索引（调试用）；达 kCap → -1。
    Q_INVOKABLE int spawnSnowball(const QVector3D &origin, const QVector3D &vel, int damage, int thrower = -1);
    // t176 存档：清空所有实体（切世界 / 退出存档前调，防上一世界的 mob / 下落方块残留进新世界）。
    //   t437：改「释放全部活体槽位」而非「清空 vector」。根因：旧 m_entities.clear() 把 count→0，QML
    //   Repeater count 随之→0；但 reparent 进 mobHost 的 3D delegate（QQuick3DNode，非 QQuickItem）不进
    //   QQuickRepeater 跟踪表、所有权已转给 mobHost → Repeater 找不到 delegate 销毁 → delegate 永久挂在
    //   mobHost 下成孤儿（lessons-learned t170）。每次退存档→再进都把上一世界全部 mob delegate 孤儿化 +
    //   新世界从 0 重建 → 跨世界单调累积（每 delegate 含 MobModel + 多 mob-type Model + 眼/火舌子树 + 动画
    //   = 数十 3D 对象）→ 内存只增不减、FPS 掉到个位数（"退存档再进仍卡"的直接根因；C++ 审计全 clean，泄漏
    //   在 QML 场景图侧；t256 已用 slot-reuse 修了「游玩期掉落沙」却漏了「切世界 clearAll」这一断点）。改释放
    //   槽位：alive=false + 入 free list + liveCount=0，但**保留 vector** → count 不降 → Repeater 不销毁 delegate
    //   （无孤儿）→ 下次进世界复用既有 delegate（aliveAt 翻回 true + revision bump 重绑新世界数据）。高水位受 kCap
    //   钳制（≤64 槽），属有界常驻开销，远优于跨世界无界泄漏。仅释放活体槽（已释放的跳过 → 幂等、保 liveCount /
    //   free list 一致）。emit entitiesChanged → QML 据 revision 把释放槽 delegate 翻 visible=false 隐藏（不销毁）。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_entities.size(); ++i)
            if (m_entities[i].alive) releaseSlot(int(i));
        emit entitiesChanged();
    }

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
    // t527：第 i 个实体携带的方块 state（仅 FallingBlock && blockId==SnowLayer 用 = 积雪层层数 metadata）。
    //   呈现层据它缩放 falling 雪层薄板高度（1/8..1.0；state 0..7）。非 SnowLayer FallingBlock / 越界 → 0。
    Q_INVOKABLE int blockStateAt(int i) const;
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
    // t510 第 i 只 mob 是否**雪傀儡且已被剪南瓜头**（snowGolemSheared=true）。仅 mobType==MobSnowGolem 用（其余
    //   mob 恒 false）。QML delegate 据它切换雪傀儡外观：未剪=南瓜头 + 刻面眼/嘴；已剪=无头 derpy 形态（眼/嘴
    //   悬浮原头位，机制等价 MC 1.0「剪后变无头形态带眼不死的 derpy 版」）。PlayerController 剪刀分支据它判是否
    //   可剪（已剪不再可剪，防刷屏）。越界 / 非 SnowGolem → false。revision 在剪南瓜头时 bump 让 QML 绑定刷新
    //   （同 shearedAt 模式）。
    Q_INVOKABLE bool snowGolemShearedAt(int i) const;
    // t510 剪雪傀儡南瓜头（spec「玩家持剪刀右键雪傀儡 → 南瓜掉落 + 雪傀儡变无头 derpy 形态」；机制等价 MC 1.0
    //   剪刀剪雪傀儡南瓜头）。第 i 只**未剪南瓜头的活体 SnowGolem** → 翻 snowGolemSheared=true + emit snowGolemSheared(坐标)
    //   让呈现层 Connections 转发到 ItemEntityManager.spawnItem 生成南瓜方块掉落实体（BlockRegistry::Pumpkin，
    //   同 sheepSheared→spawnItem 模式；单向事件流，分层：Entities 层发语义事件、呈现层只消费）。已剪南瓜头 /
    //   非 SnowGolem / dead / 越界 → 静默早退（机制等价 MC 1.0：剪南瓜头只对戴头的雪傀儡生效，已无头的右键无反应）。
    //   bump revision → QML 翻雪傀儡为无头 derpy 外观。Q_INVOKABLE 兼调试 + PlayerController 剪刀分支双入口
    //   （playercontroller 是 C++ 直调）。
    Q_INVOKABLE void shearSnowGolem(int i);
    // t400 触发求偶期（spec「喂对应食物 → 求偶 → 同种配对产幼崽」；机制等价 MC 1.0 breeding 的 love mode）。
    //   第 i 个**成体**可繁殖 mob（pig/cow/sheep/chicken）+ 非冷却 + 未在求偶 → 进求偶期（loveTimer=kLoveDuration）
    //   + bump revision（QML 显心）+ 返 true。幼崽 / 冷却中 / 已求偶 / 非可繁殖 mob / dead / 越界 → 返 false。
    //   **食物匹配**（牛/羊=小麦 / 猪=胡萝卜·马铃薯 / 鸡=种子）由 caller（PlayerController，Game 层）判 —— 物品 id
    //   属 Game 层（RecipeRegistry），Entities 层不向上依赖（PLAN §2）。caller 先据 mobTypeAt + 持物判匹配，再调本方法。
    //   Q_INVOKABLE 兼调试 + PlayerController placeBlock 食物分支双入口（playercontroller 是 C++ 直调）。
    Q_INVOKABLE bool enterLoveMode(int i);
    // t479 幼崽喂食加速成长（spec「喂幼崽对应繁殖食物 → 加速长大」；机制等价 MC 1.0 喂幼崽减 ~10% 剩余成长时间）。
    //   第 i 个**幼崽**可繁殖 mob（pig/cow/sheep/chicken）+ 非 dead → growTimer 减 kBabyFeedGrow（≈kBabyGrowTime 的
    //   10%）加速长大 + 返 true。非幼崽 / 非可繁殖 mob / dead / 越界 → 返 false（caller 不消耗食物）。
    //   **食物匹配**由 caller（PlayerController，Game 层）判（同 enterLoveMode：物品 id 属 RecipeRegistry，Entities 层
    //   不向上依赖，PLAN §2）。caller 先据 isBabyAt + 持物判「是否幼崽 + 食物匹配该物种」——幼崽 → 调本方法（加速
    //   成长）；成体 → 调 enterLoveMode（求偶）。二者互斥分流：幼崽不可求偶（enterLoveMode 守卫返 false）、成体
    //   无成长可加（feedBaby 守卫返 false），机制等价 MC「喂幼崽加速生长、喂成体进 love mode」。不查 breedCooldown
    //   （幼崽无繁殖冷却；喂食只加速成长，同 MC）。bump revision + emit → 喂食是状态变更，通知纪律同 enterLoveMode。
    Q_INVOKABLE bool feedBaby(int i);
    // t400 第 i 个 mob 是否处于求偶期（loveTimer>0）。QML delegate 据它显心形 Model（繁殖可观察反馈 ——
    //   玩家喂食后立即见心，确认求偶已触发）。非 mob / 越界 / 未求偶 → false。
    Q_INVOKABLE bool inLoveAt(int i) const;
    // t400 第 i 个 mob 的模型缩放（幼崽 kBabyScale=0.5 / 成体 1.0）。QML delegate Node scale 绑它 → 幼崽半大
    //   （机制等价 MC 幼崽体型小）。非 mob / 越界 → 1.0。revision 在长大（baby→false）时 bump 让 QML 重缩。
    Q_INVOKABLE float babyScaleAt(int i) const;
    // t479 第 i 个 mob 是否**幼崽**（baby=true）。PlayerController 喂食分流据它（幼崽 → feedBaby 加速成长 /
    //   成体 → enterLoveMode 求偶）；呈现层守卫也可据它（幼崽死亡不掉落，见 mobDied 的 wasBaby 参数）。非 Mob /
    //   越界 → false。
    Q_INVOKABLE bool isBabyAt(int i) const;
    // t400 当前**可繁殖**被动 mob 数（pig/cow/sheep/chicken 的成体 + 幼崽，alive 且非 dead）。供繁殖上限判定
    //   （kPassiveMobCap；达上限 → 配对不再产幼崽，防种群爆炸，spec「种群上限」）。hostile / MobTest /
    //   MobSquid / FallingBlock / Item 不计。const 只读自身数据。
    Q_INVOKABLE int passiveBreedableCount() const;
    // t480 第 i 只 mob 是否**已驯服狼**（wolfTamed=true）。仅 mobType==MobWolf 用（其余 mob 恒 false）。QML delegate
    //   据它切狼外观 / 行为态（未驯服=攻击玩家、驯服=跟随+防御）；PlayerController 骨头驯服 / 肉食繁殖分流读它。
    //   越界 / 非 wolf → false。
    Q_INVOKABLE bool wolfTamedAt(int i) const;
    // t480 第 i 只驯服狼是否**坐着**（wolfSitting=true；留守原地不跟随不攻击）。仅驯服狼用（未驯服恒 false）；QML
    //   delegate 据它显坐姿（压缩 + 后倾）+ 行为（aiWolf 坐态留守）。越界 / 非驯服狼 → false。
    Q_INVOKABLE bool wolfSittingAt(int i) const;
    // t480 骨头驯服（spec「右键概率驯服 ~33%」）：第 i 只**未驯服**活体狼 → ~33% 概率驯服（wolfTamed=true）+
    //   bump revision（QML 收攻击态、转跟随态）+ 返 true；未中（~67%）→ 返 false（**骨头仍消耗**，机制等价 MC
    //   喂骨无论成败都消耗）。已驯服 / 非 wolf / dead / 越界 → 返 false（caller 不消耗骨头）。Q_INVOKABLE 兼调试 +
    //   PlayerController placeBlock 骨头分支双入口。
    Q_INVOKABLE bool tameWolf(int i);
    // t480 坐/站切换（spec「驯服狼右键坐 → 再右键站」）：第 i 只**已驯服**狼 → 翻转 wolfSitting + bump revision
    //   （QML 切坐姿/站姿 + aiWolf 切留守/跟随）。未驯服 / 非 wolf / dead / 越界 → 静默 no-op（野狼右键无反应，
    //   机制等价 MC 只有驯服狼可命令坐/站）。Q_INVOKABLE 兼调试 + PlayerController 骨头分支双入口。
    Q_INVOKABLE void toggleWolfSit(int i);
    // t481 第 i 只 mob 是否**已驯服猫**（ocelotTamed=true）。仅 mobType==MobOcelot 用（其余 mob 恒 false）。QML
    //   delegate 据它切豹猫/猫外观（未驯服=斑点橙棕豹猫、驯服=3 色猫）+ 行为态（驯服=跟随+坐/站）；PlayerController
    //   生鱼驯服 / 繁殖分流读它。越界 / 非 ocelot → false。
    Q_INVOKABLE bool ocelotTamedAt(int i) const;
    // t481 第 i 只驯服猫是否**坐着**（ocelotSitting=true；留守原地不跟随）。仅驯服猫用（未驯服恒 false）；QML
    //   delegate 据它显坐姿（压缩 + 后倾）+ 行为（aiOcelot 坐态留守）。越界 / 非驯服猫 → false。
    Q_INVOKABLE bool ocelotSittingAt(int i) const;
    // t481 第 i 只驯服猫的毛色变体（0=黑 / 1=姜黄 / 2=奶油；驯服瞬间随机选，QML 据它选 mob_cat_* 贴图）。
    //   未驯服 / 非 ocelot → 0（走 mob_ocelot 豹猫贴图，不读变体）。越界 → 0。
    Q_INVOKABLE int ocelotVariantAt(int i) const;
    // t481 生鱼驯服（spec「生鱼驯服 → 变猫（3 毛色变体随机）」；机制等价 MC 1.0 豹猫生鱼驯服 ~1/3）：
    //   第 i 只**未驯服**活体豹猫 → ~kOcelotTameChance 概率驯服（ocelotTamed=true + 随机毛色变体 0..2）+
    //   bump revision（QML 收豹猫外观、转猫外观 + 跟随态）+ 返 true；未中（~2/3）→ 返 false（**生鱼仍消耗**，
    //   机制等价 MC 喂鱼无论成败都消耗）。已驯服 / 非 ocelot / dead / 越界 → 返 false（caller 不消耗生鱼）。
    //   Q_INVOKABLE 兼调试 + PlayerController 生鱼分支双入口。
    Q_INVOKABLE bool tameOcelot(int i);
    // t481 坐/站切换（spec「驯服猫坐/站（同狼模式）」；机制等价 MC 1.0 驯服猫右键坐/站）：第 i 只**已驯服**猫 →
    //   翻转 ocelotSitting + bump revision（QML 切坐姿/站姿 + aiOcelot 切留守/跟随）。未驯服 / 非 ocelot / dead /
    //   越界 → 静默 no-op（野豹猫右键无反应，机制等价 MC 只有驯服猫可命令坐/站）。Q_INVOKABLE 兼调试 +
    //   PlayerController 空手分支双入口。
    Q_INVOKABLE void toggleOcelotSit(int i);
    // t480 设置驯服狼的防御目标（主人攻击的 mob；C++ 直调，PlayerController::attackMob 命中后调，Game→Entities
    //   向下依赖）。**共享目标**：所有驯服且站立的狼都追击它（机制等价 MC 1.0 驯服狼群攻主人攻击的目标）。
    //   索引经 slot-reuse 稳定（release 不 shift）；目标死亡 / 移除由 aiWolf 每 AI tick 校验清除。越界 → 忽略。
    void setWolfTarget(int idx) { if (idx >= 0 && idx < int(m_entities.size())) m_wolfTarget = idx; }
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
    // t485 TNT 方块爆炸（playercontroller scanTntTraps 触发——玩家踩压力板、板下垫 TNT 即引爆时调）。机制等价
    //   MC 1.0 TNT 爆炸：以 (x,y,z) TNT 格为中心、kExplosionRadius 为半径的球内破坏方块（destroySphereSilent
    //   一次收口 N 写 + 1 次 refloodBox + 1 次 worldChanged + 1 次 clearAllDirty，同 Stalker t320 批量收口模式）
    //   + 距离衰减伤害玩家（emit mobAttackedPlayer，仅 Survival 应用）+ emit explosion（呈现层播爆炸音 / 迸发）+
    //   按概率 emit explosionDroppedItem（破坏块掉落，同 Stalker t297）。与 detonateStalker 的差异：无 mob 实体
    //   （TNT 是方块）→ 不标 exploded / 不 releaseSlot / 不注册驯服狼防御目标。分层（PLAN §2）：向下写 World
    //   （destroySphereSilent）+ 发语义信号；只读 World::blockAt。无向上依赖。
    void detonateTntBlock(int x, int y, int z, World *world, const QVector3D &playerPos);
    // t490 生成 PrimedTnt（引燃态 TNT 实体；机制等价 MC 1.0 primed TNT）。复用 FallingBlock kind + blockId=TntBlock
    //   + primed=true + fuse=fuseSec（caller 传，默认 kPrimedTntFuseSec ~5s）。位置存 (x+0.5, y+0.5, z+0.5)。
    //   **非完整方块 + 可穿透 + 可堆叠**（spec）：halfW/halfH=0（玩家碰撞跳过 → 可穿过）、pushable=false、不查占用
    //   （同格可叠多个 PrimedTnt）。tick FallingBlock 分支据 primed 走 fuse 倒计 → 到 0 detonatePrimedTnt 引爆。
    //   fuseJitter = 引信错峰随机量（秒；0 = 无抖动）。链式引爆时传小随机 fuseJitter 避免同帧全部引爆（错峰）。
    //   分层（PLAN §2）：Entities 层自持实体数据 + acquireSlot；无向下依赖。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnPrimedTnt(int x, int y, int z, float fuseSec = -1.0f);
    // t490 引爆 PrimedTnt（tick fuse 到 0 调；机制等价 MC 1.0 TNT 爆炸）。与 detonateTntBlock 同源（球形破坏 +
    //   引燃邻接 TNT 链式 + 衰减伤玩家 + explosion 音/视），差异：中心是 PrimedTnt 实体 pos（floor）而非 TNT 方块格，
    //   引爆后实体移除（releaseSlot）。idx = PrimedTnt 槽索引；world/playerPos 由 tick 传入。
    void detonatePrimedTnt(int idx, World *world, const QVector3D &playerPos);
    // t490 私有：以 (cx,cy,cz) 为中心的 TNT 球形爆炸公共主体（detonateTntBlock 方块路径 + detonatePrimedTnt 实体
    //   路径共用）。① destroySphereSilent 球形破坏（N 写 1 emit）；② 爆炸掉落（~50%/块，同 Stalker）；③ 链式引燃
    //   （spec t490 验收核心）：destroyed 列表中 oldId==TntBlock 的格子 spawnPrimedTnt（fuse=Jitter 随机错峰）→
    //   各 PrimedTnt fuse 到 0 再次 detonatePrimedTnt 引爆其球内 TNT，递归连锁引爆全部（踩沙漠神殿压力板 → 3×3 连锁全爆）；
    //   ④ 距离衰减伤玩家 + 击退；⑤ emit explosion（音/视单一入口）。分层：向下写 World（destroySphereSilent）+ 发语义信号。
    void detonateTntSphere(int cx, int cy, int cz, World *world, const QVector3D &playerPos);
    // t490 第 i 个实体是否 PrimedTnt（kind==FallingBlock && primed）。QML delegate 据它对 FallingBlock 叠白闪脉冲
    //   （primed=true → baseColor 白闪；false → 普通下落方块原色）。越界 / 非 primed → false。
    Q_INVOKABLE bool isPrimedAt(int i) const;
    // t490 第 i 个 PrimedTnt 的引信进度（0..1，1=刚点燃、0=即将引爆）。QML delegate 据它驱动白闪脉冲频率（频率随
    //   fuse 减少加速，机制等价 MC TNT 引信将尽时闪烁加快）。越界 / 非 primed → 0。
    Q_INVOKABLE float fuseProgressAt(int i) const;
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
    // t331 骸骨拉弓瞄准进度（0..1）：仅 mobType==MobBones 且 aimTimer>0（正在拉弓瞄准）时返
    //   clamp(aimTimer/kAimWindup,0,1)，供 QML delegate 据 it 驱动 MobModel 肩枢 Node 抬右臂 + MobBowGeometry 弦后拉
    //   （机制等价 MC 1.0 骷髅停步拉弓瞄准）。aiArcher 射程内 + 视线清 + 冷却到 → 累加 aimTimer；满 kAimWindup 才射
    //   （拉弓期位移减速到停 = SLOWS + pauses to aim，顺带拉低 t321 攻击节奏）。非 Bones / 未瞄准 / 越界 → 0。
    //   revision 在追踪期每帧 bump（tick Mob 分支）让绑定刷新（拉弓期 mob 减速到停 → moved=false 亦须刷新）。
    Q_INVOKABLE float drawAmountAt(int i) const;
    // t377 第 i 个 mob 的护甲物品 id（piece 0=头盔 / 1=胸甲 / 2=护腿 / 3=靴子；0=该部位无护甲）。
    //   仅 Shambler/Bones spawn 时随机分配（~80% 无 / ~20% 一件或一套）；QML delegate 据 it 叠加 tier 色
    //   护甲 Model（material-colored，机制等价 MC 1.0 僵尸/骷髅随机护甲）。越界 → 0。
    Q_INVOKABLE int mobArmorAt(int i, int piece) const;
    // t249 受击击退（spec「受击往攻击方向小跳击退」；C++ 直调，PlayerController::attackMob 命中后调）：
    //   给第 i 个 mob 一个水平方向 (dirX,dirZ) 的击退冲量（vx/vz=kKnockbackHoriz 沿方向）+ 小跳垂直速度
    //   （vy=kKnockbackUp 向上）；解除 resting 让 tick 重力分支处理上跳→减速→下落→着地（小弹起观感）。
    //   方向 (dirX,dirZ) 由 caller 传「玩家→mob」水平向量（knockback 内再归一 + 零向量防御）。机制等价
    //   MC 1.0 knockback：受击实体沿攻击方向被推开 + 小幅上弹（不旋转、无受击硬直打断 AI，仅速度叠加）。
    //   非 Mob / dead（尸体不被推，同 resolvePlayerPush）/ 越界 → 静默早退。bump revision → QML 位置绑定刷新。
    //   分层（PLAN §2）：与 damageEntity 同层（Entities），只改自身数据，无向下依赖；由 Game/Physics 层调。
    //   t476 strength（缺省 1.0）：击退冲量倍率。玩家「击退」附魔命中时传 >1 → 拉大击退距离（机制等价 MC
    //     knockback 附魔 +击退量）。冲量 = kKnockbackHoriz * strength 沿方向。
    void knockback(int i, float dirX, float dirZ, float strength = 1.0f);
    // t476 点燃 mob（玩家「燃焰」FireAspect 命中触发；机制等价 MC fire-aspect ignite on hit）：把第 i 个 mob 的
    //   fireTimer 刷到至少 duration 秒（取 max，不覆盖更长的已有燃烧）。fireTimer>0 → tick 火烧分支按既有时序
    //   扣血（fireDamageTimer→damageEntity）+ 视觉火焰（isBurningAt），且致死时 mobDied 带 burned=true 掉熟肉。
    //   非 Mob / dead / 越界 / duration<=0 → 静默早退。bump revision（QML 显火焰 Model）。
    Q_INVOKABLE void ignite(int i, float duration);

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
    // t392 刷怪笼周期刷怪（C++ 直调；PlayerController::tickImpl 每 tick 调，与 tickHostileLife 同级）。
    //   独立于玩家捕获态（菜单 / 暂停时仍推进 —— 玩家在范围内时刷怪笼照样刷，世界模拟连续；同 tickHostileLife）。
    //   机制等价 MC 1.0 刷怪笼（mob spawner）：玩家在 kSpawnerPlayerRange 内 + 该笼周 kSpawnerMobCheckRadius 内敌对
    //   数 < kSpawnerLocalCap + 全局 hostileCount < kHostileMobCap 时，周期 spawn 1 只敌对（Shambler/Bones 等概率）。
    //   **player-near 才扫**：内部 m_spawnAccumSpawner 节流（kSpawnerInterval 秒一次），满 → 扫玩家所在格周围
    //   ±kSpawnerScanRange 的立方体找 Spawner 方块（按需扫描，玩家不在范围 → 不扫 → 远场零开销），对每个找到的
    //   笼：玩家 XZ 距离 ≤ kSpawnerPlayerRange + 笼周敌对 < kSpawnerLocalCap + 全局敌对 < kHostileMobCap + 找到合法
    //   空气 spawn 位 → spawn 1 只敌对（找邻 8 格中首个「air + 下方 solid」的格子；全堵 → 跳过本笼）。
    //   **破笼即停**：tickSpawners 每周期读 blockAt 判格 == Spawner，玩家破坏后下次扫描自然跳过（无 setBlock 钩子，
    //   同 spec「spawner ... can be broken to stop」）。
    //   分层（PLAN §2）：Entities 层（同 tickHostileLife）只读 World（blockAt/isSolid）+ 自身实体数据；写
    //   EntityManager 自身（spawnHostileMob）。playerPos 由 Game 层（PlayerController）传入。无向上依赖。world==null → 早 return。
    void tickSpawners(qreal dt, World *world, const QVector3D &playerPos);

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
    // t239 mob 死亡一次性事件。t449：**延迟到 deathTimer 归零**（≈500ms 倒地动画播完）才发，而非 damageEntity
    //   致死瞬间 —— 给「侧倒 + 白烟 → 掉落」的 MC 式过渡（旧实现红闪与掉落同帧太急）。damageEntity 致死时仅
    //   置 dead=true + deathTimer + 快照 deathBurned；tick 死亡态分支 deathTimer≤0 时 emit 本信号 + releaseSlot。
    //   坐标 = 死亡格 floor(pos)（dead 态 pos 冻结，与致死瞬间同位），mobType = 子类 id（0=通用 / t240 pig/cow/sheep）。
    //   t242 据它 + mobType 决定掉落物（猪:生猪排 / 牛:皮革+牛肉 / 羊:羊毛）→ 呈现层转发 ItemEntityManager.spawnItem
    //   （同 fallingBlockDropped 模式）。t344 burned = 致死时刻 mob 是否处于火烧态（fireTimer>0，触碰岩浆 / 火点燃）：
    //   true → 呈现层 onMobDied 据此把被动生物的「生肉掉落」替换为熟肉（猪→熟猪排 / 牛→熟牛肉 / 羊→熟羊肉；
    //   机制等价 MC 1.0 着火死亡掉熟肉）。仅 fireTimer>0 触发（日光 burning 仅敌对、不掉肉故不参与）。
    //   分层（PLAN §2）：Entities 层发语义事件，呈现层只消费，绝不反向写栅格。
    //   t479 wasBaby = **致死瞬间**快照（Entity.deathBaby）—— 幼崽死亡不掉落（呈现层 onMobDied 守卫跳过战利品 +
    //   XP，机制等价 MC 幼崽不掉落）。0.5s 死亡动画窗口内 tickBreeding 仍衰减 growTimer，幼崽可能在其中长大
    //   （baby→false），故延迟 emit 时读 e.baby 会漏判；快照保「致死时是幼崽」语义稳定（同 deathBurned 快照模式）。
    void mobDied(int x, int y, int z, int mobType, bool burned, bool wasBaby);
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
    // t510 雪傀儡剪南瓜头（shearSnowGolem 内发，仅未剪南瓜头的活体 SnowGolem 首次翻 snowGolemSheared=true 时发）。
    //   坐标 = 雪傀儡当前格 floor(pos)（与 spawnItem 整数格约定一致，便于 ItemEntityManager 落在它身旁）。
    //   呈现层（Main.qml）Connections 据它转发 ItemEntityManager.spawnItem(100=Pumpkin, 1)（南瓜方块掉落实体；
    //   同 sheepSheared→spawnItem 模式；单向事件流，PLAN §2 分层：Entities 层发语义事件、呈现层只消费，绝不
    //   反向写栅格）。机制等价 MC 1.0 剪刀剪雪傀儡南瓜头 → 南瓜掉落 + 雪傀儡变无头 derpy 形态（不死，仅外观变化）。
    void snowGolemSheared(int x, int y, int z);
    // t505 雪球撞方块破碎（spec「砸地面 → 破碎动画消失」）：Snowball 命中方块（地面 / 墙）时发。坐标 = 雪球
    //   命中点（float 世界坐标，非整数格 —— 雪球是抛物弹丸，命中点在格内任意位置），呈现层据它在命中点迸发
    //   白色雪沫碎屑（particleLoader.item.burstSnowball，机制对标 MC 1.0 雪球撞方块碎裂成雪沫）。单向事件流
    //   （PLAN §2 分层：Entities 层发语义事件、呈现层只消费，同 blockBroken→burstBreak 模式）。
    void snowballBreak(float x, float y, float z);
    // t398 鸡下蛋（spec「periodically lays an EGG item」）：MobChicken 周期性下蛋 —— eggTimer 倒计时到 0 时
    //   发本信号。坐标 = 鸡当前格 floor(pos)（与 spawnItem 整数格约定一致，便于 ItemEntityManager 落在鸡身旁）。
    //   呈现层（Main.qml）Connections 据它转发 ItemEntityManager.spawnItem(0x22B=蛋 ×1)（同 mobDied→spawnItem 模式；
    //   单向事件流，PLAN §2 分层：Entities 层发语义事件、呈现层只消费，绝不反向写栅格）。机制等价 MC 1.0 鸡
    //   5-10 分钟下一枚蛋；周期长（kEggLayMin..Max 秒）避免满屏鸡蛋 spam。
    void chickenLaidEgg(int x, int y, int z);

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
        // t527 FallingBlock 携带的方块 state（积雪层层数 metadata）：仅 kind==FallingBlock && blockId==SnowLayer 用。
        //   state 0..7 = 1..8 层；着地 setBlockFromEntity(...,state) 写回雪层**保留层数**（机制对标 MC snow layer 8 层）。
        //   其余 FallingBlock（沙/圆石等）state=0 不读。spawn 入口（spawnFallingBlockState）写入；blockStateAt 暴露给 QML
        //   delegate 据它缩放薄板高度（雪层 falling 实体按 state 决定板厚，非满格立方）。DMI 兜底聚合初始化缺省（同 alive 模式）。
        int blockState = 0;      // t527 FallingBlock 携带的方块 state（仅 SnowLayer 用；其余 0）
        // t490 PrimedTnt 引燃态（复用 kind=FallingBlock + blockId=TntBlock 表达「点燃的 TNT 实体」）：
        //   primed=true → 本 FallingBlock 是 PrimedTnt（TNT 方块被引燃 / 链式引爆时由 spawnPrimedTnt 生成）。
        //     tick FallingBlock 分支据 primed 走「fuse 倒计 → 到 0 引爆（detonatePrimedTnt）」而非「着地放置方块」。
        //   fuse = 引信剩余秒数（spawnPrimedTnt 设 ~5s = kPrimedTntFuseSec；tick 每帧递减 dt）。
        //     到 0 → detonatePrimedTnt（球形破坏 + 引燃邻接 TNT 链式 + 衰减伤玩家 + explosion 音/视）+ 移除实体。
        //   **非完整方块 + 可穿透 + 可堆叠**（spec t490）：primed 实体 halfW/halfH=0（玩家碰撞跳过 → 可穿过），
        //     spawn 时不查占用（同格可叠多个 PrimedTnt，各自独立引爆）。复用 FallingBlock 重力（沙子般受重力下落）。
        bool primed = false;       // 是否 PrimedTnt（引燃态 TNT；复用 FallingBlock kind）
        float fuse = 0.0f;         // 引信剩余秒（仅 primed 用；spawnPrimedTnt 设 kPrimedTntFuseSec，tick 递减 dt）
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
        // 任务（弓箭 60s 必 despawn）：箭 spawn 时刻墙钟（m_clock.elapsed()）。tick Arrow 分支用它做硬上限 ——
        //   任何箭（玩家 / 骷髅 / 飞行 / 嵌入）自 spawn 起 60s 必 despawn（机制等价 MC 箭 60s 消失）。这是对
        //   arrowLife dt-累加 despawn 的安全网 + 真值源（dt 累加在低帧率 / dt=0 / 节流帧漂移时可能滞后，墙钟
        //   不依赖 dt → 必然 60s 移除，杜绝用户报告「骷髅箭插墙 / 落地不消失」）。spawn 时写入；非 Arrow 默认 0。
        //   放 arrowLife 之后（聚合初始化未显式列它 → 默认 0；DMI 兜底，同 alive 放末尾的 lessons t256 模式）。
        qint64 arrowSpawnMs = 0; // 箭 spawn 墙钟 ms（仅 kind==Arrow 用；tick 算 age 做硬 60s despawn）
        // t304 玩家射出的箭（spawnArrowPlayer）专用：arrowFromPlayer=true 的箭命中 **mob**（damageEntity +
        //   mobAttacked 语义事件）；false（骷髅 spawnArrow 射出）命中 **玩家**（mobAttackedPlayer，t283 旧路径）。
        //   机制等价 MC 1.0「玩家箭打怪、怪箭打玩家」（敌我判别由发射者决定，非箭本身阵营）。非 Arrow 默认 false。
        //   arrowDamage = 本箭命中时造成的伤害 HP（骷髅箭恒 kArrowDamage=2；玩家箭由弓蓄力 1..6 决定，spawnArrowPlayer 传）。
        bool arrowFromPlayer = false; // 是否玩家射出（命中目标分流：true→mob / false→玩家）
        int arrowDamage = 0;          // 命中伤害（HP；仅 kind==Arrow 用；骷髅箭 = kArrowDamage）
        // t480 箭发射者槽索引（骷髅箭专用；玩家箭 arrowFromPlayer=true 不设 = -1）：fireArrow 在 spawnArrow 后写
        //   它（= 发射的 Bones 槽索引）→ 箭命中玩家时注册驯服狼防御目标（m_wolfTarget = arrowShooter，主人受击 →
        //   狼攻击射箭的骸骨）。slot-reuse 索引稳定（release 不 shift），发射者存活期间索引有效。非 Arrow → -1 不读。
        int arrowShooter = -1; // 发射者槽索引（仅 kind==Arrow && !arrowFromPlayer 用；-1 = 无 / 玩家箭）
        // t323 箭嵌入态（命中方块后冻结物理）：arrowStuck=true → tick Arrow 分支仅推进 despawn 倒计时，不再
        //   重力 / 位移 / 命中判定。vx/vy/vz 保留作定向（arrowYawAt/arrowPitchAt 据 vel 算 → 嵌入箭仍朝命中
        //   飞行方向）。命中瞬间 e.pos 钉到入射面（半嵌可见）+ arrowLife 重置 kStuckArrowLifetime（~60s despawn）。
        //   玩家箭（arrowFromPlayer）嵌入后可被 PlayerController::arrowPickupScan 近距拾取；骷髅箭嵌入不可拾取
        //   （防刷箭，spec）。非 Arrow / 飞行中默认 false。
        bool arrowStuck = false; // 箭是否已嵌入方块（仅 kind==Arrow 用；spawn 默认 false，acquireSlot 覆写新实体）
        // t505 雪球命中伤害（仅 kind==Snowball 用）：雪球命 mob 时的伤害 HP。**按发射者分流**（机制对标 MC 1.0：
        //   雪傀儡抛雪球对敌对有伤害 / 玩家抛雪球对 mob 0 伤害只触发红闪 + 击退）。spawnSnowball 入口收 damage 参数
        //   写入：雪傀儡 fireSnowball 传 kSnowballDamage（保持敌对伤害）；玩家右键抛（playercontroller）传 0
        //   （0 伤害但 damageEntity 仍发 hurtFlash 红闪 + knockback 击退，机制对标 MC 玩家雪球打 mob 无伤有反馈）。
        //   命中分支读它替代旧硬编码 kSnowballDamage → 同一雪球实体两种伤害语义由 spawn 入口决定。
        //   默认 0（非 Snowball / 未传 → 安全无伤；玩家路径显式传 0 亦无伤）。
        int snowballDamage = 0;  // 雪球命中伤害 HP（仅 kind==Snowball；golem=kSnowballDamage / player=0）
        // t553 雪球发射者槽索引（仅 kind==Snowball 用）：fireSnowball 传雪傀儡 idx；玩家抛（playercontroller）传
        //   默认 -1。命中分支跳过 mi==snowballThrower —— 防「发射者自身命中」：雪球 spawn 在发射者中心 +0.5 格
        //   （fireSnowball origin），而命中盒外扩 kSnowballHitHalfW=0.3 → 发射者 AABB 外扩后距中心 0.65，雪球首帧
        //   （+0.167/tick）距 0.667 仅差 0.017 勉强逃出 —— 节流 / 步长抖动时可能误击自己（把自己击退 + 雪球消失）。
        //   t553 扩大命中到「所有 mob」（含被动）后此误击从「不可能」（旧版只打敌对、发射者 non-hostile 天然排除）
        //   变成「边缘可能」→ 显式排除发射者（机制等价 MC 投射物不命中发射者自身）。默认 -1（无发射者 → 不排除）。
        int snowballThrower = -1; // 雪球发射者槽索引（仅 kind==Snowball；fireSnowball=雪傀儡 idx / player=-1）
        // t239 生物基类（AI / 血量 / 受击 / 死亡）——仅 Mob kind 使用（FallingBlock/Item 留默认 0/false）：
        int mobType = 0;         // mob 子类 id（0=通用测试；t240 pig/cow/sheep；t280 Shambler/Bones；drop/模型据它分流）
        int maxHealth = 0;       // 血量上限（满血）；takeDamage clamp 到 [0, maxHealth]
        int health = 0;          // 当前血量；<=0 → dead（spawnMobTyped 设满血）
        bool dead = false;       // 死亡态（health<=0 → true；dead 期间冻结 AI/重力，deathTimer 到 0 移除）
        float hurtFlash = 0.0f;  // 受击红闪剩余秒数（damageEntity 设 kHurtFlashTime；tick 衰减；>0 → QML 红）
        float deathTimer = 0.0f; // 死亡到移除倒计时（dead 翻 true 时设 kDeathTime；给 QML 播死亡动画窗口）
        // t449 死亡掉落延迟：mobDied（→ 掉落物）不再在 damageEntity 致死瞬间 emit，而是延迟到 deathTimer
        //   归零（= 倒地动画播完）才 emit —— 给玩家「侧倒 + 白烟 → 掉落」的 MC 式死亡过渡（旧实现红闪与掉落
        //   同帧出现「太急」）。deathBurned 在致死瞬间快照 fireTimer>0（机制等价 MC「着火致死掉熟肉」），因
        //   dead 态 fireTimer 冻结，故与 expiry 时复算等价；显式快照更稳（防未来 dead 期间动 fireTimer 的改动）。
        bool deathBurned = false;
        // t479 幼崽死亡快照：damageEntity 致死瞬间快照 e.baby（同 deathBurned 模式）。dead 态 tickBreeding 仍衰减
        //   growTimer —— 幼崽可能在 kDeathTime≈0.5s 死亡动画窗口内长大（baby→false），故 mobDied 延迟 emit 时读
        //   e.baby 会漏判「致死时是幼崽」；快照保「幼崽死亡不掉落」语义稳定（机制等价 MC 幼崽死亡不掉物）。
        bool deathBaby = false;
        // t280 黑暗刷怪（敌对生物 Shambler/Bones 专用；passive / FallingBlock 留默认 false/0 不触发）：
        //   hostile=true 的 Mob 走 tickHostileLife 的燃烧 + 远距消失 + spawn 调度逻辑。passive（pig/cow/sheep/
        //   test）hostile=false → 不燃烧 / 不计入敌对上限 / 不远距消失（passive 永驻世界，机制等价 MC 被动生物
        //   不燃烧、不自然消失）。FallingBlock / Item 不走 Mob 分支故 hostile 字段不读。
        bool  hostile   = false; // 是否敌对生物（Shambler/Bones=true；passive=false）。spawnHostileMob 设 true。
        bool  burning   = false; // 当前是否在日光下燃烧（tickHostileLife 每 tick 重算缓存；QML isBurningAt 读）。
        float burnTimer = 0.0f;  // 燃烧扣血累积（秒；暴露日光时累加 dt，每 kBurnDamageInterval 扣 1HP）。
        // t344 火烧态（岩浆 / 火点燃；ALL mobs，含 passive）。fireTimer>0 = 正在着火（视觉火焰 + 每秒火伤 + 随机熄灭；
        //   归零即熄）。与上方 hostile-only 日光 burning（日光暴露驱动）**并列独立**：日光 burning 仅敌对 + 走
        //   tickHostileLife；fireTimer 适用于所有 Mob + 在主 tick() Mob 分支推进（触碰岩浆 / 火即点燃，离开后仍持续
        //   kFireDuration 秒）。isBurningAt 据二者之一为真即返真（QML 火焰 Model 对 passive 着火亦显）。mobDied 信号
        //   带 burned = (fireTimer>0)（着火态死亡的 mob 掉熟肉，见 Main.qml onMobDied）。
        float fireTimer       = 0.0f; // 火烧剩余秒数（>0 着火；岩浆 / 火点燃；每 tick 递减，归零熄灭）
        float fireDamageTimer = 0.0f; // 火伤累积（秒；fireTimer>0 时累加，每 kFireDamageInterval 扣 1HP + 掷随机熄灭）
        float suffocationTimer = 0.0f; // t254 窒息累积计时（头部嵌实体方块时累加，每 kSuffocationInterval 秒扣 1HP；机制同玩家 t160）
        float cactusDamageTimer = 0.0f; // t394 仙人掌接触伤害累积（mob AABB 接触 Cactus 时累加，每 kCactusDamageInterval 扣 1HP；离开归零）
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
        // t398 鸡下蛋态（仅 mobType==MobChicken 用；其余 mob 留默认 0 不触发）：
        //   eggTimer 到下次下蛋的倒计时（秒）；tick Mob 分支推进，<=0 → emit chickenLaidEgg + 重置随机周期
        //   （kEggLayMin..Max，机制等价 MC 1.0 鸡 5-10 分钟下一枚蛋）。spawn 时随机化初值防批量 spawn 的鸡同步下蛋。
        float eggTimer = 0.0f;       // 到下次下蛋倒计时（秒；仅 MobChicken 用）
        // t399 鱿鱼喷水游动态（仅 mobType==MobSquid 用；其余 mob 留默认 0 不触发）：
        //   swimTimer 到下次「喷水推进」倒计时（秒）；水中（脚位在水格）倒计时到 → 给 vy 正冲量（上浮）+ 随机换向
        //   （机制等价 MC 1.0 鱿鱼喷水推进：周期性上冲后缓沉 = 节律性上下游动）。离水不推进（搁浅态走 aiWander 慢爬）。
        //   spawn 时随机化初值防批量 spawn 的鱿鱼同步喷水（同 ambientTimer 错峰模式）。
        float swimTimer = 0.0f;      // 到下次喷水推进倒计时（秒；仅 MobSquid 用）
        // t400 繁殖态（仅 MobPig/MobCow/MobSheep/MobChicken 用；其余 mob 留默认 0/false 不触发）：
        //   机制对齐 MC 1.0 breeding —— 喂对应食物触发「求偶期」（love mode）→ 同种两求偶者相遇产幼崽 + 双方
        //   进繁殖冷却；幼崽定时长大成体。求偶期主动寻偶（tick 内 findNearestMate 设 yaw 朝配偶 → aiWander 行走）。
        //   loveTimer>0 = 求偶期（喂食触发，自然衰减；期内主动走向同类求偶者 + 与之接触产幼崽）；归零即退出。
        //   breedCooldown = 繁殖后冷却（防同对 mob 立即再繁；期内喂食不触发求偶，机制等价 MC 繁殖冷却）。
        //   baby=true = 幼崽（半大模型 + 不可繁殖 / 不可喂食触发求偶）；growTimer 倒计时到 0 → 长大成体。
        float loveTimer = 0.0f;      // 求偶期倒计时（秒；>0 求偶；喂食触发，tick 自然衰减）
        float breedCooldown = 0.0f;  // 繁殖后冷却（秒；>0 喂食不触发求偶；防刷屏）
        bool  baby = false;          // 是否幼崽（QML 据 babyScaleAt 缩 0.5；不可繁殖 / 不可喂食触发求偶）
        float growTimer = 0.0f;      // 幼崽长大倒计时（秒；baby=true 时推进，到 0 → baby=false 长大成体）
        // t480 狼态（仅 mobType==MobWolf 用；其余 mob 留默认 false/0 不触发）：
        //   wolfTamed=false → 野狼（aiWolf 敌对玩家：追击 + 咬击）；true → 驯服狼（跟随主人 + 防御主人目标）。
        //   wolfSitting=true → 坐（留守原地不跟随不攻击；机制等价 MC 1.0 驯服狼右键坐）。toggleWolfSit 翻转。
        //   wolfAttackCooldown = 狼咬击冷却（秒；<=0 可咬；aiWolf 递减，命中后置 kWolfAttackCooldown）——
        //     与敌对 attackCooldown 字段分离（狼走独立 aiWolf，不复用 aiHostile 的 attackCooldown 语义）。
        //   spawnMobCore 默认成员初始化已清回（move 入槽覆盖旧值，同繁殖态初值约定）。
        bool  wolfTamed = false;       // 是否已驯服（骨头驯服；QML wolfTamedAt 读）
        bool  wolfSitting = false;     // 是否坐着留守（右键切换；QML wolfSittingAt 读）
        float wolfAttackCooldown = 0.0f; // 狼咬击冷却（秒；仅 MobWolf 用）
        // t481 豹猫/猫态（仅 mobType==MobOcelot 用；其余 mob 留默认 false/0 不触发）：
        //   ocelotTamed=false → 丛林野豹猫（游荡被动）；true → 驯服猫（跟随主人 + 坐/站 + 繁殖）。
        //   ocelotSitting=true → 坐（留守原地不跟随；机制等价 MC 1.0 驯服猫右键坐）。toggleOcelotSit 翻转。
        //   ocelotVariant = 驯服后毛色变体（0=黑 / 1=姜黄 / 2=奶油；驯服瞬间随机选，QML 据 ocelotVariantAt
        //     切 mob_cat_* 贴图；未驯服豹猫不读变体走 mob_ocelot 贴图）。spawnMobCore 默认成员初始化已清回
        //     （move 入槽覆盖旧值，同狼态初值约定）。
        bool  ocelotTamed = false;       // 是否已驯服（生鱼驯服；QML ocelotTamedAt 读）
        bool  ocelotSitting = false;     // 是否坐着留守（右键切换；QML ocelotSittingAt 读）
        int   ocelotVariant = 0;         // 驯服猫毛色变体（0..2；随机；QML ocelotVariantAt 读）
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
        // t331 骸骨拉弓瞄准计时（秒；仅 mobType==MobBones 用；其余 mob 留默认 0 不触发）：
        //   aiArcher 在射程内 + 视线清 + 冷却到时累加；满 kAimWindup → fireArrow + 清零（每发前先拉弓瞄准 ~0.5s，
        //   期间位移减速到停 = SLOWS + pauses to aim）。脱射程 / 视线断 / 冷却中 → 清零（中止拉弓）。drawAmountAt 据
        //   它返 0..1 驱动 QML 抬臂 + 弦后拉。
        float aimTimer = 0.0f;    // 拉弓瞄准计时（秒；>0 = 正在拉弓；仅 MobBones 用）
        // t500 perf 骨骸弓手视线缓存（仅 MobBones chasing 用）：lineOfSightClear 是 32 步 isSolid march
        //   （每 AI tick 跑 = chasing 时每 ~66ms 32 blockAt）。缓存视线结果 kLosCacheInterval 秒（~0.5s = 每
        //   ~8 个 AI tick 复查一次）→ chasing 时 march 频率降 ~87%。losClear = 缓存的视线结果；losCacheTimer =
        //   到下次复查倒计时。非 Bones / 非 chasing 留默认（false/0）不触发。缓存误差：玩家躲墙后 ≤0.5s 内
        //   弓手可能仍判视线清（多发一箭）—— 可接受（MC 骷髅也有反应延迟）。
        bool  losClear = false;     // 缓存的 lineOfSightClear 结果（仅 MobBones chasing 射程内用）
        float losCacheTimer = 0.0f; // 到下次 lineOfSightClear 复查的倒计时（秒；仅 MobBones 用）
        // t377 mob 护甲（4 部位护甲物品 id；0=无）。仅 Shambler/Bones spawn 时随机分配（~80% 无 / ~20% 一件或
        //   一套）。QML delegate 据它叠加 tier 色护甲 Model（机制等价 MC 1.0 僵尸/骷髅随机护甲；spec t377）。
        //   其余 mob 留 0 不显。仅视觉 + spawn 随机（不参与 mob 减伤计算 —— spec 仅要求视觉 + 偶遇）。
        int armorHelmet = 0;      // 头盔护甲 id（0=无）
        int armorChest  = 0;      // 胸甲护甲 id（0=无）
        int armorLegs   = 0;      // 护腿护甲 id（0=无）
        int armorBoots  = 0;      // 靴子护甲 id（0=无）
        // t500 perf：mob AI / 环境扫描节流累积器（秒）。每帧 += dt；aiTick 帧（每 kAiTickInterval 帧一次，
        //   错峰 idx % kAiTickInterval）才跑 AI 决策 + 火烧 / 仙人掌 / 窒息 / 吃草扫描，传 aiDt = 累积值 →
        //   AI 速度 / 火伤 / 仙人掌扎伤 / 吃草推进「每秒平均值」与原每帧路径一致（aiDt = N·dt 抵消 N 倍节流）。
        //   放 struct 末尾保既有聚合初始化不错位（lessons-learned t256 元教训）。
        float aiAccum = 0.0f;
        // t500 perf：tickHostileLife 节流累积器（秒；仅 hostile Mob 用）。tickHostileLife 每 tick 遍历所有
        //   槽，但每个 hostile 仅每 kAiTickInterval 帧（同 aiAccum 错峰）跑燃烧 / 远距消失判定（光照 +
        //   距离 + 降水群系解析）。burnTimer 据本累积器按 aiDt 推进 → 平均燃烧扣血速率不变。
        float hostileAccum = 0.0f;
        // t482 雪球减速态（仅被雪傀儡雪球命中的 mob 用；其余 mob 留默认 0 不触发）：
        //   slowTimer > 0 = 正被减速（水平移动 ×kSnowSlowMul 缓慢；机制等价 MC 雪球减速）。tick Mob 分支
        //   每帧递减（跨 0 时 bump revision → QML isSlowedAt 翻回蓝调）；减速叠加进 speedScale（与水中减速
        //   相乘）。雪傀儡 / 铁傀儡自身不被雪球减速（它们是投掷者 / 免疫）。
        float slowTimer = 0.0f; // 雪球减速剩余秒数（>0 减速；t482）
        // t510 雪傀儡热伤害累积器（仅 mobType==MobSnowGolem 用；其余 mob 留默认 0 不触发）：
        //   meltAccum：热群系 / 入水 / 降水时每 tick 累加 aiDt；达 kSnowMeltInterval（~1s）扣 1HP（机制等价
        //     MC 1.0 雪傀儡在热群系 / 水中 / 雨天持续受热伤害直至融化死亡，**非即死**——慢扣血到 0 才死）。
        //     修 t482 旧「kSnowMeltDamage=100 一击致死」：用户报「沙漠召唤即死」，spec 要「慢慢扣血到 0 才死」。
        //   snowGolemSheared：剪刀剪南瓜头后置 true（mechanic-equivalent MC 剪刀剪雪傀儡南瓜头 → derpy 无头
        //     形态；§9 区隔纯色原创）。sheared=true → QML delegate 隐藏南瓜头（保留眼/嘴贴原头位漂浮，机制等价
        //     MC 1.0「剪后变无头形态带眼不死的 derpy 版」）。剪后不再可剪（防刷屏）。放 struct 末尾区保聚合
        //     初始化不错位（同 aiAccum / slowTimer 模式，DMI 兜底）。
        float meltAccum = 0.0f;       // 雪傀儡热伤害累积器（秒；达 kSnowMeltInterval 扣 1HP；仅 MobSnowGolem）
        bool  snowGolemSheared = false; // 雪傀儡是否已被剪南瓜头（true=无头 derpy 形态；仅 MobSnowGolem）
    };
    std::vector<Entity> m_entities;
    int m_revision = 0;
    // perf：节流 entitiesChanged emit 的「待发」脏标记。mob 每帧 wander 致 dirty 几乎每帧 → emit 每帧触发全体
    //   delegate（count × ~12 revision 绑定）NOTIFY 激活 + MobModel 重建 = mob 卡顿主因。改：dirty 只置 m_pendingEmit，
    //   每 kEmitEveryN 帧（~20Hz）才 ++revision + emit 一次。位置 / 腿动画 / 外观 20Hz 刷新（缓慢生物视觉够），
    //   spawn/despawn ≤kEmitEveryN 帧延迟（可察觉但优先恢复 FPS）。m_pendingEmit 持续脏确保节流帧间累积变更不丢。
    bool m_pendingEmit = false;

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
    // t480 驯服狼**共享**防御目标（槽索引）：主人攻击的 mob（PlayerController::attackMob → setWolfTarget）或
    //   主人受击来源（aiHostile 近战 / Stalker 爆炸 / 骷髅箭 arrowShooter 命中玩家 → 本成员赋值）。所有驯服且
    //   站立的狼都追击咬击它（机制等价 MC 1.0 驯服狼群攻主人目标）。-1 = 无目标。slot-reuse 索引稳定；aiWolf
    //   每 AI tick 校验目标存活（alive && kind==Mob && !dead），失效即清（防追尸体 / 追释放槽）。
    int m_wolfTarget = -1;
    // t392 刷怪笼 spawn 节流累积器（秒）：tickSpawners 每 tick 累加 dt，达 kSpawnerInterval 才扫描玩家周围 Spawner
    //   块（按需扫描，避免每帧扫 ~28³ 体素；playerPos 由 PlayerController 传 m_pos）。同 m_spawnAccum 模式。
    float m_spawnAccumSpawner = 0.0f;
    // t500 perf：tick 节拍计数（每 tickImpl 一次 +1，单调不溢出 —— quint32 ~2.1e9，可玩 414 天不回绕）。
    //   每 mob 的「本帧是否跑 AI / 环境扫描」据 ((m_tickPhase + idx) % kAiTickInterval) == 0 错峰判定 →
    //   每 kAiTickInterval 帧一轮、每帧约 1/N 的 mob 跑重活 → 单帧负载均摊（无 GC spike）。
    //   机制等价 MC 1.0 mob AI 节流（mob 每 4-5 tick 才 think 一次而非每 tick），只是分布到不同 mob。
    quint32 m_tickPhase = 0;
    // 任务（弓箭 60s 必 despawn）：墙钟计时器（构造时 start()）。箭 spawn 记 m_clock.elapsed() 到 Entity
    //   .arrowSpawnMs；tick Arrow 分支用它算 age 做硬 60s despawn（机制等价 MC 箭 60s 消失；不依赖 dt 累加，
    //   低帧率 / dt=0 / 节流帧漂移时仍必然移除）。同 ItemEntityManager / XpOrbManager 的 m_clock 模式。
    QElapsedTimer m_clock;

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
    //   t488 perf：释放时把 kind 清回中性值（Item=非 Mob / 非 FallingBlock）→ 空槽的 QML delegate 内所有
    //   mobType Loader / FallingBlock Model 的 active/visible 条件（entKind===Mob/FallingBlock）立即翻 false →
    //   Loader 卸载重子树（MobModel + 多子 Model + 贴图），空槽 delegate 坍缩为裸隐藏 Node。高水位 slot-reuse
    //   保 count 单调不降（Repeater delegate 永不销毁，lessons-learned t170），代价是空槽 delegate 常驻场景图；
    //   旧实现空槽残留上一任实体的 kind/mobType → 对应 Loader 仍 active → 重子树被实例化 + 每 revision bump
    //   同步遍历（64 槽高水位 × ~108 节点 = 数千 3D 节点 / 帧），是「/kill @e 后 main 仍高」的主嫌疑（t488 (a)）。
    //   索引语义零变化（release 不 shift；kind 仅影响已释放空槽的只读呈现，spawn 复用槽时 std::move 覆盖回真值）。
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_entities.size())) return;
        Entity &e = m_entities[size_t(idx)];
        e.alive = false;
        e.kind = Item; // t488：空槽视觉中性化（QML Loader 据此卸载重子树；见方法注释）
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    // t239 生物基类统一生成核心（spawnMobTyped 的实现主体，t400 抽出便于繁殖产幼崽复用）：构造 Entity（碰撞箱 /
    //   pos / 血量 / AI 初值 / 护甲随机等同 spawnMobTyped）+ acquireSlot，返回槽索引（达 kCap → -1 + 告警）。
    //   **不 bump revision / 不 emit**（由 caller 据「本次是否需刷新 QML」决定 —— spawnMobTyped 立即 emit，
    //   tickBreeding 批量产幼崽后统一一次 emit，避免高频扇出 notify 风暴，同 t320/t354 批量收口纪律）。
    //   t400 caller 据返回 slot 设 baby=true / growTimer（幼崽态）后再由 caller 统一 emit。
    int spawnMobCore(int x, int y, int z, int mobType, const QString &color, int maxHealth);

    // t239 AI wander 自主移动（tick 内 Mob 分支调）：时间片倒计时到 → 随机选向 + idle/行走；行走按 yaw 逐轴
    //   （X 后 Z）世界边界 clamp + 方块碰撞撤回。返回是否真位移（驱动 dirty + moveSpeed）。worldW/worldD =
    //   世界宽/深（边界 clamp 防 mob 走出世界坠虚空）。
    // speedScale：水平位移缩放（t298 水中减速；1.0 陆地、kWaterSpeedMul 水中）。透传给 mob 的水平移动，
    //   使在水中时既减位移又同步降低 moveSpeed（t241 腿摆频率随 moveSpeed，故水中腿也变慢 = 视觉上「挣扎」）。
    bool aiWander(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale = 1.0f);
    // t399 鱿鱼水生 AI（tick 内 passive mob 且 mobType==MobSquid 分支调，替代 aiWander）。spec t399「squid water mob:
    //   swims in water bodies」。
    //   机制对齐 MC 1.0 squid：水生被动生物，水里周期喷水推进（上浮 + 水平漂移）+ 缓沉；离水搁浅慢爬。
    //   (1) 水中（脚位在水格）：swimTimer 倒计时到 → 设 e.vy=+kSquidSwimUp（喷水推进上冲量；正=向上）+ 随机换 yawRad
    //       （水平漂游方向）+ 重置随机周期（kSquidSwimIntervalMin..Max）。其下通用重力段以 kWaterGravity 缓沉把上冲
    //       减速到 0 再反向 → 「上浮 → 缓沉」节律性游动（机制等价 MC squid 喷水推进 + 浮力缓沉）。水平位移走 yaw 漂游
    //       （kSquidSwimSpeed，慢于陆地 wander → 漂浮感），逐轴 mobAabbHitsSolid 撤回防穿墙 + 边界 clamp。
    //   (2) 离水（搁浅）：委托 aiWander（陆地慢爬，机制等价 MC squid 上岸后笨拙挪动；不复用喷水推进）。
    //   返回是否真位移（驱动 dirty + moveSpeed + walkPhase 触腕摆）。speedScale 见 aiWander（t298 水中减速；透传
    //     给 aiWander 离水回退分支；水中漂游用独立 kSquidSwimSpeed 不受 speedScale 影响 —— 鱿鱼游速是其物种特征，
    //     不应再被通用水中减速叠加）。
    //   分层（PLAN §2）：只读 World::blockAt（mobFeetInWater 脚位水格判）+ 自身数据；写 EntityManager 自身（pos / vy /
    //   yawRad / swimTimer）。无向上依赖。mobFeetInWater 同文件静态助手（同 tick / aiWander 越障查）。
    bool aiSquid(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale = 1.0f);
    // t480 狼 AI（tick Mob 分支 mobType==MobWolf 调，替代 aiWander；详见 .cpp 实现注释）。机制对齐 MC 1.0 狼
    //   三态：
    //   (1) 未驯服（wolfTamed=false）：**敌对玩家** —— 侦测范围（kWolfDetectRange）内追击 + 近距咬击
    //       （emit mobAttackedPlayer(kWolfAttackDamage, MobWolf) → 呈现层仅 Survival 应用伤害，同 aiHostile
    //       攻击模式）；非追踪 → 回退 aiWander。playerTargetable=false（创造/观察者）→ 不追咬（同 t290 门控）。
    //   (2) 驯服 + 坐（wolfSitting=true）：**留守** —— 不移动不攻击（跟随主人回来自动续跟）；机制等价 MC 坐狼。
    //   (3) 驯服 + 站：**跟随 + 防御** —— 有防御目标（m_wolfTarget：主人攻击 / 主人受击来源的 mob）→ 追击并
    //       咬击该 mob（damageEntity(targetIdx, kWolfAttackDamage)）；无目标 → 跟随主人（distXZ > kFollowMinDist
    //       走近 / <= 停步；过远 kWolfTeleportDist 瞬移到主人附近防掉队）。求偶期（loveTimer>0）优先寻偶
    //       （findNearestMate + 走近配偶，复用 t400 求偶寻偶逻辑）。
    //   返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。idx = 本 mob 槽索引（求偶寻偶 findNearestMate
    //   排除自身 + 防御目标自我排除）。分层（PLAN §2）：只读 World::isSolid + 自身数据；咬玩家 / 咬 mob 走既有
    //   受击链（mobAttackedPlayer 语义信号 / damageEntity），无向上依赖。
    bool aiWolf(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                float speedScale, bool playerTargetable);
    // t481 豹猫/猫 AI（tick Mob 分支 mobType==MobOcelot 调，替代 aiWander；详见 .cpp 实现注释）。机制对齐
    //   MC 1.0 豹猫/猫三态：
    //   (1) 未驯服（ocelotTamed=false）：**被动游荡**（丛林野豹猫，不攻击玩家不敌对；aiWander）。
    //   (2) 驯服 + 坐（ocelotSitting=true）：**留守** —— 不移动（跟随主人回来自动续跟）；机制等价 MC 坐猫。
    //   (3) 驯服 + 站：**跟随主人**（distXZ > kFollowMinDist 走近 / <= 停步；过远 kOcelotTeleportDist 瞬移到
    //       主人附近防掉队）；求偶期（loveTimer>0）优先寻偶（findNearestMate + 走近配偶，复用 t400 逻辑）。
    //   猫**不防御**（机制等价 MC 1.0 猫不攻击怪物 —— 与驯服狼的防御咬击区分；驱赶 Stalker 由 aiStalker 侧对
    //   猫/豹猫临近时逃离实现）。返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。idx = 本 mob 槽
    //   索引（求偶寻偶 findNearestMate 排除自身）。分层（PLAN §2）：只读 World::isSolid + 自身数据，无向上依赖。
    bool aiOcelot(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                  float speedScale);
    // t482 雪傀儡 AI（tick Mob 分支 mobType==MobSnowGolem 调，替代 aiWander；详见 .cpp 实现注释）。机制对齐
    //   MC 1.0 雪傀儡（防御造物：游荡 + 抛雪球打敌对 + 行走留雪 + 热/雨融化）：
    //   (1) 融化：沙漠群系（biomeIdAt==Desert，热）或降水（isPrecipitatingAt，雨/雪）或入水 → meltAccum 累加 →
    //       达 kSnowMeltInterval 扣 kSnowMeltDamage=1 HP（慢扣血，机制等价 MC 持续热伤害而非即死）。
    //   (2) 行走留雪层：**放身后格**（golem 离开格留雪脚印，t529 改自旧「放脚下」防模型与雪层重叠）。
    //   (3) 远程雪球：节流（kSnowGolemThrowInterval）扫最近敌对 mob（nearestHostile）→ fireSnowball（抛物弹丸，
    //       命中敌对低伤害 1HP + 减速 kSnowSlowDuration）。
    //   (4) 游荡（aiWander）：随机选向 + 时间片。**t529**：移除 t499 二轮「玩家在范围内 → yawRad 朝玩家」的持续
    //       覆盖（spec 改「平时随机朝向」）→ yawRad 由 aiWander 随机选向（机制等价 MC 造物自由游荡朝向）；
    //       生成时 yawRad 初值（spawnMobCore 默认 0）即「生成时固定朝」，aiWander 首次到期才随机改。
    //   返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。playerPos 参数保留为 caller 签名兼容，t529 移除
    //   朝玩家逻辑后函数体内 Q_UNUSED(playerPos) 不再读它。
    //   分层（PLAN §2）：只读 World::blockAt/isSolid/biomeIdAt/isPrecipitatingAt + 自身数据；写自身（pos /
    //   slowTimer / damageEntity / yawRad）+ 向下静默写 World（setWaterSilent 雪层）。无向上依赖。
    bool aiSnowGolem(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                     float worldW, float worldD, float speedScale);
    // t483 铁傀儡 AI（tick Mob 分支 mobType==MobIronGolem 调，替代 aiWander；详见 .cpp 实现注释）。机制对齐
    //   MC 1.0 铁傀儡（防御造物：游荡 + 追击打敌对 + 重拳击退）：
    //   (1) 节流扫最近敌对 mob（nearestHostile，kIronGolemDetectRange）→ 有目标则朝它走（kIronGolemWalkSpeed，
    //       yaw 朝目标）+ 近距（kIronGolemAttackRange）大力攻击（damageEntity kIronGolemAttackDamage 高伤害 +
    //       knockback 击退，机制等价 MC 铁傀儡重拳 + 击退）。
    //   (2) 无目标 → 游荡（aiWander）。
    //   返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。分层（PLAN §2）：只读 World::isSolid + 自身数据；
    //   攻击敌对走 damageEntity/knockback（同层），无向上依赖。
    bool aiIronGolem(int idx, Entity &e, float dt, World *world, float worldW, float worldD, float speedScale);
    // t482 朝 target 解抛物初速并抛雪球（aiSnowGolem 远程攻击调）。origin = shooter 中心 + 朝 target 前移 0.5 格
    //   （避免贴墙时雪球 spawn 入墙即被 tick 判方块命中）。水平速度固定 kSnowballSpeed → 飞行时间 t=d/vH；
    //   据 target 高度差反解 vy=(Δy+0.5·g·t²)/t（命中 target 高度的抛物解）；vy 钳到 ±kSnowballMaxVert。
    //   三轴加 ±kSnowballSpread 随机抖动（非 100% 精准）。d 太小（<0.01）→ 安全早退（防除零）。
    void fireSnowball(int idx, const Entity &shooter, const QVector3D &target);
    // t482/t483 最近**敌对** mob 查找（雪傀儡抛雪球 / 铁傀儡追击攻击调）：返距 pos 在 range 内最近一只
    //   alive && !dead && kind==Mob && hostile（敌对生物：Shambler/Bones/Stalker/Spider）的 mob 索引；无 → -1。
    //   **只打敌对**（passive / golem 自身 / 造物不攻击玩家，机制等价 MC 防御造物只打怪物）。O(n) 每 golem 每
    //   AI tick，n≤kCap=64 可忽略。const 只读自身数据。
    int nearestHostile(const QVector3D &pos, float range) const;
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
    //   t480 idx = 本 mob 槽索引：近战攻击命中玩家时注册驯服狼防御目标（m_wolfTarget = idx，机制等价 MC 驯服狼
    //   攻击咬伤主人的怪物）。
    bool aiHostile(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
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
    //       t331 先累加 aimTimer（拉弓瞄准；期间位移按 (1−draw) 减速到停 = SLOWS + pauses to aim），满 kAimWindup 才
    //       fireArrow + 重置冷却（防每帧连发）。脱射程 / 视线断 / 冷却中 → 清 aimTimer（中止拉弓）。drawAmountAt 据
    //       aimTimer 暴露 0..1 给 QML 驱动抬臂 + 弦后拉。
    //   返回是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。playerPos = 玩家脚位（tick 的 listener）。
    //   分层（PLAN §2）：只读 World::isSolid + 自身数据；shoot 走 spawnArrow（箭实体）+ 命中由 Arrow 分支发
    //   mobAttackedPlayer 语义信号让呈现层路由 PlayerState（同 aiHostile 的 attack 模式）。
    //   t480 idx = 本 mob 槽索引：传给 fireArrow 设箭 arrowShooter（箭命中玩家 → 驯服狼反击发射者）。
    bool aiArcher(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
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
    //       离开蓄力区即 defuse——中距（kFuseRange<dist<=kDefuseRange）fuseTimer-=dt 渐退回 0（非累积，反复
    //       进出不强制引爆）；逃远（>kDefuseRange）→ fuseTimer=0（熄火）。蓄力中仍朝玩家（yaw 更新）但不位移。
    //   (5) detonate：fuseTimer>=kFuseTime → 调 detonateStalker（球形破坏方块 + 距离衰减伤害玩家 + emit
    //       explosion + 标 exploded）+ 本实体当帧移除（tick Mob 分支据 exploded 入 toRemove）。
    //   返回是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。playerPos = 玩家脚位（tick 的 listener）。
    //   分层（PLAN §2）：只读 World::isSolid/blockAt + 自身数据；爆炸破坏方块走 World::setWaterSilent（向下
    //   写栅格 + worldChanged 重建 mesh）；伤害玩家走 mobAttackedPlayer 语义信号（呈现层路由 PlayerState）。
    //   t480 idx = 本 mob 槽索引：传给 detonateStalker 注册驯服狼防御目标（爆炸伤玩家 → 狼反击 Stalker）。
    bool aiStalker(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos, float worldW, float worldD,
                   float speedScale = 1.0f);
    // t284 Stalker 爆炸（aiStalker fuse 满时调）：以 e.pos 为中心、kExplosionRadius 为半径的球内破坏方块
    //   （setWaterSilent 写 Air，跳过 Bedrock / Water / Air）+ 距离衰减伤害玩家（emit mobAttackedPlayer）+
    //   emit explosion（呈现层播爆炸音 / 迸发）+ 标 e.exploded=true（tick 当帧移除）。机制等价 MC 苦力怕爆炸。
    //   破坏方块走 setWaterSilent（静默写 + worldChanged 重建 mesh，**不**发 blockBroken → 免球形内每块破块
    //   粒子 / 音 spam；爆炸的音 / 视反馈由 explosion 信号单一入口驱动）。
    //   t480 idx = 本 mob 槽索引：爆炸伤害玩家（dmg>0）时注册驯服狼防御目标（m_wolfTarget = idx）。
    void detonateStalker(int idx, Entity &e, World *world, const QVector3D &playerPos);
    // t283 朝 target 解抛物初速并发射一支箭（aiArcher shoot 段调）。origin = shooter 中心 + 朝 target 前移
    // t283 朝 target 解抛物初速并发射一支箭（aiArcher shoot 段调）。origin = shooter 中心 + 朝 target 前移
    //   0.5 格（避免贴墙时箭 spawn 入墙即没）。水平速度固定 kArrowSpeed → 飞行时间 t=d/vH；据 target 高度差
    //   反解 vy=(Δy+0.5·g·t²)/t（命中 target 高度的抛物解）；vy 钳到 ±kArrowMaxVert 防极端弧。三轴加 ±kArrowSpread
    //   随机抖动（MC 骷髅非 100% 精准；spread ≪ vH 不改飞行时间量级）。d 太小（<0.01）→ 安全早退（防除零）。
    //   t480 shooterIdx = 发射者（骸骨）槽索引：spawnArrow 返槽后写 arrowShooter —— 箭命中玩家时驯服狼据此
    //   反击发射者（主人受击 → 狼攻击该 mob）。
    void fireArrow(int shooterIdx, const Entity &shooter, const QVector3D &target);
    // t283 视线清查（aiArcher shoot 前调，防穿墙盲射）：从 from 到 to 沿连线 0.5 格步进采样，任一采样点所在
    //   格 isSolid → 视线被挡返 false。0.5 格步进足以抓 1 格墙（箭速 ~14 blocks/s、每帧 0.22 格，墙厚 ≥1）。
    //   分层：只读 World::isSolid（同 tick / aiHostile 越障查），不向下加依赖。
    bool lineOfSightClear(World *world, const QVector3D &from, const QVector3D &to) const;

    // t241 羊吃草：检测/消耗 entity 前方一格草丛。consume=false 仅检测（决定是否进入吃草周期）；
    //   consume=true 则写入（草丛→空气 + 其下草方块→泥土，走 World::setWaterSilent 静默写，非玩家破块
    //   → 不发 broken/placed，免粒子/音/掉落噪音）。返回是否在前方找到草丛（TallGrass）。
    //   目标格 = 沿 yaw 朝向 reach=0.7 前方列、y=身体格（草丛所在）+ 其下地表格（草方块）；OOB → 安全 false。
    bool sheepEatGrass(Entity &e, World *world, float worldW, float worldD, bool consume);
    // t400 繁殖 tick（tick 末尾调，主实体循环之外 —— 幼崽生成走 acquireSlot 可能 push_back，主循环持 Entity&
    //   引用期间不可 push_back 致其失效，故集中到本末段）：
    //   (1) 衰减所有 mob 的 loveTimer / breedCooldown / 幼崽 growTimer（growTimer 到 0 → baby=false 长大）。
    //   (2) 求偶期成体配对 —— 同种（pig/cow/sheep/chicken）双方均求偶（loveTimer>0）且非幼崽 / 非冷却 + XZ 中心
    //       距 ≤ kBreedRange → 产 1 幼崽（spawnMobCore 标 baby=true + growTimer）+ 双方进 kBreedCooldown 冷却 +
    //       退求偶（loveTimer=0）。受 kPassiveMobCap 钳制（达上限不再产，spec「种群上限」）。
    //   返是否变更（驱动 dirty + bump revision + emit）。配对扫描 O(n²) 但 n ≤ kCap=64 可忽略。
    //   分层（PLAN §2）：只读自身实体数据 + 调 spawnMobCore（同层）；无向下 / 向上依赖。
    bool tickBreeding(qreal dt);
    // t400 最近求偶配偶查找（tick Mob 分支 love-mode 寻偶调）：返最近一只 alive 且 !dead 且 !baby 且
    //   loveTimer>0 且 mobType==e.mobType 的 mob 索引（排除 self）；无 → -1。供求偶者设 yaw 朝配偶 → aiWander
    //   行走相遇。O(n) 每 mob 每帧，n≤64 可忽略。const 只读。
    int findNearestMate(int idx) const;
    // t481 最近豹猫/猫查找（aiStalker 驱赶调）：返距 pos 在 range 内最近一只 alive && !dead && kind==Mob &&
    //   mobType==MobOcelot 的 mob 索引；无 → -1。O(n) 每 Stalker 每 AI tick，n≤64 可忽略。const 只读。
    int nearestOcelot(const QVector3D &pos, float range) const;
    // t400 mobType 是否**可繁殖被动生物**（pig/cow/sheep/chicken 之一）。求偶寻偶 / 配对 / feedMob 食物匹配
    //   均先据它门控（hostile / MobTest / MobSquid 不可繁殖）。静态纯函数。
    static bool isBreedableType(int mobType);

    static constexpr int kCap = 64;            // 实体数上限（测试用，防溢出）
    static constexpr float kGravity = 28.0f;   // 重力加速度（blocks/s²；与玩家/掉落物同值，世界手感一致）
    static constexpr float kMaxFall = 78.4f;   // 终端下落速度（blocks/s；防无限加速）
    // t500 perf：mob AI / 环境扫描节流间隔（帧）。每 kAiTickInterval 帧每 mob 才跑一次「AI 决策 + 火烧 /
    //   仙人掌 / 窒息 / 吃草扫描」（错峰 idx % kAiTickInterval → 单帧 1/N mob 跑重活）。N=4 → 每 mob ~15Hz
    //   AI（MC 1.0 mob think 每 4-5 tick ≈ 12-15Hz 量级；机制对齐）。mob 物理（重力 / resting / 击退 / 推动）
    //   + 受击红闪 + 走路声 + 环境音仍每帧跑（连续体感 + 即时反馈）。mob 移动也走节流（每 N 帧 aiDt=N·dt
    //   一次走完 → 平均位移速度不变；aiDt=N·dt 下位移 0.05-0.15 block/AI-tick，远 < 1 block → 不穿墙）。
    //   作用：mob 桶瓶颈（用户实测 24.99ms/f，60FPS 预算 16.6ms/f）由每 mob 每帧 ~50 blockAt（mobAabbHitsSolid
    //   ×2 全格扫 + 仙人掌邻接 + 视线 raycast）× 60 槽 = 数千 blockAt/帧 构成；节流后平均 1/N → 目标 <5ms/f。
    //   保留 gameplay：mob 仍正常 AI / 攻击 / 刷怪（仅决策频率降；玩家受击 / 碰撞仍即时）；移动平滑度略降
    //   （肉眼可见小幅「步进」），MC 自身也这样（机制等价；PLAN §4 机制对标非数值 1:1）。
    static constexpr int kAiTickInterval = 4;
    // perf：entitiesChanged emit 节流间隔（帧）。tick 末 dirty 只置 m_pendingEmit；每 kEmitEveryN 帧（m_tickPhase%i==0）
    //   才 ++revision+emit。N=3 → ~20Hz 刷新（60Hz 计时器下）→ NOTIFY 激活 + MobModel 重建频率降 3×。mob 视觉 20Hz
    //   对缓慢生物足够（电影 24Hz）；spawn/despawn ≤3 帧延迟。低 FPS 时帧更长 → 实际刷新更稀，但优先减负恢复 FPS。
    static constexpr int kEmitEveryN = 3;
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
    // t392 刷怪笼周期刷怪常量（spec「periodically spawns ONE hostile mob while a player is within range;
    //   spawn capped」；机制等价 MC 1.0 刷怪笼：玩家在 16 格内 + 笼周 6 只上限 + 每 ~5-10s 刷一只）。数值为本工程
    //   小世界量身调，非 MC 精确复刻（PLAN §4「机制对标」非数值 1:1）。独立于 tickHostileLife 的「黑暗刷怪」
    //   —— 刷怪笼无视光照（地牢天然黑暗）、仅在玩家范围内刷、有局部上限（防刷爆）。
    //   - kSpawnerInterval：刷怪周期（秒；每周期扫一次玩家周围 Spawner、每笼尝试刷 1 只）。MC 1.0 刷怪笼每 ~10-40s
    //     刷一次；取 6s 平衡「可见可验收」与「不刷爆」（地牢多笼时也按周期节流）。
    //   - kSpawnerPlayerRange：玩家激活范围（blocks；XZ 距离 ≤ 此才刷，spec「player within range」）。MC 1.0 刷怪笼
    //     玩家 16 格内激活；取 16 同 MC（同 kDetectRange 量级 → 玩家进地牢即刷、走远即停）。
    //   - kSpawnerScanRange：玩家周围扫描半径（blocks；±此值立方体内找 Spawner 块）。= kSpawnerPlayerRange + 2 余量
    //     → 笼在激活带边沿时仍在扫描范围（playerPos 中心 + 半径覆盖激活带）。
    //   - kSpawnerMobCheckRadius：笼周敌对计数半径（blocks；笼为中心球内敌对数 ≥ kSpawnerLocalCap 则不刷）。
    //     MC 1.0 刷怪笼 6 只上限（球半径 ~8）；本工程取半径 4 + 上限 4（小世界合理密度；防地牢刷出十几只塞满）。
    //   - kSpawnerLocalCap：单笼周围敌对上限（≥ 此则本笼跳过；机制等价 MC 1.0 刷怪笼 6 mob cap）。
    static constexpr float kSpawnerInterval      = 6.0f;  // 刷怪周期（秒；每周期扫一次玩家周围 Spawner）
    static constexpr float kSpawnerPlayerRange   = 16.0f; // 玩家激活范围（blocks；XZ 距离 ≤ 此才刷）
    static constexpr int   kSpawnerScanRange      = 18;    // 玩家周围扫描半径（blocks；±此值立方体内找 Spawner）
    static constexpr float kSpawnerMobCheckRadius = 4.0f;  // 笼周敌对计数半径（blocks）
    static constexpr int   kSpawnerLocalCap       = 4;     // 单笼周围敌对上限（机制等价 MC 刷怪笼 6 mob cap，本工程取 4）
    // t374 被动生物群系权重表 kPassiveSpawnWeights[biome][mob]：行 = 群系（同 World::biomeIdAt 编码
    //   0=Plains, 1=Hills, 2=Desert, 3=Forest），列 = mob（0=牛 MobCow, 1=羊 MobSheep, 2=猪 MobPig, 3=鸡 MobChicken）。
    //   Plains 牛羊富集（开阔草原）、Forest 猪富集（机制等价 MC 1.0 平原牛羊 / 森林猪富集）、Hills 均衡、
    //   Desert 稀疏均匀（沙漠少动物，非排斥，仅概率差异）。鸡在平原 / 森林常见（机制等价 MC 1.0 鸡在草地群系
    //   富集）、沙漠稀少。pickPassiveMobType 据本表加权随机选 mob 类型。
    static constexpr int kPassiveSpawnWeights[4][4] = {
        { 4, 4, 2, 3 }, // Plains（牛 4 / 羊 4 / 猪 2 / 鸡 3；牛羊富集，鸡常见）
        { 2, 2, 2, 2 }, // Hills（均衡）
        { 1, 1, 1, 1 }, // Desert（稀疏均匀）
        { 2, 2, 6, 3 }, // Forest（牛 2 / 羊 2 / 猪 6 / 鸡 3；猪富集）
    };
    // t398 鸡下蛋周期（秒）：活体鸡每 [kEggLayMin, kEggLayMax] 秒下一枚蛋（emit chickenLaidEgg）。机制等价
    //   MC 1.0 鸡 5-10 分钟下一枚蛋（6000-12000 tick）；数值为本工程小世界量身调（PLAN §4「机制对标」非数值
    //   1:1）—— 取 4-8 分钟保「周期性可观察」而不刷屏。spawn 时初值随机化防批量 spawn 的鸡同步下蛋。
    static constexpr float kEggLayMin = 240.0f; // 鸡下蛋周期下限（秒；~4 分钟）
    static constexpr float kEggLayMax = 480.0f; // 鸡下蛋周期上限（秒；~8 分钟）
    // t399 鱿鱼喷水游动常量（spec「squid water mob: swims in water bodies」；机制对齐 MC 1.0 squid 周期喷水推进 +
    //   浮力缓沉的节律性游动；数值为本工程小世界量身调，非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）。
    //   - kSquidSwimUp：喷水推进上冲量（blocks/s，正=向上）。水中 swimTimer 到 → e.vy=+本值；其下通用重力段以
    //     kWaterGravity(=6) 缓沉把它减速到 0 再反向 → 上浮峰值高 ≈ v²/(2·g) = 9/12 ≈ 0.75 格，缓沉回 ~1s → 节律性
    //     「上浮 0.75 → 缓沉」bobbing 游动（机制等价 squid 喷水推进）。取 3.0 让推进观感明显（非贴底不动）。
    //   - kSquidSwimIntervalMin/Max：喷水推进随机周期（秒）。1.5-3.0s 一次 → 节律舒缓（非高频抖动），近 MC squid
    //     偶发喷水节律。spawn 初值随机化防批量同步喷水（同 ambientTimer 错峰）。
    //   - kSquidSwimSpeed：水中水平漂游速度（blocks/s）。慢于陆地 wander kWalkSpeed=1.0 → 漂浮感（非疾游）；
    //     squid 是缓游生物。不受通用 speedScale（kWaterSpeedMul）叠加 —— 物种游速特征，叠加会过慢。
    static constexpr float kSquidSwimUp          = 3.0f;  // 喷水推进上冲量（blocks/s；峰值 ~0.75 格上浮）
    static constexpr float kSquidSwimIntervalMin = 1.5f;  // 喷水推进周期下限（秒）
    static constexpr float kSquidSwimIntervalMax = 3.0f;  // 喷水推进周期上限（秒）
    static constexpr float kSquidSwimSpeed       = 0.8f;  // 水中水平漂游速度（blocks/s；慢漂非疾游）
    // t400 繁殖常量（spec t400「同种 2 只喂对应食物 → 生幼崽；种群上限防泛滥」；机制对齐 MC 1.0 breeding：
    //   喂食触发 love mode → 同种配对产幼崽 + 5 分钟冷却 + 幼崽 20 分钟长大；数值为本工程小世界量身调，
    //   非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）。
    //   - kLoveDuration：求偶期持续秒数。MC love mode ~30s 找配偶窗口；取 30（求偶者有充足时间被寻偶 AI 拉到一起）。
    //   - kBreedCooldown：繁殖后冷却秒数。MC 5 分钟；取 60（明显长于求偶期 30 → 一对 mob 1 分钟内只繁 1 次，
    //     防刷屏；又远短于 MC 5 分钟便于测试观察「冷却中再喂无效」）。
    //   - kBabyGrowTime：幼崽长大秒数。MC 20 分钟；取 120（2 分钟，肉眼可观察「幼崽渐大成体」而不冗长）。
    //   - kBreedRange：配对 XZ 中心距上界（blocks）。MC 求偶者贴近即繁；取 3.0（mob 半宽 0.4 + 接触余量 →
    //     中心距 3 内算「相遇」；求偶期主动寻偶 AI 把它们拉到一起，故无需大半径）。
    //   - kPassiveMobCap：可繁殖被动 mob 总数上限（pig/cow/sheep/chicken 成体 + 幼崽）。达上限 → 配对不再产
    //     幼崽（防种群爆炸；spec「种群上限」）。取 24（小世界合理密度；与 kHostileMobCap=30 同量级）。
    //   - kBabyScale：幼崽模型缩放（QML delegate Node scale via babyScaleAt）。MC 幼崽 ~0.5 倍体型；取 0.5。
    static constexpr float kLoveDuration    = 30.0f; // 求偶期持续（秒；MC love mode ~30s 窗口）
    static constexpr float kBreedCooldown   = 60.0f; // 繁殖后冷却（秒；防同对立即再繁；MC 5 分钟，本工程取 60 便测试）
    static constexpr float kBabyGrowTime    = 120.0f; // 幼崽长大（秒；MC 20 分钟，本工程取 120 便观察）
    static constexpr float kBabyFeedGrow    = 12.0f;  // 每次喂幼崽减成长时间（秒；≈kBabyGrowTime 的 10% —— MC 喂幼崽减 ~10% 剩余时间，t479）
    static constexpr float kBreedRange      = 3.0f;  // 配对 XZ 中心距上界（blocks；求偶寻偶 AI 把双方拉到一起后触发）
    static constexpr int   kPassiveMobCap   = 24;    // 可繁殖被动 mob 总数上限（防种群爆炸；spec「种群上限」）
    static constexpr float kBabyScale       = 0.5f;  // 幼崽模型缩放（babyScaleAt 返它；成体 1.0）
    // t480 狼常量（spec「骨头驯服 ~33% / 坐站切换 / 跟随 + 防御 / 咬击」；机制对齐 MC 1.0 驯服狼：跟随主人、
    //   攻击主人攻击/咬伤主人的 mob、咬击伤害；数值为本工程量身调，非 MC 精确复刻 —— PLAN §4「机制对标」
    //   非数值 1:1）。
    //   - kWolfDetectRange：未驯服狼侦测玩家范围（blocks；XZ）。取 12（略低于敌对 kDetectRange=16 —— 野狼非
    //     夜间刷怪敌对，属「地盘性攻击」，侦测近些；玩家走近才受袭）。
    //   - kWolfChaseSpeed：追击 / 跟随速度（blocks/s）。3.5 介于玩家走速 4.3 与 wander 1.0 之间 —— 跟随不掉队
    //     但玩家正常走略快（疾跑可拉开；机制等价 MC 狼跟随速度略低于玩家）。
    //   - kWolfAttackDamage：狼咬击伤害（HP）。机制等价 MC 1.0 驯服狼咬击 ~3 心 = 6HP；本工程取 4（2 心，
    //     介于玩家剑伤 4-6 之间 —— 战斗伙伴咬击威胁与剑相当，打敌对 20HP 需 5 咬）。
    //   - kWolfAttackCooldown：咬击间隔（秒）。机制等价 MC 狼 ~0.75s/击；取 1.0 对齐敌对 kAttackCooldown 节奏。
    //   - kFollowMinDist：驯服狼跟随到位的最小 XZ 距离（blocks；<= 停步、> 走近主人）。取 2.5（贴近不挤压）。
    //   - kWolfTeleportDist：驯服狼距主人过远 → 瞬移到主人附近（blocks；XZ）。机制等价 MC 1.0 狼距主人 >32 格
    //     传送；本工程取 24（小世界）+ 近主人选安全位，防跟随永久掉队（狼速 3.5 < 玩家 4.3）。
    //   - kWolfTameChance：骨头驯服概率（spec「~33%」）。取 0.33（机制等价 MC 1.0 狼 33% 驯服概率；失败骨头
    //     仍消耗）。
    static constexpr float kWolfDetectRange    = 12.0f; // 未驯服狼侦测玩家范围（blocks；XZ）
    static constexpr float kWolfChaseSpeed     = 3.5f;  // 追击 / 跟随速度（blocks/s）
    static constexpr int   kWolfAttackDamage   = 4;     // 狼咬击伤害（HP）
    static constexpr float kWolfAttackCooldown = 1.0f;  // 咬击间隔（秒）
    static constexpr float kFollowMinDist      = 2.5f;  // 跟随到位 XZ 距离（blocks）
    static constexpr float kWolfTeleportDist   = 24.0f; // 距主人过远瞬移阈值（blocks；XZ）
    static constexpr float kWolfTameChance     = 0.33f; // 骨头驯服概率（spec ~33%）
    // t481 豹猫/猫常量（spec「丛林生成 + 生鱼驯服变猫 3 色 + 跟随坐站 + 驱赶 Stalker + 繁殖」；机制对齐
    //   MC 1.0 豹猫：丛林群系生成、生鱼驯服、驯服猫跟随 + 坐、驱赶苦力怕；数值为本工程量身调，非 MC 精确
    //   复刻 —— PLAN §4「机制对标」非数值 1:1）。
    //   - kOcelotTameChance：生鱼驯服概率（机制等价 MC 1.0 豹猫 ~1/3 驯服概率；失败生鱼仍消耗，同 wolf）。
    //   - kOcelotFollowSpeed：驯服猫跟随速度（blocks/s）。MC 猫跟随较快；取 4.0（≈玩家走速 4.3 → 不掉队）。
    //   - kOcelotTeleportDist：驯服猫距主人过远 → 瞬移到主人附近（blocks；XZ）。同狼 kWolfTeleportDist=24
    //     （小世界；猫速 4.0 接近玩家走速 → 正常情况下不掉队，仅极端地形触发）。
    //   - kStalkerFleeRange：豹猫/猫驱赶 Stalker 半径（blocks；XZ 距离 ≤ 此 → Stalker 逃离）。MC 苦力怕被猫
    //     吓跑半径 ~6；取 6.0（玩家牵猫近距即可见驱赶，又不致全场 Stalker 逃离）。
    //   - kStalkerFleeSpeed：Stalker 逃离速度（blocks/s）。快于其追踪速 kStalkerChaseSpeed=2.6、≈玩家走速
    //     → 可逃掉但玩家追上仍有威胁（机制等价 MC 苦力怕被猫吓跑速度）。
    static constexpr float kOcelotTameChance    = 0.33f; // 生鱼驯服概率（spec ~1/3）
    static constexpr float kOcelotFollowSpeed   = 4.0f;  // 驯服猫跟随速度（blocks/s）
    static constexpr float kOcelotTeleportDist  = 24.0f; // 距主人过远瞬移阈值（blocks；XZ）
    static constexpr float kStalkerFleeRange    = 6.0f;  // 豹猫/猫驱赶 Stalker 半径（blocks）
    static constexpr float kStalkerFleeSpeed    = 4.0f;  // Stalker 逃离速度（blocks/s）
    // t482/t483 防御造物常量（spec「雪傀儡抛雪球打敌对 / 行走留雪 / 热雨融化；铁傀儡大力攻击 + 击退 / 死掉
    //   铁锭罂粟」；机制对齐 MC 1.0 雪傀儡 / 铁傀儡；数值为本工程量身调，非 MC 精确复刻 —— PLAN §4「机制对标」
    //   非数值 1:1）。
    //   - kSnowGolemThrowInterval：雪傀儡抛雪球间隔（秒）。MC 雪傀儡 ~1.5-3s 抛一次；取 2.5（节流可见不刷屏）。
    //   - kSnowGolemAttackRange：雪球攻击侦测范围（blocks；XZ）。MC 雪傀儡 ~10 格抛程；取 10（玩家牵怪近距可见攻击）。
    //   - kSnowballSpeed：雪球水平速度（blocks/s）。慢于箭 kArrowSpeed=14（雪球轻飘）；取 10（可见弧线可躲避）。
    //   - kSnowballMaxVert：vy 钳（blocks/s；防极端弧，同箭）。
    //   - kSnowballSpread：三轴初速随机抖动（blocks/s；非 100% 精准）。
    //   - kSnowballDamage：雪球命中敌对伤害（HP）。机制等价 MC 雪球 0 伤 / 对火焰系 3 伤；本工程取 1（低伤害，
    //     主要价值在减速骚扰，spec「伤害低 + 轻微减速」）。
    //   - kSnowSlowDuration：雪球减速持续（秒）。取 3.0（明显可感知的减速窗口）。
    //   - kSnowSlowMul：减速水平速度倍数（<1）。取 0.5（一半速度，机制等价 MC 雪球减速）。
    //   - kSnowTrailInterval：行走留雪层节流间隔（秒）。取 1.0（每秒一块雪层 → 走出一串雪脚印，不刷屏）。
    //   - kSnowMeltInterval：热群系 / 入水 / 降水时每扣 1HP 的累积间隔（秒）。t510 改「即死」为「持续慢扣血」：
    //     取 1.0（每秒扣 1HP，满血 4 HP 雪傀儡 ~4s 死，机制等价 MC 1.0 雪傀儡在热群系 / 水中 / 雨天持续受热
    //     伤害直至融化死亡，非一击即死）。aiSnowGolem 累加 meltAccum → 达本间隔 → damageEntity(1)。
    //   - kSnowMeltDamage：单次热伤害扣血量（HP）。t510 自旧值 100（一击致死）降到 1（慢扣血；每 kSnowMeltInterval
    //     秒扣 1HP）—— 修用户报「沙漠召唤雪傀儡立即死」的诉求：spec 要「慢慢扣血到 0 才死」。
    //   - kSnowGolemMaxHealth：雪傀儡满血（HP）。spawnMobTyped 在 playercontroller.cpp 用字面量 4（南瓜 + 雪块×2
    //     结构搭建时 maxHealth=4，机制等价 MC 1.0 雪傀儡 4HP=2 心，软质造物）。4 HP / 1 HP/s 扣血 → ~4s 融化死亡。
    //   - kIronGolemDetectRange：铁傀儡敌对侦测范围（blocks；XZ）。MC 铁傀儡 ~16 格；取 12（近守卫）。
    //   - kIronGolemAttackRange：近战攻击 XZ 距离（blocks）。铁傀儡重拳臂长；取 2.0（略大于敌对近战 1.6，
    //     重拳挥臂范围大）。
    //   - kIronGolemAttackDamage：重拳伤害（HP）。机制等价 MC 铁傀儡 7.5-21.5 伤害（正常难度 ~7-8 心）；
    //     本工程取 8（4 心，几拳打死 20HP 敌对）。
    //   - kIronGolemKnockbackStrength：重拳击退强度（倍率；knockback strength 参数，>1 拉大击退距离）。取 1.5。
    //   - kIronGolemAttackCooldown：重拳间隔（秒）。取 1.5（重拳慢而沉）。
    //   - kIronGolemWalkSpeed：追击行走速度（blocks/s）。铁傀儡迟缓；取 2.2（慢于敌对 kChaseSpeed 2.8）。
    static constexpr float kSnowGolemThrowInterval = 2.5f;  // 雪傀儡抛雪球间隔（秒）
    static constexpr float kSnowGolemAttackRange   = 10.0f; // 雪球攻击侦测范围（blocks；XZ）
    // t499 二轮复盘：雪傀儡朝玩家的 XZ 距离阈值（blocks）。玩家在此范围内 → yawRad 朝玩家（模型 -Z 正对玩家，
    //   玩家可见南瓜头刻面眼/嘴正脸）；玩家出范围 → aiWander 自主游荡朝向。机制等价 MC 防御造物观察接近的玩家。
    //   取 10（= kSnowGolemAttackRange，造物在「能抛雪球防敌对」的同一感知范围内即面向玩家，无需更近）。
    // t529 LEGACY：雪傀儡朝玩家的 XZ 距离阈值（blocks；t499 二轮）。t529 移除「朝玩家」逻辑后此常量不再被读
    //   （保留为历史记录，防误删认为「漏接线」；spec 改「平时 aiWander 随机朝向」后造物不再恒面向玩家）。
    static constexpr float kSnowGolemFaceRange     = 10.0f; // LEGACY（t529 移除朝玩家后不再读）
    static constexpr float kSnowballSpeed          = 10.0f; // 雪球水平速度（blocks/s）
    static constexpr float kSnowballMaxVert        = 14.0f; // 雪球 vy 钳（blocks/s；防极端弧）
    static constexpr float kSnowballSpread         = 1.0f;  // 雪球三轴初速随机抖动（blocks/s）
    static constexpr float kSnowballLifetime       = 5.0f;  // 雪球最长存活（秒；飞行未命中兜底移除，同箭）
    static constexpr float kSnowballHitHalfW       = 0.3f;  // 雪球 vs 敌对 mob 命中盒 XZ/Y 外扩（blocks；雪球是小点）
    static constexpr int   kSnowballDamage         = 1;     // 雪球命中伤害（HP；低伤害）
    // t553 雪球命中击退强度（倍率；knockback strength 参数）。旧版恒 1.0（= kKnockbackHoriz 4.5 blocks/s，
    //   推距 ~1.1 格）—— 用户报「雪球打生物不击退」：敌对 mob 追击玩家（~2.8 blocks/s）时，1.1 格后退被追击
    //   前进抵消 → 净位移≈0 肉眼不可见。提至 2.0（=9 blocks/s，推距 ~2.2 格）→ 追尾 mob 也被明显推开。
    //   机制对齐 MC 1.0 投射物命中击退（箭 / 雪球都推开生物；本工程箭 t553 一并补同值击退）。
    static constexpr float kSnowballKnockbackStrength = 2.0f; // 雪球命中击退强度（倍率；t553）
    static constexpr float kSnowSlowDuration       = 3.0f;  // 雪球减速持续（秒）
    static constexpr float kSnowSlowMul            = 0.5f;  // 减速水平速度倍数（<1）
    static constexpr float kSnowTrailInterval      = 1.0f;  // 行走留雪层节流间隔（秒）
    static constexpr float kSnowMeltInterval       = 1.0f;  // 热群系/入水/降水每扣 1HP 的累积间隔（秒；t510 慢扣血）
    static constexpr int   kSnowMeltDamage         = 1;     // 单次热伤害扣血量（HP；t510 自 100 降到 1，非即死）
    static constexpr float kIronGolemDetectRange   = 12.0f; // 铁傀儡敌对侦测范围（blocks；XZ）
    static constexpr float kIronGolemAttackRange   = 2.0f;  // 铁傀儡近战攻击 XZ 距离（blocks）
    static constexpr int   kIronGolemAttackDamage  = 8;     // 铁傀儡重拳伤害（HP；高伤害）
    static constexpr float kIronGolemKnockbackStrength = 1.5f; // 铁傀儡重拳击退强度（倍率）
    static constexpr float kIronGolemAttackCooldown = 1.5f; // 铁傀儡重拳间隔（秒）
    static constexpr float kIronGolemWalkSpeed     = 2.2f;  // 铁傀儡追击行走速度（blocks/s）
public:
    // t344 火烧系统常量（岩浆 / 火点燃；ALL mobs 含 passive + 玩家）。机制对齐 MC 1.0「实体触碰岩浆 / 火着火、
    //   火伤定时扣血、持续一段后或随机熄灭」；数值为本工程量身调（非 MC 精确复刻，PLAN §4 机制对标）。
    //   public 暴露 → PlayerController 复用（玩家火烧与 mob 火烧同值保一致手感；Game→Entities 向下依赖合规）。
    //   kFireDuration：着火后持续时间（秒；离开火源后仍持续此秒数才灭，机制等价 MC fire 8s）。
    //   kFireDamageInterval：火伤扣血间隔（秒；每秒 1HP，机制等价 MC 火 1HP/s 持续段）。
    //   kFireExtinguishChance：每次火伤结算时随机提前熄灭的概率（机制等价 MC fire 随机熄灭 + 雨灭；本工程无雨，
    //     仅留随机提前灭 + 定时双保险；取 0.15 = 约 15%/秒概率提前灭）。
    static constexpr float kFireDuration        = 8.0f;  // 着火持续时间（秒；岩浆/火点燃后 fireTimer 初值）
    static constexpr float kFireDamageInterval  = 1.0f;  // 火伤扣血间隔（秒/HP）
    static constexpr float kFireExtinguishChance = 0.15f; // 每次火伤结算随机提前熄灭概率
    // t394 仙人掌接触伤害扣血间隔（秒/HP；机制等价 MC 1.0 仙人掌触碰即伤，每 0.5s 扣 1HP = 2HP/s）。
    //   玩家与 mob 共用本值（Game→Entities 向下依赖，玩家复用 EntityManager::kCactusDamageInterval，保一致手感）。
    static constexpr float kCactusDamageInterval = 0.5f;
private:
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
    static constexpr int   kAttackDamage    = 3;     // 单次近战伤害（HP；t353 自 4 降回 3 = 1.5 心，配 1s 节流给反应窗口）
    // t321/t353 玩家受击全局节流（秒；详见 kPlayerHitThrottle 注释）。单 mob 冷却只防自己连抽，多 mob 围攻时各 mob
    //   独立冷却叠加 → DPS 倍乘（无节流时 4 只 × 4HP/s = 16HP/s ≈ 满血瞬死，玩家无反应窗口）。本节流在 EntityManager
    //   全局层串行化「玩家被命中」：任一 mob（近战 attack / 骷髅箭命中）经 mobAttackedPlayer 命中后置 m_playerHitCooldown
    //   = kPlayerHitThrottle；其间其它 mob 攻击 / 命中不触发（机制等价「玩家受击无敌帧」+ 强制围攻 mob 轮替出手）。
    //   t353 根因复盘：t321 节流取 0.5s（→ 围攻上限 2 命中/s）同时把近战伤害 3→4，围攻 DPS = 2×4 = 8HP/s → 满血 10HP 仅
    //   1.25s 反应窗口 ≈ 瞬死，节流过短且单发过高，故 R18i 后仍嫌过快。t353 节流 0.5→1.0（围攻命中率上限降到 1 次/s）
    //   + 近战伤害 4→3。单只 mob 不受影响（其 1s 冷却 >= 节流，节流不延后单 mob 节奏）；多只围攻时受击频率上限 = 1/1.0
    //   = 1 次/s（与只数无关）→ DPS 封顶 1×kAttackDamage = 3HP/s → 满血 10HP 至少 3.3s 反应窗口（走速 4.3 → 可移动 ~14 格脱离）。
    static constexpr float kPlayerHitThrottle = 1.0f; // 玩家受击全局节流（秒；防多 mob 围攻秒杀；t353 自 0.5 提到 1.0）
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
    static constexpr float kShootCooldown    = 2.5f;   // 射箭冷却（秒；+kAimWindup 0.5 = 每发 ~3s；t353 自 1.6 提到 2.5 降低多弓手箭雨密度）
    // t331 拉弓瞄准时长（秒）：射程内 + 视线清 + 冷却到 → 先累加 aimTimer 满 kAimWindup 才射；期间位移减速到停
    //   （机制等价 MC 1.0 骷髅停步拉弓瞄准；顺带拉低攻击节奏，缓解 t321 围攻压迫感）。drawAmountAt 据 it 返 0..1。
    static constexpr float kAimWindup        = 0.5f;   // 拉弓瞄准时长（秒；满才射）
    // t500 perf 骨骸弓手视线复查间隔（秒）：chasing 射程内时 lineOfSightClear 的 32 步 isSolid march 每
    //   kLosCacheInterval 秒复查一次（缓存结果用至下次复查）。0.5s ≈ 每 8 个 AI tick（15Hz）复查 → march
    //   频率降 ~87%。缓存误差 ≤0.5s（玩家躲墙后弓手可能多发一箭；MC 骷髅亦有反应延迟，可接受）。
    static constexpr float kLosCacheInterval = 0.5f;   // 视线复查间隔（秒；chasing 射程内缓存 lineOfSightClear）
    static constexpr float kArrowSpeed       = 14.0f;  // 箭水平速度（blocks/s）
    static constexpr float kArrowMaxVert     = 18.0f;  // vy 钳（blocks/s；防极端弧）
    static constexpr float kArrowSpread      = 1.2f;   // 三轴初速随机抖动（blocks/s）
    static constexpr float kArrowLifetime    = 5.0f;   // 箭最长存活（秒；兜底移除）
    static constexpr int   kArrowDamage      = 2;      // 命中伤害（HP）
    static constexpr float kArrowHitHalfW    = 0.4f;   // 箭 vs 玩家命中盒 XZ 外扩（blocks）
    // t553 箭命中 mob 击退强度（倍率；knockback strength 参数）。机制对齐 MC 1.0 箭命中生物被推开（用户「雪球应
    //   像箭一样击退」的参照物 —— 箭本工程此前不击退，补同族击退使投射物行为一致）。取 1.5（=6.75 blocks/s，
    //   推距 ~1.7 格，略低于雪球 2.0 —— 箭速高冲量已大，无需更高倍率）。
    static constexpr float kArrowKnockbackStrength = 1.5f; // 箭命中 mob 击退强度（倍率；t553）
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
    // 任务（弓箭 60s 必 despawn）：箭自 spawn 起的**硬墙钟上限**（ms）。任何箭（玩家 / 骷髅 / 飞行 / 嵌入）
    //   自 spawn 起 60s 必 despawn（机制等价 MC 1.0 箭 60s 消失）。这是 arrowLife dt-累加 despawn（飞行 5s /
    //   嵌入 60s）的**真值源 + 安全网**：dt 累加在低帧率 / dt=0 / t500 节流帧漂移时会滞后，墙钟不依赖 dt →
    //   必然 60s 移除，杜绝用户报告「骷髅弓手射出的箭插墙 / 落地不消失」。= 60000ms = 60s。
    static constexpr qint64 kArrowDespawnMs = 60000; // 箭墙钟上限（ms；自 spawn 必 despawn）
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
    // t494 爆炸推动 primed TNT 实体常量（机制等价 MC TNT 爆炸把邻接 primed TNT 推开飞起）：爆炸中心距 primed TNT
    //   ≤ kExplosionRadius → 施水平远离冲量（kExplosionPushSpeed × 距离衰减）+ 上抛（kExplosionUpSpeed × 距离衰减）。
    //   vx/vz 由 primed tick 水平积分 + 摩擦衰减（kExplosionEntityFriction），飞起后自然落地 / 停下（机制等价 MC
    //   爆炸推开 TNT 的抛物线）。数值量级：爆炸 max 水平 ~6 b/s（≈ 掉落物弹出 kItemPopSpeed 2.0 的 3×，明显飞起但
    //   受摩擦很快停下）、上抛 ~7 b/s（≈ 玩家跳跃初速，能离地 ~1 格）。
    static constexpr float kExplosionPushSpeed      = 6.0f; // 爆炸水平冲量上限（blocks/s；距离衰减）
    static constexpr float kExplosionUpSpeed        = 7.0f; // 爆炸上抛冲量上限（blocks/s；距离衰减）
    static constexpr float kExplosionEntityFriction = 4.0f; // primed TNT 被推后的水平摩擦衰减率（1/s；exp(-rate·dt)）
    // t297 爆炸掉落：每个被爆炸破坏的方块以此概率掉落其物品实体（机制等价 MC 爆炸弹毁方块掉物；
    //   spec「~50% 成掉落物」）。掉落 id 走 BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落）。
    static constexpr float kExplosionDropChance = 0.25f;  // 破坏块掉落概率（~25%；连锁爆炸掉落物控量，原 50% 致 items 顶满 200 卡顿）
    // t490 PrimedTnt 引信常量（机制等价 MC 1.0 primed TNT fuse ~80 tick = 4s；本工程取 ~5s，spec「~5s」）。
    //   kPrimedTntFuseSec = 引信总长（秒）；spawnPrimedTnt 默认值。tick 每帧递减 dt，到 0 → detonatePrimedTnt。
    //   kPrimedTntFuseJitterSec = 链式引爆时各 PrimedTnt 引信的随机错峰量（秒；避免同帧全部引爆 = 一次性大爆炸，
    //   错峰后各 TNT 略延时引爆，机制等价 MC TNT 链式逐个引爆的连锁观感）。
    static constexpr float kPrimedTntFuseSec = 5.0f;      // PrimedTnt 引信总长（秒；spec ~5s）
    static constexpr float kPrimedTntFuseJitterSec = 0.6f; // 链式引爆引信随机错峰（秒；0..Jitter 随机偏移避免同帧全爆）
    // t494 链式引燃专用短引信（用户「连锁引燃 TNT 太慢，像重新点燃一样；MC 链式引爆会更快」）：爆炸引燃的邻接
    //   TNT（detonateTntSphere 内 spawnPrimedTnt）用短 fuse（~1.2s，比手点 5s 快）→ 连锁快速推进（机制等价 MC
    //   TNT 链式逐个快速引爆的观感）。手点 / 机关 / 压力板点燃仍 5s（kPrimedTntFuseSec）。fuseProgressAt 按
    //   kPrimedTntFuseSec 归一 → 短 fuse 起始 progress 小 → 白闪起始即较快（连锁观感「已在燃」）。
    static constexpr float kChainFuseSec = 1.2f;          // 链式引燃 TNT 引信（秒；短于手点 5s，快速连锁）
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
