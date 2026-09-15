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
//    带坐标编辑事件 → 本类在事件回调里累积受影响 ChunkKey（ChunkKey::fromWorld +
//    Chunk::kSize，幂等去重）+ 改动量 → 每 Tick 一个 WorldDelta。**已知边界（登记）**：
//    流体蔓延等静默写只发无参 worldChanged、不带坐标 → 不入本 delta；per-tick 编辑面收口
//    是 R20.09 EditBuffer 的正席，本类只表达「命令驱动 + 语义事件可见」的改动面。
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
// 分层（PLAN §2）：Game 层编排壳——向下依赖 World（tick 家族 + setBlock 权威）+ Core
//（command/event/mathtypes/result），不依赖 Renderer/Entities/QML；不反向被 World 依赖。

#include "chunk.h"     // Chunk::kSize（chunk 路由参数——单一权威，不写魔法 16）
#include "command.h"   // Command / CommandQueue（R20.06 队列——GameSession 首个生产消费方）
#include "event.h"     // Event / WorldDelta / EventQueue（编辑面表达）
#include "mathtypes.h" // Tick::kClockTickMs / BlockPos / ChunkKey
#include "result.h"    // Result<void>（满载拒绝失败面穿透）
#include "world.h"     // World：tick 家族（模拟泵——见下「选型」）+ setBlock 写实现权威
#include "worldfacade.h" // R20.08 WorldFacade：查询/写入收窄面（命令写 + 编辑后回读走此面）

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

    // 事件观察面（BlockChanged 每 edit 一条；EventQueue 满载丢弃必须可见 → 计数器暴露）。
    EventQueue &events() { return m_events; }
    int droppedEventCount() const { return m_droppedEvents; }

signals:
    // 每整 tick 收口发（tick = 已完成 tick 号；delta = 本 tick 编辑面快照）。
    void tickCompleted(int tick, const WorldDelta &delta);

private:
    // 一个整 tick：tick 号推进 → 到期命令 drain（FIFO 保序；未到期原序回队——回队 push
    // 恒成功：刚腾出的空位 ≥ 回队数）→ World 模拟家族（Main.qml 桥接次序逐行镜像）→
    // delta 收口 + 信号。
    void runOneTick();
    // 命令执行（**委托不复制**）：BreakBlock → setBlock(pos, Air)、PlaceBlock → setBlock(pos,
    // blockId)——经 WorldFacade 收窄面落 World::setBlock「写栅格的唯一入口」权威（R20.08 迁移：
    // 命令写走 Facade；权威仍是 World::setBlock，全套写后钩子 / 语义事件照走）。
    // 越界 / 无变化由 World 权威语义静默拒绝（同玩家前门路径的行为面）。
    void executeCommand(const Command &c);
    // 编辑登记（blockBroken/blockPlaced 回调）：受影响 chunk 幂等入集 + 改动量 + BlockChanged
    // 事件入队。事件 blockId = 改动后 id（信号后回读栅格——破带旧 id、改后 id 以权威为准）。
    void noteEdit(int x, int y, int z);

    World &m_world;
    // R20.08 WorldFacade（示范迁移：新代码经收窄面读写世界）：查询/写入走 m_facade（命令写
    //   setBlock + 编辑后回读 blockAt），tick 模拟泵家族仍直调 m_world（模拟泵非查询/写面，
    //   Facade 不收拢——选型登记于 docs R20.08 关单；r2008d 阴性钉守「命令零旁路」）。
    WorldFacade m_facade;
    CommandQueue m_commands;
    EventQueue m_events;
    WorldDelta m_lastDelta;
    int m_tick = 0;
    int m_accumMs = 0;    // 整数毫秒累积器（余量跨 stepTick 保留；暂停丢弃——见头注）
    bool m_paused = false;
    int m_droppedEvents = 0;
};

inline GameSession::GameSession(World &world, QObject *parent)
    : QObject(parent)
    , m_world(world)
    , m_facade(world)
{
    // 编辑语义事件 → WorldDelta 构造点（带坐标的仅此两路，见头注③④）。直接连接同步登记
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
    m_lastDelta.clear();
    m_lastDelta.tick = m_tick;

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

    emit tickCompleted(m_tick, m_lastDelta);
}

inline void GameSession::executeCommand(const Command &c)
{
    switch (c.kind) {
    case CommandKind::BreakBlock:
        m_facade.setBlock(c.pos, quint8(BlockRegistry::Air));
        break;
    case CommandKind::PlaceBlock:
        m_facade.setBlock(c.pos, c.blockId);
        break;
    }
}

inline void GameSession::noteEdit(int x, int y, int z)
{
    m_lastDelta.addAffected(ChunkKey::fromWorld(x, z, Chunk::kSize)); // 幂等去重（重复不占位）
    ++m_lastDelta.changedBlocks;
    Event e;
    e.kind = EventKind::BlockChanged;
    e.pos = BlockPos{ x, y, z };
    e.blockId = m_facade.blockAt(x, y, z); // 改动后 id（破 = Air；Ice→水类特写按权威栅格为准）
    e.tick = m_tick;
    if (!m_events.push(e).isOk())
        ++m_droppedEvents; // 事件不可再生——满载丢弃必须可见（对比快照域的覆盖语义）
}

#endif // GAMESESSION_H
