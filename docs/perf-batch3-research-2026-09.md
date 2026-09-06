# 性能批三调研（t1023，R19.20）—— meshing 线程 / draw call 折叠 / 平滑光照

- 日期：2026-09-06；基线 HEAD=357e617（矩阵 457 PASS / 0 FAIL）
- 口径：t1023 弹性大项 = 调研 + 首批可落地项。某项调研后投入产出比不佳/风险大 → **如实登记不硬做**；
  首批落地以「小步、可测、可回退」为准。
- 回标：t906（sync meshing 疑点）、t1007（7FPS 三角对表根因 + 治理路径 a/b/c）、t858（经验球
  instancing 先例 + 掉落物取舍）、Vulkan/SIMD survey 2026-08-21（C1/C3 线程化前置 + Android P2）。
- 首批落地（本批已实现，见 §4）：QML 永停表动画 `running: visible` 门控（4 条）+ AO 环境光遮蔽
  单项（`ChunkGeometry.aoEnabled`，默认关）。

---

## 1. meshing 线程池核实（t906 回标）

### 1.1 现状核实（本批复核，结论与 t906 一致：仍全程 GUI 线程同步）

- F3 行 `threads: 0/0 (sync meshing)`（Main.qml F3 buildF3Text 段）是 **t906 钉死的现状事实字符串**，
  非活测量——t906 已核实：src/ 全树无任何线程原语。
- 本批重新 grep 复核：`QThreadPool|QThread|QtConcurrent|QFuture|moveToThread|std::thread|std::async`
  在 `src/**/*.cpp|*.h` **0 hit**。`0/0` 不是「线程池退化/未启用/配置门槛」，而是**从未有过 worker**：
  ChunkManager 是纯容器，mesh 由每 chunk 的 ChunkGeometry（QQuick3DGeometry 派生）的同步直连槽驱动。
- 驱动链（全部 GUI 线程）：
  1. `World::setBlock` → `emit worldChanged` → 每 ChunkGeometry::onWorldChanged（直连）→ dirty-gated
     `buildMesh(Dirty)`（重建同步于编辑，t155 验收「破块贴图 <1 帧消失」的语义锚点）；
  2. `setSunDir`/`setDayMul`（WorldClock 10Hz）→ `sunRebuildDue` 量化门（仰角/方位/dayMul 阈值 + 硬顶）
     → `buildMesh(Sun)`；
  3. 段开关 setter（shadows/greedy/cutoutFolded/t1023 aoEnabled）→ `buildMesh(Dirty)`；
  4. t972 渐进同步队列（Main.qml kickWorldMeshSync/_meshSyncTimer）→ `refreshMesh()` 排空窗外欠账
     （近→远、每帧限量）。

### 1.2 已缓解面（survey 后各批的累积成果，残余成本的真实边界）

| 缓解 | 效果 |
| --- | --- |
| t472 视距门控 | 窗外 chunk 跳过一切 buildMesh（600→154 段零 FPS 提升的真因根治） |
| t188 流体专用脏跳过 | 水流风暴时 terrain 段免陪跑 culled/greedy 全扫 |
| t489 水动画材质化 | 消灭「2s 一次全量水段重建」第二风暴根因（Swamp 261 段/次） |
| sun/dayMul 量化门 | dawn 太阳步进 10fps 教训（survey 实测 mesh 116–427ms 窗口）后重建稀化 |
| t860 段折叠 | 每 chunk 6 段 → 5 段（500 Model 满配） |
| t972 欠账 + 渐进排空 | 载入/窗外重建按帧预算摊平，不再单帧全量 |

- 残余：**编辑/载入当帧的 buildMesh 仍在 GUI 线程串行跑**（脏 chunk 数 × 每 chunk 三重扫描 +
  per-vertex PCF）。survey 实测高水位窗口 mesh 桶 116–427ms——这就是 `sync meshing` 的残余成本面。

### 1.3 改造面评估（结论：工程量大，登记不硬做）

survey §C1/C3 已列硬前置，本批逐条对现状确认仍成立：

1. **数据竞争（最硬）**：World 零锁（全仓零线程）。worker meshing 读 world 与 GUI 写（setBlock /
   流体 tick / 实体写入）并发即 UB。前置二选一：PLAN 不变量 C（per-chunk shared_mutex）或 C3
   「chunk+halo 扁平快照」（GUI 线程拷 18×H×18 voxels/states/light ≈ 3×32KB 连续数组/ chunk，顺带
   消除逐格 ChunkManager 路由开销，也是 SIMD① 的底座）。
2. **编辑语义重定义**：现状重建同步于 setBlock；异步化后「破块 <1 帧消失」要么变一帧延迟（可接受
   需用户确认），要么本地直插快照（复杂化）。t972 渐进队列与 worker 完成队列要合并成一条排空链。
3. **QQuick3DGeometry 线程规则无明文**（survey 原话）：安全形 = worker 产 move-only 顶点/索引
   QByteArray（chunkgeometry.h 不变量 B 注释已为此留形），GUI 线程 apply（setVertexData + update）。
   apply 风暴要按帧预算限流。
4. **生命周期**：chunk 卸载 / 换世界 hardReset vs in-flight job（结果作废标记）；sun/dayMul 量化门
   参数要随 job 下发，catch-up（chunkInRange false→true）语义迁移。
5. **量级**：survey 估「后台 meshing + 单遍多段 + SIMD」包 4–10 周；worker meshing 同时是 Android
   readiness 的 P2 前提（survey/记忆先例）。

**建议路线**（独立任务，不进小批）：C3 halo 快照底座（纯主线程、可独立验证提速）→ C1 worker 化
（QThreadPool + 脏 chunk 队列 + 帧预算 apply）→ SIMD① 顶点批填充。每步独立可回退。

**本批动作**：加 **P-t1023-c 源码钉**（矩阵探针扫 src/ 零线程原语 + Main.qml F3 事实行共存）——
未来任何人线程化 meshing 时，F3 行与探针必须同步更新，防「F3 谎报 0/0」。

---

## 2. draw call 折叠（t1007 治理路径 a/b/c）

### 2.1 动画烧帧现状（t1007-b 的核实与本批落地）

QML `Animation on property` 永不停表：`visible: false` 不暂停动画（t561 火焰教训同源）。本批逐点核实：

| 位置 | 动画 | 恒跑规模 | 本批处置 |
| --- | --- | --- | --- |
| itemHost Repeater delegate（≤200 槽，kCap=200） | `NumberAnimation on rotY`（360°/3s）+ `SequentialAnimation on bobY`（2s） | 高水位 200 槽 × 2 = **400 条**（含空槽/拾取后隐藏 delegate） | ✅ 首批落地：`running: entRoot.visible` |
| mob delegate 47 槽 endereye/enderpearl 子树 | `NumberAnimation on spin` ×2 | 2 × 47 = **94 条**（kind 不符的槽也烧） | ✅ 首批落地：`running: <node>.visible`（kind 门控复用） |
| mob delegate TNT 闪白 | tntFlashAnim | 仅 primed（t492 显式 start/stop 模式，slot 复用即停） | 无需改（已门控） |
| 附魔掉落壳呼吸 | `running: entRoot.entHasEnch` | 仅附魔活体 | 已门控（t696，本批门控的 established 模式出处） |
| blaze 火舌 flicker（Loader 子树）等其余 per-mob 动画 | flicker 等 | 随 delegate 隐藏仍跑（量小） | 登记后续（§2.3） |

- 收益：最坏 ~494 条无限动画 → 只剩可见活体的条数；QML 动画 tick 是 GUI 帧固定烧量，随隐藏
  delegate 数线性消失。用户 7FPS 场景（items 93/93 + mobs 高水位）直接受益。
- 风险 ≈ 0：隐藏态动画本就无视觉消费；槽复用重显时 `from` 重启 = 「新实体从 0 转起」的正确语义。

### 2.2 掉落物 per-family 单 Model instancing（t1007-a；设计面登记，不硬做）

- **收益面**（t1007 三角对表）：items 93/93 高水位时每 item delegate 内联 ~12 个 geometry/material/
  Texture 实例互不共享 + billboard 段 per-item `MaterialIcon { sourceItem: Canvas }` 纹理 → draw-call
  数 ∝ 活体数，93 item + 47 mob ≈ 数百 draw 叠 202 地形段 → 撞 GPU/present 预算 = 7FPS 主因之一。
  instancing 后每「几何×材质」桶 1 draw（t858 经验球已实证 1 draw/全族 + F3 drawCalls 可观测）。
- **复杂度面**（xporbinstancing.h 头注释登记的取舍，本批确认仍成立）：
  1. 三族异构：billboard 平图标（MaterialIcon 源纹理）/ 3D 方块立方（图集 per-face UV）/ 工具几何
     （PickaxeGeometry）——单 Model 单材质实例表无法覆盖，需按 itemId **分桶**动态建 Model+Texture；
  2. per-instance 数据面：位置/自转/浮动动画（下沉 C++ 解析式，t858 已有同款）/ 天光 tint（per-instance
     color 可载）/ 附魔紫晕呼吸（opacity 动画 → 需 customData 或分桶加壳 Model）；
  3. 契约链：slot-reuse/hardReset（t170/t256/t437/t492）全在这条链上；F3+B 碰撞箱调试叠层读
     `entRoot.bobY/rotY` 自发动画态——instancing 后该调试面数据源要换（feeder 暴露或随动实例表）；
  4. alpha 契约：billboard 段 alphaCutoff 0.5 + opacity 0.99（透明底 discard）须逐桶保持（D3D11 红线）。
- **建议分步**（独立任务，周级）：① 方块段先行（单一 UnitCube 几何 + 图集 UV，最接近经验球先例）→
  ② 工具几何桶 → ③ billboard 段（需静态预渲染图标图集或 texture array 前置，兼收 t183 贪婪死结）。
  拾取/合并/LRU 判定纯 C++（ItemEntityManager）不受渲染层影响，行为级探针可密闭（t858
  probeFirstInstancePosition 先例）。
- **低风险过渡微步**（登记，可先行小批）：per-delegate 的 geometry/material 声明提升到 itemHost 层
  共享（QML 资源共享，不改 draw 结构、不减 draw 数，但砍 per-item 实例创建/prep/上传成本）。

### 2.3 mob 族同向（t1007-c）

- 渲染侧 draw ∝ 活体 mob 仍是开放面，但 48 模板逐部件动画不适用 instancing（t858 调研已明确排除）。
- 本批做了动画门控同向（spin ×2）；后续可排查：Loader 子树常驻动画（blaze 火舌 flicker 等）加
  `running: mobDelegate.visible` 族门控；MobModel 几何重建已有量化门（t935 分账保留）。
- delegate 绑定成本已由 t935 槽位指纹差分收口（bump≪fan），本批不重动。

---

## 3. 平滑光照 / 光照增量调研

### 3.1 现有光照模型（改造成本的基准）

- 光场：`World::recomputeLightField` 双通道 BFS（天光列种子自顶向下首个 lightOpacity>0 截断 + 方块光
  lightEmission 种子；t334 半砖/合活版门遮光语义），存 chunk 第三数组；玩家编辑走增量 refloodBox。
- mesher 顶点色（voxellight.h 单一权威，mesher 与 BlockCube 掉落沙共用）：
  `vc = clamp(max(sky/15 × (1 − PCF软影) × dayMul, block/15), 0.08, 1.0)`。
  - **per-face 平坦**：sky/block 采「面所朝邻格」单格——一面 4 顶点同色（这正是「平滑光照缺位」的
    观感来源：面片间亮度跳变、无接触暗角）；
  - PCF 软影（t153）已是 **per-vertex**（heightmap 正交深度图 2×2，kMaxShadow=2），方向性阴影已有；
  - dayMul 只乘天光分量（PLAN §2-H 夜间火把不变暗）。
- 段覆盖：地形段 culled 主路径（+ greedy 默认关）+ 流体变高面 + PASS 1 异形/cross（PartialLightCtx
  6 面上下文）。各段光采样相互独立。

### 3.2 全量平滑光照（MC smooth lighting，角点 4 格光场平均）——登记路线，不硬做

- 改造点：culled 主路径每面 4 角点各采「邻格 + 两侧 + 对角」4 格 sky/block 平均（透明格剔除规则），
  顶点色逐角点分化。成本/风险：
  1. 光采样 ~4×（每可见面 1 对采样 → 16 对），blockAt/skyLightAt 走 ChunkManager 路由（无 halo 快照
     前）逐格路由开销放大；
  2. **greedy 合并键冲突**：合并键 (tile, sky, block) 不含角点光——平滑化后合并 quad 四角同色假设
     破产（键须扩 4 角点光 → 合并率大跌，或 greedy 与平滑互斥）；
  3. 视觉全场景回归：明暗观感全变（与 t183「贴图拉伸不可接受」同敏感度），必须实机目视 + 可一键回退；
  4. 与 PCF 软影叠加后暗部压叠语义要重调（kVcMin 地板与角点平均的相互作用）。
- 建议与「纹理数组（自研 RHI，PLAN §2-I）」同批评估：两者共享「顶点格式扩字段」改造点
  （survey B1 顶点格式 `{pos, nrm, uv, tileIndex, ao[4]}` 已预留 ao 字段位）。

### 3.3 AO 环境光遮蔽单项（首批落地，本批实现）

- 定位：平滑光照的**几何接触阴影子集**，不动光场语义（sky/block 采样不变、dayMul/PCF 不变）——
  风险面最小、观感收益最直观（墙根/拐角暗角）的单项，即 t1023 规格点名的「仅环境光遮蔽 AO 单项」。
- 实现（三文件，全部小步）：
  - `voxellight.h`：`kAoFactor[4] = {1.0, 0.8, 0.6, 0.5}` + `aoCornerFactor()`（经典 MC 规则：两侧
    邻格同时遮挡钳 3，防薄墙/凹角三重压黑）——曲线单点定义；
  - `chunkgeometry.{h,cpp}`：`aoEnabled` 属性（**默认 false 出厂关**）+ setter（Dirty 重建 + t472
    视距门控，同 setGreedyMeshing 形）+ 地形段逐格 culled 路径角点三探针（遮挡判据 =
    `occludesNeighborFace`，与邻面剔除同一谓词——满格不透明立方，叶不遮）；
  - Main.qml：`window.aoEnabled` + terrainGeo 绑定 + ESC 视频设置开关行（同 t166b 阴影开关 Row 模式）。
- 语义钉死：AO 乘在光场钳制**后**（接触阴影允许低过 kVcMin 地板——暗角即压暗语义，非光场分量）；
  范围仅地形段逐格 culled 路径（greedy 合并键不含 AO、流体水面观感未验、异形/cross 段走
  PartialLightCtx——均不采样，与 §3.2 一起登记后续）。
- 回退杠杆：`window.aoEnabled=false` 一处（顶点色与 t1023 前逐字节一致——零开销旁路，矩阵探针
  行为级钉死 round-trip）；出厂默认翻正待用户实机目视后另单。

---

## 4. 首批落地清单（本批 fix）

1. **QML 永停表门控**（t1007-b + c 最小步）：item delegate rotY/bobY `running: entRoot.visible`；
   endereye/enderpearl spin `running: <node>.visible`。共 4 条动画、零新对象、复用 t696 established 模式。
2. **AO 单项**（t1023 §3.3）：默认关；ESC 可开；greedy/流体/异形段不采样。
3. 登记（不硬做）：meshing 线程池（§1.3 路线）、掉落物 instancing（§2.2 分步）、全量平滑光照（§3.2）、
   其余 per-mob 常驻动画门控（§2.3）、过渡微步 per-delegate 资源共享（§2.2 末）。

## 5. 探针与验证（P-t1023，offscreen）

- **P-t1023-a 源码钉**（Main.qml 注释滤除后）：4 条门控文本存在（rotY/bobY 绑 `entRoot.visible`，
  endereye/enderpearl spin 绑各自节点 visible）——任何一处被删即红（t860 源码钉先例）。
- **P-t1023-b 行为腿**（ChunkGeometry 直驱，t860/t972 先例；vertexData 直读，t965 先例）：rig 清场
  3×3 平台 + L 形双墙确定性场景 → aoEnabled=false 时目标面 4 角点同色（平坦基线）；=true 时角点
  精确 = {0.5, 0.8, 0.8, 1.0}（钳 3 / 单侧 / 无遮挡三档曲线值直钉）；false→true→false round-trip
  顶点缓冲逐字节一致（可回退行为级）。面隔离口径：同角位常有多张共角顶面（邻格平台块/树冠叶顶，
  各面光场与 AO 探针集不同——首跑教训：按坐标收集顶点色断言同值在树冠遮天天光梯度下不成立，且
  Vtx 布局 ny=float[4] 误取 float[5]=nz 会滤出侧面顶点全线假值）→ 改钉「4 连续顶点四边形」（culled
  每面 4 角点连续 append，单位方格四角仅中心面唯一覆盖）逐角直钉。
- **P-t1023-c 事实钉**（meshing 线程模式断言）：src/ 递归扫 7 种线程原语 = 0 且 F3 行
  `threads: 0/0 (sync meshing)` 在场——两事实互锁，线程化改造必须同时更新（防 F3 谎报）。
- offscreen 测不了的面 → 实机确认清单（§6）。

## 6. 实机确认清单（用户在新 exe 上）

1. **动画门控观感回归**：掉落物落地自转/浮动如常；拾取瞬间无残留转动；末影之眼/珍珠投掷自旋如常。
2. **AO 目视**：ESC 视频设置开「环境光遮蔽」→ 墙根/拐角/树干底部出现柔和暗角；关 → 与旧观感逐像素
   一致；确认后决定是否另单翻出厂默认。同场景 F3 帧成本对比（aoEnabled 开/关的重建耗时读数）。
3. **draw call 实测**（本批门控不影响 draw 结构，仅省 GUI 动画 tick）：F3 render-side 行 + drawCalls
   在 items 93/93 高水位场景的前后对比——instancing 的收益量化留待 §2.2 立项时采。
4. meshing `threads: 0/0` 行为与探针共存说明：线程化立项时须同步改 F3 行 + P-t1023-c。
