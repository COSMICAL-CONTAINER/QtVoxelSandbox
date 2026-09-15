#ifndef COMMAND_H
#define COMMAND_H

// R20.06 命令体系（refactor-plan §4.2「命令、事件、快照三分法」+ §29.3 R20.06）：Command =
// 「想做什么」——由输入 / 网络 / 脚本生成的意图数据（区别于 Event「已经发生了什么」与
// Snapshot「某时间点可供外部观察的状态」，三分法不混装）。本单契约 =「先只定义数据结构和
// 队列，不大规模迁移调用点」：**不接玩家输入生产路径**（QML / PlayerController 面零改动），
// R20.07 GameSession（固定 Tick / 暂停 / BreakBlock / PlaceBlock / WorldDelta 迁移清单）才
// 是首个生产消费方。plan §4.2「Command 必须带」六项逐项落位：来源 = actorId、目标 Tick =
// targetTick、相关 EntityId = actorId（quint32 粗粒度，R20.14 EntityStore 立强类型时收口）、
// 输入序号 = sequence、必要参数 = pos + blockId、校验所需最小上下文 = pos（空间合法性判定
// 的最小信息）。验收锚：「BreakBlock 可以通过 Command 表达」（breakBlock 工厂 + 矩阵
// r2006a 行级证明）。
// 队列 = 单线程 FIFO（无锁——多线程队列留 R20 后续任务登记，届时只换实现不改调用形状）；
// 容量策略 = 固定上界 + 满载拒绝（Result<void> + kErrQueueFull，不覆盖 / 不静默挤出——
// 命令被丢弃必须被提交方看见，静默丢指令是玩法腐坏源；对照 SnapshotQueue 的「满则覆盖
// 最老」是**不同域的刻意分化**，见 snapshot.h 头注）。
// 分层（PLAN §2）：Core 叶子——只依赖 mathtypes.h（BlockPos）+ result.h（Result/错误码/
// QObjectFree 值纪律）+ <deque>，不依赖 World/Renderer/Game/Entities。

#include "mathtypes.h" // BlockPos（命令目标格——R20.05 基础类型的第一个新体系消费面）
#include "result.h"    // Result<void>（满载拒绝失败面）+ QObjectFree（值纪律编译期钉）

#include <QtGlobal> // quint8 / quint32

#include <deque>

// ── CommandKind：命令种别（本单代表集；plan §4.2 例单 MovePlayer/UseItem/AttackEntity 等
//    按 R20.07+ 迁移面渐进扩充，不预占席位）───────────────────────────────────────────
enum class CommandKind : quint8
{
    BreakBlock = 0, // 本单验收锚（plan §29.3 R20.06「BreakBlock 可以通过 Command 表达」）
    PlaceBlock = 1, // R20.07 GameSession 迁移清单第二席（与 Break 对称的最小写入命令）
};

// ── Command：一条待执行意图（纯数据值类型：无 QObject / 无信号 / 无回调 / 平凡可拷贝）──
// R20.09 增补（Review_2026-09-15 #3①）：blockState 字段——PlaceBlock 此前恒走 4 参 setBlock
//（state 派生 0），带朝向/半砖/湿度态的方块经命令放置会**静默丢 state**；扩字段后执行方
// 转五参权威（gamesession.h executeCommand → Facade::setBlockWithState），默认 0 向后兼容
// 全部既有提交方（r2006a/r2007 腿零改动）。
struct Command
{
    CommandKind kind = CommandKind::BreakBlock;
    BlockPos pos{};       // 目标格（负坐标同等合法——floorDiv 路由语义是执行方的职责）
    quint8 blockId = 0;   // PlaceBlock：放置 id；BreakBlock 忽略（恒 0）
    quint8 blockState = 0; // PlaceBlock：放置 state（朝向/半砖/湿度等；默认 0 = 旧口径）；BreakBlock 忽略
    quint32 actorId = 0;  // 来源者 EntityId（0 = 系统 / 服务器）
    quint32 sequence = 0; // 输入 / 客户端序号（提交侧单调递增；重放判定预留）
    int targetTick = 0;   // 目标固定 Tick（Tick::kClockTickMs 节拍；0 = 尽快执行）

    // BreakBlock 表达工厂：验收锚的规范姿势（blockId/blockState 占 0，显式字段齐备）。
    static Command breakBlock(const BlockPos &p, quint32 actor, quint32 seq, int tick)
    {
        return Command{ CommandKind::BreakBlock, p, 0, 0, actor, seq, tick };
    }

    // PlaceBlock 同形工厂（显式字段次序对齐 breakBlock，调用点可对称替换）；state 缺省 0
    // = 向后兼容（五参调用点语义与四参 setBlock 旧口径逐位一致）。
    static Command placeBlock(const BlockPos &p, quint8 id, quint32 actor, quint32 seq, int tick,
                              quint8 state = 0)
    {
        return Command{ CommandKind::PlaceBlock, p, id, state, actor, seq, tick };
    }
};

// 值纪律编译期钉（result.h QObjectFree 的体系内落位）：命令必须可安全跨队列 / 跨域携带
// ——成员一旦引入 QObject 派生子对象即编译失败（「摘即红」性质的正向面）。
static_assert(QObjectFree<Command>, "Command must be a QObject-free value type (plan §4.2)");
static_assert(std::is_trivially_copyable_v<Command>,
              "Command stays trivially copyable (queue-safe value semantics)");

// ── CommandQueue：单线程 FIFO 命令队列 ────────────────────────────────────────────
// 语义面（本单验收）：push（满载拒绝，失败携带 kErrQueueFull）/ pop（FIFO 最老先出；空 =
// 正常态返回 false，非错误——错误码只留给「数据会丢」的满载面）/ clear / size。
// 容量策略：固定上界 kCapacity（256 ≈ 20Hz × 12.8s 的意图洪峰缓冲；不动态增长——无界
// 队列是慢生产者的滞后放大器，满载即背压信号，扩容是 R20.07 接线后的调参面）。
class CommandQueue
{
public:
    static constexpr int kCapacity = 256;

    // 入队；满载返回 fail(kErrQueueFull)（调用方 isOk() 判定——满载必须被看见）。
    Result<void> push(const Command &c)
    {
        if (int(m_items.size()) >= kCapacity)
            return Result<void>::fail(kErrQueueFull, "command queue full");
        m_items.push_back(c);
        return Result<void>::ok();
    }

    // 出队（FIFO 最老先出）；空队列返回 false（正常态——与满载的「错误」语义刻意区分）。
    bool pop(Command &out)
    {
        if (m_items.empty())
            return false;
        out = m_items.front();
        m_items.pop_front();
        return true;
    }

    void clear() { m_items.clear(); }
    int size() const { return int(m_items.size()); }
    bool isEmpty() const { return m_items.empty(); }
    bool isFull() const { return int(m_items.size()) >= kCapacity; }

private:
    std::deque<Command> m_items;
};

#endif // COMMAND_H
