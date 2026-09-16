#include "matrix_helpers.h"

#include "chunklifecycle.h"       // 被测面：六态类型（生命周期沿断言）
#include "chunkstore.h"           // 被测面：per-chunk 附加表（D5 选型 (a) additive 载体）
#include "entitymanager.h"        // 被测面：驱逐域活体移除（mob 族）
#include "itementitymanager.h"    // 被测面：驱逐域掉落物移除（Adapter 委托 → store 权威）
#include "gamesession.h"          // 被测面：GameSession 驱逐接线（W3 宿主）
#include "generationjob.h"        // 被测面：GenerationJobKind（Load 路由观测）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// §29.5-W3 驱逐 + Edits-on-evict 落盘 探针段（4 腿；filter 词 r2025；矩阵 636→636+N）。置尾
// 先例沿用（接 section27，runAll 末执行）。任务契约（docs/refactor-plan-2026-09-08.md
// §29.5.2 W3 原文 + §29.5.3 选型 1 (a)）：
//   「fixed 零活动墙」→ r2025a（fixed 世界 GameSession 连附加表都不构造，全 tick/移动零活动）；
//   「dirtyQuery = World 脏面、persistFn = per-chunk 附加表、lifecycleTransition = 真转移
//     ⑥⑦ + 重载回灌」→ r2025b（编辑过的半径外 chunk：persist 先于转移调用序柱 → Absent +
//     擦槽 → revision 沿 → 重入半径附加表命中 → blob 物化逐位恒等[编辑+未编辑区] + population
//     跳过面；未编辑 chunk 走重derive 路径亦逐位恒等）；
//   「persist 失败中止 + 实体×卸载语义」→ r2025c（真锁注入 → 保持驻留 + aborted + 重报收敛；
//     驱逐区内 mob/掉落物先于转移移除，区外见证者不误伤）；
//   「结构钉」→ r2025d（worldstore 禁触反探 / 附加表 additive 正面钉 / 先落盘后转移源钉 /
//     restore 跳 population 函数域钉 / 实体移除单漏斗钉 / QML 零触碰 / fixed 墙）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘 chunkevictor.h evict() 的「先落盘后转移」序（dirty-persist 块移到
//     转移步之后）→ 声明红面 {r2025b 序柱}[任务书候选面：PersistOk 事件位序 > EdgeEvicting；
//     且落盘后置读已释放槽 = 零 PersistOk + 回灌走重derive → r2025b 重载柱同源红——两面同源
//     如实双红] + {r2025c 相A}[中止语义与序铁律同源：转移先执行则「失败中止保持驻留」柱红]。
//     r2025a 不误伤（fixed 零驱动器，evict 不可达）；r2025d 不误伤（钉面 = 源码文本在位，
//     块搬移保持文本不删）。
//   变异二（NEG-2）= 摘 gamesession.h 收割拍数据面的附加表命中路由（if (false && m_chunkStore
//     && ...) 前缀）→ 声明红面 {r2025b 重载柱}[命中表仍走重derive 路径 → 玩家编辑丢失 →
//     blob 物化逐位恒等柱红（population 重放面同柱实证）]。r2025a 不误伤（fixed 无路由可达）；
//     r2025c 不误伤（相A 无命中面可达，相B 无重载柱）；r2025d 不误伤（if (false) 前缀不剥
//     路由文本）。
//   阴性日志：build/ 下四件 matrix_r2025_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 时长控制：腿内 sparse 世界 spawnPreGenerateRadius=0 + 半径压小（(1,1,3)：gen 窗 3×3、scan
//   窗 7×7 给驱逐留 annulus）；收敛轮询 deadline 有界（防 flake 不挂死）；临时库 fresh + 用后
//   即删（QDir::temp() pid 键名，绝不触 saves/）。
void MatrixRun::section28_eviction_persistence()
{
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42; // sparse 核心域（与 W2 段同族）
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk）

    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0; // 零预生成：全量走流式链（驱逐/回灌面全暴露）
        return World(sp);
    };
    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2025_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 收敛轮询：泵 tick 直到 keys 全部 Loaded 或超时（真线程收割节奏不定——deadline 有界）。
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
            gs.stepTick(0.11); // 恰 1 整 tick = 1 个 tick 尾流式泵拍（提交/收割/路由全在此拍）
            QThread::msleep(2);
        }
    };
    // 全 chunk 体素快照/比对（16×16 列 × 全 y 带 id+state——blob 物化逐位恒等柱的承载面）。
    const auto snapChunk = [&](World &w, int cx, int cz) {
        QVector<QPair<int, int>> vox; // (id,state) 平铺
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kH; ++y)
                    vox.append({ w.blockAt(cx * 16 + lx, y, cz * 16 + lz),
                        w.stateAt(cx * 16 + lx, y, cz * 16 + lz) });
        return vox;
    };
    // 比对 + 差异诊断（前 8 个差异格：局部 lx/lz/y + 快照 (id,state) + 活体 (id,state)）。
    const auto chunkIdentical = [&](World &w, int cx, int cz, const QVector<QPair<int, int>> &snap,
                                    long &equalCount, QString &diffDiag) {
        int i = 0;
        int diffs = 0;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kH; ++y, ++i) {
                    const quint8 sid = snap[i].first, sst = snap[i].second;
                    const quint8 lid = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                    const quint8 lst = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                    if (lid == sid && lst == sst) {
                        ++equalCount;
                    } else if (++diffs <= 8) {
                        diffDiag += QStringLiteral("[%1,%2,%3 %4/%5->%6/%7] ")
                                        .arg(lx).arg(lz).arg(y)
                                        .arg(sid).arg(sst).arg(lid).arg(lst);
                    }
                }
        return diffs == 0;
    };
    // 轨迹事件检索（kind：0=PersistOk 1=PersistFail 2=EdgeEvicting 3=EdgeAbsent 4=Rejected）。
    const auto traceFind = [](const QVector<GameSession::EvictionTraceEvent> &tr, int kind, int cx,
                              int cz, int from = 0) -> int {
        for (int i = from; i < tr.size(); ++i)
            if (tr[i].kind == kind && tr[i].cx == cx && tr[i].cz == cz)
                return i;
        return -1;
    };

    // ── r2025a：fixed 零活动墙（回归 + 无附加表读写）─────────────────────────────────────
    // fixed 世界 GameSession：streamingActive false + 驱动器/worker/**附加表**三者连构造都不
    // 发生（chunkEditsStore()==null = 「无附加表读写」的最强形态——对象不存在即无连接面）；
    // 真 PlayerController 跨 chunk 边界扫掠 + 全 tick：零生命周期/revision/驱逐/轨迹活动，
    // 全状态逐位不动。
    runLeg(QStringLiteral("r2025a fixed inert wall (a fixed-world session constructs no streaming"
        " driver, no worker and no per-chunk edit store - the store handle stays null so no"
        " additive table access can even exist; a real player chunk-edge sweep with full session"
        " ticks produces zero lifecycle, revision, eviction or trace activity, the last eviction"
        " report stays all-zero, and every sampled state face stays bitwise identical)"), [&]() {
        bool ok = true;
        QString diag;

        World w; // fixed（默认构造 + setter incantation）
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);

        GameSession gs(w);
        const bool inertOk = !gs.streamingActive() && gs.streamDriver() == nullptr
            && gs.streamWorker() == nullptr && gs.chunkEditsStore() == nullptr;
        ok = ok && inertOk;
        if (!inertOk)
            diag += QStringLiteral("[inert active=%1 drv=%2 wrk=%3 store=%4] ")
                        .arg(gs.streamingActive())
                        .arg(gs.streamDriver() != nullptr)
                        .arg(gs.streamWorker() != nullptr)
                        .arg(gs.chunkEditsStore() != nullptr);
        // 附加表绑定面 fail-safe：fixed 世界 bind 拒（恒 false 且零副作用）。
        const bool bindRejected = !gs.bindChunkEditsStore(QStringLiteral("r2025a-should-not-exist"));
        ok = ok && bindRejected && gs.chunkEditsStore() == nullptr;
        if (!bindRejected)
            diag += QStringLiteral("[bind-not-rejected] ");

        // 全状态快照 + 真 PlayerController 沿扫掠（跨 ≥3 次 chunk 边界）+ 全 tick。
        const int revBefore = w.residentChunkRevision();
        const int resBefore = w.residentChunkCount();
        static const int kCols[][2] = { { 8, 8 }, { 40, 40 }, { 47, 5 }, { 5, 47 } };
        QVector<QPair<int, int>> voxBefore;
        for (const auto &c : kCols)
            for (int y = 0; y < kHF; ++y)
                voxBefore.append({ w.blockAt(c[0], y, c[1]), w.stateAt(c[0], y, c[1]) });

        PlayerController pc;
        pc.setWorld(&w);
        int edges = 0;
        QObject::connect(&pc, &PlayerController::playerChunkChanged, &gs,
            [&edges, &gs](int cx, int cz) {
                ++edges;
                gs.notePlayerChunk(cx, cz);
            });
        pc.loadSavedState(8.5f, 70.0f, 8.5f, 0.0f, 0.0f, 0);
        pc.tick();
        const int edgesStart = edges;
        pc.loadSavedState(30.5f, 70.0f, 12.5f, 0.0f, 0.0f, 0);
        pc.tick();
        pc.loadSavedState(41.5f, 70.0f, 39.5f, 0.0f, 0.0f, 0);
        pc.tick();
        pc.loadSavedState(9.5f, 70.0f, 41.5f, 0.0f, 0.0f, 0);
        pc.tick();
        ok = ok && edges >= edgesStart + 3;
        if (edges < edgesStart + 3)
            diag += QStringLiteral("[edges %1->%2] ").arg(edgesStart).arg(edges);

        for (int i = 0; i < 12; ++i)
            gs.stepTick(0.11);

        bool untouched = w.residentChunkRevision() == revBefore
            && w.residentChunkCount() == resBefore && gs.streamingOutcomeCount() == 0
            && gs.streamingAdoptedCount() == 0 && gs.evictionTrace().isEmpty()
            && gs.lastEvictionReport().persisted == 0 && gs.lastEvictionReport().evicted == 0
            && gs.lastEvictionReport().aborted == 0 && gs.lastEvictionReport().skipped == 0;
        int vi = 0;
        for (int ci = 0; ci < 4 && untouched; ++ci)
            for (int y = 0; y < kHF && untouched; ++y) {
                untouched = untouched
                    && w.blockAt(kCols[ci][0], y, kCols[ci][1]) == voxBefore[vi].first
                    && w.stateAt(kCols[ci][0], y, kCols[ci][1]) == voxBefore[vi].second;
                ++vi;
            }
        ok = ok && untouched;
        if (!untouched)
            diag += QStringLiteral("[untouched false] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2025a fixed inert wall: the fixed-world session constructs no"
                             " streaming driver, worker or per-chunk edit store, the bind face"
                             " rejects on the null store, a real player chunk-edge sweep with"
                             " twelve full ticks produces zero lifecycle / revision / eviction"
                             " / trace activity, and every sampled state face stays bitwise"
                             " identical" << (ok ? QString() : diag);
    });

    // ── r2025b：驱逐回灌承重（真临时库，fresh + 用后即删）──────────────────────────────────
    // 编辑过的半径外 chunk → persist 先于转移（调用序柱）→ ⑥⑦到 Absent + 擦槽 → revision 沿
    // → 重入半径 → 附加表命中 → blob 物化内容逐位恒等（编辑区含 population 终态 + 未编辑区
    // 走重derive 同样恒等）→ population 跳过面（重载内容含玩家编辑 = 未走重derive）。
    runLeg(QStringLiteral("r2025b evict-reload roundtrip load-bearing wall (with a real fresh"
        " temp database an edited out-of-radius chunk persists before any lifecycle transition"
        " - the trace order pillar holds persist-ok before edge-evicting before edge-absent -"
        " then reaches Absent with the sparse slot released and the resident revision moved,"
        " re-entering the radius hits the additive table so the harvest beat materializes the"
        " blob directly with the player edit and population content bitwise identical to the"
        " pre-evict snapshot while an unedited evicted chunk regenerates to the same bitwise"
        " content, the restored chunk starts clean and a second eviction of it stays"
        " persist-free, and the store keeps exactly one row with a stable generation)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("b");
        QFile::remove(db); // fresh（用后即删由腿尾保证）

        World ws = makeSparse();
        GameSession gs(ws);
        gs.configureStreamingRadii(1, 1, 3); // gen 窗 3×3 + scan 窗 7×7（驱逐 annulus 非空）
        const bool boundOk = gs.bindChunkEditsStore(db) && gs.chunkEditsStore() != nullptr;
        ok = ok && boundOk;
        if (!boundOk)
            diag += QStringLiteral("[bind] ");

        // ① 初载：玩家落 (2,2) → 3×3 窗全 Loaded（中心 (2,2) 构造期已预生成）。
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded(gs, ws, centerKeys, 45000, d1);
        ok = ok && c1;
        if (!c1)
            diag += d1;

        // ② 编辑 chunk (3,2) 的列 (56,44)：顶面放 Stone（破/放目标显式验证：支撑非空气 + 目标
        //    空气）。persist 域脏面正面断言；未编辑 chunk (1,2) 恒 clean。
        const int etX = 56, etZ = 44;
        const int etTop = ws.heightmapAt(etX, etZ);
        const bool editPreOk = etTop >= 0 && etTop + 1 < kH && ws.blockAt(etX, etTop, etZ) != 0
            && ws.blockAt(etX, etTop + 1, etZ) == 0;
        ok = ok && editPreOk;
        if (!editPreOk)
            diag += QStringLiteral("[editPre top=%1] ").arg(etTop);
        const bool placed = editPreOk && ws.setBlock(etX, etTop + 1, etZ, BR::Stone);
        ok = ok && placed;
        if (!placed)
            diag += QStringLiteral("[place-failed] ");
        const bool dirtyOk = ws.chunkHasUnsavedEdits(3, 2) && !ws.chunkHasUnsavedEdits(1, 2);
        ok = ok && dirtyOk;
        if (!dirtyOk)
            diag += QStringLiteral("[dirty e=%1 c=%2] ")
                        .arg(ws.chunkHasUnsavedEdits(3, 2))
                        .arg(ws.chunkHasUnsavedEdits(1, 2));

        // ③ 快照：编辑 chunk (3,2) + 未编辑 chunk (2,2) 全 chunk 体素面（逐位恒等柱基线）。
        const QVector<QPair<int, int>> snapEdited = snapChunk(ws, 3, 2);
        const QVector<QPair<int, int>> snapUnedited = snapChunk(ws, 2, 2);
        long leavesSnapshot = 0; // population 非空转签名（快照内树叶计数，diag 用）
        for (const auto &v : snapEdited)
            if (v.first == BR::Leaves)
                ++leavesSnapshot;
        for (const auto &v : snapUnedited)
            if (v.first == BR::Leaves)
                ++leavesSnapshot;

        // ④ 走离：玩家 (2,2)→(5,5)，一拍内 decide + 驱逐同步执行。旧驻留中 cheb ∈ (1,3] 者
        //    驱逐（恰 (2,2),(2,3),(3,2),(3,3) 四个）；(3,2) dirty → persist 先行。
        const int revBeforeEvict = ws.residentChunkRevision();
        const int traceBefore = gs.evictionTrace().size();
        gs.notePlayerChunk(5, 5);
        gs.stepTick(0.11); // 驱逐拍（同步：decide → evictor → 转移缝）

        const ChunkEvictor::Report rep = gs.lastEvictionReport();
        const bool evictOk = ws.chunks().lifecycleAt(3, 2) == ChunkLifecycle::Absent
            && ws.chunks().chunk(3, 2) == nullptr // 擦槽 = 数据面闭合（population teardown 同门）
            && ws.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent
            && ws.chunks().lifecycleAt(3, 3) == ChunkLifecycle::Absent
            && ws.chunks().lifecycleAt(1, 1) == ChunkLifecycle::Loaded // scan 窗外驻留不误逐
            && ws.residentChunkRevision() > revBeforeEvict
            && rep.persisted == 1 && rep.evicted == 4 && rep.aborted == 0;
        ok = ok && evictOk;
        if (!evictOk)
            diag += QStringLiteral("[evict l32=%1 rel=%2 l22=%3 l33=%4 l11=%5 rev+%6 p=%7 e=%8 a=%9] ")
                        .arg(int(ws.chunks().lifecycleAt(3, 2)))
                        .arg(ws.chunks().chunk(3, 2) != nullptr)
                        .arg(int(ws.chunks().lifecycleAt(2, 2)))
                        .arg(int(ws.chunks().lifecycleAt(3, 3)))
                        .arg(int(ws.chunks().lifecycleAt(1, 1)))
                        .arg(ws.residentChunkRevision() - revBeforeEvict)
                        .arg(rep.persisted).arg(rep.evicted).arg(rep.aborted);

        // 调用序柱（P3 铁律生产面）：(3,2) 的 PersistOk < EdgeEvicting < EdgeAbsent。
        const QVector<GameSession::EvictionTraceEvent> tr1 = gs.evictionTrace();
        const int iPok = traceFind(tr1, 0, 3, 2, traceBefore);
        const int iEv = traceFind(tr1, 2, 3, 2, traceBefore);
        const int iAb = traceFind(tr1, 3, 3, 2, traceBefore);
        const bool orderOk = iPok >= 0 && iEv > iPok && iAb > iEv;
        ok = ok && orderOk;
        if (!orderOk)
            diag += QStringLiteral("[order pok=%1 ev=%2 ab=%3] ").arg(iPok).arg(iEv).arg(iAb);
        // 落盘恰一次（仅 dirty 候选）：本批 PersistOk 全局恰 1 条。
        int pokTotal = 0;
        for (int i = traceBefore; i < tr1.size(); ++i)
            if (tr1[i].kind == 0)
                ++pokTotal;
        ok = ok && pokTotal == 1;
        if (pokTotal != 1)
            diag += QStringLiteral("[pok-total=%1] ").arg(pokTotal);

        // 附加表读面：恰一行、命中 (3,2)、代次 ≥1。
        ChunkStoreBlob blob;
        const bool storeOk = gs.chunkEditsStore() != nullptr
            && gs.chunkEditsStore()->chunkCount() == 1
            && gs.chunkEditsStore()->hasChunk(3, 2) && !gs.chunkEditsStore()->hasChunk(2, 2)
            && gs.chunkEditsStore()->loadChunk(3, 2, blob) && blob.generation >= 1
            && blob.voxels.size() == 16 * 16 * kH;
        ok = ok && storeOk;
        if (!storeOk)
            diag += QStringLiteral("[store n=%1 has32=%2 gen=%3 sz=%4] ")
                        .arg(gs.chunkEditsStore() ? gs.chunkEditsStore()->chunkCount() : -1)
                        .arg(gs.chunkEditsStore() ? gs.chunkEditsStore()->hasChunk(3, 2) : false)
                        .arg(gs.chunkEditsStore() ? qint64(blob.generation) : qint64(-1))
                        .arg(blob.voxels.size());

        // ⑤ 重入：玩家 (5,5)→(3,2)。savedContentQuery 命中 → Load job → 收割拍 blob 物化。
        gs.notePlayerChunk(3, 2);
        QVector<QPair<int, int>> backKeys{ { 3, 2 }, { 2, 2 } };
        QString d5;
        const bool c5 = convergeLoaded(gs, ws, backKeys, 60000, d5);
        ok = ok && c5;
        if (!c5)
            diag += d5;

        // 逐位恒等主柱：编辑 chunk (3,2) blob 物化 ≡ 驱逐前快照**全 16×16×H 逐位**（含玩家
        // Stone 编辑 + population 终态树等结构——编辑在列 = 未走重derive 重放，population 跳过
        // 面的行为级承载；「编辑区 + 未编辑区都验」由全 chunk 逐位面一并承载）。
        long eqEdited = 0;
        QString diffEdited;
        const bool idEdited = chunkIdentical(ws, 3, 2, snapEdited, eqEdited, diffEdited);
        ok = ok && idEdited && eqEdited >= 24000;
        if (!idEdited)
            diag += QStringLiteral("[identity-blob eq=%1 de%2] ").arg(eqEdited).arg(diffEdited);

        // 未编辑 chunk (2,2)：clean 驱逐（无表行）→ 重入半径走重derive 路径 → 与 fixed 孪生
        // 同区掩蔽恒等。掩蔽口径 = W1b 登记面如实划界：ore 块缘跨 cell 交互（(c) 替代条目）
        // + 结构族五员豁免足迹（9 点偏移栅格）+ 块缘列 [lx/lz ∈ {0,15}]（±8 列上下文敏感
        // 带）；内陆 (b) 级内容 = 上下文无关面，零差异零豁免。
        long cleanU = 0;
        bool uParity = true;
        {
            World wf; // 同 seed 同 dims fixed 孪生（r2024b 同族口径）
            wf.setWidth(kWS);
            wf.setDepth(kDS);
            wf.setHeight(kH);
            wf.setSeed(kSeed);
            const auto isOre = [](quint8 b) {
                switch (b) {
                    case BR::CoalOre: case BR::IronOre: case BR::GoldOre: case BR::DiamondOre:
                    case BR::RedstoneOre: case BR::LapisOre: case BR::CopperOre: return true;
                    default: return false;
                }
            };
            const auto structMasked = [&wf](int x, int y, int z) {
                static const int kShifts[9][2] = { { 0, 0 }, { 6, 0 }, { -6, 0 }, { 0, 6 },
                    { 0, -6 }, { 6, 6 }, { 6, -6 }, { -6, 6 }, { -6, -6 } };
                for (const auto &sh : kShifts)
                    if (wf.insideDungeon(x + sh[0], y, z + sh[1])
                        || wf.insideMineshaft(x + sh[0], y, z + sh[1])
                        || wf.insideDesertTemple(x + sh[0], y, z + sh[1])
                        || wf.insideJungleTemple(x + sh[0], y, z + sh[1])
                        || wf.insideStronghold(x + sh[0], y, z + sh[1]))
                        return true;
                return false;
            };
            for (int lz = 0; lz < 16 && uParity; ++lz)
                for (int lx = 0; lx < 16 && uParity; ++lx) {
                    const bool borderCol = lx == 0 || lx == 15 || lz == 0 || lz == 15;
                    for (int y = 0; y < kH && uParity; ++y) {
                        const int x = 2 * 16 + lx, z = 2 * 16 + lz;
                        const quint8 bs = ws.blockAt(x, y, z);
                        const quint8 bf = wf.blockAt(x, y, z);
                        if (bs == bf) {
                            if (!isOre(bf) && !borderCol && !structMasked(x, y, z))
                                ++cleanU;
                            continue;
                        }
                        if (isOre(bf) || isOre(bs) || borderCol || structMasked(x, y, z))
                            continue; // W1b 登记豁免面
                        uParity = false;
                        diag += QStringLiteral("[u-parity lx=%1 lz=%2 y=%3 s=%4 f=%5] ")
                                    .arg(lx).arg(lz).arg(y).arg(bs).arg(bf);
                    }
                }
        }
        ok = ok && uParity && cleanU >= 12000 && leavesSnapshot >= 8;
        if (!uParity || cleanU < 12000 || leavesSnapshot < 8)
            diag += QStringLiteral("[unedited parity=%1 clean=%2 leaves=%3] ")
                        .arg(uParity).arg(cleanU).arg(leavesSnapshot);

        // 重载起点 clean（blob 物化不记未落盘编辑）+ 重载后未再触附加表写面。
        const bool cleanOk = !ws.chunkHasUnsavedEdits(3, 2)
            && gs.chunkEditsStore()->chunkCount() == 1;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[restored-clean dirty=%1 rows=%2] ")
                        .arg(ws.chunkHasUnsavedEdits(3, 2))
                        .arg(gs.chunkEditsStore()->chunkCount());

        // ⑥ 二次驱逐（clean 化重载 chunk）：走离 (3,2)→(5,5) → (3,2) 驱逐零落盘（行数与
        //    代次恒定 = persist 仅 dirty 候选的负向面）。
        gs.notePlayerChunk(5, 5);
        gs.stepTick(0.11);
        const bool secondOk = ws.chunks().lifecycleAt(3, 2) == ChunkLifecycle::Absent
            && gs.chunkEditsStore()->chunkCount() == 1
            && gs.chunkEditsStore()->loadChunk(3, 2, blob) && blob.generation == 1;
        ok = ok && secondOk;
        if (!secondOk)
            diag += QStringLiteral("[second l32=%1 rows=%2 gen=%3] ")
                        .arg(int(ws.chunks().lifecycleAt(3, 2)))
                        .arg(gs.chunkEditsStore()->chunkCount())
                        .arg(qint64(blob.generation));

        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2025b evict-reload roundtrip: an edited out-of-radius chunk"
                             " persists before any transition (trace pillar persist-ok then"
                             " edge-evicting then edge-absent), reaches Absent with its slot"
                             " released and the resident revision moved, re-entry hits the"
                             " additive table and the harvest beat materializes the blob with"
                             " player edit and population content bitwise identical to the"
                             " pre-evict snapshot, the unedited chunk regenerates to the same"
                             " bitwise content, the restored chunk starts clean and its second"
                             " eviction stays persist-free with one stable row in the store"
                          << (ok ? QString() : diag);
    });

    // ── r2025c：失败中止 + 实体×卸载语义 ─────────────────────────────────────────────────
    // 相A（中止）：persist 真锁注入（第二连接 BEGIN EXCLUSIVE——t974 先例同款生产面）→
    //   dirty 候选中止：保持驻留 + 内容/脏账原样 + PersistFail 轨迹 + 零转移事件；解锁后下一
    //   变更沿重报收敛（P3「重试 = 调用方下一变更沿」语义生产面）。
    // 相B（实体语义）：驱逐转移前移除——目标区 mob+掉落物消失、区外见证者不误伤、转移仍达
    //   Absent（先移除后转移的轨迹面 = 移除缝在 EdgeEvicting 之前不可直接观测，由「区已
    //   Absent 且活体先没了」+ 单漏斗委托面承载）。
    runLeg(QStringLiteral("r2025c failure abort and entity unload semantics (with the save lock"
        " held by a second exclusive connection the dirty eviction candidate aborts - it stays"
        " resident with its edit ledger intact, the trace records persist-fail with zero"
        " transition events for it and the report counts the abort - and after the lock is"
        " released the next position edge re-reports the candidate and the eviction converges"
        " with the blob persisted; separately, mobs and dropped items inside an evicting chunk"
        " are removed through the pre-transition entity sink while an in-radius witness mob and"
        " item survive untouched and the chunk still reaches Absent)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh

        World ws = makeSparse();
        GameSession gs(ws);
        gs.configureStreamingRadii(1, 1, 3);
        ok = ok && gs.bindChunkEditsStore(db);

        // ── 相B 前置共用初载（先于相A：同会话承载两相，实体见证者在相A 驱逐窗内即布防）────
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded(gs, ws, centerKeys, 45000, d1);
        ok = ok && c1;
        if (!c1)
            diag += d1;

        // ── 相A：真锁注入 → dirty 候选中止 ────────────────────────────────────────────────
        const int etX = 56, etZ = 44;
        const int etTop = ws.heightmapAt(etX, etZ);
        ok = ok && etTop >= 0 && etTop + 1 < kH && ws.blockAt(etX, etTop, etZ) != 0
            && ws.blockAt(etX, etTop + 1, etZ) == 0;
        ok = ok && ws.setBlock(etX, etTop + 1, etZ, BR::Stone);
        ok = ok && ws.chunkHasUnsavedEdits(3, 2);

        bool lockHeld = false;
        {
            QSqlDatabase lockDb = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), QStringLiteral("r2025_locker"));
            lockDb.setDatabaseName(db);
            lockDb.open();
            QSqlQuery beginQ(lockDb);
            lockHeld = beginQ.exec(QStringLiteral("BEGIN EXCLUSIVE"));

            const int traceBefore = gs.evictionTrace().size();
            gs.notePlayerChunk(5, 5);
            gs.stepTick(0.11); // 驱逐拍：persist 被库锁挡 → 中止

            const ChunkEvictor::Report rep = gs.lastEvictionReport();
            const bool abortOk = lockHeld
                && ws.chunks().lifecycleAt(3, 2) == ChunkLifecycle::Loaded // 保持驻留（宁驻留不误删）
                && ws.chunks().chunk(3, 2) != nullptr
                && ws.blockAt(etX, etTop + 1, etZ) == BR::Stone // 内容原样
                && ws.chunkHasUnsavedEdits(3, 2) // 脏账原样（未清 = 未落盘）
                && rep.aborted >= 1;
            ok = ok && abortOk;
            if (!abortOk)
                diag += QStringLiteral("[abort held=%1 l32=%2 content=%3 dirty=%4 ab=%5] ")
                            .arg(lockHeld)
                            .arg(int(ws.chunks().lifecycleAt(3, 2)))
                            .arg(ws.blockAt(etX, etTop + 1, etZ) == BR::Stone)
                            .arg(ws.chunkHasUnsavedEdits(3, 2))
                            .arg(rep.aborted);
            // 轨迹面：(3,2) 有 PersistFail、零转移事件（EdgeEvicting/EdgeAbsent 双零）。
            const QVector<GameSession::EvictionTraceEvent> tr = gs.evictionTrace();
            const bool traceOk = traceFind(tr, 1, 3, 2, traceBefore) >= 0
                && traceFind(tr, 2, 3, 2, traceBefore) < 0
                && traceFind(tr, 3, 3, 2, traceBefore) < 0;
            ok = ok && traceOk;
            if (!traceOk)
                diag += QStringLiteral("[trace pf=%1 ev=%2 ab=%3] ")
                            .arg(traceFind(tr, 1, 3, 2, traceBefore))
                            .arg(traceFind(tr, 2, 3, 2, traceBefore))
                            .arg(traceFind(tr, 3, 3, 2, traceBefore));

            QSqlQuery endQ(lockDb);
            endQ.exec(QStringLiteral("COMMIT")); // 解锁
        }
        QSqlDatabase::removeDatabase(QStringLiteral("r2025_locker"));
        ok = ok && lockHeld;
        if (!lockHeld)
            diag += QStringLiteral("[lock-not-held] ");

        // 解锁后重报收敛：变更沿 (5,5)→(4,4)（(3,2) cheb 2 ∈ annulus）→ persist 成功 → 驱逐。
        const int traceBefore2 = gs.evictionTrace().size();
        gs.notePlayerChunk(4, 4);
        gs.stepTick(0.11);
        ChunkStoreBlob blob;
        const bool convergeOk = ws.chunks().lifecycleAt(3, 2) == ChunkLifecycle::Absent
            && ws.chunks().chunk(3, 2) == nullptr && gs.chunkEditsStore()->hasChunk(3, 2)
            && gs.chunkEditsStore()->loadChunk(3, 2, blob) && blob.generation == 1;
        ok = ok && convergeOk;
        if (!convergeOk)
            diag += QStringLiteral("[converge l32=%1 has=%2] ")
                        .arg(int(ws.chunks().lifecycleAt(3, 2)))
                        .arg(gs.chunkEditsStore()->hasChunk(3, 2));
        // 收敛拍内 PersistOk 恰 1（重报候选恰一次落盘）。
        int pok2 = 0;
        const QVector<GameSession::EvictionTraceEvent> tr2 = gs.evictionTrace();
        for (int i = traceBefore2; i < tr2.size(); ++i)
            if (tr2[i].kind == 0)
                ++pok2;
        ok = ok && pok2 == 1;
        if (pok2 != 1)
            diag += QStringLiteral("[pok2=%1] ").arg(pok2);

        // ── 相B：驱逐转移前实体移除（mob + 掉落物两族；区外见证者不误伤）────────────────
        // 布防：目标区 (3,2) 已在相A 末驱逐 → 布防前先把它请回来（重入半径：附加表命中 →
        // blob 物化重载——命中路由的行为面归承重腿专钉，本腿只取「目标区驻留」这一布防前提）。
        gs.notePlayerChunk(3, 2);
        QVector<QPair<int, int>> backKeys{ { 3, 2 } };
        QString d3;
        const bool c3 = convergeLoaded(gs, ws, backKeys, 60000, d3);
        ok = ok && c3;
        if (!c3)
            diag += d3;

        EntityManager em;
        ItemEntityManager iem;
        gs.setEvictionEntitySink([&em, &iem](int cx, int cz) {
            em.despawnInChunk(cx, cz);
            iem.despawnInChunk(cx, cz);
        });
        const int targetY = ws.heightmapAt(etX, etZ) + 1;
        const int mobSlot = em.spawnMobTyped(etX, targetY, etZ, 0, QStringLiteral("#ff5555"), 10);
        iem.spawnItem(etX, targetY, etZ, int(BR::Stone), 1);
        // 见证者：chunk (5,5)（世界 (86,88)）——选址推演：目标驱逐沿 = (3,2)→(6,6)，annulus
        // （cheb ∈ (1,3]）内一切旧驻留都将被逐（含 (3,3)/(4,x) 窗口块），见证者必须在**该沿的
        // 新 gen 窗**内（cheb((5,5),(6,6)) = 1）才保驻留；此刻 (5,5) chunk 尚 Absent → 见证者
        // y 取固定值（chunk 归属只看 x/z floorDiv16，与 y 无关；实体不随 chunk 物化缺席）。
        const int witX = 86, witZ = 88, witY = 70; // chunk (5,5)
        const int witMobSlot = em.spawnMobTyped(witX, witY, witZ, 0, QStringLiteral("#55ff55"), 10);
        iem.spawnItem(witX, witY, witZ, int(BR::Stone), 1);
        const int mobsBefore = em.liveCount();
        const int itemsBefore = iem.liveCount();
        const bool spawnOk = mobSlot >= 0 && witMobSlot >= 0 && mobsBefore == 2 && itemsBefore == 2;
        ok = ok && spawnOk;
        if (!spawnOk)
            diag += QStringLiteral("[spawn mob=%1 wit=%2 mb=%3 ib=%4] ")
                        .arg(mobSlot).arg(witMobSlot).arg(mobsBefore).arg(itemsBefore);

        const int l32BeforeEdge = int(ws.chunks().lifecycleAt(3, 2));
        const bool dirtyBeforeEdge = ws.chunkHasUnsavedEdits(3, 2);
        const int traceBefore3 = gs.evictionTrace().size();
        gs.notePlayerChunk(6, 5);
        gs.stepTick(0.11); // 驱逐拍：目标区 mob+item 经 sink 先移除 → 转移 ⑥⑦；见证者 (5,5) 在
                           //   新 gen 窗内照常请求物化，实体不误伤。选址算术：cheb((3,2),(6,5))
                           //   = 3 ∈ (1,3] annulus（scan 3 窗内）→ (3,2) 恰入 toEvict；
                           //   cheb((5,5),(6,5)) = 1 ∈ gen 窗 → 见证者随行请求、不误伤。
        const ChunkEvictor::Report rep3 = gs.lastEvictionReport();
        QString tr3Diag;
        const QVector<GameSession::EvictionTraceEvent> tr3 = gs.evictionTrace();
        for (int i = traceBefore3; i < tr3.size(); ++i)
            tr3Diag += QStringLiteral("%1:(%2,%3) ").arg(int(tr3[i].kind))
                           .arg(tr3[i].cx).arg(tr3[i].cz);

        const bool entityOk = em.liveCount() == 1 // 恰见证者 mob 存活
            && iem.liveCount() == 1 // 恰见证者 item 存活
            && !em.aliveAt(mobSlot) && em.aliveAt(witMobSlot)
            && ws.chunks().lifecycleAt(3, 2) == ChunkLifecycle::Absent;
        ok = ok && entityOk;
        if (!entityOk)
            diag += QStringLiteral("[entity mlc=%1 ilc=%2 tgt=%3 wit=%4 l32=%5 | pre l32=%6 dirty=%7 | rep p=%8 e=%9 a=%10 sk=%11 | tr3 %12] ")
                        .arg(em.liveCount()).arg(iem.liveCount())
                        .arg(em.aliveAt(mobSlot)).arg(em.aliveAt(witMobSlot))
                        .arg(int(ws.chunks().lifecycleAt(3, 2)))
                        .arg(l32BeforeEdge).arg(dirtyBeforeEdge)
                        .arg(rep3.persisted).arg(rep3.evicted).arg(rep3.aborted).arg(rep3.skipped)
                        .arg(tr3Diag);

        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2025c failure abort and entity unload semantics: an exclusive"
                             " second-connection lock makes the dirty candidate abort in place"
                             " (resident, content and edit ledger intact, persist-fail traced"
                             " with zero transition events and the abort counted), the next"
                             " position edge after unlock re-reports and converges with one"
                             " persisted row, and the eviction of a chunk holding a mob and a"
                             " dropped item removes exactly those through the pre-transition"
                             " entity sink while the in-radius witnesses survive and the chunk"
                             " still reaches Absent" << (ok ? QString() : diag);
    });

    // ── r2025d：结构钉 ──────────────────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2025d structure pins (comment-stripped source pins hold the eviction"
        " wiring on its authorized seams: worldstore stays blind to the per-chunk edit table,"
        " the additive table is created idempotently with no version bump and no destructive"
        " SQL, the evictor persists before it drives any transition, the session binds the"
        " world dirty face and the additive store on the authorized seams with the entity sink"
        " ahead of the transition and the slot release after edge-absent, the reload routing"
        " consults the store before the regeneration fallback, the restore body materializes"
        " without any population replay, both entity families expose the single chunk-despawn"
        " funnel, QML stays free of every new token, and the fixed generation chain keeps its"
        " edit-tracking suppression windows)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };

        // ① worldstore 禁触反探（miss 非空 = 合规）：附加表不漏一字进 worldstore（additive
        //    路线 = 新文件承载，worldstore.{h,cpp} 一行禁触）。
        const bool storeBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                                     "chunk_edits")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "ChunkStore")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "chunk_edits")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "ChunkStore")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "chunkstore");
        ok = ok && storeBlind;
        if (!storeBlind)
            diag += QStringLiteral("[store-blind] ");

        // ② 附加表 additive 正面钉（剥注释）：幂等建表 + upsert 在位；零 bump / 零破坏性 SQL
        //    反探（miss 非空 = 合规——user_version / PRAGMA / ALTER / DROP 一律缺席）。
        const QStringList missCs = pinSet(
            srcRoot + QStringLiteral("/World/chunkstore.cpp"), {
                SrcPin("idempotent additive create", "CREATE TABLE IF NOT EXISTS", 1),
                SrcPin("per-chunk upsert", "INSERT OR REPLACE INTO", 1),
            });
        for (const QString &m : missCs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QString csH = srcRoot + QStringLiteral("/World/chunkstore.h");
        const bool noBump = forbiddenAbsent(csH, "user_version")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"), "user_version")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"), "PRAGMA")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"), "ALTER TABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"), "DROP TABLE");
        ok = ok && noBump;
        if (!noBump)
            diag += QStringLiteral("[no-bump] ");

        // ③ 先落盘后转移源钉（P3 铁律组件面）：chunkevictor.h 内 persist 调用位序先于边⑥驱动。
        {
            QFile f(srcRoot + QStringLiteral("/World/chunkevictor.h"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const qsizetype iP = src.indexOf(QStringLiteral("m_persist(c.cx, c.cz)"));
            const qsizetype iE = src.indexOf(
                QStringLiteral("m_transition(c.cx, c.cz, ChunkLifecycle::Evicting)"));
            const bool orderPin = iP >= 0 && iE > iP;
            ok = ok && orderPin;
            if (!orderPin)
                diag += QStringLiteral("[persist-order p=%1 e=%2] ").arg(iP).arg(iE);
        }

        // ④ 会话接线正面钉：dirtyQuery/persist/转移三缝 + savedContentQuery/evictor 转正 +
        //    实体缝先于转移 + 擦槽随边⑦ + 重载路由（store 命中先行、重derive 回退在后）。
        {
            QFile f(srcRoot + QStringLiteral("/Game/gamesession.h"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const QStringList missGs = pinSet(
                srcRoot + QStringLiteral("/Game/gamesession.h"), {
                    SrcPin("dirty query seam", "m_world.chunkHasUnsavedEdits(cx, cz)", 1),
                    SrcPin("persist seam", "m_chunkStore->persistChunk(cx, cz, *c)", 1),
                    SrcPin("edit ledger clear", "m_world.clearChunkUnsavedEdits(cx, cz)", 1),
                    SrcPin("transition seam", "m_world.setChunkLifecycle(cx, cz, target)", 1),
                    SrcPin("entity sink seam", "m_entityEvictSink(cx, cz)", 1),
                    SrcPin("slot release face", "m_world.releaseStreamingChunk(cx, cz)", 1),
                    SrcPin("evictor turned on", "m_streamDriver->setEvictor(", 1),
                    SrcPin("saved content query", "m_streamDriver->setSavedContentQuery(", 1),
                    SrcPin("blob restore route", "m_world.restoreChunkFromBlob(data->key.cx", 1),
                    SrcPin("regeneration fallback", "m_world.adoptGeneratedChunk(data->key.cx", 1),
                });
            for (const QString &m : missGs) {
                ok = false;
                diag += QStringLiteral("[%1] ").arg(m);
            }
            // 位序钉：实体移除 < 转移调用；blob 路由 < 重derive 回退（同函数域内）。
            const qsizetype iSink = src.indexOf(QStringLiteral("m_entityEvictSink(cx, cz)"));
            const qsizetype iTrans = src.indexOf(
                QStringLiteral("m_world.setChunkLifecycle(cx, cz, target)"));
            const qsizetype iRestore = src.indexOf(QStringLiteral("m_world.restoreChunkFromBlob(data->key.cx"));
            const qsizetype iAdopt = src.indexOf(QStringLiteral("m_world.adoptGeneratedChunk(data->key.cx"));
            const bool orderOk = iSink >= 0 && iTrans > iSink && iRestore >= 0 && iAdopt > iRestore;
            ok = ok && orderOk;
            if (!orderOk)
                diag += QStringLiteral("[gs-order sink=%1 trans=%2 restore=%3 adopt=%4] ")
                            .arg(iSink).arg(iTrans).arg(iRestore).arg(iAdopt);
        }

        // ⑤ restore 跳 population 函数域钉：restoreChunkFromBlob 函数体（至 releaseStreamingChunk
        //    定义为止）内零 population 记号 + 三要素在位（memcpy 面 + ③晋升 + 列种子光）。
        {
            QFile f(srcRoot + QStringLiteral("/World/world.cpp"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const qsizetype b = src.indexOf(
                QStringLiteral("bool World::restoreChunkFromBlob(int cx, int cz"));
            const qsizetype e = src.indexOf(
                QStringLiteral("bool World::releaseStreamingChunk(int cx, int cz)"));
            const QString body = (b >= 0 && e > b) ? src.mid(b, e - b) : QString();
            const bool bodyOk = body.size() > 0
                && !body.contains(QStringLiteral("sparsePopulateChunk"))
                && body.contains(QStringLiteral("std::memcpy(chunk->voxelDataMut()"))
                && body.contains(QStringLiteral("ChunkLifecycle::Loaded"))
                && body.contains(QStringLiteral("refloodBox("));
            ok = ok && bodyOk;
            if (!bodyOk)
                diag += QStringLiteral("[restore-body sz=%1] ").arg(body.size());
        }

        // ⑥ 实体单漏斗钉：两族声明+定义 + Adapter 一行委托（模拟权威 = store，禁复制）。
        const QStringList missEnt = pinSet(
            srcRoot + QStringLiteral("/Entities/entitymanager.h"),
            { SrcPin("mob chunk despawn decl", "int despawnInChunk(int cx, int cz);", 1) });
        const QStringList missEntCpp = pinSet(
            srcRoot + QStringLiteral("/Entities/entitymanager.cpp"),
            { SrcPin("mob chunk despawn def", "int EntityManager::despawnInChunk(int cx, int cz)", 1) });
        const QStringList missEst = pinSet(
            srcRoot + QStringLiteral("/Entities/entitystore.h"),
            { SrcPin("item chunk despawn decl", "int despawnInChunk(int cx, int cz);", 1) });
        const QStringList missEstCpp = pinSet(
            srcRoot + QStringLiteral("/Entities/entitystore.cpp"),
            { SrcPin("item chunk despawn def", "int EntityStore::despawnInChunk(int cx, int cz)", 1) });
        const QStringList missAdapter = pinSet(
            srcRoot + QStringLiteral("/Game/itementitymanager.h"),
            { SrcPin("adapter one-line delegate", "return m_store.despawnInChunk(cx, cz);", 1) });
        for (const QString &m : missEnt + missEntCpp + missEst + missEstCpp + missAdapter) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑦ QML 零触碰反探（miss 非空 = 合规）：W3 新记号一个都不入 QML。
        const QString qml = srcRoot + QStringLiteral("/ui/Main.qml");
        const bool qmlOk = forbiddenAbsent(qml, "ChunkStore")
            && forbiddenAbsent(qml, "chunk_edits")
            && forbiddenAbsent(qml, "restoreChunkFromBlob")
            && forbiddenAbsent(qml, "despawnInChunk")
            && forbiddenAbsent(qml, "evictionTrace");
        ok = ok && qmlOk;
        if (!qmlOk)
            diag += QStringLiteral("[qml] ");

        // ⑧ fixed 墙：生成写抑制窗恰三处（generate / sparseGenerateChunk / adoptGeneratedChunk）
        //    + 漏斗标记恰一处（单漏斗面）。
        const QStringList missW = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"),
            { SrcPin("generation suppression windows", "UnsavedEditWriteWindow unsavedEditSuppression(m_chunks);", 3) });
        const QStringList missCm = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.cpp"),
            { SrcPin("unsaved edit funnel mark", "m_unsavedEdits.insert(ChunkKey{ cx, cz }.packed());", 1) });
        for (const QString &m : missW + missCm) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2025d structure pins: worldstore stays blind to the edit"
                             " table, the additive table carries idempotent create and upsert"
                             " with zero version bump and zero destructive SQL, the evictor"
                             " persists before transitions, the session binds the authorized"
                             " seams with the entity sink ahead of the transition and the slot"
                             " release on edge-absent, the reload routing consults the store"
                             " first, the restore body holds no population replay, both entity"
                             " families expose the single chunk-despawn funnel with the adapter"
                             " delegating, QML stays clean, and the generation chain keeps its"
                             " three suppression windows over a single funnel mark"
                          << (ok ? QString() : diag);
    });
}
