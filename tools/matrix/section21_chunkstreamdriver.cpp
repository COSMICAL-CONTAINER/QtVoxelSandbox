#include "matrix_helpers.h"

#include "chunkstreamdriver.h" // §29.4-P2 被测：ChunkStreamDriver 位置沿编排值组件（自持 GenerationJob）
#include "generationpolicy.h"  // 测试侧 twin policy：期望 decide 输出的独立计算源（r2016 已钉其本体）

// §29.4-P2 ChunkStreamDriver 探针段（4 腿 r2018a-d；filter 词 "r2018"；矩阵 608→612，band 610±2 内）。
// 置尾先例沿用（接 section20，runAll 末执行）；全段**零世界**（r2016/r2011a-c 同款——被测面是
// 纯编排值组件，缝 = 测试内 QMap 合成生命周期表，rig 世界 w 零接触，无 RNG 无 tick 无时间源，
// worker 全为无线程测试替身）。任务契约（docs/refactor-plan-2026-09-08.md §29.4「P2 玩家移动
// 驱动」原文：「位置沿 → 视距 chunk 请求/取消（GenerationJob 三 kind 已含优先级）；生成风暴背
// 压 = 队列满载拒绝既有语义」+ 治理审计 #10 放行）：
//   「固定世界默认关」延续 → r2018a（**本单承重墙**：streamingEnabled=false 三种构造路径
//     [默认构造 / 规整构造显式关 / setParams 关] 下玩家位扫掠（含大幅移动）提交/取消/拒绝全
//     零、底层队列逐位不动、worker 零执行；第四相 = 使能装载后中途关断——关态在途账逐位不
//     动[半径取消环在关态不得触碰在途 job，NEG-1 的恰红载体]）；
//   「位置沿 → 视距 chunk 请求/取消」→ r2018b（承重：传送序列逐沿 submit 集 ≡ twin decide
//     输出逐项恒等 + priority 保序；出半径 pending key 恰被 cancel 一次[跨沿不重复取消、已执
//     行 id 的 cancel false 不入账]且泵后不执行；同 key 跨沿合并别名制 dedupe[零重复 pending、
//     全别名死亡整体跳过零执行]；完成后重入半径重提交 = 新 requestId）；
//   「生成风暴背压 = 队列满载拒绝既有语义」→ r2018c（gen=4 窗 81 > 底层 64 容量：远距传送
//     rejectedSubmissions 恰等于被拒数、被拒 key 零 job 零 outcome[零悬别名]、泵排干 + 关单
//     格重沿重算收敛[最终提交集 == 决策集，原被拒 key 全量重发承接]）；
//   「r2017 kind 门语义不受影响」+ 值/结构纪律 → r2018d（ScriptedAsyncWorker 替身走
//     pumpAsync 交接/收割两相：交接沿 = handed 记录可见[全 Generate kind + key/priority/序
//     恒等]、收割沿 = outcome 交付可见；结构钉 = 头文件禁 QObject 基类/QML/线程原语/
//     World*/ChunkManager*/setLifecycle 记号[驱动器零边驱动——生命周期边唯一权威 stays
//     GenerationJob，ChunkManager 挂点归生产接线] + 默认关双短路正面钉 + 默认全 Generate
//     提交面钉[Mesh 禁出；r2019 起 Load 经 saved-content 缝门控合法——原 Load 禁探同变更
//     修订，见 r2018d 腿内注] + 同移动序列双驱动器重放 submit/cancel 轨迹逐位恒等[确定性]）。
// ── 阴性轮恰红面设计（**先于腿文定稿**，R20.11 收缩纪律；存证 matrix_r2018_neg1_red.log /
//    matrix_r2018_neg1_restore.log / matrix_r2018_neg2_red.log / matrix_r2018_neg2_restore.log
//    直落 build/ 终名，红跑/还原分文件，禁 TEMP 中转）────────────────────────────────────
//   NEG-1 摘默认关短路——chunkstreamdriver.h onPlayerChunk 入口 `if (!m_params.streamingEnabled)`
//     前置 `false &&`（= 关态也走编排体）→ **恰红 r2018a 单腿**（红载体 = 第四相「关态在途账
//     不动」：使能装载 25 pending → 中途关断 → 扫掠远位时半径取消环在关态照跑 → stats.canceled
//     =25>0；相①-③从未装载、在途集恒空 → 摘短路不可见[绿]）。r2018b/c/d 全使能驱动器：原门
//     对使能态本就不短路，变异后行为逐位不变 → 不误伤。
//   NEG-2 摘出半径取消——onPlayerChunk 尾部取消环条件 `it != m_pending.end()` 前置 `false &&`
//     （= 环体永不迭代，零取消）→ **恰红 r2018b 单腿的取消柱**（stats.canceled==期望 失配 +
//     泵后应被取消 key 现身 executed + D2 波零 outcome 断言失配；提交面柱[submit 集恒等/
//     priority 保序/dedupe pendingJobCount/重入新 id]不依赖取消 → diag 仅取消柱失配）。
//     r2018a 相④断言 canceled==0 + 队列不动 → 摘取消反而更「不动」→ 不红；r2018c 场景设计
//     为取消无关面[首沿即风暴、收敛沿队列已空；离位 stale id 的取消本就 false 不入账] → 不
//     红；r2018d 确定性断言比较双驱动器互证（取消面两侧同变仍恒等）、无取消绝对断言 → 不红。
// 确定性口径：被测组件纯同步编排（决定性 QMap 迭代序 + 单调 requestId）；缝表手工布置（腿选
//   址纪律：结构性布点勿靠缺席默认凑断言——期望集一律经 twin policy 独立计算 + 手工锚点交叉
//   核对），全部断言静态可预测。
void MatrixRun::section21_chunkstreamdriver()
{
    // ── 段内共享帮手（零世界；缝 = 合成生命周期表）──────────────────────────────────────
    // 缝表键：packed(cx,cz)（ChunkKey 自带双射打包；负坐标同等合法）。
    const auto kkey = [](int cx, int cz) { return ChunkKey{ cx, cz }.packed(); };
    // 合成生命周期表缝：表缺席格默认 Absent（ChunkManager::lifecycleAt 越界同款语义）。
    const auto tableSeam = [&kkey](const QMap<quint64, ChunkLifecycle> &t) {
        return [&t, &kkey](int cx, int cz) -> ChunkLifecycle {
            return t.value(kkey(cx, cz), ChunkLifecycle::Absent);
        };
    };
    // 期望集独立计算源：twin policy（本体被 r2016 钉；本段只消费其请求单——toEvict 执行属
    // P3 登记非目标，本段零消费）。
    const auto twinDecide = [&](const GenerationPolicyParams &par, int px, int pz,
                                const QMap<quint64, ChunkLifecycle> &t) {
        GenerationPolicy twin{ par };
        return twin.decide(px, pz, tableSeam(t)).toRequest;
    };
    const auto reqKeys = [](const QVector<GenerationPolicyRequest> &v) {
        QSet<quint64> s;
        for (const auto &r : v)
            s.insert(ChunkKey{ r.cx, r.cz }.packed());
        return s;
    };
    // diag 帮手：decide 单 / 执行轨迹紧凑串（腿 diag 带判别信息纪律）。
    const auto reqStr = [](const QVector<GenerationPolicyRequest> &v) {
        QString s;
        for (const auto &r : v)
            s += QStringLiteral("(%1,%2)p%3 ").arg(r.cx).arg(r.cz).arg(r.priority);
        return s;
    };
    const auto execStr = [](const QVector<GenerationRequest> &v) {
        QString s;
        for (const auto &r : v)
            s += QStringLiteral("[%1](%2,%3)p%4 ")
                     .arg(r.requestId)
                     .arg(r.key.cx)
                     .arg(r.key.cz)
                     .arg(r.priority);
        return s;
    };
    // 波次恒等：执行轨迹片段 == decide 单（逐项 key + priority + 序 = 「submit 集 ≡ decide
    // 输出、priority 原样传递」的执行面观测）。
    const auto waveMatches = [](const QVector<GenerationRequest> &exec, int from,
                                const QVector<GenerationPolicyRequest> &wave) {
        if (exec.size() - from != wave.size())
            return false;
        for (int i = 0; i < wave.size(); ++i) {
            const auto &e = exec[from + i];
            const auto &w = wave[i];
            if (e.key.cx != w.cx || e.key.cz != w.cz || e.priority != w.priority
                || e.kind != GenerationJobKind::Generate)
                return false;
        }
        return true;
    };

    // 同步轨迹 worker（全成功面；execute 序 = 提交/取消编排的最终执行轨迹）。
    class TraceSyncWorker : public GenerationWorker
    {
    public:
        QVector<GenerationRequest> executed;
        Result<void> execute(const GenerationRequest &req) override
        {
            executed.append(req);
            return Result<void>::ok();
        }
    };

    // 脚本化异步 worker（r2010bb/r2017b 先例替身：零线程零时序，完成记录按脚本 FIFO 交付）。
    class ScriptedAsyncWorker : public GenerationWorker
    {
    public:
        QVector<GenerationRequest> handed; // 交接序（submitAsync 受理记录 = 交接沿可见面）
        QVector<quint64> handedJobIds;     // 交接序 jobId（脚本完成记录对账键）
        struct Done
        {
            quint64 jobId = 0;
            Error error{};
        };
        QVector<Done> script;

        bool isAsynchronous() const override { return true; }
        Result<void> execute(const GenerationRequest &req) override
        {
            Q_UNUSED(req);
            return Result<void>::fail(201, "ScriptedAsyncWorker is async-only (use submitAsync)");
        }
        Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
        {
            handed.append(req);
            handedJobIds.append(jobId);
            return Result<void>::ok();
        }
        bool takeCompletedAsync(CompletedGeneration &out) override
        {
            if (script.isEmpty())
                return false;
            const Done d = script.takeFirst();
            out = CompletedGeneration{ d.jobId, d.error };
            return true;
        }
    };

    // ── r2018a：默认关惰性承重墙——disabled 任意扫掠全零动作 + 关态在途账逐位不动─────────
    runLeg(QStringLiteral("r2018a default-off inert load-bearing wall (section 29.4 P2, D1"
        " 'fixed world stays off by default'): with streamingEnabled=false across all three"
        " construction paths (default-constructed driver, normalized constructor with off"
        " params, setParams flip), a player-position sweep including large teleports"
        " produces zero submissions, zero cancellations, zero rejections, an untouched"
        " underlying job queue, zero outcomes and a never-executed worker through"
        " onPlayerChunk and pump alike; and a driver that loaded 25 in-flight jobs while"
        " enabled then flipped off keeps that in-flight account bit-untouched across"
        " further far-position sweeps - the radius-cancel loop must not fire and the"
        " pump must not forward while streaming is disabled"), [&]() {
        bool ok = true;
        QString diag;

        // 共享缝表（混合面：若被测短路失效即有请求可观察）。
        QMap<quint64, ChunkLifecycle> tMixed;
        tMixed.insert(kkey(0, 0), ChunkLifecycle::Absent);
        tMixed.insert(kkey(1, 0), ChunkLifecycle::Generated);
        tMixed.insert(kkey(-3, 2), ChunkLifecycle::Evicting);
        tMixed.insert(kkey(9, 9), ChunkLifecycle::Active);

        // 相①-③：三种关态构造路径 × 扫掠序列（含大幅移动 (40,-30)）→ 全零动作：
        const int sweep[4][2] = { { 0, 0 }, { 3, 0 }, { 3, 4 }, { 40, -30 } };
        for (int phase = 0; phase < 3 && ok; ++phase) {
            TraceSyncWorker w;
            ChunkStreamDriver drv; // 默认构造 = 默认关
            if (phase == 1)
                drv.setParams(GenerationPolicyParams(false, 3, 1, 5)); // 规整构造显式关
            else if (phase == 2)
                drv.setParams(GenerationPolicyParams(false, 2, 1, 4)); // setParams 关
            drv.setSeam(tableSeam(tMixed));
            drv.setWorker(&w);
            for (const auto &pos : sweep) {
                drv.onPlayerChunk(pos[0], pos[1]);
                drv.pump(); // 关态 pump 亦零动作（不转发底层泵）
                const auto st = drv.stats();
                const bool zero = st.submitted == 0 && st.canceled == 0 && st.rejected == 0
                    && drv.pendingJobCount() == 0 && drv.outcomeCount() == 0
                    && w.executed.isEmpty();
                if (!zero) {
                    ok = false;
                    diag += QStringLiteral("[ph%1 at(%2,%3) s=%4/c=%5/r=%6 pj=%7 oc=%8 ex=%9] ")
                                .arg(phase)
                                .arg(pos[0])
                                .arg(pos[1])
                                .arg(st.submitted)
                                .arg(st.canceled)
                                .arg(st.rejected)
                                .arg(drv.pendingJobCount())
                                .arg(drv.outcomeCount())
                                .arg(w.executed.size());
                }
            }
        }

        // 相④：使能装载 → 中途关断 → 关态扫掠：在途账逐位不动（NEG-1 恰红载体——摘短路则
        //   半径取消环在关态照跑：25 个在途 key 相对 (5,0) 全部 cheb≥3>2 → canceled=25>0 红）：
        TraceSyncWorker w4;
        ChunkStreamDriver drv4{ GenerationPolicyParams(true, 2, 1, 4) };
        QMap<quint64, ChunkLifecycle> tEmpty; // 全 Absent：gen=2 窗 5×5 = 25 请求
        drv4.setSeam(tableSeam(tEmpty));
        drv4.setWorker(&w4);
        drv4.onPlayerChunk(0, 0);
        const bool loaded = drv4.stats().submitted == 25 && drv4.pendingJobCount() == 25
            && drv4.stats().canceled == 0 && drv4.stats().rejected == 0;
        drv4.setParams(GenerationPolicyParams(false, 2, 1, 4)); // 中途关断（半径不变）
        const bool offNow = !drv4.params().streamingEnabled;
        const int offSweep[2][2] = { { 5, 0 }, { 5, 5 } };
        bool frozen = offNow;
        for (const auto &pos : offSweep) {
            drv4.onPlayerChunk(pos[0], pos[1]);
            drv4.pump(); // 关态：零动作（不取消在途、不转发泵）
            const auto st = drv4.stats();
            frozen = frozen && st.submitted == 25 && st.canceled == 0 && st.rejected == 0
                && drv4.pendingJobCount() == 25 && drv4.outcomeCount() == 0
                && w4.executed.isEmpty();
        }
        if (!loaded || !frozen) {
            ok = false;
            diag += QStringLiteral("[ph4 loaded=%1 frozen=%2 s=%3/c=%4/r=%5 pj=%6] ")
                        .arg(loaded)
                        .arg(frozen)
                        .arg(drv4.stats().submitted)
                        .arg(drv4.stats().canceled)
                        .arg(drv4.stats().rejected)
                        .arg(drv4.pendingJobCount());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2018a default-off inert wall: with streamingEnabled=false"
                             " (default ctor, explicit off ctor, setParams flip) position"
                             " sweeps with teleports yield zero submits/cancels/rejections,"
                             " an untouched queue, zero outcomes and an untouched worker"
                             " through onPlayerChunk and pump; a loaded-then-disabled driver"
                             " keeps its 25 in-flight jobs bit-untouched across far sweeps"
                          << (ok ? QString() : diag);
    });

    // ── r2018b：请求跟随 + 取消面（承重）——submit ≡ twin decide、priority 保序、离半径取消、
    //    合并别名 dedupe、完成后重入重提交新 id────────────────────────────────────────────
    runLeg(QStringLiteral("r2018b follow-and-cancel face (section 29.4 P2 'position edge ->"
        " in-radius requests / out-of-radius cancels'): each teleport edge's submission set"
        " equals the twin policy's decide output item-for-item with priorities passed"
        " through and fresh waves executing in decide order; repeated same-chunk calls are"
        " no-ops (change-edge semantics); pending keys leaving the generation radius are"
        " canceled exactly once (executed ids never recount, later edges never re-cancel)"
        " and never execute after the pump, with zero outcomes for canceled aliases;"
        " cross-edge duplicate requests merge into one pending job (zero duplicate"
        " pendings, work runs once per key, all-alias-death skips the job entirely); a"
        " completed-then-re-entered key is resubmitted under a brand-new requestId"), [&]() {
        bool ok = true;
        QString diag;
        const GenerationPolicyParams par(true, 2, 1, 4); // gen=2 → 5×5 窗

        // 主缝表（手工布置：Generated/Loading 混合；未列格默认 Absent——期望集一律 twin 计算）：
        QMap<quint64, ChunkLifecycle> tb;
        tb.insert(kkey(0, 0), ChunkLifecycle::Generated);
        tb.insert(kkey(1, 0), ChunkLifecycle::Generated);
        tb.insert(kkey(0, 1), ChunkLifecycle::Generated);
        tb.insert(kkey(-1, 0), ChunkLifecycle::Generated);
        tb.insert(kkey(4, 0), ChunkLifecycle::Generated);
        tb.insert(kkey(2, 2), ChunkLifecycle::Loading);

        TraceSyncWorker w;
        ChunkStreamDriver d1{ par };
        d1.setSeam(tableSeam(tb));
        d1.setWorker(&w);

        // 相①（跟随 + 保序 + 沿幂等）：e1@(0,0) → 泵 → executed == D1 逐项恒等：
        const auto D1 = twinDecide(par, 0, 0, tb);
        const bool anchor1 = D1.size() == 20; // 手工锚：25 − 4 Generated − 1 Loading
        d1.onPlayerChunk(0, 0);
        d1.pump();
        int drained = 0;
        GenerationJobOutcome oc;
        bool ocOk = true;
        while (d1.takeOutcome(oc)) {
            ++drained;
            ocOk = ocOk && !isError(oc.error) && oc.kind == GenerationJobKind::Generate;
        }
        const bool ph1 = anchor1 && D1.size() == 20 && w.executed.size() == D1.size()
            && waveMatches(w.executed, 0, D1) && drained == D1.size() && ocOk
            && d1.stats().submitted == D1.size() && d1.stats().canceled == 0
            && d1.stats().rejected == 0 && d1.pendingJobCount() == 0;
        if (!ph1)
            diag += QStringLiteral("[ph1 anchor=%1 ex=%2 want=%3 drained=%4 s=%5/c=%6] ")
                        .arg(anchor1)
                        .arg(execStr(w.executed), reqStr(D1))
                        .arg(drained)
                        .arg(d1.stats().submitted)
                        .arg(d1.stats().canceled);
        // 世界成熟（测试扮演边②）：D1 key 全部 Generated：
        for (const auto &r : D1)
            tb.insert(kkey(r.cx, r.cz), ChunkLifecycle::Generated);
        // 沿幂等：同 chunk 重复调用零动作：
        const int subBefore = d1.stats().submitted;
        const int exBefore = w.executed.size();
        d1.onPlayerChunk(0, 0);
        const bool idem = d1.stats().submitted == subBefore && w.executed.size() == exBefore
            && d1.stats().canceled == 0;
        ok = ok && ph1 && idem;
        if (!idem)
            diag += QStringLiteral("[idem s=%1 ex=%2] ")
                        .arg(d1.stats().submitted)
                        .arg(w.executed.size());

        // 相②（ disjoint 行进取消流）：e2@(5,0) 与 e3@(10,0) 窗口逐个完全离开前窗半径 →
        //   前沿 pending 全量取消且泵后零执行：
        const auto D2 = twinDecide(par, 5, 0, tb);
        const auto D3 = twinDecide(par, 10, 0, tb);
        const bool anchor23 = D2.size() == 24 && D3.size() == 25; // 手工锚：24=25−(4,0)；25 全 Absent
        d1.onPlayerChunk(5, 0); // 无泵：D2 pending
        const bool e2Ok = anchor23 && d1.stats().submitted == D1.size() + D2.size()
            && d1.stats().canceled == 0 // D1 已执行：stale id 取消 false 不入账
            && d1.pendingJobCount() == D2.size();
        d1.onPlayerChunk(10, 0); // 无泵：D2 全离半径 → 活别名取消；D3 提交
        const bool e3Ok = d1.stats().submitted == D1.size() + D2.size() + D3.size()
            && d1.stats().canceled == D2.size() // 恰一次：D2 每活别名一次取消
            && d1.stats().rejected == 0;
        d1.pump();
        const auto D2keys = reqKeys(D2);
        bool d2NeverRan = true;
        for (const auto &e : w.executed)
            d2NeverRan = d2NeverRan && !D2keys.contains(ChunkKey{ e.key.cx, e.key.cz }.packed());
        int drained2 = 0;
        bool oc2Ok = true;
        while (d1.takeOutcome(oc)) {
            ++drained2;
            oc2Ok = oc2Ok && !isError(oc.error)
                && !D2keys.contains(ChunkKey{ oc.key.cx, oc.key.cz }.packed()); // 取消别名零 outcome
        }
        const bool ph2 = e2Ok && e3Ok && d2NeverRan && oc2Ok && drained2 == D3.size()
            && w.executed.size() == D1.size() + D3.size()
            && waveMatches(w.executed, D1.size(), D3) // D3 波 == decide 序（fresh wave 保序）
            && d1.pendingJobCount() == 0 && d1.stats().rejected == 0;
        ok = ok && ph2;
        if (!ph2)
            diag += QStringLiteral("[ph2 e2=%1 e3=%2 neverRan=%3 drained=%4 ex=%5] ")
                        .arg(e2Ok)
                        .arg(e3Ok)
                        .arg(d2NeverRan)
                        .arg(drained2)
                        .arg(execStr(w.executed));

        // 相③（跨沿合并别名 dedupe + 全别名死亡整体跳过）：独立表 tm（ Generated (0,1) +
        //   远端墙 [11..16]×[-3..3]，使 e3@(13,0) 新窗零请求 → 泵后 executed 恒空）：
        QMap<quint64, ChunkLifecycle> tm;
        tm.insert(kkey(0, 1), ChunkLifecycle::Generated);
        for (int cx = 11; cx <= 16; ++cx)
            for (int cz = -3; cz <= 3; ++cz)
                tm.insert(kkey(cx, cz), ChunkLifecycle::Generated);
        ChunkStreamDriver d2{ par };
        d2.setSeam(tableSeam(tm));
        d2.setWorker(&w); // 共享轨迹 worker：executed 已含相①②记录，本相从现长度起算零增量
        const int exBase = w.executed.size();
        const auto M1 = twinDecide(par, 0, 0, tm);
        const auto M2 = twinDecide(par, 1, 0, tm);
        d2.onPlayerChunk(0, 0);
        d2.onPlayerChunk(1, 0); // 无泵：M1∩M2 重叠 key 合并别名
        QSet<quint64> unionKeys = reqKeys(M1);
        unionKeys.unite(reqKeys(M2));
        const bool dedupe = M1.size() == 24 && M2.size() == 24 && unionKeys.size() == 29
            && d2.pendingJobCount() == unionKeys.size() // 零重复 pending（48 提交 → 29 job）
            && d2.stats().submitted == M1.size() + M2.size();
        d2.onPlayerChunk(13, 0); // 全部 M1/M2 key cheb≥11>2 → 全别名取消；新窗全 Generated 零提交
        const bool allDead = d2.stats().canceled == M1.size() + M2.size() // 每活别名恰一次
            && d2.stats().submitted == M1.size() + M2.size()
            && d2.pendingJobCount() == unionKeys.size(); // 死 job 泵前仍在账（r2011 语义）
        d2.pump();
        const bool skipped = w.executed.size() == exBase // 全别名死亡 → 工作完全不跑
            && d2.pendingJobCount() == 0 && d2.outcomeCount() == 0;
        ok = ok && dedupe && allDead && skipped;
        if (!dedupe || !allDead || !skipped)
            diag += QStringLiteral("[ph3 dedupe=%1 allDead=%2 skipped=%3 pj=%4 c=%5 ex=%6] ")
                        .arg(dedupe)
                        .arg(allDead)
                        .arg(skipped)
                        .arg(d2.pendingJobCount())
                        .arg(d2.stats().canceled)
                        .arg(w.executed.size() - exBase);

        // 相④（完成后重入半径重提交 = 新 requestId）：独立表 tr（ Generated (0,1)）：
        QMap<quint64, ChunkLifecycle> tr;
        tr.insert(kkey(0, 1), ChunkLifecycle::Generated);
        ChunkStreamDriver d3{ par };
        d3.setSeam(tableSeam(tr));
        d3.setWorker(&w);
        const auto R1 = twinDecide(par, 0, 0, tr);
        d3.onPlayerChunk(0, 0);
        d3.pump();
        QMap<quint64, quint32> firstId;
        for (const auto &e : w.executed)
            firstId.insert(ChunkKey{ e.key.cx, e.key.cz }.packed(), e.requestId);
        for (const auto &r : R1)
            tr.insert(kkey(r.cx, r.cz), ChunkLifecycle::Generated);
        tr.insert(kkey(0, 0), ChunkLifecycle::Absent); // 世界变化（P3 卸载类比）：(0,0) 复归 Absent
        const int exBase4 = w.executed.size();
        d3.onPlayerChunk(2, 0); // (0,0) 重入半径且 Absent → 重提交（新 job 新 id）
        d3.pump();
        const auto R2 = twinDecide(par, 2, 0, tr);
        quint32 newId = 0;
        for (int i = exBase4; i < w.executed.size(); ++i)
            if (w.executed[i].key.cx == 0 && w.executed[i].key.cz == 0)
                newId = w.executed[i].requestId;
        const bool reentry = R2.size() == 11 // 手工锚：25 − 14 Generated（含复归 (0,0)）
            && w.executed.size() - exBase4 == R2.size()
            && waveMatches(w.executed, exBase4, R2)
            && newId != 0 && newId > firstId.value(kkey(0, 0), 0) // 全新 requestId（单调分配）
            && d3.stats().canceled == 0; // 已执行 id 的取消 false 不入账
        ok = ok && reentry;
        if (!reentry)
            diag += QStringLiteral("[ph4 R2=%1 got=%2 newId=%3 firstId=%4] ")
                        .arg(R2.size())
                        .arg(w.executed.size() - exBase4)
                        .arg(newId)
                        .arg(firstId.value(kkey(0, 0), 0));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2018b follow-and-cancel face: submission sets match the"
                             " twin decide output item-for-item with priorities preserved,"
                             " same-chunk repeats are no-ops, out-of-radius pendings are"
                             " canceled exactly once and never execute (zero outcomes),"
                             " cross-edge duplicates merge into one pending job and an"
                             " all-aliases-dead job skips work entirely, and a completed-"
                             " then-re-entered key returns under a brand-new requestId"
                          << (ok ? QString() : diag);
    });

    // ── r2018c：风暴背压——满载拒绝可见、被拒零悬别名、排干后再沿重算收敛─────────────────
    runLeg(QStringLiteral("r2018c storm backpressure (section 29.4 P2 'generation storm"
        " backpressure = the existing queue-full rejection semantics'): with gen radius 4"
        " the fresh far-teleport decision asks 81 Absent chunks against the underlying"
        " 64-slot pending capacity - exactly 17 submissions are rejected (visible in"
        " rejectedSubmissions, equal to the refused count), the 64 accepted keys all"
        " execute and deliver outcomes while none of the 17 rejected keys ever produces a"
        " job or an outcome (no dangling aliases, no crash); after the pump drains and the"
        " world matures, a one-cell re-edge resubmits the still-Absent set (all 17"
        " previously refused keys included) and converges: the final submitted set equals"
        " the decided set with the rejection counter frozen"), [&]() {
        bool ok = true;
        QString diag;
        const GenerationPolicyParams par4(true, 4, 2, 6); // gen=4 → 9×9 = 81 窗 > 64 容量
        QMap<quint64, ChunkLifecycle> tc; // 全 Absent（空表默认）

        TraceSyncWorker w;
        ChunkStreamDriver d{ par4 };
        d.setSeam(tableSeam(tc));
        d.setWorker(&w);

        // 风暴沿：远距传送 → 81 决策 vs 64 容量 → 恰 17 拒绝：
        const auto DC1 = twinDecide(par4, 20, 20, tc);
        const bool anchor = DC1.size() == 81; // 手工锚：9×9 全 Absent
        d.onPlayerChunk(20, 20);
        const auto accepted = DC1.mid(0, 64); // 提交序 = decide 序（近者优先）→ 前 64 受理
        const auto refused = DC1.mid(64);
        const auto refusedKeys = reqKeys(refused);
        const bool storm = anchor && accepted.size() == 64 && refused.size() == 17
            && d.stats().submitted == 64 && d.rejectedSubmissions() == 17
            && d.stats().canceled == 0 && d.pendingJobCount() == 64;
        ok = ok && storm;
        if (!storm)
            diag += QStringLiteral("[storm anchor=%1 s=%2 r=%3 pj=%4] ")
                        .arg(anchor)
                        .arg(d.stats().submitted)
                        .arg(d.rejectedSubmissions())
                        .arg(d.pendingJobCount());

        // 排干：64 全执行全交付；被拒 17 零 job 零 outcome（零悬别名）：
        d.pump();
        const auto acceptedKeys = reqKeys(accepted);
        int drained = 0;
        bool ocOk = true;
        GenerationJobOutcome oc;
        while (d.takeOutcome(oc)) {
            ++drained;
            ocOk = ocOk && !isError(oc.error)
                && acceptedKeys.contains(ChunkKey{ oc.key.cx, oc.key.cz }.packed())
                && !refusedKeys.contains(ChunkKey{ oc.key.cx, oc.key.cz }.packed());
        }
        const bool drainOk = w.executed.size() == 64 && waveMatches(w.executed, 0, accepted)
            && drained == 64 && ocOk && d.pendingJobCount() == 0;
        ok = ok && drainOk;
        if (!drainOk)
            diag += QStringLiteral("[drain ex=%1 drained=%2 ocOk=%3] ")
                        .arg(w.executed.size())
                        .arg(drained)
                        .arg(ocOk);

        // 世界成熟（测试扮演边②）：受理 64 key 全部 Generated；一格重沿 → 重算收敛：
        for (const auto &r : accepted)
            tc.insert(kkey(r.cx, r.cz), ChunkLifecycle::Generated);
        const auto DC2 = twinDecide(par4, 21, 20, tc);
        d.onPlayerChunk(21, 20);
        d.pump();
        const auto refusedInD2 = reqKeys(DC2).intersect(refusedKeys);
        const bool converged = DC2.size() == 26 // 手工锚：17 被拒重现 + 9 新入窗格
            && d.stats().submitted == 64 + DC2.size() // 全受理（队列已空）
            && d.rejectedSubmissions() == 17 // 拒绝计数冻结（无新增）
            && w.executed.size() == 64 + DC2.size()
            && waveMatches(w.executed, 64, DC2) // 最终提交集 == 决策集（序恒等）
            && refusedInD2.size() == 17; // 原被拒 key 全量重发承接
        int totalOutcomes = 0;
        while (d.takeOutcome(oc))
            ++totalOutcomes;
        // 本波排干 = DC2（首波 64 已在 drainOk 相排干；累计交付 64+DC2 == 累计提交 90 = 零悬别名）：
        const bool noDangling = totalOutcomes == DC2.size() && d.outcomeCount() == 0
            && drained == 64 && d.stats().submitted == drained + totalOutcomes;
        ok = ok && converged && noDangling;
        if (!converged || !noDangling)
            diag += QStringLiteral("[conv DC2=%1 s=%2 r=%3 ex=%4 refusedIn=%5 out=%6] ")
                        .arg(DC2.size())
                        .arg(d.stats().submitted)
                        .arg(d.rejectedSubmissions())
                        .arg(w.executed.size())
                        .arg(refusedInD2.size())
                        .arg(totalOutcomes);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2018c storm backpressure: an 81-chunk far teleport against"
                             " the 64-slot queue rejects exactly 17 (visible counter), the"
                             " accepted 64 execute and deliver while refused keys produce"
                             " nothing (no dangling aliases), and after drain + mature the"
                             " re-edge converges with the final submitted set equal to the"
                             " decided set including every previously refused key"
                          << (ok ? QString() : diag);
    });

    // ── r2018d：异步收割 + 结构钉——ScriptedAsyncWorker 两相、kind 纯度、确定性重放、反探───
    runLeg(QStringLiteral("r2018d async harvest and structure pins (section 29.4 P2, Mesh"
        " kind-gate semantics untouched): a scripted asynchronous worker takes the"
        " driver's Generate requests at the pumpAsync handout phase (handed records"
        " visible: all Generate kind, keys and priorities equal to the decide output in"
        " decide order) and delivers scripted completions at the harvest phase (outcomes"
        " visible through the driver's harvest face, requestId/key FIFO-matched); source"
        " pins hold the header QObject-free with the double default-off short-circuit and"
        " a default Generate-only submission face (P3 revision: Load is legal only via"
        " the seam-gated saved-content query) while reverse probes prove no QObject base,"
        " QML surface, thread primitive, World/ChunkManager pointer or setLifecycle token"
        " (lifecycle edges stay GenerationJob's single authority, mount belongs to"
        " production wiring); the codebase stays unwired and the same movement sequence"
        " replayed through two fresh drivers yields bit-identical submit/cancel/execute"
        " trajectories"), [&]() {
        bool ok = true;
        QString diag;

        // ① 异步交接/收割两相（r2010bb 替身同门；零线程零时序）：
        const GenerationPolicyParams par5(true, 1, 1, 3); // gen=1 → 3×3 = 9 窗
        QMap<quint64, ChunkLifecycle> td;
        td.insert(kkey(0, 1), ChunkLifecycle::Generated);
        td.insert(kkey(1, 0), ChunkLifecycle::Generated);
        ScriptedAsyncWorker aw;
        ChunkStreamDriver d5{ par5 };
        d5.setSeam(tableSeam(td));
        d5.setWorker(&aw);
        const auto DD = twinDecide(par5, 0, 0, td);
        const bool anchor5 = DD.size() == 7; // 手工锚：9 − 2 Generated
        d5.onPlayerChunk(0, 0);
        d5.pump(); // 交接相（isAsynchronous worker → 底层自动路由 pumpAsync）
        bool handOk = anchor5 && aw.handed.size() == DD.size() && d5.pendingJobCount() == 0
            && d5.outcomeCount() == 0;
        for (int i = 0; i < aw.handed.size() && handOk; ++i) {
            handOk = handOk && aw.handed[i].kind == GenerationJobKind::Generate // kind 纯度
                && aw.handed[i].key.cx == DD[i].cx && aw.handed[i].key.cz == DD[i].cz
                && aw.handed[i].priority == DD[i].priority; // 逐项恒等 + decide 序
        }
        for (int i = 0; i < aw.handed.size(); ++i) // 脚本完成（全成功）：
            aw.script.append({ aw.handedJobIds[i], Error{} });
        d5.pump(); // 收割相
        int harvested = 0;
        bool harvestOk = handOk;
        GenerationJobOutcome oc;
        while (d5.takeOutcome(oc) && harvestOk) {
            harvestOk = harvested < aw.handed.size()
                && oc.requestId == aw.handed[harvested].requestId // FIFO 对账
                && oc.key.cx == aw.handed[harvested].key.cx
                && oc.key.cz == aw.handed[harvested].key.cz
                && oc.kind == GenerationJobKind::Generate && !isError(oc.error);
            ++harvested;
        }
        harvestOk = harvestOk && harvested == aw.handed.size() && d5.outcomeCount() == 0;
        ok = ok && harvestOk;
        if (!harvestOk)
            diag += QStringLiteral("[async anchor=%1 hand=%2 harvest=%3/%4] ")
                        .arg(anchor5)
                        .arg(aw.handed.size())
                        .arg(harvested)
                        .arg(DD.size());

        // ② 结构钉（正面 pinSet 剥注释 + minCount=1 反探 miss 非空=合规 + 零接线）：
        static_assert(QObjectFree<ChunkStreamDriver>, "r2018d: driver must not carry QObject");
        static_assert(QObjectFree<ChunkStreamDriver::Stats>, "r2018d: stats must not carry QObject");
        static_assert(std::is_trivially_copyable_v<ChunkStreamDriver::Stats>,
            "r2018d: stats stays trivially copyable");
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src")); // r2011d/r2015d/r2016d 同款解析（exe 相对 src/）
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString drvHdr = srcRoot + QStringLiteral("/World/chunkstreamdriver.h");
        const QStringList missDrv = pinSet(drvHdr, {
            SrcPin("r2018 driver class", "class ChunkStreamDriver", 1),
            SrcPin("r2018 position edge", "void onPlayerChunk(int cx, int cz)", 1),
            SrcPin("r2018 default-off double short-circuit",
                "if (!m_params.streamingEnabled", 2),
            SrcPin("r2018 generate-only submission face",
                "GenerationJobKind::Generate", 1),
            SrcPin("r2018 job submit call", "m_jobs.submit(", 1),
            SrcPin("r2018 value discipline pin",
                "static_assert(QObjectFree<ChunkStreamDriver>", 1),
        });
        for (const QString &m : missDrv) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool negProbeOk = forbiddenAbsent(drvHdr, ": public QObject")
            && forbiddenAbsent(drvHdr, "Q_OBJECT") && forbiddenAbsent(drvHdr, "Q_INVOKABLE")
            && forbiddenAbsent(drvHdr, "Q_PROPERTY") && forbiddenAbsent(drvHdr, "QThread")
            && forbiddenAbsent(drvHdr, "std::thread") && forbiddenAbsent(drvHdr, "QMutex")
            && forbiddenAbsent(drvHdr, "World *") && forbiddenAbsent(drvHdr, "ChunkManager *")
            && forbiddenAbsent(drvHdr, "setLifecycle") // 零边驱动：生命周期边权威 stays GenerationJob
            && forbiddenAbsent(drvHdr, "GenerationJobKind::Mesh"); // 提交面零 Mesh kind
        // r2019 同变更修订（纠偏非削钉——t1023c/t1050a 钉面随搬移修订先例）：§29.4-P3 给驱动器
        //   增设 saved-content Load 路径（D5 回灌，默认 null = 全 Generate），原「Load kind 禁出
        //   提交面」反探随被钉语义退役——改钉「Load 记号恰经缝门控出现 ≥1」（结构面承接归
        //   section22 r2019d：setEvictor/setSavedContentQuery 正面钉 + Mesh 仍禁出 + 零接线）。
        const QStringList missDrvLoad = pinSet(drvHdr, {
            SrcPin("r2019 load-kind seam-gated selection (revises the r2018 Load-forbidden"
                   " probe: Load is now legal via the saved-content seam only)",
                "GenerationJobKind::Load", 1),
        });
        for (const QString &m : missDrvLoad) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        // §29.5-W2 同变更修订（纠偏留痕非削钉——t1023c/r2019 Load-kind 修订先例）：原「全库
        //   零接线」反探含 gamesession.h；W2（refactor-plan §29.5.2 原文「GameSession tick 尾
        //   driver.onPlayerChunk」）授权 GameSession 为驱动器生产接线点，该文件从禁出名单退役、
        //   改钉「接线恰经会话通电门出现」（下方 missGsWire 正面钉）；world.{h,cpp} /
        //   playercontroller 双件 / Main.qml 禁出保持原样（接线面仍不出 World 与 QML）。
        const bool unwired = forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"),
                                 "ChunkStreamDriver")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"),
                "ChunkStreamDriver")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/playercontroller.h"),
                "ChunkStreamDriver")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/playercontroller.cpp"),
                "ChunkStreamDriver")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "ChunkStreamDriver");
        const QStringList missGsWire = pinSet(
            srcRoot + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("W2 authorized session wiring (revises the r2018 gamesession-forbidden"
                       " probe: the §29.5-W2 session gate is the sanctioned driver wiring point)",
                    "ChunkStreamDriver::streamingWithDefaultRadii()", 1),
            });
        ok = ok && negProbeOk && unwired && missGsWire.isEmpty();
        if (!negProbeOk) diag += QStringLiteral("[neg-probe] ");
        if (!unwired) diag += QStringLiteral("[unwired] ");
        for (const QString &m : missGsWire) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 确定性：同移动序列 × 双 fresh 驱动器（同参同表）重放 → 轨迹逐位恒等：
        const GenerationPolicyParams parR(true, 2, 1, 4);
        QMap<quint64, ChunkLifecycle> tA;
        tA.insert(kkey(0, 1), ChunkLifecycle::Generated);
        tA.insert(kkey(-1, -1), ChunkLifecycle::Generated);
        QMap<quint64, ChunkLifecycle> tB = tA; // 同内容独立表（缝按引用捕获，防悬垂）
        const int moves[5][3] = { // {cx, cz, pumpAfter}
            { 0, 0, 1 }, { 3, 0, 0 }, { 3, 4, 1 }, { -2, 4, 0 }, { 0, 1, 1 } };
        QVector<int> snapA, snapB;
        QVector<GenerationRequest> exA, exB;
        QVector<GenerationJobOutcome> outA, outB;
        for (int side = 0; side < 2; ++side) {
            TraceSyncWorker w;
            ChunkStreamDriver drv{ parR };
            drv.setSeam(tableSeam(side == 0 ? tA : tB));
            drv.setWorker(&w);
            QVector<int> &snap = (side == 0 ? snapA : snapB); // 逐沿计数快照（submitted/canceled/
                                                              // rejected/pending/executed）
            for (const auto &mv : moves) {
                drv.onPlayerChunk(mv[0], mv[1]);
                if (mv[2])
                    drv.pump();
                const auto st = drv.stats();
                snap.append(st.submitted);
                snap.append(st.canceled);
                snap.append(st.rejected);
                snap.append(drv.pendingJobCount());
                snap.append(w.executed.size());
            }
            GenerationJobOutcome o;
            while (drv.takeOutcome(o))
                (side == 0 ? outA : outB).append(o);
            (side == 0 ? exA : exB) = w.executed;
        }
        bool replay = snapA == snapB && exA.size() == exB.size() && outA.size() == outB.size()
            && outA.size() > 0;
        for (int i = 0; i < exA.size() && replay; ++i)
            replay = exA[i].requestId == exB[i].requestId && exA[i].key.cx == exB[i].key.cx
                && exA[i].key.cz == exB[i].key.cz && exA[i].priority == exB[i].priority
                && exA[i].kind == exB[i].kind;
        for (int i = 0; i < outA.size() && replay; ++i)
            replay = outA[i].requestId == outB[i].requestId
                && outA[i].key.cx == outB[i].key.cx && outA[i].key.cz == outB[i].key.cz
                && outA[i].error.code == outB[i].error.code;
        // 非空转：重放面确有提交、确有执行（R20.11 纪律：取消绝对计数归 r2018b 专钉——
        // 确定性腿的非空转探针只钉变异无关事实，摘取消的 NEG 变异两侧同变仍恒等不误伤）：
        bool nonVacuous = replay && exA.size() > 0;
        {
            TraceSyncWorker wprobe;
            ChunkStreamDriver dprobe{ parR };
            dprobe.setSeam(tableSeam(tA));
            dprobe.setWorker(&wprobe);
            for (const auto &mv : moves) {
                dprobe.onPlayerChunk(mv[0], mv[1]);
                if (mv[2])
                    dprobe.pump();
            }
            nonVacuous = nonVacuous && dprobe.stats().submitted > 0
                && wprobe.executed.size() > 0;
        }
        ok = ok && replay && nonVacuous;
        if (!replay || !nonVacuous)
            diag += QStringLiteral("[replay eq=%1 exA=%2 exB=%3 outA=%4 outB=%5 vac=%6] ")
                        .arg(replay)
                        .arg(exA.size())
                        .arg(exB.size())
                        .arg(outA.size())
                        .arg(outB.size())
                        .arg(nonVacuous);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2018d async harvest and structure pins: the scripted async"
                             " worker hands out Generate-kind requests matching the decide"
                             " output and delivers FIFO-matched completions through the"
                             " harvest face; source pins keep the header QObject-free with"
                             " double default-off short-circuits and a default"
                             " Generate-only face (Load only via the P3 seam-gated"
                             " saved-content query),"
                             " reverse probes prove no QObject/QML/thread/World/"
                             "ChunkManager/setLifecycle tokens and production wiring only at"
                             " the W2-authorized session gate (r2018 gamesession-forbidden"
                             " probe amended in the same change), and two fresh drivers replay"
                             " the same movement sequence with bit-identical trajectories"
                          << (ok ? QString() : diag);
    });
}
