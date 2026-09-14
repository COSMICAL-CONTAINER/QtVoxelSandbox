#include "matrix_helpers.h"

#include "command.h"  // R20.06 被测：CommandKind / Command / CommandQueue
#include "event.h"    // R20.06 被测：EventKind / Event / WorldDelta / EventQueue
#include "snapshot.h" // R20.06 被测：BlockEdit / WorldSnapshot / SnapshotQueue

#include <type_traits> // 腿内编译期复钉（is_trivially_copyable 等）

// R20.06 Command/Event/Snapshot 立类型探针段（3 腿 r2006a-c；filter 词 "r2006"；矩阵
// 549→552，band 551±2 内）。任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.06 原文
// 「先只定义数据结构和队列，不大规模迁移调用点」）：本段对 rig 世界**零接触**（纯类型 /
// 队列行为面——无 setBlock / 无信号 / 无 World 构造），世界基线面零残留由构造保证（比
// r2005c 的「即写即还原本格」更强一级；section10 置尾执行，接 section09 先例）。
// 验收对照（plan §29.3 R20.06 原文四条 + 本单补充）：
//   「BreakBlock 可以通过 Command 表达」→ r2006a（breakBlock 工厂 + plan §4.2 必带字段
//     往返 + 队列 FIFO / 满载拒绝 / 清空）；
//   「WorldDelta 可以表达受影响 Chunk」→ r2006b（addAffected 幂等去重 / affects 成员 /
//     64 上界不挤出 / 清空；ChunkKey 复用钉 fromWorld(-1,47,16)==(-1,2)）；
//   「Event 不携带 QObject」→ r2006b **编译期钉**（result.h QObjectFree 概念 + event.h
//     头内 static_assert——本 TU 编译通过本身即证明；「摘即红」= 给 Event 加 QObject 派生
//     成员则构建失败，类型层阴性即此性质，腿内另作冗余复钉让 proof 面在测试文本可见）；
//   「Snapshot 可以独立复制和测试」→ r2006c（改副本源不变 / 改源副本不变双向证明 + 快照
//     队列环形覆盖语义 + 值语义「push 后改源，队内不受扰」）。
void MatrixRun::section10_command_event_snapshot()
{
    // ── r2006a：Command 表达 + CommandQueue FIFO / 容量 / 清空 ─────────────────────
    runLeg(QStringLiteral("r2006a Command expression FIFO queue capacity (R20.06 Command/Event"
        "/Snapshot types): BreakBlock is expressible as a plain-data Command via the breakBlock"
        " factory carrying the plan-mandated fields (source actorId, target tick, entity id,"
        " input sequence, params pos+blockId, minimal validation context pos) with negative"
        " coordinates passing through untouched as the BlockPos-reuse pin; CommandQueue pins"
        " FIFO order (sequences 11/12/13 pop in exactly that order), verbatim field round-"
        " trips, the reject-on-full capacity policy (push fails with kErrQueueFull at 256"
        " entries, size stays 256, isFull reports true, no silent overwrite/eviction), one"
        " pop freeing exactly one slot, clear emptying with pop reporting false afterwards,"
        " and the empty-queue-is-normal-state vs full-queue-is-Error distinction; in-TU"
        " static_asserts re-pin Command QObject-free + trivially copyable"), [&]() {
        // 编译期复钉（与 command.h 头内钉冗余为刻意——proof 面在测试文本可见）：
        static_assert(QObjectFree<Command> && std::is_trivially_copyable_v<Command>,
                      "R20.06 Command must be QObject-free trivially-copyable (plan §4.2)");

        bool ok = true;
        QString diag;

        // BreakBlock 表达（验收锚）：工厂 + plan §4.2 必带字段全往返 + 负坐标穿透
        //（BlockPos 复用钉——Core 类型不重设坐标域）：
        const Command b = Command::breakBlock(BlockPos{ -1, 47, -2 }, 7u, 11u, 900);
        const bool exprOk = b.kind == CommandKind::BreakBlock && b.pos == BlockPos{ -1, 47, -2 }
            && b.blockId == 0 && b.actorId == 7u && b.sequence == 11u && b.targetTick == 900;
        ok = ok && exprOk;
        if (!exprOk) diag += QStringLiteral("[breakblock-expr] ");

        // PlaceBlock 工厂对称（blockId 携带 + 字段次序对齐）：
        const Command p = Command::placeBlock(BlockPos{ 3, 41, 4 }, quint8(BR::Stone), 7u, 12u, 901);
        const bool placeOk = p.kind == CommandKind::PlaceBlock && p.blockId == quint8(BR::Stone)
            && p.actorId == 7u && p.sequence == 12u && p.targetTick == 901;
        ok = ok && placeOk;
        if (!placeOk) diag += QStringLiteral("[placeblock-expr] ");

        // FIFO 语义：三条序号 11/12/13 → 弹出顺序严格同序（队头恒最老）：
        CommandQueue q;
        const Result<void> r1 = q.push(Command::breakBlock(BlockPos{ 0, 41, 0 }, 1u, 11u, 100));
        const Result<void> r2 = q.push(Command::breakBlock(BlockPos{ 0, 41, 1 }, 1u, 12u, 101));
        const Result<void> r3 = q.push(Command::breakBlock(BlockPos{ 0, 41, 2 }, 1u, 13u, 102));
        const bool pushOk = r1.isOk() && r2.isOk() && r3.isOk() && q.size() == 3;
        Command out;
        const bool fifoOk = q.pop(out) && out.sequence == 11u && out.pos == BlockPos{ 0, 41, 0 }
            && q.pop(out) && out.sequence == 12u && out.targetTick == 101
            && q.pop(out) && out.sequence == 13u && q.isEmpty() && !q.pop(out);
        ok = ok && pushOk && fifoOk;
        if (!pushOk) diag += QStringLiteral("[push sz=%1] ").arg(q.size());
        if (!fifoOk) diag += QStringLiteral("[fifo] ");

        // 容量策略：满载拒绝（Result 携带 kErrQueueFull）不覆盖不挤出；弹一空一：
        while (q.push(Command::breakBlock(BlockPos{ 5, 41, 5 }, 2u, 100u, 200)).isOk()) {}
        const bool fullOk = q.size() == CommandQueue::kCapacity && q.isFull();
        const Result<void> over = q.push(Command::breakBlock(BlockPos{ 6, 41, 6 }, 2u, 999u, 201));
        const bool rejectOk = !over.isOk() && over.error().code == kErrQueueFull
            && q.size() == CommandQueue::kCapacity; // 257th 被拒且不挤出任何既有命令
        Command first;
        const bool drainOk = q.pop(first) && first.sequence == 100u // 队头仍是最早入队者
            && q.size() == CommandQueue::kCapacity - 1
            && q.push(Command::breakBlock(BlockPos{ 7, 41, 7 }, 2u, 1000u, 202)).isOk();
        ok = ok && fullOk && rejectOk && drainOk;
        if (!fullOk) diag += QStringLiteral("[full sz=%1] ").arg(q.size());
        if (!rejectOk)
            diag += QStringLiteral("[reject ok=%1 code=%2] ").arg(over.isOk()).arg(over.error().code);
        if (!drainOk) diag += QStringLiteral("[drain] ");

        // 清空语义：clear 后 size 0 / pop false / 可重新入队：
        q.clear();
        const bool clearOk = q.isEmpty() && q.size() == 0 && !q.pop(out)
            && q.push(Command::breakBlock(BlockPos{ 8, 41, 8 }, 3u, 2000u, 300)).isOk();
        ok = ok && clearOk;
        if (!clearOk) diag += QStringLiteral("[clear] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2006a Command expression FIFO queue capacity: BreakBlock as"
                             " plain-data Command (plan-mandated fields + negative BlockPos"
                             " reuse pin) + CommandQueue FIFO order + reject-on-full with"
                             " kErrQueueFull (no silent overwrite, drain-head-oldest) +"
                             " clear/empty-state semantics + QObject-free trivially-copyable"
                             " compile pins"
                          << (ok ? QString() : diag);
    });

    // ── r2006b：Event 无 QObject 编译期钉 + EventQueue + WorldDelta 受影响 chunk 集 ──
    runLeg(QStringLiteral("r2006b Event no-QObject compile pin EventQueue WorldDelta chunk set"
        " (R20.06 Command/Event/Snapshot types): the compile itself is the proof - the"
        " QObjectFree concept in result.h plus header static_asserts make any QObject-derived"
        " member on Event/Command/WorldDelta/WorldSnapshot a build failure, so remove-to-red"
        " lives at the type level (this leg additionally re-pins all four in-TU); the full"
        " plan-example EventKind list names compile with BlockChanged anchoring zero;"
        " EventQueue pins FIFO round-trip of a BlockChanged (pos/id/tick) and a SoundRequested"
        " (aux) plus the same reject-on-full policy as commands (kErrQueueFull at 256);"
        " WorldDelta expresses the affected-chunk set over the R20.05 ChunkKey (negative"
        " fromWorld(-1,47,16) landing key (-1,2) as the reuse pin), duplicate addAffected is"
        " idempotent returning false without consuming a slot, the 64-slot bound rejects the"
        " 65th distinct chunk with no eviction, affects() membership distinguishes duplicate"
        " from overflow, changedBlocks/tick round-trip, and clear empties"), [&]() {
        // 编译期复钉（验收原文「Event 不携带 QObject」：四类型全钉，冗余为刻意）：
        static_assert(QObjectFree<Event> && QObjectFree<Command> && QObjectFree<WorldDelta>
                          && QObjectFree<WorldSnapshot>,
                      "R20.06 types must not carry QObject (plan §29.3 R20.06 / §4.2)");
        static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_copyable_v<WorldDelta>,
                      "Event/WorldDelta stay trivially copyable");

        bool ok = true;
        QString diag;

        // plan §4.2 事件例单存在性钉（编译期点名——删席即编译失败）+ BlockChanged 锚零：
        constexpr EventKind kPlanEventKinds[] = {
            EventKind::BlockChanged, EventKind::EntitySpawned, EventKind::EntityDied,
            EventKind::ItemDropped,  EventKind::SoundRequested, EventKind::ChunkActivated,
            EventKind::SaveCompleted
        };
        const bool kindsOk = int(sizeof(kPlanEventKinds) / sizeof(kPlanEventKinds[0])) == 7
            && int(EventKind::BlockChanged) == 0;
        ok = ok && kindsOk;
        if (!kindsOk) diag += QStringLiteral("[kinds] ");

        // EventQueue FIFO + 字段域往返（方块域 / 音频域各一）：
        EventQueue q;
        Event blockChanged;
        blockChanged.kind = EventKind::BlockChanged;
        blockChanged.pos = BlockPos{ -3, 40, 9 };
        blockChanged.blockId = quint8(BR::Cobble);
        blockChanged.tick = 1200;
        Event sound;
        sound.kind = EventKind::SoundRequested;
        sound.pos = BlockPos{ 4, 42, 5 };
        sound.aux = 77;
        sound.tick = 1201;
        const bool pushOk = q.push(blockChanged).isOk() && q.push(sound).isOk() && q.size() == 2;
        Event out;
        const bool fifoOk = q.pop(out) && out.kind == EventKind::BlockChanged
            && out.pos == BlockPos{ -3, 40, 9 } && out.blockId == quint8(BR::Cobble)
            && out.tick == 1200
            && q.pop(out) && out.kind == EventKind::SoundRequested && out.aux == 77
            && out.tick == 1201 && q.isEmpty() && !q.pop(out);
        ok = ok && pushOk && fifoOk;
        if (!pushOk) diag += QStringLiteral("[push sz=%1] ").arg(q.size());
        if (!fifoOk) diag += QStringLiteral("[fifo] ");

        // 容量策略与命令队列同门（满载 Result 拒绝，不覆盖）：
        while (q.push(Event{}).isOk()) {}
        const Result<void> over = q.push(Event{});
        const bool capOk = q.size() == EventQueue::kCapacity && !over.isOk()
            && over.error().code == kErrQueueFull;
        ok = ok && capOk;
        if (!capOk)
            diag += QStringLiteral("[cap sz=%1 code=%2] ").arg(q.size()).arg(over.error().code);
        q.clear();

        // WorldDelta 受影响 chunk 集（验收锚；ChunkKey 复用钉——负坐标 chunk 键直接可用）：
        WorldDelta d;
        d.tick = 1300;
        const ChunkKey neg = ChunkKey::fromWorld(-1, 47, 16); // → (-1, 2)（r2005b 同钉互证）
        const bool negOk = neg.cx == -1 && neg.cz == 2;
        const bool addOk = d.addAffected(ChunkKey{ 0, 0 }) && d.addAffected(neg)
            && d.addAffected(ChunkKey{ 3, -4 }) && d.affectedCount == 3;
        const bool dupOk = !d.addAffected(neg) && d.affectedCount == 3 // 幂等去重不占位
            && d.affects(neg) && d.affects(ChunkKey{ 0, 0 }) && !d.affects(ChunkKey{ 9, 9 });
        while (d.addAffected(ChunkKey{ d.affectedCount, 0 })) {} // 灌满 64
        const bool boundOk = d.affectedCount == WorldDelta::kMaxAffectedChunks
            && !d.addAffected(ChunkKey{ 100, 100 }) // 第 65 个新键被拒、不挤出
            && d.affectedCount == WorldDelta::kMaxAffectedChunks
            && !d.affects(ChunkKey{ 100, 100 });
        d.changedBlocks = 17;
        const bool metaOk = d.changedBlocks == 17 && d.tick == 1300;
        d.clear();
        const bool dClearOk = d.affectedCount == 0 && d.changedBlocks == 0 && d.tick == 0
            && !d.affects(ChunkKey{ 0, 0 });
        ok = ok && negOk && addOk && dupOk && boundOk && metaOk && dClearOk;
        if (!negOk) diag += QStringLiteral("[negkey %1,%2] ").arg(neg.cx).arg(neg.cz);
        if (!addOk) diag += QStringLiteral("[delta-add n=%1] ").arg(d.affectedCount);
        if (!dupOk) diag += QStringLiteral("[delta-dup] ");
        if (!boundOk) diag += QStringLiteral("[delta-bound n=%1] ").arg(d.affectedCount);
        if (!metaOk) diag += QStringLiteral("[delta-meta] ");
        if (!dClearOk) diag += QStringLiteral("[delta-clear] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2006b Event no-QObject compile pin EventQueue WorldDelta chunk"
                             " set: QObjectFree build-failure proof (type-level remove-to-red)"
                             " + 7-kind plan list compile pin + EventQueue FIFO + shared"
                             " reject-on-full policy + WorldDelta affected-chunk set over"
                             " ChunkKey (negative reuse pin, idempotent dedup, 64 bound no"
                             " eviction, membership, clear)"
                          << (ok ? QString() : diag);
    });

    // ── r2006c：Snapshot 拷贝独立性（双向）+ SnapshotQueue 环形覆盖 / 值语义 ──────────
    runLeg(QStringLiteral("r2006c Snapshot copy independence SnapshotQueue ring (R20.06"
        " Command/Event/Snapshot types): WorldSnapshot copy independence is proven in BOTH"
        " directions on a filled snapshot (tick 700, extents 96x180x48, three edits incl. a"
        " negative BlockPos) - mutating the copy (tick, extents, edit payload, extra append,"
        " clear) leaves the source bit-equal to a pre-mutation baseline, and mutating the"
        " source afterwards leaves the earlier copy untouched; copy-assignment is independent"
        " too (cleared assignee does not empty its source) and an empty snapshot copy stays"
        " independent on first append; SnapshotQueue pins FIFO drain of two pushed snapshots,"
        " the deliberate ring policy overwrite-oldest-on-full (six pushes on capacity 4 pop"
        " ticks 3/4/5/6, size capped at 4), value semantics (mutating the pushed-from local"
        " after push does not disturb the queued copy), and clear; compile pins re-assert the"
        " snapshot is QObject-free and copy-constructible/assignable"), [&]() {
        // 编译期复钉（snapshot.h 头内钉的冗余可见面）：
        static_assert(QObjectFree<WorldSnapshot> && std::is_copy_constructible_v<WorldSnapshot>
                          && std::is_copy_assignable_v<WorldSnapshot>,
                      "R20.06 WorldSnapshot must be QObject-free and independently copyable");

        bool ok = true;
        QString diag;

        // 快照相等谓词（全字段；深拷贝独立性的判定基准）：
        const auto snapEq = [](const WorldSnapshot &a, const WorldSnapshot &b) {
            if (a.tick != b.tick || a.width != b.width || a.depth != b.depth
                || a.height != b.height || a.edits.size() != b.edits.size())
                return false;
            for (size_t i = 0; i < a.edits.size(); ++i)
                if (a.edits[i].pos != b.edits[i].pos || a.edits[i].id != b.edits[i].id)
                    return false;
            return true;
        };

        // 源快照（含负坐标 edit——Core 类型无坐标域假设）：
        WorldSnapshot src;
        src.tick = 700;
        src.width = 96;
        src.depth = 180;
        src.height = 48;
        src.addEdit(BlockPos{ 5, 41, 7 }, 1);
        src.addEdit(BlockPos{ -1, 47, -2 }, 3);
        src.addEdit(BlockPos{ 0, 0, 0 }, 7);
        const WorldSnapshot baseline = src; // 判定基准（拷贝即独立——本身也是深拷贝证明）

        // 方向一：改副本 → 源不变（对照 baseline 逐位）：
        WorldSnapshot cp = src;
        cp.tick = 999;
        cp.width = 1;
        cp.edits[0].id = 99;
        cp.edits[1].pos = BlockPos{ 8, 8, 8 };
        cp.addEdit(BlockPos{ 50, 1, 50 }, 5);
        const bool dir1 = snapEq(src, baseline) && src.edits.size() == 3;
        // 方向二：改源 → 先前的副本不变。期望态必须显式逐字段钉（副本在方向一被**按计划**
        //   改过，不能再对 baseline 比较——首跑实锤：对 baseline 比较把刻意的副本改动误判
        //   为串扰；显式内容钉才是「后续源改动不泄漏进副本」的承重断言）：
        src.tick = 1234;
        src.addEdit(BlockPos{ 1, 2, 3 }, 4);
        src.edits[2].id = 11;
        const bool dir2 = cp.tick == 999 && cp.width == 1 && cp.depth == 180 && cp.height == 48
            && cp.edits.size() == 4
            && cp.edits[0].pos == BlockPos{ 5, 41, 7 } && cp.edits[0].id == 99
            && cp.edits[1].pos == BlockPos{ 8, 8, 8 } && cp.edits[1].id == 3
            && cp.edits[2].pos == BlockPos{ 0, 0, 0 } && cp.edits[2].id == 7
            && cp.edits[3].pos == BlockPos{ 50, 1, 50 } && cp.edits[3].id == 5;
        ok = ok && dir1 && dir2;
        if (!dir1) diag += QStringLiteral("[copy->src] ");
        if (!dir2) diag += QStringLiteral("[src->copy] ");

        // 赋值面独立 + 空快照拷贝独立（首 append 不串源）：
        WorldSnapshot assigned;
        assigned = baseline;
        assigned.clear();
        const WorldSnapshot emptySrc;
        WorldSnapshot emptyCp = emptySrc;
        emptyCp.addEdit(BlockPos{ 2, 2, 2 }, 6);
        const bool assignOk = assigned.edits.empty() && assigned.tick == 0
            && snapEq(baseline, WorldSnapshot{}) == false // baseline 未被 assigned.clear 扫掉
            && baseline.edits.size() == 3 && baseline.tick == 700
            && emptySrc.edits.empty() && emptyCp.edits.size() == 1;
        ok = ok && assignOk;
        if (!assignOk) diag += QStringLiteral("[assign/empty] ");

        // SnapshotQueue：FIFO 双帧出队 → 环形满载覆盖最老（容量 4，六入弹 3/4/5/6）：
        SnapshotQueue sq;
        WorldSnapshot a;
        a.tick = 1;
        WorldSnapshot b;
        b.tick = 2;
        sq.push(a);
        sq.push(b);
        WorldSnapshot out;
        const bool fifoOk = sq.size() == 2 && sq.pop(out) && out.tick == 1 && sq.pop(out)
            && out.tick == 2 && !sq.pop(out) && sq.isEmpty();
        for (int t = 1; t <= 6; ++t) {
            WorldSnapshot s;
            s.tick = t;
            sq.push(s);
        }
        const bool ringOk = sq.size() == SnapshotQueue::kCapacity && sq.isFull()
            && sq.pop(out) && out.tick == 3 && sq.pop(out) && out.tick == 4
            && sq.pop(out) && out.tick == 5 && sq.pop(out) && out.tick == 6
            && sq.isEmpty() && !sq.pop(out);
        ok = ok && fifoOk && ringOk;
        if (!fifoOk) diag += QStringLiteral("[q-fifo] ");
        if (!ringOk) diag += QStringLiteral("[q-ring] ");

        // 值语义钉：push 后改源对象，队内拷贝不受扰；clear 语义：
        WorldSnapshot live;
        live.tick = 42;
        live.addEdit(BlockPos{ 9, 9, 9 }, 8);
        sq.push(live);
        live.tick = 4242;
        live.edits.clear();
        bool valOk = sq.pop(out) && out.tick == 42 && out.edits.size() == 1
            && out.edits[0].pos == BlockPos{ 9, 9, 9 } && out.edits[0].id == 8;
        sq.push(out);
        sq.clear();
        valOk = valOk && sq.isEmpty() && sq.size() == 0 && !sq.pop(out);
        ok = ok && valOk;
        if (!valOk) diag += QStringLiteral("[q-value/clear] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2006c Snapshot copy independence SnapshotQueue ring: both-"
                             "direction deep-copy proof (mutate copy -> source bit-equal to"
                             " baseline, mutate source -> copy untouched) + assignment/empty"
                             " independence + FIFO drain + overwrite-oldest-on-full ring"
                             " (6 pushes on cap 4 pop 3/4/5/6) + push-then-mutate value"
                             " semantics + clear"
                          << (ok ? QString() : diag);
    });
}
