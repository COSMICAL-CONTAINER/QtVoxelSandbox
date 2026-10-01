#include "matrix_helpers.h"

#include "backgroundgeneration.h" // 被测面：worker 诊断读面（resultDataCount / executedCount）
#include "chunklifecycle.h"       // 被测面：六态类型（收敛判据）
#include "chunkstreamdriver.h"    // 被测面：驱动器 stats / pending / outcome 只读观测
#include "gamesession.h"          // 被测面：GameSession 流式会话（t1124 数据面预算改造落点）

#include <QElapsedTimer> // 收敛 deadline（防 flake 有界——不挂死）
#include <QThread>       // 收敛轮询节流（msleep）

// t1124 流式 adopt 节流（t1123 件① 走路卡顿调研产物修复单）探针段（4 腿；filter 词 r2094；
//   矩阵 908→912）。置尾先例沿用（接 section86，runAll 末执行，rig 世界零接触——行为腿自建
//   fresh sparse 小世界 + 真线程 worker）。
//
// ── 设计裁定落地表（主控全权裁量；t1123 件① 调研数据 = 改造对象与硬数据）──────────────────
//   阻塞源（t1123 定界）：GameSession::pumpStreamingTick ④ 数据面 while(takeResultData) 无界
//     排干——worker 突发整批落一帧：入场 tick 单拍 adopt 61 = 4147.93ms、跨界 tick744 单拍
//     adopt 27 = 2322.67ms；单 adopt 主线程成本 ≈ 40-90ms（bulk memcpy + 整 chunk heightmap
//     重算 + population 窗重放 + 天光 refloodBox）。本单 bench rig 同径复现（交付前临时腿，
//     已摘除；同 rig 同路径前后对照数据入交付报告）：改造前入场 56 adopt = 4319ms / 跨界
//     9 adopt ≈ 300ms；改造后单拍峰值 89ms / 36-48ms（adoptsAtMax 恒 1）且总 adopt 恒等、
//     终态队列零。
//   修复形态 = per-tick 时间预算节流：④ 排干循环加墙钟预算（kStreamingPumpBudgetMs = 3ms，
//     建议带 2-4ms 取中值；预算计时原点 = 数据面入口，每完整落位恰 1 条后查预算——adopt
//     原子不可拆，单拍上界 = 预算 + 1×adopt）；耗尽即 break 早退，余量留 m_data 下拍续排
//     （自持缓冲持有，零丢失）。while 行本身逐字幸存（预算查检落循环体内）——r2093c 调研
//     站点锚与 r2024d 结构钉零修订（幸存复核归本段 d 腿）。
//   两条命（硬门）：①收敛不变量（t1061 livelock 史）——首条 take 前预算必未耗尽 → 深队列
//     每拍至少排 1 条 → 排空速率 ≥ 1 adopt/tick ≥ 稳态 worker 产出速率 → 队列必归零（a 腿
//     归零面 + b 腿生产预算三站走查归零面承载）；②review0916 #7 契约修订如实留痕——原
//     「两面同拍全排干」→「结果面同拍全排干（记账廉价）+ 数据面预算内排干 + 跨拍续排」，
//     队列有界性改由「worker 结果缓冲容量有限（完成量 ≤ submit 背压 kMaxQueuedTasks=64 +
//     scheduler 在途上界）+ 预算持续排空」共同保证（gamesession.h 函数头注修订段 + 本段头
//     注双落点；a 腿结果面同拍全排干行为面承载）。
//   禁绝对时间钉：腿零计时断言（机器相关脆弱）——行为面靠测试缝注入预算 0（闸门恒真 =
//     每拍恰 1 adopt 的确定性计数上界形态，t1051 语料内嵌复现体先例同门）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双摘面互不重叠；双 NEG 均编译仍绿形）──────────────
//   NEG-1 = 摘预算耗尽早退行（pumpStreamingTick ④ 循环体内 if(elapsed() >= 预算) break;
//     整行删除——while 体其余行与块注幸存，编译仍绿 = 回到无界排干旧形态）→ 恰红 = {r2094a}
//     （摘面行本腿钉 + 预算 0 注入下首拍 adopt 8 ≠ 1 / 次拍 0 ≠ 1 断言失）；r2094b 豁免（行
//     为收敛对闸门在否双向稳健 + 本腿不钉该行）；r2094c 豁免（不钉该行；家族计数下界取 3 =
//     摘行后 decl+getter+setter 恰存）；r2094d 豁免（站点/沿革锚全在非摘除文本）。
//   NEG-2 = 摘预算计时器启动行（adoptBudgetClock.start(); 整行删除——声明行与闸门行幸存，
//     编译仍绿）→ 声明红面 = {r2094a, r2094b}（如实扩面声明，t1053/r2021 NEG-2 改声明先例
//     同门——本面双轮实测恰为该二元：r2094b = 摘面行本腿钉（专权红面 = 源钉失）；r2094a
//     同住节流闸道路径——budget-0 退化计数上界依赖**活时钟**（已启动 QElapsedTimer 的
//     elapsed() 恒非负 → 闸门恒真），未启动计时器的 elapsed() 答未定值（本 rig 实测答负 →
//     闸门恒不开火 → 首拍整批排干 d=8 → 计数上界断言红，diag [beat1 d=8] 判别）；r2094c
//     豁免（钉计时器**声明**行非启动行 + 家族下界摘行后恰存）；r2094d 豁免（站点/沿革锚
//     全在非摘除文本）。

namespace {

// sparse 流式小世界（spawnPreGenerateRadius=0 = 只预生成中心 chunk——全量走流式链，
//   section27 makeSparse 同门；dims/seed 自定避免与既族耦合）。
inline World makePacingSparse()
{
    World::SparseWorldParams sp;
    sp.seed = 87;
    sp.coreWidth = 80;
    sp.coreDepth = 80;
    sp.height = 96;
    sp.spawnPreGenerateRadius = 0;
    return World(sp);
}

// 收敛轮询：泵 tick 直到 keys 全部 Loaded 或超时（真线程收割节奏不定——防 flake 靠 deadline，
//   section27 convergeLoaded 同门；每轮 stepTick(0.11) = 恰 1 整 tick = 1 个 tick 尾流式泵拍）。
inline bool pacingConvergeLoaded(GameSession &gs, World &w,
                                 const QVector<QPair<int, int>> &keys, int deadlineMs,
                                 QString &diag)
{
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
            diag += QStringLiteral("[conv timeout %1ms miss: %2] ").arg(deadlineMs).arg(miss);
            return false;
        }
        gs.stepTick(0.11);
        QThread::msleep(2);
    }
}

// 有界等待：worker 已执行数与数据面缓冲双双到位（job 全完成且缓冲全在队——executed 与
//   m_data 同锁推进入账，观察到 executed==N 即 m_data ≥ N）。防 flake = deadline 有界。
inline bool pacingWaitExecuted(const BackgroundGenerationWorker *wk, quint64 target, int deadlineMs,
                               QString &diag)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < deadlineMs) {
        if (wk->executedCount() >= target && wk->resultDataCount() >= int(target))
            return true;
        QThread::msleep(2);
    }
    diag += QStringLiteral("[wait exec=%1 data=%2 want=%3] ")
                .arg(wk->executedCount())
                .arg(wk->resultDataCount())
                .arg(target);
    return false;
}

// 三队列全空（数据面缓冲 / 驱动器 pending / 驱动器 outcome）——稳态账面闭合判据。
inline bool pacingQueuesEmpty(GameSession &gs, const BackgroundGenerationWorker *wk)
{
    return wk->resultDataCount() == 0 && gs.streamDriver()->pendingJobCount() == 0
        && gs.streamDriver()->outcomeCount() == 0;
}

// 源钉根路径（section86 同款：applicationDirPath/../src）。
inline QString srcRootForPacingPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（锚注 / 站点幸存——pinSet 剥注释会失配，section86 同款）。
inline bool rawContainsForPacing(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

} // namespace

void MatrixRun::section87_stream_pacing_t1124()
{
    // ── r2094a:节流机制柱(测试缝注入预算 0 → 确定性单拍恰 1 adopt;NEG-1 敏感面 = 预算耗尽
    //   早退行)────────────────────────────────────────────────────────────────────────────
    //   相 1 注入预算 0（闸门 elapsed()>=0 恒真 = 每拍恰排 1 条的确定性计数上界）+ 8 job
    //     突发全完成入队；相 2 单拍收割：adopt 恰 1 + 余 7 在队 + 结果面同拍全排干（#7 修订
    //     契约第一面）+ pending 清零；相 3 次拍再 adopt 恰 1 + 余 6（跨拍续排面）；相 4 收敛
    //     面：续排至队列归零 + 八键全 Loaded + 全程 adopt 恰 8（每完成 job 恰一 adopt 账面
    //     恒等）；尾 = NEG-1 摘面行源钉。
    runLeg("r2094a pacing mechanism column (a test seam injects a zero millisecond budget so"
        " the budget gate degenerates into a deterministic one adoption per pump tick cap: the"
        " eight job burst completes into the data face with the first harvest tick answering"
        " exactly one adoption while seven buffers stay queued and the outcome face drains"
        " fully in that same tick, the next tick answers exactly one more adoption with six"
        " left proving the cross tick continuation, the queue then converges to zero with all"
        " eight chunks Loaded and every job adopted exactly once, and the budget exhaustion"
        " early return row is pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w = makePacingSparse();
        GameSession gs(w);
        gs.setStreamingPumpBudgetMsForTest(0); // 预算 0 = 闸门恒真 → 单拍恰 1 adopt（确定性）
        gs.configureStreamingRadii(1, 1, 2);
        const bool activeOk = gs.streamingActive() && gs.streamWorker() != nullptr;
        ok = ok && activeOk;
        if (!activeOk) diag += QStringLiteral("[inactive]");

        // 相 1：3×3 窗（中心 (2,2) 构造期预生成 → 恰 8 job；r2024c 同窗口径）全完成入队。
        gs.notePlayerChunk(2, 2);
        gs.stepTick(0.11); // 恰 1 整 tick：decide + submit + 交接（收割留后续拍）
        const BackgroundGenerationWorker *wk = gs.streamWorker();
        const bool waitOk = pacingWaitExecuted(wk, 8, 30000, diag);
        ok = ok && waitOk;

        // 相 2：单拍收割断言（预算 0 → 恰 1 adopt；结果面同拍全排干 = #7 修订契约第一面）。
        const qint64 adopted0 = gs.streamingAdoptedCount();
        gs.stepTick(0.11);
        const qint64 delta1 = gs.streamingAdoptedCount() - adopted0;
        const bool beat1Ok = delta1 == 1 && wk->resultDataCount() == 7
            && gs.streamDriver()->outcomeCount() == 0 && gs.streamDriver()->pendingJobCount() == 0;
        ok = ok && beat1Ok;
        if (!beat1Ok)
            diag += QStringLiteral("[beat1 d=%1 q=%2 out=%3 pend=%4] ")
                        .arg(delta1)
                        .arg(wk->resultDataCount())
                        .arg(gs.streamDriver()->outcomeCount())
                        .arg(gs.streamDriver()->pendingJobCount());

        // 相 3：次拍续排（余量下拍续排面——再恰 1 条，余 6 在队）。
        gs.stepTick(0.11);
        const qint64 delta2 = gs.streamingAdoptedCount() - adopted0 - delta1;
        const bool beat2Ok = delta2 == 1 && wk->resultDataCount() == 6;
        ok = ok && beat2Ok;
        if (!beat2Ok)
            diag += QStringLiteral("[beat2 d=%1 q=%2] ")
                        .arg(delta2)
                        .arg(wk->resultDataCount());

        // 相 4：收敛面（t1061 硬门）——续排至队列归零 + 八键全 Loaded + 账面恒等（恰 8）。
        QVector<QPair<int, int>> keys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                if (!(cx == 2 && cz == 2))
                    keys.append({ cx, cz });
        const bool convOk = pacingConvergeLoaded(gs, w, keys, 45000, diag)
            && pacingQueuesEmpty(gs, wk);
        ok = ok && convOk;
        const qint64 adoptedTotal = gs.streamingAdoptedCount() - adopted0;
        const bool identityOk = adoptedTotal == 8;
        ok = ok && identityOk;
        if (!identityOk)
            diag += QStringLiteral("[identity adopted=%1 want=8]").arg(adoptedTotal);

        // NEG-1 摘面行本腿钉（预算耗尽早退行——循环体内单行 if+break，playercontroller 式
        //   摘面口径：整行删除编译仍绿）。
        {
            const QStringList missNeg = pinSet(srcRootForPacingPins()
                                               + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("budget gate row",
                       "if (adoptBudgetClock.elapsed() >= m_streamPumpBudgetMs) break;", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2094a pacing mechanism column (a test seam injects a zero millisecond"
               " budget so the budget gate degenerates into a deterministic one adoption per"
               " pump tick cap: the eight job burst completes into the data face with the"
               " first harvest tick answering exactly one adoption while seven buffers stay"
               " queued and the outcome face drains fully in that same tick, the next tick"
               " answers exactly one more adoption with six left proving the cross tick"
               " continuation, the queue then converges to zero with all eight chunks Loaded"
               " and every job adopted exactly once, and the budget exhaustion early return"
               " row is pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2094b:稳态吞吐收敛柱(生产默认预算 3ms——零注入;NEG-2 敏感面 = 预算计时器启动行)────
    //   三站走查（(2,2) → (4,2) → (4,4)，走离不回——每站新窗 8+6+6）：生产预算下每站收敛到
    //     全 Loaded + 三队列归零（稳态排空速率 ≥ 入队速率 = 收敛不变量生产行为面）+ 每站
    //     adopt 增量 == submit 增量（相对恒等锚——零拒绝零丢失）+ 全程 adopted == submitted
    //     == 20 + 生产常量在场钉（本腿零注入）。尾 = NEG-2 摘面行源钉。
    runLeg("r2094b steady throughput convergence column (at the production default budget a"
        " three station walk across chunk boundaries drives submission completion and paced"
        " adoption with every station rest answering all its window chunks Loaded and all"
        " three queues empty, each station adopts exactly what it submitted with zero"
        " rejections, the whole run closes the relative identity anchor of adopted equal to"
        " submitted at twenty with nothing lost, the session keeps the production constant"
        " unread, and the budget clock start row is pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w = makePacingSparse();
        GameSession gs(w); // 生产默认预算（零注入——kStreamingPumpBudgetMs 在场）
        gs.configureStreamingRadii(1, 1, 2);
        const BackgroundGenerationWorker *wk = gs.streamWorker();
        const bool prodOk = gs.streamingActive()
            && gs.streamingPumpBudgetMs() == GameSession::kStreamingPumpBudgetMs;
        ok = ok && prodOk;
        if (!prodOk) diag += QStringLiteral("[prod]");

        // 三站走查：每站 = 位置沿 → 收敛新窗全 Loaded → 站稳态三队列归零 → 站恒等（adopt Δ ==
        //   submit Δ）。走离不回（单调走离——被驱逐的远块不再回窗，提交集恰 20 无重复）。
        struct Station { int cx, cz; int wantNew; };
        const Station stations[3] = { { 2, 2, 8 }, { 4, 2, 6 }, { 4, 4, 6 } };
        qint64 adoptedPrev = gs.streamingAdoptedCount();
        int submittedPrev = gs.streamDriver()->stats().submitted;
        for (const Station &st : stations) {
            gs.notePlayerChunk(st.cx, st.cz);
            QVector<QPair<int, int>> win;
            for (int cz = st.cz - 1; cz <= st.cz + 1; ++cz)
                for (int cx = st.cx - 1; cx <= st.cx + 1; ++cx)
                    win.append({ cx, cz });
            QString dConv;
            const bool convOk = pacingConvergeLoaded(gs, w, win, 45000, dConv)
                && pacingQueuesEmpty(gs, wk);
            ok = ok && convOk;
            if (!convOk) diag += dConv;
            const int submittedNow = gs.streamDriver()->stats().submitted;
            const qint64 adoptedNow = gs.streamingAdoptedCount();
            const bool stationOk = adoptedNow - adoptedPrev == submittedNow - submittedPrev;
            ok = ok && stationOk;
            if (!stationOk)
                diag += QStringLiteral("[station %1,%2 adopted=%3 submitted=%4] ")
                            .arg(st.cx).arg(st.cz)
                            .arg(adoptedNow - adoptedPrev).arg(submittedNow - submittedPrev);
            adoptedPrev = adoptedNow;
            submittedPrev = submittedNow;
        }

        // 全程相对恒等锚：零拒绝 + adopted == submitted == 20（每受理 job 恰一 adopt，零丢失
        //   ——预算节流不吞账）。
        const auto statsEnd = gs.streamDriver()->stats();
        const bool identityOk = statsEnd.rejected == 0
            && gs.streamingAdoptedCount() == statsEnd.submitted
            && statsEnd.submitted == 20;
        ok = ok && identityOk;
        if (!identityOk)
            diag += QStringLiteral("[identity rej=%1 adopted=%2 sub=%3] ")
                        .arg(statsEnd.rejected)
                        .arg(gs.streamingAdoptedCount())
                        .arg(statsEnd.submitted);

        // NEG-2 摘面行本腿钉（预算计时器启动行——声明行与闸门行幸存，编译仍绿）。
        {
            const QStringList missNeg = pinSet(srcRootForPacingPins()
                                               + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("budget clock start row", "adoptBudgetClock.start();", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg2 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2094b steady throughput convergence column (at the production default"
               " budget a three station walk across chunk boundaries drives submission"
               " completion and paced adoption with every station rest answering all its"
               " window chunks Loaded and all three queues empty, each station adopts exactly"
               " what it submitted with zero rejections, the whole run closes the relative"
               " identity anchor of adopted equal to submitted at twenty with nothing lost,"
               " the session keeps the production constant unread, and the budget clock start"
               " row is pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2094c:结构钉族(NEG 双摘面豁免不钉——预算早退行归 a / 计时器启动行归 b 专权)──────────
    //   预算值面（常量声明行 + 成员初始化行 + 缝 setter + getter + 计时器声明行 + include 行，
//     家族计数下界 3/常量族 2——双摘面后仍幸存的计数形态）+ 数据面/结果面 while 行幸存 +
    //     相邻族结构钉复钉（单一 adopt 落点 / fixed 惰性早退 / 生产泵缝 / F3 adopt 推送行）+
    //     契约修订注锚 + 预算推导注锚 + 沿革注锚（raw 口径——全在非摘除文本）+ CMake 段行 +
    //     双负面门（QML 零触 + 桥/驱动器零感知——节流参数住会话编排壳不下沉）。
    runLeg("r2094c structure pin family (the budget constant declaration and the member"
        " initializer and the test seam and the getter and the timer declaration and the"
        " elapsed timer include all hold with the family and constant counts above their"
        " negative immune lower bounds, the data face and outcome face while rows survive"
        " verbatim beside the single adopt call site and the fixed inert early out and the"
        " pump seam and the adopt counter push, the contract revision note and the budget"
        " derivation note and the station lineage note are present in the raw source, the"
        " cmake section row holds, and the qml plus bridge plus driver doors all stay shut)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForPacingPins();
        const QString gsPath = srcDir + QStringLiteral("/Game/gamesession.h");
        // (1) 预算值面（剥注释口径）+ 家族计数（NEG 免疫下界：成员族 3 = decl+setter+getter、
        //     常量族 2 = 声明+成员初始化——双摘面各只少 1，下界不破 = 本腿双 NEG 下恒绿）。
        {
            const QStringList missGs = pinSet(gsPath, {
                SrcPin("budget constant row", "static constexpr int kStreamingPumpBudgetMs = 3;", 1),
                SrcPin("budget member row", "int m_streamPumpBudgetMs = kStreamingPumpBudgetMs;", 1),
                SrcPin("budget seam row", "void setStreamingPumpBudgetMsForTest(int budgetMs)", 1),
                SrcPin("budget getter row", "int streamingPumpBudgetMs() const", 1),
                SrcPin("budget timer decl row", "QElapsedTimer adoptBudgetClock;", 1),
                SrcPin("elapsed timer include row", "#include <QElapsedTimer>", 1),
                SrcPin("budget member family", "m_streamPumpBudgetMs", 3),
                SrcPin("budget constant family", "kStreamingPumpBudgetMs", 2)});
            ok = ok && missGs.isEmpty();
            if (!missGs.isEmpty())
                diag += QStringLiteral("[gs %1]").arg(missGs.join(QLatin1Char(',')));
        }
        // (2) 数据面/结果面 while 行幸存（t1124 预算查检落循环体内——while 行逐字幸存）+
        //     相邻族结构钉复钉（r2024d 单一 adopt 落点 + fixed 惰性早退；生产泵缝；F3 adopt
        //     推送行）。
        {
            const QStringList missFace = pinSet(gsPath, {
                SrcPin("data face while row", "while (m_streamWorker->takeResultData(data))", 1),
                SrcPin("outcome face while row", "while (m_streamDriver->takeOutcome(outcome))", 1),
                SrcPin("single adopt call site", "m_world.adoptGeneratedChunk(", 1),
                SrcPin("fixed inert early out", "if (!m_streamDriver)", 1),
                SrcPin("pump seam row", "void pumpStreamingFrame() { pumpStreamingTick(); }", 1),
                SrcPin("adopt counter push row", "addCount(\"streamAdopt\", adoptedDrained)", 1)});
            ok = ok && missFace.isEmpty();
            if (!missFace.isEmpty())
                diag += QStringLiteral("[face %1]").arg(missFace.join(QLatin1Char(',')));
        }
        // (3) 注锚族（raw 口径——全在非摘除文本：函数头注修订段 / 常量推导段 / 计时器块注）。
        {
            const bool anchorOk = rawContainsForPacing(gsPath, QStringLiteral("review0916 #7 契约修订"))
                && rawContainsForPacing(gsPath, QStringLiteral("t1123 件① 调研数据支撑"))
                && rawContainsForPacing(gsPath, QStringLiteral("r2093c 调研站点 → t1124 改造落点"));
            ok = ok && anchorOk;
            if (!anchorOk) diag += QStringLiteral("[anchor]");
        }
        // (4) CMake 段行。
        {
            const bool cmakeOk = pinSet(QCoreApplication::applicationDirPath()
                                            + QStringLiteral("/../CMakeLists.txt"),
                                        { SrcPin("cmake section row",
                                                 "tools/matrix/section87_stream_pacing_t1124.cpp", 1)})
                                           .isEmpty();
            ok = ok && cmakeOk;
            if (!cmakeOk) diag += QStringLiteral("[cmake]");
        }
        // (5) 双负面门：QML 玩法路径零迁移（节流词元不入 Main.qml）+ 桥/驱动器零感知（节流
        //     参数住会话编排壳——不下沉更低层，PLAN §2 分层纪律）。
        {
            const bool doorOk = !rawContainsForPacing(srcDir + QStringLiteral("/ui/Main.qml"),
                                                      QStringLiteral("PumpBudget"))
                && !rawContainsForPacing(srcDir + QStringLiteral("/ui/Main.qml"),
                                         QStringLiteral("StreamingPump"))
                && !rawContainsForPacing(srcDir + QStringLiteral("/Game/streamingbridge.h"),
                                         QStringLiteral("kStreamingPumpBudgetMs"))
                && !rawContainsForPacing(srcDir + QStringLiteral("/World/chunkstreamdriver.h"),
                                         QStringLiteral("kStreamingPumpBudgetMs"));
            ok = ok && doorOk;
            if (!doorOk) diag += QStringLiteral("[door]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2094c structure pin family (the budget constant declaration and the member"
               " initializer and the test seam and the getter and the timer declaration and"
               " the elapsed timer include all hold with the family and constant counts above"
               " their negative immune lower bounds, the data face and outcome face while rows"
               " survive verbatim beside the single adopt call site and the fixed inert early"
               " out and the pump seam and the adopt counter push, the contract revision note"
               " and the budget derivation note and the station lineage note are present in"
               " the raw source, the cmake section row holds, and the qml plus bridge plus"
               " driver doors all stay shut)"
            << (ok ? QString() : diag);
    });

    // ── r2094d:调研站点幸存复核 + 沿革锚柱(t1123 件① 三站点 t1124 改造后逐字幸存复核面)──────
    //   四站点 raw 幸存（跨界检测行 / 两排干 while 行 / 快照采集行——r2093c 锚口径逐字复核，
    //     预算查检落循环体内故 while 行零修订）+ 沿革锚（站点→改造落点注 raw 在场）+ 既有
    //     结构钉族幸存复钉（纯源钉腿，rig 世界零接触）。
    runLeg("r2094d investigation station survival and lineage column (the four walk stutter"
        " investigation stations survive verbatim after the retrofit with the two harvest"
        " drain loops and the boundary detection row and the main thread snapshot capture"
        " row, the lineage anchor note for the stations becoming the retrofit site is present"
        " in the raw source, and the pinned gameplay qml routes stay untouched)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForPacingPins();
        // (1) 四站点 raw 幸存（r2093c 锚口径——预算改造后逐字幸存复核）。
        {
            const bool stationsOk = rawContainsForPacing(
                    srcDir + QStringLiteral("/Game/gamesession.h"),
                    QStringLiteral("while (m_streamWorker->takeResultData(data))"))
                && rawContainsForPacing(srcDir + QStringLiteral("/Game/gamesession.h"),
                                        QStringLiteral("while (m_meshWorker->takeBuilt(built))"))
                && rawContainsForPacing(srcDir + QStringLiteral("/ui/Main.qml"),
                                        QStringLiteral("_refreshChunkVisibility()"))
                && rawContainsForPacing(srcDir + QStringLiteral("/World/chunkgeometry.cpp"),
                        QStringLiteral("snap = captureChunkMeshSnapshot(WorldFacade(*m_world),"
                                       " m_cx, m_cz, bake);"));
            ok = ok && stationsOk;
            if (!stationsOk) diag += QStringLiteral("[stations]");
        }
        // (2) 沿革锚（t1123 调研站点 → t1124 改造落点——本单 lawful 留痕面；raw 在场）。
        {
            const bool lineageOk = rawContainsForPacing(
                srcDir + QStringLiteral("/Game/gamesession.h"),
                QStringLiteral("r2093c 调研站点 → t1124 改造落点"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }
        // (3) 玩法 QML 路由零触碰复钉（本单零 QML 迁移——既有路由面幸存的反面锚）。
        {
            const bool qmlOk = rawContainsForPacing(srcDir + QStringLiteral("/ui/Main.qml"),
                                                    QStringLiteral("_updatePlayerChunk"))
                && rawContainsForPacing(srcDir + QStringLiteral("/ui/Main.qml"),
                                        QStringLiteral("onResidentChunkRevisionChanged"));
            ok = ok && qmlOk;
            if (!qmlOk) diag += QStringLiteral("[qml]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2094d investigation station survival and lineage column (the four walk"
               " stutter investigation stations survive verbatim after the retrofit with the"
               " two harvest drain loops and the boundary detection row and the main thread"
               " snapshot capture row, the lineage anchor note for the stations becoming the"
               " retrofit site is present in the raw source, and the pinned gameplay qml"
               " routes stay untouched)"
            << (ok ? QString() : diag);
    });
}
