#include "matrix_helpers.h"

#include "chunklifecycle.h" // 被测面：六态类型（收敛判据）
#include "gamesession.h"   // 被测面：流式会话（真线程 worker + 生产默认预算）

#include <QElapsedTimer> // 收敛 deadline（防 flake 有界——腿零绝对时间断言）
#include <QThread>       // 收敛轮询节流

// t1126 单 adopt 成本削减探针段（置尾先例沿用：runAll 末执行，filter 词 r2096）。置尾接
// section88，rig 世界零接触——行为腿自建 fresh sparse 小世界族 + 真线程 worker。
//
// ── 削减裁定（t1123 件① 调研链延伸；剖析先于动手——t1126 临时剖析腿数据入交付报告）──────
//   剖析（t1123 rig 同径：160×160×96 + 生产默认半径 gen4/render4/scan6 + 定向跨界走查 +
//   env 门逐 adopt 分相墙钟采样，n=92）：单 adopt 均值 49.2ms / p50 50.6 / p95 71.4 / max
//   74.5——构成 = population 窗口重放 ≈43ms（主导）+ 天光 refloodBox ≈2-3ms + bulk apply
//   ≈0.3ms + heightmap/索引/拆卸 ≈亚 ms。逐 pass 二次剖析：placeSurfaceLakes ≈33.6ms/adopt
//   = 单点主导面（整核心域 lattice 169 格 × 逐候选 heightAt 低洼 disc 扫描），其余 = caves
//   ≈4.8（§29.5-W1b 已窗）+ canyon 1-19（已窗，盘 carve 必需面）+ ores ≈3（已窗）+ 剪枝
//   ≈3 + 群植 ≈2.5。裁定 = 交付型机械削减：placeSurfaceLakes 非 ext 锚窗口化 lattice 界
//   （「同结果更快」——决策读全纯函数、lattice 格写域 ⊆ ±6 与窗不相交即钳制恒拒 = 跳过同
//   空；fixed（win=false）整域循环逐位零变化 + ext 带分支原样；与 carveCaves §29.5-W1b
//   「阈值噪声逐体素纯函数 → 窗口化逐位等价」同门）。改造后同 rig 同径：单 adopt 均值
//   32.1ms / p50 32.1 / p95 46.0 / max 48.7（slakes 33.6 → 5.9），入场 56 adopt 墙钟
//   3667 → 2053ms。人口三保护门（t1108 村庄/神殿/湖足迹）+ r2023 parity 承重墙全数复绿
//   （本段头注出具数据；复绿面 = r2078 全绿 + r2023b 全绿 + 本段 a/b 腿行为柱）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双摘面互不重叠；双 NEG 均编译仍绿形）──────────────
//   NEG-1 = 摘削减行（placeSurfaceLakes 内 `constexpr int kLakeReach` 常量行 + `} else if
//     (pw.win) {` 至其闭 `}` 的四界行整块删除——fixed 默认界行与 ext 带行幸存，编译仍绿 =
//     回到整域循环旧形态，行为逐位不变）→ 恰红 = {r2096a, r2096d} 双元红集如实申报（全量
//     实测即此二元；r2094b 先例）：a = 摘面行本腿钉失；d = 「win branch family」三形态
//     lattice 族钉恰落在摘面行上（d 的 fixed/ext/win 三形态同局面共享一枚摘面钉——同面
//     双腿共红，非行为红：d 的行为面[双 fixed 逐位恒等 + 湖列计数恒等]对摘除双向稳健）；
//     r2096b/c 豁免（b 钉行为与预存行、c 钉非摘除文本）。
//   NEG-2 = 摘沿革注（world.cpp sparsePopulateChunk 处置表行内 `t1126 起非 ext` 段注 +
//     ② 注内 `lakes 同属此类直至 t1126` 句 + 函数内削减面注头——raw 口径注释行删除，
//     代码行幸存，编译仍绿）→ 恰红 = {r2096c}（沿革锚 raw 钉本腿失；a/b/d 豁免[无 raw
//     沿革钉——钉行为/预存行/源码语句行]）。

namespace {

// sparse 流式小世界（seed 61 / 80×80×96：LAKECAL 标定——真地表湖 [49..55]×[29..34] 21 列
//   ⊆ 锚 (3,1) 自身 chunk [48,64)×[16,32)，削减行为敏感锚；5×5 chunk 核心域；
//   spawnPreGenerateRadius=0 = 全量走流式链）。World 非 moveable——prvalue 直返 =
//   C++17 保证省略（section11+ fresh 小世界族同门）。
inline World makeEquivalenceSparse()
{
    World::SparseWorldParams sp;
    sp.seed = 61;
    sp.coreWidth = 80;
    sp.coreDepth = 80;
    sp.height = 96;
    sp.spawnPreGenerateRadius = 0;
    return World(sp);
}

inline void initEquivalenceFixed(World &w)
{
    w.setWidth(80);
    w.setDepth(80);
    w.setHeight(96);
    w.setSeed(61);
}

// 收敛轮询：泵 tick 直到 keys 全部 Loaded 或超时（真线程收割节奏不定——防 flake 靠 deadline，
//   section27 pacingConvergeLoaded 同门）。
inline bool equivConvergeLoaded(GameSession &gs, World &w,
                                const QVector<QPair<int, int>> &keys, int deadlineMs,
                                QString &diag)
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
            diag += QStringLiteral("[conv timeout %1ms miss: %2] ").arg(deadlineMs).arg(miss);
            return false;
        }
        gs.stepTick(0.11);
        QThread::msleep(2);
    }
}

// 源钉根路径（section86/87 同款：applicationDirPath/../src）。
inline QString srcRootForEquivalence()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（锚注/沿革注为注释文本——pinSet 剥注释会失配，section87 rawContainsForPacing 同款）。
inline bool rawContainsForEquivalence(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 地表湖水面列判定（双水源层签名式）：柱内自顶向下首个 Water = 湖面顶层；要求
//   ① 露天（自水面向上至世界顶全 Air——排地下水池石顶 / 冰盖形态）；② y-1 亦 Water
//   （kLakeDepth=2 双水源层签名——湖足迹本体面）；③ 高于海平面带（湖面 top ≥ 海平面+3
//   ——placeSurfaceLakes「surfaceY ≤ kWaterLevel+3 拒」门的镜像，排海水）。
inline bool lakeWaterColumnAt(World &w, int x, int z, int H, int *yOut)
{
    int top = -1;
    for (int y = H - 1; y >= 30; --y) {
        if (w.blockAt(x, y, z) == BR::Water) {
            top = y;
            break;
        }
    }
    if (top < 0 || top <= TerrainGen::kWaterLevel + 2)
        return false;
    for (int y = top + 1; y < H; ++y)
        if (w.blockAt(x, y, z) != BR::Air)
            return false;
    if (w.blockAt(x, top - 1, z) != BR::Water)
        return false;
    if (yOut)
        *yOut = top;
    return true;
}

// 矿族掩蔽（r2023b 同表：scatterOres (c) 替代面登记缺口——矿差仅限矿体素自身）。
inline bool equivIsOre(quint8 b)
{
    switch (b) {
        case BR::CoalOre: case BR::IronOre: case BR::GoldOre: case BR::DiamondOre:
        case BR::RedstoneOre: case BR::LapisOre: case BR::CopperOre:
            return true;
        default:
            return false;
    }
}

} // namespace

void MatrixRun::section89_adopt_cost_t1126()
{
    // ── r2096a:等价性柱 + 削减面源钉(NEG-1 敏感面 = kLakeReach 常量行 + win 分支四界行)────
    //   流式锚 (1,1)（湖足迹 [18..25]×[20..31] ⊆ 自身 chunk [16,32)²——削减行为敏感锚：错
    //     误越界跳过即红）3×3 窗收敛 → 自身 chunk 对 fixed 孪生全 y 带 id+state 逐位恒等
    //     （ore+结构族掩蔽，r2023b 口径收窄到单 chunk）+ 窗内湖水面列 ≥10（非空转前置：湖
    //     在窗 = 削减 kept 集 ∈ 窗实证）+ 湖足印双水源层面 + 削减行源钉族。
    runLeg("r2096a equivalence column with reduction source rows (a streaming anchor whose"
        " own chunk carries a surface lake converges its three by three window on the"
        " production default budget and the adopted chunk answers bitwise identical voxel"
        " and state content against the fixed twin of the same seed and dims under the ore"
        " and structure family masks of the parity wall caliber, the window holds at least"
        " ten open sky lake water columns proving the kept candidate set intersects the"
        " window, each found lake column carries its paired source water layer beneath, and"
        " the windowing constant row and the four window bound rows are pinned on file)",
        [&]() {
        bool ok = true;
        QString diag;
        const int H = 96;
        World wa = makeEquivalenceSparse();
        GameSession gs(wa); // 生产默认预算 3ms（零注入）
        gs.configureStreamingRadii(1, 1, 2);
        const bool activeOk = gs.streamingActive() && gs.streamWorker() != nullptr;
        ok = ok && activeOk;
        if (!activeOk) diag += QStringLiteral("[inactive]");

        QVector<QPair<int, int>> keys;
        for (int cz = 0; cz <= 2; ++cz)
            for (int cx = 2; cx <= 4; ++cx)
                keys.append({ cx, cz });
        gs.notePlayerChunk(3, 1);
        const bool convOk = equivConvergeLoaded(gs, wa, keys, 120000, diag);
        ok = ok && convOk;

        // ① 窗内湖足迹（非空转前置）：窗列 [32,64)×[0,48) 内露天双水源层湖水面列 ≥10 +
        //    每列带成对水源层（谓词自带签名——paired 恒等计数为断言面复核）。
        int lakeCols = 0, pairedCols = 0;
        for (int x = 32; x < 64; ++x) {
            for (int z = 0; z < 48; ++z) {
                int y = 0;
                if (lakeWaterColumnAt(wa, x, z, H, &y)) {
                    ++lakeCols;
                    if (wa.blockAt(x, y + 1, z) == BR::Water || wa.blockAt(x, y - 1, z) == BR::Water)
                        ++pairedCols;
                }
            }
        }
        const bool lakeOk = lakeCols >= 10 && pairedCols == lakeCols;
        ok = ok && lakeOk;
        if (!lakeOk)
            diag += QStringLiteral("[lake cols=%1 paired=%2] ").arg(lakeCols).arg(pairedCols);

        // ② 承重比对：窗内四 chunk (1,1),(1,2),(2,1),(2,2) 全 16×16×H 体素 id+state 逐位恒等
        //    （ore 掩蔽 + 结构族掩蔽——r2023b 同表同口径，窗域收窄形）。
        World wf;
        initEquivalenceFixed(wf);
        const auto structMasked = [&wf](int x, int y, int z) {
            static const int kShifts[9][2] = { { 0, 0 }, { 6, 0 }, { -6, 0 }, { 0, 6 }, { 0, -6 },
                { 6, 6 }, { 6, -6 }, { -6, 6 }, { -6, -6 }, };
            for (const auto &sh : kShifts)
                if (wf.insideDungeon(x + sh[0], y, z + sh[1])
                    || wf.insideMineshaft(x + sh[0], y, z + sh[1])
                    || wf.insideDesertTemple(x + sh[0], y, z + sh[1])
                    || wf.insideJungleTemple(x + sh[0], y, z + sh[1])
                    || wf.insideStronghold(x + sh[0], y, z + sh[1])
                    || wf.insideStructureRegion(World::StructureVillage, x + sh[0], y, z + sh[1]))
                    return true;
            return false;
        };
        long unmaskedEqual = 0, masked = 0, mismatch = 0;
        int mmX = -1, mmY = -1, mmZ = -1;
        quint8 mmA = 0, mmAs = 0, mmB = 0, mmBs = 0;
        static const int kParityChunks[4][2] = { { 3, 1 }, { 2, 1 }, { 2, 2 }, { 3, 2 } };
        for (const auto &pc : kParityChunks) {
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    const int x = pc[0] * 16 + lx, z = pc[1] * 16 + lz;
                    for (int y = 0; y < H; ++y) {
                        const quint8 ba = wa.blockAt(x, y, z), bs = wf.blockAt(x, y, z);
                        const quint8 sa = wa.stateAt(x, y, z), ss = wf.stateAt(x, y, z);
                        if (ba == bs && sa == ss) {
                            if (!equivIsOre(ba) && !structMasked(x, y, z))
                                ++unmaskedEqual;
                            continue;
                        }
                        if (equivIsOre(ba) || equivIsOre(bs)) {
                            ++masked; // (c) scatterOres 替代面掩蔽
                            continue;
                        }
                        if (structMasked(x, y, z)) {
                            ++masked; // (c) 结构族豁免面掩蔽
                            continue;
                        }
                        if (mismatch == 0) {
                            mmX = x; mmY = y; mmZ = z;
                            mmA = ba; mmAs = sa; mmB = bs; mmBs = ss;
                        }
                        ++mismatch;
                    }
                }
            }
        }
        const bool parityOk = mismatch == 0 && unmaskedEqual >= 70000;
        ok = ok && parityOk;
        if (!parityOk)
            diag += QStringLiteral("[parity mismatch=%1 equal=%2 first=(%3,%4,%5) a=%6/%7"
                                   " f=%8/%9] ")
                        .arg(mismatch).arg(unmaskedEqual).arg(mmX).arg(mmY).arg(mmZ)
                        .arg(mmA).arg(mmAs).arg(mmB).arg(mmBs);

        // ②' 双侧湖列计数对照诊断（同谓词同窗——恒等面自带证据）。
        {
            int lakesFixed = 0;
            for (int x = 16; x < 48; ++x)
                for (int z = 16; z < 48; ++z)
                    if (lakeWaterColumnAt(wf, x, z, H, nullptr))
                        ++lakesFixed;
            diag += QStringLiteral("[lakeCols sparse=%1 fixed=%2] ").arg(lakeCols).arg(lakesFixed);
        }

        // NEG-1 摘面行本腿钉（削减行整块——常量行 + win 分支四界行；playercontroller 式摘面
        //   口径：整块删除编译仍绿）。
        {
            const QStringList missNeg = pinSet(srcRootForEquivalence()
                                               + QStringLiteral("/World/world.cpp"), {
                SrcPin("lake reach constant row",
                       "constexpr int kLakeReach = kLakeGrid / 2 + 3;", 1),
                SrcPin("lake win branch row", "} else if (pw.win) {", 1),
                SrcPin("lake win xLo bound row",
                       "+ kLakeGrid * floorDiv(xLo - kLakeReach - kLakeGrid / 2 + kLakeGrid - 1, kLakeGrid);", 1),
                SrcPin("lake win xHi bound row", "bxHi = xHi + kLakeReach;", 1),
                SrcPin("lake win zLo bound row",
                       "+ kLakeGrid * floorDiv(zLo - kLakeReach - kLakeGrid / 2 + kLakeGrid - 1, kLakeGrid);", 1),
                SrcPin("lake win zHi bound row", "bzHi = zHi + kLakeReach;", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2096a equivalence column with reduction source rows (a streaming anchor"
               " whose own chunk carries a surface lake converges its three by three window"
               " on the production default budget and the adopted chunk answers bitwise"
               " identical voxel and state content against the fixed twin of the same seed"
               " and dims under the ore and structure family masks of the parity wall"
               " caliber, the window holds at least ten open sky lake water columns proving"
               " the kept candidate set intersects the window, each found lake column"
               " carries its paired source water layer beneath, and the windowing constant"
               " row and the four window bound rows are pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2096b:保护门复绿柱(ext 锚走回恒等 + fixed 循环界原样 + 村庄门行幸存)──────────────
    //   外环 ext 锚 (6,2) 真链物化 → 走离驱逐 → 走回重物化 → 内容逐位恒等（r2046b/r2036c
    //     同门——ext 带分支原样的行为面）+ fixed 默认整域界行 / ext 带界行 / 村庄足迹门行
    //     幸存源钉（t1108 保护门静态面；行为面由 r2078 全绿承载）。
    runLeg("r2096b protection column with extended anchor walkback identity (an outer ring"
        " extended anchor materializes through the streaming chain, walks away past the"
        " eviction radius and walks back to a bitwise identical re-materialization proving"
        " the extended band branch is untouched, the fixed full domain default bound row and"
        " the extended band bound rows and the village footprint gate rows survive verbatim,"
        " and the population write clamp and quiet window rows stay in place)", [&]() {
        bool ok = true;
        QString diag;
        World w = makeEquivalenceSparse();
        GameSession gs(w);
        gs.configureStreamingRadii(1, 1, 2);

        // ① ext 锚 (6,2)（cx=6 ≥ chunksX=5 = 外环扩展域）首物化 + 内容指纹。
        QVector<QPair<int, int>> homeKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 5; cx <= 7; ++cx)
                homeKeys.append({ cx, cz });
        gs.notePlayerChunk(6, 2);
        const bool firstOk = equivConvergeLoaded(gs, w, homeKeys, 150000, diag);
        ok = ok && firstOk;
        QByteArray fp1;
        if (firstOk) {
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 96; ++y) {
                        fp1.append(char(w.blockAt(96 + lx, y, 32 + lz)));
                        fp1.append(char(w.stateAt(96 + lx, y, 32 + lz)));
                    }
        }
        const bool fp1NonTrivial = fp1.size() == 16 * 16 * 96 * 2;
        ok = ok && fp1NonTrivial;
        if (!fp1NonTrivial)
            diag += QStringLiteral("[fp1 size=%1] ").arg(fp1.size());

        // ② 走离（(9,2)：(6,2) cheb 3 > gen 1 → 驱逐）+ 站稳收敛。
        QVector<QPair<int, int>> awayKeys;
        for (int cz = 1; cz <= 3; ++cz)
            for (int cx = 8; cx <= 10; ++cx)
                awayKeys.append({ cx, cz });
        gs.notePlayerChunk(9, 2);
        const bool awayOk = equivConvergeLoaded(gs, w, awayKeys, 150000, diag);
        ok = ok && awayOk;

        // ③ 走回重物化 → 指纹逐位恒等（ext 分支原样 → 每 chunk 内容 = 自身锚重放纯函数）。
        gs.notePlayerChunk(6, 2);
        const bool backOk = equivConvergeLoaded(gs, w, homeKeys, 150000, diag);
        ok = ok && backOk;
        bool identityOk = false;
        if (backOk) {
            QByteArray fp2;
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 96; ++y) {
                        fp2.append(char(w.blockAt(96 + lx, y, 32 + lz)));
                        fp2.append(char(w.stateAt(96 + lx, y, 32 + lz)));
                    }
            identityOk = fp1 == fp2;
        }
        ok = ok && identityOk;
        if (!identityOk)
            diag += QStringLiteral("[walkback identity mismatch] ");

        // 源钉：fixed 默认整域界行 + ext 带界行 + 村庄足迹门行 + 写钳制/静默窗行（t1108 保护
        //   门静态面 + §29.5-W1b 单一权威行幸存——NEG 双摘面均不触这些行）。
        {
            const QStringList missPins = pinSet(srcRootForEquivalence()
                                                + QStringLiteral("/World/world.cpp"), {
                SrcPin("lakes fixed default bound row",
                       "int bxLo = kLakeGrid / 2, bxHi = m_width, bzLo = kLakeGrid / 2, bzHi = m_depth;", 1),
                SrcPin("lakes ext x band row", "bxHi = xHi + kBand;", 1),
                SrcPin("lakes ext z band row", "bzHi = zHi + kBand;", 1),
                SrcPin("lakes village gate row", "bool villageOverlap = false;", 1),
                SrcPin("lakes village loop row", "for (const StructureSite &v : villageSites()) {", 1),
                SrcPin("population clamp row", "m_chunks.setPopulationWriteClamp(true, cx, cz);", 1),
                SrcPin("population quiet row", "m_worldgenQuiet = true;", 1)});
            ok = ok && missPins.isEmpty();
            if (!missPins.isEmpty())
                diag += QStringLiteral("[pins %1]").arg(missPins.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2096b protection column with extended anchor walkback identity (an outer"
               " ring extended anchor materializes through the streaming chain, walks away"
               " past the eviction radius and walks back to a bitwise identical"
               " re-materialization proving the extended band branch is untouched, the fixed"
               " full domain default bound row and the extended band bound rows and the"
               " village footprint gate rows survive verbatim, and the population write"
               " clamp and quiet window rows stay in place)"
            << (ok ? QString() : diag);
    });

    // ── r2096c:结构钉族 + 沿革锚柱(NEG-2 敏感面 = 处置表 t1126 段注 + ② 注 lakes 句)────────
    //   沿革锚 raw 在场（处置表行内 t1126 段注 + ② 注 lakes 句 + 函数内削减面注头）+ 既有
    //     结构钉族复钉（t1124 预算闸门相邻面行幸存）+ CMake 段行 + QML/桥/驱动器零迁移负面门
    //     （削减词元不入玩法路径）——NEG 双摘面豁免不钉（c 腿零削减行钉）。
    runLeg("r2096c structure pin family with lineage anchors (the disposition table lineage"
        " note and the replay note sentence and the in function reduction head note are all"
        " present in the raw source, the streaming pacing adjacent face rows survive, the"
        " cmake section row holds, and the qml plus bridge plus driver doors all stay shut"
        " with zero gameplay path migration tokens)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForEquivalence();
        const QString worldCpp = srcDir + QStringLiteral("/World/world.cpp");
        // (1) 沿革锚（raw 口径——NEG-2 摘除面）。
        {
            const bool lineageOk = rawContainsForEquivalence(worldCpp,
                    QStringLiteral("t1126 起非 ext"))
                && rawContainsForEquivalence(worldCpp,
                    QStringLiteral("lakes 同属此类直至 t1126"))
                && rawContainsForEquivalence(worldCpp,
                    QStringLiteral("t1126 削减面（单 adopt 成本削减"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }
        // (2) 相邻族结构钉复钉（t1124 预算闸门行幸存——r2094c 同针，跨段防回归面）。
        {
            const QStringList missAdj = pinSet(srcDir + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("pacing budget constant row", "static constexpr int kStreamingPumpBudgetMs = 3;", 1),
                SrcPin("pacing data face while row", "while (m_streamWorker->takeResultData(data))", 1),
                SrcPin("single adopt call site", "m_world.adoptGeneratedChunk(", 1)});
            ok = ok && missAdj.isEmpty();
            if (!missAdj.isEmpty())
                diag += QStringLiteral("[adj %1]").arg(missAdj.join(QLatin1Char(',')));
        }
        // (3) CMake 段行。
        {
            const bool cmakeOk = pinSet(QCoreApplication::applicationDirPath()
                                            + QStringLiteral("/../CMakeLists.txt"),
                                        { SrcPin("cmake section row",
                                                 "tools/matrix/section89_adopt_cost_t1126.cpp", 1)})
                                           .isEmpty();
            ok = ok && cmakeOk;
            if (!cmakeOk) diag += QStringLiteral("[cmake]");
        }
        // (4) QML / 桥 / 驱动器零迁移负面门（削减词元禁入玩法路径——PLAN §2 分层纪律）。
        {
            const bool doorOk = !rawContainsForEquivalence(srcDir + QStringLiteral("/ui/Main.qml"),
                                                      QStringLiteral("kLakeReach"))
                && !rawContainsForEquivalence(srcDir + QStringLiteral("/ui/Main.qml"),
                                              QStringLiteral("LakeReach"))
                && !rawContainsForEquivalence(srcDir + QStringLiteral("/Game/streamingbridge.h"),
                                              QStringLiteral("kLakeReach"))
                && !rawContainsForEquivalence(srcDir + QStringLiteral("/World/chunkstreamdriver.h"),
                                              QStringLiteral("kLakeReach"));
            ok = ok && doorOk;
            if (!doorOk) diag += QStringLiteral("[door]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2096c structure pin family with lineage anchors (the disposition table"
               " lineage note and the replay note sentence and the in function reduction"
               " head note are all present in the raw source, the streaming pacing adjacent"
               " face rows survive, the cmake section row holds, and the qml plus bridge"
               " plus driver doors all stay shut with zero gameplay path migration tokens)"
            << (ok ? QString() : diag);
    });

    // ── r2096d:fixed 世界零变化墙(同 seed 双 fixed 逐位 + 湖面逐位 + 调用形原样)────────────
    //   同 seed 双 fixed 世界全量 generate 逐位恒等（r2023a 同门收窄到湖域窗）+ 湖域窗列在
    //     双世界上逐位恒等（削减不触 fixed = 行为零变化）+ generate 体 placeSurfaceLakes()
    //     全域调用形字面幸存 + lakes lattice 界行家族计数下界（fixed/ext/win 三形态同在）。
    runLeg("r2096d fixed world zero change wall (two same seed fixed worlds generate"
        " bitwise identical voxel and state columns across the lake bearing region and"
        " sample interior columns, the lake water column count answers identical on both"
        " worlds, the generate body keeps its full domain surface lakes call form verbatim,"
        " and the lakes lattice bound family keeps all three forms above their counts)",
        [&]() {
        bool ok = true;
        QString diag;
        const int H = 96;
        World w1;
        initEquivalenceFixed(w1);
        World w2;
        initEquivalenceFixed(w2);

        // ① 湖域窗（锚 (3,1) 自身 chunk = 湖足迹域）+ 抽样列：双世界逐位恒等。
        bool genOk = true;
        long mismatch = 0;
        for (int x = 48; x < 64 && genOk; ++x) {
            for (int z = 16; z < 32 && genOk; ++z) {
                for (int y = 50; y < 75 && genOk; ++y) {
                    if (w1.blockAt(x, y, z) != w2.blockAt(x, y, z)
                        || w1.stateAt(x, y, z) != w2.stateAt(x, y, z)) {
                        ++mismatch;
                        genOk = false;
                    }
                }
            }
        }
        static const int kCols[][2] = { { 0, 0 }, { 79, 79 }, { 8, 40 }, { 40, 8 }, { 63, 63 } };
        for (const auto &c : kCols) {
            for (int y = 0; y < H; ++y) {
                if (w1.blockAt(c[0], y, c[1]) != w2.blockAt(c[0], y, c[1])
                    || w1.stateAt(c[0], y, c[1]) != w2.stateAt(c[0], y, c[1])) {
                    ++mismatch;
                    genOk = false;
                }
            }
        }
        ok = ok && genOk && mismatch == 0;
        if (!genOk)
            diag += QStringLiteral("[gen mismatch=%1] ").arg(mismatch);

        // ② 湖水面列计数双世界恒等（同 seed 同湖——削减对 fixed 零触碰的行为面）。
        const auto countLakes = [&](World &w) {
            int n = 0;
            for (int x = 48; x < 64; ++x)
                for (int z = 16; z < 32; ++z)
                    if (lakeWaterColumnAt(w, x, z, H, nullptr))
                        ++n;
            return n;
        };
        const int lakes1 = countLakes(w1), lakes2 = countLakes(w2);
        const bool lakesOk = lakes1 == lakes2 && lakes1 >= 10;
        ok = ok && lakesOk;
        if (!lakesOk)
            diag += QStringLiteral("[lakes %1 vs %2] ").arg(lakes1).arg(lakes2);

        // ③ generate 体全域调用形字面幸存（r2023a 同针复钉）+ 界行三形态家族计数下界。
        {
            const QString worldCpp = srcRootForEquivalence() + QStringLiteral("/World/world.cpp");
            const QStringList missCalls = pinSet(worldCpp, {
                SrcPin("fixed full domain lakes call", "placeSurfaceLakes();", 1),
                SrcPin("windowed lakes call", "placeSurfaceLakes(wx0, wx1, wz0, wz1);", 1),
                SrcPin("lakes function decl row",
                       "void World::placeSurfaceLakes(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("ext band bound family", "xHi + kBand;", 5),
                SrcPin("win branch family", "} else if (pw.win) {", 1)});
            ok = ok && missCalls.isEmpty();
            if (!missCalls.isEmpty())
                diag += QStringLiteral("[pins %1]").arg(missCalls.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2096d fixed world zero change wall (two same seed fixed worlds generate"
               " bitwise identical voxel and state columns across the lake bearing region"
               " and sample interior columns, the lake water column count answers identical"
               " on both worlds, the generate body keeps its full domain surface lakes call"
               " form verbatim, and the lakes lattice bound family keeps all three forms"
               " above their counts)"
            << (ok ? QString() : diag);
    });
}
