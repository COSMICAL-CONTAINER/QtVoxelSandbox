#include "matrix_helpers.h"

#include "chunklifecycle.h"       // 被测面：六态读（回灌/弃槽沿断言）
#include "chunkstore.h"           // 被测面：chunk_edits + stream_worlds（D2/D3 元数据域）
#include "gamesession.h"          // 被测面：保存时冲洗 + 读档 overlay 合并 + 转换面（W5 宿主）
#include "worldstore.h"           // 被测面：blob 通路只读 Q_INVOKABLE 消费（hasChunks/loadChunks）

#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// §29.5-W5 后端 流式世界持久化 + D2/D3 标志 + overlay 合并 探针段（4 腿；filter 词 r2027；
// 矩阵 644→644+N）。置尾先例沿用（接 section29，runAll 末执行）。任务契约
// （refactor-plan §29.5.2 W5 后端半 + §29.5.3 选型 2；UI = W5b 例外面，本段 QML 零触碰）：
//   「fixed 零活动墙」→ r2027a（fixed 世界保存/读档全语义逐位不动：三写链 + beginLoad/
//     loadChunks/finishLoad 往返逐位恒等 + 元数据域 API 全 fail-safe false + 库面零流式表）；
//   「流式存读往返承重墙」→ r2027b（sparse 世界 W2 生成 + 三区玩家编辑 → 保存含冲洗 →
//     销毁重建 → 走查同区内容逐位恒等[编辑块=表回灌/未编辑块=seed 重生成含树/读档后追加
//     生成走 W2 路径] → 再保存再读恒等[二次幂等]）；
//   「D3 转换往返」→ r2027c（fixed 世界+三编辑 → saveAll blob → 标志翻转 → 生产同构首进
//     [blob 物化逐位] → 流式编辑 + 冲洗 → 再读档[仲裁=代次对代次：编辑区 overlay 胜/无行区
//     blob 逐位/核心外按需生成含 population/老行让位 blob/同代=表胜] → 标志翻回=fixed 读档
//     照旧 + chunks 表零触碰）；
//   「结构钉」→ r2027d（worldstore 禁触反探[新词元族] / additive 表正面钉[stream_worlds DDL
//     + 元数据 upsert + 零 bump] / 冲洗失败不谎报钉[真锁注入 → false + 账原样 + 零部分冲洗 +
//     解锁收敛] / QML 零触碰反探 / 会话缝面正面钉[冲洗枚举 + 代次先拍 + blob 先于行回灌]）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘保存时冲洗（gamesession.h flushResidentEditsForSave 的驻留 dirty 落盘
//     for 循环 if (false) 前缀——代次推进拍保留）→ 声明红面 {r2027b, r2027c}[b：flush 谎报
//     true 而行未写 → 销毁重建后编辑块走重derive 丢失 = 内容恒等柱红；c：流式时代零行 →
//     再读档仲裁无行可胜 → 编辑区 overlay 柱红]。a 不误伤（fixed 无冲洗域可达）；d 不误伤
//   （其冲洗面只钉失败侧：真锁注入在代次推进拍即 false——被摘循环不可达；b 的成功面账清
//     断言与内容柱同柱同源，不扩面）。
//   变异二（NEG-2）= 摘代次仲裁改旧胜（loadStreamingRead 仲裁行 rhs 置 false——行恒负于
//     blob）→ 声明红面 {r2027c}[有 blob 对手方时行恒败：编辑区 overlay 柱红 = 被摘语义本体]。
//     b 不误伤（原生流式无 blob = 无对手方分支，行恒胜路径不经被变异 rhs）；a/d 不误伤
//   （fixed 无读档合并可达；钉面不锚被变异字面）。
//   阴性日志：build/ 下四件 matrix_r2027_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 时长控制：sparse 走查腿 spawnPreGenerateRadius=0 + 半径压小（(1,1,3)/(1,1,2)）；收敛轮询
//   deadline 有界（防 flake 不挂死）；临时库 fresh + 用后即删（QDir::temp() pid 键名，绝不触
//   saves/——openWorld 绝对路径先例 = r2015 段同门）。
void MatrixRun::section30_streaming_persistence()
{
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42;  // sparse 核心域（W2/W3 段同族）
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk，section11 同族）

    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0; // 零预生成（中心 chunk 除外——W1 语义）：流式链全暴露
        return World(sp);
    };
    const auto makeSparseCore = [&](int coreW, int coreD) {
        World::SparseWorldParams sp;
        sp.seed = kSeedF;
        sp.coreWidth = coreW;
        sp.coreDepth = coreD;
        sp.height = kHF;
        sp.spawnPreGenerateRadius = 0;
        return World(sp);
    };
    // fixed 小世界（setter incantation；World 为 QObject 派生不可拷贝/移动 → 配置就地完成，
    // 调用点 `World w; makeFixed(w);` 形态——section11 同族 incantation）。
    const auto makeFixed = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);
    };
    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2015 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2027_%1_%2.sqlite")
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
    // 全 chunk 体素快照/比对（16×16 列 × 全 y 带 id+state——往返逐位恒等柱的承载面）。
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
        const int h = kH;
        int i = 0;
        int diffs = 0;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < h; ++y, ++i) {
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
    // 地表标记放置（选址纪律：**体素真实顶** = heightmapAt[自顶向下首个非空气]——World::
    //   heightAt 是生成器纯噪声面（海洋列与实际体素顶脱节，r2027 首跑探针实证），不可作
    //   放置判据；放置回读校验 + 植被冠层列整列换扫[偏移 ≤2 且基列距 chunk 边 ≥2 → 标记
    //   恒落原选 chunk 内]）。
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
    // 栅格指纹（blockAt+stateAt 全栅格——fixed 读档往返逐位恒等的比较面，r2015 段同款）。
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
    // 库面表名清单（raw 只读连接——fixed 零流式表 / chunks 表行数核查面）。
    const auto tableNames = [](const QString &db, QString &diag) -> QStringList {
        QStringList out;
        const QString conn = QStringLiteral("r2027_probe_%1").arg(QCoreApplication::applicationPid());
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        {
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (!p.open()) {
                diag += QStringLiteral("[probe-open %1] ").arg(p.lastError().text());
            } else {
                QSqlQuery q(p);
                if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'")))
                    while (q.next())
                        out << q.value(0).toString();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    };

    // ── r2027a：fixed 零活动墙（保存/读档全语义逐位不动 + 元数据域 fail-safe 全 false + 库面
    //    零流式表）────────────────────────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2027a fixed inert wall (a fixed world persist-and-load chain stays"
        " bitwise untouched end to end: the session exposes no streaming metadata face at all"
        " with every mark, clear, flush and overlay-load call failing safe false, the"
        " three-write persistence chain succeeds with the ok counter at three, a fresh fixed"
        " world brought back through begin-load, chunk load and finish-load matches the captured grid"
        " bitwise, and the raw database carries no streaming flag table and no per-chunk edit"
        " table of any kind)"), [&]() {
        bool ok = true;
        QString diag;

        World w;
        makeFixed(w);
        GameSession gs(w);
        // 元数据域 fail-safe 面（fixed 世界连 ChunkStore 都不构造 = D2 零活动墙最强形态）。
        const bool inertOk = !gs.streamingActive() && gs.chunkEditsStore() == nullptr
            && !gs.isStreamingWorld() && !gs.markStreamingWorld(kWF, kDF)
            && !gs.clearStreamingWorldFlag() && !gs.flushResidentEditsForSave();
        ok = ok && inertOk;
        if (!inertOk)
            diag += QStringLiteral("[inert active=%1 store=%2 stream=%3 mark=%4 clear=%5 flush=%6] ")
                        .arg(gs.streamingActive())
                        .arg(gs.chunkEditsStore() != nullptr)
                        .arg(gs.isStreamingWorld())
                        .arg(gs.markStreamingWorld(kWF, kDF))
                        .arg(gs.clearStreamingWorldFlag())
                        .arg(gs.flushResidentEditsForSave());
        // 读档合并 fail-safe：未 bind store 的 world() 恒 null ≠ 本世界 → false（误绑防御同门）。
        WorldStore strayStore;
        const bool loadRejected = !gs.loadStreamingWorld(strayStore);
        ok = ok && loadRejected;
        if (!loadRejected)
            diag += QStringLiteral("[load-not-rejected] ");

        // 三写链（保存链生产形态：saveAll + savePlayerData + saveProgress 同步三写）。
        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);
        const EditSite mark = placeTop(w, 20, 24);
        const bool editOk = mark.ok && w.blockAt(mark.x, mark.y, mark.z) == quint8(BR::Stone);
        ok = ok && editOk;
        if (!editOk)
            diag += QStringLiteral("[edit y=%1] ").arg(mark.y);
        const QByteArray gridFixed = gridOf(w);
        QVariantMap pd;
        pd.insert(QStringLiteral("x"), 8.5);
        QVariantMap pr;
        pr.insert(QStringLiteral("stat"), 1);
        const bool saveAllOk = store.saveAll(QStringLiteral("r2027a"));
        const bool pdOk = store.savePlayerData(pd);
        const bool prOk = store.saveProgress(pr);
        ok = ok && saveAllOk && pdOk && prOk && store.saveOkCount() == 3;
        if (!saveAllOk || !pdOk || !prOk || store.saveOkCount() != 3)
            diag += QStringLiteral("[three-write a=%1 p=%2 g=%3 ok=%4] ")
                        .arg(saveAllOk).arg(pdOk).arg(prOk).arg(store.saveOkCount());

        // 读档链（t176 生产形态：beginLoad → loadChunks → finishLoad）→ 栅格逐位恒等。
        World w2;
        makeFixed(w2);
        w2.beginLoad(kSeedF);
        store.setWorld(&w2); // r2010d rebind 纪律（loadChunks 写入面 = m_world）
        const int loaded = store.loadChunks();
        w2.finishLoad();
        const QByteArray gridLoaded = gridOf(w2);
        const bool roundtripOk = loaded == 9 && gridLoaded == gridFixed;
        ok = ok && roundtripOk;
        if (!roundtripOk) {
            qsizetype delta = -1;
            for (qsizetype i = 0; i < gridFixed.size(); ++i)
                if (gridFixed[i] != gridLoaded[i]) {
                    delta = i;
                    break;
                }
            diag += QStringLiteral("[roundtrip loaded=%1 delta=%2] ").arg(loaded).arg(qint64(delta));
        }

        // 库面零流式表：fixed 世界全链后，库中无流式标志表、无 per-chunk 编辑表、无台账表。
        QString probeDiag;
        const QStringList tables = tableNames(db, probeDiag);
        const bool noStreamingTables = !tables.contains(QStringLiteral("stream_worlds"))
            && !tables.contains(QStringLiteral("chunk_edits"))
            && !tables.contains(QStringLiteral("save_coord"));
        ok = ok && noStreamingTables && tables.contains(QStringLiteral("chunks"))
            && tables.contains(QStringLiteral("world_meta"));
        if (!noStreamingTables)
            diag += QStringLiteral("[tables %1] ").arg(tables.join(QLatin1Char(','))) + probeDiag;

        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2027a fixed inert wall: the fixed world save and load chain"
                             " stays bitwise untouched end to end, the session exposes no"
                             " streaming metadata face with every mark, clear, flush and"
                             " overlay-load call failing safe false, the three-write"
                             " persistence chain succeeds with the ok counter at three, the"
                             " restored fixed world matches the captured grid bitwise, and the"
                             " raw database carries no streaming flag table and no per-chunk"
                             " edit table of any kind" << (ok ? QString() : diag);
    });

    // ── r2027b：流式存读往返承重墙（真临时库 fresh + 用后即删）────────────────────────────
    // sparse 世界 W2 生成 + 三区玩家编辑 → 保存（含冲洗，行代次 = 保存代次）→ 销毁重建 →
    // 走查同区内容逐位恒等（编辑块=表回灌/未编辑块=seed 重生成含树）→ 读档后追加生成走 W2
    // 路径 → 再保存再读恒等（二次幂等，行代次推进可观测）。
    runLeg(QStringLiteral("r2027b streaming flush-reload roundtrip load-bearing wall (a sparse"
        " world driven by the real generation chain takes player edits in three chunks, the"
        " flush that fronts the persist chain writes exactly the dirty residents under an"
        " advanced persist generation while generated-but-unedited terrain stays out of"
        " the table, after session"
        " destruction and rebuild the same regions match bitwise - edited chunks replay from"
        " table rows, unedited chunks regenerate from seed with population"
        " trees intact - a chunk generated after the reload arrives through the streaming"
        " worker path, a second edit-flush-reload cycle keeps every region bitwise identical"
        " with the row generation visibly advanced)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("b");
        QFile::remove(db); // fresh

        // ── 会话 1：W2 生成 + 三区编辑 + 保存（含冲洗）────────────────────────────────────
        QVector<QPair<int, int>> snapE22, snapE32, snapE11, snapW12, snapW23, snapE44;
        int leavesSignature = 0;
        qint64 metaSaveGen1 = -1;
        {
            World w1 = makeSparse();
            GameSession g1(w1);
            g1.configureStreamingRadii(1, 1, 3); // gen 窗 3×3（r2025b 同族）
            const bool boundOk = g1.bindChunkEditsStore(db) && g1.chunkEditsStore() != nullptr
                && g1.setStreamWorldId(QStringLiteral("r2027b"));
            ok = ok && boundOk;
            if (!boundOk)
                diag += QStringLiteral("[bind] ");
            const bool markOk = g1.markStreamingWorld(kWS, kDS) && g1.isStreamingWorld();
            ok = ok && markOk;
            if (!markOk)
                diag += QStringLiteral("[mark] ");
            StreamWorldMeta meta0;
            const bool meta0Ok = g1.chunkEditsStore()->readStreamWorldMeta(meta0)
                && meta0.streaming && meta0.coreW == kWS && meta0.coreD == kDS
                && meta0.baseGen == 0 && meta0.saveGen == 0;
            ok = ok && meta0Ok;
            if (!meta0Ok)
                diag += QStringLiteral("[meta0 s=%1 w=%2 d=%3 b=%4 g=%5] ")
                            .arg(meta0.streaming).arg(meta0.coreW).arg(meta0.coreD)
                            .arg(meta0.baseGen).arg(meta0.saveGen);

            // ① 初载：玩家落 (2,2) → 3×3 窗全 Loaded（中心 (2,2) 构造期已预生成）。
            g1.notePlayerChunk(2, 2);
            QVector<QPair<int, int>> centerKeys;
            for (int cz = 1; cz <= 3; ++cz)
                for (int cx = 1; cx <= 3; ++cx)
                    centerKeys.append({ cx, cz });
            QString d1;
            const bool c1 = convergeLoaded(g1, w1, centerKeys, 45000, d1);
            ok = ok && c1;
            if (!c1)
                diag += d1;

            // ② 三区编辑（含构造期预生成的中心 chunk (2,2)——回灌弃槽路径的承载选址）。
            const EditSite s22 = placeTop(w1, 36, 36); // chunk (2,2)
            const EditSite s32 = placeTop(w1, 56, 44); // chunk (3,2)
            const EditSite s11 = placeTop(w1, 20, 20); // chunk (1,1)
            const bool editsOk = s22.ok && s32.ok && s11.ok
                && w1.chunkHasUnsavedEdits(2, 2) && w1.chunkHasUnsavedEdits(3, 2)
                && w1.chunkHasUnsavedEdits(1, 1) && !w1.chunkHasUnsavedEdits(1, 2)
                && !w1.chunkHasUnsavedEdits(2, 3);
            ok = ok && editsOk;
            if (!editsOk)
                diag += QStringLiteral("[edits s22=%1 s32=%2 s11=%3 d22=%4 d12=%5] ")
                            .arg(s22.ok).arg(s32.ok).arg(s11.ok)
                            .arg(w1.chunkHasUnsavedEdits(2, 2))
                            .arg(w1.chunkHasUnsavedEdits(1, 2));

            // ③ 快照：编辑三 chunk + 未编辑见证 (1,2)/(2,3)（population 树叶签名累计）。
            snapE22 = snapChunk(w1, 2, 2);
            snapE32 = snapChunk(w1, 3, 2);
            snapE11 = snapChunk(w1, 1, 1);
            snapW12 = snapChunk(w1, 1, 2);
            snapW23 = snapChunk(w1, 2, 3);
            for (const auto *snap : { &snapE22, &snapE32, &snapE11, &snapW12, &snapW23 })
                for (const auto &v : *snap)
                    if (v.first == BR::Leaves)
                        ++leavesSignature;

            // ④ 保存 = 冲洗（保存链前置挂点生产面）：恰 3 个 dirty 驻留落盘，行代次 = 保存代次。
            const bool flushed = g1.flushResidentEditsForSave();
            ChunkStoreBlob blob22;
            StreamWorldMeta meta1;
            const bool flushOk = flushed && g1.flushPersistedCount() == 3
                && g1.flushFailedCount() == 0 && g1.chunkEditsStore()->chunkCount() == 3
                && g1.chunkEditsStore()->loadChunk(2, 2, blob22) && blob22.generation == 1
                && g1.chunkEditsStore()->readStreamWorldMeta(meta1) && meta1.saveGen == 1
                && !w1.chunkHasUnsavedEdits(2, 2) && !w1.chunkHasUnsavedEdits(3, 2)
                && !w1.chunkHasUnsavedEdits(1, 1);
            ok = ok && flushOk;
            if (!flushOk)
                diag += QStringLiteral("[flush ok=%1 p=%2 f=%3 rows=%4 gen=%5 sg=%6 d22=%7] ")
                            .arg(flushed).arg(g1.flushPersistedCount()).arg(g1.flushFailedCount())
                            .arg(g1.chunkEditsStore()->chunkCount())
                            .arg(qint64(blob22.generation)).arg(meta1.saveGen)
                            .arg(w1.chunkHasUnsavedEdits(2, 2));
            metaSaveGen1 = meta1.saveGen;
        } // 会话 1 销毁（世界 + 会话 + store 全部析构——跨会话持久化由库承载）

        // ── 会话 2：销毁重建 → 读档合并 → 走查同区逐位恒等 + 追加生成走 W2 路径 ────────────
        {
            World w2 = makeSparse();
            GameSession g2(w2);
            g2.configureStreamingRadii(1, 1, 3);
            // 绑定无条件执行（不与 ok 链短接——setup 面独立于断言面，r2027 首跑教训）。
            const bool bound2 = g2.bindChunkEditsStore(db)
                && g2.setStreamWorldId(QStringLiteral("r2027b"));
            WorldStore st2;
            const bool opened2 = st2.openWorld(db);
            st2.setWorld(&w2); // r2010d rebind 纪律
            ok = ok && bound2 && opened2;
            if (!bound2 || !opened2)
                diag += QStringLiteral("[setup2 bound=%1 open=%2] ").arg(bound2).arg(opened2);

            const bool entered2 = g2.loadStreamingWorld(st2);
            const int restored2 = g2.loadRestoredRowCount();
            const bool loadOk = entered2 && !st2.hasChunks() && restored2 == 3
                && g2.loadBlobChunkCount() == 0;
            ok = ok && loadOk;
            if (!loadOk)
                diag += QStringLiteral("[load ok=%1 has=%2 rows=%3 blob=%4] ")
                            .arg(entered2).arg(st2.hasChunks()).arg(restored2)
                            .arg(g2.loadBlobChunkCount());

            // 未编辑块先经走查驱动重生成（读档只回灌行——未编辑块按需生成是语义本位），
            // 再作逐位比对。玩家回 (2,2) → 3×3 窗收敛（(1,2)/(2,3) 随窗物化）。
            g2.notePlayerChunk(2, 2);
            QVector<QPair<int, int>> regenKeys;
            for (int cz = 1; cz <= 3; ++cz)
                for (int cx = 1; cx <= 3; ++cx)
                    regenKeys.append({ cx, cz });
            QString dRegen;
            const bool cRegen = convergeLoaded(g2, w2, regenKeys, 60000, dRegen);
            ok = ok && cRegen;
            if (!cRegen)
                diag += dRegen;

            // 编辑块 = 表回灌逐位恒等（全 16×16×H； population 终态在列 = 跳 population 面）。
            long eq = 0;
            QString dd;
            const bool id22 = chunkIdentical(w2, 2, 2, snapE22, eq, dd);
            const bool id32 = chunkIdentical(w2, 3, 2, snapE32, eq, dd);
            const bool id11 = chunkIdentical(w2, 1, 1, snapE11, eq, dd);
            ok = ok && id22 && id32 && id11;
            if (!id22 || !id32 || !id11)
                diag += QStringLiteral("[edited 22=%1 32=%2 11=%3 eq=%4 %5] ")
                            .arg(id22).arg(id32).arg(id11).arg(eq).arg(dd);

            // 未编辑块 = seed 确定性重生成逐位恒等（sparse↔sparse 同路径同 seed）+ 树签名非空转。
            long eqW = 0;
            const bool idW12 = chunkIdentical(w2, 1, 2, snapW12, eqW, dd);
            const bool idW23 = chunkIdentical(w2, 2, 3, snapW23, eqW, dd);
            ok = ok && idW12 && idW23 && leavesSignature >= 8;
            if (!idW12 || !idW23 || leavesSignature < 8)
                diag += QStringLiteral("[witness 12=%1 23=%2 eq=%3 leaves=%4 %5] ")
                            .arg(idW12).arg(idW23).arg(eqW).arg(leavesSignature).arg(dd);

            // 读档后追加生成走 W2 路径：玩家 (4,4) → 驱动器真实物化 (4,4) 到 Loaded。
            const int revBefore = w2.residentChunkRevision();
            g2.notePlayerChunk(4, 4);
            QVector<QPair<int, int>> farKeys{ { 4, 4 } };
            QString d2;
            const bool c2 = convergeLoaded(g2, w2, farKeys, 45000, d2);
            const bool driverOk = c2
                && w2.chunks().lifecycleAt(4, 4) == ChunkLifecycle::Loaded
                && w2.residentChunkRevision() > revBefore;
            ok = ok && driverOk;
            if (!driverOk)
                diag += QStringLiteral("[driver c=%1 l44=%2 rev+%3] %4")
                            .arg(c2)
                            .arg(int(w2.chunks().lifecycleAt(4, 4)))
                            .arg(w2.residentChunkRevision() - revBefore)
                            .arg(d2);

            // ── 二次幂等：再编辑 → 再保存（行代次推进）→ 再销毁重建 → 再读恒等 ─────────────
            const EditSite s44 = placeTop(w2, 68, 68); // chunk (4,4)
            const bool edit2Ok = s44.ok;
            ok = ok && edit2Ok;
            if (!edit2Ok)
                diag += QStringLiteral("[edit2 s44=%1] ").arg(s44.ok);
            snapE44 = snapChunk(w2, 4, 4);
            const bool flushed2 = g2.flushResidentEditsForSave();
            ChunkStoreBlob blob44;
            StreamWorldMeta meta2;
            const bool flush2Ok = flushed2 && g2.flushPersistedCount() == 1
                && g2.chunkEditsStore()->chunkCount() == 4
                && g2.chunkEditsStore()->loadChunk(4, 4, blob44) && blob44.generation == 2
                && g2.chunkEditsStore()->readStreamWorldMeta(meta2) && meta2.saveGen == 2
                && metaSaveGen1 == 1;
            ok = ok && flush2Ok;
            if (!flush2Ok)
                diag += QStringLiteral("[flush2 ok=%1 p=%2 rows=%3 gen=%4 sg=%5 sg1=%6] ")
                            .arg(flushed2).arg(g2.flushPersistedCount())
                            .arg(g2.chunkEditsStore()->chunkCount())
                            .arg(qint64(blob44.generation)).arg(meta2.saveGen).arg(metaSaveGen1);
        }

        // ── 会话 3：二次读档 → 四区内容逐位恒等（gen-1 三行 + gen-2 一行）──────────────────
        {
            World w3 = makeSparse();
            GameSession g3(w3);
            g3.configureStreamingRadii(1, 1, 3);
            const bool bound3 = g3.bindChunkEditsStore(db)
                && g3.setStreamWorldId(QStringLiteral("r2027b"));
            WorldStore st3;
            const bool opened3 = st3.openWorld(db);
            st3.setWorld(&w3);
            ok = ok && bound3 && opened3;
            if (!bound3 || !opened3)
                diag += QStringLiteral("[setup3 bound=%1 open=%2] ").arg(bound3).arg(opened3);
            const bool entered3 = g3.loadStreamingWorld(st3);
            const int restored3 = g3.loadRestoredRowCount();
            const bool load3Ok = entered3 && restored3 == 4;
            ok = ok && load3Ok;
            if (!load3Ok)
                diag += QStringLiteral("[load3 ok=%1 rows=%2] ").arg(entered3).arg(restored3);
            long eq3 = 0;
            QString dd3;
            // 逐位恒等四区（二次幂等柱）：gen-1 三行内容不因二次保存漂移 + gen-2 行精确回灌。
            const bool id22 = chunkIdentical(w3, 2, 2, snapE22, eq3, dd3);
            const bool id32 = chunkIdentical(w3, 3, 2, snapE32, eq3, dd3);
            const bool id11 = chunkIdentical(w3, 1, 1, snapE11, eq3, dd3);
            const bool id44 = chunkIdentical(w3, 4, 4, snapE44, eq3, dd3);
            ok = ok && id22 && id32 && id11 && id44;
            if (!id22 || !id32 || !id11 || !id44)
                diag += QStringLiteral("[idempotent 22=%1 32=%2 11=%3 44=%4 eq=%5 %6] ")
                            .arg(id22).arg(id32).arg(id11).arg(id44).arg(eq3).arg(dd3);
        }

        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2027b streaming flush-reload roundtrip: the flush that fronts the"
                             " persist chain writes exactly the dirty residents under an advanced"
                             " persist generation, rebuilt sessions replay the edited chunks"
                             " bitwise from table rows and regenerate the unedited ones bitwise"
                             " from seed with population trees intact, post-reload generation"
                             " arrives through the streaming worker path, and a second"
                             " edit-flush-reload cycle keeps every region bitwise identical"
                             " with the row generation visibly advanced"
                          << (ok ? QString() : diag);
    });

    // ── r2027c：D3 转换往返（fixed blob → 标志翻转 → overlay 合并读档 → 逆翻照旧）─────────
    runLeg(QStringLiteral("r2027c D3 conversion roundtrip (a fixed world with edits in three"
        " chunks is written through the production blob path, the additive flag flips it to"
        " streaming with zero data movement, the first production-shaped entry materializes"
        " every core chunk bitwise from the blob through the read-only invokable face, a"
        " streaming-era edit flushed into the table wins the arbitration on the next entry so"
        " the edited chunk carries both the fixed-era and the streaming edit bitwise while"
        " unedited core chunks keep the fixed-era content and an outside-core chunk generates"
        " on demand through the streaming path with population intact, an older row loses to"
        " the blob once the anchor generation is raised past it while an equal generation keeps"
        " the table win, and flipping the flag back brings the fixed load chain back bitwise with"
        " the blob table untouched)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        WorldStore store;
        ok = ok && store.openWorld(db);

        // ── fixed 时代：三 chunk 编辑 → 生产 blob 保存 ────────────────────────────────────
        World wf;
        makeFixed(wf);
        store.setWorld(&wf);
        const EditSite s00 = placeTop(wf, 5, 7);   // chunk (0,0)
        const EditSite s11f = placeTop(wf, 20, 24); // chunk (1,1)
        const EditSite s22f = placeTop(wf, 44, 40); // chunk (2,2)
        ok = ok && s00.ok && s11f.ok && s22f.ok;
        const QVector<QPair<int, int>> snapF00 = snapChunk(wf, 0, 0);
        const QVector<QPair<int, int>> snapF11 = snapChunk(wf, 1, 1);
        const QVector<QPair<int, int>> snapF22 = snapChunk(wf, 2, 2);
        const QByteArray gridFixed = gridOf(wf);
        const bool savedOk = store.saveAll(QStringLiteral("r2027c"));
        ok = ok && savedOk;
        if (!savedOk)
            diag += QStringLiteral("[blob-save] ");

        // ── D3 转换（纯元数据：标志 + core dims + 代次锚；零 blob 搬迁）────────────────────
        ChunkStore conv;
        conv.bind(db);
        conv.setStreamWorldId(QStringLiteral("r2027c"));
        const bool markOk = conv.markStreamingWorld(kWF, kDF);
        StreamWorldMeta meta;
        const bool convOk = markOk && conv.readStreamWorldMeta(meta) && meta.streaming
            && meta.coreW == kWF && meta.coreD == kDF && meta.baseGen == 0 && meta.saveGen == 0;
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[conv mark=%1 s=%2 w=%3 b=%4 g=%5] ")
                        .arg(markOk).arg(meta.streaming).arg(meta.coreW).arg(meta.baseGen)
                        .arg(meta.saveGen);

        // 会话内薄封装：sparse 会话 + 同库绑定 + 生产同构读档（返回读档后是否成功）。
        const auto entry = [&](World &ws, const char *tag, int *restored, int *blobbed) {
            GameSession gs(ws);
            gs.configureStreamingRadii(1, 1, 2);
            if (!gs.bindChunkEditsStore(db) || !gs.setStreamWorldId(QStringLiteral("r2027c")))
                return false;
            store.setWorld(&ws); // r2010d rebind（loadChunks 写入面）
            const bool r = gs.loadStreamingWorld(store);
            if (restored)
                *restored = gs.loadRestoredRowCount();
            if (blobbed)
                *blobbed = gs.loadBlobChunkCount();
            Q_UNUSED(tag);
            return r;
        };

        // ── 首进（生产同构）：核心区 blob 全物化（逐位 = fixed 时代内容）──────────────────
        QVector<QPair<int, int>> snapS11; // 流式编辑后的 (1,1)（overlay 胜利者的真值基准）
        {
            World ws1 = makeSparseCore(kWF, kDF);
            int restored = -1, blobbed = -1;
            const bool entered = entry(ws1, "first", &restored, &blobbed);
            const bool firstOk = entered && restored == 0 && blobbed == 9;
            ok = ok && firstOk;
            if (!firstOk)
                diag += QStringLiteral("[first ok=%1 rows=%2 blob=%3] ")
                            .arg(entered).arg(restored).arg(blobbed);
            long eq = 0;
            QString dd;
            const bool id00 = chunkIdentical(ws1, 0, 0, snapF00, eq, dd);
            const bool id11 = chunkIdentical(ws1, 1, 1, snapF11, eq, dd);
            const bool id22 = chunkIdentical(ws1, 2, 2, snapF22, eq, dd);
            ok = ok && id00 && id11 && id22;
            if (!id00 || !id11 || !id22)
                diag += QStringLiteral("[blob-identity 00=%1 11=%2 22=%3 eq=%4 %5] ")
                            .arg(id00).arg(id11).arg(id22).arg(eq).arg(dd);

            // 流式时代编辑（与 fixed 时代编辑异列）：冲洗成行（行代次 = 保存代次 1）。
            // 绑定无条件执行（不与 ok 链短接——setup 面独立于断言面，r2027 首跑教训）。
            const EditSite s11s = placeTop(ws1, 22, 22);
            const bool editOk = s11s.ok;
            ok = ok && editOk;
            snapS11 = snapChunk(ws1, 1, 1);
            {
                GameSession gsFlush(ws1);
                gsFlush.configureStreamingRadii(1, 1, 2);
                const bool boundF = gsFlush.bindChunkEditsStore(db)
                    && gsFlush.setStreamWorldId(QStringLiteral("r2027c"));
                ok = ok && boundF;
                const bool flushed = gsFlush.flushResidentEditsForSave();
                ChunkStoreBlob blob11;
                const bool flushOk = flushed && boundF
                    && gsFlush.chunkEditsStore()->chunkCount() == 1
                    && gsFlush.chunkEditsStore()->loadChunk(1, 1, blob11)
                    && blob11.generation == 1;
                ok = ok && flushOk;
                if (!flushOk)
                    diag += QStringLiteral("[flush ok=%1 rows=%2 gen=%3] ")
                                .arg(flushed)
                                .arg(gsFlush.chunkEditsStore()->chunkCount())
                                .arg(qint64(blob11.generation));
            }
        }

        // ── 再读档：仲裁 = 代次对代次（编辑区 overlay 胜 / 无行区 blob 逐位 / 境外生成）────
        {
            World ws2 = makeSparseCore(kWF, kDF);
            GameSession gs2(ws2);
            gs2.configureStreamingRadii(1, 1, 2);
            const bool bound2 = gs2.bindChunkEditsStore(db)
                && gs2.setStreamWorldId(QStringLiteral("r2027c"));
            ok = ok && bound2;
            store.setWorld(&ws2); // r2010d rebind（loadChunks 写入面）
            const bool entered = gs2.loadStreamingWorld(store);
            const int restored = gs2.loadRestoredRowCount();
            const int blobbed = gs2.loadBlobChunkCount();
            const bool secondOk = entered && restored == 1 && blobbed == 8;
            ok = ok && secondOk;
            if (!secondOk)
                diag += QStringLiteral("[second ok=%1 rows=%2 blob=%3] ")
                            .arg(entered).arg(restored).arg(blobbed);
            long eq = 0;
            QString dd;
            // 编辑区 overlay 胜：行内容 = fixed 时代编辑 + 流式时代编辑 双双逐位在列。
            const bool id11 = chunkIdentical(ws2, 1, 1, snapS11, eq, dd);
            // 无行核心区：blob 胜（fixed 时代内容逐位，流式时代无痕）。
            const bool id00 = chunkIdentical(ws2, 0, 0, snapF00, eq, dd);
            const bool id22 = chunkIdentical(ws2, 2, 2, snapF22, eq, dd);
            ok = ok && id11 && id00 && id22;
            if (!id11 || !id00 || !id22)
                diag += QStringLiteral("[arb 11=%1 00=%2 22=%3 eq=%4 %5] ")
                            .arg(id11).arg(id00).arg(id22).arg(eq).arg(dd);

            // 境外按需生成（走查驱动路径）：与同参 sparse 孪生逐位恒等（含 population）。
            gs2.notePlayerChunk(3, 3);
            QVector<QPair<int, int>> outKeys{ { 3, 3 } };
            QString dOut;
            const bool cOut = convergeLoaded(gs2, ws2, outKeys, 45000, dOut);
            World twin = makeSparseCore(kWF, kDF);
            twin.loadChunkAt(3, 3);
            const QVector<QPair<int, int>> snapTwin = snapChunk(twin, 3, 3);
            long eqT = 0;
            QString ddT;
            const bool idT = chunkIdentical(ws2, 3, 3, snapTwin, eqT, ddT);
            ok = ok && cOut && idT
                && ws2.chunks().lifecycleAt(3, 3) == ChunkLifecycle::Loaded;
            if (!cOut || !idT)
                diag += QStringLiteral("[outside c=%1 eq=%2 %3 %4] ")
                            .arg(cOut).arg(eqT).arg(ddT).arg(dOut);
        }

        // ── 仲裁相位 A：老行让位（锚代次抬过行代次 → blob 胜 = 新者胜）────────────────────
        {
            StreamWorldMeta raised;
            raised.streaming = true;
            raised.coreW = kWF;
            raised.coreD = kDF;
            raised.baseGen = 5;
            raised.saveGen = 5;
            ok = ok && conv.writeStreamWorldMeta(raised);
            World ws3 = makeSparseCore(kWF, kDF);
            int restored = -1, blobbed = -1;
            const bool entered = entry(ws3, "third", &restored, &blobbed);
            long eq = 0;
            QString dd;
            const bool olderOk = entered && restored == 0 && blobbed == 9
                && chunkIdentical(ws3, 1, 1, snapF11, eq, dd);
            ok = ok && olderOk;
            if (!olderOk)
                diag += QStringLiteral("[older ok=%1 rows=%2 blob=%3 eq=%4 %5] ")
                            .arg(entered).arg(restored).arg(blobbed).arg(eq).arg(dd);
        }

        // ── 仲裁相位 B：同代 = 表胜（行代次 == 锚代次 → 行仍胜）──────────────────────────
        {
            StreamWorldMeta tie;
            tie.streaming = true;
            tie.coreW = kWF;
            tie.coreD = kDF;
            tie.baseGen = 1;
            tie.saveGen = 1;
            ok = ok && conv.writeStreamWorldMeta(tie);
            World ws4 = makeSparseCore(kWF, kDF);
            int restored = -1, blobbed = -1;
            const bool entered = entry(ws4, "fourth", &restored, &blobbed);
            long eq = 0;
            QString dd;
            const bool tieOk = entered && restored == 1
                && chunkIdentical(ws4, 1, 1, snapS11, eq, dd);
            ok = ok && tieOk;
            if (!tieOk)
                diag += QStringLiteral("[tie ok=%1 rows=%2 eq=%3 %4] ")
                            .arg(entered).arg(restored).arg(eq).arg(dd);
        }

        // ── 逆翻：标志翻回 = fixed 语义（读档链照旧 + chunks 表零触碰）────────────────────
        {
            ok = ok && conv.clearStreamingWorldFlag();
            StreamWorldMeta after;
            const bool flagOk = conv.readStreamWorldMeta(after) && !after.streaming;
            ok = ok && flagOk;
            World wf2;
        makeFixed(wf2);
            wf2.beginLoad(kSeedF);
            store.setWorld(&wf2);
            const int loaded = store.loadChunks();
            wf2.finishLoad();
            const bool fixedOk = loaded == 9 && gridOf(wf2) == gridFixed;
            ok = ok && fixedOk;
            if (!fixedOk)
                diag += QStringLiteral("[revert loaded=%1 grid=%2] ")
                            .arg(loaded).arg(gridOf(wf2) == gridFixed);
            // chunks 表行数恒 9（流式时代零 blob 触碰的行为级承载）。
            QString probeDiag;
            const QString conn = QStringLiteral("r2027_count_%1").arg(QCoreApplication::applicationPid());
            if (QSqlDatabase::contains(conn))
                QSqlDatabase::removeDatabase(conn);
            int chunkRows = -1;
            {
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                p.setDatabaseName(db);
                if (p.open()) {
                    QSqlQuery q(p);
                    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM chunks")) && q.next())
                        chunkRows = q.value(0).toInt();
                }
            }
            QSqlDatabase::removeDatabase(conn);
            ok = ok && chunkRows == 9;
            if (chunkRows != 9)
                diag += QStringLiteral("[chunk-rows %1] ").arg(chunkRows) + probeDiag;
        }

        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2027c D3 conversion roundtrip: the flipped additive flag"
                             " materializes the core region bitwise from the production blob"
                             " through the read-only invokable face with zero data movement,"
                             " the flushed streaming-era edit wins the generation-vs-generation"
                             " arbitration while unedited core chunks keep the fixed-era"
                             " content and an outside-core chunk generates on demand with"
                             " population intact, an older row loses to the blob once the"
                             " anchor generation is raised past it while an equal generation"
                             " keeps the table win, and flipping the flag back restores the"
                             " fixed load chain bitwise with the blob table untouched"
                          << (ok ? QString() : diag);
    });

    // ── r2027d：结构钉（worldstore 禁触反探 + additive 正面钉 + 冲洗失败不谎报 + QML 零触碰
    //    + 会话缝面）───────────────────────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2027d structure pins (comment-stripped source pins hold the W5"
        " backend on its authorized seams: the frozen adapter pair and the world pair stay blind to every"
        " new streaming-persistence token, the stream metadata table is created idempotently"
        " with an upsert face and zero version bump and zero destructive SQL, a flush under an"
        " exclusive second-connection lock reports failure with the persist generation unmoved and"
        " the dirty ledger intact before converging to exactly one row after unlock, a flush"
        " on an unregistered world fails safe, QML stays free of every W5 token, and the"
        " session flush enumerates the resident set through the authorized ordered face with"
        " the generation advance ahead of the persist loop and the blob materialization ahead"
        " of the row replay)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };

        // ① 禁触反探（miss 非空 = 合规）：W5 新词元族一个不入 worldstore / world 对 / QML。
        const bool storeBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"),
                                     "stream_worlds")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "StreamWorldMeta")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "loadStreamingWorld")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "stream_worlds")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "flushResidentEdits")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "stream_worlds")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "loadStreamingWorld")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "stream_worlds")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "flushResidentEdits");
        ok = ok && storeBlind;
        if (!storeBlind)
            diag += QStringLiteral("[blind] ");
        const QString qml = srcRoot + QStringLiteral("/ui/Main.qml");
        const bool qmlOk = forbiddenAbsent(qml, "stream_worlds")
            && forbiddenAbsent(qml, "StreamWorldMeta")
            && forbiddenAbsent(qml, "flushResidentEdits")
            && forbiddenAbsent(qml, "loadStreamingWorld")
            && forbiddenAbsent(qml, "markStreamingWorld");
        ok = ok && qmlOk;
        if (!qmlOk)
            diag += QStringLiteral("[qml] ");

        // ② additive 正面钉（剥注释）：stream_worlds 幂等建表 + 元数据 upsert + 代次推进在位；
        //    零 bump / 零破坏 SQL 反探延伸（miss 非空 = 合规）。
        const QStringList missCs = pinSet(
            srcRoot + QStringLiteral("/World/chunkstore.cpp"), {
                SrcPin("stream metadata table create", "CREATE TABLE IF NOT EXISTS", 2),
                SrcPin("stream metadata upsert", "INSERT OR REPLACE INTO", 2),
                SrcPin("save generation advance", "advanceStreamSaveGeneration", 1),
            });
        for (const QString &m : missCs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool noBump = forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"),
                                 "ALTER TABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstore.cpp"), "DROP TABLE");
        ok = ok && noBump;
        if (!noBump)
            diag += QStringLiteral("[no-bump] ");

        // ③ 会话缝面正面钉（剥注释）：冲洗枚举走驻留集权威面 + 代次先拍 + blob 先于行回灌。
        {
            QFile f(srcRoot + QStringLiteral("/Game/gamesession.h"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const QStringList missGs = pinSet(
                srcRoot + QStringLiteral("/Game/gamesession.h"), {
                    SrcPin("flush resident enumeration", "sparseResidentKeysOrdered()", 1),
                    SrcPin("persist single execution body", "m_chunkStore->persistChunk(cx, cz, *c)", 1),
                    SrcPin("arbitration winner face", "winsAgainstBlob", 2),
                    SrcPin("blob read-only invokable face", "store.loadChunks()", 1),
                });
            for (const QString &m : missGs) {
                ok = false;
                diag += QStringLiteral("[%1] ").arg(m);
            }
            // 位序钉：冲洗体 = 落盘循环 < 代次推进收口（先冲洗后代次——拍序论证见
            //   gamesession.h flush 头注）；读档体 = blob 物化 < 行回灌。
            const qsizetype bF = src.indexOf(
                QStringLiteral("inline bool GameSession::flushResidentEditsForSave()"));
            const qsizetype bL = src.indexOf(
                QStringLiteral("inline bool GameSession::loadStreamingWorld(WorldStore &store)"));
            if (bF >= 0 && bL > bF) {
                const QString flushBody = src.mid(bF, bL - bF);
                const qsizetype iAdv = flushBody.indexOf(
                    QStringLiteral("advanceStreamSaveGeneration()"));
                const qsizetype iLoop = flushBody.indexOf(
                    QStringLiteral("persistResidentChunk(k.first, k.second)"));
                const bool flushOrder = iAdv >= 0 && iLoop > 0 && iLoop < iAdv;
                ok = ok && flushOrder;
                if (!flushOrder)
                    diag += QStringLiteral("[flush-order adv=%1 loop=%2] ").arg(iAdv).arg(iLoop);
                const QString loadBody = src.mid(bL);
                const qsizetype iBlob = loadBody.indexOf(QStringLiteral("store.loadChunks()"));
                const qsizetype iRow = loadBody.indexOf(
                    QStringLiteral("restoreChunkFromBlob(row.cx, row.cz"));
                const bool loadOrder = iBlob >= 0 && iRow > iBlob;
                ok = ok && loadOrder;
                if (!loadOrder)
                    diag += QStringLiteral("[load-order blob=%1 row=%2] ").arg(iBlob).arg(iRow);
            } else {
                ok = false;
                diag += QStringLiteral("[body-locate f=%1 l=%2] ").arg(bF).arg(bL);
            }
        }

        // ④ 行为钉：冲洗失败不谎报（真锁注入，t974 先例生产面）+ 未登记世界 fail-safe。
        {
            const QString db = tempDb("d");
            QFile::remove(db); // fresh
            World w = makeSparse();
            GameSession gs(w);
            // 绑定无条件执行（不与 ok 链短接——setup 面独立于断言面，r2027 首跑教训）。
            const bool boundD = gs.bindChunkEditsStore(db)
                && gs.setStreamWorldId(QStringLiteral("r2027d"));
            ok = ok && boundD;
            // 未登记（无 stream_worlds 行）：冲洗 fail-safe false（无冲洗域，零标志零活动同门）。
            const EditSite s0 = placeTop(w, 36, 36); // 中心 chunk (2,2)（构造期预生成，驻留）
            ok = ok && s0.ok && w.chunkHasUnsavedEdits(2, 2);
            const bool unregistered = !gs.flushResidentEditsForSave();
            ok = ok && unregistered;
            if (!unregistered)
                diag += QStringLiteral("[unregistered-flush-true] ");

            // 登记 → 编辑 → 真锁注入：落盘与代次推进双双被挡 → false + 账原样 + 零部分冲洗
            //（本腿只钉**失败不谎报侧**；成功侧收敛 = r2027b 承重面，NEG-1 恰红面设计）。
            const bool marked = gs.markStreamingWorld(kWS, kDS);
            ok = ok && marked;
            // 锁前基线读（锁内读会被 EXCLUSIVE 挡——r2027 首跑教训）。
            StreamWorldMeta before;
            const bool readBefore = gs.chunkEditsStore()->readStreamWorldMeta(before);
            ok = ok && readBefore;
            bool lockHeld = false;
            {
                QSqlDatabase lockDb = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), QStringLiteral("r2027_locker"));
                lockDb.setDatabaseName(db);
                lockDb.open();
                QSqlQuery beginQ(lockDb);
                lockHeld = beginQ.exec(QStringLiteral("BEGIN EXCLUSIVE"));

                const bool flushed = gs.flushResidentEditsForSave();
                const bool abortOk = lockHeld && readBefore && !flushed
                    && gs.flushPersistedCount() == 0
                    && w.chunkHasUnsavedEdits(2, 2) // 脏账原样（未落盘 = 未清账）
                    && gs.chunkEditsStore()->chunkCount() == 0; // 零部分冲洗
                ok = ok && abortOk;
                if (!abortOk)
                    diag += QStringLiteral("[abort held=%1 rb=%2 ok=%3 p=%4 dirty=%5 rows=%6] ")
                                .arg(lockHeld).arg(readBefore).arg(flushed)
                                .arg(gs.flushPersistedCount())
                                .arg(w.chunkHasUnsavedEdits(2, 2))
                                .arg(gs.chunkEditsStore()->chunkCount());

                QSqlQuery endQ(lockDb);
                endQ.exec(QStringLiteral("COMMIT")); // 解锁
            }
            QSqlDatabase::removeDatabase(QStringLiteral("r2027_locker"));
            ok = ok && lockHeld;
            if (!lockHeld)
                diag += QStringLiteral("[lock-not-held] ");
            // 解锁后代次复核：推进被锁挡下 → save_gen 未动（写不进 = 未推进，marker 同门）。
            StreamWorldMeta after;
            const qint64 genAfter = gs.chunkEditsStore()->readStreamWorldMeta(after)
                ? after.saveGen
                : qint64(-1);
            ok = ok && genAfter == 0;
            if (genAfter != 0)
                diag += QStringLiteral("[gen-moved %1] ").arg(genAfter);

            QFile::remove(db); // 用后即删
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2027d structure pins: the frozen adapter pair and the world pair stay blind"
                             " to every new streaming-persistence token, the stream metadata"
                             " table is created idempotently with an upsert face and zero"
                             " destructive SQL, a flush under an exclusive lock reports failure"
                             " with zero partial rows and the dirty ledger intact while the"
                             " save generation stays unmoved, a flush on an unregistered world"
                             " fails safe, QML stays free of every W5 token, and the session"
                             " flush enumerates the resident set through the authorized"
                             " ordered face with the blob materialization ahead of the row"
                             " replay"
                          << (ok ? QString() : diag);
    });
}
