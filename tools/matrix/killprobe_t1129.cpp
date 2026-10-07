#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：统一保存协议（t1129 单事务原子化）
#include "worldstore.h"      // 被测面：四面写执行体 + 原子保存域原语
#include "streamingbridge.h" // t1142：生产进入链真实入口（实体三缝生产装配点——审查 E1 原文）
#include "entitymanager.h"   // t1142：生物管理器（属性注入读口 + 恢复面）
#include "itementitymanager.h" // t1142：掉落物管理器（同上）
#include "worldclock.h"      // t1142：泵拍驱动信号源（probe 直发 ticked——逻辑时间面）
#include "playercontroller.h" // t1142：位置沿信号源（probe 直发 playerChunkChanged）
#include "savebridge.h"      // t1142：完整保存 B 的生产链入口（载荷合并 provider 消费面）

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
    // t1135 全量零警告门顺手清（lessons 工具链节：nodiscard fallible 调用检查 + 可见诊断降级，
    //   非 (void) 糊弄——marker 打不开时阻塞面退化为忙等自旋，父 kill 出口语义不变）。
    if (!f.open(QIODevice::WriteOnly))
        qWarning("killprobe: marker %s unopenable (block-forever degrades to spin)",
                 qPrintable(marker));
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
// ── t1142 SAVE-02/E1 强退探针（驱逐暂存 × 完整保存代次；生产链真实入口）────────────────────
// child-evict     ：存 A（SaveBridge 生产链）→ D3 转换 → StreamingBridge::enterWorld 真实进入
//                   （实体三缝生产装配）→ 目标 chunk 物化 + 生物/掉落物布防 + 编辑 → 走离驱逐
//                   （暂存域承载：chunk_staging + entity_staging）→ 阻塞（kill 唯一出口）。
// child-evictsave ：同上 + 完整保存 B（SaveBridge 生产链——暂存晋升 + 载荷合并 + item 快照
//                   随 t1129 事务提交）→ 阻塞。
// parent-evict    ：kill 后重开（新进程 = 新会话）→ 四面/世界内容/两暂存表/实体两族 = 恰上一
//                   完整保存代 A（未提交暂存随会话开启截断——S1 主命题）。
// parent-evictsave：kill 后重开 → 恰 B（暂存已晋升：编辑在场 + 卸载实体恰一份恢复——E1 世代
//                   协调命题）。
constexpr int kECoreW = 48, kECoreD = 48, kEH = 96, kESeed = 42;
// 目标 chunk（4,1）与编辑格（x=72, z=24 → chunk (4,1) 局部 (8,8)；y 由 heightmap 现取）。
constexpr int kETargetCX = 4, kETargetCZ = 1, kEEditX = 72, kEEditZ = 24;

QVariantMap countFaces(const QString &db) // 两暂存表 + 实体两已提交面行数（独立连接 raw 读）。
{
    return withProbe(db, [](QSqlDatabase &p) {
        QVariantMap out;
        const auto count = [&p](const char *table) -> int {
            QSqlQuery q(p);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table)))
                && q.next())
                return q.value(0).toInt();
            return -1; // 表缺席（旧档）→ -1（调用方按 0 判读——区别于装配失败面）
        };
        out.insert(QStringLiteral("stageRows"), count("chunk_staging"));
        out.insert(QStringLiteral("entityStageRows"), count("entity_staging"));
        out.insert(QStringLiteral("entityRows"), count("entities"));
        out.insert(QStringLiteral("itemRows"), count("item_entities"));
        out.insert(QStringLiteral("editRows"), count("chunk_edits"));
        return out;
    });
}

// A 载荷（t1129 baseRequest 同形——箱空 + 背包 10 + 进度 1）；B 载荷 = movedRequest 同形
//（物品入箱）。四面对账直接复用 readFaces / printFaces（账本打印口径与 t1129 先例逐列同）。
int runEvictChild(const QString &db, const QString &marker, bool saveAfter)
{
    if (!prepareBase(db))
        return 2;
    // D3 转换（真实生产转换面：markStreamingWorld——标志 + core dims + 代次锚）。
    {
        ChunkStore meta;
        meta.bind(db);
        meta.setStreamWorldId(db);
        if (!meta.markStreamingWorld(kECoreW, kECoreD))
            return 2;
    }
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kESeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return 2;
    store.setWorld(&w); // r2010d：enterWorld 的 rebind 纪律前置（store.world == world）
    PlayerController pc;
    EntityManager em;
    ItemEntityManager iem;
    pc.setEntityManager(&em); // QML 属性注入的生产镜像（Main.qml itemEntities:/entityManager:）
    pc.setItemEntities(&iem);
    WorldClock clk;
    if (!StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db, kESeed))
        return 2; // 流式进入失败 = 装配失败（非行为面）
    // 目标 chunk 同步物化（loadChunkAt 生产入口——r2053c 先例；核心域外 chunk）。
    if (!w.loadChunkAt(kETargetCX, kETargetCZ))
        return 2;
    const int top = w.heightmapAt(kEEditX, kEEditZ);
    if (top < 1 || top + 2 >= kEH)
        return 2;
    // 布防：驯服坐下狼 + 掉落物（目标 chunk 中心域）+ 玩家编辑（顶面放 Stone）。
    const int mobSlot = em.spawnMobTyped(kEEditX, top + 1, kEEditZ, EntityManager::MobWolf,
                                         QStringLiteral("#eeeeee"), 10);
    if (mobSlot < 0)
        return 2;
    em.setTameRollOverride(0); // 确定性驯服（必成样本——探针零 RNG 依赖）
    if (!em.tameWolf(mobSlot))
        return 2;
    em.toggleWolfSit(mobSlot); // 已驯服狼 → 坐下留守（属性守恒载荷——回访/重开恢复面）
    iem.spawnItem(kEEditX, top + 1, kEEditZ, int(BR::Stone), 3);
    if (!w.setBlock(kEEditX, top + 1, kEEditZ, BR::Stone))
        return 2;
    // 位置沿 + 泵拍（probe 直发生产信号——feed 钩/pump 钩的真实消费链；驱动编排全走桥）。
    emit pc.playerChunkChanged(1, 1);
    for (int i = 0; i < 4; ++i)
        emit clk.ticked(0.1); // 初载窗（目标 chunk 已驻留——驱逐沿前的稳态）
    emit pc.playerChunkChanged(7, 7); // 走离：cheb((4,1),(7,7)) = 6 ∈ (1,3] annulus → 驱逐沿
    for (int i = 0; i < 4; ++i)
        emit clk.ticked(0.1); // 驱逐拍（persist 缝 = 暂存域写 → 转移 ⑥⑦ → 实体三缝）
    const QVariantMap pre = countFaces(db);
    fprintf(stdout, "KILLPROBE child faces stage=%d estage=%d edits=%d ents=%d items=%d"
                    " (pre-kill, saveAfter=%d)\n",
            pre.value(QStringLiteral("stageRows")).toInt(),
            pre.value(QStringLiteral("entityStageRows")).toInt(),
            pre.value(QStringLiteral("editRows")).toInt(),
            pre.value(QStringLiteral("entityRows")).toInt(),
            pre.value(QStringLiteral("itemRows")).toInt(), saveAfter ? 1 : 0);
    fflush(stdout);
    if (saveAfter) {
        // 完整保存 B（生产链——flush 钩 = 晋升 + 冲洗 + 推进 + item 快照同事务；provider 钩 =
        // 暂存 mob 行并入 entities 载荷）。载荷 = movedRequest 同形（物品入箱 + 背包空 + 进度 2）。
        const SaveRequest rb = movedRequest();
        const bool okB = SaveBridge::instance()->saveViaCoordinator(&store, db, rb.name, rb.chests,
            rb.furnaces, rb.dispensers, rb.worldTime, rb.bedSpawn, rb.playerData, rb.progress);
        if (!okB)
            return 2;
        const QVariantMap post = countFaces(db);
        fprintf(stdout, "KILLPROBE child faces stage=%d estage=%d edits=%d ents=%d items=%d"
                        " (post-save)\n",
                post.value(QStringLiteral("stageRows")).toInt(),
                post.value(QStringLiteral("entityStageRows")).toInt(),
                post.value(QStringLiteral("editRows")).toInt(),
                post.value(QStringLiteral("entityRows")).toInt(),
                post.value(QStringLiteral("itemRows")).toInt());
        fflush(stdout);
    }
    store.closeWorld();
    // 标记 = 「paused <编辑y>」一行双载荷（父进程两读：存在性轮询 + 精确格判据——重开面
    //   heightmap 随标记本身生长，top+1 读式在 evictsave 向会假阴；验收「SQL 行/游戏状态
    //   双读」的游戏状态半边由此锚定）。阻塞自旋 = blockForever 同门（kill 唯一出口）。
    {
        QFile mf(marker);
        if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qWarning("killprobe: marker %s unopenable (block degrades to spin)",
                     qPrintable(marker));
        else {
            mf.write(QStringLiteral("paused %1\n").arg(top + 1).toUtf8());
            mf.close();
        }
        fflush(stdout);
        for (;;)
            QThread::msleep(100);
    }
    return 0;
}

int runEvictParent(const QString &db, const QString &marker, bool saveAfter)
{
    QFile::remove(marker);
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({ QStringLiteral("--killprobe"),
        saveAfter ? QStringLiteral("child-evictsave") : QStringLiteral("child-evict"),
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
    const QDeadlineTimer deadline(60000);
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
    child.kill(); // 真实终止（无 atexit 无清理无回滚——t1129 parent 先例同面）
    child.waitForFinished(10000);
    fprintf(stdout, "KILLPROBE parent killed child at %s window\n",
            saveAfter ? "post-commit block" : "staged-uncommitted block");
    fflush(stdout);
    // 重开（新进程 = 新会话语义的真实承载：会话开启拍 = 暂存截断 + 升格 + 上界守卫）。
    World w;
    w.setWidth(kKW);
    w.setDepth(kKD);
    w.setHeight(kKH);
    w.setSeed(kESeed);
    WorldStore store;
    if (!(store.openWorld(db) && store.isOpen()))
        return 2;
    store.setWorld(&w);
    PlayerController pc;
    EntityManager em;
    ItemEntityManager iem;
    pc.setEntityManager(&em);
    pc.setItemEntities(&iem);
    WorldClock clk;
    if (!StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db, kESeed))
        return 2;
    // QML 进入链镜像（Main.qml 1112-1130 行为级）：清残留 → 已提交快照恢复两族。
    em.clearAll();
    iem.clearAll();
    em.restorePersistedEntities(store.loadEntities());
    iem.restorePersistedRows(store.loadItemEntities());
    // 目标 chunk 重物化走**驱动器数据面**（committed 行路由的生产行为面——loadChunkAt 同步
    //   链 = 确定性重生成，不识已提交行，作重开读面会假阴）。
    emit pc.playerChunkChanged(kETargetCX, kETargetCZ);
    // 主控勘正（t1142 验收复采抓出）：泵拍须带真实时间让渡——异步装载作业交付需要真实毫秒
    //   （convergeLoaded1142 msleep(2) 同门；紧循环零让渡 = 作业饥饿 → 恒超时静默 return 2，
    //   提交态复采两连现）。上限 400×5ms = 2s 有界防挂死不变。
    for (int i = 0; i < 400 && w.chunks().lifecycleAt(kETargetCX, kETargetCZ)
                                            != ChunkLifecycle::Loaded; ++i) {
        emit clk.ticked(0.1);
        QThread::msleep(5);
    }
    if (w.chunks().lifecycleAt(kETargetCX, kETargetCZ) != ChunkLifecycle::Loaded)
        return 2;
    QFile mf(marker);
    const int editY = mf.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(mf.readAll()).section(QLatin1Char(' '), 1).trimmed().toInt()
        : -1;
    if (editY < 0)
        return 2; // 标记缺 y = 子进程装配面破坏（非行为面）
    const quint8 cell = w.blockAt(kEEditX, editY, kEEditZ);
    const QVariantMap faces = countFaces(db);
    const KFace f = readFaces(db);
    const bool editGone = cell != quint8(BR::Stone); // 未提交编辑不复活（evict 命题）
    const bool editKept = cell == quint8(BR::Stone); // 已提交编辑恰在场（evictsave 命题）
    const bool stagedGone = faces.value(QStringLiteral("stageRows")).toInt() <= 0
        && faces.value(QStringLiteral("entityStageRows")).toInt() <= 0;
    fprintf(stdout, "KILLPROBE %s verdict edit=%d stonePresent=%d stagingEmpty=%d"
                    " mobsLive=%d itemsLive=%d entsRows=%d itemRows=%d\n",
            saveAfter ? "parent-evictsave" : "parent-evict", editGone ? 1 : 0,
            editKept ? 1 : 0, stagedGone ? 1 : 0, em.liveCount(), iem.liveCount(),
            faces.value(QStringLiteral("entityRows")).toInt(),
            faces.value(QStringLiteral("itemRows")).toInt());
    fflush(stdout);
    printFaces(saveAfter ? "parent-evictsave" : "parent-evict", db);
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
    if (mode == QStringLiteral("child-evict") && !db.isEmpty() && !marker.isEmpty())
        return runEvictChild(db, marker, false);
    if (mode == QStringLiteral("parent-evict") && !db.isEmpty() && !marker.isEmpty())
        return runEvictParent(db, marker, false);
    if (mode == QStringLiteral("child-evictsave") && !db.isEmpty() && !marker.isEmpty())
        return runEvictChild(db, marker, true);
    if (mode == QStringLiteral("parent-evictsave") && !db.isEmpty() && !marker.isEmpty())
        return runEvictParent(db, marker, true);
    fprintf(stdout, "KILLPROBE usage: --killprobe inject|inject-old|child|child-old|parent|"
                    "parent-old|child-evict|parent-evict|child-evictsave|parent-evictsave"
                    " <db> [marker]\n");
    fflush(stdout);
    return 2;
}
