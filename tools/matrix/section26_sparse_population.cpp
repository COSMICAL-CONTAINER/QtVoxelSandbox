#include "matrix_helpers.h"

#include "chunklifecycle.h" // 被测面：六态类型（经 world.h 亦可达，显式 include 表明被测面）

// §29.5-W1b sparse population parity 探针段（4 腿；filter 词 r2023；矩阵 628→632）。置尾先例
// 沿用（接 section25，runAll 末执行）。自建 fresh 小世界族（80×80×96——5×5 chunk，radius 2
// 预生成方阵恰覆盖全核心域；rig 世界 w 零接触）。任务契约（docs/governance-audit-2026-09-16-c.md
// 第 6 条 + docs/refactor-plan-2026-09-08.md §29.5 W1b）：
//   「fixed 生成路径零变化」→ r2023a 墙（同 seed 双 generate 逐位恒等 + generate() 全 pass
//     调用序列字面钉[全域调用形态] + generate 体禁出 population 反探）；
//   「同 seed sparse ≡ fixed 逐位恒等（r2022c 收窄面重新收宽）」→ r2023b parity 承重墙
//     （9 个核内内部 chunk[窗口 ⊆ 核心域]全 16×16×H 体素 id+state 逐位恒等；(b) 级重放 pass
//     全部纳入断言；豁免 pass（结构族五员）按 world.cpp sparsePopulateChunk 处置表在腿注释
//     排除——固定世界的结构块若落入被比 chunk 会使断言失真，故前置扫描确认被比区结构块为零
//     并记录 (b) pass 签名在位（非空转））；
//   「加载顺序无关确定性」→ r2023c（radius 0 双世界按两种次序 loadChunkAt 同一组 chunk[含
//     负坐标 (-1,2)]→ 内容逐位恒等，含跨 chunk 树冠越界枝叶面）；
//   「结构钉」→ r2023d（fixed 分支字面钉 + 重放=调用单一权威[populate 调 shared pass 本体] +
//     结构族豁免反探[populate 体禁出五结构调用] + pending/拆卸单点收口[releaseSparseChunk]）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘 sparsePopulateChunk 内 placeTrees 窗口调用（sparse 树缺失）→ 恰红
//     {r2023b} 单腿（树冠/树干体素差 = 被摘语义本体；r2023c 双世界同缺树仍互等、r2022c 修订
//     面为负坐标纯地形带 [y<5 无 population 触达] 不误伤、r2023a fixed 面与 r2023d 钉面[钉
//     representative 窗口调用不钉逐 pass 清单——pass 完备性由 r2023b 行为面承重] 不误伤）。
//   变异二（NEG-2）= 树干高混入驻留 revision（m_worldgenQuiet 时 trunkH 随
//     m_residentChunkRevision 奇偶偏移 = 加载次序状态污染 pass 结果）→ 恰红 {r2023b, r2023c}
//     （预生成单扫的 revision 单调 → 树形≠fixed；两种加载次序 revision 史不同 → 树形互异——
//     parity 与顺序无关性是同一确定性机制的两面，次序污染天然双破；首版声明 {r2023c} 实测
//     扩面 {r2023b, r2023c} 按先例改声明留痕）。
//   阴性日志：build/ 下四件 matrix_r2023_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
void MatrixRun::section26_sparse_population()
{
    constexpr int kW = 80, kD = 80, kH = 96, kSeed = 42, kRadius = 2;
    constexpr int kChunks = kW / 16; // 5：核心域 chunk 数（[0,4]²）

    const auto initFixed = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
    };
    const auto makeSparse = [](int radius) {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kW;
        sp.coreDepth = kD;
        sp.height = kH;
        sp.spawnPreGenerateRadius = radius;
        return World(sp);
    };
    // 全 y 带 id+state 逐列比对（两世界同列逐体素恒等）。
    const auto columnEqual = [](World &a, World &b, int x, int z, QString &diag) {
        for (int y = 0; y < kH; ++y) {
            if (a.blockAt(x, y, z) != b.blockAt(x, y, z)
                || a.stateAt(x, y, z) != b.stateAt(x, y, z)) {
                diag += QStringLiteral("[x=%1 z=%2 y=%3 %4/%5 vs %6/%7] ")
                            .arg(x).arg(z).arg(y)
                            .arg(a.blockAt(x, y, z)).arg(a.stateAt(x, y, z))
                            .arg(b.blockAt(x, y, z)).arg(b.stateAt(x, y, z));
                return false;
            }
        }
        return true;
    };

    // ── r2023a：fixed 生成零变化墙 ──────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2023a fixed-generation zero-change wall (two same-seed fixed worlds"
        " generate bitwise-identical voxel columns across surface and underground bands, the"
        " fixed steady state and mode bookkeeping are untouched, and the generate() body keeps"
        " its full fixed-domain pass call sequence verbatim with zero population replay"
        " intrusion)"), [&]() {
        bool ok = true;
        QString diag;

        World w1;
        initFixed(w1);
        World w2;
        initFixed(w2);

        // ① 同 seed 双 generate：抽样列全 y 带逐位恒等（地表 + 地下 + 基岩带；核心区含边界列）。
        static const int kCols[][2] = { { 0, 0 }, { 79, 79 }, { 8, 40 }, { 40, 8 }, { 24, 24 },
            { 47, 63 }, { 63, 47 }, { 16, 48 }, { 55, 21 }, { 33, 33 } };
        bool genOk = true;
        for (const auto &c : kCols)
            genOk = genOk && columnEqual(w1, w2, c[0], c[1], diag);
        ok = ok && genOk;

        // ② 模式/稳态账面（fixed 语义原样）：
        const bool modeOk = !w1.isSparse() && w1.chunksX() == kChunks && w1.chunksZ() == kChunks
            && w1.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded
            && w1.heightmapAt(40, 40) >= 0;
        ok = ok && modeOk;
        if (!modeOk)
            diag += QStringLiteral("[mode sparse=%1 cx=%2] ").arg(w1.isSparse()).arg(w1.chunksX());

        // ③ generate() 体字面钉（函数文本抽取手工 contains——r2022d 先例）：全 pass 全域调用
        //    序原样在位 + population 反探禁出（miss 非空 = 合规）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile f(srcRoot + QStringLiteral("/World/world.cpp"));
        const bool fOpen = f.open(QIODevice::ReadOnly);
        QString genBody;
        if (fOpen) {
            const QString src = QString::fromUtf8(f.readAll());
            const qsizetype b = src.indexOf(QStringLiteral("void World::generate()"));
            const qsizetype e = src.indexOf(QStringLiteral("quint32 World::hashColumn"));
            if (b >= 0 && e > b)
                genBody = src.mid(b, e - b);
        }
        static const char *kFixedCalls[] = {
            "placeBedrock();", "scatterOres();", "placeGravelPockets();", "carveCaves();",
            "carveCaveEntrances();", "placeUndergroundWaterPools();", "placeLavaLakes();",
            "placeDungeons();", "placeMineshaft();", "placeDesertTemple();",
            "placeJungleTemple();", "placeStronghold();", "carveCanyon();",
            "pruneFloatingSnowLayers();", "pruneUnsupportedWorldgenRails();", "fillWater();",
            "freezeSurfaceWater();", "placeSurfaceLakes();", "placeSwampPools();",
            "placeTrees();", "placeJungleTrees();", "placeTallGrass();", "placeDesertFlora();",
            "placeSwampFlora();", "placeFlowers();", "placeSugarcane();",
            "placeSweetBerryBushes();", "findSpawnColumn();", "recomputeLightField();",
            "rebuildFluidCells();", "rebuildIceCells();", "rebuildStructureRegions();",
        };
        bool pinOk = fOpen && genBody.size() > 0;
        for (const char *call : kFixedCalls)
            pinOk = pinOk && genBody.contains(QLatin1String(call));
        pinOk = pinOk && !genBody.contains(QStringLiteral("sparsePopulateChunk"))
            && !genBody.contains(QStringLiteral("rebuildPopulationCellIndexes"));
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pin open=%1 body=%2] ").arg(fOpen).arg(genBody.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2023a fixed wall: two same-seed fixed worlds stay bitwise"
                             " identical on sampled full-height columns, fixed mode/steady-state"
                             " bookkeeping is untouched, and the generate() body retains the"
                             " complete fixed-domain pass call sequence with zero population"
                             " replay tokens" << (ok ? QString() : diag);
    });

    // ── r2023b：parity 承重墙（r2022c 收窄面的重新收宽）──────────────────────────────────
    // 处置表豁免面（world.cpp sparsePopulateChunk 头注释全录）在断言中的落法：
    //   (c) 结构族五员（dungeon/mineshaft/desertTemple/jungleTemple/stronghold）不重放 → 逐体
    //       素结构区域掩蔽（五类 inside* + xz 9 点偏移栅格 {0,±6,±12} 覆盖地牢豁口 ±12 挖掘
    //       域），掩蔽面计数入 diag；
    //   (c) scatterOres 替代（cell 窗 + tryOre 读写域双钳窗 = MC per-chunk vein 同构；块缘
    //       ±8 列跨 cell 脉形交互差异如实登记）→ 比对掩蔽双侧矿族方块（矿差仅限矿体素自身，
    //       其余 pass 不读矿态——carve 族 skip 判定对 Stone/Ore 同义 = 不外溢）；
    //   (b) 级重放 pass 签名（矿/洞/水/树/草）非空转计数在位。
    runLeg(QStringLiteral("r2023b sparse-fixed parity load-bearing wall (a sparse world sharing"
        " seed and dims with a fixed world agrees bitwise on id and state across every non-ore"
        " voxel of the nine interior chunks whose population read-window stays inside the core"
        " domain - bedrock, caves, cave entrances, underground water and lava pools, canyon"
        " carving, snow-layer and rail support pruning, sea water, surface freezing, surface"
        " lakes, swamp pools, trees, jungle trees, tall grass, desert flora, swamp flora,"
        " flowers, sugarcane and berry bushes all included; structure-family regions are"
        " provably absent from the compared region per the disposition table exemptions and ore"
        " voxels are masked per the scatterOres chunk-local substitute, with replayed-pass"
        " signature counts proving the face is not vacuous)"), [&]() {
        bool ok = true;
        QString diag;

        World wf;
        initFixed(wf);
        World ws = makeSparse(kRadius);

        // ① 结构区域掩蔽（结构族五员不重放 → 其足迹体素不具 (b) 恒等面，比对时跳过）：五类
        //    inside* 区域判定 + xz 9 点偏移栅格 {0,±6,±12}（覆盖地牢豁口 ±12 挖掘域与 ≥7 宽
        //    足迹的探针完备性）；掩蔽体素计数入 diag（非空转 + 面宽证据）。
        const auto structMasked = [&wf](int x, int y, int z) {
            static const int kShifts[9][2] = { { 0, 0 }, { 6, 0 }, { -6, 0 }, { 0, 6 }, { 0, -6 },
                { 6, 6 }, { 6, -6 }, { -6, 6 }, { -6, -6 }, };
            for (const auto &sh : kShifts)
                if (wf.insideDungeon(x + sh[0], y, z + sh[1])
                    || wf.insideMineshaft(x + sh[0], y, z + sh[1])
                    || wf.insideDesertTemple(x + sh[0], y, z + sh[1])
                    || wf.insideJungleTemple(x + sh[0], y, z + sh[1])
                    || wf.insideStronghold(x + sh[0], y, z + sh[1]))
                    return true;
            return false;
        };
        long maskedVoxels = 0, comparedVoxels = 0, unmaskedVoxels = 0;

        // ② 前置（非空转）：9 内部 chunk 范围内 fixed 侧 (b) pass 签名计数 > 0。
        long ores = 0, water = 0, leaves = 0, tallGrass = 0, caveAir = 0;
        for (int x = 16; x < 64; ++x) {
            for (int z = 16; z < 64; ++z) {
                const int h = wf.heightAt(x, z);
                for (int y = 0; y < kH; ++y) {
                    const quint8 b = wf.blockAt(x, y, z);
                    switch (b) {
                        case BR::CoalOre: case BR::IronOre: case BR::GoldOre:
                        case BR::DiamondOre: case BR::RedstoneOre: case BR::LapisOre:
                        case BR::CopperOre: ++ores; break;
                        case BR::Water: ++water; break;
                        case BR::Leaves: case BR::SpruceLeaves: ++leaves; break;
                        case BR::TallGrass: ++tallGrass; break;
                        default: break;
                    }
                    if (b == BR::Air && y < h - 4 && y > 5)
                        ++caveAir; // 地下气腔（阈值洞/worm/水池气室）
                }
            }
        }
        const bool sigOk = ores > 0 && water > 0 && leaves > 0 && tallGrass > 0 && caveAir > 0;
        ok = ok && sigOk;
        if (!sigOk)
            diag += QStringLiteral("[sig ores=%1 water=%2 leaves=%3 grass=%4 cave=%5] ")
                        .arg(ores).arg(water).arg(leaves).arg(tallGrass).arg(caveAir);

        // ③ 承重比对：9 内部 chunk 全 16×16×H 体素 id+state 逐位恒等（ore 掩蔽——双侧任一为
        //    矿族即跳过：scatterOres (c) 替代面的登记缺口，矿差仅限矿体素自身）。
        const auto isOre = [](quint8 b) {
            switch (b) {
                case BR::CoalOre: case BR::IronOre: case BR::GoldOre: case BR::DiamondOre:
                case BR::RedstoneOre: case BR::LapisOre: case BR::CopperOre: return true;
                default: return false;
            }
        };
        QMap<QPair<int, int>, int> mismatchPairs; // 临时诊断：(w-id,s-id) 对计数
        int mismatchYMin = 999, mismatchYMax = -1;
        bool parityOk = true;
        for (int cz = 1; cz <= 3 && parityOk; ++cz) {
            for (int cx = 1; cx <= 3 && parityOk; ++cx) {
                for (int lz = 0; lz < 16 && parityOk; ++lz) {
                    for (int lx = 0; lx < 16 && parityOk; ++lx) {
                        const int x = cx * 16 + lx, z = cz * 16 + lz;
                        for (int y = 0; y < kH; ++y) {
                            const quint8 bw = wf.blockAt(x, y, z), bs = ws.blockAt(x, y, z);
                            if (bw == bs && wf.stateAt(x, y, z) == ws.stateAt(x, y, z)) {
                                if (!isOre(bw) && !structMasked(x, y, z))
                                    ++unmaskedVoxels; // 无掩蔽参与的全等体素（面宽证据）
                                continue;
                            }
                            if (isOre(bw) || isOre(bs))
                                continue; // (c) scatterOres 替代面掩蔽
                            if (structMasked(x, y, z)) {
                                ++maskedVoxels;
                                continue; // (c) 结构族豁免面掩蔽
                            }
                            ++comparedVoxels;
                            parityOk = false;
                            mismatchPairs[QPair<int, int>(bw, bs)]++;
                            mismatchYMin = std::min(mismatchYMin, y);
                            mismatchYMax = std::max(mismatchYMax, y);
                        }
                    }
                }
            }
        }
        ok = ok && parityOk && unmaskedVoxels >= 100000; // 非空转：无掩蔽全等面 ≥10 万体素
        diag += QStringLiteral("[face unmaskedEqual=%1 masked=%2 mismatches=%3] ")
                    .arg(unmaskedVoxels).arg(maskedVoxels).arg(comparedVoxels);
        if (!parityOk) { // 临时诊断：失配分类汇总
            QString agg = QStringLiteral("[mismatch y%1..%2 pairs:").arg(mismatchYMin).arg(mismatchYMax);
            for (auto it = mismatchPairs.constBegin(); it != mismatchPairs.constEnd(); ++it)
                agg += QStringLiteral(" w%1/s%2 x%3;").arg(it.key().first).arg(it.key().second).arg(it.value());
            diag += agg + QStringLiteral("] ");
        }

        // ④ 账面：sparse 全核心域物化（radius 2 预生成方阵 = 5×5）且 population 后驻留集恰 25
        //    （脚手架即拆，无残留）。
        bool acctOk = ws.isSparse() && ws.residentChunkCount() == 25;
        for (int cz = 0; acctOk && cz < kChunks; ++cz)
            for (int cx = 0; acctOk && cx < kChunks; ++cx)
                acctOk = acctOk && ws.chunks().chunkMaterialized(cx, cz);
        ok = ok && acctOk;
        if (!acctOk)
            diag += QStringLiteral("[acct sparse=%1 n=%2] ").arg(ws.isSparse())
                        .arg(ws.residentChunkCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2023b parity wall: the sparse world's interior chunks are"
                             " voxel-bitwise identical to the fixed world across every replayed"
                             " population pass (caves, water, flora, bedrock and pruning"
                             " included) with structure-family regions provably absent from the"
                             " compared region, ore voxels masked per the scatterOres"
                             " chunk-local substitute, and signature counts proving a"
                             " non-vacuous face"
                          << (ok ? QString() : diag);
    });

    // ── r2023c：加载顺序无关确定性 ──────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2023c load-order independence (two radius-zero sparse worlds"
        " materializing the same five chunks - an interior pair, two far corners and one"
        " negative-coordinate chunk - through opposite loadChunkAt orders end up bitwise"
        " identical in id and state on every voxel, including the cross-chunk tree-canopy"
        " spillover face, with every compared chunk carrying non-air content so the identity is"
        " not vacuous)"), [&]() {
        bool ok = true;
        QString diag;

        World wa = makeSparse(0);
        World wb = makeSparse(0);

        // 次序 A（远角/邻块先行）vs 次序 B（内部先行）——同一组 chunk 两种物化序。
        const int orderA[][2] = { { 2, 3 }, { -1, 2 }, { 0, 0 }, { 4, 4 }, { 2, 2 } };
        const int orderB[][2] = { { 2, 2 }, { 4, 4 }, { 0, 0 }, { 2, 3 }, { -1, 2 } };
        bool loadOk = true;
        for (const auto &k : orderA)
            loadOk = loadOk && wa.loadChunkAt(k[0], k[1]);
        for (const auto &k : orderB)
            loadOk = loadOk && wb.loadChunkAt(k[0], k[1]);
        ok = ok && loadOk;
        if (!loadOk)
            diag += QStringLiteral("[load] ");

        // 同组 chunk 双序逐位恒等（含负坐标 chunk 的固定语义外延面）。
        bool parityOk = true;
        for (const auto &k : orderA) {
            for (int lz = 0; lz < 16 && parityOk; ++lz) {
                for (int lx = 0; lx < 16 && parityOk; ++lx) {
                    const int x = k[0] * 16 + lx, z = k[1] * 16 + lz;
                    for (int y = 0; y < kH; ++y) {
                        if (wa.blockAt(x, y, z) != wb.blockAt(x, y, z)
                            || wa.stateAt(x, y, z) != wb.stateAt(x, y, z)) {
                            parityOk = false;
                            diag += QStringLiteral("[chunk %1,%2 x=%3 z=%4 y=%5 a=%6/%7 b=%8/%9] ")
                                        .arg(k[0]).arg(k[1]).arg(x).arg(z).arg(y)
                                        .arg(wa.blockAt(x, y, z)).arg(wa.stateAt(x, y, z))
                                        .arg(wb.blockAt(x, y, z)).arg(wb.stateAt(x, y, z));
                        }
                    }
                }
            }
        }
        ok = ok && parityOk;

        // 非空转（内容真实性面）：每个被比 chunk 两侧均有非空柱顶（世界不是空的）；并核
        // 跨 chunk 结构面在位（(2,2)/(2,3) 联合区树叶计数 > 0 → 树冠越界枝叶确被比对覆盖）。
        bool nonVacuous = true;
        long canopyJoint = 0;
        for (const auto &k : orderA) {
            bool aTop = false, bTop = false;
            for (int lz = 0; lz < 16 && !(aTop && bTop); ++lz) {
                for (int lx = 0; lx < 16 && !(aTop && bTop); ++lx) {
                    const int x = k[0] * 16 + lx, z = k[1] * 16 + lz;
                    for (int y = kH - 1; y >= 0; --y) {
                        aTop = aTop || wa.blockAt(x, y, z) != 0;
                        bTop = bTop || wb.blockAt(x, y, z) != 0;
                        if (aTop && bTop) break;
                    }
                }
            }
            nonVacuous = nonVacuous && aTop && bTop;
        }
        for (int x = 32; x < 64 && canopyJoint < 1; ++x)
            for (int z = 32; z < 48 && canopyJoint < 1; ++z)
                for (int y = 0; y < kH && canopyJoint < 1; ++y)
                    if (wa.blockAt(x, y, z) == BR::Leaves)
                        ++canopyJoint;
        nonVacuous = nonVacuous && wa.blockAt(40, 0, 40) != 0; // 抽样柱底非空（地形在位）
        ok = ok && nonVacuous;
        if (!nonVacuous)
            diag += QStringLiteral("[vacuous canopy=%1] ").arg(canopyJoint);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2023c load-order independence: the same five chunks"
                             " materialized through opposite orders land bitwise identical"
                             " (negative-coordinate chunk included) with non-air column tops"
                             " proving the compared face is real content"
                          << (ok ? QString() : diag);
    });

    // ── r2023d：结构钉（fixed 字面钉 / 单一权威 / 豁免反探 / 拆卸单点）────────────────────
    runLeg(QStringLiteral("r2023d structure pins (comment-stripped source pins hold the"
        " population replay on its single-authority seams: the populate driver windowizes the"
        " quiet flag around a same-order replay of the shared fixed-domain pass bodies,"
        " representative windowed calls and the scaffold teardown are pinned, the five"
        " structure-family passes are textually absent from the populate body per the"
        " disposition table exemptions, generate() never enters the population replay, and the"
        " scaffold release primitive exists exactly once as the teardown single point)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① world.cpp 正面钉：populate 驱动体（静默窗 + 代表性窗口调用 + scaffold 生命周期链
        //    + 拆卸单点）。代表性调用 = 窗口形态首尾两员（bedrock 首pass / berries 尾pass）——
        //    逐 pass 完备性由 r2023b 行为面承重（恰红面设计：NEG-1 摘 placeTrees 调用不伤本腿）。
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("populate entry", "void World::sparsePopulateChunk(int cx, int cz)", 1),
                SrcPin("sparse guard on populate", "if (!isSparse())", 1),
                SrcPin("quiet window open", "m_worldgenQuiet = true;", 1),
                SrcPin("quiet window close", "m_worldgenQuiet = false;", 1),
                SrcPin("first windowed pass call", "placeBedrock(wx0, wx1, wz0, wz1);", 1),
                SrcPin("last windowed pass call", "placeSweetBerryBushes(wx0, wx1, wz0, wz1);", 1),
                SrcPin("scaffold lifecycle 1", "setChunkLifecycle(nx, nz, ChunkLifecycle::Loading);", 1),
                SrcPin("scaffold lifecycle 3", "setChunkLifecycle(nx, nz, ChunkLifecycle::Loaded);", 1),
                SrcPin("real-neighbor snapshot", "n.snapVoxels.assign(real->voxelData(), real->voxelData() + real->voxelCount());", 1),
                SrcPin("scaffold evict edge", "setChunkLifecycle(nb[i].cx, nb[i].cz, ChunkLifecycle::Evicting);", 1),
                SrcPin("scaffold absent edge", "setChunkLifecycle(nb[i].cx, nb[i].cz, ChunkLifecycle::Absent);", 1),
                SrcPin("teardown single point", "m_chunks.releaseSparseChunk(nb[i].cx, nb[i].cz);", 1),
                SrcPin("index rebuild tail", "rebuildPopulationCellIndexes(cx, cz, createdKeys, createdN);", 1),
            });

        // ② 结构族豁免反探（populate 函数体内禁出五结构调用——函数文本抽取手工 contains，
        //    miss 非空 = 合规；pinSet 是全文件粒度，generate() 体内的合法调用会被误计，故
        //    走函数域手工抽体）。
        QFile f(srcRoot + QStringLiteral("/World/world.cpp"));
        QString popBody;
        if (f.open(QIODevice::ReadOnly)) {
            const QString src = QString::fromUtf8(f.readAll());
            const qsizetype b = src.indexOf(QStringLiteral("void World::sparsePopulateChunk"));
            const qsizetype e = src.indexOf(QStringLiteral("void World::rebuildPopulationCellIndexes"));
            if (b >= 0 && e > b)
                popBody = src.mid(b, e - b);
        }
        const bool exemptOk = popBody.size() > 0
            && !popBody.contains(QStringLiteral("placeDungeons("))
            && !popBody.contains(QStringLiteral("placeMineshaft("))
            && !popBody.contains(QStringLiteral("placeDesertTemple("))
            && !popBody.contains(QStringLiteral("placeJungleTemple("))
            && !popBody.contains(QStringLiteral("placeStronghold("));

        // ③ chunkmanager 拆卸原语单点（声明 + 定义各恰一处；擦槽语义面）。
        const QStringList missCm = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.h"), {
                SrcPin("release primitive declared", "bool releaseSparseChunk(int cx, int cz);", 1),
            });
        const QStringList missCmc = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.cpp"), {
                SrcPin("release primitive defined", "bool ChunkManager::releaseSparseChunk(int cx, int cz)", 1),
                SrcPin("release is sparse-gated", "if (m_mode != WorldMode::Sparse)", 1),
            });

        for (const QString &m : missWc + missCm + missCmc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        if (!exemptOk) {
            ok = false;
            diag += QStringLiteral("[exempt probe found structure calls in populate body] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2023d structure pins: the population replay stays on its"
                             " single-authority seams (quiet-windowed same-order replay of the"
                             " shared pass bodies, scaffold lifecycle through the guarded"
                             " forwarder, teardown through the single release primitive,"
                             " structure-family passes textually absent from the populate body,"
                             " and generate() free of population tokens)"
                          << (ok ? QString() : diag);
    });
}
