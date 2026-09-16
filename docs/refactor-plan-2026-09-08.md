# QtMinecraft 大规模化重构计划

日期：2026-09-08  
状态：提案 / 待按阶段执行  
适用项目：E:\Qt_Project\QtMinecraft  
目标：让项目从“固定规模、单机、主线程驱动的体素沙盒”逐步演进为能够支持数千 Chunk、无限世界、后台生成、多人联机、移动端和大量动态实体的可扩展游戏架构。

---

## 1. 先给结论：重构应该朝什么方向走

这次重构不应该以“把文件拆小”作为主要目标，也不应该从重写渲染器或一次性引入完整 ECS 开始。

真正应该建立的是下面这条主线：

1. C++ 负责确定性的世界模拟、实体模拟、规则和数据。
2. World、Entity、Renderer、Persistence、Network 之间通过稳定的命令、事件和快照通信。
3. 所有重 CPU 的工作都能够离开 GUI 线程执行。
4. 渲染器只消费不可变的渲染数据，不直接读取正在变化的世界数据。
5. QML 只负责界面、输入呈现和少量编排，不再承担世界会话、实体生命周期和存档协议。
6. 单机客户端、无头测试程序和未来服务器共用同一套核心模拟代码。
7. SQLite、Qt Quick 3D、桌面输入和文件系统都变成 Adapter，而不是核心逻辑的一部分。

最终目标架构可以概括为：

    Input / UI
        ↓ commands
    GameSession
        ↓
    Deterministic Simulation Core
        ├── WorldState / ChunkStore
        ├── WorldSimulation
        ├── EntityStore / EntitySystems
        └── Rule Systems
        ↓ immutable snapshots / events
    Renderer / Audio / UI / Network

这条路线的关键不是“以后一定使用某个具体技术”，而是建立真正的 Seam：

- 世界模拟可以没有 Qt Quick 3D 运行；
- 渲染后端可以替换；
- 存储后端可以替换；
- 客户端可以替换为无头服务器；
- 动态实体可以从 QML Delegate 改为批量渲染；
- 线程可以调整，而不用改写所有游戏规则。

---

## 2. 当前代码基线与重构动机

### 2.1 当前项目的优点

当前项目已经具备比较好的重构基础：

- 方块规则有 BlockRegistry 集中管理；
- World、Entities、Game、Renderer、Core 已经形成了大致的依赖方向；
- SQLite 存档已经有事务；
- ChunkGeometry 已经有 dirty 标记、网格统计和视距相关状态；
- FrameProfiler、F3、日志和构建版本戳提供了可观测性；
- 行为矩阵测试已经覆盖大量机制；
- 资源生成脚本和资源包管理相对完整；
- 玩法垂直切片已经很丰富，不需要为了验证架构再做一个全新的空 Demo。

这些东西都应该保留。重构的目标是把现有正确行为搬到更深的 Module 后面，而不是把已经验证的规则重新实现一遍。

### 2.2 当前最重要的结构性限制

当前实现存在以下限制：

| 当前做法 | 现在的收益 | 规模化后的问题 |
|---|---|---|
| World 直接持有全部 Chunk | 小世界简单、寻址直接 | 无限世界无法驻留；内存随世界线性增长 |
| World::generate 同步执行 | 行为容易理解 | 生成会阻塞 GUI 线程 |
| worldChanged 直接驱动 ChunkGeometry::buildMesh | 编辑后刷新直观 | 网格化和世界写入耦合，容易造成帧尖峰 |
| ChunkGeometry 继承 QQuick3DGeometry | 快速接入 Qt Quick 3D | World 数据和渲染对象绑定，线程化困难 |
| QML 每个 Chunk 多个 Model | 实现成本低 | Chunk 数量和实体数量上升后对象管理成本很高 |
| EntityManager 同时负责模拟和呈现数据 | 当前功能集中 | 大量实体、多人和后台线程时职责互相阻塞 |
| Main.qml 负责大量游戏编排 | 迭代初期速度快 | 输入、存档、实体、世界会话和 UI 变化相互影响 |
| redstone_matrix_test 是单个超大程序 | 方便快速追加探针 | 构建慢、运行慢、难以定位失败、没有 CTest 分层 |
| SQLite 全量重写 Chunk | 小存档简单可靠 | 大世界全量 DELETE + INSERT 会越来越昂贵 |
| QObject 信号直接连接大量逻辑 | Qt 集成方便 | 跨线程时难以明确所有权、顺序和生命周期 |

### 2.3 当前实现与计划文档的偏差

现有设计文档已经提出了后台生成、线程化网格、per-chunk 锁和自研 RHI 等目标，但当前代码实际仍以 Qt Quick 3D 的 QQuick3DGeometry 和同步 buildMesh 为主。

这不是失败，而是说明现在正处于一个重要的架构分叉点：

- 如果项目永远只做小型单机沙盒，当前架构可以继续完善；
- 如果目标确实包含无限世界、服务器和移动端，则必须从现在开始建立与渲染器无关的模拟核心。

本计划选择第二条路线。

相关当前代码：

- [README.md](../README.md)
- [PLAN.md](PLAN.md)
- [Main.qml](../src/ui/Main.qml)
- [world.cpp](../src/World/world.cpp)
- [chunkgeometry.cpp](../src/World/chunkgeometry.cpp)
- [worldstore.cpp](../src/World/worldstore.cpp)
- [CMakeLists.txt](../CMakeLists.txt)

---

## 3. 北极星架构

### 3.1 目标分层

最终建议将项目组织为以下逻辑层：

    voxel_base
      基础类型、坐标、ID、时间、错误、随机数、序列化接口

    voxel_world
      Chunk、WorldState、ChunkStorage、WorldGenerator、WorldQueries

    voxel_simulation
      固定 Tick、方块交互、流体、光照、植物、红石、天气

    voxel_entities
      EntityStore、玩家、生物、掉落物、载具、AI 和碰撞

    voxel_persistence
      SQLiteAdapter、RegionFileAdapter、SaveCoordinator、Migration

    voxel_render_data
      ChunkMeshData、EntityRenderSnapshot、MaterialData、RenderCommands

    voxel_renderer
      RendererInterface、QtQuick3DAdapter、RhiVoxelRenderer、GPU 资源管理

    voxel_input
      InputAction、InputMap、设备适配器、命令生成

    voxel_network
      Protocol、ServerSession、ClientSession、Snapshot、ChunkTransfer

    voxel_app
      Qt Application、QML UI、窗口、音频、平台集成

    voxel_server
      无头服务器入口，不链接 QML 和 Qt Quick 3D

    voxel_tests
      快速单元测试、模拟测试、存档测试、网络测试、渲染数据测试

依赖方向必须保持为：

    voxel_app ────────┐
    voxel_server ────┼──> voxel_network
    voxel_renderer ──┤
    voxel_persistence┤
                     ↓
              voxel_simulation
                     ↓
                voxel_world
                     ↓
                 voxel_base

约束：

- voxel_world 不得依赖 QML、QQuick3D、PlayerController、Main.qml；
- voxel_simulation 不得依赖 QQuick3D、QQuickWindow 或音频；
- voxel_entities 可以依赖 voxel_world 和 voxel_base，但不能依赖 QML Delegate；
- voxel_renderer 可以读取 render snapshot，但不得直接读取可变 WorldState；
- voxel_persistence 不得控制游戏 Tick；
- voxel_network 不得直接修改 Chunk 或 Entity，必须生成 Command；
- voxel_app 只负责把用户意图翻译成 Command，并消费 Snapshot/Event；
- voxel_server 使用和客户端相同的 simulation library。

### 3.2 为什么要使用深 Module

重构中应该优先建立少量“深 Module”，而不是拆成大量浅的转发类。

一个合格的 Module 应该具备：

- 较小的 Interface；
- 较多的行为隐藏在 Implementation 中；
- 明确的所有权和线程约束；
- 能够由同一个 Interface 进行测试；
- 调用者不需要知道内部是数组、树、数据库还是线程池。

推荐的深 Module：

| Module | 调用者只需要知道什么 | 内部隐藏什么 |
|---|---|---|
| WorldFacade | 查询、提交方块操作、获取 Chunk 快照 | Chunk 容器、索引、脏标记、加载状态 |
| SimulationClock | Tick、暂停、时间缩放 | 墙钟、固定步长、积压处理 |
| ChunkScheduler | 提交生成/网格/加载请求 | 优先级队列、取消、worker、epoch |
| EntityStore | EntityId、查询快照、提交实体命令 | dense storage、free list、空间索引 |
| SaveCoordinator | 保存会话、恢复会话、报告状态 | 多表事务、临时文件、迁移、重试 |
| RendererBackend | 提交 RenderSnapshot、创建/销毁视图 | Qt Quick 3D 或 QRhi 细节、GPU buffer |
| NetworkSession | Command、Snapshot、连接状态 | 编解码、可靠性、带宽控制、重连 |

不要把这些 Module 做成只负责调用另一个类的薄包装。如果一个类只有十几个转发方法，而没有隐藏复杂度，应重新考虑 Seam 的位置。

---

## 4. 核心设计原则

### 4.1 模拟权威原则

世界和实体的真实状态只存在于 Simulation Core。

以下对象只能是派生结果：

- QML 属性；
- QML Delegate；
- 渲染 Mesh；
- GPU Buffer；
- F3 文本；
- 网络 Snapshot；
- 存档 JSON；
- 音频播放状态。

它们都不能反过来成为规则判断的权威来源。

### 4.2 命令、事件、快照三分法

三种数据的职责必须区分：

#### Command

表示“想做什么”，由输入、网络或脚本生成。

例如：

- MovePlayer；
- BreakBlock；
- PlaceBlock；
- UseItem；
- AttackEntity；
- ToggleInventory；
- ChangeSetting。

Command 必须带：

- 来源；
- 目标 Tick；
- 相关 EntityId；
- 输入序号或客户端序号；
- 必要的参数；
- 校验所需的最小上下文。

#### Event

表示“已经发生了什么”，由模拟核心产生。

例如：

- BlockChanged；
- EntitySpawned；
- EntityDied；
- ItemDropped；
- SoundRequested；
- ChunkActivated；
- SaveCompleted。

Event 用于：

- UI 提示；
- 音频；
- 粒子；
- 网络广播；
- 统计；
- 日志。

Event 不应该用来驱动核心规则的隐式重入。核心规则应该在明确的 Tick 顺序中执行。

#### Snapshot

表示“某个时间点可供外部观察的状态”。

例如：

- WorldRenderSnapshot；
- EntityRenderSnapshot；
- PlayerHudSnapshot；
- ServerReplicationSnapshot；
- SaveSnapshot。

Snapshot 应该是：

- 对外不可变；
- 可跨线程传递；
- 尽量自包含；
- 不携带 QObject；
- 不暴露内部容器；
- 不引用已经可能失效的内存。

### 4.3 固定 Tick 原则

所有会影响规则的行为使用固定 Tick，不直接使用 frame delta：

- 世界模拟；
- 生物 AI；
- 流体；
- 红石；
- 伤害；
- 物品寿命；
- 载具；
- 服务器同步。

渲染可以使用可变帧率，但渲染只能插值两个已完成的 Simulation Snapshot。

推荐初始参数：

- Simulation Tick：20 Hz；
- 客户端渲染：30/60/120 Hz；
- 服务器逻辑：20 Hz；
- 最大单帧补 Tick 数：3；
- 超出补 Tick 上限时记录 lag event，而不是无限追赶。

这样可以避免“机器卡顿一帧后，世界模拟连续运行几十次又进一步卡死”的恶性循环。

### 4.4 所有权优先原则

每一类对象都必须有唯一 owner：

| 数据 | Owner |
|---|---|
| WorldState | Simulation Thread |
| EntityStore | Simulation Thread |
| Chunk generation job | ChunkScheduler / Worker |
| ChunkMeshData | 产生它的 job，完成后转移所有权 |
| GPU Buffer | Render Thread |
| QML UI State | GUI Thread |
| SQLite connection | Persistence Thread |
| Network socket | Network Thread |

任何跨 owner 访问都必须经过：

- Command；
- Event；
- immutable Snapshot；
- 明确的线程安全队列。

禁止通过“这个指针目前应该还有效”来跨线程传递对象。

---

## 5. 目标 Module 详细设计

## 5.1 voxel_base

### 职责

- 坐标类型；
- ChunkKey；
- EntityId；
- BlockId、BlockState；
- Tick；
- Result / Error；
- 固定种子随机数；
- 二进制读写基础接口；
- 版本号；
- 日志抽象。

### 必须新增的基础类型

#### ChunkKey

ChunkKey 必须使用有符号坐标：

- x、z 可以为负数；
- 不允许把世界坐标直接转换为无符号数组下标；
- ChunkKey 必须可哈希、可排序、可序列化；
- 负坐标的 floorDiv 和 floorMod 必须统一实现；
- 所有模块禁止自行实现负坐标换算。

#### BlockPos

统一提供：

- worldToChunk；
- worldToLocal；
- chunkToWorld；
- neighbor；
- section；
- distance；
- packed key。

#### EntityId

不要再以 QVector 下标作为对外身份。

建议使用：

- index；
- generation；

组成的稳定 EntityId。

实体释放后重新利用 index 时，generation 必须递增，防止旧引用误指向新实体。

#### Tick

不要在不同模块混用 int、qint64、毫秒和 float。

建议：

- SimulationTick 使用 uint64；
- Duration 使用明确的 Tick 或毫秒类型；
- 墙钟只属于 Platform/Clock；
- 规则代码尽量不读取 QDateTime::currentMSecsSinceEpoch。

### 验收标准

- 所有负坐标测试通过；
- 所有坐标换算只调用一套实现；
- 不依赖 Qt Quick 3D；
- 可以被无头测试程序单独链接；
- 没有静态全局随机状态。

---

## 5.2 WorldState 与 WorldFacade

当前 World 类承担了太多职责。重构后分成三个 Module：

1. WorldState：可变数据；
2. WorldFacade：受控查询和写入接口；
3. WorldSimulation：规则更新。

### WorldState

只存：

- 世界元数据；
- 活跃 Chunk；
- Chunk 状态；
- 时间和天气的模拟数据；
- 方块状态索引；
- 需要由模拟使用的空间索引。

不存：

- QML 对象；
- QQuick3DGeometry；
- Material；
- Texture；
- Audio；
- 网络连接；
- SQLite connection。

### WorldFacade 的建议 Interface

建议接口只暴露以下类别：

- getBlock；
- getBlockState；
- getLight；
- queryColumn；
- queryBox；
- submitEdit；
- getChunkSnapshot；
- getChunkLifecycle；
- getWorldMetadata。

方块写入不要向外暴露内部 Chunk 指针。所有写入都经过明确的 Edit/Command：

- EditResult；
- changed blocks；
- emitted events；
- dirty regions；
- lighting work；
- mesh invalidation。

### WorldSimulation

拆出当前 world.cpp 中的系统：

- TerrainGenerationSystem；
- FluidSystem；
- FireSystem；
- GrowthSystem；
- LeafDecaySystem；
- GravitySystem；
- RedstoneSystem；
- LightingSystem；
- WeatherSystem；
- StructureSystem。

每个系统都应遵守：

1. 只在固定 Tick 调用；
2. 通过 WorldFacade 读取；
3. 通过 EditBuffer 提交写入；
4. Tick 末尾统一应用；
5. 产生明确 Event；
6. 不直接触发渲染。

### EditBuffer

这是解除 worldChanged 风暴的关键。

系统在 Tick 内不要每写一个方块就发 QObject 信号，而是：

1. 记录 BlockEdit；
2. 合并相邻/重复修改；
3. 统一更新索引和光照；
4. 形成 DirtyChunkSet；
5. Tick 结束后发布一次 WorldDelta。

WorldDelta 包含：

- changed block ranges；
- affected chunks；
- light regions；
- block event；
- entity event；
- sound event。

渲染器根据 DirtyChunkSet 调度网格重建，不再由每个 setBlock 直接调用 buildMesh。

---

## 5.3 Chunk 数据与生命周期

### Chunk 不能再等于“永远驻留的 QVector”

无限世界必须区分 Chunk 的不同状态。

建议生命周期：

    Absent
      ↓ request
    Loading
      ↓ data ready
    Generated
      ↓ active radius
    Active
      ↓ outside active radius
    Loaded
      ↓ memory pressure
    Evicting
      ↓ save complete
    Absent

渲染还需要独立状态：

    NoMesh
    MeshQueued
    MeshBuilding
    MeshReady
    UploadQueued
    Uploaded
    Stale
    Evicting

不要把“Chunk 是否已生成”“Chunk 是否在模拟范围”“Chunk 是否有 GPU Mesh”压成一个 bool。

### Chunk 的数据拆分

建议一个 Chunk 至少拆为：

- BlockStorage；
- BlockStateStorage；
- LightStorage；
- Heightmap；
- BlockIndex；
- TickableIndex；
- EntitySpatialIndex；
- ChunkMetadata；
- Revision。

其中：

- BlockStorage 是规则数据；
- Mesh 是派生数据；
- Heightmap 如果可重新计算，应标记为派生缓存；
- GPU 资源绝不能存进 Chunk；
- revision 用于丢弃过期异步任务结果。

### Chunk Revision

每次 Chunk 数据发生变化时递增 revision。

异步任务提交时记录：

- ChunkKey；
- input revision；
- generator version；
- lighting version；
- renderer capability version。

任务完成后只有在这些条件仍然满足时才能提交结果：

- Chunk 仍然存在；
- revision 没有变化；
- world epoch 没有变化；
- job 没被取消；
- 任务没有使用过期规则版本。

否则结果直接丢弃，不允许覆盖新数据。

---

## 5.4 无限世界与 Chunk Streaming

### 目标

在不改变单机玩法的前提下支持：

- 正负坐标；
- 世界大小不预先分配；
- 玩家移动时动态加载；
- 离开范围后卸载；
- 多个兴趣点；
- 服务器和客户端不同加载半径。

### 兴趣点 Interest Point

Streaming 不应该只依赖一个 player 指针。

兴趣点可以来自：

- 本地玩家；
- 其他联机玩家；
- 相机；
- 服务器 spawn；
- 远程观察者；
- 结构预加载器。

每个兴趣点包含：

- ChunkKey；
- load radius；
- simulation radius；
- render radius；
- priority；
- owner/session。

### 三个范围必须分开

| 范围 | 内容 |
|---|---|
| Render Radius | 需要 GPU Mesh 和渲染资源 |
| Simulation Radius | 需要参与流体、生物、红石和 Tick |
| Load Radius | 需要保留 Chunk 数据，暂不一定模拟或绘制 |

推荐初始默认值：

- Render Radius：8 Chunk；
- Simulation Radius：6 Chunk；
- Load Radius：10 Chunk；
- 服务器发送半径：按客户端请求和带宽预算动态调整。

这些只是初始参数，必须通过 F3 和性能日志实时显示。

### Streaming 调度顺序

优先级建议：

1. 玩家脚下和视线前方；
2. 玩家 Render Radius 内；
3. 玩家 Simulation Radius 内；
4. 玩家 Load Radius 内；
5. 其他玩家附近；
6. 远处预取；
7. 低优先级存档写入。

### 取消策略

玩家快速移动时，旧方向的 Chunk 生成请求必须可取消。

如果底层算法无法中途取消，至少要做到：

- 任务完成后检查 epoch；
- 过期结果不进入世界；
- 过期结果不创建 GPU 资源；
- 过期结果不触发 QML 信号；
- 队列不能无限增长。

### 内存预算

必须从“世界有多少 Chunk”改为“当前允许占用多少 Chunk”。

建议配置：

- active chunk 上限；
- loaded chunk 上限；
- mesh chunk 上限；
- mesh vertex 上限；
- entity 上限；
- pending job 上限；
- save queue 上限。

超过预算时按以下顺序回收：

1. 远离所有兴趣点的 GPU Mesh；
2. 远离所有兴趣点的派生缓存；
3. 已保存的 Loaded Chunk；
4. 最后才考虑仍未保存的 Chunk。

未保存 Chunk 必须先完成 save 或进入安全的 dirty journal，不能直接丢弃。

---

## 5.5 后台世界生成

### 目标

后台生成必须做到：

- GUI 线程不执行整块地形生成；
- worker 不直接操作 QObject；
- worker 不触碰 QQuick3D；
- worker 不写共享 WorldState；
- 同一个 seed + ChunkKey + generator version 得到相同结果；
- 任务可以取消；
- 任务结果可丢弃；
- 任务不会阻塞退出。

### 推荐 Pipeline

    ChunkRequest
        ↓
    GenerationJob
        ↓
    RawChunkData
        ↓
    DecorationJob
        ↓
    LightingJob
        ↓
    WorldApplyQueue
        ↓
    WorldState

每个 Job 只接收不可变输入，输出 owning data。

### GenerationContext

不要让生成器直接读取 World 对象。

GenerationContext 应包含：

- seed；
- generator version；
- world coordinate；
- dimension；
- biome parameters；
- structure seed；
- deterministic random stream；
- cancellation token；
- generation options。

生成器不得读取：

- 当前玩家位置；
- QML 状态；
- 当前墙钟；
- 全局 RNG；
- GPU；
- SQLite。

### 跨 Chunk 结构

树、矿井、村庄、要塞等结构会跨 Chunk。

推荐两步法：

1. 每个 Chunk 先根据世界坐标确定性地产生候选结构；
2. StructurePlanner 根据结构 ID 和 bounding box 统一决定所有涉及 Chunk 的修改。

不要让某个 Chunk 的生成 Job 直接修改邻居 Chunk。

跨 Chunk 结构应生成：

- StructureId；
- bounding box；
- deterministic piece list；
- target chunk edits。

然后由各 Chunk 在自己的 Apply 阶段消费属于自己的 edits。

### 生成版本

每次改变 terrain、biome、structure 或 decoration 算法都递增 generator version。

存档需要记录：

- seed；
- generator version；
- game rules version；
- world data version。

同一个 seed 在不同 generator version 下可以产生不同世界，但必须明确标记，不能假装保持完全一致。

---

## 5.6 异步 Chunk Meshing

### 当前问题

当前 ChunkGeometry 既是：

- Qt Quick 3D 对象；
- mesh 生成器；
- World 查询者；
- dirty 状态消费者；
- 顶点统计提供者。

这导致线程化时所有职责同时冲突。

### 目标拆分

拆成三个 Module：

1. MeshBuilder：纯 C++ CPU 算法；
2. MeshScheduler：异步任务和优先级；
3. RendererBackend：GPU 资源创建和提交。

### MeshBuilder 接口

MeshBuilder 只接收：

- ChunkMeshInput；
- neighbor border snapshots；
- mesh options；
- material capabilities；
- cancellation token。

输出：

- ChunkMeshData；
- vertex count；
- index count；
- sections；
- bounds；
- source revision；
- build duration；
- diagnostic flags。

ChunkMeshData 必须：

- owning；
- move-only 或明确共享所有权；
- 不引用 Chunk 内存；
- 不包含 QObject；
- 不包含 QQuick3DGeometry；
- 不依赖 GUI 线程。

### 边界快照

Chunk 网格需要读取邻居方块，不能直接在 worker 中读取邻居的可变数组。

提交网格任务时打包：

- 当前 Chunk 的 voxel snapshot；
- 六个方向的 border snapshot；
- 必要的光照边界；
- source revision。

如果邻居在任务期间发生变化，只需根据 revision 丢弃旧 mesh。

### 线程模型

推荐：

    Simulation Thread
       └── WorldDelta
             ↓
    MeshScheduler
       ├── priority queue
       ├── worker pool
       └── cancellation / epoch
             ↓
    MeshResultQueue
             ↓
    Render Synchronize
             ↓
    GPU Upload

GPU Buffer 只能在渲染线程创建和更新。

### Qt Quick 3D 的过渡策略

不要第一步就删除 Qt Quick 3D。

分两阶段：

#### 过渡阶段

- MeshBuilder 从 ChunkGeometry 中抽出；
- ChunkGeometry 只负责把 ChunkMeshData 转成 QQuick3DGeometry；
- CPU 网格化放入 worker；
- QML 仍然可以保留 Chunk Model；
- renderer upload 仍使用现有 Quick 3D 机制。

#### 目标阶段

- RendererBackend 统一接口；
- QtQuick3DAdapter 作为兼容后端；
- RhiVoxelRenderer 作为大规模后端；
- Chunk 的 GPU 资源由 Renderer 管理；
- World 和 QML 不再持有 QQuick3DGeometry；
- 多个 Chunk 可以批量提交；
- 远处 Chunk 可以使用低精度或低频更新策略。

这样可以保证每一个阶段都有可运行版本。

---

## 5.7 渲染器与 GPU 资源

### 渲染层规则

渲染器只能消费：

- ChunkMeshData；
- EntityRenderSnapshot；
- CameraSnapshot；
- MaterialTable；
- LightSnapshot；
- RenderSettings。

渲染器不能调用：

- World::setBlock；
- EntityManager::spawnMob；
- Save；
- PlayerController；
- QML 函数；
- SQLite。

### Mesh 资源分级

建议至少分为：

- Near：完整网格、完整光照；
- Mid：普通网格、低频光照；
- Far：低细节网格或高度图；
- Hidden：只保留数据，不保留 GPU 资源。

### 批量绘制方向

不要继续线性增加每个 Chunk 的 QML Model 数量。

逐步演进：

1. 先统一 MeshData 和 material section；
2. 再把同材质 section 聚合；
3. 再减少 QML Model 数量；
4. 最终由 RhiVoxelRenderer 统一管理 Chunk GPU 资源；
5. 动态实体使用 instancing 或批量 draw；
6. 透明、cutout、液体使用独立 pass。

### 移动端渲染约束

移动端应默认：

- 关闭高成本 AO；
- 限制可见 Chunk；
- 限制透明段；
- 限制动态阴影；
- 限制每帧 GPU buffer 更新；
- 使用压缩纹理；
- 使用低分辨率或简化材质；
- 避免为每个实体创建独立 QML 3D 对象。

所有质量开关必须进入 RenderCapabilities，而不是散落在 Main.qml。

---

## 5.8 实体系统：支持大量动态实体

### 当前目标

支持：

- 数百到数千动态实体；
- 多种实体类型；
- AI 更新分级；
- 远处实体降频；
- 大量掉落物合并；
- 载具和生物空间查询；
- 客户端只渲染可见实体；
- 服务器模拟实体但不创建 QML 对象。

### 不建议一开始引入完整 ECS

项目现在最适合采用渐进式数据导向设计，而不是直接引入复杂 ECS 框架。

推荐：

- 稳定 EntityId；
- dense arrays；
- free list；
- type-specific component arrays；
- system 按类型批处理；
- presentation 与 simulation 分离。

### EntityStore

EntityStore 负责：

- 分配和回收 EntityId；
- 存放位置、速度、生命、类型、状态；
- 提供只读查询；
- 维护 spatial index；
- 输出 EntitySnapshot。

EntityStore 不负责：

- AI 规则；
- QML Delegate；
- 模型创建；
- 音效；
- 网络发送。

### System 拆分

建议至少拆成：

- MovementSystem；
- CollisionSystem；
- GravitySystem；
- AiSystem；
- CombatSystem；
- BreedingSystem；
- PickupSystem；
- ItemLifetimeSystem；
- VehicleSystem；
- ProjectileSystem；
- DamageSystem；
- SpawnSystem；
- DespawnSystem。

### 更新等级

实体距离兴趣点不同，更新频率不同：

| 等级 | 距离 | 处理 |
|---|---|---|
| Near | 玩家附近 | 每 Tick 完整 AI、碰撞和动画 |
| Active | 模拟半径内 | 每 Tick 规则，减少感知 |
| Dormant | 更远区域 | 低频更新或只处理必要计时器 |
| Stored | 未加载区域 | 只保存数据，不创建活体对象 |

不要让远处实体继续运行完整 AI。

### 空间索引

至少需要一个按 ChunkKey / CellKey 的空间索引：

- 碰撞查询；
- 邻居查询；
- 攻击范围查询；
- 掉落物合并；
- 生物生成；
- 声音范围；
- 网络兴趣管理。

禁止每个实体每 Tick 扫描全部实体。

### QML 呈现

QML 不再为所有实体创建独立、永久存在的 Delegate。

目标方式：

1. EntityStore 输出可见实体 RenderSnapshot；
2. Renderer 按类型批量绘制；
3. 只有交互 UI 或少量特殊实体才进入 QML；
4. 实体离开视野时只销毁呈现对象，不销毁模拟实体；
5. 实体跨世界卸载时由 Simulation/EntityStore 处理。

---

## 5.9 输入和 PlayerController 重构

当前 PlayerController 既是 QML 类型，又包含移动、碰撞、交互、战斗、存档相关逻辑。

建议拆为：

- InputDeviceAdapter；
- InputMap；
- InputAction；
- PlayerCommandBuilder；
- PlayerSimulation；
- PlayerInteraction；
- PlayerPresentation；
- PlayerSaveCodec。

### 输入数据流

    Keyboard / Mouse / Touch / Gamepad
                 ↓
           InputDeviceAdapter
                 ↓
             InputAction
                 ↓
         PlayerCommandBuilder
                 ↓
             Simulation Tick

键盘、鼠标和触摸不应该直接调用 World 或 PlayerController 的游戏方法。

### 移动端收益

只要触摸输入也翻译成 InputAction：

- 桌面和移动端共享模拟；
- 重绑定逻辑共享；
- 服务器可以发送同样的输入命令；
- 测试可以直接构造 InputAction；
- QML 不需要知道碰撞和移动细节。

---

## 5.10 存档与数据层

### 保存目标

需要支持：

- 小世界旧存档继续可读；
- 无限世界按 Region 或 Chunk 增量保存；
- 崩溃后恢复；
- 迁移；
- 多玩家；
- 后台写盘；
- 不阻塞模拟；
- 不覆盖损坏原档；
- 可检测保存是否完成。

### 存储 Seam

定义抽象接口：

- WorldMetaStore；
- ChunkStore；
- EntityStore；
- PlayerStore；
- JournalStore；
- CoverStore。

提供 Adapter：

1. SQLiteAdapter：迁移期继续使用；
2. RegionFileAdapter：大世界目标；
3. MemoryAdapter：测试；
4. FaultInjectingAdapter：故障测试。

模拟核心只依赖接口，不依赖 QSqlDatabase。

### Region 存储

无限世界推荐将 Chunk 按 Region 分组。

Region 可以按固定数量 Chunk 组织，例如 16×16 或 32×32，具体尺寸通过基准测试决定。

Region 文件需要包含：

- magic；
- format version；
- region coordinate；
- chunk index；
- compressed payload；
- payload length；
- checksum；
- last revision；
- optional generation version。

### 保存流程

目标流程：

    Simulation Snapshot
          ↓
    SaveCoordinator
          ↓
    Serialize / Compress
          ↓
    Temporary Journal or Temp Region
          ↓
    Flush
          ↓
    Atomic Commit
          ↓
    SaveCompleted Event

保存不得直接读取正在变化的 WorldState。

### 玩家、进度和世界一致性

当前地形存档、玩家 JSON 和进度统计存在独立写入路径。

目标方案必须有统一 save generation：

- worldGeneration；
- playerGeneration；
- entityGeneration；
- progressGeneration。

每一次保存创建统一的 SaveGeneration。

恢复时只接受一组完整 generation，避免出现：

- 玩家位置是新数据；
- 地形是旧数据；
- 成就是另一版本。

如果短期内无法放进一个物理事务，至少使用：

- manifest；
- generation；
- journal；
- commit marker。

### 损坏数据

遇到坏 Chunk 时：

1. 记录 chunk key 和 checksum；
2. 保留原文件；
3. 载入为可明确标记的缺失 Chunk；
4. UI 显示存档有损坏；
5. 未经用户确认不得用零数据覆盖原始坏数据；
6. 提供从备份或重新生成恢复的选项。

### 迁移

每次 world data version 迁移前：

- 创建备份；
- 写 migration journal；
- 执行临时副本迁移；
- 校验 Chunk 数量和 checksum；
- 写 commit marker；
- 成功后替换；
- 失败保留原档。

单独的 SQL transaction 只能保证数据库事务性，不能替代迁移前备份和语义校验。

---

## 5.11 多人联机

多人联机必须建立在确定性的单机模拟之上，而不是在现有 QML 状态之间增加 Socket。

### 推荐模式

采用服务器权威：

    Client Input
        ↓
    Network Command
        ↓
    Server Simulation
        ↓
    Authoritative Snapshot / Event
        ↓
    Client Reconciliation

服务器负责：

- 世界状态；
- 实体状态；
- 规则校验；
- 生成；
- 存档；
- 玩家权限；
- Chunk 发送。

客户端负责：

- 输入采集；
- 本地预测；
- 渲染；
- UI；
- 音频；
- 快照插值。

### 网络层不能依赖 QObject 游戏对象

协议数据应该是纯数据：

- Command；
- Event；
- Snapshot；
- ChunkPayload；
- Ack；
- Sequence；
- Tick；
- Version。

不要序列化 QObject 指针、QML 对象或 Qt Quick 3D 对象。

### 客户端预测范围

第一阶段只预测：

- 玩家移动；
- 镜头；
- 本地交互反馈。

不要一开始预测：

- 红石；
- 流体；
- 生物 AI；
- 结构生成；
- 存档。

这些由服务器权威返回。

### Chunk 同步

Chunk 同步需要：

- ChunkKey；
- data version；
- generator version；
- chunk revision；
- compressed data；
- checksum；
- server tick。

客户端收到旧 revision 时丢弃。

### 联机前置验收

必须先完成：

- headless simulation；
- 固定 Tick；
- 可重放 Command；
- 确定性 seed；
- 无 QML 的存档加载；
- 无渲染的行为测试；
- EntityId 稳定；
- WorldDelta 可序列化。

---

## 5.12 移动端

移动端不是“最后把窗口缩小”，而是对资源、输入、渲染和生命周期的综合约束。

### 平台层

建立 PlatformAdapter：

- FileSystem；
- Clock；
- Threading；
- Input；
- Audio；
- GPU capabilities；
- Save location；
- Suspend/resume；
- Low memory notification。

### 移动端必须独立配置

至少定义三个 profile：

- Desktop High；
- Desktop Low；
- Mobile；

profile 统一控制：

- render distance；
- simulation distance；
- texture resolution；
- shadow；
- AO；
- mesh detail；
- entity cap；
- particle cap；
- audio voices；
- save interval；
- worker count。

### 挂起和恢复

移动端可能随时暂停或回收进程。

必须做到：

- 应用进入后台时尽快生成 SaveSnapshot；
- 不等待所有远端 Chunk；
- 保存进行中可以安全终止；
- 恢复后检测 world epoch；
- Renderer 可以完全重建 GPU 资源；
- Simulation 不依赖 QML 组件仍然存在。

---

## 6. CMake 和仓库结构重构

### 6.1 目标 CMake target

建议最终形成：

    voxel_base
    voxel_world
    voxel_simulation
    voxel_entities
    voxel_persistence
    voxel_render_data
    voxel_renderer
    voxel_input
    voxel_network
    voxel_qt_adapters
    voxel_app
    voxel_server

测试：

    voxel_base_tests
    voxel_world_tests
    voxel_simulation_tests
    voxel_entities_tests
    voxel_persistence_tests
    voxel_render_data_tests
    voxel_network_tests
    voxel_qml_tests
    voxel_integration_tests
    voxel_stress_tests

### 6.2 头文件依赖规则

每个 target 设置自己的 include directory，不再让所有目录互相可见。

例如：

- voxel_world 只能看到 voxel_base；
- voxel_simulation 能看到 voxel_world；
- voxel_renderer 能看到 voxel_base、voxel_world 的 snapshot 接口和 voxel_render_data；
- voxel_app 能看到所有公开接口；
- voxel_server 不链接 Qt Quick 3D。

### 6.3 头文件边界

公开 header 只放：

- 小而稳定的类型；
- Module Interface；
- 不变量；
- 错误语义；
- 生命周期要求；
- 性能特征。

实现细节放到 cpp 或 private header。

### 6.4 CMake 质量门

逐步加入：

- enable_testing；
- add_test；
- compile_commands；
- 统一 warning profile；
- Debug / Release / RelWithDebInfo；
- AddressSanitizer；
- UndefinedBehaviorSanitizer；
- 静态检查；
- QML 类型检查；
- 资源完整性检查；
- 构建后启动 smoke test；
- 运行时依赖检查。

Windows、Linux 和 Android 的工具链参数必须分开定义，不要把 MinGW 参数直接散落到主 target。

---

## 7. 测试重构方案

### 7.1 测试金字塔

目标比例：

| 测试层 | 目标 | 运行时间 |
|---|---|---|
| Base 单元测试 | 坐标、ID、随机数、序列化 | 秒级 |
| World 测试 | Chunk、世界查询、生成 | 秒到十秒 |
| Simulation 测试 | 固定 Tick、规则链 | 秒到几十秒 |
| Entity 测试 | AI、碰撞、生命周期 | 秒到几十秒 |
| Persistence 测试 | 保存、恢复、迁移、损坏 | 秒级 |
| RenderData 测试 | mesh 输入输出、快照 | 秒级 |
| QML 测试 | 少量关键组件 | 十秒级 |
| 集成测试 | 游戏会话全链路 | 分钟级 |
| 压力测试 | 大世界、实体、网络 | 手动或 nightly |

### 7.2 redstone_matrix_test 的迁移

不要直接删除现有矩阵。

迁移顺序：

1. 保留现有可用行为；
2. 提取测试辅助函数；
3. 按领域切分测试注册表；
4. 每个领域生成独立 target；
5. 统一 PASS/FAIL 结果结构；
6. 接入 CTest；
7. 增加每组超时；
8. 增加进度输出；
9. 保留一个完整回归 target；
10. 完整回归只在 nightly 或发布前运行。

建议测试组：

- matrix_base；
- matrix_worldgen；
- matrix_block_rules；
- matrix_redstone；
- matrix_entity；
- matrix_player；
- matrix_persistence；
- matrix_render_geometry；
- matrix_qml;
- matrix_regression_full。

### 7.3 每个测试必须具备的元数据

- 测试名；
- 所属 Module；
- 预计运行时间；
- 是否需要 QGuiApplication；
- 是否需要 OpenGL/QRhi；
- 是否会写临时文件；
- 是否支持 Windows/Linux；
- 是否允许 flaky；
- 失败时的最小复现参数。

### 7.4 确定性与重放

每个模拟测试都应能记录：

- seed；
- world data version；
- generator version；
- starting snapshot；
- command sequence；
- expected event sequence；
- final checksum。

失败时输出 replay 文件，允许单独复现，而不是重新运行整套矩阵。

### 7.5 性能测试

增加固定 benchmark：

- 生成 1 Chunk；
- 生成 16×16 Chunk；
- 光照重算；
- 破坏一个方块；
- 批量方块编辑；
- 生成 1000 个实体；
- 10000 个掉落物合并；
- 100 个 Chunk mesh；
- 读写一个 Region；
- 加载世界首帧；
- 服务器 20 Tick。

所有 benchmark 记录：

- P50；
- P95；
- P99；
- 峰值内存；
- 队列长度；
- 主线程耗时；
- worker 耗时；
- GPU upload 数量。

---

## 8. 分阶段实施路线

下面的顺序是强制建议顺序。不要跳过前置阶段直接做无限世界或多人联机。

## Phase 0：建立基线和安全网

预计：1 至 2 周全职，个人业余开发约 2 至 4 周。

### 目标

在不改变玩法的前提下，建立可以测量、可以回滚、可以比较的基线。

### 工作项

- 记录当前启动时间；
- 记录默认世界生成时间；
- 记录进入世界首帧时间；
- 记录 mesh 重建数量；
- 记录主线程单帧耗时；
- 记录内存；
- 记录实体数量；
- 记录存档时间；
- 记录当前测试矩阵运行时间；
- 记录默认世界的 seed、Chunk 数和顶点数；
- 记录当前存档格式版本；
- 固定一个 baseline commit；
- 导出当前行为测试结果；
- 给当前工作区修改建立独立分支或提交。

### 必须新增

- perf-baseline-2026-09-08.md；
- 一个可重复的启动 smoke test；
- 一个小世界 save/load test；
- 一个固定 seed world checksum test；
- 一个当前应用启动日志模板。

### 验收

- 新旧版本可以比较；
- 任何后续性能变化有数字证据；
- 能在 10 秒内验证程序是否能启动；
- 不再使用旧 README 数字作为唯一测试依据。

### 禁止

- 不在本阶段重写 World；
- 不在本阶段改渲染后端；
- 不在本阶段删除旧测试；
- 不在本阶段引入完整 ECS。

---

## Phase 1：抽出 C++ Core 和 GameSession

预计：2 至 4 周全职，个人业余开发约 1 至 2 个月。

### 目标

让世界和实体规则可以在没有 Main.qml 和 View3D 的情况下运行。

### 工作项

- 建立 voxel_base；
- 抽出坐标、Tick、EntityId、Result；
- 建立 SimulationClock；
- 建立 GameSession；
- 把 World 的纯查询移入 WorldFacade；
- 把 World 的规则调用顺序移入 WorldSimulation；
- 把 PlayerController 的纯规则部分抽出 PlayerSimulation；
- 将 QML 调用改为提交 Command；
- 将 QObject 信号改为 EventCollector 或 WorldDelta；
- 保留旧 QML 调用作为兼容适配器。

### 目标数据流

    QML input
      ↓
    QtGameAdapter
      ↓
    GameSession::submit(Command)
      ↓
    Simulation::advance(Tick)
      ↓
    Event / Snapshot
      ↓
    QML adapter

### 验收

- 无头程序可以创建 World、Player、Entity；
- 无头程序可以执行破坏和放置方块；
- 无头程序可以运行至少 1000 个固定 Tick；
- 规则测试不需要加载 Main.qml；
- QML 仍能运行现有主要玩法；
- World 不 include QQuick3D。

### 回滚

保留 QtGameAdapter，把旧 PlayerController 和新 GameSession 并行运行一段时间。

---

## Phase 2：Chunk 数据与加载生命周期

预计：3 至 6 周全职，个人业余开发约 2 至 3 个月。

### 目标

把固定全驻留世界改成可加载、可卸载、可管理状态的 ChunkWorld。

### 工作项

- 引入 ChunkKey 和负坐标；
- 将 Chunk 容器从固定 vector 扩展为 keyed store；
- 引入 ChunkLifecycle；
- 引入 ChunkRevision；
- 引入 InterestPoint；
- 引入 ChunkScheduler 的同步版本；
- 先实现主线程调度，但不改变生成算法；
- 先支持内存卸载和重新加载；
- 让默认 10×10 世界通过新生命周期运行；
- 保存旧 SQLite 格式兼容。

### 验收

- 支持负 X/Z；
- 玩家移动可以触发 Chunk 进入和离开；
- 远端 Chunk 能够卸载；
- 重新进入后行为和原来一致；
- 当前默认地图没有明显玩法回归；
- 内存不再随访问过的世界面积无限增长。

### 特别注意

不要一开始就把世界改成无限生成。先让固定 seed 的有限区域也通过同一套生命周期运行。

---

## Phase 3：后台生成 Pipeline

预计：4 至 8 周全职，个人业余开发约 3 至 5 个月。

### 目标

将 world generation 从 GUI 线程移到 worker，并建立过期结果丢弃机制。

### 工作项

- 抽出 GenerationContext；
- 抽出纯 C++ TerrainGenerator；
- 实现 GenerationJob；
- 实现线程安全 JobQueue；
- 实现 cancellation token；
- 实现 world epoch；
- 实现 GenerationResult；
- Simulation Thread 只负责 Apply；
- 对跨 Chunk 结构使用 StructurePlanner；
- 增加 worker queue F3 统计；
- 增加生成任务 P95 benchmark。

### 验收

- GUI 线程不再执行整块地形生成；
- 修改 seed 不会卡死窗口；
- 快速移动时旧生成任务不会覆盖新世界；
- 同 seed、同版本、同 ChunkKey 输出 checksum 一致；
- 退出时可以取消任务并正常关闭；
- worker 崩溃或异常不会破坏 GUI 生命周期。

### 不应在本阶段做

- 不同时重写所有 worldgen 规则；
- 不同时改变地形外观；
- 不同时做多人同步；
- 不把 GPU mesh 生成放进同一批次。

---

## Phase 4：后台 MeshBuilder 和 Renderer Seam

预计：4 至 8 周全职，个人业余开发约 3 至 5 个月。

### 目标

将 CPU 网格化和 GPU 上传完全分离。

### 工作项

- 从 ChunkGeometry 中抽出 MeshBuilder；
- 定义 ChunkMeshInput；
- 定义 ChunkMeshData；
- 加入邻居边界快照；
- 加入 revision 校验；
- 实现 MeshScheduler；
- 让 QtQuick3DAdapter 消费 MeshData；
- 让 QML 只维护有限的视图对象；
- 保留旧 buildMesh 作为 fallback；
- 对比旧、新网格顶点和视觉结果。

### 验收

- worker 不创建 QQuick3D 对象；
- worker 不访问可变 WorldState；
- 过期 mesh 不会覆盖新 mesh；
- 方块编辑后的刷新仍然正确；
- 进入世界不会出现永久空白；
- 主线程不再承担完整 mesh build；
- 默认世界视觉回归可接受。

### 过渡开关

建议加入：

- useAsyncGeneration；
- useAsyncMeshing；
- useQuick3DAdapter；
- useRhiRenderer；
- disableStreaming；
- deterministicDebugMode。

所有开关必须集中到 RuntimeConfig，不要散落在 QML 属性中。

---

## Phase 5：EntityStore 和呈现分离

预计：4 至 8 周全职，个人业余开发约 3 至 5 个月。

### 目标

让实体模拟数量与 QML 对象数量解耦。

### 工作项

- 引入稳定 EntityId；
- 把 EntityManager 改为 EntityStore；
- 拆出 Movement、Collision、AI、Combat、Pickup 系统；
- 加入 spatial index；
- 加入 update tiers；
- 输出 EntityRenderSnapshot；
- QML 只保留玩家和少量特殊对象；
- 掉落物改为批量或合并呈现；
- 生物渲染改为可见集合；
- 统计 active / simulated / rendered 三种数量。

### 验收

- 1000 个实体不会创建 1000 个 QML Delegate；
- 远处实体不会每 Tick 执行完整 AI；
- 实体回收不会产生重复 EntityId；
- 换世界后没有残留呈现对象；
- 实体模拟可在无头程序执行；
- 实体渲染可以完全关闭而不影响模拟。

---

## Phase 6：持久化和大世界存储

预计：4 至 8 周全职，个人业余开发约 3 至 5 个月。

### 目标

将存档从“小地图全量 SQLite”升级为可增量、可恢复的大世界存储。

### 工作项

- 建立 WorldStore Interface；
- 将当前 SQLite 逻辑移为 SQLiteAdapter；
- 建立 MemoryAdapter 和故障注入 Adapter；
- 建立 SaveCoordinator；
- 增加 SaveGeneration；
- 增加 journal；
- 增加 backup；
- 增加 checksum；
- 实现 RegionFileAdapter；
- 旧存档自动迁移到新格式；
- 增加坏 Chunk 恢复界面；
- 保存过程移出 GUI 主循环。

### 验收

- 旧存档可读取；
- 新存档不再全量重写所有远端 Chunk；
- 保存期间可以继续游玩；
- 断电或强制结束后能够恢复到最近一次完整 generation；
- 损坏 Chunk 不会静默覆盖原文件；
- 保存失败有明确用户提示和诊断日志。

---

## Phase 7：真正的无限世界

预计：4 至 10 周全职，个人业余开发约 4 至 7 个月。

### 目标

让世界大小不再由初始化时的 width/depth 决定。

### 工作项

- 去除固定大数组世界假设；
- 完成负坐标；
- 完成 InterestPoint；
- 完成 Chunk streaming；
- 完成 world epoch；
- 完成 region storage；
- 完成边界结构规划；
- 处理跨 Chunk 光照；
- 处理跨 Chunk 流体；
- 处理跨 Chunk 红石；
- 处理跨 Chunk 实体；
- 加入 spawn point 和 world border 配置；
- 加入远处实体存储策略。

### 验收

- 世界可以移动到负坐标；
- 走出初始区域后仍能生成和保存；
- 远离区域后内存和 GPU 资源可回收；
- 回到旧区域后数据一致；
- 快速长距离移动不会积压无限任务；
- 世界大小不依赖一次性分配；
- 长时间运行不会出现 Chunk、Entity、QML 对象泄漏。

---

## Phase 8：无头服务器和多人联机

预计：8 至 16 周全职，个人业余开发约 6 至 12 个月。

### 目标

客户端和服务器使用同一套模拟核心。

### 工作项

- 建立 voxel_server；
- 将 Simulation Core 与 Qt GUI 完全分离；
- 定义 Command、Event、Snapshot 协议；
- 增加 server tick；
- 增加玩家认证和 session；
- 增加 Chunk streaming；
- 增加快照压缩；
- 增加客户端预测；
- 增加 reconciliation；
- 增加服务器存档；
- 增加断线重连；
- 增加权限和命令系统；
- 增加网络故障测试。

### 第一版范围

第一版联机只实现：

- 2 至 4 名玩家；
- 玩家移动；
- 方块破坏和放置；
- 基础实体同步；
- 服务器保存；
- 断线重连。

不要第一版就实现所有红石、复杂 AI、跨维度和大规模实体同步。

### 验收

- 服务器不链接 QML 和 Quick 3D；
- 两个客户端可以共享同一世界；
- 客户端不能通过直接修改本地 WorldState 绕过服务器校验；
- 丢包、延迟和乱序有测试；
- 服务器重启后世界可恢复；
- 单机模式也通过同一套 Command/Simulation 路径运行。

---

## Phase 9：移动端和低规格设备

预计：4 至 8 周全职，个人业余开发约 3 至 6 个月。

### 工作项

- 完成 PlatformAdapter；
- 完成 Mobile profile；
- 完成触摸到 InputAction；
- 完成暂停/恢复；
- 完成低内存回调；
- 完成 GPU capability detection；
- 完成纹理压缩和资源分档；
- 完成低质量渲染；
- 完成 Android 构建和部署；
- 完成低端设备 benchmark；
- 完成后台保存。

### 验收

- 启动、进入世界、保存、恢复完整；
- 触摸输入不绕过 Command；
- 切到后台不丢最近一次保存；
- GPU 资源可以重新创建；
- 低规格设备不会因为实体或 Chunk 数量失控；
- 默认质量配置不会造成不可接受的卡顿。

---

## 9. 第一个月的具体执行计划

如果现在就开始，建议只做下面这些事情。

### 第 1 周：基线

- 固定当前 baseline；
- 记录启动、世界生成、首帧、保存和测试时间；
- 保存一份默认世界；
- 运行 git diff --check；
- 为当前矩阵增加总耗时和阶段耗时；
- 加入启动 smoke test；
- 加入 fixed-seed checksum。

### 第 2 周：基础类型

- 建立 voxel_base；
- 实现 ChunkKey；
- 实现 BlockPos；
- 实现 Tick；
- 实现 EntityId；
- 实现 Result/Error；
- 为负坐标写测试；
- 不改变现有玩法。

### 第 3 周：GameSession

- 建立 GameSession；
- 建立 Command；
- 将一个最小动作改成 Command；
- 先迁移 BreakBlock；
- World 仍由旧实现执行；
- 通过 Adapter 连接 QML；
- 增加 Command 测试。

### 第 4 周：WorldDelta

- 建立 EditBuffer；
- 把一个 Tick 内多次 setBlock 合并；
- 输出 DirtyChunkSet；
- 保持旧 worldChanged 兼容；
- 对比重构前后的 mesh 重建次数；
- 确认没有行为回归。

第一个月结束时，不应该出现“玩家看不到区别但底层已经全部重写”的状态。应该得到一条可验证的新数据流：

    QML action
      → Command
      → GameSession
      → World edit
      → WorldDelta
      → render invalidation

---

## 10. 当前文件到目标 Module 的迁移映射

| 当前文件/区域 | 目标归属 | 迁移策略 |
|---|---|---|
| src/World/world.h/cpp | WorldState、WorldFacade、多个 SimulationSystem | 先保留 World 作为 facade，再逐步抽实现 |
| src/World/chunk.h/cpp | voxel_world | 先加 ChunkKey、revision、lifecycle |
| src/World/chunkmanager.* | ChunkStore、ChunkScheduler | vector 逐步改为 keyed store |
| src/World/chunkgeometry.* | MeshBuilder、QtQuick3DAdapter | 先拆 CPU mesh，再拆 Qt 对象 |
| src/World/worldstore.* | SQLiteAdapter、SaveCoordinator | 先抽 interface，再迁移事务 |
| src/Entities/entitymanager.* | EntityStore、EntitySystems | 先稳定 EntityId，再拆系统 |
| src/Game/playercontroller.* | PlayerSimulation、PlayerInteraction | QML 入口保留 adapter |
| src/ui/Main.qml | UI、GameSessionAdapter、WorldViewport | 先按职责拆文件，再减少 C++ 直连 |
| tools/redstone_matrix_test.cpp | 多个测试 target | 先切注册表，再切编译目标 |
| CMakeLists.txt | 多个 CMake target | 先增加 library target，再迁移源文件 |
| src/Audio/miniaudio.h | AudioAdapter | 保留第三方实现，但隔离到 Platform/Audio |

迁移期间允许保留 LegacyAdapter，但 LegacyAdapter 必须：

- 有明确名称；
- 有删除计划；
- 不成为新代码的默认依赖；
- 有回归测试；
- 不跨线程传递内部指针。

---

## 11. 关键接口草案

下面的接口是方向性设计，不要求一次性照抄实现。

### GameSession

职责：

- 接收 Command；
- 驱动固定 Tick；
- 维护 Simulation；
- 输出 Event 和 Snapshot；
- 不了解 QML。

调用者只需要知道：

- submit；
- advance；
- latestSnapshot；
- drainEvents；
- state。

### ChunkScheduler

职责：

- 接收 Load、Generate、Mesh、Save 请求；
- 处理优先级；
- 处理取消；
- 处理 epoch；
- 输出结果。

调用者不需要知道：

- 使用 QThreadPool 还是 std::jthread；
- 使用多少 worker；
- 队列使用什么容器；
- 如何合并重复请求。

### RendererBackend

职责：

- 初始化图形资源；
- 接收 RenderSnapshot；
- 上传 MeshData；
- 回收 GPU 资源；
- 输出 RenderStats。

不得暴露：

- World 指针；
- EntityStore 指针；
- QML 对象；
- Save 方法。

### SaveCoordinator

职责：

- 请求保存；
- 生成 snapshot；
- 组织写入；
- 提交 generation；
- 恢复和迁移；
- 报告成功、失败和恢复状态。

调用者不需要知道：

- 当前是 SQLite 还是 Region 文件；
- 是否经过 journal；
- 是否压缩；
- 如何重试；
- 事务如何拆分。

---

## 12. 性能和容量验收目标

这些不是绝对承诺，而是重构过程中必须持续测量的目标。

### 单机桌面

- 默认世界玩法不回退；
- 普通方块编辑不产生明显长帧；
- 世界生成不阻塞 GUI；
- 单次 mesh job 不在 GUI 执行；
- 远端 Chunk 不持有不必要 GPU 资源；
- 1000 个实体不等于 1000 个 QML Delegate；
- 保存过程不长时间阻塞输入；
- 长时间运行后对象和内存稳定。

### 无限世界

- 负坐标可用；
- 世界驻留内存由预算控制；
- 访问过的区域可卸载；
- 回到旧区域数据一致；
- 生成任务可取消；
- 过期任务结果不会污染新世界；
- Region 文件可独立校验和恢复。

### 服务器

- 服务器没有 GUI 依赖；
- 固定 20 Tick；
- 命令可重放；
- Snapshot 可压缩；
- Chunk 可以按兴趣点发送；
- 服务器重启可恢复；
- 丢包和延迟有自动化测试。

### 移动端

- 可检测 GPU 和内存能力；
- 可切换 profile；
- 后台/恢复安全；
- 触摸输入与桌面输入走同一套 Command；
- 低规格设备不会加载桌面级默认资源。

---

## 13. 风险清单

| 风险 | 严重度 | 缓解 |
|---|---|---|
| 一次性重写导致行为回归 | 高 | Adapter、双路径、阶段性验收 |
| worker 读取过期 Chunk | 高 | snapshot、revision、epoch |
| 异步生成结果污染新世界 | 高 | world epoch、取消和丢弃 |
| GPU 资源被错误线程操作 | 高 | Renderer 独占 GPU owner |
| 多个系统直接写 World | 高 | EditBuffer、Tick 末统一应用 |
| save/player/progress 不一致 | 高 | SaveGeneration、manifest、journal |
| 坏 Chunk 被零数据覆盖 | 高 | backup、checksum、拒绝静默覆盖 |
| QML Delegate 数量失控 | 高 | RenderSnapshot、批量呈现 |
| 完整测试运行时间越来越长 | 中 | CTest 分层、smoke、nightly |
| 计划过大导致长期没有可玩版本 | 高 | 每阶段保持旧玩法可运行 |
| Qt Quick 3D 与自研 RHI 双线失控 | 中 | RendererBackend seam，先只保留一个生产默认 |
| 多人联机过早开始 | 高 | 先完成 headless deterministic core |
| 移动端最后才发现资源不适配 | 中 | Phase 0 建立 Mobile profile 和预算 |
| 文档与实际测试数字漂移 | 中 | 从测试输出生成报告 |

---

## 14. 明确禁止的做法

以下做法会让重构方向重新失控：

1. 不要让 worker 直接调用 World::setBlock。
2. 不要让 worker 创建 QQuick3DGeometry。
3. 不要把 QML 属性当成模拟状态。
4. 不要为每个实体继续创建永久 QML Delegate。
5. 不要让服务器复用 Main.qml。
6. 不要把 SQLite connection 在线程之间共享。
7. 不要继续通过全局 QObject singleton 隐式传递游戏状态。
8. 不要为每个方块修改触发一次完整的渲染信号风暴。
9. 不要在没有 revision 的情况下接受异步任务结果。
10. 不要在没有 backup 的情况下自动迁移用户存档。
11. 不要先实现联机，再补确定性 Tick。
12. 不要先实现无限世界，再设计 Chunk lifecycle。
13. 不要为了“代码看起来更现代”而无收益地引入完整 ECS。
14. 不要在一个超大提交中同时修改 worldgen、renderer、存档和实体系统。
15. 不要用“测试矩阵最后跑过了”替代每个 Module 的快速测试。

---

## 15. 每个阶段的完成定义

一个阶段只有同时满足下面条件才算完成：

- 目标功能可运行；
- 旧功能没有已知高严重度回归；
- 有对应自动化测试；
- 有性能数据；
- 有线程和所有权说明；
- 有失败和取消路径；
- 有日志；
- 有文档；
- 有明确的回滚方式；
- 没有把临时兼容代码伪装成最终架构；
- 旧路径和新路径的职责边界已经写清楚。

特别是异步系统，必须把以下情况测试出来：

- 世界切换；
- 玩家快速移动；
- 退出时仍有任务；
- Chunk 被卸载后任务完成；
- seed 改变后旧任务完成；
- save 期间发生编辑；
- renderer 重建；
- 应用挂起；
- 网络断开。

---

## 16. 推荐的提交和分支策略

### 分支

建议：

- refactor/base-types；
- refactor/session；
- refactor/chunk-lifecycle；
- refactor/async-generation；
- refactor/async-meshing；
- refactor/entity-store；
- refactor/persistence；
- feature/server；
- feature/mobile。

### 每个提交只完成一类变化

推荐提交顺序：

1. 新增基础类型和测试；
2. 新增接口，不改变旧实现；
3. 新增 Adapter；
4. 将一个调用点切到新接口；
5. 增加新旧结果对比；
6. 扩大迁移范围；
7. 删除旧路径；
8. 更新文档和 benchmark。

### 合并前最低检查

- 编译；
- 快速 CTest；
- 旧存档 load；
- 固定 seed checksum；
- QML 启动 smoke；
- git diff --check；
- 线程相关测试；
- 资源和部署检查。

---

## 17. 最终架构的判断标准

重构成功后，应该能够回答“是”：

- 能否在没有 QML 的情况下跑一局世界模拟？
- 能否在没有 Qt Quick 3D 的情况下生成和保存 Chunk？
- 能否把 Qt Quick 3D 替换为另一个 Renderer Adapter？
- 能否让世界在后台生成而 GUI 保持响应？
- 能否让远端 Chunk 卸载而不丢数据？
- 能否让 1000 个实体存在但只呈现可见实体？
- 能否让服务器复用客户端的模拟规则？
- 能否让移动端输入复用同一套 Command？
- 能否在坏存档时恢复而不是静默覆盖？
- 能否用单个 replay 文件复现一个模拟 bug？
- 能否用一个小测试 target 在几秒内验证一个 Module？
- 能否在不改 WorldState 的前提下替换存储和渲染？

如果这些问题中大多数仍然只能回答“需要访问 Main.qml、QQuick3D 对象或某个全局 QObject 才能做到”，说明重构还没有完成核心目标。

---

## 18. 最终建议

最明确的方向是：

> 先把项目变成一个可以无头运行的确定性 C++ 游戏模拟器，再把 QML、Qt Quick 3D、SQLite、音频、网络和移动端逐一接到这个核心上。

具体优先级：

1. 基线和测试分层；
2. 基础类型与固定 Tick；
3. GameSession、Command、Event、Snapshot；
4. Chunk lifecycle；
5. 后台生成；
6. 后台 MeshBuilder；
7. EntityStore 与批量呈现；
8. Region 存储和保存 generation；
9. 无头服务器；
10. 多人联机；
11. 移动端；
12. 最后再做更大范围的玩法扩展。

不要把“重构完成”定义为目录变多、类变多或代码行数变少。真正的完成标准是：核心规则不依赖 UI 和渲染，重 CPU 工作不依赖 GUI 线程，世界和实体规模不再受当前对象模型硬限制，单机和服务器可以共享同一套模拟。

---

## 附录 A：现阶段可以保留的内容

以下内容没有必要因为重构而全部重写：

- BlockRegistry 的规则表；
- 已验证的方块状态和掉落逻辑；
- 大部分 worldgen 数学函数；
- 已验证的生物行为；
- 现有材质和纹理资源；
- miniaudio 作为 AudioAdapter 的底层实现；
- FrameProfiler 的观测思想；
- F3 的调试信息；
- 现有行为矩阵中的业务断言；
- SQLite 作为迁移期的小世界存储；
- Qt Quick 3D 作为过渡渲染后端。

应该重写的是职责和数据流，不是已经稳定的每条游戏规则。

## 附录 B：第一批真正值得提取的 Module

如果只能做五个提取，优先做：

1. ChunkKey / BlockPos / Tick；
2. GameSession；
3. WorldFacade；
4. MeshBuilder；
5. SaveCoordinator。

如果只能做一项，先做 GameSession + Command + WorldDelta。

它能为后续后台生成、多人联机、无头测试和移动端输入同时提供 Seam，是整个重构中 Leverage 最高的一步。

---

## 19. 与本项目最匹配的连续 Agent 开发工作流

本节不是普通团队的 Scrum 计划，而是针对当前项目实际情况设计的“账本驱动、单任务串行、可中断、可恢复、可长期连续推进”的 Agent 工作流。

### 19.1 从最近 100 条 Git 提交看到的真实工作流

截至 2026-09-08，最近 100 条提交大致呈现出以下结构：

| 提交类型 | 数量 | 说明 |
|---|---:|---|
| fix | 36 | 实际代码修复、机制实现和缺陷收口 |
| test | 23 | 行为探针、负向轮、源码契约和回归测试 |
| docs | 35 | dev-plan 回标、review 结论、取舍记录和批次收口 |
| feat | 6 | 较大功能或结构新增 |

这说明你的工作方式并不是“想一个功能，写完就结束”，而是：

    一个任务
      ↓
    代码实现或修复
      ↓
    行为探针 / 真实链路测试
      ↓
    阴性轮验证测试确实能抓到回退
      ↓
    dev-plan 回标
      ↓
    独立 Git 提交
      ↓
    下一任务

最近的提交还体现出几个非常稳定的模式：

1. 任务使用 t1024、t1023 这样的稳定编号。
2. 需求批次使用 R19.21 这样的批次编号。
3. fix、test、docs 通常围绕同一个任务编号形成闭环。
4. 每个任务都记录矩阵从多少 PASS 增长到多少 PASS。
5. 很多任务都包含阴性轮，故意撤掉修复，要求恰好出现预期失败。
6. 复杂问题先调查，假设被证伪后登记为“不盲修”。
7. 目视验证与自动化验证分开记录。
8. review 不是只看代码，而是同时看实现、测试、文档和取舍。
9. 批次完成后继续自动提出下一批任务。
10. 用户保留随时叫停权，但平时不需要逐项审批。

当前 t1025 的工作区状态也说明了这一点：EntityManager、PlayerController 和矩阵测试已有未提交修改，而最近提交停在 t1024 完成处。Agent 恢复时必须先识别这些未提交修改是否就是 t1025 的半成品，不能直接从 dev-plan 的下一行开始覆盖工作。

### 19.2 你现在的工作流本质上是什么

你的工作流可以正式命名为：

> Ledger-Driven Autonomous Sequential Development  
> 账本驱动的自主串行连续开发

它由四个持久化层组成：

| 层 | 持久化内容 | 作用 |
|---|---|---|
| dev-plan.md | 需求、任务、背景、取舍、验收、下一批 | 任务账本和产品记忆 |
| Git history | 每次 fix/test/docs 的不可变历史 | 实现记忆和恢复依据 |
| 测试矩阵日志 | PASS、FAIL、阴性轮、回归证据 | 行为记忆 |
| 工作区状态 | 未提交修改、当前构建产物、运行日志 | 当前会话现场 |

Agent 断链、API 限额、机器重启或上下文丢失后，只要这四层没有被破坏，就可以恢复。

### 19.3 这个工作流的真正优点

这个工作流有五个很强的优点：

#### 1. 中断成本低

每个任务都有编号、提交和文档回标，不需要依赖上一轮上下文。

#### 2. 进度不会只存在 Agent 的记忆中

即使 Agent 换模型、换窗口、换机器，Git 和 dev-plan 仍然保留事实。

#### 3. 适合长周期个人项目

你不需要一次性规划几个月的所有细节，只需要始终保证“当前任务完成后，下一任务可被发现”。

#### 4. 验收成本低

你不必参与每次函数修改，只需要关注：

- 重要功能的实际试玩；
- review 中需要用户决定的取舍；
- 发布前完整回归。

#### 5. 能够不断吸收经验

测试失败、假红、地形依赖、时间戳错位、源码钉误报等经验都会进入 dev-plan，后续 Agent 能够继承，而不是重复踩坑。

### 19.4 现有工作流需要补的四个缺口

当前流程已经有效，但如果要“无限开发”，还需要补四个控制面：

1. 当前任务状态必须机器可读；
2. 每次中断前必须有明确的恢复点；
3. 失败必须有重试上限和升级规则；
4. 下一任务必须由依赖和质量状态共同决定，而不是只读取文件最后一行。

否则长期运行会出现：

- Agent 重复实现已经完成的任务；
- Agent 把未完成任务误判为已完成；
- Agent 在旧二进制上宣称新源码测试通过；
- Agent 反复修同一个失败；
- dev-plan 的历史越来越长，启动时读取成本越来越高；
- 代码、测试和文档三者出现不一致；
- 工作区已有用户修改时被误覆盖。

---

## 20. dev-plan 应该如何升级为 Agent 控制面

### 20.1 保留 dev-plan 的历史价值

不要把现有 dev-plan.md 全部改成冷冰冰的 YAML 或 JSON。

当前 dev-plan 的长篇调查、取舍和测试证据很有价值，适合人阅读，也适合 Agent 了解历史背景。它应该继续作为：

- 产品规格；
- 任务历史；
- 研究记录；
- review 台账；
- 失败经验库；
- 未来任务池。

### 20.2 增加一个小型 Current Control Block

建议在 dev-plan.md 文件开头增加一个固定格式的“当前控制块”，并要求 Agent 每次任务结束时更新它。

如果将来愿意增加一个独立文件，最优形式是：

    docs/agent-state.md

该文件只保存当前状态，不保存历史长文。Agent 每次启动先读取它，再读取 dev-plan.md 的相关任务区和尾部。

如果希望所有状态仍然只保留在 dev-plan.md，则把同样的控制块放在文件最顶部。

建议字段：

    Control version: 1
    Last updated: 2026-09-08T20:00:00+08:00
    Current batch: R19.21
    Current task: t1025
    Task state: IN_PROGRESS
    Task owner: glm5.3flash
    Baseline commit: e0e8590
    Last verified commit: e0e8590
    Last verified matrix: 483 PASS / 0 FAIL
    Last verified binary: build/redstone_matrix_test.exe
    Binary/source alignment: STALE
    Working tree: DIRTY
    Next action: inspect t1025 uncommitted changes and finish or recover
    Human decision required: false
    Manual checks pending: t1024 bed / t1025 breeding
    Blocker: none
    Retry count: 0
    Last failure class: none

字段值必须使用固定枚举，避免 Agent 写出十几种同义词：

任务状态：

- READY
- CLAIMED
- IN_PROGRESS
- VERIFYING
- DOC_UPDATING
- COMMITTED
- BLOCKED
- NEEDS_HUMAN
- PAUSED
- CANCELLED

工作区状态：

- CLEAN
- DIRTY_OWNED
- DIRTY_EXTERNAL
- DIRTY_MIXED

验证状态：

- VERIFIED
- STALE_BINARY
- NOT_RUN
- FAILED
- FLAKY
- MANUAL_PENDING

### 20.3 控制块的更新规则

Agent 不得在任务真正完成前把状态写成 COMMITTED。

推荐状态变化：

    READY
      ↓
    CLAIMED
      ↓
    IN_PROGRESS
      ↓
    VERIFYING
      ↓
    DOC_UPDATING
      ↓
    COMMITTED
      ↓
    READY for next task

异常路径：

    IN_PROGRESS → PAUSED
    VERIFYING → FAILED
    FAILED → IN_PROGRESS
    FAILED → BLOCKED
    任何状态 → NEEDS_HUMAN

每次状态变化至少更新：

- 时间；
- 当前 commit；
- 工作区状态；
- 最后一个已验证 commit；
- 下一步动作；
- 是否需要用户决策。

这样即使 Agent 在写文件或运行测试时断链，下一次也能判断当前停在哪个阶段。

---

## 21. 每一个任务的标准生命周期

一个任务不能只写成“实现动物繁殖”或“修复矿车问题”。必须经过下面的固定流程。

### Step 0：恢复现场

Agent 启动后首先执行只读检查：

1. 读取 agent-state 或 dev-plan 当前控制块；
2. 读取当前任务的完整条目；
3. 读取 dev-plan 最近一批任务；
4. 查看 git status；
5. 查看最近 10 条 Git 提交；
6. 确认当前 branch；
7. 检查源码和测试二进制时间戳；
8. 检查是否有残留构建或测试进程；
9. 判断未提交修改属于自己、用户还是未知来源。

此时不应该立即编辑代码。

### Step 1：认领任务

只有满足以下条件才能把任务从 READY 改为 CLAIMED：

- 任务有明确验收标准；
- 依赖任务已经完成；
- 当前工作区没有无法解释的冲突；
- 没有另一个 Agent 正在修改同一批文件；
- 任务规模能够在一个连续工作单元中完成；
- 不需要用户做未解决的产品决定。

认领时记录：

- 任务编号；
- 认领时间；
- Agent 标识；
- 起始 commit；
- 起始矩阵基线；
- 预计影响文件；
- 当前分支。

### Step 2：建立任务契约

在开始改代码前，Agent 必须在任务条目中补全：

- Problem；
- Goal；
- Non-goals；
- Invariants；
- Allowed files；
- Expected behavior；
- Acceptance tests；
- Negative test；
- Manual check；
- Rollback plan。

如果这九项无法写清楚，任务不能进入实现阶段。

### Step 3：先做根因分析

Agent 必须先回答：

1. 用户看到的症状是什么；
2. 哪个 Module 拥有该行为的权威；
3. 当前路径是否有多个实现；
4. 失败属于规则错误、数据错误、呈现错误、测试错误还是环境错误；
5. 是否有可能只是测试 rig 假红；
6. 是否有反例可以证伪当前假设。

禁止“先猜一个地方改了再看”作为默认流程。

### Step 4：实现最小修改

修改必须满足：

- 只触碰任务需要的文件；
- 尽量只改变一个权威路径；
- 不为了方便复制第二套规则；
- 新增测试 seam 时说明生产路径零调用；
- 新增常量时说明单一来源；
- 不顺手重排无关代码；
- 不将历史注释大规模改写成与任务无关的内容。

### Step 5：先跑最小验证

按从快到慢的顺序：

1. 静态检查；
2. 相关 Module 的快速测试；
3. 当前任务正向行为测试；
4. 当前任务负向轮；
5. 受影响的历史回归测试；
6. 完整矩阵；
7. 启动 smoke test；
8. 必要时手工试玩。

不能只跑完整矩阵而跳过快速测试，因为完整矩阵失败时定位成本很高。

### Step 6：阴性轮

每个行为修改原则上都要有一个阴性验证：

- 临时撤掉关键条件；
- 或临时恢复旧错误逻辑；
- 或关闭新 seam；
- 或使用一个有意错误的参数；
- 期待当前任务指定的测试恰好失败；
- 恢复修改；
- 再次运行，恢复为全绿。

阴性轮不是为了增加 PASS 数，而是证明测试确实敏感。

如果无法做阴性轮，任务条目必须解释原因，例如：

- 纯文档修改；
- 纯资源修改；
- 只改变日志文案；
- 依赖真实 GPU 或人工听感；
- 阴性轮会破坏不可逆的外部状态。

### Step 7：检查工作区和构建一致性

验证前必须确认：

- 测试源码没有晚于测试二进制；
- 当前 binary 是本次源码构建出来的；
- 构建日志没有被旧文件冒充；
- 运行的进程是本次启动的进程；
- 没有把用户已有的进程或文件当成本次结果；
- 测试日志记录了退出码。

这一条对当前项目尤其重要，因为测试源码经常比 build 目录下的测试 exe 更新。

### Step 8：更新 dev-plan

只有在代码、测试和人工检查状态明确后，才回标任务：

- 实现了什么；
- 根因是什么；
- 修改了哪些文件；
- 通过了哪些测试；
- 阴性轮结果；
- 是否有 flaky；
- 哪些内容只做了自报；
- 哪些内容仍待用户目视；
- 下一任务是什么。

必须区分：

- independently verified；
- agent self-reported；
- manual pending；
- not run。

不能把 self-reported 写成 independently verified。

### Step 9：提交 Git

推荐每个任务使用以下提交组：

    fix(tXXXX): implementation
    test(tXXXX): behavior and regression probes
    docs(plan): close tXXXX with evidence

如果任务是纯测试或纯文档，可以减少提交数量，但提交主题必须仍然带任务编号。

### Step 10：写恢复点

提交后必须写：

- task state；
- latest commit；
- last verified commit；
- matrix result；
- current binary alignment；
- next task；
- manual pending；
- blocker；
- retry count。

### Step 11：准备下一任务

Agent 应该把下一项任务标成 READY，但不要在同一提交中偷偷开始下一项实现。

这样“下次自动继续”能够从一个干净边界开始。

### Step 12：安全停止

任务完成、API 快用尽、上下文接近上限或用户要求暂停时，都应该停在以下任一安全点：

- 提交完成；
- 测试完成但 docs 尚未更新；
- 实现完成但测试失败且已记录；
- 实现中断且控制块已记录当前文件和下一步；
- 明确 NEEDS_HUMAN。

绝不能只留下“代码改了一半，但 dev-plan 仍写 READY”。

---

## 22. Agent 每次启动时的恢复协议

下面是适合当前 GLM5.3Flash Agent 的固定启动顺序。

### 22.1 启动读取顺序

每次启动必须按这个顺序阅读：

1. docs/agent-state.md，如果存在；
2. docs/dev-plan.md 的当前控制块；
3. 当前任务完整描述；
4. 当前批次的依赖任务；
5. dev-plan 文件末尾最近记录；
6. git status；
7. git log 最近 10 条；
8. 最近构建和测试日志；
9. 当前源码与 binary 的时间戳。

不要每次从 dev-plan.md 第一行读到最后一行。历史文档已经超过百万字节，长期这样会浪费上下文，并增加误读概率。

### 22.2 恢复决策树

Agent 读取状态后只允许选择以下五种动作：

#### CONTINUE_IMPLEMENTATION

条件：

- 当前任务 IN_PROGRESS；
- 未提交修改属于当前任务；
- 没有未解决编译或测试失败；
- 下一步明确。

动作：

- 保留修改；
- 从控制块的 next action 继续；
- 不重做已经验证的部分。

#### RETRY_VERIFICATION

条件：

- 实现已经完成；
- 测试失败属于环境、旧 binary 或 flaky；
- 失败次数尚未达到上限。

动作：

- 先构建新 binary；
- 再重复最小测试；
- 不直接修改生产代码。

#### RECOVER_WORKTREE

条件：

- 工作区 dirty；
- 文件归属不明确；
- 当前 binary 和源码不一致；
- 存在用户外部修改。

动作：

- 先建立状态报告；
- 逐文件识别；
- 不使用破坏性 Git 命令；
- 必要时请求用户决定。

#### CONTINUE_NEXT_TASK

条件：

- 当前任务已有 fix/test/docs 完整提交；
- 控制块已写成 COMMITTED；
- 当前工作区干净或只有明确的外部修改；
- 下一任务依赖满足。

动作：

- 认领下一个 READY 任务；
- 不回头重复旧任务。

#### NEEDS_HUMAN

条件：

- 需求有多种产品解释；
- 需要批准存档迁移；
- 需要判断视觉、听感或玩法口径；
- 连续三次同根因失败；
- 发现不属于本任务的高风险问题；
- 工作区存在无法归属的用户修改。

动作：

- 停止修改；
- 写清证据和选择；
- 等用户决定。

### 22.3 断链恢复的最低信息

如果 Agent 可能马上达到 API 限额，必须优先写入以下信息，再继续耗时测试：

- 当前任务；
- 当前阶段；
- 已修改文件；
- 已完成的实现点；
- 尚未完成的实现点；
- 当前失败；
- 下一条命令或下一步动作；
- 是否可以安全重试；
- 是否需要保留未提交修改。

这是最重要的“断链保险”。

---

## 23. 适合无限连续开发的任务账本结构

### 23.1 任务不能只有标题

建议每项任务采用下面的固定模板：

    **tXXXX 任务标题**：一句话描述用户可见目标。

    状态：READY / IN_PROGRESS / DONE / BLOCKED / NEEDS_HUMAN
    批次：Rxx.xx
    优先级：P0 / P1 / P2 / P3
    依赖：无 / tXXXX / Rxx.xx
    所属 Module：World / Entities / Renderer / Persistence / UI / Test
    所有者：glm5.3flash

    背景：
    用户症状、规格来源、当前实现和历史原因。

    目标：
    这项任务完成后必须成立的行为。

    非目标：
    本项明确不修改的内容。

    权威路径：
    哪个 Module 是唯一规则来源。

    不变量：
    修改后必须保持的规则、边界和线程约束。

    允许触碰：
    预期文件或目录。

    禁止触碰：
    与本任务无关的文件或逻辑。

    验收：
    1. ...
    2. ...
    3. ...

    正向测试：
    测试名称、输入、预期结果。

    阴性测试：
    撤掉哪个条件后，哪一条测试必须失败。

    回归测试：
    需要重跑哪些历史任务。

    人工检查：
    需要用户目视、听感或实际试玩的内容。

    回滚：
    失败时如何只撤回本任务。

    完成记录：
    fix commit / test commit / docs commit / matrix / logs。

    下一任务：
    下一项 READY 任务及原因。

### 23.2 任务规模控制

为了适应单 Agent 和 API 中断，一个任务最好满足：

- 一个主要用户行为；
- 一个主要权威 Module；
- 1 至 5 个主要生产文件；
- 1 至 3 个测试文件；
- 可以在一次连续 Agent 工作单元中完成；
- 有明确的正向和负向验证；
- 不同时做两个互相独立的大主题。

以下任务太大，必须拆开：

- “完成整个异步渲染系统”；
- “重写所有实体”；
- “支持多人联机”；
- “把 World 拆成模块”；
- “完成移动端”。

正确拆法应是：

- 先建立接口；
- 再迁移一个调用点；
- 再迁移一个数据流；
- 再增加测试；
- 再删除旧路径。

### 23.3 任务依赖图

任务不能只按 dev-plan 的文本顺序执行，还要有依赖关系：

    R20.01 控制块
      ↓
    R20.02 基线和 smoke
      ↓
    R20.03 测试分层
      ↓
    R20.04 基础类型
      ↓
    R20.05 Command / Event / Snapshot
      ↓
    R20.06 GameSession
      ↓
    R20.07 WorldFacade
      ↓
    R20.08 EditBuffer
      ↓
    R20.09 ChunkKey / lifecycle
      ↓
    R20.10 同步 ChunkScheduler
      ↓
    R20.11 后台 GenerationJob
      ↓
    R20.12 MeshBuilder
      ↓
    R20.13 EntityStore
      ↓
    R20.14 SaveCoordinator
      ↓
    R20.15 headless server

Agent 每次选择下一任务时必须检查：

1. 依赖是否 DONE；
2. 当前任务是否有用户待决策；
3. 当前工作区是否适合修改；
4. 是否存在更高优先级的失败修复；
5. 是否会和当前未提交修改冲突；
6. 是否完成了前置质量门。

---

## 24. fix / test / docs 提交闭环的正式规范

### 24.1 fix 提交

fix 提交只包含：

- 生产代码；
- 必要的生产注释；
- 必要的接口或配置；
- 不包含大规模历史文档改写；
- 不包含无关格式化。

提交主题：

    fix(t1025): animal breeding and food lure behavior

提交正文回答：

- 根因是什么；
- 采用哪个权威路径；
- 为什么没有修改其他路径；
- 是否增加了测试 seam；
- 是否有已登记的边界。

### 24.2 test 提交

test 提交只包含：

- 正向行为；
- 阴性轮；
- 回归测试；
- 测试 rig；
- 诊断日志；
- 必要测试辅助。

测试提交正文必须写：

- 新增了几条 PASS；
- 阴性轮预期几条 FAIL；
- 实际几条 FAIL；
- 是否存在 flaky；
- 测试是否独立验证；
- binary 和 source 是否对齐。

### 24.3 docs 提交

docs 提交负责：

- 完成任务状态；
- 更新矩阵基线；
- 记录证据；
- 记录人工待验；
- 记录取舍；
- 写下一任务；
- 清理已经失效的 TODO。

文档提交不能把未运行的测试写成通过。

### 24.4 何时允许只有一个提交

以下情况可以只有一个提交：

- 纯文档；
- 纯测试；
- 纯资源；
- 非行为性的构建修复；
- 用户明确要求压缩历史。

但如果一个任务同时改生产行为和测试，默认仍采用 fix/test/docs 三提交。

---

## 25. 质量门：什么情况下 Agent 可以自动继续

### 25.1 自动继续条件

Agent 可以自动继续下一个任务，必须全部满足：

- 生产代码已提交；
- 任务测试已通过；
- 阴性轮符合预期，或已说明豁免原因；
- 受影响回归测试通过；
- git diff --check 通过；
- 当前 binary 与 source 对齐；
- 没有未解释的 warning；
- dev-plan 已回标；
- 控制块已更新；
- 没有 NEEDS_HUMAN；
- 下一个任务依赖满足。

### 25.2 自动继续不代表跳过人工验收

自动继续允许 Agent 处理下一个开发任务，但人工待验必须保留：

- 视觉检查；
- 音频听感；
- 大量实体实际帧率；
- 移动端体验；
- 存档升级；
- 网络延迟；
- 破坏性迁移；
- IP/资源分发。

这些可以累积到一个人工验收清单，不必每项都打断 Agent。

### 25.3 建议人工验收节奏

你的人工操作可以压缩成三种：

#### 每 1 个任务

默认不需要人工干预，只接收 Agent 最终摘要。

#### 每 5 个任务或一个小批次

检查：

- dev-plan 状态；
- 矩阵变化；
- 是否大量出现 flaky；
- 是否出现意外的大范围文件修改；
- 是否有待决策项。

#### 每个大阶段完成

人工实际运行：

- 启动；
- 新建世界；
- 读档；
- 试玩核心流程；
- F3 观察性能；
- 检查存档；
- 检查输入；
- 检查是否出现明显视觉回归。

---

## 26. 失败、断链和 API 限额的恢复机制

### 26.1 失败分类

Agent 必须把失败归类，而不是统称“测试没过”：

| 类型 | 含义 | 首要动作 |
|---|---|---|
| BUILD | 编译或链接失败 | 只修构建根因，重跑最小 target |
| BEHAVIOR | 业务行为失败 | 回到权威路径和输入条件 |
| REGRESSION | 历史功能回退 | 比较新旧 diff，优先保护旧行为 |
| TEST_HARNESS | 测试 rig 错误 | 先修测试，不盲改生产代码 |
| FLAKY | 非确定性失败 | 记录复现率、加确定性 seam |
| STALE_BINARY | 源码和 binary 不一致 | 先全量重建 |
| ENVIRONMENT | Qt、GPU、文件、进程环境 | 记录环境，不伪造通过 |
| DOC_DRIFT | 文档和代码不一致 | 先更新事实，再决定是否改代码 |
| PRODUCT_DECISION | 规格有歧义 | NEEDS_HUMAN |
| EXTERNAL_CHANGE | 用户或其他 Agent 改动 | 停止覆盖，重新归属 |

### 26.2 重试上限

推荐规则：

- 同一命令最多自动重试 2 次；
- 同一根因最多自动修复 2 轮；
- 第 3 次相同失败必须进入 BLOCKED 或 NEEDS_HUMAN；
- flaky 至少记录 5 次运行结果；
- 测试二进制过期时不允许重复宣称失败或通过；
- 运行超过预期时间 3 倍且无进度时停止并记录 TIMEOUT。

### 26.3 不允许的恢复方式

禁止：

- 反复改常量直到测试变绿；
- 放宽测试 envelope 但不记录物理依据；
- 删除失败测试；
- 修改 PASS 文案掩盖失败；
- 用旧 binary 代替新 build；
- 用 git reset --hard 清掉未知工作区；
- 覆盖用户未提交修改；
- 把 flaky 直接标成 PASS；
- 把人工未验证写成完成。

### 26.4 API 断链恢复

如果 API 在 fix 阶段断链：

- 下一次从 IN_PROGRESS 恢复；
- 先检查当前 diff；
- 不重新生成已经存在的代码；
- 只完成当前任务；
- 任务未验证前不推进下一任务。

如果 API 在 test 阶段断链：

- 下一次优先确认 binary 是否新建；
- 如果日志完整，复用已有结果；
- 如果日志不完整，重跑最小测试；
- 不直接认为测试通过。

如果 API 在 docs 阶段断链：

- 代码和测试可能已经完成；
- 状态保持 DOC_UPDATING；
- 下一次只更新文档和控制块；
- 不重复修改生产代码。

如果 API 在 commit 前断链：

- 先查看 git diff；
- 如果修改属于当前任务，继续当前任务；
- 如果无法判断，进入 RECOVER_WORKTREE；
- 不自动提交包含未知内容的混合 diff。

---

## 27. 当前项目的“无限开发”循环

### 27.1 无限不是无限重试

“无限开发”应该表示：

    当前任务完成
      ↓
    质量门通过
      ↓
    写入持久化状态
      ↓
    选择下一个有依赖的 READY 任务
      ↓
    下次会话继续

而不是：

    一个失败任务
      ↓
    无限重试
      ↓
    无限修改测试
      ↓
    不断产生新的失败

只要每个循环都留下 Git、dev-plan、测试和恢复点，就可以跨越 API 限额无限延续；Agent 本身不需要保持永远运行。

### 27.2 双车道任务池

当前最适合采用两个逻辑车道，但仍然只允许一个 Agent 串行执行：

#### Gameplay Lane

继续承载当前 t1025、t1026、t1027、t1028、t1029 等玩法任务。

#### Architecture Lane

承载 R20.01、R20.02 等重构基础任务。

两条车道不能同时修改相同文件。

推荐调度：

1. 先完成已经开始的 t1025；
2. 完成 t1025 的 fix/test/docs 闭环；
3. 插入一个 Architecture Lane 的基础任务；
4. 再继续 Gameplay Lane；
5. 每完成 2 至 3 个玩法任务，至少完成 1 个架构基础任务；
6. 进入异步生成和 Renderer 重构后，暂时减少新增玩法任务。

这样可以保持项目持续有可玩功能，又不会让架构债无限累积。

### 27.3 下一任务选择算法

Agent 每次自动选择任务时，按以下顺序：

1. 当前任务未完成：继续当前任务；
2. 高严重度回归：优先修复；
3. 阻塞其他任务的架构前置：优先；
4. 已满足依赖的 P0/P1 任务；
5. 与当前工作区文件冲突最少的任务；
6. 可以在一轮中完成并验证的任务；
7. 最后才选择纯新增内容任务。

在以下情况停止自动推进：

- 所有 READY 任务都需要产品决定；
- 任务池只剩无法拆分的大任务；
- 连续三次质量门失败；
- 工作区混乱；
- 当前阶段性能基线明显恶化；
- 存档格式需要不可逆升级；
- 发现潜在安全、版权或数据丢失问题。

---

## 28. 适合 GLM5.3Flash 的 Agent 固定工作指令

下面的文字可以作为当前 Agent 的长期操作协议，放在它的固定提示词、项目 Agent 配置或 dev-plan 的控制规则中。

    你是 QtMinecraft 的连续开发 Agent。

    你的持久化记忆不是本轮上下文，而是：
    1. docs/agent-state.md 或 dev-plan.md 当前控制块；
    2. docs/dev-plan.md；
    3. Git 提交记录；
    4. 测试日志和构建产物；
    5. 当前工作区。

    每次启动先恢复现场，不要直接开始写代码。
    先读取当前状态、当前任务、任务依赖、git status、最近提交、
    源码与 binary 时间戳，然后选择：
    CONTINUE_IMPLEMENTATION、RETRY_VERIFICATION、
    RECOVER_WORKTREE、CONTINUE_NEXT_TASK 或 NEEDS_HUMAN。

    一次只处理一个任务。
    不要跨任务顺手实现其他需求。
    不要覆盖无法归属的用户修改。
    不要使用破坏性 Git 命令。

    每个任务必须拥有：
    背景、目标、非目标、权威路径、不变量、允许触碰、
    验收、正向测试、阴性测试、回归测试、人工检查和回滚方式。

    实现后按顺序执行：
    最小静态检查、相关测试、正向测试、阴性轮、历史回归、
    新 binary smoke test、必要的人工检查。

    不要把旧 binary 的结果归因于新源码。
    不要把 Agent 自报结果写成独立验证结果。
    不要把 flaky 写成 PASS。
    不要为了测试变绿而无依据放宽测试。
    不要删除失败测试。

    一个任务完成后，必须：
    1. 提交 fix；
    2. 提交 test；
    3. 更新 dev-plan；
    4. 更新当前控制块；
    5. 写清最后验证 commit、矩阵结果、binary 对齐状态、
       人工待验和下一任务；
    6. 将下一任务标为 READY；
    7. 在安全边界停止或继续认领下一任务。

    如果 API 限额、断链或上下文不足：
    先写恢复点，再停止。
    恢复点必须包含当前任务、阶段、已改文件、已完成内容、
    未完成内容、最后失败、下一步动作和是否需要人工决定。

    同一个根因最多自动尝试两轮。
    第三次相同失败进入 BLOCKED 或 NEEDS_HUMAN。
    发现产品歧义、数据迁移风险、未知工作区修改、潜在数据丢失、
    IP 风险或高严重度回归时，停止并报告证据。

    你的首要目标不是增加代码量，而是让：
    代码、测试、dev-plan、Git 和当前工作区保持一致。

---

## 29. 现在应该如何开工

### 29.1 第一原则：先处理当前未提交 t1025

当前工作区已经存在 t1025 相关修改迹象：

- EntityManager 增加繁殖计时、食物引诱、幼崽跟随和碰撞盒逻辑；
- PlayerController 增加繁殖食物映射和引诱门控；
- redstone_matrix_test.cpp 增加 t1025 探针；
- 当前测试 binary 的时间戳早于修改后的测试源码。

因此第一步不是直接开始 R20.01，也不是继续编辑 t1026。

第一步应该是：

1. 确认当前 diff 是否全部属于 t1025；
2. 查看是否存在遗漏的用户修改；
3. 重新构建当前测试 binary；
4. 运行 t1025 最小测试；
5. 运行 t1025 阴性轮；
6. 运行历史回归；
7. 完成 t1025 的 fix/test/docs 三段闭环；
8. 如果无法完成，记录为 IN_PROGRESS 或 BLOCKED，不要伪装完成。

### 29.2 Workflow Bootstrap：R20.01 至 R20.04

在 t1025 完成后，先建立连续开发基础设施。

#### R20.01：建立 Agent 控制块

目标：

- 在 dev-plan.md 顶部加入 Current Control Block；
- 固定任务状态枚举；
- 固定验证状态枚举；
- 固定恢复协议；
- 记录当前 t1025 和工作区状态。

验收：

- 新 Agent 可以只读控制块判断当前任务；
- 控制块和 Git HEAD 不矛盾；
- 明确 last verified commit 和 binary alignment。

提交：

    docs(workflow): add resumable agent control block

#### R20.02：建立启动 smoke 和构建一致性检查

目标：

- 检查源码时间戳和 binary 时间戳；
- 检查构建退出码；
- 启动应用并确认 root object；
- 启动测试程序并确认退出码；
- 记录日志路径。

验收：

- 旧 binary 不会被误判为新代码验证；
- 应用启动失败可以快速区分构建问题和运行问题；
- 测试超时会被明确记录。

#### R20.03：建立基线报告

目标：

- 记录默认世界生成耗时；
- 记录进入世界耗时；
- 记录首帧；
- 记录 mesh rebuild；
- 记录主线程峰值；
- 记录内存；
- 记录实体数量；
- 记录保存耗时；
- 记录当前矩阵真实运行时间。

验收：

- 有一份带 commit、seed、Qt 版本、编译器和硬件信息的 baseline；
- 后续重构可以对比；
- 性能退化有数字证据。

#### R20.04：测试矩阵拆分第一步

目标：

- 不改变测试行为；
- 按模块建立测试注册表；
- 为 World、Entity、Persistence、RenderData 建立独立入口；
- 暂时仍允许完整矩阵 target；
- 增加每组进度输出；
- 增加超时；
- 保留总 PASS/FAIL 统计。

验收：

- 单个任务不必等待整个测试程序；
- 失败能定位到测试组；
- 完整矩阵结果仍可和历史比较。

### 29.3 Architecture Lane：第一批重构任务

基础设施稳定后，开始真正的代码重构。

#### R20.05：ChunkKey、BlockPos、Tick

先不改变世界生成和玩法，只统一：

- 正负坐标；
- floorDiv/floorMod；
- ChunkKey；
- BlockPos；
- 固定 Tick；
- 基础错误类型。

验收：

- 负坐标测试通过；
- 原有默认世界结果不变；
- 新类型可以被 World 和测试使用。

#### R20.06：Command、Event、Snapshot

先只定义数据结构和队列，不大规模迁移调用点。

验收：

- BreakBlock 可以通过 Command 表达；
- WorldDelta 可以表达受影响 Chunk；
- Event 不携带 QObject；
- Snapshot 可以独立复制和测试。

#### R20.07：GameSession

把当前 QML 直接编排的最小游戏循环包起来。

第一步只迁移：

- 固定 Tick；
- 暂停；
- BreakBlock；
- PlaceBlock；
- WorldDelta。

验收：

- 无头程序可以执行一段游戏 Tick；
- QML 仍然可以通过 Adapter 玩游戏；
- 旧路径与新路径结果一致。

#### R20.08：WorldFacade

把 World 的查询和写入接口收窄。

验收：

- 新代码不再直接获取 Chunk 内部指针；
- World 规则使用统一查询；
- Renderer 不需要持有可变 World；
- 旧 World 暂时仍可作为 Implementation。

#### R20.09：EditBuffer

把一次 Tick 内的方块修改合并成 WorldDelta。

验收：

- 多次相邻编辑不会产生不必要的重复通知；
- 规则终态与旧实现一致；
- DirtyChunkSet 可被测试；
- mesh 调度不再直接绑定每一次 setBlock。

#### R20.10：Chunk lifecycle

先实现 Absent、Loading、Generated、Active、Loaded、Evicting。

验收：

- 当前固定 10×10 世界仍可运行；
- Chunk 可以卸载并重新加载；
- 没有未保存数据被静默丢失；
- QML 不再决定 Chunk 的真实生命周期。

#### R20.11：同步版 ChunkScheduler

先不引入多线程，只统一请求、优先级、取消和结果模型。

验收：

- 生成、加载、mesh 请求都有 request ID；
- 重复请求可以合并；
- 过期请求可以丢弃；
- 后续替换成 worker 不需要改调用者。

#### R20.12：后台 GenerationJob

迁移一个最小地形生成路径，不一次性迁移所有结构。

验收：

- GUI 不被整 Chunk 生成阻塞；
- seed、ChunkKey、generator version 决定输出；
- world epoch 可以丢弃旧任务；
- 退出时不会被任务卡死。

#### R20.13：MeshBuilder

先把 CPU 网格算法从 ChunkGeometry 中抽出。

验收：

- MeshBuilder 不依赖 QQuick3D；
- 输入是 snapshot；
- 输出是 owning ChunkMeshData；
- 旧 QtQuick3DAdapter 可以消费新结果；
- 网格视觉和顶点统计与旧路径一致。

#### R20.14：EntityStore

先迁移掉落物或一个简单实体族，不要同时迁移全部生物。

验收：

- EntityId 稳定；
- 模拟实体不依赖 QML；
- 可见实体由 snapshot 决定；
- 旧 EntityManager 可以作为过渡 Adapter。

#### R20.15：SaveCoordinator

先保持 SQLiteAdapter，再统一 SaveGeneration 和恢复状态。

验收：

- 保存不直接读取正在变化的 WorldState；
- 玩家、世界和进度有统一 generation；
- 保存失败不会报告成功；
- 旧档仍可读取；
- 故障注入测试可以模拟锁、磁盘错误和损坏 Chunk。

### 29.4 Gameplay Lane 的安排

当前 t1025-t1029 不应全部取消，但它们必须服从新的任务流程：

1. t1025 完成后，补齐 fix/test/docs；
2. t1026 小麦农业拆为“方块/作物/收获/配方”四个子任务；
3. t1027 掉落物 instancing 必须等待 RenderData 和 EntityStore 的 Seam；
4. t1028 音符盒先走 Event 和 AudioAdapter，不直接把音频逻辑写入 World；
5. t1029 测试缝可以先完成，但应与后续 RNG 注入设计兼容；
6. 不要为了追求玩法数量，继续把新实体和新渲染对象直接塞入 Main.qml。

---

## 30. Agent 连续开发的日常输出格式

每次 Agent 完成一个任务后，只需要向你汇报以下内容：

    任务：tXXXX
    状态：COMMITTED / BLOCKED / NEEDS_HUMAN
    结果：一句话结论
    提交：fix / test / docs
    验证：当前任务、历史回归、阴性轮
    矩阵：旧值 → 新值
    Binary：已重建 / 过期 / 未运行
    工作区：clean / dirty-owned / dirty-external / dirty-mixed
    人工待验：没有 / 列出项目
    风险：没有 / 列出项目
    下一任务：编号和一句话
    需要你决定：没有 / 给出选项

不要每次输出几百行实现细节。详细证据放到 dev-plan 和日志，聊天只报告状态。

### 30.1 用户只需要关注什么

你平时只需要检查：

1. Agent 是否真的提交了 fix/test/docs；
2. 矩阵数字是否由真实运行得到；
3. 是否出现 stale binary；
4. 是否出现越来越多的 flaky；
5. 是否有人工待验积压；
6. 是否触碰了不属于任务的文件；
7. 是否出现 BLOCKED 或 NEEDS_HUMAN；
8. 架构 Lane 是否持续推进。

如果这些都正常，你完全可以继续采用“偶尔验收”的方式，不必参与每次开发。

---

## 31. 什么时候应该暂停自动开发

即使目标是长期连续开发，也必须设置暂停条件。

### 必须暂停并让用户决定

- 新增玩法存在两个同样合理的规则口径；
- 存档格式需要不可逆变更；
- 需要批准存档迁移；
- 需要删除或迁移用户数据；
- 发现资源包或第三方资产分发风险；
- 发现主分支与用户工作区修改无法安全合并；
- 发现潜在数据丢失；
- 性能下降超过基线 20% 且根因不明；
- 阴性轮和正向轮的结果矛盾；
- 同一个失败根因连续三次复现；
- 需要改变长期架构决定；
- 需要大规模重写超过当前任务允许范围。

### 可以自动登记但不必暂停

- 低严重度注释问题；
- 已有明确取舍的视觉近似；
- 文档措辞；
- 新增一个独立低风险测试；
- 已知且已记录的 flaky；
- 不影响行为的日志字段。

---

## 32. 最终建议：把 Agent 变成“可恢复的施工队”

你现在的工作流已经具备了持续开发最难得的部分：有稳定任务编号，有 Git 历史，有测试探针，有阴性轮，有 review 回标，也有用户叫停权。

接下来不要改变这种节奏，而是加三层护栏：

1. 用 Current Control Block 记录“现在做到哪一步”；
2. 用固定任务模板记录“什么条件算完成”；
3. 用状态机和重试上限记录“什么情况下继续、重试或停下”。

这样以后即使：

- GLM5.3Flash API 限额；
- 当前会话断开；
- Agent 换模型；
- 机器重启；
- 构建过程被中断；
- 你几天不看项目；
- 任务连续开发几个月；

下一次 Agent 仍然可以从：

    控制块
      → 当前任务
      → Git 状态
      → 测试证据
      → 下一动作

恢复，而不需要你重新解释项目背景。

最重要的执行顺序是：

1. 先完成当前 t1025 的恢复和闭环；
2. 建立 R20.01 至 R20.04 的连续开发控制面；
3. 以 R20.05 的基础类型作为正式重构起点；
4. 每个重构任务继续沿用 fix/test/docs；
5. 玩法任务和架构任务双车道串行推进；
6. 只在产品决策、数据风险、连续失败和阶段验收时打扰用户；
7. 每次安全停止都留下恢复点；
8. 永远不让“下一步做什么”只存在 Agent 的上下文里。

这就是适合当前项目、也适合你现有工作方式的长期无限开发机制。

---

## §29.4 流式生成激活设计（草案 v1 —— 2026-09-16 审计 #9 ②产物，**待用户定向后才开工**）

> 状态：设计草案。R20 全弧线（后台生成 R20.12 / 生命周期 R20.10 / 请求模型 R20.11 / MeshBuilder R20.13 / EntityStore R20.14 / SaveCoordinator R20.15 / 失败恢复边 R20.10b）已为按需生成铺平全部底层件；当前固定 48×48 世界全 Loaded 稳态。本节把「激活」拆成相位与决策点，**任何一行实现都等用户 md 定向后才动**。

### D. 激活形态与决策点（用户定夺项）

| # | 决策点 | 选项 A | 选项 B | 建议 |
|---|--------|--------|--------|------|
| D1 | 世界边界 | **无限世界**（真流式） | 大固定边界（如 ±512 chunk，一次性生成上限） | A（后台件已就绪；B 只是 A 的参数化特例） |
| D2 | 激活方式 | **新世界参数开关**（旧档/旧世界行为不变） | 全量切换 | A（新旧并存，风险隔离；固定世界成为「小世界」预设） |
| D3 | 存档兼容 | 现有固定档**就地流式化**（首启把未加载区标 Absent） | 仅新档流式，旧档维持固定语义 | A（SaveCoordinator 代次机制已具备迁移载体） |
| D4 | 视距/生成半径 | 与渲染距离联动单参数 | 生成/渲染分离双参数 | B（生成半径 ≥ 渲染半径，解耦调参） |
| D5 | 卸载策略 | 视距外 **Evicting→Absent + Edits-on-evict 落盘**（R20.15 存档件已具备） | 视距外仅驻留不卸载（内存换简单） | A（Edits-on-evict 是 R20.10 登记欠账的正席） |
| D6 | worker meshing | 与流式同批排期（MeshBuilder 已抽出，输入=稠密快照的形态预演已备） | 流式先行、meshing 后续 | 用户定（D6=同批则渲染收益同期兑现，工期 +1-2 单） |

### P. 相位拆分（每相位独立 fix/test/docs 闭环，串行）

- **P1 策略层骨架**：`GenerationPolicy` 组件（Absent-only 决策：哪些 chunk 该请求/该卸载）——固定世界默认关（零变化不变量延续），参数开启才激活；纯决策无头可测。
- **P2 玩家移动驱动**：位置沿 → 视距 chunk 请求/取消（GenerationJob 三 kind 已含优先级）；生成风暴背压 = 队列满载拒绝既有语义。
- **P3 卸载 + Edits-on-evict**：视距外 Evicting→Absent，改动块经 SaveCoordinator 落盘、重载回灌（R20.10d 存档往返腿族先例）。
- **P4 QML 动态化**（**全 R20 唯一动 QML 的相位**）：ChunkGeometry Repeater 固定网格 → 动态 slot 池；QML 变更面在此相位集中、单独阴性钉守护。
- **P5 实机调参验收**：观感（视距推进/卸载无感/首屏生成等待）+ 性能（F3 生成排队/帧率）。

### R. 风险登记

- **meshing 瓶颈**：MeshBuilder 已抽纯 CPU，但仍主线程同步执行——流式后 meshing 量随视距增长，D6=同批可同期引入 worker meshing（R20.13 快照形态即为此备）。
- **生成风暴**：快速移动时请求速率 > 生成速率——队列满载拒绝 + 优先级（近处优先）已有；激活相位实测定参。
- **实体边界**：mob/掉落物在卸载 chunk 边缘的行为（despawn 半径 vs chunk 卸载竞态）——EntityStore 已给 store 级 id，边界语义在 P3 设计内补。
- **首屏等待**：出生区预生成半径即 D4 参数；Cold start 观感在 P5 验收。

### 需要用户回答的四个问题（md 修正通道，其余按建议列执行）

1. D1 无限世界？（建议：是）
2. D2 新世界参数开关、旧世界不变？（建议：是）
3. D3 现有存档就地流式化？（建议：是）
4. D6 worker meshing 同批排期？（建议：是——流式后 meshing 会成为主瓶颈）

---

## §29.5 生产接线设计（草案 v1 —— 2026-09-16 主控执笔；P1-P4/D6 组件已闭环 [矩阵 624/0]，接线是流式激活的最后一弧）

> 状态：设计草案 + 任务拆分。用户定向（2026-09-16「直接开始做这几个功能继续做」）授权流式弧；本段把「组件 → 生产」的接线拆成 W1-W5 五个串行闭环。**两个关键选型见 §29.5.3，用户可随时 md 纠偏改选**。

### 29.5.1 前置事实

- World 固定 dims（chunksX×chunksZ）构造时全量 `generate()`；blob 存整世界栅格（worldstore 冻结域）。
- 组件族已就绪且零接线：GenerationPolicy（P1）→ ChunkStreamDriver（P2，自持 GenerationJob 纯请求模型、可选 evictor/savedContent 注入点）→ ChunkEvictor（P3，persist-before-transition）→ MeshWorker（D6，快照进/网格出）。
- QML 池已数据驱动（P4：residentChunkRevision 沿 = ⑥移出/③⑧加入；枚举序 = world.cpp 权威）——**QML 侧已为无限世界就绪**。
- 未加载 chunk 的查询语义可以**零新增**：稀疏模式下「未生成/未加载」直接复用现行 OOB（越界）答案（blockAt=Air、setBlock 拒、heightAt=-1 等同门）——加载后答案变真，卸载后回到 OOB 答案。

### 29.5.2 任务拆分（串行，各 fix/test/docs 闭环）

- **W1 稀疏世界核（r2022）**：World 构造模式分化——`fixed`（现状逐位不变）/`sparse`（dims 哨兵无界；构造零全量生成；出生半径预生成 = D4 参数首次落地）。未加载域 = OOB 等价语义（29.5.1 第三条）；chunklifecycle 六态在 sparse 世界进入真实消费（构造 Absent、加载走①②）。worldstore 零触碰（sparse 世界的持久化 = W3/W5）。承重腿：**同 seed 下 sparse 世界已加载区内容 ≡ fixed 世界同区内容逐位恒等**（生成权威 terraingen 单一性直接承接）。
- **W2 位置源 + 驱动接线（r2023）**：玩家位 → floorDiv16 → GameSession tick 尾 `driver.onPlayerChunk`；GenerationJob ↔ ChunkManager `setLifecycle` 挂点首次生产接线（r2011 预留面）；BackgroundGenerationWorker 首个生产消费者（R20.12 真线程件）。streaming 仅 sparse 世界使能（D2：fixed 世界连驱动器都不构造）。
- **W3 驱逐 + Edits-on-evict 落盘（r2024）**：driver evictor 注入 → ChunkEvictor 生产缝实装（dirtyQuery = World 脏面、persistFn = §29.5.3 选型、lifecycleTransition = ChunkManager 真转移）。实体×卸载竞态语义收口（P3 登记面：despawn 半径 vs 卸载顺序，MC 口径引证随单）。
- **W4 bake → MeshWorker（r2025）**：ChunkGeometry bake 段改提交快照 + 收割应用；同步内联回退开关；F3 mesh 行加 worker 列。QML 零改动（池已动态）。
- **W5 D2 参数开关 + D3 opt-in 转换（r2026）**：新世界 UI「无限世界」开关（默认关；**QML 例外登记**——P4 相位已闭，此处为 §29.5 显式登记的最小 QML 新增面，照 P4 钉纪律守护）；世界 meta 加 streaming 标志（additive 零迁移）；D3 = 世界菜单 opt-in「转为无限世界」（blob 区成为核心区，境外按需生成 + 经 W3 落盘）。**UI 文案与默认值是产品面——用户可 md 改**。

### 29.5.3 两个关键选型（默认按推荐执行，md 可纠偏）

1. **D5 执行——per-chunk 编辑持久化载体**：(a) **新增 per-chunk 附加表**（save_coord 同款 additive 路线，零格式迁移零 bump，hard-gate 安全）【推荐】；(b) 脏 chunk 留驻至下次整存后方可逐（实现最简，但与 MC「卸载即保存脏 chunk」口径有隙，且驻留集被脏块顶住 = 驱逐退化）。→ 取 (a)；W3 实装，wstore 附加表走 r2015 additive 先例。
2. **D3 形态——opt-in 转换而非静默就地**：现有档默认永远 fixed（D2 承诺）；「转为无限世界」是显式 UI 动作（W5），转换 = meta 标志翻转 + blob 区转核心区，零数据搬迁。→ 比「首启静默流式化」保守且可回退（标志翻回即回固定语义，境外增量区变成不可达数据=可再清理）。

### 29.5.4 风险登记

- **OOB 语义回归面**（最大）：sparse 世界的 blockAt/setBlock/heightAt/supportTopYAt/液体/光照边界语义全部走 OOB 等价——W1 配语义门矩阵族逐门钉；fixed 世界零变化墙照挂。
- **生成风暴**：快速移动请求速率 > 生成速率——driver 背压计数 + r2012 线程队列满载拒绝既有语义；W2 实装后 F3 加排队观测行，P5 调参。
- **实体×卸载竞态**：mob/掉落物在卸载候选区内的处理次序——W3 设计内收口（MC 引证三元组随单）。
- **首屏等待**：出生半径即 W1 参数；Cold start 观感 P5 验收（D4 双半径调参在此兑现）。
- **存档兼容**：全程 additive（worldstore 冻结域 + save_coord 先例）；任何步骤发现需迁移 → STOP NEEDS_HUMAN。
