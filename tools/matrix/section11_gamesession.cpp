#include "matrix_helpers.h"

#include "gamesession.h" // R20.07 被测：GameSession（固定 Tick / 暂停 / 命令委托 / WorldDelta）

// R20.07 GameSession 探针段（4 腿 r2007a-d；filter 词 "r2007"；矩阵 552→556，band 555±2 内）。
// 置尾先例沿用（接 section10，runAll 末执行）；本段自建 48×48×96 seed 82 fresh 小世界
//（section06 t973 同款 incantation，全矩阵 proven）×3（A/B 双生 + C 共享腿世界），对 rig
// 世界 w 零接触。任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.07 原文）：
//   「无头程序可以执行一段游戏 Tick」→ r2007b/c（无 QML / 无窗口 / 无 QTimer——纯 stepTick
//     泵驱动；r2007c 300 连跑稳定性即规模化证明）；
//   「QML 仍然可以通过 Adapter 玩游戏」→ r2007b 源码钉（Main.qml onTicked 桥接面原样 +
//     **零迁移阴性钉**：Main.qml 不出现 GameSession——现行玩法路径零变化由本探针常驻守卫）；
//   「旧路径与新路径结果一致」→ r2007a（同一命令序列：A=直调 World::setBlock 权威 + 直调
//     13-tick 家族；B=GameSession enqueue + stepTick → 全栅格 blockAt+stateAt 逐位一致 +
//     WorldDelta chunk 集与 A 编辑面等价）。
// 阴性轮（按性价比登记，docs R20.07 关单同步）：r2007d(1)「旁路即红」——入队未泵 tick 前
// 世界零变化（若日后把命令执行从整 tick 边界挪到 enqueue 直写，本探针常驻变红）；配合
// r2007b 源码钉（委托 World::setBlock 权威 + 13-tick 家族次序）承担「摘委托直连」的架构类
// 阴性。确定性口径：双生世界同 seed 同尺寸（worldgen 纯函数）；天气双钉 setWeatherState(0)
// + setWeatherRemainingSec(3600)（tickWeather 转换掷骰不进探针窗口——运行期 RNG 与 parity
// 无交）；13-tick 家族在裸地形稳态全早退（无水/火/作物/冰/叶），A/B 消费零 RNG 抽取。
void MatrixRun::section11_gamesession()
{
    // ── 共享 rig：fresh 小世界构造 + 双生基线恒等 + 确定性天气钉 ─────────────────────
    // section06 t973 同款四 setter（各触发一次 generate——秒级，矩阵 proven 成本）。
    const auto initTwin = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0); // Weather::Clear——双世界同态，转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针 tick 窗（≤30s 模拟时）→ 恒晴零 RNG
    };
    // 全栅格逐位比对（id + state 全网格——「世界逐位一致」验收的判定器；221k 格 ×2 面）。
    const auto gridEqual = [](World &a, World &b, QString &why) {
        for (int x = 0; x < a.width(); ++x)
            for (int z = 0; z < a.depth(); ++z)
                for (int y = 0; y < a.height(); ++y) {
                    if (a.blockAt(x, y, z) != b.blockAt(x, y, z)) {
                        why = QStringLiteral("id@(%1,%2,%3) %4 vs %5")
                                  .arg(x).arg(y).arg(z)
                                  .arg(a.blockAt(x, y, z)).arg(b.blockAt(x, y, z));
                        return false;
                    }
                    if (a.stateAt(x, y, z) != b.stateAt(x, y, z)) {
                        why = QStringLiteral("state@(%1,%2,%3) %4 vs %5")
                                  .arg(x).arg(y).arg(z)
                                  .arg(a.stateAt(x, y, z)).arg(b.stateAt(x, y, z));
                        return false;
                    }
                }
        return true;
    };
    // WorldDelta 等价（受影响 chunk 集 + 改动量 + tick——「chunk 集等价」判定器）。
    const auto deltaEq = [](const WorldDelta &a, const WorldDelta &b) {
        if (a.affectedCount != b.affectedCount || a.changedBlocks != b.changedBlocks
            || a.tick != b.tick)
            return false;
        for (int i = 0; i < a.affectedCount; ++i)
            if (!b.affects(a.affected[i]))
                return false;
        return true;
    };
    // 旧路径 tick 体（Main.qml onTicked 桥接次序逐行镜像——与 GameSession::runOneTick 同一
    // 次序表；r2007b 源码钉钉 gamesession.h 内同序）。
    const auto oldPathTick = [](World &w) {
        w.tickWaterFlow();
        w.tickLavaFlow();
        w.tickFire();
        w.tickCropGrowth();
        w.tickSugarcaneGrowth();
        w.tickFarmlandHydration();
        w.tickSaplingGrowth();
        w.tickSweetBerryBushGrowth();
        w.tickIceFreeze();
        w.tickIceMelt();
        w.tickRedstone();
        w.tickLeafDecay();
        w.tickWeather(Tick::kClockTickSecs);
    };
    // 放置列选址：heightAt 顶 +1 恒空的第一列（确定性扫描，双生世界同结果）。
    const auto placeCol = [](World &w, int x0, int z0) -> QPair<int, int> {
        for (int dz = 0; dz < 8; ++dz) {
            const int z = z0 + dz;
            const int h = w.heightAt(x0, z);
            if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h + 1, z) == 0)
                return QPair<int, int>(x0, z);
        }
        qFatal("r2007 rig: no placeable column near (%d,%d)", x0, z0);
        return QPair<int, int>(-1, -1);
    };

    World wA; // 双生 A（旧路径承载）
    initTwin(wA);
    World wB; // 双生 B（GameSession 路径承载）
    initTwin(wB);
    World wC; // r2007b/c/d 共享腿世界（时序 / 暂停 / 阴性腿——不与 A/B 比对）
    initTwin(wC);

    // ── r2007a：旧新一致——双路径命令序列 + 全栅格逐位 + WorldDelta chunk 集等价 ──────
    runLeg(QStringLiteral("r2007a dual-path consistency old vs GameSession (R20.07 GameSession):"
        " two fresh same-seed worlds (48x48x96 s82, clear weather pinned) start bit-equal"
        " (full id+state grid scan); identical command sequence [break top of column"
        " (10,z10) in chunk (0,0), place Stone above column x20 in chunk (1,1)] runs via"
        " path A (direct World::setBlock authority + 13 world-tick family x3, the Main.qml"
        " bridge order) and path B (GameSession enqueueCommand x2 then stepTick(0.3)) and"
        " the worlds stay bit-equal (full id+state grid rescan); path B tick count is 3,"
        " the tick-1 WorldDelta equals exactly the path-A edit chunk set {(0,0),(1,1)}"
        " with changedBlocks 2, deltas of ticks 2-3 are empty (steady-state family is"
        " write-free), two BlockChanged events carry the edit positions with after-ids"
        " (Air then Stone) at tick 1, and no event was dropped"), [&]() {
        bool ok = true;
        QString diag;

        // 双生基线恒等（worldgen 纯函数 → 逐位；世界不一致则后续 parity 判定无意义）：
        QString why;
        const bool baseOk = gridEqual(wA, wB, why);
        ok = ok && baseOk;
        if (!baseOk) diag += QStringLiteral("[twin-base %1] ").arg(why);

        // 选址（双生同结果；A 算 B 复用同一坐标）：
        const int hbA = wA.heightAt(10, 10);
        const bool breakOk = hbA >= 0 && hbA < wA.height() && wA.blockAt(10, hbA, 10) != 0
            && wB.blockAt(10, hbA, 10) != 0;
        const QPair<int, int> pp = placeCol(wA, 20, 20);
        const int hpA = wA.heightAt(pp.first, pp.second);
        ok = ok && breakOk;
        if (!breakOk) diag += QStringLiteral("[break-col h=%1 id=%2] ").arg(hbA).arg(wA.blockAt(10, hbA, 10));
        if (pp.first < 0) { ok = false; diag += QStringLiteral("[place-col] "); }

        if (ok) {
            // 路径 A（旧路径）：直调权威 + 直调家族（三轮）：
            wA.setBlock(10, hbA, 10, quint8(BR::Air));               // BreakBlock 命令同义直调
            wA.setBlock(pp.first, hpA + 1, pp.second, quint8(BR::Stone)); // PlaceBlock 同义直调
            for (int i = 0; i < 3; ++i)
                oldPathTick(wA);

            // 路径 B（新路径）：GameSession 编排壳（enqueue → stepTick 泵）：
            GameSession gs(wB);
            QVector<WorldDelta> deltas;
            QObject::connect(&gs, &GameSession::tickCompleted, &gs,
                [&](int, const WorldDelta &d) { deltas.push_back(d); });
            const Result<void> r1 = gs.enqueueCommand(
                Command::breakBlock(BlockPos{ 10, hbA, 10 }, 7u, 1u, 0));
            const Result<void> r2 = gs.enqueueCommand(Command::placeBlock(
                BlockPos{ pp.first, hpA + 1, pp.second }, quint8(BR::Stone), 7u, 2u, 0));
            const int stepped = gs.stepTick(0.3); // 一次泵 = 3 个整 tick（300ms）

            const bool cmdOk = r1.isOk() && r2.isOk() && stepped == 3 && gs.tick() == 3;
            ok = ok && cmdOk;
            if (!cmdOk) diag += QStringLiteral("[cmd r1=%1 r2=%2 stepped=%3 tick=%4] ")
                                    .arg(r1.isOk()).arg(r2.isOk()).arg(stepped).arg(gs.tick());

            // 世界逐位一致（验收核心：对比关键格 id/state → 升格全栅格 id+state）：
            why.clear();
            const bool gridOk = gridEqual(wA, wB, why);
            const bool cellsOk = wB.blockAt(10, hbA, 10) == quint8(BR::Air)
                && wB.blockAt(pp.first, hpA + 1, pp.second) == quint8(BR::Stone);
            ok = ok && gridOk && cellsOk;
            if (!gridOk) diag += QStringLiteral("[grid %1] ").arg(why);
            if (!cellsOk) diag += QStringLiteral("[cells] ");

            // WorldDelta chunk 集等价：tick1 delta == 路径 A 编辑面 chunk 集 {(0,0),(1,1)}：
            WorldDelta expect;
            expect.affected[0] = ChunkKey::fromWorld(10, 10, Chunk::kSize); // (0,0)
            expect.affected[1] = ChunkKey::fromWorld(20, 20, Chunk::kSize); // (1,1)
            expect.affectedCount = 2;
            expect.changedBlocks = 2;
            expect.tick = 1;
            const bool d1Ok = deltas.size() == 3 && deltaEq(deltas[0], expect)
                && gs.lastDelta().affectedCount == 0; // tick3 收口后 delta 已空（稳态零写）
            WorldDelta empty;
            empty.tick = 2;
            const bool d2Ok = deltas.size() == 3 && deltaEq(deltas[1], empty);
            empty.tick = 3;
            const bool d3Ok = deltas.size() == 3 && deltaEq(deltas[2], empty);
            ok = ok && d1Ok && d2Ok && d3Ok;
            if (!d1Ok) diag += QStringLiteral("[delta1 n=%1 cb=%2] ")
                                   .arg(deltas.value(0).affectedCount)
                                   .arg(deltas.value(0).changedBlocks);
            if (!d2Ok || !d3Ok) diag += QStringLiteral("[delta23] ");

            // 事件面：两 BlockChanged（破→Air、放→Stone；tick 1）+ 零丢弃：
            EventQueue &ev = gs.events();
            Event e1, e2;
            const bool evOk = ev.pop(e1) && ev.pop(e2) && ev.isEmpty()
                && e1.kind == EventKind::BlockChanged && e1.pos == BlockPos{ 10, hbA, 10 }
                && e1.blockId == quint8(BR::Air) && e1.tick == 1
                && e2.kind == EventKind::BlockChanged
                && e2.pos == BlockPos{ pp.first, hpA + 1, pp.second }
                && e2.blockId == quint8(BR::Stone) && e2.tick == 1
                && gs.droppedEventCount() == 0;
            ok = ok && evOk;
            if (!evOk) diag += QStringLiteral("[events] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2007a dual-path consistency: same command sequence via direct"
                             " authority+family (old) vs GameSession enqueue+stepTick (new)"
                             " lands bit-equal worlds (full id+state grid), tick-1 WorldDelta"
                             " equals the old-path edit chunk set, empty deltas on steady"
                             " ticks, BlockChanged events with after-ids, zero drops"
                          << (ok ? QString() : diag);
    });

    // ── r2007b：固定 Tick 语义（N×stepTick(0.1) == N×kClockTickMs 推进）+ 源码钉 ──────
    runLeg(QStringLiteral("r2007b fixed-tick semantics source pins (R20.07 GameSession):"
        " stepTick accumulates integral milliseconds and fires exactly one world tick per"
        " Tick::kClockTickMs - seven 0.1s pumps give seven ticks one each (return 1), a"
        " 0.25s pump gives two with 50ms carried, 0.03s gives zero (80ms carried), the"
        " first 0.04 crosses the boundary once (120ms), the second carries again (60ms),"
        " 0.09 fires the carried remainder once (150ms), and on a fresh"
        " session one 0.7s pump equals seven ticks (batch == split == N*kClockTickMs);"
        " tick count is monotonic; source pins (comment-stripped) hold the delegation to"
        " the World::setBlock authority (break+place), the 13-call tick family in exact"
        " Main.qml bridge order (sequential indexOf on prefixed calls), the tick-weather"
        " seconds unit, the kClockTickMs single authority, chunk routing via"
        " ChunkKey::fromWorld + Chunk::kSize, FIFO replay push and BlockChanged events;"
        " adapter-status-quo pins hold Main.qml onTicked bridge intact with a zero-"
        " migration negative pin (no GameSession reference in Main.qml)"), [&]() {
        bool ok = true;
        QString diag;

        // 拆分泵：N×stepTick(0.1) == N tick（每泵恰 1）：
        GameSession g1(wC);
        bool splitOk = true;
        for (int i = 0; i < 7; ++i)
            splitOk = splitOk && g1.stepTick(0.1) == 1 && g1.tick() == i + 1;
        ok = ok && splitOk;
        if (!splitOk) diag += QStringLiteral("[split tick=%1] ").arg(g1.tick());

        // 整批 + 余量语义：0.25→2 tick（余 50ms）；0.03→0（余 80）；0.04→0（余 120→实际
        // 80+40=120 ≥100 → 1 tick 余 20。拆开钉：0.04 第一泵 0、第二泵 1——边界跨一次）：
        int got = g1.stepTick(0.25);
        const bool batchOk = got == 2 && g1.tick() == 9;
        got = g1.stepTick(0.03);
        const bool rem1Ok = got == 0 && g1.tick() == 9; // 50+30=80 余量保留
        got = g1.stepTick(0.04);
        const bool rem2Ok = got == 1 && g1.tick() == 10; // 80+40=120 ≥100 → 1 tick 余 20
        got = g1.stepTick(0.04);
        const bool rem3Ok = got == 0 && g1.tick() == 10; // 20+40=60 余量保留
        got = g1.stepTick(0.09);
        const bool rem4Ok = got == 1 && g1.tick() == 11; // 60+90=150 → 1 tick 余 50
        ok = ok && batchOk && rem1Ok && rem2Ok && rem3Ok && rem4Ok;
        if (!batchOk) diag += QStringLiteral("[batch got=%1 tick=%2] ").arg(got).arg(g1.tick());
        if (!rem1Ok || !rem2Ok || !rem3Ok || !rem4Ok)
            diag += QStringLiteral("[rem got=%1 tick=%2] ").arg(got).arg(g1.tick());

        // 批量 == 拆分：fresh 会话一次 stepTick(0.7) == 7 tick（N×kClockTickMs 推进等价）：
        GameSession g2(wC);
        const int batch7 = g2.stepTick(0.7);
        const bool eqOk = batch7 == 7 && g2.tick() == 7;
        ok = ok && eqOk;
        if (!eqOk) diag += QStringLiteral("[batch7=%1 tick=%2] ").arg(batch7).arg(g2.tick());

        // 源码钉（剥注释；委托权威 + 家族次序 + 单一权威 + chunk 路由 + 事件面）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missGs = pinSet(srcRoot + QStringLiteral("/Game/gamesession.h"), {
            // R20.08 迁移：命令写经 WorldFacade 收窄面（m_facade.setBlock ×2）——终局权威仍是
            //   World::setBlock（facade 纯转发，worldfacade.h 委托钉在 section12 r2008c）。
            SrcPin("r2007 delegate both commands to World::setBlock authority via WorldFacade", "m_facade.setBlock(c.pos", 2),
            SrcPin("r2007 fixed tick base = Tick::kClockTickMs single authority", "Tick::kClockTickMs", 1),
            SrcPin("r2007 integral-ms accumulator crosses at kClockTickMs", "m_accumMs >= kClockTickMs", 1),
            SrcPin("r2007 due-drain via CommandQueue pop (delegate queue, no bypass)", "m_commands.pop(c)", 1),
            SrcPin("r2007 deferred commands replayed in order", "m_commands.push(d)", 1),
            SrcPin("r2007 delta chunk routing ChunkKey::fromWorld x Chunk::kSize", "ChunkKey::fromWorld", 1),
            SrcPin("r2007 BlockChanged event per edit", "EventKind::BlockChanged", 1),
        });
        // 家族次序钉：13 个带前缀调用在源码文本中 indexOf 严格递增（注释不含 "m_world."
        // 前缀 → 无别名；Main.qml 桥接次序即节拍语义的承重钉）：
        QFile gf(srcRoot + QStringLiteral("/Game/gamesession.h"));
        QString gsSrc;
        if (gf.open(QIODevice::ReadOnly))
            gsSrc = QString::fromUtf8(gf.readAll());
        const char *family[] = { "m_world.tickWaterFlow();", "m_world.tickLavaFlow();",
            "m_world.tickFire();", "m_world.tickCropGrowth();", "m_world.tickSugarcaneGrowth();",
            "m_world.tickFarmlandHydration();", "m_world.tickSaplingGrowth();",
            "m_world.tickSweetBerryBushGrowth();", "m_world.tickIceFreeze();",
            "m_world.tickIceMelt();", "m_world.tickRedstone();", "m_world.tickLeafDecay();",
            "m_world.tickWeather(Tick::kClockTickSecs);" };
        int prev = -1;
        bool orderOk = gsSrc.size() > 0;
        for (const char *n : family) {
            const int at = gsSrc.indexOf(QLatin1String(n));
            if (at < 0 || at <= prev) {
                orderOk = false;
                diag += QStringLiteral("[order %1] ").arg(QLatin1String(n));
                break;
            }
            prev = at;
        }
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        ok = ok && orderOk;
        if (!orderOk && gsSrc.isEmpty()) diag += QStringLiteral("[gs-unreadable] ");

        // QML Adapter 现状钉：onTicked 桥接面原样（QML 仍经既有方式玩游戏）+ 零迁移阴性钉：
        const QString mainPath = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml"));
        const QStringList missQml = pinSet(mainPath, {
            SrcPin("r2007 adapter status quo: worldClock onTicked bridge", "function onTicked(dt)", 1),
            SrcPin("r2007 adapter status quo: world tick family bridge intact", "theWorld.tickWaterFlow()", 1),
        });
        for (const QString &m : missQml) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        QFile mf(mainPath);
        if (mf.open(QIODevice::ReadOnly)) {
            const QString qmlSrc = QString::fromUtf8(mf.readAll());
            const bool noMigOk = !qmlSrc.contains(QLatin1String("GameSession"));
            ok = ok && noMigOk;
            if (!noMigOk) diag += QStringLiteral("[zero-migration] ");
        } else {
            ok = false;
            diag += QStringLiteral("[mainqml-unreadable] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2007b fixed-tick semantics: split 7x0.1 == batch 0.7 == 7"
                             " ticks of Tick::kClockTickMs, 0.25s -> 2, sub-tick remainders"
                             " carried and fire exactly on boundary crossing, monotonic"
                             " count; source pins for World::setBlock delegation, 13-call"
                             " family in Main.qml order, seconds unit, chunk routing,"
                             " FIFO replay + events; adapter-status-quo + zero-migration"
                             " pins on Main.qml"
                          << (ok ? QString() : diag);
    });

    // ── r2007c：暂停语义 + 无头多 Tick 连跑稳定（「无头程序可执行一段游戏 Tick」规模化）─
    runLeg(QStringLiteral("r2007c pause resume headless soak (R20.07 GameSession): pause is"
        " idempotent and while paused stepTick returns 0, the tick count freezes and the"
        " discarded dt leaves no catch-up debt (resume + 0.6s pumps exactly 6 ticks, not"
        " 10); a command enqueued while paused is not executed and the world is untouched"
        " until resume, after which the next tick executes it; resume is idempotent; a"
        " 300-tick headless soak (300 x stepTick(0.1) on a real generated world, no QML /"
        " window / timer) completes stable with the monotonic tick count and zero dropped"
        " events - the plan acceptance 'a headless program can run a stretch of game"
        " ticks' demonstrated at scale"), [&]() {
        bool ok = true;
        QString diag;

        GameSession gs(wC);
        gs.pause();
        gs.pause(); // 幂等
        const bool pauseIdemOk = gs.isPaused() && gs.tick() == 0;

        // 暂停泵无效果：dt 丢弃不累积（0.5s 若被累积，复跑 0.6s 会泵出 11 而非 6）：
        const int p1 = gs.stepTick(0.5);
        const bool pauseNoopOk = p1 == 0 && gs.tick() == 0;

        // 暂停期命令入队不执行（世界零变化）：
        const QPair<int, int> pc = placeCol(wC, 30, 30);
        const int hp = wC.heightAt(pc.first, pc.second);
        const Result<void> rp = gs.enqueueCommand(
            Command::placeBlock(BlockPos{ pc.first, hp + 1, pc.second }, quint8(BR::Stone), 9u, 5u, 0));
        const bool pausedCmdOk = rp.isOk()
            && wC.blockAt(pc.first, hp + 1, pc.second) == quint8(BR::Air);

        // 复跑（幂等）：0.6s → 恰 6 tick（无暂停期 0.5s 的欠账）+ 到期命令执行：
        gs.resume();
        gs.resume(); // 幂等
        const int r6 = gs.stepTick(0.6);
        const bool resumeOk = !gs.isPaused() && r6 == 6 && gs.tick() == 6
            && wC.blockAt(pc.first, hp + 1, pc.second) == quint8(BR::Stone)
            && gs.lastDelta().affectedCount == 0; // tick6 收口：放置发生在 tick1，终态 delta 空
        ok = ok && pauseIdemOk && pauseNoopOk && pausedCmdOk && resumeOk;
        if (!pauseIdemOk) diag += QStringLiteral("[pause-idem] ");
        if (!pauseNoopOk) diag += QStringLiteral("[pause-noop p1=%1] ").arg(p1);
        if (!pausedCmdOk) diag += QStringLiteral("[paused-cmd] ");
        if (!resumeOk) diag += QStringLiteral("[resume r6=%1 tick=%2] ").arg(r6).arg(gs.tick());

        // 无头连跑：300 × stepTick(0.1) 稳定完成（真世界全家族 tick；单调；零丢弃）：
        const int before = gs.tick();
        int soak = 0;
        for (int i = 0; i < 300; ++i)
            soak += gs.stepTick(0.1);
        const bool soakOk = soak == 300 && gs.tick() == before + 300
            && gs.droppedEventCount() == 0;
        ok = ok && soakOk;
        if (!soakOk) diag += QStringLiteral("[soak got=%1 tick=%2] ").arg(soak).arg(gs.tick());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2007c pause/resume + headless soak: idempotent gates, paused"
                             " stepTick is a no-op with no time debt (resume+0.6s = exactly 6"
                             " ticks), paused-enqueued command stays dormant until resume,"
                             " 300-tick headless run on a real world completes with"
                             " monotonic ticks and zero dropped events"
                          << (ok ? QString() : diag);
    });

    // ── r2007d：阴性轮——旁路即红 + targetTick 门控 + FIFO 保序 + 满载可见 ──────────────
    runLeg(QStringLiteral("r2007d negative round enqueue bypass tick-gate FIFO capacity (R20.07"
        " GameSession): the standing remove-to-red probe - a command enqueued but never"
        " pumped leaves the world untouched (if command execution is ever moved off the"
        " integral-tick boundary to a direct write at enqueue, this leg goes red);"
        " targetTick gates execution to exactly its tick (future-dated placement is still"
        " air after tick 1 and Stone after tick 2); FIFO order is observable through world"
        " state (place-then-break ends Air, no-op-break-then-place ends Stone); the"
        " capacity policy stays visible through the session API (256 accepts then reject"
        " with kErrQueueFull, no silent overwrite)"), [&]() {
        bool ok = true;
        QString diag;

        GameSession gs(wC);

        // (1) 旁路即红（阴性主腿）：入队未泵 → 世界零变化：
        const int hb12 = wC.heightAt(12, 12);
        const quint8 before12 = wC.blockAt(12, hb12, 12);
        const Result<void> rb = gs.enqueueCommand(
            Command::breakBlock(BlockPos{ 12, hb12, 12 }, 3u, 1u, 0));
        const bool noBypassOk = rb.isOk() && gs.tick() == 0
            && wC.blockAt(12, hb12, 12) == before12 // 未泵不执行——「摘队列直写」即红
            && before12 != quint8(BR::Air);
        ok = ok && noBypassOk;
        if (!noBypassOk) diag += QStringLiteral("[bypass id=%1] ").arg(before12);

        // (2) targetTick 门控：未来票在到期 tick 恰执行一次：
        const QPair<int, int> pd = placeCol(wC, 13, 13);
        const int hpd = wC.heightAt(pd.first, pd.second);
        const Result<void> rd = gs.enqueueCommand(Command::placeBlock(
            BlockPos{ pd.first, hpd + 1, pd.second }, quint8(BR::Cobble), 3u, 2u, 2));
        gs.stepTick(0.1); // tick 1：未到期
        const bool gate1Ok = rd.isOk()
            && wC.blockAt(pd.first, hpd + 1, pd.second) == quint8(BR::Air) && gs.tick() == 1;
        gs.stepTick(0.1); // tick 2：到期执行
        const bool gate2Ok
            = wC.blockAt(pd.first, hpd + 1, pd.second) == quint8(BR::Cobble) && gs.tick() == 2;
        ok = ok && gate1Ok && gate2Ok;
        if (!gate1Ok) diag += QStringLiteral("[gate1] ");
        if (!gate2Ok) diag += QStringLiteral("[gate2] ");

        // (3) FIFO 保序经世界态可观察：P1 放→破 = 终 Air；P2 破(空操)→放 = 终 Stone：
        const QPair<int, int> p1 = placeCol(wC, 14, 14);
        const int h1 = wC.heightAt(p1.first, p1.second);
        const QPair<int, int> p2 = placeCol(wC, 15, 15);
        const int h2 = wC.heightAt(p2.first, p2.second);
        gs.enqueueCommand(Command::placeBlock(BlockPos{ p1.first, h1 + 1, p1.second },
            quint8(BR::Stone), 3u, 10u, 0));
        gs.enqueueCommand(Command::breakBlock(BlockPos{ p1.first, h1 + 1, p1.second }, 3u, 11u, 0));
        gs.enqueueCommand(Command::breakBlock(BlockPos{ p2.first, h2 + 1, p2.second }, 3u, 12u, 0));
        gs.enqueueCommand(Command::placeBlock(BlockPos{ p2.first, h2 + 1, p2.second },
            quint8(BR::Stone), 3u, 13u, 0));
        gs.stepTick(0.1); // tick 3：四命令按 FIFO 恰序执行
        const bool fifoOk = wC.blockAt(p1.first, h1 + 1, p1.second) == quint8(BR::Air) // 放后破
            && wC.blockAt(p2.first, h2 + 1, p2.second) == quint8(BR::Stone); // 破空操后放
        ok = ok && fifoOk;
        if (!fifoOk) diag += QStringLiteral("[fifo p1=%1 p2=%2] ")
                                 .arg(wC.blockAt(p1.first, h1 + 1, p1.second))
                                 .arg(wC.blockAt(p2.first, h2 + 1, p2.second));

        // (4) 满载可见（会话 API 面）：256 收纳 → 第 257 拒 kErrQueueFull（不静默挤出）：
        //    （本腿会话就此废弃——满载队列不泵，不扰共享世界 wC 的后续判定。）
        int accepted = 0;
        Result<void> last = Result<void>::ok();
        BlockPos far{ 40, 90, 40 }; // 越上界 y=90 <96? 90<96 且空 → 合法格（满载腿不泵，无副作用）
        while ((last = gs.enqueueCommand(Command::breakBlock(far, 0u, quint32(100u + accepted), 0))).isOk())
            ++accepted;
        const bool capOk = accepted == CommandQueue::kCapacity
            && !last.isOk() && last.error().code == kErrQueueFull;
        ok = ok && capOk;
        if (!capOk) diag += QStringLiteral("[cap accepted=%1 code=%2] ")
                                .arg(accepted).arg(last.error().code);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2007d negative round: standing bypass probe (enqueue without"
                             " pumping leaves the world untouched), targetTick gates to its"
                             " exact tick, FIFO order observable via final world state"
                             " (place->break = Air, break-noop->place = Stone), capacity"
                             " reject visible at session API (256 then kErrQueueFull)"
                          << (ok ? QString() : diag);
    });
}
