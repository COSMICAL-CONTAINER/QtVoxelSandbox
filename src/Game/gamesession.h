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
// 分层（PLAN §2）：Game 层编排壳——向下依赖 World（tick 家族 + setBlock 权威）+ Core
//（command/event/mathtypes/result），不依赖 Renderer/Entities/QML；不反向被 World 依赖。

#include "chunk.h"     // Chunk::kSize（chunk 路由参数——单一权威，不写魔法 16）
#include "command.h"   // Command / CommandQueue（R20.06 队列——GameSession 首个生产消费方）
#include "editbuffer.h" // R20.09 EditBuffer / DirtyChunkSet（Tick 内编辑合并收口——本类首个消费方）
#include "event.h"     // Event / WorldDelta / EventQueue（编辑面表达）
#include "mathtypes.h" // Tick::kClockTickMs / BlockPos / ChunkKey
#include "result.h"    // Result<void>（满载拒绝失败面穿透）
#include "world.h"     // World：tick 家族（模拟泵——见下「选型」）+ setBlock 写实现权威
#include "worldfacade.h" // R20.08 WorldFacade：查询/写入收窄面（命令写 + 编辑后回读走此面）

#include <QDebug>  // qWarning（超界 dt 丢弃——背压可见，t1050）
#include <QObject>
#include <QVector> // 未到期命令暂存（drain-then-replay；容量受 CommandQueue::kCapacity 上界）

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
    const DirtyChunkSet &lastDirtyChunks() const { return m_edits.dirtyChunks(); }

    // 事件观察面（BlockChanged 每合并编辑格一条、Tick 末统一入队；EventQueue 满载丢弃
    // 必须可见 → 计数器暴露）。EditBuffer 记录面满载丢弃同门可见（droppedEditCount）。
    EventQueue &events() { return m_events; }
    int droppedEventCount() const { return m_droppedEvents; }
    int droppedEditCount() const { return m_droppedEdits; }
    // t1050：超界 dt（> kMaxStepSecs）整体丢弃累计——背压可见（真机 Adapter 接墙钟后异常
    // 墙钟差频度可观测；矩阵腿 r2007e 断言丢弃 + 计数）。
    int droppedDtCount() const { return m_droppedDts; }

signals:
    // 每整 tick 收口发（tick = 已完成 tick 号；delta = 本 tick 编辑面快照）。
    void tickCompleted(int tick, const WorldDelta &delta);

private:
    // 一个整 tick：tick 号推进 → 到期命令 drain（FIFO 保序；未到期原序回队——回队 push
    // 恒成功：刚腾出的空位 ≥ 回队数）→ World 模拟家族（Main.qml 桥接次序逐行镜像）→
    // EditBuffer 收口（takeDelta + 按合并编辑面派生 BlockChanged 事件）+ 信号。
    void runOneTick();
    // 命令执行（**委托不复制**）：BreakBlock → setBlock(pos, Air)、PlaceBlock →
    // setBlockWithState(pos, blockId, blockState)——经 WorldFacade 收窄面落 World::setBlock
    //「写栅格的唯一入口」权威（R20.08 迁移：命令写走 Facade；R20.09 落 Review_2026-09-15
    // #3①：PlaceBlock 带 state 五参权威落地——带朝向/半砖态方块不再静默丢 state，默认 0
    // 向后兼容）。越界 / 无变化由 World 权威语义静默拒绝（同玩家前门路径的行为面）。
    void executeCommand(const Command &c);
    // 编辑登记（blockBroken/blockPlaced 回调 → R20.09 EditBuffer 委托）：改动后 id 回读 +
    // record（同格合并 / 新格首记 / 满载丢弃可见）。**Tick 内零通知**——BlockChanged 事件
    // 由 runOneTick 收口按合并编辑面统一派生（每格一条、id=终态——验收①「不产生不必要的
    // 重复通知」的会话面承担点）。
    void noteEdit(int x, int y, int z);

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
    m_edits.clear(); // R20.09：Tick 内编辑面从零累积（溢出累计跨 tick 保留——不可再生可见）

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
}

inline void GameSession::executeCommand(const Command &c)
{
    switch (c.kind) {
    case CommandKind::BreakBlock:
        m_facade.setBlock(c.pos, quint8(BlockRegistry::Air));
        break;
    case CommandKind::PlaceBlock:
        // R20.09（Review_2026-09-15 #3①）：id+state 五参权威落地——带朝向/半砖态方块经
        // 命令放置不再被 4 参版静默重置 state=0（blockState 默认 0 向后兼容旧提交方）。
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

#endif // GAMESESSION_H
