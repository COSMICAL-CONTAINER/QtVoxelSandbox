#include "matrix_helpers.h"

#include <QElapsedTimer>

#include <memory>
#include <thread>
#include <vector>

#include "backgroundgeneration.h" // R20.12 被测：后台 GenerationJob（真线程 worker + 自持缓冲）
#include "terraingen.h"           // R20.12 被测：纯地形生成单一权威（同步对照面）

// R20.12 后台 GenerationJob 探针段（4 腿 r2012a-d；filter 词 "r2012"；矩阵 578→582，band
// 580±2 内）。置尾先例沿用（接 section15，runAll 末执行）。r2012a-c 零世界（裸 scheduler +
// 真线程后台 worker——worker 不持 QObject，测试 rig 纯值面）；r2012d 裸 3×3 ChunkManager +
// fresh 小世界 48×48×96 seed 82 ×1（section11+ 先例；本段不 tick，无 RNG 进窗），rig 世界 w
// 零接触。任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.12 原文四验收）：
//   「GUI 不被整 Chunk 生成阻塞」→ r2012a（submit 立即返回 = 提交断言先于任何 pump 的**代码
//     事件序**；随后有界轮询收满 9 chunk outcome——deadline 取 worker 工作量的充分倍数（10s
//     ≈ 实测毫秒级负载的 3 个数量级余量），超时 = 红；主线程泵轮计数 ≥1 = 等待期事件计数对账
//     （不比对墙钟）；线程身份三重对账：执行线程 id == 工作线程 id && != 调用者线程——确定性
//     断言不依赖时序巧合）；
//   「seed、ChunkKey、generator version 决定输出」→ r2012b（承重墙：同 (seed,key,version)
//     双 worker 实例双跑逐位一致 + 后台输出与**同步共享函数**（主线程直调 fillTerrainColumn，
//     手写 capture sink）逐位一致——传输层（线程+队列+move）不改变字节 + 真世界锚：fresh 世界
//     48×48×96 s82 的 256 列逐列 envelope==min(heightAt,H-1) 且表层 id 逐群系一致（World 查询
//     面已委托 TerrainGen 单一权威——迁移后同步路径 = 同一份函数）；对照 seed 83 输出必不同 =
//     恒等断言非空转）；
//   「world epoch 可以丢弃旧任务」→ r2012c（后台版两级丢弃：交接后才 bump epoch 的在途结果
//     在收割时被静默丢弃——零投递（outcome 只有新 epoch 一条）而执行账面完整（worker 不识
//     epoch——丢弃语义在调度侧 deliver，与 R20.11 同门）；取消在交接前 = 永不执行永不投递）；
//   「退出时不会被任务卡死」→ r2012d（三个立即析构场景：在途+待队满载 / 仅待队零执行 / 空闲
//     ——腿体走完即证 join 有界返回；worker 停止时队内未开工任务整体丢弃 = 头注退出语义）+
//     与 R20.10 生命周期联动（Absent chunk 经合法边⑥⑦布点 → 交接边①（pump 后 != Absent——
//     确定性：交接无条件先于收割）→ 收割边②（终态 Generated——守卫证明两边皆合法经历：
//     Absent 上边②必被拒，终态 Generated ⟹ 边①先于边②发生）+ 取消不推进（恒 Absent，无
//     非法回退）+ 结果经守卫入口写入（applyGeneratedChunkData→ChunkManager::setBlock 落格
//     逐位比对）+ 固定世界零变化（默认稳态 Loaded chunk submit+泵到收干：表快照逐位不动）。
// 钉纪律（R20.11 恰红面设计纪律延续）：辅助断言只钉相对恒等（A≡B / 方向对照）；绝对计数只
//   归被测语义腿（r2012c 的 outcome 计数=1 是 epoch 丢弃语义本体）；pinSet 0 计数恒不红——
//   禁出钉走 forbiddenAbsent 反探惯用法（miss 非空 = 合规缺席）。
// 阴性轮登记（matrix_r2012_neg.log 存证 + Edit 反向还原）：①摘「过期结果不投递」——deliver
//   的 `req.epoch != m_worldEpoch` 过滤子句前置 `false &&`（调度侧丢弃语义本体精确摘除；同步
//   腿不受扰——r2011b 的过期断言钉在**选活扫描**（selectNextLiveJob 的同形子句不动），同步
//   pump 执行+投递同调用内 epoch 不可能中途变化）→ r2012c 恰红（outcome 计数 1→2）；②摘
//   「结果经守卫入口写入」——applyGeneratedChunkData 的空气跳过守卫改恒真（`if (id == 0)` →
//   `if (true)`）= 全部跳写 → r2012d 恰红（落格逐位比对全零 vs 缓冲非零）→ 各 Edit 反向还原
//   → filter r2012 回绿。
// 确定性 / 防 flake 口径（本单测试第一设计约束）：全部断言要么是确定性终态（outcome/数据/
//   账本/生命周期终态），要么是有界轮询的 deadline 面（超时才红）；**禁裸 sleep 断言**——
//   msleep 只作轮询节流；工作负载 = 真实 hashVoxel 路径的小世界 chunk（48×48×96）确定性生成；
//   线程身份用 id 对账不用时序。
void MatrixRun::section16_backgroundgen()
{
    constexpr int kW = 48, kD = 48, kH = 96;
    const TerrainGen::Dims kDims{ kW, kD, kH };
    constexpr int kDeadlineMs = 10000; // worker 工作量（毫秒级）的充分倍数——超时 = 红
    const auto pk = [](int cx, int cz) { return ChunkKey{ cx, cz }.packed(); };

    // 有界轮询帮手：泵到收满 want 条 outcome（或超时）。返回主线程 pump 轮数（事件计数）。
    const auto pumpUntilOutcomes = [](GenerationScheduler &s, int want,
                                      QVector<GenerationJobOutcome> &outs) {
        QElapsedTimer tm;
        tm.start();
        outs.clear();
        int rounds = 0;
        while (tm.elapsed() < kDeadlineMs) {
            s.pump();
            ++rounds;
            GenerationJobOutcome o;
            while (s.takeOutcome(o))
                outs.append(o);
            if (outs.size() >= want)
                break;
            QThread::msleep(2); // 轮询节流（非断言面）
        }
        return rounds;
    };

    // ── r2012a：非阻塞 + 真线程执行（验收①）─────────────────────────────────────────────
    runLeg(QStringLiteral("r2012a non-blocking background generation (R20.12 acceptance 'GUI is"
        " not blocked by whole-chunk generation'): nine Generate submits for a 3x3 chunk grid"
        " return immediately with visible acceptance accounts (9 pending, 0 outcomes) BEFORE"
        " any pump runs - the code order IS the event order - then bounded polling collects"
        " all nine outcomes within a deadline that is orders of magnitude above the actual"
        " worker workload (timeout = red), in exact submission order; the main thread kept"
        " pumping other work while the worker generated (pump-round event count >= 1, no"
        " wall-clock comparison); thread identity is reconciled by id: the executing thread"
        " equals the worker thread and differs from the caller thread, and nine self-owned"
        " data buffers arrive in completion order with matching keys, seed and generator"
        " version stamps"), [&]() {
        bool ok = true;
        QString diag;

        const std::thread::id mainTid = std::this_thread::get_id();
        BackgroundGenerationWorker worker(82, kDims);
        GenerationScheduler sched;
        sched.setWorker(&worker);

        // ① 提交面：9 chunk submit 立即返回（先于任何 pump——代码序 = 事件序）：
        QVector<quint32> ids;
        QVector<quint64> wantKeys;
        bool subOk = true;
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx) {
                const auto r = sched.submit(GenerationJobKind::Generate, ChunkKey{ cx, cz });
                subOk = subOk && r.isOk();
                ids.append(r.value());
                wantKeys.append(pk(cx, cz));
            }
        subOk = subOk && int(ids.size()) == 9 && sched.pendingJobCount() == 9
            && sched.pendingRequestCount() == 9 && sched.inFlightJobCount() == 0
            && sched.outcomeCount() == 0; // 提交即返回的账面证据（未泵 → 零 outcome 零在途）
        ok = ok && subOk;
        if (!subOk)
            diag += QStringLiteral("[submit n=%1 pj=%2 pr=%3 inf=%4 out=%5] ")
                        .arg(ids.size())
                        .arg(sched.pendingJobCount())
                        .arg(sched.pendingRequestCount())
                        .arg(sched.inFlightJobCount())
                        .arg(sched.outcomeCount());

        // ② 有界轮询：deadline 内收满 9 outcome（超时 = 红）；主线程泵轮计数 = 等待期事件对账：
        QVector<GenerationJobOutcome> outs;
        const int rounds = pumpUntilOutcomes(sched, 9, outs);
        bool drainOk = outs.size() == 9 && rounds >= 1;
        for (int i = 0; drainOk && i < 9; ++i) {
            drainOk = drainOk && !isError(outs[i].error) && outs[i].requestId == ids[i]
                && outs[i].key.packed() == wantKeys[i]; // FIFO = 提交序（交接序/执行序/收割序同源）
        }
        drainOk = drainOk && sched.pendingJobCount() == 0 && sched.inFlightJobCount() == 0
            && sched.pendingRequestCount() == 0; // 双账收干
        ok = ok && drainOk;
        if (!drainOk)
            diag += QStringLiteral("[drain n=%1 rounds=%2 pj=%3 inf=%4] ")
                        .arg(outs.size()).arg(rounds)
                        .arg(sched.pendingJobCount()).arg(sched.inFlightJobCount());

        // ③ 线程身份（id 对账——确定性，不靠时序）：执行线程 == 工作线程 && != 调用者线程：
        const bool tidOk = worker.workerThreadId() != mainTid
            && worker.observedExecThreadId() == worker.workerThreadId()
            && worker.observedExecThreadId() != mainTid && worker.executedCount() == 9;
        ok = ok && tidOk;
        if (!tidOk)
            diag += QStringLiteral("[tid main==exec=%1 exec==worker=%2 executed=%3] ")
                        .arg(mainTid == worker.observedExecThreadId())
                        .arg(worker.observedExecThreadId() == worker.workerThreadId())
                        .arg(worker.executedCount());

        // ④ 数据面：9 自持缓冲按完成序交付（与 outcome 同源同序），三要素戳齐全：
        bool dataOk = worker.resultDataCount() == 9;
        for (int i = 0; dataOk && i < 9; ++i) {
            std::unique_ptr<GeneratedChunkData> d;
            dataOk = worker.takeResultData(d) && bool(d) && d->valid()
                && d->key.packed() == wantKeys[i] && d->seed == 82u
                && d->generatorVersion == TerrainGen::kGeneratorVersion
                && d->originX == (i % 3) * TerrainGen::kChunkSize
                && d->originZ == (i / 3) * TerrainGen::kChunkSize && d->height == kH
                && d->contentHash() != 0;
        }
        dataOk = dataOk && worker.resultDataCount() == 0;
        ok = ok && dataOk;
        if (!dataOk)
            diag += QStringLiteral("[data left=%1] ").arg(worker.resultDataCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2012a non-blocking background generation: nine submits return"
                             " immediately (acceptance accounts visible pre-pump), bounded"
                             " polling collects all outcomes in submission order within a"
                             " generous deadline, the caller thread pumped other work while"
                             " the worker thread generated (id-reconciled identity), and nine"
                             " self-owned buffers arrive with matching keys/seed/version"
                          << (ok ? QString() : diag);
    });

    // ── r2012b：纯函数性——后台 ≡ 双跑 ≡ 同步共享函数 ≡ 真世界锚（验收②承重墙）─────────────
    runLeg(QStringLiteral("r2012b purity bitwise identity (R20.12 acceptance 'seed, ChunkKey and"
        " generator version decide the output' - the load-bearing wall): two independent worker"
        " instances running the same (seed 82, chunk, version) produce bitwise-identical"
        " buffers (meta + both arrays), the background output is bitwise-identical to a"
        " MAIN-THREAD synchronous drive of the SAME shared column-fill function (hand-written"
        " capture sink over one TerrainGen - only the transport differs, never the logic),"
        " every one of the 256 columns of the middle chunk is anchored against a fresh real"
        " world (envelope == min(heightAt, H-1); surface id follows biome: Grass inland,"
        " Sand desert, SnowLayer snowy - the World query face delegates to the same single"
        " authority after the migration), and a different seed yields a different buffer"
        " (identity assertions are not vacuous)"), [&]() {
        bool ok = true;
        QString diag;

        // 单块后台运行帮手：独立 worker 实例 + 有界轮询 → 自持缓冲。
        const auto runBackgroundChunk = [](int seed, ChunkKey key) {
            BackgroundGenerationWorker wk(seed, TerrainGen::Dims{ kW, kD, kH });
            GenerationScheduler s;
            s.setWorker(&wk);
            const auto r = s.submit(GenerationJobKind::Generate, key);
            QElapsedTimer tm;
            tm.start();
            GenerationJobOutcome o;
            bool got = false;
            while (tm.elapsed() < kDeadlineMs && !got) {
                s.pump();
                got = s.takeOutcome(o);
                if (!got)
                    QThread::msleep(2);
            }
            std::unique_ptr<GeneratedChunkData> data;
            wk.takeResultData(data);
            return (r.isOk() && got && !isError(o.error)) ? std::move(data)
                                                          : std::unique_ptr<GeneratedChunkData>();
        };

        const ChunkKey kMid{ 1, 1 }; // 3×3 网格中块——列 x,z ∈ [16,32)
        std::unique_ptr<GeneratedChunkData> buf1 = runBackgroundChunk(82, kMid);
        std::unique_ptr<GeneratedChunkData> buf2 = runBackgroundChunk(82, kMid);

        // ① 双实例双跑逐位一致（同 (seed,key,version) → 同输出；纯函数性的后台面）：
        const bool twinOk = bool(buf1) && bool(buf2) && buf1->identical(*buf2)
            && buf1->contentHash() == buf2->contentHash();
        ok = ok && twinOk;
        if (!twinOk)
            diag += QStringLiteral("[twin b1=%1 b2=%2 eq=%3 h1=%4 h2=%5] ")
                        .arg(buf1 ? int(buf1->valid()) : -1)
                        .arg(buf2 ? int(buf2->valid()) : -1)
                        .arg(buf1 && buf2 ? buf1->identical(*buf2) : false)
                        .arg(buf1 ? buf1->contentHash() : 0)
                        .arg(buf2 ? buf2->contentHash() : 0);

        // ② 后台 ≡ 同步共享函数（承重墙）：主线程直调同一份 fillTerrainColumn（手写 capture
        //    sink——传输层换成「零线程零队列」，生成逻辑单一权威不变）：
        GeneratedChunkData syncRef;
        syncRef.key = kMid;
        syncRef.seed = 82u;
        syncRef.generatorVersion = TerrainGen::kGeneratorVersion;
        syncRef.originX = kMid.cx * TerrainGen::kChunkSize;
        syncRef.originZ = kMid.cz * TerrainGen::kChunkSize;
        syncRef.height = kH;
        const size_t want = size_t(TerrainGen::kChunkSize) * size_t(TerrainGen::kChunkSize)
            * size_t(kH);
        syncRef.blocks.assign(want, 0);
        syncRef.states.assign(want, 0);
        struct CaptureSink
        {
            GeneratedChunkData *d;
            void write(int x, int y, int z, quint8 id, quint8 state)
            {
                const int lx = x - d->originX, lz = z - d->originZ;
                if (lx < 0 || lz < 0 || lx >= TerrainGen::kChunkSize
                    || lz >= TerrainGen::kChunkSize || y < 0 || y >= d->height)
                    return;
                const size_t i = size_t(lx) + size_t(TerrainGen::kChunkSize)
                    * (size_t(lz) + size_t(TerrainGen::kChunkSize) * size_t(y));
                d->blocks[i] = id;
                d->states[i] = state;
            }
        } capture{ &syncRef };
        TerrainGen syncGen(82, TerrainGen::Dims{ kW, kD, kH });
        for (int lz = 0; lz < TerrainGen::kChunkSize; ++lz)
            for (int lx = 0; lx < TerrainGen::kChunkSize; ++lx)
                syncGen.fillTerrainColumn(syncRef.originX + lx, syncRef.originZ + lz, capture);
        const bool syncEq = bool(buf1) && buf1->identical(syncRef)
            && buf1->contentHash() == syncRef.contentHash();
        ok = ok && syncEq;
        if (!syncEq)
            diag += QStringLiteral("[syncEq eq=%1 hBuf=%2 hSync=%3] ")
                        .arg(buf1 ? buf1->identical(syncRef) : false)
                        .arg(buf1 ? buf1->contentHash() : 0)
                        .arg(syncRef.contentHash());

        // ③ 真世界锚：fresh 48×48×96 s82 世界，中块 256 列逐列对照（世界查询面已委托同一
        //    TerrainGen 权威——同步路径 = 同一份函数的可观测面）。中块列距四角 ≥ ~22.6 格 >
        //    海域最大可达（半径 14×1.12 + 过渡 4.2 ≈ 19.9）→ 结构性内陆（海列若出现会被
        //    envelope 断言当场抓红——固定 seed 下确定性）：
        World wD;
        wD.setWidth(kW);
        wD.setDepth(kD);
        wD.setHeight(kH);
        wD.setSeed(82);
        bool anchorOk = bool(buf1);
        int anchorCols = 0;
        for (int lz = 0; anchorOk && lz < TerrainGen::kChunkSize; ++lz) {
            for (int lx = 0; anchorOk && lx < TerrainGen::kChunkSize; ++lx) {
                const int wx = syncRef.originX + lx, wz = syncRef.originZ + lz;
                int top = -1;
                for (int y = kH - 1; y >= 0; --y)
                    if (buf1->blockAt(lx, y, lz) != 0) { top = y; break; }
                const int expectTop = std::min(wD.heightAt(wx, wz), kH - 1);
                const int biome = wD.biomeIdAt(wx, wz);
                const quint8 topId = top >= 0 ? buf1->blockAt(lx, top, lz) : quint8(0);
                bool colOk = top == expectTop;
                if (colOk && biome == 2) // Desert → 沙
                    colOk = topId == BR::Sand;
                else if (colOk && biome == 4) // Snowy → 积雪层（state 0..2）
                    colOk = topId == BR::SnowLayer && buf1->stateAt(lx, top, lz) <= 2;
                else if (colOk) // 其余内陆群系 → 草
                    colOk = topId == BR::Grass;
                anchorOk = anchorOk && colOk;
                ++anchorCols;
                if (!colOk)
                    diag += QStringLiteral("[anchor lx=%1 lz=%2 top=%3 want=%4 biome=%5 id=%6] ")
                                .arg(lx).arg(lz).arg(top).arg(expectTop).arg(biome).arg(topId);
            }
        }
        anchorOk = anchorOk && anchorCols == 256;
        ok = ok && anchorOk;

        // ④ 方向对照：不同 seed 输出必不同（恒等断言非空转——固定 seed 下确定性）：
        std::unique_ptr<GeneratedChunkData> buf83 = runBackgroundChunk(83, kMid);
        const bool dirOk = bool(buf83) && !buf1->identical(*buf83)
            && buf83->seed == 83u && buf83->generatorVersion == TerrainGen::kGeneratorVersion;
        ok = ok && dirOk;
        if (!dirOk)
            diag += QStringLiteral("[dir diff=%1 seed=%2] ")
                        .arg(buf1 && buf83 ? !buf1->identical(*buf83) : false)
                        .arg(buf83 ? buf83->seed : 0u);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2012b purity bitwise identity: two worker instances on the"
                             " same (seed,key,version) agree bitwise, the background buffer is"
                             " bitwise-identical to a main-thread synchronous drive of the same"
                             " shared column-fill function, all 256 middle-chunk columns match"
                             " a fresh real world's heightAt/biomeIdAt anchors (inland"
                             " structurally), and a different seed differs (non-vacuous)"
                          << (ok ? QString() : diag);
    });

    // ── r2012c：epoch 丢弃旧任务 + 取消（验收③的后台版）──────────────────────────────────
    runLeg(QStringLiteral("r2012c world-epoch drops old in-flight tasks + cancel (R20.12"
        " acceptance 'the world epoch can discard old tasks', background edition): a request"
        " handed to the worker BEFORE an epoch bump still EXECUTES (the worker is epoch-blind"
        " by design - both data buffers land, execution accounting is complete) but its"
        " outcome is silently dropped at delivery on the caller thread (exactly ONE outcome,"
        " carrying the new-epoch request id; the stale buffer arrives first and is identified"
        " for discard by key) - dropping is a scheduler-side delivery filter, the same gate"
        " as R20.11; a request canceled BEFORE handout never executes and never delivers"
        " (execution and data counts unchanged), and both ledgers (pending + in-flight) drain"
        " to zero"), [&]() {
        bool ok = true;
        QString diag;

        BackgroundGenerationWorker worker(82, kDims);
        GenerationScheduler sched;
        sched.setWorker(&worker);

        // ① 交接后才 bump epoch：K1（epoch 0）先 submit + pump 交接 → setWorldEpoch(7) →
        //    K2（epoch 7）submit → 泵到收干：
        const auto r1 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 2, 1 });
        sched.pump(); // K1 交接（边①若有挂点——本腿零世界纯账本）
        const bool handed = sched.inFlightJobCount() == 1 && sched.pendingJobCount() == 0;
        sched.setWorldEpoch(7); // 世界换代（交接后才发生——R20.12 验收③的核心时序）
        const auto r2 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 2 });
        QVector<GenerationJobOutcome> outs;
        pumpUntilOutcomes(sched, 1, outs); // 只等 K2 的 outcome（K1 永不投递——deadline 内必到）

        // ② 执行账面完整：worker 不识 epoch → 两个任务都跑了、两条缓冲都到（首条 = K1 的过期
        //    产物，按键识别后由调用方丢弃——「丢弃」发生在投递与应用之间，不在执行侧）：
        const quint64 executed = worker.executedCount();
        const int dataLeft = worker.resultDataCount();
        bool execOk = r1.isOk() && r2.isOk() && handed && outs.size() == 1
            && !isError(outs[0].error) && outs[0].requestId == r2.value()
            && outs[0].key == ChunkKey{ 0, 2 } && executed == 2 && dataLeft == 2;
        std::unique_ptr<GeneratedChunkData> stale, fresh;
        if (execOk) {
            worker.takeResultData(stale);
            worker.takeResultData(fresh);
            execOk = bool(stale) && bool(fresh) && stale->key == ChunkKey{ 2, 1 } // 完成序 = 交接序
                && stale->seed == 82u && fresh->key == ChunkKey{ 0, 2 };
        }
        execOk = execOk && sched.pendingJobCount() == 0 && sched.inFlightJobCount() == 0
            && sched.pendingRequestCount() == 0; // 双账收干（过期别名随完成清账）
        ok = ok && execOk;
        if (!execOk)
            diag += QStringLiteral("[epoch outs=%1 oid=%2 want=%3 exec=%4 data=%5 sk=%6 fk=%7] ")
                        .arg(outs.size())
                        .arg(outs.isEmpty() ? 0u : outs[0].requestId)
                        .arg(r2.value())
                        .arg(executed)
                        .arg(dataLeft)
                        .arg(stale ? stale->key.packed() : quint64(0))
                        .arg(fresh ? fresh->key.packed() : quint64(0));

        // ③ 交接前取消：永不执行永不投递（执行/数据计数不动；账面照常收干）：
        const quint64 executedBefore = worker.executedCount();
        const int dataBefore = worker.resultDataCount();
        const auto r3 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 2 });
        const bool c3 = sched.cancel(r3.value());
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < kDeadlineMs
               && (sched.pendingJobCount() + sched.inFlightJobCount()) > 0) {
            sched.pump();
            QThread::msleep(2);
        }
        GenerationJobOutcome extra;
        int extraOutcomes = 0;
        while (sched.takeOutcome(extra))
            ++extraOutcomes; // r3 不得有 outcome
        const bool cancelOk = r3.isOk() && c3 && extraOutcomes == 0
            && worker.executedCount() == executedBefore // 死 job 整体跳过零执行
            && worker.resultDataCount() == dataBefore
            && sched.pendingJobCount() == 0 && sched.inFlightJobCount() == 0
            && sched.pendingRequestCount() == 0;
        ok = ok && cancelOk;
        if (!cancelOk)
            diag += QStringLiteral("[cancel r3=%1 c=%2 extra=%3 exec=%4/%5 data=%6/%7 pj=%8 inf=%9] ")
                        .arg(r3.isOk()).arg(c3).arg(extraOutcomes)
                        .arg(worker.executedCount()).arg(executedBefore)
                        .arg(worker.resultDataCount()).arg(dataBefore)
                        .arg(sched.pendingJobCount()).arg(sched.inFlightJobCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2012c epoch drop + cancel: an in-flight pre-bump task still"
                             " executes (worker is epoch-blind, both buffers land, stale one"
                             " identified by key for discard) but delivers exactly zero"
                             " outcomes - only the new-epoch request delivers - while a"
                             " cancel-before-handout request never executes and never"
                             " delivers, and both ledgers drain to zero"
                          << (ok ? QString() : diag);
    });

    // ── r2012d：退出安全 + R20.10 生命周期互锁 + 固定世界零变化 + 源码钉/反探（验收④⑤）────
    runLeg(QStringLiteral("r2012d exit safety + lifecycle interlock + fixed-world zero change +"
        " structure pins (R20.12 acceptance 'exit is never stuck on tasks' + R20.10"
        " integration): three destroy-immediately scenarios (in-flight plus queued backlog,"
        " queued-only with zero executions, idle) all leave their scopes - the leg body"
        " completing IS the bounded-join proof; on a bare 3x3 ChunkManager an Absent chunk"
        " (routed via legal Evicting edges) advances past Absent at handout (edge 1 fires"
        " before collection - ordering inherent, not timing-dependent) and lands exactly"
        " Generated after the outcome"
        " (the guard proves both edges were traversed legally: edge 2 from Absent would be"
        " rejected), generated data is applied through the guarded ChunkManager::setBlock"
        " entry and matches the buffer bitwise per column, a cancel-before-handout request"
        " leaves its chunk at Absent (no advance, no illegal rollback), and a"
        " default-steady-Loaded chunk runs the work with the nine-entry lifecycle table"
        " bit-untouched; comment-stripped pins hold the QObject-free output pin, the thread"
        " primitive, the cv wake, the queue capacity, the async seam overrides and the"
        " terraingen single-authority anchors, while reverse probes prove no QML surface"
        " ever touches the background worker and world.cpp carries zero generation-job or"
        " worker references (zero production wiring)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 退出安全（验收④）：三个立即析构场景——腿体走完即证 join 有界（挂死 = 整体超时红）：
        {
            BackgroundGenerationWorker wA(5, kDims);
            GenerationScheduler sA;
            sA.setWorker(&wA);
            for (int i = 0; i < 12; ++i)
                sA.submit(GenerationJobKind::Generate, ChunkKey{ i % 3, i / 3 });
            sA.pump(); // 在途 + 待队并存
        } // 立即析构：stop + 丢弃队内未开工 + join（至多一个在途任务）
        {
            BackgroundGenerationWorker wB(6, kDims);
            GenerationScheduler sB;
            sB.setWorker(&wB);
            for (int i = 0; i < 3; ++i)
                sB.submit(GenerationJobKind::Generate, ChunkKey{ i, 2 });
            // 零 pump：任务全在 scheduler 账面，worker 空闲
        } // 立即析构（空闲 worker）
        {
            BackgroundGenerationWorker wC(7, kDims); // 空转即毁
        }
        const bool exitOk = true; // 走到此处 = 三场景均未挂死（PASS 行本身即证据）
        ok = ok && exitOk;

        // ② R20.10 生命周期互锁：裸 3×3 网格（默认稳态全 Loaded——r2010a/r2011d 同款）：
        ChunkManager mgr(kW, kD, kH);
        bool steadyOk = mgr.chunksX() == 3 && mgr.chunksZ() == 3;
        for (int cz = 0; cz < 3 && steadyOk; ++cz)
            for (int cx = 0; cx < 3 && steadyOk; ++cx)
                steadyOk = steadyOk && mgr.lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && steadyOk;
        if (!steadyOk) diag += QStringLiteral("[steady] ");

        GenerationScheduler sched(&mgr);
        BackgroundGenerationWorker worker(82, kDims);
        sched.setWorker(&worker);

        // ③ 互锁主链：(1,1) 经合法边⑥⑦布到 Absent → submit → 泵一轮（交接边①在收割相之前
        //    无条件发生 → 终态 != Absent 是确定性的）→ 有界轮询收干 → 终态 Generated（守卫
        //    证明两边皆合法：Absent 上边②必被拒 → Generated ⟹ 边①先于边②发生）：
        const bool route11 = mgr.setLifecycle(1, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(1, 1, ChunkLifecycle::Absent);
        const auto ra = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        sched.pump();
        const bool edge1 = route11 && ra.isOk()
            && mgr.lifecycleAt(1, 1) != ChunkLifecycle::Absent; // 边①已取（Loading 或已到 Generated）
        QVector<GenerationJobOutcome> outs;
        pumpUntilOutcomes(sched, 1, outs);
        const bool edge2 = outs.size() == 1 && !isError(outs[0].error)
            && outs[0].requestId == ra.value()
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Generated; // 边②成功后
        ok = ok && edge1 && edge2;
        if (!edge1 || !edge2)
            diag += QStringLiteral("[lock route=%1 e1=%2 e2=%3 life=%4 outs=%5] ")
                        .arg(route11).arg(edge1).arg(edge2)
                        .arg(int(mgr.lifecycleAt(1, 1))).arg(outs.size());

        // ④ 结果经守卫入口写入：applyGeneratedChunkData（ChunkManager::setBlock 5 参守卫）
        //    落格 → 逐列 envelope + 逐采样点与缓冲逐位比对：
        std::unique_ptr<GeneratedChunkData> buf;
        const bool tookData = worker.takeResultData(buf) && bool(buf)
            && buf->key == ChunkKey{ 1, 1 } && buf->valid();
        const bool applied = tookData && applyGeneratedChunkData(mgr, *buf);
        bool applyOk = applied;
        for (int lz = 0; applyOk && lz < TerrainGen::kChunkSize; ++lz) {
            for (int lx = 0; applyOk && lx < TerrainGen::kChunkSize; ++lx) {
                const int wx = buf->originX + lx, wz = buf->originZ + lz;
                int top = -1;
                for (int y = kH - 1; y >= 0; --y)
                    if (buf->blockAt(lx, y, lz) != 0) { top = y; break; }
                applyOk = applyOk && top >= 0
                    && mgr.blockAt(wx, top, wz) == buf->blockAt(lx, top, lz)
                    && top > 2
                    && mgr.blockAt(wx, top - 1, wz) == buf->blockAt(lx, top - 1, lz)
                    && mgr.blockAt(wx, top - 2, wz) == buf->blockAt(lx, top - 2, lz)
                    && mgr.blockAt(wx, 0, wz) == buf->blockAt(lx, 0, lz);
            }
        }
        ok = ok && applyOk;
        if (!applyOk)
            diag += QStringLiteral("[apply took=%1 applied=%2] ").arg(tookData).arg(applied);

        // ⑤ 取消不推进：(2,0) 布到 Absent → submit → 取消（交接前）→ 泵收干 → 恒 Absent
        //    （六态表无 Loading→Absent 回退边，本层不做非法回退）：
        const bool route20 = mgr.setLifecycle(2, 0, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(2, 0, ChunkLifecycle::Absent);
        const auto rb = sched.submit(GenerationJobKind::Generate, ChunkKey{ 2, 0 });
        const bool cb = sched.cancel(rb.value());
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < kDeadlineMs
               && (sched.pendingJobCount() + sched.inFlightJobCount()) > 0) {
            sched.pump();
            QThread::msleep(2);
        }
        const bool noAdvance = route20 && rb.isOk() && cb
            && mgr.lifecycleAt(2, 0) == ChunkLifecycle::Absent;
        ok = ok && noAdvance;
        if (!noAdvance)
            diag += QStringLiteral("[noadv route=%1 life=%2] ")
                        .arg(route20).arg(int(mgr.lifecycleAt(2, 0)));

        // ⑥ 固定世界零变化：默认稳态 Loaded chunk 直接 submit + 泵收干 → 工作执行、outcome
        //    正常、生命周期表 9 格逐位不动（守卫拒非法转移 → best-effort 忽略——零变化结构根据）：
        std::array<ChunkLifecycle, 9> lifeBefore {};
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                lifeBefore[size_t(cx + 3 * cz)] = mgr.lifecycleAt(cx, cz);
        const auto rd = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 0 });
        QVector<GenerationJobOutcome> outsD;
        pumpUntilOutcomes(sched, 1, outsD);
        bool fixedOk = rd.isOk() && outsD.size() == 1 && !isError(outsD[0].error)
            && worker.executedCount() >= 2 // (1,1) + (0,0) 至少都执行过（(2,0) 已取消不执行）
            && mgr.lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
        for (int cz = 0; cz < 3 && fixedOk; ++cz)
            for (int cx = 0; cx < 3 && fixedOk; ++cx)
                fixedOk = fixedOk
                    && mgr.lifecycleAt(cx, cz) == lifeBefore[size_t(cx + 3 * cz)];
        ok = ok && fixedOk;
        if (!fixedOk)
            diag += QStringLiteral("[fixed outs=%1 life00=%2] ")
                        .arg(outsD.size()).arg(int(mgr.lifecycleAt(0, 0)));

        // ⑦ 源码钉（pinSet 剥注释）+ 反探（minCount=1 空转钉惯用法——miss 非空 = 合规缺席）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QStringList missBg = pinSet(
            srcRoot + QStringLiteral("/World/backgroundgeneration.h"), {
                SrcPin("r2012 output QObjectFree pin",
                    "static_assert(QObjectFree<GeneratedChunkData>", 1),
                SrcPin("r2012 thread primitive", "std::thread m_thread;", 1),
                SrcPin("r2012 cv wake on stop", "m_cv.notify_all()", 1),
                SrcPin("r2012 cv wake on submit", "m_cv.notify_one()", 1),
                SrcPin("r2012 queue capacity bound", "kMaxQueuedTasks", 2),
                SrcPin("r2012 async seam override", "bool isAsynchronous() const override", 1),
                SrcPin("r2012 handout backpressure", "background generation task queue full", 1),
                SrcPin("r2012 chunk generator", "generateTerrainChunk(", 2),
                SrcPin("r2012 guarded application entry", "applyGeneratedChunkData(", 1),
                SrcPin("r2012 chunk-size cross pin", "TerrainGen::kChunkSize == Chunk::kSize", 1),
            });
        for (const QString &m : missBg) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missTg = pinSet(
            srcRoot + QStringLiteral("/World/terraingen.h"), {
                SrcPin("r2012 generator version constant", "kGeneratorVersion", 1),
                SrcPin("r2012 biome enum single authority", "enum class Biome", 1),
                SrcPin("r2012 column fill single authority", "fillTerrainColumn", 1),
                SrcPin("r2012 voxel hash single authority", "quint32 hashVoxel(int seed", 1),
                SrcPin("r2012 water level single authority", "kWaterLevel", 2),
            });
        for (const QString &m : missTg) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("r2012 world sampler member", "TerrainGen m_terrain", 1),
                SrcPin("r2012 world biome alias", "using Biome = TerrainGen::Biome;", 1),
            });
        for (const QString &m : missWh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("r2012 world sampler rebuild sites", "m_terrain = TerrainGen(", 2),
                SrcPin("r2012 world column fill consumer", "m_terrain.fillTerrainColumn(", 1),
                SrcPin("r2012 world biome delegate", "m_terrain.biomeComputeAt(", 2),
                SrcPin("r2012 world height delegate", "m_terrain.heightAt(", 1),
            });
        for (const QString &m : missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool negOk
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/backgroundgeneration.h"), "Q_OBJECT")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/backgroundgeneration.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/backgroundgeneration.h"), "Q_PROPERTY")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/terraingen.h"), "Q_OBJECT")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "buildPermutation")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "m_perm")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "BackgroundGenerationWorker")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "BackgroundGenerationWorker")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "ackgroundGeneration");
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg-probe] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2012d exit safety + lifecycle interlock + fixed-world zero"
                             " change: three destroy-immediately scenarios leave their scopes"
                             " (bounded join), an Absent chunk advances at handout and lands"
                             " Generated through the guarded lifecycle edges, applied data"
                             " matches the buffer through the guarded setBlock entry,"
                             " cancel-before-handout never advances, a default-Loaded chunk"
                             " runs with the lifecycle table bit-untouched, and structure"
                             " pins + reverse probes hold the value/thread discipline and"
                             " zero production wiring"
                          << (ok ? QString() : diag);
    });
}
