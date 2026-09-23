#include "matrix_helpers.h"

#include "savebridge.h"      // 被测面：t1064 探锁退避直调面（探针保持 0 的复钉面）
#include "savecoordinator.h" // 台账读数（Clean/代次不谎报面——历史原样柱）
#include "worldstore.h"      // 计数观测面（saveOkCount 成功 +3 / 失败零动口径）

#include <QElapsedTimer>
#include <atomic>
#include <cstdio> // t1082 探针：退避打卡消息钩子默认处理器兜底直写 stderr
#include <thread>

// t1066 保存链 BUSY_TIMEOUT=0 探针段（4 腿 r2040a-d；filter 词 "r2040"；矩阵 691→691+N）。
// 置尾先例沿用（接 section40，runAll 末执行）；真临时库（pid 键名，测试自清理）+ 真桥单例 +
// 真 QQmlEngine；真实用户 saves/ 零触碰。任务契约（dev-plan t1066；t1064 真锁腿实测发现）：
//   Qt QSQLITE 驱动默认 busy timeout 秒级——外部锁全程持有时保存链**首试**在第一条撞锁语句
//   （台账 SELECT）上阻塞秒级才失败（t1064 真锁腿 wall≈29s 实锤）；t1064 的 ≤300ms 退避只约束
//   重试间隔、不约束首试阻塞。修法（worldstore/savecoordinator/chunkstore 三文件全部连接设置
//   点显式归零 busy 等待——连接盘点表/自锁竞态面论证见 worldstore.cpp 文件头）：
//   「无锁零变化墙」→ r2040a（真链首试即真[恰 1 调用零重试] + 墙钟 ≤300ms + +3 口径 + 台账
//     Clean 1/1 + 探锁直调面/守卫面恒 0 + 完成门/toast/lastExitSaveOk 语义面原样——busy 归零
//     对无锁路径零观感的承重柱）；
//   「首试即时性承重」→ r2040b（**本单核心增量**：真 BEGIN EXCLUSIVE 全程持有 + 真保存链零
//     故障注入直调——首试必须在 <1s 内诚实失败[旧形态秒级；t1064 段头登记的前置既有面就此
//     清偿]；wrapper 整链[首试→探锁→150ms 一档→恰一次重试]仍诚实 false + 全链墙钟 <2s 有界 +
//     计数零动 + 台账历史原样 Clean 1/1；释放后重存收敛 +3 Clean 2/2——诚实失败面不弱化的
//     r2038c 同变更核）；
//   「锁中途释放收敛」→ r2040c（真锁事件驱动窗内释放[t1082 加固——旧 60ms 绝对窗已退役] +
//     真保存链零故障注入：首试瞬时败 → 探锁败 → 150ms 档 → 重试收敛真 + 恰一次重试 + 退避
//     打卡恰一次[t1082 逻辑轮次面] + +3 仅重试计数 + 代次单调 Clean 2/2 + 全链墙钟上界——
//     r2038b 收敛语义的同变更核，且自此**无需 BeginMark 故障缝**：r2038b 首跑登记的「免保存
//     链默认 busy 等待」前置面已由本单消除，真链自身即瞬时失败）；
//   t1082 加固定案（r2040c 三连红[t1081 全矩阵 ×3 wall=82/85/87 + HEAD 同刻复现 wall=82]：
//     满矩阵负载下探针被调度晚于 60ms 锁窗 → 探锁见锁已放 → **退避整段跳过** → 立即重试
//     收敛 → 唯旧 wall≥100 下限窗红。被测语义本体 = t1066 退避机制发生且有序，墙钟毫秒是
//     负载导出量）：①wall 绝对窗下限 → **逻辑轮次计数面**——探针退避 qWarning 落账恰 1 次
//     （qInstallMessageHandler 捕获，t1010 无捕获 lambda + static 着陆门）+ 恰一次重试计数
//     面[saveCalls==2]原样承重；②锁窗绝对毫秒 → **事件驱动持锁**——持锁者不见退避打卡绝不
//     放手（探针必然见锁在持 = 与调度延迟解耦），打卡先于 150ms 档睡眠发出 → 持锁者得全额
//     150ms 余量释放 → 重试必然落释放后。负载鲁棒性论证：旧形态是双侧竞速（首试不得早于
//     锁立、探锁不得晚于锁放——60ms 双向窗），新形态是单向余量（打卡→释放须在 150ms 档内
//     完成，持锁者 2ms 轮询 + ROLLBACK 微秒级），墙钟只剩上界 2000ms（旧形态秒级不可达面
//     原样保留）。落选面留痕：a) 削下限换绿 = 违 t1071/t1079 铁律；b) 固定毫秒持有时长钉 =
//     制造时序非去除时序仍双侧竞速；c) 改生产 wrapper 捕获探针返回值 = 动生产破 r2040d 零
//     触碰复钉面；d) 仅保留 saveCalls==2 = S1（退避发生）/S2（退避跳过）不可分辨 = 断言
//     空心化）；
//   「结构钉」→ r2040d（全部连接设置点 busy 归零的**逐连接放置钉**[每 addDatabase 后 2 行内
//     必跟归零设置——防「只配一半」漂移 + 每文件设置点计数 4/2/1/1 = 新增连接漏配即响亮红]
//     + 剥注释 minCount 复钉 + r2038 零触碰复钉[探锁 SQL/msleep/单点 wrapper/两调用点/完成门
//     /toast/lastExitSaveOk] + 探锁连接保持 0 钉）。
// 与 r2038 段的分工（不重复计时）：r2038b/c 的「首试失败」由 BeginMark 故障缝制造（当时真锁
//   下真链会 busy 等待秒级——该段头登记的前置既有面）；本段 r2040b/c 反其道用**零注入真链**
//   直面真锁——首试即时性正是本单的增量主张，故障缝反而会遮蔽它。r2038 腿自身零触碰（其时序
//   断言全为上界/退避窗界，busy 归零后仍成立——同变更核留痕即本段注释）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘台账读面设置（savecoordinator.cpp recover() 归零行注释掉）→ 声明红面 =
//     {r2040b, r2040c, r2040d}（b：首试回退秒级 busy 等待 → <1s 墙钟红 + 全链 <2s 红；c：首试
//     秒级阻塞跨过事件驱动锁窗[持锁者 5s cap 兜底放行] → 重试仍败/退避面破 → 「恰一次重试+
//     退避打卡恰一次」计数形态红；d：台账连接放置钉红）；r2040a 不误伤（无锁路径零观感）。
//   NEG-2 摘主连接设置（worldstore.cpp openWorld 归零行注释掉）→ 声明红面 = {r2040d}（行为腿
//     不误伤：首试死于台账读面、重试锁已放，主连接配置面不可达 = 行为腿无观感；放置钉红恰证
//     「禁漏一处」的结构承重——盘点钉抓行为腿探不到的漂移）。
//   NEG-3（t1082 恰红存证）退避打卡计数面断线（腿内 sink 诊断词临时变异）→ 声明红面 =
//     {r2040c}（打卡恒 0 → 持锁者不见打卡持锁至 stop 汇合 → 重试败 → conv/call 计数/退避
//     打卡恰一次/仅重试 +3/代次 2/2 五面齐红——恰证计数面非空心：观测丢失即响亮红，且腿有界
//     不挂死）。
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section41_save_busy_zero()
{
    // ── 段内共享帮手（腿间无共享 rig 状态——各腿自建 fresh 世界 + 临时库；section39 同门）───
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
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2040_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // Main.qml 共用 wrapper 原文抽取（brace 配平——section39 同门）：生产 wrapper 缺席/改名/
    //   体破损即 false（响亮红）。
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
    // 真链引擎装配（真 QQmlEngine + 上下文注入 + 生产 wrapper 原文体；section39 同门）。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeRig = [&extractBackoffWrapper](Rig &rig, WorldStore *store) {
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 真链先例同款：防磁盘缓存重定向
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
        // 生产体自由名落位："SaveBridge" 同名注入 = C++/QML 共用真桥单例；"worldStore" =
        //   真被测库；currentWorldFile = wrapper 属性。
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
            "            \"r2040\", [], [], [], {}, {}, {px: 41.5}, {statPlayedMinutes: saveCalls})\n"
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
        return SaveBridge::instance()->saveViaCoordinator(&store, file, QStringLiteral("r2040"),
                                                          QVariantList(), QVariantList(),
                                                          QVariantList(), QVariantMap(),
                                                          QVariantMap(), player, progress);
    };
    // 真锁注入（section39 同门：持锁者 = 自管线程连接——创建/使用/销毁全程单线程，Qt Sql 纪律
    //   合规）。持锁窗 = deadline 制 + stop 汇合；err 仅 join 后读（join = 同步点，无竞态）。
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
    // 事件驱动持锁（t1082 加固，r2040c 专用）：持锁窗不用绝对毫秒 deadline——改由「探针退避
    //   已打卡」观测驱动（fired 落账才放手）。确定性论证：持锁者不见打卡绝不释放 → 探针必然
    //   见锁在持（与负载/调度延迟解耦——旧 60ms 绝对窗在满矩阵负载下被探针晚到越过，退避整段
    //   跳过[t1081 三跑 wall=82-87 实锤]）；退避打卡先于 150ms 档睡眠发出（savebridge.cpp：
    //   qWarning 在 msleep 之前）→ 持锁者得全额 150ms 余量（2ms 轮询 + ROLLBACK 微秒级）→
    //   重试必然落释放后。cap 仅防挂死兜底（打卡面破时腿红而非挂死——wrapper 各语句 busy=0
    //   即败即返，stopAndJoin 提前汇合，不会等满 cap）。
    const auto holdExclusiveUntil = [](LockThread &h, const QString &db, const QString &conn,
                                       const std::atomic_int &fired, int capMs) {
        h.thr = std::thread([&h, db, conn, &fired, capMs]() {
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
                    while (!h.stop && fired.load() == 0 && hold.elapsed() < capMs)
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
    // r2040c 逻辑轮次面（t1082）：「退避发生了」的确定性观测 = 探针退避 qWarning 落账恰 1 次，
    //   替代旧 wall≥100ms 下限窗（负载导出量——满矩阵下探针晚到跳过退避时 wall 反而更小，下限
    //   窗误伤真语义[t1081 实锤]）。QtMessageHandler 是裸函数指针 → 无捕获 lambda + 函数级
    //   static 着陆点（t1010 同门）；非本诊断词链式转发不吞（前任处理器 / 默认 stderr 兜底）。
    //   落选面留痕：a) 削 wall 下限 = 断言空心化；b) 固定毫秒锁窗 = 制造时序非去除时序；
    //   c) 动生产 wrapper 取返回值 = 破 r2040d 零触碰复钉面。
    static std::atomic_int s_backoffFired{0};
    static QtMessageHandler s_prevBackoffHandler = nullptr;
    QtMessageHandler backoffFiredSink =
        [](QtMsgType type, const QMessageLogContext &ctx, const QString &m) {
            if (type == QtWarningMsg && m.contains(QLatin1String("exitSaveRetryBackoff")))
                s_backoffFired.fetch_add(1, std::memory_order_relaxed);
            if (s_prevBackoffHandler)
                s_prevBackoffHandler(type, ctx, m);
            else
                std::fprintf(stderr, "%s\n", qUtf8Printable(m)); // 默认处理器兜底直写 stderr
        };

    SaveBridge &bridge = *SaveBridge::instance();
    bridge.setFaultHook(SaveFaultHook()); // 桥间复位缝（singleton 跨腿共享——入口归零 = 生产形态；
                                          //   本段四腿全程零注入——真链直面真锁）

    // ── r2040a：无锁零变化墙（首试即真零重试 + 零退避 + +3 口径 + 语义面原样）────────────────
    runLeg(QStringLiteral("r2040a lock-free zero-change wall (with nothing holding the library"
        " the busy-timeout pinning is invisible end to end: the real-engine first attempt"
        " answers true with the retry-observable call counter at exactly one and the measured"
        " call far under the backoff floor, the write counter moves by exactly +3 per save with"
        " the ledger Clean at 1/1 and the string face agreeing, the bridge probe face answers"
        " zero on a free library and on every degenerate guard face (no store, empty file name,"
        " missing library), the idempotent replay stays true with the counter resuming at +3,"
        " and the completion-gate / toast / lastExitSaveOk semantics stay in place verbatim in"
        " both exit paths)"), [&]() {
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

        // 无锁常态：首试即真——零重试零退避（权威证 = 调用计数恰 1 + 墙钟有界；+3 口径原样）。
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

        // 桥直调面：锁已释放/无锁 = 恒 0；守卫面（无 store/空名/缺库）恒 0（t1064 零退避承诺）。
        const int backFree = bridge.exitSaveRetryBackoff(&store, db);
        const int backNoStore = bridge.exitSaveRetryBackoff(nullptr, db);
        const int backNoName = bridge.exitSaveRetryBackoff(&store, QString());
        const QString missing = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_r2040_missing_%1.sqlite").arg(QCoreApplication::applicationPid()));
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
                          << "| r2040a lock-free zero-change wall: with nothing holding the"
                             " library the busy-timeout pinning is invisible end to end (first"
                             " attempt answers true with exactly one call far under the backoff"
                             " floor, the counter moves by exactly +3 with the ledger Clean at"
                             " 1/1, the probe face answers zero on a free library and on every"
                             " guard face, the replay stays true, and the completion-gate /"
                             " toast / lastExitSaveOk semantics stay verbatim in both paths)"
                          << (ok ? QString() : diag);
    });

    // ── r2040b：首试即时性承重（真锁全程持有 + 真链零注入：首试 <1s 诚实败 + 整链 <2s 有界）──
    runLeg(QStringLiteral("r2040b first-attempt immediacy load-bearing wall under a real"
        " exclusive lock held throughout (with the real save chain and zero fault injection a"
        " direct first attempt fails honestly in well under a second where the legacy driver"
        " default stalled for seconds, the full retry chain - first attempt, probe, one 150ms"
        " backoff step, exactly one retry - answers an honest false with the call counter on"
        " two and never three, the whole chain bounded far below two seconds, the write counter"
        " untouched beyond the baseline, and the ledger history intact at a clean 1/1; once the"
        " lock is released a fresh call converges true with the counter resuming at +3 and the"
        " ledger advancing to a clean 2/2)"), [&]() {
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

        // 基线 Clean 1/1（C++ 直调，+3——「历史不失真」柱的锚）。
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

        // (i) 首试即时性（本单核心增量）：真锁全程持有 + 真链零注入直调——首试 <1s 诚实失败
        //     （旧形态：驱动默认秒级 busy 等待，台账 SELECT 一条即 ≈5s、全链 wall≈29s）。
        LockThread lockH;
        holdExclusive(lockH, db, QStringLiteral("r2040_locker_b"), 60000);
        const bool locked = waitLocked(lockH.locked);
        qint64 wallFirst = -1;
        bool firstSaved = true;
        if (locked) {
            QElapsedTimer w;
            w.start();
            firstSaved = bridgeSave(store, db, 21.0); // 零注入真链首试（失败则计数零动）
            wallFirst = w.elapsed();
        }
        const bool firstOk = locked && !firstSaved && wallFirst >= 0 && wallFirst < 1000;
        ok = ok && firstOk;
        if (!firstOk)
            diag += QStringLiteral("[first locked=%1 r=%2 wall=%3] ")
                        .arg(locked).arg(firstSaved).arg(wallFirst);

        // (ii) wrapper 整链（首试→探锁→150ms 一档→恰一次重试）：仍诚实 false + 全链有界 <2s
        //      + 计数零动 + 台账历史原样（OpenError 拒存 = 代次零消耗，marker-first 语义）。
        bool conv = true;
        qint64 wallChain = -1;
        if (locked) {
            QElapsedTimer w;
            w.start();
            conv = runBackoffSave(rig);
            wallChain = w.elapsed();
        }
        stopAndJoin(lockH);
        const int c1 = store.saveOkCount();
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const QString stLocked = bridge.recoveryState(db);
        const bool chainOk = locked && !conv && saveCallsOf(rig) == 2 // 恰一次重试（不多不少）
            && wallChain >= 0 && wallChain < 2000 // 整链有界（退避一档 + 两试，远低于秒级）
            && c1 == c0 + 3 // 两试零计数（诚实失败，t974 契约原样）
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 // 台账历史原样（拒存窗口零写入）
            && stLocked == QStringLiteral("clean") && lockH.err.isEmpty();
        ok = ok && chainOk;
        if (!chainOk)
            diag += QStringLiteral("[chain locked=%1 r=%2 calls=%3 wall=%4 c=%5/%6 st=%7 g=%8/%9 err=%10]")
                        .arg(locked).arg(conv).arg(saveCallsOf(rig)).arg(wallChain)
                        .arg(c1).arg(c0 + 3).arg(stLocked).arg(f1.generation)
                        .arg(f1.completeGeneration).arg(lockH.err);

        // (iii) 释放后重存收敛：失败如实上报后照常可恢复（+3，代次单调推进 2/2 Clean）——
        //       诚实失败面不弱化的同变更核（r2038c 语义延续）。
        const bool after = runBackoffSave(rig);
        const int c2 = store.saveOkCount();
        const SaveGenerationInfo f2 = bridge.recoveryInfo(db);
        const bool afterOk = after && saveCallsOf(rig) == 3 && c2 == c1 + 3
            && f2.state == SaveRecoveryState::Clean && f2.generation == 2
            && f2.completeGeneration == 2;
        ok = ok && afterOk;
        if (!afterOk)
            diag += QStringLiteral("[after r=%1 calls=%2 c=%3/%4 st=%5 g=%6/%7] ")
                        .arg(after).arg(saveCallsOf(rig)).arg(c2).arg(c1 + 3)
                        .arg(int(f2.state)).arg(f2.generation).arg(f2.completeGeneration);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2040b first-attempt immediacy load-bearing wall under a real"
                             " exclusive lock held throughout: the real chain with zero fault"
                             " injection fails honestly in well under a second where the legacy"
                             " default stalled for seconds, the full retry chain answers an"
                             " honest false with exactly one retry bounded far below two"
                             " seconds, counter and ledger history untouched, and a fresh call"
                             " after release converges true resuming at +3 with a clean 2/2"
                          << (ok ? QString() : diag);
    });

    // ── r2040c：锁中途释放收敛（t1082 加固：事件驱动锁窗 + 退避打卡恰一次计数面——首试瞬时败
    //    → 探锁败 → 150ms 档 → 重试收敛真，零毫秒下限窗）──
    runLeg(QStringLiteral("r2040c mid-lock release convergence under a real exclusive lock"
        " released only after the probe's backoff warning is seen (with the real save chain"
        " and zero fault injection the first attempt fails instantly, the probe fails while"
        " the lock is still held, the backoff warning fires exactly once and the event-driven"
        " holder releases inside the single 150ms step, so the one retry converges true with"
        " exactly two calls, the wall clock bounded under two seconds, the counter moving by"
        " exactly +3 for the retry only, and the ledger advancing monotonically to a clean"
        " 2/2 - the convergence face now carried by the real chain itself with no fault seam"
        " needed)"), [&]() {
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

        // 基线 Clean 1/1（+3；代次单调与「历史不失真」柱的锚）。
        const int c0 = store.saveOkCount();
        const bool base = bridgeSave(store, db, 12.5);
        const SaveGenerationInfo f0 = bridge.recoveryInfo(db);
        const bool baseOk = base && f0.state == SaveRecoveryState::Clean
            && f0.generation == 1 && f0.completeGeneration == 1
            && store.saveOkCount() == c0 + 3;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base s=%1 st=%2 g=%3/%4 c=%5/%6] ")
                        .arg(base).arg(int(f0.state)).arg(f0.generation)
                        .arg(f0.completeGeneration).arg(store.saveOkCount()).arg(c0 + 3);

        // 真锁事件驱动窗内释放 + 真链零注入 wrapper：首试瞬时败（真链自身，无故障缝）→ 探锁败
        //   （锁仍在持——持锁者不见退避打卡绝不放手）→ 打卡落账恰一次 → 150ms 档内释放 → 重试
        //   收敛真 + 恰一次重试 + 全链墙钟上界（下限面已由打卡计数承重，毫秒只剩上界）。
        LockThread lockB;
        s_backoffFired.store(0); // 打卡面清零（static 跨腿存活——本腿入口归零）
        holdExclusiveUntil(lockB, db, QStringLiteral("r2040_locker_c"), s_backoffFired, 5000);
        const bool locked = waitLocked(lockB.locked);
        bool conv = false;
        qint64 wallC = -1;
        if (locked) {
            QElapsedTimer w;
            w.start();
            s_prevBackoffHandler = qInstallMessageHandler(backoffFiredSink); // 捕获窗恰罩 wrapper
            conv = runBackoffSave(rig);
            qInstallMessageHandler(s_prevBackoffHandler); // 捕获窗收口（用后即还原——r2029d 同门）
            wallC = w.elapsed();
        }
        stopAndJoin(lockB);
        const int backoffFired = s_backoffFired.load();
        const int c1 = store.saveOkCount();
        const SaveGenerationInfo f1 = bridge.recoveryInfo(db);
        const bool convOk = locked && conv && saveCallsOf(rig) == 2 // 恰一次重试（不多不少）
            && backoffFired == 1 // 退避恰一次（t1082 逻辑轮次面：探锁见锁在持才走退避路径）
            && wallC < 2000 // 全链墙钟上界（旧形态秒级不可达面保留；下限已由计数面承重）
            && c1 == c0 + 6 // 仅重试计数（首试 marker-first/OpenError 拒存零动）
            && f1.state == SaveRecoveryState::Clean && f1.generation == 2
            && f1.completeGeneration == 2 && lockB.err.isEmpty();
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[conv locked=%1 r=%2 calls=%3 b=%4 wall=%5 c=%6/%7 st=%8"
                                   " g=%9/%10 err=%11]")
                        .arg(locked).arg(conv).arg(saveCallsOf(rig)).arg(backoffFired)
                        .arg(wallC)
                        .arg(c1).arg(c0 + 6).arg(int(f1.state)).arg(f1.generation)
                        .arg(f1.completeGeneration).arg(lockB.err);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2040c mid-lock release convergence under a real exclusive lock"
                             " released only after the probe's backoff warning is seen: with"
                             " the real chain and zero fault injection the first attempt fails"
                             " instantly, the backoff warning fires exactly once, the"
                             " event-driven holder releases inside the single 150ms step, and"
                             " the single retry converges true with exactly two calls bounded"
                             " under two seconds, +3 for the retry only, and a monotonic clean"
                             " 2/2 ledger - carried by the real chain itself with no fault seam"
                             " needed"
                          << (ok ? QString() : diag);
    });

    // ── r2040d：结构钉（逐连接放置钉 + 每文件设置点计数 + 剥注释复钉 + r2038 零触碰复钉）──────
    runLeg(QStringLiteral("r2040d structure pins (every SQLite connection setup point in the"
        " save chain carries the zero busy-timeout connect option within two lines of its"
        " addDatabase call - four sites in the world store, two in the save coordinator, one in"
        " the chunk store, and the t1064 probe connection keeping its existing zero - with the"
        " per-file site counts pinned so a future unconfigured connection fails loudly, the"
        " comment-stripped min-count pins agreeing, and the r2038 probe faces (lock SQL,"
        " release, single backoff sleep site) plus the shared retry wrapper single-point and"
        " both exit-path semantics staying verbatim)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absolutePath();

        // ① 逐连接放置钉：每文件逐行扫 addDatabase——其后 2 行内必跟 busy 归零设置（防「只配
        //    一半」漂移）；每文件设置点总数钉死（4/2/1/1）——新增连接漏配 = 响亮红（禁漏一处）。
        const auto placementScan = [](const QString &path, int expectedSites, QString &why)
            -> bool {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                why = QStringLiteral("unreadable %1").arg(path);
                return false;
            }
            const QStringList lines = QString::fromUtf8(f.readAll())
                                          .split(QLatin1Char('\n'));
            int sites = 0;
            for (int i = 0; i < lines.size(); ++i) {
                if (!lines.at(i).contains(QStringLiteral("QSqlDatabase::addDatabase")))
                    continue;
                ++sites;
                bool pinned = false;
                for (int j = i + 1; j <= qMin(i + 2, lines.size() - 1); ++j) {
                    if (lines.at(j).contains(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0"))) {
                        pinned = true;
                        break;
                    }
                }
                if (!pinned) {
                    why = QStringLiteral("site at %1:%2 not pinned within 2 lines")
                              .arg(path).arg(i + 1);
                    return false;
                }
            }
            if (sites != expectedSites) {
                why = QStringLiteral("site count %1 != %2 in %3").arg(sites)
                          .arg(expectedSites).arg(path);
                return false;
            }
            return true;
        };
        QString why;
        const bool wsOk = placementScan(srcRoot + QStringLiteral("/src/World/worldstore.cpp"),
                                        4, why);
        ok = ok && wsOk;
        if (!wsOk) diag += QStringLiteral("[ws %1] ").arg(why);
        const bool scpOk = placementScan(
            srcRoot + QStringLiteral("/src/World/savecoordinator.cpp"), 2, why);
        ok = ok && scpOk;
        if (!scpOk) diag += QStringLiteral("[scp %1] ").arg(why);
        const bool csOk = placementScan(srcRoot + QStringLiteral("/src/World/chunkstore.cpp"),
                                        1, why);
        ok = ok && csOk;
        if (!csOk) diag += QStringLiteral("[cs %1] ").arg(why);
        // 探锁连接保持 0 钉（t1064 面零回归——连接设置点数与归零放置同扫）。
        const bool sbOk = placementScan(srcRoot + QStringLiteral("/src/Game/savebridge.cpp"),
                                        1, why);
        ok = ok && sbOk;
        if (!sbOk) diag += QStringLiteral("[sb %1] ").arg(why);

        // ② 剥注释 minCount 复钉（pinSet 字符串感知——注释文本不计数，与放置钉互为双证）。
        const QStringList missPins = pinSet(srcRoot + QStringLiteral("/src/World/worldstore.cpp"),
            { SrcPin("worldstore busy zero x4", "QSQLITE_BUSY_TIMEOUT=0", 4) });
        for (const QString &m : missPins) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missScp = pinSet(
            srcRoot + QStringLiteral("/src/World/savecoordinator.cpp"),
            { SrcPin("coordinator busy zero x2", "QSQLITE_BUSY_TIMEOUT=0", 2) });
        for (const QString &m : missScp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missCs = pinSet(
            srcRoot + QStringLiteral("/src/World/chunkstore.cpp"),
            { SrcPin("chunkstore busy zero x1", "QSQLITE_BUSY_TIMEOUT=0", 1) });
        for (const QString &m : missCs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ r2038 零触碰复钉（探针三件套 + 共用 wrapper 单点 + 两调用点 + 完成门/toast/两路径写）。
        const QStringList missSbc = pinSet(srcRoot + QStringLiteral("/src/Game/savebridge.cpp"), {
            SrcPin("probe takes the write lock", "BEGIN EXCLUSIVE", 1),
            SrcPin("probe releases immediately", "ROLLBACK", 1),
            SrcPin("single backoff sleep site", "QThread::msleep(kExitSaveBackoffMs)", 1),
            SrcPin("probe busy timeout pinned to zero", "QSQLITE_BUSY_TIMEOUT=0", 1),
        });
        for (const QString &m : missSbc) {
            ok = false;
            diag += QStringLiteral("[sbc-%1] ").arg(m);
        }
        {
            QFile mf(srcRoot + QStringLiteral("/src/ui/Main.qml"));
            const QString qmlSrc = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll())
                                                                : QString();
            const int nDef = int(qmlSrc.count(QStringLiteral("function runExitSaveWithBackoff()")));
            const int nProbe = int(qmlSrc.count(QStringLiteral(
                "SaveBridge.exitSaveRetryBackoff(worldStore, currentWorldFile)")));
            const int nButton = int(qmlSrc.count(QStringLiteral(
                "let exitSaveOk = runExitSaveWithBackoff()")));
            const int nClose = int(qmlSrc.count(QStringLiteral(
                "let okClose = runExitSaveWithBackoff()")));
            const int iGate = qmlSrc.indexOf(QStringLiteral("let exitSaveOk = runExitSaveWithBackoff()"));
            const int iToast = qmlSrc.indexOf(QStringLiteral("存档写入失败，本次进度未保存"));
            const int iCover = qmlSrc.indexOf(QStringLiteral("coverGrabPending = true"));
            const bool facesOk = nDef == 1 && nProbe == 1 && nButton == 1 && nClose == 1
                && iGate >= 0 && iToast >= 0 && iCover >= 0 && iGate < iToast && iToast < iCover
                && qmlSrc.contains(QStringLiteral("window.lastExitSaveOk = exitSaveOk"))
                && qmlSrc.contains(QStringLiteral("window.lastExitSaveOk = okClose"));
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[qml def=%1 probe=%2 btn=%3 close=%4 gate=%5 toast=%6 cover=%7] ")
                            .arg(nDef).arg(nProbe).arg(nButton).arg(nClose)
                            .arg(iGate).arg(iToast).arg(iCover);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2040d structure pins: every SQLite connection setup point in"
                             " the save chain carries the zero busy-timeout option within two"
                             " lines of its addDatabase call (world store 4, coordinator 2,"
                             " chunk store 1, probe keeping its existing zero), per-file site"
                             " counts pinned so a future unconfigured connection fails loudly,"
                             " comment-stripped min-count pins agreeing, and the r2038 probe"
                             " faces plus the shared retry wrapper single point and both"
                             " exit-path semantics staying verbatim"
                          << (ok ? QString() : diag);
    });
}
