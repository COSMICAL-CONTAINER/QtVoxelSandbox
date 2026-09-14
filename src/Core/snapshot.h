#ifndef SNAPSHOT_H
#define SNAPSHOT_H

// R20.06 快照体系（refactor-plan §4.2「Snapshot」+ §29.3 R20.06）：Snapshot = 「某个时间
// 点可供外部观察的状态」。plan §4.2 四纪律逐项落位：自包含 = 全值成员（tick / 尺度元数据
// / 自持改动面 vector）；不携带 QObject = result.h QObjectFree 编译期钉；不引用可能失效
// 的内存 = 无指针 / 无引用成员（edits 自持，快照对象存活期外无别名）；对外不可变 = 消费
// 面只读（本胚无任何可变观察口；采集面 addEdit 属生产侧，R20.08 WorldFacade 接线后才由
// World 填充）。本单落最小 WorldSnapshot 胚（改动面式——稠密方块域快照是 R20.13
// MeshBuilder 消费侧的事）；「可独立复制」= 拷贝即深拷贝（vector 值语义），矩阵 r2006c
// 双向独立性行级证明（本单验收原文「Snapshot 可以独立复制和测试」）。
// 快照队列 = 单线程定容环形，容量策略与命令 / 事件队列**刻意不同**：满则覆盖最老（渲染
// 只插值两个已完成的 Simulation Snapshot——plan §4.2 原文；陈旧快照无消费价值，覆盖优于
// 拒绝，更不该以「错误」打扰生产者；事件不可再生故拒绝、快照可再生故覆盖——两域分化见
// event.h 头注）。可跨线程传递（plan §4.2 纪律）依赖的正是本胚的无 QObject / 全值形态，
// 多线程队列本体留 R20 后续任务。
// 分层（PLAN §2）：Core 叶子——mathtypes.h（BlockPos）+ result.h + <array>/<vector>。

#include "mathtypes.h" // BlockPos
#include "result.h"    // QObjectFree（值纪律编译期钉）

#include <QtGlobal> // quint8

#include <array>
#include <vector>

// ── BlockEdit：快照改动面记录单元（词表对齐 plan「EditBuffer…1. 记录 BlockEdit」）────
struct BlockEdit
{
    BlockPos pos{}; // 改动格
    quint8 id = 0;  // 改动后方块 id
};

// ── WorldSnapshot：世界观察态最小胚（自持 / 可独立复制 / 无 QObject）─────────────────
struct WorldSnapshot
{
    int tick = 0;  // 快照对应固定 Tick（渲染插值两快照的时间基准）
    int width = 0; // 世界尺度元数据（自包含观察面；真实值由采集方填——默认世界 96×180×48）
    int depth = 0;
    int height = 0;
    std::vector<BlockEdit> edits{}; // 自持改动面（拷贝 = 深拷贝 → 复制后与源独立）

    // 生产侧采集口（R20.08 WorldFacade 前仅测试 / 未来采集器使用；消费侧无对应写口）。
    void addEdit(const BlockPos &p, quint8 id) { edits.push_back(BlockEdit{ p, id }); }

    // 全量复位（观察面归零 = 等价 fresh 快照）。
    void clear()
    {
        tick = 0;
        width = 0;
        depth = 0;
        height = 0;
        edits.clear();
    }
};

// 值纪律编译期钉（同 Command/Event 面）。WorldSnapshot 含 vector → 非平凡可拷贝是**设计
// 事实**（自持深拷贝面），故钉「可拷贝构造 / 可赋值」而非 trivially copyable。
static_assert(QObjectFree<WorldSnapshot>, "WorldSnapshot must not carry QObject (plan §4.2)");
static_assert(std::is_copy_constructible_v<WorldSnapshot>
                  && std::is_copy_assignable_v<WorldSnapshot>,
              "WorldSnapshot must be independently copyable (plan §29.3 R20.06)");

// ── SnapshotQueue：单线程定容环形快照队列（满则覆盖最老——容量策略见头注）────────────
class SnapshotQueue
{
public:
    static constexpr int kCapacity = 4; // 渲染插值仅需最新两帧（plan §4.2）；4 = 观察余量

    // 入队（恒成功）：满载覆盖最老（快照域语义——最新即权威，旧帧让位）。
    void push(const WorldSnapshot &s)
    {
        if (m_size < kCapacity) {
            m_slots[(m_head + m_size) % kCapacity] = s;
            ++m_size;
        } else {
            m_slots[m_head] = s; // 满载时尾 == 头：写头槽 = 覆盖最老，再推进头游标
            m_head = (m_head + 1) % kCapacity;
        }
    }

    // 出队（FIFO 最老先出——与覆盖方向自洽）；空返回 false（正常态，非错误）。
    bool pop(WorldSnapshot &out)
    {
        if (m_size == 0)
            return false;
        out = m_slots[m_head];
        m_head = (m_head + 1) % kCapacity;
        --m_size;
        return true;
    }

    void clear()
    {
        m_head = 0;
        m_size = 0;
    }
    int size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    bool isFull() const { return m_size == kCapacity; }

private:
    std::array<WorldSnapshot, kCapacity> m_slots{};
    int m_head = 0; // 最老元素槽位
    int m_size = 0;
};

#endif // SNAPSHOT_H
