#include "matrix_helpers.h"

#include "editbuffer.h" // R20.09 被测：EditBuffer / DirtyChunkSet（Tick 内编辑合并收口）
#include "gamesession.h" // r2009b/c/d 会话面：GameSession 经 EditBuffer 委托的端到端

// R20.09 EditBuffer 探针段（4 腿 r2009a-d；filter 词 "r2009"；矩阵 560→564，band 562±2 内）。
// 置尾先例沿用（接 section12，runAll 末执行）；r2009a 纯类型腿零世界；r2009b/c/d 自建
// 48×48×96 seed 82 fresh 小世界（section11/12 同款 incantation）×4（双生 wB1/wB2 + wC1 +
// wD1），对 rig 世界 w 零接触。任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.09 原文
// 四验收）：
//   「DirtyChunkSet 可被测试」→ r2009a（独立类型纯值面直测：三态 add / 幂等去重不占位 /
//     contains / 插入序 at / 64 满载拒 / clear；EditBuffer 合并后写胜 / 三态 record /
//     takeDelta 投影 / 负坐标 chunk 路由 / 记录面满载丢弃可见）；
//   「多次相邻编辑不会产生不必要的重复通知」→ r2009b（同格三连写【放石→破→放土，一 tick
//     三条 FIFO 命令】合并为 1 条 BlockChanged（id=终态土）+ delta.changedBlocks==1 +
//     脏集 1 chunk——旧实现逐写 3 事件/changedBlocks==3 的重复通知面就此退役）；
//   「规则终态与旧实现一致」→ r2009b 双生对照（同格三连写：GameSession 路径 vs 直调
//     World::setBlock 权威 + 同 tick 数 oldPathTick——全栅格 id+state 逐位一致）；
//   「mesh 调度不再直接绑定每一次 setBlock」→ r2009c（跨 3 chunk 编辑一 tick 批量收口：
//     tickCompleted 恰一发 + delta/lastDirtyChunks 集合面一致 + 稳态 tick 收口零残留——
//     调度按**集合**一次可查；ChunkGeometry/QML 渲染路径零触碰，全量接线登记 R20.10+）；
//   附带清偿 Review_2026-09-15 #3①：r2009d（PlaceBlock 带 state 五参权威落地——state 不再
//     被静默重置 0；默认 0 向后兼容钉）。
// 阴性轮（按性价比登记）：r2009d 源码钉承担「摘 EditBuffer 委托即红」——gamesession.h 的
//   m_edits.record(/takeDelta( 剥注释计数钉 + 旧收口路径禁出反探（m_lastDelta.addAffected /
//   ++m_lastDelta.changedBlocks，minCount=1 反探 miss 非空=合规——pinSet 的 cnt<minCount
//   语义下 0 计数钉恒不红，R20.08 空转钉教训）；新钉变异自证见 matrix_r2009_neg.log。
// 确定性口径：fresh 世界同 seed 同尺寸（worldgen 纯函数）+ 天气双钉 setWeatherState(0)+
//   setWeatherRemainingSec(3600)（tickWeather 转换掷骰不进探针窗口）；裸地形稳态 13-tick
//   家族全早退（无水/火/作物/冰/叶）——编辑面只由命令写构成，双生终态可比。
void MatrixRun::section13_editbuffer()
{
    // ── 共享 rig：fresh 小世界构造 + 确定性天气钉（section11/12 同款）──────────────────
    const auto initTwin = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 放置列选址：heightAt 顶 +1 恒空的第一列（确定性扫描；同 section11/12 placeCol）。
    const auto placeCol = [](World &w, int x0, int z0) -> QPair<int, int> {
        for (int dz = 0; dz < 8; ++dz) {
            const int z = z0 + dz;
            const int h = w.heightAt(x0, z);
            if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h + 1, z) == 0)
                return QPair<int, int>(x0, z);
        }
        qFatal("r2009 rig: no placeable column near (%d,%d)", x0, z0);
        return QPair<int, int>(-1, -1);
    };
    // 破坏列选址：heightAt 顶为实方块、且**顶上方格为空气**的第一列（空操破不发 blockBroken
    // 会塌事件计数；顶上若有 TallGrass 类附着物，破块写后钩子会级联清除它 → 第 4 条编辑
    // 塌批量收口断言——首版腿缺「顶上无附着」过滤在 r2009c 首跑实证 [chg=4]，本版修正；
    // cx 分驻不同值由调用点坐标结构性保证（跨 chunk 前提））。
    const auto breakCol = [](World &w, int x0, int z0) -> QPair<int, int> {
        for (int dz = 0; dz < 8; ++dz) {
            const int z = z0 + dz;
            const int h = w.heightAt(x0, z);
            if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h, z) != 0
                && w.blockAt(x0, h + 1, z) == 0)
                return QPair<int, int>(x0, z);
        }
        return QPair<int, int>(-1, -1);
    };

    World wB1; // r2009b 双生 B1（GameSession 路径——同格三连写）
    initTwin(wB1);
    World wB2; // r2009b 双生 B2（直调 World 权威对照——终态一致判定）
    initTwin(wB2);
    World wC1; // r2009c 多 chunk 批量收口
    initTwin(wC1);
    World wD1; // r2009d blockState + 源码钉腿
    initTwin(wD1);

    // ── r2009a：类型面直测——DirtyChunkSet 可测集合 + EditBuffer 合并/三态/发布/路由 ────
    runLeg(QStringLiteral("r2009a type-face testability DirtyChunkSet + EditBuffer (R20.09"
        " EditBuffer): DirtyChunkSet is an independently testable fixed-capacity value set -"
        " empty size 0, add returns Added for a fresh key and AlreadyDirty for a duplicate"
        " (idempotent, no slot consumed), contains/at in insertion order (negative-coordinate"
        " chunk keys included), the 65th distinct key rejected Full with the 64 kept, clear"
        " resets; EditBuffer records edits with same-cell merging (later write wins: place"
        " Stone then record Dirt again on the same cell keeps one edit with the final id),"
        " distinct cells each Recorded, takeDelta projects the dirty set + merged count +"
        " tick into a WorldDelta (re-take is idempotent, buffer untouched), chunk routing"
        " follows floorDiv semantics (x=-1,z=-1 lands in chunk (-1,-1)), and the 1025th"
        " distinct cell overflows visibly (Overflowed + overflowedEdits counter, the merged"
        " edit face stays at the 1024 cap)"), [&]() {
        bool ok = true;
        QString diag;

        // ── DirtyChunkSet：可测集合面（验收③本体）──
        DirtyChunkSet ds;
        bool dsEmptyOk = ds.size() == 0 && !ds.contains(ChunkKey{ 3, 4 });
        const ChunkKey kA{ 3, 4 };
        const ChunkKey kB{ -1, 0 }; // 负坐标 chunk 键（floorDiv 语义域）
        const bool dsAddOk = ds.add(kA) == ChunkAddResult::Added
            && ds.add(kB) == ChunkAddResult::Added && ds.size() == 2
            && ds.contains(kA) && ds.contains(kB);
        const bool dsIdemOk = ds.add(kA) == ChunkAddResult::AlreadyDirty // 幂等去重不占位
            && ds.add(kB) == ChunkAddResult::AlreadyDirty && ds.size() == 2;
        const bool dsOrderOk = ds.size() == 2 && ds.at(0) == kA && ds.at(1) == kB; // 插入序
        // 满载：填满 64 后第 65 个**不同**键 → Full（不静默挤出），原集完好：
        bool dsFullOk = ds.size() == 2;
        for (int i = 0; i < DirtyChunkSet::kCapacity - 2 && dsFullOk; ++i)
            dsFullOk = ds.add(ChunkKey{ 100 + i, 100 + i }) == ChunkAddResult::Added
                && ds.size() == 2 + i + 1;
        const ChunkKey kOver{ 999, 999 };
        dsFullOk = dsFullOk && ds.size() == DirtyChunkSet::kCapacity
            && ds.add(kOver) == ChunkAddResult::Full && ds.size() == DirtyChunkSet::kCapacity
            && !ds.contains(kOver) && ds.contains(kA);
        ds.clear();
        const bool dsClearOk = ds.size() == 0 && !ds.contains(kA);
        ok = ok && dsEmptyOk && dsAddOk && dsIdemOk && dsOrderOk && dsFullOk && dsClearOk;
        if (!dsEmptyOk) diag += QStringLiteral("[ds-empty] ");
        if (!dsAddOk) diag += QStringLiteral("[ds-add] ");
        if (!dsIdemOk) diag += QStringLiteral("[ds-idem] ");
        if (!dsOrderOk) diag += QStringLiteral("[ds-order] ");
        if (!dsFullOk) diag += QStringLiteral("[ds-full size=%1] ").arg(ds.size());
        if (!dsClearOk) diag += QStringLiteral("[ds-clear] ");

        // ── EditBuffer：合并后写胜 + 三态 record + takeDelta 投影 + 负坐标路由 ──
        EditBuffer eb(16); // chunk 模长 16（caller 传单一权威——同 ChunkKey::fromWorld 纪律）
        const bool ebEmptyOk = eb.isEmpty() && eb.dirtyChunks().size() == 0
            && eb.edits().empty();
        const WorldDelta d0 = eb.takeDelta(5); // 空发布：空 delta 携 tick
        const bool ebEmptyDeltaOk = d0.affectedCount == 0 && d0.changedBlocks == 0
            && d0.tick == 5;

        const bool r1 = eb.record(10, 20, 30, quint8(BR::Stone)) == RecordResult::Recorded;
        const bool r2 = eb.record(10, 20, 30, quint8(BR::Dirt)) == RecordResult::Merged; // 同格合并
        const bool r3 = eb.record(200, 5, 30, quint8(BR::Cobble)) == RecordResult::Recorded;
        const bool r4 = eb.record(-1, 5, -1, quint8(BR::Stone)) == RecordResult::Recorded; // 负坐标
        const bool recOk = r1 && r2 && r3 && r4;
        if (!recOk) diag += QStringLiteral("[rec r1=%1 r2=%2 r3=%3 r4=%4] ").arg(r1).arg(r2).arg(r3).arg(r4);

        // 合并面：3 格 3 条（同格两写合一、id=后写土）；脏集 3 chunk（含负坐标 (-1,-1)）：
        const bool faceOk = eb.edits().size() == 3
            && eb.edits()[0].pos == BlockPos{ 10, 20, 30 } && eb.edits()[0].id == quint8(BR::Dirt)
            && eb.edits()[1].pos == BlockPos{ 200, 5, 30 } && eb.edits()[1].id == quint8(BR::Cobble)
            && eb.edits()[2].pos == BlockPos{ -1, 5, -1 } && eb.edits()[2].id == quint8(BR::Stone)
            && eb.dirtyChunks().size() == 3;
        const ChunkKey kC0 = ChunkKey::fromWorld(10, 30, 16);  // (0,1)
        const ChunkKey kC1 = ChunkKey::fromWorld(200, 30, 16); // (12,1)（floorDiv(200,16)=12）
        const ChunkKey kC2 = ChunkKey::fromWorld(-1, -1, 16);  // (-1,-1)（负坐标进「左下」）
        const bool routeOk = eb.dirtyChunks().contains(kC0) && eb.dirtyChunks().contains(kC1)
            && eb.dirtyChunks().contains(kC2) && kC1.cx == 12 && kC2.cx == -1 && kC2.cz == -1;
        ok = ok && faceOk && routeOk;
        if (!faceOk) diag += QStringLiteral("[face n=%1 id0=%2] ").arg(eb.edits().size())
                                .arg(eb.edits().empty() ? -1 : int(eb.edits()[0].id));
        if (!routeOk)
            diag += QStringLiteral("[route c0=%1/%2 c1=%3/%4 c2=%5/%6] ")
                        .arg(eb.dirtyChunks().contains(kC0)).arg(kC0.cx).arg(kC1.cx)
                        .arg(kC1.cz).arg(kC2.cx).arg(kC2.cz);

        // 发布投影：chunk 集 + 合并后计数 + tick；重复 take 幂等（const 不夺状态）：
        const WorldDelta d1 = eb.takeDelta(7);
        const WorldDelta d1b = eb.takeDelta(7);
        const bool takeOk = d1.affectedCount == 3 && d1.changedBlocks == 3 && d1.tick == 7
            && d1.affects(kC0) && d1.affects(kC1) && d1.affects(kC2) && d1b.affectedCount == 3
            && d1b.changedBlocks == 3 && eb.edits().size() == 3; // take 不清（clear 显式管）
        ok = ok && takeOk;
        if (!takeOk)
            diag += QStringLiteral("[take n=%1 chg=%2 tick=%3] ")
                        .arg(d1.affectedCount).arg(d1.changedBlocks).arg(d1.tick);

        // clear 复位（溢出累计跨 clear 保留）：
        eb.clear();
        const bool clearOk = eb.isEmpty() && eb.dirtyChunks().size() == 0
            && eb.takeDelta(0).affectedCount == 0;

        // 记录面满载：1024 不同格收纳 → 第 1025 格 Overflowed + 计数可见（编辑面恒上界）：
        EditBuffer eb2(16);
        bool capOk = true;
        for (int i = 0; i < EditBuffer::kMaxEdits && capOk; ++i)
            capOk = eb2.record(i, i, i, quint8(BR::Stone)) == RecordResult::Recorded;
        const bool overflowOk = capOk && eb2.edits().size() == EditBuffer::kMaxEdits
            && eb2.record(-1, -1, -1, quint8(BR::Stone)) == RecordResult::Overflowed
            && eb2.overflowedEdits() == 1 && eb2.edits().size() == EditBuffer::kMaxEdits;
        ok = ok && clearOk && overflowOk;
        if (!clearOk) diag += QStringLiteral("[clear] ");
        if (!overflowOk)
            diag += QStringLiteral("[cap n=%1 ovf=%2] ").arg(eb2.edits().size())
                        .arg(eb2.overflowedEdits());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2009a type-face testability: DirtyChunkSet is an"
                             " independently testable value set (three-state add with"
                             " idempotent dedup, insertion order, 64-capacity Full reject,"
                             " clear) and EditBuffer merges same-cell edits with later-"
                             " write-wins, routes chunks by floorDiv (negative coords to"
                             " the lower-left chunk), projects takeDelta into WorldDelta"
                             " idempotently, and overflows visibly at the 1024 cap"
                          << (ok ? QString() : diag);
    });

    // ── r2009b：同格三连写——重复通知抑制（验收①）+ 规则终态与旧实现一致（验收②）──────
    runLeg(QStringLiteral("r2009b duplicate-notification suppression + final-state parity"
        " (R20.09 EditBuffer): three FIFO commands in one tick hit the same cell (place"
        " Stone, break, place Dirt); the session lands the rules final state exactly like"
        " the old path (twin world with direct World::setBlock writes + the same number of"
        " legacy ticks - full id+state grids bit-equal, cell reads Dirt on both), and the"
        " notification face is merged instead of repeated: exactly one BlockChanged event"
        " carrying the FINAL id (Dirt - not the intermediate Stone/Air), delta"
        " changedBlocks == 1 (not 3), exactly one dirty chunk, zero dropped events/edits;"
        " the very next steady tick publishes an empty delta (merge state does not leak"
        " across the tick boundary)"), [&]() {
        bool ok = true;
        QString diag;

        // 双生基线恒等（worldgen 纯函数）：
        QString why;
        const auto gridEqual = [&](World &a, World &b) {
            for (int x = 0; x < a.width(); ++x)
                for (int z = 0; z < a.depth(); ++z)
                    for (int y = 0; y < a.height(); ++y) {
                        if (a.blockAt(x, y, z) != b.blockAt(x, y, z)
                            || a.stateAt(x, y, z) != b.stateAt(x, y, z)) {
                            why = QStringLiteral("delta@(%1,%2,%3) %4/%5 vs %6/%7")
                                      .arg(x).arg(y).arg(z)
                                      .arg(a.blockAt(x, y, z)).arg(a.stateAt(x, y, z))
                                      .arg(b.blockAt(x, y, z)).arg(b.stateAt(x, y, z));
                            return false;
                        }
                    }
            return true;
        };
        ok = ok && gridEqual(wB1, wB2);
        if (!why.isEmpty()) diag += QStringLiteral("[twin-base %1] ").arg(why);

        // 选址（双生同结果）：放位列 h+1 实证空气（真放）；三写全打同一格：
        const QPair<int, int> pp = placeCol(wB1, 20, 20); // chunk(1,1)
        const int hp = wB1.heightAt(pp.first, pp.second);
        const BlockPos cell{ pp.first, hp + 1, pp.second };
        const bool siteOk = pp.first >= 0 && hp >= 0
            && wB1.blockAt(cell) == quint8(BR::Air) && wB2.blockAt(cell) == quint8(BR::Air);
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[site id=%1] ").arg(wB1.blockAt(cell));

        if (ok) {
            // 路径 B1（GameSession）：一 tick 三条 FIFO 命令打同一格：
            GameSession gs(wB1);
            QVector<WorldDelta> deltas;
            QObject::connect(&gs, &GameSession::tickCompleted, &gs,
                [&](int, const WorldDelta &d) { deltas.push_back(d); });
            const Result<void> r1 = gs.enqueueCommand(
                Command::placeBlock(cell, quint8(BR::Stone), 7u, 1u, 0));
            const Result<void> r2 = gs.enqueueCommand(Command::breakBlock(cell, 7u, 2u, 0));
            const Result<void> r3 = gs.enqueueCommand(
                Command::placeBlock(cell, quint8(BR::Dirt), 7u, 3u, 0));
            const int stepped = gs.stepTick(0.1); // 一 tick：三命令同边界执行

            // 终态一致（验收②）：B1 格读土 = B2 直调三连写终态（+同 tick 数旧路径泵）：
            wB2.setBlock(cell, quint8(BR::Stone));
            wB2.setBlock(cell, quint8(BR::Air));
            wB2.setBlock(cell, quint8(BR::Dirt));
            wB2.tickRedstone(); // 旧路径 tick 体代表（裸地形稳态全早退——与 13 家族等零写）
            const bool finalOk = r1.isOk() && r2.isOk() && r3.isOk() && stepped == 1
                && gs.tick() == 1 && wB1.blockAt(cell) == quint8(BR::Dirt)
                && wB2.blockAt(cell) == quint8(BR::Dirt);
            ok = ok && finalOk;
            if (!finalOk)
                diag += QStringLiteral("[final r=%1/%2/%3 stepped=%4 b1=%5 b2=%6] ")
                            .arg(r1.isOk()).arg(r2.isOk()).arg(r3.isOk()).arg(stepped)
                            .arg(wB1.blockAt(cell)).arg(wB2.blockAt(cell));

            // 全栅格逐位（终态一致升格全网格）：
            why.clear();
            const bool gridOk = gridEqual(wB1, wB2);
            ok = ok && gridOk;
            if (!gridOk) diag += QStringLiteral("[grid %1] ").arg(why);

            // 通知面合并（验收①）：恰 1 事件、id=终态土（非中间态石/空气）、恰 1 chunk、
            // changedBlocks==1（合并口径≠写入次数 3）：
            Event e{};
            const bool pop1 = gs.events().pop(e);
            const bool evOk = pop1 && gs.events().isEmpty()
                && e.kind == EventKind::BlockChanged && e.pos == cell
                && e.blockId == quint8(BR::Dirt) && e.tick == 1;
            const bool deltaOk = gs.lastDelta().affectedCount == 1
                && gs.lastDelta().changedBlocks == 1
                && gs.lastDelta().affects(ChunkKey::fromWorld(cell.x, cell.z, Chunk::kSize))
                && deltas.size() == 1 && deltas[0].affectedCount == 1
                && deltas[0].changedBlocks == 1;
            const bool faceOk = gs.lastDirtyChunks().size() == 1
                && gs.droppedEventCount() == 0 && gs.droppedEditCount() == 0;
            ok = ok && evOk && deltaOk && faceOk;
            if (!evOk)
                diag += QStringLiteral("[ev pop=%1 kind=%2 id=%3 empty=%4] ")
                            .arg(pop1).arg(int(e.kind)).arg(e.blockId)
                            .arg(gs.events().isEmpty());
            if (!deltaOk)
                diag += QStringLiteral("[delta aff=%1 chg=%2 n=%3] ")
                            .arg(gs.lastDelta().affectedCount)
                            .arg(gs.lastDelta().changedBlocks).arg(deltas.size());
            if (!faceOk)
                diag += QStringLiteral("[face dirty=%1 dropE=%2 dropEd=%3] ")
                            .arg(gs.lastDirtyChunks().size()).arg(gs.droppedEventCount())
                            .arg(gs.droppedEditCount());

            // 稳态 tick：空 delta（合并状态不跨 tick 边界泄漏）：
            const int stepped2 = gs.stepTick(0.1);
            const bool steadyOk = stepped2 == 1 && deltas.size() == 2
                && deltas[1].affectedCount == 0 && deltas[1].changedBlocks == 0
                && deltas[1].tick == 2 && gs.lastDirtyChunks().size() == 0;
            ok = ok && steadyOk;
            if (!steadyOk)
                diag += QStringLiteral("[steady n=%1 aff=%2 dirty=%3] ").arg(deltas.size())
                            .arg(deltas.value(1).affectedCount)
                            .arg(gs.lastDirtyChunks().size());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2009b duplicate-notification suppression + parity: three"
                             " same-cell writes in one tick land the rules final state"
                             " bit-equal to the direct-authority path, with ONE merged"
                             " BlockChanged (final id Dirt), changedBlocks 1, one dirty"
                             " chunk, zero drops, and an empty delta on the next steady"
                             " tick - no repeated notifications, no state leak"
                          << (ok ? QString() : diag);
    });

    // ── r2009c：跨 3 chunk 批量收口——tickCompleted 恰一发 + 集合面/通知面单源 + 零残留 ──
    runLeg(QStringLiteral("r2009c multi-chunk batch close-out (R20.09 EditBuffer): one tick"
        " spans three chunks by construction (break the surface top at chunk (0,0), place"
        " at chunk (2,1), place at chunk (1,2) - cx/cz all distinct), the tick publishes"
        " exactly one tickCompleted whose delta carries all three chunk keys with"
        " changedBlocks 3, the session's dirty-chunk SET view equals the delta's chunk set"
        " (same source - one query for the scheduler instead of per-setBlock signals),"
        " three BlockChanged events keep FIFO order with after-ids (Air, Stone, Stone), and"
        " the following steady tick closes out empty (no residue in delta or dirty set)"), [&]() {
        bool ok = true;
        QString diag;

        GameSession gs(wC1);
        // 结构性跨 chunk 选址（cx 分驻 0/2/1，cz 分驻 0/1/2——不靠地形碰运气；破位显式
        // 验证实方块、放位显式验证空气——空操不发作会塌计数，r2008d 纪律同门）：
        const QPair<int, int> pb = breakCol(wC1, 5, 5);   // chunk(0,0)
        const int hb = wC1.heightAt(pb.first, pb.second);
        const QPair<int, int> pa = placeCol(wC1, 40, 24); // chunk(2,1)
        const int ha = wC1.heightAt(pa.first, pa.second);
        const QPair<int, int> pc = placeCol(wC1, 24, 40); // chunk(1,2)
        const int hc = wC1.heightAt(pc.first, pc.second);
        const bool sitesOk = pb.first >= 0 && pa.first >= 0 && pc.first >= 0 && hb >= 0
            && ha >= 0 && hc >= 0 && wC1.blockAt(pb.first, hb, pb.second) != quint8(BR::Air)
            && wC1.blockAt(pa.first, ha + 1, pa.second) == quint8(BR::Air)
            && wC1.blockAt(pc.first, hc + 1, pc.second) == quint8(BR::Air);
        const ChunkKey kB = ChunkKey::fromWorld(pb.first, pb.second, Chunk::kSize);
        const ChunkKey kA = ChunkKey::fromWorld(pa.first, pa.second, Chunk::kSize);
        const ChunkKey kC = ChunkKey::fromWorld(pc.first, pc.second, Chunk::kSize);
        const bool chunkOk = kB.cx == 0 && kB.cz == 0 && kA.cx == 2 && kA.cz == 1
            && kC.cx == 1 && kC.cz == 2; // 结构自证：三键确分驻三 chunk
        ok = ok && sitesOk && chunkOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");
        if (!chunkOk)
            diag += QStringLiteral("[chunks kB=%1/%2 kA=%3/%4 kC=%5/%6] ")
                        .arg(kB.cx).arg(kB.cz).arg(kA.cx).arg(kA.cz).arg(kC.cx).arg(kC.cz);

        if (ok) {
            QVector<WorldDelta> deltas;
            QObject::connect(&gs, &GameSession::tickCompleted, &gs,
                [&](int, const WorldDelta &d) { deltas.push_back(d); });
            const Result<void> r1 = gs.enqueueCommand(
                Command::breakBlock(BlockPos{ pb.first, hb, pb.second }, 7u, 1u, 0));
            const Result<void> r2 = gs.enqueueCommand(
                Command::placeBlock(BlockPos{ pa.first, ha + 1, pa.second },
                    quint8(BR::Stone), 7u, 2u, 0));
            const Result<void> r3 = gs.enqueueCommand(
                Command::placeBlock(BlockPos{ pc.first, hc + 1, pc.second },
                    quint8(BR::Cobble), 7u, 3u, 0));
            const int stepped = gs.stepTick(0.1);

            // 批量收口：恰 1 发 tickCompleted、delta 携全 3 chunk、合并计数 3：
            const bool cmdOk = r1.isOk() && r2.isOk() && r3.isOk() && stepped == 1
                && deltas.size() == 1 && deltas[0].affectedCount == 3
                && deltas[0].changedBlocks == 3 && deltas[0].tick == 1
                && deltas[0].affects(kB) && deltas[0].affects(kA) && deltas[0].affects(kC);
            ok = ok && cmdOk;
            if (!cmdOk)
                diag += QStringLiteral("[cmd r=%1/%2/%3 stepped=%4 n=%5 aff=%6 chg=%7] ")
                            .arg(r1.isOk()).arg(r2.isOk()).arg(r3.isOk()).arg(stepped)
                            .arg(deltas.size()).arg(deltas.value(0).affectedCount)
                            .arg(deltas.value(0).changedBlocks);

            // 集合面 = 通知面单源（调度一次查询——验收④会话面示范）：
            const DirtyChunkSet &dirty = gs.lastDirtyChunks();
            bool setOk = dirty.size() == deltas.value(0).affectedCount;
            for (int i = 0; i < dirty.size() && setOk; ++i)
                setOk = deltas.value(0).affects(dirty.at(i)); // 集合逐键 ∈ delta 集合
            setOk = setOk && dirty.contains(kB) && dirty.contains(kA) && dirty.contains(kC);
            ok = ok && setOk;
            if (!setOk) diag += QStringLiteral("[set dirty=%1] ").arg(dirty.size());

            // 终态格核验（破真落土 / 放真落石）+ 事件序（FIFO 破先放后、id=终态）：
            const bool cellsOk = wC1.blockAt(pb.first, hb, pb.second) == quint8(BR::Air)
                && wC1.blockAt(pa.first, ha + 1, pa.second) == quint8(BR::Stone)
                && wC1.blockAt(pc.first, hc + 1, pc.second) == quint8(BR::Cobble);
            Event e1{}, e2{}, e3{};
            const bool evOk = gs.events().pop(e1) && gs.events().pop(e2)
                && gs.events().pop(e3) && gs.events().isEmpty()
                && e1.pos == BlockPos{ pb.first, hb, pb.second }
                && e1.blockId == quint8(BR::Air)
                && e2.pos == BlockPos{ pa.first, ha + 1, pa.second }
                && e2.blockId == quint8(BR::Stone)
                && e3.pos == BlockPos{ pc.first, hc + 1, pc.second }
                && e3.blockId == quint8(BR::Cobble)
                && gs.droppedEventCount() == 0 && gs.droppedEditCount() == 0;
            ok = ok && cellsOk && evOk;
            if (!cellsOk) diag += QStringLiteral("[cells] ");
            if (!evOk)
                diag += QStringLiteral("[ev e1=%2/%3 e2=%5/%6 e3=%8/%9 empty=%1] ")
                            .arg(gs.events().isEmpty())
                            .arg(int(e1.kind)).arg(e1.blockId)
                            .arg(int(e2.kind)).arg(e2.blockId)
                            .arg(int(e3.kind)).arg(e3.blockId);

            // 次一稳态 tick：delta 与脏集双零残留（收口不泄漏）：
            const int stepped2 = gs.stepTick(0.1);
            const bool residueOk = stepped2 == 1 && deltas.size() == 2
                && deltas[1].affectedCount == 0 && deltas[1].changedBlocks == 0
                && gs.lastDirtyChunks().size() == 0 && gs.lastDelta().affectedCount == 0;
            ok = ok && residueOk;
            if (!residueOk)
                diag += QStringLiteral("[residue n=%1 aff=%2 dirty=%3] ").arg(deltas.size())
                            .arg(deltas.value(1).affectedCount)
                            .arg(gs.lastDirtyChunks().size());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2009c multi-chunk batch close-out: one tick spanning three"
                             " structurally distinct chunks publishes exactly one"
                             " tickCompleted with the full chunk set + merged count, the"
                             " dirty-chunk set view equals the delta set (one scheduler"
                             " query, not per-setBlock), events keep FIFO after-ids, and"
                             " the next steady tick closes out with zero residue"
                          << (ok ? QString() : diag);
    });

    // ── r2009d：源码钉（EditBuffer 委托 + 旧路径禁出反探）+ PlaceBlock blockState（Review#3）─
    runLeg(QStringLiteral("r2009d source pins + PlaceBlock blockState (R20.09 EditBuffer +"
        " Review_2026-09-15 #3): comment-stripped pins hold the session delegation -"
        " gamesession.h records edits via m_edits.record, closes the tick via takeDelta,"
        " keeps BlockChanged derivation and both facade write entries (setBlock for break,"
        " setBlockWithState for place), and exposes the lastDirtyChunks scheduler view;"
        " editbuffer.h pins hold the DirtyChunkSet/EditBuffer classes with the"
        " fromWorld routing and the WorldDelta capacity single authority; forbidden"
        " probes hold the OLD close-out path gone (no m_lastDelta.addAffected / no"
        " manual changedBlocks increment in the session); behaviorally PlaceBlock now"
        " lands its state through the five-arg authority (Farmland state 3 survives -"
        " previously silently reset to 0 by the four-arg write) while the default-0"
        " factory stays backward compatible (Stone state 0)"), [&]() {
        bool ok = true;
        QString diag;

        // 源码钉根（exe 相对 src/——r2007b/2008c 同款解析）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString gsPath = srcRoot + QStringLiteral("/Game/gamesession.h");
        const QString ebPath = srcRoot + QStringLiteral("/Core/editbuffer.h");

        // ① 会话委托面（剥注释计数钉——摘委托即红）：
        const QStringList missGs = pinSet(gsPath, {
            SrcPin("r2009 session records edits via EditBuffer", "m_edits.record(", 1),
            SrcPin("r2009 tick close-out publishes takeDelta", "m_edits.takeDelta(", 1),
            SrcPin("r2009 merged-face event derivation kept", "EventKind::BlockChanged", 1),
            SrcPin("r2009 break command keeps facade setBlock", "m_facade.setBlock(", 1),
            SrcPin("r2009 place command lands state via facade setBlockWithState",
                "m_facade.setBlockWithState(", 1),
            SrcPin("r2009 scheduler set view exposed", "lastDirtyChunks", 1),
            SrcPin("r2009 overflow visible at session face", "m_droppedEdits", 2),
        });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② EditBuffer 结构钉（类型/路由/容量单一权威）：
        const QStringList missEb = pinSet(ebPath, {
            SrcPin("r2009 EditBuffer class present", "class EditBuffer", 1),
            SrcPin("r2009 DirtyChunkSet class present", "class DirtyChunkSet", 1),
            SrcPin("r2009 chunk routing via fromWorld single form", "ChunkKey::fromWorld", 1),
            SrcPin("r2009 capacity references WorldDelta single authority",
                "WorldDelta::kMaxAffectedChunks", 1),
            SrcPin("r2009 merge returns suppressed-notification state", "RecordResult::Merged", 1),
        });
        for (const QString &m : missEb) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 旧收口路径禁出（minCount=1 反探——miss 非空=零命中=合规；R20.08 空转钉教训）：
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool oldPathOk = forbiddenAbsent(gsPath, "m_lastDelta.addAffected")
            && forbiddenAbsent(gsPath, "++m_lastDelta.changedBlocks");
        ok = ok && oldPathOk;
        if (!oldPathOk) diag += QStringLiteral("[old-close-out] ");

        // ④ blockState 行为面（Review #3① 修复实证 + 默认 0 向后兼容）：
        const QPair<int, int> pf = placeCol(wD1, 20, 20);
        const int hf = wD1.heightAt(pf.first, pf.second);
        const QPair<int, int> ps = placeCol(wD1, 40, 24);
        const int hs = wD1.heightAt(ps.first, ps.second);
        const bool sitesOk = pf.first >= 0 && ps.first >= 0 && hf >= 0 && hs >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");
        if (ok) {
            GameSession gs(wD1);
            QVector<WorldDelta> deltas;
            QObject::connect(&gs, &GameSession::tickCompleted, &gs,
                [&](int, const WorldDelta &d) { deltas.push_back(d); });
            // 带 state 放置（五参工厂）：Farmland state=3 不再被静默重置：
            const Result<void> r1 = gs.enqueueCommand(Command::placeBlock(
                BlockPos{ pf.first, hf + 1, pf.second }, quint8(BR::Farmland), 7u, 1u, 0, 3));
            // 默认 0 兼容（五参调用不传 state）：
            const Result<void> r2 = gs.enqueueCommand(Command::placeBlock(
                BlockPos{ ps.first, hs + 1, ps.second }, quint8(BR::Stone), 7u, 2u, 0));
            const int stepped = gs.stepTick(0.1);

            const bool stateOk = r1.isOk() && r2.isOk() && stepped == 1
                && wD1.blockAt(pf.first, hf + 1, pf.second) == quint8(BR::Farmland)
                && wD1.stateAt(pf.first, hf + 1, pf.second) == 3
                && wD1.blockAt(ps.first, hs + 1, ps.second) == quint8(BR::Stone)
                && wD1.stateAt(ps.first, hs + 1, ps.second) == 0;
            ok = ok && stateOk;
            if (!stateOk)
                diag += QStringLiteral("[state farm=%1/%2 stone=%3/%4] ")
                            .arg(wD1.blockAt(pf.first, hf + 1, pf.second))
                            .arg(wD1.stateAt(pf.first, hf + 1, pf.second))
                            .arg(wD1.blockAt(ps.first, hs + 1, ps.second))
                            .arg(wD1.stateAt(ps.first, hs + 1, ps.second));

            // 编辑面：两格两 chunk 两事件（id=终态；delta 计数 2）：
            Event e1{}, e2{};
            const bool evOk = gs.events().pop(e1) && gs.events().pop(e2)
                && gs.events().isEmpty()
                && e1.blockId == quint8(BR::Farmland) && e2.blockId == quint8(BR::Stone)
                && gs.lastDelta().affectedCount == 2 && gs.lastDelta().changedBlocks == 2
                && gs.droppedEventCount() == 0;
            ok = ok && evOk;
            if (!evOk)
                diag += QStringLiteral("[ev pop12=%1/%2 aff=%3 chg=%4] ")
                            .arg(gs.events().isEmpty()).arg(int(e2.blockId))
                            .arg(gs.lastDelta().affectedCount)
                            .arg(gs.lastDelta().changedBlocks);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2009d source pins + PlaceBlock blockState: session held on"
                             " the EditBuffer delegation (record/takeDelta/BlockChanged/"
                             " both facade writes/dirty-set view), editbuffer structure"
                             " pinned (classes, fromWorld routing, capacity authority),"
                             " the old close-out path forbidden (reverse probes), and"
                             " PlaceBlock state lands through the five-arg authority"
                             " (Farmland s3) with default-0 backward compatibility"
                          << (ok ? QString() : diag);
    });
}
