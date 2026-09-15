#ifndef EVENT_H
#define EVENT_H

// R20.06 事件体系（refactor-plan §4.2 + §29.3 R20.06 + 「EditBuffer / WorldDelta」节）：
// Event = 「已经发生了什么」——模拟核心产生，UI 提示 / 音频 / 粒子 / 网络广播 / 统计 /
// 日志消费；事件**不驱动核心规则的隐式重入**（plan §4.2 原文），规则只在明确 Tick 顺序中
// 执行。**Event 不携带 QObject**（本单验收原文）：result.h 的 QObjectFree 概念 + 本文件
// static_assert 编译期钉死——Event / WorldDelta 成员一旦引入 QObject 派生子对象即编译
// 失败（「摘即红」在类型层成立，阴性轮豁免的替代证据之一，矩阵 r2006b 腿内复钉）。
// WorldDelta = 一次 Tick 编辑面的「受影响 chunk 集」表达（本单验收锚：「WorldDelta 可以
// 表达受影响 Chunk」）；plan 的完整 WorldDelta（changed block ranges / light regions /
// block-entity-sound event 明细）的**类型收口**已由 R20.09 EditBuffer 正席承担
//（editbuffer.h——DirtyChunkSet 独立集合类型 + takeDelta 单点投影），ranges/light 明细
// 仍登记 R20.13 MeshBuilder 消费侧接线时再立；本类型保持 chunk 集 + 改动量汇总的最小
// 骨架不变（r2006-r2009 存量契约零破坏）。
// 事件队列 = 单线程 FIFO；容量策略与命令队列同门：固定上界 + 满载拒绝（事件不静默丢失；
// 对照 SnapshotQueue「满则覆盖最老」的刻意分化——事件是已发生事实不可再生，快照可再生）。
// 分层（PLAN §2）：Core 叶子——mathtypes.h（ChunkKey/BlockPos）+ result.h + <array>/
// <deque>，不依赖 World/Renderer/Game/Entities。

#include "mathtypes.h" // ChunkKey（受影响 chunk 集）/ BlockPos（方块域事件位置）
#include "result.h"    // Result<void>（满载拒绝失败面）+ QObjectFree（值纪律编译期钉）

#include <QtGlobal> // quint8 / quint16 / quint32

#include <array>
#include <deque>

// ── EventKind：事件种别（plan §4.2 事件例单全席落位；矩阵 r2006b 编译期点名钉存在性）──
enum class EventKind : quint8
{
    BlockChanged = 0, // 主例：方块改动（pos + 改动后 blockId；EditBuffer 主事件）
    EntitySpawned,
    EntityDied,
    ItemDropped,
    SoundRequested,
    ChunkActivated,
    SaveCompleted,
};

// ── Event：一条已发生事实（纯数据值类型；kind 决定字段解释域）──────────────────────
struct Event
{
    EventKind kind = EventKind::BlockChanged;
    BlockPos pos{};       // 方块域位置（BlockChanged / SoundRequested / ChunkActivated）
    quint8 blockId = 0;   // BlockChanged：改动后 id（旧值/范围化明细登记 R20.13 MeshBuilder 消费侧）
    quint32 entityId = 0; // 实体域（Spawned / Died / Dropped；0 = 非实体域）
    quint16 aux = 0;      // 小型副参（SoundRequested 音效 id 等；按 kind 解释）
    int tick = 0;         // 产生时固定 Tick（消费方时效判定 / 重放排序）
};

// 验收原文钉：「Event 不携带 QObject」+ 平凡可拷贝（安全过队列 / 未来跨线程面）。
static_assert(QObjectFree<Event>, "Event must not carry QObject (plan §29.3 R20.06)");
static_assert(std::is_trivially_copyable_v<Event>,
              "Event stays trivially copyable (queue-safe value semantics)");

// ── WorldDelta：一次 Tick 的世界改动面（受影响 chunk 集 = 本单验收锚）────────────────
// 表达形态：固定容量数组 + 有效前缀长度（Core 叶子不引 Qt 容器 / 无堆分配；64 = 10×10
// rig 网格全图单 Tick 全脏仍富余的上界）。登记幂等去重：同一 chunk 重复登记不占位——
// plan R20.09「多次相邻编辑不会产生不必要的重复通知」验收的本类型层前奏。
struct WorldDelta
{
    static constexpr int kMaxAffectedChunks = 64;

    std::array<ChunkKey, kMaxAffectedChunks> affected{};
    int affectedCount = 0; // 有效前缀长度（0..kMaxAffectedChunks）
    int changedBlocks = 0; // 汇总口径：本 delta 合并后编辑格数（R20.09 EditBuffer.takeDelta 产；
                           //   ranges 化登记 R20.13 MeshBuilder 消费侧）
    int tick = 0;          // 产出时固定 Tick

    // 登记受影响 chunk：新登记 true；已在集内（幂等去重，不占位）或集满（不静默挤出）
    // 返回 false。重复与满载共用 false——**细分语义已由 R20.09 DirtyChunkSet 三态收口**
    //（editbuffer.h ChunkAddResult：Added/AlreadyDirty/Full）；本类型保持共用 false 的
    // 保守面不变（r2006-r2008 存量消费方零改动——调用方需区分时改用 DirtyChunkSet）。
    bool addAffected(const ChunkKey &k)
    {
        if (affectedCount >= kMaxAffectedChunks || affects(k))
            return false;
        affected[affectedCount++] = k;
        return true;
    }

    // 线性成员判定（64 上界内 O(n) 足够；无序集优化留真实接入后的 profile 证据再议）。
    bool affects(const ChunkKey &k) const
    {
        for (int i = 0; i < affectedCount; ++i)
            if (affected[i] == k)
                return true;
        return false;
    }

    void clear()
    {
        affectedCount = 0;
        changedBlocks = 0;
        tick = 0;
    }
};

static_assert(QObjectFree<WorldDelta>, "WorldDelta must not carry QObject (plan §29.3 R20.06)");

// ── EventQueue：单线程 FIFO 事件队列（容量策略同 CommandQueue：上界拒绝不覆盖）──────
class EventQueue
{
public:
    static constexpr int kCapacity = 256;

    // 入队；满载返回 fail(kErrQueueFull)（事件是已发生事实，丢弃必须被看见）。
    Result<void> push(const Event &e)
    {
        if (int(m_items.size()) >= kCapacity)
            return Result<void>::fail(kErrQueueFull, "event queue full");
        m_items.push_back(e);
        return Result<void>::ok();
    }

    // 出队（FIFO 最老先出）；空队列返回 false（正常态，非错误）。
    bool pop(Event &out)
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
    std::deque<Event> m_items;
};

#endif // EVENT_H
