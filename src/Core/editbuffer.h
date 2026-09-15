#ifndef EDITBUFFER_H
#define EDITBUFFER_H

// R20.09 EditBuffer（refactor-plan §5.2「EditBuffer」+ §29.3 R20.09「把一次 Tick 内的方块修改
// 合并成 WorldDelta」）：Tick 内编辑累积器——系统在 Tick 内**不逐写发通知**，而是
//   1. 记录 BlockEdit（词表对齐 plan「1. 记录 BlockEdit」——单元类型即 snapshot.h 既有面）；
//   2. 合并相邻/重复修改（同格同 Tick 多写合一、后写胜——终态 id 权威）；
//   3. 形成 DirtyChunkSet（受影响 chunk 幂等集合）；
//   4. Tick 结束后发布一次 WorldDelta（takeDelta——R20.06 WorldDelta 的单点生产者）。
// 验收对照（§29.3 R20.09 原文四条）：①「多次相邻编辑不会产生不必要的重复通知」= record 的
// Merged 语义（同格重复写只占一条 BlockEdit / 一个 dirty chunk / tick 末一条事件——通知去重
// 由本类型承担，消费方按 Recorded 才发通知）；②「规则终态与旧实现一致」= 本类型**只记录不
// 写入**（写入权威仍是 World::setBlock 家族，GameSession 语义事件回调后登记）——终态由
// 后写胜合并如实表达（r2009b 行为腿钉）；③「DirtyChunkSet 可被测试」= 独立类型 + 纯值面
//（add/contains/at/clear 全可断言，r2009a 类型腿直测）；④「mesh 调度不再直接绑定每一次
// setBlock」= 类型层前提（脏信息以**集合**形态在 Tick 末一次可查 dirtyChunks()/takeDelta，
// 不再逐 setBlock 粒度）；GameSession 面的批量化示范见 gamesession.h lastDirtyChunks()，
// ChunkGeometry/QML 渲染路径本单零触碰（渲染行为零变化——全量接线登记 R20.10+ 非目标）。
//
// ── DirtyChunkSet 与 R20.06 WorldDelta 的关系（选型登记，event.h 头注待定项由本单收口）──
// **分化不复用**：DirtyChunkSet 独立立类型（渲染调度视角的 chunk 集合），WorldDelta（R20.06
// 既有通知快照）保持原样零改动，EditBuffer::takeDelta 是前者到后者的单点投影。不复用
// WorldDelta 本体当集合用的理由：①WorldDelta::addAffected 对「幂等重复」与「集满」共用
// false 返回（event.h 头注登记「细分语义留 R20.09 EditBuffer 接线时再定」）——调度面必须
// 区分二者（重复登记是合法幂等；集满是需扩容决策的异常态），本类型用 ChunkAddResult 三态
// 显式分化；②渲染调度（R20.10 Chunk lifecycle / R20.13 MeshBuilder 的 NoMesh/MeshQueued
// 状态族）将在此集合上加网格状态维度，钉在 Tick 通知类型上会越摊越大；③存量契约零破坏
//（r2006 类型腿 / r2007-r2008 会话腿的 addAffected 语义与 tickCompleted 携带面原样）。
// 容量 = WorldDelta::kMaxAffectedChunks 单一权威引用（64 同上界——takeDelta 拷贝恒可容）。
//
// ── 分层（PLAN §2）与落位依据（Core，非 Game）──────────────────────────────────────
// ① 零 World 依赖：全值成员（ChunkKey/BlockEdit/WorldDelta 皆 Core 叶子类型），chunk 尺寸
//    由构造传入（同 ChunkKey::fromWorld 的 caller 传参纪律——Core 不引用 Chunk::kSize）；
// ② 未来消费方在 World 层：plan §5.2 WorldSimulation 系统族「通过 EditBuffer 提交写入」——
//    World 层不得 include Game 层（PLAN §2 低层永不 include 高层，mobmodel.h 先例口径），
//    故落 Core 而非 Game；
// ③ 与 event.h/snapshot.h 的既有登记闭环：两者头注均把「ranges/light 明细 / 旧值明细留
//    R20.09 EditBuffer」——收口点与被收口类型同域，防跨层登记漂移。ranges/light regions
//    明细本单仍不展开（登记非目标：合并粒度到「格」已满足本单四验收；ranges 化是
//    R20.13 MeshBuilder 消费侧的输入形态，随其接线再立）。

#include "event.h"     // WorldDelta（Tick 末发布物）+ kMaxAffectedChunks（容量单一权威）
#include "mathtypes.h" // ChunkKey（dirty chunk 路由；chunkSize 由调用方传入）
#include "result.h"    // QObjectFree（值纪律编译期钉）
#include "snapshot.h"  // BlockEdit（记录单元——改动格 + 改动后 id）

#include <QtGlobal> // quint8

#include <array>
#include <vector>

// ── ChunkAddResult：dirty chunk 登记三态（WorldDelta::addAffected 共用 false 的显式分化）──
enum class ChunkAddResult : quint8
{
    Added = 0,      // 新登记（chunk 首脏）
    AlreadyDirty,   // 幂等去重（已在集内，不占位——合法态）
    Full,           // 集满拒绝（不静默挤出——需扩容决策的异常态，计数器可见）
};

// ── DirtyChunkSet：受影响 chunk 的可测集合（验收③本体）────────────────────────────
// 固定容量数组 + 有效前缀长度（Core 叶子无堆分配；线性成员判定——64 上界内 O(n) 足够，
// 同 WorldDelta::affects 先例）。插入序保持（at(i) 序访问 = 调度遍历与测试断言面）。
class DirtyChunkSet
{
public:
    // 上界单一权威：与 WorldDelta 受影响 chunk 上界同值（takeDelta 投影恒可容）。
    static constexpr int kCapacity = WorldDelta::kMaxAffectedChunks;

    // 登记chunk；三态返回（见 ChunkAddResult——重复/满载显式分化，不再共用 false）。
    ChunkAddResult add(const ChunkKey &k)
    {
        if (contains(k))
            return ChunkAddResult::AlreadyDirty;
        if (m_count >= kCapacity)
            return ChunkAddResult::Full;
        m_keys[m_count++] = k;
        return ChunkAddResult::Added;
    }

    // 线性成员判定。
    bool contains(const ChunkKey &k) const
    {
        for (int i = 0; i < m_count; ++i)
            if (m_keys[i] == k)
                return true;
        return false;
    }

    int size() const { return m_count; }
    const ChunkKey &at(int i) const { return m_keys[i]; } // [0, size) 序访问（插入序）

    void clear() { m_count = 0; }

private:
    std::array<ChunkKey, kCapacity> m_keys{};
    int m_count = 0;
};

// ── RecordResult：编辑记录三态（通知去重的判定面——验收①的类型层承担点）────────────
enum class RecordResult : quint8
{
    Recorded = 0, // 新格首记（调用方发一次通知——tick 末按合并面派生）
    Merged,       // 同格重复写：合并进已有 BlockEdit（后写胜），**不再通知**
    Overflowed,   // 记录面满载丢弃（不可再生——调用方计数器必须可见）
};

// ── EditBuffer：Tick 内编辑累积器（plan §5.2 五步的 1/2/4/5 步本体）─────────────────
// 非 QObject 纯值类型组件（拷贝 = 深拷贝，vector 值语义同 WorldSnapshot::edits 先例）。
// 满载策略：kMaxEdits 上界拒绝（对齐队列家族「丢弃必须可见」纪律——Overflowed 返回 +
// overflowedEdits 累计；chunk 集满不挤已有键、溢出计入 overflowedChunkAdds，BlockEdit
// 本身仍被记录——chunk 投影缺失可见，编辑事实不丢）。
class EditBuffer
{
public:
    static constexpr int kMaxEdits = 1024; // 单 Tick 编辑格上界（≈4 chunk 全表扫描量级；富余）

    // chunkSize = chunk 路由模长（调用方传单一权威值——GameSession 传 Chunk::kSize）。
    explicit EditBuffer(int chunkSize) : m_chunkSize(chunkSize) {}

    // 记录一次方块写（世界坐标 + 改动后 id）：同格合并（后写胜）/ 新格首记 / 满载拒绝。
    bool isEmpty() const { return m_edits.empty(); }

    RecordResult record(int x, int y, int z, quint8 afterId)
    {
        // 合并相邻/重复修改（plan 第 2 步）：同格同 Tick 多写合一，终态 id = 后写。
        const BlockPos p{ x, y, z };
        for (BlockEdit &e : m_edits) {
            if (e.pos == p) {
                e.id = afterId;
                return RecordResult::Merged; // 重复通知抑制（验收①）
            }
        }
        if (int(m_edits.size()) >= kMaxEdits) {
            ++m_overflowedEdits;
            return RecordResult::Overflowed;
        }
        m_edits.push_back(BlockEdit{ p, afterId });
        // chunk 路由（floorDiv 语义——负坐标进「左下」chunk，ChunkKey::fromWorld 单一权威）。
        switch (m_dirty.add(ChunkKey::fromWorld(x, z, m_chunkSize))) {
        case ChunkAddResult::Full: ++m_overflowedChunkAdds; break; // 溢出可见，记录不丢
        case ChunkAddResult::Added:
        case ChunkAddResult::AlreadyDirty: break;
        }
        return RecordResult::Recorded;
    }

    // 脏 chunk 集（Tick 末调度面一次可查——验收④类型层前提；Tick 内只增不减）。
    const DirtyChunkSet &dirtyChunks() const { return m_dirty; }
    // 合并后编辑面（同格单条、后写 id；事件派生与测试断言的遍历面）。
    const std::vector<BlockEdit> &edits() const { return m_edits; }
    // 溢出累计（跨 clear 保留——对齐 GameSession::droppedEventCount 的累计口径）。
    int overflowedEdits() const { return m_overflowedEdits; }
    int overflowedChunkAdds() const { return m_overflowedChunkAdds; }

    // Tick 结束后发布一次 WorldDelta（plan 第 5 步）：chunk 集投影 + 合并后改动量 + tick。
    // const（不夺状态——clear 由 tick 边界显式管；重复 take 同一 Tick 幂等同值）。
    WorldDelta takeDelta(int tick) const
    {
        WorldDelta d;
        d.tick = tick;
        d.changedBlocks = int(m_edits.size()); // 合并后编辑格数（≠ 写入次数——验收①口径）
        d.affectedCount = m_dirty.size();
        for (int i = 0; i < m_dirty.size(); ++i)
            d.affected[i] = m_dirty.at(i);
        return d;
    }

    // 全量复位（Tick 边界调用；溢出累计不清——见上）。
    void clear()
    {
        m_edits.clear();
        m_dirty.clear();
    }

private:
    int m_chunkSize = 16;                 // chunk 路由模长（构造传入，单一权威在调用方）
    DirtyChunkSet m_dirty;
    std::vector<BlockEdit> m_edits;       // 合并后编辑面（自持深拷贝）
    int m_overflowedEdits = 0;            // 记录面满载丢弃累计（不可再生，必须可见）
    int m_overflowedChunkAdds = 0;        // chunk 集满溢出累计（投影缺失可见）
};

// 值纪律编译期钉（R20.06 起纪律：QObjectFree + 形态钉；EditBuffer 含 vector → 对齐
// snapshot.h 先例钉可拷贝面，DirtyChunkSet 全值 → 钉 trivially copyable）。
static_assert(QObjectFree<EditBuffer>, "EditBuffer must not carry QObject (plan §5.2)");
static_assert(QObjectFree<DirtyChunkSet>, "DirtyChunkSet must not carry QObject");
static_assert(std::is_copy_constructible_v<EditBuffer> && std::is_copy_assignable_v<EditBuffer>,
              "EditBuffer must be independently copyable (value-semantics component)");
static_assert(std::is_trivially_copyable_v<DirtyChunkSet>,
              "DirtyChunkSet stays trivially copyable (fixed-capacity value set)");

#endif // EDITBUFFER_H
