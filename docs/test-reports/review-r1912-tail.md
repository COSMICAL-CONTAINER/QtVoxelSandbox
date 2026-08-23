# R19.12 尾批架构审查报告（review-r1912-tail）

- 审查范围：git 56cb951..HEAD 中的尾批 14 提交——t789 13ade6f、t777 c02adf6、t778 8c98ed8、t779 4d68510、t780 9b70536、t781 a7cf3bd、t782 9e3a543、t783 db450cf、t784 602db76、t790 0fd157b、t791 89435a6、t806 1e8b577、t807 1727847（清单 13 项 + t789 计 14 提交口径；范围内另含 t792-t805 中段批，非本报告重点）。
- 审查方式：源码只读静态审查（Read/Grep/Glob）。⚠️ 本审查环境**无 Bash 执行工具**：逐提交 `git show` diff、行数统计、`redstone_matrix_test.exe` 实跑三项不可执行，以工作区最新源码 + dev-plan 任务记录 + 矩阵探针源码交叉佐证（见"矩阵终态"节）。工作区另有 5 个未提交 M 文件（entitymanager/Main.qml/ResourceBrowser/redstone_matrix_test 等，git status 快照后产生），审查按工作区最新态进行。

## 总结论：有条件通过（CONDITIONAL PASS）

无高危项。PLAN §2 硬不变量全部干净；分层纪律、单向事件流、确定性哈希、注释即契约、探针纪律均达标。两条中危项（契约钉法缺口 + IP 门标识符词根扩散）均有明确低风险修法，不阻塞合入，建议排入下一批修复。

## 不变量核验（全过）

| 项 | 结论 |
|---|---|
| §2-A RHI 囚笼 | PASS。全 src/ 无 `qrhi.h`/`QShader`/`QQuickRhiItemRenderer` 命中；仅 2 处**注释**提及 QRhiGpuTimer（Main.qml:11636、playercontroller.h:137，"待自研 RHI 迁移"设计说明），不匹配 CI guard 正则 |
| §2 分层铁律 | PASS。t784 BedModelGeometry（Renderer）include Core blockregistry + World partialblockgeometry = 向下（同 blockcube 先例，bedmodelgeometry.cpp:6 注释自证）；t791 PlayerController（Game）→ World::applyBonemeal 向下；t806 三方法自 PlayerController **下沉** World 层（修正依赖方向 + 矩阵可直调）；t789 Entities 发语义信号（mobDied/sheepSheared 携 woolIndex）→ 呈现层 QML 映射掉落，单向事件流；t780 mobEntityMap 提头声明入 Core resourcepackmanager.h，Core 不 include Renderer（注释互指 + 数值镜像既有模式，resourcepackmanager.cpp:26-29） |
| §2-K 确定性 | PASS。src/ 零 `rand()/srand/qrand`。t791 骰子 = hashVoxel(seed ⊕ useIndex·0x9E3779B9, x,y,z) 纯哈希零随机源（world.cpp:3335-3336），m_bonemealUseIndex 每有效使用 +1，作物/树苗/浆果丛三分支递进序号语义正确（树苗"使用即耗"先取值后 +1，world.cpp:3355 注释钉死）；t786/t787 地牢池 hash 加权。t789 羊毛色用 QRandomGenerator（entitymanager.cpp:349）——运行期实体随机（同 ambientTimer/t377 护甲既有模式），不涉 worldgen golden，不算违规 |
| §2-G EventBus | PASS。mobDied/sheepSheared 信号追加尾部参数，旧连接兼容 |
| IP 门 §9 | 见中危 #2（标识符词根）；用户可见字符串全中文区隔名（余烬门/暗渊之眼/夜行者/燃烬者/暗渊珠）✓；资产仅运行期读用户 gitignored pack、零 bake 进 qrc/VCS（resourcepackmanager.h:214-215 红线注释）✓；"nether_portal"/"wolf/wolf.png"/"enderman/enderman.png" 等为 pack 格式兼容键（读用户包的寻址路径，非自带资产、非用户可见），按项目既有政策豁免 |

## 问题清单（按严重度）

### 中危

**#1 [t789] QML 字面量 63..77 / 0x20E 跨层契约未钉**
- 位置：`src/ui/Main.qml:1960-1962`（`sheepWoolDropId`：`(woolIdx > 0) ? (63 + woolIdx - 1) : 0x20E`）
- 描述：掉落映射的 63（=BlockRegistry::WoolOrange=FirstWoolVariant）与 0x20E（=RecipeRegistry::WoolId）是全工程唯一持字面量处，仅有注释互指；recipe.cpp 的 static_assert 家族（CoalId 0x201、DyeIdBase 0x24B 等 12 条先例，recipe.cpp:1161-1189）**缺这两条**；矩阵 t789 探针注释明言"QML 层 sheepWoolDropId 映射在呈现层，C++ 锁信号载荷正确性"（redstone_matrix_test.cpp:3803）——即 63..77 → 方块 id 的映射**零编译期/探针锁**。若羊毛变体段迁移（新方块插段挤压 id），剪/杀有色羊将静默掉错方块且矩阵全绿。
- 建议修法：recipe.cpp 补 `static_assert(RecipeRegistry::WoolId == 0x20E)` 与 `static_assert(BlockRegistry::WoolOrange == 63)`（注释互指 Main.qml sheepWoolDropId，同 CoalId 跨层钉法）。QML 无法 import constexpr，C++ 侧断言即项目标准解。

**#2 [t806/t807，遗留扩散] IP 门：MC 专有词根标识符 NetherPortal / EnderEye / EnderPearl**
- 位置：`src/Core/blockregistry.h:1029`（`NetherPortal = 138`）、`src/World/world.h:623/630/637` 与 world.cpp:2482-2590（`tryIgniteNetherPortal` / `removeNetherPortalAt` / `breakNetherPortalsAround`）、`src/World/chunk.cpp:57` 等 8 处、EntityManager 的 `spawnEnderEye`/`EnderEye`/`EnderPearl` kind 族、Main.qml t807 delegate（`endereyeNode`/`enderpearlNode`）
- 描述："Nether"（下界）/"Ender"（末影）是 MC 生造专有词根，按项目自身 §9 区隔政策（Blaze→Emberling、Enderman→Nightwalker、Creeper→Stalker，entitymanager.cpp:3776 注释自证该政策存在）不应作为自有代码标识符。用户可见名已全改（余烬门/暗渊之眼/暗渊珠），英文标识符与可见名不一致 = 政策执行缺口。**本批扩散点**：t806 下沉时新增/命名了 3 个以 NetherPortal 为词根的 World 公有方法；t807 沿用 EnderEye/EnderPearl delegate 命名。主要存量来自 t725/t729/t757（非本批引入）。
- 建议修法：排一个纯重命名任务（如 NetherPortal→EmberGate、EnderEye→VoidEye、EnderPearl→VoidPearl，全工程机械替换 + 编译验证；pack 兼容字符串键 `"nether_portal"`/`"ender_eye.png"` 等**保留**——那是读用户包的功能性寻址元数据）。不阻塞本批。

### 低危

**#3 [t784] bedHalfBoxes 无矩阵探针**
- 位置：`src/World/partialblockgeometry.h`（bedHalfBoxes 声明）
- 描述：床盒数学下沉单一权威是对的，但"逐字承值零行为差"论证无回归网。矩阵 target 已链 partialblockgeometry.cpp（CMakeLists.txt:783），补一个床盒数/边界/AABB 探针成本极低（P11 直调模式），却未加（纯视觉豁免理由对 World 层纯函数不成立）。
- 建议：下一批补 1 探针（双半盒计数 + kBedHeadboardTop 顶界 + facing 0 的 foot/head 邻格关系）。

**#4 [t806] removeNetherPortalAt 种子前置条件未断言**
- 位置：`src/World/world.cpp:2543-2577`
- 描述：BFS 对种子格宽容（isSeed 旁路门格检查，2560-2561），当前两调用点（breakNetherPortalsAround / finishMiningAt）均先验门格，但作为 World 新公有方法攻击面的一环，无 debug 断言/头注释前置条件标注。BFS 线性查重 O(n²) 在 ≤20 格门域无碍。
- 建议：world.h:630 注释补"种子须为门格或刚被清的门格位"前置条件。

**#5 [t783] variantPanel 区域手势竞争（观察项）**
- 位置：`src/ui/ResourceBrowser.qml:1185-1286`
- 描述：悬浮面板 z:10 覆盖 previewArea 下沿 ~1/3，该区域 3D 拖拽（previewDrag DragHandler 挂 previewArea，682 行）与面板 MouseArea/TapHandler 竞争——面板位拖模型手感受遮挡，属可接受 UX 债；其他条目 visible 门控（1187）+ 锚定不参与 Column 布局 = 零回归，右列 360≤366 布局不变式注释已钉（1309-1311）。目视清单已含，无需代码改动。

**#6 [流程] 矩阵终态未能现场复跑**
- 描述：审查环境无执行工具，见下节。建议主 Agent 在可执行环境补跑一次落 `matrix_review.log` 存档。

## 逐提交要点

| 提交 | 判定 | 备注 |
|---|---|---|
| t789 13ade6f 羊自然色 | PASS（中 #1） | 权重表 6 色万分位单源（entitymanager.cpp:47-50，Σ=10000 兜底白防越界采样）；信号载荷 woolIndex 全链（shear/mobDied/繁殖继承）矩阵已锁 |
| t777 c02adf6 羊模型 | PASS | 羊皮腿独立 Model #d6b890（ResourceBrowser.qml:876-909）、sheepWoolFaceActive 单一权威 + 密闭探针（temp PNG rig 不实例化 BuiltState，防宿主机 pack 态泄漏） |
| t778 8c98ed8 鱿鱼 | PASS | 尖顶盒几何层整删 + 三渲染端眼层统一删除；纯视觉零探针有 t778 先例论证 |
| t779 4d68510 头像裁剪 | PASS | mobHeadIconLayout 单一权威出口（防生成器/探针漂移）+ 覆写盒 NSDMI 缺省零改动扩表；探针锁布局常量 + 端到端 |
| t780 9b70536 狼豹猫 | PASS | mobEntityMap 匿名 ns 拆段提头（同 TU 未命名 ns 符号互通，ODR 无险）+ 蠹虫 14 不入表防误采（探针锁）；豹猫驯服态恒程序贴图的边界诚实 |
| t781 a7cf3bd 夜行者模型 | PASS（激活依赖 t782） | 细肢人形参数表 + hitbox 吻合核验（±0.35/halfH 1.40）在注释闭环 |
| t782 9e3a543 燃烬者+白名单 | PASS | 白名单修复（mobmodel.cpp:333-336 根因注释完整）连带激活 t781/t728 死代码——跨任务连锁风险点：三消费端（delegate/图鉴/笼迷你）共享 MobModel 几何，一次激活三处同步生效，冒烟 2 轮 + t781 六项目视清单一并验收即可；rodSpin Q_PROPERTY 量化 6°/步防每帧 rebuild（性能守卫在位） |
| t783 db450cf 浏览器悬浮面板 | PASS（低 #5） | 单根因（Column 溢出 454>366 + footer 后声明兄弟盖点击）定位扎实；rgba 底板而非 Item opacity（不连带淡化子控件）正确 |
| t784 602db76 床模型 | PASS（低 #3） | CMake 模块归属正确（Renderer 段 CMakeLists.txt:84-85；矩阵 target 不链 Quick3D 故不含此文件，合理）；图集 UV 单一权威半纹素内缩（t54 教训执行）；TexCoord0Semantic 在位（t757 教训执行） |
| t790 0fd157b 成就弹窗 | PASS | 右下 dock + 全屏透明点击吸收层（modal 语义保持）+ Esc Shortcut 窗口级兜底；"progress+stats 双开不可能"副作用 dev-plan 已自述（可接受） |
| t791 89435a6 骨粉 | PASS | 数值全收口 World::applyBonemeal（playercontroller.cpp:3423-3443 只管分流/消耗/挥手，分层正确）；3-4 骨粉催熟有数学保证（3 骨粉推进和 ∈[6,9]≥7）；守卫负例（石支撑/主干阻塞仍消耗返 true）探针覆盖 |
| t806 1e8b577 传送门 | PASS（中 #2、低 #4） | 框检测四步全有界（越界 blockAt 返 Air 自然判败，world.cpp:2481 注释钉）；宽 2..4/高 3..5 算术核验无 off-by-one（wRight 循环界 kMaxW-1 恰覆盖点燃列在最右）；四角不查（MC 1.0）与 6 邻扫不到对角格自洽；异轴门 state&1 隔离；12 场景探针含点燃位无关性 5 位轮询 + 破角不碎门负例 |
| t807 1727847 投掷物模型 | PASS（中 #2 标识符） | billboard + MaterialIcon 两级图源单一权威；alpha 契约（alphaCutoff 0.5 + opacity 0.99）沿掉落物段；同族排查结论（雪球/蛋/火球/箭无同病）钉进注释防复刻 |

## 矩阵终态

- **未能现场执行**（审查环境无 Bash）：`redstone_matrix_test.exe` 实跑 192 PASS / 0 FAIL 待主 Agent 在可执行环境复跑落 `matrix_review.log` 确认。
- 静态佐证：探针 t806 十二场景完整在位（redstone_matrix_test.cpp:4409-4617，含超限拒/低于最小拒/缺梁柱拒/非矩形拒/破框碎门/破角健在）；文件以 `=== total FAIL: N ===` + exit(N==0?0:1) 汇总（4619-4620），PASS 计数需外部 grep 统计。dev-plan 记录 t806、t807 两轮均为 **192 PASS / 0 FAIL**（t807 零新探针持平）；本批 14 提交中带新探针者 t789(+1)/t791(+1)/t806(+1)，其余增量来自范围内 t792-t805 中段批。注意：工作区 `tools/redstone_matrix_test.cpp` 当前有未提交改动（git status M），实跑前应确认 HEAD 干净或先提交。

## 统计

- 提交数：14（尾批；范围 56cb951..HEAD 另含 t792-t805）
- 文件数 / ±行数：**无法获取**（无 git 执行工具）——由主 Agent 以 `git diff --stat 56cb951..HEAD` 补入
- 探针增量：186 → 192（本批贡献 +3：t789/t791/t806；余 +3 来自 t792-t805）
- 问题数：高危 0 / 中危 2 / 低危 4
