# Qt 体素沙盒 — 设计与路线图

> 状态：**活文档（已批准，持续修订）**。经"研究(4 路并行查证) → 综合 → 对抗性审查(2 路)"三轮打磨。
> 最近修订：**pin Qt 6.11.1**（你已安装）；**性能目标改为 MC 官方现行最低/推荐配**（现行最低配已要求 Vulkan 1.3）；补 **Phase 0 引导、.gitattributes、CI 轻量版、TL;DR**。
> 目标读者：你（单人、业余、主导者）。

---

## TL;DR

- **做什么**：Qt6.11.1 / C++20 体素沙盒，**机制对标 Minecraft Java 1.0.0**（仅机制，非视觉/专有名词），原创资产与名称。
- **栈**：CMake + QML(UI) + 自研 RHI 体素渲染层（Vulkan 为主，D3D11/GL 兜底）；SQLite 存档；miniaudio；后期自有协议联机（Qt↔Qt）。
- **分期**：Phase 0 引导 → **1.0 引擎纵切（创造沙盒，真正的成功线）** → 1.1 存档+最小生存 → 1.x 完整 1.0.0 桌面 → 2 安卓 → 3 联机 → 4 Linux+打磨。
- **现实**：单人业余，多年级（完整"逼近 1.0.0" ≈ 4–8 年）；Phase 1.0 是验收闸，带退出阀。
- **性能目标**：对齐 MC 官方现行最低配（Vulkan 1.3 GPU / ≥2GB VRAM、8–12GB RAM、四核）→ 1080p @ ≥30fps（Fast 档）；推荐配 → 60fps（Fancy 档）。
- **红线**：不用 MC 任何资产 / 名称 / 专有名词；不与正版互通。

---

## Context（为什么 / 做什么）

- **做什么**：用 **Qt6 / C++** 从零做一个体素沙盒游戏，**对标 Minecraft Java Edition 1.0.0（2011-11-18 发布）的单机桌面版本**，"尽可能逼近"——但**仅指机制（mechanics）**，绝不指视觉外观或专有名词。
- **为什么是 1.0.0 而非 1.21**：1.21 是 Mojang/Microsoft 用 15 年、上百人堆出来的；单人业余"完美复刻 1.21"不可界定、不可收敛。1.0.0 是**可界定、可验收**的靶子（128 高度、`(id, metadata)` 方块模型、Region 存档、有限生物群系/生物集）。
- **锁定决策**（前期讨论已拍板）：
  1. 桌面单机先行；安卓 / 联机 / Linux 分期。
  2. 联机 = **自有协议、仅 Qt↔Qt**（不与正版互通，无法律灰度）。
  3. 渲染 = **QML 做 UI + 自研 Qt6 RHI 体素渲染层**。
  4. 投入 = **你一个人，业余时间**。
  5. 可参考开源代码，但**资产/名称必须原创**。
  6. **Qt 版本 = 6.11.1**（你已安装，直接 pin）。
  7. **桌面性能目标 = MC 官方现行最低/推荐配**（让大多数人能玩，见 §4）。
- **核心现实**（最重要的一句）：这是**多年级**工程。连"桌面单机 1.0.0 核心"都是数月到年级。**真正的成功线是 Phase 1.0（引擎纵切）**——能玩、能交给朋友。完整"逼近 1.0.0"按诚实估计是 **4–8 年**，且大多数单人项目做不完。所以本计划围绕"最小可玩纵切 + 预先写好的砍单清单 + 退出阀"来排。

---

## 1. 技术栈（含研究纠错）

| 关注点 | 决定 | 关键纠错 / 依据 |
|---|---|---|
| Qt 版本 | **6.11.1（已安装，pin）** | 非 LTS（当前 LTS 是 6.8）。你已装 6.11.1 → 直接 pin、省一次安装、RHI 更新。代价：6.11 是非 LTS，补丁窗口短（6.12 发布即停），OSS 补丁本就滞后于商业。缓解：QRhi **无源/二进制兼容保证**的 churn 靠**不变量 A（RHI 囚禁）+ "渲染器对下一 Qt 小版本编译"CI job** 兜住。日后若求最长稳定期，可并行装 LTS。 |
| C++ 标准 | **C++20** | `std::span`/`std::jthread`/ranges/`std::format`；避开 C++23（工具链参差）。 |
| 构建 | **CMake + Qt CMake API**（`qt_standard_project_setup`、`qt_add_executable`、`qt_add_qml_module`、`qt_add_shader`） | 一棵树出 Win/Linux/Android。 |
| UI | **Qt Quick 2 + QML**（HUD/背包/合成/菜单） | **不用** Qt Quick 3D 画体素世界（它不为海量动态区块设计）。 |
| 桌面后端 | **Vulkan 主**，D3D11(Win)/OpenGL(Linux) 兜底 | Vulkan 是 Win+Linux+Android 共有的唯一后端 → 单一主代码路径。**且 MC 官方现行最低配已要求 Vulkan 1.3**，印证此选择；D3D11/GL 仅作安全兜底。 |
| 安卓后端 | **OpenGL ES 主**（覆盖率），Vulkan 可选 | GLES 是 QRhi 在安卓的默认，几乎所有设备可用。 |
| RHI 集成 | **`QQuickRhiItem` + `QQuickRhiItemRenderer`** | 6.7 起公开（⚠️"6.11 仍 Preliminary"经核实为**误**）；但 **QRhi 全家无源/二进制兼容保证**，跨小版本会破 → 必须囚禁（见不变量 A）。 |
| 纹理 | **纹理数组为快路径 + 半像素内缩/逐 tile mipmap 图集为可移植兜底** | ⚠️ `TextureArrays` 是**运行期 feature-gated**（Qt 6.11 文档确认），GLES 与桌面 GL 都可能不支持。**第一天就要备好图集兜底**，并在 `initialize()` 里 `isFeatureSupported()` probe。禁止"只做数组、图集以后再说"。 |
| 着色器 | Vulkan GLSL → **`qt_add_shader()`/qsb 构建期离线烘焙** `.qsb` | ⚠️"qsb 运行期翻译"是**误**；qsb 离线烘出含全后端的容器，运行期 QRhi 选 blob。没烘的后端运行期直接失败。 |
| 线程 | **QThreadPool/QtConcurrent**（gen/mesh worker）+ **`std::jthread`**（长寿命后台）+ 渲染线程（QQuickRhiItem 持有） | GPU buffer 只在渲染线程建/传。 |
| 数学 | **GLM**（热路径）+ **QtMath**（QML 边界） | GLM 与 shader uniform 1:1 对齐。 |
| 音频 | **miniaudio**（单头，MIT） | Qt Multimedia 后端矩阵是已知跨平台不一致源。 |
| 存档 | **SQLite**（事务性）+ 自描述 chunk blob + 迁移注册表 | 比自研 McRegion 崩溃安全得多。 |
| 联机（后期） | 长度前缀 + **CBOR** over `QTcpSocket`/`QWebSocket`，**服务端权威** | 仅 Qt↔Qt。 |

---

## 2. 架构（分层 + 审查强制的硬不变量）

```
┌───────────────────────────────────────────────┐
│ UI (QML): HUD / 背包 / 合成 / 菜单 / 选项        │ Qt Quick
├───────────────────────────────────────────────┤
│ ViewModels (C++): QML_NAMED_ELEMENT 暴露        │ ◀ QML/C++ 桥
├───────────────────────────────────────────────┤
│ Game: 模式/玩家态/合成/冶炼/时钟                 │
├───────────────────────────────────────────────┤
│ Entities: Player/Mob/AI/掉落物                   │
├───────────────────────────────────────────────┤
│ Physics: swept-AABB 碰撞                         │
├───────────────────────────────────────────────┤
│ Renderer: RHI/mesher/mesh cache/camera/frustum   │
├───────────────────────────────────────────────┤
│ World: chunks/worldgen/lighting/save             │
├───────────────────────────────────────────────┤
│ Core/Platform: math/IO/threading/config/assets/log │
└───────────────────────────────────────────────┘
        Network（当前 stub）— 兄弟模块，Phase 3
```

**分层铁律**：低层永不 include/link 高层；依赖只向下。

### 审查挖出、已纳入的硬不变量（计划能活下来的关键）

- **A. RHI 囚笼**：所有 `QRhi*` / `QShader*` / `QQuickRhiItemRenderer` 只许出现在 `src/Renderer/`；模块外 header 不得含。CI include-guard（`grep -rn 'qrhi.h\|QShader' src/` 命中 Renderer 外 → 构建失败）。再加"渲染器对下一个 Qt 小版本编译"的 CI job，API 破坏当天发现。
- **B. mesh → 渲染线程握手**（崩溃所在）：worker 产出**不可变** `ChunkMeshData`（顶点+索引、owning、move-only）→ **MPSC 队列 / 互斥交换** → `synchronize()` 抽干到 pending-upload → `render()`/`prepare()` 建/重建 `QRhiBuffer` → 每 chunk 跟踪 `UploadState`。**渲染线程绝不读未完成上传的 cache**。
- **C. 世界锁模型**：per-chunk `std::shared_mutex`（读共享/写独占）。所有非所属线程的 voxel 读走共享锁。ThreadSanitizer CI 强制此不变量。
- **D. 单一输入路径**：边缘统一把原始事件翻译成 `InputAction` → `InputBus`（桌面 `VoxelView` override 与安卓 PointerHandler **都**翻译）；绑定表（`QKeySequence → InputAction`）放 `Config`、在边缘读；渲染线程只消费 `synchronize()` 准备好的 InputAction 快照。→ 使"按键重绑"可实现。
- **E. 错误模型**：fallible 的 World/Renderer 操作用 `Result<T>`；chunk 加载失败 → "错误块"（品红）兜底（世界一致而非崩溃）；shader 编译失败 → 内置 unlit 兜底 + 大声告警；存档损坏 → 停机 + **迁移前先备份**。
- **F. 日志 + F3 叠层**：`QLoggingCategory` 按模块（`vo.world`/`vo.render`/...）+ 滚动文件 sink + **F3 QML 调试叠层**（fps / chunk / mesh / 线程计数）。没有它，帧率验收无法诊断帧抖。
- **G. Core 决定**：**Config = JSON**（原生 `QJsonDocument`，避免引入 toml++）；**EventBus = 类型化 QObject 信号 + `Qt::QueuedConnection` 跨线程**（无字符串派发）。
- **H. 光照模型**（删除"旋转方向光"）：**MC 1.0 的 per-column 天光**——自顶向下首个实体的 heightmap，顶点"见天"则天光；昼夜用**天光亮度乘子** lerp（不是旋转方向光）；方块光（火把/岩浆）独立 flood-fill、时间不变。平滑光照=逐顶点混合(**M**)，AO(**S**)。
- **I. 顶点格式（贪婪/AO-ready，day-1）**：`{vec3 pos, vec3 normal, vec2 uv, uint tileIndex, uint8 ao[4]}`，即使初期 `ao≡1.0`。避免将来开 AO 时重网格化所有缓存 buffer。
- **J. 存储单元**：**16×16×128 列**为主存储/网格化/光照/culling 单元（贴合 MC 1.0，简化 heightmap 与跨 border 光照）；16³ 仅作内部并行子区（如需）。**不要** chunklet×8 双重抽象（两边成本都吃）。
- **K. 世界生成确定性**：固定 `WorldgenVersion` 烘进 chunk；加载未生成邻居若版本不符 → **拒绝生成并告警**，而非悄悄生成不一致地形；CI golden heightmap 回归。
- **L. 资产管线**：`assets-src/`(PNG/WAV) → CMake/Python 预构建（打纹理数组源图 + `tile_index.json`（name→layer/UV）+ `qt_add_shader` 烘 `.qsb` + 音频归一化）→ 从**磁盘**加载（非 `:/`，便于热补丁 & 安卓包体管控）。**资产获取是 Phase 1.0 验收门，不是勾选框**。
- **M. LocalServer 诚实化**：SP = 进程内**直接 C++ 调用**，与 `RemoteServer` 的包处理器**共享方法签名 + 共享 `ServerLogic` 校验 mixin**。**不在 SP 走 CBOR-over-socket**。Phase 3 真正工作 = "给 `IServer` 套 socket + 写 codec"，范围诚实。

---

## 3. 模块与文件骨架（greenfield，全部新建）

```
QtMinecraft/
├─ CMakeLists.txt                 # 顶层 + 平台 toolchain
├─ src/
│  ├─ Core/      # math(GLM/QtMath)、ThreadPool、InputBus、AssetManager、Config、EventBus、log、Result<T>
│  ├─ World/     # BlockRegistry、Chunk(16x16x128)、ChunkManager(LRU+dirty)、WorldGen、Lighting、WorldStore(SQLite)
│  ├─ Renderer/  # VoxelView(QQuickRhiItem)、VoxelRenderer、ChunkMesher、ChunkMeshCache、TextureArray、Camera、Frustum  ← QRhi 唯一驻地
│  ├─ Physics/   # AABB、SweptCollider
│  ├─ Entities/  # Entity/Player/Mob/EntityManager
│  ├─ Game/      # GameMode、PlayerState、CraftingSystem、SmeltingSystem、RecipeRegistry、WorldClock
│  └─ Network/   # IServer、LocalServer(SP 直接调用)；Phase 3 加 RemoteServer+codec
├─ src/ui/*.qml                   # hud/inventory/crafting/pause/options + debug 叠层
├─ assets-src/                    # PNG/WAV 原始资产
├─ tools/bake_assets/             # 预构建脚本（纹理数组/qsb/音频归一化/manifest）
├─ shaders/                       # Vulkan GLSL 源
└─ tests/                         # golden(worldgen/light/save)、单元、ThreadSanitizer
```

每模块职责见 §2 分层说明；依赖方向严格向下。

### Phase 0 — 项目初始化（bootstrap，进行中）

把上面的骨架立起来、确认工具链就位、让一个空 Qt6.11.1 窗口能编译运行。这是 Phase 1.0 的前置，也是**最早的去风险**（验证 Qt6.11.1 + RHI 工具链在你机器上能跑通）。

- [x] git 仓库初始化（`main`）+ `.gitignore`（Qt/C++/CMake/安卓）+ `.gitattributes`（统一 LF 行尾）。
- [x] 本计划文档落地 `docs/PLAN.md` 并入库。
- [ ] 顶层 `CMakeLists.txt`（`find_package(Qt6 6.11.1 COMPONENTS Core Gui Quick QuickControls2 ...)`、`qt_standard_project_setup()`）。
- [ ] 一个最小 `QQuickWindow` / `QQuickRhiItem` 空集成**能编译并在桌面跑出空窗口**（Vulkan 后端）。
- [ ] 工具链核实：Qt 6.11.1 路径、编译器（MSVC/MinGW/Clang）、CMake、Ninja。

**验收**：空窗口启动不崩、Vulkan 后端生效（F3 叠层或日志可见）、Windows 构建配置绿。

---

## 4. Phase 1.0 — 引擎纵切（engine spike，**第一个真正的成功线**）

> **诚实重命名**：这是"引擎证明（创造沙盒）"，**不是**生存循环。把"证明核心循环有趣"放到 Phase 1.1。把存档 round-trip 也移到 1.1（避免在没有任何生存机制压测前就定 chunk-store 设计）。

**范围（精简后）**
- 第一人称 + 鼠标看（捕获指针）+ WASD + 跳 + **fly 开关**（调试用）。
- 有限世界：单 16×16 列区域（256×256），平原一生物群系，OpenSimplex 高度图 + 树。
- **8 方块**：草/土/石/圆石/原木(橡)/木板/树叶/沙。
- **射线选体 + 线框高亮**（游戏手感第一要素）。
- 左键破、右键放（选中 hotbar 方块）；**culled meshing**（贪婪网格化推迟到 profile 要求）。
- QML **hotbar（9 槽）**，1–9 / 滚轮切换。
- 昼夜（天光亮度乘子 lerp，~20 min）。
- **原创占位贴图（16×16）+ 3 原创 SFX**（破/放/脚步）。
- **资产门**：8 贴图 + 像素字体 + GUI 铬，全部 CC0/原创，**每文件具名来源**。
- **F3 调试叠层**。

**验收（全部可证伪）**
- [ ] **性能（分档，对齐 MC 官方现行最低/推荐配）**：
  - **最低配**（官方现行 min：64 位、四核 CPU、**8GB RAM（独显）/ 12GB（核显）**、**Vulkan 1.3 + ≥2GB VRAM 的 GPU**）→ **1080p @ ≥30fps**，Fast 档（降视距/降画质）。⚠️ 官方现行最低配**已要求 Vulkan 1.3**，印证我们 Vulkan 为主；D3D11/GL 仍作安全兜底。
  - **推荐配**（16GB RAM、现代 CPU、≥6GB VRAM）→ **1080p @ ≥60fps**，Fancy 档。
  - 写死帧时间切分（CPU/GPU ms）、draw-call / 三角面上限 → 可 pass/fail，非"感觉还行"。来源：[minecraft.net 现行系统要求](https://www.minecraft.net/en-us/article/minecraft-java-edition-system-requirements)，落地前以官方页面最终核对。
- [ ] 鼠标看/WASD/跳/fly 无抖动。
- [ ] 射线命中 + 线框高亮渲染在命中面。
- [ ] 左破/右放 + hotbar 1–9/滚轮，选中槽视觉高亮。
- [ ] **跨 chunk 边界破放不破坏邻居 mesh**（脏标记邻接失效）。
- [ ] 窗口缩放 RHI 重建不崩、不拉伸。
- [ ] **`isFeatureSupported(TextureArrays)` 已 probe**；不支持则走图集兜底或拒启（有代码路径）。
- [ ] **零 Minecraft 资产 / 零专有名词**（Creeper 等已改名）。
- [ ] Windows(Vulkan) `-Wall -Wextra` / `/W4` 项目自有代码零警告。
- [ ] **Win + Linux CI 绿**（从 Phase 1.0 起，轻量"仅编译"版，见 §10）。
- [ ] **资产门通过**（每文件具名来源）。

**工时（已应用 triple-it）**：原始估 3–5 月（熟 RHI）/ 5–9 月（生）→ 诚实 **~9–15 / 15–27 hobbyist-month**（1 hobbyist-month ≈ 40–60 业余小时）。

**退出阀（写在前）**：**若 18 个 hobbyist-month 内 Phase 1.0 验收未过 → 以"创造沙盒"形态发布，关闭后续生存阶段。** 先写退路，再上路。

---

## 5. Phase 1.1 — 存档 + 最小生存循环（第一个"游戏"）

- SQLite 存档 round-trip（自描述 chunk blob + 迁移注册表 + temp-then-rename + `PRAGMA user_version`）。
- **最小核心循环**：原木 → 木板 → 工作台 → 夜间刷 1 种怪。**这才是验证"采集-合成-生存"是否好玩**的一步——即纵切本该回答的问题。
- 平面光照先稳，再上平滑/AO。

---

## 6. Phase 1.x — 完整 MC 1.0.0 单机桌面（多年生存模式）

**M = 必做验收；S = 有空再做；X = 明确排除出 v1**（精简）

- **世界生成/生物群系**：M 128 高度 + ~8 核心群系(Ocean/Plains/Forest/Taiga/Desert/Swampland/Extreme Hills/Ice + Beach/River 技术) + 树/矿/花/基础洞穴；S 丛林/蘑菇岛/峡谷/废弃矿井/地牢/村庄/3 要塞；X 现代群系。
- **方块**：M ~40 核心(石/土/草/沙/砾/圆石/三类木+板+叶/玻璃/水/岩/基岩/四矿+块/TNT/火把/工作台/炉/箱/门/梯/羊毛/沙岩/砖/黑曜/雪/冰/仙人掌/甘蔗/小麦/南瓜/蛋糕/面包)；S 补到 ~90；X 方块状态/Flattening（保持 `(id,metadata)`）。
- **生存机制**：M 生存+创造、生命+饥饿、冲刺/潜行、伤害/死亡/重生、2×2+3×3 合成、炉冶炼、昼夜+天气、夜间刷怪/白天动物/消失规则；S Hardcore/繁殖/附魔/酿造；X 村民交易/铁砧/信标/双持等(1.3+)。
- **生物**：M 牛/猪/羊/鸡 + 僵尸+爬行者(改名)类；S 其余 1.0 敌对集 + 雪傀儡/铁傀儡/狼/豹猫 + 末影龙+末地 + 下界+下界要塞+生物；X 马/兔等。
- **光照**：M 16 级天光+方块光/heightmap/flood-fill + 平滑；S AO。
- **红石**：**整体推迟到 post-1.0**（即使子集也会组合爆炸，单人坑）。
- **UI**：M HUD/背包/3×3 合成/炉/暂停/选项；S 附魔/酿造屏。

**预算诚实化**：**+30–60 hobbyist-month**（triple-it 后；"逼近 1.0.0" = **4–8 年**地平线）。**预先写好的砍单清单**（压力来时按此砍，而非临场砍掉当时烦你的东西）：红石 → 推迟；附魔/酿造 → S；Hardcore → 砍；下界/末地 → 作扩展硬排除出 v1。每条 M 配一句**可观测** exit test。

---

## 7. Phase 2 / 3 / 4（简）

- **P2 安卓**（+4–8 月）：触摸控件（虚拟摇杆 + 屏上破/放/跳 via QML PointerHandler）、GLES 主 + Vulkan 可选、**设备丢失恢复**（resume 重建全部 GPU 资源）、内存有界 LRU、`androiddeployqt` 出 APK/AAB、min-spec 拒启。**桌面无瑕前不动**。**物理安卓机从 1.x 起上工作台**（模拟器对 GLES 驱动撒谎）。验收：min-spec 设备 RD=6–10 ≥30fps；后台→恢复不崩；pause/resume 无图形损坏。min-spec 设备待你定（§11）。
- **P3 自有协议联机**（+6–12 月）：服务端权威（校验每个方块编辑/移动）、chunk+玩家态同步、基础反作弊、host-a-game QML UI；`LocalServer` 复用。验收：2–4 客户端共享一世界；编辑 ~200ms 内传播；30min 无 desync；SP 经 `LocalServer` 不变。**依赖 1.x 完成**（单机循环必须先稳——联网会让每个系统的复杂度乘上）。
- **P4 Linux + 打磨**（+3–6 月）：Linux Vulkan 验证、资产打磨、可访问性（按键重绑/色盲安全 HUD）、性能调优、打包（Win 安装包 + Linux AppImage）。验收：Win+Linux+Android 每 push CI 绿；2h playtest 无崩。

---

## 8. 风险（重排，审查纠正后）

1. **单人范围 / 动力悬崖**（最高）。地形+渲染+破放 ~20% 工作量；UI/背包/合成/存档/生存/光照传播/音频/打磨是另外 80%，项目死在这里。
2. **资产获取是门而非勾选框**（新晋 #2）。$0、非美术、**无现成 CC0 MC 风格子集**、法律禁用正版衍生 → ~90 方块×N 面变体 + 羊毛色 + GUI 图标 + 像素字体全要手画/委托，**数百小时坐在关键路径上**。
3. **QRhi 无兼容保证**（升档 top-3）。⚠️"6.11 Preliminary"说法**未证实**（6.7 起公开）；但病更重：**整个渲染器建在半私有 API 上，跨 Qt 小版本会破**（在非 LTS 的 6.11 上尤其要靠此纪律）。→ 囚禁(不变量 A) + "对下一小版本编译"CI job。
4. **安卓性能 + 驱动碎片**（桌面后最大技术风险）。Adreno/Mali/PowerVR 驱动方差大；后台 context loss；内存压力杀后台应用。
5. **法律 / IP（半边没画）**。版权半边 ✓（原创资产/名/不互通）；**商业外观(trade dress)半边缺失**——专有名词(Creeper 等)、UI 布局、美术方向都要区隔（见 §9）。
6. **存档 / 版本化腐蚀**。20 小时后存档损坏永久杀动力。
7. **光照 × 流式交互**（新列风险）。跨 chunk 边界的脏光传播 + 流式进出 → 闪烁/陈旧光 bug 吃周。→ 先稳平面光照再上平滑/AO；CI 确定性光照传播回归。
8. **CPU meshing 吞吐**（静默掉帧杀手）。→ `QThreadPool` meshing day-1；每帧 mesh 预算；脏标记+邻接失效；culled 起步；`Immutable` `QRhiBuffer`。

---

## 9. 法律 / IP（不可协商，day-1 就做对）

- **概念改名表**（现在决定、代码里就用，别等）：所有 Mojang 专有名词换原创名——例：Creeper→`Stalker`、Nether→`Ash`、The End→[名]、Enderman→[名]、Ender Dragon→[名]、Ghast/Blaze/Zombie Pigman… 全部。内部设计笔记可记"机制等价"，**代码与面向用户的字串绝不用原名**。
- **项目名**：不含 `mine`/`craft` 语素。
- **UI 布局非 1:1**：不照搬"底部居中 9 槽 + 上方一排心 + 饥饿排 + 中心十字"。
- **美术方向区隔**：不做"法律上 distinct 的 Minecraft 1.0"。
- **"逼近 1.0" = 机制对齐**，**绝不**视觉/专有名词对齐。
- **许可证优先**（先定，再碰任何第三方资产）：⚠️ CC-BY/CC-BY-SA 与 **MIT 不兼容**。建议 **MIT 或 GPLv3**；若想用 CC-BY-SA 资产则必须 GPLv3+。LICENSE + README 写明。
- 正版 Minecraft 安装里的**任何东西**视为禁区。

---

## 10. 验证（端到端怎么测）

- **CI（从 Phase 1.0 起，单人可承受的轻量版）**：先**仅编译**矩阵（Win + Linux），每 push 绿——**不上设备/图形测试**（对单人太重）。后续逐步加：渲染器对"下一个 Qt 小版本"编译 job（提早发现 QRhi 破坏）、ThreadSanitizer build（世界锁不变量 C）、golden 回归。
- **Golden 回归**：世界生成高度图（同 seed 同版本 diff）+ 存档每版本一个 fixture round-trip + 光照传播确定性。
- **性能预算**：分档命名机器（最低配/推荐配，§4）+ 帧时间切分 + draw-call/三角面上限（可 pass/fail，非"感觉还行"）。
- **物理安卓机**从 Phase 1.x 起每次 tag 烟测三端。
- **F3 叠层**人工诊断帧抖/区块/mesh。

---

## 11. 待你拍板的开放决策（影响计划形状）

> 已定：**Qt 6.11.1**；**桌面性能目标 = MC 官方现行最低/推荐配**。以下仍待拍板：

1. **项目名**（原创，不含 mine/craft）。
2. **许可证**：MIT / GPLv3 / 专有 —— 决定合法资产池。建议 MIT 或 GPLv3。
3. **你 Qt6 RHI / 图形的熟悉度** —— 决定 Phase 1.0 是 ~9–15 还是 ~15–27 月。若生疏，预算 2–4 月 ramp。
4. **存档格式**：SQLite（推荐，事务性）vs 自研 McRegion 式 `.mcr`（更"原教旨"但自己实现崩溃安全）。
5. **min-spec 安卓设备**（Phase 2）：GPU 级 / RAM 档（如 4GB）/ 最低 Android 版本。不能为每台手机优化。
6. **美术 / 音效能力**：自画 vs CC0 vs 委托 —— 决定 §6 S 内容落地速度。
7. **红石**：v1 推迟（**推荐**）还是子集？
8. **下界 / 末地**：v1 硬排除（**推荐**）还是实验性 flag？

---

## 引用来源（关键）
Qt 6.11.1 文档（`QQuickRhiItem`、`QRhi` TextureArrays 运行期 gating、无兼容保证）、Qt 维护期/OSS 滞后(qutebrowser #8464)、Qt 6.7 发布说明(QQuickRhiItem 公开)、Qt RHI Texture Item 示例("limited compatibility guarantee")、**[minecraft.net — Minecraft Java Edition System Requirements（更新版：现行最低配要求 Vulkan 1.3、1080p@30fps Fast）](https://www.minecraft.net/en-us/article/minecraft-java-edition-system-requirements)**、Minecraft Wiki(Java 1.0.0/919 天/Anvil vs Region/128 高度)、Luanti(Minetest 2024 改名)wiki+GitHub、fogleman/Craft、0fps《Meshing in a Minecraft Game》、miniaudio、gamedeveloper.com 单人 postmortem(Bass Monkey/Last Humble Bee "triple-it")、OpenGameArt-CC0。

---

*本计划骨头是对的（Phase 1.0 作为成功线、culled-meshing-first、纹理数组、SQLite、服务端权威、C++20/Qt6.11.1/CMake）。审查的价值在于把 ~6 处"看起来是决定、其实只是结果描述"的地方补上了机制，并纠正了 2 处自相矛盾的锁定决策（纹理数组+GLES、LocalServer 回环）。把这些修了，架构才站得住。*
