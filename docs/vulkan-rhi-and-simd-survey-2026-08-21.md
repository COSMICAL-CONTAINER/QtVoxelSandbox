# Vulkan 渲染路线与 SIMD 优化调研报告

> **状态注记（2026-09-18 主控）**：路线 C1（worker meshing）已全量兑现——流式半边 = D6 MeshWorker + W4 接线（r2020/r2026），fixed 半边 = t1060（r2034，黎明 sun 重烘风暴后台摊平 + QTVOXEL_SYNC_BAKE=1 回退）；C 包「存活资产 = worker 管线」句自此退役。C2/C7/SIMD/C4 维持「按 P5 实测数据再定」；本报告其余技术事实（Qt 6.11.1 事实清单/瓶颈结论）继续有效。


> 日期：2026-08-21（v1.1，已并入两路独立审阅修正：事实核对 + 对抗性技术审）。**只读调研**：本报告不改动任何代码与既有文档；全部结论来自代码走读（`src/`、`main.cpp`、CMakeLists）、历史文档（PLAN/lessons-learned/usage-report/dev-spec 等，未读 dev-plan.md）、运行日志（`logs/voxelsandbox.log`）、本机 Qt 6.11.1 mingw_64 kit 头文件核对与 Qt 官方文档核实。所有代码断言带 file:line，可直接复核。
>
> 背景问题（用户提问）：① 按 PLAN.md 最初的设计走 Vulkan，需要改哪些地方？对比当前 Qt Quick 前端渲染有哪些优化空间？② 同事建议试 SIMD，怎么优化比较好？

---

## 0. TL;DR

1. **「走 Vulkan」有两层完全不同的含义**，必须分开决策：
   - **路线 A（仅切后端）**：Quick3D 场景原样，`main.cpp` 加一行 `QQuickWindow::setGraphicsApi(VulkanRhi)`（在构造第一个 QQuickWindow 之前）即可跑 Vulkan。**对帧率收益 ≈ 0**——日志实证瓶颈在主线程 CPU（9FPS 时 `main*131ms / render 5.0ms`，usage-report.md:504），不在 GPU/后端。价值在于为未来最低配验收（Vulkan 1.3）铺路的自测，与提前暴露驱动差异。工作量 1–2 天（含回归）。
   - **路线 B（回 PLAN 原始设计）**：自研 `QQuickRhiItem` 体素渲染层（Vulkan 主、D3D11/GL 兜底），QML 退回纯 UI。这是**数月级**迁移，但一次性解决当前 Quick3D 路线的全部结构性限制（见 §2.3 对照表）。
2. **当前渲染管线最大的问题不是后端，而是**：meshing 全部在 GUI 线程同步跑（全仓库无任何 QtConcurrent/QThread）、每 chunk 6 个段实例共 7 次全量三重扫描（逐格分类路由重复 6–7 次）、光照/软影烘死在顶点色里导致 sun 步进全量重烘、600 Model 的 delegate 高水位问题。这些**不换 Vulkan 也必须修**，且修完之后构成路线 B 的生产端（PLAN 不变量 B 的 worker mesher + 不可变 ChunkMeshData）。
3. **SIMD 现状**：MinGW GCC 13.1、`-O3`、仅 SSE2 基线、自有代码零 intrinsics（vendored `src/Audio/miniaudio.h` 除外）、无 `-march`。最值得做的 Top5：① mesher 可见面顶点批填充 + PCF 软影批量采样；② `refloodBox` 快照/清零/比对三段流式化；③ 「chunk+halo 扁平快照」底座（消除逐格路由）；④ Perlin 噪声批量化（worldgen 启动卡顿）；⑤ nibble 光照批量访问器。DDA 射线、BFS 传播、物理 footprint **不要** SIMD（串行依赖/体量太小）。
4. **推荐路线**：先做「不依赖 Vulkan 的优化包」（后台 meshing + 单遍多段 + SIMD，**4–10 周量级**，立竿见影；显式排除 C4 自定义材质，避免与 B1 重复投资）→ 顺手做 A 切后端回归（1–2 天）→ 中期按阶段做 B（B0 技术验证 spike 先行）。复用关系要诚实：第 1 步里**存活的是结构性资产**（worker 生产管线、halo 快照、SoA 面列表、golden 框架、SIMD 分派基建），Quick3D 特定的上传层与顶点格式层在 B 时会报废重写。

---

## 1. 现状基线（证据清单）

### 1.1 当前渲染栈全貌

| 层面 | 现状 | 证据 |
|---|---|---|
| RHI 后端 | **D3D11**（Qt Windows 默认），从未调 `setGraphicsApi` | main.cpp:84-108（只 `QSG_INFO=1`）；logs/voxelsandbox.log:138 `Creating QRhi with backend D3D11` |
| 渲染循环 | threaded render loop + vsync | 日志开头 `qt.scenegraph.general: threaded render loop` |
| GPU | NVIDIA RTX 3060，FLIP swapchain（ALLOW_TEARING，latency 2），MSAA=1，2D 图集 2048×1024，66 shader 管线缓存 | 同日志适配器/swapchain 段 |
| 3D 场景 | 单 View3D（默认 Inline 模式）+ SceneEnvironment(Color 背景) + 固定 DirectionalLight（无阴影，只影响 lit 元素） | Main.qml:2629-2688、:3459-3463 |
| 体素世界 | 每 chunk **6 个 Model/ChunkGeometry 段**（地形/水/岩浆/玻璃/冰/cutout），满配 100 chunk = 600 Model | Main.qml:281、:3822-3849；chunkgeometry.h:28（`QQuick3DGeometry` 子类） |
| 顶点格式 | stride 48：pos3 + normal3 + uv2 + color4；**光照/软影全部烘进 color4 顶点色**，无独立 light/AO 属性 | chunkgeometry.cpp:1069-1090 |
| 材质 | 全部 PrincipledMaterial + `lighting: NoLighting` + 单张 2D 图集（`generateMipmaps:false`，半纹素内缩）；**零自定义 shader、零后处理**。注意 chunkgeometry.h:67-70 自记录的 D3D11 专属契约：alphaCutoff 仅在 opacity<1 时生效 | Main.qml:3606、:3888；全仓 grep `CustomMaterial\|ShaderEffect\|Effect` 零命中 |
| 光照 | flood-fill 天光/方光（nibble 打包）+ heightmap 2×2 PCF 软影（每顶点 8 次路由查询），昼夜 dayMul 量化门控 → **周期性全量重烘顶点色** | voxellight.h:30-95；chunkgeometry.h:42-56 |
| meshing 线程 | **全部 GUI 线程同步**（无 QtConcurrent/QThreadPool/QThread/std::thread），F3 直言 `threads: 0/0 (sync meshing)` | chunkgeometry.cpp:284-308；Main.qml:375 |
| mesher 结构 | 每段实例独立跑 buildMesh：PASS1 异形/cross（~50 case switch）+ PASS2 culled/greedy + 流体段 + greedy 段（**默认关**，t183 贴图拉伸不可接受）。每 chunk 实际 7 次全量三重循环（terrain 跑 PASS1+PASS2 两次、cutout 仅 PASS1、water/lava 流体路径、glass/ice culled）——逐格分类路由重复 6–7 次，光照采样按段分区（通常各段只算自己拥有的面） | chunkgeometry.cpp:442-1116（:512 PASS1 守卫、:644 cutout 跳 PASS2）；chunkgeometry.h:250（`m_greedyMeshing = false`） |
| 实体/粒子 | mob/掉落物/经验球等 = QML Repeater/createObject delegate（mob delegate 区间 48 个 Model 模板，全文档 255 处 `Model {`）；粒子混用 ParticleSystem3D 与 Model+Timer 池 | Main.qml:5857-6800 等；BlockParticles.qml（池化） |
| 帧分解探针 | main_total / render_cpu / qmlSync / residual + 9 个逐帧桶 + mesh/world/mob 子桶 | main.cpp:128-177；frameprofiler.cpp |

### 1.2 实测性能数字（RTX 3060 / D3D11 / 1080p）

| 场景 | 数字 | 出处 |
|---|---|---|
| 稳态 60fps | `main*16.6 render 1.4 qmlSync 0.0`（vsync 空闲段） | logs/voxelsandbox.log:1994 |
| 9FPS 深挖期 | `main*131 render 5.0` —— **铁证主线程 bound 非 GPU（排除"转 Vulkan 提帧率"）** | usage-report.md:504 |
| 开局 → 夜战 → 白天 | 100 FPS → 14 FPS（mesh 254 reb + mob phys 14ms）→ 9 FPS（mesh 116ms 441 reb） | usage-report.md:650 |
| Swamp 夜 r=8 | 14 FPS，`mesh 159.66ms (622reb [36d 325s 261w])` | usage-report.md:666 |
| 世界生成批重建 | 同窗 `mesh 426.88ms (1818reb [1420d 298s 100w])`（一次性，窗口累计） | logs/voxelsandbox.log:1993 |
| 单 chunk 重建 | dirty 路径 ≈ 995–1083 µs；sun 重烘路径 ≈ 1822–1846 µs | 日志 vo.render 行（:164-165、:1267-1268、:1407-1408） |
| 视距剔除 | 600 Model → 154 可见段（t470）；t472 根治"段上 sun/water 无视距重建"风暴 | usage-report.md:457-459；chunkgeometry.h:113-126 |
| 5FPS 回归根因 | 场景图节点 8000→~500（108 Model/槽 × 64 槽，95% invisible-but-synced）→ Loader 化 + qmlSync 桶 | usage-report.md:628-644 |

> ⚠️ 勘误备注：lessons-learned.md:119「main.cpp 已确认 Vulkan 生效」为**陈旧条目**，与当前日志 D3D11 事实矛盾（`git log -S "setGraphicsApi"` 无任何提交）。本报告以日志为准。

### 1.3 现状 vs PLAN.md 原始设计的差距

| PLAN.md 目标（原设计） | 现状 | 差距性质 |
|---|---|---|
| §1：QML 只做 UI，体素渲染走**自研 RHI 渲染层**（QQuickRhiItem + Vulkan 主 + D3D11/GL 兜底） | 渲染整体跑在 Quick3D（QQuick3DGeometry + Model），后端 D3D11（默认值，非主动选择） | 架构级偏差（Phase 0 起的实现捷径，lessons-learned.md:39 已自认"纹理数组 = 自研 RHI 路线"） |
| §2-A RHI 囚笼（QRhi 只许在 src/Renderer/） | 全仓无 QRhi 实码（仅 playercontroller.h:137、Main.qml:10762 两处注释），CI include-guard 从未激活 | 现状合规，B 启动日激活 |
| §2-B worker mesher + MPSC + 不可变 ChunkMeshData + 渲染线程建 buffer | meshing GUI 线程同步 | **核心缺失**，也是当前帧尖刺主因 |
| §2-C per-chunk shared_mutex 世界锁模型 | World 无任何锁（全仓零线程） | C1 线程化的前置缺口 |
| §2-I 顶点格式 `{pos, normal, uv, tileIndex, ao[4]}`（纹理数组 + greedy-ready） | `{pos, normal, uv, colorRGBA}`，图集 CLAMP + 半纹素内缩，greedy 因贴图拉伸默认禁用 | 顶点格式需迁移 |
| §1 纹理数组快路径 + 图集兜底（运行期 feature-gated probe） | 单图集（无 mipmap），无数组路径 | B 需新建 |
| §1 qsb 离线烘 shader（qt_add_shader） | 零自定义 shader | B 需新建 |
| §4 验收：最低配（Vulkan 1.3）1080p≥30fps / 推荐配 ≥60fps | RTX 3060 单机实测，无常驻 60fps 保证 | 验收体系未建 |

---

## 2. 「走 Vulkan」需要改什么：三条路线

### 2.1 路线 A —— Quick3D 原样，切 Vulkan 后端（1–2 天，含回归）

**改哪里**（改动极小，因为 Quick3D 内置材质全部经 RHI 抽象、Vulkan 是官方一等后端，且本项目零自定义 shader）：

1. `main.cpp`：加 `QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);`。官方约束是**在构造第一个 QQuickWindow 之前**调用（惯例放 main() 开头、`QGuiApplication` 构造前最保险）。**注意：一旦代码写死 setGraphicsApi，`QSG_RHI_BACKEND` 环境变量即失效**——建议从 Config/settings 读开关决定是否调用，保留 env 与 `QSG_RENDER_LOOP=basic` 作诊断/回退杠杆。
2. 验证顺序：先**不改码**跑一次 `QSG_RHI_BACKEND=vulkan` 启动，日志确认 `Creating QRhi with backend Vulkan`（现有 `QSG_INFO=1`，main.cpp:84，会打印），再落代码。
3. 部署注意：Windows 上 vulkan-1.dll 由 GPU 驱动安装（RTX 3060 机器无虞）；`windeployqt` 不会部署 Vulkan loader——发布到纯净机时需确认目标机驱动带 loader（NVIDIA/AMD/Intel 近十年驱动均带）。

**回归风险清单**（重点回归项，均来自本项目自家记录 + Qt 已知问题）：

- **alphaCutoff 的 D3D11 专属契约**（chunkgeometry.h:67-70「PrincipledMaterial alphaCutoff 仅在 opacity<1 时生效」是本 D3D11 后端实测行为）——cutout/crack/torch 系材质的透明契约是**最高优先回归项**。
- 「lit 材质/PointLight 在 D3D11 不出像素」族（Main.qml:3923/3956/4039/7929）——D3D11 上的记录，Vulkan 上行为可能不同；当前全 NoLighting 不受影响，但 CharacterPreview3D 等 lit 元素要看一眼。
- Particles3D 依赖增强特性，历史上 Loader 隔离降级——Vulkan 下重新验证。
- 透明排序（冰/水 t495 教训）、flipbook 材质动画、管线缓存（qqpc_d3d11 → qqpc_vulkan，**首启全量 shader 编译尖峰**是首启体验回归项）。
- present mode 差异：日志实证 D3D11 走 FLIP+ALLOW_TEARING（latency 2），Vulkan 是 FIFO 路径——帧节奏观感可能变化。
- 混合 GPU 笔记本：QRhi 默认取 adapter 0，Vulkan 拿不到 D3D 按窗口关联的 GPU 选择，发布目标含双显卡机器需验证。
- Qt Quick on Vulkan 在 Windows 是活跃 bug 源（官方博客自述 threaded render loop 历经 lockup/crash 打磨；近期实例：QTBUG-135635，Qt 6.9 AMD+Vulkan 黑屏用户回退 6.8.3）。RTX 3060/NVIDIA 不直接命中，但 6.11 非 LTS、补丁窗口短，回归要当真。

**预期收益（诚实）**：帧率 ≈ 0 提升（§1.2 实证主线程 bound）。真实价值：① 为 PLAN §4 最低配「Vulkan 1.3」验收提前铺路的**自测项**（RTX 3060 远高于最低配档位，不构成验收本身）；② 若未来走 B，提前在本机验证 Vulkan 驱动栈；③ Win/Linux **桌面**主路径的一致性铺垫（注意：PLAN Phase 2 安卓是 **GLES 主** + Vulkan 可选，不要把安卓算进"单一后端"）。**建议定位为"回归测试项"而非"性能优化项"。**

### 2.2 路线 B —— 回 PLAN 原始设计：自研 QQuickRhiItem 体素渲染层

这是「最初的设计」（PLAN §1/§2/§3 骨架：VoxelView(QQuickRhiItem) + VoxelRenderer + ChunkMesher + ChunkMeshCache + TextureArray + qsb shader）。迁移清单按模块列：

**B0. 技术验证 spike（先做，2–4 周量级，去风险）**
- 新建 `src/Renderer/voxelview.h/.cpp`（`QQuickRhiItem` 子类，QML_NAMED_ELEMENT）+ `voxelrenderer.h/.cpp`（`QQuickRhiItemRenderer` 实现，虚函数是 `initialize()/synchronize()/render()` 三个——本机 6.11.1 头文件 qquickrhiitem.h:28-30 已核对，无 prepare()）。
- **依赖前提（第一小时就会撞上）**：`qrhi.h/qshader.h` 在私有 include 路径（include/QtGui/6.11.1/...），公共头只前向声明 → CMake 需 `find_package(Qt6 ... GuiPrivate)`。这正是不变量 A「RHI 囚禁 + 对下一小版本编译 CI」的物质原因，B0 日同步激活 include-guard（dev-spec.md:447 已预留口径）。
- Vulkan 后端下渲染**单 chunk 静态网格**：手写最小 `qt_add_shader` 烘 `.qsb` 管线（顶点拉 pos/uv，片元采样数组纹理）。
- 验证点：`initialize()` 里 `rhi->isFeatureSupported(QRhi::TextureArrays)` probe（PLAN §1 红线：图集兜底路径第一天就要有）；生命周期与窗口缩放 RHI 重建不崩。

**B1. 地形管线（核心阶段）**
- **mesher 改造**（复用现有逻辑，改输出契约）：`chunkgeometry.cpp` 的 buildMesh 主体拆成纯函数 `ChunkMesher::build(...)` → 产出**不可变 `ChunkMeshData`**（顶点+索引，owning，move-only——PLAN 不变量 B 的原话）。顶点格式换不变量 I：`{vec3 pos, vec3 normal, vec2 uv, uint tileIndex, uint8 ao[4]}`（+ sky/block 光照字段或烘色，见下）。
- **线程模型**：QThreadPool mesh worker（脏 chunk 队列 + 每帧预算）→ MPSC/互斥交换 → `synchronize()` 抽干 → `render()` 建/重建 `QRhiBuffer`。渲染线程绝不读未完成上传的 cache。**这一步同时消灭当前最大的帧尖刺来源**（§1.2 的 mesh 427ms 窗口、14/9 FPS 场景）。
- **shader 侧光照解放**：天光/方光/软影随顶点走，但 **dayMul、sunDir、水波 phase 全部改 uniform** → 昼夜过渡、太阳步进**不再触发任何重网格化**（现状：chunkgeometry.h:42-56 的量化门 + catch-up 重建整段消失）。
- **纹理数组 + per-tile mipmap**：`QRhiTexture` 数组（layer = tileIndex），UV 在 [0,1] 内 per-layer REPEAT → **greedy meshing 与逐格清晰贴图同时成立**（t183 死结的直接解，chunkgeometry.h:103-111 注释原话"真正的逐格清晰+顶点预算需纹理数组 = 自研 RHI 路线"）。图集兜底路径保留（运行期 feature-gated）。
- **draw call 与统计真值**：自编码 pass → 每 chunk 一组 draw 或合并多 chunk 大 buffer + 视锥剔除在 C++ 侧；draw-call 计数是真值（自己编码自己数）。**GPU 计时要诚实**：scenegraph 自建的 QRhi 是 `Timestamps: 0`（日志实证），程序化 GPU 时间需 `QQuickRenderControl` 自建带 `QRhi::Timestamps` 的 QRhi（配 `QRhiCommandBuffer::lastCompletedGpuTime()`，半私有 API）才成立；B1 的现实预期是 **CPU 侧 pass 编码真值 + RenderDoc/Nsight 采样**，比现状"连 draw-call 都只能估算"前进一大截。
- QML 侧：Main.qml 的 6 段 Model/Component.createObject 区（:3811-4060）整体替换为一个 `VoxelView {}`；ChunkGeometry 类退役或转为 worker 侧纯数据生产者。

**B2/B3. 实体与剩余元素收编**
- 关键约束：**QQuickRhiItem 渲染到独立离屏纹理再合成进 scenegraph（官方文档口径），与 View3D 同窗口可共存但不共享深度缓冲** → 地形在自研层、生物留在 Quick3D 的"混居"会有穿插错误（洞里生物画在山前）。因此世界内容必须**整体**迁移：B2 实体/掉落物、B3 手持/天空穹/云/粒子/选框线。
- **实体 instancing 的能力边界要认清**：`QQuick3DInstancing` 每实例只有一个变换，适合掉落物/经验球等刚体；mob 是 48 个带逐部件动画的 Model 模板，instanced 化意味着动画方案重做（烘帧或放弃逐部件动画）——B2 的工作量主要在这里，不是"顺带"。
- CharacterPreview3D（第二 View3D，纯 UI 预览）**可以永远留在 Quick3D**——PLAN 本来就允许 QML/Quick3D 做 UI。
- 期间用 feature flag（默认 Quick3D 路径）保可玩性，B3 完成后切默认。

**B 阶段工作量（诚实估计，业余时间口径）**：B0 spike 2–4 周；B1 是大头（mesher 契约重构 + 线程握手 + shader + 纹理数组 + 透明排序迁移），参照 PLAN 对 Phase 1.0 的 triple-it 估计（9–15/15–27 hobbyist-month 含从零整个引擎），纯渲染层迁移估 **3–6 个月业余时间**；B2/B3 再 2–4 个月（含 mob 动画方案重做）。⚠️ 这不是全New 开支——B1 的 worker 管线/SIMD/golden 框架在第 1 步（§2.4）已经建好（见 §4 复用清单）。

### 2.3 路线 B 解决的当前结构性限制（对照表）

| 当前 Quick3D 痛点（证据） | 路线 B 的解 |
|---|---|
| 每 chunk 6 Model = 最多 6 draw，600 Model 满配（Main.qml:281） | 合批/合并大 buffer + C++ 视锥剔除，draw 数自主可控 |
| 光照烘死顶点色 → sun 步进/昼夜全量重烘（chunkgeometry.h:42-56） | dayMul/sunDir → shader uniform，零重网格化 |
| greedy 与图集互斥（t183，贴图拉伸） | 纹理数组 per-layer REPEAT，两者兼得 |
| **GPU 计时拿不到**（lessons-learned.md:43；draw-call 估算见下） | 自编码 pass + RenderDoc/Nsight；程序化 GPU 时间需 RenderControl 自建 QRhi（§2.2-B1 诚实口径） |
| meshing 卡 GUI 线程（`sync meshing`，F3） | 不变量 B：worker + MPSC + 渲染线程建 buffer |
| delegate 高水位永驻/节点爆炸（lessons-learned.md:138-146） | 掉落物/刚体改 C++ 侧 instanced 管理；mob 需动画方案重做（见 B2） |
| lit 材质/PointLight 在 D3D11 不出像素（Main.qml:7929） | 自研 shader，光照自由（雾/水波/真软影） |
| 透明排序失控（冰-水闪烁 t495） | 自控 pass 顺序与深度技巧 |
| mipmap 不可用（图集渗色） | 数组纹理 per-layer mipmap |

> 附：draw-call **计数**真值其实现在就能拿——`View3D.renderStats`（RenderStats QML 类型）暴露 `drawCallCount/drawVertexCount/renderPassCount/renderTime`，lessons-learned.md:43 的抱怨已过时一半（GPU 计时那半边仍只有 B 能解）。见 §2.4-C7。

### 2.4 路线 C —— 留在 Quick3D 的优化空间（不换后端，立竿见影）

按收益/成本排序；C1/C2/C3 与 SIMD 是**无论走不走 B 都要做**的（见 §4 路线图）。**定位声明：路线 C 是对"Quick3D 画体素世界"这一既成架构偏差（PLAN 锁定决策 3）的有意止损与过渡投资，不是对偏差的背书。**

- **C1. meshing 移出 GUI 线程**：脏 chunk 集合经 `QtConcurrent`/QThreadPool 在 worker 里把顶点/索引算进不可变 `QByteArray`，GUI 线程只做 `setVertexData/setIndexData + update()` 上传（QQuick3DGeometry 的真实 API 名；Qt 文档对其线程规则**无明文**，「数据可任意线程生产、实例操作归 GUI 线程」是 QObject 亲和规则的推断，需 spike 验证）。**两个硬前置**：(a) World 目前零锁——worker 读 world 与 GUI 写并发就是数据竞争，要么先落实 PLAN 不变量 C（per-chunk shared_mutex），要么把 C3 的 halo 快照（快照本身在 GUI 线程做）前置为 C1 的一部分；(b) 现状重建同步于 setBlock（onWorldChanged 同步槽），异步化后 t155「破块贴图 <1 帧消失」的验收语义要重新定义（编辑延迟一帧还是立即），sun 量化 catch-up、chunk 卸载与 worker 生命期都要处理。风险中低，不是"白捡"。
- **C2. 单遍多段同产**：现状每 chunk 7 次全量三重循环（§1.1 表），逐格**分类路由**（blockAtWorld+分支过滤）重复 6–7 次；光照采样虽按段分区，但扫描/分类/邻居探针的分摊收益仍在。改成一次扫描同时产出各段 buffer（或一次收集可见面清单再分桶）。注意收益上限是"扫描与分类"那部分，不是 6× 全量。
- **C3. 「chunk+halo 扁平快照」底座**（服务 C1/C2 与 SIMD①）：现状所有逐格访问走「ChunkManager 路由（除法+边界+查表）→ 越界分支（6 比较）→ 乘加索引 → nibble 移位」。buildMesh 入口一次拷 18×H×18 的 voxels/states/light 进连续局部数组，之后剔除判断用 `isSolid` 256 项 LUT 做 16-wide 比较。数据布局本身理想（3 个 32KB 连续字节流平行数组，Y-major）。即便不上向量指令，仅消路由就明显提速。
- **C4.（显式排除出第 1 步）**首个自定义材质（CustomMaterial/qsb）把 dayMul 合成挪进 shader，可立刻消灭 sun 步进重烘——但 B1 的 uniform 化会让这笔投资报废，且引入首批自定义 shader 风险。**只在最终决定不做 B 时才值得做。**
- **C5.（低收益/有坑，不建议）图集 mipmap**：单图集开 mipmap 会跨瓦片渗色（需 gutter/padding）；真正的解在 B 的数组纹理。
- **C6. 非 SIMD 的分配修复**（廉价、独立）：`collisionAABBsAt` 按值返回 `std::vector<BlockAABB>` = 每格一次堆分配（world.h:135；玩家 3 轴 × ~12 格/tick + 60 mob 各自调），改 out-param/small_buffer。resourcepackmanager 的像素循环已用 scanLine（applyTint :1098-1110、cropTopBlank :1489-1505），仅 :1875 有单点 `pixel()` 调用，不值得动。F3 顶点求和只覆盖 9 chunk 的显示偏差（t178-correctness.md:87）顺手修。
- **C7.（当天可做的快赢）F3 换真值 + Quick3D 原生 instancing**：① `~drawEst`（Main.qml:345）直接换 `view3d.renderStats.drawCallCount` 等真值；② 掉落物/经验球等刚体 delegate **今天就能**用 `Model.instancing` + `QQuick3DInstancing`（C++ 子类喂实例表）压成 1 个 Model/1 draw——mob 不适用（逐部件动画）。③ 可测"cutout 段折叠进不透明段"（地形材质已是 `alphaMode: Mask`（Main.qml:3888），alpha test 不需要独立半透段，6 段减 1）——受 chunkgeometry.h:67-70 的 D3D11 alphaCutoff 契约制约，需实测。

---

## 3. SIMD 调研

### 3.1 构建基线与前置条件（先做，否则一切免谈）

- 编译器 **MinGW-w64 GCC 13.1.0**（CMakeCache 实证），`-O3 -DNDEBUG -std=gnu++20`（build/build.ninja:56），x86-64 **仅 SSE2 基线**，无 `-march`/`/arch`，自有代码零 intrinsics（vendored `src/Audio/miniaudio.h` 除外）。GCC 自动向量化在逐格路由/分支密集循环上基本不命中。
- **前置改动**：为热点翻译单元开 `-march=x86-64-v3`（AVX2+FMA）。注意：① PLAN 最低配只保证四核 CPU、不保证 AVX2 → 需要**运行期分派**；② MinGW/PE 上 ifunc/`target_clones` 不可靠（ifunc 是 ELF 机制）→ **手工分派**：同一函数两份实现分放两个 TU（基线版 / v3 版），启动时 `__builtin_cpu_supports("avx2")` 选函数指针（该 builtin 已含 OS 支持检查 XCR0，不必自己担心老系统 YMM 关闭）。仓库已有 per-source COMPILE_OPTIONS 先例（CMakeLists.txt:707-712 miniaudio 的 `-w`），`set_source_files_properties(... PROPERTIES COMPILE_OPTIONS "-march=x86-64-v3")` 在 Ninja 下无障碍。
- **双 TU 分派的四条坑**（写进实施规范）：(i) 禁 LTO/IPO（当前未开，保持——开了会跨 TU 内联错档代码）；(ii) 两份实现严禁放头文件/模板（inline 实例化会 ODR 混档）；(iii) 分派用函数内 magic static，别用全局初始化顺序；(iv) `-march=v3` 会把 `std::experimental::native_simd` 宽度从 4 变 8，代码不得假设宽度（用 `simd::size()`）。
- **写法选型**：GCC 13 的 libstdc++ **内置 `std::experimental::simd`**（ISO TS 19570，GCC≥11 自带，已核实）——`<experimental/simd>` 写法可移植（C++26 `std::simd` 平滑过渡），比裸 `_mm256_*` 可读；gather 密集处（heightmap）可局部降级用 intrinsics。
- **正确性纪律（golden 逐字节）的真正杀手是 `-ffp-contract=fast`（GCC 默认）**：v3 TU 的 FMA 融合与基线 SSE2 标量必然 ±1ulp。配套三条：(a) v3 TU 加 `-ffp-contract=off`（或证明无 mul-add 邻接）；(b) golden 权威输出钉死为**标量基线版**，按 bit 比较；(c) golden 与构建档位绑定（基线/v3 两份实现的差异是预期内白名单，不是回归）。烘光公式尽量整数 LUT 化后 SIMD，天然确定。沿用本项目零警告 + golden 回归传统（lessons-learned.md:113-122）。

### 3.2 Top5 热点方案（按 收益/难度 排序）

**① mesher 可见面「顶点批填充 + PCF 软影批量采样」** —— 收益：高
- 现状：每可见面 4 顶点循环（chunkgeometry.cpp:1030-1047），每顶点一次 `sunShadowAt` = kMaxShadow 2 步 × 2×2 PCF = **8 次 `columnTopSurfaceY` 路由查询**（voxellight.h:35/58-95 → chunkmanager.cpp:73-84），再加 branchy UV 选择 + clamp/max/min 光照合成。greedy 段 948-969 同构。
- 改法：先把一个 chunk 的可见面收集成 SoA 面列表（tile/sky/block/4 角坐标），软影对 chunk 邻域 heightmap（`int[256]` 连续）做 8-wide gather + 比较；顶点 12-float 输出用 FMA/blend 一次算 4 顶点。clamp/max/min 全是向量指令，公式零分支。
- 预期：该子路径 4–8×；配合②③与 C2，整 buildMesh 综合 2–3×（受路由与 switch 段摊薄，Amdahl）。
- 验收：FrameProfiler `mesh` 桶 + golden 顶点逐字节对比（§3.1 纪律）。

**② `refloodBox` 快照/清零/比对三段流式化** —— 收益：中（批量编辑场景放大时受益）
- 现状：world.cpp:6502-6518（快照/清零逐格走 `skyLightAt/setLight` 路由 + nibble 移位）与 :6640-6661（逐 chunk 切片逐格路由比对）。这三段是纯连续字节流操作（32KB/chunk）。注意触发有早退门：recomputeLightAround 在遮光/发光均无变化时直接 return（world.cpp:6454），只有光相关编辑才到 refloodBox；代码注释自述典型快照/比对各 <0.5ms（:6498）。
- 改法：按 chunk 切片直接 memcpy 快照 / `std::fill` 清零 / `memcmp` 比对；nibble 解包比较处一条移位+and 处理 16 字节。BFS 本体（数据依赖）不动。
- 预期：单次典型编辑省的是 <1ms 里的约一半；**真正的受益场景是挖掘连破/爆炸/流体 tick 的批量收口**（m_pendingLightEdits 联合盒）。

**③ 「chunk+halo 扁平快照」底座** —— 收益：高（结构性，服务①，也是 C1 的免锁快照与 C2 的单遍底座）
- 见 §2.4-C3。预期：即便不上任何向量指令，仅消路由就明显提速；是①的前置。

**④ Perlin noise2/noise3 列批向量化 + sin/cos 表** —— 收益：中（改善 worldgen 卡顿，非每帧）
- 现状：world.cpp:2973-3063 纯 double 标量 Perlin + 4-octave fBm；biomeAt 每列最多链 5 张 fBm 图（20 次 noise2/列，:3119-3158）；carveCaves 每地下格 2 次 noise3（~190k 格 × 2，:4607-4610）；worm 用 `std::sin/cos` 无表（:4701-4704）。
- 改法：纯函数零分支，最 SIMD 友好；改 float 精度 + 列批 8-wide（v3 16-wide）；perm 表 256B 常驻 L1，gather 友好。
- 预期：worldgen 数十 ms 级 → 个位数 ms（新建世界/改尺寸的启动卡顿直接改善）。
- ⚠️ **改 double→float 会变地形结果，与不变量 K 的交互必须想清楚**：bump `WorldgenVersion` 后，旧存档**继续向外探索会撞 K 的拒生门**（版本不符 → 拒绝生成并告警，PLAN.md:94），不是"旧世界完全不受影响"。策略二选一：双版本共存（按 chunk 记录的版本选噪声路径）；或接受旧世界边界拒生成并在 UI 告知。

**⑤ 光照 nibble 批量访问器** —— 收益：中（实现最简）
- 现状：`skyLightAt/blockLightAt` 的 `>>4 &0xF` 被 mesher/flood/快照/比对四处高频调用（chunk.cpp:78-106）。
- 改法：提供「整块取 sky 数组 / 整块取 block 数组」批量解包（一条指令处理 16 字节），供①②消费；`clearAllLight` memset 化。

### 3.3 明确不建议 SIMD 的（用别的手段）

| 热点 | 为什么不 SIMD | 替代 |
|---|---|---|
| DDA 射线（raycast.cpp:269-288） | 串行依赖链（下一步依赖上一步），每帧仅 2 次、射程 ≤5 格 | 不动 |
| 光照 BFS 传播本体 | 不规则数据依赖 | 只有②的周边流式段值得做 |
| 玩家/mob 碰撞 footprint（playercontroller.cpp:5172-5225） | 每 tick ~12 格，体量喂不宽向量 | C6：消 `collisionAABBsAt` 按值 vector 的堆分配 |
| 图集 tint/合成（resourcepackmanager.cpp:1098-1110 等） | 已用 scanLine；且只在启动/切包跑一次 | 不动（:1875 单点 pixel() 无所谓） |
| 存档 memcpy（worldstore.cpp:532-534） | 已是整块 memcpy（CRT 的 SIMD 实现） | 不动 |
| worldgen 填柱 setBlock 循环（world.cpp:3408-3461） | 收益被④覆盖 | 按列段 memset 式填充 |

---

## 4. 推荐路线图（把三条线拧成一股）

```
第 0 步（当天可做的快赢，无任何风险）
  └─ C7：F3 draw-call 换 renderStats 真值 + 掉落物/经验球 QQuick3DInstancing 试点

第 1 步（现在，4–10 周量级，全部无后端依赖；显式排除 C4）
  ├─ SIMD 前置：-march=x86-64-v3 双 TU + 运行期分派基建（含 -ffp-contract=off 与 golden 档位绑定）
  ├─ C6 分配修复（1–2 天，独立可先落）
  ├─ C3 halo 快照底座（同时充当 C1 的免锁快照——World 零锁现状下这是 worker 化的前置）
  ├─ C2 单遍多段同产（在 C3 的快照上做）
  ├─ C1 meshing 移出 GUI 线程（worker 产不可变数据 + GUI 上传；t155 刷新语义重定义 + worker 生命期）
  └─ ①②⑤ SIMD 化（golden 逐字节验收）
  → 存活到 B 的资产：worker 生产管线、halo 快照、SoA 面列表、golden 框架、分派基建
  → 报废于 B 的资产：Quick3D 上传层（setVertexData/6 段实例）与 48 字节 color4 顶点格式（换不变量 I）

第 2 步（顺手，1–2 天）
  └─ 路线 A：切 Vulkan 后端回归（自测项；alphaCutoff 契约/lit 材质/Particles3D/透明排序/首启管线编译/present mode 重点回归）

第 3 步（中期，B0 先行再决策）
  ├─ B0 spike（2–4 周，去风险闸门）：QQuickRhiItem + GuiPrivate + qsb + 纹理数组 probe 单 chunk 渲染
  ├─ B1 地形管线（3–6 个月业余量级）：mesher 契约迁移（第 1 步成果复用）+ uniform 光照 + 数组纹理 + 合批
  └─ B2/B3 实体/粒子/手持收编（2–4 个月，含 mob 动画方案重做；feature flag 保可玩性）→ 移除世界内容的 Quick3D 依赖
```

判断点：B0 结束时若 QRhi/Vulkan 工具链或纹理数组在本机受阻（或第 1 步做完后帧率已达验收），B 可以推迟而不损失——第 1 步的每一项都独立有收益。

---

## 5. 风险与不确定项

1. **QRhi 无兼容保证（持续）**：Qt 6.11.1 官方文档至今原文"no source or binary compatibility guarantees"（QQuickRhiItem 与 QRhi 皆然）；且 `qrhi.h` 等在私有 include 路径（需 `GuiPrivate`）。PLAN 的缓解（囚禁 + 对下一小版本编译 CI）在 B0 启动日必须生效。6.11 非 LTS，补丁窗口短——若 B 启动时 6.12 已出，评估先升版本再动渲染层。
2. **Qt Quick on Vulkan 在 Windows 是活跃 bug 源**（threaded loop 历史 lockup/crash、QTBUG-135635 AMD 黑屏等）；RTX 3060 不直接命中但回归要当真（§2.1 清单）。
3. **两个 3D 视图不共享深度**：B 期间世界内容必须整体迁移（§2.2 B2），中途 feature flag 双路径，不能"地形自研层 + 实体 Quick3D"长期混居。
4. **C1 线程化的数据竞争前置**：World 零锁现状下必须先有不变量 C 或 halo 快照（§2.4-C1）；t155 同步刷新语义要重定义。
5. **SIMD 改 float 求值顺序**：golden 必须逐字节且 `-ffp-contract=off`（§3.1）；④改 float 精度必须 bump `WorldgenVersion` 并处理 K 的拒生门边界策略（§3.2-④）。
6. **MinGW 无 ifunc**：运行期分派用手工双 TU 函数指针（§3.1 四条坑），不要 `target_clones`。
7. **本报告为只读调研**：实施拆解（任务粒度/验收文案）归 dev-plan 流程；本报告未读 dev-plan.md，若与其冲突以协商为准。
8. 历史文档勘误待办（非本报告范围）：lessons-learned.md:119 "Vulkan 已生效" 陈旧条目、:39 "greedy 默认开"表述与代码（默认 false，chunkgeometry.h:250）不一致，建议顺手修正。

---

## 6. 证据索引（复核入口）

- 代码：main.cpp:84-108/128-177；chunkgeometry.h:28/42-56/67-70/95-126/250、chunkgeometry.cpp:442-1116（:512/:644 段守卫、:1030-1047、:1069-1090）；voxellight.h:30-95；chunk.h:97-105、chunk.cpp:37-106；world.cpp:2973-3100/3119-3158/3408-3461/4590-4704/6333-6663（:6454 早退门、:6498 注释、:6502-6518、:6640-6661）；raycast.cpp:117-290；playercontroller.cpp:5172-5225；resourcepackmanager.cpp:1098-1110/1489-1505/1875；worldstore.cpp:434-534；CMakeLists.txt:9/707-724。
- QML：Main.qml:244/281/345/2629-2688/3459-3463/3606/3811-4060/5857-6800/7929-7969；BlockParticles.qml。
- 文档：PLAN.md §1/§2（:84/:85/:92/:94）/§4；lessons-learned.md L7-63/L113-146；usage-report.md L451-677；dev-spec.md:15/447；test-reports/229-arch.md:19、276-correctness.md:39、t178-correctness.md:87。
- 日志：logs/voxelsandbox.log（开头 backend/threaded loop/适配器/swapchain；:164-165、:1267-1268、:1407-1408 单 chunk 重建；:1993 mesh 批重建；:1994 帧分解）。
- 本机 kit 核对（Qt 6.11.1 mingw_64）：qquickrhiitem.h:28-30（Renderer 虚函数三件套）；qrhi.h:1763/1894（Timestamps/lastCompletedGpuTime，私有路径）；Quick3D.qmltypes（RenderStats.drawCallCount）；qrhiprofiler.h **未随 kit 安装**（内部 API，v1.0 误引已删）。
- 外部核实（2026-08-21）：Qt 6.11.1 QQuickWindow::setGraphicsApi / QQuickRhiItem / QRhi / QQuick3DGeometry / Quick3D Instanced Rendering 官方文档；VcDevel/std-simd 与 cppreference（`std::experimental::simd` GCC≥11 内置于 libstdc++，C++26 起 `std::simd`）；QTBUG-135635（Qt 6.9 AMD+Vulkan 黑屏）。
- 审阅记录：v1.0 经两路独立子 agent 审阅——事实核对（16 项关键断言逐条对码，14 项通过、2 项修正后通过，随机抽查 8 处 file:line 全中）与对抗性技术审（判定"需小修"，8 项必修已全部并入本版）。
