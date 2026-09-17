#ifndef GAMESESSION_H
#define GAMESESSION_H

// R20.07 GameSession（refactor-plan §29.3 R20.07）：把当前 QML 直接编排的最小游戏循环包成
// 编排壳。**第一步只迁移五项**：固定 Tick / 暂停 / BreakBlock / PlaceBlock / WorldDelta。
// 验收三条（plan 原文）：无头程序可执行一段游戏 Tick（本类无 QTimer、无 QML 依赖——调用方
// 泵 stepTick 即驱动，矩阵探针 r2007 系列即无头消费者）；QML 仍可通过 Adapter 玩游戏
//（本单零迁移——QML 经 WorldClock.ticked 桥接 World tick 家族的现行玩法路径**逐字不动**，
// Main.qml 不挂 GameSession，见 docs/refactor-plan-2026-09-08.md R20.07 关单登记）；旧路径
// 与新路径结果一致（r2007a 双路径探针断言）。
//
// ── 基线盘点（R20.07 落回标，决策依据）────────────────────────────────────────────
// ① 现行 QML 编排面：Main.qml「Connections { target: worldClock } onTicked(dt)」把 100ms
//    节拍桥接给 World 模拟家族（tickWaterFlow → tickLavaFlow → tickFire → tickCropGrowth →
//    tickSugarcaneGrowth → tickFarmlandHydration → tickSaplingGrowth → tickSweetBerryBushGrowth
//    → tickIceFreeze → tickIceMelt → tickRedstone → tickLeafDecay → tickWeather(dt)，**次序
//    即节拍语义**）+ UI 域（furnacePanel / progress / cloudDrift——呈现层私有，不入会话）。
// ② BreakBlock / PlaceBlock 既有权威执行链：PlayerController::breakBlock()/placeBlock()
//    （Q_INVOKABLE 无参——射线命中 + 指针捕获 + hotbar 持物门控的**输入前门**）→
//    finishMiningAt / useBlock → **World::setBlock(4 参)**。world.cpp 对 4 参 setBlock 的
//    自述即「写栅格的唯一入口」：写后钩子族（生长 / 流体 / 冰 / 火索引 + 增量光场）+
//    blockBroken/blockPlaced 语义事件 + worldChanged 全套照走。
// ③ 决策：**命令执行委托 World::setBlock 同一终端权威**（不复制游戏逻辑——本类零掉落 /
//    零成就 / 零级联代码）。PlayerController 前门依赖相机射线 / 持物 / 模式，无头域不可达，
//    且命令携带显式 BlockPos（意图数据），与「射线算位」的输入前门不同构；玩家域语义
//    （掉落 / 耐久 / 成就）属 PlayerController 域，留后续 intent 层任务收口（登记非目标）。
// ④ WorldDelta 构造点：World::blockBroken(x,y,z,oldId) / blockPlaced(x,y,z,id) 是仅有的
//    带坐标编辑事件 → 本类在事件回调里经 R20.09 EditBuffer 登记（同格同 Tick 重复写合并
//    ——后写胜，重复通知抑制），每 Tick 收口 takeDelta 出一个 WorldDelta + 按合并面派生
//    BlockChanged 事件（Tick 末统一发布——不再逐写入队）。**R20.09 已落**：per-tick 编辑
//    面收口正席由 src/Core/editbuffer.h 承担（本类是首个消费方）；流体等静默写只发无参
//    worldChanged、不带坐标 → 不经本面（静默写族全量接线仍登记 R20.10+——EditBuffer 类型
//    已正席，调用点迁移是后续单）。
// ⑤ 与 WorldClock 的关系（盘点后定，登记）：WorldClock 仍是 QML 路径的墙钟单一权威
//   （QTimer 100ms → ticked(0.1)）；本类**不接管、不绑定**它（QML 面零变化的承重面）。
//    固定 Tick 基准同源 Tick::kClockTickMs（mathtypes.h 单一权威）；stepTick 的 dt 口径与
//    WorldClock::ticked(deltaSecs) 一致（秒），未来 Adapter 只需把 ticked(dt) 桥到
//    stepTick(dt)（本单不接）。pause() 是编排壳级闸（世界模拟 + 命令执行停），不等价
//    WorldClock 硬暂停（昼夜 QTimer 面）——两闸并存、互不越权（登记）。
//
// 暂停语义：pause() 后 stepTick 恒返回 0 且 **dt 丢弃不累积**（复跑无 catch-up 跳变——与
// WorldClock::setRunning(false) 停表语义同门：暂停期时间债不存在）。命令仍可入队（输入不
// 丢），复跑后按 targetTick 正常执行。
//
// 固定 Tick 语义：整数毫秒累积器（dt 秒 → qRound(dt×1000) ms，0.1s → 100ms 精确无浮点残
// 渣），每攒满 kClockTickMs 执行一个整 tick（N×stepTick(0.1) == stepTick(N×0.1) == N 个
// tick——r2007b 行级钉）。tick 体次序 = Main.qml 桥接次序逐行镜像（①）。
//
// dt 钳制（t1050，Review_2026-09-15 #1）：stepTick 入口双钳制——负 dt / NaN → 零累积（无负
// 时间债入口）；dt > kMaxStepSecs(1.0s) 整体丢弃 + qWarning + droppedDtCount 计数（断点续跑 /
// 切后台恢复量级的异常墙钟差不 catch-up——与暂停「dt 丢弃不欠账」同门；int 累积器输入域被
// 入口钳死故无 qRound 溢出）。选型依据与探针见 stepTick 注 + r2007e。
//
// ── §29.5-W5 后端：流式世界持久化 + D2/D3 标志 + overlay 合并（r2027；QML 零触碰）─────────
// 计划原文（refactor-plan §29.5.2 W5 + §29.5.3 选型 2）：世界 meta streaming 标志（additive
// 零迁移）；D3 = opt-in 转换（blob 区成为核心区，境外按需生成 + 经 W3 落盘）；转换可逆
// （标志翻回 = fixed 语义，境外数据不可达但不清除）。本单 = W5 后端半（UI = 下一单 W5b，
// §29.5 登记的 QML 例外面——本壳零 QML 新增）。接线宿主 = 本流式会话（W2/W3/W4 同门）：
//
//   · **D2 后端（流式世界元数据）**：stream_worlds 附加表（chunkstore.h 头注 W5 段 = DDL 与
//     代次刻度权威）——世界行本身经现有 Q_INVOKABLE 面创建（createWorld/openWorld，本壳零
//     涉），streaming 标志与 core_w/core_d（出生区界）只住附加表；fixed 世界零标志零活动
//     （连 ChunkStore 都不构造 = W3 零活动墙同门延续）。markStreamingWorld = D2 创建与 D3
//     转换同一落点（置位 + core dims + 代次锚）；clearStreamingWorldFlag = D3 逆翻（仅翻
//     标志，行保留）。
//   · **保存时冲洗（流式世界持久化主通路）**：flushResidentEditsForSave——保存链（Main.qml
//     saveAndExitToWorldList → persistWorldState 三同步写 [saveAll/savePlayerData/
//     saveProgress]）的**前置挂点**（W5b 接线：冲洗成功才续走三写，失败上报不谎报 = r2015
//     complete 戳同门）。冲洗域 = 全部驻留 dirty chunk 经 W3 persistFn 同缝（persistResidentChunk
//     ——驱逐与冲洗共用唯一落盘执行体）落 chunk_edits；流式世界的持久化形态 = 玩家/进度
//     blob（现行走）+ chunk_edits 全量——已生成未编辑的地形块不存，读档按 seed 确定性重生
//     成（W1b parity 是正确性根基，头注释立证）。冲洗第一拍 = advanceStreamSaveGeneration
//     （保存代次推进；写不进即失败上报——marker 同门）。
//   · **读档合并（overlay 语义）**：loadStreamingWorld = sparse 构造（W1）+ 出生半径预生成
//     之后的行全量回灌（跳 population——W3 语义）+ D3 核心区 blob 物化（经现有只读
//     Q_INVOKABLE 面 hasChunks/loadChunks——worldstore 零改动，只读消费）。**仲裁 = 代次
//     对代次**（chunk_edits 行代次 vs 世界存代次锚 base_gen，新者胜；同代 = 表胜——刻度统
//     一与锚语义立证见 chunkstore.h 头注 W5 段）：行代次 ≥ base_gen → 行胜（当前流式时代
//     事实）；< base_gen → blob 胜（再转换锚让位的老时代行）；无 blob 在场 → 无对手方，行
//     全胜（原生流式世界，chunks 表恒空）。读档后追加生成的区块走 W2 生成路径（本壳既有
//     收割拍照常）。
//   · **D3 转换语义（后端）**：既有 fixed 世界 → 标志置位（不动原 blob，零数据搬迁——
//     §29.5.3 选型 2 原文）→ 读档走「核心区 = blob 物化 + 境外按需生成 + 编辑块 overlay」；
//     标志翻回 = fixed 语义（fixed 读档链不识 chunk_edits——worldstore 冻结域的结构性事实）。
//   登记非目标：UI（W5b——本壳 API 为其接线面）；删除流式世界的清表策略（登记）；despawn
//     半径接线（既有登记）；性能调参（P5）；保存流统一经 SaveCoordinator（W5b+——base_gen
//     的「fixed 重存不可见」限制随保存流统一收敛，chunkstore.h 头注登记）。
//
// ── 分层（PLAN §2）：Game 层编排壳——向下依赖 World（tick 家族 + setBlock 权威）+ Core
//（command/event/mathtypes/result），不依赖 Renderer/Entities/QML；不反向被 World 依赖。
//
// ── §29.5-W2 位置源 + 驱动接线（r2024；流式激活第一次生产通电）────────────────────────
// 计划原文：「玩家位 → floorDiv16 → GameSession tick 尾 driver.onPlayerChunk」。本壳为流式
// 会话的编排归属（R20.07 纪律——驱动编排归会话，World 保持既有职责面）：①通电条件 =
// World::isSparse()——fixed 世界连驱动器 / worker 都不构造（D2 零活动墙的最强形态，app 冒烟
// 同面实证）；②位置源 = PlayerController 移动沿（floorDiv16 换格检测在 PlayerController C++
// 侧）→ playerChunkChanged 信号直连 notePlayerChunk 钩子（零 QML 改动）；③tick 尾
// pumpStreamingTick = 位置沿喂驱动器 + 泵（边①②由 GenerationJob 唯一权威驱动，r2011 预留
// ChunkManager 挂点首用）+ review0916 #7 硬契约兑现（结果面/数据面收割拍内同拍双消）+ 数据
// 面主线程落位（World::adoptGeneratedChunk：缓冲落格 + ③晋升 + W1b population 主线程重放）。
// 登记非目标：驱逐面（W3——toEvict 决策产出的消费回调保持 null）/bake→worker 网格化（W4）/
// UI 开关（W5）/半径参数实值（P5——W2 用 P1 默认）；worldstore 零触碰（sparse 持久化=W3/W5）。
//
// ── §29.5-W3 驱逐 + Edits-on-evict 落盘（r2025；setEvictor 回调自本单起从 null 转正）──────
// 计划原文（refactor-plan §29.5.2 W3）：「driver evictor 注入 → ChunkEvictor 生产缝实装
//（dirtyQuery = World 脏面、persistFn = §29.5.3 选型、lifecycleTransition = ChunkManager 真
// 转移）。实体×卸载竞态语义收口」。W2 流式会话是接线宿主（R20.07 编排壳纪律——驱动编排归
// 会话，World/组件保持既有职责面）。三缝的生产绑定点（头注释立证）：
//   · dirtyQuery = World::chunkHasUnsavedEdits（persist 域 chunk 级编辑权威——与 mesh 域
//     markDirty 分域，单漏斗标记面见 chunkmanager.h 头注释）；
//   · persistFn = ChunkStore（per-chunk 附加表，§29.5.3 选型 1 (a)：additive 零 bump、
//     r2015 save_coord 先例同款；blob = 驱逐时刻三数组原样字节）——持久化时机 = 驱逐候选
//     dirty 时即时落盘（不走整世界 saveAll）；成功即清该 chunk 未落盘账；失败 = ChunkEvictor
//     顺序铁律中止驱逐（保持驻留，P3 语义生产面）；
//   · lifecycleTransition = World::setChunkLifecycle 真转移（⑥⑦合法边；经唯一守卫入口，
//     驻留 revision 沿自动携带 = P4 池消费面连通）。转移缝内的两项生产语义：
//       (1) 边⑥（target==Evicting）先调实体移除缝（setEvictionEntitySink——Entities 层
//           despawnInChunk 经 std::function 注入，本壳零 Entities 类型依赖[分层不变]；
//           选型 + MC 引证三元组 = EntityManager::despawnInChunk 头注释）——先于转移；
//       (2) 边⑦（target==Absent）接受后 World::releaseStreamingChunk 擦槽（数据面闭合：
//           内容已落盘或 clean 可重derive；残留槽会让重物化把陈旧内容复活[守卫入口空气
//           零写]，population 脚手架拆卸同门）。
//   重载路径：driver savedContentQuery = 附加表存在性查询（hasChunk——行只增不删，命中即
//     稳定）→ Load kind job 照常走边①②（kind 门语义不变）→ 收割拍数据面按表命中路由：
//     命中 → World::restoreChunkFromBlob 直接物化（**跳过 population**——存档内容已是终态
//     含 population，头注释立证）；未命中 → W2 现行 adoptGeneratedChunk 生成路径。
//   登记非目标：跨会话 blob×附加表 overlay 合并（W5/D3 域——**W5 已兑现**：loadStreamingWorld
//     overlay 仲裁，见下方 W5 头注段；W3 时点为登记）；ChunkStore 生产 bind（W5 存档
//     入口接线——未 bind 时 dirty 候选中止驱逐 = 宁驻留不误删 fail-safe）；hasChunk 逐查询
//     开闭连接的经济学（W5/P5 优化面）；despawn 半径语义（不引入，见实体移除头注释）。
//
// ── §29.5-W4 bake→worker 网格化（r2026；D6 网格执行器自本单起生产接线）──────────────────
// 计划原文（refactor-plan §29.5.2 W4）：「ChunkGeometry bake 段改提交快照 + 收割应用；同步
// 内联回退开关；F3 mesh 行加 worker 列。QML 零改动（池已动态）」。接线宿主 = 本流式会话
//（R20.07 编排壳纪律 + W2「驱动编排归会话」同门；bake 发起点可达 + 收割拍可达两准绳的
// 承重面）。三件生产绑定（头注释立证）：
//   · 执行器所有权 = m_meshWorker（构造即起真线程；**仅 sparse 世界构造**——isSparse() 门内，
//     fixed 世界连构造都不发生 = D2 零活动墙延续，app 冒烟同面实证）。它不入 World（W2
//     「驱动器/worker 不入 World」职责面纪律）也不入 ChunkGeometry（Renderer 只达 World）：
//     「bake 发起点可达」经 World 桥提交缝（setChunkMeshBuildSink——std::function 注入，执行
//     器类型零泄漏进 World/Renderer 头）；「收割拍可达」= 本壳 pumpStreamingTick tick 尾
//     单点（W2 既有收割拍同宿，延迟有界 ≤ 一个 tick、零阻塞、QML 零改动）。
//   · bake 异步化 = ChunkGeometry 主线程定格快照 → World 桥提交（requestId=(cx,cz,段,代次)
//     派生——基座+代次选型立证见 chunkgeometry.cpp deriveMeshJobRequestId）→ 本壳收割拍
//     takeBuilt 排干 → World::deliverBuiltChunkMesh 按注册表路由回几何灌注。**latest-snapshot-
//     wins 双保险**：几何重提交先显式注销旧在途（World 侧注册表 miss = 可见丢弃）+ 交付回调
//     自证最新在途键（几何侧第二道闸）；执行器组件对 requestId 不查重不合并（meshworker.h
//     头注语义原样），覆盖语义全权落路由层。
//   · 同步回退 = 执行器满载（kErrQueueFull）/已停（kErrMeshWorkerStopped）拒绝时几何走现行
//     同步内联路径（网格仍正确产出；拒绝面计数可见 + 告警）。worker 析构（会话终）丢弃在途
//     = droppedCount 组件可见、join 有界绝不挂死（meshworker.h 退出语义）。
// 登记非目标：多 worker 扩并发 / 优先级调度（后续单）；W3 驱逐×在途网格竞态（在途网格按
//   现有可见性门自然失效——QPointer 守卫交付丢弃，行为登记）；QML 任何改动（F3 worker 列在
//   FrameProfiler win 行——C++ 面）。

#include "chunk.h"     // Chunk::kSize（chunk 路由参数——单一权威，不写魔法 16）
#include "command.h"   // Command / CommandQueue（R20.06 队列——GameSession 首个生产消费方）
#include "editbuffer.h" // R20.09 EditBuffer / DirtyChunkSet（Tick 内编辑合并收口——本类首个消费方）
#include "event.h"     // Event / WorldDelta / EventQueue（编辑面表达）
#include "mathtypes.h" // Tick::kClockTickMs / BlockPos / ChunkKey
#include "meshworker.h" // §29.5-W4：D6 网格执行器（r2020 组件的 W4 生产接线——r2020d 全树
                        //   记号反探同变更修订：接线宿主白名单 = meshworker.h + 本文件）
#include "result.h"    // Result<void>（满载拒绝失败面穿透）
#include "world.h"     // World：tick 家族（模拟泵——见下「选型」）+ setBlock 写实现权威
#include "worldfacade.h" // R20.08 WorldFacade：查询/写入收窄面（命令写 + 编辑后回读走此面）
#include "worldstore.h" // §29.5-W5：读档 blob 通路的只读 Q_INVOKABLE 消费面（hasChunks/loadChunks
                        //   ——worldstore 零改动纯消费；Game→World 向下依赖，PLAN §2 同门）

#include <QDebug>  // qWarning（超界 dt 丢弃——背压可见，t1050）
#include <QObject>
#include <QSet>    // 读档仲裁胜者集（ChunkKey::packed 键）
#include <QVector> // 未到期命令暂存（drain-then-replay；容量受 CommandQueue::kCapacity 上界）

#include <functional> // std::function（§29.5-W3 实体移除缝）
#include <memory> // std::unique_ptr（§29.5-W2 流式会话件）

#include "backgroundgeneration.h" // §29.5-W2：BackgroundGenerationWorker（R20.12 真线程件——
                                  //   W2 首个生产消费者）+ GeneratedChunkData（数据面值类型）
#include "chunkstreamdriver.h"    // §29.5-W2：ChunkStreamDriver（P2 位置沿编排器——生产通电）
#include "chunkevictor.h" // §29.5-W3：ChunkEvictor（P3 驱逐编排器——生产实缝三件）
#include "chunkstore.h"   // §29.5-W3：ChunkStore（D5 选型 (a) per-chunk 附加表——零 bump additive）

// WorldDelta 经信号外发（直接连接无需元类型；声明以备未来跨线程排队连接）。
Q_DECLARE_METATYPE(WorldDelta)

class GameSession : public QObject
{
    Q_OBJECT

public:
    // 固定 Tick 基准（Tick::kClockTickMs 的会话侧别名——单一权威不出 mathtypes.h）。
    static constexpr int kClockTickMs = Tick::kClockTickMs;

    // t1050（Review_2026-09-15 #1）：单次 stepTick 的 dt 上限（秒）。超过即整体丢弃 + qWarning
    // + droppedDtCount 计数（不累积不 catch-up——见 stepTick 注）。未来 Adapter 接墙钟时同读此值。
    static constexpr qreal kMaxStepSecs = 1.0;

    // 持 World 引用（编排壳不拥有世界——World 生命周期归 caller / QML，会话可随时重建）。
    explicit GameSession(World &world, QObject *parent = nullptr);

    // 已执行的固定 Tick 数（单调递增；暂停不推进，时间单向）。
    int tick() const { return m_tick; }
    bool isPaused() const { return m_paused; }

    // 暂停 / 复跑（幂等；见头注暂停语义——停表不欠账，命令入队不受限）。
    void pause() { m_paused = true; }
    void resume() { m_paused = false; }

    // 固定 Tick 泵：累积 dt（秒，口径同 WorldClock::ticked）→ 每满 kClockTickMs 执行一个
    // 整 tick（命令 drain → World 模拟家族 → WorldDelta 收口 + tickCompleted）。返回本次
    // 实际执行的 tick 数（暂停恒 0；余量跨调用保留）。无头驱动的唯一入口。
    int stepTick(qreal deltaSecs);

    // 命令入队（提交侧）：经 R20.06 CommandQueue（FIFO + 满载 Result 拒绝——丢弃必须被
    // 提交方看见）。**入队不执行**（执行只发生在 stepTick 的整 tick 边界——r2007d 钉
    // 「旁路即红」：入队后未泵 tick 前世界必须零变化）。sequence / targetTick 语义见
    // command.h（targetTick 0 = 尽快 = 下一整 tick 执行）。
    Result<void> enqueueCommand(const Command &c) { return m_commands.push(c); }

    // 最近一个整 tick 的编辑面（tickCompleted 同帧快照；无编辑 tick = 空 delta）。
    const WorldDelta &lastDelta() const { return m_lastDelta; }

    // 最近一个整 tick 的脏 chunk 集（R20.09 验收④的会话面示范：渲染调度按**集合**一次
    // 查询——每 Tick 一次、不再直绑每一次 setBlock；全量接线 ChunkGeometry/QML 归 R20.10+）。
    // 活引用非拷贝，有效域 = tickCompleted 信号栈内（同帧快照）；r2017 起 stepTick 返回时
    // 已收口清账（窗口 = 上收口到本收口）——跨帧持有请自行拷贝（值拷贝化维持 review #12
    // 登记非目标）。
    const DirtyChunkSet &lastDirtyChunks() const { return m_edits.dirtyChunks(); }

    // 事件观察面（BlockChanged 每合并编辑格一条、Tick 末统一入队；EventQueue 满载丢弃
    // 必须可见 → 计数器暴露）。EditBuffer 记录面满载丢弃同门可见（droppedEditCount）。
    EventQueue &events() { return m_events; }
    int droppedEventCount() const { return m_droppedEvents; }
    int droppedEditCount() const { return m_droppedEdits; }
    // t1050：超界 dt（> kMaxStepSecs）整体丢弃累计——背压可见（真机 Adapter 接墙钟后异常
    // 墙钟差频度可观测；矩阵腿 r2007e 断言丢弃 + 计数）。
    int droppedDtCount() const { return m_droppedDts; }

    // ── §29.5-W2 位置源 + 驱动接线（r2024；计划原文「玩家位 → floorDiv16 → GameSession tick
    //    尾 driver.onPlayerChunk」）────────────────────────────────────────────────────────
    // 位置源钩子（生产接线 = PlayerController::playerChunkChanged 直连本钩子）：floorDiv16
    // 换格检测在 PlayerController C++ 侧（位置权威），本侧零几何逻辑——只缓存最新玩家 chunk；
    // 喂入本身零动作，驱动器变更沿在 tick 尾泵拍承接（onPlayerChunk 幂等：同 chunk 重复零动作）。
    void notePlayerChunk(int cx, int cz)
    {
        m_playerChunkCx = cx;
        m_playerChunkCz = cz;
        m_hasPlayerChunk = true;
    }
    // 流式观测面（矩阵腿 / F3 排队观测消费口；fixed 世界恒 false / 0 / null）。
    bool streamingActive() const { return m_streamDriver != nullptr; }
    int streamingOutcomeCount() const { return m_streamOutcomeCount; }
    int streamingAdoptedCount() const { return m_streamAdoptedCount; }
    const ChunkStreamDriver *streamDriver() const { return m_streamDriver.get(); }
    // worker 诊断读面（线程身份对账 / 已执行计数——r2012a 先例；fixed 恒 null）。
    const BackgroundGenerationWorker *streamWorker() const { return m_streamWorker.get(); }
    // P5 调参入口（W2 生产 = P1 默认半径不传即用；矩阵腿内压小半径控时长）。
    void configureStreamingRadii(int generationRadiusChunks, int renderRadiusChunks,
                                 int scanExtentChunks)
    {
        if (m_streamDriver)
            m_streamDriver->enableStreamingWith(generationRadiusChunks, renderRadiusChunks,
                                                scanExtentChunks);
    }

    // ── §29.5-W5b 生产帧泵（r2028；QML 路径零迁移的会话侧承重面）────────────────────────
    // 生产形态下 World 模拟家族仍由 QML 既有 tick 桥独占驱动（「玩法路径零改动」R20 主线不变
    // 量——本壳 runOneTick 的模拟族镜像只服务无头全语义入口 stepTick，矩阵腿族消费）；流式世界
    // 的生产泵拍 = 本方法：只做 tick 尾流式收割拍（pumpStreamingTick——位置沿喂入 + 驱动器泵 +
    // #7 同拍双消 + 网格收割），零模拟零命令。调用方 = StreamingBridge 挂在 WorldClock::ticked
    // 上的槽（与 QML tick 桥同拍同源）；fixed 世界/无驱动器 = 泵体首行零动作墙（D2 同门）。
    void pumpStreamingFrame() { pumpStreamingTick(); }

    // ── §29.5-W4 网格收割观测面（C++ only；fixed 世界恒 null / 0）─────────────────────────
    // 网格执行器读面（线程身份对账 / 队列账面——r2012a/r2020 先例；fixed 恒 null = D2 墙）。
    const MeshWorker *meshWorker() const { return m_meshWorker.get(); }
    // 收割拍网格交付累计（每被接受请求恰一条——恰一产出铁律的会话侧对账锚）。
    int meshHarvestedCount() const { return m_meshHarvestedCount; }

    // ── §29.5-W3 驱逐 + Edits-on-evict 接线面（C++ only；fixed 世界恒 null / 空 / no-op）──
    // 附加表绑定（生产 = W5 存档入口接线；矩阵 = fresh 临时库[r2015 先例，绝触 saves/]）。
    // 未 bind：persist 缝恒失败（dirty 候选中止驱逐 = 宁驻留不误删）、savedContentQuery 恒
    // miss（全 Generate）——fail-safe 两面都保守。
    bool bindChunkEditsStore(const QString &dbFilePath)
    {
        if (!m_chunkStore)
            return false; // fixed 世界无附加表（连构造都不发生 = D2 零活动墙同门）
        m_chunkStore->bind(dbFilePath);
        return true;
    }
    // §29.5-W5：世界标识（stream_worlds 主键转发——生产 = 存档文件名；bind 后、首次元数据
    //   操作前设置。缺省空串 = 匿名键）。返回 false = fixed 世界无 store（零活动墙同门）。
    bool setStreamWorldId(const QString &worldId)
    {
        if (!m_chunkStore)
            return false; // fixed 世界无附加表（连构造都不发生）
        m_chunkStore->setStreamWorldId(worldId);
        return true;
    }
    QString streamWorldId() const
    {
        return m_chunkStore ? m_chunkStore->streamWorldId() : QString();
    }
    // ── §29.5-W5 D2/D3 标志面（元数据域转发；fixed 世界恒 false——无 store 可言）─────────
    // D2 创建与 D3 转换同一落点：streaming 置位 + core dims（出生区界 / 被转换世界 dims）+
    // 代次锚（选型立证 = chunkstore.h 头注 W5 段）。纯元数据，零 blob 搬迁零 chunk_edits 触碰。
    bool markStreamingWorld(int coreWidthBlocks, int coreDepthBlocks)
    {
        return m_chunkStore && m_chunkStore->isBound()
            && m_chunkStore->markStreamingWorld(coreWidthBlocks, coreDepthBlocks);
    }
    // D3 逆翻：标志翻回 = fixed 语义（fixed 读档链不识 chunk_edits——worldstore 冻结域的
    //   结构性事实）；行保留（境外数据不可达但不清除，§29.5.3 既有口径）。
    bool clearStreamingWorldFlag()
    {
        return m_chunkStore && m_chunkStore->isBound() && m_chunkStore->clearStreamingWorldFlag();
    }
    // 流式世界读面（fixed 恒 false；streaming=0 的残留行同样 false）。
    bool isStreamingWorld() const
    {
        if (!m_chunkStore || !m_chunkStore->isBound())
            return false;
        StreamWorldMeta meta;
        return m_chunkStore->readStreamWorldMeta(meta) && meta.streaming;
    }
    // ── §29.5-W5 保存时冲洗（流式世界持久化主通路；语义见类头注 W5 段）─────────────────────
    // 保存链前置挂点（W5b 接线：成功才续走三写）。拍序 = **先冲洗、后代次推进**（头注释立
    //   证）：行盖写 = save_gen + 1（ChunkStore::persistChunk 统一刻度——驱逐在途写同式），
    //   冲洗后推进 save_gen := 旧值 + 1 = 恰为本批行的盖写代次——「行代次 = 本次保存代次」
    //   精确成立，且推进失败（锁/病）时行已在场（代次锚仲裁只对 base_gen，行不因失号失效，
    //   marker 同门 = 写不进台账不销数据）；冲洗域 = 全部驻留 dirty chunk 经 W3 persistFn
    //   同缝落盘（persistResidentChunk 唯一执行体——驱逐与冲洗共用）。任一 chunk 失败或代次
    //   推进失败 = 返回 false（上报不谎报；账不清 = 下次保存重报收敛；已成功行不回滚）。
    bool flushResidentEditsForSave();
    // ── §29.5-W5 读档合并（overlay 语义；语义见类头注 W5 段）──────────────────────────────
    // 行全量回灌 + D3 核心区 blob 物化（store 须已 openWorld 本世界且 setWorld 指向本壳世界
    //   ——r2010d rebind 纪律；误绑防御 = qWarning + false）。非流式世界（无行 / streaming=0）
    //   → false（caller 走既有 fixed 读档链）。返回聚合成败（行/blob 物化失败逐条 qWarning
    //   不中断——诚实降级同门，世界仍可玩）。
    bool loadStreamingWorld(WorldStore &store);
    // 观测面（矩阵 / 诊断；fixed 恒 0）：冲洗落盘/失败 chunk 计数 + 读档行回灌/blob 物化计数。
    int flushPersistedCount() const { return m_flushPersistedCount; }
    int flushFailedCount() const { return m_flushFailedCount; }
    int loadRestoredRowCount() const { return m_loadRestoredRowCount; }
    int loadBlobChunkCount() const { return m_loadBlobChunkCount; }
    // 驱逐转移前活体移除缝（Entities 层接线方注入：[cx,cz] → 两族 despawnInChunk 的组合；
    // 本壳零 Entities 类型依赖——分层不变[R20.07]。null = 无实体面可移除[驱逐照常进行]）。
    void setEvictionEntitySink(std::function<void(int cx, int cz)> sink)
    {
        m_entityEvictSink = std::move(sink);
    }
    // 观测面：附加表（fixed 恒 null）/ 最近一次驱逐批的 ChunkEvictor 记账 / 调用序逐事件
    // 轨迹（persist 先于转移的调用序柱[r2019b 同款生产面] + 先移除后转移的实体语义面）。
    const ChunkStore *chunkEditsStore() const { return m_chunkStore.get(); }
    ChunkEvictor::Report lastEvictionReport() const { return m_lastEvictionReport; }
    // 轨迹事件（纯值小聚合）：kind 语义 = PersistOk[落盘成功，先于本候选任何转移]
    // / PersistFail[落盘失败 = 中止驱逐]/ EdgeEvicting[边⑥接受]/ EdgeAbsent[边⑦接受]
    // / TransitionRejected[转移被守卫拒]。诊断面，会话生命期累计（驱逐沿粒度，量级极小）。
    struct EvictionTraceEvent
    {
        quint8 kind = 0; // 0=PersistOk 1=PersistFail 2=EdgeEvicting 3=EdgeAbsent 4=TransitionRejected
        int cx = 0;
        int cz = 0;
    };
    const QVector<EvictionTraceEvent> &evictionTrace() const { return m_evictionTrace; }

signals:
    // 每整 tick 收口发（tick = 已完成 tick 号；delta = 本 tick 编辑面快照）。
    void tickCompleted(int tick, const WorldDelta &delta);

private:
    // 一个整 tick：tick 号推进 → 到期命令 drain（FIFO 保序；未到期原序回队——回队 push
    // 恒成功：刚腾出的空位 ≥ 回队数）→ World 模拟家族（Main.qml 桥接次序逐行镜像）→
    // EditBuffer 收口（takeDelta + 按合并编辑面派生 BlockChanged 事件）+ 信号。
    void runOneTick();
    // §29.5-W2 流式泵拍（tick 尾）：位置沿 → 驱动器 onPlayerChunk（变更沿幂等）→ 泵（底层
    // 「交接 + 收割」两相，边①②由 GenerationJob 唯一权威驱动）→ review0916 #7 硬契约兑现：
    // 收割拍内结果面（takeOutcome，每活别名一条）与数据面（takeResultData，每完成 job 恰一条
    // 自持缓冲）同拍双消——两面各自独立 pop（#7 原文：只收一面 = 另一面无界积压）；数据面 =
    // 主线程落位唯一通路（World::adoptGeneratedChunk：缓冲落格 + ③晋升 + W1b population 主
    // 线程重放）。fixed 世界无驱动器 = 本函数零动作（D2 零活动墙，连构造都不发生）。
    void pumpStreamingTick();
    // 命令执行（**委托不复制**）：BreakBlock → setBlock(pos, Air)、PlaceBlock → 按 blockState
    // 双路由（state==0 → setBlock 四参同 id no-op 保 state[r2017 MC 口径恢复]；state!=0 →
    // setBlockWithState 五参全写[R20.09 权威]）——经 WorldFacade 收窄面落 World::setBlock
    //「写栅格的唯一入口」权威（R20.08 迁移：命令写走 Facade；R20.09 落 Review_2026-09-15
    // #3①：PlaceBlock 带 state 五参权威落地——带朝向/半砖态方块不再静默丢 state）。越界 /
    // 无变化由 World 权威语义静默拒绝（同玩家前门路径的行为面）。
    void executeCommand(const Command &c);
    // 编辑登记（blockBroken/blockPlaced 回调 → R20.09 EditBuffer 委托）：改动后 id 回读 +
    // record（同格合并 / 新格首记 / 满载丢弃可见）。**Tick 内零通知**——BlockChanged 事件
    // 由 runOneTick 收口按合并编辑面统一派生（每格一条、id=终态——验收①「不产生不必要的
    // 重复通知」的会话面承担点）；r2017 起 tick 收口后（tick 栈外）到达的编辑同入本面归入
    // 下一窗，收口单点发布不破（见 runOneTick 注）。
    void noteEdit(int x, int y, int z);

    // ── §29.5-W5 落盘/回灌执行体（私有；冲洗与驱逐共用的唯一 chunk 落盘缝）────────────────
    // 单 chunk 落盘（W3 persistFn 同体收口——驱逐缝与保存时冲洗两路共用，头注立证）：
    // chunk 缺席 / store 未 bind → fail（fail-safe 面）；成功 = 清该 chunk 未落盘账（已落盘
    // 不再 dirty）。代次盖写在 ChunkStore::persistChunk 内（世界保存代次刻度——chunkstore.h
    // 头注 W5 契约修订段）。
    Result<void> persistResidentChunk(int cx, int cz);
    // 回灌前弃槽（读档 overlay 的「物化内容让位」语义）：已物化（出生预生成占位 / blob 先行
    //  物化）→ ⑥⑦ 合法边到 Absent + 擦槽（数据面闭合，W3 转移缝同门）；未物化 = no-op
    //  （restoreChunkFromBlob 自 ensure 物化）。被让位内容 = seed 可重derive 生成物或仲裁
    //  败者 blob——让位零不可再生损失。
    void discardStreamingChunkForRestore(int cx, int cz);

    World &m_world;
    // R20.08 WorldFacade（示范迁移：新代码经收窄面读写世界）：查询/写入走 m_facade（命令写
    //   setBlock/setBlockWithState + 编辑后回读 blockAt），tick 模拟泵家族仍直调 m_world（模
    //   拟泵非查询/写面，Facade 不收拢——选型登记于 docs R20.08 关单；r2008d 阴性钉守「命
    //   令零旁路」）。
    WorldFacade m_facade;
    CommandQueue m_commands;
    EventQueue m_events;
    EditBuffer m_edits; // R20.09：Tick 内编辑合并收口（chunk 路由模长取 Chunk::kSize 单一权威）
    WorldDelta m_lastDelta;
    int m_tick = 0;
    int m_accumMs = 0;    // 整数毫秒累积器（余量跨 stepTick 保留；暂停丢弃——见头注）
    bool m_paused = false;
    int m_droppedEvents = 0;
    int m_droppedEdits = 0; // EditBuffer 记录面满载丢弃累计（kMaxEdits 上界——不可再生必须可见）
    int m_droppedDts = 0;   // t1050：超界 dt 丢弃累计（> kMaxStepSecs 整体丢弃——背压可见）

    // ── §29.5-W2 流式会话件（sparse 世界独占构造；fixed 世界恒 null = D2 零活动墙）────────
    // 成员声明序 = 析构序的承重选择（review0916 #8 析构序契约）：worker 先声明、驱动器后声明
    // → 析构按声明逆序：驱动器（内含 GenerationScheduler，挂 worker 非拥有指针）先于 worker
    // 消亡——「scheduler 先于 worker 析构」结构性成立，悬垂泵面不存在。零活动墙：
    // BackgroundGenerationWorker 构造即起真线程——fixed 世界连构造都不发生（app 冒烟同面实证）。
    std::unique_ptr<BackgroundGenerationWorker> m_streamWorker; // R20.12 真线程件（W2 首个生产消费者）
    std::unique_ptr<ChunkStreamDriver> m_streamDriver;          // P2 位置沿编排器（W2 生产通电）
    // ── §29.5-W4 网格执行器（sparse 独占构造；独立线程件，与 driver/worker 对无交叉引用
    // ——声明序无析构序约束；构造即起真线程，fixed 世界连构造都不发生 = D2 零活动墙延续）。
    std::unique_ptr<MeshWorker> m_meshWorker; // D6 网格执行器（W4 生产接线宿主面）
    int m_meshHarvestedCount = 0;             // 收割拍网格交付累计（恰一产出对账锚）
    // ── §29.5-W3 驱逐件（sparse 独占构造；纯值组件零线程——声明序无析构序约束）──────────
    std::unique_ptr<ChunkStore> m_chunkStore; // D5 选型 (a)：per-chunk 编辑附加表（默认未 bind）
    ChunkEvictor m_chunkEvictor;              // P3 驱逐编排器（三缝生产绑定；构造后惰性）
    std::function<void(int cx, int cz)> m_entityEvictSink; // 驱逐转移前活体移除缝（可空）
    ChunkEvictor::Report m_lastEvictionReport;             // 最近一次驱逐批记账（观测面）
    QVector<EvictionTraceEvent> m_evictionTrace;           // 调用序轨迹（persist/⑥/⑦/拒 逐事件）
    bool m_hasPlayerChunk = false; // 位置沿缓存（floorDiv16 换格检测在 PlayerController C++ 侧）
    int m_playerChunkCx = 0;
    int m_playerChunkCz = 0;
    int m_streamOutcomeCount = 0; // 收割拍结果面累计（每活别名一条——#7 同拍消费账面）
    int m_streamAdoptedCount = 0; // 收割拍数据面累计（每完成 job 恰一条——#7 同拍消费账面）
    // ── §29.5-W5 冲洗/读档观测账面（矩阵断言 / 诊断；fixed 恒 0）─────────────────────────
    int m_flushPersistedCount = 0;   // 冲洗落盘 chunk 累计（逐 chunk 原子成功面）
    int m_flushFailedCount = 0;      // 冲洗失败 chunk 累计（失败上报可见面）
    int m_loadRestoredRowCount = 0;  // 读档行回灌累计（overlay 仲裁胜者面）
    int m_loadBlobChunkCount = 0;    // 读档 blob 物化累计（D3 核心区面）
};

inline GameSession::GameSession(World &world, QObject *parent)
    : QObject(parent)
    , m_world(world)
    , m_facade(world)
    , m_edits(Chunk::kSize) // R20.09：chunk 路由模长单一权威（Core 叶子不自持该常量）
{
    // 编辑语义事件 → EditBuffer 登记点（带坐标的仅此两路，见头注③④）。直接连接同步登记
    //（World setBlock 栈内执行——与 tick 收口同线程同序，无队列延迟）。
    QObject::connect(&m_world, &World::blockBroken, this, [this](int x, int y, int z, int) {
        noteEdit(x, y, z);
    });
    QObject::connect(&m_world, &World::blockPlaced, this, [this](int x, int y, int z, int) {
        noteEdit(x, y, z);
    });

    // ── §29.5-W2 流式会话通电（D2 承重门）：streaming 仅 sparse 世界使能——fixed 世界连驱
    // 动器 / worker 都不构造（计划原文；零活动墙的最强形态，app 冒烟同面实证）。通电条件 =
    // World::isSparse()（W1 模式位）。会话参数 = P1 默认半径（P5 调参前的实值权威）；r2011
    // 预留 ChunkManager 挂点（边①②首用接线）+ W1 lifecycleAt 读缝 + 槽位物化缝 + 真线程
    // worker（R20.12 件的首个生产消费者）在此一次接齐——World 保持既有职责面（驱动编排归
    // 本编排壳，R20.07 纪律）。
    if (m_world.isSparse()) {
        m_streamWorker = std::make_unique<BackgroundGenerationWorker>(
            m_world.seed(), TerrainGen::Dims{ m_world.width(), m_world.depth(), m_world.height() });
        m_streamDriver = std::make_unique<ChunkStreamDriver>(
            ChunkStreamDriver::streamingWithDefaultRadii());
        m_streamDriver->setSeam([this](int cx, int cz) {
            return m_world.chunks().lifecycleAt(cx, cz);
        });
        m_streamDriver->setSlotEnsure([this](int cx, int cz) {
            m_world.ensureStreamingChunkSlot(cx, cz);
        });
        m_streamDriver->attachLifecycleSink(m_world.streamingLifecycleSink()); // r2011 预留面首用
        m_streamDriver->setWorker(m_streamWorker.get());

        // ── §29.5-W3 驱逐 + Edits-on-evict 生产实缝（三缝绑真实权威；选型论证见类头注）────
        m_chunkStore = std::make_unique<ChunkStore>(); // 默认未 bind（W5 存档入口接线；fail-safe 两面保守）
        // dirtyQuery = World persist 域脏面（chunk 级编辑权威——单漏斗标记面在 ChunkManager）。
        m_chunkEvictor.setDirtyQueryFn([this](int cx, int cz) {
            return m_world.chunkHasUnsavedEdits(cx, cz);
        });
        // persistFn = per-chunk 附加表即时落盘（驱逐候选 dirty 时；不走整世界 saveAll）。
        // 成功 = 清该 chunk 未落盘账；失败 = Result 穿透 → ChunkEvictor 顺序铁律中止驱逐
        //（保持驻留零转移——P3「先落盘后转移」语义的生产执行体在本缝与组件内共同成立）。
        // §29.5-W5：落盘执行体收口 persistResidentChunk（冲洗同缝共用——唯一落盘执行体），
        // 本 lambda 只补驱逐轨迹（冲洗走独立计数，不污染驱逐沿轨迹语义）。
        m_chunkEvictor.setPersistFn([this](int cx, int cz) -> Result<void> {
            const Result<void> r = persistResidentChunk(cx, cz);
            if (r.isOk())
                m_evictionTrace.append({ 0, cx, cz });  // PersistOk
            else
                m_evictionTrace.append({ 1, cx, cz });  // PersistFail
            return r;
        });
        // lifecycleTransition = World::setChunkLifecycle 真转移（⑥⑦合法边；revision 沿自动
        // 携带）。转移缝内两项生产语义：边⑥前实体移除（先于转移——卸载语义，头注引证）；
        // 边⑦接受后擦槽（数据面闭合——population 脚手架拆卸同门）。
        m_chunkEvictor.setTransitionFn([this](int cx, int cz, ChunkLifecycle target) -> bool {
            if (target == ChunkLifecycle::Evicting && m_entityEvictSink)
                m_entityEvictSink(cx, cz); // 驱逐候选区活体先于转移移除（实体语义收口点）
            const bool ok = m_world.setChunkLifecycle(cx, cz, target);
            if (ok) {
                if (target == ChunkLifecycle::Evicting)
                    m_evictionTrace.append({ 2, cx, cz }); // EdgeEvicting
                else if (target == ChunkLifecycle::Absent) {
                    m_evictionTrace.append({ 3, cx, cz }); // EdgeAbsent
                    m_world.releaseStreamingChunk(cx, cz); // 数据面闭合：擦槽（内容真实丢弃）
                }
            } else {
                m_evictionTrace.append({ 4, cx, cz }); // TransitionRejected
            }
            return ok;
        });
        // setEvictor 回调转正（W2 登记非目标自本单解除）：决策沿 toEvict 单 → P3 执行器。
        // 形参用 generic lambda（r2016d「接线面零策略类型记号」纪律延续——W2 同款免修订，
        // 策略类型留在 World 层，会话面只见回调形状）。
        m_streamDriver->setEvictor([this](const auto &cands) {
            m_lastEvictionReport = m_chunkEvictor.evict(cands);
        });
        // savedContentQuery = 附加表存在性（D5 回灌：命中 → Load kind job；未 bind 恒 miss）。
        m_streamDriver->setSavedContentQuery([this](int cx, int cz) {
            return m_chunkStore && m_chunkStore->isBound() && m_chunkStore->hasChunk(cx, cz);
        });

        // ── §29.5-W4 bake→worker 生产接线（执行器构造 + World 桥提交缝绑定）────────────────
        // 构造即起真线程（meshworker.h 退出语义：析构 stop+丢弃计数+join 有界绝不挂死；生产
        // 零参 = 默认队列容量 64）。提交缝 = std::function 注入 World（执行器类型零泄漏进
        // World/Renderer 头——bake 发起点经 World::chunkMeshAsyncActive/submitChunkMeshJob
        // 可达；fixed 世界不进本分支 = 连 sink 绑定都不发生 → 几何全同步内联，零变化墙）。
        m_meshWorker = std::make_unique<MeshWorker>();
        m_world.setChunkMeshBuildSink([this](const ChunkMeshSnapshot &snap, quint64 requestId) {
            return m_meshWorker->submit(snap, requestId);
        });
    }
}

inline int GameSession::stepTick(qreal deltaSecs)
{
    if (m_paused)
        return 0; // 暂停：dt 丢弃不累积（无时间债；复跑自冻结点继续——WorldClock 停表同门）
    // t1050（Review_2026-09-15 #1，方案①入口双钳制——选型依据：方案②「保留累积 + 单次 N tick
    // 上界」留有 qRound(deltaSecs*1000.0) 超出 int 域的 UB 洞（1e6 秒级墙钟差在 tick 上界生效
    // 前就溢出 int 累积器），方案①在入口把累积器输入域钳死 [0, kMaxStepSecs]（1000ms ≪
    // INT_MAX，溢出不可能发生）；「超界整体丢弃」与暂停「dt 丢弃不欠账」同门——断点续跑 /
    // 切后台恢复量级的墙钟差属异常 gap，不 catch-up 不补账（单泵至多 10 tick，无「万 tick 连
    // 跑」的世界模拟假死）。既有探针 dt 全部 ≤0.7s（r2007a 0.3 / r2007b 0.7 / r2007c 0.6），
    // 钳制不触达——N×stepTick(0.1)==stepTick(N×0.1) 与暂停停表逐位兼容（r2007e 回归钉）。
    if (!(deltaSecs >= 0.0))
        return 0; // 负 dt / NaN → 零累积（NaN 比较恒假一并拦）——负时间债入口焊死
    if (deltaSecs > kMaxStepSecs) {
        ++m_droppedDts; // 背压可见（droppedDtCount 暴露）
        qWarning() << "GameSession::stepTick: dt" << deltaSecs << "s exceeds" << kMaxStepSecs
                   << "s cap - dropped wholesale (no catch-up debt); dropped-dt total"
                   << m_droppedDts;
        return 0;
    }
    m_accumMs += qRound(deltaSecs * 1000.0);
    int executed = 0;
    while (m_accumMs >= kClockTickMs) {
        m_accumMs -= kClockTickMs;
        runOneTick();
        ++executed;
    }
    return executed;
}

inline void GameSession::runOneTick()
{
    ++m_tick; // tick 号先推进：targetTick ≤ 新号即「到期」（0 = 尽快，首个整 tick 即执行）
    // r2017（agent-review-2026-09-16 #3）：**tick 开头不再清账**——tick 窗口语义从「start-
    //   clear 到收口」改为「上一收口到本收口」（清账唯一落点 = 收口发布之后，见函数尾）：
    //   tick 收口后、下一 tick 开始前到达的编辑（World 全局信号 → noteEdit → m_edits）自然
    //   归入下一窗，在下一收口照常作为 WorldDelta + BlockChanged 事件发布，绝不静默丢失
    //   （event.h 纪律「事件丢弃必须被看见」）。旧码开头 clear() 会把跨收口边界的编辑无事件
    //   无 delta 无计数地整体抹除（r2017c 行为钉）。R20.09 纪律「Tick 内零通知、收口单点
    //   发布」不破——out-of-tick 编辑与 tick 内编辑走同一 EditBuffer 合并面、同一收口发布点。

    // 到期命令 drain（FIFO 保序；未到期暂存原序回队——回队数 ≤ 刚弹出数，push 恒成功）。
    QVector<Command> deferred;
    Command c;
    while (m_commands.pop(c)) {
        if (c.targetTick <= m_tick)
            executeCommand(c);
        else
            deferred.push_back(c);
    }
    for (const Command &d : deferred)
        m_commands.push(d);

    // World 模拟家族——Main.qml onTicked 桥接次序逐行镜像（头注①；次序即节拍语义，
    // 重排即行为漂移）。UI 域桥接（furnacePanel / progress / cloudDrift）不入会话。
    m_world.tickWaterFlow();
    m_world.tickLavaFlow();
    m_world.tickFire();
    m_world.tickCropGrowth();
    m_world.tickSugarcaneGrowth();
    m_world.tickFarmlandHydration();
    m_world.tickSaplingGrowth();
    m_world.tickSweetBerryBushGrowth();
    m_world.tickIceFreeze();
    m_world.tickIceMelt();
    m_world.tickRedstone();
    m_world.tickLeafDecay();
    m_world.tickWeather(Tick::kClockTickSecs); // 秒制口径 = WorldClock::ticked 携带值（0.1）

    // R20.09 收口：Tick 末发布一次 WorldDelta（takeDelta——合并面投影）+ 按合并编辑面派生
    // BlockChanged 事件（每格一条、id = 终态；逐写入队已成历史——重复通知由 EditBuffer 合并
    // 抑制）。事件满载丢弃可见（同旧逐条口径，计数器不变）。
    m_lastDelta = m_edits.takeDelta(m_tick);
    for (const BlockEdit &ed : m_edits.edits()) {
        Event e;
        e.kind = EventKind::BlockChanged;
        e.pos = ed.pos;
        e.blockId = ed.id;
        e.tick = m_tick;
        if (!m_events.push(e).isOk())
            ++m_droppedEvents;
    }

    emit tickCompleted(m_tick, m_lastDelta);

    // r2017：清账唯一落点 = 收口发布**之后**（emit 栈内消费者按 tickCompleted 同帧快照读
    //   lastDirtyChunks()——本行在信号栈外执行；r2009b/c 的集合面单源断言随窗口语义移入
    //   信号栈内钉，收口后账面恒空）。溢出累计跨清账保留（EditBuffer::clear 口径——不可
    //   再生必须可见，口径不变）。
    m_edits.clear();

    // §29.5-W2：流式泵拍（tick 尾——计划原文「GameSession tick 尾 driver.onPlayerChunk」；
    // 驱动编排归会话壳[R20.07]；sparse 会话才有驱动器，fixed 零动作）。落位为静默写（worldgen
    // 同门，不经 blockBroken/blockPlaced）→ 不触本 tick 已收口的编辑账面，次序在收口后无账面歧义。
    pumpStreamingTick();
}

inline void GameSession::executeCommand(const Command &c)
{
    switch (c.kind) {
    case CommandKind::BreakBlock:
        m_facade.setBlock(c.pos, quint8(BlockRegistry::Air));
        break;
    case CommandKind::PlaceBlock:
        // r2017（agent-review-2026-09-16 #1）：按 blockState 双路由——
        //   state == 0（默认/兼容面）走 4 参 setBlock：World 权威对「同 id」早退 no-op
        //   （state 保留、零信号零写后钩子）。MC 口径：放置只进 Air/可替换格，目标格已被
        //   同款**不可替换**方块占用 = 放置无效、原方块无变化（引证三元组：minecraft.wiki
        //   /w/Block_properties「replaceable」属性——"blocks placed on, against, or in the
        //   same location as the replaceable block replace it" + "Most blocks are not
        //   replaceable"，Java 版现行机制，2026-09-16 实读）。旧码此处直走 5 参，同 id 异
        //   state 格（楼梯朝向/上半砖/耕地湿度）被强刷 state=0 + 全套写后钩子 = 命令放置
        //   从「静默拒绝」变「改写既有方块形态」。4/5 参在「Air 格首放」「异 id 格替换」
        //   「同 id 同 state(0)」上行为逐位一致（都写 id,state=0）；本修唯一语义变化 = 同
        //   id 异 state 格从「强刷 0」回「no-op」（r2017a 三相钉）。
        //   state != 0（显式面）维持 5 参 setBlockWithState 全写（R20.09 权威落地不变，
        //   r2009d 回归柱）。
        if (c.blockState == 0)
            m_facade.setBlock(c.pos, c.blockId);
        else
            m_facade.setBlockWithState(c.pos, c.blockId, c.blockState);
        break;
    }
}

inline void GameSession::noteEdit(int x, int y, int z)
{
    // R20.09：改动后 id 经 Facade 回读（信号后栅格已是权威终态——破 = Air；Ice→水类特写
    // 同口径），登记委托 EditBuffer（同格合并后写胜 / 新格首记 / 满载丢弃可见）。
    // **Tick 内零通知**——事件由 runOneTick 收口统一派生（见上）。
    const quint8 afterId = m_facade.blockAt(x, y, z);
    if (m_edits.record(x, y, z, afterId) == RecordResult::Overflowed)
        ++m_droppedEdits;
}

// ── §29.5-W5 落盘/回灌执行体 + 保存时冲洗 + 读档合并（语义见类头注 W5 段）──────────────────

// 单 chunk 落盘唯一执行体（W3 驱逐缝与 W5 冲洗缝共用——persistFn lambda 只补轨迹）。
inline Result<void> GameSession::persistResidentChunk(int cx, int cz)
{
    const Chunk *c = m_world.chunks().chunk(cx, cz);
    if (!c || !m_chunkStore || !m_chunkStore->isBound())
        return Result<void>::fail(kErrChunkStoreNotBound,
                                  "persist: store unbound or chunk missing");
    const Result<void> r = m_chunkStore->persistChunk(cx, cz, *c);
    if (r.isOk())
        m_world.clearChunkUnsavedEdits(cx, cz); // 已落盘 → 不再 dirty
    return r;
}

// 回灌前弃槽：已物化（出生预生成占位 / blob 先行物化）→ ⑥⑦ 合法边 + 擦槽；未物化 no-op。
inline void GameSession::discardStreamingChunkForRestore(int cx, int cz)
{
    if (!m_world.chunks().chunkMaterialized(cx, cz))
        return; // 无槽可弃（restoreChunkFromBlob 自 ensure 物化）
    m_world.setChunkLifecycle(cx, cz, ChunkLifecycle::Evicting); // ⑥（驻留态唯一合法出口）
    m_world.setChunkLifecycle(cx, cz, ChunkLifecycle::Absent);   // ⑦
    m_world.releaseStreamingChunk(cx, cz); // 擦槽（数据面闭合——W3 转移缝同门）
}

// 保存时冲洗（主通路）：驻留 dirty 逐 chunk 落盘（盖写 = save_gen + 1）→ 代次推进（旧值 + 1
// = 恰为本批行盖写代次）。失败面逐 chunk 原子，聚合返回不谎报（任一失败或推进失败 = false；
// 账不清 = 下次保存重报收敛）。
inline bool GameSession::flushResidentEditsForSave()
{
    if (!m_chunkStore || !m_chunkStore->isBound() || !m_world.isSparse())
        return false; // 无冲洗域（fixed / 未 bind）——上报失败不谎报
    StreamWorldMeta meta;
    if (!m_chunkStore->readStreamWorldMeta(meta) || !meta.streaming)
        return false; // 未登记流式世界：无冲洗域（fixed 零标志零活动同门）
    bool allOk = true;
    // 驻留集枚举（ChunkManager sparse 版 = {Loaded, Active} 与驻留集同谓词，cz 外 cx 内序）。
    const QVector<QPair<int, int>> resident = m_world.chunks().sparseResidentKeysOrdered();
    for (const auto &k : resident) {
        if (!m_world.chunkHasUnsavedEdits(k.first, k.second))
            continue; // dirtyQuery 同驱逐缝：已生成未编辑的地形块不存（确定性重生成承载）
        if (persistResidentChunk(k.first, k.second).isOk())
            ++m_flushPersistedCount;
        else {
            ++m_flushFailedCount;
            allOk = false; // 失败上报（该 chunk 账不清；已成功者不回滚）
        }
    }
    // 代次推进收口：save := max(save, base) + 1 = 本批行的盖写代次（写不进 = 上报失败，
    // 已落盘行不因失号失效——仲裁只对 base_gen，marker 同门）。
    if (m_chunkStore->advanceStreamSaveGeneration() <= 0)
        allOk = false;
    return allOk;
}

// 读档合并（overlay 语义）：行全量回灌 + D3 核心区 blob 物化 + 代次对代次仲裁。
inline bool GameSession::loadStreamingWorld(WorldStore &store)
{
    if (!m_world.isSparse() || !m_chunkStore || !m_chunkStore->isBound())
        return false; // fixed 域 / 未 bind：无合并域
    if (store.world() != &m_world) {
        qWarning() << "GameSession::loadStreamingWorld: store not bound to this world"
                   << "- rebind required (r2010d discipline)";
        return false; // r2010d rebind 纪律防御（loadChunks 写入面 = store.m_world）
    }
    StreamWorldMeta meta;
    if (!m_chunkStore->readStreamWorldMeta(meta) || !meta.streaming)
        return false; // 非流式世界：caller 走既有 fixed 读档链
    QVector<ChunkStoreBlob> rows;
    if (m_chunkStore->loadAllRows(rows) < 0)
        qWarning() << "GameSession::loadStreamingWorld: chunk_edits scan failed"
                   << "- degrading to empty overlay"; // 诚实降级（读缝同门不抛不堵）

    // D3 blob 存在面（现有只读 Q_INVOKABLE 消费）→ 仲裁：行代次 ≥ base_gen（锚）→ 行胜
    //（新者胜；同代 = 表胜）；< → blob 胜（再转换锚让位的老时代行）。无 blob = 无对手方，
    // 行全胜（原生流式世界，chunks 表恒空）。
    const bool hasBlob = store.hasChunks();
    QSet<quint64> winners;
    for (const ChunkStoreBlob &row : rows) {
        const bool winsAgainstBlob = !hasBlob || row.generation >= meta.baseGen;
        if (winsAgainstBlob)
            winners.insert(ChunkKey{ row.cx, row.cz }.packed());
    }

    // ① D3 核心区 blob 物化（经现有只读 Q_INVOKABLE 面 loadChunks——worldstore 零改动）：
    //    核心域铺槽（仲裁胜者除外——槽缺席使 loadChunks 跳过该键，行回灌直落）→ loadChunks
    //    把 blob 字节 memcpy 进槽位 → 逐槽「取字节 → 非零判定 → 弃槽 → restoreChunkFromBlob
    //    物化」（restore = W3 物化链权威：①②③ + heightmap + 索引 + 列种子光，跳 population
    //    ——存档内容已是终态）。全零槽 = 部分 blob（尺寸守卫跳过/缺行）→ 原样留存走重生成
    //   （worldstore 尺寸守卫降级同门，不硬造空 chunk）。
    if (hasBlob) {
        const int coreCX = (meta.coreW + Chunk::kSize - 1) / Chunk::kSize;
        const int coreCZ = (meta.coreD + Chunk::kSize - 1) / Chunk::kSize;
        for (int cz = 0; cz < coreCZ; ++cz)
            for (int cx = 0; cx < coreCX; ++cx) {
                if (winners.contains(ChunkKey{ cx, cz }.packed()))
                    continue; // 仲裁胜者：不铺槽（blob 让位，行回灌直落）
                if (!m_world.chunks().chunkMaterialized(cx, cz))
                    m_world.ensureStreamingChunkSlot(cx, cz);
            }
        store.loadChunks(); // 现有只读 Q_INVOKABLE：blob 字节 memcpy 进已存在槽位
        for (int cz = 0; cz < coreCZ; ++cz)
            for (int cx = 0; cx < coreCX; ++cx) {
                if (winners.contains(ChunkKey{ cx, cz }.packed()))
                    continue;
                const Chunk *c = m_world.chunks().chunk(cx, cz);
                if (!c)
                    continue; // 防御（未铺槽 = 仲裁胜者）
                const size_t n = c->voxelCount();
                const QByteArray voxels(reinterpret_cast<const char *>(c->voxelData()), int(n));
                const QByteArray states(reinterpret_cast<const char *>(c->stateData()), int(n));
                const QByteArray light(reinterpret_cast<const char *>(c->lightData()), int(n));
                bool filled = false;
                for (const char byte : voxels)
                    if (byte != 0) {
                        filled = true;
                        break;
                    }
                if (!filled)
                    continue; // 零填充槽 = blob 未覆盖（部分 blob 降级）→ 留 Absent 走重生成
                discardStreamingChunkForRestore(cx, cz); // 出生预生成占位让位（seed 可重derive）
                if (m_world.restoreChunkFromBlob(cx, cz, voxels, states, light))
                    ++m_loadBlobChunkCount;
                else
                    qWarning() << "GameSession::loadStreamingWorld: blob materialize failed for"
                               << cx << cz << "- chunk stays regenerable";
            }
    }

    // ② 行全量回灌（仲裁胜者；核心外自然覆盖）：blob 命中路由同门（W3 收割拍路由的读档版）。
    for (const ChunkStoreBlob &row : rows) {
        if (!winners.contains(ChunkKey{ row.cx, row.cz }.packed()))
            continue; // 仲裁败者（老时代行）：blob / 重生成权威
        discardStreamingChunkForRestore(row.cx, row.cz); // 出生预生成占位让位
        if (m_world.restoreChunkFromBlob(row.cx, row.cz, row.voxels, row.states, row.light))
            ++m_loadRestoredRowCount;
        else
            qWarning() << "GameSession::loadStreamingWorld: row restore failed for" << row.cx
                       << row.cz << "- degrading to regeneration";
    }
    return true;
}

// §29.5-W2 流式泵拍（tick 尾；语义见声明处头注释）。review0916 #7 硬契约的兑现点在双面
// 循环的**同拍并列**——两面各自独立 pop（takeCompletedAsync 已在 pump 内被 scheduler 消费、
// 转译为每活别名一条 outcome；takeResultData 与其同源同序、每完成 job 恰一条自持缓冲），
// 只收一面 = 另一面无界积压（#7 原文）——故两面必须在同一收割拍内全部排干。
inline void GameSession::pumpStreamingTick()
{
    if (!m_streamDriver)
        return; // fixed 世界零驱动器（连构造都不发生）——D2 零活动墙
    // ① 位置沿喂驱动器（变更沿幂等在驱动器：同 chunk 重复喂零动作、首喂即沿）。
    if (m_hasPlayerChunk)
        m_streamDriver->onPlayerChunk(m_playerChunkCx, m_playerChunkCz);
    // ② 泵转发：同步 worker 内联直跑 / 异步 worker「交接 + 收割」两相自动路由（交接相边①、
    //    收割相边②由 GenerationJob 唯一权威驱动——r2011 预留面 + r2017 kind 门语义原样）。
    m_streamDriver->pump();
    // ③ #7 同拍双面·结果面（outcome 每活别名一条；本会话只记账——投递面业务消费归后续单）。
    GenerationJobOutcome outcome;
    while (m_streamDriver->takeOutcome(outcome))
        ++m_streamOutcomeCount;
    // ④ #7 同拍双面·数据面（漏取 = m_data 无界积压——#7 原文）：主线程落位唯一通路。
    //    §29.5-W3 路由：附加表命中（= 该 chunk 曾驱逐落盘，行只增不删故命中稳定）→
    //    World::restoreChunkFromBlob 直接物化（跳过 population——存档内容已是终态，头注立证）；
    //    未命中 → W2 现行 adoptGeneratedChunk 生成路径。命中但 blob 读回失败（病/尺寸守卫拒）
    //    → 降级生成路径并告警（诚实降级 + 驱逐面已保内容，无正确性损失面——重载内容退化为
    //    重derive 是已登记的尺寸守卫语义[worldstore loadChunks 同门]）。
    std::unique_ptr<GeneratedChunkData> data;
    while (m_streamWorker->takeResultData(data)) {
        if (data) {
            ChunkStoreBlob blob;
            const bool stored = m_chunkStore && m_chunkStore->isBound()
                && m_chunkStore->hasChunk(data->key.cx, data->key.cz)
                && m_chunkStore->loadChunk(data->key.cx, data->key.cz, blob);
            bool restored = false;
            if (stored) {
                restored = m_world.restoreChunkFromBlob(data->key.cx, data->key.cz, blob.voxels,
                                                        blob.states, blob.light);
                if (!restored)
                    qWarning() << "GameSession: chunk_edits blob restore failed for"
                               << data->key.cx << data->key.cz << "- degrading to regeneration";
            }
            if (!restored)
                m_world.adoptGeneratedChunk(data->key.cx, data->key.cz, *data);
        }
        ++m_streamAdoptedCount;
    }
    // ⑤ W4 网格收割拍（tick 尾单点收口——「收割拍单点收口」r2026d 钉面；与 ④ 数据面同拍
    //    并列排干，防收割队列无界积压）：每被接受请求恰一条 built（恰一产出铁律）→ 按
    //    requestId 经 World 注册表路由回几何（交付回调内自证最新在途键 = latest-wins 第二道
    //    闸；miss = 已取消/几何已亡 → 可见丢弃计数）。主线程非阻塞轮询：延迟有界 ≤ 一个
    //    tick、零 QML 改动。fixed 世界无执行器 = 本段零动作（D2 零活动墙）。
    if (m_meshWorker) {
        MeshBuiltItem built;
        while (m_meshWorker->takeBuilt(built)) {
            m_world.deliverBuiltChunkMesh(built.requestId, std::move(built.mesh));
            ++m_meshHarvestedCount;
        }
    }
}

#endif // GAMESESSION_H
