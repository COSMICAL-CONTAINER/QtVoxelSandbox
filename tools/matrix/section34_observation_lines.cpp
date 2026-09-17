#include "matrix_helpers.h"

#include "chunklifecycle.h"    // 被测面：六态类型（驱逐收敛断言）
#include "chunkstreamdriver.h" // 被测面：驱动器 stats 只读观测（逐项恒等的权威侧）
#include "gamesession.h"       // 被测面：dt 钳制分域读面 + F3 stream 行推送面

#include <QElapsedTimer>
#include <QThread> // 收敛轮询节拍（deadline 有界防 flake）

// t1059 P5 观测前置 探针段（3 腿；filter 词 r2033；矩阵 664→664+N）。置尾先例沿用（接
// section33，runAll 末执行）。任务契约（docs/refactor-plan-2026-09-08.md §29.4/§29.5 风险登记
// 「生成风暴……F3 生成排队/帧率」的兑现前置 + review0916 Info 负/NaN 丢弃无计数）：
//   「F3 流式观测行 + fixed 恒零」→ r2033a（fixed 世界静默窗报告含全零 stream 行[行恒在、
//     格式可 grep——与 worker 列恒 0 先例同门] + 行格式解析钉 + 拼行层/推送面源码正面钉）；
//   「流式行计数与权威观测面逐项恒等」→ r2033b（sparse 会话走流式通路小场景四窗：初载/
//     取消走查/网格收割/驱逐落盘——每窗 flush 后 stream 行逐字段 == 权威面窗口增量
//     [driver stats 三计数差分 / 会话收割拍三计数 / 驱逐轨迹五分域]，场景精确值钉
//     [sub==8 初载、can==9 走离、evP==1 evE==4 evS==4 驱逐]）；
//   「dt 负/NaN 分域计数」→ r2033c（负/NaN/超界三类 dt → 各计数精确[旧 >1s 计数回归柱
//     不动 + 负/NaN 新计数分域] + 丢弃不欠账行为零变化墙 + 读面/判据源码钉）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘 stream 行 sub 域推送（gamesession.h 泵拍 addCount("streamSub",…) 调用体 if (false)
//     前缀）→ 声明红面 {r2033b} 单腿（行字段 sub=0 ≠ 权威 stats 增量 8，恒等断言即被摘语义
//     本体）。r2033a 不误伤（全零行仍全零——sub 恒 0 与 fixed 零值墙同值；源码钉 needle
//     `addCount("streamSub",` 在 if (false) 前缀下文本仍在场）；r2033c 不误伤（dt 域独立）。
//   NEG-2 摘负/NaN 分域计数（stepTick 钳制分支两个 ++ 计数语句各自 if (false) 前缀——丢弃
//     语义零变化只摘可见性）→ 声明红面 {r2033c} 单腿（负/NaN 计数恒 0 ≠ 精确值 1/1/2）。
//     r2033c 源码钉 needle `++m_droppedNanDts;`/`++m_droppedNegativeDts;` 前缀下仍在场不误伤；
//     r2007 超界计数族不误伤（>1s 旧分支零触碰）；r2033a/b 不误伤（无 dt 域断言）。
// 数据流（头注释立证 = gamesession.h t1059 段）：本壳在事件落账点把增量推入 FrameProfiler
//   计数桶（键前缀 stream*），flush 拼行读桶清窗——本段以「flush 界定的窗口」为断言单元，
//   每窗先 flush 清残留（前段腿推送不得漏入）、再驱动场景、再 flush 出报告解析。
// 时长控制：腿内 sparse 世界 spawnPreGenerateRadius=0 + 半径压小（(1,1,2)/(1,1,3)）+ 收敛
//   轮询 deadline 有界（防 flake 不挂死）；临时库 fresh + 用后即删（pid 键名，绝不触 saves/）。
void MatrixRun::section34_observation_lines()
{
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42; // sparse 核心域（5×5 chunk，流式段同族）
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk）

    FrameProfiler *fp = FrameProfiler::instance();

    // stream 行字段下标（与拼行层字段序一致：s sub can rej out adopt mesh ev[P E S F R]）。
    enum StreamField {
        kSfS = 0, kSfSub, kSfCan, kSfRej, kSfOut, kSfAdopt, kSfMesh,
        kSfEvP, kSfEvE, kSfEvS, kSfEvF, kSfEvR, kSfN
    };
    // 报告 → stream 行字段解析（行恒在：找不到/不可解析都是响亮失败——格式正面钉的承载面）。
    const auto parseStreamLine = [](const QString &report, qint64 (&f)[kSfN], QString &err) {
        const QStringList lines = report.split(QLatin1Char('\n'));
        static const QRegularExpression re(QStringLiteral(
            "^stream s=(\\d+) sub=(\\d+) can=(\\d+) rej=(\\d+) out=(\\d+) adopt=(\\d+)"
            " mesh=(\\d+) ev\\[P=(\\d+) E=(\\d+) S=(\\d+) F=(\\d+) R=(\\d+)\\]$"));
        for (const QString &l : lines) {
            const QString t = l.trimmed();
            if (!t.startsWith(QLatin1String("stream ")))
                continue;
            const auto m = re.match(t);
            if (!m.hasMatch()) {
                err = QStringLiteral("unparsable stream line: %1").arg(t);
                return false;
            }
            for (int i = 0; i < kSfN; ++i)
                f[i] = m.captured(i + 1).toLongLong();
            return true;
        }
        err = QStringLiteral("no stream line in report");
        return false;
    };

    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0; // 零预生成：全量走流式链（推送面全暴露）
        return World(sp);
    };
    // 收敛轮询：泵 tick 直到谓词成立或超时（真线程收割节奏不定——deadline 有界防 flake；
    //   每轮 stepTick(0.11) = 恰 1 整 tick = 1 个 tick 尾流式泵拍 = 1 组推送）。
    const auto converge = [&](GameSession &gs, const std::function<bool()> &done,
                              int deadlineMs, QString &diag) {
        QElapsedTimer t;
        t.start();
        while (!done()) {
            if (t.elapsed() > deadlineMs) {
                diag += QStringLiteral("[converge-timeout %1ms] ").arg(deadlineMs);
                return false;
            }
            gs.stepTick(0.11);
            QThread::msleep(2);
        }
        return true;
    };
    const auto loadedAll = [](World &w, const QVector<QPair<int, int>> &keys) {
        for (const auto &k : keys)
            if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded)
                return false;
        return true;
    };
    // 驱逐轨迹分域增量（kind：0=PersistOk 1=PersistFail 2=EdgeEvicting 3=EdgeAbsent
    // 4=TransitionRejected——从基线下标起数，与 ev 五域推送同源对账）。
    const auto traceKindDelta = [](const QVector<GameSession::EvictionTraceEvent> &tr,
                                   int kind, int from) {
        int n = 0;
        for (int i = from; i < tr.size(); ++i)
            if (tr[i].kind == kind)
                ++n;
        return n;
    };
    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰——r2015/r2025 先例同门）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2033_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };

    // ── r2033a：fixed 世界全零行 + 行格式钉 ─────────────────────────────────────────────
    // fixed 世界静默窗：stream 行恒在且全零（行恒在零值 vs 条件显行的选型立证见
    // gamesession.h t1059 段——与 worker 列恒 0 先例同门）+ 字段解析器可整行解析（格式正面
    // 钉）+ 拼行层/推送面源码正面钉（剥注释）。既有 t1007/t1020 F3 行族零触碰由各自 filter
    // 腿回归（本段不重复）。
    runLeg(QStringLiteral("r2033a fixed-world zero wall (the F3 report gains an always-present"
        " stream observation line: on a fixed-world quiescent window every field reads zero"
        " exactly as formatted - s=0 sub=0 can=0 rej=0 out=0 adopt=0 mesh=0 ev[P=0 E=0 S=0"
        " F=0 R=0] - the line parses field-by-field through the report formatter, the session"
        " stays fully inert across pumped ticks, and comment-stripped source pins hold the"
        " line assembly on the win-line family plus every push site [heartbeat, the three"
        " driver-stat deltas, the three harvest-beat deltas and the five eviction-trace"
        " domains])"), [&]() {
        bool ok = true;
        QString diag;

        fp->flush(); // 清残留窗（此前各段腿的推送不得漏入本窗）

        World w; // fixed（默认构造 + setter incantation）
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);

        GameSession gs(w);
        const bool inertOk = !gs.streamingActive() && gs.streamDriver() == nullptr
            && gs.streamingOutcomeCount() == 0 && gs.streamingAdoptedCount() == 0
            && gs.meshHarvestedCount() == 0 && gs.droppedDtCount() == 0
            && gs.droppedNegativeDtCount() == 0 && gs.droppedNanDtCount() == 0;
        ok = ok && inertOk;
        if (!inertOk)
            diag += QStringLiteral("[inert] ");

        for (int i = 0; i < 6; ++i)
            gs.stepTick(0.11); // 泵拍对 fixed 恒零动作（推送面首行不可达——D2 同门）

        fp->tickFrame();
        fp->flush(); // 静默窗报告
        const QString rep = fp->report();
        static const char *kZeroLine = "stream s=0 sub=0 can=0 rej=0 out=0 adopt=0 mesh=0"
                                       " ev[P=0 E=0 S=0 F=0 R=0]";
        const bool zeroOk = rep.contains(QLatin1String(kZeroLine));
        ok = ok && zeroOk;
        if (!zeroOk)
            diag += QStringLiteral("[zero-line missing] report=%1").arg(rep);

        // 行格式解析钉（同一行逐字段可解析——格式漂移即响红）。
        qint64 f[kSfN] = { 0 };
        QString err;
        bool fmtOk = parseStreamLine(rep, f, err);
        for (int i = 0; i < kSfN && fmtOk; ++i)
            fmtOk = f[i] == 0;
        ok = ok && fmtOk;
        if (!fmtOk)
            diag += QStringLiteral("[parse %1]").arg(err);

        // 源码正面钉（剥注释）：拼行层（键读面 ×11 + 行标 + win 行族挂载位）+ 推送面全站点。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missFp = pinSet(
            srcRoot + QStringLiteral("/Core/frameprofiler.cpp"), {
                SrcPin("stream line label", "QStringLiteral(\"stream s=\")", 1),
                SrcPin("stream sub read", "streamCnt(\"streamSub\")", 1),
                SrcPin("stream can read", "streamCnt(\"streamCan\")", 1),
                SrcPin("stream rej read", "streamCnt(\"streamRej\")", 1),
                SrcPin("stream out read", "streamCnt(\"streamOut\")", 1),
                SrcPin("stream adopt read", "streamCnt(\"streamAdopt\")", 1),
                SrcPin("stream mesh read", "streamCnt(\"streamMesh\")", 1),
                SrcPin("stream evP read", "streamCnt(\"streamEvP\")", 1),
                SrcPin("stream evE read", "streamCnt(\"streamEvE\")", 1),
                SrcPin("stream evS read", "streamCnt(\"streamEvS\")", 1),
                SrcPin("stream evF read", "streamCnt(\"streamEvF\")", 1),
                SrcPin("stream evR read", "streamCnt(\"streamEvR\")", 1),
                SrcPin("stream line after win on the report family",
                       "arg(tickLine, winLine, streamLine, cntLine, frameLine, frame2Line, mobLine)", 1),
            });
        const QStringList missGs = pinSet(
            srcRoot + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("push heartbeat", "count(\"streamPump\")", 1),
                SrcPin("push sub delta", "addCount(\"streamSub\",", 1),
                SrcPin("push can delta", "addCount(\"streamCan\",", 1),
                SrcPin("push rej delta", "addCount(\"streamRej\",", 1),
                SrcPin("push out delta", "addCount(\"streamOut\", outcomesDrained)", 1),
                SrcPin("push adopt delta", "addCount(\"streamAdopt\", adoptedDrained)", 1),
                SrcPin("push mesh delta", "addCount(\"streamMesh\", meshDrained)", 1),
                SrcPin("push evP", "count(\"streamEvP\")", 1),
                SrcPin("push evF", "count(\"streamEvF\")", 1),
                SrcPin("push evE", "count(\"streamEvE\")", 1),
                SrcPin("push evS", "count(\"streamEvS\")", 1),
                SrcPin("push evR", "count(\"streamEvR\")", 1),
            });
        for (const QString &m : missFp + missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2033a fixed zero wall: the F3 report carries an always-present"
                             " stream observation line that reads all-zero on a fixed-world"
                             " quiescent window (line-always-present selection, zero-value"
                             " contract identical in kind to the win-line worker column), the"
                             " line is fully parseable field-by-field, and source pins hold"
                             " the assembly on the win-line family plus every session push"
                             " site" << (ok ? QString() : diag);
    });

    // ── r2033b：流式行计数与权威观测面逐项恒等 ──────────────────────────────────────────
    // 四窗结构（每窗 = flush 清窗 → 场景 → flush 出报告 → 逐字段恒等断言）：
    //   窗 A 初载：3×3 窗收敛（sub==8/out==adopt==8 精确——中心预生成同 r2024c 相 1 口径）；
    //   窗 B 取消走查：走离拍提交 9 → 跳回拍半径取消恰 9（r2024b 序）→ 被取消窗收敛；
    //   窗 B2 网格收割：几何桥提交 → tick 尾收割拍（mesh 域 == 收割计数增量且 ≥1）；
    //   窗 E 驱逐：会话 2（真临时库）编辑 (3,2) → 走离拍同步驱逐恰 4（evP==1 evE==4 evS==4
    //     evF==evR==0——r2025b 同址同径）。
    // 恒等口径：行字段 == 权威面窗口增量（stats 三计数对窗首快照差分 / 收割拍三计数 / 驱逐
    //   轨迹五分域自基线下标起数）——聚合正确性的承重断言；精确值为场景确定性副钉。
    runLeg(QStringLiteral("r2033b streaming-window identity wall (four flush-delimited windows"
        " over real streaming sessions: window A materializes the 3x3 window with the line"
        " reading sub=8 out=8 adopt=8 exactly; window B walks away and back so the radius"
        " cancellation step accounts exactly nine cancels and the abandoned window still"
        " converges; window B2 submits a mesh through the World bridge and the harvest beat"
        " delivers it with the mesh field tracking the harvest count; window E on a second"
        " session with a real temp store evicts exactly the four out-of-radius chunks with"
        " one persist - ev[P=1 E=4 S=4 F=0 R=0]; in every window every line field equals the"
        " authoritative surface's window delta [driver submitted/canceled/rejected diffs,"
        " session outcome/adopt/mesh harvest counters, eviction-trace domain counts] and the"
        " leading s flag reads 1)"), [&]() {
        bool ok = true;
        QString diag;
        qint64 f[kSfN] = { 0 };
        QString err;

        // ── 会话 1（窗 A / B / B2）─────────────────────────────────────────────────────
        World ws = makeSparse();
        GameSession gs(ws);
        gs.configureStreamingRadii(1, 1, 2); // 腿内压小半径（时长控制；生产 = P1 默认）
        ok = ok && gs.streamingActive();

        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QVector<QPair<int, int>> awayKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 5; cx <= 7; ++cx)
                awayKeys.append({ cx, cz });

        // 窗 A：初载 3×3（中心 (2,2) 构造期预生成 → 恰 8 请求；r2024c 相 1 同窗口径）。
        fp->flush(); // 清残留窗
        const auto stA0 = gs.streamDriver()->stats();
        const qint64 outA0 = gs.streamingOutcomeCount();
        const qint64 adoptA0 = gs.streamingAdoptedCount();
        const qint64 meshA0 = gs.meshHarvestedCount();
        const int trA0 = gs.evictionTrace().size();
        gs.notePlayerChunk(2, 2);
        ok = ok && converge(gs, [&]() { return loadedAll(ws, centerKeys); }, 45000, diag);
        gs.stepTick(0.11); // 追加收割拍：末批 outcome/adopt 全排干（r2024c 同门）
        fp->flush();
        bool winOk = parseStreamLine(fp->report(), f, err);
        const auto stA1 = gs.streamDriver()->stats();
        winOk = winOk && f[kSfS] == 1
            && f[kSfSub] == stA1.submitted - stA0.submitted && f[kSfSub] == 8
            && f[kSfCan] == stA1.canceled - stA0.canceled && f[kSfCan] == 0
            && f[kSfRej] == stA1.rejected - stA0.rejected && f[kSfRej] == 0
            && f[kSfOut] == gs.streamingOutcomeCount() - outA0 && f[kSfOut] == 8
            && f[kSfAdopt] == gs.streamingAdoptedCount() - adoptA0 && f[kSfAdopt] == 8
            && f[kSfMesh] == gs.meshHarvestedCount() - meshA0 && f[kSfMesh] == 0
            && f[kSfEvP] == traceKindDelta(gs.evictionTrace(), 0, trA0) && f[kSfEvP] == 0
            && f[kSfEvF] == traceKindDelta(gs.evictionTrace(), 1, trA0) && f[kSfEvF] == 0
            && f[kSfEvE] == traceKindDelta(gs.evictionTrace(), 2, trA0) && f[kSfEvE] == 0
            && f[kSfEvS] == traceKindDelta(gs.evictionTrace(), 3, trA0) && f[kSfEvS] == 0
            && f[kSfEvR] == traceKindDelta(gs.evictionTrace(), 4, trA0) && f[kSfEvR] == 0;
        ok = ok && winOk;
        if (!winOk)
            diag += QStringLiteral("[winA %1 sub=%2 can=%3 rej=%4 out=%5 adopt=%6 mesh=%7"
                                   " raw=%8/%9/%10 ev=%11/%12/%13/%14/%15] ")
                        .arg(err, QString::number(f[kSfSub]), QString::number(f[kSfCan]),
                             QString::number(f[kSfRej]), QString::number(f[kSfOut]),
                             QString::number(f[kSfAdopt]), QString::number(f[kSfMesh]))
                        .arg(stA1.submitted - stA0.submitted)
                        .arg(gs.streamingOutcomeCount() - outA0)
                        .arg(gs.streamingAdoptedCount() - adoptA0)
                        .arg(f[kSfEvP]).arg(f[kSfEvF]).arg(f[kSfEvE]).arg(f[kSfEvS])
                        .arg(f[kSfEvR]);

        // 窗 B：取消走查（走离拍提交 9 → 跳回拍半径取消恰 9 → 被取消窗经数据面收敛）。
        fp->flush();
        const auto stB0 = gs.streamDriver()->stats();
        const qint64 outB0 = gs.streamingOutcomeCount();
        const qint64 adoptB0 = gs.streamingAdoptedCount();
        const qint64 meshB0 = gs.meshHarvestedCount();
        const int trB0 = gs.evictionTrace().size();
        gs.notePlayerChunk(6, 2);
        gs.stepTick(0.11); // 走离拍：提交 + 交接 (5..7)×(1..3) 窗
        gs.notePlayerChunk(2, 2);
        gs.stepTick(0.11); // 跳回拍：半径取消步执行（先于本拍收割——r2024b 同序）
        ok = ok && converge(gs, [&]() { return loadedAll(ws, awayKeys); }, 45000, diag);
        gs.stepTick(0.11);
        fp->flush();
        winOk = parseStreamLine(fp->report(), f, err);
        const auto stB1 = gs.streamDriver()->stats();
        winOk = winOk && f[kSfS] == 1
            && f[kSfSub] == stB1.submitted - stB0.submitted && f[kSfSub] == 9
            && f[kSfCan] == stB1.canceled - stB0.canceled && f[kSfCan] == 9
            && f[kSfRej] == stB1.rejected - stB0.rejected
            && f[kSfOut] == gs.streamingOutcomeCount() - outB0
            && f[kSfAdopt] == gs.streamingAdoptedCount() - adoptB0
            && f[kSfMesh] == gs.meshHarvestedCount() - meshB0 && f[kSfMesh] == 0
            && f[kSfEvP] == traceKindDelta(gs.evictionTrace(), 0, trB0)
            && f[kSfEvF] == traceKindDelta(gs.evictionTrace(), 1, trB0)
            && f[kSfEvE] == traceKindDelta(gs.evictionTrace(), 2, trB0)
            && f[kSfEvS] == traceKindDelta(gs.evictionTrace(), 3, trB0)
            && f[kSfEvR] == traceKindDelta(gs.evictionTrace(), 4, trB0);
        ok = ok && winOk;
        if (!winOk)
            diag += QStringLiteral("[winB %1 sub=%2/%3 can=%4/%5 rej=%6 out=%7/%8 adopt=%9/%10"
                                   " ev=%11/%12/%13/%14/%15] ")
                        .arg(err, QString::number(f[kSfSub]),
                             QString::number(stB1.submitted - stB0.submitted),
                             QString::number(f[kSfCan]),
                             QString::number(stB1.canceled - stB0.canceled),
                             QString::number(f[kSfRej]), QString::number(f[kSfOut]),
                             QString::number(gs.streamingOutcomeCount() - outB0),
                             QString::number(f[kSfAdopt]),
                             QString::number(gs.streamingAdoptedCount() - adoptB0))
                        .arg(f[kSfEvP]).arg(f[kSfEvF]).arg(f[kSfEvE]).arg(f[kSfEvS])
                        .arg(f[kSfEvR]);

        // 窗 B2：网格收割（几何桥提交 → tick 尾收割拍交付；mesh 域 == 收割计数增量 ≥1）。
        fp->flush();
        const auto stC0 = gs.streamDriver()->stats();
        const qint64 outC0 = gs.streamingOutcomeCount();
        const qint64 adoptC0 = gs.streamingAdoptedCount();
        const qint64 meshC0 = gs.meshHarvestedCount();
        const int trC0 = gs.evictionTrace().size();
        ChunkGeometry gM;
        gM.setWorld(&ws);
        gM.setCx(2);
        gM.setCz(2);
        gM.refreshMesh(); // 桥提交（真执行器；r2026b 同门形态）
        ok = ok && converge(gs, [&]() { return gs.meshHarvestedCount() > meshC0; }, 45000, diag);
        gs.stepTick(0.11);
        fp->flush();
        winOk = parseStreamLine(fp->report(), f, err);
        const auto stC1 = gs.streamDriver()->stats();
        winOk = winOk && f[kSfS] == 1
            && f[kSfSub] == stC1.submitted - stC0.submitted
            && f[kSfCan] == stC1.canceled - stC0.canceled
            && f[kSfRej] == stC1.rejected - stC0.rejected
            && f[kSfOut] == gs.streamingOutcomeCount() - outC0
            && f[kSfAdopt] == gs.streamingAdoptedCount() - adoptC0
            && f[kSfMesh] == gs.meshHarvestedCount() - meshC0 && f[kSfMesh] >= 1
            && f[kSfEvP] == traceKindDelta(gs.evictionTrace(), 0, trC0)
            && f[kSfEvF] == traceKindDelta(gs.evictionTrace(), 1, trC0)
            && f[kSfEvE] == traceKindDelta(gs.evictionTrace(), 2, trC0)
            && f[kSfEvS] == traceKindDelta(gs.evictionTrace(), 3, trC0)
            && f[kSfEvR] == traceKindDelta(gs.evictionTrace(), 4, trC0);
        ok = ok && winOk;
        if (!winOk)
            diag += QStringLiteral("[winB2 %1 mesh=%2/%3] ").arg(err)
                        .arg(f[kSfMesh]).arg(gs.meshHarvestedCount() - meshC0);

        // ── 会话 2（窗 E）：驱逐五分域（真临时库 + r2025b 同址同径）────────────────────
        const QString db = tempDb("evict");
        QFile::remove(db); // fresh
        World ws2 = makeSparse();
        GameSession gs2(ws2);
        gs2.configureStreamingRadii(1, 1, 3); // gen 窗 3×3 + scan 窗 7×7（驱逐 annulus 非空）
        const bool boundOk = gs2.bindChunkEditsStore(db) && gs2.chunkEditsStore() != nullptr;
        ok = ok && boundOk;
        if (!boundOk)
            diag += QStringLiteral("[bind] ");
        gs2.notePlayerChunk(2, 2);
        ok = ok && converge(gs2, [&]() { return loadedAll(ws2, centerKeys); }, 45000, diag);

        // 编辑 chunk (3,2) 列 (56,44)（r2025b 同址：顶面放 Stone——破/放目标显式验证）。
        const int etX = 56, etZ = 44;
        const int etTop = ws2.heightmapAt(etX, etZ);
        const bool editPreOk = etTop >= 0 && etTop + 1 < kH && ws2.blockAt(etX, etTop, etZ) != 0
            && ws2.blockAt(etX, etTop + 1, etZ) == 0;
        ok = ok && editPreOk && ws2.setBlock(etX, etTop + 1, etZ, BR::Stone)
            && ws2.chunkHasUnsavedEdits(3, 2);
        if (!editPreOk)
            diag += QStringLiteral("[editPre top=%1] ").arg(etTop);

        fp->flush(); // 窗 E 起点（驱逐拍 + 其新窗提交恰为本窗全部活动）
        const auto stE0 = gs2.streamDriver()->stats();
        const qint64 outE0 = gs2.streamingOutcomeCount();
        const qint64 adoptE0 = gs2.streamingAdoptedCount();
        const qint64 meshE0 = gs2.meshHarvestedCount();
        const int trE0 = gs2.evictionTrace().size();
        gs2.notePlayerChunk(5, 5);
        gs2.stepTick(0.11); // 驱逐拍（同步：decide → evictor → 转移缝）+ 新窗提交
        fp->flush();
        winOk = parseStreamLine(fp->report(), f, err);
        const auto stE1 = gs2.streamDriver()->stats();
        const QVector<GameSession::EvictionTraceEvent> trE = gs2.evictionTrace();
        winOk = winOk && f[kSfS] == 1
            && f[kSfEvP] == traceKindDelta(trE, 0, trE0) && f[kSfEvP] == 1
            && f[kSfEvF] == traceKindDelta(trE, 1, trE0) && f[kSfEvF] == 0
            && f[kSfEvE] == traceKindDelta(trE, 2, trE0) && f[kSfEvE] == 4
            && f[kSfEvS] == traceKindDelta(trE, 3, trE0) && f[kSfEvS] == 4
            && f[kSfEvR] == traceKindDelta(trE, 4, trE0) && f[kSfEvR] == 0
            && f[kSfSub] == stE1.submitted - stE0.submitted && f[kSfSub] == 9
            && f[kSfCan] == stE1.canceled - stE0.canceled
            && f[kSfRej] == stE1.rejected - stE0.rejected
            && f[kSfOut] == gs2.streamingOutcomeCount() - outE0
            && f[kSfAdopt] == gs2.streamingAdoptedCount() - adoptE0
            && f[kSfMesh] == gs2.meshHarvestedCount() - meshE0;
        ok = ok && winOk;
        if (!winOk)
            diag += QStringLiteral("[winE %1 ev=%2/%3/%4/%5/%6 sub=%7/%8 out=%9/%10 adopt=%11/%12"
                                   " l32=%13] ")
                        .arg(err, QString::number(f[kSfEvP]), QString::number(f[kSfEvF]),
                             QString::number(f[kSfEvE]), QString::number(f[kSfEvS]),
                             QString::number(f[kSfEvR]), QString::number(f[kSfSub]),
                             QString::number(stE1.submitted - stE0.submitted),
                             QString::number(f[kSfOut]),
                             QString::number(gs2.streamingOutcomeCount() - outE0),
                             QString::number(f[kSfAdopt]),
                             QString::number(gs2.streamingAdoptedCount() - adoptE0))
                        .arg(int(ws2.chunks().lifecycleAt(3, 2)));
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2033b streaming identity wall: across four flush-delimited"
                             " windows over real streaming sessions (initial 3x3 load with"
                             " exact eights, a walk-away/back with exactly nine radius"
                             " cancellations and full convergence, a bridge-submitted mesh"
                             " harvested on the tick tail, and a temp-store eviction of"
                             " exactly four chunks with one persist) every stream-line field"
                             " equals the authoritative surface's window delta - the three"
                             " driver stats diffs, the three session harvest counters and"
                             " the five eviction-trace domains - with the s flag live"
                          << (ok ? QString() : diag);
    });

    // ── r2033c：dt 负/NaN 分域计数 ─────────────────────────────────────────────────────
    // 负/NaN/超界三类 dt → 各计数精确（负/NaN 新计数分域互不串账；>1s 旧计数回归柱不动）+
    // 行为零变化墙（丢弃不欠账：三类丢弃后 0.11s×3 → 恰 3 tick 无追账）+ 读面/判据源码钉。
    runLeg(QStringLiteral("r2033c dt drop counters (the stepTick entry gate now accounts the"
        " two new drop domains beside the existing over-cap counter without changing any"
        " drop semantics: a negative dt and two NaN dts increment exactly their own domains"
        " [negative=1, nan=2] while the over-cap counter stays untouched by them, a 2.5s dt"
        " still increments only the over-cap counter [regression pillar], and after all"
        " three kinds of drops three normal 0.11s steps execute exactly three ticks with no"
        " catch-up debt [behavior-zero-change wall]; source pins hold both increment sites,"
        " both read faces and the NaN classification gate)"), [&]() {
        bool ok = true;
        QString diag;

        World w; // fixed（dt 域与流式无关——最小世界）
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);

        GameSession gs(w);
        const bool zeroOk = gs.droppedDtCount() == 0 && gs.droppedNegativeDtCount() == 0
            && gs.droppedNanDtCount() == 0;
        ok = ok && zeroOk;
        if (!zeroOk)
            diag += QStringLiteral("[init] ");

        // 负 dt：恰入负域（超界/NaN 域不动；零执行零 tick）。
        int r = gs.stepTick(-0.5);
        bool negOk = r == 0 && gs.tick() == 0 && gs.droppedNegativeDtCount() == 1
            && gs.droppedNanDtCount() == 0 && gs.droppedDtCount() == 0;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg r=%1 tick=%2 n=%3 nan=%4 cap=%5] ")
                        .arg(r).arg(gs.tick()).arg(gs.droppedNegativeDtCount())
                        .arg(gs.droppedNanDtCount()).arg(gs.droppedDtCount());

        // NaN dt：恰入 NaN 域（负域不动）+ 累计不覆盖。
        r = gs.stepTick(qQNaN());
        bool nanOk = r == 0 && gs.droppedNanDtCount() == 1
            && gs.droppedNegativeDtCount() == 1 && gs.droppedDtCount() == 0;
        r = gs.stepTick(qQNaN());
        nanOk = nanOk && r == 0 && gs.droppedNanDtCount() == 2
            && gs.droppedNegativeDtCount() == 1;
        ok = ok && nanOk;
        if (!nanOk)
            diag += QStringLiteral("[nan n=%1 neg=%2] ")
                        .arg(gs.droppedNanDtCount()).arg(gs.droppedNegativeDtCount());

        // 旧超界计数回归柱（> kMaxStepSecs 域不动——r2007e 家族语义原样）。
        r = gs.stepTick(2.5);
        bool capOk = r == 0 && gs.droppedDtCount() == 1 && gs.droppedNanDtCount() == 2
            && gs.droppedNegativeDtCount() == 1;
        ok = ok && capOk;
        if (!capOk)
            diag += QStringLiteral("[cap r=%1 cap=%2] ").arg(r).arg(gs.droppedDtCount());

        // 行为零变化墙：三类丢弃不欠账——0.11s×3 → 恰 3 tick（无追账无跳变）。
        int executed = 0;
        for (int i = 0; i < 3; ++i)
            executed += gs.stepTick(0.11);
        const bool debtOk = executed == 3 && gs.tick() == 3;
        ok = ok && debtOk;
        if (!debtOk)
            diag += QStringLiteral("[debt executed=%1 tick=%2] ").arg(executed).arg(gs.tick());

        // 源码钉（剥注释）：两计数语句 + 两读面 + NaN 判据门。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missGs = pinSet(
            srcRoot + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("neg dt increment", "++m_droppedNegativeDts;", 1),
                SrcPin("nan dt increment", "++m_droppedNanDts;", 1),
                SrcPin("neg dt read face",
                       "int droppedNegativeDtCount() const { return m_droppedNegativeDts; }", 1),
                SrcPin("nan dt read face",
                       "int droppedNanDtCount() const { return m_droppedNanDts; }", 1),
                SrcPin("nan classification gate", "if (qIsNaN(deltaSecs))", 1),
            });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2033c dt drop counters: negative and NaN dts now land in"
                             " their own visibility domains (negative=1, nan=2 across two"
                             " drops) without touching the over-cap counter or the drop"
                             " semantics themselves (a 2.5s dt still counts only over-cap,"
                             " and three normal steps after all three drop kinds execute"
                             " exactly three ticks with no catch-up debt); source pins hold"
                             " both increments, both read faces and the NaN gate"
                          << (ok ? QString() : diag);
    });
}
