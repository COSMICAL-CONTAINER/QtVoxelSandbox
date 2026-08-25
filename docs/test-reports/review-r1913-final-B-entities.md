# R19.13 终审 B 组（Entities 层）审查报告

**范围**：提交窗口 08ffc71..HEAD(6f78e7f) 中 Entities 层改动——5f9a40c（t828-t833 生物组主体）、f28ff44（t835 暗渊珠 tick 重写）、15a1000（t856 spawnPrimedTnt 尾参）、20a0efa 部分（铁砧记账换序 + kValidMobModelType 表长钉）、0a1acb5（t836 Bobber 实体 + Game 层收竿镜像）。
**方法**：无 git 工具权限（只读角色），以 HEAD 全量源码交叉核对 dev-plan R19.13 条目语义与完成记录；重点段逐行读（溺水/浮力、夜行者水伤+箭、驯服链、染羊长回、珠物理、TNT 尾参、Bobber 四态机、铁砧记账）+ PLAN §2 分层/IP 门扫描。
**日期**：2026-08-25

---

## 判定：PASS（1 中低 + 3 低 findings，均非不变量违反）

---

## Findings

### 中

| # | 位置 | 问题 | 触发场景 | 建议 |
|---|------|------|----------|------|
| M1 | 20a0efa；src/Entities/entitymanager.cpp:5861-5895 | 铁砧砸伤 review24 换序只封了 mob 引用 `m` 的悬垂窗口（记账 5861 先于 damageEntity 5862），**外层落体引用 `e` 的窗口未封**：mob 循环内 damageEntity 的 `emit entitiesChanged()`（2195）若经任一 QML handler 同步触发实体 spawn → acquireSlot push_back → vector realloc → `e` 悬垂，其后玩家段（5864-5877 读 e.pos/e.spawnSerial）与着地段（5883-5895 读 e.blockId/e.blockState/e.pos）继续解引用 = UB。注释自认"damageEntity 今日只 emit 不增删槽"，**今日无实际触发路径**（entitiesChanged 消费者是绑定刷新，不 spawn），故为防御性收口不完整而非现行 bug——与 5238 行终审修（"spawn 可 push_back → 本循环内 Entity& 悬垂"同款已知模式）不一致的残留。 | 未来把任何 spawn 挂进 entitiesChanged 链（或 mobDied 之外的同步实体创建）时，铁砧落地帧崩溃/内存损坏 | 玩家段前重取引用（`Entity &e = m_entities[size_t(idx)];`，5238 先例）或进入砸伤段前对 e 的 pos/blockId/spawnSerial 做值快照 |

> **M1 处置（R19.13 收尾批 fix(review-r1913-final)）——已修**：进砸伤段前值快照 `aPos/aBlockId/aBlockState/aSerial/aHalfH`（emit 链不改本落体数据 → 快照 == 搬家后活值），mob 循环逐迭代 AABB 复检、玩家段、着地/塌落段全读快照；「继续下落」分支是唯一写点 → 分支内重取引用 `Entity &eNow = m_entities[size_t(idx)]` 再写（快照管读、重取管写）。非铁砧落体无 emit 窗口，行为零变。

### 低

| # | 位置 | 问题 | 触发场景 | 建议 |
|---|------|------|----------|------|
| L1 | 5f9a40c（t829①）；src/Entities/entitymanager.cpp:5010-5020 | 箭命中夜行者的日志分支恒输出 "(forced hit)"：`const Entity &m = m_entities[mi]` 是槽引用，teleportEntity 修改同一对象后 `m_entities[mi].pos != m.pos` 是同对象自比较恒 false。瞬移成功也记 "forced hit"，日志与注释（5002-5007 的 race 修复说明）矛盾，排障时误导。仅日志，无行为影响。 | 排查夜行者闪避行为读日志时 | teleport 前快照 `const QVector3D oldPos = nm.pos;`，日志改 `nm.pos != oldPos` |
| L2 | 0a1acb5；src/Game/playercontroller.cpp:2435-2439 + src/Entities/entitymanager.cpp:717-721 | 收竿拉拽扣耐久不校验目标存活：hooked ≥ 0 时无条件 `pullMobToward` + 耐久 -5；若钩住的 mob 在收竿同帧已死（死亡动画 0.5s 窗内、浮标 tick 尚未跑脱钩验证），pullMobToward 的 `e.dead` 守卫静默早退 → 拉拽无效果但仍扣 5 耐久。 | 对垂死 mob 收竿 | pullMobToward 改返 bool（拉拽生效才 true），false 时按空收处理不扣耐久 |
| L3 | 全局（非本批引入）；src/Entities/entitymanager.{h,cpp} 标识符 `EnderPearl/EnderEye/NetherPortal/enderPearlLanded` 等 | IP 门登记项：MC 词根（Ender*/Nether*）仍作标识符——R19.12 备忘已登记"遗留 MC 词根改名批"的既有遗留；本批（含 f28ff44 大量触碰 EnderPearl 分支）**未新增**任何 MC 专有名词（新增 Bobber/kBobberSt*/sheepWoolDyed/mobAirTimer/healTamedPet 等均通用词或原创，条目中文名"暗渊珠/余烬门"已换、代码词根未跟）。src/Core/resourcepackmanager.* 的 zombie/skeleton/creeper/enderman 文件名映射为资源包兼容层的功能性元数据（t419 起既有，§9 注释明示），同属登记不改判。 | — | 改名批尽快收口：EnderPearl→暗渊珠词根、NetherPortal→余烬门词根（只动本工程标识符，资源包映射表保留——它是读外部包的目录约定） |

> **L1/L2 处置（R19.13 收尾批 fix(review-r1913-final)）——已修**：L1 日志改比 `teleport` 前快照的 `oldPos`（消除同对象自比较恒 false，瞬移成功如实记 "(teleport dodge)"）；L2 `pullMobToward` 改返 bool（拉拽实际生效才 true），caller 据此垂死 mob 收竿不扣 5 耐久（按空收处理），矩阵探针 t836(d2) 行为级锁死（钩住→打死→收竿→耐久不变）。
> **L3 登记不修**：MC 词根改名批已立项遗留（R19.12 备忘），本批未新增词根类，非本批收口范围。

---

## 通过项（按审查点）

1. **t828 溺水/浮力** ✓
   - DMI/槽复用：mobAirTimer/mobDrownTimer 默认成员初始化（entitymanager.h:1202-1203）；spawnMobCore `Entity e;` 值初始化 + acquireSlot move 整体覆盖槽（entitymanager.cpp:281/396）→ 复用槽的新 mob 恒从 0 起。
   - 头位判定 `floor(pos.y + halfH·0.8)`（6175），矮/高 mob 各得其所；**在 aiTick 节流块内用累积 aiDt**（6035/6014——节流累积器纪律 t500，平均速率与每帧一致）；头出水双清零（6191-6193）；致死本帧 continue（6195，同火/仙人掌语义）。
   - 鱼雷豁免面：溺水豁免 `mobType != MobSquid`（6174）、浮力分流 `mobType == MobSquid`（6712）——本工程水生仅鱿鱼，豁免面完整。
   - **resting 打破无副作用**：打破分支严格限鱿鱼（6640-6646）；水底非鱿鱼生物保持 resting（走原支撑复探），溺水计时器在节流块内照常推进——沉底生物 15s 后 1HP/s 溺至死，与条目"太久不浮上来掉血"及 t828 探针"溺水猪 20s 扣至死"的钉死语义一致（被动生物无上浮逃生 AI 是既有 t298 取舍，非本批回归）。
   - 鱿鱼浮力：kSquidBuoyancy=3.0 反转重力 + kSquidRiseMax=1.6 钳制（6718-6719）；resting 复探对水底固体恒真 → 打破后浮力分支可达（注释 6637-6639 论证成立）；头出水面落回普通重力 → bobbing。

2. **t829③ 水伤首触即伤 + ① 箭强制瞬移** ✓
   - ③（3722-3741）：waterDamageAccum≤0 → 首触分支立即扣 1HP + 置周期倒计；持续接触每满 kNightwalkerWaterDamageTick(1s) 再扣；**离水重置 0**（3740）→ 每次独立接触都是首触即伤；teleportEntity 拒水落点（3939）→ 逃离必中断浸泡。
   - ①（5009-5023）：箭命中夜行者绕过 teleportCooldown 直调 teleportEntity；**瞬移失败退化路径有完整结算**（damageEntity + knockback + arrowHitMob + remove——零结算穿身不复发）；瞬移成功箭一并 remove（防 dodge 后箭再命中其后 mob/落地可拾的间接收益）；近战 30% 掷骰路径不变。

3. **t831 驯服链** ✓
   - tickBreeding 衰减段 loveTimer（2024-2027）与 tameHeartTimer（2030-2033）**两计时器都递减**、各自归零收心 dirty。
   - 分离正确：inLoveAt（1929）= loveTimer>0 ∥ tameHeartTimer>0，消费面仅 QML 心形（Main.qml:6629）；寻偶 AI 与配对（2065/2071）全读 loveTimer → 驯服爱心不触发寻偶/配对。
   - 喂食三分流（playercontroller.cpp:3361-3370 狼 / 3405-3413 猫）：幼崽 feedBaby → 受伤 healTamedPet(4)（healthAt<maxHealthAt 门）→ 满血 enterLoveMode，优先级与条目一致；healTamedPet（1664-1679）钳上限 + 满血/未驯服返 false + bump；受伤+繁殖冷却中的成体回血不受冷却门（MC 口径）。

4. **t832 染羊** ✓
   - dyeSheep（1644-1659）：sheepWool=染下标 + sheepWoolDyed=true + sheared=false（染即长毛）+ regrowCooldown=0（新毛不走吃草重掷——sheared=false 后长回分支进不去）。
   - 长回（6345-6375）：`if (e.sheepWoolDyed) { sheepWool = rollNaturalSheepWool(); sheepWoolDyed = false; }`——**未染羊长回不重掷**（保持原色）；rollNaturalSheepWool 从 spawnMobCore 抽出共用（365），t789 kSheepNaturalWeights 单一权威；自然生成 sheepWoolDyed=false（366）。
   - 繁殖幼崽继承父代色（2110-2114）覆写自然随机 → 染色羊后代不掉进"重掷自然色"路径。

5. **t835 珠物理** ✓
   - presence 命中判据（5529-5537）：本格 blockAt 实存，豁免 {Air, Water, Lava, NetherPortal, Fire}。**火豁免与条目语义无冲突**：条目"任何接触（含岩浆）必传送"的症状面是铁轨/墙（无碰撞盒/薄盒漏检），岩浆已覆盖（缓沉族沉底接触底面格必传送，5501）；Fire 是效果格，注释引 MC 1.0 投掷物 raytrace 对无碰撞格穿过——与 MC Java 实际行为一致（火无碰撞盒，投射物穿过），dev-plan 完成记录亦明示该豁免。口径自洽。
   - 终速推导：`vy = qMax(vy − 12·dt, −sink)`（5514/5520）→ 水中收敛到 −1.5 b/s、岩浆 −0.7（更粘更慢）；水平阻尼 5/s（5515-5517，~0.2s 基本停）；入液判定按当前格、命中按 next 格——高速入液下一帧起接管，无穿透；液体期寿命暂停（5519）防深柱半水传送；寿命/越界同 tick 时 emit 前界内校验（5551-5553，L2 修）。

6. **t856 spawnPrimedTnt 尾参** ✓
   - 声明默认 `velX = 0.0f, velZ = 0.0f`（entitymanager.h:593-594）；实现仅在非零时写 vx/vz（4485-4488）。
   - 既有消费端逐字不变：机关右键 6 邻（playercontroller.cpp:2983）、压力板 6 邻（5010）、firePowerTnt 电力点火（5262，QML onPowerTntTriggered → firePowerTnt(x,y,z) → 3 参）、链式（entitymanager.cpp:4252/4361/4391，4 参 fuse 不含 vel）——全走默认 0，primed tick 水平积分段（5757 `vx!=0 ∥ vz!=0` 门）零位移行为不变。发射器新路径（5376）传朝向×kDispenserTntPopSpeed。

7. **t836 Bobber** ✓
   - 四态机转换完整：Flying→Hooked（5674，AABB 外扩 kBobberHookHitPad 点测）/→Water（5694-5704，XZ 收格心 + Y=液面−kBobberFloatDip，液面按水 state 折算源 1.0/流 (8−s)/8 与 mesher 同口径）/→Ground（5708 岩浆面浮住 + 5716 实体接触贴面静止，豁免族同珠口径）/出界消散（5687-5691）。
   - Hooked 解绑：目标死/移除/槽复用换任经 **spawnSerial 双查**（5595，acquireSlot 全局单调代际号 entitymanager.h:1444）→ 脱钩转 Flying 零速下落（5601-5606）；换绑（新浮标命中已钩 mob）旧浮标脱钩（5661-5672）。
   - Ground 后再甩无残留：收竿/换槽/失焦/暂停/重生全路径经 useFishingRod 收竿分支或 cancelFishing removeEntityAt（playercontroller.cpp:2427/2517）；m_fishing 单门 + 单浮标源（钓鱼中不能再甩）→ 槽复用误绑新 Bobber 不可能。
   - 180s 寿命与 Game 镜像竞态：arrowLife 全态递减（5587）→ 到期消散；updateFishing（2488-2497）aliveAt+kindAt 双查 → 消散/复用（Mob 占槽）都判 invalid 自动收竿态，无实体可清不误 remove；收竿先快照三值再 remove（2424-2427）→ remove 后 pullMobToward 的目标 mob 同步无 tick 间隔，dead 守卫兜底（残余边缘见 L2）。
   - 确定性等待：bobberWaitSeconds = 5 + (h%2501)×0.01 ∈ [5,30]（708-712），hashVoxel(seed⊕0xF15C⊕序号)，鱼跑重掷序号 ++（5631）每轮新值；咬钩/逃走沿 emit（5626/5634）语义事件，Entities 发、呈现层消费。
   - pullMobToward（717-740）：水平归一 + 微上抛 + resting 解除 + 零向量 yaw 兜底，不调 damageEntity（钩中 0 伤害）。

8. **铁砧记账换序 + kValidMobModelType** ✓
   - 换序等价性（5861-5862）：damageEntity 守卫（2159-2161 越界/非活体/非 Mob/dead/amount≤0）在铁砧 caller 已过滤（5849-5855 + dmg>0）后全部不可达 → 记账先行不改变任何可达状态；换序消除 `m` 的 emit-realloc 悬垂写窗口（残留 `e` 窗口见 M1）。
   - kValidMobModelType（mobmodel.cpp:354-374）：19 行与 MobTest=0..MobAnvil=18 一一对应（Tnt/Anvil 哨兵 false）；static_assert（379-380）钉表长 == MobModel::kValidMobTypeCount=19（mobmodel.h:147）+ 矩阵探针侧 kValidMobTypeCount == EntityManager::MobAnvil+1 两级编译期拦截；**分层合规**：Renderer 不 include entitymanager.h，枚举互钉落在上层测试 TU（375-378 注释明示）。
   - anvilCrushSerial DMI（entitymanager.h:1039 =0）→ 槽复用清零；玩家侧 m_playerAnvilCrushSerial 同语义（1371-1375）。

### 架构面

- **分层**：entitymanager.cpp include 仅 world.h（向下）+ 同层载具管理器 + Core；PlayerController（Game）向下调 EntityManager；mobmodel（Renderer）不 include Entities；Bobber hashVoxel 经 World public（Entities→World 向下）——无越界。
- **IP 门**：本批新增代码零新增 MC 专有名词/零新资产；注释"机制等价 MC"为合法设计参照。遗留词根登记见 L3。
- **约定**：Q_INVOKABLE 越界防御、常量 static constexpr + 头文件注释块、spawnSerial 快照复用 snowball 先例、槽复用 DMI——与工程既有模式一致。

---

## 总体判定

**PASS**。八个审查点全部落实、无 PLAN §2 不变量违反、无分层越界、无本批新增 IP 泄漏；1 中低（铁砧段外层引用悬垂窗口，防御性收口不完整、今日无触发路径）+ 3 低（日志分支恒假、垂死 mob 收竿扣耐久、MC 词根遗留登记）建议随下批收口。

---

## R19.13 收尾批处置记录（fix(review-r1913-final)）

- **M1 已修**：铁砧砸伤段外层落体引用 `e` 值快照（aPos/aBlockId/aBlockState/aSerial/aHalfH）+ 继续下落分支重取引用写回（:5238 先例同款）。
- **L1 已修**：夜行者日志 oldPos 快照比对。
- **L2 已修**：pullMobToward 返 bool，垂死 mob 收竿不扣耐久（探针 t836(d2) 锁）。
- **L3 登记不修**：MC 词根改名批（已立项遗留）。
- 验证：全量重编零警告（dxcompiler.dll 提示豁免）；矩阵 252 PASS / 0 FAIL；exe 冒烟 14s 存活零 QML 错误。
