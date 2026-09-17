#include "matrix_helpers.h"

#include "chunkstore.h"           // 被测面：stream_worlds 行读回（D2/D3 标志柱的库面判据）
#include "chunklifecycle.h"       // 被测面：六态读（通电走查收敛判据）
#include "streamingbridge.h"      // 被测面：W5b QML 消费桥（QML 单例 instance = 生产/QML 同对象）
#include "worldstore.h"           // 被测面：blob 通路（saveAll/三写链/roundtrip 生产形态）

#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// §29.5-W5b 流式 UI 探针段（3 腿；filter 词 r2028；矩阵 648→648+N）。置尾先例沿用（接
// section30，runAll 末执行）。任务契约（refactor-plan §29.5.2 W5b + §29.5.3 选型 2；§29.5
// 登记的 QML 例外单——治理审计 #12 放行；P4 钉纪律先例守护）：
//   「默认关零变化墙」→ r2028a（不勾选 = fixed 语义逐位：真链进入分流恒 false + 会话零构造 +
//     标志读面 false + 保存链三写回归 + fixed 读档往返栅格逐位 + 库面零流式表 + 冲洗放行恒
//     true——既有创建/进入/保存/读档路径的桥侧零变化承重面）；
//   「开关 + 转换通电面」→ r2028b（real-chain 优先：勾选创建链 → 标志行在 + 进入即 sparse 构造
//     + 会话通电 + 真玩家走查真驱动收敛[W2 语义腿族复用断言面]；转换动作 → 标志翻转 + 重进
//     sparse 读档[blob 核心区逐位 + 冲洗生产面 + 行回灌仲裁胜利——W5 后端 r2027c 语义的生产
//     面连通]。真 QQmlEngine × 真桥单例 × 真 World/WorldStore/WorldClock/PlayerController——
//     QML→C++ 类型化指针调用链与生产同构）；
//   「QML 面钉」→ r2028c（变更面集中：WorldList/Main 四 hunk 逐一正面钉 + StreamingBridge.
//     调用精确计数[WorldList x3 / Main x2 = 两段之外零调用] + 新增 Q_INVOKABLE 逐一正面钉 +
//     r2007b 复述 + W5 词元 QML 禁触反探[worldstore 冻结域镜像延续] + 值组件零暴露钉
//    [桥头零 Q_PROPERTY / QML 零 ChunkStore]）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   NEG-1 摘开关→创建链标志传递（streamingbridge.cpp flagNewWorldStreaming 体 if(false) 前缀
//     包裹标志落库调用——传递链在 C++ 落点被摘，QML 调用照发）→ 声明红面 {r2028b}[开关柱：
//     标志行缺席 → saveIsStreaming false + enterWorld 走 fixed → sparse/通电/走查柱同源红；
//     diag 带 [toggle] 前缀判别]。r2028a 不误伤（fixed 墙不走标志传递）；r2028c 不误伤（钉
//     Q_INVOKABLE 声明面在场，不钉函数体内部调用——变异不可达性设计）。
//   NEG-2 摘转换动作→API 接线（streamingbridge.cpp convertSaveToStreaming 体 if(false) 前缀
//     包裹转换落库调用）→ 声明红面 {r2028b}[转换柱：标志未翻转 → saveIsStreaming false +
//     重进 fixed → blob/仲裁/行回灌柱同源红；diag 带 [convert] 前缀判别]。r2028a/c 不误伤
//     （同上——a 不经转换；c 声明面钉不受函数体变异影响）。
//   阴性日志：build/ 下四件 matrix_r2028_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 时长控制：真链腿 sparse 预生成 = W1 默认半径 2（25 chunk，两模式同构最小区）；走查收敛
//   deadline 有界（防 flake 不挂死）；临时库 fresh + 用后即删（QDir::temp() pid 键名，绝对
//   路径直用 = openWorld 先例，绝不触 saves/）。
void MatrixRun::section31_streaming_ui()
{
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk，section11 同族）

    // fixed 小世界（setter incantation；World 为 QObject 派生不可拷贝/移动 → 配置就地完成）。
    const auto makeFixed = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口（section11 先例）
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2015/r2027 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2028_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 栅格指纹（blockAt+stateAt 全栅格——fixed 读档往返逐位恒等的比较面，r2027a 同款）。
    const auto gridOf = [&](World &w) {
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
    // 单 chunk 体素快照/比对（16×16 列 × 全 y 带 id+state——blob 物化逐位柱承载面，r2027c 同款）。
    const auto snapChunk = [&](World &w, int cx, int cz) {
        QVector<QPair<int, int>> vox;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kHF; ++y)
                    vox.append({ w.blockAt(cx * 16 + lx, y, cz * 16 + lz),
                        w.stateAt(cx * 16 + lx, y, cz * 16 + lz) });
        return vox;
    };
    const auto chunkIdentical = [&](World &w, int cx, int cz, const QVector<QPair<int, int>> &snap,
                                    long &equalCount, QString &diffDiag) {
        int i = 0, diffs = 0;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kHF; ++y, ++i) {
                    const quint8 sid = snap[i].first, sst = snap[i].second;
                    const quint8 lid = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                    const quint8 lst = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                    if (lid == sid && lst == sst)
                        ++equalCount;
                    else if (++diffs <= 6)
                        diffDiag += QStringLiteral("[%1,%2,%3 %4/%5->%6/%7] ")
                                        .arg(lx).arg(lz).arg(y)
                                        .arg(sid).arg(sst).arg(lid).arg(lst);
                }
        return diffs == 0;
    };
    // 地表标记放置（选址纪律 = r2027 同族：体素真实顶 heightmapAt + 回读校验 + 冠层列换扫）。
    struct EditSite
    {
        int x = 0, y = -1, z = 0;
        bool ok = false;
    };
    const auto placeTop = [](World &w, int bx, int bz) -> EditSite {
        static const int kOff[10][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
            { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 1, 1 } };
        for (const auto &o : kOff) {
            const int x = bx + o[0], z = bz + o[1];
            const int h = w.heightmapAt(x, z);
            if (h < 1 || h + 2 >= w.height())
                continue;
            if (w.blockAt(x, h, z) == 0 || w.blockAt(x, h + 1, z) != 0)
                continue; // 空列 / 顶上有附着 → 换列
            if (!w.setBlock(x, h + 1, z, quint8(BR::Stone), 0))
                continue;
            if (w.blockAt(x, h + 1, z) != quint8(BR::Stone))
                continue;
            return EditSite{ x, h + 1, z, true };
        }
        return EditSite{ bx, -1, bz, false };
    };
    // 库面表名清单（raw 只读连接——零流式表核查面，r2027a 同款）。
    const auto tableNames = [](const QString &db) -> QStringList {
        QStringList out;
        const QString conn = QStringLiteral("r2028_probe_%1").arg(QCoreApplication::applicationPid());
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        {
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery q(p);
                if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'")))
                    while (q.next())
                        out << q.value(0).toString();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    };
    // 收敛轮询：桥泵拍驱动（生产同体——pumpTick）直到 keys 全部 Loaded 或超时（r2027 同族节奏）。
    const auto convergeLoaded = [&](StreamingBridge &bridge, World &w,
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
            bridge.pumpTick(); // 生产泵拍同体（WorldClock::ticked 槽体——QML tick 桥同拍同源）
            QThread::msleep(2);
        }
    };

    // 真链引擎装配（r2021c 同门：真 QQmlEngine + 上下文注入 + setData wrapper；引擎零警告门）。
    //   wrapper = Main.qml enterWorld 分流 / WorldList 创建-转换链的最小消费镜像——经真引擎调真
    //   桥单例（C++/QML 共用同一对象），QML→C++ 类型化指针转换链与生产同构。被消费对象经上下文
    //   属性注入（名字动态解析——换柱重设即换绑，r2021c 初参注入先例的函数体引用变体）。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto setRigCtx = [](Rig &rig, const char *name, QObject *obj) {
        rig.engine.rootContext()->setContextProperty(QLatin1String(name), obj);
    };
    const auto makeRig = [](Rig &rig) {
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 既有真链先例同款：防磁盘缓存重定向
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
    property int seed: 0
    property bool entered: false
    property bool flagged: false
    // 进入分流镜像（Main.qml enterWorld 分流行同式——五参类型化指针过真引擎转换链）。
    function enter() {
        entered = r2028Bridge.enterWorld(r2028World, r2028Store, r2028Clock, r2028Player, file, seed)
    }
    // 创建链标志传递镜像（WorldList 勾选行同式；return 供腿面断言落库返回值）。
    function flag(cw, cd) {
        flagged = r2028Bridge.flagNewWorldStreaming(file, cw, cd)
        return flagged
    }
    function isStream() { return r2028Bridge.saveIsStreaming(file) }
    function convert(cw, cd) { return r2028Bridge.convertSaveToStreaming(file, cw, cd) }
    function flush() { return r2028Bridge.flushForSave() }
}
)QML", QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine); // 引擎析构兜底
    };
    const auto setRigFile = [](Rig &rig, const QString &file, int seed) {
        if (!rig.wrap)
            return;
        rig.wrap->setProperty("file", file);
        rig.wrap->setProperty("seed", seed);
    };

    // ── r2028a：默认关零变化墙（真链进入分流恒 false + 会话零构造 + 三写/往返回归 + 库面零表）──
    runLeg(QStringLiteral("r2028a default-off inert wall (with the toggle untouched the whole"
        " create-enter-persist-load chain stays fixed-semantics bitwise through the production"
        " bridge: the real-engine enter handoff answers false and constructs no session, the"
        " flag read face answers false with no streaming tables in the raw database, the"
        " flush hook passes through true with zero session, the three-write chain succeeds with"
        " the ok counter at three, the reloaded fixed world matches the captured grid bitwise,"
        " and a second enter handoff keeps the world non-sparse with the session face still"
        " inert)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        World w;
        makeFixed(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2028Bridge", &bridge);
        setRigCtx(rig, "r2028World", &w);
        setRigCtx(rig, "r2028Store", &store);
        setRigCtx(rig, "r2028Clock", &clock);
        setRigCtx(rig, "r2028Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // 真链进入分流（镜像 Main.qml 分流行）：fixed 存档恒 false + 世界零模式迁移 + 零会话。
        setRigFile(rig, db, kSeedF);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter");
        const bool enteredA = rig.wrap ? rig.wrap->property("entered").toBool() : true;
        const bool fixedEnterOk = !enteredA && !w.isSparse() && !bridge.sessionActive();
        ok = ok && fixedEnterOk;
        if (!fixedEnterOk)
            diag += QStringLiteral("[fixed-enter e=%1 sparse=%2 sess=%3] ")
                        .arg(enteredA).arg(w.isSparse()).arg(bridge.sessionActive());

        // 标志读面 + 库面零流式表（不勾选 = 零标志零活动；读面纯读——不建表）。
        QVariant isStream(true);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "isStream", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, isStream));
        const QStringList tables = tableNames(db);
        const bool noFlag = !isStream.toBool() && !tables.contains(QStringLiteral("stream_worlds"))
            && !tables.contains(QStringLiteral("chunk_edits"));
        ok = ok && noFlag;
        if (!noFlag)
            diag += QStringLiteral("[no-flag isStream=%1 tables=%2] ")
                        .arg(isStream.toBool())
                        .arg(tables.join(QLatin1Char(',')));

        // 冲洗放行（无会话恒 true——fixed 保存链零变化面）。
        QVariant flushed(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flush", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, flushed));
        ok = ok && flushed.toBool();
        if (!flushed.toBool())
            diag += QStringLiteral("[flush-true] ");

        // 保存链回归（三写生产形态）→ 读档往返栅格逐位。
        const EditSite mark = placeTop(w, 20, 24);
        const QByteArray gridFixed = gridOf(w);
        QVariantMap pd;
        pd.insert(QStringLiteral("x"), 8.5);
        QVariantMap pr;
        pr.insert(QStringLiteral("stat"), 1);
        const bool saveAllOk = store.saveAll(QStringLiteral("r2028a"));
        const bool pdOk = store.savePlayerData(pd);
        const bool prOk = store.saveProgress(pr);
        ok = ok && mark.ok && saveAllOk && pdOk && prOk && store.saveOkCount() == 3;
        if (!mark.ok || !saveAllOk || !pdOk || !prOk || store.saveOkCount() != 3)
            diag += QStringLiteral("[three-write m=%1 a=%2 p=%3 g=%4 ok=%5] ")
                        .arg(mark.ok).arg(saveAllOk).arg(pdOk).arg(prOk).arg(store.saveOkCount());

        World w2;
        makeFixed(w2);
        w2.beginLoad(kSeedF);
        store.setWorld(&w2); // r2010d rebind 纪律
        const int loaded = store.loadChunks();
        w2.finishLoad();
        const bool roundtripOk = loaded == 9 && gridOf(w2) == gridFixed;
        ok = ok && roundtripOk;
        if (!roundtripOk)
            diag += QStringLiteral("[roundtrip loaded=%1 grid=%2] ")
                        .arg(loaded).arg(gridOf(w2) == gridFixed);

        // 二次分流（消费对象换绑 w2 后再入——fixed 存档照旧 false；非 sparse 世界零归位触发）。
        setRigCtx(rig, "r2028World", &w2);
        setRigFile(rig, db, kSeedF);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter");
        const bool enteredA2 = rig.wrap ? rig.wrap->property("entered").toBool() : true;
        const bool secondOk = !enteredA2 && !w2.isSparse() && !bridge.sessionActive();
        ok = ok && secondOk;
        if (!secondOk)
            diag += QStringLiteral("[second e=%1 sparse=%2 sess=%3] ")
                        .arg(enteredA2).arg(w2.isSparse()).arg(bridge.sessionActive());

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2028a default-off inert wall: with the toggle untouched the"
                             " whole create-enter-persist-load chain stays fixed-semantics"
                             " bitwise through the production bridge, the real-engine enter"
                             " handoff answers false and constructs no session, the flag read"
                             " face answers false with no streaming tables in the raw database,"
                             " the flush hook passes through true with zero session, the"
                             " three-write chain succeeds with the ok counter at three, the"
                             " reloaded fixed world matches the captured grid bitwise, and a"
                             " second enter handoff keeps the world non-sparse with the session"
                             " face still inert" << (ok ? QString() : diag);
    });

    // ── r2028b：开关 + 转换通电面（real-chain；单腿双柱——NEG-1 红开关柱 / NEG-2 红转换柱）────
    runLeg(QStringLiteral("r2028b toggle and conversion powered faces (real-chain; a real engine"
        " drives the real bridge singleton: the creation-chain flag lands a streaming row with"
        " zero-based generations and the enter handoff rebuilds the world sparse with a live"
        " session that walks real player chunk edges through the production pump to converge new"
        " chunks to Loaded with the resident revision moving [toggle face], and the opt-in"
        " conversion flips the additive flag of a persisted fixed world with zero data movement"
        " so the enter handoff materializes the whole core bitwise from the blob, the flush hook"
        " lands the resident edit as a row and a second enter replays it over the blob"
        " [conversion face])"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2028Bridge", &bridge);
        setRigCtx(rig, "r2028Clock", &clock);
        setRigCtx(rig, "r2028Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // ══ 柱 1 [toggle]：勾选创建链 → 标志行在 → 进入即 sparse + 通电 → 真走查收敛 ═════════
        const QString dbT = tempDb("t");
        QFile::remove(dbT); // fresh
        World wt;
        makeFixed(wt); // 宿主固定世界（创建链起点——QML theWorld 装配态镜像）
        WorldStore store;
        store.openWorld(dbT); // 创建面镜像（createWorld 的建库+meta 面；绝对路径先例）
        store.setWorld(&wt);
        setRigCtx(rig, "r2028World", &wt);
        setRigCtx(rig, "r2028Store", &store);
        setRigFile(rig, dbT, kSeedF);

        // 勾选 → 标志落库（core dims = 宿主 dims 48×48）。
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, flagged),
                                      Q_ARG(QVariant, 48), Q_ARG(QVariant, 48));
        ChunkStore probeT;
        probeT.bind(dbT);
        probeT.setStreamWorldId(dbT);
        StreamWorldMeta metaT;
        const bool hasRowT = probeT.readStreamWorldMeta(metaT);
        QVariant isStreamT(true);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "isStream", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, isStreamT));
        const bool rowOk = flagged.toBool() && hasRowT && metaT.streaming && metaT.coreW == 48
            && metaT.coreD == 48 && metaT.baseGen == 0 && metaT.saveGen == 0
            && isStreamT.toBool();
        ok = ok && rowOk;
        if (!rowOk)
            diag += QStringLiteral("[toggle-row flag=%1 row=%2 s=%3 w=%4 d=%5 b=%6 g=%7 read=%8] ")
                        .arg(flagged.toBool()).arg(hasRowT)
                        .arg(metaT.streaming).arg(metaT.coreW).arg(metaT.coreD)
                        .arg(qint64(metaT.baseGen)).arg(qint64(metaT.saveGen))
                        .arg(isStreamT.toBool());

        // 进入分流（真链）→ sparse 重构 + 会话通电（W1 默认半径 2：core 3×3 → 中心 (1,1) ±2 =
        // 5×5 = 25 chunk 预生成全 Loaded）。
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter");
        const bool enteredT = rig.wrap ? rig.wrap->property("entered").toBool() : false;
        const bool sparseOk = enteredT && wt.isSparse() && bridge.sessionActive()
            && wt.residentChunkCount() == 25;
        ok = ok && sparseOk;
        if (!sparseOk)
            diag += QStringLiteral("[toggle-enter e=%1 sparse=%2 sess=%3 res=%4] ")
                        .arg(enteredT).arg(wt.isSparse()).arg(bridge.sessionActive())
                        .arg(wt.residentChunkCount());

        // 真玩家走查（r2024b 生产链同门：真 PlayerController 位置沿 → 桥喂入 → 生产泵拍收敛）。
        pc.setWorld(&wt);
        pc.loadSavedState(72.5f, 70.0f, 72.5f, 0.0f, 0.0f, 0); // 落 chunk (4,4)（预生成窗外）
        pc.tick(); // 起始沿 → playerChunkChanged(4,4) → 桥挂钩喂入会话
        const int revBefore = wt.residentChunkRevision();
        QVector<QPair<int, int>> walkKeys{ { 4, 4 } };
        QString dWalk;
        const bool walked = convergeLoaded(bridge, wt, walkKeys, 45000, dWalk);
        const bool walkOk = walked && wt.chunks().lifecycleAt(4, 4) == ChunkLifecycle::Loaded
            && wt.residentChunkRevision() > revBefore;
        ok = ok && walkOk;
        if (!walkOk)
            diag += QStringLiteral("[toggle-walk c=%1 l44=%2 rev+%3 res=%4] %5")
                        .arg(walked)
                        .arg(int(wt.chunks().lifecycleAt(4, 4)))
                        .arg(wt.residentChunkRevision() - revBefore)
                        .arg(wt.residentChunkCount())
                        .arg(dWalk);

        // ══ 柱 2 [convert]：D3 opt-in 转换 → 重进 sparse 读档 → 冲洗生产面 → 行回灌仲裁胜利 ══
        const QString dbC = tempDb("c");
        QFile::remove(dbC); // fresh
        World wc;
        makeFixed(wc);
        WorldStore storeC;
        storeC.openWorld(dbC);
        storeC.setWorld(&wc);
        setRigCtx(rig, "r2028World", &wc);
        setRigCtx(rig, "r2028Store", &storeC);
        setRigFile(rig, dbC, kSeedF);
        // fixed 时代：两处编辑 → 生产 blob 保存 → 快照（blob 物化逐位柱基准）。
        const EditSite s00 = placeTop(wc, 5, 7);    // chunk (0,0)
        const EditSite s11f = placeTop(wc, 20, 24); // chunk (1,1)
        const bool editsOk = s00.ok && s11f.ok;
        ok = ok && editsOk;
        if (!editsOk)
            diag += QStringLiteral("[conv-edits %1 %2] ").arg(s00.ok).arg(s11f.ok);
        const QVector<QPair<int, int>> snap00 = snapChunk(wc, 0, 0);
        const QVector<QPair<int, int>> snap11 = snapChunk(wc, 1, 1);
        const bool savedOk = storeC.saveAll(QStringLiteral("r2028b"));
        ok = ok && savedOk;
        if (!savedOk)
            diag += QStringLiteral("[conv-blob-save] ");

        // 转换（真链）：标志翻转 + core dims + 代次锚（零数据搬迁——库面 chunk_edits 零行）。
        QVariant converted(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "convert", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, converted),
                                      Q_ARG(QVariant, 48), Q_ARG(QVariant, 48));
        ChunkStore probeC;
        probeC.bind(dbC);
        probeC.setStreamWorldId(dbC);
        StreamWorldMeta metaC;
        const bool hasRowC = probeC.readStreamWorldMeta(metaC);
        QVariant isStreamC(false);
        if (rig.wrap) {
            setRigFile(rig, dbC, kSeedF);
            QMetaObject::invokeMethod(rig.wrap, "isStream", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, isStreamC));
        }
        const bool convOk = converted.toBool() && hasRowC && metaC.streaming && metaC.coreW == 48
            && metaC.coreD == 48 && metaC.baseGen == 0 && probeC.chunkCount() == 0
            && isStreamC.toBool();
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[convert conv=%1 row=%2 s=%3 w=%4 d=%5 b=%6 rows=%7 read=%8] ")
                        .arg(converted.toBool()).arg(hasRowC)
                        .arg(metaC.streaming).arg(metaC.coreW).arg(metaC.coreD)
                        .arg(qint64(metaC.baseGen)).arg(probeC.chunkCount())
                        .arg(isStreamC.toBool());

        // 首进（真链）：sparse 重构 + blob 核心区全物化（9 chunk 逐位 = fixed 时代内容）。
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter"); // 进入首拍自拆旧会话（生产换世界同体）
        const bool enteredC = rig.wrap ? rig.wrap->property("entered").toBool() : false;
        const bool sparseCOk = enteredC && wc.isSparse() && bridge.sessionActive()
            && wc.residentChunkCount() == 25; // 半径 2 预生成 25（核心 9 被弃槽让位 blob 后物化回驻留）
        ok = ok && sparseCOk;
        if (!sparseCOk)
            diag += QStringLiteral("[conv-enter e=%1 sparse=%2 sess=%3 res=%4] ")
                        .arg(enteredC).arg(wc.isSparse()).arg(bridge.sessionActive())
                        .arg(wc.residentChunkCount());
        long eqC = 0;
        QString ddC;
        const bool id00 = chunkIdentical(wc, 0, 0, snap00, eqC, ddC);
        const bool id11 = chunkIdentical(wc, 1, 1, snap11, eqC, ddC);
        ok = ok && id00 && id11;
        if (!id00 || !id11)
            diag += QStringLiteral("[conv-blob-identity 00=%1 11=%2 eq=%3 %4] ")
                        .arg(id00).arg(id11).arg(eqC).arg(ddC);

        // 冲洗生产面（保存链前置挂钩）：流式时代编辑 → 恰 1 行 + 行代次 1 + flushForSave true。
        const EditSite s11s = placeTop(wc, 22, 22); // chunk (1,1) 异列（与 fixed 时代编辑区分）
        const QVector<QPair<int, int>> snap11s = snapChunk(wc, 1, 1); // 行回灌逐位柱基准（编辑后）
        const bool flushedC = s11s.ok && bridge.flushForSave();
        ChunkStoreBlob blob11;
        const bool flushCOk = flushedC && probeC.chunkCount() == 1
            && probeC.loadChunk(1, 1, blob11) && blob11.generation == 1;
        ok = ok && flushCOk;
        if (!flushCOk)
            diag += QStringLiteral("[conv-flush site=%1 ok=%2 rows=%3 gen=%4] ")
                        .arg(s11s.ok).arg(flushedC).arg(probeC.chunkCount())
                        .arg(qint64(blob11.generation));

        // 再进（真链二次进入）：行代次 ≥ 锚 → 行胜仲裁 → 流式编辑回灌。
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter");
        const bool entered2 = rig.wrap ? rig.wrap->property("entered").toBool() : false;
        const bool reEnterOk = entered2 && wc.isSparse() && bridge.sessionActive();
        ok = ok && reEnterOk;
        if (!reEnterOk)
            diag += QStringLiteral("[conv-reenter e=%1 sparse=%2 sess=%3] ")
                        .arg(entered2).arg(wc.isSparse()).arg(bridge.sessionActive());
        // 流式编辑在（行回灌）+ fixed 时代编辑在（blob 物化）= 双时代内容同柱并存；
        // (1,1) 全量逐位 = 编辑后快照（行字节 = 冲洗时刻原样）。
        const bool editKept = s11s.ok
            && wc.blockAt(s11s.x, s11s.y, s11s.z) == quint8(BR::Stone)
            && wc.blockAt(s00.x, s00.y, s00.z) == quint8(BR::Stone);
        long eq2 = 0;
        QString dd2;
        const bool id11s = chunkIdentical(wc, 1, 1, snap11s, eq2, dd2);
        ok = ok && editKept && id11s;
        if (!editKept || !id11s)
            diag += QStringLiteral("[conv-replay kept=%1 id11s=%2 eq=%3 %4] ")
                        .arg(editKept).arg(id11s).arg(eq2).arg(dd2);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        storeC.closeWorld();
        QFile::remove(dbT); // 用后即删
        QFile::remove(dbC);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2028b toggle and conversion powered faces: the creation-chain"
                             " flag lands a streaming row and the enter handoff rebuilds the"
                             " world sparse with a live session that walks real player chunk"
                             " edges through the production pump [toggle face], and the opt-in"
                             " conversion flips the additive flag of a persisted fixed world"
                             " with zero data movement so the enter handoff materializes the"
                             " whole core bitwise from the blob, the flush hook lands the"
                             " resident edit as a row and a second enter replays it over the"
                             " blob [conversion face]"
                          << (ok ? QString() : diag);
    });

    // ── r2028c：QML 面钉（变更面集中 + 新增 Q_INVOKABLE 逐一正面钉 + 词元禁触 + 值组件零暴露）──
    runLeg(QStringLiteral("r2028c surface pins (comment-stripped source pins hold the registered"
        " QML exception surface concentrated in its hunks: the world list carries the default-off"
        " infinite toggle with its label and exactly one creation-chain flag call, the convert"
        " action with its three-point confirm copy and exactly one conversion call, the enter"
        " handoff and the persist-side flush hook are the only two bridge touches in Main, the"
        " bridge type exposes exactly five invokables with zero value-component properties, the"
        " world pair and the session shell carry the minimal mode-migration and pump seams,"
        " gameplay markers stay put with zero session-type mentions, and every frozen-domain"
        " token stays out of both QML files)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString mainPath = srcRoot + QStringLiteral("/ui/Main.qml");
        const QString listPath = srcRoot + QStringLiteral("/ui/WorldList.qml");
        const QString bridgeH = srcRoot + QStringLiteral("/Game/streamingbridge.h");

        // ① 桥类型面（QML 单例形态 + 五 Q_INVOKABLE 逐一正面钉 + 值组件零 Q_PROPERTY 暴露）。
        const QStringList missBridge = pinSet(bridgeH, {
            SrcPin("bridge qml singleton", "QML_SINGLETON", 1),
            SrcPin("bridge qml name", "QML_NAMED_ELEMENT(StreamingBridge)", 1),
            SrcPin("bridge invokable: creation flag",
                "Q_INVOKABLE bool flagNewWorldStreaming(const QString &file, int coreWidth, int"
                " coreDepth);", 1),
            SrcPin("bridge invokable: flag read",
                "Q_INVOKABLE bool saveIsStreaming(const QString &file) const;", 1),
            SrcPin("bridge invokable: conversion",
                "Q_INVOKABLE bool convertSaveToStreaming(const QString &file, int coreWidth, int"
                " coreDepth);", 1),
            SrcPin("bridge invokable: enter handoff",
                "Q_INVOKABLE bool enterWorld(World *world, WorldStore *store, WorldClock *clock,", 1),
            SrcPin("bridge invokable: save flush", "Q_INVOKABLE bool flushForSave();", 1),
            SrcPin("bridge singleton factory",
                "static StreamingBridge *create(QQmlEngine *, QJSEngine *)", 1),
        });
        for (const QString &m : missBridge) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool noValueProps = forbiddenAbsent(bridgeH, "Q_PROPERTY");
        ok = ok && noValueProps;
        if (!noValueProps)
            diag += QStringLiteral("[bridge-value-prop] ");

        // ② World/会话/网格最小新增面（模式迁移两法 + fixed 归位 + 帧泵——逐一正面钉）。
        const QStringList missWorld = pinSet(srcRoot + QStringLiteral("/World/world.h"), {
            SrcPin("world sparse re-init", "void reinitializeAsSparse(const SparseWorldParams &sp);", 1),
            SrcPin("world fixed re-init", "void reinitializeAsFixed(int width, int depth, int height);", 1),
        });
        const QStringList missWorldCpp = pinSet(srcRoot + QStringLiteral("/World/world.cpp"), {
            SrcPin("world sparse re-init body", "void World::reinitializeAsSparse(const SparseWorldParams &sp)", 1),
            SrcPin("world fixed re-init body", "void World::reinitializeAsFixed(int width, int depth, int height)", 1),
            SrcPin("sparse ctor single authority", "reinitializeAsSparse(sp);", 1),
        });
        const QStringList missCm = pinSet(srcRoot + QStringLiteral("/World/chunkmanager.h"), {
            SrcPin("chunkmanager fixed re-init", "void reinitializeFixed(int width, int depth, int height);", 1),
        });
        const QStringList missGs = pinSet(srcRoot + QStringLiteral("/Game/gamesession.h"), {
            SrcPin("session production pump seam", "void pumpStreamingFrame() { pumpStreamingTick(); }", 1),
        });
        for (const QString &m : missWorld + missWorldCpp + missCm + missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ WorldList.qml 变更面（勾选默认关 + 文案 + 创建链传递 + 转换动作 + 确认文案三点）。
        const QStringList missList = pinSet(listPath, {
            SrcPin("infinite toggle default off", "property bool checked: false", 1),
            SrcPin("infinite toggle label", "text: \"无限世界\"", 1),
            SrcPin("creation-chain flag pass", "StreamingBridge.flagNewWorldStreaming", 1),
            SrcPin("convert action label", "text: \"转为无限世界\"", 1),
            SrcPin("convert confirm copy: three points",
                "境外按需生成、编辑自动保存；已卸载区的实体不保留", 1),
            SrcPin("convert confirm copy: revert note", "界面暂不提供入口", 1),
            SrcPin("conversion api call", "StreamingBridge.convertSaveToStreaming", 1),
            SrcPin("conversion confirm state", "property string convertingFile", 1),
        });
        for (const QString &m : missList) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        // 桥调用集中计数（WorldList 恰三处：flag / saveIsStreaming / convert——两段之外零调用）。
        {
            QFile f(listPath);
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const int calls = int(src.count(QLatin1String("StreamingBridge.")));
            const bool concentrated = calls == 3;
            ok = ok && concentrated;
            if (!concentrated)
                diag += QStringLiteral("[list-calls %1] ").arg(calls);
        }

        // ④ Main.qml 变更面（进入分流 + 保存链冲洗——桥调用恰两处）+ 玩法标记在位 + r2007b 复述。
        const QStringList missMain = pinSet(mainPath, {
            SrcPin("enter handoff branch",
                "if (StreamingBridge.enterWorld(theWorld, worldStore, worldClock, player, file,"
                " seed)) {", 1),
            SrcPin("save-chain flush hook", "if (!StreamingBridge.flushForSave())", 1),
            SrcPin("gameplay entry intact: enterWorld", "function enterWorld(", 1),
            SrcPin("gameplay bridge intact: worldClock onTicked", "function onTicked(dt)", 1),
            SrcPin("gameplay entry intact: startGame", "function startGame()", 1),
        });
        for (const QString &m : missMain) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        {
            QFile f(mainPath);
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const int calls = int(src.count(QLatin1String("StreamingBridge.")));
            ok = ok && calls == 2;
            if (calls != 2)
                diag += QStringLiteral("[main-calls %1] ").arg(calls);
            // r2007b 站立钉复述（裸字面含注释——Main.qml 禁出会话类型名）。
            const bool noSession = !src.contains(QLatin1String("GameSession"));
            ok = ok && noSession;
            if (!noSession)
                diag += QStringLiteral("[zero-migration] ");
        }

        // ⑤ 词元禁触反探（miss 非空 = 合规缺席）：W5 冻结域词元不入两段 QML；生命周期/会话记号
        //    站立钉复述；值组件类型零 QML 暴露。
        const bool qmlBlind = forbiddenAbsent(mainPath, "stream_worlds")
            && forbiddenAbsent(mainPath, "StreamWorldMeta")
            && forbiddenAbsent(mainPath, "flushResidentEdits")
            && forbiddenAbsent(mainPath, "loadStreamingWorld")
            && forbiddenAbsent(mainPath, "markStreamingWorld")
            && forbiddenAbsent(mainPath, "ChunkStore")
            && forbiddenAbsent(mainPath, "setChunkLifecycle")
            && forbiddenAbsent(listPath, "GameSession")
            && forbiddenAbsent(listPath, "stream_worlds")
            && forbiddenAbsent(listPath, "StreamWorldMeta")
            && forbiddenAbsent(listPath, "ChunkStore")
            && forbiddenAbsent(listPath, "setChunkLifecycle");
        ok = ok && qmlBlind;
        if (!qmlBlind)
            diag += QStringLiteral("[qml-blind] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2028c surface pins: the registered QML exception surface stays"
                             " concentrated in its hunks with the default-off infinite toggle and"
                             " its label and one creation-chain flag call, the convert action with"
                             " its three-point confirm copy and one conversion call, the enter"
                             " handoff and the persist-side flush hook are the only two bridge"
                             " touches in Main, the bridge type exposes exactly five invokables"
                             " with zero value-component properties, the world pair and the"
                             " session shell carry the minimal mode-migration and pump seams,"
                             " gameplay markers stay put with zero session-type mentions, and"
                             " every frozen-domain token stays out of both QML files"
                          << (ok ? QString() : diag);
    });
}
