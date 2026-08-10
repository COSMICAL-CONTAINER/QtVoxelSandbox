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

