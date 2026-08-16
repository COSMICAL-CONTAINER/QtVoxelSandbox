# 经验库（voxelsandbox）

> 仅收录**本项目已验证**、可迁移的经验。未在本代码库验证的（如 QQuick3DInstancing）不写入，避免误导。每个 voxel-dev 子 Agent 开工前必读。

---

## 渲染 / QtQuick3D（当前渲染路径）

- **PerspectiveCamera 默认 clipNear=10**，MC 尺度（1 单位=1 方块）的小场景会被近裁面完全剪掉 → 必须设 `clipNear≈0.05`。
  - 证据：`Main.qml` `PerspectiveCamera { clipNear: 0.05; clipFar: 1000 }`。
- **QQuick3DGeometry 上传顺序**：`clear()` → `setVertexData()` →（**带索引几何时** `setIndexData()`）→ `setStride()` → `setBounds()` → `setPrimitiveType(Triangles)` → `addAttribute(...)` → **`update()`**。漏掉 `update()` 后端不会重新上传 GPU；带 `IndexSemantic` 却漏 `setIndexData()` 见下条。
  - 证据：`chunkgeometry.cpp` `buildMesh()` 末尾。
- **属性语义用新名**：`TexCoord0Semantic`（非旧 `TexCoordSemantic`）；索引用 `IndexSemantic` + `U32Type`；`setBounds()` 必填（影响视锥剔除）。
  - 证据：`chunkgeometry.cpp` addAttribute 段。
- **`addAttribute(IndexSemantic,…)` 只「声明」索引缓冲的布局，必须配 `setIndexData()` 才真正上传索引数据**：`QQuick3DGeometry` 里**顶点缓冲**与**索引缓冲是两块独立数据**——顶点走 `setVertexData()`+`setStride()`+`addAttribute(PositionSemantic,offset,…)`；索引走 `setIndexData(QByteArray)`+`addAttribute(IndexSemantic,0,U32Type)`。后者那行 `addAttribute` **只**声明「有索引缓冲、类型 U32」，**不**上传任何字节。漏掉 `setIndexData()` 时，你为索引手写的 `quint32 idx[N]` 数组声明后无任何引用 → `-Wunused-variable` 警告（PLAN §4 零警告门不过）**且**索引永不进 GPU —— 后端无索引缓冲，退化成按顶点序非索引绘制：24 顶点只凑出 8 个三角形（24÷3），本应 12 三角形的立方体几何畸形/缺面。**判别信号**：几何类里出现 `addAttribute(IndexSemantic,…)` 但全文搜不到 `setIndexData(` 调用 → 即此坑（典型成因是「复制了顶点上传模板、把索引当成又一个 vertex attribute、忘了索引是另一块缓冲」）。**通用形态**：凡是带 `IndexSemantic` 的 `QQuick3DGeometry`，`setIndexData()` 与 `addAttribute(IndexSemantic,…)` 是**成对契约**，缺一即 bug（警告 + 几何缺面）。既有已验证正确范本：`chunkgeometry.cpp`（`setIndexData(ib)` 紧跟 `addAttribute(IndexSemantic,0,U32Type)`）。静态/编译期三测全 PASS 但「带贴图的小方块/裂纹叠层几何肉眼缺面或畸形」类 bug 时常根因在此。
  - 证据：t35——`blockcube.cpp` / `crackbox.cpp` 各声明 `quint32 idx[36]` 但从未 `setIndexData()`，`-Wall -Wextra` 下 `-Wunused-variable`（blockcube.cpp:80）；补 `setIndexData(QByteArray(...,sizeof(idx)))` 后警告清零、索引真正进 GPU。
- **手写 per-face 顶点拼立方体，三轴必须同一原点约定、且与 Model 摆位约定成对**：列每面 4 角顶点拼 1×1×1 立方体时，三轴要么全用「角点原点 [0,1]³」（Model 摆 `blockMin`），要么全用「原点居中 ±0.5」（Model 摆 `blockCenter = block+0.5`）——**不能混**。典型坑：把常数轴（面法线轴）写成「+面=h(0.5)、−面=0」（疑似 [0,1]³），面内两轴却跨 `[0,1]`，于是对面间距仅 0.5 而非 1.0 → 6 张 1×1 大面片拼不出**闭合**立方体，是「缩水外壳 + 面片外溢」的畸形体。诡异处：**单看每面尺寸/UV/绕序都合理、culling 也对**（每面是合法 1×1 朝外四边形），编译/静态测试全过；只有「8 个唯一点是否恰为 {±0.5}³ 或 {0,1}³」的**闭合性检查**能抓到。**几何局部原点约定与 Model 摆位约定是成对契约**：错配则叠层整体偏移半格（几何居中却按 min 摆、或反之）。通用自检：从顶点表抽出所有不重复的 (x,y,z)，断言它们恰好构成闭合单位立方体（8 个角、对面间距=1）；并对照 `.h` 注释里声明的原点约定与 QML 里 `position` 是 center 还是 min。本工程已验证约定是 **±0.5 居中**（UnitCube/WireSquare/CrackBox 三者同基准），新写带贴图的盒体几何应对齐它。
  - 证据：t34——`CrackBox` 顶点常数轴 +面=0.5/−面=0、面内 [0,1]，未闭合；`.h` 自称「居中 ±0.5」但实现偏离，叠 `Main.qml` 的 `+0.5` center 摆位 → 裂纹向 +X/+Y/+Z 偏半格且畸形；改成严格 ±0.5 后闭合、与 center 摆位自洽。
- **「棱表 / 面表」携带隐式「角点序约定」，照搬棱表却用不同约定派生角坐标 → 两表错位 → 画对角线而非棱**：线框 / 边界盒几何常用「8 角顶点 + 12 棱端点对索引表」结构（如 `kEdges[12*2]`）。这张棱表**不是坐标无关**的 —— 它假定角点按某种序排，典型两种互不相容：(a) **周长序 / face-sequential**（z=min 面 0→1→2→3 绕一圈、z=max 同序）；(b) **位编码序**（bit0=+x、bit1=+y、bit2=+z）。两者在角 2/3（及 6/7）**相反**：周长序 2=(max,max,*)、3=(min,max,*)；位编码序 2=(min,max,*)、3=(max,max,*)。从兄弟文件**逐字照搬棱表**（继承其周长序前提）却在自身代码里**用位编码派生角坐标** → 棱 1-2 / 3-0 两端点 x、y 同时变化 = **面内对角线**，前后两面各一对对角线在面心交叉 = **每盒一个 X 叉叉**，而非干净 AABB 棱。整套代码「棱表自洽 + 角坐标自洽 + 注释自称一致」单读全合理，几何缺陷是纯数学可静态证伪却肉眼才暴露。**判别信号**：本该是轴对齐盒轮廓的线框、却在某面中央显 X / 某条「棱」两端点两轴同时变 → 即角序约定错配。**几何自检（静态可证，无需 run）**：对每条棱两端点断言**恰好一轴**坐标不同（AABB 棱的定义）；两轴同变即对角线。**通用修法**：照搬棱表就照搬其配套角表（显式 8 角坐标、同序），「棱表 + 角表」作为**成对契约**整体复用；若想自定角序，则**同时**重写棱表匹配新角序。绝不可「棱表来自 A 约定、角坐标来自 B 约定」混搭。
  - 证据：t216——`SelectionWireBoxes` 逐字照搬 `wirecube.cpp` 的 `kEdges`（周长序前提），自身却用位编码 `cornerCoord` 派生角坐标 → 角 2/3 错位 → z=min/z=max 面各画 2 条对角线 = 选中框带 X 叉（任务「去叉叉」核心验收 FAIL）。改显式 8 角表（周长序、与 wirecube.cpp:21-30 同序）后 12 棱全轴对齐、零对角线。
- **纹理图集防渗色**：per-face UV 加**半像素内缩**（`hx = 0.5/(图集宽)`、`hy = 0.5/(瓦片高)`），否则相邻瓦片线性采样会渗色。
  - 证据：`chunkgeometry.cpp` `constexpr float hx = 0.5f/(N*16), hy = 0.5f/16`。
- **culled meshing 是起点而非优化**：每实体方块只发"邻居是空气"的面，越界 `blockAt` 返回 0（空气）→ 边界面正确画出、单 draw call。本项目从第一版就用 culled，**不要回退到"全部块 6 面"**（已知 overdraw 致 10–20fps）。
  - 证据：`chunkgeometry.cpp` 6 面 × 邻居判定。
- **世界数据与网格/物理共享同一份栅格**（`World` 类）。网格(`ChunkGeometry`)与物理(`PlayerController`)都走 `World::blockAt/isSolid`，**绝不在视图层另持体素副本**——保证"看得见的方块 = 碰得到的方块"。
  - 证据：`world.h` 注释 + 两处消费者。

> ⚠️ 当前未用 `QQuick3DInstancing`（实例化）。"每实例 20 float / 80 字节"那条暂不适用，待真用上再补。

---

## 性能

- **数据源单一**：`World` 持栅格，mesher/physics 只读，避免多处副本导致"看见 ≠ 碰到"。
- **dt 钳制 + 子步防穿墙**：`tick()` 里 `dt = min(restart, 0.05)` 钳 50ms（卡顿后大步会穿墙）；行走模式按"任意轴单步 ≤0.4 格"分子步。
  - 证据：`playercontroller.cpp` `tick()` / `step()`。
- **日志噪音治理**：`QLoggingCategory::setFilterRules("qt.qpa.*=false\nqt.scenegraph.*=false")` 关掉最啰嗦 debug category，日志从 ~1.2MB 降到几 KB。
  - 证据：`main.cpp`。
- **贪婪网格化（greedy meshing）与纹理图集是天然冲突——图集路径下合并 quad 的贴图必然拉伸，逐格清晰平铺需纹理数组**：经典 0fps greedy 算法按面方向逐层建 2D mask、合并同纹理共面连续格为单个大矩形，顶点/三角数大幅下降（平坦地面 16×16=256 quad → 1）。但本项目走**纹理图集**（一张 atlas PNG，每瓦片子区 UV + 半纹素内缩）：图集采样是 CLAMP，UV 一旦越过 `[u0,u1]` 就采到**相邻瓦片**（不是同瓦片重复），所以无法用「UV 超 [0,1] + REPEAT」实现逐格平铺。结果：合并了 w×h 格的大 quad 只能把**一张瓦片拉伸**铺满整块（近看糊/块状），而非每格各显一张清晰瓦片。**判别信号**：开了 greedy 后平坦地面/大面墙体贴图变糊变大块 = 即此权衡（非 bug）。**通用形态**：凡体素引擎在「图集 + 固定功能材质」路径上做 greedy meshing，必须二选一——(a) 接受拉伸（本工程 t178 取此：默认 greedy 开、`greedyMeshing` 属性可关回逐格 culled 保清晰）；(b) 迁移到**纹理数组**（per-face `tileIndex` 顶点属性 + `sampler2DArray` 采样、UV 在 [0,1] 内 per-layer REPEAT）= PLAN §2-I 顶点格式 / 自研 RHI 路径，届时 greedy 可兼得「合并 + 逐格清晰」。在迁移前，**把 greedy 做成可关开关**而非硬替换 culled，让贴图清晰度与顶点预算可按场景权衡。
  - **合并键要含光照（tile + 邻格天光 + 邻格方光），不能只含 tile**：若只按 tile 合并，一块跨「火把照亮区」的大 quad 只在四角采样光、内部按四角插值 → 内部格本该亮却变暗（greedy + 平滑光的经典暗斑）。把邻格光值纳入合并键 → 仅**均匀照明**区合并（合并 quad 内部光值一致、无暗斑），火把/墙边光变化处自动不合并（贴图也保持逐格清晰，一举两得）。PCF 软影仍 per-vertex（四角各自采样→影边光栅化平滑），不进合并键。
  - **per-face-direction greedy（6 面各扫一次）比经典 3 轴双向 mask 更易对齐既有 culled 的 UV/绕序/法线**：复用既有 `kFaces[f]` 的角点偏移 + per-face UV 轴规则（±X cu=Z,cv=Y；±Y cu=X,cv=Z；±Z cu=X,cv=Y），合并 quad 的四角 = 法线轴定值 + 两 in-plane 轴按合并宽/高缩放；UV 角分量取 0/1（拉伸铺满）。这样 w=h=1 时**逐顶点退化为原 culled 输出**（UV/绕序/法线完全一致），可静态核对 greedy 实现未回退 culled 几何。
  - 证据：t178——`chunkgeometry.cpp` greedy（默认）vs culled：chunk(0,0) 9156→3240 顶点（~2.8×，约 −65%）；`window.greedyMeshing` 开关绑全 50 个 ChunkGeometry；F3 叠层显 mesh 模式 + 顶点/三角。
- **F3 帧时间切分在「无 GPU 计时」路径上要诚实标注估算值，不得伪造**：PLAN §4 验收要「CPU/GPU ms + draw-call 预算」可 pass/fail。但 QtQuick3D 路径**不公开**逐帧 GPU 计时（无 QRhiGpuTimer）也不暴露真 draw-call 计数（QSGRendererInterface 查询有限）。诚实做法：(a) CPU ms **实测**（`QElapsedTimer` 包 `tick()`，~1s 滚动平均暴露 Q_PROPERTY）；(b) frameMs = 1000/fps（总预算）；(c) draw-call **估算**并明确标 `≈`（chunk×2 段 + 实体 + 火把 + 固定场景 Model 数），spec「不得伪造数字」= 估算值必须可视地标注非真值。GPU 真计时 / 真 draw-call 留待自研 RHI 迁移（`QRhiGpuTimer` / RHI stats）。**通用形态**：性能验收叠层在缺真值的维度上，永远「实测 + 标注估算」优于「伪造精确值」——前者可诊断、后者误导。
  - 证据：t178——`playercontroller.cpp` tick() 包计时 → `m_simMs` Q_PROPERTY（NOTIFY `perfChanged` 每 ~60 tick 发）；`Main.qml` F3 加 `frame / cpu sim / draw-calls ≈`。
- **「批量收口」必须覆盖一次事件触发的**所有**跨层 NOTIFY emit，只批一层、漏批另一层 → 风暴原样留存（性能 bug 复发的典型根因）**：一个高扇出事件（如爆炸：球内 N 块破坏）会沿**多条独立信号链**各自扇出 N 次 notify——World 层 `worldChanged`（驱动 chunk 重建）、Item 层 `entitiesChanged`（驱动 QML Repeater 追加 delegate + 全体 delegate 触碰 revision 重算绑定）。若只把**一条**链改成「N 写 1 emit」（如 `destroySphereSilent` 收口 worldChanged），另一条链仍逐项 `spawnItem` 各发一次 `entitiesChanged`，则：N 次单独 emit ×（Repeater model 变更 + 全体已有 delegate 4 绑定重算）= **O(N²) 绑定重算** + N 次重 3D delegate 即时实例化 → 一帧数十 ms（FPS 崩 + 落地前每帧续卡）。**判别信号**：修了「批量」仍卡 / 复发 → 几乎肯定是漏批了**并行的另一条** notify 链（不是数值没调对、不是算法没换）。**通用形态**：任何「一次事件 → 多个 ViewModel 各自累积 N 次变更」的场景（爆炸掉落、连锁破块、群体生成），每个 ViewModel 都要能**抑制内部 emit 直到事件结束才 1 次收口**——用 `beginBatch/endBatch`（深度计数 + dirty 标志，notifyChanged 在批内只标 dirty）把「N 次 emit」折叠成「1 次」，把 O(N²) 绑定风暴降到 O(N)。批态集中在**数据所有者** C++（单一事实源），呈现层用 `batchActive()` 守卫仅 `begin` 一次、用「事件总结信号」（如爆炸末尾恒发的 `explosion`）`end` 收口；非批的常规单次 spawn（depth=0）立即 emit、行为不变。
  - 证据：t320 把 World 层 `worldChanged` 收口为「1 次/爆炸」（`destroySphereSilent`），但 Item 层 `entitiesChanged` 仍由 `spawnItem` 逐块 emit → 爆炸仍卡（t354 复发）。t354 修：`ItemEntityManager` 加 `beginBatch/endBatch/notifyChanged/batchActive`，所有 `++revision; emit entitiesChanged()` 改走 `notifyChanged()`（批内只标 dirty）；`Main.qml` `onExplosionDroppedItem` 首发 `beginBatch`、`onExplosion`（detonateStalker 末尾恒发、与掉落同栈同步）`endBatch` 收口 → N 个掉落 1 次 emit 补齐。
- **多实例 per-frame 重活的「错峰节流」：每实例每 N 帧才跑一次重活，按 idx 错峰把单帧负载均摊到 1/N**：当一类对象（mob / 物品 / chunk）数量 ≥ 几十、每个每帧都跑「决策 + 多次 blockAt / 邻接扫 / raycast」时，单帧 CPU 工作量 ∝ 实例数。盲目按帧节流（全员同帧跑 / 同帧不跑）会生成节拍 spike（一帧全员跑、下三帧全员闲 → 仍卡）。正确做法是**按实例 idx 错峰**：`bool tick = ((globalTickCounter + idx) % N) == 0` → 每帧恰好 1/N 的实例跑重活、其余实例闲帧，单帧负载均摊（无 spike）。**关键约束**：(a) **状态累积器（计时器 / 速度 / 燃烧 dmg / 窒息 dmg）必须用「累积 dt」而非每帧 dt** —— 节流帧传 `accumulatedDt = ∑(N 帧 dt)` 给节流重活，使「每秒平均速率」（移动速度、扣血速率、计时衰减）与原每帧路径一致；写 `timer -= dt`（per-frame dt）会把节流当作实时减速 → 燃烧 / 窒息扣血慢 N 倍 / mob 移动慢 N 倍。(b) **连续体感项（受击红闪 / 走路声 / 击退位移 / 重力 / 水流推动）保留每帧跑**，仅节流「决策 + 扫描 + 移动应用」（移动应用每 N 帧一次但 aiDt × N 抵消 → 平均位移速度不变；最大单步位移须 « 1 block 防 AABB 全格扫穿墙，e.g. aiDt=4·50ms=200ms × chase 2.8 = 0.56 block < 1 block 安全）。(c) **同帧两个独立循环读同一组实例时（如 `tick()` + `tickHostileLife()`）共用同一 `globalTickCounter` + 同一 idx 错峰式** → 一致地把「mob X 的重活」集中到同一帧（两循环都跑或都不跑），避免 mob X 在 A 循环节流到 frame 0、在 B 循环节流到 frame 2 → 节流不一致使某帧多 1× 实例。(d) **首次 aiTick 前累积值 = 0**：新 spawn 的实例 `aiAccum` 初值 0 → 第一帧（若非节流帧）不动不扫，下次节流帧 aiDt 取小值（1-3 帧 dt）跑一次迷你 AI → 自然 ramp，无需特判。**机制对齐 MC**：MC 1.0 mob AI 本就「think 每 4-5 tick」（非每 tick 全员跑），本节流是其同族设计；mob 移动会有肉眼可见的「小幅步进」（15Hz 位置更新 vs 60Hz 渲染），是节流的固有取舍（PLAN §4「机制对标」非数值 1:1）。
  - 证据：t500——mob 桶用户实测 24.99ms/f（60FPS 预算 16.6ms/f → 单桶超 → 10 FPS）。根因：60 槽 mob × 每 mob 每帧 ~50 blockAt（mobAabbHitsSolid × 2 全格扫 + 仙人掌 10 邻接 + 视线 raycast 32 步进 + 窒息 / 火烧扫描）= 数千 blockAt / 帧。修：`EntityManager` 加 `m_tickPhase` 单调计数 + 每 mob `aiAccum` 累积器；`tick()` Mob 分支 + `tickHostileLife()` per-hostile 循环同用 `((m_tickPhase + idx) % kAiTickInterval=4) == 0` 错峰；火烧 / 仙人掌 / 窒息 / AI 移动 / 决策 / 吃草扫描全部走 aiTick + aiDt；hurtFlash / ambientTimer / walkPhase / 击退 / 水流推动保留每帧 dt。
- **「FrameProfiler 桶」是「kFramePhases 表 + flush 报告格式」的成对契约；新增桶漏一边 → 桶恒 0 / 漏报数**：`FrameProfiler` 把逐帧桶名硬编码在 `kFramePhases[9]` 静态表 + `flush()` 用下标 `fpMs[i]` 拼报告字符串。新增 / 改逐帧桶必须**两边都改**：表加新名 + flush 字符串加新 `"name " + ...`；漏一边 → 桶累加正常但报告不显示（误以为没插桩）/ 报告显示一个恒 0 桶（误以为该阶段零开销）。**窗口桶（非逐帧）** 走另一路：直接 `m_ns["name"]` 累加、`flush()` 内 `bucket("name")` 读 + 拼 → 加桶无需改 kFramePhases 表（仅 flush 字符串加新段）。两类桶的区别：逐帧桶 report 时 ÷ `m_frameCount` 得 ms/frame（与帧率挂钩，如 mob/ms-per-frame）；窗口桶 report 时直接总 ms（与帧率无关，如 mesh 总重建耗时、`bp` 粒子 Timer 总耗时）。**QML 侧样本入口**：QML Timer（如 `BlockParticles.qml` 50Hz 弹道 Timer）与 C++ 60Hz tick 异步节拍，但其 onTriggered JS 仍占 GUI 线程帧预算 → 需独立测。给 `FrameProfiler` 加 `Q_INVOKABLE addSampleMs(name, ms)`，QML 在 onTriggered 用 `Date.now()/performance.now()` 包测后推到桶，flush 报告读为窗口总 ms。**判别信号**：F3 / 日志的 prof 行少了某阶段 / 某阶段恒 0，但代码里 Scope("name") 确有插桩 → 检查 kFramePhases 表 + flush 字符串是否两边同步。
  - 证据：t500——加 `bp`（BlockParticles）窗口桶：仅改 `flush()` 拼 `"bp " + bucket("bp")/1e6`（窗口桶无需改 kFramePhases）；同时加 `addSampleMs` Q_INVOKABLE 让 QML 推样本（QML 单例 `FrameProfiler.addSampleMs("bp", dt)` 在 BlockParticles.qml onTriggered 内调）。
- **FrameProfiler 跨线程样本（render_cpu / main_total）必须 QMutex 保护 m_ns；不加锁会 hash race 崩 / 数字乱跳**：扩展 FrameProfiler 加「帧时间切分桶」区分主线程 vs 渲染线程瓶颈时，`main_total` 由 GUI 线程发射（`QQuickWindow::frameSwapped`），`render_cpu` 由**渲染线程**发射（`QQuickWindow::beforeRendering` / `afterRendering`，用 `Qt::DirectConnection` 在发射线程上跑 lambda）。两个线程并行写同一个 `m_ns` `unordered_map` → 不加锁会内部 hash 表 race，崩溃 / 数字乱跳 / 偶发正确（最阴险——多数帧对、少数帧错）。**通用形态**：凡「同一进程内多个线程发射 Qt 信号 → lambda 推样本到共享 map」的诊断探针，**所有写路径**（包括原本只 GUI 线程用的 `add` / `count`）都必须加同一 `QMutex`；只给「新跨线程路径」加锁而保留旧路径裸写 → 仍 race（旧路径与新路径并发写即破）。`flush()` 也在持锁内读全部桶（否则并发写时报告数字部分来自旧 / 部分来自新）。emit + qInfo 在**释锁后**调（避免锁内回调入函数再持锁 / qInfo 内部加锁交叉）。**时间戳读取**本身跨线程安全：`QElapsedTimer::nsecsElapsed` 只读静态计时器，无需加锁；只有「读 → 算差 → 写 map」的「写」这一步要锁。
  - 证据：perf-t520——`main_total`（frameSwapped → frameSwapped 间隔）+ `render_cpu`（beforeRendering → afterRendering）两个新桶推入 `FrameProfiler::addSampleMs`；未加锁构建期不报错，但运行期若两个 Qt 信号并发发射即 hash race。改 `m_ns` / `m_counts` / `m_frameCount` 全部 `QMutexLocker` 保护、`bucket` 改名 `bucketLocked`（标锁内调用契约）、`flush()` 持锁读全部桶 + 释锁后 emit/qInfo 后稳定。
- **「QtQuick3D 路径区分主线程 vs 渲染线程瓶颈」的诚实标注：frameSwapped 间隔 = main_total、beforeRendering→afterRendering = render_cpu（**非**真 GPU 时间）**：用户报 `frame 125ms - sim 25ms = 100ms 在 render/main`，需先区分瓶颈侧再改。QtQuick3D（QQuick3DGeometry 路径）**不公开逐帧 GPU 计时**（无 `QRhiGpuTimer` 暴露），但可用 `QQuickWindow` 的进程级信号近似切分：(a) `frameSwapped`（GUI 线程）的帧间间隔 = 「GUI 线程帧周期」（含 sim tick + QML binding / scenegraph update + 同步等待渲染线程），(b) `beforeRendering` + `afterRendering`（DirectConnection 在渲染线程上跑 lambda）= 「渲染线程 CPU 侧编码 + GPU 提交阻塞」。**threaded render loop 下** `frame ≈ max(main_total, render_cpu)` 近似（两线程并行，慢者卡帧）；`main_total >> render_cpu` = 主线程 bound（QML binding / 物理 tick / scene-graph update），反之 = 渲染线程 bound。**诚实标注**：`render_cpu` 含 GPU stall（驱动阻塞等待 GPU）但**不等同**纯 GPU 时间——`render_cpu` 80ms 可能是 GPU 真慢、也可能是 CPU 驱动路径低效，无法在 QtQuick3D 层区分，报告字串须明确写 `render_cpu=CPU+GPU stall, not pure GPU time`（PLAN §4「不得伪造数字」）。真 GPU 时间 / 真 draw-call 待自研 RHI 迁移（`QRhiGpuTimer` / RHI stats）。**判别信号**：进游戏跑几秒读 prof 报告的 `frame ms/f: main X render Y` 行即可定位瓶颈侧——`main*` 标星 = 主线程瓶颈，`render*` 标星 = 渲染线程瓶颈。
  - 证据：perf-t520——加 `main_total`（frameSwapped）+ `render_cpu`（before/afterRendering）两桶，flush 报告加 `frame ms/f: main X render Y (frame ≈ max; render_cpu=...)` 行，max 一侧标 `*`。menu 态实测 `main*16.6 render 1.2`（vsync 60FPS cap，主线程瓶颈 = 60FPS 极限），与 sim≈0 一致（menu 无实体 tick）。
- **多行调试叠层（F3）的整块 text 绑定是「每帧固定开销」杀手 —— 把 body 抽成普通函数 + 单 string 属性 + 低频 Timer 刷新**：F3 文本绑定的 body 内若读**任一**高频 NOTIFY 属性（如 `player.position`，60Hz `positionChanged` 每 tick 发），整块多行字符串（~30 行 concat + 多个 Q_INVOKABLE 调用如 `biomeIdAt` / `liveCount`）每帧重算，每秒 ~60 次。即便该叠层只是「调试」、不参与玩法，它仍占用主线程帧预算——在 <10FPS 场景下，F3 text 重算本身可能是 5-15% 的 main thread 开销。**通用修法**：把 text 绑定的 JS body 抽成 `function buildF3Text() { ...same JS... return "..." }`，加 `property string f3Text: ""` 单一 string 缓存，加 `Timer { interval: 100; onTriggered: window.f3Text = buildF3Text() }`（10Hz 刷新），text 元素改 `text: window.f3Text`。原理：JS 函数内的属性读取（`player.position`）**不建立** QML binding 依赖（依赖只对绑定 / 显式属性赋值生效）→ 函数内的读取是「快照当前值」，无 NOTIFY 触发链。Timer 10Hz 触发 → 函数被调 → 读最新值 → 写单一 `f3Text` 属性 → 仅 1 个 NOTIFY（f3TextChanged）触发 text 元素重算。重算频率从 60Hz 降到 10Hz（6× 降）。**切换可见性首帧延迟**：进 playing / F3 toggle on 时立即手动调一次 buildF3Text（Connections 监听 appStateChanged / f3VisibleChanged），避免 100ms 内空白。**适用范围**：任何「body 依赖高频 NOTIFY 但人眼可接受 100ms+ 延迟」的叠层（F3 / HUD 提示 / 状态面板），都可套此模式。机制核心玩法（hotbar / 背包 / 准星）仍需即时响应，不可节流。
  - 证据：perf-t520——Main.qml 原 F3 text 绑定读 `player.position/feetPosition/yaw/pitch/speed/onGround/hasHit/hitBlock`（60Hz）+ `theWorld.biomeIdAt(...)` Q_INVOKABLE + `liveCount()` ×3 + `worldClock.dayPhase/skyLight`（10Hz）→ 整块字符串每帧重算。抽 `buildF3Text()` + `window.f3Text` + 10Hz `f3RefreshTimer` + Connections 首帧立即刷；HUD pos 小条同套 `buildHudPosText` + `hudPosText`。F3 是调试叠层 100ms 延迟零影响；HUD pos 100ms 延迟肉眼读字 < 200ms 起跳阈值仍近实时。
- **「N 个 inline Material 实例各自绑定同一派生值」是隐式 N× 重算：抽成单一 window 属性共享**：QML `Component` 里 inline 的 `PrincipledMaterial { baseColor: f(someProp) }`，每次该 Component 被 `createObject` 实例化都会**复制一份 Material + 复制其 baseColor 绑定**。N 个实例 → N 个独立 Material + N 个独立绑定，当 `someProp` NOTIFY 时 N 个绑定**各自**重算（N 次 JS 调用 + N 次 QColor 分配 + N 次 scenegraph 标脏）。即便 `f()` 是纯函数、输入相同、输出相同，仍 N× 重算（QML 绑定不 CSE 共享）。**修法**：把 `f(someProp)` 的结果提到 window 级 `property color skyBaseColor: f(someProp)`（**单一**绑定），各 Material 改 `baseColor: window.skyBaseColor`（**读取**已计算的共享值，非各算各的）。NOTIFY 触发时仅 1 次重算（window 层）+ N 次属性读取赋值（常数开销，无 JS / 无 QColor 分配）。**判别信号**：grep 代码库 `terrainLight(` / `tintBySkyLight(` / 任何 `f(prop)` 出现在 N 个 Component 内的 Material / 属性绑定时 → 即此坑（N× 重算）。改单一 window 属性共享即降 1×。**适用范围**：所有「N 个实例共享同派生值」场景（chunk Model material 共享昼夜色、mob delegate 共享 tier 色调制、UI 槽框共享选中高亮色……）。
  - 证据：perf-t520——10×10 chunk × 3 段（terrain/water/cross）= 300 个 inline `PrincipledMaterial { baseColor: terrainLight(worldClock.skyLight) }`；`skyLight` 每 100ms（dayPhaseChanged）触发 300 绑定重算（3000 次/秒 JS + QColor 分配）。加 `window.skyBaseColor: terrainLight(worldClock.skyLight)` + 各 chunk material 改 `baseColor: window.skyBaseColor` → 10Hz 仅 1 次重算，材料绑定从「计算 + QColor 分配」降为「常数属性读取」。lava/glass/ice 段用固定色不动；mob tier 色（tintBySkyLight）走另一路径（mob delegate 数 << 300，结构不同）保留独立。
- **每 tick 全图扫描（O(W×D×H)）找某类方块是体素引擎的头号稳态杀手；任何「按方块类型逐 tick 处理」的系统都该用「增量位置索引集」替代全图扫描**（t425 生长方格 / 本任务流体方格同族）：一个 tick 函数若开头是 `for x: for z: for y: if blockAt(x,y,z)==X ...` 来找它要处理的格子，则**每次调用都付 O(世界体积)**。80×80×128=3.28M 格 × chunk 路由除法（blockAt）≈ **19ms/扫**；节流到每 0.3s 一扫 = 57ms/s——这就是用户实测「wat 57ms/s + 554 reb/s 不 settle」的真因（稳态早退 `if (!dirty) return` 本想救，但只要流场还在动 dirty 就反复真 → 每 0.3s 烧 19ms 扫 + 触发 mesh 重建）。**修法（t425 模式）**：维护 `std::unordered_set<quint64> m_XCells`（坐标打包键），写入路径（setBlock / setWaterSilent / setVoxelIfAir / setBlockFromEntity，**全部**含 5 参数变体）在 `m_chunks.setBlock` 后调 `noteXWrite(x,y,z,oldId,newId)` 按 oldId/newId 是否目标方块增删集合项（id 不变如水流 level 变 → no-op）；generate/beginLoad 清空、finishLoad + generate 末全图重建一次（worldgen / 存档 blob 直写 chunk 不经写入路径 → 索引不捕获，必须一次性 rebuild）。tick 改遍历集合（O(目标格数)）+ `blockAt` 复核跳过过期项（防御某条直写路径漏通知）。**实测**：水扫描 19ms → 0-42us（~500×），稳态 wat 0.0 / mesh 0 reb（水 settle 后 dirty 早退 → 零扫描零重建）。
  - **判别信号**：FrameProfiler 某世界 tick 桶（wat/lav/ice/crop/...）稳态非零且随世界体积线性涨，但该系统「逻辑上应已稳态」（海洋全源 / 无作物 / 全成熟）→ 高度怀疑 tick 在全图扫描找候选而非用位置索引。grep 该 tick 函数见三层 `for x/z/y` + `blockAt` → 即此坑。
  - **`unordered_set` 边遍历边删会迭代器失效**：tick 内若遍历 m_XCells 的同时调写入路径（其 noteXWrite 会 erase 当前格，如水→冰、水→蒸发），必须**先收集目标到 vector 再统一 apply**（同 tick 内「快照 → 计算 → apply」纪律的 set 变体）；snapshot 阶段只读集合不写 → 无失效。
  - **通用形态 / 自检门槛**：凡体素引擎有「逐 tick 按方块类型处理」的系统（流体蔓延 / 作物生长 / 叶衰减 / 结冰 / 火传播 / 藤蔓蔓延……），审其「如何定位待处理格」——三层 for 全图扫 → 必改成位置索引集。改后稳态零扫描（配合 dirty 早退）、活跃期 O(目标格数) 扫描。这是「全图扫描 → 增量索引」的结构性优化，与「dirty 早退」正交（dirty 管「扫不扫」、索引集管「扫多少」——dirty=true 仍要扫时，索引集把 O(3.28M) 降到 O(目标格数)）。
- **resting 态的「支撑格复探」坐标必须 FP-robust：restY = mobSolidY+1+halfH（落地设），复探 floor(pos.y−halfH)−1 算支撑 cellY 时，halfH 非 2 的幂（0.45/0.9/0.3）致 pos.y−halfH 有 ~1 ULP 残差落整数之下 → floor 取低一格 → 复探查到支撑格**下方**（薄地板下是空气）→ 误判失支撑 → resting 翻 false → 重力下落 1 帧 → 落回原位 resting=true → 下个复探又翻 false = 周期振荡（每复探一次重力 + dirty bump + emit = 持续卡顿源）**。**修法**：复探坐标加 0.01f（» 1 ULP ~1e-5、« 1.0 格）把任何向下残差推回整数之上 → 支撑 cellY 稳定。仅在 resting 复探生效（pos.y 已 snap 到 restY，feet 恒 ≈ 整数）；下落中 mob 不走此分支故不受影响。厚地面下邻格也是实体 → 误判不暴露；**薄地板**（1 格厚天花板 / 生成结构顶）下邻格是空气 → 暴露。判别信号：mob 在薄地板上「原地微抖 / F3 显示反复 resting↔falling / 无位移却 dirty 每 tick」→ 锁定复探坐标的 FP 边界。通用：凡「浮点 pos 经运算定整数格 + 该格做关键判定（支撑 / 嵌入 / 碰撞）」的组合，都要审 floor(浮点) 在整数边界 ±1 ULP 内的取整方向，必要时加容差。



---

## 物理 / 碰撞（玩家）

- **玩家 AABB**：宽 0.6（半宽 0.3）× 高 1.8；`pos` 存**脚底中心**；对外 `position` = 眼睛 = 脚底 + (0, 1.62, 0)。
- **逐轴 move-and-resolve**：按 Y/X/Z 顺序，每轴移动后检测重叠 → 贴面 + `eps=1e-4` 防卡缝 + 清该轴速度。
- **严格重叠采样**：`x1 = ceil(max) - 1` 排除"仅贴面"的方块（否则贴着墙会被判碰撞而抖动）。
- **着地判定**：下落被挡（`dy<0 && vel.y==0`）= 着地；另做"脚底下方 0.05 有实体"的稳健复探。
- **重力/跳跃常量**（已实测手感可玩）：`gravity=28`、`jump=8.4`（顶点约 1.25 格）、`maxFall=78.4`、走速 `4.3`、飞/观察速 `8`、鼠标灵敏度 `0.25 度/像素`。
  - 证据：`playercontroller.h` constexpr 常量 + `playercontroller.cpp`。
- **垂直碰撞的「支撑面」必须按「可着陆性」过滤后再聚合，不能取所有重叠块顶的 MAX**：身体被「在身上 materialize 的块」（下落沙凝固 / 侧面塞入）部分嵌入时，把所有重叠 sub-AABB 的顶面取 MAX 当着地面，会把「可着陆块」（身体移动前接触点原本在其顶之上 = 地面）与「intrusion 块」（块顶在接触点之上 = 沙）混进同一个值。更糟的是 snap 目标（贴到 maxSurf）与 snap 许可（`pyBefore >= maxSurf`）**共用这一个值 → 耦合**：intrusion 块抬高 maxSurf 后，snap 许可随目标一起失效，**连原本有效的地面托举也一并丢失**。tick 内重力每帧重施、substep 循环无 Y 复位、地面复探只置 onGround 不改 Y → 身体每 tick 净下沉、无恢复力 → 逐格穿地坠虚空。**通用形态**：凡「身体可被部分嵌入」的碰撞，聚合支撑面时**先用「接触点移动前位置」做阈值过滤**（只取块顶 ≤ 移动前接触点的面），把 support 与 intrusion 在 snap 决策**之前**分离；intrusion 块不动该轴，交该轴之外的方向挤出。
  - 证据：t161——`overlapSubAABBs` 取所有重叠块 bmax 的 MAX；`moveAxis(1)` 下行用 `pyBefore>=maxSurf-1e-3` 门控 snap，沙覆盖时 maxSurf=沙顶 → 门假 → 不 snap → 地面托举随之失效 → 玩家逐 tick 下沉穿地坠虚空。修：`overlapSubAABBs` 加 `maxSurfCap=pyBefore` 只累计 bmax≤cap 的块顶 + `outHasMax` 区分「有可着陆面 / 纯 bury」；`moveAxis(1)` 下行 hasMax 则 snap 到可着陆最高面（地面顶，非沙顶），纯 bury 才留格内待横向挤出。
- **删 snap 不是免费的——防「瞬移上爬」不能靠「全不 snap」**：「嵌入不上抬」直觉对（防逐层下落沙把玩家逐格顶到柱顶），但删掉 snap 等于同时删掉了合法地面的向上支撑。**判别信号**：身体在重力下**单调下沉**（每 tick 净降、无任何 tick 把 Y 推回支撑面）= 支撑丢失，而非「嵌入挤出待生效」。修法永远是「找到合法支撑面并 snap 到它」+「intrusion 块交水平挤出」二分，而非「全不 snap」一刀切——后者是用一个新 bug（穿地）换掉旧 bug（上爬）。
- **「维持正确支撑 Y」是所有 Y-相对下游逻辑的承重不变量；Y 错则全错**：水平挤出（columnClear 从 `floor(Y)` 起扫邻列）、窒息（眼位 `floor(Y+eye)`）、掉落伤害峰基准……都从身体当前 Y 推导采样格。若垂直解算把 Y 留在「下沉后的错误值」（本应贴顶=N 却停在 N−ε → floor=N−1 = 地面所在格），columnClear 扫到的是脚下方块而非脚位空气 → 全堵 → 挤出永不触发 → 穿地无任何横向救场。诊断「挤出/窒息/伤害行为异常」时**先查身体 Y 是否被正确 snap 到支撑面**，再查各自逻辑——多数情形下游 defect 只是 Y 错的连锁症状，修 Y 即连带解除。
- **同 tick 内实体物理先于玩家物理 → 世界可在身体脚下 mid-tick 凭空多出一块，碰撞器须把 overlap 视作「可能是 intrusion」**：`EntityManager::tick`（下落沙凝固 `setBlockFromEntity(cx, solidCellY+1, cz)`）在 `PlayerController::step` 之前跑；身体非体素不挡沙 → 沙越过身体落到其下支撑块、凝固在支撑块+1 = 身体脚位格。即身体本 tick 解算重力时，世界已在其脚列多出一块。碰撞 resolver 必须鲁棒于「上一 tick 到本 tick 之间世界在我身上变了」——**overlap 不恒为墙/地，可能是 intrusion**，故上条的支撑面过滤是必须的而非可选优化。
- **外力冲量（击退 / 爆炸 / 弹跳板 / dash）若同时存进「独立累加器」+「主速度」两个都被重力积分的 store，垂直方向会双重力；且「着地清零」粘不住 —— 残余水平冲量让积分块继续跑、把已清零的垂直分量反复拉负，吃掉其后的跳跃 / 上浮 = 用户体感「被打后就跳不起来、过一会儿恢复」**：玩家受击击退旧设计把**水平**冲量存进独立累加器 `m_knockback.x/z`（合理：玩家 `m_vel.x/z` 每 tick 被 wish 输入覆盖，独立累加 + 指数衰减才存得住），但**垂直小跳**也对称地存进 `m_knockback.y` 并在 step 里每 tick `m_knockback.y -= kGravity*dt`——而 `m_vel.y` 自身重力分支（同一 step）**也**施 `-kGravity*dt`。两者叠入 `delta.y = (m_vel.y + m_knockback.y)*dt` → 有效垂直减速度 = 2g（小跳峰值腰斩）；更阴险的是：小跳着地后 `m_knockback.y` 被清 0，但**水平**击退（~1s 衰减）让积分块仍每 tick 跑 → 再把已清 0 的 `m_knockback.y` 按 `-kGravity*dt` 拉成负值 → 持续把 `delta.y` 向下拽 → 其后玩家跳跃（`m_vel.y=kJump`）/ 水中上浮（`m_vel.y=kSwimUp`）的有效向上速度被这股**陈旧负冲量 + 双重力**吃掉，跳跃 1.25→~0.5 格、水里顶不住一格 = 用户实测「被怪打后跳不起来、水里更糟、过一会儿恢复」（水平衰减完才恢复）。**判别信号**：某「外力给玩家一个瞬时垂直冲量」的机制，玩家**被作用后一段时间内**跳跃 / 上浮变弱、之后自恢复，且代码里该冲量与主垂直速度**分居两个都被重力 tick 的变量** → 即双重力 + 陈旧负值。**根治**：垂直冲量直接进**主垂直速度** `m_vel.y`（`m_vel.y` 不被 wish 覆盖、已由重力 / 着地分支统一管，单一重力），水平冲量才需要独立累加器（`m_vel.x/z` 被 wish 覆盖）。**通用形态**：凡「外部瞬时冲量」要叠到已有重力 / 阻尼积分系统的实体上，**只把会被输入覆盖的那几轴**进独立累加器；**不被覆盖的轴**（典型如垂直速度、或本就不被 wish 重写的轴）直接进主速度，由主积分器统一一个重力 —— 否则独立累加器的重力 + 主速度的重力 = 双重力，且着地 / 清零窗口与残余冲量的衰减窗口不一致时，清掉的垂直分量会被「为兄弟轴衰减而仍每 tick 跑的积分块」反复拉回负值。一句话：**独立冲量累加器只该承担「主速度每 tick 被覆盖、存不住」的轴；不该承担「主速度本就能自然积分」的轴**，否则就是把一个积分器能干的事拆成两个、且两个都施重力。

---

## 输入 / 鼠标指针锁定

- **指针锁定实现**：`QGuiApplication::setOverrideCursor(BlankCursor)`（全局隐藏，最可靠）+ 每帧 `QCursor::setPos(windowCenterGlobal())` 居中并算 delta + 回中（永不撞屏边 = 无限旋转）。比 `QWindow::setCursorGrabbed` 跨平台更稳。
  - 证据：`playercontroller.cpp` `grab/pollMouse`。
- **release 必须成对**：`restoreOverrideCursor()` 恢复光标 + `m_keys.clear()` 丢弃按住的 WASD（否则恢复时玩家会前冲）。
  - 证据：`playercontroller.cpp` `release()`。
- **失焦/切走绝不留"锁住的光标"**：事件过滤器拦 `WindowDeactivate` / `FocusOut` / `Esc` → 自动 release。
  - 证据：`playercontroller.cpp` `eventFilter()`。
- **QML Keys 必须过滤 `isAutoRepeat`**，否则长按键反复触发边沿/双击逻辑（长按空格会反复触发飞行闪烁）。
  - 证据：`Main.qml` `Keys.onPressed/onReleased` 首行 `if (e.isAutoRepeat) return`。
- **创造飞行手势**：进创造模式默认走（`m_flying=false`）；**真实按下**空格（非 autorepeat、非已按住）→ 300ms 内第二次按下 = 切换飞行；长按空格用边沿触发（`spaceEdge = space && !m_spacePrev`）只跳一次。
  - 证据：`playercontroller.cpp` `setKey()` / `step()`。

---

## 选体射线（raycast 阻挡谓词 + 交互目标）

- **选体射线的「阻挡谓词排除清单」会造出一批「主射线永不命中」的方块类型；任何依赖主射线命中格来交互这些类型的逻辑都是死代码**：主选体射线（`raycastVoxel` 的 `blocksRay`）为游戏手感排除某些非实体方块是正确设计——Water 不挡选体（t165：否则眼位入水即命中水面 → 水下挖不了方块，机制等价 MC 水不挡选体）、Torch 穿透（t157：准星瞄火把选中其背后实体）。但这意味着主射线命中格 `m_hitBx/y/z` **永不可能**是被排除的类型。于是「命中格 == 被排除类型」的判定（如 `if (blockAt(hit) == Water)` 舀水）**恒 false → 死代码，功能完全不可用**，且静态读「if 写得对、调的 API 也对」极难发现——根因在**另一处**（射线的排除清单），不在 if 本身。
  - **判别信号**：某交互（舀水 / 收水流 / 取被排除方块）写「查主射线命中格是否为 X」，而 X 出现在 `blocksRay` 排除清单里 → 必死代码。**修复不是改 if**，是给该交互配一条**独立的、把 X 重新纳入阻挡**的射线（或查命中面「玩家侧相邻格」= 射线穿入命中实体前的最后一格），主选体 / 相机射线仍排除 X 保原手感。
  - **射线模式改变阻挡清单时，「起点嵌格退化」判定要同步放宽**：`raycastVoxel` 起点若已在「挡射线」的格内会判退化（`valid=false`，防相机穿模进实体方块出无意义高亮）。但当新模式把某非实体方块（水）纳入阻挡后，起点在该格属**玩家正常所在态**（水中游泳）而非相机穿模——沿用旧退化会把「眼位即水」的交互（水下舀水）漏掉。通用：新增射线模式时审一遍起点退化分支，「起点在被该模式视为阻挡、但属玩家可正常占据的格」应判为**命中起点格**（dist=0、无法线），而非退化。
  - **系统模拟 / 工具操作改非玩家语义方块应走「静默写入」而非通用 setBlock**：水流系统的增删（蔓延、蒸发、被桶舀走）是**系统模拟 / 工具操作**，非玩家破块；走通用 5 参数 `setBlock(Air)` 会发 `blockBroken(Water)` → 触发破块粒子 / 音（视觉像「破水」）。MC 铁桶舀水无此反馈。应走 `setWaterSilent`（同 `setBlockFromEntity` 模式：直写 + `worldChanged` 重建 mesh，**不**发 broken/placed）。判别：改的是「水」这类非玩家语义方块、且动作不是玩家敲破 → 用静默写入入口；玩家放置水源（倒水）才是玩家动作 → 走 `setBlock` 发 `blockPlaced`。
  - 证据：t174——空桶舀水查 `blockAt(m_hitBx,...) == Water`，但 t165 为修「水下挖掘不了」已把 Water 加进 `blocksRay` 排除清单 → 命中格恒非水 → 舀水 if 死代码（功能完全不可用，三测却判「代码合理」PASS）。修：`raycastVoxel` 加 `waterBlocks` 开关（默认 false 保 t165），桶舀水跑 `waterBlocks=true` 独立射线命中首个水格；放宽起点退化（waterBlocks 模式起点在水 → 命中起点格）覆盖水下舀水；舀水改 `setWaterSilent` 免破块噪音。**placeBlock 入口的 `!m_hasHit` 一刀切门也要为「不依赖主命中的交互」局部放开**（桶舀水在深水 / 水下时主射线无实体命中 m_hasHit=false），否则交互分支在门后不可达——把该门改成「仅工作台/熔炉/门/放块需命中」的局部门控。

---

## 工具链 / 项目纪律

- **构建测试维度的警告边界 = 整个项目，非本任务 diff**：build 角色的「项目自有代码零警告 → 否则 FAIL」规则扫的是**全量构建**，不是你这次改动的文件清单。一个既有警告（哪怕是你没碰的文件、上一任 commit 留下的）照样让本轮 build FAIL。修正时**先看全量构建输出里有没有任何警告**，再聚焦自己 diff；别只盯着自己的文件。
  - 证据：t01 修复时，build FAIL 的唯一项在 `main.cpp`（非 t01 改动文件），属既有 `QFile::open` nodiscard 警告。
- **`[[nodiscard]]` 的 fallible 调用不许丢返回值**：Qt 里 `QFile::open` / `QIODevice::open` / `QBuffer::open` 等标了 `[[nodiscard]]`，忽略返回值即 `-Wunused-result` 警告 → 零警告门不过。正确模式是**检查 + 优雅降级 + 可见诊断**（失败时走兜底路径并 `qWarning` 告知，而非 `(void)`-discarded 或静默吞掉），契合 PLAN §2-E「保持运行而非崩溃」。绝不要用 `(void)expr;` 糊弄——那是把错误藏起来。
  - 证据：`main.cpp` 日志文件打开失败 → `qWarning` + 让 `logHandler` 在 `!isOpen()` 时静默丢弃消息，应用照常运行。
- **GateGuard hook 硬拦截** `rm`、破坏性 bash、`cat > 已存在文件`。删文件用 `mv` 到 `build/`（已 gitignore）；改文件用 Write/Edit。
- **改 CMake 源文件清单**：新增 `.cpp/.h` 必须同步进 `qt_add_executable(...)` 列表（当前扁平结构，无自动 glob）。
- **RHI 后端日志**：`qputenv("QSG_INFO","1")` 启动时打印所选 RHI 后端（走 scenegraph 日志 → 文件）；`main.cpp` 已确认 Vulkan 生效。
- **FPS 计数**：`QQuickWindow::frameSwapped`（GUI 线程，每帧 +1）→ 1s 定时器回填 QML `fps` 属性。
  - 证据：`main.cpp`。
- **三目（条件表达式）两支必须同型；标量与枚举混用（即便枚举带 `enum Id : quint8` 固定底层类型）仍触发 `-Wextra`「enumerated and non-enumerated type in conditional expression」**：本工程方块 id 一物两态——`World::blockAt()` 返回 `quint8`（标量），而 `BlockRegistry::Air` 等是 `enum Id : quint8`（枚举）。写成 `cond ? blockAt(...) : BlockRegistry::Air` 时，**两支类型不一致**（一支标量 quint8、一支枚举 `Id`），即便枚举已用 `: quint8` 钉死底层类型，编译器仍视两者为不同类型 → `-Wextra` 告警（PLAN §4 零警告门不过）。**判别信号**：build-test 报「enumerated and non-enumerated type in conditional expression」→ 即某三目把枚举常量与标量放了两支。**通用修法**：把枚举那一支显式强转为标量（`quint8(BlockRegistry::Air)`，左值本就是 quint8 故无需改声明），或两支统一到同一类型（都 `quint8(...)` / 都 `BlockRegistry::Id(...)`）。**自检**：凡三目/条件赋值里出现「枚举常量（`BlockRegistry::Air/Water` 等）与 `blockAt()/stateAt()` 返回的标量」并存的，必有一支强转对齐类型。**通用形态**：任何「枚举 + 标量」进同一条件表达式的写法（不限方块 id，状态 / 模式 / 类型码同族），都要显式统一类型——`enum : <标量>` 只保证**底层存储**相同，**不**让枚举在类型系统里隐式等同于该标量。增量构建（项目 CMake 未开 `-Wall -Wextra`）不会暴露此告警，须用 `-Wall -Wextra -fsyntax-only` 单文件复核（与 build-test 角色等价口径）才能抓到。
  - 证据：t271——`itementitymanager.cpp:184` `const quint8 sb = (supportY >= 0) ? world->blockAt(...) : BlockRegistry::Air;` 触发 `-Wextra`；改 `: quint8(BlockRegistry::Air)` 后告警清零（`-Wall -Wextra -fsyntax-only` exit 0）。

---

## QML 模块 / 运行期部署

- **顶层 QML `import` 是整份文档的硬加载期依赖**：被 import 的模块运行期未部署 → 该 QML 文件整体编译失败 → `objectCreationFailed` → app `exit(-1)`。对「可选 / 增强型」QML 特性（粒子、特效、多媒体、Extended…）**绝不在主文档顶层 import**；把用到该模块的节点抽成独立文件，经 `Loader` 动态加载——Loader 失败只置 `item=null` + 打 warn，父级（与整个 app）继续运行（`if (loader.item) loader.item.xxx()`）。这是 QML 侧兑现 PLAN §2-E「保持运行而非崩溃」的标准手法，也是「增强特性绝不能拖垮核心加载」的通用原则。
  - 证据：`Main.qml` 不再顶层 `import QtQuick3D.Particles3D`；粒子节点 `BlockParticles.qml` 经 `Loader { source:"BlockParticles.qml" }` 加载；`Connections` 守 `if (particleLoader.item)`。模块缺失时 Main 照常 `root objects after load: 1`。
- **windeployqt 的 QML 插件部署是 import 驱动、非二进制驱动**：只 link 一个 Qt C++ 库**不会**让 windeployqt 部署对应的 QML 模块插件。必须给 windeployqt 传 `--qmldir <qml 源目录>`，让它扫 `import` 语句、拷匹配的 QML 插件目录（如 `qml/QtQuick3D/Particles3D/`，含 qmldir + plugin dll）及其 C++ 依赖（`Qt6Quick3DParticles.dll`）。漏 `--qmldir` → 新引入的「纯 QML」依赖静默不部署 → 运行期"模块没有安装"→ 崩。**每引入一个新 QML 模块，就重跑 windeployqt，或把它做成 CMake POST_BUILD 步骤**（每次构建自动刷新，保证 fresh build 自洽）。
  - 证据：`CMakeLists.txt` 的 windeployqt `POST_BUILD` 用 `--qmldir ${CMAKE_CURRENT_SOURCE_DIR}`；部署日志见 `'QtQuick3D.Particles3D'` 被解析、`Qt6Quick3DParticles` 进入 "To be deployed"。windeployqt.exe 路径靠 `${Qt6_DIR}/../../../bin` 推导。
- **"模块没有安装"型启动崩的修复双解（防御纵深）**：代码正确但启动崩在缺模块时，修法分两层——(1) 部署层：windeployqt `--qmldir` 把缺失模块拷进 build（治标，让它能跑）；(2) 鲁棒层：把该 import 改成 `Loader` 可选加载（治本，即使将来部署缺口也永不崩，只静默降级）。两层都做 = 任何部署缺口都不会复现崩溃。**诊断这类崩溃时先分清"代码错"还是"部署缺"**——若手动补部署后能跑，则代码正确、blocker 纯属部署，开发侧重跑 windeployqt 即可，但同步做 Loader 化才真正达标「不支持则静默降级」的验收。
  - 证据：t14 复测——手动补 Particles3D 部署后可跑，证明 QML 代码正确；开发侧改成 windeployqt POST_BUILD + Loader 化双管齐下。
- **windeployqt 的 dxcompiler.dll/dxil.dll "Warning" 是工具噪音、非项目代码警告**：只在用 Direct3D 12 时才有意义；本项目走 D3D11，此行可忽略，不算入 PLAN §4「零警告」门槛（该门槛只扫项目自有代码）。
- **Loader 加载出的 3D Node 必须显式领养进场景 Node，否则 parent=null 永不渲染**：`Loader` 本身是 2D `QQuickItem`；当它加载一个以 `Node`/`Model`/`ParticleSystem3D`（任何 `QQuick3DObject`）为**根**的 QML 文件时，加载出的对象**不会**自动并入 `View3D` 的 3D 场景图——其 `parent` 为 `null`（孤儿），整棵子树永远不渲染；即便 `Loader.status===Ready`、`item` 非 null、`ParticleSystem3D.running===true`、`burst()` 正常调用也白搭。这是「编译通过 + 自动化三测全 PASS、但肉眼啥也看不见」类 bug 的典型根因——**静态/编译期测试无法发现「3D 对象未进场景图」**。修法：在 `View3D` 内放一个锚点 `Node { id: anchor }`，`Loader.onLoaded` 里 `item.parent = anchor` 显式领养进场景图（领养后 `item.parent` 从 `null` 变成 `QQuick3DNode*`，子树才开始渲染）。诊断时打印 `item.parent`：`null` = 孤儿未渲染，`QQuick3DNode*` = 已进场景。**Loader 隔离可选 QML 模块（如 Particles3D）的「运行期缺失则降级不崩」价值仍在**，但降级不得静默：`onStatusChanged === Loader.Error` 必须 `console.warn` 显式告警（§2-E）。一句话：**Loader 适合隔离 2D/可选依赖，但加载出的 3D 内容必须 reparent 进场景；「Loader Ready」绝不等于「3D 内容可见」，凡 Loader 装载 3D 的呈现层都要补运行期「肉眼可见」验证项**。
  - 证据：t16——破/放粒子骨架（t14）三测 PASS 但肉眼不可见；实测 log `parent = null`，加 `item.parent = anchor` 后变 `QQuick3DNode`、连续发射的粒子与测试立方体随即在场景中出现。
- **动态实例化的 3D 对象（Repeater/Instantiator/Loader-of-Node）默认不进 3D 场景图**：`Repeater` 是 `QQuickItem`，会把 delegate 领养到**自己的 parent**（一个 QQuickItem / 2D 上下文），而非 3D 场景节点；当 delegate 是 3D 对象（`Model`/`Node`/`ParticleSystem3D`）时，它们要么成孤儿（`parent=null` → 不渲染，同 t16 Loader 坑同族），要么触发 `Delegate must not be of Item type` 告警——这是「静态/编译期测试全 PASS、运行期肉眼不可见」类 bug 的同一族根因。**通用原则**：凡在 QML 里**动态**（Repeater/Loader/Instantiator）创建 3D 场景对象，必须显式把它们领养进一个**已在 3D 场景里**的 `Node`（如 t16 的 `item.parent = anchor`）；或对**固定小网格**直接写成 3D 场景节点的静态直接子节点。**诊断信号**：「`Delegate must not be of Item type`」告警 / 动态创建的 3D 内容肉眼缺失 = 该机制没把 3D 对象放进场景图。**大网格（如 16×16=256 chunk）不要用 QML Repeater 重复声明**——改走 C++ 侧的 mesh/节点管理（按需 `new QQuick3DGeometry` 挂到场景 `Node`），既避开领养坑也省 QML 解析开销。
  - 证据：t03——per-chunk mesher 初版 `View3D { Repeater { delegate: Model{...} } }`，运行期 log 出 `Delegate must not be of Item type`（Model delegate 有成孤儿不渲染之虞）；改成「9 个显式 `Model` 直接作 View3D 3D 子节点」（与原单 Model 同已验证路径）后告警消失、9 chunk 各自重建（`vo.render: chunk(x,z) rebuilt` 日志可见）。
- **Repeater + 3D delegate 的「创建能渲染、销毁不回收」坑：移除条目后 delegate 永久残留**：上一条解决了「3D delegate 进场景图」（靠 `Component.onCompleted: parent = anchor` reparent），但 reparent 同时埋了**销毁**坑——`QQuickRepeater` 的 delegate 跟踪表是 `QQuickItem*` 类型，3D delegate（`QQuick3DNode`，非 `QQuickItem`）**不进该表**；且 reparent 把 QObject 所有权转给了 anchor Node。结果：无论 `model` 是 `ListModel`（走 `QQmlDelegateModel` 路径）还是 int-count（走 Repeater 直接路径），**减小 count 时 Repeater 都找不到 delegate 来销毁** → delegate 的 `Component.onDestruction` **永不触发** → delegate（含子 Model + 动画）永久挂在 anchor 下不回收。判别信号：从 model 删了条目（`count` 已减）但「删不掉的视觉残留」类 bug（挖掉方块后贴图/光晕/小模型还在），且 `console.log` 在 `Component.onDestruction` 里收不到 = 即此坑（Repeater 根本没销毁 delegate）。**通用修法**：对生命周期需「按条目增删」的 3D delegate，**不要用 Repeater**——改 `Component.createObject(parent, {initialProps})` 显式创建 + 自维护 `({key: obj})` 实例表 + `.destroy()` 显式销毁（QML 动态对象标准生命周期，createObject 对象由 JS 表持有引用、destroy 可靠回收，无 Repeater 中间层）。Repeater 仅留作「int-count + 永不删除（或删除可接受残留）」场景（如 itemHost/mobHost 用 int-count Repeater：拾取/移除时 count 减小，最后一个 delegate 即便不被销毁，其数据绑定读到越界 index 也常 render 到无效位 → 用户不易察觉残留；而火把等「固定可见位置」的残留极刺眼）。**自测门槛**：凡 Repeater/Loader 装 3D delegate 且验收含「移除后应消失」的，必须运行期验证 delegate 销毁（`onDestruction` log 或 肉眼），编译通过不算 PASS。
  - 证据：t170——火把 `torchHost` 用 `Repeater { model: torchPositions(ListModel); delegate: Node{木柄+火焰} }` + `onCompleted: parent=torchHost`，挖掉火把后木柄+火焰 Model 永久残留（t131 的 `removeTorchAt` 从 ListModel 删条目逻辑正确，但 Repeater 不销毁 3D delegate = 治标失效）；改 ListModel→int-count 仍不销毁（onDestruction 不触发）；最终改成 `Component.createObject(torchHost,...)` + `torchObjs` 表 + `.destroy()`，`blockBroken(Torch)→removeTorchVis→destroy()` 后 onDestruction 触发、delegate 即消失。`itemEntities.count`/`entityManager.count`（itemHost/mobHost）是 int-count Repeater，其 delegate 同样不被销毁，但因 delegate 数据按 index 读、count 减小后越界 index 渲染到无效位而未被察觉。
- **「count 减小不销毁」的泄漏量 ∝ count 抖动频率；高频抖动源（掉落沙 / 拾取）喂出 GB 级泄漏，且 C++ 内存审计查不出来**：上一条 t170 的「reparent 后 3D delegate count 减小不销毁」在**低频**场景（火把挖掉 / mob 偶发死亡）只是「视觉残留」（几个 delegate，用户难察觉）；但当某个实体类型**高频 spawn + 高频移除**时，每次「count 升」新建的 delegate（含 `QQuick3DGeometry` + `PrincipledMaterial` + 子 `Model` 树）在「count 降」时全部不回收 → 累积。典型：掉落沙——沙柱塌落每帧 spawn 多个 FallingBlock、着地即 erase 移除，count 上下剧烈抖动 → 10min 累积数千孤儿 delegate → ~2GB / 卡顿；重启进程清零（孤儿 delegate 随进程死）。**致命的查不出**：C++ 侧 `EntityManager`/`ItemEntityManager` 持 `std::vector<Entity>` 且有 `kCap` 上限、无裸 `new`、light recompute 用栈上 `std::queue`/`std::vector` 函数返回即释放——**所有 C++ 内存审计（leak sanitizer / 容器 size 检查）全 CLEAN**。泄漏在 QML 场景图侧（delegate 对象归 QtQuick3D 场景图管，C++ 容器看不见），C++ 工具盲区。**判别信号**：(1) 玩某**特定玩法**（掉落沙 / 大量挖掘产出掉落物）时间越长越卡、内存单调涨、重启恢复；(2) C++ 审计干净（容器有界、无裸 new）；(3) 该玩法对应「Repeater int-count model 频繁增减」的实体类型 → 即此坑（QML delegate 累积，非 C++ 泄漏）。**通用修法（二选一，按改动面）**：(a) **slot-reuse / tombstone**——C++ 容器移除时**不 erase-shift**，改标槽位空（`alive=false`）+ 入 free list，下次 spawn 复用空槽；于是 `count`（= `vector::size()` = QML Repeater model）在游玩期**单调不降** → Repeater 永不需要销毁 delegate → 无累积（空槽 delegate 绑 `visible:aliveAt(index)` 隐藏，复用时 revision bump 重显重绑；索引稳定不 shift）。改动小（容器内部存储模型 + 各遍历跳空槽 + delegate 加 visible 绑定），不动 QML delegate 结构。**注意 `alive` 标志位放 struct 末尾**——若该 struct 有聚合初始化 `{field1,field2,...}`，新加首字段会错位（`cannot convert X to bool`）；放末尾则聚合初始化尾字段缺省取 default member init。(b) **createObject/destroy + 稳定 id**（t170 推荐）——C++ 暴露单调 id + spawn/remove 信号，QML 维护 `{id:delegate}` 表 + `.destroy()`。改动大（QML delegate 全部重写为 id-based），适合 delegate 结构简单、id-based 数据访问易迁移的场景。两者都根治；slot-reuse 对「QML delegate 已复杂、index-based 绑定多」（如 mobHost 含 MobModel + F3+B + 攻击框）更省改动。**自测门槛**：修完须 run 该高频玩法一段时间（掉落沙 / 连续挖掘）+ 观察内存**不单调涨**（F3 / 任务管理器），仅 build 绿不算 PASS——泄漏是运行期累积，静态/编译期测试全盲。**元教训**：「C++ 审计干净」不等于「无内存泄漏」——凡呈现层（QML 场景图）按 C++ 容器 count 动态创建对象、而 C++ 容器又做 erase-shift 的，泄漏面在 C++ 视野外；排查「玩法相关、时间累积、重启恢复」型内存涨时，优先怀疑「Repeater/Loader 装 3D delegate + 容器 erase-shift」组合，而非 C++ 容器本身。
  - 证据：t256——掉落沙玩 ~10min → 2GB / 卡顿、重启恢复。C++ 审计干净（EntityManager `std::vector<Entity>` + kCap=64；ItemEntityManager kCap=200；light recompute 栈上 queue）。根因：`mobHost`（mob + FallingBlock 共用）/`itemHost`（掉落物）Repeater 的 reparent 3D delegate 在 count 减小（沙着地 erase / 掉落物拾取 erase）时不销毁（t170 族）；掉落沙高频 spawn/land 使 count 剧烈抖动 → 每次升新建的 BlockCube + MobModel delegate 在降时全泄漏。修法 (a) slot-reuse：两 manager 加 `alive` 标志 + `m_freeSlots` + `m_liveCount` + `acquireSlot/releaseSlot`（移除改 release 不 erase → count 单调不降）；遍历（tick/findMobHit/resolvePlayerPush/pickupScan）跳空槽；QML 两 delegate 加 `visible:{revision;aliveAt(index)}`；F3 draw 估算改用 `liveCount()`（空槽 delegate 隐藏不绘制，count 会高估）。
- **slot-reuse 的「count 单调不变量」必须覆盖所有 count-reducer 路径，包括「语义上理应全清」的 reset/clearAll——漏掉任何一条，跨世界（或跨会话）的退场即把全部 reparent 3D delegate 一次性孤儿化，比游玩期的高频抖动泄漏更隐蔽、更致命**：上一条 t256 把「游玩期」移除（拾取 / 死亡 / 落沙着地）改成 `releaseSlot`（不 erase → count 不降 → Repeater 不销毁 delegate），根除了「玩法相关、高频抖动」型泄漏。但**「切世界 / 退存档」走的 `clearAll()` 仍写 `m_entities.clear()`**（即 `vector::clear()` 把 `size()→0`，count 属性随之→0）。这条路径**语义上**是「全部实体作废、进新世界从空起」，作者直觉「clearAll 理应清空 vector」看似无可挑剔；但它把 count 砸到 0，触发的正是 t170 族「Repeater count 减小 → reparent 进 host Node 的 3D delegate 销毁不到 → 孤儿」——而且**一次清掉全部**（不是掉落沙那种逐个抖动）。于是：退存档 → clearAll 把上一世界全部 mob/item/xp delegate 一次性孤儿化（挂在 mobHost/itemHost/xpOrbHost 下永不回收）；再进新世界 → spawn 从 0 起重建 → **孤儿堆之上再加新一层** → 跨世界单调累积。每个 mob delegate 含 MobModel（多 mob-type Model）+ 眼 Model + 火舌 Repeater（7×3 Model + 动画）= 数十 3D 对象；item/xp 各自的 BlockCube/billboard/球 delegate 子树同理。用户症状「**退存档再进仍然卡**」正是此机制的指纹——卡顿跨世界保留（孤儿在 QML 场景图、重启进程才清），而 C++ 内存审计（容器有界、无裸 new、size 检查）**全 CLEAN**（泄漏面在 QML 场景图侧，C++ 视野外，同 t256 元教训）。**判别信号**：(1) 性能问题「**跨世界保留**」（退存档再进不缓解、甚至更糟），区别于「纯 per-tick 扫描残留」（仅当 tick 跑才卡、换世界即清）；(2) C++ 审计干净；(3) 该实体 / 对象有「切世界 / 退会话时走 `clearAll`-style 全量 `vector::clear()` reset」的入口 → 即此坑（reset 断点漏盖 slot-reuse 不变量）。**通用修法**：凡已用 slot-reuse（releaseSlot 不 erase）保 count 单调的实体 / 资源容器，其 `clearAll()` / `reset()` / `unloadWorld()` **也必须走「释放全部活体槽（标 alive=false + 入 free list + liveCount=0）但保留 vector」**，**绝不** `vector::clear()`。这样 count 在切世界时也单调不降 → Repeater delegate 既不销毁（无孤儿）也不重建 → 下次进世界直接复用既有 delegate 子树（aliveAt 翻回 true + revision bump 重绑新世界数据），稳态常驻受 cap 钳制（有界），远优于跨世界无界泄漏。幂等要点：clearAll 仅释放「当前 alive」的槽（已释放的跳过），否则 free list 重复入栈 + liveCount 下溢。**自测门槛**：修「跨世界保留型」卡顿，必须 run「进世界 A→退→进世界 B（甚至同一世界重复进出 N 次）」+ 观察内存 / FPS **不随进出次数单调恶化**（仅 build 绿、仅单世界内玩不算 PASS——孤儿是跨世界累积的，单世界内 slot-reuse 本就正常）。**元教训**：不变量（如「count 单调」）是**全局**契约，不能只在「热路径」守、「冷路径 / 重置路径」破——「重置语义上想清空」与「不变量要求不降」冲突时，**不变量优先**，重置改走「释放但不销毁容器」语义（把「作废」从「销毁对象」降级为「标记空槽」，复用时重生）。任何「先全局不变量、后局部实现」的设计，审计时要把**每一条**会减 count 的路径（含 reset/clear/unload/quit）逐一对照不变量，而非只盯热路径。
- **「用隐藏替代销毁」修 Repeater-3D-delegate 不销毁坑时，delegate 的**稳态常驻数 = 历史最高并发峰值**——任何一次峰值（爆炸 / 大规模刷怪 / 玩法峰值）会把 count 推到高水位，之后即使全部 slot 释放（delegate visible=false 隐藏），那些 delegate（每个含完整子树：BlockCube + Material + 多 Model + 动画）**永久驻留 host 场景图**，重进世界它们仍在（仅隐藏），持续吃场景图遍历 / 绘制 / 绑定重算开销 → 用户报「峰值玩法后退存档再进仍卡、只有重启 exe 才恢复」**：上一条把 clearAll 改成 slot-reuse（释放槽保 count 不降）解决了「跨世界孤儿化」，但留下了它的对偶问题——既然 delegate「永不被销毁」，那么**历史峰值期间创建的所有 delegate 都永久驻留**。cap（kCap=200/64）只钳「最大并发活体数」，不钳「曾经达到过的 delegate 总数」（二者在 slot-reuse 下相等，因为 count 单调不降 = delegate 永不回收 = count 恒等于历史峰值）。判别信号：(1) 用户报某种**峰值玩法**（TNT 连锁爆炸一次产数百掉落物、大规模刷怪、群攻）之后**整个进程**变卡；(2) 退存档再进**同一世界**（甚至新世界）仍卡；(3) 完全关闭 exe 重开才恢复（delegate 随进程死）；(4) C++ 审计干净（slot-reuse 容器有界）；(5) 该实体用了「Repeater int-count + reparent 3D delegate + slot-reuse 保 count 单调」组合 → 即此坑（隐藏≠销毁，高水位 delegate 永驻）。**通用修法（双层）**：(a) **真正重置入口**（world-exit / enterWorld 清旧 / unloadWorld）须提供一个「真清 vector（count→0）」的 `hardReset()`（区别于只标 alive=false 的 `clearAll()`），让 count 真正回落到 0——hardReset 让 Repeater model 变 0，但 reparent 的 3D delegate **仍不被 Repeater 销毁**（t170：跟踪表是 QQuickItem*，3D delegate 不进表），所以 (b) **caller 须配套手动 destroy** reparent 的 delegate：给每个 delegate 加一个**布尔标记 property**（如 `isEntDelegate: true`），world-exit 时扫 `host.children`、对带该标记的子节点调 `.destroy()`（绕过 Repeater 直接销毁 QML 动态对象）。遍历 `host.children` 时**先 `.slice()` 复制**再遍历（destroy 异步但会改 children 列表，原表迭代失效）；Repeater 自身也是 host 的 child 但无该标记（undefined falsy）→ 跳过不误毁。**仅重置路径调 hardReset + destroy**，游玩期的拾取 / 死亡 / 移除仍走 releaseSlot（保 slot-reuse 不变量、防高频抖动重建 delegate）。**关键区分**：`clearAll`（标空保 count 单调）vs `hardReset`（真清 count→0）是**两种语义**——前者用于「游玩期 / 同世界内」的移除（保 delegate 复用、避免抖动重建），后者用于「跨世界 / 跨会话」的真清场（连 delegate 一起销毁、下一世界从 0 起重建）。把二者混用（用 clearAll 做跨世界清场 → delegate 永驻；或用 hardReset 做游玩期移除 → 高频抖动重建 delegate 致卡顿）都是错的。**自测门槛**：必须 run「峰值玩法（让 count 冲到 cap）→ 退存档 → 再进 → 验证不再卡、F3 / 任务管理器内存回落到基线」，仅 build 绿 / 单世界内测不算 PASS——高水位 delegate 是峰值后累积的，静态测试盲。**元教训**：「用隐藏替代销毁」是性能权衡（避免高频销毁/重建抖动），但**任何「永不被销毁」的对象池都有「稳态 = 历史峰值」的代价**——峰值越高、池越大、常驻开销越大；当存在「理应彻底清场」的语义入口（换世界 / 换关卡 / 换会话）时，必须配套「真正销毁」的路径，不能让对象池只增不减。对象池（slot-reuse / tombstone / 隐藏池）设计三问：① 峰值有多大？② 谁负责在「真清场」时排空池？③ 排空路径是否覆盖了 Repeater-3D-delegate 的手动 destroy？（漏任一 → 峰值后永久变卡，重启才恢复）。
  - 证据：t492——TNT 连锁爆炸一次产大量掉落物，itemEntities.count 冲到 kCap(200) → 200 个 item delegate（每个 BlockCube/BillboardQuad + Material + 动画）驻留 itemHost 子树。t437 的 clearAll 只标 alive=false（delegate visible=false 隐藏），保了 count 单调无孤儿，但 200 个 delegate **永驻不销毁**。退存档→再进同一世界：clearAll 释放所有槽（items 0/0、F3 显示归零），但 200 个隐藏 delegate 仍在 itemHost.children → 场景图每帧遍历 / 绘制排序仍扫这 200 棵子树 → 卡顿跨世界保留；完全关 exe 重开 → delegate 随进程死 → 恢复。修法：ItemEntityManager/EntityManager/XpOrbManager 加 `hardReset()`（真 `m_entities.clear()` → count→0）；QML 三个 delegate Node 加 `property bool isEntDelegate: true`；window 级 `clearEntDelegates(host)` 扫 `host.children.slice()` destroy 带 isEntDelegate 的子节点（Repeater 自身无此标记 → 跳过）；三处 world-exit/enterWorld-清旧 段把 `xxx.clearAll()` 改 `xxx.hardReset(); clearEntDelegates(host)`；`/kill @e` 游玩期路径保 clearAll（同世界内 slot-reuse 复用 delegate，不重建）。

- **Repeater `model` 绑定到「返回数组的 Q_INVOKABLE 函数调用」不会自动跟踪该类型的 NOTIFY——纯函数调用不建 QML 依赖，模型数据陈旧且肉眼难察（编译/AOT/启动三测全 PASS）**：当 C++ ViewModel 用 `Q_INVOKABLE QVariantList achievements() const` / `Q_INVOKABLE QVariantList statsList() const` 这类「返数组的方法」（非 Q_PROPERTY）暴露列表数据，并配套一个 `revision` Q_PROPERTY + NOTIFY 驱动刷新（同 ChestStore / Hotbar 的 moc 安全契约）时，QML 侧若写 `Repeater { model: progress.achievements() }`，**该绑定永远不会因 `progressChanged` 而重算**——QML binding 依赖只对「读 Q_PROPERTY」生效，函数调用 `achievements()` 是「一次快照」、不订阅任何 NOTIFY。结果：列表首次渲染正确，但底层 C++ 数据变更（解锁新成就 / 统计累加）后 **delegate 不刷新**（仍显旧数据），而 `revision` 已 bump、`achievements()` 也确实返了新数据——纯 QML 绑定语义盲区。**整套代码读起来全合理**（C++ 有 revision + NOTIFY、QML 调了 achievements()、delegate 也触碰了 progress.revision），qmllint 不报、AOT 编译过、启动 `root objects = 1`、甚至「首次打开面板」肉眼正确——只有「触发数据变更后重看面板」才暴露模型陈旧。**判别信号**：QML Repeater 的 `model` 是 `<vm>.<method>()` 形式（非 ListModel / 非 Q_PROPERTY），且数据源 C++ 类有独立 `revision` Q_PROPERTY + NOTIFY → 几乎必中此坑（model 绑定不会自动随 NOTIFY 重算）。**通用修法**：把 model 绑定写成「**先读 revision 建依赖、再返函数结果**」的表达式——`model: { const _r = progress.revision; return progress.achievements() }`。`_r = progress.revision` 这一步是对 Q_PROPERTY 的读（建 NOTIFY 依赖），progressChanged → revision 变 → 整个绑定表达式失效重算 → 再次调 `achievements()` 取最新。`_r` 本身不被使用（仅触发依赖建立），但**不可省略**（省了就退回纯函数调用的盲区）。**delegate 内勿再重复触碰 revision**（早期修法在 delegate 加 `property int _rev: progress.revision`）——那只让 delegate 属性重算，**不**让 Repeater model 重算（model 长度不变时 delegate 复用旧数据绑定，刷新不可靠）；正确做法是把触碰收敛到 model 表达式一处。**适用范围**：所有「C++ ViewModel 用 Q_INVOKABLE 返列表 + revision NOTIFY」的 QML 列表（成就 / 统计 / 排行榜 / 任何 method-returns-array 模型），都要在 model 绑定显式读 revision 建依赖。同族： delegate 内直接绑 `modelData.xxx` 静态类型推不出（qmllint 报 unqualified / missing-type）也是同根——QML 对动态数组元素的属性访问无静态类型，属正常噪音非 bug。
  - 证据：pause-menu 进度 / 统计面板——`Repeater { model: progress.achievements() }` 初次打开显当前成就态正确，但游戏中解锁新成就后重开面板 delegate 不刷新（revision 已 bump、achievements() 返新数据，但 model 绑定没建 NOTIFY 依赖没重算）。改 `model: { const _r = progress.revision; return progress.achievements() }` 后 progressChanged → model 重算 → delegate 刷新正确。
  - **同族扩展（t561 火焰不显）**：Repeater `model` 是「**返回 bool 的 Q_INVOKABLE 方法调用 ? 数组字面量 : []**」三元门控数组时，**同样不建 NOTIFY 依赖** → model 只在 delegate 创建瞬间求值一次、之后恒为初值（`[]`）——即便配套的 `visible` 绑定已正确读 revision 翻 true（子树可见），model 仍是空数组 → 该条件模型**永不更新**（用户观感「某效果消失了 / 被删了」，实际是条件模型陈旧）。**判别信号**：同一组件里 `visible` 用 `{ _r; ... }` 触碰 revision 正常翻转、但同 scope 的 Repeater `model` 是裸 `<vm>.<method>() ? [...] : []` → 即此坑（visible 依赖建立了、model 没建立，两处各自独立）。**修法同 t498**：model 表达式改成 `{ const _r = <vm>.revision; return _r >= 0 ? (<vm>.<method>() ? [...] : []) : [] }` —— 触碰收敛到 model 表达式一处。**通用形态**：凡「条件显隐 + 条件模型」双闸门里，`visible` 与 `model` 是两条**独立绑定**，一个建了依赖不代表另一个建了；方法调用（返 bool 或返数组）作模型两侧都不建依赖，必须各自显式触碰 revision。

- **qmlcachegen AOT 编译通过 ≠ QML 正确**：`qt_add_qml_module` 的 qmlcachegen 把 `.qml` 预编译进二进制，但它**只做词法/语法层**校验，**不**校验「信号处理器名是否存在」「属性能否被赋值」这类语义绑定。一个写错的信号处理器（如 `onHoverChanged`，应为 `onHoveredChanged`——源自 `HoverHandler.hovered` 属性的 `hoveredChanged` 信号）能通过 AOT 编译 + 链接成功 + ninja 报「no work to do」，却在**运行期组件加载**时报「类型 X 不可用 / 无法分配到不存在的属性」→ `objectCreationFailed` / `root objects after load: 0`。**判别信号**：build 绿 但启动日志 `QQmlApplicationEngine failed to load component` + 「不可用」/「不存在」 + `root objects after load: 0` → 即此坑。**通用原则**：改 QML 后的自测门槛**必须**包含「启动 app + 日志 `root objects after load: 1`」，仅「编译通过 / build 绿」**不算** PASS（与 t16「编译通过但粒子不可见」同族：静态/编译期测试漏判运行期问题）。**Handler 信号命名规则**：Pointer Handler（`HoverHandler`/`TapHandler`/`DragHandler`/`DropArea`）的 change 信号严格由属性名推导：`hovered`→`onHoveredChanged`、`active`→`onActiveChanged`、`pressed`→`onPressedChanged`、`containsDrag`→`onContainsDragChanged`，没有简写；DropArea 的事件处理用 `onDropped`/`onEntered`/`onPositionChanged`（是其声明的方法，非属性 change 信号）。
  - 证据：t23——`Inventory.qml` 的 `HoverHandler { onHoverChanged: ... }` 编译通过、build 绿，运行期 `qrc:/VoxelSandbox/Inventory.qml: 类型 Inventory 不可用 ... 无法分配到不存在的属性"onHoverChanged"`，`root objects = 0`；改 `onHoveredChanged` 后 `root objects = 1`。

---

## 音频 / miniaudio（t177 验证）

- **解码器里为「主流资产类」设计的「安全上限」会静默截断「合法属于另一长度类」的资产，且静态/编译期测试全 PASS**：音频解码器（`ma_decoder_init_memory` + `get_length_in_pcm_frames` + 预分配 PCM 缓冲）常带一个兜底上限 —— 形如 `if (total==0 || total > SAFE_CAP) total = FALLBACK;`，原意是「防异常大值占满内存 / 总长未知时给个合理量」。这条上限是按**该 loader 当时承载的主导资产类**定的（如全部是 <0.3s 的短 SFX → 5s 上限 + 2s 兜底，绰绰有余）。但当资产库后来**新增另一长度类**（如 8.0s 的长循环环境音床），新资产**合法地超过 SAFE_CAP**（8s > 5s），于是 total 被截到 FALLBACK（2s）——asset 文件的「首末淡化无缝循环」是设计在完整 [0, 8s] 边界上的，截到 [0, 2s) 后，循环点（2s 处的满幅中波）回绕到起点（淡化起点 ≈0）产生大幅不连续 → **每 FALLBACK 秒一次循环咔哒爆音**，正是淡化设计要消除的伪影。**整套链路读起来全合理**：decoder 返回了一个值、clip 标 ok=true、sound init 成功、播放不崩——静态/编译/降级三测全 PASS；只有「run + 肉耳」能抓到周期性咔哒。
  - **判别信号**：一个**刻意做了边界连续性**（无缝循环淡化 / loop point 零交叉）的长音，run 后听到**周期性的咔哒/爆音、且周期明显短于文件时长** → 高度怀疑 loader 把它截到 SAFE_CAP/FALLBACK 了，先查解码器的长度上限常量是否 ≥ 该文件实际帧数。**修法不是改资产**（加更长淡化没用——文件根本没被读全），是**按资产类参数化上限**（`loadClip(Clip&, ma_uint64 maxFrames)` 给短 SFX 默认 2s、给长循环床传 16s），让每类资产各自够用、又各自有独立防异常大的护栏。
  - **通用形态**：凡 loader 用**单一硬编码常量**做「长度护栏 / 缓冲预分配 / 截断点」，而该 loader 要承载**多种长度量级**（短瞬态 SFX vs 长循环床 vs 流式音乐）的资产 → 必然对某一类偏小、静默截断。护栏要**按资产类**（不是按文件名）参数化；新增资产类时**审一遍 loader 的长度上限是否覆盖**该类最大合法长度，别只看「clip 加载 ok」就以为读全了。
- **长循环 ambient 床永远不要含「宽带高通噪声层」——它在任何音量下都是持续静态噪声，且会自动淹没所有前景 SFX**：环境音床（风声 / 水流 / 雨声…）是**进游戏即自动启动、且 looping=true 持续播放**的。一条「让合成更亮 / 更细腻」的常见改动是给它**加一层高通白噪**（拟空气 / 细颗粒），再把整 clip `finalize` 峰值归一化到满刻度。结果：该层占整段 ~50%+ 高频（>2kHz）能量，且因 clip 一直 loop → **持续满幅宽带嘶嘶 = 电视雪花 / 雨声白噪**，掩盖所有前景 SFX（脚步「听不清」、动物叫「像没声」）。**根因不是音量**（调低只是把噪声变小、仍是静态），**是宽带噪声层本身**。**判别信号**：进游戏即出现**持续**的均匀嘶嘶 / 沙沙（频率分布平、无起伏包络）、且进程一启动就在、退出世界才停 → 即某个 looping ambient 床含宽带噪声层。**修法**：删掉该高通层，仅留**低通化**的低频风势 body + 慢 LFO AM（自然起伏）；用频谱平度（spectral flatness，白噪≈1 / 纯音≈0）+ 高频能量占比（>2kHz）做验收门槛（ambient 床应 flat<0.1、high>2k<10%）。**通用形态**：任何「自动启动 + 长循环」的背景音，其合成里**禁止出现裸宽带噪声**（高通 / 微分器差分都是高通变体）；要「空气感 / 细颗粒」就用**低通后**的中频沙沙 + **音高化的瞬态**（气泡 / tok / 哨音），不要用裸高通白噪——裸高通白噪在 looping 下永远是静态噪声。同理，单次触发的 mob 叫声若想「可辨」，也必须以**音高 / 共振峰 / 包络轮廓**为骨架（骨头 tok、引信哨音、虫嗡载波），裸噪声咔哒 / 嘶嘶在 0.3s 内听不出「是什么生物」。
  - 证据：t366——`gen_ambient_wind` 在 t328 加了高通「gust」层（权重 0.40）+ 满刻度归一化，频谱 flatness=0.80、>2kHz 占 53%，进游戏即持续白噪掩盖一切；删 gust 层改两级低通后 flatness=0.02、>2kHz 占 0%（白噪消失）。同族：`mob_idle_bones/stalker/spider` t328 用裸噪声合成（flatness ~0.81）听不出是什么生物，改音高化（木块 tok / 引信哨音 / 虫嗡载波）后可辨。`gen_water_flow` 的「中频流水」用微分器（本质高通）致 >2kHz 占 85%（嘶嘶），改中低通后降到 28%。

---

## 待补（未验证，开工时再回填）
- 性能 benchmark / 帧时间切分（t13 落地后补）。

---

## 体素 / 分区缓存失效（per-chunk mesher + 跨边界脏标记，t03 验证）

- **空间分区缓存的脏标记是「写者设、消费者重建后清」的协作，而非渲染层自管**：体素栅格按 chunk 分区，每个 chunk 持一个 `dirty` 标记。写者（`setBlock`）改格后标**目标 chunk 脏**；渲染层（mesher）的 rebuild 入口先检 `if (chunk.dirty())`，仅脏的重建、重建完清脏，非脏直接 return。这样 `worldChanged` 这类粗粒度信号每次编辑都广播，但 9 个消费者各检各的 dirty → **实际 rebuild 次数 = dirty chunk 数**（非脏跳过），把「全量重建」降到「增量重建」。可观测性：rebuild 时打一行 `qInfo`（chunk 坐标 + 顶点数），即可在日志核对 rebuild 计数。
  - 证据：t03——`ChunkGeometry::onWorldChanged()` 检 `myChunk()->dirty()` 才 `buildMesh()`；启动 log 见恰好 9 行 `vo.render: chunk(x,z) rebuilt`（= 9 chunk 初次全脏），无重复。
- **改边界格必须标邻接分区脏，否则邻居缓存的「边界可见性」会陈旧**：一个格的面是否可见取决于**邻居格**的实体性；当该格贴在 chunk 边沿时，它的实体性同时决定**邻接 chunk** 边界面的去留。故 `setBlock` 写边界格（局部 x/z 贴 0 或 size-1）时，除目标 chunk 外必须**同标 4 向邻接 chunk 脏**（`ChunkManager::setBlock` 已做）。漏标 → 跨边界破/放后邻 chunk 的边界面不更新 → 残留夹层面或黑缝（「看得见的破洞/黑线」根因）。**通用形态**：任何「格的派生态（可见性/光照/碰撞）依赖邻居」的分区缓存，改边界格都要级联标邻居脏。
  - 证据：t03——`ChunkManager::setBlock` 在 `lx==kSize-1/0`、`lz==kSize-1/0` 时各标 ±X/±Z 邻 chunk 脏；跨 chunk 边界面剔除走 `world.blockAt`（跨 chunk 路由）→ 相邻实体共边面剔除、一侧空气画出、越界=空气 → 3×3 无缝。
- **per-chunk mesher 的顶点用 chunk 局部坐标 + Model 世界定位，bounds 用局部 AABB**：每个 chunk 的 `QQuick3DGeometry` 产出的顶点是 `[0,size)` 局部坐标，QML 把 `Model` 摆到 `(cx*size, 0, cz*size)` 做世界定位；`setBounds(0, size)` 是局部 AABB，配合 Model 变换给出正确的逐 chunk 视锥剔除盒。邻居查询仍走**世界坐标** `blockAt`（跨 chunk 路由），故「顶点局部、查询世界」两套坐标不冲突。
  - 证据：t03——`ChunkGeometry::buildMesh` 顶点写 `lx/dx`，邻居查 `blockAtWorld(originX+lx+dir, ...)`。
- **每格并行元数据（state/light/...）在「改 id」的写操作里会被重置，凡需用旧元数据推导后续写的，必须在改 id 之前快照**：本工程 `Chunk` 把方块的 state（朝向/开合等）存在与 `m_voxels` 并行的 `m_states` 数组同索引处；**4 参数 `setBlock(id)` 委托 5 参数版以 `state=0` 写入**（`chunk.cpp` 注释钉死「id 变更时重置 state=0 是正确语义，防 stale 残留」）。于是 `setBlock(Air)` 之后 `stateAt` 永远返回 0。若某逻辑需要**原 state** 来计算后续写（典型：破门时据 `bit3=isUpper` 判配对格在 y-1 还是 y+1，再去清配对格），把 `stateAt` 放在 `setBlock(Air)` **之后**就会读到 0 → 配对方向恒算成单一方向 → 破上格时清不到下格，留半截悬空。**通用形态**：凡是「读元数据 → 推导 → 改 id（顺带把元数据重置）」的级联，**先快照元数据、再改 id**；或用 5 参数 `setBlock(id, oldState)` 显式保留。**判别信号**：同一份元数据，一条代码路径（如 useBlock：setBlock 前读）工作正常、另一条平行路径（如 finishMiningAt：setBlock 后读）行为退化为一侧永远失灵 → 即此坑（不是设计意图差异，是读时序疏漏）。**自检**：审任何「先 setBlock 再 stateAt」的相邻行，问「这个 stateAt 想读的是旧值还是新值」——若要旧值（推导配对/朝向），必须在 setBlock 前 capture。
  - 证据：t134——`PlayerController::finishMiningAt` 在 `setBlock(Air)` 后读 `stateAt` 得 0，破门上格（原 bit3=1）时 `py=y+1`（应 y-1）→ 下格不清、留悬空门；同文件 `useBlock` 路径在 setBlock 前读 st 故无此 bug。修复 = `brokenState` 提到 `setBlock(Air)` 之前 capture。
- **按「id 段」路由渲染 / 碰撞的判定必须用闭区间哨兵，单边 `id >= FirstX` 会在段后追加**非 X 类**方块时静默误路由**：本工程把异形方块（slab/stairs/...）放在连续 id 段 `[FirstPartial, LastPartial]`，mesher 据此分流——`>= FirstPartial` 进 `PartialBlockGeometry` 异形路径、`< FirstPartial` 进 1×1×1 立方面 culled 路径。但后来**段后**又追加了非异形方块（Water=21 / Chest=22），它们的 id 仍 `>= FirstPartial` 却**不是异形**（Water 走水段、Chest 是整立方 `ShapeFull`）。单边 `>= FirstPartial` 把这类段后方块一并误判为异形 → 路由进 `PartialBlockGeometry`，而其 `switch` **无对应 case** → `default: return 0`（追加 0 顶点）→ **该方块在世界里渲染成完全透明**（碰撞 / 右键交互因走 `shape`/`useBlock` 另一条路径而正常，故「放得下、点得开、但肉眼看不见」）。**判别信号**：某方块「能放、能碰、右键有反应，唯独肉眼看不着」（透视格子 / 空气感）→ 高度怀疑它的 id 落在某个「段哨兵」之后却被单边判定误归进该段，mesher 的 switch 又没它的 case。**通用形态**：任何按连续 id 段做「类型分流」（异形 vs 整立方 / 可挖 vs 不可挖 / 可燃 vs 不可燃…）的哨兵，**段是有上界的区间而非单边开区间**——一旦段后还会追加**非该类型**的新方块，单边 `>= FirstX` 必误判；要么用闭区间 `[FirstX, LastX]`（追加同类型时右移 LastX），要么用显式谓词（`isPartialBlock(id)` / `shape(id) != ShapeFull`）。**自检**：审所有 `id >= <某哨兵>` 的判定，问「这个哨兵之后还会不会追加**不属于该段类型**的方块？」——会则必改闭区间 / 谓词。mesher 路由的 switch 若按 id 取 case，**必须保证凡进该路径的 id 都有 case**，否则 `default` 追加 0 顶点 = 静默透明（非崩溃、非警告，三测难抓）。
  - 证据：t194——`PartialBlockGeometry::append` 的 switch 只有 6 个异形 case（WoodSlab...WoodTrapdoor）+ `default: return 0`；`chunkgeometry` 用 `b >= FirstPartial(15)` 单边把 Chest(22) 误路由进此路径 → 无 case → 0 顶点 → 放置后箱子透明（透视格子），而 `shape=ShapeFull` 的碰撞 + `useBlock` 右键开箱正常。修：`BlockRegistry` 加 `LastPartial = WoodTrapdoor(20)` 哨兵，3 处 mesher 路由 + QML 选中框路由统一改 `>= FirstPartial && <= LastPartial` 闭区间 → Chest 落回 culled 立方面路径（与工作台/熔炉同，按 chest_top/side/front 各面贴图渲染）。同一 id 段误路由坑（单边 `>=` 把段后非段类方块归错）会随「段后继续追加新方块」反复复发，闭区间哨兵是结构性根治。

---

## C++ / moc 陷阱（ViewModel 暴露列表给 QML，t18 验证）

- **本工具链（Qt 6.11.1 MinGW）的 moc 拒绝 `Q_PROPERTY(QVariantList …)`**：写 `Q_PROPERTY(QVariantList slots READ slots NOTIFY …)` 会报 `Parse error at "READ"`，且与注释无关（删掉 Q_PROPERTY 之间的中文注释仍失败）。`QVariantList` 作 Q_INVOKABLE 方法的**返回类型**则完全没问题（极常见）。**通用解法**：列表数据用 `Q_INVOKABLE QVariantList xxx() const` 取，另加一个 `Q_PROPERTY(int revision READ revision NOTIFY changed)` 作版本号；QML 的 Repeater `model` 绑定写成 `model: { vm.revision; return vm.xxx() }`——「触碰 revision」建立对 NOTIFY 的依赖，changed 后绑定重算返回新数组 → 整列重建。这等价于 `Q_PROPERTY(QVariantList … NOTIFY)` 的自动刷新，且 moc 安全。**判别信号**：moc 报 `Parse error at "READ"`/`"WRITE"` 且出错行是 `Q_PROPERTY(QVariantList …)` → 即此坑。
  - 证据：t18——`Hotbar` 槽内容最初想做 `Q_PROPERTY(QVariantList slots NOTIFY slotsChanged)`，moc 报 parse error；改为 `Q_INVOKABLE slotList()` + `Q_PROPERTY(int slotRevision NOTIFY slotsChanged)` + QML `model: { hotbarVM.slotRevision; return hotbarVM.slotList() }` 后通过；`setSlotBlock` 里 `++m_slotRevision; emit slotsChanged()` 触发刷新。
- **`slots` 是 Qt 关键字宏，不能作方法/成员名**：`QVariantList slots()` 会被预处理成 `QVariantList ()` → 编译报 `expected unqualified-id before ')' token`（moc 生成的 `_t->slots()` 同样炸）。同理 `signals`/`emit`/`Q_SLOTS` 等都保留。给「槽位列表」取访问器名时用 `slotList()`/`slotsModel()` 之类，避开裸 `slots`。
  - 证据：t18——`Hotbar::slots()` 编译失败在 `.h`/`.cpp`/`moc_hotbar.cpp` 三处同报；改名 `slotList()` 后通过。
- **Q_PROPERTY 的 NOTIFY 只能挂一个信号，多触发源要「挂主、补发副」**：当一个属性（如 `selectedBlockId`，NOTIFY=`selectedSlotChanged`）的值既随选中槽**索引**变、又随选中槽**内容**变，而 Q_PROPERTY 只允许一个 NOTIFY 信号时，把信号挂在「索引变更」上，在「内容变更」路径里**条件补发**同一个信号（仅当改的是当前选中槽时发）。这样消费者（QML 绑定 / `player.selectedBlock`）两条路径都刷新，且非选中槽内容变更不触发无谓重算。
  - 证据：t18——`Hotbar::setSlotBlock` 改非选中槽只 `emit slotsChanged()`；改选中槽额外 `emit selectedSlotChanged()`（驱动 `selectedBlockId`→`player.selectedBlock`→HUD 手持名刷新）。

---

## 渲染盲区静态化（第 5 轮 8-bug 教训：harness 三测全 PASS 但 8 项视觉/交互 bug 全漏）

> 第 4 轮 t25-t29 三测全 PASS、第 5 轮用户验收发现 8 项 bug。根因：correctness 角色被指示「代码路径合理 → PASS，视觉项标『需人工验证』不计 FAIL」，于是 workflow 自动 commit，盲区全部漏网。以下把**已知会复现的视觉/交互失败模式**从「需人工验证」**升格为可静态判定的 FAIL 项**——见到即 FAIL，不得用「代码合理」搪塞。

- **所有要肉眼可见的 3D `Model` 必须用 `PrincipledMaterial { lighting: NoLighting }`**：本工程里**默认 lit 的 PrincipledMaterial 不渲染**（地形/线框/粒子全用 NoLighting 才可见；第 4 轮玩家模型 + 第一人称手用默认 lit → 三视角全空/手完全透明，三测却 PASS）。**判据**：新加任何可见 Model，扫其 `materials:` —— 是 `NoLighting`？不是 → **FAIL**（除非能引用本工程已验证 lit 可渲染的先例，目前无）。这是比「在不在场景图」更深一层的可见性检查（t16/t03 解决了「孤儿不进场景」，本条解决「进了场景但材质不出像素」）。**别再写「编译通过即应可见」**——lit 材质编译通过照样不渲染。
  - 证据：第 5 轮 commit `9a1de7c`——模型/手 6+1 个材质补 `NoLighting` 后才可见；t28/t29 correctness 报告写了「编译通过即应可见」+ FOV 推算，却漏查材质，判 PASS → bug 漏到用户验收。
- **`ModelParticle3D` 实例忽略 delegate 的 `scale`，粒子实际大小由 `ParticleEmitter3D.particleScale` 决定**：给 delegate `Model { scale: Qt.vector3d(0.12,…) }` **无效**（仍是 1 格大方块）；必须设 `emitter.particleScale: 0.12~0.15`。第 4 轮 c490c05 误判为「delegate scale 覆盖 particleScale」反向修，导致破/放迸发满屏大方块。**判据**：审粒子代码，见 delegate 显式 scale 而 emitter `particleScale` 缺省/为 1 → **FAIL**（碎屑会满屏）。
  - 证据：第 5 轮——`particleScale: 1.0` + delegate `scale 0.12` 实测渲染成 1 格大方块满屏；改 `particleScale 0.15/0.12` 后正常。
- **全屏遮罩 `MouseArea { onClicked: panel.close() }` 会让「点面板外部」误关面板**：MC 风格背包只能由 E/Esc 关，点外部应**仅吸收点击**（防穿透到背后游戏层）而不关闭。**判据**：背包/弹窗类面板的背景遮罩若带 `onClicked: close` → **FAIL**（交互错误）；遮罩应 `MouseArea { anchors.fill: parent }` 无 `onClicked`。
  - 证据：第 5 轮——Inventory.qml / SurvivalInventory.qml 遮罩删 `onClicked` 后点外部不再误关。
- **背包「移动物品」需要光标手持物系统，不是「点调色板→装 hotbar」**：MC 背包交互是「点击拾取→物品贴光标→点击放置/互换」。仅做 `TapHandler{ setSlotBlock(selectedSlot, x) }` 用户会判「单击完全没用」。**判据**：背包任务验收若含「移动物品/拖拽」，代码无 `heldBlock`（或等价光标手持态）+ 各槽 pickup/place → **FAIL**（功能未实现，非「需人工验证」）。
  - 证据：第 5 轮——Hotbar 加 `heldBlock` Q_PROPERTY + Main.qml 浮动光标图标 + 各槽点击拾取/放置/互换 后用户诉求才满足。

> **元教训（适用所有 tester）**：「需人工验证」是**诚实的边界标注**，不是 PASS 的借口。当一条**核心验收标准**（如「第三人称可见玩家模型」「背包可移动物品」）无法静态判定时，应判 **⚠️ NEEDS-RUN（待主编排 run+肉眼/日志验证后再 commit）**，而非 PASS。workflow 见 ⚠️ 不得自动 commit，须交主编排验证。仅「边缘打磨项」（如幽灵半透的透明排序观感、树冠形状美感）才用「需人工验证」且仍 PASS。判别：该标准**是否属本任务核心交付**？是 → ⚠️；否 → 需人工验证。

---

## 工程重组 / 文件搬迁（t41 验证）

- **`git mv` 保留原 mtime → 增量构建看不出文件移动 → 复用按旧布局编译的 qmlcachegen/moc/object 产物 → 静默运行期损坏**：搬迁源文件后 `cmake --build` 报「绿」（链接成功、exe 刷新），但运行期 QML 报「`XxxType is not a type`」/ 行为回退，因为 qmlcachegen 预编译的 Main.qml 字节码（`.rcc/qmlcache/voxelsandbox_Main_qml.cpp`）还停在搬迁前的时间戳，ninja 认为无需重生成。**判别信号**：搬迁后构建绿但运行报「type 不可用」或行为异常，且 qmlcache/*.cpp 的 mtime 早于本次改动 → 即此坑。**通用修法**：搬迁后**显式 `touch` 所有搬动的源文件**（.cpp/.h/.qml + CMakeLists.txt）再构建，强制 qmlcachegen/moc/编译全部重跑；或直接 clean rebuild。别信「构建绿 = 二进制含最新源」——文件移动不改变内容哈希时，mtime 是构建系统唯一的「需重建」信号，git mv 把它抹平了。
  - 证据：t41——`git mv` 后首次构建运行报 `CrackBox is not a type`；查 `voxelsandbox_Main_qml.cpp` mtime 停在搬迁前（13:36），touch 全部源 + 重建后 qmlcache 重生（产物名变成 `voxelsandbox_src/ui/Main_qml.cpp`）、`root objects after load: 1`。
- **QML 文件迁入模块子目录后丢失对模块 C++ 类型（QML_NAMED_ELEMENT）的隐式访问**：qt_add_qml_module 把 `src/ui/Main.qml` 注册为模块类型 `Main`，但其**资源路径**保留源相对结构 → `qrc:/VoxelSandbox/src/ui/Main.qml`（子目录，非模块根）。QML 的隐式 import 只含「当前目录的兄弟 .qml」，**不含**模块的 C++ 注册类型——后者只对「模块根的文件」或「显式 `import <OwnModule>` 的文件」可见。于是 Main.qml 里 `World { }` / `Hotbar { }` / `CrackBox { }` 全报「is not a type」，而同目录的 `Loader{source:"BlockParticles.qml"}` 仍能解析（兄弟文件）。**判别信号**：QML 文件在模块子目录、运行报「某 C++ QML 类型 is not a type」、但兄弟 .qml 互相引用正常 → 即此坑。**通用修法**：在用到 C++ 类型的 QML 文件顶部加 `import <OwnModule>`（Qt6 子目录文件访问自身模块 C++ 类型的标准做法；自身 import 不递归、不告警）。仅 `var` 属性 + QtQuick 内置类型的纯组件（Canvas 像素图、背景槽等）不需要。
  - 证据：t41——迁 src/ui/ 后 Main.qml 报 `WorldClock is not a type`（line 67）；Inventory.qml/SurvivalInventory.qml 用 `property Hotbar hotbar`（**类型化**属性）同样需 import，`var` 属性的 VitalIcon/ToolIcon/InvSlot 不需要。三者加 `import VoxelSandbox` 后 `root objects after load: 1`。
- **分层铁律 vs「建议结构」冲突时，以 PLAN §2 为准**：dev-spec 的文件夹「建议」把 raycast 列在 `src/Core/`，但 `raycast.cpp` `#include "world.h"` → Core 依赖 World = 低层依赖高层 = 破 PLAN §2 铁律。扁平结构下此依赖不可见，**一旦引入分层目录就把「既存的向上依赖」暴露成可见违规**。**通用原则**：搬迁到分层目录时，逐文件核对其 `#include` 方向——叶子（无内部 include）可放最低层；凡 include 了某层头文件的，必须放在**同层或更低**于被依赖项的目录。「中性重构」不创造新依赖，但**不得把既存依赖包装成新违规**。
  - 证据：t41——raycast 按 dev-spec 进 Core 会违铁律；改放 `src/Game/`（与消费者 playercontroller 同层，world.h 在 World 层 = 向下）后全工程 include 方向严格向下。

---

## 同一贴图在多个呈现路径的 alpha 处理契约（t169 火把黑底 / 太阳不显示 / 不完整方块图标）

> 元原则：**「同一份带 alpha 的贴图被多个 Model 共用时，每条呈现路径都必须各自履行 alpha 契约」**。
> 漏掉任意一条 → 该路径渲染回退为「透明底当不透明黑」/「被深度遮蔽」/「剪影坍成方块」，且静态编译 / 三测全过
> （每条路径单独看代码都「合理」），只在用户 run + 肉眼才暴露。判别：贴图含 alpha 但只在一两处设了
> alphaCutoff / opacity<1 → 扫所有消费该 tile 的 Model，逐处补齐。

- **带透明底的 tile（火把 tile、MaterialIcon Canvas、crack 贴图）铺到 BlockCube 6 面时，承载 Model 必须显式
  alphaCutoff:0.5**：PrincipledMaterial 默认不透明路径会把 alpha=0 的透明像素当 RGB=0 不透明黑渲染 → 整面成
  黑色填充（用户肉眼「黑底 / 黑方块」）。同一方块（如火把 id 13）会在**多个呈现路径**同时出现 —— 第一人称手持
  BlockCube、第三人称手持 BlockCube、掉落实体 BlockCube、（未来）任何复用 BlockCube 的地方 —— 每条路径
  都须各自加 `alphaCutoff: blockId === Torch ? 0.5 : 0.0`。**漏一处 = 那一处黑底**（其它处正常 → 用户混淆「有时
  黑有时不黑」）。**判别信号**：用 Grep 搜某 tile 的所有 BlockCube 消费处，比对各自的 PrincipledMaterial 字段，
  见 `baseColorMap: voxelAtlas`（共享图集）但无 `alphaCutoff` → 该路径对火把类透明底 tile 必黑。
  - 证据：t169——第一人称手持火把（viewModelHand）有 `alphaCutoff: selectedBlock===13 ? 0.5 : 0.0`，但掉落实体
    Repeater 的 BlockCube + 第三人称 playerModel 手持 BlockCube 都没设 → 用户实测掉落 / 第三人称持火把黑方块。
    三处统一加 alphaCutoff 后一致。
- **「天空元素」（太阳 / 月亮 / 远景 billboard）必须摆在「视点远端」= 远超世界几何边界，否则被深度测试遮蔽**：
  把太阳摆在距相机 40 格的 sunDir 方向，看似合理（数字干净），但 5×5 chunk 世界地形从原点向 ±40 延伸 →
  距眼 40 恰好落在地形体积内（黄昏 sunDir.y≈0 时太阳水平距 40 = 地形边缘）。 opaque PrincipledMaterial 走
  深度测试 → 太阳与视线相交的地形/树叶胜出 → 太阳被遮蔽（正午头顶尚可见，其余时段隐身）。机制对齐 MC 1.0：
  太阳贴在「天空穹顶」= 视点无穷远。**通用修法**：把天空 billboard 推到距眼 ≥ 数倍世界半径（如世界 ±40 → 太阳
  距 500），scale 同比放大保角尺寸；clipFar 据此调够（500 < clipFar 1000）。**判别信号**：天空 billboard 在某些
  视角 / 时段可见、其它不可见、且不可见时「视野里没遮挡物」→ 高度怀疑被远端地形深度遮蔽，先排查距离。
  - 证据：t169——太阳 Model 距眼 40，5×5 世界地形 ±40 → 太阳常落在地形后；推到 500 + scale 80 后太阳在天
    空穹顶、地形永不遮蔽。
- **异形方块的「图标」应与世界内几何同走立体投影，平面 2D 剪影与立方体图标混排使槽位观感割裂**：v1 把 6 类
  不完整方块（slab/stairs/trapdoor/pressure_plate/door/fence）中 4 类做 3D dimetric、2 类（door/fence）留
  flat 2D 剪影 → 创造调色板里前 4 类立体、后 2 类平面剪影，肉眼观感割裂 + door/fence 与满方块立方体难辨。
  **通用原则**：同一「类别」的物品图标路径应统一（要么全 3D 立体、要么全 2D 剪影），混排只在「该形状无法用
  立体投影表达」时（如工具的 ToolIcon 镐形 / 材料的 MaterialIcon 像素图 —— 这些是「非方块物品」走自绘）。
  异形方块（slab/stairs/door/fence/...）的世界内几何本就是轴对齐盒组合 → 图标用同一套盒组合 + dimetric
  投影（render_partial_3d 的 `_render_box_d` + depth buffer 解多盒遮挡）即天然可表达，无剪影必要。
  - 证据：t169——door/fence 由 flat 2D 升级为 3D（door=薄板 box / fence=立柱+横档 3 box），6 类同为立体，
    槽位观感统一；door 用 x[0,1]·y[0,1]·z[0,3/16] 的 3/16 厚薄板（dimetric 视角见大面 + 薄边），fence 用 4/16
    方柱 + 2 横档的 3-box 组合（depth buffer 解相交遮挡）。

---

## 序列化 round-trip 保真（存档 / 读档，t176 验证）

- **默认值兜底必须严格区分「字段缺省」与「字段值为合法 0/空」，`||` 一类的 truthy/falsy 兜底会把合法 0 误当缺省 → round-trip 不保真**：任何「存 → 读」的序列化系统，反序列化端常对「旧存档可能缺某字段」做兜底（缺则用默认值）。在 JS/QML 里写成 `player.x = data.x || 40` 看似优雅，但 `||` 的右操作数是**任何 falsy 值**都触发 —— `0`、`NaN`、`""`、`false` 全部被当「未提供」而回退默认。于是当存档字段**恰好存了 0**（玩家走到世界边沿 x=0、水平视角 pitch=0、Spectator 模式 mode=0、第 0 槽选中），读回时被替换成默认值（40 / -42 / 出生态 X），存什么≠读什么。**判别信号**：在「save 的写者总写齐全部字段、load 的读者用 `||` 兜底」的 round-trip 里，存 `0` 读回非 `0`（或读 default）→ 即此坑；静态读「每个字段单独看都合理」（特别是同一函数里别的字段用了正确的 `!== undefined ?`）极难发现，根因在 `||` 的语义混淆，不在字段名。
  - **通用修法**：用 `key !== undefined ? value : default`（或 `nullish coalescing` `value ?? default`，后者只对 `null`/`undefined` 回退、对 `0`/`""`/`false` 保留）显式区分「缺省」与「合法 falsy 值」。**自检**：审 round-trip 路径里所有 `value || default` 形态，问「这个 value 的合法取值域是否包含 0 / "" / false / NaN？」——包含则必改 `!== undefined ?`。这是**语言级**坑（JS/Python 的 `or`/`||`、Shell 的 `${x:-d}` 全同族），不限于存档 —— 任何「输入可能缺省、但合法值含 falsy」的默认值赋值都适用（配置项开关、enum=0 的首项、坐标 0 原点）。
  - 证据：t176——`Main.qml::applyPlayerState` 用 `data.px || 40 / data.pitch || -42` 兜底，存 pitch=0（水平视角）读回 -42（俯视）、存 px=0（世界边沿）读回 40（出生点）；同函数 `health/hunger` 用 `!== undefined ?` 正确，证明作者知道正解只是疏漏。改 6 个 `||` 全为 `!== undefined ? DF.x` 后保真。`gatherPlayerState`（写者）总写齐字段 → `!== undefined ?` 在正常 round-trip 永走前者，旧存档兜底语义也被更精确表达。

---

## QML/JS 信号处理器内的静默退化（t205 验证）

- **`.js` / QML 信号处理器函数体内引用**未定义**变量 → 运行期 `ReferenceError`，QML 引擎吞掉异常（仅 console 报一行错，app 不崩）→ 该信号路径的功能静默失效，但「编译过 + 三测全过 + app 启动 root objects=1」全绿**：qmlcachegen 只做词法/语法层 AOT 编译，**不**做函数体内「标识符是否已定义」的引用解析（JS 动态语义使然），故 `parseInt(p[1], …)` 里 `p` 从未声明也能通过 AOT、链接、ninja「no work」全绿。运行期首次执行到该行才抛 `ReferenceError: p is not defined`；而该函数若由 QML 信号处理器调用（`DragHandler.onActiveChanged` / 逐槽 `HoverHandler.onHoveredChanged` → 共享 `.js` 库函数），异常被 QML 事件循环捕获、记一行日志后**继续**，不冒泡不崩。结果：该交互**全程坏掉**（每次触发抛错、核心动作未执行），用户「run + 肉眼」才见，静态/编译期三测全 PASS。
  - **判别信号**：某 QML 交互「编译过、app 不崩，但功能完全不工作 / 退化成另一更简单行为」，且日志里能搜到 `ReferenceError: <标识符> is not defined`（可能混在大量正常日志里被忽略）→ 高度怀疑该交互的 `.js`/信号处理器函数体里有未定义变量名。**优先 grep 该功能的 `.js` 函数体里所有「裸标识符.属性」访问，核对每个标识符在同一作用域有声明**（典型：复制邻近函数时把 `p0` 写成 `p`、`group` 写成 `grp`、循环变量 `i`/`j` 混用）。
  - **通用形态 / 自检门槛**：(1) 凡修改 `.js` 共享库或 QML 信号处理器（`onXxx`、`Connections`、Handler 信号）的 JS 函数体，自测**必须 run + 触发该交互**，仅「build 绿」不算 PASS（同 t16/t23/t31「编译过但运行期坏」同族，本条是其 JS 变体）。(2) 复制粘贴邻近函数后，逐行核对函数体内**每一个被读的标识符**是否在当前作用域声明（参数 / 局部 `const`/`let` / 外层闭包 / QML 上下文属性）—— 最易错的是 `split`/解析后取下标的 `p`/`p0`/`parts` 之类短名。(3) 经验级防呆：`.js` 库可加 `'use strict';`（pragma library 模块不支持 strict，但可对易错函数手动校验入参）；或把「解析 key 取 index」抽成一个 `indexFromKey(key)` 辅助，单点定义、多处复用，杜绝 `p0`/`p` 拼写漂移。
  - 证据：t205——`InventoryOps.js::addRightDragSlot`（右键拖拽每格放1 的右键路径分发函数）写 `placeOneInSlot(root, p0[0], parseInt(p[1], 10))`，本作用域只声明了 `p0`（`const p0 = key.split(":")`），`p` 未定义 → 每滑入一格即抛 ReferenceError、`placeOneInSlot` 永不执行 → 右键拖拽全程不放物，只有松手时 `endRightDrag` 微拖退路（`rightDragPlaced` 仍 false → `singleRightClick`）补放 1 个到松手格 = 用户观感「右键单击放1正常，但右键拖只放1个/没激活」。改 `p`→`p0` 后每格放1 生效。配套：左/右拖拽两套 `dragSlots` 路径此前无任何面板把右键 `rightDragSlots` 绑到绿框高亮（左键有、右键无），右键拖拽无视觉反馈 —— 同任务一并补 `rightDragHasKey` 暴露 + 各面板高亮 visible 加右键分支。

---

## 透明体积网格化的「邻实体剔面」契约（t222 验证）

> 元原则：**culled meshing 的「邻实体 → 剔除本面」是对**不透明**自渲染的正确优化（实体邻居的面填上缺口），
> 但对**透明**体积（水 / 半透流体，独立透明 mesh 段）是错的 —— 透明材质下，被剔除的侧壁不再封闭体积，
> 透过透明顶面斜视会穿透到背后的实体 / 水底，体积从「满」坍成「空壳」，贴图像消失 / 变透明**。

- **透明体积必须由「自己的面」自我封闭，不能借实体邻居的面来封**：opaque 体积（地形）邻实体剔面 OK ——
  实体邻居的不透明面正好挡在缺口上，观感无差。但**透明**体积（水段独立 mesh、opacity<1）邻实体剔面后，
  本格侧壁消失 → 透明顶面斜透视穿过缺口见到背后实体 / 水底 → 用户观感「水面贴图消失 / 透明、透视见底」。
  根因不在「是否重建」（dirty 标记 / 邻接脏都对，mesh 确实重算），而在**重算出的 mesh 少了封闭体积的那面**。
  - **判别信号**：透明流体（水 / 熔岩 / 半透 X）「邻接实体方块处贴图消失 / 可透视见底」，而邻接空气 / 同类
    处贴图正常 → 高度怀疑 mesher 用了 `if (isSolid(nb)) continue;` 把透明体积贴实体那侧的面整面剔了。grep 该
    流体段的侧面剔除分支，见 `isSolid(neighbor) → continue` 且该段材质 opacity<1 → 即此坑。
  - **修法**：透明体积的侧面剔除要**区分「满高」与「降水面」**——满高格（state=0，水面 = cell 顶 1.0）邻实体
    仍可剔（实体完全遮挡，画了只在实体面叠一层半透色 = z-fight / 渗色，无收益）；**降水面格（state>0，水面 < 1.0）
    邻实体不剔**，画 `[0, myTop]` 满侧保持贴图可见、封闭体积。即把 `if (isSolid(nb)) continue;` 改为
    `if (isSolid(nb)) { if (满高) continue; /* 降水面：画满侧，不 continue */ }`。
  - **通用形态**：凡**独立透明 mesh 段**（水 / 半透 / 玻璃 / 能量护盾…）做 culled meshing，其「邻实体剔面」
    规则不能照搬 opaque 地形的规则。透明体积要**自我封闭**：邻实体时仍画自己的侧壁（哪怕几何上与实体面重合、
    透明叠在不透明上 = 正确的「流体贴壁」观感），否则体积漏空。降水面 / 异形透明体积尤其敏感（侧壁是可见贴图
    特征，而非被完全遮挡）。**自检**：审透明段的每条 `isSolid(nb)→continue`，问「本面被剔后，透明体积是否还
    封闭？」——漏空即改；满高被实体完全遮挡的可保留剔除。
  - 证据：t222——`chunkgeometry.cpp` 水段（PASS 2）侧面剔除 `if (isSolid(nb)) continue;` 对**所有**水格（含
    流水 state>0）一律剔。t198 玩家在流水格 F 放方块（setBlock 覆盖水→实体）后，邻接流水 N 朝 F 的侧壁被剔 →
    流水侧壁贴图（water_flow）消失、透明体积漏空 → 用户「水面贴图消失 / 透视见底」。修：流水（state>0）邻实体
    不剔、画 `[0,myTop]` 满侧；水源（state=0）保留剔除。改动仅在 mesher 一处（dirty 标记 / 邻接脏本就正确，setBlock
    无需改）。**needs-run 复核**：透明体积封闭的实际观感（邻实体后流水侧壁显出、无 z-fight / 黑缝）属视觉项。

- **凡「在逐帧 tick 里读玩家格坐标 floor(pos) 再判碰撞/嵌入」的逻辑，嵌入检测必须与同类逻辑共用同一「内缩容差」，否则边界浮点残差会把玩家自己站立的方块误判为嵌入 → 硬瞬移 = 单机 rubberband。** 本工程玩家物理 pos 存浮点脚底，重力 + 落地 snap 留 eps 余量使 `m_pos.y` 常贴整数边界（如 `64.9999` 而非 `65.0001`）。`floor()` 因此落到**脚下方块格**（而非脚内空气格）。此时若嵌入判定用裸「任意 ε 接触即重叠」，玩家 AABB 与「自己站其上的地面方块」产生 hairline Y 重叠 → 被判中心列嵌入 → `extrudeEmbedded` 把玩家横向推到邻列。同高邻列通常也是地面 → 推回 → 来回抖 = 用户报告的「已站定却被瞬移回去，像联机延迟」（实为单机碰撞修正误判，非存档/网络）。**修法**：嵌入检测内缩 `kEmbedTol=0.1`（玩家 AABB 各轴向内收 0.1 后再测 3 轴严格重叠），仅「显著嵌入」（穿透 >0.1：落沙压身 / 侧面塞入 / 卡墙）才触发推出；hairline / 边界 FP 不触发。**关键**：同一文件里 `isLockedBuried` 早在此前修复（t289）已加此容差，但与其**同源同判据**的 `extrudeEmbedded` 漏改 → 一处修了「误锁移动」、另一处仍「误瞬移」，属同一根因的两副面孔。**通用形态**：审任何「逐帧读 floor(浮点坐标) 定格 + 该格碰撞判定」的相邻函数，问「它们用的是同一容差吗」——若一个加了边界 FP 容差、另一个裸判，后者必在某类 FP 边界态复发前者已治的症状（移动锁死 / 瞬移 / 穿墙等）。**自检信号**：单机出现「像联机 rubberband 的偶发瞬移」且日志无存档/重生/网络事件 → 锁定逐帧碰撞修正（嵌入挤出 / 锁定 / snap）的边界 FP 误判。**判别**：去数值化（不提具体 Y 值 / 函数名）仍成立——「浮点坐标贴整数边界时 floor 落错格 + 嵌入判定无容差 → 自身支撑块被当嵌入 → 横向推出」是引擎级机制，换任何体素游戏都成立。

---

## 铰链板件开合动画（箱子盖 / 门 / 活板门 / 阀门类，t441 验证）

> 元原则：**绕轴翻转的板件，其「开态立起高度」由「从铰链轴沿摆动平面量出的延伸量」（俗称"深 / 臂长"）
> 决定，而非板件的「厚度」。反复调厚度对开态轮廓零效果 —— 这是「铰链动画反复修仍坏」的典型真因。**

- **铰链板件开 ~90° 后的立起高度 = 它离铰链的「延伸量」，不是「厚度」；开态"像第二只整块"要减的是延伸量而非厚度**：一块绕后棱 +X 翻 ~90° 的盖板（箱子盖），其顶点经旋转后，「垂直于铰链轴、沿摆动平面指向远处的那一维」（盖子的"深"，从后棱指向前缘）会被旋转**映射成竖直高度**；而「沿铰链轴方向的厚度」（盖子多薄）只决定立起后面板"边缘多厚"，**不**决定它立多高。于是：满深 1.0 的盖板开 ~90° 立起 ~1.0 高 = 与整立方本体等高 → 正面呈现一张满格贴图面 → 肉眼读作「箱子背后立着第二只箱子」（额外错位件）。历次"修"只改厚度数值（0.16→0.10→0.25），而**厚度根本不进开态高度公式** → 改了等于没改 → 即「反复修仍坏」。修法：把盖板「沿摆动平面的延伸量」缩短（如箱子盖缩到 0.5 深 = MC 8/16），开 ~90° 只立起 ~0.5（明显矮于 1.0 本体）→ 读作「翻起的盖子」。**判别信号**：铰链板件开态「太高 / 像独立整块 / 像第二只箱子」→ 量它开态的**投影高度**，对照它「沿摆动平面离铰链的延伸量」（不是厚度）；投影高度 ≈ 延伸量 × sin(开角) 即此机制。**通用形态**：凡绕轴翻转开合的板件（箱子盖 / 门 / 活板门 / 阀门 / 翻斗 / 吊桥），"开态显得太满 / 像独立块"时，调「离铰链的延伸量」而非「板厚」；MC 箱子盖正是 8/16 浅深（非满格深）才开态自然。
- **3D 变换几何「对不对」无法靠读 transform 数值 / scenePosition 判定 —— 一个 pivot+hinge+scale 嵌套的数学可以完全正确（逐节点 scenePosition 精确对上手工算），渲染出的体积投影轮廓仍可能是错的（满深盖立起满高像第二只箱子）。** scenePosition 只确认了「节点原点的位置」，确认不了「整块几何顶点经全套变换后的投影体积」。判铰链 / 旋转板件几何对错，**必须实测开态投影轮廓**：抓「子件不渲染」与「子件渲染（开态）」两帧，做**像素差分** → 差分区域 = 那个运动子件的真实投影轮廓（隔离掉场景里同色干扰 —— 单一高亮色会被场景同类色如草地 / 本体棕色污染， bbox 量到的是场景而非子件）。量差分区域的宽高比即可判「立起多高 / 是否像独立整块」。**判别信号**：铰链 / 旋转板件几何"数值都对但观感错" → 别再读 transform，改抓两帧差分量投影轮廓。**自测门槛**：改任何旋转 / 翻转 3D 子件的几何（盖 / 门 / 摆臂 / 旗），自测**必须**含「合态 vs 开态两帧差分量轮廓」，仅「scenePosition 对 / 编译过」不算 PASS —— 前者只验了原点，后者验了体积，两者不等价。**元教训**：「数值正确」≠「渲染正确」；3D 呈现层的几何验收维度是「投影体积」，只能靠差分实测，不能靠 transform 数学推导（推导只覆盖原点，覆盖不了体积）。
  - 证据：t441——箱子盖 t409 等历次只改 `scale` 厚度数值（0.16→0.10→0.25），开态观感不变（仍「整方块 + 额外错位件」）。实测：盖子 `scale (1.0,0.25,1.0)` 满深 1.0，开 105° 立起满高（差分轮廓 33×31 近方形 = 满格面），scenePosition(16.5,66.515,118.009) 精确对上手工算（数学"对"）。改 `scale (1.0,0.30,0.50)`（缩**深**到 0.5）+ 开角 105→95，开态差分轮廓降到 33×16（宽矮，约为本体一半高）→ 读作翻起的盖子。改 `scale(0.1,0.1,0.1)` 验证 scale 确实生效（盖子缩成不可见），排除"scale 被忽略"误判（早先误用宽松颜色阈值量"盖子"，把棕色本体当成了盖子 → 差分法才隔离出真盖子）。

---

## worldgen 「表层 y」查询须用「铺表层方块的那个高度源」（t446 验证）

> 元原则：**worldgen 把方块铺在某 y，之后另一个 worldgen 特征（散布植物 / 结构）查「该列表层方块类型」时，
> 必须用「铺表层方块时用的那个高度函数」定位 y —— 而非另写一个「自然地形高度」函数。两者对「被重塑过的列」
> 恒不等 → 读到错误 y 的方块 → 表层类型判定恒假 → 特征对该类列**全量静默跳过**（产出 0）或读到错位的方块
> （错放）。这是「worldgen 散布类特征整片消失 / 错位」的典型真因，且静态读「每个判定都合理」全 PASS。**

- **凡 worldgen 分阶段重塑地形高度（海面重塑 / 湖下凹 / 河流 / 沙漠重塑 / 道路…），其「被重塑列」的真实表层方块
  位于「重塑高度」而非「自然高度函数」；后续特征查表层类型必须走重塑高度源，否则在该类列上恒读错格**：本工程
  `heightAt(x,z)` 是纯 fBm 自然地形高度；但海域列（`seaColumnHeight >= 0`）的表层方块由 `generate()` 铺在
  `seaColumnHeight(x,z)`（角点最深→岸线沙滩缓坡 + 噪声重塑，t338/t372），**与 heightAt 完全解耦、恒不等**
  （实测 1854 个海域沙列里 0 个 heightAt==seaColumnHeight）。`placeSugarcane` 旧实现 `surfaceY = heightAt(x,z)` 后
  读 `blockAt(x, surfaceY, z)` 查 Sand → 对所有海域列读错 y（读到自然高度处的土 / 石 / 空气 / 水，从不是 Sand）
  → `surf != Sand` 恒真 → 全图 0 甘蔗（机制等价 MC 的"水边甘蔗"完全消失）。**修法**：海域列用 `seaColumnHeight` 取
  真实沙顶 y，非海域列（内陆草地 / 湖底 / 沼泽）seaColumnHeight<0 直接跳过（无沙顶）。
  - **判别信号**：某 worldgen 散布特征（须特定表层方块：沙顶植物 / 草顶花 / 水面浮叶…）**全图产出 0 或远低于预期**，
    且该表层方块只在「被重塑过的列」出现（海沙 / 湖岸 / 河岸 / 道路…），而特征代码用「自然高度函数」定位表层 y →
    高度嫌疑。**诊断**：遍历该类列，断言 `heightAt == <铺表层的高度>` 的比例 —— 接近 0% 即此坑。
  - **通用形态 / 自检门槛**：审任何「查列表层方块类型再决定是否散布」的 worldgen 代码，问「这个表层方块是由**哪个**
    高度源铺的？（generate / fillWater / placeLakes / 海面重塑…）」→ 查表层 y 必须用**同一**高度源；若该列被多阶段
    重塑（先地形后水后湖），取「最后一次铺该表层方块的高度」。**绝不可**用「自然 fBm 高度」当万能表层 y —— 它只对
    「未被重塑的内陆列」有效。同源一致 = 散布命中；异源 = 整类列静默漏。
  - **元教训（反复修不好的根因排查）**：用户报「特征出现在错位（草上 / 水里）」而当前代码产出 0 时，两者矛盾 →
    说明报告来自**更早的代码态**（重塑逻辑尚未引入、或表层判定尚未收紧），当前真 bug 是「重塑引入后表层 y 失配 →
    0 产出」。**禁止照着旧报告打补丁**（lessons「反复修不好须先复现」）：先 run + 加产出计数诊断确认当前态，再据实修。
- **「特征放置」与「特征生长 / 蔓延 tick」的支撑谓词必须一致 —— 否则放置守得住、生长把守不住的旧 / 玩家误放实例
  放大，表现为「放置收紧了仍到处长」的残留路径**：放置（worldgen / 玩家交互）校验支撑（如「须沙地」），但**生长 /
  蔓延 tick**（每窗 / 每 tick 扩展已存在实例：甘蔗长高、藤蔓蔓延、菌丝扩散…）若**不复验同一支撑谓词**，则：(a) 玩家
  在非合规支撑（草地 / 泥土）上的误放实例、(b) 旧世界存档里规则收紧前的残留实例，会被 tick 持续放大（长高 / 蔓延）
  → 用户观感「规则说只能沙地生、却见草地上长出一片」。这**不是放置的 bug**（放置已守），是**生长 tick 漏了同一谓词**。
  **通用形态**：凡「放置谓词 P」+「生长 tick 扩展该特征」的组合，生长 tick 的每窗判定**必须重算 P**（向下找柱基 /
  根 → 查支撑格是否仍满足 P → 不满足则本窗跳过、不扩展），与放置谓词同源。**自检**：审每个生长 / 蔓延 tick，问
  「它的扩展条件是否包含放置时的全部支撑谓词？」漏掉任一（如只查邻水、不查沙基）→ 即残留路径。**判别信号**：放置
  已收紧（worldgen 不再产非法位）但场景里仍见非法位生长 → 锁定生长 tick 的谓词完整性，而非反复改放置。
  - 证据：t446——`placeSugarcane`（worldgen）误用 `heightAt` 读表层 → 海域列 0 命中 → 全图 0 甘蔗（修复改用
    `seaColumnHeight` 后 81 块 / 37 列，且诊断扫柱基支撑 37/37 全 Sand、0 草 / 0 水 / 0 泥，满足 spec「沙地 + 邻水 +
    不在水里 / 湖底 / 草地」）；`tickSugarcaneGrowth` 旧版只查柱基邻水、不查柱基沙地支撑 → 补 `blockAt(by-1)==Sand`
    门，关闭「玩家误放 / 旧世界残留的草地甘蔗柱长高」残留路径，与 worldgen 放置谓词同源。

- **「幽灵实现」——注释 / 文档里以函数名引用某个机制，但该函数从未定义 / 从未接线（挂着「will be done」语义但
  代码库里查无此物）**：跨多个文件、多任务的注释互相引用一个名字（如「生长由 `tickXxxGrowth` 推进」「玩家采摘后
  回阶段 0 由 `tickXxxGrowth` 重新长」），形成一张**自洽的注释网**，读任一条都觉得「机制已实现、在别处」—— 但
  `grep` 函数定义（`void World::tickXxxGrowth`）零命中、调用点（QML WorldClock tick 桥接列表、C++ 调用）也零命中。
  外部表现：玩家可**放置**该方块（放置逻辑在 Game 层独立实现）、worldgen 会**散布**它、mesher 会**渲染**它，但
  「随时间推进状态」的动态机制完全不发生（种下永远停在初态）。**判别信号**：用户报「X 可放置但不会 Y（生长 / 蔓延 /
  转化）」+ 注释里反复出现某个 `tickY` / `updateZ` 名字 → **立刻 grep 定义 + 调用点**，若任一为零即幽灵实现。**自检门槛**
  （审「某方块 / 实体的动态机制」时必跑）：对注释声称负责该机制的每个函数名，断言「定义存在 ∧ 调用点存在」—— 二者
  缺一即未实现。**元教训**：注释网会自我强化「已实现」错觉（A 引用 B、B 引用 A），静态阅读任一文件都查不出 → 唯有
  「函数名 → 定义 / 调用点」的机械 grep 能戳破。新任务开工 grep 机制名时，看到「只有注释命中、无定义 / 无调用」要警觉。
  - 证据：t514——`tickSweetBerryBushGrowth` 在 `world.cpp:2933`（worldgen 注释）、`blockregistry.h:922`（方块段注释）、
    `recipe.h`（物品段注释）三处以已实现口吻引用，但 `world.h` / `world.cpp` 无定义、`Main.qml` WorldClock tick 桥接
    列表无调用 → 浆果丛种下永不升阶段（玩家种植落地 state=0 停滞、采后回 0 不再长）。补定义（world.cpp 仿 tickCropGrowth
    增量索引模式）+ 接线（Main.qml `theWorld.tickSweetBerryBushGrowth()`）+ `isGrowthBlock` 加 SweetBerryBush 入
    `m_growthCells` 索引后机制方闭环。配套的采摘 / 接触伤害 / 阶段贴图三路是**真已实现**（playercontroller 有实体分支），
    唯生长这一路是幽灵。

- **归一函数里「结构体默认值」与「显式哨兵」不可混同 —— 把默认值当退化态会静默腐蚀新实例（"一次性物品"陷阱）**：
  当一个值类型字段（如 `ItemStack::durability`）的**结构体默认值**是 0，而业务上另用 **-1 作"缺省/自动"哨兵**时，归一函数
  若把 `==0` 单独判成某种退化态（如"只剩 1 次耐久"），则**任何未显式赋值的新实例**（聚合初始化漏字段、迁移 / 旧存档
  round-trip 丢字段、拾取 / 合成兜底未传值）只要带上默认 0 落进归一入口，就被静默降级成退化态 —— 用户观感"新工具 /
  锄头用一次就消失"。t448：`normalizeDurability` 把 `durability==0` 归一为 **1**（注释自圆其说"破损实例不写槽"），但
  `ItemStack` 默认 `durability=0` 正是"新工具未赋值"，于是任何 0 落槽的工具都变 1 耐久 → 首次锄地 / 挖掘触发
  `damageSelectedItem` 的 `<=1` 清槽分支 → 凭空消失。**根因不在耐久数值（59/131/250 都对）也不在每次 -1（对）**，全在
  这条"0→1"的归一脚注。**修法**：`<=0` 一律按"缺省 / 新实例"处理（返 max），与 -1 哨兵同义；真正的"耐久归零"由破损
  分支**清空槽位**（写空栈）保证，永不写 0 耐久实例进槽。
  - **通用形态 / 自检门槛**：审任何带"默认值字段 + 哨兵 + 归一函数"的组合（耐久 / 弹药 / 冷却 / 剩余次数…），问：
    「这个字段的结构体默认值（常是 0 / -1 / INT_MAX）被归一函数判成了什么？是不是退化态？」若默认值被当成"仅剩 1 次 /
    立即过期 / 上限"等业务退化态，而新实例构造路径（聚合初始化 / 迁移 / 兜底）有可能带默认值进归一 → 即本坑。
  - **判别信号**：某类实例（新工具 / 新弹药…）**首次使用即消失 / 立即失效**，但数值表与"每次消耗 1"逻辑都查无 bug →
    锁定归一函数对"默认 / 0 / 未初始化"输入的处理，而非反复改数值表或消耗逻辑。
  - **元教训**："用一次就消失"这类"新实例即坏"的症状，根因几乎总在**初始化 / 归一层的默认值语义**，不在消耗层；
    先核实数值与每次消耗量，二者皆对时，下一步必查"新实例从哪条路径进来、带了什么默认值、归一后变成什么"。

---

## 派生资产（烘焙图标）与源资产脱钩（t454 验证）

> 元原则：**「派生资产由离线工具从源资产烘焙而来」的管线，若源资产被后续任务改了而烘焙工具没重跑，
> 派生资产就静默停留在旧源的状态——没有任何编译/运行错误，只有「图标 vs 实物不一致 / 糊」的肉眼症状，
> 且静态读每条代码都合理。**

- **由源贴图烘焙的 icon_*.png，在 default_*.png 被改色 / 重画后必须重跑烘焙工具，否则图标停留在旧源的色 / 形态**：本工程
  `tools/build_cube_icons.py` 把 `textures/default_*.png`（方块面贴图）烘焙成 `textures/icon_*.png`（hotbar / 创造调色板的等距
  立方体 / flat-2D 图标），`tools/build_atlas.py` 把 default 拼成 `atlas.png`。这些 build_*.py **不进 CMake 构建图**（离线生成，
  PNG 直接 commit 进 qrc），所以改了 default_*.png 后系统**不会**自动重生 icon / atlas —— 必须手动重跑。t404 把
  `default_sand.png` 由偏橙（均值 ~R202 G138 B70）改偏黄（~R231 G218 B155），却没重跑 build_cube_icons → `icon_sand.png` 仍停留在
  偏橙态（均值 R162 G110 B55，R-G=52 读作橙）→ 用户肉眼「hotbar 沙子图标显太橙、放置的方块却黄」。t367 把
  `default_tall_grass.png` 锐化草叶同理漏跑 → icon_tall_grass 也 stale。**整套代码「hotbar.cpp iconFileForBlock 路由正确、
  build_sand.py 色对、qrc 注册对」全 PASS，缺口纯在「烘焙产物没随源重生」**。
  - **判别信号**：某方块「hotbar/调色板**图标**与**放置在世界里的方块**色 / 清晰度对不上」（图标偏色或糊、方块正常），
    而该方块的 icon 由 build_cube_icons.py 从 default_*.png 烘焙 → 高度怀疑 icon 是 stale（烘焙后 default 又被改过、没重跑）。
    **诊断**：算 icon 与 default 的不透明像素均值；色相（R-G、G-B 差）显著不同即 stale（同一烘焙算法对同源只会等比例变暗、
    色相应一致）。修法：重跑 `tools/build_cube_icons.py`（按需也跑 `build_atlas.py`）；正本清源是把「源 → 派生」烘焙做成
    CMake 依赖（default_*.png 为依赖、icon_*.png 为产物），default 改即自动重生 —— 当前离线模式靠纪律，改源即跑全套 build_*.py。
  - **通用形态 / 自检门槛**：凡「派生产物由离线工具从源烘焙」的管线（图标 ← 面贴图；atlas ← 瓦片；qsb ← GLSL；
    压缩音频 ← WAV…），改了**源**后**必须**把所有读该源的烘焙工具重跑一遍，否则派生产物 stale、且无任何错误提示——只有
    肉眼「图标/产物 vs 实物源不对」。审计「改了某 default_*.png / .glsl / .wav」的提交时，问「有哪些产物由它烘焙？都重跑了吗？」
    最易漏的是**间接派生**（atlas 由 default 拼成、icon 又由 default 烘焙 → 改一个 default 触发两条重生链）。
- **cross 广告牌方块（草 / 枯灌木 / 花 / 树苗…）的图标走「直接 4× NEAREST 放大源贴图」路径，源贴图里**每根枝 / 叶的线宽 ×4
  即图标线宽**——源里 2px 横带 → 图标 8px 粗块 = 「糊块」，源里 1px 细线 → 图标 4px 细线 = 清晰**：`render_flat_2d` 把 16×16 透明底
  cross 贴图直接 NEAREST 放大到 64×64 当图标（保 alpha、保像素硬边）。若源贴图为「加粗防缩放太细」把每根枝画成 2px 横带
  （`put(x,y)+put(x+1,y)`），放大后变 8px 粗块；多根枝在底部根区密集重叠 → 图标读作一团棕褐色模糊块而非清晰的枯枝线条。
  t454 枯灌木旧实现即此：用户「图标现糊 / 糊块」。修法：源贴图改用 **1px Bresenham 细线**画放射枝（彼此留白、各自分散），
  仅根簇 + 枝梢种壳做 2~3px 局部加粗（锚点 / 视觉重量）→ 4× 放大后呈 4px 细枝 + 锚点亮点，读作「清晰的枯木枝线条」。
  - **判别信号**：cross 方块图标（透明底 flat-2D）显得「糊 / 块状 / 枝条过粗连成一片」→ 查源贴图的线宽：若用「put+邻居」把
    每根线画成 ≥2px 横带 → 即此（×4 放大后过粗）。修源贴图线宽到 1px（仅锚点 / 尖端局部加粗），勿在图标层改。
  - **通用形态**：凡图标 = 源贴图「直接整数倍 NEAREST 放大」的，**图标的可读线宽 = 源线宽 × 放大倍**。设计源贴图时要按
    「放大后仍清晰」倒推源线宽（4× 放大 → 源 1px = 图标 4px 合适；源 2px = 图标 8px 偏粗）。

---

## 值类型结构体新增字段与部分聚合初始化（t477 验证）

> 元原则：**给一个「大量处用部分聚合初始化」的值类型结构体（如 `ItemStack`）追加字段时，新字段必须带
> 显式默认成员初始化（`= T()`），否则 `-Wextra` 的 `-Wmissing-field-initializers` 会在**全工程所有既有部分初始化处**
> 爆一片警告 —— 即便那些初始化处不是你这次改的文件。**

- **`ItemStack{0,0}` / `{id,count,dur}` 这类「只写前几个字段、靠默认值补齐尾部」的写法，依赖尾字段有默认成员初始化
  （DMI）才不触发 `-Wmissing-field-initializers`**：旧 `ItemStack{id,count,durability,enchants[4]={0,0,0,0}}` 里
  `enchants` 有 DMI，故 `{0,0}`（只写 id/count）不告警 —— durability 与 enchants 都有 DMI 兜底。但**新增一个无 DMI 的
  字段**（如 `QString customName;`）后，`{0,0}` 现在漏掉了**无 DMI 的** customName → -Wextra 在 22 处既有初始化全爆
  （hotbar.cpp 全工程的栈构造点）。**判别信号**：给值类型 struct 加字段后，`-Wall -Wextra -fsyntax-only` 报一片
  `missing initializer for member 'X::newField'` 且行号遍布既有代码（非本次 diff）→ 即新字段缺 DMI。
- **修法**：新字段写 `QString customName = QString();`（显式 DMI），-Wmissing-field-initializers 即对它静默
  （与既有 `enchants[4] = {0,0,0,0}` 同机制 —— 有 DMI 的尾字段不告警）。**不要**去改 22 处既有 `{0,0}` → `{0,0,0,{},QString()}`
  （既 invasive 又每处重复、易错）；DMI 是单点根治。
- **通用形态 / 自检门槛**：凡给「被广泛部分聚合初始化」的值类型（物品栈 / 配置项 / 消息包 / 颜色结构…）追加字段，
  一律给新字段显式 DMI（`= T()` 或合理默认），即便该字段类型默认构造本就空（QString / std::vector / 智能指针）——
  DMI 的作用在此不是「给默认值」（成员本就默认构造），而是**告诉 -Wextra「此字段允许部分初始化时缺省」**。
  CI / build-test 角色按 `-Wall -Wextra` 口径扫全量代码（非本次 diff），故单文件复核 + 全量构建两关都要过。
  - 证据：t477——给 `ItemStack` 加 `QString customName;`（铁砧重命名），缺 DMI → hotbar.cpp 22 处既有
    `{0,0}`/`{id,count,dur}` 全报 `-Wmissing-field-initializers`（默认 cmake 构建**不开** -Wextra 故不可见，须
    `-Wall -Wextra -fsyntax-only` 单文件复核才暴露，同 t271 enum/scalar 三目族）；改 `= QString()` 后清零。

---

## Qt 6.11 Texture API 重命名 + 条带 flipbook 动画（t489 验证）

> 元原则：**Qt 小版本会静默重命名 QML 属性；旧名（vOffset/hOffset）在新版被 `Cannot assign to non-existent
> property` 拒绝 → 编译期不报错、qmlcachegen AOT 通过、运行期组件加载失败 → `root objects after load: 0`。
> 凡「文档记忆里的旧属性名」跨 Qt 小版本前都要实测命中，不能照搬记忆。**

- **QtQuick3D `Texture.vOffset` / `hOffset` 在 Qt 6.11 已被 `positionV` / `positionU` 取代**（伴 `scaleU/V`、
  `rotationUV`、`pivotU/V`、`flipU/V` 等一批 UV 变换属性）：用旧名 `Texture { vOffset: 0.5 }` 会运行期报
  `Cannot assign to non-existent property "vOffset"`、该组件加载失败、整个 QML 文档 `root objects: 0`。旧教程 /
  记忆里的 `vOffset` 在 6.11+ 必须改 `positionV`。**判别信号**：日志 `root objects after load: 0` + `non-existent
  property "vOffset"`（或 hOffset）→ 即此重命名；改 positionV/positionU 即过。本工程 `flipV` 名未变（仍可用），
  但 `vOffset/hOffset` 已废。
- **`positionV` 的方向语义 = 正值上移采样（采更高 V 区）**：经最小 QtQuick3D 测试程序（`Texture{ positionV:X }` +
  `window.grabWindow()` 读像素）实测：positionV=k/N 把面 UV v∈[0,1/N]（帧 0 区）上移到 v∈[k/N,(k+1)/N]（帧 k 区）。
  故 flipbook 条带应「帧 0 放条带**底部**」、positionV 从 0 递增到 (N-1)/N 正向播放（QtQuick3D Texture 把图像顶部
  对应 v=1，故底部 = 帧 0 = v∈[0,1/N]）。**QtQuick3D 默认 V 约定：图像顶 ↔ v=1**（与 OpenGL 底纹素原点相反，由
  Texture 上传时翻转；与既有 grass_side「绿在顶 = v=1」、Canvas flipV:false「开口朝上」两处证据一致）。
  - **方向无法静态/无头验证时， Repeat tiling 兜底**：若方向判反，`tilingModeVertical: Texture.Repeat` 让
    positionV 超出 [0,1] 的采样回绕 → 仍是循环动画（仅反向播放），不会 ClampToEdge 钉死单帧。无头环境做不了
    动画肉眼验证时，Repeat 是「方向错也不破」的保险（牺牲：方向判反 = 反向动画，肉眼需 playtest 确认）。
- **材质级 flipbook（独立条带纹理 + positionV 动画）替代「mesh 烘帧」是流体/连续动画的正确范式**：把 N 帧竖排
  成**独立条带**（不走共享图集 voxelAtlas，才能只动画水/岩浆而不动其它方块）；mesher 一次性烘焙面 UV 到「单帧
  区域」v∈[0,1/N]；帧切换由 QML Timer 推进 `property int frame` → 绑 Texture `positionV: frame / N` → 纯材质参数
  变化。**零 mesh 重建**（F3 `[Xw]` 水重建计数不回升）。对比 t222/t223 旧「每 2s setWaterAnimPhase → buildMesh
  换 UV 帧」（Swamp 261 段/次 mesh 重建风暴）是结构升级：动画时间维度的变化由材质参数承担，空间维度的几何（水面
  高度 / 侧面带）由 mesh 承担，两者解耦。
  - **静水/流水同帧同步**：水条带做「2 列（左=静水 / 右=流水）× N 帧」，mesher 按 cell state 选列（源→左、流→右），
    两列共享同一 positionV → 静水/流水同帧索引同步动画（机制等价 MC 1.0 still/flow 同步 flipbook）。
- **半纹素内缩量 = 半纹素 / **纹理总尺寸**，不是 / 单帧尺寸**：flipbook 条带做帧间防渗色时，V 内缩 `hys` 应是
  `0.5 / 条带总高(px) = 0.5/(N×帧高px)`，**不是** `0.5/帧高px`。后者（如 N=16、帧高 16px 时 = 0.5/16 = 0.03125）
  恰好等于半帧高 → 把帧 0 区 [0,1/N] 内缩成单点 → 面四顶点 v 全相同 → V 维坍缩成单纹素行（采一条横纹而非整帧）。
  写成 `0.5/framePx` 看似合理（「半纹素」），但忽略了「归一化 V 的纹素尺寸取决于纹理总高、非单帧高」——条带的
  纹素密度是按整条带算的。**通用形态**：凡 UV 内缩防渗色，内缩量的分母是该轴的**纹理总尺寸**（像素），与瓦片/
  帧是否子区无关；子区只决定 UV 落点 [k/N, (k+1)/N]，不改变纹素密度。
- **动画贴图抽帧规则（MC 动画贴图是单列竖排 strip）**：MC water_still/water_flow/lava_still/lava_flow 等「.png +
  .png.mcmeta」动画贴图，其 PNG 是**单列竖排 flipbook**：图像宽 = 帧像素边长、高 = 帧数 × 帧边长。抽第 i 帧 = 行
  [i×width, (i+1)×width)（width 既是帧宽也是帧高，因为正方形帧）。包内帧数与本引擎常量（如 kLavaStripFrames=16）
  不一致时：取前 N 帧 + 末尾循环补齐（保条带恒 N 帧 → mesher UV 1/N 子区与 QML positionV 步长不错配）。帧数不一致
  是常态（demo 包 lava_still 20 帧 vs MC 1.0 16 帧），不要假设包帧数 == 引擎常量。
- **动画方向 / V 约定这类「肉眼才能确认」的渲染细节，用最小 QtQuick3D 测试程序 + grabWindow 读像素可无头验证**：
  写一个独立 `main.cpp` + `main.qml`（用 `Model{ source:"#Rectangle" }` 内建网格，**勿用 `PlaneGeometry`——Qt 6.11
  未注册该 QML 类型**，会报 `PlaneGeometry is not a type`；内建网格用 `source:"#Rectangle"/"#Cube"/"#Sphere"`），
  `Texture{ positionV:X }` + `QTimer::singleShot` 后 `win->grabWindow()` 存 PNG → Python 读像素均色判方向。
  编译：`g++ main.cpp -lQt6Core -lQt6Gui -lQt6Qml -lQt6Quick -lQt6Quick3D`（MinGW 工具链 g++ 在 D:/Qt/Tools/，
  Qt 在 D:/Qt/6.11.1/mingw_64/）。这种「最小复现 + 像素断言」是肉眼盲区（动画方向 / 透明排序 / UV 朝向 / 着色）的
  可迁移验证范式——比「编译过即应可见」（已被 lessons 多条判 FAIL）可靠得多。
  - 证据：t489——水/岩浆条带 flipbook。用上述测试程序确认 positionV 方向（正向 = 正值上移采样）+ V 约定（图像顶 =
    v=1）+ Repeat tiling 兜底（方向判反仍循环不破）。运行期实测：稳态 prof `mesh Xreb [Nd 0s **0w**]`（水重建恒 0，
    动画纯材质参数），`root objects after load: 1`。肉眼动画观感（水面/岩浆面流动）需用户 playtest 确认（无头盲区）。
  - **t563 ② 复盘（t489 的「Repeat 兜底」确实兜住了一个真实 bug——动画方向是反的）**：「组装竖排 flipbook 条带时，
    想把帧 0 放到图像底部，用 `np.vstack(帧序) + np.flipud(整图)` 会把**每帧内容**也上下翻转」——flipud 是整图操作，
    不区分「帧序反转」（我要的）和「帧内像素反转」（不要的）。帧内容翻转后，逐帧「下移 1px」的流动编码在屏幕上
    呈「图案上移」→ 用户实测「水往下流动画斑点往上走（方向反，岩浆同病）」。正确写法：`rows.reverse()` 后直接
    vstack（帧 0 排底、帧内容保持原方向）。**判别信号**：材质级 flipbook 的动画方向反了（图案沿错误方向走）→ 查
    条带生成脚本的「帧 0 到底」实现是「反序拼接」（对）还是「整图 flipud」（错——附带翻转帧内容）。
    **通用形态**：任何「重排二维数组行序」的需求（帧序 / 表格行 / 图层序），`flipud`/上下翻转会同时翻转「行内
    内容」；要保内容只换行序必须用「索引反序」（reverse / [::-1]），二者不可互替。验证范式：帧 k 的内容锚点
    （如左上角特征像素）应在条带里「帧 k 行块的第 0 行」（正立），而非「第 15 行」（倒立）——Python 直接断言即可，
    无需肉眼。

- **「增量波前模拟」配「活动盒过滤」时，盒内零写入 ≠ 全场稳态：盒外仍要动的格会被静默饿死——必须给盒过滤
  加「零写入兜底重扫」路径，否则级联中途永久停摆（放方块 poke 才续一格）**：流体 tick（快照 → 蒸发 → 扩散 →
  应用）为省扫描成本常用「活动盒」——只扫最近有写入/编辑的区域（盒随写入逐 tick 外扩，波前跟着盒走）。盒机制
  对「单一连续波前」自洽（每 tick 的写入把盒推到新前沿），但对「多条流场共存 / 玩家在别处编辑」会破：玩家编辑
  把盒设到编辑点邻域，波前在盒外 → 本 tick 盒内格全稳态零写入 → dirty 标志已被 tick 开头清 false → 下 tick 早退
  → 盒外的波前**永久停摆**（用户实测「峡谷水流到一半不流了，放方块刷新后续流一段又停」——每次 poke 只救一格）。
  **修法（3 行）**：tick 末 `else if (盒有效 && !anyChange) { dirty = true; }` —— 保持 dirty → 下 tick 盒已被重置
  （零写入不重建盒）→ 自动退回**全量快照**兜底扫一次：真稳态则零写入停扫（早退优化不受影响）；有盒外流场则
  续扫、写入重建盒（级联恢复自愈）。代价：每次「盒过滤稳态」多 1 次全量扫（一次性）。**判别信号**：「模拟走
  到一半停、玩家编辑一下又动一下」= 活动盒饿死盒外级联，不是模拟算法本身的收敛缺陷。**通用形态**：任何「为省
  全量扫描而只处理活动子集」的增量模拟（流体 / 火 / 光照 / 污染 / 声纳……），「子集内无变化」不能直接等价为
  「全场收敛」——子集外的待处理项需要一条兜底路径（全量重扫或子集并集扩张），否则静默饿死。子集零变化要么
  触发全量确认，要么有机制保证「全场待处理项都在子集内」，二选一。

- **两种透明流体（或透明体）邻接时，双方 mesher 各自画分界面会得到**同一平面的两张透明面** → z-fighting 逐帧
  闪烁；分界面必须单侧持有（或双侧都剔），由「看穿」表达接触**：水段（opacity 0.7）与岩浆段（opacity 0.95）是
  两个独立透明 mesh 段；水格朝岩浆的侧壁、岩浆格朝水的侧壁在**同一格界平面**上各画一张满侧（互相都因
  `isSolid(对方)=false` 且 `≠fluidId` 落进「邻空气」分支）→ 共面透明面 z-fight → 用户「水岩浆混合闪烁」。
  修法：流体段面剔除加「邻接**异种**流体 → 剔本面」（两侧都剔 → 分界面无共面 → 不闪；两体积在界处「看穿」
  相接，交互凝固成实体由模拟层处理，渲染层只管不共面）。**判别信号**：两种半透材质的体积接触处闪烁 → 查双方
  mesher 是否都在分界面画了面。**通用形态**：凡「N 个透明体积段 + 各自 culled meshing」，跨段邻接（水-岩浆、
  玻璃-水、能量膜-任意）的面剔除必须显式处理「邻是另一种透明体积」——不能因为 `!isSolid && !同种` 就当空气
  画满侧（那是画共面透明面 = 闪烁）；要么剔（本工程取此，靠看穿表达）、要么单侧持有（低 opacity 方画）。同种
  邻接（水-水）已有「比液面」逻辑，不受影响——坑只在**异种**。

- **worldgen 的「网格采样 × 概率」散布对『唯一性地标』（要塞 / 末地传送门 / 主 BOSS 巢穴）是数量失控的根源：
  期望座数 = 网格数 × 概率，随世界面积线性涨，与「全图至多一座」的设计意图无关——唯一性地标必须显式
  「收集候选 → 选一（距出生点最近）→ 放置」**：如 40 格网格 × 55% 命中在 160×160 世界 ≈ 9 座要塞（每座含
  末地传送门），用户在出生点附近就撞见好几个传送门（「应至多一个」）。网格 × 概率是**散布类**（矿 / 树 / 湖 /
  地牢）的正确工具——它们本就该「多而均」；但**唯一类**结构的「稀有」不能用概率表达（概率控制的是期望值，
  不是上界）。修法：验证循环里只 `candidates.push_back`（不放置），循环后取距世界中心（= 出生点）最近的一座
  放置（placeAt lambda 收口全部建造代码；确定性 hash 采样不变 → 同 seed 同唯一位置）。**判别信号**：用户报
  「同类结构出现好几个 / 出生点附近好几个」且 worldgen 用网格 × 概率 → 数量失控，不是「运气好」。**通用形态**：
  任何「每世界至多 N 个」的结构生成，数量上界必须由**显式计数**（收集后截断 / 放置后停）保证，不能由概率间接
  「期望」——概率只能调密度，给不了硬上界；且唯一性地标的位置语义常是「距出生点可寻」（取最近候选比取首个
  网格序候选更贴用户预期，首个会偏向角落格）。

---

## 「语义守卫被跨语义复用」+「失败被同坐标副作用掩盖」（t490fix 验证）

> 元原则：**一个带「专用语义守卫」的写入 helper（如 setBlockFromEntity 的 occ 守卫「仅 air/水可被覆盖」）
> 被语义不匹配的路径复用时，守卫会静默拒写；若 caller 又忽略返回值、且后续在同坐标做了「看起来成功」
> 的副作用（如生成实体），失败就被完全掩盖——用户只看到「方块没消 + 实体叠在原位 = 1.5 格高」。**

- **「FallingBlock 着地专用」的 occ 守卫（仅 air/水可被覆盖）不能复用到「清任意实体方块」路径——后者守卫必命中
  被拒写**：`setBlockFromEntity(x,y,z,Air)` 本是「FallingBlock（沙）着地时把 air/水格写成沙」的专用入口，其
  occ 守卫 `if (occ != Air && occ != Water) return false;` 语义是「沙落非空格不覆盖」（正确，防沙覆盖玩家放的方块）。
  但点火 TNT 路径想「清掉原 TNT 方块（实体方块）」也调它 → TNT 是实体方块 → occ 守卫命中 → **静默 return false 不写**
  → 原 TNT 方块没清，而 spawnPrimedTnt 又在同格生成 PrimedTnt 实体 → 1.5 格叠加；爆炸时 detonateTntSphere
  球心格还是原 TNT 方块 → 连锁递归 + 坑越炸越大。**判别信号**：某「移除某方块」路径调 setBlockFromEntity(Air)
  但「方块没消」（mesh 仍在 / 碰撞仍在 / 旁生实体叠在其位），且该方块是实体方块（非 air/水）→ 即 occ 守卫拒写。
  **通用形态**：凡带「专用语义守卫」的写入 helper（着地 occ / 放置需 air / 系统模拟无条件覆盖 …），**绝不为新
  caller 去削弱守卫**（会破坏原语义，如沙覆盖玩家方块）；而是**新写一个独立 helper**（如 `clearBlockSilent`），
  名字 + 头注释**明确钉死「仅供 X 路径用」**，复用原 helper 的全部写后钩子（noteGrowth/noteFluid/recomputeLight/
  clearAllDirty/emit worldChanged）仅去掉守卫。这样两条语义各自独立、守卫各自正确，不互相污染。**自检**：审任何
  `setBlockFromEntity(...,Air)` / `setXxx(条件覆盖)` 调用，问「被清/覆盖的方块语义上是否满足守卫？」不满足 → 守卫
  必拒，写一个绕守卫的独立入口，别改守卫。
  - **失败被「同坐标副作用」掩盖是这类 bug 最阴险处**：caller 忽略 setBlockFromEntity 的 `false` 返回值（无告警），
    而紧随其后的 `spawnPrimedTnt(tx,ty,tz)` 在**同坐标**生成实体（这条路径不查原方块、必成功）→ 肉眼看到一个「实体
    在原 TNT 位置」就以为「清 + 生成」都成功了，唯独「原方块没清」看不见（被实体挡住）直到爆炸才暴露（坑越炸越大）。
    机制等价「主流程成功掩盖了前置清场的失败」。**通用形态**：凡「先清场（fallible）→ 再在同位置做事（必成功）」的
    序列，若清场返回值被忽略，失败必被掩盖——清场 helper 应 `[[nodiscard]]` 或 caller 须检查，否则静默坏。
- **证据**：t490fix——3 处 TNT 点火路径（playercontroller 右键机关四邻 / 右键 TNT 本体 / 压力板四邻）原写
  `setBlockFromEntity(...,Air)` 清原 TNT 方块，但 occ 守卫拒写（TNT 实体方块）→ 原 TNT 方块没清 + spawnPrimedTnt
  同格生成 PrimedTnt → 用户实测「引燃变成实体掉落了，但原来方块没消除，1.5 格高的 TNT」+ 爆炸连锁坑变大。修：新增
  `World::clearBlockSilent`（照搬 setBlockFromEntity 主体仅删 occ 守卫；头注释明确「点火专用，勿用于破坏/沙着地/放置」），
  3 处点火路径全换它；setBlockFromEntity 守卫保留不动（沙着地的 FallingBlock 路径仍依赖它防覆盖玩家方块）。

---

## 「低层需查高层拥有的物品属性」的分层解法（t490fix 验证）

> 元原则：**低层（如 Entities）需要查询某个物品属性（如 maxStack），但属性的权威实现位于高层（如 Game 层
> Hotbar::maxStackSize，含桶/蘑菇汤/护甲特例）—— 低层不能 include 高层。解法二选一：(a) 在最低公共依赖（Core）
> 加一个**纯函数近似查询**，明确标注「仅供 X 语义用、不替代高层权威」；或 (b) 由高层注入回调。两者都接受
> 「近似在高层特例上偏差，但偏差在该语义下无害」的取舍。**

- **掉落物就近合并需查 maxStack，但 Entities 不能 include Game 层 Hotbar → 在 Core 加近似 maxStackSize(int)**：
  ItemEntityManager（Entities）做「同 itemId 掉落物近邻合并」需知 maxStack（工具不可叠=1，材料=64）。但权威的
  `Hotbar::maxStackSize`（Game 层）含桶/蘑菇汤/护甲特例（maxStack=1）+ 工具段 + 材料段判定，Entities 不能 include
  Game（PLAN §2 依赖只向下）。解法：在 Core `BlockRegistry` 加 `maxStackSize(int itemId)` 纯函数（方块段走
  BlockDef.maxStack；[0x100,0x200) 工具段=1；≥0x200 材料=64），明确标注「仅供掉落物合并用、不替代 Hotbar::maxStackSize
  作为背包槽权威」。偏差：桶/蘑菇汤/护甲（材料段内 maxStack=1 的特例）按 64 合并 → 但这些**不会作为 mob/破块掉落物出现**
  （仅玩家 Q 键丢弃），即便按 64 合并也无数据错（拾取时 Hotbar.addStack 按真实 maxStack=1 自然分槽）。**判别信号**：
  某低层需查「物品属性 X」但 X 的权威实现含高层语义 → 别在低层重复实现完整 X（重复 + 漂移），也别破分层去 include
  高层；在 Core 加**近似版**（标注语义边界 + 偏差无害性），或让高层注入回调。**通用形态**：分层铁律下「低层需要高层
  知识」时，接受「近似 + 边界无害」优于「完整 + 破分层」——前提是文档明确近似在哪类输入上偏差、为何在该语义下无害。
  - **证据**：t490fix——ItemEntityManager 掉落物合并查 Core `BlockRegistry::maxStackSize(int)`（新加），不 include
    Hotbar；mob 掉落物（骨头/腐肉/箭/火药/羽毛/线/皮革/墨囊/蛋/肉/铁锭/花/雪球，均材料段 64）合并正确；工具（弓 0x10F）
    maxStack=1 不合并（独立耐久，正确）；桶/汤/护甲按 64（偏差，但掉落物中不存在，无害）。

---

## 「QML NOTIFY 依赖用语句块 `{ rev; return f() }` 形式在静态构建节点上会漏注册」的绑定形式陷阱（t498 验证）

> 元原则：**QML 属性绑定依赖 NOTIFY 属性时，语句块形式 `property T x: { vm.someRevision; return vm.someAt(i) }`
> （NOTIFY 属性作为「裸表达式语句」读取）与表达式形式 `property T x: vm.someRevision >= 0 ? vm.someAt(i) : fallback`
> （NOTIFY 属性**参与值计算**）并不等价：前者在某些上下文（本工程实测：**静态构建的 QQuick3D Node 子树内的 Model 上**）
> 会**静默漏注册**该 NOTIFY 依赖 —— 信号照常发、甚至能被同级 `Connections` 收到，但绑定体不重算，属性恒为初值。后者把
> NOTIFY 属性写进三元/算术/逻辑表达式，依赖被可靠注册。**

- **判别信号**（如何认出这类 bug，而非死磕数值）：
  - 同一个 VM 的 NOTIFY 信号**已经到达**目标对象的父 Node（用 `Connections { function onXxxChanged() {...} }` 能打 log 证明），
    但目标对象上「读该 NOTIFY 属性 + 调 Q_INVOKABLE accessor」的自定义属性**恒为初值、Changed 信号从不触发**。
  - 把绑定从语句块 `{ rev; return f() }` 改成表达式 `rev >= 0 ? f() : fallback` 后，Changed 信号立刻触发、值更新 → 确诊。
  - **强对照**：Repeater delegate（item/mob/经验球/船，全用语句块形式）工作正常 —— 因为它们是**动态创建**（Loader/Repeater 实例化）
    且 NOTIFY 信号高频（`revision` 每帧随实体移动触发）。静态构建 + 低频 NOTIFY 的组合才暴露漏注册。mob 护甲（t377）也用语句块形式
    却「看似工作」纯属侥幸 —— `entityManager.revision` 高频触发顺带重算；一旦把同样形式搬到 player 护甲（`armorRevision` 仅装备时变、
    且 Model 在静态 playerModel 子树内），漏注册立刻致命。
  - **「修了好几次还不好，但每次都只在调数值（scale/凸出量/opacity）」是这类 bug 的典型指纹**：根因在依赖没建立 → 属性恒初值 →
    `visible` 恒 false → Model 根本不进渲染；调再多几何尺寸都没用，因为像素从没被产出过。
- **通用形态与防御**：
  - 任何「`{ vm.revision; return vm.xxxAt(i) }`」写在**静态构建**的 QQuick3D Model/Node 上时，**改写成表达式形式**
    （`vm.revision >= 0 ? vm.xxxAt(i) : fallback`，或在算术/逻辑/三元里读它）。Repeater delegate 内可保留语句块形式（动态 + 高频双保险），
    但新写的静态节点绑定一律用表达式形式以绝后患。
  - **诊断套路**：怀疑某属性绑定不更新时，加 (1) 同级 `Connections.onXxxChanged` log（证明信号到达）+ (2) 一个**简单绑定** `property int probe: vm.revision`
    + `onProbeChanged` log（证明该 NOTIFY 属性本身可被订阅）+ (3) 目标属性的 `onXxxChanged` log。三者交叉：信号到 + probe 变 + 目标不变 = 漏注册。
- **证据**：t498（玩家 F5 装甲修 3-4 次仍不显示，mob 装甲正常）。player 护甲 8 个 Model 的 `property int armId: { hotbarVM.armorRevision; return hotbarVM.armorBlockIdAt(slot) }`
  装备后 armId 恒 0、visible 恒 false；`Connections.onArmorSlotsChanged` 收到信号、同级 `probeArmorRev`（简单绑定）正确变 1→2→3→4，唯独 armId 不重算。
  改 `hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(slot) : 0` 后，8 件全部 `onArmIdChanged→772/773/774/775` + `visible→true`（log 实测）。
  前 3-4 次只调 z scale（0.46→0.56）等凸出量无效 —— 因为护甲 Model 压根没渲染（visible 恒 false），不是被身体遮挡。

---

## 「受击 helper 带 amount<=0 早退守卫」与「0 伤害但仍需受击反馈」的语义错配（t505 验证）

> 元原则：**「扣血 helper」（damageEntity）通常带 `amount <= 0` 早退守卫防误用（负伤害 / 零误调）。
> 当某机制需要「0 伤害但触发完整受击反馈链（红闪 + 击退 + 减速）」时（典型：玩家抛雪球打 mob —— 机制对标
> MC 1.0 玩家雪球 0 HP 伤害但有红闪 + 击退反馈），直接调 `damageEntity(idx, 0)` 会被守卫静默吞掉 →
> 红闪不闪、击退不发，用户报「砸到怪物没反应」。这不是「数值没传对」，是「helper 的守卫语义」与
> 「调用方的意图」错配。**

- **判别信号**：某投射物 / 攻击机制 spec 要「0 伤害但红闪 + 击退」，代码写 `damageEntity(idx, 0)` 或
  `damageEntity(idx, computedAmount)`（computedAmount 在某分支算到 0）→ 用户报「砸中没反应 / 无红闪 / 无击退」，
  但 grep `damageEntity` 调用确实在执行 → 高度怀疑 helper 的 `amount<=0` 早退守卫吞掉了调用。**诊断**：
  读 damageEntity 主体前几行，见 `if (... || amount <= 0) return;` → 即此坑。
- **修法（不要削弱守卫）**：守卫本身是对的（防负伤害误用），不要删它。而是**按伤害值分流**：
  - `amount > 0` → 走 damageEntity（扣血 + 设 hurtFlash 红闪 + 归零死亡链，复用完整受击链）；
  - `amount == 0` → **手动设 `e.hurtFlash = kHurtFlashTime`**（绕过 damageEntity 的扣血，但仍触发红闪）+
    独立调 knockback 击退 + 独立设 slowTimer 减速（这些副作用在 damageEntity 外单独应用）。
  - bump revision 让 QML 红闪绑定刷新（damageEntity 内部本会 bump，0 伤害路径须手动补）。
- **通用形态**：凡「受击 helper 带 `amount<=0` / `target.dead` / `target.invulnerable` 早退守卫」，而某机制需要
  「不扣血但仍触发受击反馈」（红闪 / 击退 / 减速 / 命中音），**不能依赖 helper 副作用**——须在 helper 外独立应用
  那些「非扣血的受击反馈」。自检：审每个「0 伤害 / 无伤但 spec 要反馈」的机制，问「我调的 helper 在 amount=0 时
  会早退吗？早退后红闪 / 击退谁负责？」——若没人负责，手动补。机制对标 MC 1.0 的典型场景：玩家雪球 / 雪球打
  非火焰系 mob / 末影珍珠投掷命中（无伤有反馈）。
- **证据**：t505——玩家抛雪球 spec「砸到怪物不扣血但红色受击动画 + 少量击退」。旧 `damageEntity(mi, kSnowballDamage)`
  对所有雪球统一伤害；改 per-snowball `snowballDamage` 字段（玩家抛=0 / 雪傀儡抛=kSnowballDamage）后，玩家雪球
  `damageEntity(mi, 0)` 被 `amount<=0` 守卫吞掉 → 无红闪无击退。修：snowball 命中分支 `if (e.snowballDamage > 0)
  damageEntity(...); else { tm.hurtFlash = kHurtFlashTime; ++m_revision; }` 后接独立 `knockback(...)` +
  `slowTimer = kSnowSlowDuration`（二者对 0 伤害 / 正伤害都应用，golem 雪球也叠加小幅击退）。

---

## 「同一实体类的不同发射者需不同伤害语义」用 per-entity 字段而非全局常量（t505 验证）

> 元原则：**当同一类投射物实体（雪球 / 箭 / 火球）由不同发射者（玩家 vs mob）发射时需不同伤害（典型：雪傀儡
> 雪球伤敌对 / 玩家雪球 0 伤只击退），伤害值必须是**实体字段**（spawn 入口写入），而非命中分支读全局常量。
> 否则「同一类实体两种语义」无法区分 —— 命中分支只看实体自身，不知道是谁发的。**

- **判别信号**：某投射物实体的命中分支写 `damageEntity(target, kXxxDamage)`（读全局常量），而 spec 要求「玩家发的
  与 mob 发的不同伤害」→ 必然有一方语义错（玩家雪球也扣血，或 mob 雪球也无伤）。
- **修法**：给实体加 per-instance 伤害字段（如 `snowballDamage`），spawn 入口收 damage 参数由 caller 决定
  （mob 发射路径传 `kXxxDamage`、玩家发射路径传 0 或蓄力值）。命中分支读实体字段而非全局常量。
- **对照先例**：本工程箭实体早有此模式 —— `arrowDamage` 字段（骷髅箭 spawnArrow 写 `kArrowDamage=2`、玩家箭
  spawnArrowPlayer 写蓄力 1..6），命中分支读 `e.arrowDamage`。雪球应同构（旧版漏了，t505 补 `snowballDamage`）。
- **证据**：t505——雪球 spawnSnowball 旧签名 `(origin, vel)` 命中分支硬读 `kSnowballDamage`；改 `(origin, vel, damage)`
  + Entity.snowballDamage 字段，fireSnowball（雪傀儡）传 kSnowballDamage、playercontroller 玩家抛传 0。命中分支
  按 `e.snowballDamage > 0` 分流扣血 vs 纯红闪（结合上一条「0 伤害仍需反馈」语义）。

---

## 「带朝向特征面（刻面/正脸）的实体模型，朝向错了会读作『缺件』」（t499 验证）

> 元原则：**一个有「前后面不对称」特征的实体模型（刻面南瓜头 / 正脸眼嘴 / 武器只挂一侧），其「特征面可见性」
> 完全由实体朝向（yaw）决定。模型几何上「件」在（顶点 / 面都齐），但实体背对观察者时，观察者只见对称的背面
> （无刻面 / 无脸），读作「这部件缺失」。** 反复调几何尺寸（放大 / 位移 / z-凸出）修不好这类「缺失」报告，
> 因为根因在朝向，不在几何。判别：用户报「X 部件消失」但 grep 几何代码该部件 Model 确在（顶点 / scale / 位置
> 都合理）+ NoLighting + 进了场景图（Loader onLoaded reparent）→ 高度怀疑朝向问题，去查 yaw 来源。

- **判别信号**：
  - 用户报某部件（刻面 / 脸 / 单侧装饰）「消失 / 看不见」，但代码里该部件 Model 几何完整（顶点 / scale / 位置
    合理）+ NoLighting + 已进场景图（无孤儿）+ 受击红闪 / tint 调制都接了 → 部件本身没问题；
  - 该部件的「特征面」（眼/嘴/刻面）只贴在模型本地 -Z（前面）或 +Z（后面）一侧，**不是全周对称**；
  - 实体朝向（yawRad / eulerRotation.y）由 AI 随机选向（aiWander）且不面向观察者 → 观察者常看到背面 → 特征面
    不可见 → 读作「缺件」。
  - **强对照**：把实体 yaw 强制面向观察者后，特征面立现 → 确诊。
- **修法（朝向优先，几何次之）**：
  - 先审「实体朝向由谁决定」—— 若是 AI wander 随机选向且无「面向观察者」逻辑，加一条「观察者在 N 格内 → yaw
    朝观察者」（atan2(-dx,-dz)，同本工程 yaw 约定 dir=(-sin,-cos) → QML eulerRotation.y=yawDeg 模型 -Z 正对观察者）。
  - **后置于 wander**：若 AI 既有 wander（随机选向）又有「面向观察者」需求，面向观察者的 yawRad 赋值须放在
    aiWander **之后** —— aiWander 在 wanderTimer 到期时会覆盖 yawRad 为随机值；后置保「面向观察者」的最终决定权
    （视觉朝向恒朝观察者）。aiWander 的位移仍用入口 yawRad（上帧设的朝观察者）→ 实体边走边面朝观察者。
  - 几何尺寸（部件放大 / 凸出）是次要优化（让部件更醒目），但**单靠几何修不好「朝向错」的报告** —— 必须先正朝向。
- **通用形态**：凡实体模型有「前后面不对称」特征（生物脸 / 刻面南瓜 / 单侧武器 / 胸口徽章 / 背后背包），且实体
  朝向可被 AI 自主选向（非锁定朝玩家 / 朝目标），都要审「观察者从典型视角（玩家走近）看该实体时，是否能看到
  特征面」。看不到 → 加「面向观察者」AI 逻辑（同 aiHostile 朝玩家 / aiWolf 朝目标的 yawRad 赋值模式）。
- **证据**：t499——雪傀儡模型南瓜头 + 刻面眼/嘴（眼/嘴贴 -Z 前面）。t499 一轮放大南瓜头 + 前推眼/嘴 z 仍报「无头
  无眼」，根因是 aiSnowGolem 不接 playerPos → 只走 aiWander 随机朝向，常背对玩家 → 玩家只见南瓜对称背面（无刻面）
  误判「纯雪块无头无眼」。二轮修：aiSnowGolem 接 playerPos + 玩家在 kSnowGolemFaceRange 内 → yawRad 朝玩家（后置
  于 aiWander 保最终决定权）→ 玩家走近时雪傀儡正脸朝玩家，刻面眼/嘴 + 南瓜头都现。

---

## 「AI 既有 wander 又需面向某目标（玩家 / 敌对 / 配偶）时，面向目标的 yaw 赋值须后置于 aiWander」（t499 验证）

> 元原则：**aiWander（随机选向游荡）在 wanderTimer 到期时会覆写 `e.yawRad` 为随机值。若同一 AI 既有 aiWander
> 又有「面向目标」需求（防御造物面向玩家 / 狼面向配偶 / 守卫面向敌对），「面向目标」的 yawRad 赋值必须放在
> aiWander 调用**之后** —— 否则 aiWander 在 timer 到期的那一帧会覆盖掉面向目标的 yaw，实体偶发背对目标。**

- **判别信号**：实体「应该面向 X（玩家 / 敌对 / 配偶）」但偶发 / 周期性背对 X（每 wanderTimer 周期一次跳变），
  且代码里「面向 X」的 yawRad 赋值写在 aiWander **之前** → 即此坑。
- **修法**：把「面向 X」的 yawRad 赋值移到 aiWander 调用之后（同一 AI 函数末尾，return 之前）。aiWander 的位移
  仍用入口 yawRad（上一帧设的面向 X）→ 实体边按朝 X 方向走边保持视觉朝向 X。面向条件不满足（目标出范围）→
  不覆盖，aiWander 的随机 yaw 生效（自由游荡）。
- **对照先例**：本工程 aiIronGolem 把「朝目标 yawRad」写在 AI 顶部（敌对检测段），但 aiIronGolem **有目标时不调
  aiWander**（直接 return），故无覆盖冲突。aiSnowGolem 既有「朝玩家」又**总调 aiWander**（造物始终游荡）→ 必须后置。
  区分：若 AI 在「有目标」分支直接 return 不走 aiWander，朝向赋值在顶部 OK；若 AI 总走 aiWander，朝向赋值须后置。
- **通用形态**：审每个「既调 aiWander 又在某条件下设 yawRad 朝目标」的 AI 函数，问「aiWander 会在本帧覆盖 yawRad
  吗（wanderTimer 到期）？覆盖后朝目标还成立吗？」若不成立 → 朝目标赋值后置到 aiWander 之后。
- **证据**：t499——aiSnowGolem 一轮把「朝玩家 yawRad」写在 aiWander 之前（紧跟热伤害段），aiWander 在 wanderTimer
  到期时覆盖为随机值 → 雪傀儡周期性背对玩家。二轮改：朝玩家赋值移到 aiWander 之后（函数末尾 return 前），保最终
  决定权 → 视觉朝向恒朝玩家（玩家在范围内时）。

---

## QML 绑定「裸语句触碰依赖」会被 qmlcachegen AOT 死代码消除 → 绑定永不重算（t177 熔炉三轮）

> 元原则：**QML 绑定 body 里用「裸表达式语句」触碰一个依赖（`{ root.depProp; return root.otherProp }`，
> `depProp` 的值被丢弃只用其「读」来注册依赖），在 qmlcachegen AOT 编译下可能被死代码消除——读被优化掉 →
> 依赖不注册 → `depProp` 变后该绑定**永不重算**，读到的永远是首值。**配套的「触碰聚合属性」（如
> `furnaceCoordRev`）自身却正常刷新**（它的绑定 body 把 revision 用在算术里，值被真正使用）→ 判别信号：
> 「聚合属性值对、依赖它的槽绑定值旧」的组合——哪怕 60fps 渲染、帧间绑定 flush 正常，槽绑定也恒旧。**
> 熔炉三轮实测：`furnaceCoordRev`（值用 revision 算术）刷新到 11930，同组件 `inId`（裸语句触碰
> `furnaceCoordRev`）恒 0，而 store 直读 `slotIdAt` 已是 523。

- **判别信号**：
  - QML 组件有「revision / coordRev 触碰聚合属性」（C++ store 的 revision Q_PROPERTY + NOTIFY 直连，值被算术使用 →
    刷新正常）与若干「读槽 / 读态」绑定（仅 `{ root.xxxRev; return store.yyy() }` 裸语句触碰）并存；
  - 用 C++ 对象（Q_INVOKABLE 方法 + revision NOTIFY）作数据源时，槽绑定在数据变更后**恒旧**（首值）；
  - **关键对照**：JS 函数里读 store 直取方法（`store.slotIdAt(...)`）返新值、读绑定（`root.inId`）返旧值，且
    `revision`/`coordRev` 绑定值已刷新 → 即此坑（裸语句触碰被 AOT 优化掉，依赖未注册）。
- **修法（触碰值必须参与返回值）**：把触碰读放进绑定返回路径，且守卫恒真、只负责注册依赖：
  `{ const _r = root.furnaceStore ? root.furnaceStore.revision : -1; return root.furnaceStore && _r >= 0 ? root.furnaceStore.slotIdAt(...) : 0 }`。
  `revision` 恒 ≥0（只 ++）→ `_r >= 0` 守卫恒真，值真正被使用 → AOT 无法消除读 → NOTIFY 直连注册依赖 → 刷新可靠。
  **不要**用裸 `root.furnaceCoordRev;`（AOT 可消除）；**不要**用「赋给未使用局部」`const _c = root.furnaceCoordRev;`
  （未使用仍可能被消除，未验证）。
- **同族风险**：
  - **按时间推进的 read-modify-write 循环（熔炉 tick 等 10Hz 状态机）绝不要从只读绑定快照**——绑定刷新有 AOT /
    帧序不确定性，tick 决策必须**直读 store**（`slotIdAt`/`burnProgressAt` 等同步权威），写回用**脏标记**
    （tick 动了哪字段写哪字段），不用「`local !== binding`」比较（绑定陈旧会误判不写 / 误写回覆盖玩家操作）。
    玩家点击槽与 tick 均为同步 JS、事件串行不可能交错 → tick 快照即当前权威态，推进写回无竞态。
  - **裸语句触碰在「Repeater delegate 绑定」里是否也失效未验证**（hotbar `{ vm.slotRevision; return ... }` 正常，
    FurnaceUI 根属性 `{ root.furnaceCoordRev; return ... }` 失效——差异疑似「读同组件根属性 vs 读外部 C++ 对象属性」，
    但未深究；凡见 `{ xxxRev; return ... }` 且数据变更后不刷新，一律改成「读参与返回值」最稳）。
  - **「返数组的 Q_INVOKABLE 当 Repeater model」同族**（见上一条 pause-menu 经验）：模型绑定必须显式读 revision
    建依赖，model 表达式一处收敛。
- **自测门槛**：改「读 store 的 QML 绑定」后，必须 run「变更数据 → 读绑定看是否刷新」（JS 直读绑定 + 直读 store
  对照），仅 build 绿 / 启动过不算 PASS——AOT 优化掉的依赖编译期不可见、qmlcachegen 不报。
- **证据**：t177 二轮 FurnaceUI.qml——inId/inCount/fuelId/fuelCount/outId/outCount/burnRemain/smeltProgress 八条
  只读绑定全用 `{ root.furnaceCoordRev; return ... }` 裸语句触碰 → revision 变后全不刷新 → 熔炉「放肉看不见
  （东西消失）/ 烤不了 / 不显示」，tick 恒读 0。改「读 revision 参与返回值」+ tick 直读 store 后：放肉即显、
  点燃烧煤、10s 产出熟猪排、取出不被 tick 恢复（不复制）、全流程无崩溃。

---

## QML / QtQuick

- **QtQuick3D 嵌套结构里材质（PrincipledMaterial 等）作用域的 `parent` 解析到**外层 Model**（材质的 3D 父节点，
  QQuick3DObject 的 Q_PROPERTY parent），不是 QML 文档里包住它的那个 Node —— 材质里调 `parent.xxx()` 访问 Node
  上的自定义属性 / 函数必 TypeError（"Property 'xxx' of object QQuick3DModel is not a function"）→ 绑定求值
  undefined → 该材质通道静默退默认值**：绑定依赖注册、NOTIFY 信号全正常（不是 t498 的漏注册族），唯独值恒错。
  这是「受击红闪 / 状态 tint / 昼夜灰阶」类「Node 持 color property + function tinted()，材质 baseColor: parent.tinted(...)」
  模式的必踩坑——代码静态读完全正确（Node 确实有 tinted、Model 确实在 Node 里），只有运行期 log 有 TypeError 行。
  **判别信号**：(1) 材质属性绑定写 `parent.<自定义属性/函数>` 且该成员声明在**包材质的 Node**（非 Model）上；
  (2) 用户报「X 状态视觉从不生效（恒默认色）」但同 delegate 其它绑定（position/visible）全正常；
  (3) 日志 grep `TypeError` 有该行——**任何「3D 材质值恒默认 + 静态审查全对」的报告先 grep 运行 log 的 TypeError**。
  **通用修法**：给持共享状态（tint / 函数）的 Node 显式 `id:`，材质经 id 引用（`id.tinted(...)`）——作用域链内
  显式 id 不受 parent 重解析影响。**通用形态**：QtQuick3D 的 `parent` 是 QQuick3DObject 的 3D 场景父（材质→Model→Node），
  与 QQuick Item 的视觉父一致语义，但**文档嵌套层级 ≠ 3D 父链上的直接父**（材质的直接 3D 父是 Model），凡「材质
  内引用外层容器的自定义成员」一律用显式 id，禁用 parent。
  - 证据：t610——雪/铁傀儡两段 `Node { property color tint; function tinted(hex) ... Model { materials:
    PrincipledMaterial { baseColor: parent.tinted(...) } } }` 运行期恒 TypeError（logs/voxelsandbox.log.preT605 实锤
    5496/5519 两行）→ 红闪 / 蓝调 / 昼夜灰阶全失效（用户「受伤不闪红」）。改 `id: snowGolemRoot` /
    `id: ironGolemRoot` + `snowGolemRoot.tinted(...)` 后四调用点全通。

- **「每帧速度清零」型的碰撞闸门会把实体**焊死**在障碍物上——清除必须只砍朝向障碍的分量**：碰撞检测若在
  位移积分**之前**无条件双轴清零速度（「探到障碍 → v=0」），而检测条件在实体贴障碍时持续为真，则死锁：
  每帧输入 lerp 刚建起反向速度 → 闸门清零 → 位移为零 → footprint 永不离开障碍 → 下一帧再清。速度永远无法
  起步，除非外部瞬移。**判别信号**：用户报「撞到 X 后完全不能动了（应能退回来）」且碰撞代码是「检测命中 →
  全轴清零」而非「检测命中方向 → 清该向分量」。**通用修法**：分向探测（对 ±X/±Z 四向各前探一小步查阻挡），
  只清「速度在朝障碍方向上的分量」（v·n>0 部分）；背向 / 正交分量保留 → 实体能退离障碍，退出检测范围后
  自由。对任何「贴墙持续检测 + 速度清除」的物理闸门（船碰岸 / 实体推挤 / 粘性区域）都适用——**闸门只该挡
  「试图穿入」的速度，不该挡「离开」的速度**。
  - 证据：t611——t584 船碰岸检测「水面同高层 footprint 触岸 → vx=vz=0」在贴岸后每帧清掉倒退速度（清除在积分
    前）→ 船焊死在岸边。改四向前探（kShoreProbe=0.15）+ 只清朝岸分量后可倒退；高速撞毁路径保留。

- **资源包「懒拷贝」贴图（A 与 B 逐像素相同的占位复用）会让「按文件名取瓦片」的映射静默取到错误内容——
  关键特征瓦片（正面 / 刻面 / 状态帧）宜做「退化检测 + 候选链回退」**：部分资源包（demo 包实测
  pumpkin_face_off.png == pumpkin_side.png）直接把相邻贴图复制改名占位，图集合成按文件名覆盖后「正面瓦片 ==
  侧面瓦片」→ 用户报「没有脸 / 没有正面」。文件存在性检查全过（不缺文件），纯像素级退化，静态审查 + 加载
  日志都看不出。**判别信号**：(1) 用户报某面 / 某状态「没有特征」但同名映射确实命中；(2) 对合成后图集做
  像素级 diff——目标瓦片与它的「无特征基版」（side/top）逐像素相同。**通用修法**：在图集合成处对该瓦片做
  退化检测（与基版同格式逐像素比较），命中则按候选链回退（如 face_off → carved_pumpkin → face_on）；
  比较须双转同一 Format（QImage::operator== 不同格式恒 false）。
  - 证据：t610——pack 图集 tile 118 == tile 117（懒包复用文件）→ 南瓜四面全瓜棱无刻脸。合成处检测退化 →
    回退 carved_pumpkin.png（log「是侧贴图拷贝（无刻脸），南瓜前面回退」），tile 118 恢复刻脸。

- **QML `url` 值类型（`Image.source` / `Texture.source` / 任何 `Q_PROPERTY(QUrl)` 属性）在 QML JS 里是 QUrl 对象，
  不是字符串——`.length` 对空 url 和有效 url 都恒 `undefined`**：`source.length > 0` 恒 false（即使 source 已是非空
  file:// URL），`source.length === 0` 恒 false，裸 `source` 布尔（QUrl("") 也是 truthy）也不可用。**正确判法**：
  `source.toString().length > 0`（toString 得字符串；空 url → "" → 0；有效 → >0），或 `status !== Image.Null`
  （Image 专用，status 0=Null/1=Loading/2=Ready/3=Error）。**这是「pack 图标覆盖 / 条件显隐 Image」类绑定恒回退、
  pack 图永不显示、肉眼只见自绘/占位层的隐藏根因**——source 绑定本身正确（返回合法 file:// URL、图片能解码，
  `sourceSize` 有值），唯独 `visible` 判定恒 false；且**与「AOT 死代码消除裸触碰」完全无关**（`source.length` 是
  实打实执行了、值就是 undefined），所以上两轮只修 source 绑定依赖仍不显。**判别信号**：(1) QML 里出现
  `<某url属性>.length > 0 / === 0 / !== 0` 比较 → 直接换成 `.toString().length` 版本；(2) 肉眼现象 = 「功能层
  （pack 图 / 条件图标）永不切换、恒显回退层」，而该 url 单独 log 出字符串是完整正确的；(3) 同源对照 = 同一
  数据源的另一条渲染路径（普通 `Image` 无此判定）正常、走「双层 Image+回退」的路径恒回退 → 判据就在 visible 判定。
  **通用修法**：url 值类型判空一律 `x.toString().length > 0`；`Q_INVOKABLE` 返的 QString（如 `itemIconSource` /
  `iconSourceForBlock` / `packPath`）是真字符串，`.length` 无妨——**先确认属性是 url 还是 string 再选判式**。
  Qt 6.11 运行时实证：空 url `.length`=undefined、非空 `file:///E:/...wooden_pickaxe.png` `.length` 仍 undefined、
  `toString().length`=0/117、`source.length > 0` 的 visible=false / `source.toString().length > 0` 的 visible=true。
  本工程一次性修掉六处同坑：ToolIcon / MaterialIcon 的 `packImg.visible`、SurvivalInventory 空护甲槽 pack 占位图、
  AnvilUI 槽位图标、Main.qml 8 个 mob `Texture.source.length` 的 packTextured/baseColorMap 判定（t421 生物贴图
  覆盖同样从未生效——pack 启用时 mob 恒用程序生成贴图）。
  - 证据：t497——ToolIcon/MaterialIcon 的 `packImg.visible: source.length > 0`：source 绑定正确返 pack 的
    `file:///.../wooden_pickaxe.png`，但 visible 恒 false → Canvas 自绘恒显 → 用户「创造背包工具/护甲图标恒自绘、
    床/梯却显 pack 图」（床/梯是方块走普通 Image、无此判定；工具/护甲走 ToolIcon/MaterialIcon 双层、有此判定）。
    改 `source.toString().length > 0` 后可见性正确（运行时探针 Bvis=true）。

- **给子组件传「与子组件自身同名属性」时，裸名 RHS（`Child { prop: prop }`）会被子组件自身同名属性 shadow 成自引用 → 恒为默认值（null/0），父级注入的值永不生效；必须用显式父路径 `Child { prop: root.prop }`**：当父级想把一个值（PlayerController / VM / 任意对象）注入到**自身也声明了同名 `property`** 的子组件时，写成 `CharacterPreview3D { player: player }`（RHS 裸 `player`）在运行期解析为 CharacterPreview3D **自己**的 `player` 属性（自引用，取默认 null）→ 子组件里的 `root.player` 恒 null、依赖它的绑定/信号全部静默失效（父级自己 `Component.onCompleted` 里读裸 `player` 却能拿到真值——同一名字、父作用域 vs 子对象作用域解析结果不同）。判别信号：(a) 父级在同一文件里读裸名（如面板 onCompleted 打 `player !== null`）为 true，但子组件收到的同名属性（onCompleted 打 `root.player !== null`）恒 false；(b) 子组件逻辑「代码全对」却不工作（null 守卫静默跳过 / 绑定取默认值）。**通用修法**：传属性给子组件一律用**显式父路径** `player: root.player`（父组件 root 先声明同名 `property var player: null`、由更上层 `Parent { root: {...} }`——如 Main.qml `panel { player: player }`——注入），裸名注入在「名字与子组件自身属性撞车」时必自引用。**同名撞车 vs 不同名**：`hotbar: hotbarVM`（父组件属性名 hotbar ≠ 子组件属性名 hotbarVM）无撞车、裸名也可；撞车只在「子组件也恰好声明同名 property」时发生（本工程 PlayerController 注入：FurnaceUI/AnvilUI/WeatherParticles 等面板 + 本组件 CharacterPreview3D 全有 `property var player`）。**自检**：审所有 `Child { xxx: <裸名与 xxx 相同> }` 的注入，改为 `Child { xxx: root.xxx }`。
  - 证据：t551——面板 `CharacterPreview3D { player: player }`：面板 onCompleted 打裸 `player` 非 null（Main.qml id 经 context 可达），但 CharacterPreview3D 内 `root.player !== null` 恒 false（自引用到自身默认 null）→ 角色预览不跟玩家动作（走/蹲/跳动画全失效）。改面板声明 `property var player: null` + Main.qml `panel { player: player }` 注入 + 子组件 `player: root.player` 后 `playerInjected=true`（运行期 log 实证）。Main.qml 侧的 `panel { player: player }`（父组件注入面板）不撞车、裸名可用——因为面板的 `player` property 在面板**文件内部**作用域、不在 Main.qml 作用域，RHS `player` 在 Main.qml 解析为它的 id；撞车只发生在「RHS 求值作用域包含被定义对象自身属性」的子组件内联实例化处。

- **Qt 6.11 的 `Item` 没有 `mapFromScene`/`mapToScene`（运行期 `TypeError: Property 'mapFromScene' is not a function`），把「全局/屏幕坐标 → Item 本地坐标」映射只能用 `mapFromGlobal`（配 `HandlerPoint.globalPosition`）或 `mapFromWindow`（配 `scenePosition`）**：qmlcachegen AOT 对「方法名不存在」不报错（词法/语法层），运行期首次求值才抛 TypeError（QML 引擎吞掉、仅 log 一行、功能静默失效）——与「编译过但运行期坏」同族。判别信号：log 出现 `TypeError: Property 'mapFromScene' of object X is not a function` 且该功能「看不出效果」（如角色不转脸）。**通用修法**：跨坐标系统一用 `Item.mapFromGlobal(handlerPoint.globalPosition)`（屏幕坐标→本地）或 `mapFromWindow(scenePosition)`（窗口坐标→本地），`mapFromItem`/`mapToItem` 用于 Item 间映射。`HoverHandler.point` 是 `HandlerPoint`，其 `position`（相对 target）/`scenePosition`（相对场景）/`globalPosition`（屏幕）都是 bindable 属性（绑定自动随光标刷新，同 Main.qml cursorTracker `x: ...point.position.x` 用法）。
  - 证据：t551——角色预览「看鼠标」初版写 `mapFromScene(mouseScene)` → 运行期 `TypeError: mapFromScene is not a function`（root objects 仍 1、app 不崩、功能静默失效）；改 `mapFromGlobal` + 宿主绑 `cursorTracker.point.globalPosition` 后转身/抬头生效。

- **C++ 方法供 QML 信号处理器 / 命令式代码裸调（`vm.setXxx(...)` 函数调用语法）时必须挂 `Q_INVOKABLE`，且「编译过 + 调用点代码看似接线」不代表调用成功——运行期 TypeError 被引擎静默吞掉，功能静默失效且零报错可见**：与 Q_PROPERTY setter（QML 赋值语法 `vm.xxx = v` 走属性系统、无需 Q_INVOKABLE）不同，QML 里 `vm.setXxx(v)` 是方法调用，方法未挂 Q_INVOKABLE 时运行期抛 `TypeError: Property 'setXxx' of object ... is not a function`——但该调用通常发生在 TapHandler.onTapped 等信号处理器内，异常被 QML 引擎吞掉（不崩 app、qmlcachegen 只做词法校验不查方法存在性），结果就是「数据链路前半段全对、最后一厘米静默断掉」。判别信号：某 setter「到处都在调、从未生效」且无可见报错；典型症状是**搬运数据（耐久 / 附魔 / 名字）在某一跳恒丢**（源头读出正确、目标写入从未发生）。**通用规则**：给 QML 用的每个 C++ 方法（含看起来像 setter 的）都挂 `Q_INVOKABLE`，除非它已经是 Q_PROPERTY 的 WRITE（那走赋值语法）；QVariantList 等非 moc 友好类型不能作 Q_PROPERTY 时，setter 就**必须**走 Q_INVOKABLE 函数调用路径——此时漏挂 = 必静默失效。**自检**：审所有「QML 内 `xxx.yyy(` 形式调用的 C++ 方法」，逐个核对其头文件声明是否挂 Q_INVOKABLE；读写不对称（读侧挂了写侧漏挂）是快速判别特征。
  - 证据：t622/t623——`Hotbar::setHeldEnchants` 自 t475 起未挂 Q_INVOKABLE：九个背包面板的槽点击处理器全部正确读出槽内附魔、正确调 `hotbar.setHeldEnchants(r.heldEnch)`，但该调用运行期 TypeError 被吞 → 光标手持物附魔恒保持 `setHeldBlock` 清空后的空值 → 「附魔台产物左键取出瞬间变普通附魔书 / 附魔工具取出丢附魔」的直接根因。同批连带核对：读侧 `heldEnchants()` 挂了 Q_INVOKABLE（读一直正常）而写侧漏挂——读写不对称正是漏挂特征。





