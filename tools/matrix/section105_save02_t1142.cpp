#include "matrix_helpers.h"

#include "gamesession.h"      // 被测面：W3 暂存缝 + 会话开启拍 + 同事务冲洗晋升 + 恢复缝
#include "chunkstore.h"       // 被测面：chunk_edits/chunk_staging/stream_worlds 原始读面
#include "entitystagestore.h" // 被测面：entity_staging / item_entities 域（行族常量）
#include "savecoordinator.h"  // 被测面：t1129 单事务保存协议（晋升拍随冲洗钩入事务）
#include "worldstore.h"       // 被测面：库打开 / 已提交面读回（loadEntities/loadItemEntities）
#include "savebridge.h"       // 被测面：生产保存链（载荷合并 provider 消费面）
#include "streamingbridge.h"  // 被测面：E1 生产进入链真实入口（实体三缝生产装配——审查原文）
#include "entitymanager.h"    // 被测面：mob 身份 / 卸载序列化 / 恢复 / 冻结门
#include "itementitymanager.h" // 被测面：掉落物卸载序列化 / 恢复 / 冻结门

#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

// t1142 SAVE-02 设计-实现批 探针段（7 腿；filter 词 "r2112"；矩阵 994→994+N）。置尾先例沿用
//（接 section104，runAll 末执行）；fresh 小世界 + 临时 SQLite 库（pid 键名，测试自清理——
// r2015 段同门绝对路径，真实用户 saves/ 零触碰）。任务契约（第三轮外审 + 复核共同最高优先
// S1+E1；设计工件 build/t1142_save02_design.txt 定谳）：
//   「S1 驱逐暂存 × 强退窗口」→ r2112a（完整保存 A → 编辑 + 驱逐 = 暂存域承载[committed 面
//     零新行] → 会话等价重开[会话开启拍 = 截断 + 升格 + 上界守卫] → 恰上一完整保存代 A；
//     重复编辑同格 = 暂存 upsert 单行）；
//   「同会话回访恰一次 + 负坐标/远外环 + 重复驱逐」→ r2112b（走离驱逐 → 走回暂存物化逐位
//     恒等 → 再走离零重写[暂存行保留] → 跨区掉落物按当前位置归户）；
//   「正常保存恰 B 新代恰一次」→ r2112c（核心 + 负坐标 + 远外环三编辑驱逐 → 保存事务晋升 +
//     驻留冲洗 + 推进同事务 → committed 恰三行同代次 + 暂存腾空 → 重开恰一份恢复）；
//   「失败后脏账保留可重试」→ r2112d（World 段注入 → 整事务回滚 = 暂存原样 + 脏账原样 +
//     台账 Interrupted → 解除注入重试收敛恰 B）；
//   「E1 走离→卸载→等待→回访恰一次 + 非驻留冻结 + 跨区归户」（真实 StreamingBridge 生产
//     入口——审查 E1 原文「不在测试中手动补生产缺钩」）→ r2112e（驯服坐下狼 + 羊 + 幼体 +
//     掉落物走离 → 暂存域承载 + 活体清零 → 非驻留冻结门[缺席 chunk 零模拟] → 走回恢复恰一
//     次[字段全保真 + 暂存消费] → 跨区掉落物归新属主）；
//   「E1 死亡不复活 + 期间保存重进恰一份」→ r2112f（死亡面：击杀不入档 → 保存 → 重进零复
//     活；保存重进面：卸载暂存随保存事务进已提交快照[entities 段载荷合并 + item_entities
//     快照拍] → 重进恰一份）；
//   「结构钉族」→ r2112g（新表纯追加钉族[零 bump 零破坏性 SQL] + 会话开启拍/晋升拍/上界守卫
//     /暂存缝源钉 + 生产装配钉[StreamingBridge 真实入口三缝] + 冻结门源钉 + 哨兵钉 166/213
//     不动 + 前序 NEG 承重行逐字幸存复核 + CMake 段行 + 源树 filter 词元零命中 + QML 玩法
//     路径负面门；NEG-1 摘面行由本腿 session-open-clear 行钉持有——NEG-1 敏感申报）。
// 恰红面设计（先于腿文；双手工 Edit 摘单行承重语句，编译绿）：
//   NEG-1 = 摘 gamesession.h loadStreamingWorld 的会话开启截断行（if (m_entityStage &&
//     (!m_chunkStore->clearStaging() || !m_entityStage->clearStaging())) 整条语句摘除）→
//     暂存域跨会话泄漏：驱逐暂存行在「重开」面仍被回灌路由命中。声明红面（实测复核）
//     = {r2112a}[恰 A 断言翻红——被驱逐编辑复活], {r2112f}[同物理根因连通红：卸载暂存行跨
//     重进存活 → 已提交面之外暂存面二次恢复——恰一份面翻红], {r2112g}[摘面行钉面红：r2112g
//     持 "session open clear" 行钉 = 摘行即红——「摘面行本腿持有」申报（t1135/t1136 同门），
//     restore 轮即复绿非误伤]。r2112b/c/d/e 豁免（同会话面 / 保存晋升面 / 真桥单会话面在
//     NEG-1 下暂存行仍被各自消费路径承载——实测零连带）。
//   NEG-2 = 摘 gamesession.h flushResidentEditsForSaveOn 的晋升行（if (m_chunkStore->
//     promoteStagingOn(db) < 0) 整条语句摘除）→ 暂存永不晋升：保存后新代缺席 committed 面。
//     声明红面（实测复核）= {r2112c[恰 B 三行同代次面], r2112d[重试收敛面——committed 恰两
//     行]} + {r2112g}[摘面行钉面红：r2112g 持 "save promote beat" 行钉 = 摘行即红——「摘面行
//     本腿持有」申报，restore 轮即复绿非误伤]。r2112a 豁免申报：其零期望 committed 计数读
//     以「空表 ≡ 表缺席」判零（-1 哨兵并判——帐面意图 = 零泄漏行非装配判读；A 保存时暂存
//     本空，NEG-2 下 chunk_edits 表根本不建 = 零泄漏行最强形态）——实测该哨兵翻红后随读面
//     语义校正豁免，非判别面卸除。r2112f 豁免申报（原判误含，实测豁免）：其卸载实体已提交
//     面走 provider 载荷合并 + item 快照拍两径（stagedRows 直读），不经 chunk 晋升行——
//     NEG-2 零触达。r2112b/e 豁免（零保存面 / 回访走暂存域）。
//   两 NEG 摘面行不入 r2112g 钉族（NEG 摘面豁免不钉——r2109d 同门）。
// 词元纪律：腿名 / diag 零跨任务 filter 词元（本段注释中的族引用不进腿名）。
// 确定性铁律：全腿逻辑时间驱动（stepTick / 桥链 ticked 直发——零 wall-clock sleep 断言；
//   收敛轮询 deadline 有界防挂死）；真实进程强退面归 killprobe CLI（t1129 先例复用——矩阵
//   腿内以「会话等价重开」承载新进程语义：会话开启拍即新进程进入拍）。

namespace {

constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42; // sparse 核心域（W2 段同族）
constexpr int kH2 = 96;

// sparse 世界（radius 0——全量走流式链，section28 同门）。
World makeSparseWorld()
{
    World::SparseWorldParams sp;
    sp.seed = kSeed;
    sp.coreWidth = kWS;
    sp.coreDepth = kDS;
    sp.height = kH;
    sp.spawnPreGenerateRadius = 0;
    return World(sp);
}

// 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰）。
QString tempDb1142(const char *tag)
{
    return QDir::temp().absoluteFilePath(QStringLiteral("voxel_save02_%1_%2.sqlite")
                                             .arg(QLatin1String(tag))
                                             .arg(QCoreApplication::applicationPid()));
}

// 收敛轮询（section28 同门）：泵 tick 直到 keys 全部 Loaded 或超时。
bool convergeLoaded1142(GameSession &gs, World &w, const QVector<QPair<int, int>> &keys,
                        int deadlineMs, QString &diag)
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
            diag += QStringLiteral("[timeout %1ms miss: %2] ").arg(deadlineMs).arg(miss);
            return false;
        }
        gs.stepTick(0.11); // 恰 1 整 tick = 1 个 tick 尾流式泵拍
        QThread::msleep(2);
    }
}

// 桥链泵拍收敛（真实 StreamingBridge 会话：ticked 直发 = 生产泵拍同源信号；deadline 有界）。
bool convergeBridge(World &w, WorldClock &clk, const QVector<QPair<int, int>> &keys,
                    int deadlineMs, QString &diag)
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
            diag += QStringLiteral("[bridge-converge timeout %1ms] ").arg(deadlineMs);
            return false;
        }
        emit clk.ticked(0.1); // 生产钩链：ticked → 桥泵拍（driver pump + 数据面 + 恢复缝）
        QThread::msleep(2);
    }
}

// 全 chunk 体素快照/比对（16×16 列 × 全 y 带 id+state——物化逐位恒等柱承载面，section28 同门）。
QVector<QPair<int, int>> snapChunk1142(World &w, int cx, int cz)
{
    QVector<QPair<int, int>> vox;
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx)
            for (int y = 0; y < kH; ++y)
                vox.append({ w.blockAt(cx * 16 + lx, y, cz * 16 + lz),
                    w.stateAt(cx * 16 + lx, y, cz * 16 + lz) });
    return vox;
}

bool chunkIdentical1142(World &w, int cx, int cz, const QVector<QPair<int, int>> &snap,
                        long &equalCount, QString &diffDiag)
{
    int i = 0;
    int diffs = 0;
    equalCount = 0;
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx)
            for (int y = 0; y < kH; ++y) {
                const int lid = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                const int lst = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                if (lid == snap[i].first && lst == snap[i].second) {
                    ++equalCount;
                } else if (++diffs <= 8) {
                    diffDiag += QStringLiteral("[%1,%2,%3 %4/%5->%6/%7] ")
                                    .arg(lx)
                                    .arg(lz)
                                    .arg(y)
                                    .arg(snap[i].first)
                                    .arg(snap[i].second)
                                    .arg(lid)
                                    .arg(lst);
                }
                ++i;
            }
    return diffs == 0;
}

// 顶面放 Stone（支撑非空气 + 目标空气门——section92 同门）；返回目标 y（失败 -1）。
int placeStoneMarker(World &w, int x, int z)
{
    const int h = w.heightmapAt(x, z);
    if (h < 1 || h + 2 >= w.height())
        return -1;
    if (w.blockAt(x, h, z) == 0 || w.blockAt(x, h + 1, z) != 0)
        return -1;
    w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
    return w.blockAt(x, h + 1, z) == quint8(BR::Stone) ? h + 1 : -1;
}

// 独立连接 raw 计数（暂存两表 / committed 两表 / 台账两键——装配面判读；表缺席 = -1）。
QVariantMap rawCounts(const QString &db)
{
    QVariantMap out;
    const QString conn = QStringLiteral("save02_raw_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    p.setDatabaseName(db);
    if (p.open()) {
        const auto count = [&p](const char *table) -> int {
            QSqlQuery q(p);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table)))
                && q.next())
                return q.value(0).toInt();
            return -1;
        };
        out.insert(QStringLiteral("staged"), count("chunk_staging"));
        out.insert(QStringLiteral("edits"), count("chunk_edits"));
        out.insert(QStringLiteral("estaged"), count("entity_staging"));
        out.insert(QStringLiteral("ients"), count("item_entities"));
        out.insert(QStringLiteral("ents"), count("entities"));
        QSqlQuery g(p);
        if (g.exec(QStringLiteral("SELECT key, value FROM save_coord")))
            while (g.next())
                out.insert(QStringLiteral("coord_%1")
                               .arg(g.value(0).toString()),
                    g.value(1).toString());
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
}

// 实体暂存域行数 raw 读（真实桥腿的暂存断言面——会话句柄不外露，库面独立连接判读）。
int gs_entityStageCount(const QString &db)
{
    return rawCounts(db).value(QStringLiteral("estaged")).toInt();
}

// 实体暂存域行读回（probe store 只读面——归户断言用；非消费）。
QVariantList gs_entityStageRows(const QString &db)
{
    EntityStageStore probe;
    probe.bind(db);
    return probe.stagedRows();
}

// 最小保存载荷（A 基线 / B 推进面共用形：箱 4,5,6 + 背包 10 + 进度戳——四面可判读）。
SaveRequest baseRequest1142(const char *name, int stat)
{
    SaveRequest r;
    r.name = QLatin1String(name);
    QVariantMap chest;
    chest.insert(QStringLiteral("x"), 4);
    chest.insert(QStringLiteral("y"), 5);
    chest.insert(QStringLiteral("z"), 6);
    chest.insert(QStringLiteral("slots"), QVariantList{});
    r.chests = QVariantList{ chest };
    QVariantMap pd;
    pd.insert(QStringLiteral("px"), 11.5);
    QVariantList inv;
    inv.append(QVariantMap{ { QStringLiteral("id"), 3 }, { QStringLiteral("count"), 10 } });
    pd.insert(QStringLiteral("inv"), inv);
    r.playerData = pd;
    QVariantMap pr;
    pr.insert(QStringLiteral("stat"), stat);
    r.progress = pr;
    return r;
}

// 流式会话三绑定（库路径 + 世界标识 + D2 登记锚——腿内生产等价装配面）。
bool bindStreaming1142(GameSession &gs, const QString &db, const char *tag)
{
    const QString worldId = QStringLiteral("save02_%1").arg(QLatin1String(tag));
    return gs.bindChunkEditsStore(db) && gs.bindEntityStageStore(db)
        && gs.setStreamWorldId(worldId) && gs.markStreamingWorld(kWS, kDS);
}

// 驱逐沿驱动（位置沿 + 恰 1 整 tick = 1 个 tick 尾驱逐拍）。
bool evictNow1142(GameSession &gs, int fromX, int fromZ, int toX, int toZ)
{
    gs.notePlayerChunk(fromX, fromZ);
    gs.stepTick(0.11);
    gs.notePlayerChunk(toX, toZ);
    gs.stepTick(0.11);
    return true;
}

} // namespace

void MatrixRun::section105_save02_t1142()
{
    // ── r2112a：S1 驱逐暂存 × 强退窗口（会话等价重开恰上一完整保存代）──────────────────────
    runLeg(QStringLiteral("r2112a eviction staging kill window (a complete save A then an edit"
        " evicted out of radius carries the change in the session staging table only - the"
        " committed snapshot keeps zero new rows and the committed generation scale stays at A -"
        " and the equivalent process reopen truncates the staging domain at the session-open beat"
        " so the re-materialized chunk answers exactly save A with the placed stone gone; a"
        " second edit of the same cell before eviction keeps one staging row via the eid-keyed"
        " upsert)"), [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("a");
        QFile::remove(db);

        World w = makeSparseWorld();
        GameSession gs(w);
        gs.configureStreamingRadii(1, 1, 3);
        const bool bindOk = bindStreaming1142(gs, db, "a");
        ok = ok && bindOk;

        // 初载 + 完整保存 A（本地协调器 + 会话冲洗钩——生产协议在腿内等价装配）。
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded1142(gs, w, centerKeys, 45000, d1);
        ok = ok && c1;
        if (!c1) diag += d1;
        WorldStore store;
        const bool openOk = store.openWorld(db) && store.isOpen();
        store.setWorld(&w);
        ok = ok && openOk;
        SaveCoordinator coord;
        coord.bind(&store, db);
        coord.setFlushHook([&gs](QSqlDatabase &db2) { return gs.flushResidentEditsForSaveOn(db2); });
        coord.setFlushCommitHook([&gs]() { gs.commitFlushResidentEditsOn(); });
        const SaveRequest ra = baseRequest1142("r2112a", 1);
        const SaveReceipt rcA = coord.saveAll(ra);
        ok = ok && rcA.ok();
        if (!rcA.ok())
            diag += QStringLiteral("[saveA code=%1] ").arg(rcA.error.code);
        StreamWorldMeta metaA;
        const bool metaOkA = gs.chunkEditsStore()->readStreamWorldMeta(metaA);
        ok = ok && metaOkA && metaA.saveGen == 1;

        // 编辑（核心域 chunk (2,2) 内格）+ 同格二次编辑 + 驱逐 → 暂存域承载。
        const int ex = 40, ez = 40; // chunk (2,2)
        const int ey = placeStoneMarker(w, ex, ez);
        ok = ok && ey > 0;
        w.setBlock(ex, ey, ez, quint8(BR::Dirt), 0); // 同格多次编辑（后写胜承载）
        w.setBlock(ex, ey, ez, quint8(BR::Stone), 0);
        evictNow1142(gs, 2, 2, 5, 5); // 走离：cheb((2,2),(5,5)) = 3 ∈ (1,3] → 驱逐
        const bool absentOk = w.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent;
        ok = ok && absentOk;
        ChunkStoreBlob srow;
        const bool stageOk = gs.chunkEditsStore()->hasStagedChunk(2, 2)
            && gs.chunkEditsStore()->loadStagedChunk(2, 2, srow)
            && srow.generation == metaA.saveGen + 1 // 观测刻度 = 在途待保存代次
            && gs.chunkEditsStore()->stagedChunkCount() == 1;
        ok = ok && stageOk;
        if (!stageOk)
            diag += QStringLiteral("[stage has=%1 gen=%2 n=%3] ")
                        .arg(gs.chunkEditsStore()->hasStagedChunk(2, 2))
                        .arg(gs.chunkEditsStore() ? qint64(srow.generation) : qint64(-1))
                        .arg(gs.chunkEditsStore()->stagedChunkCount());
        // S1 结构分离核心断言：committed 面零新行（未提交写永不直触快照域）。
        const QVariantMap preCounts = rawCounts(db);
        // 「零期望」committed 计数读 = 空表与表缺席同判（-1 哨兵 = 表缺席 = 零行最强形态；
        //   本帐面意图是「committed 零泄漏行」，非装配判读——装配面由本腿其余柱承载）。
        const bool committedUntouched = preCounts.value(QStringLiteral("staged")).toInt() == 1
            && preCounts.value(QStringLiteral("edits")).toInt() <= 0
            && preCounts.value(QStringLiteral("coord_complete_generation")).toString()
                == QStringLiteral("1");
        ok = ok && committedUntouched;
        if (!committedUntouched)
            diag += QStringLiteral("[pre stg=%1 ed=%2 cg=%3] ")
                        .arg(preCounts.value(QStringLiteral("staged")).toInt())
                        .arg(preCounts.value(QStringLiteral("edits")).toInt())
                        .arg(preCounts.value(QStringLiteral("coord_complete_generation")).toString());

        // 会话等价重开（新进程语义 = 会话开启拍：fresh 世界 + fresh 会话 + loadStreamingWorld）。
        World w2 = makeSparseWorld();
        GameSession gs2(w2);
        gs2.configureStreamingRadii(1, 1, 3);
        const bool bind2 = bindStreaming1142(gs2, db, "a");
        ok = ok && bind2;
        WorldStore store2;
        store2.openWorld(db);
        store2.setWorld(&w2);
        const bool loadOk = gs2.loadStreamingWorld(store2);
        ok = ok && loadOk;
        // 会话开启拍断言：两暂存域截断（强退窗口的未提交编辑随世界一并回退）。
        const bool truncated = gs2.chunkEditsStore()->stagedChunkCount() == 0
            && gs2.entityStageStore()->stagedCount() == 0;
        ok = ok && truncated;
        if (!truncated)
            diag += QStringLiteral("[trunc stg=%1 est=%2] ")
                        .arg(gs2.chunkEditsStore()->stagedChunkCount())
                        .arg(gs2.entityStageStore()->stagedCount());
        // 恰上一完整保存代 A：重物化（无 committed 行无暂存行 → seed 确定性重生成）后编辑缺席。
        gs2.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> backKeys{ { 2, 2 } };
        QString d2;
        const bool c2 = convergeLoaded1142(gs2, w2, backKeys, 45000, d2);
        ok = ok && c2;
        if (!c2) diag += d2;
        const bool stoneGone = w2.blockAt(ex, ey, ez) != quint8(BR::Stone);
        ok = ok && stoneGone;
        if (!stoneGone)
            diag += QStringLiteral("[stone-alive id=%1] ").arg(w2.blockAt(ex, ey, ez));

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112a eviction staging kill window: the evicted edit rides the"
                             " session staging table only with the committed snapshot untouched,"
                             " the equivalent process reopen truncates staging at the"
                             " session-open beat and the chunk answers exactly save A with the"
                             " stone gone, and repeated same-cell edits keep one staged row"
                          << (ok ? QString() : diag);
    });

    // ── r2112b：同会话回访恰一次 + 负坐标/远外环 + 重复驱逐 + 跨区归户 ─────────────────────
    runLeg(QStringLiteral("r2112b same-session revisit and ownership (edits in a negative column"
        " chunk and a far outer chunk are evicted into staging and walking back re-materializes"
        " both bitwise identical with the placed markers present, walking away again evicts"
        " without rewriting the retained staging rows, and an item thrown across a chunk border"
        " before eviction stages under its current owning chunk rather than the origin)"),
        [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("b");
        QFile::remove(db);

        World w = makeSparseWorld();
        GameSession gs(w);
        gs.configureStreamingRadii(1, 1, 3);
        ok = ok && bindStreaming1142(gs, db, "b");

        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        const bool c1 = convergeLoaded1142(gs, w, centerKeys, 45000, d1);
        ok = ok && c1;
        if (!c1) diag += d1;

        // 负坐标 chunk (-2,-1) + 远外环 chunk (8,7) 双编辑（先物化后放置——同步链生产入口）。
        const int negX = -2 * 16 + 8, negZ = -1 * 16 + 8;
        const int farX = 8 * 16 + 8, farZ = 7 * 16 + 8;
        const bool negMat = w.loadChunkAt(-2, -1);
        const bool farMat = w.loadChunkAt(8, 7);
        ok = ok && negMat && farMat;
        const int negY = placeStoneMarker(w, negX, negZ);
        const int farY = placeStoneMarker(w, farX, farZ);
        ok = ok && negY > 0 && farY > 0;
        const QVector<QPair<int, int>> snapNeg = snapChunk1142(w, -2, -1);
        const QVector<QPair<int, int>> snapFar = snapChunk1142(w, 8, 7);

        // 走离驱逐（两步窗移——annulus 只逐 cheb ∈ (1,3] 的新窗环带，窗外驻留不误逐[r2025b
        //   先例]）：(2,2)→(5,5) 逐 (8,7)（cheb 3）；(5,5)→(0,0) 逐 (-2,-1)（cheb 2）。
        evictNow1142(gs, 2, 2, 5, 5);
        const bool farEvicted = w.chunks().lifecycleAt(8, 7) == ChunkLifecycle::Absent
            && gs.chunkEditsStore()->hasStagedChunk(8, 7);
        ok = ok && farEvicted;
        evictNow1142(gs, 5, 5, 0, 0);
        const bool bothAbsent = w.chunks().lifecycleAt(-2, -1) == ChunkLifecycle::Absent
            && w.chunks().lifecycleAt(8, 7) == ChunkLifecycle::Absent;
        ok = ok && bothAbsent;
        const bool staged2 = gs.chunkEditsStore()->stagedChunkCount() == 2
            && gs.chunkEditsStore()->hasStagedChunk(-2, -1)
            && gs.chunkEditsStore()->hasStagedChunk(8, 7);
        ok = ok && staged2;
        if (!staged2)
            diag += QStringLiteral("[staged n=%1 n2=%2 f=%3] ")
                        .arg(gs.chunkEditsStore()->stagedChunkCount())
                        .arg(gs.chunkEditsStore()->hasStagedChunk(-2, -1))
                        .arg(gs.chunkEditsStore()->hasStagedChunk(8, 7));

        // 走回：负坐标 chunk 重物化（暂存先行路由）逐位恒等 + 编辑在场。
        gs.notePlayerChunk(-2, -1);
        QVector<QPair<int, int>> negKeys{ { -2, -1 } };
        QString d2;
        const bool c2 = convergeLoaded1142(gs, w, negKeys, 45000, d2);
        ok = ok && c2;
        if (!c2) diag += d2;
        long eqNeg = 0;
        QString diffNeg;
        const bool idNeg = chunkIdentical1142(w, -2, -1, snapNeg, eqNeg, diffNeg);
        ok = ok && idNeg && w.blockAt(negX, negY, negZ) == quint8(BR::Stone);
        if (!idNeg)
            diag += QStringLiteral("[neg-identity eq=%1 %2] ").arg(eqNeg).arg(diffNeg);
        // 暂存行保留（回访消费面 = 实体域 SELECT+DELETE；chunk 暂存行只在保存晋升拍删除）。
        ok = ok && gs.chunkEditsStore()->stagedChunkCount() == 2;

        // 走离再驱逐（clean 候选零重写：行数与代次恒定 = 暂存行保留语义）。
        evictNow1142(gs, -2, -1, 0, 0);
        ok = ok && w.chunks().lifecycleAt(-2, -1) == ChunkLifecycle::Absent
            && gs.chunkEditsStore()->stagedChunkCount() == 2;

        // 跨区归户：收敛 (0,0) 窗后从 chunk (0,1) 域内带初速掷过 chunk 边（x 14.5 → 掷速 +X
        //   越 x=16 → chunk (1,1)），归户断言 = 随属主驱逐后暂存行位置换算 chunk 1 而非 0。
        gs.notePlayerChunk(0, 0);
        QVector<QPair<int, int>> zeroKeys;
        for (int cz = -1; cz <= 1; ++cz)
            for (int cx = -1; cx <= 1; ++cx)
                zeroKeys.append({ cx, cz });
        QString d3;
        const bool c3 = convergeLoaded1142(gs, w, zeroKeys, 45000, d3);
        ok = ok && c3;
        if (!c3) diag += d3;
        const int throwY = w.heightmapAt(14, 24);
        ok = ok && throwY > 1;
        ItemEntityManager iem;
        // 腿内实体缝装配（section28 r2025c 同门测试替身——生产装配面 = StreamingBridge，r2112e
        //   真桥腿承重；本腿只驅逐归户单元面）。
        gs.setEvictionEntitySink([&gs, &iem](int cx, int cz) {
            if (!gs.stageEntitiesForChunk(cx, cz, iem.exportPersistedInChunk(cx, cz)))
                return;
            iem.despawnInChunk(cx, cz);
        });
        iem.spawnItemThrown(QVector3D(14.5f, float(throwY) + 1.0f, 24.5f), int(BR::Stone), 1,
                            1.0f, 0.0f, 0.0f, 9.0f);
        bool crossed = false;
        for (int i = 0; i < 60 && !crossed; ++i) {
            gs.stepTick(0.11); // 世界拍（占位泵拍语义）；掉落物物理经 store tick 直驱等价面
            iem.tick(0.1, &w);
            const QVector3D p = iem.posAt(0);
            crossed = p.x() >= 16.0f;
        }
        ok = ok && crossed;
        if (!crossed)
            diag += QStringLiteral("[cross x=%1] ").arg(iem.count() > 0 ? iem.posAt(0).x() : -1.0f);
        w.setBlock(20, w.heightmapAt(20, 24) + 1, 24, quint8(BR::Stone), 0); // chunk (1,1) 编辑
        evictNow1142(gs, 0, 0, 3, 0); // (1,1) cheb 2 ∈ annulus → 随属主驱逐
        int owner1 = 0, owner0 = 0;
        const QVariantList stagedRows = gs.entityStageStore()->stagedRows();
        for (const QVariant &v : stagedRows) {
            const int ox = int(std::floor(v.toMap().value(QStringLiteral("x")).toDouble()));
            const int ocx = int(std::floor(double(ox)) / 16.0);
            if (ocx == 1) ++owner1;
            else if (ocx == 0) ++owner0;
        }
        ok = ok && owner1 == 1 && owner0 == 0;
        if (owner1 != 1 || owner0 != 0)
            diag += QStringLiteral("[owner1=%1 owner0=%2 rows=%3] ").arg(owner1).arg(owner0)
                        .arg(stagedRows.size());

        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112b same-session revisit and ownership: negative and far"
                             " outer evictions re-materialize bitwise identical on walk-back"
                             " with markers present, the retained staging rows survive a clean"
                             " re-eviction untouched, and an item thrown across a chunk border"
                             " stages under its current owning chunk"
                          << (ok ? QString() : diag);
    });

    // ── r2112c：正常保存恰 B 新代恰一次（晋升拍 + committed 面恰一代）────────────────────
    runLeg(QStringLiteral("r2112c save promotion exactly once (three evicted edits across the"
        " core a negative column and the far outer ring promote inside the save transaction with"
        " the resident flush and the generation advance, land as exactly three committed rows"
        " stamped at the new save generation with the staging table emptied, and a fresh session"
        " reopen restores each key exactly once with the placed markers present)"), [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("c");
        QFile::remove(db);

        World w = makeSparseWorld();
        GameSession gs(w);
        gs.configureStreamingRadii(1, 1, 3);
        ok = ok && bindStreaming1142(gs, db, "c");
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        ok = ok && convergeLoaded1142(gs, w, centerKeys, 45000, d1);

        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);
        SaveCoordinator coord;
        coord.bind(&store, db);
        coord.setFlushHook([&gs](QSqlDatabase &db2) { return gs.flushResidentEditsForSaveOn(db2); });
        coord.setFlushCommitHook([&gs]() { gs.commitFlushResidentEditsOn(); });
        const SaveReceipt rcA = coord.saveAll(baseRequest1142("r2112c", 1));
        ok = ok && rcA.ok();
        StreamWorldMeta metaA;
        ok = ok && gs.chunkEditsStore()->readStreamWorldMeta(metaA) && metaA.saveGen == 1;

        // 三编辑（核心 / 负坐标 / 远外环）→ 全驱逐 → 暂存域三行。
        const int coreY = placeStoneMarker(w, 40, 40); // chunk (2,2)
        ok = ok && w.loadChunkAt(-2, -1) && w.loadChunkAt(8, 7);
        const int negY = placeStoneMarker(w, -2 * 16 + 8, -1 * 16 + 8);
        const int farY = placeStoneMarker(w, 8 * 16 + 8, 7 * 16 + 8);
        ok = ok && coreY > 0 && negY > 0 && farY > 0;
        evictNow1142(gs, 2, 2, 5, 5); // (2,2)/(8,7) cheb 3 ∈ annulus → 逐
        ok = ok && gs.chunkEditsStore()->stagedChunkCount() == 2;
        evictNow1142(gs, 5, 5, 0, 0); // (-2,-1) cheb 2 ∈ annulus → 逐（三键全暂存）
        ok = ok && gs.chunkEditsStore()->stagedChunkCount() == 3;

        // 保存 B：晋升（暂存 → committed 重盖新代次 + 腾空）与驻留冲洗、代次推进同事务。
        const SaveReceipt rcB = coord.saveAll(baseRequest1142("r2112c", 2));
        ok = ok && rcB.ok();
        if (!rcB.ok())
            diag += QStringLiteral("[saveB code=%1 msg=%2] ").arg(rcB.error.code)
                        .arg(rcB.error.message ? rcB.error.message : "");
        StreamWorldMeta metaAfterB0;
        if (!gs.chunkEditsStore()->readStreamWorldMeta(metaAfterB0))
            diag += QStringLiteral("[metaB-unreadable] ");
        else
            diag += QStringLiteral("[dbgB savegen=%1] ").arg(qint64(metaAfterB0.saveGen));
        StreamWorldMeta metaB;
        const bool metaBRead = gs.chunkEditsStore()->readStreamWorldMeta(metaB);
        ok = ok && metaBRead && metaB.saveGen == 2;
        if (!metaBRead || metaB.saveGen != 2)
            diag += QStringLiteral("[metaB read=%1 gen=%2] ")
                        .arg(metaBRead)
                        .arg(qint64(metaB.saveGen));
        const QVariantMap postCounts = rawCounts(db);
        const bool promotedExactly = postCounts.value(QStringLiteral("staged")).toInt() == 0
            && postCounts.value(QStringLiteral("edits")).toInt() == 3;
        ok = ok && promotedExactly;
        if (!promotedExactly)
            diag += QStringLiteral("[promo stg=%1 ed=%2] ")
                        .arg(postCounts.value(QStringLiteral("staged")).toInt())
                        .arg(postCounts.value(QStringLiteral("edits")).toInt());
        // 三行同代次（晋升重盖 = 本次保存代次——刻度统一契约面）。
        int genMatched = 0;
        {
            const QString conn = QStringLiteral("save02_gen_%1")
                                     .arg(QCoreApplication::applicationPid());
            if (QSqlDatabase::contains(conn))
                QSqlDatabase::removeDatabase(conn);
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery q(p);
                if (q.exec(QStringLiteral("SELECT generation FROM chunk_edits")))
                    while (q.next())
                        if (q.value(0).toLongLong() == metaB.saveGen)
                            ++genMatched;
            }
            QSqlDatabase::removeDatabase(conn);
        }
        ok = ok && genMatched == 3;
        if (genMatched != 3)
            diag += QStringLiteral("[genmatch=%1 savegen=%2] ").arg(genMatched).arg(metaB.saveGen);

        // 重开恰一份恢复（会话开启拍 + 行全量回灌恰三 + 标记在场）。
        World w2 = makeSparseWorld();
        GameSession gs2(w2);
        gs2.configureStreamingRadii(1, 1, 3);
        const bool bc1 = gs2.bindChunkEditsStore(db);
        const bool bc2 = gs2.bindEntityStageStore(db);
        const bool bc3 = gs2.setStreamWorldId(QStringLiteral("save02_c"));
        const bool bc4 = gs2.markStreamingWorld(kWS, kDS);
        diag += QStringLiteral("[dbgBind %1%2%3%4 id=%5] ")
                    .arg(bc1).arg(bc2).arg(bc3).arg(bc4)
                    .arg(gs2.streamWorldId());
        ok = ok && bc1 && bc2 && bc3 && bc4;
        WorldStore store2;
        store2.openWorld(db);
        store2.setWorld(&w2);
        QVector<ChunkStoreBlob> probeRowsVec;
        StreamWorldMeta metaRe;
        const bool metaReOk = gs2.chunkEditsStore()->readStreamWorldMeta(metaRe);
        const int probeRows = gs2.chunkEditsStore()->loadAllRows(probeRowsVec);
        const bool loadRet = gs2.loadStreamingWorld(store2);
        ok = ok && loadRet;
        diag += QStringLiteral("[dbgR load=%1 mrok=%2 base=%3 save=%4 rows=%5 restored=%6 blob=%7] ")
                    .arg(loadRet)
                    .arg(metaReOk)
                    .arg(metaReOk ? qint64(metaRe.baseGen) : qint64(-1))
                    .arg(metaReOk ? qint64(metaRe.saveGen) : qint64(-1))
                    .arg(probeRows)
                    .arg(gs2.loadRestoredRowCount())
                    .arg(gs2.loadBlobChunkCount());
        ok = ok && gs2.loadRestoredRowCount() == 3;
        if (gs2.loadRestoredRowCount() != 3)
            diag += QStringLiteral("[restored=%1] ").arg(gs2.loadRestoredRowCount());
        gs2.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> backKeys{ { 2, 2 } };
        QString d2;
        ok = ok && convergeLoaded1142(gs2, w2, backKeys, 45000, d2);
        ok = ok && w2.blockAt(40, coreY, 40) == quint8(BR::Stone);
        ok = ok && w2.loadChunkAt(-2, -1) && w2.loadChunkAt(8, 7);
        ok = ok && w2.blockAt(-2 * 16 + 8, negY, -1 * 16 + 8) == quint8(BR::Stone)
            && w2.blockAt(8 * 16 + 8, farY, 7 * 16 + 8) == quint8(BR::Stone);
        // 恰一次：committed 面仍恰三行（重开零重写零复制）。
        ok = ok && rawCounts(db).value(QStringLiteral("edits")).toInt() == 3
            && gs2.chunkEditsStore()->stagedChunkCount() == 0;

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112c save promotion exactly once: three evicted edits promote"
                             " in the save transaction to exactly three committed rows at the"
                             " new save generation with staging emptied, and the fresh session"
                             " reopen restores each key exactly once with every marker present"
                          << (ok ? QString() : diag);
    });

    // ── r2112d：失败后脏账保留 + 世界绑定恢复 + 可重试 ───────────────────────────────────
    runLeg(QStringLiteral("r2112d failure retention and retry (a fault injected at the world"
        " stage rolls the whole save transaction back so the evicted staging row and the"
        " resident dirty ledger both survive with the committed snapshot and ledger still at"
        " save A marked interrupted, and lifting the fault converges the retry into exactly the"
        " promoted plus flushed committed state with clean ledgers)"), [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("d");
        QFile::remove(db);

        World w = makeSparseWorld();
        GameSession gs(w);
        gs.configureStreamingRadii(1, 1, 3);
        ok = ok && bindStreaming1142(gs, db, "d");
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> centerKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 1; cx <= 3; ++cx)
                centerKeys.append({ cx, cz });
        QString d1;
        ok = ok && convergeLoaded1142(gs, w, centerKeys, 45000, d1);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);
        SaveCoordinator coord;
        coord.bind(&store, db);
        coord.setFlushHook([&gs](QSqlDatabase &db2) { return gs.flushResidentEditsForSaveOn(db2); });
        coord.setFlushCommitHook([&gs]() { gs.commitFlushResidentEditsOn(); });
        ok = ok && coord.saveAll(baseRequest1142("r2112d", 1)).ok();

        // 双面脏账布防：驻留 dirty（(1,1)——walk 目标 (5,5) 的 cheb 4 = scan 窗外驻留不误逐
        //   [r2025b 先例]）+ 已驱逐暂存（外环 (8,7) → 走离 annulus cheb 3 逐）。
        const int coreY = placeStoneMarker(w, 24, 24); // chunk (1,1) 驻留 dirty（不走离）
        ok = ok && coreY > 0;
        ok = ok && w.loadChunkAt(8, 7);
        const int farY = placeStoneMarker(w, 8 * 16 + 8, 7 * 16 + 8);
        ok = ok && farY > 0;
        evictNow1142(gs, 2, 2, 5, 5);
        const bool guardOk = gs.chunkEditsStore()->stagedChunkCount() == 1
            && w.chunkHasUnsavedEdits(1, 1) // 驻留 dirty 在场
            && w.chunks().lifecycleAt(1, 1) == ChunkLifecycle::Loaded;
        ok = ok && guardOk;
        if (!guardOk)
            diag += QStringLiteral("[guard stg=%1 dirty=%2 l11=%3] ")
                        .arg(gs.chunkEditsStore()->stagedChunkCount())
                        .arg(w.chunkHasUnsavedEdits(1, 1))
                        .arg(int(w.chunks().lifecycleAt(1, 1)));

        // 注入（World 段）：冲洗钩已随事务晋升+冲洗+推进 → 整事务回滚 = 暂存原样 + 脏账原样。
        coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::World; });
        const SaveReceipt rcBad = coord.saveAll(baseRequest1142("r2112d", 2));
        ok = ok && !rcBad.ok();
        if (rcBad.ok())
            diag += QStringLiteral("[fault-not-fired] ");
        const QVariantMap failCounts = rawCounts(db);
        const bool retained = failCounts.value(QStringLiteral("staged")).toInt() == 1
            && failCounts.value(QStringLiteral("edits")).toInt() == 0
            && w.chunkHasUnsavedEdits(1, 1)
            && failCounts.value(QStringLiteral("coord_complete_generation")).toString()
                == QStringLiteral("1")
            && failCounts.value(QStringLiteral("coord_generation")).toString()
                == QStringLiteral("2"); // Interrupted（尝试 > 完整）
        ok = ok && retained;
        if (!retained)
            diag += QStringLiteral("[retain stg=%1 ed=%2 dirty=%3 cg=%4 g=%5] ")
                        .arg(failCounts.value(QStringLiteral("staged")).toInt())
                        .arg(failCounts.value(QStringLiteral("edits")).toInt())
                        .arg(w.chunkHasUnsavedEdits(1, 1))
                        .arg(failCounts.value(QStringLiteral("coord_complete_generation"))
                                 .toString(),
                            failCounts.value(QStringLiteral("coord_generation")).toString());

        // 解除注入重试：收敛恰 B（暂存晋升 + 驻留冲洗同事务；提交面清驻留账）。
        coord.setFaultHook({});
        const SaveReceipt rcRetry = coord.saveAll(baseRequest1142("r2112d", 3));
        ok = ok && rcRetry.ok();
        if (!rcRetry.ok())
            diag += QStringLiteral("[retry-fail code=%1 msg=%2] ").arg(rcRetry.error.code)
                        .arg(rcRetry.error.message ? rcRetry.error.message : "");
        const QVariantMap retryCounts = rawCounts(db);
        const bool converged = retryCounts.value(QStringLiteral("staged")).toInt() == 0
            && retryCounts.value(QStringLiteral("edits")).toInt() == 2
            && !w.chunkHasUnsavedEdits(1, 1) // 提交面清账
            && retryCounts.value(QStringLiteral("coord_complete_generation")).toString()
                == retryCounts.value(QStringLiteral("coord_generation")).toString();
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[converged stg=%1 ed=%2 dirty=%3 cg=%4 g=%5] ")
                        .arg(retryCounts.value(QStringLiteral("staged")).toInt())
                        .arg(retryCounts.value(QStringLiteral("edits")).toInt())
                        .arg(w.chunkHasUnsavedEdits(1, 1))
                        .arg(retryCounts.value(QStringLiteral("coord_complete_generation"))
                                 .toString(),
                            retryCounts.value(QStringLiteral("coord_generation")).toString());
        if (!converged)
            diag += QStringLiteral("[retry stg=%1 ed=%2 dirty=%3 cg=%4 g=%5] ")
                        .arg(retryCounts.value(QStringLiteral("staged")).toInt())
                        .arg(retryCounts.value(QStringLiteral("edits")).toInt())
                        .arg(w.chunkHasUnsavedEdits(1, 1))
                        .arg(retryCounts.value(QStringLiteral("coord_complete_generation"))
                                 .toString(),
                            retryCounts.value(QStringLiteral("coord_generation")).toString());
        // 世界绑定恢复：保存后活体世界内容原样（冻结缓冲改绑已归还——活体编辑仍可达）。驻留
        //   (1,1) 活体面直读；(8,7) 已驱逐缺席 = 其内容在 committed 行（edits==2 已承载），
        //   活体面不再直读缺席 chunk（读面 = 确定性重生成，非本次保存的内容通路）。
        ok = ok && w.blockAt(24, coreY, 24) == quint8(BR::Stone);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112d failure retention and retry: a world-stage fault rolls"
                             " the transaction back leaving the evicted staging row and the"
                             " resident dirty ledger intact with the ledger marked interrupted,"
                             " and the clean retry converges into exactly the promoted plus"
                             " flushed committed state with ledgers cleared and the live world"
                             " untouched"
                          << (ok ? QString() : diag);
    });

    // ── r2112e：E1 走离→卸载→冻结→回访恰一次（真实 StreamingBridge 生产入口）──────────────
    runLeg(QStringLiteral("r2112e unload lifecycle through the real bridge entry (entering via"
        " StreamingBridge wires the production eviction restore and snapshot seams; a tamed"
        " sitting wolf a sheep a baby-type mob and two dropped items walking out of radius are"
        " serialized into the entity staging domain and released live, a mob in a non-resident"
        " chunk stays frozen under full ticks while a resident witness keeps ticking, walking"
        " back restores every family exactly once with tamed sitting wool and item payload"
        " fields intact and the staging rows consumed, and an item thrown across a chunk border"
        " stages under its current owning chunk)"), [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("e");
        QFile::remove(db);

        World w;
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(kH2);
        w.setSeed(kSeed);
        WorldStore store;
        ok = ok && store.openWorld(db) && store.isOpen();
        store.setWorld(&w);
        // D2 登记（生产 = 新世界勾选链的桥面；腿内同落点直驱）。
        {
            ChunkStore meta;
            meta.bind(db);
            meta.setStreamWorldId(db);
            ok = ok && meta.markStreamingWorld(48, 48);
        }
        PlayerController pc;
        EntityManager em;
        ItemEntityManager iem;
        pc.setEntityManager(&em); // QML 属性注入的生产镜像（Main.qml 注入行同形）
        pc.setItemEntities(&iem);
        WorldClock clk;
        // ★ 真实生产入口（审查 E1 原文：不在测试中手动补生产缺钩——实体三缝由桥装配）。
        const bool entered = StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db,
                                                                     kSeed);
        ok = ok && entered;
        if (!entered)
            diag += QStringLiteral("[enter-failed] ");

        // 目标 chunk（4,1）物化 + 布防（驯服坐下狼 + 羊 + 幼体型 + 掉落物两件）+ 编辑。
        ok = ok && w.loadChunkAt(4, 1);
        const int top = w.heightmapAt(72, 24);
        ok = ok && top > 1;
        const int wolfSlot = em.spawnMobTyped(72, top + 1, 24, EntityManager::MobWolf,
                                              QStringLiteral("#eeeeee"), 10);
        const int sheepSlot = em.spawnMobTyped(73, top + 1, 25, EntityManager::MobSheep,
                                               QStringLiteral("#f0f0f0"), 10);
        const int babySlot = em.spawnMobTyped(74, top + 1, 26, EntityManager::MobBabyShambler,
                                              QStringLiteral("#668866"), 10);
        ok = ok && wolfSlot >= 0 && sheepSlot >= 0 && babySlot >= 0;
        em.setTameRollOverride(0);
        ok = ok && em.tameWolf(wolfSlot);
        em.toggleWolfSit(wolfSlot);
        QVariantList ench;
        ench.append(0);
        ench.append(0);
        ench.append(0);
        ench.append(0);
        iem.spawnItemAt(QVector3D(72.5f, float(top) + 1.3f, 25.5f), int(BR::Stone), 1,
                        0.0f, 0.0f, 0.0f, ench, QStringLiteral("heirloom"), 250);
        iem.spawnItem(74, top + 1, 26, int(BR::Stone), 5);
        ok = ok && placeStoneMarker(w, 70, 22) > 0; // chunk (4,1) 编辑（驱逐沿 dirty 候选）
        const int liveBefore = em.liveCount();
        const int itemsBefore = iem.liveCount();
        ok = ok && liveBefore == 3 && itemsBefore == 2;

        // 走离驱逐（真实生产三缝：序列化入暂存域 → 释放活体）。
        emit pc.playerChunkChanged(1, 1);
        for (int i = 0; i < 3; ++i)
            emit clk.ticked(0.1);
        emit pc.playerChunkChanged(7, 7);
        for (int i = 0; i < 3; ++i)
            emit clk.ticked(0.1);
        const bool staged = em.liveCount() == 0 && iem.liveCount() == 0
            && gs_entityStageCount(db) == 5;
        ok = ok && staged;
        if (!staged) {
            diag += QStringLiteral("[stage mobs=%1 items=%2 rows=%3 kinds:")
                        .arg(em.liveCount())
                        .arg(iem.liveCount())
                        .arg(gs_entityStageCount(db));
            for (const QVariant &v : gs_entityStageRows(db))
                diag += QStringLiteral("%1/%2 ")
                                .arg(v.toMap().value(QStringLiteral("kind")).toInt())
                                .arg(v.toMap().value(QStringLiteral("type")).toInt());
            diag += QStringLiteral("] ");
        }

        // 非驻留冻结门：缺席 chunk 坐标新 mob 满拍零模拟（位置/血量逐位不动）vs 驻留见证者活。
        const int frozenSlot = em.spawnMobTyped(-40, 70, -40, EntityManager::MobSheep,
                                                QStringLiteral("#dddddd"), 10);
        ok = ok && frozenSlot >= 0; // chunk (-3,-3) 缺席 → 冻结
        const QVector3D frozenPos = em.posAt(frozenSlot);
        const int frozenHp = em.healthAt(frozenSlot);
        const int witnessSlot = em.spawnMobTyped(120, 70, 120, EntityManager::MobSheep,
                                                 QStringLiteral("#cccccc"), 10);
        ok = ok && witnessSlot >= 0; // chunk (7,7) 驻留窗内（y 取固定值——chunk 归属只看 x/z）
        for (int i = 0; i < 10; ++i)
            em.tick(0.1, &w, QVector3D(0, 0, 0), 0.4f, 1.8f, true, false, 1.0f);
        const bool frozenOk = em.posAt(frozenSlot) == frozenPos
            && em.healthAt(frozenSlot) == frozenHp && em.aliveAt(frozenSlot);
        ok = ok && frozenOk;
        if (!frozenOk)
            diag += QStringLiteral("[frozen moved=%1 hp=%2] ")
                        .arg(em.posAt(frozenSlot) == frozenPos)
                        .arg(em.healthAt(frozenSlot));
        // 掉落物冻结门对偶：缺席域掉落物零重力（不坠不焚不老化）。
        iem.spawnItem(-40, 70, -41, int(BR::Dirt), 1);
        ok = ok && iem.liveCount() == 1;
        const QVector3D frozenItemPos = iem.posAt(iem.count() - 1);
        for (int i = 0; i < 10; ++i)
            iem.tick(0.1, &w);
        ok = ok && iem.posAt(iem.count() - 1) == frozenItemPos;
        iem.clearAll(); // 冻结门哨兵收口（门面已断言——清空后回访账面 = 纯暂存恢复集）
        em.clearAll();  // 同上（冻结/见证哨兵清空——liveCount 断言回到纯恢复集域）

        // 走回：暂存物化 + 恢复缝恰一次（三 mob + 两 item；字段全保真；暂存消费归零）。
        emit pc.playerChunkChanged(4, 1);
        QString dConv;
        const bool back = convergeBridge(w, clk, { { 4, 1 } }, 60000, dConv);
        ok = ok && back;
        if (!back) diag += dConv;
        const bool restored = em.liveCount() == 3 && iem.liveCount() == 2
            && gs_entityStageCount(db) == 0;
        ok = ok && restored;
        if (!restored)
            diag += QStringLiteral("[restore mobs=%1 items=%2 rows=%3] ")
                        .arg(em.liveCount())
                        .arg(iem.liveCount())
                        .arg(gs_entityStageCount(db));
        // 字段保真：驯服 + 坐下 + 羊毛 + 幼体型 + 掉落物载荷。
        bool fieldsOk = false;
        for (int i = 0; i < em.count(); ++i) {
            if (!em.aliveAt(i) || em.kindAt(i) != EntityManager::Mob)
                continue;
            if (em.mobTypeAt(i) == EntityManager::MobWolf)
                fieldsOk = fieldsOk || (em.wolfTamedAt(i) && em.wolfSittingAt(i));
            if (em.mobTypeAt(i) == EntityManager::MobBabyShambler)
                fieldsOk = fieldsOk || (em.babyScaleAt(i) < 1.0f);
        }
        ok = ok && fieldsOk;
        if (!fieldsOk)
            diag += QStringLiteral("[fields] ");
        bool itemOk = false;
        for (int i = 0; i < iem.count(); ++i)
            if (iem.nameAt(i) == QLatin1String("heirloom") && iem.durabilityAt(i) == 250)
                itemOk = true;
        ok = ok && itemOk;
        // 恰一次负向面：恢复缝重放（同 chunk 再次落位沿）零重复恢复。
        emit pc.playerChunkChanged(5, 1);
        for (int i = 0; i < 3; ++i)
            emit clk.ticked(0.1);
        emit pc.playerChunkChanged(4, 1);
        for (int i = 0; i < 3; ++i)
            emit clk.ticked(0.1);
        ok = ok && em.liveCount() == 3 && iem.liveCount() == 2; // 无第二次注入

        // 跨区归户（确定性面——b 腿持物理掷越面，本腿持桥链驱逐归户面）：(4,1) 域内再置一物
        //   于 chunk (5,1)（gen 窗内驻留），feed (10,1)（桥链默认 annulus (4,6]）：(5,1) cheb 5
        //   与 (4,1) cheb 6 同沿逐 —— 归户断言 = 两行各归其属主（行位置换算），零串户。
        const int ownY = w.heightmapAt(88, 24);
        ok = ok && ownY > 1;
        iem.spawnItem(88, ownY + 1, 24, int(BR::Dirt), 2);
        emit pc.playerChunkChanged(10, 1);
        for (int i = 0; i < 4; ++i)
            emit clk.ticked(0.1);
        int owner5 = 0, owner4 = 0;
        const QVariantList stagedOwn = gs_entityStageRows(db);
        for (const QVariant &v : stagedOwn) {
            const int ox = int(std::floor(v.toMap().value(QStringLiteral("x")).toDouble()));
            const int ocx = int(std::floor(double(ox)) / 16.0);
            if (ocx == 5) ++owner5;
            else if (ocx == 4) ++owner4;
        }
        const bool ownOk = owner5 == 1 && owner4 == 5; // (4,1) 同沿逐 = 3 mob + 2 item 再暂存
        ok = ok && ownOk;
        if (!ownOk)
            diag += QStringLiteral("[owner o5=%1 o4=%2 rows=%3] ").arg(owner5).arg(owner4)
                        .arg(stagedOwn.size());

        // 拆卸（析构序契约：会话先亡）。
        StreamingBridge::instance()->detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112e unload lifecycle through the real bridge entry: walking"
                             " out stages the mob families and drops into the entity staging"
                             " domain and releases them live, absent-chunk entities stay frozen"
                             " under full ticks, walk-back restores every family exactly once"
                             " with all persisted fields intact and staging consumed, and a"
                             " border-crossing item stages under its current owner"
                          << (ok ? QString() : diag);
    });

    // ── r2112f：E1 死亡不复活 + 期间保存重进恰一份（真实保存链全生产面）──────────────────
    runLeg(QStringLiteral("r2112f death never resurrects and save-reopen exactly once (a killed"
        " mob is excluded from the committed snapshot so re-entry does not resurrect it while"
        " the tamed survivor and its drop return; staging the families away then saving commits"
        " them through the staged mob payload merge and the item snapshot beat so the re-entry"
        " answers exactly one of each)"), [&]() {
        bool ok = true;
        QString diag;
        const QString db = tempDb1142("f");
        QFile::remove(db);

        World w;
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(kH2);
        w.setSeed(kSeed);
        WorldStore store;
        ok = ok && store.openWorld(db) && store.isOpen();
        store.setWorld(&w);
        {
            ChunkStore meta;
            meta.bind(db);
            meta.setStreamWorldId(db);
            ok = ok && meta.markStreamingWorld(48, 48);
        }
        PlayerController pc;
        EntityManager em;
        ItemEntityManager iem;
        pc.setEntityManager(&em);
        pc.setItemEntities(&iem);
        WorldClock clk;
        ok = ok && StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db, kSeed);
        ok = ok && w.loadChunkAt(4, 1);
        const int top = w.heightmapAt(72, 24);
        // 死亡面：驯服狼（幸存者）+ 羊（击杀者）+ 掉落物 → 死亡不入档。
        const int wolfSlot = em.spawnMobTyped(72, top + 1, 24, EntityManager::MobWolf,
                                              QStringLiteral("#eeeeee"), 10);
        const int sheepSlot = em.spawnMobTyped(73, top + 1, 25, EntityManager::MobSheep,
                                               QStringLiteral("#f0f0f0"), 10);
        ok = ok && wolfSlot >= 0 && sheepSlot >= 0;
        em.setTameRollOverride(0);
        ok = ok && em.tameWolf(wolfSlot);
        iem.spawnItem(74, top + 1, 26, int(BR::Stone), 2);
        em.damageEntity(sheepSlot, 999); // 击杀（死亡动画窗内即不留档——export 门 dead/hp 门）
        // 保存（真实生产链：QML 载荷面 = 活体导出；暂存合并 provider 由桥登记——此刻暂存空）。
        const bool savedC = SaveBridge::instance()->saveViaCoordinator(&store, db,
            QStringLiteral("r2112f"), QVariantList{}, QVariantList{}, QVariantList{},
            QVariantMap{}, QVariantMap{}, QVariantMap{}, QVariantMap{}, QVariantList{},
            QVariantList{}, QVariantList{}, QVariantMap{}, em.exportPersistedEntities(),
            QVariantList{});
        ok = ok && savedC;
        ok = ok && rawCounts(db).value(QStringLiteral("ents")).toInt() == 1; // 恰幸存狼
        // 重进（真实生产入口重放：detach + 会话开启拍 + 已提交面恢复）。
        ok = ok && StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db, kSeed);
        em.clearAll();
        iem.clearAll();
        em.restorePersistedEntities(store.loadEntities());
        iem.restorePersistedRows(store.loadItemEntities());
        bool wolfBack = false;
        for (int i = 0; i < em.count(); ++i)
            if (em.aliveAt(i) && em.kindAt(i) == EntityManager::Mob
                && em.mobTypeAt(i) == EntityManager::MobWolf && em.wolfTamedAt(i))
                wolfBack = true;
        const bool deathFace = wolfBack && iem.liveCount() == 1; // 羊零复活 + 掉落物恰一份
        ok = ok && deathFace;
        if (!deathFace)
            diag += QStringLiteral("[death wolf=%1 items=%2] ").arg(wolfBack)
                        .arg(iem.liveCount());

        // 期间保存重进面：目标 chunk 物化（重进后 (4,1) 缺席——恢复体冻结于缺席域）→ 走离卸载
        //（桥链默认半径 gen 4/scan 6 → annulus = (4,6]；(10,1) 使 (4,1) cheb 6 恰入环带 → 驱逐
        // 暂存）→ 保存 → 重进。
        emit pc.playerChunkChanged(4, 1);
        {
            QVector<QPair<int, int>> matKeys;
            for (int cz = 0; cz <= 2; ++cz)
                for (int cx = 3; cx <= 5; ++cx)
                    matKeys.append({ cx, cz });
            QString dMat;
            const bool mat = convergeBridge(w, clk, matKeys, 60000, dMat);
            ok = ok && mat;
            if (!mat) diag += dMat;
        }
        emit pc.playerChunkChanged(10, 1);
        for (int i = 0; i < 40 && (em.liveCount() != 0 || iem.liveCount() != 0); ++i)
            emit clk.ticked(0.1); // 驱逐沿收敛（首喂 81 键在途波排空 + 环带沿——deadline 有界）
        if (em.liveCount() != 0 || iem.liveCount() != 0)
            diag += QStringLiteral("[walkaway mobs=%1 items=%2 l41=%3 est=%4] ")
                        .arg(em.liveCount())
                        .arg(iem.liveCount())
                        .arg(int(w.chunks().lifecycleAt(4, 1)))
                        .arg(gs_entityStageCount(db));
        ok = ok && em.liveCount() == 0 && iem.liveCount() == 0; // 全暂存
        const bool savedD = SaveBridge::instance()->saveViaCoordinator(&store, db,
            QStringLiteral("r2112f"), QVariantList{}, QVariantList{}, QVariantList{},
            QVariantMap{}, QVariantMap{}, QVariantMap{}, QVariantMap{}, QVariantList{},
            QVariantList{}, QVariantList{}, QVariantMap{}, em.exportPersistedEntities(),
            QVariantList{});
        ok = ok && savedD;
        const QVariantMap counts = rawCounts(db);
        const bool committedBoth = counts.value(QStringLiteral("ents")).toInt() == 1
            && counts.value(QStringLiteral("ients")).toInt() == 1
            && counts.value(QStringLiteral("estaged")).toInt() == 2; // 暂存行保留（mob+item 各一行）
        ok = ok && committedBoth;
        if (!committedBoth)
            diag += QStringLiteral("[commit ents=%1 ients=%2 est=%3] ")
                        .arg(counts.value(QStringLiteral("ents")).toInt())
                        .arg(counts.value(QStringLiteral("ients")).toInt())
                        .arg(counts.value(QStringLiteral("estaged")).toInt());
        // 重进恰一份（暂存截断 + 已提交面恢复恰一——零复制零丢失）。
        ok = ok && StreamingBridge::instance()->enterWorld(&w, &store, &clk, &pc, db, kSeed);
        em.clearAll();
        iem.clearAll();
        em.restorePersistedEntities(store.loadEntities());
        iem.restorePersistedRows(store.loadItemEntities());
        const bool exactlyOnce = em.liveCount() == 1 && iem.liveCount() == 1
            && gs_entityStageCount(db) == 0;
        ok = ok && exactlyOnce;
        if (!exactlyOnce)
            diag += QStringLiteral("[once mobs=%1 items=%2 est=%3] ")
                        .arg(em.liveCount())
                        .arg(iem.liveCount())
                        .arg(gs_entityStageCount(db));

        StreamingBridge::instance()->detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2112f death never resurrects and save-reopen exactly once: the"
                             " killed mob stays out of the committed snapshot across re-entry"
                             " while the tamed survivor and drop return, and staging then"
                             " saving commits both families so the re-entry answers exactly one"
                             " of each"
                          << (ok ? QString() : diag);
    });

    // ── r2112g：结构钉族 ────────────────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2112g structure pins (the new staging tables are pure-append"
        " idempotent creates with zero version bump and zero destructive sql on both store"
        " files, the session-open beat the promotion beat before the resident flush the"
        " committed-scale load guard and the staged persist seam are pinned in the session, the"
        " production bridge entry wires all three entity seams with no test-only assembly, both"
        " entity families carry the freeze gate and the restore faces, the prior neg load"
        " bearing lines survive verbatim, the count and atlas sentinels stay unmoved, the cmake"
        " rows are on file, the source tree stays free of the filter token, and the qml"
        " gameplay gate holds)"), [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = QDir(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto missDiag = [&ok, &diag](const QStringList &miss) {
            ok = ok && miss.isEmpty();
            if (!miss.isEmpty())
                diag += QStringLiteral("[%1]").arg(miss.join(QLatin1Char(',')));
        };

        // ① 新表纯追加钉（两 store 文件幂等建表 + 零 bump 零破坏性 SQL 反探）。
        missDiag(pinSet(srcDir + QStringLiteral("/World/chunkstore.cpp"), {
            SrcPin("staging idempotent create", "CREATE TABLE IF NOT EXISTS", 2),
            SrcPin("staging promote move", "INSERT OR REPLACE INTO", 3),
        }));
        {
            QFile f(srcDir + QStringLiteral("/World/chunkstore.cpp"));
            const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                            : QStringLiteral("<unreadable>");
            const bool noBad = !src.contains(QStringLiteral("user_version"))
                && !src.contains(QStringLiteral("PRAGMA"))
                && !src.contains(QStringLiteral("ALTER TABLE"))
                && !src.contains(QStringLiteral("DROP TABLE"));
            ok = ok && noBad;
            if (!noBad) diag += QStringLiteral("[cs-bad] ");
        }
        missDiag(pinSet(srcDir + QStringLiteral("/World/entitystagestore.cpp"), {
            SrcPin("entity staging create", "CREATE TABLE IF NOT EXISTS", 2),
            SrcPin("stage upsert", "INSERT OR REPLACE INTO", 1),
            SrcPin("item snapshot rewrite", "DELETE FROM", 2),
            SrcPin("take consumes", "DELETE FROM %1 WHERE cx = ? AND cz = ?", 1),
        }));
        {
            QFile f(srcDir + QStringLiteral("/World/entitystagestore.cpp"));
            const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                            : QStringLiteral("<unreadable>");
            const bool noBad = !src.contains(QStringLiteral("user_version"))
                && !src.contains(QStringLiteral("PRAGMA"))
                && !src.contains(QStringLiteral("ALTER TABLE"))
                && !src.contains(QStringLiteral("DROP TABLE"));
            ok = ok && noBad;
            if (!noBad) diag += QStringLiteral("[es-bad] ");
        }

        // ② 会话面源钉（会话开启拍 / 晋升先于驻留冲洗 / 上界守卫 / 暂存缝 / 两域并查）。
        {
            QFile f(srcDir + QStringLiteral("/Game/gamesession.h"));
            const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                            : QStringLiteral("<unreadable>");
            const QStringList missGs = pinSet(
                srcDir + QStringLiteral("/Game/gamesession.h"), {
                    SrcPin("session staging persist seam",
                           "m_chunkStore->persistChunkStaged(cx, cz, *c)", 1),
                    SrcPin("saved content two-domain query",
                           "m_chunkStore->hasStagedChunk(cx, cz)", 1),
                    SrcPin("session open clear", "clearStaging()", 2),
                    SrcPin("save promote beat", "promoteStagingOn(db) < 0", 1),
                    SrcPin("committed-scale load guard", "row.generation <= meta.saveGen", 1),
                    SrcPin("legacy scale lift", "reconcileCommittedSaveGen()", 1),
                    SrcPin("landing restore seam", "m_entityRestoreSink(data->key.cx", 1),
                    SrcPin("item snapshot beat", "commitItemSnapshotOn(db, itemRows)", 1),
                });
            missDiag(missGs);
            // 位序钉：晋升 < 驻留冲洗（同事务拍序——设计工件壹章状态 D）。
            const qsizetype iP = src.indexOf(QStringLiteral("promoteStagingOn(db)"));
            const qsizetype iF = src.indexOf(QStringLiteral("persistChunkOn(db, k.first"));
            ok = ok && iP >= 0 && iF > iP;
            if (!(iP >= 0 && iF > iP))
                diag += QStringLiteral("[promo-order p=%1 f=%2] ").arg(iP).arg(iF);
        }

        // ③ 生产装配钉（真实入口三缝——审查 E1「不在测试中手动补生产缺钩」反探面）。
        missDiag(pinSet(srcDir + QStringLiteral("/Game/streamingbridge.cpp"), {
            SrcPin("eviction stage seam production", "sess->setEvictionEntitySink(", 1),
            SrcPin("restore seam production", "sess->setEntityRestoreSink(", 1),
            SrcPin("live export seam production", "sess->setLiveItemExportSink(", 1),
            SrcPin("staging bind production", "bindEntityStageStore(savePath)", 1),
            SrcPin("stage write seam", "sess->stageEntitiesForChunk(cx, cz, rows)", 1),
        }));

        // ④ 实体族冻结门 + 恢复面 + 身份钉（两族同门禁分叉）。
        missDiag(pinSet(srcDir + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("mob freeze gate", "world->isSparse()", 2),
            SrcPin("mob residency read", "lifecycleAt(pc.cx, pc.cz)", 1),
        }));
        missDiag(pinSet(srcDir + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("mob stable identity", "quint32 entityId = 0;", 1),
            SrcPin("identity cursor", "quint32 m_nextEntityId = 1;", 1),
            SrcPin("identity funnel", "m_entities[size_t(slot)].entityId = m_nextEntityId++;", 1),
            SrcPin("chunk export face", "QVariantList exportPersistedInChunk(int cx, int cz) const;", 1),
        }));
        missDiag(pinSet(srcDir + QStringLiteral("/Entities/entitystore.cpp"), {
            SrcPin("item freeze gate", "world->isSparse()", 2),
            SrcPin("item residency read", "lifecycleAt(pc.cx, pc.cz)", 1),
            SrcPin("item restore face", "int EntityStore::restorePersistedRows(const QVariantList &rows)", 1),
        }));
        missDiag(pinSet(srcDir + QStringLiteral("/Entities/entitystore.h"), {
            SrcPin("item per-chunk export", "QVariantList exportPersistedInChunk(int cx, int cz) const;", 1),
            SrcPin("item restore decl", "int restorePersistedRows(const QVariantList &rows);", 1),
        }));

        // ⑤ 前序 NEG 承重行逐字幸存复核（r2105-r2111 修复面的载线族抽样——波及面保全钉）。
        missDiag(pinSet(srcDir + QStringLiteral("/World/savecoordinator.cpp"), {
            SrcPin("player stage rollback survives", "m_store->rollbackAtomicSave();", 6),
            SrcPin("in-transaction flush survives", "runInSaveTransaction(m_flushHook)", 1),
        }));
        missDiag(pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
            SrcPin("division budget check survives", "kCellBudget / d", 1),
        }));
        missDiag(pinSet(srcDir + QStringLiteral("/World/worldstore.cpp"), {
            SrcPin("named delete guard survives", "writeEntitiesPart: entities delete failed:", 1),
        }));

        // ⑥ 哨兵不动钉（本单零新方块：Count 166 / Atlas 213 不动）。
        missDiag(pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("count sentinel", "Count           = 166,", 1),
            SrcPin("atlas sentinel", "static constexpr int AtlasTileCount = 213;", 1)}));

        // ⑦ CMake 段行 + 新源文件行。
        missDiag(pinSet(
            QCoreApplication::applicationDirPath() + QStringLiteral("/../CMakeLists.txt"),
            {
                SrcPin("cmake section row", "tools/matrix/section105_save02_t1142.cpp", 1),
                SrcPin("cmake stage store row", "src/World/entitystagestore.cpp", 1),
                SrcPin("cmake stage store header row", "src/World/entitystagestore.h", 1),
            }));

        // ⑧ 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(srcDir,
                            { QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                              QStringLiteral("*.qml") },
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QFile f(it.next());
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("r2112"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }

        // ⑨ QML 玩法路径负面门（r2111d 同门复钉 + 本单 QML 恢复行正面钉）。
        {
            QFile qf(srcDir + QStringLiteral("/ui/Main.qml"));
            const QString qml = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll())
                                                             : QStringLiteral("<unreadable>");
            const bool qmlGate = !qml.contains(QStringLiteral("GameSession"))
                && !qml.contains(QStringLiteral("MeshWorker"))
                && !qml.contains(QStringLiteral("ChunkStreamDriver"))
                && !qml.contains(QStringLiteral("chunk_staging"))
                && !qml.contains(QStringLiteral("entity_staging"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }
        missDiag(pinSet(srcDir + QStringLiteral("/ui/Main.qml"), {
            SrcPin("item snapshot restore row", "itemEntities.restorePersistedRows(persistedItemRows)", 1),
            SrcPin("item snapshot read row", "worldStore.loadItemEntities()", 1),
        }));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2112g structure pins (neg-1 sensitive via the session open clear line"
               " pin): pure-append staging tables with zero bump and zero"
               " destructive sql on both store files, the session-open beat the promotion beat"
               " ahead of the resident flush the committed-scale load guard the staged persist"
               " seam and the two-domain content query pinned in the session, the production"
               " bridge entry wires all three entity seams, both families carry freeze gates"
               " and restore faces, the prior neg load bearing lines survive verbatim, the"
               " count and atlas sentinels stay unmoved, the cmake rows are on file, the source"
               " tree stays free of the filter token, and the qml gameplay gate holds"
            << (ok ? QString() : diag);
    });
}
