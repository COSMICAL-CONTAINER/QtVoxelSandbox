#include "matrix_helpers.h"

#include "savecoordinator.h" // R20.15 被测：SaveCoordinator（存档协调层，非 QObject 值语义）

// R20.15 SaveCoordinator 探针段（4 腿 r2015a-d；filter 词 "r2015"；矩阵 590→594，band 592±2 内）。
// 置尾先例沿用（接 section18，runAll 末执行）；fresh 小世界 incantation 同 section11+，临时
// SQLite 库放 QDir::temp()（pid 键名，测试自清理——t822/t1024c 先例），真实用户 saves/ 零触碰。
// 任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.15 原文五验收）：
//   「先保持 SQLiteAdapter」→ 全段前提：WorldStore 零改动（r2015d 反探钉 save_coord/
//     SaveCoordinator 禁入 worldstore.{h,cpp} + kSchemaVersion=2 正面钉），协调层只经其现有
//     Q_INVOKABLE 面驱动（saveAll/savePlayerData/saveProgress）；
//   「保存不直接读取正在变化的 WorldState」→ r2015b（冻结-持久化分离承重墙：SaveFaultHook 的
//     World 段窗 = 冻结后、持久化前的无头观察窗——窗内改动活体，重读档 = 冻结点 ≠ 活体；
//     chunk blob 三段逐字节比对）+ 损坏 chunk 注入（外部 SQL 直改 blob → worldstore 尺寸守卫
//     跳过坏块、好块照读 = 降级一致，loadChunks 返回数即损坏可见面）；
//   「玩家、世界和进度有统一 generation」→ r2015a（一次 saveAll = 一个代次覆盖三部分；saveOkCount
//     精确 +3；legacy 裸 store 写不产生/推进协调层代次——两域正交；代次单调推进）；
//   「保存失败不会报告成功」→ r2015c（真锁 BEGIN EXCLUSIVE → marker 写不进 = 零部分尝试 + 计数
//     零动 + receipt 失败；注入 Player 段 = world 新/玩家旧的部分写形态如实 Interrupted；注入
//     Finalize 段 = 各部分已写仍不得报成功——complete 戳是「成功」唯一权威）；
//   「旧档仍可读取」→ r2015a（无台账旧库 = Fresh，legacy 裸写面照常）+ r2015c（失败/中断/缺
//     generation 键各形态下重读档全部照常——缺台账删键 → Fresh，旧读路径不受影响）；
//   「故障注入测试可以模拟锁、磁盘错误和损坏 Chunk」→ r2015c（真锁 + 钩注入）+ r2015b（坏 blob）。
//   恢复状态机（Fresh/Clean/Interrupted）= r2015a（Fresh/Clean 正面）+ r2015c（Interrupted 三
//   形态 + 代次跨中断单调 1→2→3→4 不重置不复用）。
// 阴性轮恰红面设计（先于腿文定稿；R20.11 起收缩纪律）：
//   NEG1 摘 finalize 守卫（savecoordinator.cpp 完整戳条件改恒真）→ 失败保存也被盖 complete 戳 →
//     recover 全 Clean → 恰红 r2015c 单腿（r2015a/b 全成功路径戳写本就发生，不受扰）；
//   NEG2 摘冻结改绑调用点（`if (false) m_store->setWorld(m_buffer)`）→ 下游仍读活体 → 保存内容
//     = 窗内漂移后的活体 → 重读 ≠ 冻结点 → 恰红 r2015b 单腿（r2015a/c 无窗内漂移，冻结面与
//     活体面重合，不受扰）。存证 matrix_r2015_neg.log（含 restore 段）。
void MatrixRun::section19_savecoordinator()
{
    // ── 段内共享帮手（腿间无共享 rig 状态——各腿自建 fresh 世界 + 临时库）───────────────────
    // fresh 小世界 incantation（48×48×96 s82 + 天气双钉，section11+/section18 先例）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);               // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f);  // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名 + 腿标；QDir::temp()，测试自清理）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2015_%1_%2.sqlite")
                                                 .arg(QCoreApplication::applicationPid())
                                                 .arg(QLatin1String(tag)));
    };
    // 地表标记放置（t1051 选址纪律：显式读 heightAt + 放置回读校验，静默拒绝即响亮失败）。
    const auto placeMarker = [](World &w, int x, int z) -> int {
        const int h = w.heightAt(x, z);
        if (h < 0 || h + 2 >= w.height()) return -1;
        w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
        if (w.blockAt(x, h + 1, z) != quint8(BR::Stone)) return -1;
        return h + 1;
    };
    // 栅格指纹（blockAt+stateAt 全栅格，r2010d gridEqual 口径；往返逐位恒等的比较面）。
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
    // 栅格首差 diag（判别信息：坐标 + 双方 id/state）。
    const auto firstGridDelta = [](World &a, World &b) -> QString {
        for (int x = 0; x < a.width(); ++x)
            for (int z = 0; z < a.depth(); ++z)
                for (int y = 0; y < a.height(); ++y)
                    if (a.blockAt(x, y, z) != b.blockAt(x, y, z)
                        || a.stateAt(x, y, z) != b.stateAt(x, y, z))
                        return QStringLiteral("delta@(%1,%2,%3) %4/%5 vs %6/%7")
                            .arg(x).arg(y).arg(z)
                            .arg(a.blockAt(x, y, z)).arg(a.stateAt(x, y, z))
                            .arg(b.blockAt(x, y, z)).arg(b.stateAt(x, y, z));
        return QStringLiteral("<none>");
    };
    // 冻结拷贝（测试侧独立做的 chunk 三段深拷贝——与协调层内部快照同构，作 r2015b 的比对基准）。
    const auto blobCopy = [](World &w) {
        WorldSaveSnapshot s;
        s.seed = w.seed();
        s.width = w.width();
        s.height = w.height();
        s.depth = w.depth();
        const ChunkManager &cm = w.chunks();
        for (int cz = 0; cz < cm.chunksZ(); ++cz)
            for (int cx = 0; cx < cm.chunksX(); ++cx) {
                const Chunk *c = cm.chunk(cx, cz);
                if (!c) continue;
                SaveChunkBlob b;
                b.cx = cx;
                b.cz = cz;
                const size_t n = c->voxelCount();
                b.voxels = QByteArray(reinterpret_cast<const char *>(c->voxelData()), int(n));
                b.states = QByteArray(reinterpret_cast<const char *>(c->stateData()), int(n));
                b.light = QByteArray(reinterpret_cast<const char *>(c->lightData()), int(n));
                s.chunks.append(b);
            }
        return s;
    };
    // 逐字节对账：世界 vs 冻结拷贝（voxels/states/light 三段 memcmp；diag 带段名 + chunk 键）。
    const auto blobsEqual = [](World &w, const WorldSaveSnapshot &s, QString &why) {
        const ChunkManager &cm = w.chunks();
        if (s.width != w.width() || s.height != w.height() || s.depth != w.depth()) {
            why = QStringLiteral("dims %1x%2x%3 vs snap %4x%5x%6")
                      .arg(w.width()).arg(w.height()).arg(w.depth())
                      .arg(s.width).arg(s.height).arg(s.depth);
            return false;
        }
        for (const SaveChunkBlob &b : s.chunks) {
            const Chunk *c = cm.chunk(b.cx, b.cz);
            if (!c) {
                why = QStringLiteral("missing chunk (%1,%2)").arg(b.cx).arg(b.cz);
                return false;
            }
            const size_t n = c->voxelCount();
            const QByteArray *arrs[3] = { &b.voxels, &b.states, &b.light };
            const char *names[3] = { "voxels", "states", "light" };
            for (int k = 0; k < 3; ++k) {
                if (size_t(arrs[k]->size()) != n
                    || std::memcmp(k == 0 ? c->voxelData() : k == 1 ? c->stateData() : c->lightData(),
                                   arrs[k]->constData(), n) != 0) {
                    why = QStringLiteral("%1 mismatch @chunk(%2,%3)").arg(QLatin1String(names[k]))
                              .arg(b.cx).arg(b.cz);
                    return false;
                }
            }
        }
        return true;
    };
    // 玩家态 / 进度载荷（裸原语形状，t974 makePlayerT974 先例的最小版）。
    const auto playerAt = [](double px) {
        QVariantMap m;
        m.insert(QStringLiteral("px"), px);
        m.insert(QStringLiteral("py"), 41.0);
        m.insert(QStringLiteral("pz"), 8.5);
        return m;
    };
    const auto progressAt = [](int minutes) {
        QVariantMap m;
        m.insert(QStringLiteral("statPlayedMinutes"), minutes);
        return m;
    };
    // 重读档 incantation（r2010d rebind 教训钉在腿文：setWorld 先于 loadChunks）。
    const auto reloadWorld = [](WorldStore &store, World &target, int expectChunks, QString &why) {
        target.beginLoad(82); // 零填充分区网格（loadChunks 前置，同 Main.qml 载入流）
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

    // ── r2015a：统一 SaveGeneration 代次语义（验收② + 恢复态 Fresh/Clean 正面 + 两域正交）────
    runLeg(QStringLiteral("r2015a unified SaveGeneration across the world/player/progress save"
        " trio (R20.15 SaveCoordinator): one coordinator saveAll consumes exactly one generation"
        " that covers all three parts in a single receipt (world+player+progress, saveOkCount"
        " precisely +3), the ledger records generation=complete_generation=1 and recovery reads"
        " Clean; a bare WorldStore save before any coordinator activity leaves the ledger Fresh"
        " (pre-coordinator old saves are a legal clean form) and bare store-only saves afterwards"
        " never mint or advance coordinator generations (the two domains are orthogonal); a"
        " second unified save advances monotonically to generation 2 with the player data and"
        " progress round-tripping under each generation"), [&]() {
        bool ok = true;
        QString diag;

        World w1;
        freshWorld48(w1);
        const int mA = placeMarker(w1, 10, 10);
        ok = ok && mA > 0;
        if (mA <= 0) diag += QStringLiteral("[site] ");

        WorldStore store;
        store.setWorld(&w1);
        const QString db = tempDb("a");
        QFile::remove(db);
        SaveCoordinator coord;
        coord.bind(&store, db);

        // ① legacy 裸 store 保存（协调层之前形态）：代次域必须纹丝不动（Fresh 0/0）。
        ok = ok && store.openWorld(db);
        const SaveGenerationInfo f0 = coord.recover();
        const bool f0Ok = f0.state == SaveRecoveryState::Fresh && f0.generation == 0
            && f0.completeGeneration == 0;
        ok = ok && f0Ok;
        if (!f0Ok)
            diag += QStringLiteral("[f0 state=%1 gen=%2 comp=%3] ")
                        .arg(int(f0.state)).arg(f0.generation).arg(f0.completeGeneration);

        const int c0 = store.saveOkCount();
        const bool legacy1 = store.saveAll(QStringLiteral("r2015a-legacy"));
        const int c1 = store.saveOkCount();
        const SaveGenerationInfo f1 = coord.recover();
        const bool legacyOk = legacy1 && c1 == c0 + 1
            && f1.state == SaveRecoveryState::Fresh && f1.generation == 0;
        ok = ok && legacyOk;
        if (!legacyOk)
            diag += QStringLiteral("[legacy s=%1 c=%2/%3 fgen=%4] ")
                        .arg(legacy1).arg(c1).arg(c0 + 1).arg(f1.generation);

        // ② 统一保存 #1：一个代次覆盖 world+player+progress（计数恰 +3 = 三部分各一）。
        SaveRequest rq1;
        rq1.name = QStringLiteral("r2015a");
        rq1.playerData = playerAt(44.5);
        rq1.progress = progressAt(7);
        const SaveReceipt rc1 = coord.saveAll(rq1);
        const bool rc1Ok = rc1.ok() && rc1.generation == 1 && rc1.worldSaved && rc1.playerSaved
            && rc1.progressSaved && store.saveOkCount() == c1 + 3;
        ok = ok && rc1Ok;
        if (!rc1Ok)
            diag += QStringLiteral("[rc1 ok=%1 gen=%2 w=%3 p=%4 pr=%5 c=%6/%7 err=%8] ")
                        .arg(rc1.ok()).arg(rc1.generation).arg(rc1.worldSaved)
                        .arg(rc1.playerSaved).arg(rc1.progressSaved)
                        .arg(store.saveOkCount()).arg(c1 + 3).arg(rc1.error.code);

        const SaveGenerationInfo f2 = coord.recover();
        const bool f2Ok = f2.state == SaveRecoveryState::Clean && f2.generation == 1
            && f2.completeGeneration == 1;
        ok = ok && f2Ok;
        if (!f2Ok)
            diag += QStringLiteral("[f2 state=%1 gen=%2 comp=%3] ")
                        .arg(int(f2.state)).arg(f2.generation).arg(f2.completeGeneration);

        // 三部分数据面（统一代次下的三块同点落地）：
        const QVariantMap pd1 = store.loadPlayerData();
        const QVariantMap pr1 = store.loadProgress();
        const bool data1Ok = store.hasChunks() && store.hasPlayerData()
            && pd1.value(QStringLiteral("px")).toDouble() == 44.5
            && pr1.value(QStringLiteral("statPlayedMinutes")).toInt() == 7;
        ok = ok && data1Ok;
        if (!data1Ok)
            diag += QStringLiteral("[data1 chunks=%1 pd=%2 pr=%3] ")
                        .arg(store.hasChunks())
                        .arg(pd1.value(QStringLiteral("px")).toDouble())
                        .arg(pr1.value(QStringLiteral("statPlayedMinutes")).toInt());

        // ③ legacy 裸 store 保存再来 → 代次域仍不动（两域正交：store 不产代次，coord 不碰业务表）。
        const bool legacy2 = store.saveAll(QStringLiteral("r2015a-legacy2"));
        const SaveGenerationInfo f3 = coord.recover();
        const bool orthoOk = legacy2 && store.saveOkCount() == c1 + 4
            && f3.state == SaveRecoveryState::Clean && f3.generation == 1
            && f3.completeGeneration == 1;
        ok = ok && orthoOk;
        if (!orthoOk)
            diag += QStringLiteral("[ortho s=%2 fgen=%3 fcomp=%4] ")
                        .arg(legacy2).arg(f3.generation).arg(f3.completeGeneration);

        // ④ 统一保存 #2 → 代次单调 2（中断/保存不重置不复用的单调基线面）。
        SaveRequest rq2;
        rq2.name = QStringLiteral("r2015a");
        rq2.playerData = playerAt(45.5);
        rq2.progress = progressAt(8);
        const SaveReceipt rc2 = coord.saveAll(rq2);
        const SaveGenerationInfo f4 = coord.recover();
        const QVariantMap pd2 = store.loadPlayerData();
        const QVariantMap pr2 = store.loadProgress();
        const bool rc2Ok = rc2.ok() && rc2.generation == 2
            && f4.state == SaveRecoveryState::Clean && f4.generation == 2
            && f4.completeGeneration == 2
            && pd2.value(QStringLiteral("px")).toDouble() == 45.5
            && pr2.value(QStringLiteral("statPlayedMinutes")).toInt() == 8
            && store.saveOkCount() == c1 + 7;
        ok = ok && rc2Ok;
        if (!rc2Ok)
            diag += QStringLiteral("[rc2 ok=%1 gen=%2 f=%3/%4 px=%5 pr=%6 c=%7/%8] ")
                        .arg(rc2.ok()).arg(rc2.generation).arg(f4.generation)
                        .arg(f4.completeGeneration)
                        .arg(pd2.value(QStringLiteral("px")).toDouble())
                        .arg(pr2.value(QStringLiteral("statPlayedMinutes")).toInt())
                        .arg(store.saveOkCount()).arg(c1 + 7);

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2015a unified SaveGeneration: one coordinator save = one"
                             " generation covering world/player/progress (+3 exact), ledger"
                             " Clean 1/1 then 2/2 monotonic, bare store saves leave the ledger"
                             " Fresh and never mint generations (orthogonal domains), player"
                             " and progress round-trip under each generation"
                          << (ok ? QString() : diag);
    });

    // ── r2015b：保存不读正在变化的 WorldState（冻结-持久化分离承重墙）+ 损坏 chunk 注入────────
    runLeg(QStringLiteral("r2015b save never reads the changing WorldState - freeze then persist"
        " (R20.15 acceptance 'saving does not directly read a mutating WorldState'): the fault"
        " hook's World-stage window sits after the freeze and before persistence, so mutating"
        " the live world inside it (removing marker A, placing marker B) must not leak into the"
        " saved bytes - reloading yields the freeze point bit-for-bit (all three chunk blob"
        " arrays byte-equal, A present, B absent) while the live world provably drifted (A gone,"
        " B present) and the two grids differ; a corrupted chunk blob injected straight into the"
        " store (size-truncated voxels) is skipped by the store's size guard so 8 of 9 chunks"
        " load with the good chunks intact (degraded-consistent, corruption visible in the"
        " loaded count) and the generation ledger stays Clean"), [&]() {
        bool ok = true;
        QString diag;

        World w1;
        freshWorld48(w1);
        const int mAy = placeMarker(w1, 8, 8);    // chunk (0,0)
        const int hB = w1.heightAt(40, 40);       // chunk (2,2)——B 只在冻结后的窗内出现
        const int mBY = hB + 1;
        ok = ok && mAy > 0 && hB > 0 && mBY + 1 < w1.height();
        if (mAy <= 0 || hB <= 0)
            diag += QStringLiteral("[sites A=%1 hB=%2] ").arg(mAy).arg(hB);

        // 冻结基准（测试侧独立拷贝，先于任何保存）：
        const WorldSaveSnapshot frozen = blobCopy(w1);
        ok = ok && frozen.chunkCount() == 9;
        if (frozen.chunkCount() != 9) diag += QStringLiteral("[frozen n=%1] ").arg(frozen.chunkCount());

        WorldStore store;
        store.setWorld(&w1);
        const QString db = tempDb("b");
        QFile::remove(db);
        ok = ok && store.openWorld(db);

        SaveCoordinator coord;
        coord.bind(&store, db);
        int hookHits = 0;
        coord.setFaultHook([&](SaveFaultStage st) {
            if (st != SaveFaultStage::World) return false;
            ++hookHits;
            // 冻结后、持久化前：改动活体（拆 A + 放 B）。不注入失败——窗口只制造「活体漂移」。
            w1.setBlock(8, mAy, 8, quint8(BR::Air), 0);
            w1.setBlock(40, mBY, 40, quint8(BR::Stone), 0);
            return false;
        });

        SaveRequest rq;
        rq.name = QStringLiteral("r2015b");
        const SaveReceipt rc = coord.saveAll(rq);
        const bool liveDrift = w1.blockAt(8, mAy, 8) == quint8(BR::Air)
            && w1.blockAt(40, mBY, 40) == quint8(BR::Stone);
        const bool rcOk = hookHits == 1 && rc.ok() && rc.generation == 1 && rc.worldSaved
            && !rc.playerSaved && liveDrift; // 未请求部分 = false 不计错（skip 语义）
        ok = ok && rcOk;
        if (!rcOk)
            diag += QStringLiteral("[rc hits=%1 ok=%2 gen=%3 w=%4 p=%5 drift=%6 err=%7] ")
                        .arg(hookHits).arg(rc.ok()).arg(rc.generation).arg(rc.worldSaved)
                        .arg(rc.playerSaved).arg(liveDrift).arg(rc.error.code);

        // 承重断言：重读 == 冻结点（A 在、B 无），且 ≠ 活体（漂移真实发生）。
        World w2;
        freshWorld48(w2); // dims/seed 台面（beginLoad 在 reloadWorld 内零填充）
        QString why;
        const bool loadOk = reloadWorld(store, w2, 9, why);
        const bool blobsOk = loadOk && blobsEqual(w2, frozen, why);
        const bool freezeContent = blobsOk && w2.blockAt(8, mAy, 8) == quint8(BR::Stone)
            && w2.blockAt(40, mBY, 40) == quint8(BR::Air);
        const QByteArray g2 = gridOf(w2);
        const QByteArray g1 = gridOf(w1);
        const bool differsLive = freezeContent && g2 != g1;
        ok = ok && differsLive;
        if (!differsLive)
            diag += QStringLiteral("[freeze load=%1 blobs=%2 why=%3 sameAsLive=%4] ")
                        .arg(loadOk).arg(blobsOk).arg(why).arg(g2 == g1 ? 1 : 0)
                        .arg(firstGridDelta(w2, w1));

        const SaveGenerationInfo fb = coord.recover();
        ok = ok && fb.state == SaveRecoveryState::Clean && fb.generation == 1;

        // 损坏 chunk 注入（外部 SQL 直改 blob——协调层/存储层零钩子，世界侧守卫应接住）：
        {
            QSqlDatabase corruptor = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                               QStringLiteral("r2015_corruptor"));
            corruptor.setDatabaseName(db);
            const bool cOpen = corruptor.open();
            QSqlQuery cq(corruptor);
            cq.prepare(QStringLiteral("UPDATE chunks SET voxels = ? WHERE cx = 2 AND cz = 2"));
            cq.addBindValue(QByteArray(7, '\x33')); // 尺寸残缺 = 损坏 blob
            const bool cExec = cq.exec();
            ok = ok && cOpen && cExec;
            if (!cOpen || !cExec)
                diag += QStringLiteral("[corrupt open=%1 exec=%2] ").arg(cOpen).arg(cExec);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("r2015_corruptor"));

        // 降级一致：9 存 1 损 → 8 载入（损坏在返回数可见），好块内容无损。
        World w3;
        freshWorld48(w3);
        QString why3;
        const bool load3 = reloadWorld(store, w3, 8, why3);
        const bool goodIntact = load3 && w3.blockAt(8, mAy, 8) == quint8(BR::Stone);
        ok = ok && goodIntact;
        if (!goodIntact)
            diag += QStringLiteral("[corrupt-load %1 A=%2] ")
                        .arg(why3.isEmpty() ? QStringLiteral("ok") : why3)
                        .arg(w3.blockAt(8, mAy, 8));

        const SaveGenerationInfo fc = coord.recover();
        ok = ok && fc.state == SaveRecoveryState::Clean && fc.generation == 1
            && fc.completeGeneration == 1; // 外部损坏不属代次域（分类不变）

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2015b freeze-then-persist: the World-stage hook window mutates"
                             " the live world after the freeze and before persistence, the"
                             " reload is byte-identical to the freeze point (A present, B"
                             " absent) while the live world provably drifted; a size-truncated"
                             " chunk blob is skipped by the store guard (8 of 9 load, good"
                             " chunks intact, ledger untouched)"
                          << (ok ? QString() : diag);
    });

    // ── r2015c：故障注入（真锁/磁盘错误形态/崩溃前收尾/缺台账）+ 恢复三态 + 失败不报成功──────
    runLeg(QStringLiteral("r2015c fault injection and the recovery state machine (R20.15"
        " acceptance 'save failures never report success' + injected faults covering lock, disk"
        " error and crashed finalize): a real second-connection BEGIN EXCLUSIVE makes the"
        " marker-first protocol abort before any part is attempted (receipt fails with the ledger"
        " error, saveOkCount moves by exactly zero, the ledger still reads Clean at generation 1,"
        " and after release the previous save reloads bit-identical); an injected Player-stage"
        " fault lands the world part but not the player part - the mixed state is honestly"
        " classified Interrupted (generation 2 > complete 1) while the old player data stays"
        " readable (partial-write protection); an injected Finalize fault persists every part yet"
        " still must not report success (the complete stamp is the only success authority),"
        " classified Interrupted 3>1 and fully readable; the next clean save advances"
        " monotonically to generation 4 (interrupted attempts consume but never reset or reuse"
        " the counter); deleting the ledger keys reclassifies Fresh with every read path"
        " unaffected"), [&]() {
        bool ok = true;
        QString diag;

        World w1;
        freshWorld48(w1);
        const int mAy = placeMarker(w1, 12, 12);
        ok = ok && mAy > 0;
        if (mAy <= 0) diag += QStringLiteral("[site] ");
        const QByteArray gRef = gridOf(w1);

        WorldStore store;
        store.setWorld(&w1);
        const QString db = tempDb("c");
        QFile::remove(db);
        ok = ok && store.openWorld(db);

        SaveCoordinator coord;
        coord.bind(&store, db);

        // 基线 #1：三部分统一保存 → Clean 1/1。
        const int c0 = store.saveOkCount();
        SaveRequest rq1;
        rq1.name = QStringLiteral("r2015c");
        rq1.playerData = playerAt(11.5);
        rq1.progress = progressAt(1);
        const SaveReceipt rc1 = coord.saveAll(rq1);
        const SaveGenerationInfo f1 = coord.recover();
        const bool baseOk = rc1.ok() && rc1.generation == 1
            && f1.state == SaveRecoveryState::Clean && f1.generation == 1
            && f1.completeGeneration == 1 && store.saveOkCount() == c0 + 3;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base ok=%1 gen=%2 state=%3 c=%4/%5] ")
                        .arg(rc1.ok()).arg(rc1.generation).arg(int(f1.state))
                        .arg(store.saveOkCount()).arg(c0 + 3);

        // (a) 真锁（t974 先例：第二连接 BEGIN EXCLUSIVE 瞬持库写锁）：
        //   marker-first 第一闸 = 台账写不进 → 零部分尝试、receipt 失败、计数零动。
        {
            QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                            QStringLiteral("r2015_locker"));
            locker.setDatabaseName(db);
            const bool lOpen = locker.open();
            QSqlQuery lq(locker);
            const bool lLock = lOpen && lq.exec(QStringLiteral("BEGIN EXCLUSIVE"));
            SaveRequest rq2;
            rq2.name = QStringLiteral("r2015c");
            rq2.playerData = playerAt(22.5);
            rq2.progress = progressAt(2);
            const SaveReceipt rc2 = coord.saveAll(rq2);
            const int cA = store.saveOkCount();
            const bool lockOk = lOpen && lLock && !rc2.ok()
                && rc2.error.code == kErrSaveCoordSql && rc2.generation == 0
                && cA == c0 + 3; // 计数零动 = marker-first 零部分尝试（接受③失败不报成功的真锁面）
            ok = ok && lockOk;
            if (!lockOk)
                diag += QStringLiteral("[lock open=%1 lck=%2 rok=%3 code=%4 gen=%5 c=%6] ")
                            .arg(lOpen).arg(lLock).arg(rc2.ok()).arg(rc2.error.code)
                            .arg(rc2.generation).arg(cA);
            lq.exec(QStringLiteral("ROLLBACK")); // 释放（QSqlQuery 先于连接销毁出作用域）
            locker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("r2015_locker"));

        // 锁释放后：台账仍是 Clean 1/1（未记录在途尝试）+ 旧档照常读（验收④经失败路径）。
        {
            const SaveGenerationInfo fa = coord.recover();
            const bool faOk = fa.state == SaveRecoveryState::Clean && fa.generation == 1
                && fa.completeGeneration == 1;
            ok = ok && faOk;
            if (!faOk)
                diag += QStringLiteral("[fa state=%1 gen=%2] ").arg(int(fa.state)).arg(fa.generation);
        }
        World wL;
        freshWorld48(wL);
        QString whyL;
        const bool loadA = reloadWorld(store, wL, 9, whyL);
        store.setWorld(&w1); // 恢复下游活体绑定（后续协调保存的冻结源仍是 w1）
        const QVariantMap pdA = store.loadPlayerData();
        const QVariantMap prA = store.loadProgress();
        const bool oldReadable = loadA && gridOf(wL) == gRef
            && wL.blockAt(12, mAy, 12) == quint8(BR::Stone)
            && pdA.value(QStringLiteral("px")).toDouble() == 11.5
            && prA.value(QStringLiteral("statPlayedMinutes")).toInt() == 1;
        ok = ok && oldReadable;
        if (!oldReadable)
            diag += QStringLiteral("[oldread load=%1 %2 px=%3 pr=%4] ")
                        .arg(whyL.isEmpty() ? QStringLiteral("ok") : whyL)
                        .arg(firstGridDelta(wL, w1))
                        .arg(pdA.value(QStringLiteral("px")).toDouble())
                        .arg(prA.value(QStringLiteral("statPlayedMinutes")).toInt());

        // (b) 注入 Player 段（磁盘错误形态）：world 部分落、player 部分未落 = 部分写形态，
        //   如实 Interrupted（gen2>complete1）；旧 player 数据照读（短路：progress 不试）。
        coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Player; });
        SaveRequest rq3;
        rq3.name = QStringLiteral("r2015c");
        rq3.playerData = playerAt(33.5);
        rq3.progress = progressAt(3);
        const SaveReceipt rc3 = coord.saveAll(rq3);
        const SaveGenerationInfo fb3 = coord.recover();
        const int c3 = store.saveOkCount();
        const bool partOk = !rc3.ok() && rc3.error.code == kErrSaveFaultInjected
            && rc3.generation == 2 && rc3.worldSaved && !rc3.playerSaved && !rc3.progressSaved
            && c3 == c0 + 4 // 只 world 部分计入（+1）
            && fb3.state == SaveRecoveryState::Interrupted && fb3.generation == 2
            && fb3.completeGeneration == 1;
        ok = ok && partOk;
        if (!partOk)
            diag += QStringLiteral("[part rok=%3 code=%4 gen=%5 w=%6 p=%7 pr=%8 c=%9/%10"
                                   " st=%11 g=%12/%13] ")
                        .arg(rc3.ok()).arg(rc3.error.code).arg(rc3.generation)
                        .arg(rc3.worldSaved).arg(rc3.playerSaved).arg(rc3.progressSaved)
                        .arg(c3).arg(c0 + 4).arg(int(fb3.state))
                        .arg(fb3.generation).arg(fb3.completeGeneration);
        {
            const bool load3 = reloadWorld(store, wL, 9, whyL);
            store.setWorld(&w1);
            const QVariantMap pd3 = store.loadPlayerData();
            const QVariantMap pr3 = store.loadProgress();
            const bool mixedReadable = load3 && gridOf(wL) == gRef
                && pd3.value(QStringLiteral("px")).toDouble() == 11.5 // player 部分未落 = 旧值
                && pr3.value(QStringLiteral("statPlayedMinutes")).toInt() == 1;
            ok = ok && mixedReadable;
            if (!mixedReadable)
                diag += QStringLiteral("[partread load=%1 px=%2 pr=%3] ")
                            .arg(whyL.isEmpty() ? QStringLiteral("ok") : whyL)
                            .arg(pd3.value(QStringLiteral("px")).toDouble())
                            .arg(pr3.value(QStringLiteral("statPlayedMinutes")).toInt());
        }

        // (c) 注入 Finalize 段（崩溃在收尾前）：各部分**已写**仍不得报成功——complete 戳是唯一
        //   成功权威；分类 Interrupted 3>1；中断档照常读（数据完好，分类在台账面）。
        coord.setFaultHook([](SaveFaultStage st) { return st == SaveFaultStage::Finalize; });
        SaveRequest rq4;
        rq4.name = QStringLiteral("r2015c");
        rq4.playerData = playerAt(44.5);
        rq4.progress = progressAt(4);
        const SaveReceipt rc4 = coord.saveAll(rq4);
        const SaveGenerationInfo fc4 = coord.recover();
        const int c4 = store.saveOkCount();
        const bool crashOk = !rc4.ok() && rc4.error.code == kErrSaveFaultInjected
            && rc4.generation == 3 && rc4.worldSaved && rc4.playerSaved && rc4.progressSaved
            && c4 == c0 + 7 // 三部分全计（+3）
            && fc4.state == SaveRecoveryState::Interrupted && fc4.generation == 3
            && fc4.completeGeneration == 1;
        ok = ok && crashOk;
        if (!crashOk)
            diag += QStringLiteral("[crash rok=%1 code=%2 gen=%3 w=%4 p=%5 pr=%6 c=%7/%8"
                                   " st=%9 g=%10/%11] ")
                        .arg(rc4.ok()).arg(rc4.error.code).arg(rc4.generation)
                        .arg(rc4.worldSaved).arg(rc4.playerSaved).arg(rc4.progressSaved)
                        .arg(c4).arg(c0 + 7).arg(int(fc4.state))
                        .arg(fc4.generation).arg(fc4.completeGeneration);
        {
            const bool load4 = reloadWorld(store, wL, 9, whyL);
            store.setWorld(&w1);
            const QVariantMap pd4 = store.loadPlayerData();
            const QVariantMap pr4 = store.loadProgress();
            const bool interruptedReadable = load4 && gridOf(wL) == gRef
                && pd4.value(QStringLiteral("px")).toDouble() == 44.5
                && pr4.value(QStringLiteral("statPlayedMinutes")).toInt() == 4;
            ok = ok && interruptedReadable;
            if (!interruptedReadable)
                diag += QStringLiteral("[intread load=%1 px=%2 pr=%3] ")
                            .arg(whyL.isEmpty() ? QStringLiteral("ok") : whyL)
                            .arg(pd4.value(QStringLiteral("px")).toDouble())
                            .arg(pr4.value(QStringLiteral("statPlayedMinutes")).toInt());
        }

        // (e) 收敛：无钩统一保存 → 代次跨中断单调推进到 4（2/3 中断号消耗但不重置不复用）。
        coord.setFaultHook(SaveFaultHook()); // 清钩 = 生产形态
        SaveRequest rq5;
        rq5.name = QStringLiteral("r2015c");
        rq5.playerData = playerAt(55.5);
        rq5.progress = progressAt(5);
        const SaveReceipt rc5 = coord.saveAll(rq5);
        const SaveGenerationInfo f5 = coord.recover();
        const int c5 = store.saveOkCount();
        {
            const bool load5 = reloadWorld(store, wL, 9, whyL);
            store.setWorld(&w1);
            const QVariantMap pd5 = store.loadPlayerData();
            const QVariantMap pr5 = store.loadProgress();
            const bool convOk = rc5.ok() && rc5.generation == 4
                && f5.state == SaveRecoveryState::Clean && f5.generation == 4
                && f5.completeGeneration == 4 && c5 == c0 + 10
                && load5 && gridOf(wL) == gRef
                && pd5.value(QStringLiteral("px")).toDouble() == 55.5
                && pr5.value(QStringLiteral("statPlayedMinutes")).toInt() == 5;
            ok = ok && convOk;
            if (!convOk)
                diag += QStringLiteral("[conv rok=%1 gen=%2 st=%3 c=%4/%5 load=%6 px=%7] ")
                            .arg(rc5.ok()).arg(rc5.generation).arg(int(f5.state))
                            .arg(c5).arg(c0 + 10).arg(whyL.isEmpty() ? QStringLiteral("ok") : whyL)
                            .arg(pd5.value(QStringLiteral("px")).toDouble());
        }

        // (d) 缺 generation 键（台账被清——异常形态）：Fresh 分类 + 全部旧读路径不受影响。
        {
            QSqlDatabase wiper = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                           QStringLiteral("r2015_wiper"));
            wiper.setDatabaseName(db);
            const bool wOpen = wiper.open();
            QSqlQuery wq(wiper);
            const bool wExec = wOpen && wq.exec(QStringLiteral("DELETE FROM save_coord"));
            ok = ok && wOpen && wExec;
            if (!wOpen || !wExec)
                diag += QStringLiteral("[wipe open=%1 exec=%2] ").arg(wOpen).arg(wExec);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("r2015_wiper"));
        {
            const SaveGenerationInfo fd = coord.recover();
            const bool load6 = reloadWorld(store, wL, 9, whyL);
            store.setWorld(&w1);
            const QVariantMap pd6 = store.loadPlayerData();
            const QVariantMap pr6 = store.loadProgress();
            const bool freshOk = fd.state == SaveRecoveryState::Fresh && fd.generation == 0
                && fd.completeGeneration == 0 && load6 && gridOf(wL) == gRef
                && pd6.value(QStringLiteral("px")).toDouble() == 55.5
                && pr6.value(QStringLiteral("statPlayedMinutes")).toInt() == 5;
            ok = ok && freshOk;
            if (!freshOk)
                diag += QStringLiteral("[fresh st=%1 g=%2/%3 load=%4 px=%5] ")
                            .arg(int(fd.state)).arg(fd.generation).arg(fd.completeGeneration)
                            .arg(whyL.isEmpty() ? QStringLiteral("ok") : whyL)
                            .arg(pd6.value(QStringLiteral("px")).toDouble());
        }

        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2015c fault injection + recovery state machine: real EXCLUSIVE"
                             " lock aborts marker-first with zero parts attempted and zero"
                             " counter movement (old save readable after release); injected"
                             " Player fault leaves world new / player old, honestly Interrupted"
                             " 2>1; injected Finalize fault persists every part yet reports"
                             " failure (complete stamp is the only success authority),"
                             " Interrupted 3>1 and readable; clean save converges at generation"
                             " 4 (monotonic, interrupted numbers never reused); a wiped ledger"
                             " reads Fresh with every read path intact"
                          << (ok ? QString() : diag);
    });

    // ── r2015d：结构钉（QObjectFree 复述 + 协议关键步 + Adapter 全盲反探 + 零 QML 零接线）────
    runLeg(QStringLiteral("r2015d structure pins (R20.15 SaveCoordinator): compile-time"
        " restatement - SaveCoordinator, WorldSaveSnapshot, SaveRequest, SaveReceipt and"
        " SaveGenerationInfo are QObjectFree and the freeze snapshot is independently copyable"
        " with value isolation; comment-stripped source pins hold the protocol steps (single"
        " freeze capture point, snapshot replay into the frozen buffer, the two ledger write"
        " points for generation and complete_generation, the Interrupted classification, all"
        " three parts driven through the downstream adapter); reverse probes keep the"
        " SQLiteAdapter blind to the coordinator (no SaveCoordinator/save_coord in"
        " worldstore.{h,cpp}, schema version still 2, the saveAll face and the saveOkCount"
        " property untouched), keep the coordinator header free of any QML surface and Main.qml"
        " free of any SaveCoordinator mention, and keep world.h/world.cpp unwired"), [&]() {
        bool ok = true;
        QString diag;

        // ① 编译期复述（r2006b/r2014d 腿内复钉先例——头钉被删即双红）：
        static_assert(QObjectFree<SaveCoordinator>, "r2015d: SaveCoordinator must not carry QObject");
        static_assert(QObjectFree<WorldSaveSnapshot>, "r2015d: snapshot must not carry QObject");
        static_assert(QObjectFree<SaveRequest>, "r2015d: request must not carry QObject");
        static_assert(QObjectFree<SaveReceipt>, "r2015d: receipt must not carry QObject");
        static_assert(QObjectFree<SaveGenerationInfo>, "r2015d: info must not carry QObject");
        static_assert(std::is_copy_constructible_v<WorldSaveSnapshot>
                          && std::is_copy_assignable_v<WorldSaveSnapshot>,
                      "r2015d: snapshot must be independently copyable");

        // 值隔离运行探针（拷贝即深拷贝，改拷贝不回灌）：
        WorldSaveSnapshot vA;
        vA.seed = 7;
        SaveChunkBlob blob;
        blob.cx = 1;
        blob.cz = 2;
        blob.voxels = QByteArray(4, '\x01');
        blob.states = QByteArray(4, '\x02');
        blob.light = QByteArray(4, '\x03');
        vA.chunks.append(blob);
        const WorldSaveSnapshot vB = vA; // 深拷贝（拷贝面 const——隔离探针读 vA，可变探针读 vC）
        WorldSaveSnapshot vC = vA; // 可变拷贝（改拷贝不回灌的写面）
        vC.chunks[0].voxels[0] = '\x09';
        ok = ok && vB.seed == 7 && vA.chunks.at(0).voxels.at(0) == '\x01'
            && vC.chunks.at(0).voxels.at(0) == '\x09' && vA.findChunk(1, 2) != nullptr
            && vA.findChunk(1, 3) == nullptr && vA.chunkCount() == 1;

        // receipt / info / request 值面：
        SaveReceipt rc;
        rc.error = Error{ kErrSaveStoreRejected, "probe" };
        const bool receiptFail = !rc.ok();
        rc.error = Error{};
        ok = ok && receiptFail && rc.ok();
        SaveGenerationInfo gi;
        ok = ok && gi.state == SaveRecoveryState::Fresh && gi.generation == 0;
        SaveRequest sr;
        ok = ok && sr.playerData.isEmpty() && sr.chests.isEmpty();

        // ② 源码钉根（exe 相对 src/——r2007b/r2010d 同款解析）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ③ 协调层头：类型权威 + 钉在位。
        const QStringList missSch = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("r2015 coordinator class", "class SaveCoordinator", 1),
            SrcPin("r2015 recovery state authority", "enum class SaveRecoveryState", 1),
            SrcPin("r2015 recover entry", "SaveGenerationInfo recover() const", 1),
            SrcPin("r2015 unified save entry", "SaveReceipt saveAll(const SaveRequest", 1),
            SrcPin("r2015 fault hook seam", "using SaveFaultHook", 1),
            SrcPin("r2015 snapshot type", "struct WorldSaveSnapshot", 1),
            SrcPin("r2015 bind entry", "void bind(WorldStore", 1),
            SrcPin("r2015 qobjectfree pin", "static_assert(QObjectFree<SaveCoordinator>", 1),
        });
        for (const QString &m : missSch) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ 协调层实现：协议关键步唯一落位。
        const QStringList missScp = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.cpp"), {
            SrcPin("r2015 single freeze capture point", "captureSnapshot(", 2),
            SrcPin("r2015 snapshot replay step", "applySnapshotToBuffer(", 2),
            SrcPin("r2015 generation marker write point", "coordUpsert(kCoordKeyGeneration", 1),
            SrcPin("r2015 complete stamp write point", "coordUpsert(kCoordKeyComplete", 1),
            SrcPin("r2015 generation key authority", "kCoordKeyGeneration", 2),
            SrcPin("r2015 interrupted classification", "SaveRecoveryState::Interrupted", 1),
            SrcPin("r2015 player part via adapter", "m_store->savePlayerData(req.playerData)", 1),
            SrcPin("r2015 progress part via adapter", "m_store->saveProgress(req.progress)", 1),
        });
        for (const QString &m : missScp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 反探惯用法（minCount=1 空转钉恒不红——r2010d/r2014d 先例）：
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        // ⑤a 协调层头禁 QML 面（零迁移类型层）：
        const QString schPath = srcRoot + QStringLiteral("/World/savecoordinator.h");
        const bool schQmlFree = forbiddenAbsent(schPath, "Q_OBJECT")
            && forbiddenAbsent(schPath, "Q_PROPERTY")
            && forbiddenAbsent(schPath, "Q_INVOKABLE")
            && forbiddenAbsent(schPath, "QML_NAMED_ELEMENT")
            && forbiddenAbsent(schPath, "qqml");
        ok = ok && schQmlFree;
        if (!schQmlFree) diag += QStringLiteral("[coord-h-qml] ");

        // ⑤b SQLiteAdapter 对协调层全盲（保持现状的强形态——协调概念零渗入 worldstore）：
        const bool wsBlind = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "SaveCoordinator")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "save_coord")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "SaveCoordinator")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "save_coord");
        ok = ok && wsBlind;
        if (!wsBlind) diag += QStringLiteral("[adapter-not-blind] ");

        // ⑤c QML 零迁移 + World 零接线（零生产接线 = 现行保存/读档行为零变化的结构性事实）：
        const bool qmlSilent = forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "SaveCoordinator");
        const bool worldUnwired = forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "SaveCoordinator")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "SaveCoordinator");
        ok = ok && qmlSilent && worldUnwired;
        if (!qmlSilent || !worldUnwired)
            diag += QStringLiteral("[migration qml=%1 world=%2] ").arg(qmlSilent).arg(worldUnwired);

        // ⑥ SQLiteAdapter 保持现状正面钉（无不可逆格式迁移的面证据）：
        const QStringList missWsh = pinSet(srcRoot + QStringLiteral("/World/worldstore.h"), {
            SrcPin("r2015 schema version unmoved", "static constexpr int kSchemaVersion = 2", 1),
            SrcPin("r2015 adapter saveAll face intact", "Q_INVOKABLE bool saveAll(", 1),
            SrcPin("r2015 saveOkCount face intact",
                   "Q_PROPERTY(int saveOkCount READ saveOkCount NOTIFY saveOkCountChanged)", 1),
        });
        for (const QString &m : missWsh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2015d structure pins: five QObjectFree restatements plus a"
                             " value-isolation probe on the freeze snapshot; protocol steps"
                             " pinned at their single authority (capture, replay, both ledger"
                             " write points, Interrupted classification, three-part adapter"
                             " drives); reverse probes keep worldstore blind to the coordinator"
                             " with schema version 2 and both public faces intact, the"
                             " coordinator header free of QML surface, Main.qml and world.{h,cpp}"
                             " free of any mention"
                          << (ok ? QString() : diag);
    });
}
