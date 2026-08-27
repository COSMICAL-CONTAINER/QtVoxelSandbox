# 架构终审报告 — R19.15（t861-t903）+ Review26 修复批

## 审查窗口 e17325a..ca93b79（HEAD = ca93b79）

### 判定：PASS（带 1 中 / 4 低 findings；无本窗口引入的不变量违反）

方法：PLAN §2 逐项 + Review_2026-08-26.md ✅ 标记抽查（#1/#4/#5/#8/#9/#11/#21/#25 八条亲核，另附带核 #2/#6/#7/#10/#20）+ 8 个重点审查面全量源码亲核（Read/Grep）。矩阵 305 PASS 基线未重跑（按已知基线采信）。

---

## 一、Findings

### 中-1 IP 门存量命中（非本窗口引入，登记「MC 词根改名批」遗留）
- **位置**：`src/Game/hotbar.cpp:1021` — `QStringLiteral("末影之眼")`（EndEyeId 用户可见显示名，tooltip 面）。同族 EnderPearl 已改原创「暗渊珠」（:1083），本条漏改；注释侧（world.h:126 等）对同一物品已统一称「暗渊之眼」，代码显示名却是 MC 官方中文译名——不一致 + 用户可见泄漏。
- 同族标识符（源码层，非注释）：`BlockRegistry::NetherPortal`、`tryIgniteNetherPortal/removeNetherPortalAt/breakNetherPortalsAround`（world.h:723-745）、`Kind::EnderEye/EnderPearl`、`spawnEnderEye`、`kEnderEye*` 族（entitymanager.h:336-1199）、blockregistry.cpp:727 行内键 `"nether_portal"`、`textures/default_end_portal.png`。
- **建议**：改名批把「末影之眼→暗渊之眼」列最高优先（用户可见面），标识符/键名次之。本窗口（e17325a..ca93b79）**未新增**任何 MC 词根——waterSurfaceFrac/rideRevision/fishXpGained/collisionTopY/kFishCatchPopOffset/burnout 族/plantGroundBlock 全原创。

### 低-2 supportTopYAt 收口的旧口径残留：夜行者瞬移两处
- **位置**：`src/Entities/entitymanager.cpp:3941`（teleportEntity 地面扫描）、`:3980`（teleportBehindPlayer）——`world->isSolid`（非空气即地面）：水/岩浆/花草/火把/火格均当选「地面」。
- **触发**：夜行者弹射物闪避/水逃逸/背后瞬移可落位在水柱/岩浆柱上方一格（resting 置位后下帧穿入：怕水者落水、岩浆烧伤）或无碰撞植物格（穿落到下层）。review26 #19 只收口狼/豹猫瞬移 + 刷怪四处，夜行者两处漏网。
- **建议**：同 #19 式改 `World::isCollidable`（或 supportTopYAt ≥0）。

### 低-3 findShadeTarget 支撑判 isSolid
- `entitymanager.cpp:107`：遮荫目标列的「下方支撑」用 isSolid → 花草/火把/火格当选可站遮荫位（亡灵走到后坠落一格 / 站进火格点燃）。t670 存量，非本窗口。

### 低-4 resolvePlayerPush resting 维持判与 tick 复探谓词分裂
- `entitymanager.cpp:4694` 用 isSolid，tick 复探 `mobFootprintHasSupport` 用 isCollidable（:225）。isSolid ⊇ isCollidable → 推向花草列时多保持一帧 resting，下帧才落（行为无害）；但 :4688 注释声称「与 tick 复探同公式…保证一致」在 t865 后只剩列/Y 公式一致、谓词不一致——注释声明过时，顺手对齐即可。

### 低-5 t898 睡觉 × 骑乘交叉无守卫
- `trySleepAt`（playercontroller.cpp:2745）与 placeBlock 床分支（:3105）均无骑乘守卫：骑矿车/船右键床可入睡；睡眠分支跳过 step()/tickVehicleRiding（:945-952）但 ridingIndex 保留；醒后 leaveBedTeleport 放床边，下一 step() 骑乘钉位把玩家拉回（可能远处的）车——视觉突跳。**建议**：trySleepAt 入口先 dismount（respawn :416-422 先例）。

---

## 二、通过面（8 项逐条）

1. **单一权威谓词族自洽**：`waterSurfaceFrac`（blockregistry.h:1779）四方消费（mesher chunkgeometry.cpp:723 / 浮标 entitymanager.cpp:5860 / 掉落物 itementitymanager.cpp:476 / 船 boatmanager.cpp:377 / 眼位 playercontroller.cpp:5993）全走单一权威，无残留固定偏移。`supportTopYAt` 消费面（mobAabbHitsSolid 脚位格豁免 :189-192 + isJumpObstacle feetY 7 调用点全传 / 物品 resting 两格窗 + hBlocked :613-617 + groundedRest 下半修 / 矿车 :465/:503 / 玩家列扫 :5414）全收口；`collisionTopY`（blockregistry.cpp:1719）特例表逐字对齐、world.cpp:565 慢路径委托。`redstoneDustPowersNeighbor`（blockregistry.cpp:1288）臂轴判据与 ✅ 描述一致（±X iff px||nx、±Z iff pz||nz、dot false）。残留旧口径仅 findings 2-4 三处（均存量）。
2. **暂停语义消费面**：worldRunning 派生（Main.qml:173-180：软档=面板/聊天/死亡屏白名单，硬档=ESC/非 playing）核对无漏网面板；WorldClock.running 直绑（:1756）；#11 21 处 Timer 复核——9 处 gate 落位（水/岩浆/火/门翻书 :1862/:1869/:1878/:1887、GlyphFlow spawn/tick :159/:251、EnchantRunes 两处改声明式退役 start()、BlockParticles :203、pageFlip :9231、爱心 NumberAnimation :6829），10 处豁免判据成立（一次性 toast/UI chrome/输入静态）；硬档停表 + tickImpl 早退 + 睡觉分支 checkCartEnvironment 补调（:951）在位。
3. **序列守卫收口**：src/ui 全量 Array.isArray 复核——显示侧（14 光晕 + 5 攻击行 + 2 锐锋）全真值守卫；写侧 list4 归一；残留 isArray 8 处（AnvilUI:141/:1478、CraftingTableUI:75、EnchantingTableUI:131/:336、Inventory:326、SurvivalInventory:90、InventoryOps:228/:814）全作用于本地 JS 数组或 readSlot/list4 归一输入 = 合法。t898-t903 未引入新序列消费面。
4. **emit/信号纪律**：rideRevision 专用通道（entitymanager.cpp:4883 pinMoved 值变帧才 bump，静止/无乘客零发射；Main.qml:6512 仅 position 一条绑定触碰）；entitiesChanged 保持 20Hz 相位门（:4864-4871）；fishXpGained 事件驱动；本批无每帧直发回归。
5. **分层与 IP**：qrhi.h/QShader/QQuickRhiItemRenderer 零命中（仅 2 处注释提 QRhiGpuTimer，非 include）→ §2-A 过；World 不 include Game/Entities、Entities 不 include Game、Renderer 不 include 上层；t880 ItemShapeGeometry 纯 QQuick3DGeometry + 只读 BlockRegistry，合规。IP 见 finding 1（存量）。
6. **✅ 标记真实性**：八条全部与 HEAD 一致——#1（脚位格豁免 + feetY 推广）、#4（值门 hit*FromRay + m_hitDist + attackMob 落回，:1189-1213）、#5（臂轴语义）、#8（clearAggro 参数门控 :3961，箭 :5080 / 浮标 :5815 显式 false）、#9、#11、#21（fishXpGained 1-6 + Main.qml:2713 路由 addXp+playPickup）、#25（applyHitKnockback/applyGolemLaunch 门只拦 m_dead，:2010/:2037）。附带核 #2/#6（m_torchBurnout 确定性整数计数 + 头段独立计时）/ #7（列扫弹出 + kFishCatchPopOffset）/ #10 / #20 亦一致。
7. **t898 睡觉链**：瞬移（床脚端 + bedPartnerOffset 解码 + 速度/击退/摔伤基准清零）/躺姿（sleepLying 派生门 + Waking 补发 sleepingChanged）/相机/回位（leaveBedTeleport 幂等门）四块核对；死亡链（onDied→release:541→cancelSleep→respawn:414 双保险）与存档往返（loadSavedState:466 cancelSleep）复位在位。骑乘交叉见 finding 5。
8. **t903 草丛收紧**：plantGroundBlock（blockregistry.cpp:1201-1213）仅 TallGrass→Grass；花（Dirt/Grass/Farmland）、蘑菇（Dirt/Grass）、枯灌木（Sand）口径不动；失撑 carve-out（world.cpp:2450 grassOnNonGrass）只对草丛、花/蘑菇保留 t507 Air-only；setWaterSilent 挂钩在位——无误伤。

## 三、结论

窗口本身干净：43 任务 + 25 修复的 ✅ 标记与代码一致、谓词收口无本窗口回归、分层/Timer/序列/信号四条纪律过。Findings 中-1 为存量登记债的执行提醒（建议下一批立即排期），2-5 为收口尾巴，无阻塞项。
