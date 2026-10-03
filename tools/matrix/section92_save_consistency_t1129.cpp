#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：统一保存协议（marker-first + t1129 单事务原子化）
#include "worldstore.h"      // 被测面：四面写执行体 + 原子保存域原语
#include "gamesession.h"     // 被测面：流式同事务冲洗变体（flushResidentEditsForSaveOn）
#include "chunkstore.h"      // chunk_edits / stream_worlds 原始读面（仲裁账目诊断）

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <memory> // std::unique_ptr（World 非拷贝 → 堆装两模式 rig）

// t1129 SAVE-01 保存中断混合代次 探针段（5 腿 r2099a-e；filter 词 "r2099"；矩阵 930→930+N）。
// 置尾先例沿用（接 section91，runAll 末执行）；fresh 小世界 + 临时 SQLite 库（pid 键名，测试
// 自清理——r2015 段同门绝对路径），真实用户 saves/ 零触碰。任务契约（月度交接单 SAVE-01 原文）：
//   「复现柱」→ r2099a（注入缝驱动：存 A → 变更 → 存 B 于 Player 段注入失败 → 关库重开四面
//     读回 = 恰上一完整代 A + 物品守恒账平——固定与流式两模式；同店重试收敛 = 恰 B）；
//   「失败点矩阵柱」→ r2099b（五注入点 × 两模式：BeginMark/World/Player/Progress/Finalize 逐点
//     注入 → receipt false + 关库重开四面恰 A + 台账分类如实[BeginMark 恒 Clean@A；余点
//     Interrupted] + 流式附加面[chunk_edits 行数/save_gen] 恰 A + Player 点同店重试恰 B）；
//   「重复中断+重试柱」→ r2099c（同店三连中断：代次单调消耗不重置不复用 + 每次中断四面恰 A
//     + 终态重存收敛恰新代 Clean）；
//   「旧档兼容柱」→ r2099d（无台账裸三写旧库[save_coord 缺席 = Fresh] + 手工最小库[user_version 0
//     + 缺表旧形态] + 旧中断代次标记库[手工置 gen>complete]——三形态读入不丢不崩 + 新码在其上
//     统一保存照常收敛；零 schema 变化 = 兼容硬门）；
//   「结构钉族」→ r2099e（原子保存域原语/戳入事务行/冲洗钩转发行/同事务冲洗体/提交清账体/
//     生产登记闭包 + Main.qml 冲洗行退役面[StreamingBridge. 计数 2→1 lawful 修订 t1129 +
//     旧前置行禁出负面钉 + toast 文案与统一口进行原样] + worldstore 盲区钉幸存复核
//     [save_coord/SaveCoordinator/flushResidentEdits/stream_worlds 四词元禁出] + CMake 段行
//     ——NEG 双摘面行由 a/b 持有[见下]，本腿零 NEG 面钉）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘步⑤b 流式冲洗调用块（savecoordinator.cpp `if (m_flushHook && !m_store->
//     runInSaveTransaction(m_flushHook)) {...}` 五行整块摘除 = 摘调用点形编译绿）→ 流式半边
//     退役：声明红面 = {r2099a, r2099b}（两腿的流式场景红：冲洗不再随保存发生 = chunk_edits/
//     save_gen 账目红 + 阻断面重试红；fixed 场景/其余腿幸存——r2099c/d 固定模式零涉，r2099e
//     不持该行钉）。NEG-1 同时连带 r2099b 所持回滚计数钉（minCount 6→5）——如实申报连带红。
//     【实测 = 声明精确命中：neg1red 轮恰 {r2099a, r2099b} 双红零连带】。
//   NEG-2 摘步⑦ Player 段回滚行（savecoordinator.cpp `m_store->rollbackAtomicSave();` 六处中
//     恰摘 Player 段一处 = 摘调用点形编译绿）→ 中断尝试不再清场：声明红面 = {r2099a, r2099b,
//     r2099c}（三腿都驱「注入后同店 raw 读 + 不关店重试」——关店会让 SQLite 弃事务回滚从而
//     掩盖面，故承重读面走独立连接 raw 读）。
//     【实测 = 声明 {r2099a, r2099b, r2099c} 精确红 + 两处诚实申报的跨族连带：
//     r2015c / r2031b（两腿的旧协议腿链在摘行下同店续走撞挂起事务级联红——与声明面同物理
//     根因 = 摘行后事务不清场，非误伤；两族 restore 轮即复绿）+ r2097c（已知时序 flake——
//     该腿与保存域零因果，同轮 pos/final/neg1 双轮绿实证，复跑清协议裁定 flake】。
//   两 NEG 红面重叠 {r2099a, r2099b} = 物理同源（冲洗块与回滚行同在保存主链），非误伤。
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section92_save_consistency_t1129()
{
    // ── 段内共享帮手（腿间无共享 rig 状态——各腿自建 fresh 世界 + 临时库）───────────────────
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk）
    constexpr int kWS = 48, kDS = 48, kHS = 96, kSeedS = 42; // sparse 核心域（3×3 chunk，节流 dims）
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);
        w.setWeatherState(0);               // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f);  // >> 探针窗 → 恒晴零 RNG
    };
    const auto makeSparseCore = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeedS;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kHS;
        sp.spawnPreGenerateRadius = 0; // 零预生成（中心 chunk 除外——W1 语义，section30 同门）
        return World(sp);
    };
    // 临时库路径（pid 键名 + 腿标；QDir::temp()，测试自清理；绝对路径直用 = openWorld 先例）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2099_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 地表标记放置（heightmapAt 真实体素顶——heightAt 生成器噪声面不可作判据，section30 同门）。
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
                continue;
            w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
            if (w.blockAt(x, h + 1, z) != quint8(BR::Stone))
                continue;
            return EditSite{ x, h + 1, z, true };
        }
        return EditSite{ bx, -1, bz, false };
    };
    const auto placeMarker = [](World &w, int x, int z) -> int {
        const int h = w.heightmapAt(x, z);
        if (h < 1 || h + 2 >= w.height()) return -1;
        w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
        if (w.blockAt(x, h + 1, z) != quint8(BR::Stone)) return -1;
        return h + 1;
    };
    // 栅格指纹（blockAt+stateAt 全栅格——世界面逐位恒等的比较面）。
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
        const QString conn = QStringLiteral("r2099_coord_%1").arg(QCoreApplication::applicationPid());
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
    // 流式附加面账目（独立连接 raw 读：chunk_edits 行数 + stream_worlds.save_gen）。
    //   chunk_edits 表缺席（尚无任何 persist——零编辑纯冲洗域）= 0 行合法态（loadAllRows 同门）。
    struct StreamLedger
    {
        int rows = -1;
        qint64 saveGen = -1;
    };
    const auto streamLedger = [](const QString &db, const QString &worldId) {
        StreamLedger out;
        const QString conn = QStringLiteral("r2099_stream_%1").arg(QCoreApplication::applicationPid());
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        {
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery q(p);
                if (q.exec(QStringLiteral("SELECT COUNT(*) FROM chunk_edits")) && q.next())
                    out.rows = q.value(0).toInt();
                else
                    out.rows = 0; // 表缺席 = 零行（-1 只留给 open 失败）
                QSqlQuery g(p);
                g.prepare(QStringLiteral("SELECT save_gen FROM stream_worlds WHERE world = ?"));
                g.addBindValue(worldId);
                if (g.exec() && g.next())
                    out.saveGen = g.value(0).toLongLong();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    };
    // 物品守恒账（player inv + 首箱 slots 的 id 物品总数——复制/丢失的唯一判定量）。
    const auto countItems = [](const QVariantList &slotRows) {
        int n = 0;
        for (const QVariant &v : slotRows)
            n += v.toMap().value(QStringLiteral("count")).toInt();
        return n;
    };
    // 统一保存载荷组（守恒剧本 = 交接单原文：背包 10 件 + 箱空 → 物品入箱 → 注入失败）。
    const auto playerWith = [](int invCount) { // 10 = 背包 10 件；0 = 已入箱（背包空）
        QVariantMap pd;
        pd.insert(QStringLiteral("px"), 11.5);
        QVariantList inv;
        if (invCount > 0)
            inv.append(QVariantMap{ { QStringLiteral("id"), 3 }, { QStringLiteral("count"), invCount } });
        pd.insert(QStringLiteral("inv"), inv);
        return pd;
    };
    const auto chestWith = [](int itemCount) { // 首箱 slots：0 = 箱空；10 = 物品入箱
        QVariantMap chest;
        chest.insert(QStringLiteral("x"), 4);
        chest.insert(QStringLiteral("y"), 5);
        chest.insert(QStringLiteral("z"), 6);
        QVariantList slotRows; // 命名避 Qt slots 空宏（t1129 首编译教训面）
        if (itemCount > 0)
            slotRows.append(QVariantMap{ { QStringLiteral("id"), 3 }, { QStringLiteral("count"), itemCount } });
        chest.insert(QStringLiteral("slots"), slotRows);
        return QVariantList{ chest };
    };
    const auto progressWith = [](int stat) {
        QVariantMap pr;
        pr.insert(QStringLiteral("stat"), stat);
        return pr;
    };
    // 四面读回帮手（关库重开后的全表读——调用方先 closeWorld 旧店；本帮手开新店新世界）：
    //   返回 {chests 总件数, inv 件数, progress stat, 栅格}——世界面栅格经 beginLoad/loadChunks/
    //   finishLoad 全量回填。
    struct Faces
    {
        int chestItems = -1;
        int invItems = -1;
        int progressStat = -1;
        QByteArray grid;
        bool opened = false;
    };
    const auto readFaces = [&](const QString &db, int expectChunks) {
        Faces f;
        World w;
        freshWorld48(w);
        WorldStore store;
        f.opened = store.openWorld(db) && store.isOpen();
        if (f.opened) {
            store.setWorld(&w);
            w.beginLoad(kSeedF);
            store.loadChunks();
            w.finishLoad();
            const QVariantList chests = store.loadChests();
            f.chestItems = 0;
            for (const QVariant &c : chests)
                f.chestItems += countItems(c.toMap().value(QStringLiteral("slots")).toList());
            f.invItems = countItems(
                store.loadPlayerData().value(QStringLiteral("inv")).toList());
            f.progressStat = store.loadProgress().value(QStringLiteral("stat")).toInt();
        }
        Q_UNUSED(expectChunks);
        store.closeWorld();
        f.grid = gridOf(w);
        return f;
    };
    // 原容器三面读（**独立连接**——kConn 互不占用；调用方店保持开启 = 同店重试前提的中间读面）。
    //   玩家/进度 JSON 经 QJsonDocument 拆包（自描述面——与 store 读路径同构的 raw 形）。
    struct RawFaces
    {
        int chestItems = -1;
        int invItems = -1;
        int progressStat = -1;
    };
    const auto readFacesRaw = [&](const QString &db) {
        RawFaces f;
        const QString conn = QStringLiteral("r2099_raw_%1").arg(QCoreApplication::applicationPid());
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);
        {
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery cq(p);
                if (cq.exec(QStringLiteral("SELECT data FROM chests"))) {
                    f.chestItems = 0;
                    while (cq.next())
                        f.chestItems += countItems(QJsonDocument::fromJson(
                            cq.value(0).toString().toUtf8()).toVariant().toList());
                }
                QSqlQuery pq(p);
                if (pq.exec(QStringLiteral("SELECT data FROM player_state WHERE id = 0"))
                    && pq.next())
                    f.invItems = countItems(QJsonDocument::fromJson(
                        pq.value(0).toString().toUtf8()).toVariant().toMap()
                        .value(QStringLiteral("inv")).toList());
                QSqlQuery gq(p);
                if (gq.exec(QStringLiteral("SELECT data FROM progress WHERE key = 'main'"))
                    && gq.next())
                    f.progressStat = QJsonDocument::fromJson(gq.value(0).toString().toUtf8())
                                         .toVariant().toMap()
                                         .value(QStringLiteral("stat")).toInt();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return f;
    };

    // ── r2099a：复现柱（Player 段注入 → 关库重开四面恰 A + 守恒账平 + 同店重试恰 B；两模式）──
    runLeg(QStringLiteral("r2099a interruption reproduction column (the injection-seam driven"
        " save A then changed save B with a player-stage fault closes the database and reopens"
        " to read all four faces back as exactly the previous complete generation with the"
        " item conservation ledger balanced in both modes: the fixed world reads the old chest"
        " empty plus the old ten-item inventory plus the old progress stat and the bitwise old"
        " grid, the streaming world reads zero edit rows at the old save generation with the"
        " same old player and container faces, the immediate same-store retry converges to the"
        " new complete generation carrying the moved item exactly once, and the streaming"
        " no-fault save lands its edit row stamped at the advanced save generation)"), [&]() {
        bool ok = true;
        QString diag;

        // ── 固定模式 ────────────────────────────────────────────────────────────────────
        {
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

            SaveCoordinator coord;
            coord.bind(&store, db);
            // 存 A（完整）：背包 10 + 箱空 + 进度 1 → Clean 1/1。
            SaveRequest ra;
            ra.name = QStringLiteral("r2099a");
            ra.chests = chestWith(0);
            ra.playerData = playerWith(10);
            ra.progress = progressWith(1);
            const SaveReceipt rsa = coord.saveAll(ra);
            const SaveGenerationInfo fa = coord.recover();
            const bool aOk = rsa.ok() && fa.state == SaveRecoveryState::Clean
                && fa.generation == 1 && fa.completeGeneration == 1;
            ok = ok && aOk;
            if (!aOk)
                diag += QStringLiteral("[A rok=%1 st=%2 g=%3/%4] ").arg(rsa.ok())
                            .arg(int(fa.state)).arg(fa.generation).arg(fa.completeGeneration);

            // 存 B（Player 段注入失败）：物品入箱 + 背包清空 + 进度 2 → receipt false。
            coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
            SaveRequest rb = ra;
            rb.chests = chestWith(10);
            rb.playerData = playerWith(0);
            rb.progress = progressWith(2);
            const SaveReceipt rsb = coord.saveAll(rb);
            const SaveGenerationInfo fb = coord.recover();
            const bool bOk = !rsb.ok() && fb.state == SaveRecoveryState::Interrupted
                && fb.generation == 2 && fb.completeGeneration == 1;
            ok = ok && bOk;
            if (!bOk)
                diag += QStringLiteral("[B rok=%1 st=%2 g=%3/%4] ").arg(rsb.ok())
                            .arg(int(fb.state)).arg(fb.generation).arg(fb.completeGeneration);

            // 中断后原容器/玩家/进度三面原读（恰 A + 守恒账平）：**独立连接**原读（原店保持开启
            //   = 同店重试前提——WorldStore 全局单连接名 kConn，r2015 段同门约束；关店会让
            //   SQLite 弃事务，NEG-2 的重试承重面要求同店不关）。
            const RawFaces rA = readFacesRaw(db);
            const bool facesOk = rA.chestItems == 0 && rA.invItems == 10
                && rA.progressStat == 1 && rA.chestItems + rA.invItems == 10;
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[facesA chest=%1 inv=%2 stat=%3] ")
                            .arg(rA.chestItems).arg(rA.invItems).arg(rA.progressStat);

            // 同店重试收敛（生产 caller 幂等重试一次的同门）：清钩重存 → 恰 B（四面新代 + 守恒）。
            //   代次期望 3/3：中断尝试消耗 gen 2（marker 落、事务回滚），重试 = max(2,1)+1 = 3
            //   （r2015 口径：中断号消耗不重置不复用）。
            coord.setFaultHook(SaveFaultHook());
            SaveRequest rc = rb;
            const SaveReceipt rsc = coord.saveAll(rc);
            const SaveGenerationInfo fc = coord.recover();
            store.closeWorld();
            const Faces fB = readFaces(db, 9);
            const bool convOk = rsc.ok() && fc.state == SaveRecoveryState::Clean
                && fc.generation == 3 && fc.completeGeneration == 3
                && fB.opened && fB.chestItems == 10 && fB.invItems == 0
                && fB.progressStat == 2 && fB.chestItems + fB.invItems == 10;
            ok = ok && convOk;
            if (!convOk)
                diag += QStringLiteral("[conv rok=%1 st=%2 g=%3/%4 chest=%5 inv=%6 stat=%7] ")
                            .arg(rsc.ok()).arg(int(fc.state))
                            .arg(fc.generation).arg(fc.completeGeneration)
                            .arg(fB.chestItems).arg(fB.invItems).arg(fB.progressStat);
            QFile::remove(db);
        }

        // ── 流式模式 ────────────────────────────────────────────────────────────────────
        {
            const QString db = tempDb("as");
            QFile::remove(db); // fresh
            World w = makeSparseCore();
            GameSession gs(w);
            const bool boundOk = gs.bindChunkEditsStore(db)
                && gs.setStreamWorldId(QStringLiteral("r2099as"))
                && gs.markStreamingWorld(kWS, kDS);
            ok = ok && boundOk;
            if (!boundOk) diag += QStringLiteral("[bind] ");
            WorldStore store;
            const bool opened = store.openWorld(db) && store.isOpen();
            ok = ok && opened;
            if (!opened) diag += QStringLiteral("[open] ");
            store.setWorld(&w);

            // 同事务冲洗缝（生产同款：flush 钩 = 事务连接上的驻留编辑落盘；commit 钩 = 提交面清账）。
            SaveCoordinator coord;
            coord.bind(&store, db);
            coord.setFlushHook([&gs](QSqlDatabase &dbh) { return gs.flushResidentEditsForSaveOn(dbh); });
            coord.setFlushCommitHook([&gs]() { gs.commitFlushResidentEditsOn(); });

            // 存 A（完整、零编辑）：冲洗域放行 + save_gen 推进 0→1 + 零行 → Clean 1/1。
            SaveRequest ra;
            ra.name = QStringLiteral("r2099as");
            ra.chests = chestWith(0);
            ra.playerData = playerWith(10);
            ra.progress = progressWith(1);
            const SaveReceipt rsa = coord.saveAll(ra);
            StreamLedger la = streamLedger(db, QStringLiteral("r2099as"));
            const bool aOk = rsa.ok() && la.rows == 0 && la.saveGen == 1;
            ok = ok && aOk;
            if (!aOk)
                diag += QStringLiteral("[As rok=%1 rows=%2 sg=%3] ").arg(rsa.ok())
                            .arg(la.rows).arg(qint64(la.saveGen));

            // 编辑（chunk (1,1) 未落盘账置位——radius 0 预生成中心 = 出生 chunk (1,1)，选址纪律
            //   = section30 同门）→ 存 B（Player 段注入）：行+代次随事务回滚 = 附加面恰 A。
            const EditSite ed = placeTop(w, 24, 24);
            const bool editOk = ed.ok && w.chunkHasUnsavedEdits(1, 1);
            ok = ok && editOk;
            if (!editOk) diag += QStringLiteral("[edit] ");
            coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
            SaveRequest rb = ra;
            rb.chests = chestWith(10);
            rb.playerData = playerWith(0);
            rb.progress = progressWith(2);
            const SaveReceipt rsb = coord.saveAll(rb);
            StreamLedger lb = streamLedger(db, QStringLiteral("r2099as"));
            const bool bOk = !rsb.ok() && lb.rows == 0 && lb.saveGen == 1;
            ok = ok && bOk;
            if (!bOk)
                diag += QStringLiteral("[Bs rok=%1 rows=%2 sg=%3] ").arg(rsb.ok())
                            .arg(lb.rows).arg(qint64(lb.saveGen));

            // 中断后原容器/玩家/进度 + 流式附加面原读（恰 A）：独立连接原读（原店保持开启 =
            //   同会话重试前提；回滚面账面天然未清 → 同会话重试重报收敛）。
            const RawFaces rA = readFacesRaw(db);
            StreamLedger lr = streamLedger(db, QStringLiteral("r2099as"));
            const bool facesOk = rA.chestItems == 0 && rA.invItems == 10
                && rA.progressStat == 1 && rA.chestItems + rA.invItems == 10
                && lr.rows == 0 && lr.saveGen == 1;
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[facesAs chest=%1 inv=%2 stat=%3 rows=%4 sg=%5] ")
                            .arg(rA.chestItems).arg(rA.invItems).arg(rA.progressStat)
                            .arg(lr.rows).arg(qint64(lr.saveGen));

            // 同会话同店重试收敛恰 B：行落（stamp=2）+ save_gen 2 + 箱 10 + 背包 0 + 进度 2 + 守恒平。
            coord.setFaultHook(SaveFaultHook());
            SaveRequest rc = rb;
            const SaveReceipt rsc = coord.saveAll(rc);
            StreamLedger lc = streamLedger(db, QStringLiteral("r2099as"));
            store.closeWorld();
            const Faces fB = readFaces(db, 0);
            const bool convOk = rsc.ok() && lc.rows == 1 && lc.saveGen == 2
                && fB.chestItems == 10 && fB.invItems == 0 && fB.progressStat == 2
                && fB.chestItems + fB.invItems == 10;
            ok = ok && convOk;
            if (!convOk)
                diag += QStringLiteral("[convs rok=%1 rows=%2 sg=%3 chest=%4 inv=%5 stat=%6] ")
                            .arg(rsc.ok()).arg(lc.rows).arg(qint64(lc.saveGen))
                            .arg(fB.chestItems).arg(fB.invItems).arg(fB.progressStat);
            QFile::remove(db);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2099a interruption reproduction column: the faulted save B"
                             " closes and reopens to all four faces exactly at the previous"
                             " complete generation A in both modes with the conservation"
                             " ledger balanced (old empty chest plus old ten-item inventory,"
                             " zero edit rows at the old save generation), and the same-store"
                             " retry converges to exactly B carrying the moved item once"
                          << (ok ? QString() : diag);
    });

    // ── r2099b：失败点矩阵柱（五注入点 × 两模式 → 四面恰 A + 台账如实 + Player 点重试恰 B）────
    runLeg(QStringLiteral("r2099b failure-point matrix column (all five fault stages times both"
        " modes leave every reopened face exactly at the previous complete generation: the"
        " begin-mark stage aborts with the ledger still clean at the old generation, the world"
        " player progress and finalize stages each honestly interrupt with the attempt"
        " generation consumed beyond the complete stamp while the fixed world grid, container,"
        " player and progress faces all read the old values, the streaming faces read zero edit"
        " rows at the old save generation with rollback discarding the in-transaction flush,"
        " and the player stage additionally converges its same-store retry to exactly the new"
        " generation in both modes)"), [&]() {
        bool ok = true;
        QString diag;

        const auto runStage = [&](const char *tag, SaveFaultStage stage, bool streaming) {
            bool localOk = true;
            QString local;
            const QString db = tempDb(tag);
            QFile::remove(db); // fresh
            // World 非拷贝（QObject）→ 不可拷贝型三元式禁用：堆装 + 引用面（两模式同参同场景）。
            std::unique_ptr<World> wHolder;
            if (streaming) {
                World::SparseWorldParams sp;
                sp.seed = kSeedS;
                sp.coreWidth = kWS;
                sp.coreDepth = kDS;
                sp.height = kHS;
                sp.spawnPreGenerateRadius = 0;
                wHolder = std::make_unique<World>(sp);
            } else {
                wHolder = std::make_unique<World>();
                freshWorld48(*wHolder);
                placeMarker(*wHolder, 10, 10);
            }
            const QByteArray gRef = gridOf(*wHolder); // 世界面基准（fixed 模式 A 面逐位比对面）
            World &w = *wHolder;
            GameSession gs(w);
            if (streaming) {
                if (!gs.bindChunkEditsStore(db)
                    || !gs.setStreamWorldId(QStringLiteral("r2099b"))
                    || !gs.markStreamingWorld(kWS, kDS))
                    localOk = false;
            }
            WorldStore store;
            const bool opened = store.openWorld(db) && store.isOpen();
            localOk = localOk && opened;
            store.setWorld(&w);
            SaveCoordinator coord;
            coord.bind(&store, db);
            if (streaming) {
                coord.setFlushHook([&gs](QSqlDatabase &dbh) { return gs.flushResidentEditsForSaveOn(dbh); });
                coord.setFlushCommitHook([&gs]() { gs.commitFlushResidentEditsOn(); });
            }
            // 存 A：Clean（代次 1/1；流式：save_gen 1 零行）。
            SaveRequest ra;
            ra.name = QStringLiteral("r2099b");
            ra.chests = chestWith(0);
            ra.playerData = playerWith(10);
            ra.progress = progressWith(1);
            const SaveReceipt rsa = coord.saveAll(ra);
            localOk = localOk && rsa.ok();
            // 注入点置位 → 存 B（变更载荷）→ receipt false + 分类如实。
            coord.setFaultHook([stage](SaveFaultStage st) { return st == stage; });
            if (streaming)
                placeTop(w, 24, 24); // chunk (1,1) 编辑（出生 chunk 预生成中心——事务内冲洗载荷）
            SaveRequest rb = ra;
            rb.chests = chestWith(10);
            rb.playerData = playerWith(0);
            rb.progress = progressWith(2);
            const SaveReceipt rsb = coord.saveAll(rb);
            const SaveGenerationInfo fb = coord.recover();
            const bool receiptOk = !rsb.ok()
                && (stage == SaveFaultStage::BeginMark
                        ? (fb.state == SaveRecoveryState::Clean && fb.generation == 1
                           && fb.completeGeneration == 1)
                        : (fb.state == SaveRecoveryState::Interrupted && fb.generation == 2
                           && fb.completeGeneration == 1));
            localOk = localOk && receiptOk;
            if (!receiptOk)
                local += QStringLiteral("[receipt st=%1 g=%2/%3] ").arg(int(fb.state))
                             .arg(fb.generation).arg(fb.completeGeneration);
            // Player 点：中断后 raw 三面原读（恰 A）+ **同店同会话重试**恰 B（NEG-2 承重面——
            //   原店保持开启：回滚行在则重试 BEGIN 不撞活动事务；摘行则重试撞挂起事务即红。
            //   kConn 单连接名约束下 raw 读 = 唯一不关店的读面；流式账面未清 → 同会话重报收敛）。
            // 其余四点：关库重开四面恰 A（「关闭重开数据库再读全表」协议面——kConn 单连接名
            //   约束：读前关原店，读后不续走重试即关店删库）。
            if (stage == SaveFaultStage::Player) {
                const RawFaces rA = readFacesRaw(db);
                const bool facesOk = rA.chestItems == 0 && rA.invItems == 10
                    && rA.progressStat == 1 && rA.chestItems + rA.invItems == 10
                    && (streaming || gridOf(w) == gRef);
                localOk = localOk && facesOk;
                if (!facesOk)
                    local += QStringLiteral("[facesA chest=%1 inv=%2 stat=%3] ")
                                 .arg(rA.chestItems).arg(rA.invItems).arg(rA.progressStat);
                if (streaming) {
                    StreamLedger la = streamLedger(db, QStringLiteral("r2099b"));
                    const bool streamOk = la.rows == 0 && la.saveGen == 1;
                    localOk = localOk && streamOk;
                    if (!streamOk)
                        local += QStringLiteral("[streamA rows=%1 sg=%2] ")
                                     .arg(la.rows).arg(qint64(la.saveGen));
                }
                coord.setFaultHook(SaveFaultHook());
                SaveRequest rc = rb;
                const SaveReceipt rsc = coord.saveAll(rc);
                store.closeWorld();
                const Faces fB = readFaces(db, streaming ? 0 : 9);
                const bool convOk = rsc.ok() && fB.chestItems == 10
                    && fB.invItems == 0 && fB.progressStat == 2;
                localOk = localOk && convOk;
                if (!convOk)
                    local += QStringLiteral("[retryB rok=%1 chest=%2 inv=%3 stat=%4] ")
                                 .arg(rsc.ok()).arg(fB.chestItems).arg(fB.invItems)
                                 .arg(fB.progressStat);
                if (streaming) {
                    StreamLedger lc = streamLedger(db, QStringLiteral("r2099b"));
                    if (lc.rows != 1 || lc.saveGen != 2) {
                        localOk = false;
                        local += QStringLiteral("[retryStream rows=%1 sg=%2] ")
                                     .arg(lc.rows).arg(qint64(lc.saveGen));
                    }
                }
            } else {
                store.closeWorld();
                const Faces fA = readFaces(db, streaming ? 0 : 9);
                const bool facesOk = fA.opened && fA.chestItems == 0 && fA.invItems == 10
                    && fA.progressStat == 1 && fA.chestItems + fA.invItems == 10
                    && (streaming || fA.grid == gRef); // fixed 模式世界面逐位恒等（恰基线代）
                localOk = localOk && facesOk;
                if (!facesOk)
                    local += QStringLiteral("[facesA chest=%1 inv=%2 stat=%3 grid=%4] ")
                                 .arg(fA.chestItems).arg(fA.invItems).arg(fA.progressStat)
                                 .arg(fA.grid == gRef ? 1 : 0);
                if (streaming) {
                    StreamLedger la = streamLedger(db, QStringLiteral("r2099b"));
                    const bool streamOk = la.rows == 0 && la.saveGen == 1;
                    localOk = localOk && streamOk;
                    if (!streamOk)
                        local += QStringLiteral("[streamA rows=%1 sg=%2] ")
                                     .arg(la.rows).arg(qint64(la.saveGen));
                }
            }
            QFile::remove(db);
            if (!localOk)
                diag += QStringLiteral("[%1%2 %3] ").arg(QLatin1String(tag))
                            .arg(streaming ? "s" : "f").arg(local);
            return localOk;
        };

        ok = ok && runStage("b1", SaveFaultStage::BeginMark, false);
        ok = ok && runStage("b2", SaveFaultStage::World, false);
        ok = ok && runStage("b3", SaveFaultStage::Player, false);
        ok = ok && runStage("b4", SaveFaultStage::Progress, false);
        ok = ok && runStage("b5", SaveFaultStage::Finalize, false);
        ok = ok && runStage("b6", SaveFaultStage::BeginMark, true);
        ok = ok && runStage("b7", SaveFaultStage::World, true);
        ok = ok && runStage("b8", SaveFaultStage::Player, true);
        ok = ok && runStage("b9", SaveFaultStage::Progress, true);
        ok = ok && runStage("b10", SaveFaultStage::Finalize, true);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2099b failure-point matrix column: all five fault stages in"
                             " both modes reopen to exactly the previous complete generation"
                             " with honest ledger classification and balanced conservation,"
                             " the streaming rollback discards the in-transaction flush, and"
                             " the player stage converges its same-store retry to exactly the"
                             " new generation"
                          << (ok ? QString() : diag);
    });

    // ── r2099c：重复中断+重试柱（同店三连中断 → 代次单调消耗 + 四面恒 A → 重存收敛恰新代）──────
    runLeg(QStringLiteral("r2099c repeated interruption and retry column (three consecutive"
        " player-stage faulted saves from one clean baseline consume three monotonic attempt"
        " generations without reset or reuse while every reopen still reads exactly the"
        " baseline generation on all four faces with the conservation ledger balanced, and the"
        " hook-cleared retry save converges the ledger to clean at the fourth generation with"
        " all four faces carrying the retry payload exactly)"), [&]() {
        bool ok = true;
        QString diag;

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        freshWorld48(w);
        const int mAy = placeMarker(w, 10, 10);
        ok = ok && mAy > 0;
        const QByteArray gRef = gridOf(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        store.setWorld(&w);
        SaveCoordinator coord;
        coord.bind(&store, db);

        SaveRequest ra;
        ra.name = QStringLiteral("r2099c");
        ra.chests = chestWith(0);
        ra.playerData = playerWith(10);
        ra.progress = progressWith(1);
        const SaveReceipt rsa = coord.saveAll(ra);
        ok = ok && rsa.ok();

        coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
        qint64 expectGen = 1;
        for (int round = 0; round < 3; ++round) {
            SaveRequest r = ra;
            r.chests = chestWith(10);
            r.playerData = playerWith(0);
            r.progress = progressWith(2 + round);
            const SaveReceipt rs = coord.saveAll(r);
            const SaveGenerationInfo f = coord.recover();
            ++expectGen; // 2/3/4：中断号单调消耗不重置不复用（r2015 口径同门）
            const bool roundOk = !rs.ok() && f.state == SaveRecoveryState::Interrupted
                && f.generation == expectGen && f.completeGeneration == 1;
            ok = ok && roundOk;
            if (!roundOk)
                diag += QStringLiteral("[r%1 st=%2 g=%3/%4] ").arg(round)
                            .arg(int(f.state)).arg(f.generation).arg(f.completeGeneration);
        }

        // 三连中断后：原容器/玩家/进度三面原读恒 A（恰基线代 + 守恒平；独立连接原读 = 同店重试
        //   前提——kConn 单连接名约束 + 关店会让 SQLite 弃事务，NEG-2 重试承重面要求同店不关）。
        const RawFaces rA = readFacesRaw(db);
        const bool facesOk = rA.chestItems == 0 && rA.invItems == 10
            && rA.progressStat == 1 && rA.chestItems + rA.invItems == 10;
        ok = ok && facesOk;
        if (!facesOk)
            diag += QStringLiteral("[facesA chest=%1 inv=%2 stat=%3] ")
                        .arg(rA.chestItems).arg(rA.invItems).arg(rA.progressStat);

        // 清钩同店重存收敛：恰代次 5/5 Clean + 四面全 B（物品恰迁移一次 + 守恒平）。
        coord.setFaultHook(SaveFaultHook());
        SaveRequest rc = ra;
        rc.chests = chestWith(10);
        rc.playerData = playerWith(0);
        rc.progress = progressWith(9);
        const SaveReceipt rsc = coord.saveAll(rc);
        const SaveGenerationInfo fc = coord.recover();
        store.closeWorld();
        const Faces fB = readFaces(db, 9);
        const bool convOk = rsc.ok() && fc.state == SaveRecoveryState::Clean
            && fc.generation == 5 && fc.completeGeneration == 5
            && fB.chestItems == 10 && fB.invItems == 0 && fB.progressStat == 9
            && fB.chestItems + fB.invItems == 10;
        ok = ok && convOk;
        if (!convOk)
            diag += QStringLiteral("[conv rok=%1 st=%2 g=%3/%4 chest=%5 inv=%6 stat=%7] ")
                        .arg(rsc.ok()).arg(int(fc.state)).arg(fc.generation)
                        .arg(fc.completeGeneration).arg(fB.chestItems).arg(fB.invItems)
                        .arg(fB.progressStat);
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2099c repeated interruption and retry column: three consecutive"
                             " faulted saves consume three monotonic attempt generations while"
                             " every face stays exactly at the clean baseline, and the retry"
                             " converges the ledger clean at generation five with the retry"
                             " payload on all four faces"
                          << (ok ? QString() : diag);
    });

    // ── r2099d：旧档兼容柱（无台账裸三写库 + 手工最小库 + 旧中断标记库——读入不丢不崩 + 新码收敛）─
    runLeg(QStringLiteral("r2099d legacy compatibility column (three old-shape databases load"
        " into the new binary with zero data loss and zero crash: the bare three-write save"
        " without any generation ledger reads fresh and keeps every container, player and"
        " progress row intact through a coordinated save on top, the hand-built minimal"
        " pre-era database with user_version zero and missing container tables opens with"
        " idempotent schema completion and reads its hand-inserted rows back exactly, and the"
        " hand-stamped interrupted-ledger database classifies honestly interrupted and the next"
        " coordinated save converges it clean while overwriting all four faces consistently)"), [&]() {
        bool ok = true;
        QString diag;

        // (1) 无台账裸三写库（旧代码形态：save_coord 缺席 = Fresh）。
        {
            const QString db = tempDb("d1");
            QFile::remove(db); // fresh
            World w;
            freshWorld48(w);
            WorldStore store;
            const bool opened = store.openWorld(db) && store.isOpen();
            store.setWorld(&w);
            // 旧三写形态：裸 saveAll + savePlayerData + saveProgress（无台账、无协调层）。
            const bool bare = store.saveAll(QStringLiteral("r2099d-legacy"), chestWith(0))
                && store.savePlayerData(playerWith(10)) && store.saveProgress(progressWith(1));
            store.closeWorld();
            // Fresh 分类（无台账 = 旧档合法形态——只读台账面，r2015 同门栈上实例）。
            SaveCoordinator probe;
            probe.bind(nullptr, db);
            const SaveGenerationInfo info = probe.recover();
            const Faces f1 = readFaces(db, 9);
            World w3;
            freshWorld48(w3);
            WorldStore store3;
            const bool open3 = store3.openWorld(db) && store3.isOpen();
            store3.setWorld(&w3);
            SaveCoordinator coord3;
            coord3.bind(&store3, db);
            SaveRequest rq;
            rq.name = QStringLiteral("r2099d-legacy");
            rq.chests = chestWith(10);
            rq.playerData = playerWith(0);
            rq.progress = progressWith(3);
            const SaveReceipt rs = coord3.saveAll(rq);
            const SaveGenerationInfo fin = coord3.recover();
            store3.closeWorld();
            const Faces f2 = readFaces(db, 9);
            const bool legOk = opened && bare && info.state == SaveRecoveryState::Fresh
                && info.generation == 0
                && f1.opened && f1.chestItems == 0 && f1.invItems == 10 && f1.progressStat == 1
                && open3 && rs.ok() && fin.state == SaveRecoveryState::Clean
                && fin.generation == 1 && fin.completeGeneration == 1
                && f2.opened && f2.chestItems == 10 && f2.invItems == 0 && f2.progressStat == 3;
            ok = ok && legOk;
            if (!legOk)
                diag += QStringLiteral("[d1 open=%1 bare=%2 st=%3 chest=%4 inv=%5 stat=%6"
                                       " rok=%7 st2=%8 g=%9/%10 f2=%11/%12/%13] ")
                            .arg(opened).arg(bare).arg(int(info.state))
                            .arg(f1.chestItems).arg(f1.invItems).arg(f1.progressStat)
                            .arg(rs.ok()).arg(int(fin.state)).arg(fin.generation)
                            .arg(fin.completeGeneration).arg(f2.chestItems).arg(f2.invItems)
                            .arg(f2.progressStat);
            QFile::remove(db);
        }

        // (2) 手工最小库（user_version 0 + 仅 meta/chunks/player_state/progress 四表 + 手插行）。
        {
            const QString db = tempDb("d2");
            QFile::remove(db); // fresh
            const QString conn = QStringLiteral("r2099_d2_%1").arg(QCoreApplication::applicationPid());
            bool built = false;
            {
                if (QSqlDatabase::contains(conn))
                    QSqlDatabase::removeDatabase(conn);
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                p.setDatabaseName(db);
                if (p.open()) {
                    QSqlQuery q(p);
                    built = q.exec(QStringLiteral(
                        "CREATE TABLE world_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))
                        && q.exec(QStringLiteral(
                            "CREATE TABLE chunks (cx INTEGER NOT NULL, cz INTEGER NOT NULL,"
                            " voxels BLOB NOT NULL, states BLOB NOT NULL, light BLOB NOT NULL,"
                            " PRIMARY KEY (cx, cz))"))
                        && q.exec(QStringLiteral(
                            "CREATE TABLE player_state (id INTEGER PRIMARY KEY DEFAULT 0,"
                            " data TEXT NOT NULL)"))
                        && q.exec(QStringLiteral(
                            "INSERT INTO world_meta (key, value) VALUES ('name', 'legacy-min')"))
                        && q.exec(QStringLiteral(
                            "INSERT INTO world_meta (key, value) VALUES ('seed', '82')"))
                        && q.exec(QStringLiteral(
                            "INSERT INTO player_state (id, data) VALUES (0, '{\"inv\":"
                            "[{\"id\":3,\"count\":10}],\"px\":11.5}')"));
                }
            }
            QSqlDatabase::removeDatabase(conn);
            // 新码开库：幂等补表（chests/furnaces/.../save_coord 全 IF NOT EXISTS）+ 读行原样。
            const Faces f1 = readFaces(db, 0);
            const bool minOk = built && f1.opened && f1.chestItems == 0 && f1.invItems == 10;
            ok = ok && minOk;
            if (!minOk)
                diag += QStringLiteral("[d2 built=%1 opened=%2 inv=%3] ")
                            .arg(built).arg(f1.opened).arg(f1.invItems);
            // 其上统一保存：容器表补建后落盘 + 读回更新（旧行零丢失面 = name 键原样幸存）。
            World w3;
            freshWorld48(w3);
            WorldStore store3;
            const bool open3 = store3.openWorld(db) && store3.isOpen();
            store3.setWorld(&w3);
            SaveCoordinator coord3;
            coord3.bind(&store3, db);
            SaveRequest rq;
            rq.name = QStringLiteral("legacy-min");
            rq.chests = chestWith(6);
            rq.playerData = playerWith(4);
            rq.progress = progressWith(5);
            const SaveReceipt rs = coord3.saveAll(rq);
            store3.closeWorld();
            const Faces f2 = readFaces(db, 9);
            QVariantMap meta2;
            {
                const QString c2 = QStringLiteral("r2099_d2m_%1").arg(QCoreApplication::applicationPid());
                if (QSqlDatabase::contains(c2))
                    QSqlDatabase::removeDatabase(c2);
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), c2);
                p.setDatabaseName(db);
                if (p.open()) {
                    QSqlQuery q(p);
                    if (q.exec(QStringLiteral("SELECT key, value FROM world_meta")))
                        while (q.next())
                            meta2.insert(q.value(0).toString(), q.value(1).toString());
                }
                QSqlDatabase::removeDatabase(c2);
            }
            const bool overOk = open3 && rs.ok() && f2.chestItems == 6 && f2.invItems == 4
                && f2.chestItems + f2.invItems == 10 && f2.progressStat == 5
                && meta2.value(QStringLiteral("name")).toString() == QStringLiteral("legacy-min");
            ok = ok && overOk;
            if (!overOk)
                diag += QStringLiteral("[d2over open=%1 rok=%2 chest=%3 inv=%4 stat=%5 name=%6] ")
                            .arg(open3).arg(rs.ok()).arg(f2.chestItems).arg(f2.invItems)
                            .arg(f2.progressStat)
                            .arg(meta2.value(QStringLiteral("name")).toString());
            QFile::remove(db);
        }

        // (3) 旧中断标记库（手工置 gen>complete = 旧代中断形态）：如实分类 + 新码首存收敛。
        {
            const QString db = tempDb("d3");
            QFile::remove(db); // fresh
            World w;
            freshWorld48(w);
            WorldStore store;
            const bool opened = store.openWorld(db) && store.isOpen();
            store.setWorld(&w);
            SaveCoordinator coord;
            coord.bind(&store, db);
            SaveRequest ra;
            ra.name = QStringLiteral("r2099d-int");
            ra.chests = chestWith(0);
            ra.playerData = playerWith(10);
            ra.progress = progressWith(1);
            const bool base = coord.saveAll(ra).ok();
            // 手工置中断形态（gen=7 > complete=3——旧代码代次历史的任意合法快照）。
            bool stamped = false;
            {
                const QString conn = QStringLiteral("r2099_d3_%1").arg(QCoreApplication::applicationPid());
                if (QSqlDatabase::contains(conn))
                    QSqlDatabase::removeDatabase(conn);
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                p.setDatabaseName(db);
                if (p.open()) {
                    QSqlQuery q(p);
                    q.prepare(QStringLiteral(
                        "UPDATE save_coord SET value='7' WHERE key='generation'"));
                    q.exec();
                    QSqlQuery q2(p);
                    q2.prepare(QStringLiteral(
                        "UPDATE save_coord SET value='3' WHERE key='complete_generation'"));
                    stamped = q2.exec();
                }
                QSqlDatabase::removeDatabase(conn);
            }
            const SaveGenerationInfo finfo = coord.recover();
            const bool intOk = opened && base && stamped
                && finfo.state == SaveRecoveryState::Interrupted
                && finfo.generation == 7 && finfo.completeGeneration == 3;
            ok = ok && intOk;
            if (!intOk)
                diag += QStringLiteral("[d3 base=%1 stamped=%2 st=%3 g=%4/%5] ")
                            .arg(base).arg(stamped).arg(int(finfo.state))
                            .arg(finfo.generation).arg(finfo.completeGeneration);
            // 新码首存：从 max(7,3)+1=8 编号（历史不失真）→ 恰 8/8 Clean + 四面一致。
            SaveRequest rq = ra;
            rq.chests = chestWith(10);
            rq.playerData = playerWith(0);
            rq.progress = progressWith(4);
            const SaveReceipt rs = coord.saveAll(rq);
            const SaveGenerationInfo fnext = coord.recover();
            store.closeWorld();
            const Faces f2 = readFaces(db, 9);
            const bool convOk = rs.ok() && fnext.state == SaveRecoveryState::Clean
                && fnext.generation == 8 && fnext.completeGeneration == 8
                && f2.opened && f2.chestItems == 10 && f2.invItems == 0 && f2.progressStat == 4;
            ok = ok && convOk;
            if (!convOk)
                diag += QStringLiteral("[d3conv rok=%1 st=%2 g=%3/%4 chest=%5 inv=%6 stat=%7] ")
                            .arg(rs.ok()).arg(int(fnext.state)).arg(fnext.generation)
                            .arg(fnext.completeGeneration).arg(f2.chestItems)
                            .arg(f2.invItems).arg(f2.progressStat);
            QFile::remove(db);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2099d legacy compatibility column: the bare three-write legacy"
                             " database, the hand-built minimal pre-era database and the"
                             " hand-stamped interrupted-ledger database all load with zero"
                             " data loss, classify honestly, and converge clean under the"
                             " coordinated save with every face consistent"
                          << (ok ? QString() : diag);
    });

    // ── r2099e：结构钉族（原子域原语 + 戳入事务 + 冲洗缝转发 + QML 退役面 + 盲区钉幸存复核）────
    runLeg(QStringLiteral("r2099e structure pins (comment-stripped source pins hold the atomic"
        " multi-face save domain at its authority points: the transaction primitives and the"
        " in-transaction complete stamp live beside the marker-first attempt line in the"
        " coordinator, the store keeps the ledger domain blind with the table and key names"
        " injected by the caller, the chunk-edits store shares a single persist execution body"
        " across both shells, the session exposes the in-transaction flush and the commit-phase"
        " account clearing, the bridge forwards both flush hooks per save, the production"
        " registration closes over the session in the streaming bridge instance, the removed"
        " pre-save flush line stays out of Main with the bridge call count at exactly one and"
        " the toast copy and the unified save entry untouched, and the section row is"
        " registered)"), [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① 协调层（marker-first 幸存 + t1129 新步在位；NEG 摘面行由 a/b 持有——本腿零摘面钉）。
        const QStringList missScp = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.cpp"), {
            SrcPin("marker-first attempt line", "coordUpsert(kCoordKeyGeneration, newGen)", 1),
            SrcPin("atomic begin in unified save", "m_store->beginAtomicSave()", 1),
            SrcPin("complete stamp inside transaction",
                   "m_store->stampLedgerKeyInTxn(kCoordTable, kCoordKeyComplete, newGen)", 1),
            SrcPin("commit with per-part counting", "m_store->commitAtomicSave(partsWritten)", 1),
            SrcPin("commit-phase account hook", "m_flushCommitHook()", 1),
            SrcPin("bind restore on every exit", "m_store->setWorld(live)", 1),
        });
        for (const QString &m : missScp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missSch = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("flush hook type", "using SaveFlushHook = std::function<bool(QSqlDatabase &)>", 1),
            SrcPin("commit hook type", "using SaveFlushCommitHook = std::function<void()>", 1),
            SrcPin("flush hook setter", "void setFlushHook(SaveFlushHook hook)", 1),
            SrcPin("fault hook setter intact", "void setFaultHook(SaveFaultHook hook)", 1),
        });
        for (const QString &m : missSch) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 存储域（原子原语声明 + 盲区钉幸存复核——四词元禁出 = r2015d/r2027d 反探同门复钉）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool wsBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "SaveCoordinator")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "save_coord")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "SaveCoordinator")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "save_coord")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "flushResidentEdits")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "stream_worlds");
        ok = ok && wsBlind;
        if (!wsBlind) diag += QStringLiteral("[ws-blind] ");
        const QStringList missWsh = pinSet(srcRoot + QStringLiteral("/World/worldstore.h"), {
            SrcPin("atomic begin declaration", "bool beginAtomicSave();", 1),
            SrcPin("world part declaration", "bool writeWorldPart(", 1),
            SrcPin("player part declaration", "bool writePlayerPart(", 1),
            SrcPin("progress part declaration", "bool writeProgressPart(", 1),
            SrcPin("stamp declaration injected names", "bool stampLedgerKeyInTxn(const char *table", 1),
            SrcPin("commit declaration", "bool commitAtomicSave(int partsWritten);", 1),
            SrcPin("rollback declaration", "void rollbackAtomicSave();", 1),
            SrcPin("external work seam declaration",
                   "bool runInSaveTransaction(const std::function<bool(QSqlDatabase &)> &work);", 1),
            SrcPin("legacy unified save face intact", "Q_INVOKABLE bool saveAll(", 1),
        });
        for (const QString &m : missWsh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 附加表域（单一执行体两壳 + 同事务变体在位）。
        const QStringList missCs = pinSet(srcRoot + QStringLiteral("/World/chunkstore.cpp"), {
            SrcPin("persist execution body shared", "persistChunkIntoDb(QSqlDatabase &db", 1),
            SrcPin("own-connection shell delegates", "persistChunkIntoDb(db, m_worldId, cx, cz, chunk)", 1),
            SrcPin("on-connection shell delegates",
                   "persistChunkIntoDb(db, m_worldId, cx, cz, chunk)", 2),
            SrcPin("advance execution body shared",
                   "qint64 ChunkStore::advanceStreamSaveGenerationOn(QSqlDatabase &db)", 1),
        });
        for (const QString &m : missCs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missCsh = pinSet(srcRoot + QStringLiteral("/World/chunkstore.h"), {
            SrcPin("on-connection persist declaration",
                   "Result<void> persistChunkOn(QSqlDatabase &db, int cx, int cz, const Chunk &chunk);", 1),
            SrcPin("on-connection advance declaration",
                   "qint64 advanceStreamSaveGenerationOn(QSqlDatabase &db);", 1),
        });
        for (const QString &m : missCsh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ 会话域（同事务冲洗体 + 提交面清账体 + 键集成员）。
        const QStringList missGs = pinSet(srcRoot + QStringLiteral("/Game/gamesession.h"), {
            SrcPin("in-transaction flush body",
                   "inline bool GameSession::flushResidentEditsForSaveOn(QSqlDatabase &db)", 1),
            SrcPin("commit-phase account body",
                   "inline void GameSession::commitFlushResidentEditsOn()", 1),
            SrcPin("flush key set member", "QVector<QPair<int, int>> m_lastFlushKeys;", 1),
            SrcPin("standalone flush intact",
                   "inline bool GameSession::flushResidentEditsForSave()", 1),
        });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 桥域（双钩逐保存转发 + 生产登记闭包 + 事务冲洗执行体）。
        const QStringList missSbc = pinSet(srcRoot + QStringLiteral("/Game/savebridge.cpp"), {
            SrcPin("flush hook pass-through", "coord.setFlushHook(m_flushHook)", 1),
            SrcPin("commit hook pass-through", "coord.setFlushCommitHook(m_flushCommitHook)", 1),
            SrcPin("fault hook pass-through intact", "coord.setFaultHook(m_faultHook)", 1),
        });
        for (const QString &m : missSbc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missSb = pinSet(srcRoot + QStringLiteral("/Game/streamingbridge.cpp"), {
            SrcPin("production flush registration", "instance()->flushForSaveOn(db)", 1),
            SrcPin("production commit registration", "commitFlushResidentEditsOn()", 1),
            SrcPin("one-shot registration guard", "flushSeamRegistered", 2),
        });
        for (const QString &m : missSb) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missSbh = pinSet(srcRoot + QStringLiteral("/Game/streamingbridge.h"), {
            SrcPin("in-transaction flush face", "bool flushForSaveOn(QSqlDatabase &db);", 1),
            SrcPin("standalone flush invokable intact", "Q_INVOKABLE bool flushForSave();", 1),
        });
        for (const QString &m : missSbh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑥ Main.qml 退役面（StreamingBridge. 计数 2→1 lawful 修订 t1129——旧前置冲洗行禁出 +
        //    toast 文案/统一口进行/进入分流三幸存 + 类型盲复述）。
        {
            QFile f(srcRoot + QStringLiteral("/ui/Main.qml"));
            QString src;
            if (f.open(QIODevice::ReadOnly))
                src = QString::fromUtf8(f.readAll());
            const int streamCalls = int(src.count(QLatin1String("StreamingBridge.")));
            const bool concentrated = streamCalls == 1
                && !src.contains(QLatin1String("if (!StreamingBridge.flushForSave())"))
                && src.contains(QString::fromUtf8(
                    "上次保存未完成，已载入最后完整数据；建议立即重新保存"))
                && src.contains(QStringLiteral("return SaveBridge.saveViaCoordinator(worldStore"))
                && src.contains(QStringLiteral(
                    "if (StreamingBridge.enterWorld(theWorld, worldStore, worldClock, player, file,"))
                && !src.contains(QLatin1String("GameSession"))
                && !src.contains(QLatin1String("SaveCoordinator"))
                && !src.contains(QLatin1String("setFaultHook"))
                && !src.contains(QLatin1String("setFlushHook"));
            ok = ok && concentrated;
            if (!concentrated)
                diag += QStringLiteral("[qml stream=%1 flushline=%2 toast=%3 save=%4 enter=%5 blind=%6] ")
                            .arg(streamCalls)
                            .arg(src.contains(QLatin1String("if (!StreamingBridge.flushForSave())")))
                            .arg(src.contains(QString::fromUtf8(
                                "上次保存未完成，已载入最后完整数据；建议立即重新保存")))
                            .arg(src.contains(QStringLiteral(
                                "return SaveBridge.saveViaCoordinator(worldStore")))
                            .arg(src.contains(QStringLiteral(
                                "if (StreamingBridge.enterWorld(theWorld, worldStore, worldClock, player, file,")))
                            .arg(!src.contains(QLatin1String("GameSession"))
                                 && !src.contains(QLatin1String("SaveCoordinator"))
                                 && !src.contains(QLatin1String("setFaultHook"))
                                 && !src.contains(QLatin1String("setFlushHook")));
        }

        // ⑦ CMake 段行（置尾注册；exe 同级上级解析 = srcRoot 同门——CWD 无关）。
        {
            QFile f(QDir(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("CMakeLists.txt")));
            QString cm;
            if (f.open(QIODevice::ReadOnly))
                cm = QString::fromUtf8(f.readAll());
            const bool cmakeOk = cm.contains(QLatin1String("section92_save_consistency_t1129.cpp"));
            ok = ok && cmakeOk;
            if (!cmakeOk) diag += QStringLiteral("[cmake-row] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2099e structure pins: the atomic save domain holds at its"
                             " authority points with the in-transaction complete stamp beside"
                             " the marker-first line, the store stays ledger-blind through"
                             " injected names, both chunk-edits shells share one persist body,"
                             " the session and bridge flush seams are single-point, the"
                             " production registration is one-shot in the streaming bridge,"
                             " and the Main.qml retirement face keeps the bridge concentrated"
                             " at exactly one call with the toast and unified entry untouched"
                          << (ok ? QString() : diag);
    });
}
