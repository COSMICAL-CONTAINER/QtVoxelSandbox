#include "matrix_helpers.h"

#include "savebridge.h"      // 被测面：t1064 探锁退避（Q_INVOKABLE 直调 + 真链 QML 面经同名单例上下文注入）
#include "savecoordinator.h" // 台账读数（Clean/代次不谎报面——历史原样柱）
#include "worldstore.h"      // 计数观测面（saveOkCount 成功 +3 / 失败零动口径）

#include <QElapsedTimer>
#include <atomic>
#include <thread>

// t1064 退出存档失败重试退避 探针段（4 腿 r2038a-d；filter 词 "r2038"；矩阵 683→683+N）。
// 置尾先例沿用（接 section38，runAll 末执行）；真临时库（pid 键名，测试自清理）+ 真桥单例 +
// 真 QQmlEngine；真实用户 saves/ 零触碰。任务契约（review0901 #36 原文 + dev-plan t1064）：
//   「常态墙」→ r2038a（无锁常态：QML 共用 wrapper 首试即真零重试[调用计数恰 1] + 零退避
//     [墙钟 < 退避下界 + 桥直调面返 0 双证] + saveOkCount 恰 +3 口径 + 台账 Clean 1/1 +
//     完成门/toast/lastExitSaveOk 语义面原样 + 桥守卫面[无 store/空名/缺库]恒零退避）；
//   「退避承重」→ r2038b（真锁 BEGIN EXCLUSIVE 注入[自管线程连接——创建/使用/销毁全程单线
//     程，Qt Sql 纪律合规]：桥直调面锁在持返回 [100,300]ms 窗 + 墙钟 ≤300 + 释放后恒 0；
//     QML wrapper 面——锁 60ms 窗内释放，首试败→探锁→退避→重试收敛真 + 恰一次重试[调用计
//     数恰 2] + +3 口径 + 代次单调 2/2 Clean）；
//   「上限与不谎报」→ r2038c（锁全程持有：恰一次重试即止[调用计数恰 2，不第二重风暴] +
//     返回 false 不谎报 + 计数零动 + 台账历史原样 Clean 1/1 + recoveryState 仍 clean；释放后
//     重存收敛 +3 = 失败如实上报后照常可恢复）；
//   「结构钉」→ r2038d（共用实现单点钉[wrapper 定义恰 1 + 桥探锁调用恰 1 + msleep 单点] +
//     两调用点钉[按钮/关窗各恰一 + 瞬态归还前置 + 完成门先于退出置位] + 旧 0ms 立即重放两行
//     禁入反探 + 完成门/toast/lastExitSaveOk/归还序零触碰复钉 + 退避常量 ≤300ms 源钉）。
// 被测 QML 面装配（生产函数真文本驱动，非镜像复写）：从 Main.qml brace 配平抽取
//   runExitSaveWithBackoff **逐字节原样**嵌入 wrapper（归还函数抽取先例同门），wrapper 只补
//   runExitSave 镜像（真桥单调用 + 调用计数 = 恰一次重试观测面）与上下文属性（"SaveBridge"
//   同名注入 = 真桥单例对象、"worldStore"、currentWorldFile）——生产体引用的三个自由名全部
//   落位，抽取失败/改名即响亮红。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘退避（savebridge.cpp 退避 sleep 短路为立即返回）→ 声明红面 = {r2038b, r2038d}
//     （b：直调窗下界断言红 + 60ms 锁窗收敛腿重试再败红；d：msleep 单点源钉红）；r2038a 不
//     误伤（零退避柱本就断 0）；r2038c 不误伤（锁全程持有时退避与否都两连败——不设下界计
//     时柱，恰一次/不谎报面不变）。
//   NEG-2 重试风暴（Main.qml wrapper 重试臂叠为两次重试 + 第二重退避）→ 声明红面 =
//     {r2038c, r2038d}（c：持锁腿调用计数恰 2 断言红[三试]；d：桥探锁调用恰 1 钉红）；r2038b
//     不红（收敛腿首重试即真 = 风暴臂不可达——承重红面由持锁腿承载，非连带伤，此处预声明）。
// 首跑实证登记（run1，后置为装配依据）：Qt QSQLITE 驱动默认 busy timeout **非零**（实测 >
//   60ms，秒级）——真锁下保存链首试会先在 recover() 台账 SELECT 上 busy 等待秒级才失败
//   （r2038c 首跑纯真锁全程持锁腿 wall≈29s 实锤），故：
//   ①「首试失败」用 SaveFaultHook::BeginMark 缝制造（r2015 五段缝的登记用途；该缝在 recover
//     之前短路 = 瞬时失败零阻塞——savecoordinator.cpp saveAll 步①序）；
//   ②「探锁/退避/重试收敛」全走真锁路径（探针 BUSY_TIMEOUT=0 显式归零 = 瞬时真答案）；
//   ③纯真锁的「保存链锁下拒存」行为已有既有腿盖（真锁柱族）——本段不重复计时。
//   （保存链 busy 等待秒级 = 前置既有面，非本单回归；t1064 退避只对「已失败后的重试间隔」
//   负责，上报主控登记。）
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section39_exit_save_backoff()
{
    // ── 段内共享帮手（腿间无共享 rig 状态——各腿自建 fresh 世界 + 临时库）───────────────────
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk）
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);
        w.setWeatherState(0);               // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f);  // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名 + 腿标；QDir::temp()，测试自清理；绝对路径直用 = openWorld 先例）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2038_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // Main.qml 共用 wrapper 原文抽取（brace 配平——t974(e) 归还函数抽取先例同门）：驱动生产
    //   函数真文本；wrapper 缺席/改名/体破损即 false（修前形态本腿族全红 = 响亮）。
    const auto extractBackoffWrapper = [](QString &out, QString &why) {
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile f(root + QStringLiteral("/src/ui/Main.qml"));
        const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                        : QString();
        const int iFn = src.indexOf(QStringLiteral("function runExitSaveWithBackoff()"));
        if (iFn < 0) {
            why = QStringLiteral("wrapper missing (pre-fix shape)");
            return false;
        }
        const int iOpen = src.indexOf(QLatin1Char('{'), iFn);
        int depth = 0;
        for (int i = iOpen; i >= 0 && i < src.size(); ++i) {
            const QChar c = src.at(i);
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}') && --depth == 0) {
                out = src.mid(iFn, i - iFn + 1);
                break;
            }
        }
        if (out.isEmpty()) {
            why = QStringLiteral("brace walk failed");
            return false;
        }
        return true;
    };
    // 真链引擎装配（真 QQmlEngine + 上下文注入 + setData wrapper——真链腿族同门；引擎零警告门）。
    //   wrapper = runExitSave 镜像（真桥单调用 + saveCalls 计数）+ 生产 wrapper 原文体。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeRig = [&extractBackoffWrapper](Rig &rig, WorldStore *store) {
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 既有真链先例同款：防磁盘缓存重定向
        QObject::connect(&rig.engine, &QQmlEngine::warnings, &rig.engine,
            [&rig](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++rig.warnings;
                    if (rig.firstWarning.isEmpty())
                        rig.firstWarning = e.toString();
                }
            });
        QString fn;
        QString why;
        if (!extractBackoffWrapper(fn, why)) {
            rig.firstWarning = why; // 抽取失败 → wrap 恒空 → 腿红（诊断面）
            return;
        }
        // 生产体自由名落位："SaveBridge" 同名注入 = C++/QML 共用真桥单例（桥面零镜像）；
        //   "worldStore" = 真被测库；currentWorldFile = wrapper 属性。
        rig.engine.rootContext()->setContextProperty(QStringLiteral("SaveBridge"),
                                                     SaveBridge::instance());
        rig.engine.rootContext()->setContextProperty(QStringLiteral("worldStore"), store);
        QQmlComponent comp(&rig.engine);
        const QString qml = QStringLiteral(
            "import QtQuick\n"
            "Item {\n"
            "    property string currentWorldFile: \"\"\n"
            "    property int saveCalls: 0\n"
            "    // runExitSave 镜像（Main.qml 三写段 = 桥单调用同构；saveCalls = 恰一次重试观测面）。\n"
            "    function runExitSave() {\n"
            "        saveCalls++\n"
            "        return SaveBridge.saveViaCoordinator(worldStore, currentWorldFile,\n"
            "            \"r2038\", [], [], [], {}, {}, {px: 41.5}, {statPlayedMinutes: saveCalls})\n"
            "    }\n") + fn + QStringLiteral("\n}\n");
        comp.setData(qml.toUtf8(), QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine); // 引擎析构兜底
    };
    const auto setRigFile = [](Rig &rig, const QString &file) {
        if (rig.wrap)
            rig.wrap->setProperty("currentWorldFile", file);
    };
    // 生产 wrapper 直调（invokeMethod 经元对象系统 = QML 函数真实调用链）。
    const auto runBackoffSave = [](Rig &rig) {
        QVariant ok(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "runExitSaveWithBackoff", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, ok));
        return ok.toBool();
    };
    const auto saveCallsOf = [](Rig &rig) {
        return rig.wrap ? rig.wrap->property("saveCalls").toInt() : -1;
    };
    // 基线保存（C++ 直调桥——不经 wrapper，saveCalls 语义保持「wrapper 重试观测」专用）。
    const auto bridgeSave = [](WorldStore &store, const QString &file, double px) {
        QVariantMap player;
        player.insert(QStringLiteral("px"), px);
        QVariantMap progress;
        progress.insert(QStringLiteral("statPlayedMinutes"), 1);
        return SaveBridge::instance()->saveViaCoordinator(&store, file, QStringLiteral("r2038"),
                                                          QVariantList(), QVariantList(),
                                                          QVariantList(), QVariantMap(),
                                                          QVariantMap(), player, progress);
    };
    // 真锁注入（同款 BEGIN EXCLUSIVE 真锁先例；持锁者 = 自管线程连接——连接创建/使用/销毁全
    //   程单线程内，Qt Sql 线程纪律合规）。持锁窗 = deadline 制（hold.elapsed() 计——不逐毫秒
    //   步进计数，防 Windows 定时器粒度把 holdMs 放大成秒级）。stop 置位 = 提前放手（join 前
    //   汇合）；err 仅 join 后读（join = 同步点，无竞态）。
    struct LockThread
    {
        std::thread thr;
        std::atomic_bool locked{false};
        std::atomic_bool stop{false};
        QString err;
    };
    const auto holdExclusive = [](LockThread &h, const QString &db, const QString &conn,
                                  int holdMs) {
        h.thr = std::thread([&h, db, conn, holdMs]() {
            if (QSqlDatabase::contains(conn))
                QSqlDatabase::removeDatabase(conn);
            {
                QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                d.setDatabaseName(db);
                QSqlQuery q(d);
                if (d.open() && q.exec(QStringLiteral("BEGIN EXCLUSIVE"))) {
                    h.locked = true;
                    QElapsedTimer hold;
                    hold.start();
                    while (!h.stop && hold.elapsed() < holdMs)
                        QThread::msleep(2);
                    QSqlQuery r(d);
                    r.exec(QStringLiteral("ROLLBACK"));
                } else {
                    h.err = QStringLiteral("locker open/begin failed");
                }
            }
            QSqlDatabase::removeDatabase(conn);
        });
    };
    // 主线程旋等锁就位（有界 5s——防锁从未建立时腿挂死）。
    const auto waitLocked = [](std::atomic_bool &flag) {
        QElapsedTimer t;
        t.start();
        while (!flag && t.elapsed() < 5000)
            QThread::msleep(1);
        return bool(flag);
    };
    const auto stopAndJoin = [](LockThread &h) {
        h.stop = true;
        if (h.thr.joinable())
            h.thr.join();
    };

    SaveBridge &bridge = *SaveBridge::instance();
    bridge.setFaultHook(SaveFaultHook()); // 桥间复位缝（singleton 跨腿共享——入口归零 = 生产形态）

    // ── r2038a：无锁常态墙（首试即真零重试 + 零退避双证 + +3 口径 + 完成门/toast 语义面）────
    runLeg(QStringLiteral("r2038a lock-free normal-path wall (with nothing holding the library"
        " the shared exit-save retry helper stays inert end to end: the real-engine first"
        " attempt answers true with the retry-observable call counter at exactly one, the"
        " measured call stays far under the backoff floor, the bridge probe face answers zero"
        " backoff milliseconds on a free library and on every degenerate guard face (no store,"
        " empty file name, missing library), the write counter moves by exactly +3 per save"
        " with the ledger Clean at 1/1 and the string face agreeing, and the completion-gate"
        " / toast / lastExitSaveOk semantics stay in place verbatim in both exit paths)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig %1] ").arg(rig.firstWarning);
        setRigFile(rig, db);

        // 无锁常态：首试即真——零重试（恰 1 调用）+ 零退避（墙钟有界 ≤300ms；零重试/零退避的
        //   权威证 = 调用计数恰 1 + 桥直调面返 0——墙钟只作「无秒级停顿」的量界）。
        const int c0 = store.saveOkCount();
        QElapsedTimer wall;
        wall.start();
        const bool saved = runBackoffSave(rig);
        const qint64 wallMs = wall.elapsed();
        const int c1 = store.saveOkCount();
        const bool firstOk = saved && saveCallsOf(rig) == 1 && wallMs <= 300
            && c1 == c0 + 3; // 计数口径逐位同旧：三部分各 +1 = +3（重试零发生 = 零额外计数）
        ok = ok && firstOk;
        if (!firstOk)
            diag += QStringLiteral("[first r=%1 calls=%2 wall=%3 c=%4/%5] ")
                        .arg(saved).arg(saveCallsOf(rig)).arg(wallMs).arg(c1).arg(c0 + 3);

        // 台账面：Clean 1/1 + 字符串态一致（成功唯一权威照旧）。
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const bool ledgerOk = f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1
            && bridge.recoveryState(db) == QStringLiteral("clean");
        ok = ok && ledgerOk;
        if (!ledgerOk)
            diag += QStringLiteral("[ledger st=%1 g=%2/%3] ").arg(int(f1.state))
                        .arg(f1.generation).arg(f1.completeGeneration);

        // 桥直调面：锁已释放/无锁 = 恒 0（零退避承诺的权威面）；守卫面（无 store/空名/缺库）恒 0。
        const int backFree = bridge.exitSaveRetryBackoff(&store, db);
        const int backNoStore = bridge.exitSaveRetryBackoff(nullptr, db);
        const int backNoName = bridge.exitSaveRetryBackoff(&store, QString());
        const QString missing = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_r2038_missing_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(missing);
        const int backMissing = bridge.exitSaveRetryBackoff(&store, missing);
        const bool zeroOk = backFree == 0 && backNoStore == 0 && backNoName == 0
            && backMissing == 0;
        ok = ok && zeroOk;
        if (!zeroOk)
            diag += QStringLiteral("[zero free=%1 nostore=%2 noname=%3 miss=%4] ")
                        .arg(backFree).arg(backNoStore).arg(backNoName).arg(backMissing);

        // 幂等重放（旧门语义）：第二次保存仍零重试零退避、+3 累计。
        const bool saved2 = runBackoffSave(rig);
        const int c2 = store.saveOkCount();
        const bool replayOk = saved2 && saveCallsOf(rig) == 2 && c2 == c1 + 3;
        ok = ok && replayOk;
        if (!replayOk)
            diag += QStringLiteral("[replay r=%1 calls=%2 c=%3/%4] ")
                        .arg(saved2).arg(saveCallsOf(rig)).arg(c2).arg(c1 + 3);

        // 完成门/toast/lastExitSaveOk 语义面原样（两路径写入 + toast 文案 + 完成门先于退出置位）。
        {
            const QString root = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString src = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll())
                                                             : QString();
            const int iGate = src.indexOf(QStringLiteral("let exitSaveOk = runExitSaveWithBackoff()"));
            const int iToast = src.indexOf(QStringLiteral("存档写入失败，本次进度未保存"));
            const int iCover = src.indexOf(QStringLiteral("coverGrabPending = true"));
            const bool semOk = iGate >= 0 && iToast >= 0 && iCover >= 0 && iGate < iToast
                && iToast < iCover
                && src.contains(QStringLiteral("window.lastExitSaveOk = exitSaveOk"))
                && src.contains(QStringLiteral("window.lastExitSaveOk = okClose"));
            ok = ok && semOk;
            if (!semOk)
                diag += QStringLiteral("[sem gate=%1 toast=%2 cover=%3] ")
                            .arg(iGate).arg(iToast).arg(iCover);
        }

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);
        QFile::remove(missing);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2038a lock-free normal-path wall: the shared exit-save retry"
                             " helper stays inert when nothing holds the library (first attempt"
                             " answers true with exactly one call, the call stays far under the"
                             " backoff floor, the bridge probe face answers zero on a free"
                             " library and on every degenerate guard face, the counter moves by"
                             " exactly +3 with the ledger Clean at 1/1, and the completion-gate"
                             " / toast / lastExitSaveOk semantics stay verbatim in both paths)"
                          << (ok ? QString() : diag);
    });

    // ── r2038b：退避承重（真锁注入：桥直调窗 + 锁窗内释放 → 重试收敛恰一次）─────────────────
    runLeg(QStringLiteral("r2038b backoff load-bearing wall under a real exclusive lock (a"
        " self-managed thread connection holds BEGIN EXCLUSIVE and the lock probe pays for"
        " itself: the bridge face called directly under the lock answers within [100,300]"
        " milliseconds with the wall clock capping at 300 and answering zero again right after"
        " release; driving the production wrapper text through the real engine with the lock"
        " released inside the backoff window the first attempt fails, the probe fails, the one"
        " backoff step lands, and the single retry converges true with exactly two calls, the"
        " counter moving by exactly +3 for the retry only, and the ledger advancing"
        " monotonically to a clean 2/2)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("b");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig %1] ").arg(rig.firstWarning);
        setRigFile(rig, db);

        // 基线：Clean 1/1（C++ 直调，+3；后续代次单调与「历史不失真」柱的锚）。
        const int c0 = store.saveOkCount();
        const bool base = bridgeSave(store, db, 11.5);
        const SaveGenerationInfo f0 = bridge.recoveryInfo(db);
        const bool baseOk = base && f0.state == SaveRecoveryState::Clean
            && f0.generation == 1 && f0.completeGeneration == 1
            && store.saveOkCount() == c0 + 3;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base s=%1 st=%2 g=%3/%4 c=%5/%6] ")
                        .arg(base).arg(int(f0.state)).arg(f0.generation)
                        .arg(f0.completeGeneration).arg(store.saveOkCount()).arg(c0 + 3);

        // (i) 桥直调面：锁在持 → 退避窗 [100,300]ms + 墙钟 ≤300（退避上限的行为级证据）。
        LockThread lockA;
        holdExclusive(lockA, db, QStringLiteral("r2038_locker_b1"), 300);
        const bool lockedA = waitLocked(lockA.locked);
        qint64 wallA = -1;
        int backLocked = -1;
        if (lockedA) {
            QElapsedTimer wall;
            wall.start();
            backLocked = bridge.exitSaveRetryBackoff(&store, db);
            wallA = wall.elapsed();
        }
        stopAndJoin(lockA);
        const bool windowOk = lockedA && backLocked >= 100 && backLocked <= 300
            && wallA >= 0 && wallA <= 300 && backLocked <= wallA && lockA.err.isEmpty();
        ok = ok && windowOk;
        if (!windowOk)
            diag += QStringLiteral("[window locked=%1 back=%2 wall=%3 err=%4] ")
                        .arg(lockedA).arg(backLocked).arg(wallA).arg(lockA.err);
        // 释放后直调：恒 0（锁已释放 → 零退避面收敛）。
        const int backAfter = bridge.exitSaveRetryBackoff(&store, db);
        ok = ok && backAfter == 0;
        if (backAfter != 0)
            diag += QStringLiteral("[after %1] ").arg(backAfter);

        // (ii) wrapper 面：首试败（BeginMark 一次性钩 = 瞬时失败，免保存链默认 busy 等待——
        //     段头登记）+ 真锁 60ms 窗内释放 → 探锁败（BUSY_TIMEOUT=0 瞬时真答案）→ 退避档 →
        //     重试（钩已耗 + 锁已放）收敛真 + 恰一次重试。
        int beginFaultBudget = 1;
        bridge.setFaultHook([&beginFaultBudget](SaveFaultStage st) -> bool {
            return st == SaveFaultStage::BeginMark && beginFaultBudget-- > 0;
        });
        LockThread lockB;
        holdExclusive(lockB, db, QStringLiteral("r2038_locker_b2"), 60);
        const bool lockedB = waitLocked(lockB.locked);
        bool conv = false;
        qint64 wallB = -1;
        if (lockedB) {
            QElapsedTimer wall;
            wall.start();
            conv = runBackoffSave(rig); // 内序：首试败（钩）→ 探锁败（真锁）→ 150ms 档 → 重试真
            wallB = wall.elapsed();
        }
        stopAndJoin(lockB);
        bridge.setFaultHook(SaveFaultHook()); // 生产形态复位（一次性钩耗尽后显式归零）
        const int c1 = store.saveOkCount();
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const bool convOk = lockedB && conv && saveCallsOf(rig) == 2 // 恰一次重试（不多不少）
            && wallB >= 100 && wallB <= 400 && c1 == c0 + 6 // 仅重试计数（首试 marker-first 零动）
            && f1.state == SaveRecoveryState::Clean && f1.generation == 2
            && f1.completeGeneration == 2 && lockB.err.isEmpty();
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[conv locked=%1 r=%2 calls=%3 wall=%4 c=%5/%6 st=%7 g=%8/%9]")
                        .arg(lockedB).arg(conv).arg(saveCallsOf(rig)).arg(wallB)
                        .arg(c1).arg(c0 + 6).arg(int(f1.state)).arg(f1.generation)
                        .arg(f1.completeGeneration);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2038b backoff load-bearing wall under a real exclusive lock:"
                             " the bridge face answers inside [100,300] ms under the lock and"
                             " zero right after release, and the production wrapper text driven"
                             " through the real engine converges with the lock released inside"
                             " the backoff window - first attempt fails, probe fails, one"
                             " backoff step, exactly one retry, +3 for the retry only, ledger"
                             " clean at 2/2"
                          << (ok ? QString() : diag);
    });

    // ── r2038c：上限与不谎报（锁全程持有：恰一次重试即止 + 不谎报 + 历史原样 + 释放后收敛）──
    runLeg(QStringLiteral("r2038c cap and honesty wall with the lock held throughout (the"
        " persistent exclusive lock makes both attempts fail and the helper stops at exactly"
        " one retry - the call counter lands on two and never three, the answer is an honest"
        " false with zero counter movement, the ledger history stays untouched at a clean 1/1"
        " with the string face agreeing, and the whole bounded call stays far below any"
        " second-level stall; once the lock is released a fresh call converges true with the"
        " counter resuming at +3 - failure was reported as failure and nothing lied)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig %1] ").arg(rig.firstWarning);
        setRigFile(rig, db);

        // 基线 Clean 1/1（+3）。
        const int c0 = store.saveOkCount();
        const bool base = bridgeSave(store, db, 21.5);
        const SaveGenerationInfo f0 = bridge.recoveryInfo(db);
        const bool baseOk = base && f0.state == SaveRecoveryState::Clean
            && f0.generation == 1 && f0.completeGeneration == 1
            && store.saveOkCount() == c0 + 3;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base s=%1 st=%2 g=%3/%4] ").arg(base).arg(int(f0.state))
                        .arg(f0.generation).arg(f0.completeGeneration);

        // 锁全程持有（长 holdMs + stop 提前放手）+ BeginMark 持续钩（两试皆瞬时败——段头登记
        //   的 busy 等待免除）：两试两败 → 恰一次重试即止 + 不谎报。探锁面全程真锁（BUSY_TIMEOUT=0
        //   瞬时真答案）：首试后探锁败 → 退避档 → 重试仍败 → wrapper 如实 false。
        int beginFaultHold = 99;
        bridge.setFaultHook([&beginFaultHold](SaveFaultStage st) -> bool {
            return st == SaveFaultStage::BeginMark && beginFaultHold-- > 0;
        });
        LockThread lockH;
        holdExclusive(lockH, db, QStringLiteral("r2038_locker_c"), 60000);
        const bool locked = waitLocked(lockH.locked);
        bool saved = true;
        qint64 wallH = -1;
        if (locked) {
            QElapsedTimer wall;
            wall.start();
            saved = runBackoffSave(rig);
            wallH = wall.elapsed();
        }
        stopAndJoin(lockH);
        bridge.setFaultHook(SaveFaultHook()); // 生产形态复位（收敛面前摘钩）
        const int c1 = store.saveOkCount();
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const QString stLocked = bridge.recoveryState(db);
        const bool honestOk = locked && !saved && saveCallsOf(rig) == 2 // 不第二重风暴
            && c1 == c0 + 3 // 两试零计数（marker-first 零部分尝试，t974 契约原样）
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 // 台账历史原样（不可写窗口零写入）
            && stLocked == QStringLiteral("clean") && lockH.err.isEmpty()
            && wallH >= 0 && wallH <= 400; // 有界：一次退避档 + 两试（远低于秒级「未响应」）
        ok = ok && honestOk;
        if (!honestOk)
            diag += QStringLiteral("[honest locked=%1 r=%2 calls=%3 c=%4/%5 st=%6 g=%7/%8 wall=%9]")
                        .arg(locked).arg(saved).arg(saveCallsOf(rig)).arg(c1).arg(c0 + 3)
                        .arg(stLocked).arg(f1.generation).arg(f1.completeGeneration).arg(wallH);

        // 释放后重存收敛：失败如实上报后照常可恢复（+3，代次单调推进 2/2 Clean）。
        const bool conv = runBackoffSave(rig);
        const int c2 = store.saveOkCount();
        const SaveGenerationInfo f2 = bridge.recoveryInfo(db);
        const bool convOk = conv && saveCallsOf(rig) == 3 && c2 == c1 + 3
            && f2.state == SaveRecoveryState::Clean && f2.generation == 2
            && f2.completeGeneration == 2;
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[conv r=%1 calls=%2 c=%3/%4 st=%5 g=%6/%7] ")
                        .arg(conv).arg(saveCallsOf(rig)).arg(c2).arg(c1 + 3).arg(int(f2.state))
                        .arg(f2.generation).arg(f2.completeGeneration);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2038c cap and honesty wall with the lock held throughout:"
                             " exactly one retry then stop (counter on two, never three), an"
                             " honest false with zero counter movement, ledger history intact"
                             " at a clean 1/1, the bounded call far below any second-level"
                             " stall, and a fresh call after release converges true with the"
                             " counter resuming at +3"
                          << (ok ? QString() : diag);
    });

    // ── r2038d：结构钉（共用实现单点 + 两调用点 + 旧 0ms 重放禁入 + 零触碰复钉 + 常量上界源钉）
    runLeg(QStringLiteral("r2038d structure pins (the backoff retry lives at exactly one point:"
        " the shared wrapper is defined once in Main with exactly one bridge probe call and one"
        " msleep site in the bridge source, both exit paths call the wrapper exactly once with"
        " the transient-return chain ahead of the save and the completion gate ahead of the"
        " exit handoff, the two legacy zero-delay immediate-replay lines are absent, the"
        " completion-gate / toast copy / lastExitSaveOk both-paths / transient-return function"
        " / registration-comment faces stay verbatim, and the backoff constant is pinned from"
        " source to the registered cap of at most 300 milliseconds)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① 共用实现单点钉（wrapper 定义恰 1 + 桥探锁调用恰 1 + 两调用点各恰 1——原文计数钉
        //   [注释文本刻意不含这些 needle，原文计数 = 剥注释等价]）。
        {
            QFile mf(srcRoot + QStringLiteral("/ui/Main.qml"));
            const QString qmlSrc = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll())
                                                                : QString();
            const int nDef = int(qmlSrc.count(QStringLiteral("function runExitSaveWithBackoff()")));
            const int nProbe = int(qmlSrc.count(QStringLiteral(
                "SaveBridge.exitSaveRetryBackoff(worldStore, currentWorldFile)")));
            const int nButton = int(qmlSrc.count(QStringLiteral(
                "let exitSaveOk = runExitSaveWithBackoff()")));
            const int nClose = int(qmlSrc.count(QStringLiteral(
                "let okClose = runExitSaveWithBackoff()")));
            const bool singlePoint = nDef == 1 && nProbe == 1 && nButton == 1 && nClose == 1;
            ok = ok && singlePoint;
            if (!singlePoint)
                diag += QStringLiteral("[single def=%1 probe=%2 btn=%3 close=%4] ")
                            .arg(nDef).arg(nProbe).arg(nButton).arg(nClose);
        }
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool noLegacyRetry = forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"),
                                                   "if (!exitSaveOk) exitSaveOk = runExitSave()")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"),
                               "if (!okClose) okClose = runExitSave()")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "= runExitSave()");
        ok = ok && noLegacyRetry;
        if (!noLegacyRetry) diag += QStringLiteral("[legacy-retry-present] ");

        // ② 桥源钉（探锁 SQL 对 + 退避 sleep + 探针零等待选项——pinSet 剥注释形态：注释掉退避
        //   sleep 行即红，摘退避变异无注释死角；重名钉死调用面文本 = 单点收口的反散写钉）。
        const QStringList missSbc = pinSet(srcRoot + QStringLiteral("/Game/savebridge.cpp"), {
            SrcPin("probe takes the write lock", "BEGIN EXCLUSIVE", 1),
            SrcPin("probe releases immediately", "ROLLBACK", 1),
            SrcPin("single backoff sleep site", "QThread::msleep(kExitSaveBackoffMs)", 1),
            SrcPin("probe busy timeout pinned to zero", "QSQLITE_BUSY_TIMEOUT=0", 1),
        });
        for (const QString &m : missSbc) {
            ok = false;
            diag += QStringLiteral("[sbc-%1] ").arg(m);
        }

        // ③ 退避常量 ≤300ms 源钉（声明行解析取值——钉上界不钉死值，调值不红、越界即红）。
        int backoffVal = -1;
        {
            QFile hf(srcRoot + QStringLiteral("/Game/savebridge.h"));
            const QString hSrc = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll())
                                                              : QString();
            const int iDecl = hSrc.indexOf(QStringLiteral("constexpr int kExitSaveBackoffMs ="));
            if (iDecl >= 0) {
                static QRegularExpression numRe(QStringLiteral("=\\s*(\\d+)"));
                const QRegularExpressionMatch m = numRe.match(hSrc.mid(iDecl, 80));
                if (m.hasMatch())
                    backoffVal = m.captured(1).toInt();
            }
        }
        const bool capOk = backoffVal > 0 && backoffVal <= 300;
        ok = ok && capOk;
        if (!capOk)
            diag += QStringLiteral("[cap %1] ").arg(backoffVal);

        // ④ 两路径调用点 + 零触碰复钉（完成门链序 / toast / lastExitSaveOk 两路径 / 归还序前置）。
        {
            QFile mf(srcRoot + QStringLiteral("/ui/Main.qml"));
            const QString src = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll())
                                                             : QString();
            const int iOnClose = src.indexOf(QStringLiteral("onClosing: (close) => {"));
            const QString closeSlice = iOnClose >= 0 ? src.mid(iOnClose, 1400) : QString();
            const int iRetClose = closeSlice.indexOf(QStringLiteral("returnTransientItemsBeforeSave()"));
            const int iSaveClose = closeSlice.indexOf(QStringLiteral("let okClose = runExitSaveWithBackoff()"));
            const int iGate = src.indexOf(QStringLiteral("let exitSaveOk = runExitSaveWithBackoff()"));
            const int iToast = src.indexOf(QStringLiteral("存档写入失败，本次进度未保存"));
            const int iCover = src.indexOf(QStringLiteral("coverGrabPending = true"));
            const int iFnDef = src.indexOf(QStringLiteral("function returnTransientItemsBeforeSave()"));
            const int iRunExitDef = iFnDef >= 0
                                        ? src.indexOf(QStringLiteral("function runExitSave()"), iFnDef) : -1;
            const QString fnSlice = (iFnDef >= 0 && iRunExitDef > iFnDef)
                                        ? src.mid(iFnDef, iRunExitDef - iFnDef) : QString();
            const bool facesOk = iOnClose >= 0 && iRetClose >= 0 && iSaveClose > iRetClose
                && closeSlice.contains(QStringLiteral("worldStore.closeWorld()"))
                && closeSlice.contains(QStringLiteral("window.lastExitSaveOk = okClose"))
                && iGate >= 0 && iToast >= 0 && iCover >= 0 && iGate < iToast && iToast < iCover
                && src.contains(QStringLiteral("window.lastExitSaveOk = exitSaveOk"))
                // 完成门/归还序零触碰复钉（定义 + 两路径调用 = 恰 3；三归还臂 + 三合成格 + 手持兜底）。
                && src.count(QStringLiteral("returnTransientItemsBeforeSave()")) == 3
                && fnSlice.contains(QStringLiteral("if (enchantingTableOpen) closeEnchantingTable()"))
                && fnSlice.contains(QStringLiteral("if (anvilOpen) closeAnvil()"))
                && fnSlice.contains(QStringLiteral("if (dispenserOpen) closeDispenser()"))
                && fnSlice.count(QStringLiteral("returnCraftToHotbar()")) == 3
                && fnSlice.contains(QStringLiteral("if (inventoryOpen) closeInventory()"))
                && fnSlice.contains(QStringLiteral("returnHeldToHotbar()"))
                // 登记注释面（#36 清偿留痕恰两处 + 退避上限方向词仍在位）。
                && src.count(QStringLiteral("review0901 #36")) == 2
                && src.contains(QStringLiteral("退避 ≤300ms"));
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[faces close=%1 ret=%2 save=%3 gate=%4 toast=%5 cover=%6]")
                            .arg(iOnClose).arg(iRetClose).arg(iSaveClose).arg(iGate).arg(iToast)
                            .arg(iCover);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2038d structure pins: the backoff retry lives at exactly one"
                             " point (wrapper defined once, one bridge probe call, one msleep"
                             " site, busy timeout pinned to zero), both exit paths call the"
                             " wrapper exactly once with the transient-return chain ahead and"
                             " the completion gate ahead of the exit handoff, the legacy"
                             " zero-delay replay lines are gone, the completion-gate / toast /"
                             " lastExitSaveOk / transient-return faces stay verbatim, and the"
                             " backoff constant is pinned from source to at most 300 ms"
                          << (ok ? QString() : diag);
    });
}
