#include "matrix_helpers.h"

#include "generationjob.h" // R20.11 被测：同步版 ChunkScheduler / GenerationJob（请求模型 + worker 缝 + 生命周期互锁）

// R20.11 GenerationJob 探针段（4 腿 r2011a-d；filter 词 "r2011"；矩阵 574→578，band 576±2 内）。
// 置尾先例沿用（接 section14，runAll 末执行）；r2011a-c 纯请求模型腿零世界（裸 scheduler +
// 测试 worker），r2011d 裸 3×3 ChunkManager（默认稳态全 Loaded）+ fresh 小世界 48×48×96
// seed 82 ×1（section11+ 先例；本段不 tick，无 RNG 进窗），对 rig 世界 w 零接触。任务契约
//（docs/refactor-plan-2026-09-08.md §29.3 R20.11 原文四验收）：
//   「生成、加载、mesh 请求都有 request ID」→ r2011a（三 kind 单调计数器唯一 id + 合并别名
//     各自新 id + 满载可见拒绝 kErrQueueFull + 合并语义：pending 重复合并不重跑工作——
//     同 chunk 二次请求 worker 执行计数不增，但两别名各收自己的 outcome；执行完的再请求 =
//     新 job 重跑）+ 优先级序（pri 0 最紧急，同优先级按提交序）；
//   「过期请求可以丢弃」→ r2011b（取消：pending 请求取消后**零执行零投递**，全别名死亡 job
//     整体跳过；合并 job 部分取消 = 其余别名照常执行+投递；幂等拒重复取消；epoch 过期：
//     bump 世界 epoch 后旧请求 pump 时静默丢弃——不执行、不投递）；
//   「重复请求可以合并」→ r2011a（同上）；「结果模型」→ r2011c（worker 失败面 Result 穿透：
//     Error code/message 原样落到 outcome；成功面 outcome FIFO 序可精确预测）；
//   「后续替换成 worker 不需要改调用者」→ r2011c（两个不同实现类 worker 实例跑**同一段调用
//     者驱动代码**（同一 lambda）：requestId 流 / outcome 流 / 执行轨迹三方恒等 = 可替换性
//     的无头实证（不加线程）；中途 setWorker 换实例调用者面零改动）；
//   与 R20.10 生命周期互锁 + 固定世界零变化 → r2011d（挂 ChunkManager 的 scheduler：Absent
//     chunk 的 Generate job 执行前取边① Absent→Loading（worker 执行时刻实测 Loading）+ 成功
//     后取边② Loading→Generated；取消在执行前 = 状态**不推进**（恒 Absent——六态表无
//     Loading→Absent 回退边，本层不做非法回退）；失败停在 Loading（六态表无失败边，恢复登记
//     R20.12）；默认稳态 Loaded chunk 被 submit+pump 后生命周期表**逐位不动**（守卫拒非法
//     转移 → best-effort 忽略 = 固定世界零变化的结构性根据）+ fresh 世界全表 Loaded 稳态 +
//     源码钉（值纪律四钉 / worker 缝签名 / 生命周期边①②调用点 / 满载拒绝码）+ 反探
//    （generationjob.h 零 Q_INVOKABLE/Q_PROPERTY；world.cpp / worldstore.h / Main.qml 零
//     GenerationScheduler/generationjob 引用——生产路径零接线、QML 零迁移）。
// **World 挂点选型登记（dev-plan R20.11 关单同文）**：scheduler 生命周期挂点 = 可变
//   ChunkManager*（ChunkManager::setLifecycle 唯一守卫入口——与 World::setChunkLifecycle
//   forwarder 同一终点）。本单**刻意不**给 World 加非 const chunks() 访问器（World 文件零
//   改动 = 固定世界零变化的最强保证）；真实 World 接线（submit 侧只对 Absent chunk 提交的
//   策略层）登记 R20.12 后台 GenerationJob 一并收口。
// 阴性轮登记（matrix_r2011_neg.log 存证 + Edit 反向还原）：①摘合并——submit 的 pending
//   合并分支条件前置 `false &&`（= 无重复检测，语义恰为「摘合并」；重复提交各成新 job）→
//   r2011a 恰红（合并断言面：job 计数 / worker 执行计数 / outcome 计数），其余腿不受扰
//  （r2011b 部分取消场景无合并时终态同形、r2011c 可替换性两侧同变仍恒等）；②摘过期丢弃——
//   pump 选活扫描的 `r.epoch != m_worldEpoch` 子句摘除（取消子句保留）→ 过期 job 被执行 →
//   r2011b 恰红（执行轨迹含 K3），其余腿全零 epoch 不受扰。
// 确定性口径：全部 worker 逻辑纯确定性（无 RNG / 无时间源 / 无 tick）；裸网格与 fresh 世界
//   worldgen 纯函数于 seed——同序列恒同果，双 worker 对照恒等比较成立。
void MatrixRun::section15_generationjob()
{
    // ── 测试 worker 族（调度缝的两个独立实现类 + 生命周期观察）────────────────────────
    // TraceWorker：同步直跑 worker（记录执行轨迹 + 执行时刻生命周期观察 + 可配置失败集）。
    class TraceWorker : public GenerationWorker
    {
    public:
        struct Exec
        {
            quint32 requestId = 0;
            quint64 key = 0;
            int lifecycle = -1; // 执行时刻 lifecycleAt 观察（-1 = 无观察挂点）
        };
        QVector<Exec> trace;
        QSet<quint64> failKeys; // 命中 → Result::fail(42, "synthetic worker failure")
        const ChunkManager *observe = nullptr;

        Result<void> execute(const GenerationRequest &req) override
        {
            Exec e;
            e.requestId = req.requestId;
            e.key = req.key.packed();
            if (observe)
                e.lifecycle = int(observe->lifecycleAt(req.key.cx, req.key.cz));
            trace.append(e);
            if (failKeys.contains(e.key))
                return Result<void>::fail(42, "synthetic worker failure");
            return Result<void>::ok();
        }
    };
    // AltTraceWorker：**不同实现类**的第二 worker（不同内部表示：平铺双表替代结构体轨迹）
    // ——worker 可替换性腿用：同一调用者驱动代码喂两个类，调度面结果必须恒等。
    class AltTraceWorker : public GenerationWorker
    {
    public:
        QVector<quint32> ids;  // 执行序 requestId
        QVector<quint64> keys; // 执行序 packed key

        Result<void> execute(const GenerationRequest &req) override
        {
            ids.append(req.requestId);
            keys.append(req.key.packed());
            return Result<void>::ok();
        }
    };
    const auto pk = [](int cx, int cz) { return ChunkKey{ cx, cz }.packed(); };

    // ── r2011a：请求模型核心——request ID 唯一性 / 重复合并 / 优先级序 / 满载拒绝（零世界）──
    runLeg(QStringLiteral("r2011a request model core (R20.11 acceptance 'generate/load/mesh"
        " requests all carry request IDs' + 'duplicate requests merge'): one monotonic"
        " counter hands strictly increasing unique IDs across all three kinds, a duplicate"
        " Generate submit for the same pending chunk merges into the existing job (job"
        " count unchanged, a fresh ID still issued to the submitter, the worker executes"
        " the work exactly once while BOTH aliases receive their own Completed outcome in"
        " predictable FIFO order, and different kinds on the same key never merge), a"
        " re-request after completion starts a new job and re-executes (merge is pending-"
        " only), pump order follows (priority asc, submission order) with 0 most urgent,"
        " the 65th distinct pending job is rejected visibly with kErrQueueFull while a"
        " duplicate submit still merges at capacity, and taking from an empty outcome"
        " queue is a normal false"), [&]() {
        bool ok = true;
        QString diag;

        // ① 三 kind 各得唯一且严格递增的 request ID + 空 outcome 队列取值 = 正常 false：
        GenerationScheduler s1;
        TraceWorker w1;
        s1.setWorker(&w1);
        const auto rG = s1.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        const auto rL = s1.submit(GenerationJobKind::Load, ChunkKey{ 1, 1 });
        const auto rM = s1.submit(GenerationJobKind::Mesh, ChunkKey{ 1, 1 });
        const quint32 idG = rG.value(), idL = rL.value(), idM = rM.value();
        bool idOk = rG.isOk() && rL.isOk() && rM.isOk() && idG != 0 && idL != 0 && idM != 0
            && idG < idL && idL < idM; // 单调计数器：唯一 + 递增（跨 kind 共用）
        GenerationJobOutcome probe;
        idOk = idOk && !s1.takeOutcome(probe) && s1.outcomeCount() == 0;
        idOk = idOk && s1.pendingJobCount() == 3 && s1.pendingRequestCount() == 3;
        ok = ok && idOk;
        if (!idOk)
            diag += QStringLiteral("[ids %1/%2/%3 pj=%4 pr=%5] ")
                        .arg(idG).arg(idL).arg(idM)
                        .arg(s1.pendingJobCount()).arg(s1.pendingRequestCount());

        // ② 重复合并：同 (Generate,{1,1}) 再提交 → 新 id（提交方各持己号）但不占新 job 位；
        //    同 key 不同 kind 已由 ① 证不合并（三 job）：
        const auto rG2 = s1.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        const quint32 idG2 = rG2.value();
        const bool mergeOk = rG2.isOk() && idG2 != idG && idG2 > idM
            && s1.pendingJobCount() == 3 && s1.pendingRequestCount() == 4;
        ok = ok && mergeOk;
        if (!mergeOk)
            diag += QStringLiteral("[merge idG2=%1 pj=%2 pr=%3] ")
                        .arg(idG2).arg(s1.pendingJobCount()).arg(s1.pendingRequestCount());

        // ③ pump：工作恰执行 3 次（Generate{1,1} 只跑一次——「同 chunk 二次请求不重跑生成」）；
        //    执行序 = (priority 0 全同) 提交序 → 首活 id 顺序 [idG, idL, idM]；outcome 每活
        //    别名一条、FIFO = [idG, idG2, idL, idM]（job1 双别名按提交序，再 job2/job3）：
        s1.pump();
        bool execOk = w1.trace.size() == 3 && w1.trace[0].requestId == idG
            && w1.trace[1].requestId == idL && w1.trace[2].requestId == idM
            && w1.trace[0].key == pk(1, 1);
        ok = ok && execOk;
        if (!execOk) {
            diag += QStringLiteral("[exec n=%1").arg(w1.trace.size());
            for (const auto &e : w1.trace)
                diag += QStringLiteral(" %2@%3").arg(e.requestId).arg(e.key);
            diag += QStringLiteral("] ");
        }
        const QVector<quint32> wantOutcomes = { idG, idG2, idL, idM };
        QVector<quint32> gotOutcomes;
        bool outOk = s1.outcomeCount() == 4;
        while (s1.takeOutcome(probe)) {
            gotOutcomes.append(probe.requestId);
            outOk = outOk && isError(probe.error) == false; // 成功面
        }
        outOk = outOk && gotOutcomes == wantOutcomes;
        ok = ok && outOk;
        if (!outOk) {
            diag += QStringLiteral("[out n=%1 want=%2/%3/%4/%5 got=")
                        .arg(s1.outcomeCount())
                        .arg(idG).arg(idG2).arg(idL).arg(idM);
            for (quint32 id : gotOutcomes)
                diag += QStringLiteral("%1 ").arg(id);
            diag += QStringLiteral("] ");
        }

        // ④ 执行完再请求 = 新 job 重跑（合并仅 pending 域）：
        const auto rG3 = s1.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        bool reOk = rG3.isOk() && s1.pendingJobCount() == 1;
        s1.pump();
        reOk = reOk && w1.trace.size() == 4 && s1.outcomeCount() == 1
            && s1.takeOutcome(probe) && probe.requestId == rG3.value();
        ok = ok && reOk;
        if (!reOk)
            diag += QStringLiteral("[re pj=%1 n=%2] ")
                        .arg(s1.pendingJobCount()).arg(w1.trace.size());

        // ⑤ 优先级序：pri 0 最紧急，同优先级按提交序（首活 requestId 升序）：
        GenerationScheduler s2;
        TraceWorker w2;
        s2.setWorker(&w2);
        s2.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 }, 5); // A
        s2.submit(GenerationJobKind::Generate, ChunkKey{ 3, 3 }, 1); // B
        s2.submit(GenerationJobKind::Generate, ChunkKey{ 4, 4 }, 5); // C
        s2.submit(GenerationJobKind::Generate, ChunkKey{ 5, 5 }, 0); // D
        s2.pump();
        const QVector<quint64> wantPrio
            = { pk(5, 5), pk(3, 3), pk(2, 2), pk(4, 4) }; // D(0) B(1) A(5) C(5)
        QVector<quint64> gotPrio;
        for (const auto &e : w2.trace)
            gotPrio.append(e.key);
        const bool prioOk = gotPrio == wantPrio;
        ok = ok && prioOk;
        if (!prioOk) {
            diag += QStringLiteral("[prio want=%1/%2/%3/%4 got=")
                        .arg(pk(5, 5)).arg(pk(3, 3)).arg(pk(2, 2)).arg(pk(4, 4));
            for (quint64 k : gotPrio)
                diag += QStringLiteral("%1 ").arg(k);
            diag += QStringLiteral("] ");
        }

        // ⑥ 满载可见拒绝 + 满载下合并仍可达（合并不占新 job 位）：
        GenerationScheduler s3;
        Result<quint32> rLast = Result<quint32>::fail(kErrQueueFull, "loop-init");
        for (int i = 0; i < GenerationScheduler::kMaxPendingJobs; ++i)
            rLast = s3.submit(GenerationJobKind::Generate, ChunkKey{ i, 0 });
        const auto rOver
            = s3.submit(GenerationJobKind::Generate, ChunkKey{ 999, 999 }); // 第 65 个不同键
        const auto rMergeAtCap = s3.submit(GenerationJobKind::Generate, ChunkKey{ 0, 0 });
        const bool fullOk = rLast.isOk()
            && !rOver.isOk() && rOver.error().code == kErrQueueFull
            && s3.pendingJobCount() == GenerationScheduler::kMaxPendingJobs
            && rMergeAtCap.isOk() && s3.pendingJobCount() == GenerationScheduler::kMaxPendingJobs;
        ok = ok && fullOk;
        if (!fullOk)
            diag += QStringLiteral("[full last=%1 over=%2 code=%3 pj=%4 merge=%5] ")
                        .arg(rLast.isOk()).arg(rOver.isOk()).arg(rOver.error().code)
                        .arg(s3.pendingJobCount()).arg(rMergeAtCap.isOk());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2011a request model core: unique strictly-increasing IDs"
                             " across generate/load/mesh, a duplicate pending submit merges"
                             " (fresh ID to the submitter, work runs exactly once, both"
                             " aliases get their own Completed outcome in FIFO order,"
                             " cross-kind never merges), post-completion re-request starts"
                             " a new job, pump order is (priority asc, submission order),"
                             " the 65th distinct job is rejected with kErrQueueFull while"
                             " merges still land at capacity, and empty-result take is a"
                             " normal false"
                          << (ok ? QString() : diag);
    });

    // ── r2011b：取消 + 过期丢弃（验收③「过期请求可以丢弃」——都零执行零投递）────────────
    runLeg(QStringLiteral("r2011b cancel and stale drop (R20.11 acceptance 'stale requests"
        " can be dropped'): canceling an unknown id returns false, a pending single request"
        " canceled before pump executes nothing and delivers nothing (the whole job is"
        " skipped silently, pending drains to zero), re-canceling the same id is an"
        " idempotent false, canceling one alias of a merged pair keeps the job alive for"
        " the surviving alias only (one execution, one outcome carrying the survivor's id,"
        " the canceled id never delivered), and after a world-epoch bump the old-epoch"
        " request is dropped at pump without execution or delivery while the new-epoch"
        " request runs normally"), [&]() {
        bool ok = true;
        QString diag;

        GenerationScheduler s;
        TraceWorker w;
        s.setWorker(&w);

        // ① 未知 id 取消 = false：
        const bool unknownOk = !s.cancel(4242);
        ok = ok && unknownOk;
        if (!unknownOk) diag += QStringLiteral("[unknown-cancel] ");

        // ② 单请求取消：零执行零投递 + 账面清空 + 重复取消幂等拒：
        const auto r1 = s.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        const bool c1 = s.cancel(r1.value());
        const bool c1again = s.cancel(r1.value());
        s.pump();
        const bool cancelOk = r1.isOk() && c1 && !c1again && w.trace.isEmpty()
            && s.outcomeCount() == 0 && s.pendingJobCount() == 0
            && s.pendingRequestCount() == 0;
        ok = ok && cancelOk;
        if (!cancelOk)
            diag += QStringLiteral("[cancel r=%1 c=%2 c2=%3 exec=%4 out=%5 pj=%6] ")
                        .arg(r1.isOk()).arg(c1).arg(c1again).arg(w.trace.size())
                        .arg(s.outcomeCount()).arg(s.pendingJobCount());

        // ③ 合并 job 部分取消：其余别名照常（一次执行 + 幸存者一条 outcome，被取消者永无投递）：
        const auto ra = s.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 });
        const auto rb = s.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 }); // 合并别名
        const bool ca = s.cancel(ra.value());
        s.pump();
        GenerationJobOutcome o;
        bool partialOk = ra.isOk() && rb.isOk() && ca && w.trace.size() == 1
            && s.outcomeCount() == 1 && s.takeOutcome(o) && o.requestId == rb.value()
            && !isError(o.error);
        ok = ok && partialOk;
        if (!partialOk)
            diag += QStringLiteral("[partial ra=%1 rb=%2 ca=%3 exec=%4 out=%5 oid=%6] ")
                        .arg(ra.value()).arg(rb.value()).arg(ca).arg(w.trace.size())
                        .arg(s.outcomeCount()).arg(o.requestId);

        // ④ epoch 过期：bump 世界 epoch 后旧请求 pump 时静默丢弃（不执行不投递），新 epoch 请求照常：
        s.setWorldEpoch(1);
        const auto rs = s.submit(GenerationJobKind::Generate, ChunkKey{ 3, 3 }); // epoch 1 快照
        s.setWorldEpoch(2);
        const auto rn = s.submit(GenerationJobKind::Generate, ChunkKey{ 4, 4 }); // epoch 2
        s.pump();
        bool staleHit = false;
        for (const auto &e : w.trace)
            staleHit = staleHit || e.key == pk(3, 3); // 过期 job 不得执行
        GenerationJobOutcome o2;
        bool staleOk = rs.isOk() && rn.isOk() && !staleHit && w.trace.size() == 2
            && w.trace[1].key == pk(4, 4) && w.trace[1].requestId == rn.value()
            && s.outcomeCount() == 1 && s.takeOutcome(o2) && o2.requestId == rn.value()
            && s.pendingJobCount() == 0 && s.pendingRequestCount() == 0;
        ok = ok && staleOk;
        if (!staleOk)
            diag += QStringLiteral("[stale hit=%1 exec=%2 out=%3 oid=%4 oreq=%5 want=%6] ")
                        .arg(staleHit).arg(w.trace.size()).arg(s.outcomeCount())
                        .arg(o2.requestId).arg(o2.requestId).arg(rn.value());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2011b cancel and stale drop: unknown cancel false, a"
                             " canceled pending request runs nothing and delivers nothing"
                             " (whole job skipped, ledger drained, re-cancel idempotent"
                             " false), partial alias cancel keeps the merged job alive for"
                             " the survivor only, and an old-epoch request is dropped"
                             " silently at pump (no execution, no delivery) while the"
                             " current-epoch request runs normally"
                          << (ok ? QString() : diag);
    });

    // ── r2011c：失败面 Result 穿透 + worker 可替换性（验收④「换 worker 不改调用者」）────
    runLeg(QStringLiteral("r2011c failure face Result penetration + worker replaceability"
        " (R20.11 acceptance 'later worker replacement needs no caller changes'): a worker"
        " failure lands as a Failed outcome with the worker's error code and message"
        " penetrating verbatim while sibling requests complete unaffected (the failed work"
        " is still attempted), and the identical caller driver lambda run against two"
        " different worker implementation classes (two instances) produces identical"
        " request-ID streams, identical outcome streams and identical execution traces -"
        " the worker seam is swappable without touching caller code, including a mid-life"
        " setWorker swap that keeps the submit/pump/take call sites unchanged"), [&]() {
        bool ok = true;
        QString diag;

        // ① 失败面：worker Result::fail 穿透到 outcome（code+message 原样），兄弟请求不受扰：
        GenerationScheduler sf;
        TraceWorker wf;
        wf.failKeys.insert(pk(7, 7)); // 该 key 失败
        sf.setWorker(&wf);
        const auto rf1 = sf.submit(GenerationJobKind::Generate, ChunkKey{ 7, 7 });
        const auto rf2 = sf.submit(GenerationJobKind::Load, ChunkKey{ 8, 8 });
        sf.pump();
        GenerationJobOutcome of1, of2;
        const bool failOk = rf1.isOk() && rf2.isOk() && wf.trace.size() == 2 // 失败仍被尝试
            && sf.outcomeCount() == 2 && sf.takeOutcome(of1) && sf.takeOutcome(of2)
            && of1.requestId == rf1.value() && isError(of1.error)
            && of1.error.code == 42
            && of1.error.message
                && qstrcmp(of1.error.message, "synthetic worker failure") == 0 // Result 穿透
            && of2.requestId == rf2.value() && !isError(of2.error);
        ok = ok && failOk;
        if (!failOk)
            diag += QStringLiteral("[fail r1=%1 r2=%2 exec=%3 n=%4 code=%5 msg=%6 ok2=%7] ")
                        .arg(rf1.isOk()).arg(rf2.isOk()).arg(wf.trace.size())
                        .arg(sf.outcomeCount())
                        .arg(of1.error.code)
                        .arg(of1.error.message ? of1.error.message : "(null)")
                        .arg(!isError(of2.error));

        // ② 可替换性：同一段调用者驱动代码 × 两个不同 worker 实现类 → 三方恒等：
        const auto drive = [](GenerationScheduler &s, QVector<quint32> &idStream,
                              QVector<GenerationJobOutcome> &outcomes) {
            const auto i1 = s.submit(GenerationJobKind::Generate, ChunkKey{ 10, 10 });
            const auto i2 = s.submit(GenerationJobKind::Mesh, ChunkKey{ 10, 10 });
            const auto i3 = s.submit(GenerationJobKind::Generate, ChunkKey{ 10, 10 }); // 合并
            const auto i4 = s.submit(GenerationJobKind::Load, ChunkKey{ 11, 11 }, 3);
            const auto i5 = s.submit(GenerationJobKind::Generate, ChunkKey{ 12, 12 }, 1);
            idStream = { i1.value(), i2.value(), i3.value(), i4.value(), i5.value() };
            s.pump();
            GenerationJobOutcome o;
            while (s.takeOutcome(o))
                outcomes.append(o);
        };
        GenerationScheduler sA;
        TraceWorker wA;
        sA.setWorker(&wA);
        GenerationScheduler sB;
        AltTraceWorker wB; // 不同实现类、不同内部表示——同一缝
        sB.setWorker(&wB);
        QVector<quint32> idsA, idsB;
        QVector<GenerationJobOutcome> outsA, outsB;
        drive(sA, idsA, outsA); // 调用者代码字面同一（lambda 复用）
        drive(sB, idsB, outsB);
        const QVector<quint32> wantIds = { 1, 2, 3, 4, 5 }; // 双方各自从 1 起单调
        // 可替换性腿只断言 **A ≡ B 相对恒等**（绝对 FIFO 序已由 r2011a③ 钉——此处若再钉
        // 绝对序，「摘合并」阴性会误伤本腿：无合并时两侧同变为 [1,2,3,5,4] 仍恒等）：
        bool sameIds = idsA == idsB && idsA == wantIds;
        bool sameOuts = outsA.size() == outsB.size() && outsA.size() == 5;
        QSet<quint32> outcomeIdSetA, outcomeIdSetB;
        for (int i = 0; sameOuts && i < outsA.size(); ++i) {
            sameOuts = sameOuts && outsA[i].requestId == outsB[i].requestId
                && outsA[i].kind == outsB[i].kind && outsA[i].key == outsB[i].key
                && !isError(outsA[i].error) && !isError(outsB[i].error);
            outcomeIdSetA.insert(outsA[i].requestId);
            outcomeIdSetB.insert(outsB[i].requestId);
        }
        sameOuts = sameOuts && outcomeIdSetA.size() == 5 && outcomeIdSetB.size() == 5;
        // 执行轨迹相对恒等（绝对执行次数 = 合并后 job 数，由 r2011a 绝对钉——本腿只钉
        // 「两个 worker 类看到同一序列」，摘合并时两侧同变仍恒等）：
        bool sameTrace = wA.trace.size() == wB.ids.size() && !wB.ids.isEmpty();
        for (int i = 0; sameTrace && i < wA.trace.size(); ++i)
            sameTrace = sameTrace && wA.trace[i].requestId == wB.ids[i]
                && wA.trace[i].key == wB.keys[i];
        const bool replOk = sameIds && sameOuts && sameTrace;
        ok = ok && replOk;
        if (!replOk)
            diag += QStringLiteral("[repl ids=%1 outs=%2 trace=%3 nA=%4 nB=%5] ")
                        .arg(sameIds).arg(sameOuts).arg(sameTrace)
                        .arg(outsA.size()).arg(outsB.size());

        // ③ 中途换 worker：submit/pump/take 调用点零改动，新实例接走后续执行
        //    （旧 worker 执行计数快照锚——摘合并时该计数变 5，相对锚不受扰）：
        const int execsBeforeSwap = wA.trace.size();
        TraceWorker wA2;
        sA.setWorker(&wA2);
        const auto rs = sA.submit(GenerationJobKind::Generate, ChunkKey{ 20, 20 });
        sA.pump();
        GenerationJobOutcome os;
        const bool swapOk = rs.isOk() && wA2.trace.size() == 1
            && wA2.trace[0].key == pk(20, 20)
            && wA.trace.size() == execsBeforeSwap // 旧 worker 不再增长
            && sA.outcomeCount() == 1 && sA.takeOutcome(os)
            && os.requestId == rs.value();
        ok = ok && swapOk;
        if (!swapOk)
            diag += QStringLiteral("[swap new=%1 old=%2 out=%3 oid=%4 want=%5] ")
                        .arg(wA2.trace.size()).arg(wA.trace.size())
                        .arg(sA.outcomeCount()).arg(os.requestId).arg(rs.value());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2011c failure face + worker replaceability: a worker"
                             " failure is attempted then delivered as a Failed outcome"
                             " with error code/message penetrating verbatim (siblings"
                             " unaffected), and the same caller driver lambda against two"
                             " different worker classes yields identical ID streams,"
                             " outcome streams and execution traces, including a mid-life"
                             " setWorker swap with untouched call sites"
                          << (ok ? QString() : diag);
    });

    // ── r2011d：R20.10 生命周期互锁 + 固定世界零变化 + 源码钉/反探 ────────────────────────
    runLeg(QStringLiteral("r2011d lifecycle interlock + fixed-world zero change + structure"
        " pins (R20.10 integration): a scheduler attached to a bare 3x3 ChunkManager drives"
        " the documented lifecycle edges only - an Absent chunk's Generate job is observed"
        " in Loading by the worker at execution time (edge 1) and lands in Generated after"
        " success (edge 2), a canceled-before-run request leaves its chunk at Absent (no"
        " advance, no illegal rollback), a failed job stays at Loading (no failure edge,"
        " recovery registered for R20.12), and submitting+pumping a default-steady-Loaded"
        " chunk executes the work but leaves all nine lifecycle entries bit-untouched (the"
        " guarded transition rejects and the scheduler ignores - the structural reason the"
        " fixed world is behaviorally unchanged); a fresh 48x48x96 s82 world still comes up"
        " all-Loaded; comment-stripped pins hold the value-discipline static asserts, the"
        " worker seam signature, both lifecycle edge call sites and the queue-full code,"
        " while reverse probes prove no Q_INVOKABLE/Q_PROPERTY ever touches"
        " generationjob.h and world.cpp / worldstore.h / Main.qml never reference the"
        " scheduler (zero production wiring, zero QML migration)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 裸 3×3 网格（默认稳态全 Loaded——r2010a 同款）+ 挂 scheduler：
        ChunkManager mgr(48, 48, 32);
        bool steadyOk = mgr.chunksX() == 3 && mgr.chunksZ() == 3;
        for (int cz = 0; cz < 3 && steadyOk; ++cz)
            for (int cx = 0; cx < 3 && steadyOk; ++cx)
                steadyOk = steadyOk && mgr.lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && steadyOk;
        if (!steadyOk) diag += QStringLiteral("[steady] ");

        GenerationScheduler sched(&mgr);
        TraceWorker w;
        w.observe = &mgr; // 执行时刻生命周期观察（边① 的实测面）
        sched.setWorker(&w);

        // ② 互锁主链：(2,2) 布到 Absent → submit → 执行（worker 实测 Loading）→ Generated；
        //    (0,1) 布到 Absent → submit → **取消**（执行前）→ 泵后恒 Absent（不推进不回退）：
        const bool route22 = mgr.setLifecycle(2, 2, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(2, 2, ChunkLifecycle::Absent);
        const auto ra = sched.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 });
        const bool route01 = mgr.setLifecycle(0, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(0, 1, ChunkLifecycle::Absent);
        const auto rb = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 1 });
        const bool cb = sched.cancel(rb.value());
        const bool setupOk = route22 && ra.isOk() && route01 && rb.isOk() && cb;
        ok = ok && setupOk;
        if (!setupOk)
            diag += QStringLiteral("[setup r22=%1 ra=%2 r01=%3 rb=%4 cb=%5] ")
                        .arg(route22).arg(ra.isOk()).arg(route01).arg(rb.isOk()).arg(cb);
        sched.pump();
        const bool execOne = w.trace.size() == 1 && w.trace[0].key == pk(2, 2)
            && w.trace[0].requestId == ra.value();
        const bool edge1 = w.trace.size() == 1
            && w.trace[0].lifecycle == int(ChunkLifecycle::Loading); // 边①执行前已取
        const bool edge2 = mgr.lifecycleAt(2, 2) == ChunkLifecycle::Generated; // 边②成功后
        const bool noAdvance = mgr.lifecycleAt(0, 1) == ChunkLifecycle::Absent; // 取消不推进
        GenerationJobOutcome oa;
        const bool outA = sched.outcomeCount() == 1 && sched.takeOutcome(oa)
            && oa.requestId == ra.value() && !isError(oa.error);
        const bool interlockOk = execOne && edge1 && edge2 && noAdvance && outA;
        ok = ok && interlockOk;
        if (!interlockOk)
            diag += QStringLiteral("[lock exec=%1 e1=%2 e2=%3 adv=%4 out=%5 01=%6] ")
                        .arg(execOne).arg(edge1).arg(edge2).arg(noAdvance).arg(outA)
                        .arg(int(mgr.lifecycleAt(0, 1)));

        // ③ 失败不推进：六态表无 Loading 失败边 → 失败 job 停在 Loading（恢复登记 R20.12）：
        const bool route11 = mgr.setLifecycle(1, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(1, 1, ChunkLifecycle::Absent);
        w.failKeys.insert(pk(1, 1));
        const auto rc = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        sched.pump();
        GenerationJobOutcome oc;
        const bool failPathOk = route11 && rc.isOk() && w.trace.size() == 2
            && sched.outcomeCount() == 1 && sched.takeOutcome(oc)
            && oc.requestId == rc.value() && isError(oc.error) && oc.error.code == 42
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Loading;
        ok = ok && failPathOk;
        if (!failPathOk)
            diag += QStringLiteral("[failpath r=%1 exec=%2 code=%3 life=%4] ")
                        .arg(rc.isOk()).arg(w.trace.size()).arg(oc.error.code)
                        .arg(int(mgr.lifecycleAt(1, 1)));
        w.failKeys.clear();

        // ④ 固定世界零变化（结构性）：默认稳态 Loaded chunk 直接 submit+pump → 工作执行、
        //    生命周期表 9 格逐位不动（守卫拒非法转移、scheduler 忽略——零变化的结构根据）。
        //    表快照对照（前态：②③已把 (2,2)=Generated、(0,1)=Absent、(1,1)=Loading 置位——
        //    ④ 的断言面是「④ 前后表逐位恒等」+ (1,0) 特判仍 Loaded）：
        std::array<ChunkLifecycle, 9> lifeBefore {};
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                lifeBefore[size_t(cx + 3 * cz)] = mgr.lifecycleAt(cx, cz);
        const auto rd = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 0 });
        sched.pump();
        bool fixedOk = rd.isOk() && w.trace.size() == 3
            && mgr.lifecycleAt(1, 0) == ChunkLifecycle::Loaded;
        for (int cz = 0; cz < 3 && fixedOk; ++cz)
            for (int cx = 0; cx < 3 && fixedOk; ++cx)
                fixedOk = fixedOk
                    && mgr.lifecycleAt(cx, cz) == lifeBefore[size_t(cx + 3 * cz)];
        ok = ok && fixedOk;
        if (!fixedOk)
            diag += QStringLiteral("[fixed exec=%1 life10=%2] ")
                        .arg(w.trace.size()).arg(int(mgr.lifecycleAt(1, 0)));

        // ⑤ fresh 世界稳态零变化（固定 10×10 / 48×48 世界族的行为基线照常）：
        World wD;
        wD.setWidth(48);
        wD.setDepth(48);
        wD.setHeight(96);
        wD.setSeed(82);
        bool worldOk = wD.chunksX() == 3 && wD.chunksZ() == 3;
        for (int cz = 0; cz < wD.chunksZ() && worldOk; ++cz)
            for (int cx = 0; cx < wD.chunksX() && worldOk; ++cx)
                worldOk = worldOk
                    && wD.chunks().lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && worldOk;
        if (!worldOk) diag += QStringLiteral("[world] ");

        // ⑥ 源码钉（pinSet 剥注释）+ 反探（minCount=1 空转钉惯用法——miss 非空 = 合规缺席）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QStringList missGj = pinSet(
            srcRoot + QStringLiteral("/World/generationjob.h"), {
                SrcPin("r2011 request QObjectFree pin",
                    "static_assert(QObjectFree<GenerationRequest>", 1),
                SrcPin("r2011 outcome QObjectFree pin",
                    "static_assert(QObjectFree<GenerationJobOutcome>", 1),
                SrcPin("r2011 request trivially-copyable pin",
                    "std::is_trivially_copyable_v<GenerationRequest>", 1),
                SrcPin("r2011 outcome trivially-copyable pin",
                    "std::is_trivially_copyable_v<GenerationJobOutcome>", 1),
                SrcPin("r2011 worker seam typed on Result", "virtual Result<void> execute(", 1),
                SrcPin("r2011 lifecycle edges driven via the guarded entry",
                    "m_chunks->setLifecycle(", 2),
                SrcPin("r2011 queue-full visible rejection", "kErrQueueFull", 1),
                SrcPin("r2011 request kind enum", "enum class GenerationJobKind : quint8", 1),
            });
        for (const QString &m : missGj) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool negOk
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/generationjob.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationjob.h"), "Q_PROPERTY")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "GenerationScheduler")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "generationjob")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "GenerationScheduler")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "enerationJob");
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg-probe] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2011d lifecycle interlock + fixed-world zero change:"
                             " worker observes Loading at execution time and the chunk"
                             " lands Generated after success, cancel-before-run never"
                             " advances (stays Absent), failure parks at Loading, a"
                             " default-Loaded chunk runs the work with the nine-entry"
                             " lifecycle table bit-untouched, a fresh world still comes up"
                             " all-Loaded, and structure pins + reverse probes hold the"
                             " value-discipline asserts, the guarded lifecycle call sites"
                             " and zero QML / zero production wiring"
                          << (ok ? QString() : diag);
    });
}
