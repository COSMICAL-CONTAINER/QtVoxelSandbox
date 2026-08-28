# 架构测试报告 — Review27 修复批 + R19.16 收尾终审

- **窗口**：6b5ea26..09a221a（HEAD = 09a221a，49 提交；docs 提交跳过）
- **范围**：① Review_2026-08-27.md 25 条处置抽查（#1/#2/#3/#5/#7/#10/#11/#13 亲核）；② R19.16 t904-t932 全 29 项，重点面：t930 级联、t907-t909 矿车、t923 狼生态、t914/t915 附魔视觉、t926/t927 钓鱼、t904/t905 插桩、分层/IP/RHI 门、emit 纪律、✅✅ 标记抽样（t904/t909/t917/t919/t923/t926/t930/t931 八项亲核与代码一致）。
- **基线**：矩阵 344 PASS / 0 FAIL 已由主 Agent 亲核；t882/t891 flake 复跑清，未复验。

## 判定：PASS

## Findings

### 低（2）

| # | 位置 | 问题 | 触发场景 | 建议 |
|---|------|------|----------|------|
| L1 | src/World/world.cpp:2851-2879 cascadeGravityAround | **级联坍落的逐柱 emit 放大**：BFS 每坍一柱各调一次 dropGravityColumn，每柱自带 worldChanged+clearAllDirty + 逐格 blockBroken/gravityBlockFell/recomputeLightAround/recheckAttachmentsAfterClear。大范围人工悬空沙阵（数百柱）一次编辑 = 单帧尖峰 + 下落实体 kCap(64) 溢出 qWarning（超帽沙既非实体也非方块 = 物料蒸发）。终止性/正确性无虞（栅格单调减、Air 复扫跳过、setBlockFromEntity 不回调 check 族，world.h:667-671 论证成立；MC 同为 block-update 级联） | 玩家搭大悬空沙结构后挖一格支撑 | 若实测出现尖峰：把 worldChanged/clearAllDirty 上提至 cascadeGravityAround 末尾单发（跨柱批量收口，同 destroySphereSilent 先例）；kCap 溢出降级为静默计数 |
| L2 | tools/redstone_matrix_test.cpp:17653（review27-5 探针） | `cntBranch == 4` 计数钉依赖 `m.mobType == MobNightwalker` 全文恰 4 处的格式恒等（新增第五条投射物闪避入口或改写任一处写法即翻数强制同步——设计意图即如此，但同时把无害的等价改写也拦成 FAIL） | 后续重构该判据表达式写法 | 可加注释说明计数钉只认字面写法，重构时须同步；非必改 |

### 未发现问题的重点面（亲核通过）

- **t930 级联终止性/交互**：谓词与 ① 同源（下方非完整立方/世界底）；环/双向触发不死循环（重力格单调递减，无 visited 亦收敛）；柱顶上格入队覆盖间隙链；recheck 脱落物（火把/活板门/铁轨）上方不可能有合法路径放置的重力方块（放置即被 ① 坍落）——论证与代码一致；t734 失撑链/paintings 经 recheckAttachmentsAfterClear 收口；探针 P-t930 五腿（含阴性 D1/D2）与实现吻合。
- **t907**：clampShift 同层闸（colRailY 相等）+ 腰位闸（落点中心格非实体）+ 冲量钳 ±kCartBoostSpeed + tickDerailedCart ≤0.45 子步（while 严格递减必终止，无死循环）；t864 卡阻契约探针经 t909② 语义适配（10333 行注释钉）。
- **t908**：轨向投影分解绝对值口径（沿轨后退合法）、kCartPushProjMin 0.3 > dir 兜底 0.25（纯横推必拦）、死端推离改轨轴符号向、孤轨/地面车不分解（既有语义钉）。
- **t909**：kCartSlopeGravity=28×0.70711 与世界重力同源；静置闸（行进侧下坡起步/背向下坡翻向，平/上坡/死端维持停驻）不破「平死端停靠」与 t735④「静置空车不被动力轨弹射」（两闸先后序正确）；V 形永动探针（≥6 次反转 + 负验证）在案。
- **t910/t911**：动力轨链 BFS 定深 <8 + goldenPowered set 去重（环轨不死循环）、railProbeDelta 三高探针 same 优先、降沿 goldenSeenAll 对称收缩；仙人掌整柱口径 = 下探柱基再 dropCactusColumn（柱中段命中全柱坍落），铁轨非法邻面经 ④ 非空门天然覆盖。
- **t904/t905 插桩**：main.cpp 四段 hook 无双连接（旧 qmlSync connect 并入、注释钉死）；0 值守卫 = 漏 hook 自诊；FrameProfiler 1s 窗聚合非逐帧 QML emit；mob 段手动 nowNs 2-3 次/实体（~25ns）开销声明成立；biomeAt 列级 memo 契约探针在案。
- **t914/t915**：faceTimer running 并 worldRunning 门（review27-13 gate 未被 bookOpen 破坏，探针 count==1 钉）；迟滞带 4.0/4.4 防抖；flutter 声明式互斥 `worldRunning && bookOpen && !pageFlipAnim.running`；EnchantRunes（彩色立方）与 EnchantGlyphFlow（白字形流）两套并存、双方头注释互钉；GlyphFlow 性能红线四件（池 48/maxPerTick 5/16 格距离门/0.55 字每书架）齐备。
- **t923**：浮面门 = 头格浸水（pos.y+halfH×0.8，与溺水同式「会淹才游」）；鱿鱼特例分支在前不受影响；浅水跋涉（仅脚位浸水）保持 t298 缓沉无振荡；rise 钳只作用浮力累积（泳跃 vy 直设穿过）；wolfRetaliateAgainst 守卫链（活体驯服狼 + 活体 mob 攻击者 + 去重）齐备，爆炸者自毁无活体不注册（caller 不调）；骷髅箭滤网 t712 扩狼/火球 slot+serial 双查两接线点源码钉；豹猫 no-op 阴性腿在案；t480 主人受击链未动。
- **t925**：itemshapegeometry 扩面（栅栏/门/机关/cross 15 id）限 src/Renderer 内、消费 Core BlockRegistry 单一几何源（mechBoxes 同源、mesher 盒区常数同值），分层向下无越界。
- **t926/t927**：kBobberBiteWindowSec=1.0（用户口径覆写在常量注释+探针双钉）、kMirrorBiteWindow 同步 1.0（P18）；t927 落差解算常量组（28/0.6/1.3/1.0/0.55）注释完整、kFishHookLiftBase/LiftGain 退役留碑、重型 halfH 质量口径与 mobType 表一致（铁傀儡 1.20/夜行者 1.40 折扣，猪 0.45 不折）；探针高台/折扣/耐久三断言与代码推算吻合。
- **t913/t912/t929/t931/t932**：kDispenserCooldown=0.2 constexpr + 插入点 + 探针窗 (0.1,0.3] 三段（t868 上升沿语义不动）；发射器矿车分支（轨上定向/地面静止/堵口 spoutCellClear 降级/投掷器兜底）四路齐；峡谷置源 pass 退役 + kDrainRadius 排水带 + 6 seed 探针；DurabilityBar/SurvivalInventory armorDurBar/数字 Text/Main.qml hotbar 四面 `curDur < maxDur` 与源码钉逐一相符；t932 四材质 Material.AlwaysDepthDraw（Qt 6.11 正确枚举名）blend 契约不动、纯视觉 t781 先例零探针已声明。
- **Review27 抽查**：#1 sleepLying Q_PROPERTY 在属性区（h:263）+ Main.qml:4712 属性消费 + QMetaObject 探针改法；#2 清锚 onPressed（Main.qml:12195-12197）+ userMoved 旗 + drag.target 接线 + treeViewport 口径钳位/Connections 缩窗钳回五要素全在位（t928 补 drag.target 入钉）；#3 站顶分支 Y 界定 + t843 快速路径同款 Y 界（7414/7423）；#10 mob 侧同构站顶分支（6483-6496）+ 中心列快速路径 Y 界（6502）+ 误导注释重写；#5 四链 clearAggro=false（箭/雪球/蛋/浮标）+ 近战默认 true + 四入口计数钉；#7 烧尽终局 recheckAttachmentsAfterClear + 门格豁免（1702 `!isDoor(id)`，湿/雨守卫分支专用）——与 ✅ 标记一致。
- **分层/RHI/IP/emit 门**：全窗口新代码无 include 上行（FrameProfiler 自 Core 向下；EnchantGlyphFlow 只读 World blockAt）；`qrhi|QRhi|QShader` 仅存两处注释性提及（未来工作引用），零 include，§2-A 围笼完好；新标识符（cascadeGravityAround/kCartSlopeGravity/kCartSlopeKick/kCartPushProjMin/belowId/kBobberBiteWindowSec/kFishHookLift 族/wolfRetaliateAgainst/tierSeed/optionReroll/bookOpen/EnchantGlyphFlow）全原创，无 MC 专有名词入标识符或用户可见字符串（resourcepackmanager 的 assets/minecraft/... 路径串为读取用户自带资源包的功能性元数据 + §9 改名映射注释，系窗口外既有基线，非本批引入）；无每帧直发 QML 信号回归（本批 emit 全为事件级/批量收口）。

## 总体判定

**PASS**。窗口内 25 条 review27 处置与 29 项 R19.16 交付在抽查面上与代码、探针、dev-plan ✅✅ 三方一致；两条低危 finding 均为性能/维护性建议，无不变量违反、无跨层依赖、无 IP 泄漏、无分层破坏。待用户目视项（t914/t915/t923/t928/t919 等）按 dev-plan 标注由实机确认后关单，不影响本判定。
