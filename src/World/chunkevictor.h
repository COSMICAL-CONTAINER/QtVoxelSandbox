#ifndef CHUNKEVICTOR_H
#define CHUNKEVICTOR_H

// §29.4-P3 卸载 + Edits-on-evict / ChunkEvictor（refactor-plan §29.4「P. 相位拆分」P3 原文：
// 「**P3 卸载 + Edits-on-evict**：视距外 Evicting→Absent，改动块经 SaveCoordinator 落盘、
// 重载回灌（R20.10d 存档往返腿族先例）」+ D5 决策「视距外 **Evicting→Absent + Edits-on-evict
// 落盘**（R20.15 存档件已具备）」——P1 GenerationPolicy 的 toEvict 单消费面）。
//
// header-only 非 QObject 纯编排值组件（chunkstreamdriver.h / generationpolicy.h 同形态，World 层
// 无 AUTOMOC）：把 P1 决策单（decide → toEvict）翻译成「落盘 + 转移」两步执行，驱动器本体
// **零策略逻辑零持久化逻辑零转移合法性逻辑**——候选权威在 P1 policy、落盘权威在 R20.15
// SaveCoordinator（经 persist 缝）、转移合法性权威在 R20.10 chunklifecycle.h 六态表（经转移缝
// 终点 ChunkManager::setLifecycle 唯一守卫入口，本组件不自判合法边）。
//
// ── 三条纯缝（零 World*/ChunkManager*/SaveCoordinator*/QObject* 持有，P1/P2 同门）──────────
//   lifecycleTransitionFn(cx, cz, target) → bool：转移驱动缝——生产接线期绑
//     ChunkManager::setLifecycle 等价面（World::setChunkLifecycle forwarder），测试侧用 QMap 缝
//     + chunklifecycle.h 合法表自校验（r2019 腿）。bool = 守卫入口是否接受该转移（被拒 false）。
//   dirtyQueryFn(cx, cz) → bool：该 chunk 是否持未落盘编辑——生产接线期绑 DirtyChunkSet
//     （R20.09）等价脏面，测试侧用 scratch 集。
//   persistFn(cx, cz) → Result<void>：落盘执行缝——生产接线期绑真实 SaveCoordinator 调用，
//     测试侧用真 SaveCoordinator + fresh 临时库（r2015 先例，绝触 saves/）。
//
// ── null 缝行为（头注释立证）──────────────────────────────────────────────────────────
//   任一必需缝为 null = 该项无信息 → **该 chunk 跳过驱逐（fail-safe 保守：宁驻留不误删）**：
//     · dirtyQueryFn null：无法判「是否持未落盘编辑」→ 跳过（不能赌「无编辑」）；
//     · lifecycleTransitionFn null：无法驱动转移 → 跳过；
//     · persistFn null 且该 chunk 有未落盘编辑：不能落盘即不能驱逐 → 跳过（非 dirty chunk
//       不需要落盘缝，照常驱逐——persist 缝仅 dirty 候选必需）。
//   跳过计入 Report::skipped、零缝调用零状态副作用（跳过 = 什么都不做，非「转移后补票」）。
//
// ── 顺序铁律：先落盘后转移（头注释立证：内容存续优先于状态迁移）────────────────────────
//   dirty 候选必须 persistFn 成功后才允许任何转移驱动调用；persist 失败 = **中止该 chunk 驱逐**
//   （保持驻留原态、计入 aborted、零转移调用——绝不「先掉内容再补盘」）。该序对每个候选独立
//   成立且可由缝调用序观测（r2019b 调用序柱）。自动重试登记非目标：aborted 候选不重试，重试
//   语义 = 调用方下一变更沿重报候选（P2 onPlayerChunk 决策沿天然承接）。
//
// ── 转移驱动：合法边路径（以 chunklifecycle.h 六态表为准，具体边序读表确定）────────────────
//   表读数：Evicting 的唯一入边 = ⑥ Loaded→Evicting；Absent 的唯一入边 = ⑦ Evicting→Absent；
//   Generated/Active 不直连 Evicting——须经 ③ Generated→Loaded / ⑤ Active→Loaded 先归 Loaded。
//   据此每候选的驱动序全确定（三步，同一路径无分支变体）：
//     步1 ensure-Loaded：→Loaded——Generated 经边③、Active 经边⑤晋升/回落；已 Loaded 的
//        自转移被守卫拒（非法）= 预期 no-op 不计失败（best-effort 记账，GenerationJob 同门：
//        非法转移被守卫拒即静默忽略）；
//     步2 →Evicting（边⑥）：被拒 = 候选不在可驱逐稳态（Absent/Loading/Evicting/越界/域外）
//        → 计入 aborted，**不重试**；
//     步3 →Absent（边⑦）：被拒 = 计入 aborted 不重试；此时槽位停 Evicting（半途）——best-effort
//        边⑧ Evicting→Loaded 回驻留（宁驻留不半途弃离；回驻留本身再被拒则停 Evicting 如实暴露）。
//   转移被拒永不抛错不阻断后续候选（逐项独立，单候选失败不影响其余候选的驱逐）。
//
// ── 候选序与确定性 ────────────────────────────────────────────────────────────────────
//   evict 按调用方给定序逐项处理（不重排不去重——P1 toEvict 距离降序同距字典序已全确定；
//   重复候选语义 = 各自独立执行一次）。同候选序 + 同缝 → 轨迹逐位恒等（r2019d 确定性钉）。
//
// ── 默认关恒惰 ────────────────────────────────────────────────────────────────────────
//   本组件无使能旗（与 P1/P2 分工）：默认关的承重点在消费面——ChunkStreamDriver 的 evictor
//   回调默认 null（r2019 起）且仅在 streamingEnabled 决策沿内被调，streaming off 时 evict
//   不可达（r2019a 承重墙钉）；裸 ChunkEvictor 只有被显式调用才动作（零自触发零线程零定时器）。
//
// ── 登记非目标（本单零做）──────────────────────────────────────────────────────────────
//   生产接线（P4/P5：ChunkManager / DirtyChunkSet / SaveCoordinator 真实绑面）；worker meshing
//   （D6 后续单）；内容真实丢弃（World 侧 chunk 数据释放归生产接线——本组件只驱动六态转移，
//   不触 Chunk 内存，r2010「最小解释：驱逐不销毁 Chunk 对象」延续）；失败自动重试。
//
// ── QML / 线程 / 分层面 ───────────────────────────────────────────────────────────────
//   非 QObject、无 Q_INVOKABLE / Q_PROPERTY（驱逐编排不进 QML，R20 主线「QML 零迁移」不变量）；
//   零线程原语（t1023c 白名单纪律）；零 World*/ChunkManager*/SaveCoordinator* 持有（世界/存档
//   接触唯一通道 = 纯缝）；分层：World 层 header-only 值组件，向下只依赖 chunklifecycle.h +
//   generationpolicy.h（候选条目）+ Core 叶子（result），不依赖 Renderer/Game/Entities/QML。
//
// ── 值纪律（P1/P2 同款，R20.05/06/13 先例）──────────────────────────────────────────
//   本组件为纯值编排组件：QObjectFree 编译期钉（文件尾）；Report 为纯值小聚合（平凡可拷贝钉）。

#include <QtGlobal> // quint8 域（无直接用，保持与 P1/P2 同款最小 include 面）

#include <QVector>
#include <functional>   // std::function（三条纯缝）
#include <type_traits>  // std::is_trivially_copyable_v（值纪律钉）

#include "chunklifecycle.h"   // ChunkLifecycle 六态 + 转移表单一权威（驱动序读表依据）
#include "generationpolicy.h" // GenerationPolicyEvict 候选条目（P1 决策单）
#include "result.h"           // Result<void>（persist 缝返回域）+ QObjectFree（值纪律钉）

// ── ChunkEvictor：驱逐候选 → 落盘+转移 编排器（调用者的唯一访问面）──────────────────────
class ChunkEvictor
{
public:
    // 转移驱动缝（生产接线期绑 ChunkManager::setLifecycle 等价面；可空 = 无信息 → 跳过）。
    using LifecycleTransitionFn = std::function<bool(int cx, int cz, ChunkLifecycle target)>;
    // 脏查询缝（生产接线期绑 DirtyChunkSet 等价脏面；可空 = 无信息 → 跳过）。
    using DirtyQueryFn = std::function<bool(int cx, int cz)>;
    // 落盘执行缝（生产接线期绑 SaveCoordinator 真实调用；可空——仅 dirty 候选必需）。
    using PersistFn = std::function<Result<void>(int cx, int cz)>;

    // 只读结果小聚合（纯值；头测断言用）：persisted = persist 缝成功次数；evicted = 完成
    // →Absent 的候选数；aborted = persist 失败或转移被拒的候选数；skipped = null 缝跳过数。
    // 口径：每候选恰落入 {evicted, aborted, skipped} 之一；persisted 是其中的落盘成功计数
    // （≤ evicted + aborted，不参与划分——persist 成功后仍可能因转移被拒转 aborted）。
    struct Report
    {
        int persisted = 0;
        int evicted = 0;
        int aborted = 0;
        int skipped = 0;
    };

    ChunkEvictor() = default;

    // 纯缝注入（可空；null 缝行为见类头注「null 缝行为」节）。
    void setTransitionFn(const LifecycleTransitionFn &f) { m_transition = f; }
    void setDirtyQueryFn(const DirtyQueryFn &f) { m_dirty = f; }
    void setPersistFn(const PersistFn &f) { m_persist = f; }

    // 逐候选驱逐（见类头注「顺序铁律」「转移驱动」两节；候选按给定序处理，零重排）。
    Report evict(const QVector<GenerationPolicyEvict> &candidates)
    {
        Report rep;
        for (const GenerationPolicyEvict &c : candidates) {
            // null 缝 = 该项无信息 → 跳过驱逐（fail-safe 保守：宁驻留不误删；零缝调用零副作用）。
            if (!m_dirty || !m_transition) {
                ++rep.skipped;
                continue;
            }
            const bool dirty = m_dirty(c.cx, c.cz);
            if (dirty) {
                if (!m_persist) {
                    ++rep.skipped; // 有未落盘编辑但无落盘缝 → 宁驻留不误删
                    continue;
                }
                // 顺序铁律（头注「先落盘后转移」）：persist 物理先行于本候选任何转移驱动调用。
                const Result<void> pr = m_persist(c.cx, c.cz);
                if (!pr.isOk()) {
                    // persist 失败 = 中止该 chunk 驱逐：保持驻留原态、零转移调用、不重试
                    // （绝不「先掉内容再补盘」；重试 = 调用方下一变更沿重报候选）。
                    ++rep.aborted;
                    continue;
                }
                ++rep.persisted;
            }
            // 转移驱动（合法边路径，读表确定的三步序——头注「转移驱动」节）。
            // 步1 ensure-Loaded（③/⑤；已 Loaded 自转移守卫拒 = 预期 no-op 不计失败）。
            m_transition(c.cx, c.cz, ChunkLifecycle::Loaded);
            // 步2 边⑥ Loaded→Evicting：被拒 = 不在可驱逐稳态（非法/越界）→ aborted 不重试。
            if (!m_transition(c.cx, c.cz, ChunkLifecycle::Evicting)) {
                ++rep.aborted;
                continue;
            }
            // 步3 边⑦ Evicting→Absent：被拒 → best-effort 边⑧回驻留 + aborted 不重试。
            if (!m_transition(c.cx, c.cz, ChunkLifecycle::Absent)) {
                m_transition(c.cx, c.cz, ChunkLifecycle::Loaded); // 边⑧取消驱逐（best-effort）
                ++rep.aborted;
                continue;
            }
            ++rep.evicted;
        }
        return rep;
    }

private:
    LifecycleTransitionFn m_transition; // 转移驱动缝（可空）
    DirtyQueryFn m_dirty;               // 脏查询缝（可空）
    PersistFn m_persist;                // 落盘执行缝（可空；仅 dirty 候选必需）
};

// 值纪律编译期钉（P1/P2 同款——result.h QObjectFree 概念 + 小聚合平凡性）。
static_assert(QObjectFree<ChunkEvictor>, "ChunkEvictor must be QObject-free (plan §29.4 P3)");
static_assert(QObjectFree<ChunkEvictor::Report>, "ChunkEvictor::Report must be QObject-free (plan §29.4 P3)");
static_assert(std::is_trivially_copyable_v<ChunkEvictor::Report>,
              "ChunkEvictor::Report stays trivially copyable (value-pass discipline)");

#endif // CHUNKEVICTOR_H
