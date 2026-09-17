#include "matrix_helpers.h"

#include "gamesession.h"  // t1055 被测：GameSession（lastDirtyChunks 值快照面——r2029b）
#include "generationjob.h" // t1055 被测：GenerationScheduler / GenerationWorker（detachWorker
                           //   守卫缝 + submitAsync fail-fast 默认——r2029a/r2029c）
#include "meshbuilder.h"  // t1056 被测：ChunkMeshSnapshot 列顶域常量 + captureChunkMeshSnapshot（r2030a）
#include "voxellight.h"   // t1056 被测：kMaxShadow（PCF 半格触达推导另一端——r2030a）
#include "worldfacade.h"  // t1056：captureChunkMeshSnapshot 的收窄查询入参（r2030a）
#include "terraingen.h"   // t1056：TerrainGen 纯函数对照（r2030b 结果零变化面）

// t1055 agent-review 残余清偿合集一 探针段（4 腿 r2029a-d；filter 词 "r2029"；矩阵 651→655）。
// 置尾先例沿用（接 section31，runAll 末执行）；r2029a/c 零世界（裸 scheduler + 脚本化替身
// worker，零线程——r2011/r2010bb/r2017b 替身先例），r2029b 自建 fresh 小世界 48×48×96 s82
// ×1（section11+ 先例），r2029d 自建 32×32×64 s2029 小世界 ×1（weather 面零地形依赖，取最
// 小尺寸控时长）——对 rig 世界 w 零接触。四修契约（fix 提交头注释立证，此处只列腿面）：
//   r2029a（agent-review-2026-09-16 #8）= GenerationScheduler::detachWorker 守卫：m_inFlight
//     按「已放弃」销账 + m_worker 摘指针 → 后续 pump 恒安全 no-op；重挂新 worker 账本仍一致
//     可用；析构序契约「scheduler 先于 worker 析构，或显式 detach」（生产路径以 gamesession
//     成员声明序结构性满足，本缝服务非成员序消费方）。
//   r2029b（#12）= lastDirtyChunks 返回 DirtyChunkSet 值拷贝（真快照，lastDelta 同门）：
//     信号栈内取拷贝、stepTick 返回（收口清账）后持拷贝读不受影响；后续 tick 新快照只携带
//     自己的编辑面，旧拷贝逐位不动。
//   r2029c（Info「GenerationWorker 基类默认实现自相矛盾」）= submitAsync 默认
//     fail(kErrAsyncNotImplemented=204)：漏覆写的 isAsynchronous worker 从「静默永久滞留
//     m_inFlight」变「失败 outcome 可见穿透 + job 就地销账」（kErrQueueFull 背压重试语义
//     原样保留）；覆写正确的替身与同步 worker（execute 纯虚面）零影响。
//   r2029d（Review 2026-09-15 #7）= weather 0 值回落诊断：恢复端 setWeatherRemainingSec 对
//     sec<=0 拒面打一条 qInfo（区分「精确续跑」与「0 值回落随机重抽」）——行为零变化
//    （计时照旧不动），纯日志可见性；qInfo 捕获 = 文件尾 static 消息槽 + qInstallMessageHandler
//     窗口（用后即还原）。
// 阴性两轮（matrix_r2029_neg{1,2}_{red,restore}.log 存证 + Edit 反向还原）：
//   NEG-1 摘 detachWorker 销账（m_inFlight.clear() 行变异移除）→ 恰红 {r2029a}（销账断言
//     面红：inFlight 僵尸账残留）；NEG-2 摘 fail 默认（submitAsync 默认体回 ok()）→ 恰红
//    {r2029c}（滞留面复现：零 outcome + inFlight 卡 1）。
// 确定性口径：r2029a/c 全纯值替身零 RNG 零时间源；r2029b/d 的 setWeatherState 随机重抽窗
//   只断「>0 且不被 0 值写破坏」，不断窗内具体值（运行期 RNG 与 parity 无交——section11 同门）。
//
// ── t1056 残余清偿合集二追加（2 腿 r2030a-b；filter 词 "r2030"；矩阵 655→657）──────────────
//   r2030a（agent-review-2026-09-16 #9）= 软影列顶域扩到 PCF 半格触达：kTopDim 21→22
//     （域 [-2,+19]）——前向探测 floor(origin+kChunk+kMaxShadow)+1 = origin+19 旧域差 1 列
//     （域外 -1 不遮挡 = 边缘影偏亮）；常量面 + 紧覆盖推导（运行时承载——编译期紧式会把
//     NEG-1 的 kTopDim 回退变编译错误，选型见 meshbuilder.cpp 静态断言处留痕）+ 真实快照
//     逐探测列对 World 真值（世界边 (0,0) 与内陆 (1,1)，域外 -1 防御分支保留柱）。
//   r2030b（#6）= generate 首循环以 TerrainColumnSummary.biome 回填 m_biomeCache（消群系
//     fBm 双算；结果零变化）：全列已填计数 + 判别探针 biomeMemoMissCount==0（biomeAt 懒算
//     计数——回填在位 → 后续 pass 全命中恒 0；NEG-2 摘回填 → 后续 pass 逐列首查各付一次
//     fBm 计数飙升。恰红面实证注记：「全列已填」对摘回填不敏感——NEG-2 首跑实测后续 pass
//     懒填同会把 memo 填满，判别力全在懒算计数）+ biome/height 采样 vs TerrainGen 纯函数
//     直连（零变化面）+ 命中稳定柱 + beginLoad 懒填兜底柱（清表后 0 → 随查 3 列恰 3 且
//     懒算恰 +3）。
//   阴性两轮：NEG-1 回退 kTopDim 22→21 → 恰红 {r2030a, r2013d}（a 的紧覆盖推导 18<19 红
//     + d 的同变更修订钉 "kTopDim = 22" miss——受影响源钉腿）；NEG-2 摘 memo 回填
//     （assign 回 clear + 摘循环内回填行）→ 恰红 {r2030b}（懒算计数飙升 = #6 双算本体）。
//   rig 零接触（两腿各自 fresh 世界）。

// r2029d 专用：qInfo 捕获槽（QtMessageHandler 是裸函数指针不可捕获——文件级 static 落账，
// 用后还原 handler；窗口内只过滤本诊断词，其余消息照旧走默认通道不落账）。
static QStringList s_r2029dDiag;
static void r2029dMessageSink(QtMsgType, const QMessageLogContext &, const QString &m)
{
    if (m.contains(QLatin1String("setWeatherRemainingSec")))
        s_r2029dDiag.append(m);
}

void MatrixRun::section32_residual_sweep()
{
    // ── r2029a：detachWorker 守卫缝——销账 + 摘指针 + 泵安全 no-op + 重挂一致可用 ──────────
    runLeg(QStringLiteral("r2029a scheduler detachWorker guard (agent-review-2026-09-16 #8):"
        " handing two jobs to an async-only scripted stand-in leaves both in the in-flight"
        " ledger with pending drained (the normal-path accounting column - hand-out"
        " bookkeeping is intact before any detach), detachWorker then writes the abandoned"
        " in-flight account off to zero and nulls the worker pointer, a later pump"
        " consults the detached seam zero times (no completion take, no further hand-out,"
        " no outcome) and stays a safe no-op, a post-detach submit remains pending"
        " untouched (zero worker contact - pending work was never the worker's), and"
        " re-attaching a fresh scripted worker completes the surviving job through the"
        " normal hand-out and harvest path with a clean success outcome - the account"
        " stays consistent and usable after the write-off, enforcing the destructor-order"
        " contract (scheduler dies before the worker, or detach explicitly) at the ledger"
        " level"), [&]() {
        bool ok = true;
        QString diag;

        // 替身族①：只受理、永不完成（= #8 描述的危险态本体：交接成功后完成记录永不到来）。
        class NeverDoneWorker : public GenerationWorker
        {
        public:
            QVector<GenerationRequest> handed;
            int takeCalls = 0;
            bool isAsynchronous() const override { return true; }
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                return Result<void>::fail(201, "stand-in is async-only");
            }
            Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
            {
                Q_UNUSED(jobId);
                handed.append(req);
                return Result<void>::ok();
            }
            bool takeCompletedAsync(CompletedGeneration &out) override
            {
                Q_UNUSED(out);
                ++takeCalls;
                return false;
            }
        };
        // 替身族②：受理 + 按脚本 FIFO 交付完成记录（r2010bb/r2017b 脚本化先例，零线程）。
        class ScriptedDoneWorker : public GenerationWorker
        {
        public:
            QVector<GenerationRequest> handed;
            QVector<quint64> done; // 待交付 jobId 队列（单调分配 → jobId 可精确预告）
            bool isAsynchronous() const override { return true; }
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                return Result<void>::fail(201, "stand-in is async-only");
            }
            Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
            {
                handed.append(req);
                Q_UNUSED(jobId);
                return Result<void>::ok();
            }
            bool takeCompletedAsync(CompletedGeneration &out) override
            {
                if (done.isEmpty())
                    return false;
                out = CompletedGeneration{ done.takeFirst(), Error{} };
                return true;
            }
        };

        // ① 正常交接柱（未 detach 的常态账面）：双 job 交接入在途账，pending 清空，零 outcome：
        NeverDoneWorker w1;
        GenerationScheduler s1;
        s1.setWorker(&w1);
        const auto ra = s1.submit(GenerationJobKind::Generate, ChunkKey{ 2, 3 });
        const auto rb = s1.submit(GenerationJobKind::Load, ChunkKey{ 4, 5 });
        s1.pump();
        const bool handOk = ra.isOk() && rb.isOk() && w1.handed.size() == 2
            && s1.inFlightJobCount() == 2 && s1.pendingJobCount() == 0
            && s1.outcomeCount() == 0;
        ok = ok && handOk;
        if (!handOk)
            diag += QStringLiteral("[hand r=%1/%2 n=%3 fl=%4 pj=%5 out=%6] ")
                        .arg(ra.isOk()).arg(rb.isOk()).arg(w1.handed.size())
                        .arg(s1.inFlightJobCount()).arg(s1.pendingJobCount())
                        .arg(s1.outcomeCount());

        // ② detach：在途账按「已放弃」销账归零 + 指针摘除（NEG-1 变异敏感面）：
        s1.detachWorker();
        const bool detachOk = s1.inFlightJobCount() == 0 && s1.worker() == nullptr;
        ok = ok && detachOk;
        if (!detachOk)
            diag += QStringLiteral("[detach fl=%1 w=%2] ")
                        .arg(s1.inFlightJobCount())
                        .arg(s1.worker() != nullptr);

        // ③ detach 后 pump 恒安全 no-op：死缝零咨询（不 take 不交接）、账面稳定、零 crash：
        const int takesBefore = w1.takeCalls;
        const int handedBefore = w1.handed.size();
        s1.pump();
        const bool noopOk = w1.takeCalls == takesBefore && w1.handed.size() == handedBefore
            && s1.outcomeCount() == 0 && s1.inFlightJobCount() == 0;
        ok = ok && noopOk;
        if (!noopOk)
            diag += QStringLiteral("[noop take=%1/%2 handed=%3/%4 out=%5 fl=%6] ")
                        .arg(w1.takeCalls).arg(takesBefore)
                        .arg(w1.handed.size()).arg(handedBefore)
                        .arg(s1.outcomeCount()).arg(s1.inFlightJobCount());

        // ④ detach 后仍可提交：pending 账保留（未交接 job 从未经 worker 之手），worker 零触：
        const auto rc = s1.submit(GenerationJobKind::Generate, ChunkKey{ 6, 7 });
        s1.pump();
        const bool pendOk = rc.isOk() && s1.pendingJobCount() == 1
            && w1.handed.size() == handedBefore && s1.outcomeCount() == 0
            && s1.inFlightJobCount() == 0;
        ok = ok && pendOk;
        if (!pendOk)
            diag += QStringLiteral("[pend r=%1 pj=%2 n=%3 out=%4 fl=%5] ")
                        .arg(rc.isOk()).arg(s1.pendingJobCount()).arg(w1.handed.size())
                        .arg(s1.outcomeCount()).arg(s1.inFlightJobCount());

        // ⑤ 重挂新 worker 复服：销账后账本仍一致可用——幸存 job 走正常交接+收割收成功 outcome
        //    （jobId 单调分配：ra=1 / rb=2 / rc=3，脚本预告交付 3）：
        ScriptedDoneWorker w2;
        w2.done.append(3); // rc 的 jobId（本 scheduler 第三次占用 job 位）
        s1.setWorker(&w2);
        s1.pump();
        GenerationJobOutcome o;
        const bool reattachOk = w2.handed.size() == 1 && w2.handed[0].key == ChunkKey{ 6, 7 }
            && s1.outcomeCount() == 1 && s1.takeOutcome(o) && o.requestId == rc.value()
            && !isError(o.error) && s1.pendingJobCount() == 0 && s1.inFlightJobCount() == 0;
        ok = ok && reattachOk;
        if (!reattachOk)
            diag += QStringLiteral("[re n=%1 out=%2 rid=%3/%4 err=%5 pj=%6 fl=%7] ")
                        .arg(w2.handed.size()).arg(s1.outcomeCount())
                        .arg(o.requestId).arg(rc.value()).arg(isError(o.error))
                        .arg(s1.pendingJobCount()).arg(s1.inFlightJobCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2029a scheduler detachWorker guard: normal hand-out keeps"
                             " both jobs in the in-flight ledger (accounting column),"
                             " detachWorker writes the abandoned account off to zero and"
                             " nulls the worker pointer, a later pump consults the detached"
                             " seam zero times and stays a safe no-op, a post-detach submit"
                             " stays pending untouched, and re-attaching a fresh scripted"
                             " worker completes the surviving job with a clean success"
                             " outcome (ledger consistent after the write-off)"
                          << (ok ? QString() : diag);
    });

    // ── r2029b：lastDirtyChunks 值快照语义——收口清账后持拷贝读不受影响 ────────────────────
    runLeg(QStringLiteral("r2029b lastDirtyChunks value-snapshot semantics (agent-review-"
        "2026-09-16 #12): the session hands the dirty-chunk set out BY VALUE - a copy taken"
        " inside the tickCompleted signal scope keeps holding tick 1's two-chunk edit set"
        " after stepTick returns (the post-close-out account is cleared and a fresh call"
        " reports an empty set - window semantics column), the next window's snapshot"
        " carries only its own third-chunk edit while the first copy stays bit-untouched"
        " (insertion order included), and the held copy stays readable across ticks - a"
        " true snapshot, the same door as lastDelta"), [&]() {
        bool ok = true;
        QString diag;

        // fresh 小世界（section11 同款四 setter；天气双钉——转换掷骰不进探针窗口）：
        World wB;
        wB.setWidth(48);
        wB.setDepth(48);
        wB.setHeight(96);
        wB.setSeed(82);
        wB.setWeatherState(0);
        wB.setWeatherRemainingSec(3600.0f);

        // 放置列选址：heightAt 顶 +1 恒空的第一列（section13 placeCol 同款；扫描窗锚定不同
        // chunk——结构性保证跨 chunk，不靠地形碰运气）：A∈cx1/cz1、B∈cx0/cz2、C∈cx2/cz0。
        const auto placeCol = [](World &w, int x0, int z0) -> QPair<int, int> {
            for (int z = z0; z < z0 + 8 && z < w.depth(); ++z)
                for (int x = x0; x < x0 + 8 && x < w.width(); ++x) {
                    const int h = w.heightAt(x, z);
                    if (h >= 0 && h + 1 < w.height()
                        && w.blockAt(x, h + 1, z) == quint8(BR::Air))
                        return { x, z };
                }
            return { -1, -1 };
        };
        const auto placeEdit = [&placeCol](World &w, int x0, int z0) -> QPair<int, int> {
            const QPair<int, int> p = placeCol(w, x0, z0);
            if (p.first < 0)
                return { -1, -1 };
            const int h = w.heightAt(p.first, p.second);
            // 直写权威 → blockPlaced 信号 → 会话 noteEdit 登记（编辑面进下一收口窗）。
            return w.setBlock(p.first, h + 1, p.second, quint8(BR::Stone))
                ? QPair<int, int>{ p.first, p.second }
                : QPair<int, int>{ -1, -1 };
        };

        // 快照捕获（信号栈内值拷贝；先声明后建会话 = 析构序安全）：
        DirtyChunkSet captured1;
        DirtyChunkSet captured2;
        int cap1Tick = -1;
        int cap2Tick = -1;
        GameSession gs(wB);
        QObject::connect(&gs, &GameSession::tickCompleted, &gs,
            [&](int t, const WorldDelta &) {
                if (t == 1) {
                    captured1 = gs.lastDirtyChunks(); // 值拷贝（被测面本体）
                    cap1Tick = t;
                } else if (t == 2) {
                    captured2 = gs.lastDirtyChunks();
                    cap2Tick = t;
                }
            });

        // 窗 1：跨两 chunk 的两条编辑 → 一 tick 收口：
        const QPair<int, int> pa = placeEdit(wB, 16, 16); // chunk (1,1) 域
        const QPair<int, int> pb = placeEdit(wB, 0, 32);  // chunk (0,2) 域
        const ChunkKey ka = ChunkKey::fromWorld(pa.first, pa.second, Chunk::kSize);
        const ChunkKey kb = ChunkKey::fromWorld(pb.first, pb.second, Chunk::kSize);
        const int stepped1 = gs.stepTick(0.1);
        const bool capOk = pa.first >= 0 && pb.first >= 0 && ka != kb && stepped1 == 1
            && cap1Tick == 1 && captured1.size() == 2
            && captured1.contains(ka) && captured1.contains(kb);
        ok = ok && capOk;
        if (!capOk)
            diag += QStringLiteral("[cap a=%1,%2 b=%3,%4 step=%5 t=%6 n=%7] ")
                        .arg(pa.first).arg(pa.second).arg(pb.first).arg(pb.second)
                        .arg(stepped1).arg(cap1Tick).arg(captured1.size());

        // stepTick 返回后（收口已清账）：活账空（窗口语义回归柱）而持拷贝仍满员——值语义本体：
        const bool afterOk = gs.lastDirtyChunks().size() == 0 && captured1.size() == 2
            && captured1.contains(ka) && captured1.contains(kb);
        ok = ok && afterOk;
        if (!afterOk)
            diag += QStringLiteral("[after live=%1 n=%2] ")
                        .arg(gs.lastDirtyChunks().size()).arg(captured1.size());

        // 窗 2：第三 chunk 编辑 → 新快照只携带本窗；旧拷贝逐位不动（含插入序）：
        const QPair<int, int> pc = placeEdit(wB, 32, 0); // chunk (2,0) 域
        const ChunkKey kc = ChunkKey::fromWorld(pc.first, pc.second, Chunk::kSize);
        const int stepped2 = gs.stepTick(0.1);
        bool tick2Ok = pc.first >= 0 && ka != kc && kb != kc && stepped2 == 1
            && cap2Tick == 2 && captured2.size() == 1 && captured2.contains(kc)
            && captured1.size() == 2 && captured1.contains(ka) && captured1.contains(kb)
            && !captured1.contains(kc);
        for (int i = 0; tick2Ok && i < captured1.size(); ++i)
            tick2Ok = tick2Ok && (captured1.at(i) == ka || captured1.at(i) == kb);
        ok = ok && tick2Ok;
        if (!tick2Ok)
            diag += QStringLiteral("[t2 c=%1,%2 step=%3 t=%4 n2=%5 n1=%6] ")
                        .arg(pc.first).arg(pc.second).arg(stepped2).arg(cap2Tick)
                        .arg(captured2.size()).arg(captured1.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2029b lastDirtyChunks value-snapshot semantics: a copy"
                             " taken inside the tickCompleted signal scope keeps tick 1's"
                             " two-chunk edit set after stepTick returns while the live"
                             " account reads empty (post-close-out), the next window's"
                             " snapshot carries only its own edit, and the first copy"
                             " stays bit-untouched across ticks (true snapshot, lastDelta"
                             " same door)"
                          << (ok ? QString() : diag);
    });

    // ── r2029c：submitAsync fail-fast 默认——漏覆写可见穿透不滞留 + 替身/同步零影响 ────────
    runLeg(QStringLiteral("r2029c GenerationWorker submitAsync fail-fast default (agent-"
        "review Info; kErrAsyncNotImplemented 204): an isAsynchronous worker that forgets"
        " to override submitAsync gets its job visibly failed through the outcome channel"
        " (one error outcome per live alias with code 204 penetrating verbatim), the job"
        " is written off in place (zero pending, zero in-flight, the chunk never leaves"
        " Absent because edge 1 is only taken after a successful hand-out) and a further"
        " pump produces no retry storm (the consumed job yields nothing), while a"
        " properly-overriding scripted async worker still completes normally and a"
        " synchronous execute-only worker is untouched (the pure-virtual face never walks"
        " the async protocol)"), [&]() {
        // 分域续位编译期钉：204 = 执行域下一位（201/202/203 先例同域递增）。
        static_assert(kErrAsyncNotImplemented == 204,
                      "t1055 C: the async-protocol-missing code must stay 204 (2xx domain"
                      " increment after 201/202/203)");
        bool ok = true;
        QString diag;

        // 替身①：漏覆写替身（只有 isAsynchronous/execute——submitAsync/takeCompletedAsync
        // 走基类默认 = 被测面本体）：
        class LeakyAsyncWorker : public GenerationWorker
        {
        public:
            bool isAsynchronous() const override { return true; }
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                return Result<void>::fail(201, "leaky stand-in is async-only");
            }
        };
        // 替身②：覆写正确替身（脚本化受理+交付——r2010bb 先例）：
        class ProperAsyncWorker : public GenerationWorker
        {
        public:
            int handed = 0;
            QVector<quint64> done;
            bool isAsynchronous() const override { return true; }
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                return Result<void>::fail(201, "proper stand-in is async-only");
            }
            Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
            {
                Q_UNUSED(req);
                Q_UNUSED(jobId);
                ++handed;
                return Result<void>::ok();
            }
            bool takeCompletedAsync(CompletedGeneration &out) override
            {
                if (done.isEmpty())
                    return false;
                out = CompletedGeneration{ done.takeFirst(), Error{} };
                return true;
            }
        };
        // 替身③：同步 worker（只覆写 execute 纯虚面——默认异步协议对它不可达）：
        class SyncWorker : public GenerationWorker
        {
        public:
            int executed = 0;
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                ++executed;
                return Result<void>::ok();
            }
        };

        // ① 漏覆写替身：交接相永久性失败 → 失败 outcome 可见穿透（204 原样）+ job 就地销账
        //    + 边①未取（挂 chunk 挂点的 Absent 格恒 Absent）+ 零重试风暴：
        ChunkManager mgr(48, 48, 32); // 裸 3×3（默认稳态全 Loaded——r2011d 同款）
        const bool route11 = mgr.setLifecycle(1, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(1, 1, ChunkLifecycle::Absent);
        GenerationScheduler s1(&mgr);
        LeakyAsyncWorker wk;
        s1.setWorker(&wk);
        const auto r1 = s1.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        s1.pump();
        GenerationJobOutcome o1;
        const bool leakOk = route11 && r1.isOk() && s1.outcomeCount() == 1
            && s1.takeOutcome(o1) && o1.requestId == r1.value() && isError(o1.error)
            && o1.error.code == kErrAsyncNotImplemented
            && o1.error.message != nullptr
            && s1.pendingJobCount() == 0 && s1.inFlightJobCount() == 0
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Absent;
        ok = ok && leakOk;
        if (!leakOk)
            diag += QStringLiteral("[leak r=%1 out=%2 code=%3 pj=%4 fl=%5 life=%6] ")
                        .arg(r1.isOk()).arg(s1.outcomeCount())
                        .arg(o1.error.code).arg(s1.pendingJobCount())
                        .arg(s1.inFlightJobCount()).arg(int(mgr.lifecycleAt(1, 1)));
        s1.pump(); // 重泵：job 已销账 → 零新 outcome（不滞留不重试风暴）
        const bool stormOk = s1.outcomeCount() == 0 && s1.pendingJobCount() == 0
            && s1.inFlightJobCount() == 0;
        ok = ok && stormOk;
        if (!stormOk)
            diag += QStringLiteral("[storm out=%1 pj=%2 fl=%3] ")
                        .arg(s1.outcomeCount()).arg(s1.pendingJobCount())
                        .arg(s1.inFlightJobCount());

        // ② 覆写正确替身零影响：同一调度面换脚本化替身 → 正常交接+收割+成功 outcome：
        GenerationScheduler s2;
        ProperAsyncWorker wp;
        s2.setWorker(&wp);
        const auto r2 = s2.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 });
        wp.done.append(1); // 本 scheduler 首个 job → jobId 1（单调分配可预告）
        s2.pump();
        GenerationJobOutcome o2;
        const bool properOk = r2.isOk() && wp.handed == 1 && s2.outcomeCount() == 1
            && s2.takeOutcome(o2) && o2.requestId == r2.value() && !isError(o2.error)
            && s2.inFlightJobCount() == 0 && s2.pendingJobCount() == 0;
        ok = ok && properOk;
        if (!properOk)
            diag += QStringLiteral("[proper r=%1 n=%2 out=%3 err=%4 fl=%5 pj=%6] ")
                        .arg(r2.isOk()).arg(wp.handed).arg(s2.outcomeCount())
                        .arg(isError(o2.error)).arg(s2.inFlightJobCount())
                        .arg(s2.pendingJobCount());

        // ③ 同步 worker 面零影响：execute 纯虚面照旧内联直跑（异步协议默认体不可达）：
        GenerationScheduler s3;
        SyncWorker ws;
        s3.setWorker(&ws);
        const auto r3 = s3.submit(GenerationJobKind::Generate, ChunkKey{ 3, 3 });
        s3.pump();
        GenerationJobOutcome o3;
        const bool syncOk = r3.isOk() && ws.executed == 1 && s3.outcomeCount() == 1
            && s3.takeOutcome(o3) && o3.requestId == r3.value() && !isError(o3.error)
            && s3.inFlightJobCount() == 0;
        ok = ok && syncOk;
        if (!syncOk)
            diag += QStringLiteral("[sync r=%1 ex=%2 out=%3 err=%4] ")
                        .arg(r3.isOk()).arg(ws.executed).arg(s3.outcomeCount())
                        .arg(isError(o3.error));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2029c submitAsync fail-fast default: a leaking async"
                             " worker's job is visibly failed through the outcome channel"
                             " (code 204 per live alias, verbatim Error penetration),"
                             " written off in place (zero pending / zero in-flight / chunk"
                             " stays Absent / no retry storm), while a properly-overriding"
                             " scripted async worker completes normally and a synchronous"
                             " execute-only worker is untouched"
                          << (ok ? QString() : diag);
    });

    // ── r2029d：weather 0 值回落 qInfo 诊断——日志可见性 + 行为零变化 ─────────────────────
    runLeg(QStringLiteral("r2029d weather zero-value fallback diagnostic (Review 2026-09-15"
        " #7): restoring with weatherTimerMs==0 now emits one qInfo naming the zero-value"
        " fallback while the timer keeps the setWeatherState random re-roll window"
        " (behavior bit-unchanged - the rejected call leaves the timer untouched), a"
        " negative value logs the same diagnostic, a precise resume (sec>0) stays silent"
        " and still writes the archived window exactly, and a second state re-roll plus a"
        " zero write keeps the freshly rolled window (fallback semantics unchanged on"
        " every path)"), [&]() {
        bool ok = true;
        QString diag;

        // 最小 fresh 世界（weather 面零地形依赖；32×32×64 控 worldgen 一次性成本）：
        World wD;
        wD.setWidth(32);
        wD.setDepth(32);
        wD.setHeight(64);
        wD.setSeed(2029);

        // 相①：态设 Rain（随机重抽窗 R0 > 0）→ 精确续跑（sec>0）静默受理 + 落值精确：
        wD.setWeatherState(1);
        const float r0 = wD.weatherRemainingSec();
        s_r2029dDiag.clear();
        QtMessageHandler prevHandler = qInstallMessageHandler(&r2029dMessageSink);
        wD.setWeatherRemainingSec(2.5f); // 精确续跑：不打卡（零噪声）+ 计时精确写入
        const bool preciseOk = s_r2029dDiag.isEmpty()
            && wD.weatherRemainingSec() > 2.4f && wD.weatherRemainingSec() < 2.6f;

        // 相②：0 值回落（存档带键但值 0 的恢复形态）→ 恰一条 qInfo + 计时不动（行为零变化）：
        wD.setWeatherRemainingSec(0.0f);
        const float afterZero = wD.weatherRemainingSec();
        wD.setWeatherRemainingSec(-1.0f); // 负值同拒面
        const float afterNeg = wD.weatherRemainingSec();
        const bool zeroOk = s_r2029dDiag.size() == 2
            && afterZero > 2.4f && afterZero < 2.6f
            && afterNeg > 2.4f && afterNeg < 2.6f;
        bool msgOk = zeroOk;
        for (const QString &m : s_r2029dDiag)
            msgOk = msgOk && m.contains(QLatin1String("zero-value fallback"));
        ok = ok && preciseOk && zeroOk && msgOk;
        if (!(preciseOk && zeroOk && msgOk))
            diag += QStringLiteral("[d precise=%1 n=%2 t0=%3 t-=%4 msg=%5] ")
                        .arg(preciseOk).arg(s_r2029dDiag.size())
                        .arg(afterZero).arg(afterNeg).arg(msgOk);

        // 相③：重抽窗保持柱——重设态得新随机窗 R1，0 值写后计时仍 = R1（回落语义不变）：
        wD.setWeatherState(2); // Snow → 该态随机窗重抽（R1 > 0）
        const float r1 = wD.weatherRemainingSec();
        wD.setWeatherRemainingSec(0.0f);
        const float afterZero2 = wD.weatherRemainingSec();
        qInstallMessageHandler(prevHandler); // 捕获窗收口（用后即还原）
        const bool keepOk = r0 > 0.0f && r1 > 0.0f && s_r2029dDiag.size() == 3
            && afterZero2 > 0.0f
            && (afterZero2 - r1) < 0.001f && (r1 - afterZero2) < 0.001f;
        ok = ok && keepOk;
        if (!keepOk)
            diag += QStringLiteral("[k r0=%1 r1=%2 t=%3 n=%4] ")
                        .arg(r0).arg(r1).arg(afterZero2).arg(s_r2029dDiag.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2029d weather zero-value fallback diagnostic: a zero"
                             " (or negative) restore write emits exactly one qInfo naming"
                             " the zero-value fallback while the timer stays bit-untouched"
                             " (random re-roll window kept - behavior unchanged), a precise"
                             " resume stays silent and lands the archived window exactly,"
                             " and a freshly re-rolled window survives a zero write the"
                             " same way (capture window installed and restored around the"
                             " probe only)"
                          << (ok ? QString() : diag);
    });

    // ── r2030a：软影列顶域扩到 PCF 半格触达（agent-review-2026-09-16 #9）────────────────────
    runLeg(QStringLiteral("r2030a shadow column-top domain widened to cover the PCF half-cell"
        " probe (agent-review-2026-09-16 #9): the snapshot constants pin kTopDim == 22 with"
        " kTopLo == -2 and the tight coverage derivation rides on them at runtime - the"
        " backward probe lands exactly on -kMaxShadow (floor never dips below the exact"
        " landing) while the forward probe lands on floor(origin + kChunk + kMaxShadow) + 1"
        " = origin + kChunk + kMaxShadow + 1 (the PCF half-cell column), which the domain's"
        " last index kTopLo + kTopDim - 1 now covers exactly at 19 == 19 - the old 21-wide"
        " domain [-2,+18] missed that one column and answered -1 there (falsely unoccluded"
        " edge shadows, the reviewed lesion); on real captured snapshots every extreme PCF"
        " probe column answers the World truth (columnTopAt == columnTopSurfaceY, and never"
        " the defensive -1 on in-world columns) across a world-edge chunk (0,0) and an"
        " interior chunk (1,1), and columns far beyond the domain still return the defensive"
        " -1 (the OOB branch survives as pure defense)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 常量面 + 紧覆盖推导（NEG-1 恰红面本体：kTopDim 回退 21 → last 18 < 前向探测 19 红；
        //    推导以运行时常量承载——编译期紧式会让 NEG-1 直接变编译错误，见 meshbuilder.cpp
        //    静态断言处的选型留痕）：
        const bool domOk = ChunkMeshSnapshot::kTopDim == 22 && ChunkMeshSnapshot::kTopLo == -2;
        constexpr int kFwdProbe = ChunkMeshSnapshot::kChunk + VoxelLight::kMaxShadow + 1; // = 19
        constexpr int kLastIdx = ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim - 1;
        const bool tightOk = kLastIdx == kFwdProbe
            && ChunkMeshSnapshot::kTopLo <= -VoxelLight::kMaxShadow;
        ok = ok && domOk && tightOk;
        if (!(domOk && tightOk))
            diag += QStringLiteral("[dom dim=%1 lo=%2 last=%3 fwd=%4] ")
                        .arg(ChunkMeshSnapshot::kTopDim).arg(ChunkMeshSnapshot::kTopLo)
                        .arg(kLastIdx).arg(kFwdProbe);

        // ② 真实快照行为面：fresh 48×48×96 s82（天气双钉——section11 同门）→ 世界边 chunk
        //    (0,0) 与内陆 chunk (1,1) 各采一快照，对 PCF 极端探测列（域两端 ±19/-2 与中带）
        //    断言 snapshot 应答 == World 真值；世界内列恒 ≥ 0（+19 列即被测病灶本体：
        //    origin+16 右缘顶点 + kMaxShadow 步进 + floor 后 +1 半格 = origin+19）。
        World wA;
        wA.setWidth(48);
        wA.setDepth(48);
        wA.setHeight(96);
        wA.setSeed(82);
        wA.setWeatherState(0);
        wA.setWeatherRemainingSec(3600.0f);
        WorldFacade fA(wA);
        const ChunkMeshBakeParams bake; // 默认阴影开（列顶域唯此消费路径）
        const int chunksA[2][2] = { { 0, 0 }, { 1, 1 } };
        const int probesA[6][2] = { { -2, -2 }, { -2, 19 }, { 19, -2 },
                                    { 19, 19 }, { 0, 8 },  { 16, 16 } };
        bool behOk = true;
        QString behDiag;
        for (const auto &cc : chunksA) {
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(fA, cc[0], cc[1], bake);
            if (!snap.valid()) {
                behOk = false;
                behDiag += QStringLiteral("[invalid c=%1,%2] ").arg(cc[0]).arg(cc[1]);
                continue;
            }
            for (const auto &p : probesA) {
                const int wx = snap.originX + p[0], wz = snap.originZ + p[1];
                const float got = snap.columnTopAt(wx, wz);
                const float want = wA.columnTopSurfaceY(wx, wz);
                const bool inWorld = wx >= 0 && wz >= 0 && wx < wA.width() && wz < wA.depth();
                if (got != want || (inWorld && got < 0.0f)) {
                    behOk = false;
                    behDiag += QStringLiteral("[c=%1,%2 d=%3,%4 got=%5 want=%6] ")
                                   .arg(cc[0]).arg(cc[1]).arg(p[0]).arg(p[1]).arg(got).arg(want);
                }
            }
        }
        ok = ok && behOk;
        diag += behDiag;

        // ③ 防御分支保留柱：域外远点恒 -1（columnTopAt 域外语义在位——修一是扩域不是删防御）。
        const ChunkMeshSnapshot snap00 = captureChunkMeshSnapshot(fA, 0, 0, bake);
        const bool defOk = snap00.valid()
            && snap00.columnTopAt(snap00.originX - 100, snap00.originZ - 100) == -1.0f;
        ok = ok && defOk;
        if (!defOk)
            diag += QStringLiteral("[def] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2030a shadow column-top domain widened to cover the PCF"
                             " half-cell probe: constants pin kTopDim 22 / kTopLo -2 with the"
                             " tight derivation at runtime (backward probe = -kMaxShadow"
                             " exact, forward probe = kChunk + kMaxShadow + 1 after floor's"
                             " +1 half-cell, covered exactly by the domain's last index - the"
                             " old 21-wide domain missed that column), captured snapshots"
                             " answer the World truth on every extreme probe column for both"
                             " the world-edge and an interior chunk (never the defensive -1"
                             " in-world), and far-out-of-domain columns still answer the"
                             " defensive -1"
                          << (ok ? QString() : diag);
    });

    // ── r2030b：generate 首循环回填群系 memo（agent-review-2026-09-16 #6，结果零变化）────────
    runLeg(QStringLiteral("r2030b biome memo refilled on the generate first pass (agent-"
        "review-2026-09-16 #6, zero-result-change): after a fresh fixed world's generate the"
        " column memo is fully populated (filled-sentinel count == width*depth) AND the"
        " discriminating lazy-compute counter reads ZERO - the first-loop backfill makes"
        " every later-pass biomeAt a cache hit, whereas removing the backfill (the exact-red"
        " face) makes the later passes pay one fBm per column and the counter spikes (the"
        " filled count alone is NOT discriminating: the later passes' lazy fills would"
        " populate the memo anyway - proven in the NEG-2 dry run); biome and height sampling"
        " still match the TerrainGen pure authority column-for-column on a probe grid (the"
        " backfilled values ARE the first-pass computations - results bit-unchanged), further"
        " sampling keeps both counters stable (full memo, zero misses - cache hits never"
        " churn), and the lazy-fill fallback still works on a beginLoad world (memo 0 after"
        " the reset, then exactly the three distinct columns queried show up as filled with"
        " exactly three lazy computes)"), [&]() {
        bool ok = true;
        QString diag;

        // fresh fixed 世界（构造即 generate——首循环回填被测面本体；天气双钉防 RNG 进窗）：
        World wF;
        wF.setWidth(48);
        wF.setDepth(48);
        wF.setHeight(96);
        wF.setSeed(82);
        wF.setWeatherState(0);
        wF.setWeatherRemainingSec(3600.0f);

        // ① 全列已填 + 判别计数归零（NEG-2 恰红面本体：摘回填 → 后续 pass 懒填仍会把 memo
        //    填满（filled 不断红）但逐列首查的懒算计数飙升 → miss 断言红）：
        const int filled = wF.biomeMemoFilledCount();
        const int misses = wF.biomeMemoMissCount();
        const bool fillOk = filled == 48 * 48 && misses == 0;
        ok = ok && fillOk;
        if (!fillOk)
            diag += QStringLiteral("[fill n=%1/%2 miss=%3] ").arg(filled).arg(48 * 48).arg(misses);

        // ② 结果零变化面：biome/height 采样栅格 == TerrainGen 纯函数直连（同 seed 同 dims；
        //    逐位一致 = 回填是缓存不是改写）：
        const TerrainGen::Dims dimsF{ 48, 48, 96 };
        TerrainGen genF(82, dimsF);
        bool pureOk = true;
        QString pureDiag;
        const int pxF[6] = { 0, 7, 16, 24, 33, 47 };
        const int pzF[6] = { 0, 5, 21, 40, 44, 47 };
        for (int ix = 0; ix < 6; ++ix) {
            for (int iz = 0; iz < 6; ++iz) {
                const int x = pxF[ix], z = pzF[iz];
                const bool eq = wF.biomeIdAt(x, z) == int(genF.biomeComputeAt(x, z))
                    && wF.heightAt(x, z) == genF.heightAt(x, z);
                if (!eq) {
                    pureOk = false;
                    pureDiag += QStringLiteral("[%1,%2] ").arg(x).arg(z);
                }
            }
        }
        ok = ok && pureOk;
        diag += pureDiag;

        // ③ 稳定柱：②的采样全走命中后再数一遍——memo 仍满员、懒算计数仍 0（命中不重填）：
        const bool stableOk = wF.biomeMemoFilledCount() == 48 * 48
            && wF.biomeMemoMissCount() == 0;
        ok = ok && stableOk;
        if (!stableOk)
            diag += QStringLiteral("[stable n=%1 miss=%2] ")
                        .arg(wF.biomeMemoFilledCount()).arg(wF.biomeMemoMissCount());

        // ④ 懒填兜底柱（beginLoad 世界——memo 清表语义原样、懒填路径未被本修破坏）：
        //    清表后计数 0；随查 3 个不同列 → 计数恰 3 且懒算恰 +3。
        wF.beginLoad(82);
        const int afterLoad = wF.biomeMemoFilledCount();
        const int missAtLoad = wF.biomeMemoMissCount();
        wF.biomeIdAt(3, 4);
        wF.biomeIdAt(20, 30);
        wF.biomeIdAt(47, 47);
        const int afterLazy = wF.biomeMemoFilledCount();
        const int missAfter = wF.biomeMemoMissCount();
        const bool lazyOk = afterLoad == 0 && afterLazy == 3
            && (missAfter - missAtLoad) == 3;
        ok = ok && lazyOk;
        if (!lazyOk)
            diag += QStringLiteral("[lazy load=%1 lazy=%2 miss %3->%4] ")
                        .arg(afterLoad).arg(afterLazy).arg(missAtLoad).arg(missAfter);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2030b biome memo refilled on the generate first pass: the"
                             " memo is fully populated after generate AND the lazy-compute"
                             " counter reads zero (the discriminating face - later passes all"
                             " hit the first-loop backfill), biome and height sampling still"
                             " match the TerrainGen pure authority on a probe grid (results"
                             " bit-unchanged), both counters stay stable across cache-hit"
                             " sampling, and the lazy-fill fallback still works on a"
                             " beginLoad world (0 after reset, three filled with exactly"
                             " three lazy computes)"
                          << (ok ? QString() : diag);
    });
}
