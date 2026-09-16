#include "matrix_helpers.h"

#include "gamesession.h" // R20.07 被测：GameSession（固定 Tick / 暂停 / 命令委托 / WorldDelta）

// R20.07 GameSession 探针段（5 腿 r2007a-e；filter 词 "r2007"；矩阵 552→556，band 555±2 内；
// t1050 增 r2007e dt 钳制腿 → 569；r2017 增 r2017a/c 两腿 → 606，见尾注）。
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
            // R20.08 迁移：命令写经 WorldFacade 收窄面——R20.09 起为 setBlock（破）+
            //   setBlockWithState（放，Review #3① state 落地）双入口，共 2 处（前缀式计数；
            //   setBlockWithState 单独钉在 section12 r2008c / section13 r2009d）。
            SrcPin("r2007 delegate both commands to World::setBlock authority via WorldFacade", "m_facade.setBlock", 2),
            SrcPin("r2007 fixed tick base = Tick::kClockTickMs single authority", "Tick::kClockTickMs", 1),
            SrcPin("r2007 integral-ms accumulator crosses at kClockTickMs", "m_accumMs >= kClockTickMs", 1),
            SrcPin("r2007 due-drain via CommandQueue pop (delegate queue, no bypass)", "m_commands.pop(c)", 1),
            SrcPin("r2007 deferred commands replayed in order", "m_commands.push(d)", 1),
            // R20.09 迁移：delta chunk 路由形态（ChunkKey::fromWorld）移入 editbuffer.h
            //   （r2009d 钉）；会话面保留路由模长单一权威（构造传 Chunk::kSize，Core 叶子
            //   不自持该常量）——钉随事实迁移面走。
            SrcPin("r2007 delta chunk routing modulus = Chunk::kSize single authority", "Chunk::kSize", 1),
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

    // ── r2007e：dt 钳制（t1050 修1，Review_2026-09-15 #1 方案①入口双钳制）──────────────────
    //    (1) dt=3600s（断点续跑 / 切后台恢复量级墙钟差）→ 整体丢弃：返回 0、tick 不动、计数 +1、
    //        累积器零残留（无 catch-up 突发、无时间债）；(2) 丢弃后 0.1s 泵照常 1 tick（无污染）；
    //    (3) 负 dt → 零累积（负时间债入口焊死——后续正常 dt 不被吃）；(4) 恰上界 1.0s（== 严格
    //        大于才丢）→ 10 tick 照常。选型依据（方案① vs ②）见 gamesession.h stepTick 注。
    //    阴性轮敏感：摘负 dt 门（false && 前缀）→ (3) 红（-0.5s 入累积器成 -500ms 负债，下一泵
    //    0.1s 只到 -400 < 100 → 0 tick ≠ 1，恰红且快）；摘超界门同理 (1) 红（3600s → 36000 tick
    //    连跑，探针挂死即红）。共享 wC 稳态，(4) 十 tick 无面（r2007c 300 soak 先例）。
    runLeg(QStringLiteral("r2007e dt clamp entry guard (t1050, Review_2026-09-15 #1 option A):"
        " a 3600s stepTick (wall-clock gap scale - debugger breakpoint resume / background"
        " return) is dropped wholesale - 0 ticks executed, tick count unchanged,"
        " droppedDtCount reads 1 and the accumulator carries no residue (no catch-up"
        " burst, no time debt); a following normal 0.1s pump still fires exactly one tick"
        " (unpoisoned); a negative dt (-0.5s) is clamped to zero accumulation so the next"
        " 0.1s pump still fires exactly one tick (no negative debt eating future time);"
        " dt exactly at the 1.0s cap is accepted (ten ticks, strict-greater drop) - the"
        " int-ms accumulator input domain is bounded at entry so qRound cannot overflow"
        " (negative-round sensitive: negative-dt guard removal / over-limit guard removal)"), [&]() {
        bool ok = true;
        QString diag;

        // (1) 超界丢弃：dt=3600s → 0 tick + 计数 1 + tick 不动：
        GameSession g1(wC);
        const int big = g1.stepTick(3600.0);
        const bool bigOk = big == 0 && g1.tick() == 0 && g1.droppedDtCount() == 1;
        ok = ok && bigOk;
        if (!bigOk) diag += QStringLiteral("[big got=%1 tick=%2 drops=%3] ")
                                 .arg(big).arg(g1.tick()).arg(g1.droppedDtCount());

        // (2) 丢弃后零残留：0.1s 泵照常 1 tick（累积器未被 3600s 污染）：
        const int after = g1.stepTick(0.1);
        const bool afterOk = after == 1 && g1.tick() == 1 && g1.droppedDtCount() == 1;
        ok = ok && afterOk;
        if (!afterOk) diag += QStringLiteral("[after got=%1 tick=%2] ").arg(after).arg(g1.tick());

        // (3) 负 dt → 零累积：自身 0 tick 且**不吃后续**（-500ms 负债若入累积器，下一泵 100ms
        //     只到 -400 < 100 → 0 tick——阴性轮摘负 dt 门的恰红点）。tick 断言分两步：负泵后
        //     查一次（tick 仍 0），后续泵后再查一次（tick 1）——同语句内先泵后断会时序错位。
        GameSession g2(wC);
        const int neg = g2.stepTick(-0.5);
        const bool negSelf = neg == 0 && g2.tick() == 0; // 负泵自身零推进
        const int negAfter = g2.stepTick(0.1);
        const bool negOk = negSelf && negAfter == 1 && g2.tick() == 1; // 后续正常 dt 不被吃
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg self=%1 got=%2 after=%3 tick=%4] ")
                                 .arg(negSelf).arg(neg).arg(negAfter).arg(g2.tick());

        // (4) 恰上界不超界：1.0s（== kMaxStepSecs，严格大于才丢）→ 10 tick 照常 + 零丢弃计数：
        GameSession g3(wC);
        const int atCap = g3.stepTick(1.0);
        const bool capOk = atCap == 10 && g3.tick() == 10 && g3.droppedDtCount() == 0;
        ok = ok && capOk;
        if (!capOk) diag += QStringLiteral("[cap got=%1 tick=%2] ").arg(atCap).arg(g3.tick());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2007e dt clamp entry guard: 3600s wall-clock gap dropped"
                             " wholesale (0 ticks, droppedDtCount=1, no residue), following"
                             " normal 0.1s pump unaffected, negative dt clamped to zero"
                             " accumulation (no negative debt eating the next pump), dt at"
                             " the 1.0s cap still yields ten ticks - accumulator input"
                             " domain bounded at entry (t1050, Review_2026-09-15 #1)"
                          << (ok ? QString() : diag);
    });

    // ── r2017a：PlaceBlock state=0 同 id 异 state no-op 恢复（r2017 fix A，agent-review-
    //    2026-09-16 #1）——三相对：①同 id 异 state 格（WoodStairs 朝向 s1）+ state=0 命令 →
    //    no-op（id/state 保留、零事件零 delta、命令被消费[FIFO 后继 marker 照常执行]）；
    //    ②同格显式 state=5 → 5 参全写生效（state 落 5；同 id 仅 state 变不发 broken/placed
    //    = 既有接线语义[门开合口径]，编辑面如实不可见）+ 异格显式 state 对照（id 变 → 有
    //    事件）；③回归柱：state=0 命令在「Air 格首放」「异 id 格替换」上落 (id,0) 与旧 5 参
    //    /4 参权威逐位一致（绝对结果钉，非相对直调——直调写属 tick 外编辑，见 r2017c 窗口
    //    语义）。选址显式布 state≠0 方块（楼梯朝向/台阶半高），diag 带 pre/post id+state。
    //    MC 引证（三元组，2026-09-16 实读）：minecraft.wiki/w/Block_properties「replaceable」
    //    属性——placed "in the same location as the replaceable block replace it" + "Most
    //    blocks are not replaceable"（Java 现行机制）→ 同款不可替换方块占位 = 放置无效不改
    //    原方块 → state=0 命令恢复 4 参同 id no-op。
    //    阴性敏感（NEG-A）：摘 state==0 路由（state=0 回 5 参）→ ① 红（state 1 被强刷 0 =
    //    被摘语义本体；②③⑤在 Air/异 id/显式 state 面与 5 参逐位一致不红）。
    runLeg(QStringLiteral("r2017a PlaceBlock state=0 same-id no-op restore (r2017 fix A,"
        " agent-review 2026-09-16 #1): a state=0 command onto a cell already holding the"
        " SAME id with a non-zero state (WoodStairs facing state 1) is a no-op - id and"
        " state preserved, zero BlockChanged events, empty WorldDelta, command consumed"
        " (FIFO successor marker still executes in the same tick); an explicit state=5"
        " command on the same cell full-writes through the five-arg authority (state"
        " lands 5; a same-id state-only write emits no broken/placed so the edit face is"
        " honestly invisible, while a fresh-cell explicit-state command lands state with"
        " its event); regression columns hold: state=0 commands onto an Air cell and onto"
        " a different-id cell land (id, state 0) exactly as the pre-fix authorities did"), [&]() {
        bool ok = true;
        QString diag;

        // 选址（placeCol 列顶 +1 恒空；五格互斥，均不在本段早前腿用格上）：
        const QPair<int, int> ps = placeCol(wC, 26, 26); // ①② 同格（WoodStairs）
        const int hs = wC.heightAt(ps.first, ps.second);
        const QPair<int, int> pr = placeCol(wC, 40, 24); // ③b 异 id 替换基底（先 Stone）
        const int hr = wC.heightAt(pr.first, pr.second);
        const QPair<int, int> pm = placeCol(wC, 12, 44); // ① FIFO 消费 marker（Dirt）
        const int hm = wC.heightAt(pm.first, pm.second);
        const QPair<int, int> p2 = placeCol(wC, 28, 28); // ② 显式 state 对照格（WoodSlab s1）
        const int h2 = wC.heightAt(p2.first, p2.second);
        const QPair<int, int> pa = placeCol(wC, 30, 30); // ③a Air 格首放（placeCol 跳过占用列）
        const int ha = wC.heightAt(pa.first, pa.second);
        const bool sitesOk = ps.first >= 0 && pr.first >= 0 && pm.first >= 0 && p2.first >= 0
            && pa.first >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");

        GameSession gs(wC);
        QVector<WorldDelta> deltas;
        QObject::connect(&gs, &GameSession::tickCompleted, &gs,
            [&](int, const WorldDelta &d) { deltas.push_back(d); });

        if (ok) {
            // 相 0（tick 1）：显式 state 命令布底——S=WoodStairs 朝向 s1（5 参权威）、R=Stone：
            gs.enqueueCommand(Command::placeBlock(BlockPos{ ps.first, hs + 1, ps.second },
                quint8(BR::WoodStairs), 7u, 1u, 0, 1));
            gs.enqueueCommand(Command::placeBlock(BlockPos{ pr.first, hr + 1, pr.second },
                quint8(BR::Stone), 7u, 2u, 0));
            const int t1 = gs.stepTick(0.1);
            const quint8 sId = wC.blockAt(ps.first, hs + 1, ps.second);
            const quint8 sSt = wC.stateAt(ps.first, hs + 1, ps.second);
            const bool baseOk = t1 == 1 && deltas.size() == 1
                && deltas[0].affectedCount == 2 && deltas[0].changedBlocks == 2
                && sId == quint8(BR::WoodStairs) && sSt == 1
                && wC.blockAt(pr.first, hr + 1, pr.second) == quint8(BR::Stone);
            // 相 0 事件面收账（两 BlockChanged：Stairs@S / Stone@R，tick 1）——不清队会污染
            //   相① 的 pop 序（首版腿即此坑：pop 到 tick1 遗留事件）：
            Event eb1{}, eb2{};
            const bool baseEvOk = gs.events().pop(eb1) && gs.events().pop(eb2)
                && gs.events().isEmpty()
                && eb1.blockId == quint8(BR::WoodStairs) && eb1.tick == 1
                && eb2.blockId == quint8(BR::Stone) && eb2.tick == 1;
            ok = ok && baseOk && baseEvOk;
            if (!baseOk || !baseEvOk)
                diag += QStringLiteral("[base t=%1 d=%2/%3 S=%4/%5 ev=%6/%7] ").arg(t1)
                            .arg(deltas.value(0).affectedCount)
                            .arg(deltas.value(0).changedBlocks).arg(sId).arg(sSt)
                            .arg(int(eb1.blockId)).arg(int(eb2.blockId));
        }

        // 相 ①（tick 2）：同 id 异 state 格 + state=0 命令 → no-op（NEG-A 恰红点）：
        if (ok) {
            const quint8 preId = wC.blockAt(ps.first, hs + 1, ps.second);
            const quint8 preSt = wC.stateAt(ps.first, hs + 1, ps.second);
            gs.enqueueCommand(Command::placeBlock(BlockPos{ ps.first, hs + 1, ps.second },
                quint8(BR::WoodStairs), 7u, 3u, 0, 0)); // state=0 默认面 → 4 参 no-op
            gs.enqueueCommand(Command::placeBlock(BlockPos{ pm.first, hm + 1, pm.second },
                quint8(BR::Dirt), 7u, 4u, 0)); // FIFO marker：证 no-op 命令被消费不堵队
            const int t2 = gs.stepTick(0.1);
            const quint8 postId = wC.blockAt(ps.first, hs + 1, ps.second);
            const quint8 postSt = wC.stateAt(ps.first, hs + 1, ps.second);
            Event e{};
            const bool noopOk = t2 == 1 && deltas.size() == 2
                && deltas[1].affectedCount == 1 && deltas[1].changedBlocks == 1 // 恰 marker 一格
                && postId == preId && preId == quint8(BR::WoodStairs)
                && postSt == preSt && preSt == 1 // id/state 全保留（强刷 0 即 NEG-A 红）
                && wC.blockAt(pm.first, hm + 1, pm.second) == quint8(BR::Dirt)
                && gs.events().pop(e) && gs.events().isEmpty()
                && e.pos == BlockPos{ pm.first, hm + 1, pm.second }
                && e.blockId == quint8(BR::Dirt) && e.tick == 2;
            ok = ok && noopOk;
            if (!noopOk)
                diag += QStringLiteral("[noop pre=%1/%2 post=%3/%4 d=%5/%6 evM=%7] ")
                            .arg(preId).arg(preSt).arg(postId).arg(postSt)
                            .arg(deltas.value(1).affectedCount)
                            .arg(deltas.value(1).changedBlocks)
                            .arg(e.blockId);
        }

        // 相 ②（tick 3）：同格显式 state=5 → 5 参全写生效（state-only 写如实无事件）+ 异格
        //    显式 state=1（id 变 → 有事件——5 参权威 + 信号面双钉）：
        if (ok) {
            gs.enqueueCommand(Command::placeBlock(BlockPos{ ps.first, hs + 1, ps.second },
                quint8(BR::WoodStairs), 7u, 5u, 0, 5)); // 同 id state 1→5（5 参全写）
            gs.enqueueCommand(Command::placeBlock(BlockPos{ p2.first, h2 + 1, p2.second },
                quint8(BR::WoodSlab), 7u, 6u, 0, 1)); // Air 格显式 state（上半砖）
            const int t3 = gs.stepTick(0.1);
            const quint8 postId = wC.blockAt(ps.first, hs + 1, ps.second);
            const quint8 postSt = wC.stateAt(ps.first, hs + 1, ps.second);
            Event e{};
            const bool explicitOk = t3 == 1 && deltas.size() == 3
                && deltas[2].affectedCount == 1 && deltas[2].changedBlocks == 1 // 恰 S2 一格
                && postId == quint8(BR::WoodStairs) && postSt == 5 // state 落 5（全写生效）
                && wC.stateAt(p2.first, h2 + 1, p2.second) == 1
                && wC.blockAt(p2.first, h2 + 1, p2.second) == quint8(BR::WoodSlab)
                && gs.events().pop(e) && gs.events().isEmpty()
                && e.pos == BlockPos{ p2.first, h2 + 1, p2.second }
                && e.blockId == quint8(BR::WoodSlab) && e.tick == 3;
            ok = ok && explicitOk;
            if (!explicitOk)
                diag += QStringLiteral("[expl S=%1/%2 d=%3/%4 slab=%5/%6] ")
                            .arg(postId).arg(postSt)
                            .arg(deltas.value(2).affectedCount)
                            .arg(deltas.value(2).changedBlocks)
                            .arg(wC.blockAt(p2.first, h2 + 1, p2.second))
                            .arg(wC.stateAt(p2.first, h2 + 1, p2.second));
        }

        // 相 ③（tick 4）：回归柱——state=0 命令在 Air 格首放与异 id 替换上落 (id,0)（与旧
        //    4/5 参权威逐位一致；fix A 唯一语义变化 = 相① 的同 id 异 state no-op）：
        if (ok) {
            gs.enqueueCommand(Command::placeBlock(BlockPos{ pa.first, ha + 1, pa.second },
                quint8(BR::WoodSlab), 7u, 7u, 0, 0)); // Air 格首放 → (WoodSlab, 0) 下半
            gs.enqueueCommand(Command::placeBlock(BlockPos{ pr.first, hr + 1, pr.second },
                quint8(BR::Cobble), 7u, 8u, 0, 0)); // Stone→Cobble 替换 → (Cobble, 0)
            const int t4 = gs.stepTick(0.1);
            Event e1{}, e2{};
            const bool regOk = t4 == 1 && deltas.size() == 4
                && deltas[3].affectedCount == 2 && deltas[3].changedBlocks == 2
                && wC.blockAt(pa.first, ha + 1, pa.second) == quint8(BR::WoodSlab)
                && wC.stateAt(pa.first, ha + 1, pa.second) == 0
                && wC.blockAt(pr.first, hr + 1, pr.second) == quint8(BR::Cobble)
                && wC.stateAt(pr.first, hr + 1, pr.second) == 0
                && gs.events().pop(e1) && gs.events().pop(e2) && gs.events().isEmpty()
                && e1.blockId == quint8(BR::WoodSlab) && e2.blockId == quint8(BR::Cobble)
                && gs.droppedEventCount() == 0 && gs.droppedEditCount() == 0;
            ok = ok && regOk;
            if (!regOk)
                diag += QStringLiteral("[reg d=%1/%2 A=%3/%4 R=%5/%6] ")
                            .arg(deltas.value(3).affectedCount)
                            .arg(deltas.value(3).changedBlocks)
                            .arg(wC.blockAt(pa.first, ha + 1, pa.second))
                            .arg(wC.stateAt(pa.first, ha + 1, pa.second))
                            .arg(wC.blockAt(pr.first, hr + 1, pr.second))
                            .arg(wC.stateAt(pr.first, hr + 1, pr.second));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2017a PlaceBlock state=0 same-id no-op restore: same-id"
                             " different-state cell (WoodStairs s1) + state=0 command is a"
                             " consumed no-op (id/state kept, zero events, empty delta,"
                             " FIFO marker executes), explicit state=5 full-writes (state"
                             " lands, state-only write honestly invisible to the edit"
                             " face) with fresh-cell explicit state keeping its event,"
                             " and Air-first / different-id replacement columns land"
                             " (id, 0) unchanged (fix A, review 2026-09-16 #1)"
                          << (ok ? QString() : diag);
    });

    // ── r2017c：out-of-tick 编辑可见性（r2017 fix C，agent-review-2026-09-16 #3）——tick
    //    收口后、下一 tick 开始前到达的编辑（直写 World → blockPlaced/blockBroken → noteEdit
    //    → m_edits）在下一收口照常作为 WorldDelta + BlockChanged 发布，绝不静默丢失；注入后
    //    事件队列仍空 = R20.09「收口单点发布」钉；tick 内累积面回归柱（下一 tick 命令编辑在
    //    自身收口发布）不变。
    //    窗口语义（选型②，gamesession.h 头注立证）：「start-clear 到收口」→「上收口到本收
    //    口」，清账唯一落点 = 收口发布之后；r2009b/c 集合面单源断言已随修订移入信号栈内
    //    （双语义不变量，本腿是唯一 NEG-C 敏感面）。
    //    阴性敏感（NEG-C）：复原 runOneTick 开头 m_edits.clear() → 注入编辑在 tick 2 开头被
    //    整体抹除 → 收口 delta（aff/chg 2）与事件（Stone@P/Air@Q）双缺 → 恰红本腿。
    runLeg(QStringLiteral("r2017c out-of-tick edit visibility (r2017 fix C, agent-review"
        " 2026-09-16 #3): edits arriving after a tick close-out and before the next tick"
        " (direct World writes firing blockPlaced/blockBroken into the session's edit"
        " buffer) are published at the NEXT close-out as a WorldDelta carrying both"
        " affected chunks and two BlockChanged events (Stone placed, surface broken) -"
        " never silently dropped; before that close-out the event queue stays empty and"
        " the delta/dirty face shows the pending pair (single-point publish preserved);"
        " the in-tick accumulation face is unchanged (the next tick's command edit"
        " publishes at its own close-out)"), [&]() {
        bool ok = true;
        QString diag;

        // 选址：注入格 P（放 Stone，chunk (2,2)）、破位 Q（表面非 Air 实证 + 顶上两格净空
        //    自证——附着花草/甘蔗会级联清块 = 第三笔编辑，r2009c chg 首红同源教训）、
        //    tick 内回归柱格 K（placeCol 选列，h+1 空自证）——互斥且不在本段早前腿用格上：
        const QPair<int, int> pp = placeCol(wC, 34, 34);
        const int hp = wC.heightAt(pp.first, pp.second);
        QPair<int, int> pq(-1, -1);
        int hq = -1;
        for (int dz = 0; dz < 8 && pq.first < 0; ++dz) {
            const int z = 40 + dz;
            const int h = wC.heightAt(4, z);
            if (h >= 0 && h + 2 < wC.height() && wC.blockAt(4, h, z) != quint8(BR::Air)
                && wC.blockAt(4, h + 1, z) == 0 && wC.blockAt(4, h + 2, z) == 0) {
                pq = QPair<int, int>(4, z);
                hq = h;
            }
        }
        const QPair<int, int> kp = placeCol(wC, 8, 8);
        const int hk = kp.first >= 0 ? wC.heightAt(kp.first, kp.second) : -1;
        const bool sitesOk = pp.first >= 0 && pq.first >= 0 && kp.first >= 0
            && wC.blockAt(pp.first, hp + 1, pp.second) == quint8(BR::Air);
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites pp=%1 pq=%2 kp=%3] ")
                                 .arg(pp.first).arg(pq.first).arg(kp.first);

        GameSession gs(wC);
        QVector<WorldDelta> deltas;
        QObject::connect(&gs, &GameSession::tickCompleted, &gs,
            [&](int, const WorldDelta &d) { deltas.push_back(d); });

        // tick 1：稳态收口（建立「上一收口」时点）：
        const int t1 = gs.stepTick(0.1);
        const bool steadyOk = t1 == 1 && deltas.size() == 1
            && deltas[0].affectedCount == 0 && deltas[0].changedBlocks == 0;
        ok = ok && steadyOk;
        if (!steadyOk) diag += QStringLiteral("[steady t=%1] ").arg(t1);

        // tick 1 收口后（tick 栈外）注入：直写 World → 语义信号 → noteEdit → m_edits：
        const bool placed = wC.setBlock(pp.first, hp + 1, pp.second, quint8(BR::Stone));
        const bool broke = wC.setBlock(pq.first, hq, pq.second, quint8(BR::Air));
        ok = ok && placed && broke;
        if (!placed || !broke) diag += QStringLiteral("[inject p=%1 b=%2] ").arg(placed).arg(broke);

        // 单点发布钉：注入后、下一 tick 前事件队列恒空；账面挂起恰 2 chunk（下一窗待发布）：
        const bool singleOk = gs.events().isEmpty() && gs.lastDelta().affectedCount == 0
            && gs.lastDirtyChunks().size() == 2;
        ok = ok && singleOk;
        if (!singleOk)
            diag += QStringLiteral("[single evEmpty=%1 lastDelta=%2 pending=%3] ")
                        .arg(gs.events().isEmpty()).arg(gs.lastDelta().affectedCount)
                        .arg(gs.lastDirtyChunks().size());

        // tick 2：稳态泵——上一窗残余（out-of-tick 注入对）在本收口完整发布：
        const int t2 = gs.stepTick(0.1);
        Event e1{}, e2{};
        const bool publishedOk = t2 == 1 && deltas.size() == 2
            && deltas[1].affectedCount == 2 && deltas[1].changedBlocks == 2
            && deltas[1].tick == 2
            && deltas[1].affects(ChunkKey::fromWorld(pp.first, pp.second, Chunk::kSize))
            && deltas[1].affects(ChunkKey::fromWorld(pq.first, pq.second, Chunk::kSize))
            && gs.events().pop(e1) && gs.events().pop(e2) && gs.events().isEmpty()
            && e1.pos == BlockPos{ pp.first, hp + 1, pp.second }
            && e1.blockId == quint8(BR::Stone) && e1.tick == 2
            && e2.pos == BlockPos{ pq.first, hq, pq.second }
            && e2.blockId == quint8(BR::Air) && e2.tick == 2;
        ok = ok && publishedOk;
        if (!publishedOk)
            diag += QStringLiteral("[pub n=%1 d=%2/%3 e1@%4=%5 e2@%6=%7] ")
                        .arg(deltas.size()).arg(deltas.value(1).affectedCount)
                        .arg(deltas.value(1).changedBlocks)
                        .arg(e1.pos.x).arg(e1.blockId)
                        .arg(e2.pos.x).arg(e2.blockId);

        // tick 内累积面回归柱：tick 3 的命令编辑在自身收口发布（不与注入窗混淆）：
        gs.enqueueCommand(Command::placeBlock(BlockPos{ kp.first, hk + 1, kp.second }, quint8(BR::Stone),
            7u, 9u, 0));
        const int t3 = gs.stepTick(0.1);
        Event e3{};
        const bool inTickOk = t3 == 1 && deltas.size() == 3
            && deltas[2].affectedCount == 1 && deltas[2].changedBlocks == 1
            && deltas[2].tick == 3
            && gs.events().pop(e3) && gs.events().isEmpty()
            && e3.pos == BlockPos{ kp.first, hk + 1, kp.second } && e3.blockId == quint8(BR::Stone)
            && e3.tick == 3 && gs.droppedEventCount() == 0 && gs.droppedEditCount() == 0;
        ok = ok && inTickOk;
        if (!inTickOk)
            diag += QStringLiteral("[intick n=%1 d=%2/%3] ").arg(deltas.size())
                        .arg(deltas.value(2).affectedCount)
                        .arg(deltas.value(2).changedBlocks);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2017c out-of-tick edit visibility: edits landing after a"
                             " close-out (direct writes -> noteEdit) publish at the NEXT"
                             " close-out as one WorldDelta (both chunks) plus both"
                             " BlockChanged events, with the queue empty and the pending"
                             " pair visible before it (single-point publish kept), and"
                             " the next tick's command edit still publishes at its own"
                             " close-out (fix C, review 2026-09-16 #3)"
                          << (ok ? QString() : diag);
    });
}
