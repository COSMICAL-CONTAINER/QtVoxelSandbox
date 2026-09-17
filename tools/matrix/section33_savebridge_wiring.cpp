#include "matrix_helpers.h"

#include "savebridge.h"       // 被测面：t1057 QML 消费桥（QML 单例 instance = 生产/QML 同对象）
#include "savecoordinator.h"  // 被测面：台账读数结构 / 恢复态枚举（#5② OpenError 可区分态）
#include "worldstore.h"       // 被测面：三写执行体（saveOkCount 观测面 + 读回 Q_INVOKABLE 面）

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSqlDatabase>
#include <QSqlQuery>

// t1057 SaveCoordinator 生产接线 探针段（4 腿 r2031a-d；filter 词 "r2031"；矩阵 657→657+N）。
// 置尾先例沿用（接 section32，runAll 末执行）；fresh 小世界 + 临时 SQLite 库（pid 键名，测试
// 自清理——r2015 段同门绝对路径），真实用户 saves/ 零触碰。任务契约（refactor-plan §29.6 原文）：
//   「fixed 零变化墙」→ r2031a（Clean 路径经真 QQmlEngine 走桥：返回 true + saveOkCount 恰 +3
//     + 三写参数逐位透传读回[meta/player/progress/worldTime/bed/chests] + 重读档栅格逐位 +
//     旧档 Fresh 读档回归[无台账旧库与裸 store 保存均 fresh、旧读路径照旧]——桥即唯一路径后
//     「无桥路径」不复存在，墙 = 参数透传断言 + Fresh 回归，见任务书留痕）；
//   「marker→complete 往返 + 中断恢复」→ r2031b（真保存走桥 → save_coord 两键 raw 直读断言
//     [attempt 标记 + complete 戳] + 代次单调推进；SaveFaultHook 经桥注入三写中途失败 → 返回
//     false + 计数只动已写部分 + 恢复态 Interrupted；Finalize 段注入 = 三部分全写仍不得报成功
//     [complete 戳 = 成功唯一权威]；清钩重存收敛 Clean 且代次跨中断单调不重置不复用——r2015
//     腿族语义的生产化等价口径）；
//   「#5② 开库失败可区分」→ r2031c（真锁占 BEGIN EXCLUSIVE → 恢复态 open-error 非 fresh +
//     桥保存拒存零部分尝试 + 锁释放后台账历史不失真[代次键原样]；open 级失败[库路径为目录]同
//     归 open-error——两面目可区分且都不谎报）；
//   「结构钉」→ r2031d（worldstore 零触碰反探[SaveBridge/save_coord 禁入 + schema 版本原样] +
//     additive 正面钉[save_coord 幂等建表 + OpenError 守卫落位] + QML 两处例外面钉
//     [saveViaCoordinator/recoveryState 各恰一处 + StreamingBridge 恰两处 + 会话类型禁入复述 +
//     toast 文案在位] + 生产零挂载反探[setFaultHook 不入 Main.qml/main.cpp]）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘 complete 戳（savecoordinator.cpp 收尾戳条件加 false && 前缀 = 写完不盖且 receipt
//     仍报成功）→ 每次保存 gen>complete 恒在 → 声明红面 = r2031 族内 {r2031b, r2031c} + **跨族
//     同源 {r2015a, r2015b, r2015c, r2019b}**（complete 戳是共享语义：r2015a/b/c 与 r2019b 的
//     「成功保存后台账 Clean/complete_generation」断言同样锚被摘语义本体——戳权威被摘则这些腿
//     如实全红，按 t1053/r2021 改声明先例如实预声明非连带伤；r2031a 不红 = 恰红面前置设计的
//     墙零台账断言兑现；r2015d/r2031d 不红 = 文本钉非行为钉）。跨族面由实测核对（filter
//     r2015/r2019 同轮跑红面计数入日志）。
//   NEG-2 摘 #5② 错误态（savecoordinator.cpp recover() 的 OpenError 派生改回 Fresh）→ 锁占下
//     恢复态谎报 fresh → 声明红面 {r2031c}（open-error 字符串柱 + 枚举柱同源红）。r2031a 不误伤
//     （legacy 库走 NoTable→Fresh 合法路径不变）；r2031b 不误伤（台账可读路径派生不变）；
//     r2015 不误伤（其真锁柱锚 error 码 kErrSaveCoordSql + 计数零动——新守卫跳过落回 coordUpsert
//     失败旧路，可观测量逐位同）；r2031d 不误伤（OpenError 文本钉 ≥1 不受派生式改写影响）。
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section33_savebridge_wiring()
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
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2031_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 地表标记放置（选址纪律：显式读高度 + 放置回读校验，静默拒绝即响亮失败）。
    const auto placeMarker = [](World &w, int x, int z) -> int {
        const int h = w.heightAt(x, z);
        if (h < 0 || h + 2 >= w.height()) return -1;
        w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
        if (w.blockAt(x, h + 1, z) != quint8(BR::Stone)) return -1;
        return h + 1;
    };
    // 栅格指纹（blockAt+stateAt 全栅格——Clean 路径保存/读档往返逐位恒等的比较面）。
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
    // save_coord 两键 raw 直读（独立临时连接开-用-关；缺表 = 空 map——Fresh 面判据）。
    const auto coordKeys = [](const QString &db) {
        QVariantMap out;
        const QString conn = QStringLiteral("r2031_probe_%1").arg(QCoreApplication::applicationPid());
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        {
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery q(p);
                if (q.exec(QStringLiteral("SELECT key, value FROM save_coord")))
                    while (q.next())
                        out.insert(q.value(0).toString(), q.value(1).toString());
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    };
    // 重读档 incantation（rebind 纪律钉在腿文：setWorld 先于 loadChunks）。
    const auto reloadWorld = [](WorldStore &store, World &target, int expectChunks, QString &why) {
        target.beginLoad(kSeedF); // 零填充分区网格（loadChunks 前置，同 Main.qml 载入流）
        store.setWorld(&target);
        const bool opened = store.isOpen();
        const int n = opened ? store.loadChunks() : -1;
        target.finishLoad();
        if (n != expectChunks) {
            why = QStringLiteral("loadChunks=%1 expect=%2 opened=%3").arg(n).arg(expectChunks).arg(opened);
            return false;
        }
        return true;
    };
    // 真链引擎装配（真 QQmlEngine + 上下文注入 + setData wrapper——真链腿族同门；引擎零警告门）。
    //   wrapper = Main.qml runExitSave 三写段 / enterWorld 恢复态读面的最小消费镜像——经真引擎
    //   调真桥单例（C++/QML 共用同一对象），QML→C++ 类型化指针 + QVariant 载荷转换链与生产同构
    //   （载荷在 JS 侧组 map/list = 生产 gatherPlayerState/allChests 同形）。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeRig = [](Rig &rig, SaveBridge *bridge, WorldStore *store) {
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 既有真链先例同款：防磁盘缓存重定向
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2031Bridge"), bridge);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2031Store"), store);
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
    property string wname: ""
    property bool saved: false
    property string recState: ""
    // 退出存档三写段镜像（Main.qml runExitSave 置换段同式——分参全集过真引擎转换链）。
    function exitSave(px, stat) {
        saved = r2031Bridge.saveViaCoordinator(r2031Store, file, wname,
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
    // 恢复态读面镜像（Main.qml enterWorld 恢复态段同式——字符串态直读；函数名与属性名避开）。
    function recStateOf() {
        recState = r2031Bridge.recoveryState(file)
        return recState
    }
}
)QML", QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine); // 引擎析构兜底
    };
    const auto setRigFile = [](Rig &rig, const QString &file, const QString &name) {
        if (!rig.wrap)
            return;
        rig.wrap->setProperty("file", file);
        rig.wrap->setProperty("wname", name);
    };
    const auto qmlExitSave = [](Rig &rig, double px, int stat) {
        QVariant savedOut(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "exitSave", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, savedOut),
                Q_ARG(QVariant, px), Q_ARG(QVariant, stat));
        return savedOut.toBool();
    };
    const auto qmlRecState = [](Rig &rig) {
        QVariant st(QStringLiteral("<unset>"));
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "recStateOf", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, st));
        return st.toString();
    };

    SaveBridge &bridge = *SaveBridge::instance();
    bridge.setFaultHook(SaveFaultHook()); // 桥间复位缝（singleton 跨腿共享——入口归零 = 生产形态）

    // ── r2031a：Clean 路径零变化墙（返回语义/计数观测/参数透传/往返/旧档 Fresh 回归）────────
    runLeg(QStringLiteral("r2031a clean-path zero-change wall through the production bridge (the"
        " coordinated exit save keeps every legacy observable bitwise: real-engine save answers"
        " true, the write-completion counter moves by exactly +3, every payload passes through"
        " byte-identical (meta name, player pose/health/xp, progress stat, world clock"
        " phase/day/weather/timer, bed anchor, chest/furnace/dispenser tables), the reloaded"
        " world matches the captured grid bitwise with the marker present, and legacy saves stay"
        " Fresh with every old read path intact (bare store save on a second db never mints a"
        " ledger, a missing library answers fresh, the bare db reloads bit-identical)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        const int mAy = placeMarker(w, 10, 10);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");
        const QByteArray gRef = gridOf(w);

        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &bridge, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig] ");
        setRigFile(rig, db, QStringLiteral("r2031a"));

        // Clean 路径：真链三写段（返回语义 = 旧 runExitSave「三写全成才真」逐位同）。
        const int c0 = store.saveOkCount();
        const bool saved = qmlExitSave(rig, 44.5, 7);
        const int c1 = store.saveOkCount();
        const bool saveOk = saved && c1 == c0 + 3; // 计数观测面逐位同旧：三部分各 +1 = +3
        ok = ok && saveOk;
        if (!saveOk)
            diag += QStringLiteral("[save r=%1 c=%2/%3] ").arg(saved).arg(c1).arg(c0 + 3);

        // 参数逐位透传（三写照旧经 store 现有读面读回——每参一组判据）。
        const QVariantMap meta = store.loadMeta();
        const QVariantMap pd = store.loadPlayerData();
        const QVariantMap pr = store.loadProgress();
        const QVariantMap wt = store.loadWorldTime();
        const QVariantMap bs = store.loadBedSpawn();
        const QVariantList ch = store.loadChests();
        const QVariantList fu = store.loadFurnaces();
        const QVariantList di = store.loadDispensers();
        const bool passOk = meta.value(QStringLiteral("name")).toString() == QStringLiteral("r2031a")
            && pd.value(QStringLiteral("px")).toDouble() == 44.5
            && pd.value(QStringLiteral("py")).toDouble() == 41.5
            && pd.value(QStringLiteral("yaw")).toDouble() == 1.25
            && pd.value(QStringLiteral("health")).toDouble() == 17
            && pd.value(QStringLiteral("xp")).toDouble() == 2
            && pr.value(QStringLiteral("statPlayedMinutes")).toInt() == 7
            && wt.value(QStringLiteral("phase")).toDouble() == 0.25
            && wt.value(QStringLiteral("day")).toLongLong() == 3
            && wt.value(QStringLiteral("weather")).toInt() == 1
            && wt.value(QStringLiteral("hasWeather")).toBool()
            && wt.value(QStringLiteral("weatherTimerMs")).toLongLong() == 45000
            && bs.value(QStringLiteral("hasBed")).toBool()
            && bs.value(QStringLiteral("x")).toDouble() == 5.5
            && ch.size() == 1 && fu.size() == 1 && di.size() == 1;
        ok = ok && passOk;
        if (!passOk)
            diag += QStringLiteral("[pass meta=%1 px=%2 yaw=%3 hp=%4 xp=%5 stat=%6 ph=%7 day=%8"
                                   " w=%9 wt=%10 bed=%11 bx=%12 ch=%13 fu=%14 di=%15] ")
                        .arg(meta.value(QStringLiteral("name")).toString())
                        .arg(pd.value(QStringLiteral("px")).toDouble())
                        .arg(pd.value(QStringLiteral("yaw")).toDouble())
                        .arg(pd.value(QStringLiteral("health")).toDouble())
                        .arg(pd.value(QStringLiteral("xp")).toDouble())
                        .arg(pr.value(QStringLiteral("statPlayedMinutes")).toInt())
                        .arg(wt.value(QStringLiteral("phase")).toDouble())
                        .arg(wt.value(QStringLiteral("day")).toLongLong())
                        .arg(wt.value(QStringLiteral("weather")).toInt())
                        .arg(wt.value(QStringLiteral("weatherTimerMs")).toLongLong())
                        .arg(bs.value(QStringLiteral("hasBed")).toBool())
                        .arg(bs.value(QStringLiteral("x")).toDouble())
                        .arg(ch.size()).arg(fu.size()).arg(di.size());

        // 重读档往返逐位（Clean 路径保存/读档行为同旧）。
        World w2;
        freshWorld48(w2);
        QString why;
        const bool loadOk = reloadWorld(store, w2, 9, why);
        const bool roundOk = loadOk && gridOf(w2) == gRef
            && w2.blockAt(10, mAy, 10) == quint8(BR::Stone);
        ok = ok && roundOk;
        if (!roundOk)
            diag += QStringLiteral("[round %1 same=%2] ")
                        .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                        .arg(gridOf(w2) == gRef ? 1 : 0);

        // 台账断言刻意缺席（恰红面设计：戳权威/两键值归下一腿专钉——本墙对 NEG-1 摘戳零敏感，
        //   行为面/计数面/透传面/Fresh 回归即零变化墙的全部）。

        // 旧档 Fresh 读档回归（旧码形态在桥侧可判读且旧读路径照旧）。
        {
            const QString dbL = tempDb("aL");
            QFile::remove(dbL); // fresh
            const bool missFresh = bridge.recoveryState(QStringLiteral("<no-such-db>"))
                    == QStringLiteral("fresh"); // 无库 = 无台账 = Fresh
            World wL;
            freshWorld48(wL);
            WorldStore storeL;
            const bool openL = storeL.openWorld(dbL);
            storeL.setWorld(&wL);
            const int cL0 = storeL.saveOkCount();
            const bool legacy = storeL.saveAll(QStringLiteral("r2031a-legacy")); // 裸写：无台账
            const int cL1 = storeL.saveOkCount();
            const bool legacyFresh = bridge.recoveryState(dbL) == QStringLiteral("fresh")
                && bridge.recoveryInfo(dbL).state == SaveRecoveryState::Fresh
                && bridge.recoveryInfo(dbL).generation == 0;
            World wL2;
            freshWorld48(wL2);
            // 裸 saveAll 保存了绑定世界（9 chunk blob 在库）→ 重读 9 块 = 旧读路径照旧。
            const bool legacyLoad = reloadWorld(storeL, wL2, 9, why);
            storeL.setWorld(&wL);
            const bool legacyOk = missFresh && openL && legacy && cL1 == cL0 + 1
                && legacyFresh && legacyLoad && gridOf(wL2) == gridOf(wL);
            ok = ok && legacyOk;
            if (!legacyOk)
                diag += QStringLiteral("[legacy miss=%1 open=%2 save=%3 c=%4/%5 st=%6 load=%7 %8] ")
                            .arg(missFresh).arg(openL).arg(legacy).arg(cL1).arg(cL0 + 1)
                            .arg(int(bridge.recoveryInfo(dbL).state))
                            .arg(legacyLoad)
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why);
            storeL.closeWorld();
            QFile::remove(dbL);
        }

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2031a clean-path zero-change wall: the coordinated exit save"
                             " keeps every legacy observable bitwise (true return, +3 exact"
                             " counter movement, byte-identical payload passthrough across"
                             " meta/player/progress/clock/bed/container tables, bitwise grid"
                             " reload with the marker present) and legacy saves stay Fresh"
                             " with every old read path intact"
                          << (ok ? QString() : diag);
    });

    // ── r2031b：marker→complete 往返 + 中断恢复收敛 + 戳权威（FaultHook 经桥注入）────────────
    runLeg(QStringLiteral("r2031b marker-to-complete roundtrip and interruption convergence"
        " (real saves through the bridge land both ledger keys with raw reads proving it: the"
        " first coordinated save writes the in-flight generation and the complete stamp at 1/1"
        " and the second advances monotonically to 2/2; a bridge-mounted fault at the player"
        " stage returns false with only the world part counted and the recovery state honestly"
        " Interrupted 3>2 while the previous player data stays readable after reload; a fault at"
        " the finalize stage persists all three parts (counter +3) yet still must not report"
        " success - the complete stamp is the only success authority, Interrupted 4>2; clearing"
        " the hook converges at generation 5/5 Clean (interrupted numbers consumed, never reset"
        " or reused)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("b");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        const int mAy = placeMarker(w, 12, 12);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");
        const QByteArray gRef = gridOf(w);

        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &bridge, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig] ");
        setRigFile(rig, db, QStringLiteral("r2031b"));

        // 保存 #1：两键 raw 直读（attempt 标记 + complete 戳同值 1/1）。
        const int c0 = store.saveOkCount();
        const bool s1 = qmlExitSave(rig, 11.5, 1);
        QVariantMap k1 = coordKeys(db);
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const int c1 = store.saveOkCount();
        const bool round1Ok = s1 && k1.value(QStringLiteral("generation")).toString() == QLatin1String("1")
            && k1.value(QStringLiteral("complete_generation")).toString() == QLatin1String("1")
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 && c1 == c0 + 3;
        ok = ok && round1Ok;
        if (!round1Ok)
            diag += QStringLiteral("[r1 s=%1 g=%2 c=%3 st=%4 cnt=%5/%6] ")
                        .arg(s1).arg(k1.value(QStringLiteral("generation")).toString(),
                                     k1.value(QStringLiteral("complete_generation")).toString())
                        .arg(int(f1.state)).arg(c1).arg(c0 + 3);

        // 保存 #2：代次单调推进 2/2。
        const bool s2 = qmlExitSave(rig, 22.5, 2);
        k1 = coordKeys(db);
        const SaveGenerationInfo f2 = bridge.recoveryInfo(db);
        const bool advanceOk = s2
            && k1.value(QStringLiteral("generation")).toString() == QLatin1String("2")
            && k1.value(QStringLiteral("complete_generation")).toString() == QLatin1String("2")
            && f2.state == SaveRecoveryState::Clean && f2.generation == 2;
        ok = ok && advanceOk;
        if (!advanceOk)
            diag += QStringLiteral("[r2 s=%1 g=%2 c=%3 st=%4] ")
                        .arg(s2).arg(k1.value(QStringLiteral("generation")).toString(),
                                     k1.value(QStringLiteral("complete_generation")).toString())
                        .arg(int(f2.state));

        // 中途失败（Player 段经桥钩注入）：world 部分落、player 不落 → false + Interrupted 2 域
        // 面如实（计数只动已写部分）+ 旧 player 数据重读照旧（短路保护生产面）。
        bridge.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
        const bool s3 = qmlExitSave(rig, 33.5, 3);
        const SaveGenerationInfo f3 = bridge.recoveryInfo(db);
        const int c3 = store.saveOkCount();
        const bool partOk = !s3 && f3.state == SaveRecoveryState::Interrupted
            && f3.generation == 3 && f3.completeGeneration == 2
            && c3 == c1 + 4; // +3（#2）+1（#3 只 world 部分计入）
        ok = ok && partOk;
        if (!partOk)
            diag += QStringLiteral("[r3 s=%1 st=%2 g=%3/%4 c=%5/%6] ")
                        .arg(s3).arg(int(f3.state)).arg(f3.generation)
                        .arg(f3.completeGeneration).arg(c3).arg(c1 + 4);
        {
            World wL;
            freshWorld48(wL);
            QString why;
            const bool load3 = reloadWorld(store, wL, 9, why);
            store.setWorld(&w);
            const QVariantMap pd3 = store.loadPlayerData();
            const bool oldReadable = load3 && gridOf(wL) == gRef
                && pd3.value(QStringLiteral("px")).toDouble() == 22.5; // player 部分未落 = #2 值
            ok = ok && oldReadable;
            if (!oldReadable)
                diag += QStringLiteral("[r3read load=%1 %2 px=%3] ")
                            .arg(why.isEmpty() ? QStringLiteral("ok") : why)
                            .arg(gridOf(wL) == gRef ? 1 : 0)
                            .arg(pd3.value(QStringLiteral("px")).toDouble());
        }

        // Finalize 段注入（崩溃在收尾前形态）：三部分全写仍不得报成功——戳是成功唯一权威。
        bridge.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Finalize; });
        const bool s4 = qmlExitSave(rig, 44.5, 4);
        const SaveGenerationInfo f4 = bridge.recoveryInfo(db);
        const int c4 = store.saveOkCount();
        const bool stampOk = !s4 && f4.state == SaveRecoveryState::Interrupted
            && f4.generation == 4 && f4.completeGeneration == 2
            && c4 == c3 + 3; // 三部分全计 = 数据已写，但成功仍不报
        ok = ok && stampOk;
        if (!stampOk)
            diag += QStringLiteral("[r4 s=%1 st=%2 g=%3/%4 c=%5/%6] ")
                        .arg(s4).arg(int(f4.state)).arg(f4.generation)
                        .arg(f4.completeGeneration).arg(c4).arg(c3 + 3);

        // 清钩重存收敛：代次跨中断单调 5/5、恢复态 Clean（中断号消耗不重置不复用）。
        bridge.setFaultHook(SaveFaultHook()); // 生产形态复位
        const bool s5 = qmlExitSave(rig, 55.5, 5);
        k1 = coordKeys(db);
        const SaveGenerationInfo f5 = bridge.recoveryInfo(db);
        const QVariantMap pd5 = store.loadPlayerData();
        const bool convOk = s5 && f5.state == SaveRecoveryState::Clean
            && f5.generation == 5 && f5.completeGeneration == 5
            && k1.value(QStringLiteral("complete_generation")).toString() == QLatin1String("5")
            && pd5.value(QStringLiteral("px")).toDouble() == 55.5
            && store.saveOkCount() == c4 + 3;
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[r5 s=%1 st=%2 g=%3/%4 key=%5 px=%6 c=%7/%8] ")
                        .arg(s5).arg(int(f5.state)).arg(f5.generation)
                        .arg(f5.completeGeneration)
                        .arg(k1.value(QStringLiteral("complete_generation")).toString())
                        .arg(pd5.value(QStringLiteral("px")).toDouble())
                        .arg(store.saveOkCount()).arg(c4 + 3);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2031b marker-to-complete roundtrip and interruption convergence:"
                             " raw ledger reads prove both keys land and advance monotonically"
                             " (1/1, 2/2); a player-stage fault answers false with only the world"
                             " part counted and honestly Interrupted; a finalize-stage fault"
                             " persists all three parts yet never reports success (the stamp is"
                             " the only authority); clearing the hook converges Clean at 5/5"
                             " with interrupted numbers consumed and never reused"
                          << (ok ? QString() : diag);
    });

    // ── r2031c：#5② 开库/读库失败可区分（真锁占 + open 级失败两面目；不谎报不重编）──────────
    runLeg(QStringLiteral("r2031c distinguishable ledger-unreadable state (acceptance: an"
        " unopenable or unreadable ledger must never masquerade as Fresh - a real second-"
        " connection BEGIN EXCLUSIVE over a Clean-at-1/1 ledger reads open-error through the"
        " bridge (not fresh), a save attempt under the lock is refused with zero parts attempted"
        " and zero counter movement, and after release the ledger still reads clean at exactly"
        " generation 1/1 (no renumbering, history intact); an open-level failure (the library"
        " path exists but is a directory) reads open-error through both the string face and the"
        " struct face - the two failure modes are distinguishable from Fresh and never lie)"),
        [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        const int mAy = placeMarker(w, 14, 14);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");

        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened) diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        Rig rig;
        makeRig(rig, &bridge, &store);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig] ");
        setRigFile(rig, db, QStringLiteral("r2031c"));

        // 基线：一次成功保存 → Clean 1/1（后续「历史不失真」柱的锚）。
        const int c0 = store.saveOkCount();
        const bool s1 = qmlExitSave(rig, 11.5, 1);
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const bool baseOk = s1 && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 && store.saveOkCount() == c0 + 3;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base s=%1 st=%2 g=%3/%4 c=%5/%6] ")
                        .arg(s1).arg(int(f1.state)).arg(f1.generation)
                        .arg(f1.completeGeneration).arg(store.saveOkCount()).arg(c0 + 3);

        // (a) 真锁占（t974 先例：第二连接 BEGIN EXCLUSIVE 瞬持库写锁；SQLite open 惰性——锁占在
        //     读面炸 = #5② 的读不了面目）：
        {
            QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                            QStringLiteral("r2031_locker"));
            locker.setDatabaseName(db);
            const bool lOpen = locker.open();
            QSqlQuery lq(locker);
            const bool lLock = lOpen && lq.exec(QStringLiteral("BEGIN EXCLUSIVE"));
            const QString stLocked = qmlRecState(rig);
            const SaveGenerationInfo fL = bridge.recoveryInfo(db);
            const bool lockStateOk = lOpen && lLock
                && stLocked == QStringLiteral("open-error")
                && fL.state == SaveRecoveryState::OpenError
                && fL.state != SaveRecoveryState::Fresh; // 可区分：绝不与无台账混淆
            ok = ok && lockStateOk;
            if (!lockStateOk)
                diag += QStringLiteral("[lock open=%1 lck=%2 st=%3 enum=%4] ")
                            .arg(lOpen).arg(lLock).arg(stLocked).arg(int(fL.state));
            // 锁下保存：拒存（零部分尝试、计数零动、无代次重编）。
            const int cLock = store.saveOkCount();
            const bool sLock = qmlExitSave(rig, 22.5, 2);
            const bool refuseOk = !sLock && store.saveOkCount() == cLock;
            ok = ok && refuseOk;
            if (!refuseOk)
                diag += QStringLiteral("[refuse s=%1 c=%2/%3] ")
                            .arg(sLock).arg(store.saveOkCount()).arg(cLock);
            lq.exec(QStringLiteral("ROLLBACK")); // 释放（QSqlQuery 先于连接销毁出作用域）
            locker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("r2031_locker"));

        // 锁释放后：台账历史不失真（代次键原样 1/1 = Clean）——不可读窗口零写入。
        const SaveGenerationInfo fAfter = bridge.recoveryInfo(db);
        const QString stAfter = qmlRecState(rig);
        const bool intactOk = fAfter.state == SaveRecoveryState::Clean
            && fAfter.generation == 1 && fAfter.completeGeneration == 1
            && stAfter == QStringLiteral("clean");
        ok = ok && intactOk;
        if (!intactOk)
            diag += QStringLiteral("[intact st=%1 enum=%2 g=%3/%4] ")
                        .arg(stAfter).arg(int(fAfter.state))
                        .arg(fAfter.generation).arg(fAfter.completeGeneration);

        // (b) open 级失败（库路径存在但是目录 → sqlite open 直接拒 = #5② 的打不开面目）：
        {
            const QString dirPath = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_r2031_dir_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QDir(dirPath).removeRecursively();
            if (!QDir().mkpath(dirPath)) {
                ok = false;
                diag += QStringLiteral("[mkpath] ");
            }
            const QString stDir = bridge.recoveryState(dirPath);
            const SaveGenerationInfo fDir = bridge.recoveryInfo(dirPath);
            const bool dirOk = stDir == QStringLiteral("open-error")
                && fDir.state == SaveRecoveryState::OpenError;
            ok = ok && dirOk;
            if (!dirOk)
                diag += QStringLiteral("[dir st=%1 enum=%2] ").arg(stDir).arg(int(fDir.state));
            QDir(dirPath).removeRecursively(); // 用后即删
        }

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2031c distinguishable ledger-unreadable state: an exclusive lock"
                             " over a Clean 1/1 ledger reads open-error (never fresh), the save"
                             " attempt under it is refused with zero counter movement, and the"
                             " released ledger still reads clean at exactly 1/1 (no renumbering);"
                             " an open-level failure (library path is a directory) reads"
                             " open-error through both faces - distinguishable and never lying"
                          << (ok ? QString() : diag);
    });

    // ── r2031d：结构钉（worldstore 零触碰反探 + additive 正面钉 + QML 两处例外面钉 + 零挂载）──
    runLeg(QStringLiteral("r2031d structure pins (the frozen store domain stays blind and intact:"
        " no SaveBridge token in worldstore.{h,cpp} with the schema version and counter property"
        " unmoved, the ledger stays a pure additive idempotent-create table with its own named"
        " connection, the distinguishable-error machinery is pinned at its authority points (the"
        " OpenError value, the unreadable-ledger refusal in the unified save, the sqlite-master"
        " probe); the bridge surface is exactly the singleton pair of invokables with the hook"
        " pass-through as its single mount seam; the QML exception face is exactly two SaveBridge"
        " calls in Main (one save swap inside the exit-save function, one recovery read at world"
        " entry) with the registered toast copy in place, the streaming bridge still touched"
        " exactly twice, session and coordinator type names still absent from Main, and the"
        " fault hook mounted nowhere in production (Main.qml and the app entry carry no"
        " mounting call)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① 冻结域（worldstore）零触碰反探 + 保持现状正面钉（本单新增桥词元也禁入）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool wsBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "SaveBridge")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "save_coord")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "SaveBridge")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "save_coord");
        ok = ok && wsBlind;
        if (!wsBlind) diag += QStringLiteral("[store-not-blind] ");
        const QStringList missWsh = pinSet(srcRoot + QStringLiteral("/World/worldstore.h"), {
            SrcPin("store schema version unmoved", "static constexpr int kSchemaVersion = 2", 1),
            SrcPin("store counter face intact",
                   "Q_PROPERTY(int saveOkCount READ saveOkCount NOTIFY saveOkCountChanged)", 1),
            SrcPin("store unified save face intact", "Q_INVOKABLE bool saveAll(", 1),
        });
        for (const QString &m : missWsh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② additive 正面钉（save_coord = 幂等建表 + 独立命名连接；无版本 bump 面复述）。
        const QStringList missScp = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.cpp"), {
            SrcPin("ledger additive create", "CREATE TABLE IF NOT EXISTS %1 (key TEXT PRIMARY KEY", 1),
            SrcPin("ledger table key", "static const char *const kCoordTable = \"save_coord\"", 1),
            SrcPin("ledger named connection", "static const char *const kCoordConn", 1),
            SrcPin("unreadable refusal in unified save",
                   "SaveRecoveryState::OpenError", 1),
            SrcPin("master-table probe", "sqlite_master", 1),
        });
        for (const QString &m : missScp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missSch = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("distinguishable state value", "OpenError", 1),
            SrcPin("recovery state authority", "enum class SaveRecoveryState", 1),
            SrcPin("recover entry intact", "SaveGenerationInfo recover() const", 1),
        });
        for (const QString &m : missSch) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 桥面正面钉（QML 单例 + 两件 Q_INVOKABLE + 钩子缝唯一透传点）。
        const QStringList missSbh = pinSet(srcRoot + QStringLiteral("/Game/savebridge.h"), {
            SrcPin("bridge class", "class SaveBridge : public QObject", 1),
            SrcPin("bridge qml name", "QML_NAMED_ELEMENT(SaveBridge)", 1),
            SrcPin("bridge singleton macro", "QML_SINGLETON", 1),
            SrcPin("bridge save invokable",
                   "Q_INVOKABLE bool saveViaCoordinator(WorldStore *store", 1),
            SrcPin("bridge recovery invokable",
                   "Q_INVOKABLE QString recoveryState(const QString &worldFile) const", 1),
            SrcPin("bridge singleton factory",
                   "static SaveBridge *create(QQmlEngine *, QJSEngine *)", 1),
        });
        for (const QString &m : missSbh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missSbc = pinSet(srcRoot + QStringLiteral("/Game/savebridge.cpp"), {
            SrcPin("hook pass-through single seam", "coord.setFaultHook(m_faultHook)", 1),
            SrcPin("refusal warning face", "saveViaCoordinator: coordinated save failed", 1),
        });
        for (const QString &m : missSbc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        // 桥零值属性（值组件零暴露——r2028 桥同门）。
        const bool sbNoProp = forbiddenAbsent(srcRoot + QStringLiteral("/Game/savebridge.h"), "Q_PROPERTY");
        ok = ok && sbNoProp;
        if (!sbNoProp) diag += QStringLiteral("[bridge-value-prop] ");

        // ④ QML 两处例外面钉（变更面集中：saveViaCoordinator 恰 1 / recoveryState 恰 1 +
        //   StreamingBridge 恰 2 复述 + toast 文案 + 类型名禁入复述）。
        {
            QFile f(srcRoot + QStringLiteral("/ui/Main.qml"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const int saveCalls = int(src.count(QLatin1String("SaveBridge.saveViaCoordinator")));
            const int recCalls = int(src.count(QLatin1String("SaveBridge.recoveryState")));
            const int streamCalls = int(src.count(QLatin1String("StreamingBridge.")));
            const bool concentrated = saveCalls == 1 && recCalls == 1 && streamCalls == 2
                && src.contains(QString::fromUtf8(
                    "上次保存未完成，已载入最后完整数据；建议立即重新保存"))
                && src.contains(QStringLiteral("return SaveBridge.saveViaCoordinator(worldStore"));
            ok = ok && concentrated;
            if (!concentrated)
                diag += QStringLiteral("[qml-face save=%1 rec=%2 stream=%3 toast=%4 swap=%5] ")
                            .arg(saveCalls).arg(recCalls).arg(streamCalls)
                            .arg(src.contains(QString::fromUtf8(
                                     "上次保存未完成，已载入最后完整数据；建议立即重新保存")))
                            .arg(src.contains(QStringLiteral(
                                     "return SaveBridge.saveViaCoordinator(worldStore")));
            // 类型名禁入复述（会话壳 + 协调层本体不入 QML——零迁移钉站立）。
            const bool typeBlind = !src.contains(QLatin1String("GameSession"))
                && !src.contains(QLatin1String("SaveCoordinator"))
                && !src.contains(QLatin1String("SaveRequest"))
                && !src.contains(QLatin1String("setFaultHook"));
            ok = ok && typeBlind;
            if (!typeBlind) diag += QStringLiteral("[qml-type-blind] ");
        }

        // ⑤ 生产零挂载反探（钩子缝不入 app 入口；桥注册 = 纯声明式无手工注册行）。
        {
            QFile fMain(QStringLiteral("main.cpp"));
            QString appMain;
            if (fMain.open(QIODevice::ReadOnly))
                appMain = QString::fromUtf8(fMain.readAll());
            const bool entryClean = !appMain.contains(QLatin1String("SaveBridge"))
                && !appMain.contains(QLatin1String("setFaultHook"));
            ok = ok && entryClean;
            if (!entryClean) diag += QStringLiteral("[entry-mount] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2031d structure pins: the frozen store domain stays blind"
                             " (no bridge or ledger tokens, schema and faces unmoved), the"
                             " ledger is a pure additive idempotent-create table with the"
                             " distinguishable-error machinery pinned at its authority points,"
                             " the bridge exposes exactly the singleton invokable pair with a"
                             " single hook seam, the QML exception face is exactly two bridge"
                             " calls with the registered toast copy, and the fault hook is"
                             " mounted nowhere in production"
                          << (ok ? QString() : diag);
    });
}
