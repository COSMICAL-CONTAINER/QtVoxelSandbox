// tools/matrix/section45_savepath_opt.cpp —— t1070 流式/存档路径优化合集三件 探针段（4 腿
// r2044a-d；filter 词 r2044；矩阵 707→707+4）。置尾先例沿用（接 section44，runAll 末执行）。
// rig 世界 w 零接触；真实用户 saves/ 零触碰（临时库 QDir::temp() pid 键名，fresh + 用后即删）。
//
// 任务契约（dev-plan t1070 三件；登记源 = §29.5-W3 两件 + t1057/Review_2026-09-18 #1 一件）：
//   件一 = Load job worker 空跑消除：走回未编辑 chunk 时 savedContentQuery 命中 → Load kind
//     job → BackgroundGenerationWorker 旧形态仍 generateTerrainChunk 造一份注定被 blob 回灌
//     覆盖的地形缓冲。修法（读码定选型）= worker 侧消费 submit 前置的 kind 域短路地形生成，
//     投「键载体信封」（loadCarrier，零地形载荷）；数据面唯一通路不变（收割拍 blob 物化 /
//     读败降级确定性重生成——数据从不来自 worker）。
//   件二 = hasChunk 连接经济学：量化结论（chunkstore.h hasChunk 注「t1070 件二量化」）= 保留
//     逐查询开-用-关现状（不加存在缓存/复用/批查询——错缓存面 = 走回丢编辑），只收收割拍
//     hasChunk && loadChunk 冗余双查（loadChunk 自带 miss/读败面，语义逐位等价）。
//   件三 = 冻结缓冲跨保存复用：桥持长活 coordinator（savebridge.h m_coord——r2015 复用前提
//     兑现；旧逐保存栈上实例 = Review_2026-09-18 #1 指认面）；陈旧冻结点防面 = 快照每保存
//     重新冻结 + beginLoad 每保存重置缓冲网格（复用的只是 World 壳）——冻结点语义逐位不弱化。
//
// 被测面拆解（三柱互补）：件一承重 = 真会话真线程走回波（9 编辑 chunk 全走 Load 路由）+
//   worker 双计数对账（executed 推进而 generated 恒冻 = 「执行了但零生成」的行为级锚）+ 逐位
//   恒等回灌（r2025b 同族口径）；件三承重 = 真链三保存（唯一 dims 64×64×96——全套件桥链唯一，
//   首保存重建恰 +1、二三次保存复用恒 0[bufferRebuildCount 计数断言]）+ 冻结点三面（含变更 /
//   窗内漂移不泄 / 活体漂移真实）+ 台账 Clean 1/1→2/2→3/3；结构钉 = 单一权威源钉族（NEG-1
//   韧性选针：token/序/信封针在 `false &&` 前缀变异下逐位存活——红面归行为腿专钉）+ 退役
//   双查反探 + 量化注锚 + 桥顶针（栈上实例恰 1 = 只读恢复面）+ 禁触面反探（worldstore 与
//   chunk_edits 数据面零触碰）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1（backgroundgeneration.h）= 摘件一短路（kind 门加 `false && ` 前缀 = Load 恢复空跑
//     生成）→ 声明红面 {r2044b}（走回全 Load 波的 generatedCount 冻结柱红；内容/生命周期/
//     收割面不红——信封路由与 blob 回灌不受影响[生成物照旧被覆盖丢弃，正是被消除的浪费]；
//     r2044a 不误伤[fixed 零 worker]；r2044c 不误伤[保存链零涉]；r2044d 不误伤——选针刻意
//     韧性：Load token 针/序针/信封针在 `false &&` 前缀下文本逐位存活，语义红面归 b 专钉）。
//   NEG-2（savebridge.cpp）= 摘件三长活（`SaveCoordinator &coord = m_coord;` 回退栈上局部
//     实例）→ 声明红面 {r2044c, r2044d} 同源双红（c：成员 coordinator 永不保存 → 重建计数
//     恒 0 → 「首保存 +1 / 后续 0」柱双红；d：栈上实例顶针 [恰 1] 被第二处栈上实例击穿——
//     同源双红按 r2043 NEG-1 声明先例如实预声明；r2044a 不误伤[组件面用腿内自建栈实例，与
//     桥无关]；r2044b 不误伤[流式波零保存]）。
//   阴性日志：build/ 下四件 matrix_r2044_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 腿名纪律：腿名/diag/PASS 文本零跨任务 filter 词元（r2018 P2 双向污染教训）；相邻族基线
//   计数只出现在钉 needle 与被钉文件路径实参里，不进任何输出行。
#include "matrix_helpers.h"

#include "chunklifecycle.h"  // 被测面：六态转移（走回波生命周期边断言）
#include "chunkstore.h"      // 被测面：per-chunk 附加表（回灌行面 + 件二量化注锚所在文件）
#include "gamesession.h"     // 被测面：流式会话收割拍（件一/件二路由宿主）
#include "savebridge.h"      // 被测面：t1070 件三长活 coordinator 宿主（QML 单例 = 生产同对象）
#include "savecoordinator.h" // 被测面：冻结缓冲重建诊断面（件三计数权威）

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

void MatrixRun::section45_savepath_opt()
{
    constexpr int kW48 = 48, kD48 = 48, kH96 = 96, kSeed48 = 82;  // fixed 小世界（3×3 chunk）
    constexpr int kW64 = 64, kD64 = 64;                            // 件三唯一 dims（4×4 chunk）
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42;         // sparse 核心域（section28 同族）

    const QString projRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absolutePath();
    const QString srcRoot = projRoot + QStringLiteral("/src");
    const QString matrixRoot = projRoot + QStringLiteral("/tools/matrix");

    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰——section28/33 同门）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2044_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // fresh fixed 小世界 incantation（section33/44 同款：天气双钉——转换掷骰不进探针窗）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kW48);
        w.setDepth(kD48);
        w.setHeight(kH96);
        w.setSeed(kSeed48);
        w.setWeatherState(0);
        w.setWeatherRemainingSec(3600.0f);
    };
    const auto freshWorld64 = [](World &w) {
        w.setWidth(kW64);
        w.setDepth(kD64);
        w.setHeight(kH96);
        w.setSeed(kSeed48);
        w.setWeatherState(0);
        w.setWeatherRemainingSec(3600.0f);
    };
    // 地表标记放置（选址纪律：显式读高度 + 放置回读校验——section33 同门）。
    const auto placeMarker = [](World &w, int x, int z) -> int {
        const int h = w.heightAt(x, z);
        if (h < 0 || h + 2 >= w.height()) return -1;
        w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
        if (w.blockAt(x, h + 1, z) != quint8(BR::Stone)) return -1;
        return h + 1;
    };
    // 栅格指纹（blockAt+stateAt 全栅格——冻结点逐位恒等的比较面；section33 同门）。
    const auto gridOf = [](World &w) {
        QByteArray g;
        g.resize(w.width() * w.depth() * w.height() * 2);
        int i = 0;
        for (int x = 0; x < w.width(); ++x)
            for (int z = 0; z < w.depth(); ++z)
                for (int y = 0; y < w.height(); ++y) {
                    g[i++] = char(w.blockAt(x, y, z));
                    g[i++] = char(w.stateAt(x, y, z));
                }
        return g;
    };
    // 重读档 incantation（rebind 纪律：setWorld 先于 loadChunks——section33 同门）。
    const auto reloadWorld = [](WorldStore &store, World &target, int seed, int expectChunks,
                                QString &why) {
        target.beginLoad(seed); // 零填充分区网格（loadChunks 前置，同 Main.qml 载入流）
        store.setWorld(&target);
        const bool opened = store.isOpen();
        const int n = opened ? store.loadChunks() : -1;
        target.finishLoad();
        if (n != expectChunks) {
            why = QStringLiteral("loadChunks=%1 expect=%2 opened=%3")
                      .arg(n).arg(expectChunks).arg(opened);
            return false;
        }
        return true;
    };
    // 稀疏世界核（零预生成：全量走流式链——section28 r2025b 同款）。
    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0;
        return World(sp);
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
    // 全 chunk 体素快照/比对（16×16 列 × 全 y 带 id+state——回灌逐位恒等柱的承载面）。
    const auto snapChunk = [&](World &w, int cx, int cz) {
        QVector<QPair<int, int>> vox;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kH; ++y)
                    vox.append({ w.blockAt(cx * 16 + lx, y, cz * 16 + lz),
                        w.stateAt(cx * 16 + lx, y, cz * 16 + lz) });
        return vox;
    };
    const auto chunkIdentical = [&](World &w, int cx, int cz,
                                    const QVector<QPair<int, int>> &snap, long &equalCount,
                                    QString &diffDiag) {
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
    // 头注/注释体裸读（量化注锚在注释体——pinSet 剥注释会失配，raw 先例 section43/44 同门）。
    const auto rawContains = [](const QString &path, const char *needle) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        return QString::fromUtf8(f.readAll()).contains(QString::fromUtf8(needle));
    };
    const auto rawIndexOf = [](const QString &path, const char *needle) -> qsizetype {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return -1;
        return QString::fromUtf8(f.readAll()).indexOf(QString::fromUtf8(needle));
    };
    // 禁触反探（needle 在场即红——section33 r2031d 同款帮手）。
    const auto forbiddenAbsent = [rawContains](const QString &path, const char *needle) {
        return !rawContains(path, needle);
    };

    // 真链引擎装配（真 QQmlEngine + 上下文注入 + setData wrapper——section33 同门最小面）。
    struct SaveRig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeSaveRig = [](SaveRig &rig, SaveBridge *bridge, WorldStore *store) {
        qputenv("QML_DISABLE_DISK_CACHE", "1");
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2044Bridge"), bridge);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2044Store"), store);
        QObject::connect(&rig.engine, &QQmlEngine::warnings, &rig.engine,
            [&rig](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++rig.warnings;
                    if (rig.firstWarning.isEmpty())
                        rig.firstWarning = e.toString();
                }
            });
        QQmlComponent comp(&rig.engine);
        comp.setData(R"QML(import QtQuick
Item {
    property string file: ""
    property bool saved: false
    function exitSave(px, stat) {
        saved = r2044Bridge.saveViaCoordinator(r2044Store, file, "r2044c",
            [{x: 4, y: 5, z: 6, slots: [{id: 3, count: 7}]}],
            [{x: 14, y: 15, z: 16, slots: [{id: 9, count: 2}], burn: 3, smelt: 4}],
            [{x: 24, y: 25, z: 26, slots: [{id: 12, count: 5}]}],
            {phase: 0.25, day: 3, weather: 1, weatherTimerMs: 45000},
            {valid: true, x: 5.5, y: 6.5, z: 7.5},
            {px: px, py: 41.5, pz: 8.5, yaw: 1.25, pitch: -0.5, mode: 0,
             health: 17, hunger: 19, xp: 2},
            {statPlayedMinutes: stat})
        return saved
    }
}
)QML", QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine); // 引擎析构兜底
    };
    const auto qmlExitSave = [](SaveRig &rig, double px, int stat) {
        QVariant savedOut(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "exitSave", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, savedOut),
                Q_ARG(QVariant, px), Q_ARG(QVariant, stat));
        return savedOut.toBool();
    };

    SaveBridge &bridge = *SaveBridge::instance();
    bridge.setFaultHook(SaveFaultHook()); // 桥间复位缝（singleton 跨腿共享——入口归零 = 生产形态）

    // ── r2044a：零变化墙（相邻族基线计数 + 组件保存面逐位双保存 + fixed 会话惰性面）────────
    runLeg(QStringLiteral("r2044a zero-change wall for the three save-path items (the adjacent"
        " probe families keep their exact four-leg entry counts in source, a fixed-world"
        " coordinated save through a leg-owned stack coordinator keeps the component face"
        " bitwise across two consecutive saves - the second save reuses the frozen buffer at"
        " the same dimensions, both receipts answer ok with the ledger clean at successive"
        " generations, and each reload matches the live grid at its own save moment bit for"
        " bit with the per-save marker present - and a fixed-world session stays fully inert"
        " with no additive store constructed and zero lifecycle, revision or trace movement"
        " through a full player-edge tick sweep)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 相邻族腿名计数源钉（基线同底——needle 含跨段词元 = 被钉源码事实，不进任何输出行）。
        const QStringList miss19 = pinSet(matrixRoot + QStringLiteral("/section19_savecoordinator.cpp"),
            { SrcPin("save-path wall family baseline", "runLeg(QStringLiteral(\"r2015", 4) });
        const QStringList miss28 = pinSet(matrixRoot + QStringLiteral("/section28_eviction_persistence.cpp"),
            { SrcPin("save-path wall family baseline", "runLeg(QStringLiteral(\"r2025", 4) });
        const QStringList miss33 = pinSet(matrixRoot + QStringLiteral("/section33_savebridge_wiring.cpp"),
            { SrcPin("save-path wall family baseline", "runLeg(QStringLiteral(\"r2031", 4) });
        for (const QString &m : miss19 + miss28 + miss33) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 组件保存面逐位（腿内自建栈 coordinator——组件语义零变化的直接行为面；两次连存
        //    走同一实例 = 组件级 dims 复用路径逐字节正确[件三不触组件语义的正面实证]）。
        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        const int mAy = placeMarker(w, 10, 10);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");
        const QByteArray gRef1 = gridOf(w); // 保存 #1 时刻的活体栅格（冻结点基准）

        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        SaveCoordinator coord;
        coord.bind(&store, db);
        const int c0 = store.saveOkCount();

        SaveRequest rq1;
        rq1.name = QStringLiteral("r2044a-one");
        // 只请求 world 部分（player/progress 空 map = 不请求 → skip 语义：仅 world 计 +1，
        //   playerSaved/progressSaved false 不计错——r2015b 同款请求形态与计数口径）。
        const SaveReceipt rc1 = coord.saveAll(rq1);
        const SaveGenerationInfo f1 = coord.recover();
        const bool s1Ok = rc1.ok() && rc1.generation == 1 && rc1.worldSaved
            && !rc1.playerSaved && !rc1.progressSaved
            && store.saveOkCount() == c0 + 1
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1;
        ok = ok && s1Ok;
        if (!s1Ok)
            diag += QStringLiteral("[s1 ok=%1 g=%2 w=%3 p=%4 pr=%5 c=%6/%7 st=%8] ")
                        .arg(rc1.ok()).arg(rc1.generation).arg(rc1.worldSaved)
                        .arg(rc1.playerSaved).arg(rc1.progressSaved)
                        .arg(store.saveOkCount()).arg(c0 + 1).arg(int(f1.state));

        {
            World w2;
            freshWorld48(w2);
            QString why;
            const bool load1 = reloadWorld(store, w2, kSeed48, 9, why);
            store.setWorld(&w);
            const bool round1 = load1 && gridOf(w2) == gRef1
                && w2.blockAt(10, mAy, 10) == quint8(BR::Stone);
            ok = ok && round1;
            if (!round1)
                diag += QStringLiteral("[round1 %1 same=%2] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(gridOf(w2) == gRef1 ? 1 : 0);
        }

        // 活体推进（B 标记）→ 第二次保存（同 coordinator = dims 复用路径）→ 冻结点含 B。
        const int mBy = placeMarker(w, 40, 40);
        ok = ok && mBy > 0;
        if (mBy <= 0) diag += QStringLiteral("[siteB] ");
        const QByteArray gRef2 = gridOf(w);

        SaveRequest rq2;
        rq2.name = QStringLiteral("r2044a-two");
        const SaveReceipt rc2 = coord.saveAll(rq2);
        const SaveGenerationInfo f2 = coord.recover();
        const bool s2Ok = rc2.ok() && rc2.generation == 2
            && store.saveOkCount() == c0 + 2
            && f2.state == SaveRecoveryState::Clean && f2.generation == 2;
        ok = ok && s2Ok;
        if (!s2Ok)
            diag += QStringLiteral("[s2 ok=%1 g=%2 c=%3 st=%4] ")
                        .arg(rc2.ok()).arg(rc2.generation)
                        .arg(store.saveOkCount()).arg(int(f2.state));
        {
            World w3;
            freshWorld48(w3);
            QString why;
            const bool load2 = reloadWorld(store, w3, kSeed48, 9, why);
            store.setWorld(&w);
            const bool round2 = load2 && gridOf(w3) == gRef2
                && w3.blockAt(40, mBy, 40) == quint8(BR::Stone)
                && w3.blockAt(10, mAy, 10) == quint8(BR::Stone);
            ok = ok && round2;
            if (!round2)
                diag += QStringLiteral("[round2 %1 same=%2] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(gridOf(w3) == gRef2 ? 1 : 0);
        }
        store.closeWorld();
        QFile::remove(db);

        // ③ fixed 会话惰性面：无附加表（对象不存在 = 连接经济学整域不可达）+ 全 tick 扫掠
        //    零生命周期/revision/轨迹活动（紧凑化——完整承重墙归既有 fixed 零活动腿族）。
        {
            World wf;
            freshWorld48(wf);
            GameSession gsf(wf);
            const int rev0 = wf.residentChunkRevision();
            const ChunkLifecycle lc0 = wf.chunks().lifecycleAt(1, 1);
            for (int i = 0; i < 8; ++i)
                gsf.stepTick(0.11);
            const bool inertOk = gsf.chunkEditsStore() == nullptr
                && wf.residentChunkRevision() == rev0
                && wf.chunks().lifecycleAt(1, 1) == lc0
                && gsf.evictionTrace().isEmpty();
            ok = ok && inertOk;
            if (!inertOk)
                diag += QStringLiteral("[inert store=%1 rev=%2 trace=%3] ")
                            .arg(gsf.chunkEditsStore() == nullptr)
                            .arg(wf.residentChunkRevision() - rev0)
                            .arg(gsf.evictionTrace().size());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2044a zero-change wall: the adjacent probe families keep"
                             " their four-leg entry counts, the component save face stays"
                             " bitwise across two consecutive saves on one coordinator (the"
                             " second reuses the frozen buffer at the same dimensions, both"
                             " receipts ok with the ledger clean at successive generations,"
                             " each reload matches its own save-moment grid with markers in"
                             " place), and the fixed-world session stays inert with no"
                             " additive store and zero lifecycle or revision movement"
                          << (ok ? QString() : diag);
    });

    // ── r2044b：件一承重（走回全 Load 波：worker 双计数对账 + 逐位恒等回灌 + 行面稳定）────
    runLeg(QStringLiteral("r2044b load-job idle-run elimination load-bearing wall (the scan-"
        "annulus eviction batch persists the four edited chunks it evicts, and the walk-back"
        " wave re-enters those four through the load kind exclusively: the worker executes each"
        " completion while its terrain-generation count stays frozen at the pre-wave value,"
        " both counters having provably advanced on the two generate waves that bracket the"
        " walk-back, the four chunks converge to Loaded through the usual lifecycle edges while"
        " the five far chunks stay resident untouched, both sampled chunk snapshots stay"
        " bitwise identical to their pre-evict capture with the player edits in place, the"
        " restored chunks start clean, and the additive table keeps exactly four rows)"), [&]() {
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

        const BackgroundGenerationWorker *worker = gs.streamWorker();
        ok = ok && worker != nullptr;
        if (!worker)
            diag += QStringLiteral("[worker] ");

        QVector<QPair<int, int>> homeKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                homeKeys.append({ cx, cz });

        // ① 初载波（全 Generate）：地形生成计数真实前进（计数器绑定面）。
        gs.notePlayerChunk(2, 2);
        QString d1;
        const bool c1 = convergeLoaded(gs, ws, homeKeys, 45000, d1);
        ok = ok && c1;
        if (!c1)
            diag += d1;
        const quint64 g1 = worker ? worker->generatedCount() : 0;
        const quint64 e1 = worker ? worker->executedCount() : 0;
        // 计数器绑定面（下界口径：W1b 在途邻 population 提升可把某邻块先行推到 Loaded——
        //   单波精确执行数受该机制时序性影响；承重对账在走回波的「生成恒冻」柱）。
        const bool wave1Ok = g1 >= 1 && e1 >= 1;
        ok = ok && wave1Ok;
        if (!wave1Ok)
            diag += QStringLiteral("[wave1 g=%1 e=%2] ").arg(g1).arg(e1);

        // ② 九 chunk 全编辑（只有 dirty 候选才落盘 → 走回时九键全命中附加表 = 全 Load 路由）。
        QVector<QPair<int, int>> markerPos; // (x, y) 逐 chunk（z = 同列）
        bool editsOk = true;
        for (const auto &k : homeKeys) {
            const int x = k.first * 16 + 8, z = k.second * 16 + 8;
            const int h = ws.heightmapAt(x, z);
            const bool placeable = h >= 0 && h + 1 < kH && ws.blockAt(x, h + 1, z) == 0;
            const bool placed = placeable && ws.setBlock(x, h + 1, z, BR::Stone);
            editsOk = editsOk && placed && ws.chunkHasUnsavedEdits(k.first, k.second);
            markerPos.append({ x, h + 1 });
        }
        ok = ok && editsOk;
        if (!editsOk)
            diag += QStringLiteral("[edits n=%1] ").arg(markerPos.size());

        // ③ 快照两块（(2,2) 中心 + (3,3) 角——两块都在扫描环带驱逐批内，走回走 blob 物化）。
        const QVector<QPair<int, int>> snap22 = snapChunk(ws, 2, 2);
        const QVector<QPair<int, int>> snap33 = snapChunk(ws, 3, 3);

        // ④ 走离波（新窗九键无行 = 全 Generate）：扫描环带批（cheb ∈ (1,3] from (5,5)）驱逐
        //    恰四块（(2,2)/(2,3)/(3,2)/(3,3)，全部 dirty → 恰四行落盘）；远五块（cheb > 3）
        //    越出扫描窗 = 本批不逐，保持驻留（r2025b 同款批口径：批候选数 × 驱逐频率的
        //    「批候选数」即此环带批）。
        gs.notePlayerChunk(5, 5);
        gs.stepTick(0.11); // 驱逐拍（同步：decide → evictor → 转移缝）
        const ChunkEvictor::Report rep = gs.lastEvictionReport();
        const bool evictOk = ws.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent
            && ws.chunks().chunk(2, 2) == nullptr
            && ws.chunks().lifecycleAt(3, 3) == ChunkLifecycle::Absent
            && ws.chunks().lifecycleAt(1, 1) == ChunkLifecycle::Loaded // 远块越出扫描窗不误逐
            && rep.persisted == 4 && rep.evicted == 4 && rep.aborted == 0;
        ok = ok && evictOk;
        if (!evictOk)
            diag += QStringLiteral("[evict l22=%1 rel=%2 l33=%3 l11=%4 p=%5 e=%6 a=%7] ")
                        .arg(int(ws.chunks().lifecycleAt(2, 2)))
                        .arg(ws.chunks().chunk(2, 2) != nullptr)
                        .arg(int(ws.chunks().lifecycleAt(3, 3)))
                        .arg(int(ws.chunks().lifecycleAt(1, 1)))
                        .arg(rep.persisted).arg(rep.evicted).arg(rep.aborted);
        QVector<QPair<int, int>> awayKeys;
        for (int cz = 4; cz <= 6; ++cz)
            for (int cx = 4; cx <= 6; ++cx)
                awayKeys.append({ cx, cz });
        QString d4;
        const bool c4 = convergeLoaded(gs, ws, awayKeys, 45000, d4);
        ok = ok && c4;
        if (!c4)
            diag += d4;
        const quint64 g2 = worker ? worker->generatedCount() : 0;
        const quint64 e2 = worker ? worker->executedCount() : 0;
        const bool wave2Ok = g2 > g1 && e2 > e1; // 走离 Generate 波两计数都前进（绑定面）
        ok = ok && wave2Ok;
        if (!wave2Ok)
            diag += QStringLiteral("[wave2 g=%1/%2 e=%3/%4] ").arg(g2).arg(g1).arg(e2).arg(e1);

        // ⑤ 走回波（环带四键全有行 → 全 Load job；远五块仍驻留不被重请求）：执行计数前进
        //    （边①②照常）+ 生成计数恒冻（空跑消除的行为级锚——旧形态此处必然再付 worldgen）。
        gs.notePlayerChunk(2, 2);
        QString d5;
        const bool c5 = convergeLoaded(gs, ws, homeKeys, 60000, d5);
        ok = ok && c5;
        if (!c5)
            diag += d5;
        const quint64 g3 = worker ? worker->generatedCount() : 0;
        const quint64 e3 = worker ? worker->executedCount() : 0;
        const bool idleGone = e3 > e2 && g3 == g2;
        ok = ok && idleGone;
        if (!idleGone)
            diag += QStringLiteral("[idle e=%1/%2 g=%3==%4] ").arg(e3).arg(e2).arg(g3).arg(g2);

        // ⑥ 逐位恒等回灌（含玩家 Stone 编辑 + population 终态）+ 标记在场 + 重载起点 clean。
        long eq22 = 0, eq33 = 0;
        QString dif22, dif33;
        const bool id22 = chunkIdentical(ws, 2, 2, snap22, eq22, dif22);
        const bool id33 = chunkIdentical(ws, 3, 3, snap33, eq33, dif33);
        const bool mark22 = ws.blockAt(markerPos[4].first, markerPos[4].second, 2 * 16 + 8)
            == quint8(BR::Stone);
        const bool mark33 = ws.blockAt(markerPos[8].first, markerPos[8].second, 3 * 16 + 8)
            == quint8(BR::Stone);
        const bool idOk = id22 && id33 && eq22 >= 24000 && eq33 >= 24000 && mark22 && mark33
            && !ws.chunkHasUnsavedEdits(2, 2) && !ws.chunkHasUnsavedEdits(3, 3);
        ok = ok && idOk;
        if (!idOk)
            diag += QStringLiteral("[id %1eq=%2 %3eq=%4 m22=%5 m33=%6 d22=%7 d33=%8 dirty=%9] ")
                        .arg(id22).arg(eq22).arg(id33).arg(eq33)
                        .arg(mark22).arg(mark33)
                        .arg(dif22).arg(dif33)
                        .arg(ws.chunkHasUnsavedEdits(3, 3));

        // ⑦ 行面：恰四行、四键全命中（远五块未逐 = 未落盘不增行）。
        bool rowsOk = gs.chunkEditsStore() != nullptr
            && gs.chunkEditsStore()->chunkCount() == 4
            && gs.chunkEditsStore()->hasChunk(2, 2)
            && gs.chunkEditsStore()->hasChunk(3, 2) && gs.chunkEditsStore()->hasChunk(2, 3)
            && gs.chunkEditsStore()->hasChunk(3, 3)
            && !gs.chunkEditsStore()->hasChunk(1, 1); // 远块未逐未落盘
        ok = ok && rowsOk;
        if (!rowsOk)
            diag += QStringLiteral("[rows n=%1] ")
                        .arg(gs.chunkEditsStore() ? gs.chunkEditsStore()->chunkCount() : -1);

        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2044b load-job idle-run elimination: the scan-annulus batch"
                             " persists the four edited chunks it evicts and the walk-back"
                             " wave re-enters those four through the load kind exclusively -"
                             " every completion executes with the terrain-generation count"
                             " frozen at the pre-wave value (both counters advanced on the"
                             " bracketing generate waves), the four converge Loaded through"
                             " the usual edges while the five far chunks stay resident, both"
                             " sampled snapshots stay bitwise identical with player edits in"
                             " place, the restored chunks start clean, and the table keeps"
                             " exactly four rows"
                          << (ok ? QString() : diag);
    });

    // ── r2044c：件三承重（真链三保存：重建计数 +1/0/0 + 冻结点三面 + 台账单调 Clean）──────
    runLeg(QStringLiteral("r2044c frozen-buffer cross-save reuse load-bearing wall (through the"
        " production bridge at dimensions unique in the suite the first save rebuilds the"
        " frozen buffer exactly once and the second and third saves rebuild it zero times, the"
        " frozen point never goes stale - a marker placed between the first two saves lands in"
        " the second save's reload, and a mutation injected inside the post-freeze window of"
        " the third save stays out of its reload while the live world provably drifted - and"
        " the ledger advances clean 1/1, 2/2, 3/3 with every receipt true)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        freshWorld64(w);
        const int mAy = placeMarker(w, 10, 10);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");
        const QByteArray gRef1 = gridOf(w); // 保存 #1 冻结点基准

        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        SaveRig rig;
        makeSaveRig(rig, &bridge, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig] ");
        if (rig.wrap)
            rig.wrap->setProperty("file", db); // 存档目标（绝对路径直用——r2031 真链同门）

        // 保存 #1：唯一 dims 首建 → 重建计数恰 +1；台账 Clean 1/1；计数 +3；重读逐位。
        const int c0 = store.saveOkCount();
        const qint64 n0 = bridge.frozenBufferRebuildCount();
        const bool s1 = rig.wrap && qmlExitSave(rig, 11.5, 1);
        const qint64 n1 = bridge.frozenBufferRebuildCount();
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const bool save1Ok = s1 && n1 == n0 + 1
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 && store.saveOkCount() == c0 + 3;
        ok = ok && save1Ok;
        if (!save1Ok)
            diag += QStringLiteral("[s1 s=%1 n=%2/%3 st=%4 g=%5/%6 c=%7/%8] ")
                        .arg(s1).arg(n1).arg(n0 + 1)
                        .arg(int(f1.state)).arg(f1.generation).arg(f1.completeGeneration)
                        .arg(store.saveOkCount()).arg(c0 + 3);
        {
            World w2;
            freshWorld64(w2);
            QString why;
            const bool load1 = reloadWorld(store, w2, kSeed48, 16, why);
            store.setWorld(&w);
            const bool round1 = load1 && gridOf(w2) == gRef1
                && w2.blockAt(10, mAy, 10) == quint8(BR::Stone);
            ok = ok && round1;
            if (!round1)
                diag += QStringLiteral("[round1 %1 same=%2] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(gridOf(w2) == gRef1 ? 1 : 0);
        }

        // 保存 #2（活体先落 B 标记）：复用 → 重建计数恒 0 增量；冻结点含变更（陈旧防面①）。
        const int mBy = placeMarker(w, 40, 40);
        ok = ok && mBy > 0;
        if (mBy <= 0) diag += QStringLiteral("[siteB] ");
        const bool s2 = rig.wrap && qmlExitSave(rig, 22.5, 2);
        const qint64 n2 = bridge.frozenBufferRebuildCount();
        const SaveGenerationInfo f2 = bridge.recoveryInfo(db);
        const bool save2Ok = s2 && n2 == n1
            && f2.state == SaveRecoveryState::Clean && f2.generation == 2;
        ok = ok && save2Ok;
        if (!save2Ok)
            diag += QStringLiteral("[s2 s=%1 n=%2==%3 st=%4 g=%5] ")
                        .arg(s2).arg(n2).arg(n1).arg(int(f2.state)).arg(f2.generation);
        {
            World w3;
            freshWorld64(w3);
            QString why;
            const bool load2 = reloadWorld(store, w3, kSeed48, 16, why);
            store.setWorld(&w);
            const bool round2 = load2 && w3.blockAt(40, mBy, 40) == quint8(BR::Stone)
                && w3.blockAt(10, mAy, 10) == quint8(BR::Stone)
                && gridOf(w3) == gridOf(w);
            ok = ok && round2;
            if (!round2)
                diag += QStringLiteral("[round2 %1 b=%2 same=%3] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(w3.blockAt(40, mBy, 40) == quint8(BR::Stone))
                            .arg(gridOf(w3) == gridOf(w) ? 1 : 0);
        }

        // 保存 #3（复用缓冲上的冻结点语义墙复验）：钩子在冻结后窗内改动活体（拆 A + 落 C）
        //   且不注入失败 → 保存照常收尾；重读 = 冻结点（A 在 C 无）≠ 漂移后活体——承重墙
        //   在长活缓冲上逐位不弱化；重建计数仍 0 增量。
        const int hC = w.heightAt(50, 50); // heightAt = worldgen 纯函数（放置不漂移，先取定）
        ok = ok && hC >= 0 && hC + 1 < w.height();
        const QByteArray gRef3 = gridOf(w); // 保存 #3 冻结点基准（= 漂移前活体：A/B 在、C 无）
        bridge.setFaultHook([&](SaveFaultStage st) {
            if (st != SaveFaultStage::World)
                return false;
            w.setBlock(10, mAy, 10, quint8(BR::Air), 0); // 拆 A（冻结后窗内）
            w.setBlock(50, hC + 1, 50, quint8(BR::Stone), 0); // 落 C（窗内新起）
            return false; // 只制造「活体漂移」，不注入失败
        });
        const bool s3 = rig.wrap && qmlExitSave(rig, 33.5, 3);
        const qint64 n3 = bridge.frozenBufferRebuildCount();
        const SaveGenerationInfo f3 = bridge.recoveryInfo(db);
        bridge.setFaultHook(SaveFaultHook()); // 生产形态复位
        const bool liveDrift = w.blockAt(10, mAy, 10) == quint8(BR::Air)
            && w.blockAt(50, hC + 1, 50) == quint8(BR::Stone);
        const bool save3Ok = s3 && n3 == n1 && f3.state == SaveRecoveryState::Clean
            && f3.generation == 3 && f3.completeGeneration == 3 && liveDrift;
        ok = ok && save3Ok;
        if (!save3Ok)
            diag += QStringLiteral("[s3 s=%1 n=%2==%3 st=%4 g=%5/%6 drift=%7] ")
                        .arg(s3).arg(n3).arg(n1).arg(int(f3.state))
                        .arg(f3.generation).arg(f3.completeGeneration).arg(liveDrift);
        {
            World w4;
            freshWorld64(w4);
            QString why;
            const bool load3 = reloadWorld(store, w4, kSeed48, 16, why);
            store.setWorld(&w);
            const bool round3 = load3 && gridOf(w4) == gRef3
                && w4.blockAt(10, mAy, 10) == quint8(BR::Stone)
                && w4.blockAt(40, mBy, 40) == quint8(BR::Stone)
                && w4.blockAt(50, hC + 1, 50) != quint8(BR::Stone); // 窗内 C 不泄入存档
            ok = ok && round3;
            if (!round3)
                diag += QStringLiteral("[round3 %1 a=%2 b=%3 c=%4 same=%5] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(w4.blockAt(10, mAy, 10) == quint8(BR::Stone))
                            .arg(w4.blockAt(40, mBy, 40) == quint8(BR::Stone))
                            .arg(w4.blockAt(50, hC + 1, 50) != quint8(BR::Stone))
                            .arg(gridOf(w4) == gRef3 ? 1 : 0);
        }

        const bool cleanOk = rig.warnings == 0 && rig.wrap != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2044c frozen-buffer cross-save reuse: the first save at"
                             " suite-unique dimensions rebuilds the frozen buffer exactly"
                             " once and the next two rebuild it zero times, the frozen point"
                             " never goes stale (an between-saves marker lands in the second"
                             " reload and a post-freeze window mutation stays out of the"
                             " third reload while the live world drifted), and the ledger"
                             " advances clean 1/1, 2/2, 3/3 with every receipt true"
                          << (ok ? QString() : diag);
    });

    // ── r2044d：结构钉（件一权威源钉族[NEG-1 韧性选针] + 件二量化注锚 + 件三桥顶针 + 禁触面）──
    runLeg(QStringLiteral("r2044d structure pins (the load-kind short circuit sits at its single"
        " worker authority with the load token ahead of the terrain-generation call, the"
        " carrier-envelope mark and the generation diagnostics face in place; the harvest beat"
        " routes the degraded carrier through deterministic regeneration with the blob route"
        " ahead of the generation fallback and the retired per-completion double query gone;"
        " the connection-economics quantification note stands at the query authority; the"
        " bridge carries the long-lived coordinator with the rebuild counter wired at its"
        " single dims-mismatch authority and exactly one stack construction left in the"
        " read-only recovery face; and the forbidden surfaces stay clean - the worldstore pair"
        " and the additive-table files carry none of the three item tokens)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 件一权威源钉族（选针刻意韧性：token/序/信封针在 `false && ` 门变异下逐位存活——
        //    语义红面归 r2044b 行为腿专钉，恰红面设计见段头）。
        const QString bgPath = srcRoot + QStringLiteral("/World/backgroundgeneration.h");
        const QStringList missBg = pinSet(bgPath, {
            SrcPin("load kind token at worker authority", "GenerationJobKind::Load", 1),
            SrcPin("carrier envelope mark", "data->loadCarrier = true;", 1),
            SrcPin("carrier field on the payload type", "bool loadCarrier = false;", 1),
            SrcPin("generation diagnostics face", "quint64 generatedCount() const", 1),
        });
        for (const QString &m : missBg) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const qsizetype iLoad = rawIndexOf(bgPath, "GenerationJobKind::Load");
        const qsizetype iGen = rawIndexOf(bgPath, "generateTerrainChunk(m_terrain, t.req.key)");
        const bool orderOk = iLoad >= 0 && iGen > iLoad; // 短路门先于生成调用（单一权威位序）
        ok = ok && orderOk;
        if (!orderOk)
            diag += QStringLiteral("[bg-order load=%1 gen=%2] ").arg(iLoad).arg(iGen);

        // ② 收割拍路由钉（件一降级面 + 件二退役双查反探 + 既有 blob/回退序锚复钉）。
        const QString gsPath = srcRoot + QStringLiteral("/Game/gamesession.h");
        const QStringList missGs = pinSet(gsPath, {
            SrcPin("carrier degraded route", "m_world.loadChunkAt(data->key.cx", 1),
            SrcPin("blob restore route", "m_world.restoreChunkFromBlob(data->key.cx", 1),
            SrcPin("generate fallback route", "m_world.adoptGeneratedChunk(data->key.cx", 1),
            SrcPin("carrier routing consumer", "data->loadCarrier", 1),
        });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const qsizetype iRestore = rawIndexOf(gsPath, "m_world.restoreChunkFromBlob(data->key.cx");
        const qsizetype iAdopt = rawIndexOf(gsPath, "m_world.adoptGeneratedChunk(data->key.cx");
        const bool gsOrderOk = iRestore >= 0 && iAdopt > iRestore;
        ok = ok && gsOrderOk;
        if (!gsOrderOk)
            diag += QStringLiteral("[gs-order restore=%1 adopt=%2] ").arg(iRestore).arg(iAdopt);
        const bool doubleQueryGone = forbiddenAbsent(gsPath, "hasChunk(data->key.cx");
        ok = ok && doubleQueryGone;
        if (!doubleQueryGone)
            diag += QStringLiteral("[double-query-alive] ");

        // ③ 件二量化注锚（注释体 → raw contains；剥注释针会失配——section43/44 raw 先例）。
        const bool quantNote = rawContains(srcRoot + QStringLiteral("/World/chunkstore.h"),
                                           "t1070 件二量化");
        ok = ok && quantNote;
        if (!quantNote)
            diag += QStringLiteral("[quant-note missing] ");

        // ④ 件三桥钉（长活成员 + 诊断透传 + 重建计数单一落点 + 只读面栈实例顶针恰 1）。
        const QString sbhPath = srcRoot + QStringLiteral("/Game/savebridge.h");
        const QStringList missSbh = pinSet(sbhPath, {
            SrcPin("long-lived coordinator member", "SaveCoordinator m_coord;", 1),
            SrcPin("rebuild count passthrough", "int frozenBufferRebuildCount() const", 1),
        });
        for (const QString &m : missSbh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QString sbcPath = srcRoot + QStringLiteral("/Game/savebridge.cpp");
        const QStringList missSbc = pinSet(sbcPath, {
            SrcPin("per-save rebind face", "coord.bind(store, resolveSavePath(worldFile))", 1),
        });
        for (const QString &m : missSbc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        {
            QFile f(sbcPath);
            const QString src = f.open(QIODevice::ReadOnly)
                ? QString::fromUtf8(f.readAll()) : QString();
            const int stackSites = int(src.count(QStringLiteral("SaveCoordinator coord;")));
            const bool ceilingOk = stackSites == 1; // 恰剩只读恢复面一处（保存链栈上实例 = 退役）
            ok = ok && ceilingOk;
            if (!ceilingOk)
                diag += QStringLiteral("[stack-sites n=%1 want 1] ").arg(stackSites);
        }
        const QString scpPath = srcRoot + QStringLiteral("/World/savecoordinator.cpp");
        const QStringList missScp = pinSet(scpPath, {
            SrcPin("rebuild counter increment", "++m_bufferRebuilds;", 1),
        });
        for (const QString &m : missScp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QString schPath = srcRoot + QStringLiteral("/World/savecoordinator.h");
        const QStringList missSch = pinSet(schPath, {
            SrcPin("rebuild diagnostics face", "int bufferRebuildCount() const", 1),
        });
        for (const QString &m : missSch) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const qsizetype iDims = rawIndexOf(scpPath, "if (!dimsMatch)");
        const qsizetype iIncr = rawIndexOf(scpPath, "++m_bufferRebuilds;");
        const qsizetype iBegin = rawIndexOf(scpPath, "beginLoad(snap.seed)");
        const bool scOrderOk = iDims >= 0 && iIncr > iDims && iBegin > iIncr;
        ok = ok && scOrderOk;
        if (!scOrderOk)
            diag += QStringLiteral("[sc-order dims=%1 incr=%2 begin=%3] ")
                        .arg(iDims).arg(iIncr).arg(iBegin);

        // ⑤ 禁触面反探（miss 非空 = 合规）：三件新词元一个都不入 worldstore 对与附加表数据面。
        const bool wsBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                                                 "loadCarrier")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                                                 "bufferRebuildCount")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "m_coord")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"),
                                                 "loadCarrier")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"),
                                                 "bufferRebuildCount")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "m_coord");
        ok = ok && wsBlind;
        if (!wsBlind)
            diag += QStringLiteral("[store-blind] ");
        const bool csBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.h"),
                                               "loadCarrier")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.h"),
                                               "bufferRebuild")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"),
                                               "loadCarrier")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"),
                                               "bufferRebuild");
        ok = ok && csBlind;
        if (!csBlind)
            diag += QStringLiteral("[additive-blind] ");

        // ⑥ 段自锚（PASS 如实化锚——本段腿文的关键词元在场，防腿名漂移[先例 r2043d]）。
        const QStringList missSelf = pinSet(matrixRoot + QStringLiteral("/section45_savepath_opt.cpp"),
            { SrcPin("pass honesty anchor", "terrain-generation count", 1),
              SrcPin("pass reuse anchor", "rebuilds it zero times", 1) });
        for (const QString &m : missSelf) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2044d structure pins: the load-kind short circuit sits at its"
                             " single worker authority ahead of the terrain-generation call"
                             " with the carrier mark and diagnostics face in place, the"
                             " harvest beat routes the degraded carrier through deterministic"
                             " regeneration with the retired double query gone, the"
                             " connection-economics note stands at the query authority, the"
                             " bridge carries the long-lived coordinator with the counter"
                             " wired at its single rebuild authority and one read-only stack"
                             " site left, and every forbidden surface stays clean"
                          << (ok ? QString() : diag);
    });
}
