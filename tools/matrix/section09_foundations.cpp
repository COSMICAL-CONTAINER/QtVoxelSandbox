#include "matrix_helpers.h"

#include "mathtypes.h" // R20.05 被测基础类型：floorDiv/floorMod / ChunkKey / BlockPos / Tick
#include "result.h"    // R20.05 被测基础错误类型：Error / Result<T>

#include <QDir>
#include <QFile>
#include <QHash> // r2005b：QHash<ChunkKey,int> 可哈希面钉
#include <unordered_set>

// R20.05 基础类型探针段（4 腿 r2005a-d；filter 词 "r2005"；矩阵 545→549）。
// 任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.05 + 编号正名注记：agent-state 曾误标
// 「R20.04 基础类型」，plan 原文 R20.04=测试矩阵拆分（已由 R20.03 实质覆盖），本单实为 plan
// R20.05）：floorDiv/floorMod、ChunkKey、BlockPos、固定 Tick、基础错误类型——只立类型 + 最小
// 示范采用，不改变世界生成和玩法。
// 验收对照（plan 原文）：
//   「负坐标测试通过」→ r2005a（全象限钉表 + 代数恒等式全域扫）+ r2005b（ChunkKey 负坐标
//     fromWorld / packed 往返单射）；
//   「原有默认世界结果不变」→ 本段对世界基线面零接触（置尾执行 + setBlock 即写即清），默认
//     世界逐位恒等由既有 worldgen 腿族守（matrix_r2005_pos.log vs matrix_r2003_split_run3.log
//     的 worldgen 数值头 + 全部 PASS 行逐位恒等 = 本单阴性轮豁免的闭环证据，见 docs）；
//   「新类型可被 World 和测试使用」→ r2005c（ChunkManager/World 的 BlockPos 加性重载对真 rig
//     世界行为级行）+ r2005d（示范采用的「摘即红」源码钉——revert 到字面量即翻红）。
void MatrixRun::section09_foundations()
{
    // ── r2005a：floorDiv / floorMod 负坐标全象限钉 ──────────────────────────────
    runLeg(QStringLiteral("r2005a floor-div-mod negative quadrants (R20.05 foundational types): "
        "floor semantics single authority in Core/mathtypes.h pins floorDiv/floorMod across all "
        "four sign quadrants - a 15-case table covers a=0/±1 boundaries, the chunk-boundary exacts "
        "(15/16/-16/-17 at 16) and the -4k±1 rule (-63/-64/-65 landing chunk -4/-4/-5 with locals "
        "1/0/15, the MC-negative-coordinate chunking semantics: block x=-1 belongs to chunk -1 "
        "local 15) plus negative divisors where the remainder takes the divisor's sign "
        "(floorMod(7,-16)=-9); an exhaustive algebraic sweep over a in [-4097,4097] x b in "
        "{16,-16,7,-7} asserts a == b*floorDiv(a,b) + floorMod(a,b) and sign(floorMod)==sign(b) "
        "or zero; the C++ truncating /,% baseline is cross-pinned wrong on negatives "
        "(-7/16==0, -7%16==-7) to prove the authority is load-bearing"), [&]() {
        bool ok = true;
        QString diag;
        const struct { int a, b, ed, em; } cases[] = {
            {   0, 16,  0,  0 }, {   1, 16,  0,  1 }, {  15, 16,  0, 15 }, {  16, 16,  1,  0 },
            {  -1, 16, -1, 15 }, { -15, 16, -1,  1 }, { -16, 16, -1,  0 }, { -17, 16, -2, 15 },
            { -63, 16, -4,  1 }, { -64, 16, -4,  0 }, { -65, 16, -5, 15 },   // -4k±1 规律
            {   0, -16,  0, 0 }, {   7, -16, -1, -9 }, { -7, -16, 0, -7 }, { -32, -16, 2, 0 },
        };
        for (const auto &c : cases) {
            const int d = floorDiv(c.a, c.b), m = floorMod(c.a, c.b);
            const bool good = (d == c.ed && m == c.em);
            ok = ok && good;
            if (!good)
                diag += QStringLiteral("[a=%1 b=%2 d=%3/%4 m=%5/%6] ")
                            .arg(c.a).arg(c.b).arg(d).arg(c.ed).arg(m).arg(c.em);
        }
        // 代数恒等式 + 符号约定全域扫（遇首败即停，diag 只记第一处）。
        for (int b : { 16, -16, 7, -7 }) {
            for (int a = -4097; a <= 4097 && ok; ++a) {
                const int d = floorDiv(a, b), m = floorMod(a, b);
                if (!(a == b * d + m && (m == 0 || (m > 0) == (b > 0)))) {
                    ok = false;
                    diag += QStringLiteral("[identity a=%1 b=%2 d=%3 m=%4] ").arg(a).arg(b).arg(d).arg(m);
                }
            }
        }
        // chunk 路由语义钉（负方块坐标 → chunk/局部，MC 口径）+ 截断除法基线对照（证明权威承重）：
        const bool routePin = floorDiv(-1, 16) == -1 && floorMod(-1, 16) == 15
            && floorDiv(-16, 16) == -1 && floorMod(-16, 16) == 0
            && floorDiv(15, 16) == 0 && floorMod(15, 16) == 15;
        const bool truncBaseline = (-7 / 16 == 0) && (-7 % 16 == -7); // C++ 原生语义（若语言变了本钉红 → 重审本头）
        ok = ok && routePin && truncBaseline;
        if (!routePin) diag += QStringLiteral("[route-pin] ");
        if (!truncBaseline) diag += QStringLiteral("[trunc-baseline] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2005a floor-div-mod negative quadrants (Core/mathtypes.h single"
                             " authority): 15-case all-quadrant table + exhaustive identity sweep"
                             " a in [-4097,4097] x b in {16,-16,7,-7} + MC chunk-routing pin"
                             " (x=-1 -> chunk -1 local 15) + truncating-baseline cross-pin"
                          << (ok ? QString() : diag);
    });

    // ── r2005b：ChunkKey 往返 / 单射 / 哈希稳定 ─────────────────────────────────
    runLeg(QStringLiteral("r2005b ChunkKey round-trip injectivity hash stability (R20.05 foundational"
        " types): the signed-coordinate chunk key (Core/mathtypes.h) pins fromWorld floor semantics"
        " on negatives (world (-1,47) at size 16 -> key (-1,2)), the quint32 two's-complement packed"
        " 64-bit form round-trips through fromPacked bit-exactly for negative chunks (unlike the"
        " legacy quint16-truncation packing in world.cpp packGrowthCell which silently wraps), the"
        " 7x7 grid cx,cz in [-3,3] packs 49-distinct (injectivity) with hash()==hash() stable and"
        " equal keys hashing equal, hashMix64 being a bijection pins hash injectivity on the same"
        " grid, flatIndex is the verbatim cx+chunksX*cz formula (parity against the raw expression"
        " over the grid at chunksX=13), lexicographic operator< orders the grid monotonically, and"
        " the type is usable from QHash (insert at key fromWorld(-32,80,16)==(-2,5), read back) and"
        " std::unordered_set; Chunk::kSize==16 is cross-pinned so the Core chunkSize parameter and"
        " the World-layer constant cannot drift"), [&]() {
        bool ok = true;
        QString diag;

        // fromWorld 负坐标 floor 语义 + packed 负坐标往返（位精确）：
        const ChunkKey k1 = ChunkKey::fromWorld(-1, 47, 16);
        ok = ok && k1.cx == -1 && k1.cz == 2;
        if (!(k1.cx == -1 && k1.cz == 2)) diag += QStringLiteral("[fromWorld %1,%2] ").arg(k1.cx).arg(k1.cz);
        const ChunkKey k2 = ChunkKey::fromPacked(k1.packed());
        ok = ok && k2 == k1 && !(k2 != k1);
        if (!(k2 == k1)) diag += QStringLiteral("[packed-rt] ");

        // 7×7 负坐标网格：packed 单射 / 哈希稳定 + 相等键同哈希 / 哈希单射（hashMix64 双射性质）/ 平凡单射：
        std::unordered_set<quint64> packedSeen;
        std::unordered_set<quint64> hashSeen;
        bool gridOk = true;
        for (int cx = -3; cx <= 3 && gridOk; ++cx) {
            for (int cz = -3; cz <= 3 && gridOk; ++cz) {
                const ChunkKey k{ cx, cz };
                if (!packedSeen.insert(k.packed()).second) {
                    gridOk = false;
                    diag += QStringLiteral("[packed-dup %1,%2] ").arg(cx).arg(cz);
                    break;
                }
                if (k.hash() != ChunkKey::fromPacked(k.packed()).hash()) { // 相等键 → 同哈希（稳定性）
                    gridOk = false;
                    diag += QStringLiteral("[hash-unstable %1,%2] ").arg(cx).arg(cz);
                    break;
                }
                if (!hashSeen.insert(k.hash()).second) { // hashMix64 双射 → 网格上哈希必不同
                    gridOk = false;
                    diag += QStringLiteral("[hash-dup %1,%2] ").arg(cx).arg(cz);
                    break;
                }
                // flatIndex 与既有布局公式逐位同式（chunksX=13 任意面）：
                if (k.flatIndex(13) != cx + 13 * cz) {
                    gridOk = false;
                    diag += QStringLiteral("[flat-index %1,%2] ").arg(cx).arg(cz);
                    break;
                }
            }
        }
        ok = ok && gridOk;

        // 字典序全序（网格上 cx 主序单调）+ QHash 可用 + Chunk::kSize 互钉：
        bool ordOk = true;
        for (int cx = -3; cx < 3 && ordOk; ++cx)
            if (!(ChunkKey{ cx, 0 } < ChunkKey{ cx + 1, 0 } && ChunkKey{ 0, cx } < ChunkKey{ 0, cx + 1 }))
                ordOk = false;
        ok = ok && ordOk;
        QHash<ChunkKey, int> m;
        m.insert(ChunkKey{ -2, 5 }, 7);
        const bool qhashOk = m.value(ChunkKey::fromWorld(-32, 80, 16), -1) == 7;
        ok = ok && qhashOk;
        if (!qhashOk) diag += QStringLiteral("[qhash] ");
        const bool sizePin = Chunk::kSize == 16; // Core 参数取值 ↔ World 层常量互钉（防漂移）
        ok = ok && sizePin;
        if (!sizePin) diag += QStringLiteral("[ksize=%1] ").arg(Chunk::kSize);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2005b ChunkKey round-trip injectivity hash stability: signed"
                             " fromWorld floor semantics + quint32-complement packed 64-bit"
                             " bit-exact round-trip on negatives + 7x7 negative grid packed/hash"
                             " injectivity + stable equal-key hashing + verbatim flatIndex parity"
                             " + lexicographic order + QHash/std::unordered_set usability +"
                             " Chunk::kSize==16 cross-pin"
                          << (ok ? QString() : diag);
    });

    // ── r2005c：BlockPos 邻接 / 运算 + World 层最小采用行为行 ─────────────────────
    runLeg(QStringLiteral("r2005c BlockPos adjacency arithmetic World adoption (R20.05 foundational"
        " types): the block coordinate type (Core/mathtypes.h) pins its fixed-order 6-neighbor set"
        " (+X,-X,+Y,-Y,+Z,-Z), adjacency as exactly-Manhattan-1 accepting self/diagonal/"
        " manhattan-2 rejections while negative coordinates are equally valid"
        " ((-1,-1,-1) adj (-1,0,-1)), component-wise + - equality, and the plan acceptance'new"
        " types usable by World and tests' behaviorally on the real rig world: ChunkManager's"
        " additive BlockPos overloads (chunkAtWorld/blockAt/setBlock) and World's"
        " (blockAt/setBlock) agree verbatim with their int versions - chunk lookup pointer-equal"
        " in-world and both-null out-of-bounds at (-1,-1), a full-height column read at (20,*,20)"
        " byte-equal across the overload pair, and a write round at rig-outside cell (90,41,170)"
        " reads the original id first (worldgen hills legitimately reach y=41 there - writing the"
        " SAME id is setBlock's lawful no-change rejection, first-run sb=0/w=3 lesson), writes a"
        " different id via World::setBlock(BlockPos), reads it back through World and"
        " ChunkManager::blockAt(BlockPos) alike, then restores the original id in-leg so the world"
        " baseline face keeps zero residue (section runs last regardless)"), [&]() {
        bool ok = true;
        QString diag;
        const BlockPos p{ 5, 41, 7 };

        // 6 邻固定序内容钉 + 邻接对称性：
        const auto nb = p.neighbors();
        bool nbOk = int(nb.size()) == 6
            && nb[0] == BlockPos{ 6, 41, 7 } && nb[1] == BlockPos{ 4, 41, 7 }
            && nb[2] == BlockPos{ 5, 42, 7 } && nb[3] == BlockPos{ 5, 40, 7 }
            && nb[4] == BlockPos{ 5, 41, 8 } && nb[5] == BlockPos{ 5, 41, 6 };
        for (const auto &n : nb)
            nbOk = nbOk && p.isAdjacentTo(n) && n.isAdjacentTo(p);
        ok = ok && nbOk;
        if (!nbOk) diag += QStringLiteral("[neighbors] ");

        // 非邻接负例（自身 / 对角 / 曼哈顿 2）+ 负坐标同等有效 + 运算 / 比较：
        const bool adjOk = !p.isAdjacentTo(p)
            && !p.isAdjacentTo(BlockPos{ 6, 42, 7 })   // 对角（切比雪夫 1、曼哈顿 2）
            && !p.isAdjacentTo(BlockPos{ 7, 41, 7 })   // 曼哈顿 2
            && BlockPos{ -1, -1, -1 }.isAdjacentTo(BlockPos{ -1, 0, -1 });
        const bool arithOk = p + BlockPos{ -6, 1, -7 } == BlockPos{ -1, 42, 0 }
            && p - p == BlockPos{ 0, 0, 0 }
            && (p != BlockPos{ 5, 41, 8 })
            && p.manhattanLength() == 53; // |5|+|41|+|7|
        ok = ok && adjOk && arithOk;
        if (!adjOk) diag += QStringLiteral("[adj] ");
        if (!arithOk) diag += QStringLiteral("[arith] ");

        // World 层最小采用：BlockPos 加性重载 ↔ int 版全等（真 rig 世界行为行）。
        const ChunkManager &cm = w.chunks();
        const bool lookupOk = cm.chunkAtWorld(BlockPos{ 10, 0, 10 }) == cm.chunkAtWorld(10, 10)
            && cm.chunkAtWorld(BlockPos{ -1, 0, -1 }) == nullptr
            && cm.chunkAtWorld(-1, -1) == nullptr;
        bool colOk = true;
        for (int y = 0; y < w.height(); ++y)
            colOk = colOk && cm.blockAt(BlockPos{ 20, y, 20 }) == cm.blockAt(20, y, 20);
        ok = ok && lookupOk && colOk;
        if (!lookupOk) diag += QStringLiteral("[lookup] ");
        if (!colOk) diag += QStringLiteral("[column] ");

        // 写路径（即写即还原本格；本段置尾执行——世界基线面零残留。槽位 (90,41,170) 在 rig
        //   网格外，worldgen 山体可自然到 41 高 → 写前读原 id，换异 id 保「真改动」（写同 id
        //   是 setBlock 合法拒绝面——首跑 sb=0/w=3 实锤即此），断言读写链 + 还原：
        const BlockPos q{ 90, 41, 170 }; // 界内（width 96 / depth 180 / height 48）
        const int orig = int(w.blockAt(q));
        const int newId = (orig == int(BR::Stone)) ? int(BR::Cobble) : int(BR::Stone);
        const bool sbRet = w.setBlock(q, quint8(newId));
        const int wRead = int(w.blockAt(q));
        const int cmRead = int(cm.blockAt(BlockPos{ 90, 41, 170 }));
        const bool writeOk = sbRet && wRead == newId && cmRead == newId;
        w.setBlock(q, quint8(orig)); // 还原原 id（即写即清——基线面零残留，比「清回空气」更强）
        const bool cleanOk = w.blockAt(q) == quint8(orig);
        ok = ok && writeOk && cleanOk;
        if (!writeOk) diag += QStringLiteral("[write sb=%1 orig=%2 new=%3 w=%4 cm=%5] ")
                                 .arg(sbRet).arg(orig).arg(newId).arg(wRead).arg(cmRead);
        if (!cleanOk) diag += QStringLiteral("[clean] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2005c BlockPos adjacency arithmetic World adoption: fixed-order"
                             " 6-neighbor set + manhattan-1 adjacency negatives + negative-coord"
                             " validity + component arithmetic + ChunkManager/World additive"
                             " BlockPos overloads verified verbatim against the int pair on the"
                             " real rig world (lookup/column/write round, cleaned in-leg)"
                          << (ok ? QString() : diag);
    });

    // ── r2005d：Tick 权威钉 + Error/Result 雏形 + 示范采用「摘即红」源码钉 ──────────
    runLeg(QStringLiteral("r2005d Tick authority Error-Result skeleton adoption source pins (R20.05"
        " foundational types): Tick::kClockTickMs pins 100 and kClockTickSecs stays consistent"
        " (x1000 == ms); the adoption remove-to-red source pins (pinSet comment-aware counting) lock"
        " the exemplary adoptions into real statements - chunkmanager.cpp carries >=6 floorDiv( and"
        " >=4 floorMod( routing calls (blockAt/chunkAtWorld/setBlock) and >=2 ChunkKey wraps"
        " (chunk()/recreate()), worldclock.h declares kTickMs = Tick::kClockTickMs (reverting any"
        " of these to the literal formulas flips this leg red); the Error/Result skeleton behaves -"
        " Result<int> ok(42) is truthy with value 42 and a zero-code error, fail(404) is falsy"
        " carrying code+message, Result<void> ok/fail mirror it, and bare Error::isError "
        " distinguishes zero from nonzero codes"), [&]() {
        bool ok = true;
        QString diag;

        // Tick 值钉 + 内部一致（秒制 ↔ 毫秒制）：
        const bool tickOk = Tick::kClockTickMs == 100
            && int(Tick::kClockTickSecs * 1000.f) == Tick::kClockTickMs;
        ok = ok && tickOk;
        if (!tickOk) diag += QStringLiteral("[tick] ");

        // 摘即红源码钉（pinSet 字符串感知剥注释——注释提及不计入，钉必锚真实语句）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missCm = pinSet(srcRoot + QStringLiteral("/World/chunkmanager.cpp"), {
            SrcPin("r2005 floorDiv route x6 (blockAt x2 + chunkAtWorld x2 + setBlock x2)", "floorDiv(", 6),
            SrcPin("r2005 floorMod route x4 (blockAt x2 + setBlock x2)", "floorMod(", 4),
            SrcPin("r2005 ChunkKey grid-index wrap x2 (chunk + recreate)", "ChunkKey", 2),
        });
        const QStringList missWc = pinSet(srcRoot + QStringLiteral("/World/worldclock.h"), {
            SrcPin("r2005 Tick declaration-point kTickMs = Tick::kClockTickMs", "Tick::kClockTickMs", 1),
        });
        for (const QString &m : missCm + missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // Error / Result 雏形行为钉：
        const Result<int> r = Result<int>::ok(42);
        const Result<int> e = Result<int>::fail(404, "missing");
        const bool resOk = r.isOk() && bool(r) && r.value() == 42 && !isError(r.error())
            && !e.isOk() && !bool(e) && e.error().code == 404 && e.error().message != nullptr;
        const Result<void> v = Result<void>::ok();
        const Result<void> f = Result<void>::fail(7);
        const bool voidOk = v.isOk() && !f.isOk() && f.error().code == 7 && f.error().message == nullptr;
        const bool bareOk = isError(Error{ 3, "x" }) && !isError(Error{});
        ok = ok && resOk && voidOk && bareOk;
        if (!resOk) diag += QStringLiteral("[result] ");
        if (!voidOk) diag += QStringLiteral("[result-void] ");
        if (!bareOk) diag += QStringLiteral("[error] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2005d Tick authority Error-Result skeleton adoption source pins:"
                             " Tick 100ms value pin + kClockTickSecs consistency + remove-to-red"
                             " pinSet locks on chunkmanager.cpp (floorDiv x6 / floorMod x4 /"
                             " ChunkKey x2) and worldclock.h (kTickMs = Tick::kClockTickMs) +"
                             " Result<int>/Result<void>/Error minimal behavior"
                          << (ok ? QString() : diag);
    });
}
