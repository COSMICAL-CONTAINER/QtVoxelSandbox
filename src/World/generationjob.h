#ifndef GENERATIONJOB_H
#define GENERATIONJOB_H

// R20.11 同步版 ChunkScheduler / GenerationJob（refactor-plan §29.3 R20.11「先不引入多线程，
// 只统一请求、优先级、取消和结果模型」）。本单契约 = **同步版请求编排层**：不引入线程、不改
// 任何既有调用点（固定世界 worldgen 路径逐位不动——生产路径零接线，本组件是 R20.12 后台
// GenerationJob 落地时才接入的请求模型地基），只把「生成 / 加载 / mesh 请求」的公共语义收拢
// 成一个可测的值类型 + 调度器面。
//
// ── 验收对照（plan §29.3 R20.11 原文四条）────────────────────────────────────────
// ① 「生成、加载、mesh 请求都有 request ID」：GenerationJobKind 三种别（Generate/Load/Mesh）
//    共用单一单调计数器，每次 submit 分配新 requestId（1 起，0 = 无效哨兵，永不复用）——
//    每条请求可独立追踪 / 独立取消 / 独立收结果（GenerationJobOutcome.requestId）。
// ② 「重复请求可以合并」：同一 (kind, key) 在**仍 pending** 时重复 submit → 合并为同一 job
//    的新别名（alias）：工作只执行一次，但每个别名（各自 requestId）都收到自己的完成结果
//    （合并不减提交方的可见性）。合并且仅 pending——job 执行完毕后的再次提交 = 新 job 重跑
//    （如 mesh 在编辑后需要重建）；不同 kind 同 key 不合并（Generate 与 Mesh 语义独立）。
//    合并不改既有 job 的优先级（加入者按原 job 的优先级执行）。
// ③ 「过期请求可以丢弃」：两级丢弃语义，都「不投递结果」——
//    a) 取消：cancel(requestId) 只对 pending 请求有效；被取消的别名此后**永不产生 outcome**
//       （job 若因此全别名死亡则整体跳过——工作完全不跑）。
//    b) 世界 epoch：submit 时快照当时 epoch；pump 时 epoch != 当前世界 epoch 的请求按过期
//       丢弃——不执行、不投递（R20.12「world epoch 可以丢弃旧任务」的同步版前置）。全别名
//       过期的 job 整体静默跳过。
// ④ 「后续替换成 worker 不需要改调用者」：调用者只见 GenerationScheduler 面（submit/cancel/
//    pump/takeOutcome）；执行后端藏在 GenerationWorker 接口后（同步直跑是首个实现，未来
//    R20.12 换后台线程 worker = 换 setWorker 实例，调用者代码零改动）。矩阵 r2011c 用两个
//    不同 worker 实现类跑同一请求序列断言结果恒等（可替换性的无头实证——不加线程）。
//
// ── 与 R20.10 Chunk lifecycle 的互锁（读码后选型，头注释立此存照）──────────────────
//    GenerationScheduler 可挂一个 ChunkManager（构造参数，可空 = 纯请求模型），对 Generate/
//    Load 类 job 驱动 chunklifecycle.h 转移图的 ①②⑨ 三条边：
//      执行开始前：Absent→Loading（边①，即 chunklifecycle.h 注释预留的「R20.11 挂点」）；
//      执行成功后：Loading→Generated（边②）。晋升 Generated→Loaded（边③）**不由本层驱动**
//      （驻留晋升是 mesh/激活策略的决策面，登记后续单）；失败面走**失败恢复边⑨
//      Loading→Absent**（R20.10b：失败 outcome **实际投递**才转移——epoch 过期/取消的丢弃面
//      不投递故不转移；转移后 chunk 回可重试态，重请求 = 新 job，与 R20.12「epoch 丢弃旧任务」
//      语义同门。自动重试策略不属本层，登记非目标）。
//    **r2017（agent-review-2026-09-16 #2）kind 门 = 上述契约的代码兑现**：同步 pump 与
//      pumpAsync 两路共六处边驱动点全部带 `job.kind != GenerationJobKind::Mesh` 门——
//      Mesh 类 job 三边全不驱动（此前仅本注释声明、代码未过滤：一个 Mesh 请求可把 Absent
//      chunk 推入 Loading/Generated，而本层不驱动边③晋升且 Generated 不在
//      chunkLifecycleQueryable 可见集 → chunk 永久停 Generated 对 mesher 三门不可见；异步
//      交接窗内 Mesh 失败交付还会经边⑨把 Generate 在途的 chunk 打回 Absent = 状态与数据
//      失配）。r2017b 双路行为钉；Generate/Load 两 kind 三边照旧（kind 域门非全局摘除）。
//    驱动是 **best-effort 记账**：经 ChunkManager::setLifecycle 唯一守卫入口尝试，非法转移
//    （含默认稳态 Loaded→Loading）被守卫拒绝即静默忽略、不影响工作执行——这正是「固定世界
//    零变化」的结构性根据：默认世界全表 Loaded，任何 submit+pump（含失败注入）都改不了生命
//    周期表（r2011d 实证 + r2010ba 失败注入版实证）。取消发生在执行前 = 状态从未离开 Absent
//    （「不推进」——本层不做取消回退；边⑨只由失败投递面驱动，非取消回退路径）。生产语义登
//    记：真实流式世界里 submit 侧必须只对 Absent chunk 提交 Generate（本层是请求模型不是策
//    略层，R20.12 接线时收口）。
//
// ── 结果模型（「Result 穿透」）───────────────────────────────────────────────────
//    worker 返回 Result<void>（result.h，成功面 + 失败面）；失败时 worker 的 Error **原样
//    穿透**到 GenerationJobOutcome.error（code/message 逐位保留），调用方以 isError() 判定。
//    outcome 每活别名一条、FIFO（takeOutcome）。提交满载 = Result::fail(kErrQueueFull)
//   （命令/事件队列同门：拒绝必须被看见）。outcome 队列**不设容量上界**（刻意分化，登记）：
//    同步版 pump 由调用者内联驱动，产出与消费无生产者/消费者不对称，outcome 总量 ≤ 本次
//    pump 前的活请求数——容量策略是 R20.12 线程化后才出现的真问题，届时与线程队列一并设计。
//
// ── 执行序（优先级）────────────────────────────────────────────────────────────
//    priority 值小 = 更紧急（0 = 最高）；同优先级按提交序（首活别名 requestId 升序）。pump
//    每轮取 (priority, 首活 requestId) 最小的 job 执行。合并别名不改 job 优先级。
//
// ── QML 面 ────────────────────────────────────────────────────────────────────────
//    非 QObject、无 Q_INVOKABLE / Q_PROPERTY（生命周期与调度决策不进 QML，R20 主线「QML 零
//    迁移」不变量；矩阵 r2011d 反探钉）。分层（PLAN §2）：World 层 header-only 组件（同
//    chunklifecycle.h 形态），向下依赖 Core 叶子（mathtypes/result）+ ChunkManager，不依赖
//    Renderer/Game/Entities/QML。
//
// ── 值纪律（R20.05/06 先例）────────────────────────────────────────────────────
//    GenerationRequest / GenerationJobOutcome 为纯值类型：QObjectFree 编译期钉 + trivially
//    copyable 钉（安全过队列 / 未来跨线程面），见文件尾 static_assert。

#include <QtGlobal> // quint8 / quint32

#include <type_traits> // std::is_trivially_copyable_v（值纪律钉）
#include <deque>

#include "chunkmanager.h" // ChunkManager（生命周期记账挂点——setLifecycle 唯一守卫入口）
#include "mathtypes.h"    // ChunkKey（请求目标 chunk——Core 叶子键类型）
#include "result.h"       // Result / Error / kErrQueueFull（结果模型 + QObjectFree 值纪律）

// ── GenerationJobKind：请求种别（plan 验收①的三席；不预占席位，随 R20.12+ 接线面扩充）──
enum class GenerationJobKind : quint8
{
    Generate = 0, // 地形 / 内容生成（lifecycle 边①②的驱动种别）
    Load,         // 从存档读回（同走边①②——「加载/生成」在六态图共用 Absent→Loading→Generated）
    Mesh,         // 网格重建请求（不驱动生命周期——mesher 的门查询面自判可见态）
};

inline const char *generationJobKindName(GenerationJobKind k)
{
    switch (k) {
    case GenerationJobKind::Generate: return "Generate";
    case GenerationJobKind::Load: return "Load";
    case GenerationJobKind::Mesh: return "Mesh";
    }
    return "?";
}

// ── GenerationRequest：一条生成/加载/mesh 请求（纯值；worker 执行时的请求视图）─────────
struct GenerationRequest
{
    quint32 requestId = 0; // 1 起单调，永不复用（0 = 无效哨兵）
    GenerationJobKind kind = GenerationJobKind::Generate;
    ChunkKey key{};        // 目标 chunk（网格坐标域；负坐标同等合法）
    quint8 priority = 0;   // 0 = 最紧急（值小优先；同优先级按提交序）
    quint32 epoch = 0;     // 提交时世界 epoch 快照（pump 时 != 当前 = 过期丢弃）
};

// ── GenerationJobOutcome：一条请求的终局（成功或失败；取消/过期**无 outcome**）─────────
struct GenerationJobOutcome
{
    quint32 requestId = 0;
    GenerationJobKind kind = GenerationJobKind::Generate;
    ChunkKey key{};
    Error error{}; // code==0 = 完成；非零 = 失败（worker 的 Error 原样穿透）
};

// 值纪律编译期钉（R20.05/06 先例——result.h QObjectFree 概念 + trivially copyable）：
// 请求/结果必须可安全跨队列携带、未来跨线程传递（R20.12）不携带 QObject 子对象。
static_assert(QObjectFree<GenerationRequest>, "GenerationRequest must be QObject-free (plan §29.3 R20.11)");
static_assert(std::is_trivially_copyable_v<GenerationRequest>,
              "GenerationRequest stays trivially copyable (queue-safe value semantics)");
static_assert(QObjectFree<GenerationJobOutcome>, "GenerationJobOutcome must be QObject-free (plan §29.3 R20.11)");
static_assert(std::is_trivially_copyable_v<GenerationJobOutcome>,
              "GenerationJobOutcome stays trivially copyable (queue-safe value semantics)");

// ── CompletedGeneration：异步完成记录（交接账本对账键 + worker 的 Error 穿透）────────────
// 持 Error 而非 Result<void>：Result 不可默认构造（fail-safe 取值纪律）不宜作队列成员默认
// 值；Error 是平凡聚合（code==0 = 成功面），调度侧 pumpAsync 换算回 Result<void> 走既有
// deliver 穿透路径。trivially copyable 钉不变。
struct CompletedGeneration
{
    quint64 jobId = 0;
    Error error{}; // code==0 = 完成；非零 = 失败（worker 的 Error 原样穿透，同同步 execute 语义）
};
// 值纪律编译期钉（同 Request/Outcome：可安全跨线程携带、不携带 QObject 子对象）。
static_assert(QObjectFree<CompletedGeneration>, "CompletedGeneration must be QObject-free (plan §29.3 R20.12)");
static_assert(std::is_trivially_copyable_v<CompletedGeneration>,
              "CompletedGeneration stays trivially copyable (thread-safe value semantics)");

// ── GenerationWorker：执行后端缝（验收④——换 worker 不改调用者）──────────────────────
// 同步版的默认形态 = pump 内联直跑；R20.12 换后台线程实现时调用者（submit/cancel/pump/
// takeOutcome）零改动。worker 的失败面经 Result<void> 穿透到 outcome（不吞错）。
class GenerationWorker
{
public:
    virtual ~GenerationWorker() = default;
    // 执行一个（已合并的）请求单元。返回 ok = 完成；fail = 失败（Error 穿透到每个活别名）。
    virtual Result<void> execute(const GenerationRequest &req) = 0;

    // ── R20.12 后台线程协议（可选扩展；以下默认实现 = 同步语义逐位不变，r2011 腿面全绿守）──
    // isAsynchronous() 为真时 scheduler.pump() 走「交接 + 收割」两相（pumpAsync），不再内联
    // execute()：交接经 submitAsync（满载 fail = 背压，job 留队下轮 pump 再试），完成经
    // takeCompletedAsync（FIFO）回到调用者线程投递。outcome 仍**只**产生在 pump/takeOutcome
    // 侧（调用者线程）——epoch/取消过滤在投递时刻（deliver）生效 = 「world epoch 可以丢弃旧
    // 任务」的后台版（交接后才过期的在途结果被静默丢弃，不投递）。数据面（自持缓冲）由具体
    // worker 自带（见 backgroundgeneration.h GeneratedChunkData），不经本协议 payload 化——
    // GenerationJobOutcome 保持 r2011 值纪律（trivially copyable 钉不动）。
    virtual bool isAsynchronous() const { return false; }
    // 移交一个（已合并的）请求单元到后台。ok = 受理；fail = 暂不受理（队列满载等，背压重试）。
    virtual Result<void> submitAsync(const GenerationRequest &req, quint64 jobId)
    {
        Q_UNUSED(req);
        Q_UNUSED(jobId);
        return Result<void>::ok();
    }
    // 取一条完成记录（FIFO；空 = false 正常态，同 takeOutcome 口径）。jobId 对账移交账本。
    virtual bool takeCompletedAsync(CompletedGeneration &out)
    {
        Q_UNUSED(out);
        return false;
    }
};

// ── GenerationScheduler：同步版请求编排器（调用者的唯一访问面）──────────────────────
class GenerationScheduler
{
public:
    // pending job 容量上界（满载提交拒绝可见——命令/事件队列同门纪律；64 与 CommandQueue
    // /EventQueue 同量级：≥ 4×4 chunk 网格全图单波请求富余）。
    static constexpr int kMaxPendingJobs = 64;

    // chunks 可空 = 纯请求模型（不驱动生命周期记账）；非空则对每次执行尝试边①②。
    explicit GenerationScheduler(ChunkManager *chunks = nullptr) : m_chunks(chunks) {}

    // worker 缝（验收④）：换实例 = 换实现，调用者面零改动。同步版 pump 前必须 setWorker。
    void setWorker(GenerationWorker *w) { m_worker = w; }
    GenerationWorker *worker() const { return m_worker; }

    // §29.5-W2 生产接线缝（加性；构造参数形态不变——默认 nullptr = 纯请求模型，r2011~r2021
    // 全部既有腿的形态零变化）：世界级接线方（GameSession 流式会话）在构造后把 ChunkManager
    // 挂点接入，边①②⑨（本类唯一权威驱动，r2017 kind 门语义不变）自此真实落表。这是 r2011
    // 头注「可挂可变 ChunkManager*」预留面的生产首用缝——挂点转移仍经 ChunkManager::setLifecycle
    // 唯一守卫入口；①②⑨三边均不触驻留集（{Loaded,Active}），驻留 revision 沿由晋升边③的
    // 调用方（World::adoptGeneratedChunk 经 World::setChunkLifecycle forwarder）携带。
    void setChunks(ChunkManager *chunks) { m_chunks = chunks; }

    // 世界 epoch（过期判定基准）：世界换代（regenerate / 读档）时递增——旧 epoch 的 pending
    // 请求在 pump 时按过期丢弃（不执行、不投递）。0 = 初始 epoch。
    void setWorldEpoch(quint32 e) { m_worldEpoch = e; }
    quint32 worldEpoch() const { return m_worldEpoch; }

    // 提交请求：分配新 requestId。同 (kind,key) 仍 pending → 合并为别名（工作只跑一次，
    // 本请求仍收到自己的 outcome）；满载（distinct pending job 数达上界）→ fail(kErrQueueFull)
    // （合并不占新 job 位——满载时重复提交仍可合并）。合并别名不改变既有 job 的优先级。
    Result<quint32> submit(GenerationJobKind kind, const ChunkKey &key, quint8 priority = 0)
    {
        for (const Job &j : m_jobs)
            if (j.kind == kind && j.key == key) { // pending 合并（别名不占新 job 位）
                const quint32 id = m_nextRequestId++;
                m_requests.push_back(Request{ id, j.jobId, m_worldEpoch, false });
                return Result<quint32>::ok(id);
            }
        if (int(m_jobs.size()) >= kMaxPendingJobs)
            return Result<quint32>::fail(kErrQueueFull, "generation job queue full");
        const quint32 id = m_nextRequestId++;
        const quint64 jobId = m_nextJobId++;
        m_jobs.push_back(Job{ jobId, kind, key, priority });
        m_requests.push_back(Request{ id, jobId, m_worldEpoch, false });
        return Result<quint32>::ok(id);
    }

    // 取消 pending 请求：被取消的别名永不投递 outcome；其所在 job 若全别名死亡则整体跳过
    // （工作不跑）。对未知 / 已执行 / 已取消的 id 返回 false（幂等拒绝）。
    bool cancel(quint32 requestId)
    {
        for (Request &r : m_requests)
            if (r.requestId == requestId) {
                if (r.canceled)
                    return false;
                r.canceled = true;
                return true;
            }
        return false;
    }

    // pending 账面（job = 合并后的执行单元数；request = 别名数）。同步 pump 后归零（全消耗）；
    // 异步 pump 后 = 未交接的余量（已交接部分入 inFlightJobCount 账，完成收割时双账同消）。
    int pendingJobCount() const { return int(m_jobs.size()); }
    int pendingRequestCount() const { return int(m_requests.size()); }
    // R20.12：已交接后台、尚未收割的 job 数（异步账本；同步 worker 恒 0）。
    int inFlightJobCount() const { return int(m_inFlight.size()); }

    // 同步执行全部 pending job（按优先级序内联直跑），并为每个活别名投递 outcome。
    // 过期/取消的请求与全死 job 按验收③静默丢弃（不执行、不投递）。无 worker 时不执行。
    // R20.12：isAsynchronous() worker 改走「交接 + 收割」两相（pumpAsync）——请求模型
    //   （submit/cancel/epoch 快照/优先级选活/投递过滤）与调用者面零改动。
    void pump()
    {
        if (!m_worker)
            return;
        if (m_worker->isAsynchronous()) {
            pumpAsync();
            return;
        }
        while (!m_jobs.empty()) {
            std::deque<Job>::iterator best = m_jobs.end();
            quint32 bestFirstLive = 0;
            if (!selectNextLiveJob(best, bestFirstLive)) {
                // 全部 job 无活别名（取消/过期耗尽）——静默清账，零执行零投递
                m_jobs.clear();
                m_requests.clear();
                break;
            }
            // 拷出并摘队（先摘后执行：worker 内若再 submit 不扰动本账本——防重入）。
            const Job job = *best;
            m_jobs.erase(best);
            // 边①（Absent→Loading）best-effort：非 Absent（含默认稳态 Loaded）被守卫拒绝
            // 即忽略——固定世界生命周期表零变化的结构性根据（r2011d 实证）。
            // r2017 kind 门：Mesh 类 job 不驱动（头注互锁契约的代码兑现，r2017b 钉）。
            if (m_chunks && job.kind != GenerationJobKind::Mesh)
                m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Loading);
            const GenerationRequest req{ bestFirstLive, job.kind, job.key, job.priority,
                                         m_worldEpoch };
            const Result<void> r = m_worker->execute(req);
            // 边②（Loading→Generated）仅成功面推进。同步泵内执行与投递同调用、epoch 不可能
            //   中途变化、被选中的 job 必有活别名 ⟹ 失败 outcome 必然实际投递——失败恢复边⑨
            //   随投递面取（见 deliver 之后），与异步收割同一判据（单一语义）。
            if (r.isOk() && m_chunks && job.kind != GenerationJobKind::Mesh) // 边②仅成功面推进（r2017 Mesh kind 门）
                m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Generated);
            const int delivered = deliver(job, r);
            // 边⑨（Loading→Absent，R20.10b 失败恢复）：仅失败 outcome 实际投递时取——经唯一
            //   守卫入口回 Absent（重请求=新 job）。默认稳态 Loaded chunk 上边①已被守卫拒，
            //   此处边⑨同样被拒 = 固定世界零变化不破（r2010ba 失败注入版实证）。
            //   r2017 kind 门：Mesh 类 job 不驱动。
            if (!r.isOk() && delivered > 0 && m_chunks && job.kind != GenerationJobKind::Mesh)
                m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Absent);
            eraseRequests(job.jobId);
        }
    }

    // 结果面：FIFO 取一条 outcome（空 = false 正常态，非错误——同命令/事件队列口径）。
    bool takeOutcome(GenerationJobOutcome &out)
    {
        if (m_outcomes.empty())
            return false;
        out = m_outcomes.front();
        m_outcomes.pop_front();
        return true;
    }
    int outcomeCount() const { return int(m_outcomes.size()); }

private:
    struct Job
    {
        quint64 jobId = 0; // job 句柄（别名经它挂靠；单调分配）
        GenerationJobKind kind = GenerationJobKind::Generate;
        ChunkKey key{};
        quint8 priority = 0;
    };
    struct Request
    {
        quint32 requestId = 0; // 别名的请求 id（提交方视角的追踪键）
        quint64 jobId = 0;     // 挂靠的执行单元
        quint32 epoch = 0;     // 提交时 epoch 快照
        bool canceled = false; // 取消标记（取消后永不投递）
    };

    // 选下一执行者（同步内联直跑与异步交接共用）：(priority, 首活 requestId) 最小；无活别名
    // （取消/过期耗尽）的 job 跳过。返回 false = 全部 job 无活别名（调用方按各自语义清账）。
    bool selectNextLiveJob(std::deque<Job>::iterator &bestOut, quint32 &bestFirstLiveOut)
    {
        bool found = false;
        for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
            quint32 firstLive = 0;
            bool anyLive = false;
            for (const Request &r : m_requests) {
                if (r.jobId != it->jobId || r.canceled || r.epoch != m_worldEpoch)
                    continue; // 取消 / 过期别名不救活 job（验收③丢弃语义）
                anyLive = true;
                if (firstLive == 0 || r.requestId < firstLive)
                    firstLive = r.requestId;
            }
            if (!anyLive)
                continue;
            if (!found || int(it->priority) < int(bestOut->priority)
                || (it->priority == bestOut->priority && firstLive < bestFirstLiveOut)) {
                found = true;
                bestOut = it;
                bestFirstLiveOut = firstLive;
            }
        }
        return found;
    }

    // R20.12 异步泵（isAsynchronous() worker 专用）——交接 + 收割两相：
    //   交接相：按既有选活序逐 job submitAsync 移交后台（满载 fail = 背压本轮止，job 留队下轮
    //     pump 再试）；受理即取边①（Absent→Loading best-effort，同同步泵守卫语义——非法转移
    //     被拒即忽略 = 固定世界零变化）并记入在途账本。全部剩余 job 无活别名时只清死 job 账面
    //     （在途 job 的别名必须保留到收割）。
    //   收割相：takeCompletedAsync FIFO 逐完成记录——边②仅成功面推进 + deliver 投递（取消/
    //     epoch 过滤在此生效：交接后才 bump epoch 的在途结果被静默丢弃 = 验收③后台版）；
    //     失败 outcome **实际投递**时取失败恢复边⑨（R20.10b——丢弃面不转移，见收割体内注）。
    //   线程边界：submitAsync/takeCompletedAsync 只在调用者线程调（本函数的调用者 = pump 的
    //     调用者）；跨线程同步由具体 worker 自管（见 backgroundgeneration.h 的锁与条件变量）。
    void pumpAsync()
    {
        for (;;) {
            std::deque<Job>::iterator best = m_jobs.end();
            quint32 bestFirstLive = 0;
            if (!selectNextLiveJob(best, bestFirstLive)) {
                for (const Job &j : m_jobs) // 死 job 清账（在途账本不动）
                    eraseRequests(j.jobId);
                m_jobs.clear();
                break;
            }
            const Job job = *best;
            const GenerationRequest req{ bestFirstLive, job.kind, job.key, job.priority,
                                         m_worldEpoch };
            if (!m_worker->submitAsync(req, job.jobId).isOk())
                break; // worker 队列满载 = 背压（可见拒绝在 worker 侧记账；本轮止）
            m_jobs.erase(best);
            if (m_chunks && job.kind != GenerationJobKind::Mesh) // 边① handout 时取（生成在途；r2017 Mesh kind 门）
                m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Loading);
            m_inFlight.push_back(job);
        }
        CompletedGeneration c;
        while (m_worker->takeCompletedAsync(c)) {
            for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ++it) {
                if (it->jobId != c.jobId)
                    continue;
                const Job job = *it;
                m_inFlight.erase(it);
                const Result<void> r = isError(c.error) ? Result<void>::fail(c.error)
                                                        : Result<void>::ok(); // Error → Result 换算
                if (r.isOk() && m_chunks && job.kind != GenerationJobKind::Mesh) // 边②成功后推进（投递与否照旧驱动——内容已生成，R20.12 既有语义；r2017 Mesh kind 门）
                    m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Generated);
                const int delivered = deliver(job, r);
                // 边⑨（Loading→Absent，R20.10b 失败恢复）——仅失败 outcome 实际投递时取：
                //   交接后才过期/被取消的丢弃面不投递故不转移（chunk 停 Loading——世界已换代，
                //   旧任务产物连同其状态转移一并作废，与「epoch 丢弃旧任务」同门）。
                //   r2017 kind 门：Mesh 类 job 不驱动。
                if (!r.isOk() && delivered > 0 && m_chunks && job.kind != GenerationJobKind::Mesh)
                    m_chunks->setLifecycle(job.key.cx, job.key.cz, ChunkLifecycle::Absent);
                eraseRequests(job.jobId);
                break;
            }
        }
    }

    // 为 job 的每个活别名（未取消 && epoch 当前）投递一条 outcome（Error 穿透）。
    //   返回实际投递条数——R20.10b 失败恢复边⑨的投递面判据（丢弃面 = 0 → 不转移）。
    int deliver(const Job &job, const Result<void> &r)
    {
        int delivered = 0;
        for (const Request &req : m_requests) {
            if (req.jobId != job.jobId || req.canceled || req.epoch != m_worldEpoch)
                continue;
            GenerationJobOutcome o;
            o.requestId = req.requestId;
            o.kind = job.kind;
            o.key = job.key;
            o.error = r.error();
            m_outcomes.push_back(o);
            ++delivered;
        }
        return delivered;
    }
    void eraseRequests(quint64 jobId)
    {
        for (auto it = m_requests.begin(); it != m_requests.end();) {
            if (it->jobId == jobId)
                it = m_requests.erase(it);
            else
                ++it;
        }
    }

    GenerationWorker *m_worker = nullptr; // 非拥有（调用者管生命周期）
    ChunkManager *m_chunks = nullptr;     // 可空（纯请求模型）；生命周期 best-effort 记账挂点
    quint32 m_worldEpoch = 0;             // 当前世界 epoch（过期判定基准）
    quint32 m_nextRequestId = 1;          // 0 = 无效哨兵，故从 1 起
    quint64 m_nextJobId = 1;
    std::deque<Job> m_jobs;         // pending 执行单元（合并后）
    std::deque<Request> m_requests; // pending 别名账本（jobId 挂靠）
    std::deque<Job> m_inFlight;     // R20.12：已交接后台未收割的 job（jobId 对账；同步 worker 恒空）
    std::deque<GenerationJobOutcome> m_outcomes; // 已完成结果 FIFO（容量登记见头注分化说明）
};

#endif // GENERATIONJOB_H
