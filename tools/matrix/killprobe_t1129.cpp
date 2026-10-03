#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：统一保存协议（t1129 单事务原子化）
#include "worldstore.h"      // 被测面：四面写执行体 + 原子保存域原语

#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

// t1129 杀进程探针（SAVE-01 交接单第四步「杀进程实验」执行体；非矩阵腿——CLI 模式，矩阵主
//   运行零触碰）。模式族：
//   inject      新序真实面：Player 段注入失败 → 关库重开四面恰 A + 守恒账平（打印 verdict）。
//   inject-old  旧序重放面（复现实验）：旧协议提交序逐语句重放[marker 原语序等价面(台账 UPDATE)
//               → world 段事务提交 → 停在「player 未写」]→ 四面 = 世界/容器新 + 玩家/进度旧 =
//               混合代次（物品复制形态）。重放语句与 HEAD^ savecoordinator.cpp 步⑤⑥⑦ 的提交序
//               同源（refactor 把旧 saveAll/savePlayerData/saveProgress 的语句体**搬移**进
//               writeWorldPart/writePlayerPart/writeProgressPart，未改任何 SQL——git diff 佐证）。
//   parent      父流程：spawn 自身 child → 轮询标记文件 → QProcess::kill 真实终止（无清理无
//               回滚）→ 重开库四面读回 + 台账分类 + 守恒账 → 打印 verdict。
//   child       子流程（新序）：存 A → 存 B 于 Player 段钩内写标记后永久阻塞（kill 落在保存
//               事务开启、四面未提交窗）。
//   child-old   子流程（旧序重放）：存 A → 重放旧序至「world 已提交、player 未写」窗写标记后
//               永久阻塞（kill 落在旧协议崩溃窗）。
// 输出协议：stdout 逐行 "KILLPROBE ..."（报告机读）；exit 0 = 流程走完，2 = 装配失败。
namespace {

constexpr int kKW = 48, kKD = 48, kKH = 96, kKSeed = 82;

// 独立探针连接帮手（开-用-关；与 worldstore kConn 互不占用——r2031 coordKeys 同门）。
template <typename Fn>
auto withProbe(const QString &db, Fn &&fn) -> decltype(fn(std::declval<QSqlDatabase &>()))
{
    const QString conn = QStringLiteral("r2099kp_probe_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    p.setDatabaseName(db);
    decltype(fn(std::declval<QSqlDatabase &>())) out{};
    if (p.open())
        out = fn(p);
    QSqlDatabase::removeDatabase(conn);
    return out;
}

struct KFace
{
    int chestItems = -1;
    int invItems = -1;
    int progressStat = -1;
    int chunkRows = -1;
};

KFace readFaces(const QString &db)
{
    return withProbe(db, [](QSqlDatabase &p) {
        KFace f;
        QSqlQuery cq(p);
        if (cq.exec(QStringLiteral("SELECT data FROM chests"))) {
            f.chestItems = 0;
            while (cq.next()) {
                const QJsonDocument doc = QJsonDocument::fromJson(cq.value(0).toString().toUtf8());
                for (const QVariant &s : doc.toVariant().toList())
                    f.chestItems += s.toMap().value(QStringLiteral("count")).toInt();
            }
        }
        QSqlQuery pq(p);
        if (pq.exec(QStringLiteral("SELECT data FROM player_state WHERE id = 0")) && pq.next()) {
            const QJsonDocument doc = QJsonDocument::fromJson(pq.value(0).toString().toUtf8());
            f.invItems = 0;
            for (const QVariant &s : doc.toVariant().toMap()
                                        .value(QStringLiteral("inv")).toList())
                f.invItems += s.toMap().value(QStringLiteral("count")).toInt();
        }
        QSqlQuery gq(p);
        if (gq.exec(QStringLiteral("SELECT data FROM progress WHERE key = 'main'")) && gq.next())
            f.progressStat = QJsonDocument::fromJson(gq.value(0).toString().toUtf8())
                                 .toVariant().toMap()
                                 .value(QStringLiteral("stat")).toInt();
        QSqlQuery kk(p);
        if (kk.exec(QStringLiteral("SELECT COUNT(*) FROM chunks")) && kk.next())
            f.chunkRows = kk.value(0).toInt();
        return f;
    });
}

QVariantMap coordKeysOf(const QString &db)
{
    return withProbe(db, [](QSqlDatabase &p) {
        QVariantMap out;
        QSqlQuery q(p);
        if (q.exec(QStringLiteral("SELECT key, value FROM save_coord")))
            while (q.next())
                out.insert(q.value(0).toString(), q.value(1).toString());
        return out;
    });
}

void printFaces(const char *phase, const QString &db)
{
    const KFace f = readFaces(db);
    const QVariantMap keys = coordKeysOf(db);
    const int total = (f.chestItems < 0 ? 0 : f.chestItems) + (f.invItems < 0 ? 0 : f.invItems);
    fprintf(stdout, "KILLPROBE %s faces chest=%d inv=%d progress=%d chunkrows=%d total=%d\n",
            phase, f.chestItems, f.invItems, f.progressStat, f.chunkRows, total);
    fprintf(stdout, "KILLPROBE %s ledger generation=%s complete=%s\n", phase,
            keys.value(QStringLiteral("generation")).toString().toUtf8().constData(),
            keys.value(QStringLiteral("complete_generation")).toString().toUtf8().constData());
    fflush(stdout);
}

SaveRequest baseRequest() // A 载荷：箱空 + 背包 10 件 + 进度 1
{
    SaveRequest ra;
    ra.name = QStringLiteral("r2099killprobe");
    QVariantMap chest;
    chest.insert(QStringLiteral("x"), 4);
    chest.insert(QStringLiteral("y"), 5);
    chest.insert(QStringLiteral("z"), 6);
    chest.insert(QStringLiteral("slots"), QVariantList{});
    ra.chests = QVariantList{ chest };
    QVariantMap pd;
    pd.insert(QStringLiteral("px"), 11.5);
    QVariantList inv;
    inv.append(QVariantMap{ { QStringLiteral("id"), 3 }, { QStringLiteral("count"), 10 } });
    pd.insert(QStringLiteral("inv"), inv);
    ra.playerData = pd;
    QVariantMap pr;
    pr.insert(QStringLiteral("stat"), 1);
    ra.progress = pr;
    return ra;
}

SaveRequest movedRequest() // B 载荷：物品入箱（箱 10）+ 背包空 + 进度 2
{
    SaveRequest rb = baseRequest();
    QVariantMap chest;
    chest.insert(QStringLiteral("x"), 4);
    chest.insert(QStringLiteral("y"), 5);
    chest.insert(QStringLiteral("z"), 6);
    QVariantList slotRows;
    slotRows.append(QVariantMap{ { QStringLiteral("id"), 3 }, { QStringLiteral("count"), 10 } });
    chest.insert(QStringLiteral("slots"), slotRows);
    rb.chests = QVariantList{ chest };
    rb.playerData = QVariantMap{ { QStringLiteral("px"), 11.5 },
        { QStringLiteral("inv"), QVariantList{} } };
    QVariantMap pr;
    pr.insert(QStringLiteral("stat"), 2);
    rb.progress = pr;
    return rb;
}

bool prepareBase(const QString &db) // 存 A（完整 Clean 1/1）——两序共用基线
{
    QFile::remove(db);
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kKSeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return false;
    store.setWorld(&w);
    SaveCoordinator coord;
    coord.bind(&store, db);
    const SaveReceipt r = coord.saveAll(baseRequest());
    store.closeWorld();
    return r.ok();
}

// 永久阻塞（父 kill 为唯一出口——硬终止无 atexit 无清理；返回值形存编译面）。
void blockForever(const QString &marker)
{
    QFile f(marker);
    f.open(QIODevice::WriteOnly);
    f.write("paused\n");
    f.close();
    fflush(stdout);
    for (;;)
        QThread::msleep(100);
}

// 旧协议提交序重放（至「world 已提交、player 未写」窗）：语句与 HEAD^ savecoordinator.cpp
//   步⑤⑥⑦ 同源（saveAll[begin/写表/commit] + savePlayerData 未达）。marker 以台账 UPDATE
//   等价面落（coordUpsert 私有——SQL 语义同 INSERT OR REPLACE）。返回 false = 装配失败。
bool replayOldOrderToWindow(WorldStore &store, SaveCoordinator &coord, const QString &db)
{
    const SaveGenerationInfo prior = coord.recover();
    const qint64 newGen = qMax(prior.generation, prior.completeGeneration) + 1;
    // marker 等价面（台账 UPDATE 到新尝试代次——旧 coordUpsert 同语义）。
    const bool marked = withProbe(db, [&](QSqlDatabase &p) {
        QSqlQuery q(p);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO save_coord (key, value) VALUES ('generation', ?)"));
        q.addBindValue(QString::number(newGen));
        return q.exec();
    });
    if (!marked)
        return false;
    // 步⑤ world 段单事务提交（旧 saveAll 的表写体原语序）。
    if (!store.beginAtomicSave())
        return false;
    const SaveRequest rb = movedRequest();
    if (!store.writeWorldPart(rb.name, rb.chests, rb.furnaces, rb.dispensers, rb.worldTime,
                              rb.bedSpawn, rb.hoppers, rb.brewingStands, rb.signs)) {
        store.rollbackAtomicSave();
        return false;
    }
    if (!store.commitAtomicSave(1))
        return false;
    return true; // 窗到：世界/容器已新、玩家/进度仍旧（旧 savePlayerData 未执行）
}

int runInject(const QString &db)
{
    if (!prepareBase(db))
        return 2;
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kKSeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return 2;
    store.setWorld(&w);
    SaveCoordinator coord;
    coord.bind(&store, db);
    coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
    const SaveReceipt r = coord.saveAll(movedRequest());
    store.closeWorld();
    fprintf(stdout, "KILLPROBE inject receipt ok=%d code=%d gen=%lld\n", r.ok() ? 1 : 0,
            r.error.code, static_cast<long long>(r.generation));
    printFaces("inject", db);
    return 0;
}

int runInjectOld(const QString &db)
{
    if (!prepareBase(db))
        return 2;
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kKSeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return 2;
    store.setWorld(&w);
    SaveCoordinator coord;
    coord.bind(&store, db);
    if (!replayOldOrderToWindow(store, coord, db))
        return 2;
    store.closeWorld();
    fprintf(stdout, "KILLPROBE inject-old replayed pre-fix commit order (marker + world-commit,"
                    " stopped before player)\n");
    printFaces("inject-old", db);
    return 0;
}

int runChild(const QString &db, const QString &marker, bool oldOrder)
{
    if (!prepareBase(db))
        return 2;
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kKSeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return 2;
    store.setWorld(&w);
    SaveCoordinator coord;
    coord.bind(&store, db);
    if (oldOrder) {
        if (!replayOldOrderToWindow(store, coord, db))
            return 2;
        blockForever(marker); // kill 落在旧协议崩溃窗（世界已新、玩家未写）
        return 0;
    }
    // 新序：kill 落在保存事务开启、四面未提交窗。
    coord.setFaultHook([&marker](SaveFaultStage st) {
        if (st != SaveFaultStage::Player)
            return false;
        blockForever(marker);
        return false; // 不可达面（阻塞内被杀）——编译形存
    });
    coord.saveAll(movedRequest());
    return 0;
}

int runParent(const QString &db, const QString &marker, bool oldOrder)
{
    QFile::remove(db);
    QFile::remove(marker);
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({ QStringLiteral("--killprobe"),
        oldOrder ? QStringLiteral("child-old") : QStringLiteral("child"),
        QDir(db).absolutePath(), QDir(marker).absolutePath() });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    child.setProcessEnvironment(env);
    child.start();
    if (!child.waitForStarted(10000)) {
        fprintf(stdout, "KILLPROBE parent child-start FAILED\n");
        fflush(stdout);
        return 2;
    }
    const QDeadlineTimer deadline(30000);
    bool hit = false;
    while (!deadline.hasExpired()) {
        if (QFileInfo::exists(marker)) {
            hit = true;
            break;
        }
        QThread::msleep(20);
    }
    if (!hit) {
        child.kill();
        child.waitForFinished(10000);
        fprintf(stdout, "KILLPROBE parent marker TIMEOUT - child killed unanchored\n");
        fflush(stdout);
        return 2;
    }
    child.kill(); // 真实终止（Windows TerminateProcess——无 atexit 无清理无回滚）
    const bool finished = child.waitForFinished(10000);
    fprintf(stdout, "KILLPROBE parent killed child at %s window (finished=%d exit=%d)\n",
            oldOrder ? "pre-fix world-committed" : "in-transaction", finished ? 1 : 0,
            child.exitCode());
    fflush(stdout);
    printFaces("parent", db);
    QFile::remove(db);
    QFile::remove(marker);
    return 0;
}
} // namespace

int runKillProbe(int argc, char *argv[])
{
    // 调用形：--killprobe <mode> [db] [marker]；无参/未知模式 = 用法简报（防误跑）。
    const QString mode = argc > 2 ? QString::fromUtf8(argv[2]) : QString();
    const QString db = argc > 3 ? QString::fromUtf8(argv[3]) : QString();
    const QString marker = argc > 4 ? QString::fromUtf8(argv[4]) : QString();
    if (mode == QStringLiteral("inject") && !db.isEmpty())
        return runInject(db);
    if (mode == QStringLiteral("inject-old") && !db.isEmpty())
        return runInjectOld(db);
    if (mode == QStringLiteral("child") && !db.isEmpty() && !marker.isEmpty())
        return runChild(db, marker, false);
    if (mode == QStringLiteral("child-old") && !db.isEmpty() && !marker.isEmpty())
        return runChild(db, marker, true);
    if (mode == QStringLiteral("parent") && !db.isEmpty() && !marker.isEmpty())
        return runParent(db, marker, false);
    if (mode == QStringLiteral("parent-old") && !db.isEmpty() && !marker.isEmpty())
        return runParent(db, marker, true);
    fprintf(stdout, "KILLPROBE usage: --killprobe inject|inject-old|child|child-old|parent|"
                    "parent-old <db> [marker]\n");
    fflush(stdout);
    return 2;
}
