#include "matrix_helpers.h"

#include "backgroundgeneration.h" // 被测面：worker 诊断读面（线程身份对账 r2012a 先例）+ 数据面值类型
#include "chunklifecycle.h"       // 被测面：六态类型（生命周期沿断言）
#include "chunkstreamdriver.h"    // 被测面：驱动器 stats / pending 只读观测
#include "gamesession.h"          // 被测面：GameSession 流式会话（W2 驱动编排归属——R20.07 壳）
#include "generationjob.h"        // 被测面：GenerationJobOutcome（#7 结果面）

#include <QElapsedTimer>
#include <QThread> // 线程身份对账（QThread::currentThreadId）

// §29.5-W2 位置源 + 驱动接线 探针段（4 腿；filter 词 r2024；矩阵 632→632+N）。置尾先例沿用
//（接 section26，runAll 末执行）。任务契约（docs/refactor-plan-2026-09-08.md §29.5.2 W2 原文）：
//   「fixed 世界连驱动器都不构造（D2 零活动墙）」→ r2024a（fixed 世界 GameSession 全 tick/移动
//     扫掠 → 无驱动器无 worker 无生命周期活动，全状态逐位不动）；
//   「玩家位 → floorDiv16 → GameSession tick 尾 driver.onPlayerChunk + Absent→Loading→
//     Generated→Loaded 沿真实驱动 + OOB 门翻真 + residentChunkRevision 沿发射 + 走离半径
//     pending 取消」→ r2024b（真 PlayerController 走查 × 真驱动器 × 真线程 worker）；
//   「BackgroundGenerationWorker 首个生产消费者 + 收割拍双面同拍消费（review0916 #7 硬契约）
//     + 主线程落位/population + 背压可见 + 重请求重生成」→ r2024c；
//   「结构钉」→ r2024d（QML 零触碰反探 / fixed 零构造门钉 / #7 双面同拍函数域钉 /
//     population 主线程单一落点钉 / worldstore 禁触反探）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘 GameSession::pumpStreamingTick 的位置沿喂入（m_streamDriver->
//     onPlayerChunk 调用点 if (false) 前缀）→ 声明红面 {r2024b, r2024c}（任务书候选面 = r2024b
//     走查收敛红——零提交零物化；r2024c 同住在该生产接线上（线程身份/配对面同源）如实双红）。
//     r2024a 不误伤（fixed 世界 m_streamDriver 恒 null，摘除行不可达）；r2024d 不误伤（钉面 =
//     源码文本在位，if (false) 前缀不剥调用文本）。
//   变异二（NEG-2）= 摘 GameSession::pumpStreamingTick 的 #7 数据面同拍消费（takeResultData
//     while 循环 if (false) 前缀）→ 声明红面 {r2024b, r2024c}（任务书候选面 = r2024c 的 m_data
//     积压红；数据面 = 主线程落位唯一通路 → r2024b 物化收敛同源红——两面同住数据面，扩面如实
//     声明[t1053/r2021 NEG-2 改声明先例]）。r2024a/r2024d 同上不误伤。
//   阴性日志：build/ 下四件 matrix_r2024_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 时长控制：腿内 sparse 世界 spawnPreGenerateRadius=0（零预生成，全量走流式链）+ 半径压小
//   （r2024b/c 主走查 (1,1,2)=3×3；r2024c 背压相短暂 (4,4,6)=9×9 后立即收半径）；收敛轮询
//   deadline 有界（防 flake 不挂死）+ 真线程腿三连跑（r2012 防 flake 先例）。
void MatrixRun::section27_streaming_wiring()
{
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42; // sparse 核心域（5×5 chunk，与 r2023 同族）
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk，r2007 先例）

    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0; // 零预生成：全量走流式链（通电面全暴露）
        return World(sp);
    };
    // 收敛轮询：泵 tick 直到 keys 全部 Loaded 或超时（真线程收割节奏不定——防 flake 靠 deadline，
    //   不靠睡等；每轮 stepTick(0.11) = 恰 1 整 tick = 1 个 tick 尾流式泵拍）。
    const auto convergeLoaded = [&](GameSession &gs, World &w,
                                    const QVector<QPair<int, int>> &keys, int deadlineMs,
                                    QString &diag) {
        QElapsedTimer t;
        t.start();
        for (;;) {
            bool all = true;
            for (const auto &k : keys)
                if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded) {
                    all = false;
                    break;
                }
            if (all)
                return true;
            if (t.elapsed() > deadlineMs) {
                QString miss;
                for (const auto &k : keys)
                    if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded)
                        miss += QStringLiteral("(%1,%2)=%3 ")
                                    .arg(k.first)
                                    .arg(k.second)
                                    .arg(int(w.chunks().lifecycleAt(k.first, k.second)));
                diag += QStringLiteral("[timeout %1ms miss: %2] ").arg(deadlineMs).arg(miss);
                return false;
            }
            gs.stepTick(0.11);
            QThread::msleep(2);
        }
    };

    // ── r2024a：fixed 零活动承重墙 ──────────────────────────────────────────────────────
    // fixed 世界 GameSession：streamingActive()==false（驱动器/worker 连构造都不发生）+ 全
    // tick/移动扫掠零生命周期零沿零落位 + 全状态逐位不动（六态表/驻留账/体素列/编辑账）。
    // 位置源本身照常发沿（真 PlayerController floorDiv16 沿检测工作面）——fixed 会话对沿零响应
    // = 零活动墙的最强形态（app 冒烟同面实证：app fixed 世界 → 零驱动器构造）。
    runLeg(QStringLiteral("r2024a fixed-world inert wall (a fixed-world GameSession constructs no"
        " streaming driver and no background worker - streamingActive stays false and the driver"
        " handle stays null; a real PlayerController chunk-edge sweep across multiple chunk"
        " boundaries drives full session ticks with zero lifecycle transitions, zero resident"
        " revision movement, zero outcome/adoption accounting and a bitwise-untouched state"
        " face: all nine chunk lifecycle entries, the resident set, sampled voxel columns and"
        " heightmaps all identical before and after)"), [&]() {
        bool ok = true;
        QString diag;

        World w; // fixed（默认构造路径 + setter incantation，r2007 先例）
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);

        GameSession gs(w);
        const bool inertOk = !gs.streamingActive() && gs.streamDriver() == nullptr
            && gs.streamWorker() == nullptr && gs.streamingOutcomeCount() == 0
            && gs.streamingAdoptedCount() == 0;
        ok = ok && inertOk;
        if (!inertOk)
            diag += QStringLiteral("[inert active=%1 drv=%2 wrk=%3] ")
                        .arg(gs.streamingActive())
                        .arg(gs.streamDriver() != nullptr)
                        .arg(gs.streamWorker() != nullptr);

        // 全状态快照（扫掠前）：九 chunk 六态 + 驻留账 + 抽样列体素/高度图 + revision。
        QVector<int> lifeBefore;
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                lifeBefore.append(int(w.chunks().lifecycleAt(cx, cz)));
        const int revBefore = w.residentChunkRevision();
        const int resBefore = w.residentChunkCount();
        static const int kCols[][2] = { { 8, 8 }, { 40, 40 }, { 47, 5 }, { 5, 47 }, { 24, 33 } };
        QVector<QPair<int, int>> voxBefore; // (id,state) 平铺
        QVector<int> hmBefore;
        for (const auto &c : kCols) {
            hmBefore.append(w.heightmapAt(c[0], c[1]));
            for (int y = 0; y < kHF; ++y)
                voxBefore.append({ w.blockAt(c[0], y, c[1]), w.stateAt(c[0], y, c[1]) });
        }

        // 真 PlayerController 位置沿扫掠（跨 ≥3 次 chunk 边界）+ 全 tick 驱动。
        PlayerController pc;
        pc.setWorld(&w);
        GameSession gsEdgeProbe(w); // 沿计数会话（第二会话仅收沿计数；fixed 零活动同面）
        int edges = 0;
        QObject::connect(&pc, &PlayerController::playerChunkChanged, &gs,
            [&edges, &gsEdgeProbe](int cx, int cz) {
                ++edges;
                gsEdgeProbe.notePlayerChunk(cx, cz);
            });
        pc.loadSavedState(8.5f, 70.0f, 8.5f, 0.0f, 0.0f, 0); // chunk (0,0)
        pc.tick();
        const int edgesStart = edges;
        pc.loadSavedState(30.5f, 70.0f, 12.5f, 0.0f, 0.0f, 0); // chunk (1,0)
        pc.tick();
        pc.loadSavedState(41.5f, 70.0f, 39.5f, 0.0f, 0.0f, 0); // chunk (2,2)
        pc.tick();
        pc.loadSavedState(9.5f, 70.0f, 41.5f, 0.0f, 0.0f, 0); // 回 (0,2)
        pc.tick();
        const bool edgeOk = edgesStart >= 1 && edges >= edgesStart + 3; // 起始沿 + ≥3 次换格沿
        ok = ok && edgeOk;
        if (!edgeOk)
            diag += QStringLiteral("[edges start=%1 total=%2] ").arg(edgesStart).arg(edges);

        for (int i = 0; i < 12; ++i)
            gs.stepTick(0.11); // 12 整 tick：tick 尾流式泵拍对 fixed 恒零动作（null 驱动器早退）

        // 全状态逐位不动断言。
        bool untouched = w.residentChunkRevision() == revBefore && w.residentChunkCount() == resBefore
            && gs.streamingOutcomeCount() == 0 && gs.streamingAdoptedCount() == 0
            && gsEdgeProbe.streamingOutcomeCount() == 0 && gsEdgeProbe.streamingAdoptedCount() == 0;
        int lifeIdx = 0;
        for (int cz = 0; cz < 3 && untouched; ++cz)
            for (int cx = 0; cx < 3 && untouched; ++cx)
                untouched = untouched
                    && int(w.chunks().lifecycleAt(cx, cz)) == lifeBefore[lifeIdx++];
        int vi = 0;
        for (int ci = 0; ci < 5 && untouched; ++ci) {
            untouched = untouched && w.heightmapAt(kCols[ci][0], kCols[ci][1]) == hmBefore[ci];
            for (int y = 0; y < kHF && untouched; ++y) {
                untouched = untouched && w.blockAt(kCols[ci][0], y, kCols[ci][1]) == voxBefore[vi].first
                    && w.stateAt(kCols[ci][0], y, kCols[ci][1]) == voxBefore[vi].second;
                ++vi;
            }
        }
        ok = ok && untouched;
        if (!untouched)
            diag += QStringLiteral("[untouched false] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2024a fixed inert wall: the fixed-world session constructs no"
                             " streaming driver or worker, a real player chunk-edge sweep with"
                             " twelve full session ticks produces zero lifecycle / revision /"
                             " outcome / adoption activity, and every sampled state face stays"
                             " bitwise identical" << (ok ? QString() : diag);
    });

    // ── r2024b：通电走查承重 ─────────────────────────────────────────────────────────────
    // 真 PlayerController 走查 × 真驱动器 × 真线程 worker：半径内 chunk 按请求物化（①②③沿
    // 真实驱动到 Loaded）+ OOB 门翻真 + residentChunkRevision 沿发射（P4 池消费面连通）+
    // 走离半径请求取消（驱动器半径取消步的生产面）+ 内容正确性抽样柱（异步链 ≡ fixed 同区，
    // generateTerrainChunk ≡ 同步权威 r2012b + population 同一重放 r2023 承接）。
    runLeg(QStringLiteral("r2024b powered walk load-bearing wall (a sparse world with a real"
        " player walking by saved-state teleports drives the session tick-tail streaming beat:"
        " the nine in-radius chunks materialize through the real lifecycle chain to Loaded, the"
        " OOB gates flip to real answers inside the loaded set while staying OOB outside, the"
        " resident revision moves and the resident set counts exactly the loaded chunks, walking"
        " away triggers the driver's radius cancellation accounting and the abandoned window"
        " still converges to Loaded, and sampled full-height voxel columns are bitwise identical"
        " to the fixed twin of the same seed with a non-vacuous population signature)"), [&]() {
        bool ok = true;
        QString diag;

        World ws = makeSparse();
        GameSession gs(ws);
        gs.configureStreamingRadii(1, 1, 2); // 腿内压小半径（时长控制；生产 = P1 默认 4/4/6）
        const bool poweredOk = gs.streamingActive() && gs.streamDriver() != nullptr
            && gs.streamWorker() != nullptr;
        ok = ok && poweredOk;
        if (!poweredOk)
            diag += QStringLiteral("[powered] ");

        PlayerController pc;
        pc.setWorld(&ws);
        QObject::connect(&pc, &PlayerController::playerChunkChanged, &gs,
            [&gs](int cx, int cz) { gs.notePlayerChunk(cx, cz); });

        const int rev0 = ws.residentChunkRevision();
        const int res0 = ws.residentChunkCount(); // radius 0 构造仍预生成中心 chunk (2,2)（W1 链）
        const bool preOk = ws.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded;
        ok = ok && preOk;

        // ① 走查相：玩家落到 chunk (2,2) → 收敛窗内余 8 chunk（中心已预生成）全 Loaded。
        pc.loadSavedState(40.5f, 70.0f, 40.5f, 0.0f, 0.0f, 0); // Spectator noclip：无重力静态
        pc.tick(); // 起始沿 → notePlayerChunk(2,2)
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded(gs, ws, centerKeys, 30000, d1);
        ok = ok && c1;
        if (!c1)
            diag += d1;

        // OOB 门翻真：界内 heightmap 自洽非空柱 / 界外（未加载域）恒 OOB 同值。
        bool gateOk = ws.heightmapAt(40, 40) >= 0 && ws.heightmapAt(200, 200) == -1
            && ws.blockAt(200, 5, 200) == 0;
        int topNonAir = -1;
        for (int y = kH - 1; y >= 0 && topNonAir < 0; --y)
            if (ws.blockAt(40, y, 40) != 0)
                topNonAir = y;
        gateOk = gateOk && topNonAir >= 0 && topNonAir <= ws.heightmapAt(40, 40) + 1;
        ok = ok && gateOk;
        if (!gateOk)
            diag += QStringLiteral("[gate hm=%1 top=%2 oobhm=%3] ")
                        .arg(ws.heightmapAt(40, 40)).arg(topNonAir)
                        .arg(ws.heightmapAt(200, 200));

        // 驻留账 + revision 沿（P4 池消费面连通：③晋升经 setChunkLifecycle forwarder 携带）。
        const int rev1 = ws.residentChunkRevision();
        const bool resOk = rev1 > rev0 && ws.residentChunkCount() == res0 + 8;
        ok = ok && resOk;
        if (!resOk)
            diag += QStringLiteral("[res rev %1->%2 n %3->%4] ")
                        .arg(rev0).arg(rev1).arg(res0).arg(ws.residentChunkCount());

        // 内容正确性抽样柱（异步链 ≡ fixed 同区逐位——三 chunk 各一列，全 y 带 id+state）。
        // 掩蔽口径 = r2023b 同族：ore 体素（scatterOres (c) 块缘替代面）+ 结构族五员足迹
        //（(c) 豁免不重放，9 点偏移栅格覆盖地牢豁口）不具恒等面，跳过；无掩蔽全等体素计数
        // 入 diag（非空转）。
        World wf; // 同 seed 同 dims fixed 孪生（r2023b 同族口径）
        wf.setWidth(kWS);
        wf.setDepth(kDS);
        wf.setHeight(kH);
        wf.setSeed(kSeed);
        const auto structMasked = [&wf](int x, int y, int z) {
            static const int kShifts[9][2] = { { 0, 0 }, { 6, 0 }, { -6, 0 }, { 0, 6 }, { 0, -6 },
                { 6, 6 }, { 6, -6 }, { -6, 6 }, { -6, -6 }, };
            for (const auto &sh : kShifts)
                if (wf.insideDungeon(x + sh[0], y, z + sh[1])
                    || wf.insideMineshaft(x + sh[0], y, z + sh[1])
                    || wf.insideDesertTemple(x + sh[0], y, z + sh[1])
                    || wf.insideJungleTemple(x + sh[0], y, z + sh[1])
                    || wf.insideStronghold(x + sh[0], y, z + sh[1]))
                    return true;
            return false;
        };
        const auto isOre = [](quint8 b) {
            switch (b) {
                case BR::CoalOre: case BR::IronOre: case BR::GoldOre: case BR::DiamondOre:
                case BR::RedstoneOre: case BR::LapisOre: case BR::CopperOre: return true;
                default: return false;
            }
        };
        bool parityOk = true;
        long cleanEqual = 0;
        static const int kParityCols[][2] = { { 36, 44 }, { 20, 60 }, { 56, 20 } };
        for (const auto &c : kParityCols) {
            for (int y = 0; y < kH && parityOk; ++y) {
                const quint8 bs = ws.blockAt(c[0], y, c[1]);
                const quint8 bf = wf.blockAt(c[0], y, c[1]);
                if (bs == bf && ws.stateAt(c[0], y, c[1]) == wf.stateAt(c[0], y, c[1])) {
                    if (!isOre(bf) && !structMasked(c[0], y, c[1]))
                        ++cleanEqual;
                    continue;
                }
                if (isOre(bf) || isOre(bs) || structMasked(c[0], y, c[1]))
                    continue; // 掩蔽面（r2023b 同族豁免）
                parityOk = false;
                diag += QStringLiteral("[parity x=%1 z=%2 y=%3 s=%4/%5 f=%6/%7] ")
                            .arg(c[0]).arg(c[1]).arg(y).arg(bs).arg(ws.stateAt(c[0], y, c[1]))
                            .arg(bf).arg(wf.stateAt(c[0], y, c[1]));
            }
        }
        ok = ok && parityOk && cleanEqual >= 200;
        if (!parityOk || cleanEqual < 200)
            diag += QStringLiteral("[cleanEqual=%1] ").arg(cleanEqual);

        // population 非空转签名（物化沿 population 落地：加载区树叶 > 0——r2023b 同 seed 同区
        // 已证树叶存在，流式链用同一重放）。
        long leaves = 0;
        for (int x = 16; x < 64 && leaves < 8; ++x)
            for (int z = 16; z < 64 && leaves < 8; ++z)
                for (int y = 0; y < kH && leaves < 8; ++y)
                    if (ws.blockAt(x, y, z) == BR::Leaves)
                        ++leaves;
        ok = ok && leaves >= 8;
        if (leaves < 8)
            diag += QStringLiteral("[leaves=%1] ").arg(leaves);

        // ② 走离相：玩家跳到 chunk (6,2)（半径外新窗）→ 泵 1 拍（提交 + 交接 9）→ 立即跳回
        //    (2,2)：驱动器半径取消步对新窗在途别名逐个 cancel（取消判据 = 半径几何）。取消计数
        //    == 9（拍间 worker 仅毫秒级推进、收割发生在本拍泵内且晚于取消步 → 9 条请求全部仍
        //    pending 可取消）。
        const int canceledBefore = gs.streamDriver()->stats().canceled;
        pc.loadSavedState(104.5f, 70.0f, 40.5f, 0.0f, 0.0f, 0); // chunk (6,2) 核心域外延展区
        pc.tick();
        gs.stepTick(0.11); // 泵 1 拍：提交 + 交接 (5..7)×(1..3) 窗
        pc.loadSavedState(40.5f, 70.0f, 40.5f, 0.0f, 0.0f, 0); // 跳回 (2,2)
        pc.tick();
        gs.stepTick(0.11); // 泵 1 拍：半径取消步执行（先于本拍收割）
        const int canceledAfter = gs.streamDriver()->stats().canceled;
        const bool cancelOk = canceledAfter - canceledBefore == 9;
        ok = ok && cancelOk;
        if (!cancelOk)
            diag += QStringLiteral("[cancel +%1] ").arg(canceledAfter - canceledBefore);

        // ③ 收敛相：被取消窗口的已完成/在途产物经数据面照常落位（W2 语义：内容已算出即采纳；
        //   离场驻留的回收 = W3 驱逐面）→ (5..7)×(1..3) 窗全 Loaded，驻留恰 res0 + 8 + 9。
        QVector<QPair<int, int>> awayKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 5; cx <= 7; ++cx)
                awayKeys.append({ cx, cz });
        QString d3;
        const bool c3 = convergeLoaded(gs, ws, awayKeys, 30000, d3);
        ok = ok && c3 && ws.residentChunkCount() == res0 + 17;
        if (!c3)
            diag += d3;
        if (ws.residentChunkCount() != res0 + 17)
            diag += QStringLiteral("[res n=%1 want=%2] ")
                        .arg(ws.residentChunkCount()).arg(res0 + 17);
        ok = ok && ws.residentChunkRevision() > rev1; // 沿单调

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2024b powered walk: a real player walk drives the tick-tail"
                             " streaming beat through the real worker - nine in-radius chunks"
                             " reach Loaded via the real lifecycle chain, OOB gates flip only"
                             " inside the loaded set, the resident revision and count track the"
                             " materializations, walking away records nine radius cancellations"
                             " and the abandoned window still converges, and sampled columns"
                             " match the fixed twin bitwise with a live population signature"
                          << (ok ? QString() : diag);
    });

    // ── r2024c：真线程收割 + #7 契约 + 背压可见/重请求重生成 ─────────────────────────────
    // 相 1（9 chunk 小窗）：worker 线程身份（执行线程 == 工作线程 ≠ 调用者线程——r2012a 先例；
    // 落位/收集发生在调用者线程 = 主线程）+ #7 同拍配对精确性（每 job 恰一数据一条 outcome：
    // submitted==9、outcome 累计==9、adopted 累计==9、收割拍后 outcomeCount==0 且
    // resultDataCount()==0——双面同拍排干，m_data 零积压）。
    // 相 2（背压）：短暂 (4,4,6) 大窗 → 81 请求 > scheduler 64 pending 容量 → 恰 17 条可见拒绝
    //   （距离秩升序提交序 → 最远环带尾部被拒，(24,24) 为字典序末位 = 必拒）→ 拒绝块保持
    //   Absent（无正确性损失）。
    // 相 3（重请求重生成）：半径收窄到 (0,0,1) 直取被拒块 (24,24) → 重提交 → 收敛 Loaded
    //   = 被背压拒绝的 chunk 在后续沿重请求后重生成（r2018c「后续收敛」语义的生产面）。
    runLeg(QStringLiteral("r2024c real-thread harvest with the dual-face same-beat contract and"
        " visible backpressure (eight jobs execute on the background worker thread - observed"
        " execution thread equals the worker thread and differs from the calling thread that"
        " performs the main-thread adoption - with exact one-outcome-and-one-data-per-job"
        " pairing: eight submissions, eight cumulative outcomes, eight cumulative adoptions, and"
        " both faces fully drained after the harvest beat with zero outcome backlog and zero"
        " result-data backlog; a transient 9x9 window then overloads the 64-slot pending"
        " capacity producing exactly seventeen visible rejections whose farthest dictionary-last"
        " chunk stays Absent untouched, and a narrowed radius re-request regenerates that"
        " rejected chunk to Loaded, proving rejection costs no correctness and later edges"
        " converge)"), [&]() {
        bool ok = true;
        QString diag;

        World ws = makeSparse();
        GameSession gs(ws);
        gs.configureStreamingRadii(1, 1, 2);
        ok = ok && gs.streamingActive();

        // 相 1：位置沿序列（模拟玩家走查入口——floorDiv16 沿已由 r2024b 真 PlayerController 面
        // 承证，本腿直接喂沿序列保时序确定）。radius 0 构造预生成中心 chunk (2,2) → 窗内余
        // 8 请求（精确计数面 = 8 jobs → 8 outcome → 8 数据）。
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded(gs, ws, centerKeys, 30000, d1);
        ok = ok && c1;
        if (!c1)
            diag += d1;
        gs.stepTick(0.11); // 追加一拍：保证最后完成批次的收割 + 双面排干在同一拍内发生

        // 线程身份（r2012a 先例）：执行线程 == 工作线程 ≠ 调用者线程；落位（adopt）发生在
        // 调用者线程 = 本腿主线程（convergeLoaded 的 stepTick 栈内）。
        const BackgroundGenerationWorker *wrk = gs.streamWorker();
        const std::thread::id mainTid = std::this_thread::get_id();
        bool threadOk = wrk != nullptr && wrk->executedCount() >= 8
            && wrk->observedExecThreadId() == wrk->workerThreadId()
            && wrk->workerThreadId() != mainTid;
        ok = ok && threadOk;
        if (!threadOk)
            diag += QStringLiteral("[thread exec=%1 obs==tid=%2 tid!=main=%3] ")
                        .arg(wrk ? int(wrk->executedCount()) : -1)
                        .arg(wrk && wrk->observedExecThreadId() == wrk->workerThreadId())
                        .arg(wrk && wrk->workerThreadId() != mainTid);

        // #7 同拍配对精确性（每 job 恰一数据一条 outcome；单别名受控场景 → 计数恰等）。
        const bool pairOk = gs.streamDriver()->stats().submitted == 8
            && gs.streamingOutcomeCount() == 8 && gs.streamingAdoptedCount() == 8
            && gs.streamDriver()->outcomeCount() == 0 && wrk->resultDataCount() == 0;
        ok = ok && pairOk;
        if (!pairOk)
            diag += QStringLiteral("[pair sub=%1 out=%2 adop=%3 qOut=%4 qData=%5] ")
                        .arg(gs.streamDriver()->stats().submitted)
                        .arg(gs.streamingOutcomeCount())
                        .arg(gs.streamingAdoptedCount())
                        .arg(gs.streamDriver()->outcomeCount())
                        .arg(wrk->resultDataCount());

        // 相 2：背压（9×9 窗 81 请求 > 64 pending 容量 → 恰 17 拒绝；距离秩升序 → 字典序末位
        //   的最远环块 (24,24) 必拒，中心 (20,20) 必受理）。
        const int rejectedBefore = gs.streamDriver()->rejectedSubmissions();
        gs.configureStreamingRadii(4, 4, 6);
        gs.notePlayerChunk(20, 20);
        gs.stepTick(0.11); // 一拍：decide + submit + 交接（收割留后续拍）
        const int rejectedDelta = gs.streamDriver()->rejectedSubmissions() - rejectedBefore;
        const bool bpOk = rejectedDelta == 17
            && ws.chunks().lifecycleAt(24, 24) == ChunkLifecycle::Absent
            && gs.streamDriver()->stats().submitted > 64;
        ok = ok && bpOk;
        if (!bpOk)
            diag += QStringLiteral("[bp rej=%1 far=%2 sub=%3] ")
                        .arg(rejectedDelta)
                        .arg(int(ws.chunks().lifecycleAt(24, 24)))
                        .arg(gs.streamDriver()->stats().submitted);

        // 相 3：重请求重生成——半径收窄直取被拒块：重提交（重入沿）→ 收敛 Loaded。
        gs.configureStreamingRadii(0, 0, 1);
        gs.notePlayerChunk(24, 24);
        QVector<QPair<int, int>> farKey{ { 24, 24 } };
        QString d3;
        const bool c3 = convergeLoaded(gs, ws, farKey, 45000, d3);
        ok = ok && c3;
        if (!c3)
            diag += d3;

        // 收尾账面：全部受理 job 排干（pending 零 + 双面零积压——无正确性损失）。
        for (int i = 0; i < 400 && (gs.streamDriver()->pendingJobCount() > 0
                                    || wrk->resultDataCount() > 0);
             ++i) {
            gs.stepTick(0.11);
            QThread::msleep(2);
        }
        const bool drained = gs.streamDriver()->pendingJobCount() == 0
            && gs.streamDriver()->outcomeCount() == 0 && wrk->resultDataCount() == 0;
        ok = ok && drained;
        if (!drained)
            diag += QStringLiteral("[drain pend=%1 qOut=%2 qData=%3] ")
                        .arg(gs.streamDriver()->pendingJobCount())
                        .arg(gs.streamDriver()->outcomeCount())
                        .arg(wrk->resultDataCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2024c real-thread harvest: the background worker executes on"
                             " its own thread while the calling thread adopts, every completed"
                             " job yields exactly one outcome and one data buffer consumed in"
                             " the same harvest beat with both faces fully drained, the 9x9"
                             " overload produces exactly seventeen visible rejections with the"
                             " dictionary-last far chunk untouched at Absent, and the narrowed"
                             " re-request regenerates it to Loaded with a fully drained ledger"
                          << (ok ? QString() : diag);
    });

    // ── r2024d：结构钉 ──────────────────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2024d structure pins (comment-stripped source pins hold the W2"
        " wiring on its authorized seams: the session gate constructs driver and worker only"
        " for sparse worlds, the tick-tail beat feeds the position edge, pumps once and drains"
        " both the outcome face and the data face inside the same function body with the data"
        " adoption as the single adopt call site, the world-side adopt materializes through the"
        " r2012 guarded application plus the Loaded promotion and the main-thread population"
        " replay with the in-flight neighbor guard, QML stays free of every W2 token, and"
        " worldstore stays blind to the streaming seams)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };

        // ① gamesession.h 正面钉：通电门 + tick 尾泵拍 + #7 双面同拍 + 单一 adopt 落点。
        const QStringList missGs = pinSet(
            srcRoot + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("W2 sparse-only construction gate", "if (m_world.isSparse())", 1),
                SrcPin("W2 driver constructed", "ChunkStreamDriver::streamingWithDefaultRadii()", 1),
                SrcPin("W2 worker constructed", "std::make_unique<BackgroundGenerationWorker>(", 1),
                SrcPin("W2 r2011 lifecycle sink first use", "attachLifecycleSink(", 1),
                SrcPin("W2 slot ensure seam wired", "setSlotEnsure(", 1),
                SrcPin("W2 fixed inert early-out", "if (!m_streamDriver)", 1),
                SrcPin("W2 position edge feed", "m_streamDriver->onPlayerChunk(", 1),
                SrcPin("W2 #7 outcome face", "while (m_streamDriver->takeOutcome(outcome))", 1),
                SrcPin("W2 #7 data face", "while (m_streamWorker->takeResultData(data))", 1),
                SrcPin("W2 single adopt call site", "m_world.adoptGeneratedChunk(", 1),
            });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② #7 双面同拍函数域钉：两面 while 循环必须同居于 pumpStreamingTick 单一函数体
        //    （同拍并列的文本面——r2023d 函数域手工抽体先例）。
        QFile gf(srcRoot + QStringLiteral("/Game/gamesession.h"));
        QString beatBody;
        if (gf.open(QIODevice::ReadOnly)) {
            const QString src = QString::fromUtf8(gf.readAll());
            const qsizetype b = src.indexOf(QStringLiteral("inline void GameSession::pumpStreamingTick()"));
            if (b >= 0)
                beatBody = src.mid(b);
        }
        const bool sameBeat = beatBody.size() > 0
            && beatBody.contains(QStringLiteral("takeOutcome(outcome)"))
            && beatBody.contains(QStringLiteral("takeResultData(data)"))
            && beatBody.indexOf(QStringLiteral("takeOutcome(outcome)"))
                < beatBody.indexOf(QStringLiteral("takeResultData(data)"));
        ok = ok && sameBeat;
        if (!sameBeat)
            diag += QStringLiteral("[same-beat body=%1] ").arg(beatBody.size());

        // ③ world.cpp 正面钉：adopt 落位链（r2012 守卫入口 + ③晋升 + population 主线程 + 列
        //    种子光）与在途邻居守卫（W2 流式在途槽不复用脚手架拆卸）。
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("adopt entry", "bool World::adoptGeneratedChunk(int cx, int cz, const GeneratedChunkData &data)", 1),
                SrcPin("adopt fixed gate", "if (m_chunks.mode() != WorldMode::Sparse)", 2),
                SrcPin("adopt idempotence", "if (m_chunks.chunkMaterialized(cx, cz))", 1),
                SrcPin("adopt guarded application", "applyGeneratedChunkData(m_chunks, data)", 1),
                SrcPin("adopt Loaded promotion", "setChunkLifecycle(cx, cz, ChunkLifecycle::Loaded);", 2),
                SrcPin("adopt main-thread population", "sparsePopulateChunk(cx, cz);", 2),
                SrcPin("adopt column light reflood", "refloodBox(cx * TerrainGen::kChunkSize", 2),
                SrcPin("slot ensure entry", "bool World::ensureStreamingChunkSlot(int cx, int cz)", 1),
                SrcPin("in-flight neighbor guard",
                    "inflightLife == ChunkLifecycle::Loading || inflightLife == ChunkLifecycle::Generated", 1),
            });
        for (const QString &m : missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ generationjob / driver 预留面接线钉：setChunks 缝与槽位缝调用点（r2011 预留面首用
        //    的组件侧文本）。
        const QStringList missGj = pinSet(
            srcRoot + QStringLiteral("/World/generationjob.h"), {
                SrcPin("scheduler chunk hook setter", "void setChunks(ChunkManager *chunks)", 1),
            });
        const QStringList missDrv = pinSet(
            srcRoot + QStringLiteral("/World/chunkstreamdriver.h"), {
                SrcPin("driver sink forwarding", "m_jobs.setChunks(&chunks)", 1),
                SrcPin("driver slot ensure seam", "m_slotEnsure(r.cx, r.cz)", 1),
                SrcPin("driver default radii factory", "static ChunkStreamDriver streamingWithDefaultRadii()", 1),
            });
        for (const QString &m : missGj + missDrv) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ QML 零触碰反探（miss 非空 = 合规）：W2 新记号一个都不入 QML——位置源是 C++ 直连面。
        const QString qml = srcRoot + QStringLiteral("/ui/Main.qml");
        const bool qmlOk = forbiddenAbsent(qml, "playerChunkChanged")
            && forbiddenAbsent(qml, "notePlayerChunk")
            && forbiddenAbsent(qml, "streamingActive")
            && forbiddenAbsent(qml, "adoptGeneratedChunk");
        ok = ok && qmlOk;
        if (!qmlOk)
            diag += QStringLiteral("[qml] ");

        // ⑥ worldstore 禁触反探（miss 非空 = 合规）：sparse 持久化 = W3/W5，本单零触碰。
        const bool storeOk = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                                 "adoptGeneratedChunk")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                "ensureStreamingChunkSlot")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"),
                "adoptGeneratedChunk")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"),
                "streamingLifecycleSink");
        ok = ok && storeOk;
        if (!storeOk)
            diag += QStringLiteral("[store] ");

        // ⑦ playercontroller 位置源钉：floorDiv16 换格检测单一落点（ChunkKey::fromWorld 权威
        //    + 哨兵起始沿 + setWorld 重置）。
        const QStringList missPc = pinSet(
            srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("chunk edge detection", "ChunkKey::fromWorld(int(std::floor(double(m_pos.x())))", 1),
                SrcPin("edge signal emission", "emit playerChunkChanged(pc.cx, pc.cz);", 1),
                SrcPin("sentinel reset on world swap",
                    "m_lastChunkCx = std::numeric_limits<int>::min();", 1),
            });
        for (const QString &m : missPc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2024d structure pins: the W2 wiring stays on its authorized"
                             " seams (sparse-only session gate, tick-tail beat with both faces"
                             " drained in one function body and a single adopt call site,"
                             " world-side adopt on the guarded application + Loaded promotion +"
                             " main-thread population chain with the in-flight neighbor guard,"
                             " QML and worldstore free of every streaming token)"
                          << (ok ? QString() : diag);
    });
}
