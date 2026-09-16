#ifndef CHUNKSTREAMDRIVER_H
#define CHUNKSTREAMDRIVER_H

// §29.4-P2 玩家移动驱动 / ChunkStreamDriver（refactor-plan §29.4「P. 相位拆分」P2 原文：
// 「**P2 玩家移动驱动**：位置沿 → 视距 chunk 请求/取消（GenerationJob 三 kind 已含优先级）；
// 生成风暴背压 = 队列满载拒绝既有语义。」——治理审计 #10 放行（docs/
// governance-audit-2026-09-16-b.md），D1-D6 用户定向 2026-09-16 全取建议列）。
//
// header-only 非 QObject 纯编排值组件（generationpolicy.h / generationjob.h 同形态，World 层
// 无 AUTOMOC）：把 P1 GenerationPolicy 的决策单（decide → toRequest）翻译成 GenerationJob 的
// 请求面（submit / cancel），驱动器本体**零策略逻辑零转移逻辑**——决策权威在 P1 policy、
// 请求/取消/生命周期边权威在 R20.11 GenerationJob（本组件只做位置沿编排）。
//
// ── 语义依据（§29.4 决策表）────────────────────────────────────────────────────────
//   D1 无限世界：固定世界 = streamingEnabled=false 的结构性特例（P1 承重墙延续）——本组件
//     默认关恒惰：streamingEnabled=false 时 onPlayerChunk / pump 全零动作（不决策不提交不
//     取消不转发泵、在途账逐位不动，r2018a 承重墙钉），参数开启才激活。
//   D4 生成/渲染分离双参数：半径不变量（gen ≥ render 等）由 GenerationPolicyParams::
//     normalized 规整唯一路径在构造/setParams 处强制（r2017 同门），本组件不自设半径语义。
//
// ── 构成与自持选型（头注释立证）────────────────────────────────────────────────────
//   持 GenerationPolicyParams + GenerationPolicy（决策权威）+ 生命周期纯缝
//   LifecycleSeamFn = std::function<ChunkLifecycle(int,int)>（P1 同款缝语义：零 World*/
//   Chunk*/QObject* 指针、null 缝 = 无信息 → decide 恒空 fail-safe）+ **自持一个
//   GenerationJob**（GenerationScheduler 值成员）。自持选型依据：组件自包含 → 可无头测
//   （r2011a-c 纯请求模型腿同门）；生产接线期若需共享世界级实例（世界 epoch / 生命周期
//   挂点全局面）再演化（登记非目标）。自持实例按 GenerationJob「纯请求模型」形态构造
//   （chunks 挂点为空）：生命周期边①②⑨的驱动唯一权威仍 GenerationJob（r2017 kind 门
//   语义不受影响——驱动器只提交 Generate kind 且 pump 纯转发，零转移逻辑零边驱动，见
//   r2018d 结构钉「驱动器头禁 setLifecycle 记号」）；ChunkManager 挂点属生产接线面（P3）。
//
// ── onPlayerChunk：玩家所在 chunk 变更沿（幂等）────────────────────────────────────
//   同 chunk 重复调用零动作（变更沿语义：m_hasPlayer 起始沿 + 同位短路）；变更时：
//   ① policy.decide(cx, cz, seam) → toRequest **逐项** submit Generate-kind job（priority
//      原样传递 = P1 距离秩；r2011 pending (kind,key) 合并别名制天然去重——同 key 仍
//      pending 的重复请求合并为别名，工作恰执行一次，重复请求零副作用）。
//   ② 离半径取消：驱动器自持「在途提交集」m_pending（packed key → 活别名 id 列表），新
//      沿对「仍 pending 但切比雪夫距离已 > generationRadius」的 key 逐别名 cancel（r2011
//      cancel pending 专属语义；取消幂等由 r2011 拒重复取消承接——本集 erase-on-cancel
//      保证每别名至多一次取消尝试；已执行/已消费 id 的 cancel 返回 false 不计入账）。
//      取消判据是**半径**（位置几何）而非 decide 输出成员——在途（Loading 语义）chunk 不在
//      toRequest（非 Absent）但仍属半径内，不得误取消；null 缝沿零提交、取消面照常。
//   计数口径（stats 注）：submitted = submit 成功次数（合并别名各计一次）；canceled = 底层
//   cancel 返回 true 的次数（每活别名一次）；rejected = 满载拒绝次数。
//
// ── 背压可见（r2011 kErrQueueFull 满载拒绝既有语义）────────────────────────────────
//   submit 返回 kErrQueueFull 时计入 rejectedSubmissions() 并跳过——不抛不堵；该 key 不入
//   在途集，下一变更沿 decide 重算时天然重发（r2011 完成后重请求 = 新 job 语义承接）。
//   生成风暴下拒绝必须被看见（命令/事件队列同门纪律），风暴腿断言：拒绝可见、无正确性
//   损失、后续收敛（r2018c）。
//
// ── pump：转发底层 GenerationJob 泵（头注释选型：单转发覆盖同步/异步两路）───────────────
//   底层 pump() 对同步 worker 内联直跑、对 isAsynchronous() worker 自动路由「交接 + 收割」
//   两相（generationjob.h R20.12 缝——isAsynchronous 分派在底层，调用者面零改动即验收④），
//   故本组件单一 pump() 转发即同时是 pumpAsync 通路；收割缝 = takeOutcome / outcomeCount /
//   pendingJobCount 三个只读/取用转发（outcome 为每活别名一条 FIFO，r2011 语义原样）。
//   驱动器 kind 面：默认全 Generate（P2 形态）；r2019 起 saved-content 缝使能时可选 Load
//   （D5 回灌，见类头注「P3 扩展」节），零 Mesh——r2017 kind 门（Mesh 三边不驱动）等生命
//   周期语义全部留在 GenerationJob 唯一权威内，不受本层影响。
//
// ── 登记非目标（本单零做）──────────────────────────────────────────────────────────
//   生产接线（World / PlayerController / QML 位置源——P4/P5）；worker meshing（D6 后续单）；
//   参数实值（P5 调参）；共享世界级 GenerationJob 实例（生产接线期再演化）。
//
// ── §29.4-P3 扩展（r2019）：可选 evictor 回调 + saved-content Load 路径（最小扩展面）──────
//   P3 原文：「视距外 Evicting→Absent，改动块经 SaveCoordinator 落盘、重载回灌」。本组件
//   只扩两个**可选注入点**，驱逐执行本体在 chunkevictor.h 的 ChunkEvictor（纯编排值组件）：
//   ① setEvictor：onPlayerChunk 决策沿在提交/取消完成后以 d.toEvict 调用（P1 toEvict 单的
//      消费面）——**默认 null = 逐位零变化**（r2018 全部既有腿回归绿为实证）；使能回调 +
//      streaming off 时不可达（回调点在 onPlayerChunk 默认关短路之后，r2019a 承重墙钉）。
//   ② setSavedContentQuery：D5 回灌路径——非 null 时 submit 前查询该 chunk 是否有存档内容：
//      有 → Load kind（r2011 Load 语义：与 Generate 共用边①②）；无 → Generate。
//      **默认 null = 全 Generate（P2 行为逐位不变，r2018b 双生断言继续成立）**。
//   两扩展「默认注入前与 P2 逐位等价」由头注释立证 + r2018 既有腿回归绿实证；P3 落盘/转移
//   执行器与生命周期语义仍零进入本组件（驱动器零 setLifecycle 记号不变，r2018d 反探钉）。
//
// ── QML / 线程 / 分层面 ───────────────────────────────────────────────────────────
//   非 QObject、无 Q_INVOKABLE / Q_PROPERTY（调度编排不进 QML，R20 主线「QML 零迁移」不
//   变量）；零线程原语（t1023c 白名单纪律）；零 World*/ChunkManager* 持有（世界接触唯一
//   通道 = 纯缝）；分层：World 层 header-only 值组件，向下只依赖 generationpolicy.h +
//   generationjob.h（及它们的 Core 叶子），不依赖 Renderer/Game/Entities/QML。
//
// ── 值纪律（P1 同款，R20.05/06/13 先例）────────────────────────────────────────────
//   本组件为纯值编排组件：QObjectFree 编译期钉（文件尾）；Stats 为纯值小聚合。

#include <QtGlobal> // qint64 / quint32

#include <QMap>
#include <QVector>
#include <functional>   // std::function（生命周期纯缝）
#include <type_traits>  // std::is_trivially_copyable_v（值纪律钉）

#include "generationjob.h"    // GenerationScheduler / GenerationJobKind / GenerationJobOutcome
#include "generationpolicy.h" // GenerationPolicy(Params) / decide 决策权威 + 纯缝类型语义

// ── ChunkStreamDriver：位置沿 → 请求/取消编排器（调用者的唯一访问面）──────────────────
class ChunkStreamDriver
{
public:
    // 生命周期纯缝（P1 同款语义：零 World*/Chunk*/QObject* 指针；可空 = 无信息）。
    using LifecycleSeamFn = std::function<ChunkLifecycle(int cx, int cz)>;
    // §29.4-P3 扩展缝（r2019）：toEvict 消费回调 + saved-content 查询（均可空——null = P2 形态）。
    using EvictorFn = std::function<void(const QVector<GenerationPolicyEvict> &)>;
    using SavedContentQueryFn = std::function<bool(int cx, int cz)>;

    // 只读统计小聚合（命名从简；头测断言用）。
    struct Stats
    {
        int submitted = 0; // submit 成功次数（合并别名各计一次）
        int canceled = 0;  // 底层 cancel 返回 true 的次数（幂等拒绝面不计）
        int rejected = 0;  // 满载拒绝（kErrQueueFull 域）次数
    };

    // 默认构造 = 默认关（GenerationPolicyParams 默认即 disabled——固定世界零变化的参数面）。
    ChunkStreamDriver() = default;
    // 规整构造：参数经 normalized 唯一路径（r2017 同门）后同喂 params 读视图与 policy。
    explicit ChunkStreamDriver(const GenerationPolicyParams &params) { setParams(params); }

    // 参数写入（唯一写路径：双份状态同步自规整唯一路径，杜绝发散）。
    void setParams(const GenerationPolicyParams &params)
    {
        m_params = GenerationPolicyParams::normalized(params.streamingEnabled,
            params.generationRadiusChunks, params.renderRadiusChunks, params.scanExtentChunks);
        m_policy.setParams(m_params);
    }
    const GenerationPolicyParams &params() const { return m_params; }

    // 纯缝注入（P1 同款：可空；null 缝 = 无信息 → 零提交，取消面照常半径驱动）。
    void setSeam(const LifecycleSeamFn &seam) { m_seam = seam; }

    // §29.4-P3 扩展注入（r2019；均可空——默认 null = P2 逐位等价，见类头注「P3 扩展」节）。
    // evictor：决策沿 toEvict 消费回调（生产接线期绑 ChunkEvictor::evict 等价执行面）。
    void setEvictor(const EvictorFn &evictor) { m_evictor = evictor; }
    // savedContentQuery：submit 前「有存档内容 → Load kind」查询（null = 全 Generate）。
    void setSavedContentQuery(const SavedContentQueryFn &q) { m_savedQuery = q; }

    // worker 缝转发（generationjob 验收④同门：换实例 = 换实现，编排面零改动）。
    void setWorker(GenerationWorker *worker) { m_jobs.setWorker(worker); }

    // 玩家所在 chunk 变更沿（幂等；见类头注释「onPlayerChunk」节）。
    void onPlayerChunk(int cx, int cz)
    {
        // 默认关恒惰（§29.4「固定世界默认关」——P1 承重墙延续）：零决策零提交零取消，
        // 在途账逐位不动（r2018a 承重墙：使能→装载→关断→扫掠后队列 bit-untouched）。
        if (!m_params.streamingEnabled)
            return;
        // 变更沿幂等：同 chunk 重复调用零动作（头注「变更沿」语义）。
        if (m_hasPlayer && m_playerCx == cx && m_playerCz == cz)
            return;
        m_hasPlayer = true;
        m_playerCx = cx;
        m_playerCz = cz;

        // ① 决策（P1 纯函数权威；null 缝 fail-safe 恒空 → 零提交）。
        const GenerationPolicyDecision d = m_policy.decide(cx, cz, m_seam);
        // ② 逐项 submit（priority 原样传递；r2011 合并别名制天然去重——重复请求零副作用）。
        //    P3 D5 回灌路径（r2019）：saved-content 缝非 null 时 submit 前查询——有存档内容
        //    → Load kind（r2011 Load 语义），无 → Generate；null = 全 Generate（P2 逐位不变）。
        for (const GenerationPolicyRequest &r : d.toRequest) {
            const GenerationJobKind kind = (m_savedQuery && m_savedQuery(r.cx, r.cz))
                ? GenerationJobKind::Load
                : GenerationJobKind::Generate;
            const Result<quint32> id = m_jobs.submit(
                kind, ChunkKey{ r.cx, r.cz }, r.priority);
            if (id.isOk()) {
                m_pending[ChunkKey{ r.cx, r.cz }.packed()].append(id.value());
                ++m_stats.submitted;
            } else {
                // 背压可见：满载拒绝（r2011 kErrQueueFull 唯一失败面）计数并跳过——不抛不
                // 堵；不入在途集，下一变更沿 decide 重算天然重发（完成后重请求 = 新 job）。
                ++m_stats.rejected;
            }
        }
        // ③ 离半径取消（半径判据非 decide 成员判据——在途 chunk 不在 toRequest 也不误取消；
        //    erase-on-cancel 保证每别名至多一次尝试，已消费 id 的 false 不计入账）。
        const int gen = m_params.generationRadiusChunks;
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            const ChunkKey k = ChunkKey::fromPacked(it.key());
            const int dx = k.cx - cx, dz = k.cz - cz;
            const int cheb = qMax(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
            if (cheb > gen) {
                const QVector<quint32> aliases = it.value(); // 拷贝遍历（erase 前的快照语义）
                for (const quint32 aliasId : aliases) {
                    if (m_jobs.cancel(aliasId))
                        ++m_stats.canceled; // 全别名死亡 → job 整体跳过（工作不跑，r2011 语义）
                }
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
        // ④ §29.4-P3 驱逐消费沿（r2019）：提交/取消完成后以 decide 的 toEvict 单回调 evictor
        //    （执行本体在 ChunkEvictor——本组件零转移零落盘逻辑；默认 null = 逐位零变化，
        //    streaming off 时此处不可达[短路在入口]，r2019a 承重墙钉）。
        if (m_evictor)
            m_evictor(d.toEvict);
    }

    // 泵转发（同步内联 / 异步「交接+收割」自动路由在底层——头注「pump」节选型）。
    void pump()
    {
        // 默认关恒惰：不转发底层泵（固定世界零变化——r2018a「pump 全零动作」钉）。
        if (!m_params.streamingEnabled)
            return;
        m_jobs.pump();
    }

    // 收割缝（转发底层 outcome FIFO：每活别名一条；取消/过期无 outcome——r2011 语义原样）。
    bool takeOutcome(GenerationJobOutcome &out) { return m_jobs.takeOutcome(out); }
    int outcomeCount() const { return m_jobs.outcomeCount(); }
    // 底层 pending 账面转发（job = 合并后执行单元数；队列逐位不动断言的只读观测点）。
    int pendingJobCount() const { return m_jobs.pendingJobCount(); }

    // 背压可见面（满载拒绝累计；不重置——组件生命期累计口径，头测断言用）。
    int rejectedSubmissions() const { return m_stats.rejected; }
    Stats stats() const { return m_stats; }

private:
    GenerationPolicyParams m_params;  // 读视图（默认关旗 + gen 半径；唯一写路径 = setParams）
    GenerationPolicy m_policy;        // 决策权威（decide 纯函数；参数与读视图同步）
    LifecycleSeamFn m_seam;           // 生命周期纯缝（P1 同款；可空）
    EvictorFn m_evictor;              // §29.4-P3 toEvict 消费回调（r2019；默认 null = P2 形态）
    SavedContentQueryFn m_savedQuery; // §29.4-P3 saved-content 查询（r2019；默认 null = 全 Generate）
    GenerationScheduler m_jobs;       // 自持 GenerationJob（纯请求模型形态——挂点归生产接线）
    // 在途提交集：packed(cx,cz) → 该 key 的活别名 id 列表（合并别名制下可多别名；离半径
    // 逐别名取消；erase-on-cancel 保证幂等——已消费 id 的 cancel false 不入账）。
    QMap<quint64, QVector<quint32>> m_pending;
    bool m_hasPlayer = false; // 首次 onPlayerChunk 即变更沿（无位 → 有位）
    int m_playerCx = 0;
    int m_playerCz = 0;
    Stats m_stats;
};

// 值纪律编译期钉（P1 同款——result.h QObjectFree 概念；编排值组件不携带 QObject 子对象）。
static_assert(QObjectFree<ChunkStreamDriver>, "ChunkStreamDriver must be QObject-free (plan §29.4 P2)");
static_assert(QObjectFree<ChunkStreamDriver::Stats>, "ChunkStreamDriver::Stats must be QObject-free (plan §29.4 P2)");
static_assert(std::is_trivially_copyable_v<ChunkStreamDriver::Stats>,
              "ChunkStreamDriver::Stats stays trivially copyable (value-pass discipline)");

#endif // CHUNKSTREAMDRIVER_H
