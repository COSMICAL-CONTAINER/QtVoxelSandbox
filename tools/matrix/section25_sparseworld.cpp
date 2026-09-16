#include "matrix_helpers.h"

#include "backgroundgeneration.h" // 被测对照面：generateTerrainChunk（terraingen 单一权威的 chunk 视图）
#include "chunklifecycle.h"       // 被测面：六态类型（经 world.h 亦可达，显式 include 表明被测面）

// §29.5-W1 稀疏世界核探针段（4 腿；filter 词见腿名前缀；矩阵 624→628）。置尾先例沿用（接
// section24，runAll 末执行）。自建 fresh 小世界族（section11+ 先例：48×48×96 seed 82 四
// setter incantation + 本段新增 sparse 构造缝），rig 世界 w 零接触。任务契约
//（docs/refactor-plan-2026-09-08.md §29.5 W1 原文 + 29.5.1 第三条 OOB 等价语义）：
//   「fixed 默认 = 全库既有行为逐位不变」→ 承重墙腿（构造模式 / 六态稳态 / 驻留集全网格 /
//     越界门逐门同值 / 尺寸 setter 语义 / D4 参数归一化；worldgen/determin/store 家族全绿为
//     实证面）；
//   「未加载域 = OOB 等价 + 加载翻转」→ 语义族腿（sparse 未物化 chunk 逐门 = 现行越界分支
//     同值：blockAt=Air / setBlock 拒零副作用 / heightmap=-1 / supportTop=-1 / columnTop=-1 /
//     光=0 / chunkAtWorld=nullptr / 态=Absent——含负坐标域与 y 域两模式同构；loadChunkAt 后
//     答案变真值[内容无关面：列顶非空气 + heightmap 一致 + 顶上天光 15]；经生命周期 forwarder
//     ⑥ Evicting 后回到 OOB 答案、⑧ 取消驱逐恢复真值——查询门谓词的端到端实证）；
//   「同 seed 已加载区 ≡ terraingen 权威逐位恒等」→ 恒等承重腿（三面：双世界纯函数门逐位
//     恒等[heightAt/biomeIdAt 含负坐标] + sparse 已加载 chunk 对 generateTerrainChunk 权威
//     缓冲逐体素恒等[id+state] + 结构性内陆列 envelope 锚；fixed 世界含 bedrock/矿/洞/树等
//     后地形 pass，其与 terrain-only 稀疏块的全逐素恒等在物理上不成立——恒等面如实锚权威，
//     population 重放登记非目标）；
//   「结构钉」→ 钉面腿（Fixed 默认参正面钉 + worldstore 禁触反探 + terraingen 单列消费 +
//     生命周期经 setChunkLifecycle 唯一入口 + QML/P1-P3/D6 组件族禁出）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一 = 摘「未加载=OOB 等价」查询门谓词的**可查询合取**（chunkMaterialized 的 && → ||，
//     存在即可查询）→ 恰红语义族腿（Evicting 相位查询返回真值 = 被摘语义本体）；承重墙腿
//     （fixed 路径）/ 恒等腿（已加载域读不受谓词影响）/ 钉面腿（needle 文本在位）不误伤。
//   变异二 = 摘「同 seed 生成路径一致性」（sparseGenerateChunk 列坐标 +16 偏置 = 生成源
//     chunk 平移）→ 恰红恒等腿（权威缓冲逐体素比对 + 纯函数门恒等全红 = 被摘语义本体）；
//     语义族腿断言内容无关真值面（非空气/heightmap 一致/光 15）平移后仍成立不误伤。
// 阴性轮日志：build/ 下四件（neg1/neg2 × red/restore）直接落终名（证据面铁律）。
void MatrixRun::section25_sparseworld()
{
    constexpr int kW = 48, kD = 48, kH = 96, kSeed = 82, kRadius = 2;
    const TerrainGen::Dims kDims{ kW, kD, kH }; // 与固定世界同 dims——同 seed 恒等的生成语义前提
    constexpr int kCenterChunk = (kW / 2) / 16; // 中心 chunk（1）：预生成方阵中心

    // sparse 构造缝（核心域 = 固定世界同 dims；半径 2 → chunk cx,cz ∈ [-1,3] 5×5=25 方阵，
    // 负坐标 chunk 天然在阵——无界域必测面）。
    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kW;
        sp.coreDepth = kD;
        sp.height = kH;
        sp.spawnPreGenerateRadius = kRadius;
        return World(sp);
    };
    // 固定世界四 setter incantation（section11+ 全矩阵 proven——fixed 构造路径零改动的活证据）。
    const auto initFixed = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
    };

    // ── 承重墙：fixed 默认模式全语义与现行逐位恒等 ──────────────────────────────────────
    runLeg(QStringLiteral("r2022a fixed-default zero-change load-bearing wall (mode Fixed by default:"
        " the proven four-setter incantation keeps its full current semantics - full dense grid"
        " all-Loaded lifecycle steady state, resident set enumerates the whole chunk grid in"
        " cz-major order with revision 0 and zero lifecycle edges across queries and edits,"
        " every out-of-domain gate answers exactly the legacy values (block air / setBlock"
        " reject with zero worldChanged emission / heightmap -1 / support -1 / column top -1 /"
        " chunk pointer null / light 0 with open sky above the world top), a bare ChunkManager"
        " reports Fixed mode with the unified predicates constantly true, and the spawn"
        " pre-generate radius normalizes negatives to zero and clamps the upper domain"), [&]() {
        bool ok = true;
        QString diag;

        World wf;
        initFixed(wf);

        // ① 模式 / 尺寸 / 稠密网格稳态：
        const bool modeOk = !wf.isSparse() && wf.width() == kW && wf.depth() == kD
            && wf.height() == kH && wf.chunksX() == 3 && wf.chunksZ() == 3;
        ok = ok && modeOk;
        if (!modeOk)
            diag += QStringLiteral("[mode sparse=%1 w=%2 cx=%3] ").arg(wf.isSparse())
                        .arg(wf.width()).arg(wf.chunksX());

        bool steadyOk = true;
        for (int cz = 0; steadyOk && cz < 3; ++cz)
            for (int cx = 0; steadyOk && cx < 3; ++cx)
                steadyOk = steadyOk
                    && wf.chunks().lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && steadyOk;
        if (!steadyOk)
            diag += QStringLiteral("[steady life=%1] ").arg(int(wf.chunks().lifecycleAt(1, 1)));

        // ② 驻留集 = 全网格 cz 主序 + revision 0 + 查询/编辑零生命周期沿（信号计数）：
        int revisionEdges = 0, worldEdges = 0;
        QObject::connect(&wf, &World::residentChunkRevisionChanged,
                         [&revisionEdges]() { ++revisionEdges; });
        QObject::connect(&wf, &World::worldChanged, [&worldEdges]() { ++worldEdges; });
        bool residentOk = wf.residentChunkCount() == 9 && wf.residentChunkRevision() == 0;
        static const QPair<int, int> kExpected[9] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 },
            { 1, 1 }, { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 } };
        for (int i = 0; residentOk && i < 9; ++i) {
            const QVariantList k = wf.residentChunkKeyAt(i);
            residentOk = residentOk && k.size() == 2 && k.at(0).toInt() == kExpected[i].first
                && k.at(1).toInt() == kExpected[i].second;
        }
        residentOk = residentOk && wf.residentChunkKeyAt(-1).isEmpty()
            && wf.residentChunkKeyAt(9).isEmpty(); // 越界空表
        ok = ok && residentOk;
        if (!residentOk)
            diag += QStringLiteral("[resident n=%1 rev=%2] ").arg(wf.residentChunkCount())
                        .arg(wf.residentChunkRevision());

        // ③ 越界门逐门 = 现行同值；越界 setBlock 拒且零 worldChanged 发射：
        bool oobOk = wf.blockAt(-1, 0, 0) == 0 && wf.blockAt(48, 0, 0) == 0
            && wf.blockAt(0, kH, 0) == 0 && wf.blockAt(0, -1, 0) == 0
            && wf.stateAt(-1, 0, 0) == 0
            && wf.heightmapAt(-1, 16) == -1 && wf.heightmapAt(48, 16) == -1
            && wf.supportTopYAt(-1, 5, 0) == -1.0f
            && wf.columnTopSurfaceY(-1, 0) == -1.0f
            && wf.chunks().chunkAtWorld(-1, 0) == nullptr
            && wf.chunks().chunk(-1, 0) == nullptr
            && wf.chunks().lifecycleAt(-1, 0) == ChunkLifecycle::Absent
            && wf.skyLightAt(0, kH, 0) == 15 // 世界顶之上 = 开阔天空（顶面采样语义）
            && wf.skyLightAt(-1, 0, 0) == 0 && wf.blockLightAt(-1, 0, 0) == 0
            && !wf.setBlock(-1, 5, 0, BR::Stone) && !wf.setBlock(48, 5, 0, BR::Stone)
            && !wf.setBlock(0, kH, 0, BR::Stone) && worldEdges == 0;
        ok = ok && oobOk;
        if (!oobOk)
            diag += QStringLiteral("[oob b=%1 hm=%2 sup=%3 ct=%4 sky5=%5 rej=%6 we=%7] ")
                        .arg(wf.blockAt(-1, 0, 0) == 0)
                        .arg(wf.heightmapAt(-1, 16) == -1)
                        .arg(wf.supportTopYAt(-1, 5, 0) == -1.0f)
                        .arg(wf.columnTopSurfaceY(-1, 0) == -1.0f)
                        .arg(wf.skyLightAt(0, kH, 0) == 15)
                        .arg(!wf.setBlock(-1, 5, 0, BR::Stone))
                        .arg(worldEdges);

        // ④ 界内编辑照常（真值 + worldChanged 发射）而生命周期沿恒零：
        const bool editOk = wf.setBlock(24, 90, 24, BR::Stone) && worldEdges == 1
            && wf.setBlock(24, 90, 24, BR::Air) && worldEdges == 2
            && revisionEdges == 0 && wf.residentChunkRevision() == 0;
        ok = ok && editOk;
        if (!editOk)
            diag += QStringLiteral("[edit we=%1 re=%2] ").arg(worldEdges).arg(revisionEdges);

        // ⑤ 裸 ChunkManager：Fixed 模式 + 统一谓词恒真（固定稠密网格 chunk 恒在恒驻留）：
        ChunkManager bare(kW, kD, kH);
        const bool bareOk = bare.mode() == WorldMode::Fixed && bare.chunkMaterialized(1, 1)
            && bare.chunkContentPresent(1, 1) && bare.ensureChunk(1, 1) != nullptr;
        ok = ok && bareOk;
        if (!bareOk)
            diag += QStringLiteral("[bare] ");

        // ⑥ D4 参数归一化（负值归 0 / 上界钳 64 / 合法值原样）：
        const bool normOk = World::normalizedSpawnPreGenerateRadius(-1) == 0
            && World::normalizedSpawnPreGenerateRadius(0) == 0
            && World::normalizedSpawnPreGenerateRadius(kRadius) == kRadius
            && World::normalizedSpawnPreGenerateRadius(64) == 64
            && World::normalizedSpawnPreGenerateRadius(65) == 64;
        ok = ok && normOk;
        if (!normOk)
            diag += QStringLiteral("[norm] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2022a fixed-default wall: the four-setter fixed world keeps"
                             " its full current semantics (dense grid all-Loaded, whole-grid"
                             " resident enumeration with revision 0 and zero lifecycle edges,"
                             " legacy out-of-domain answers on every gate with zero-emission"
                             " rejections, in-domain edits unchanged, bare manager Fixed with"
                             " constantly-true unified predicates, radius normalization sane)"
                          << (ok ? QString() : diag);
    });

    // ── 语义族：未加载域 = OOB 等价 + 加载翻转 + 驱逐回环 ────────────────────────────────
    runLeg(QStringLiteral("r2022b unloaded-domain OOB equivalence + load flip + evict round-trip (a"
        " sparse world constructs with zero full generation: a 5x5 chunk square around the core"
        " center is materialized to the Loaded steady state through the guarded lifecycle"
        " chain while everything else is absent; every gate on an unloaded chunk - positive"
        " beyond-radius and negative beyond-radius alike - answers exactly the legacy"
        " out-of-domain value, identical value-for-value to the fixed world's answers at its"
        " own out-of-domain coordinates, with y-domain semantics shared (open sky above the"
        " top, reject below zero) and rejected writes costing zero side effects (no"
        " worldChanged, no signal); loadChunkAt flips the same coordinates to truth"
        " (content-agnostic: non-air column top, heightmap agreement, sky light 15 above the"
        " top, real chunk pointer, resident bookkeeping +1 with cz-major ordering); routing"
        " the loaded chunk to Evicting through the lifecycle forwarder returns every gate to"
        " the OOB answers and rejects edits, and cancel-evict restores truth)"), [&]() {
        bool ok = true;
        QString diag;

        World ws = makeSparse();
        int worldEdges = 0;
        QObject::connect(&ws, &World::worldChanged, [&worldEdges]() { ++worldEdges; });

        // ① 构造账面：isSparse + 预生成方阵全 Loaded + revision 随 ③ 沿增长：
        const int wantResident = (2 * kRadius + 1) * (2 * kRadius + 1);
        bool ctorOk = ws.isSparse() && ws.spawnPreGenerateRadius() == kRadius
            && ws.chunksX() == 3 && ws.width() == kW // 核心域尺寸（生成语义参考）如实保留
            && ws.residentChunkCount() == wantResident && ws.residentChunkRevision() > 0
            && ws.chunks().lifecycleAt(kCenterChunk, kCenterChunk) == ChunkLifecycle::Loaded
            && ws.chunks().lifecycleAt(-1, -1) == ChunkLifecycle::Loaded;
        for (int cz = kCenterChunk - kRadius; ctorOk && cz <= kCenterChunk + kRadius; ++cz)
            for (int cx = kCenterChunk - kRadius; cx <= kCenterChunk + kRadius; ++cx)
                ctorOk = ctorOk && ws.chunks().chunkMaterialized(cx, cz);
        ok = ok && ctorOk;
        if (!ctorOk)
            diag += QStringLiteral("[ctor sparse=%1 n=%2 life11=%3 life-1-1=%4] ")
                        .arg(ws.isSparse()).arg(ws.residentChunkCount())
                        .arg(int(ws.chunks().lifecycleAt(kCenterChunk, kCenterChunk)))
                        .arg(int(ws.chunks().lifecycleAt(-1, -1)));

        // ② 未加载域逐门 = 现行越界同值（正出界 (5,1) 与负出界 (-3,3) 双 probe；与固定世界
        //    自身越界答案逐门同值——「等价」的字面）：
        World wf;
        initFixed(wf);
        const int ux = 5 * 16, uz = 1 * 16; // (5,1) chunk 原点（x∈[80,96) 半径外）
        const int nx = -3 * 16, nz = 3 * 16; // (-3,3) chunk 原点（负出界）
        bool absentOk = ws.blockAt(ux, 5, uz) == 0 && ws.blockAt(nx, 5, nz) == 0
            && ws.stateAt(ux, 5, uz) == 0
            && ws.heightmapAt(ux, uz) == -1 && ws.heightmapAt(nx, nz) == -1
            && ws.supportTopYAt(ux, 5, uz) == -1.0f
            && ws.columnTopSurfaceY(ux, uz) == -1.0f
            && ws.skyLightAt(ux, 5, uz) == 0 && ws.blockLightAt(ux, 5, uz) == 0
            && ws.chunks().chunkAtWorld(ux, uz) == nullptr
            && ws.chunks().chunk(5, 1) == nullptr
            && ws.chunks().lifecycleAt(5, 1) == ChunkLifecycle::Absent
            // 与 fixed 越界逐门同值（OOB 等价的字面对账）：
            && ws.blockAt(ux, 5, uz) == wf.blockAt(-1, 5, 0)
            && ws.heightmapAt(ux, uz) == wf.heightmapAt(-1, 16)
            && ws.supportTopYAt(ux, 5, uz) == wf.supportTopYAt(-1, 5, 0)
            && ws.columnTopSurfaceY(ux, uz) == wf.columnTopSurfaceY(-1, 0)
            && (ws.chunks().chunkAtWorld(ux, uz) == nullptr) == (wf.chunks().chunkAtWorld(-1, 0) == nullptr)
            // y 域两模式同构（顶上开阔天 / 底下无光；越界写拒）：
            && ws.skyLightAt(ux, kH, uz) == 15 && wf.skyLightAt(-1, kH, 0) == 15
            && ws.blockAt(ux, kH, uz) == 0 && ws.blockAt(ux, -1, uz) == 0
            // 拒写零副作用（返回 false + 零 worldChanged 零信号面）：
            && !ws.setBlock(ux, 5, uz, BR::Stone) && !ws.setBlock(ux, 5, uz, BR::Stone, 0)
            && !ws.setBlock(ux, kH, uz, BR::Stone) && worldEdges == 0
            // 驻留账面不动 + keyAt 越界空表：
            && ws.residentChunkCount() == wantResident
            && ws.residentChunkKeyAt(wantResident).isEmpty();
        ok = ok && absentOk;
        if (!absentOk)
            diag += QStringLiteral("[absent b=%1 hm=%2 sup=%3 rej=%4 we=%5 n=%6] ")
                        .arg(ws.blockAt(ux, 5, uz) == 0)
                        .arg(ws.heightmapAt(ux, uz) == -1)
                        .arg(ws.supportTopYAt(ux, 5, uz) == -1.0f)
                        .arg(!ws.setBlock(ux, 5, uz, BR::Stone))
                        .arg(worldEdges).arg(ws.residentChunkCount());

        // ③ 加载翻转：loadChunkAt → ①②③ 链到 Loaded；答案变真值（内容无关面）+ 账面 +1：
        const bool flipped = ws.loadChunkAt(5, 1);
        int top = -1;
        for (int y = kH - 1; top < 0 && y >= 0; --y)
            if (ws.blockAt(ux, y, uz) != 0) top = y;
        int scanned = -1;
        for (int y = kH - 1; scanned < 0 && y >= 0; --y)
            if (ws.blockAt(ux, y, uz) != 0) scanned = y; // 同列复扫（heightmap 对账基准）
        const bool flipOk = flipped
            && ws.chunks().lifecycleAt(5, 1) == ChunkLifecycle::Loaded
            && ws.chunks().chunkMaterialized(5, 1)
            && ws.residentChunkCount() == wantResident + 1
            && top > 0 && top == scanned
            && ws.heightmapAt(ux, uz) == top
            && ws.blockAt(ux, top, uz) != 0
            && ws.supportTopYAt(ux, top, uz) > 0.0f
            && ws.columnTopSurfaceY(ux, uz) > 0.0f
            && ws.chunks().chunkAtWorld(ux, uz) != nullptr
            && ws.skyLightAt(ux, top + 1, uz) == 15; // 列种子天光（真值面）
        ok = ok && flipOk;
        if (!flipOk)
            diag += QStringLiteral("[flip ok=%1 life=%2 n=%3 top=%4 hm=%5 sky=%6] ")
                        .arg(flipped).arg(int(ws.chunks().lifecycleAt(5, 1)))
                        .arg(ws.residentChunkCount()).arg(top)
                        .arg(ws.heightmapAt(ux, uz))
                        .arg(top >= 0 ? ws.skyLightAt(ux, top + 1, uz) : -1);

        // ④ 驻留枚举随加载跟随（cz 主序插入：cz=1 组内 cx=5 排 (3,1) 之后 → index 15）：
        const QVariantList key15 = ws.residentChunkKeyAt(15);
        const bool enumOk = key15.size() == 2 && key15.at(0).toInt() == 5
            && key15.at(1).toInt() == 1;
        ok = ok && enumOk;
        if (!enumOk)
            diag += QStringLiteral("[enum15 %1,%2] ")
                        .arg(key15.value(0).toInt()).arg(key15.value(1).toInt());

        // ⑤ 驱逐回环（生命周期 forwarder；无专用卸载缝——⑥/⑧ 合法边即测试缝）：
        const bool evictRoute = ws.setChunkLifecycle(5, 1, ChunkLifecycle::Evicting);
        const int topBeforeEvict = top;
        const bool evictedOk = evictRoute
            && ws.residentChunkCount() == wantResident
            && ws.blockAt(ux, topBeforeEvict, uz) == 0 // 查询门回 OOB 答案
            && ws.heightmapAt(ux, uz) == -1
            && ws.chunks().chunkAtWorld(ux, uz) == nullptr
            && !ws.setBlock(ux, topBeforeEvict, uz, BR::Air); // Evicting 拒写
        const bool restoreRoute = ws.setChunkLifecycle(5, 1, ChunkLifecycle::Loaded);
        const bool restoredOk = restoreRoute
            && ws.residentChunkCount() == wantResident + 1
            && ws.blockAt(ux, topBeforeEvict, uz) != 0 // 真值恢复
            && ws.heightmapAt(ux, uz) == topBeforeEvict;
        ok = ok && evictedOk && restoredOk;
        if (!evictedOk || !restoredOk)
            diag += QStringLiteral("[evict r=%1 b=%2 n=%3 | restore r=%4 b=%5 hm=%6] ")
                        .arg(evictRoute)
                        .arg(ws.blockAt(ux, topBeforeEvict, uz) == 0)
                        .arg(ws.residentChunkCount())
                        .arg(restoreRoute)
                        .arg(ws.blockAt(ux, topBeforeEvict, uz) != 0)
                        .arg(ws.heightmapAt(ux, uz) == topBeforeEvict);

        // ⑥ 界内已加载域编辑照常（放置 + 撤销，worldChanged 恰 2 次发射）：
        const int editY = top + 3;
        const bool editOk = editY < kH && ws.setBlock(ux, editY, uz, BR::Stone)
            && worldEdges == 1 && ws.setBlock(ux, editY, uz, BR::Air) && worldEdges == 2;
        ok = ok && editOk;
        if (!editOk)
            diag += QStringLiteral("[edit y=%1 we=%2] ").arg(editY).arg(worldEdges);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2022b sparse semantics: zero-generation construction"
                             " materializes exactly the spawn square (all Loaded) while every"
                             " gate on unloaded ground (positive and negative beyond-radius)"
                             " answers the legacy out-of-domain value identical to the fixed"
                             " world's, loadChunkAt flips the coordinates to truth with"
                             " resident bookkeeping and cz-major enumeration, the lifecycle"
                             " forwarder's evict edge returns every gate to OOB answers (edits"
                             " rejected) and cancel-evict restores truth, and in-domain edits"
                             " flow" << (ok ? QString() : diag);
    });

    // ── 恒等承重：同 seed 双世界纯函数门 + sparse 已加载区 ≡ terraingen 权威缓冲 ─────────
    runLeg(QStringLiteral("r2022c same-seed region identity against the single terrain authority (a"
        " fixed world and a sparse world sharing seed and generation dims agree bitwise on"
        " both pure gates - heightAt and biomeIdAt - across the loaded region AND negative"
        " coordinates, both worlds matching a direct TerrainGen instance; every materialized"
        " sparse chunk including negative-coordinate ones is voxel-bitwise identical [id and"
        " state] to the authority chunk buffer produced by the shared column-fill function;"
        " and the structurally inland center chunk anchors its column envelope to the same"
        " pure heightAt with biome-consistent surface ids)"), [&]() {
        bool ok = true;
        QString diag;

        World wf;
        initFixed(wf);
        World ws = makeSparse();
        const TerrainGen tg(kSeed, kDims); // 权威直连实例（同 seed 同 dims 双生）

        // ① 双世界纯函数门逐位恒等（含负坐标——无界域必测面；权威直连三面对账）：
        static const int kCols[] = { -40, -17, -1, 0, 7, 16, 24, 33, 47 };
        bool pureOk = true;
        int pureCols = 0;
        for (int x : kCols) {
            for (int z : kCols) {
                const int hw = wf.heightAt(x, z);
                const int hs = ws.heightAt(x, z);
                const int ht = tg.heightAt(x, z);
                const int bw = wf.biomeIdAt(x, z);
                const int bs = ws.biomeIdAt(x, z);
                const int bt = int(tg.biomeComputeAt(x, z));
                if (!(hw == hs && hs == ht && bw == bs && bs == bt)) {
                    pureOk = false;
                    diag += QStringLiteral("[pure x=%1 z=%2 hw=%3 hs=%4 ht=%5 bw=%6 bs=%7 bt=%8] ")
                                .arg(x).arg(z).arg(hw).arg(hs).arg(ht).arg(bw).arg(bs).arg(bt);
                }
                ++pureCols;
            }
        }
        pureOk = pureOk && pureCols == 81;
        ok = ok && pureOk;

        // ② sparse 已加载 chunk 对权威缓冲逐体素恒等（id + state；含负坐标 chunk）：
        const ChunkKey kKeys[] = { ChunkKey{ 0, 0 }, ChunkKey{ 2, 1 }, ChunkKey{ 3, 3 },
            ChunkKey{ -1, -1 }, ChunkKey{ -1, 3 } };
        bool bufOk = true;
        for (const ChunkKey &k : kKeys) {
            const std::unique_ptr<GeneratedChunkData> buf = generateTerrainChunk(tg, k);
            if (!buf || !buf->valid()) {
                bufOk = false;
                diag += QStringLiteral("[buf %1,%2 invalid] ").arg(k.cx).arg(k.cz);
                break;
            }
            for (int lz = 0; bufOk && lz < 16; ++lz) {
                for (int lx = 0; bufOk && lx < 16; ++lx) {
                    const int wx = k.cx * 16 + lx, wz = k.cz * 16 + lz;
                    for (int y = 0; y < kH; ++y) {
                        if (ws.blockAt(wx, y, wz) != buf->blockAt(lx, y, lz)
                            || ws.stateAt(wx, y, wz) != buf->stateAt(lx, y, lz)) {
                            bufOk = false;
                            diag += QStringLiteral("[voxel %1,%2 lx=%3 lz=%4 y=%5 w=%6/%7 a=%8/%9] ")
                                        .arg(k.cx).arg(k.cz).arg(lx).arg(lz).arg(y)
                                        .arg(ws.blockAt(wx, y, wz)).arg(ws.stateAt(wx, y, wz))
                                        .arg(buf->blockAt(lx, y, lz)).arg(buf->stateAt(lx, y, lz));
                        }
                    }
                }
            }
        }
        ok = ok && bufOk;

        // ③ 结构性内陆中块 envelope 锚（列顶 = min(heightAt, H-1)；表层 id 按群系——同
        //    r2012b 锚形态transpose 到 sparse 查询面）：
        const int ox = 1 * 16, oz = 1 * 16;
        bool envOk = true;
        int envCols = 0;
        for (int lz = 0; envOk && lz < 16; ++lz) {
            for (int lx = 0; envOk && lx < 16; ++lx) {
                const int wx = ox + lx, wz = oz + lz;
                int top = -1;
                for (int y = kH - 1; y >= 0; --y)
                    if (ws.blockAt(wx, y, wz) != 0) { top = y; break; }
                const int expectTop = std::min(ws.heightAt(wx, wz), kH - 1);
                const int biome = ws.biomeIdAt(wx, wz);
                const quint8 topId = top >= 0 ? ws.blockAt(wx, top, wz) : quint8(0);
                bool colOk = top == expectTop;
                if (colOk && biome == 2) // Desert → 沙
                    colOk = topId == BR::Sand;
                else if (colOk && biome == 4) // Snowy → 积雪层
                    colOk = topId == BR::SnowLayer && ws.stateAt(wx, top, wz) <= 2;
                else if (colOk) // 其余内陆群系 → 草
                    colOk = topId == BR::Grass;
                if (!colOk) {
                    envOk = false;
                    diag += QStringLiteral("[env lx=%1 lz=%2 top=%3 want=%4 bio=%5 id=%6] ")
                                .arg(lx).arg(lz).arg(top).arg(expectTop).arg(biome).arg(topId);
                }
                ++envCols;
            }
        }
        envOk = envOk && envCols == 256;
        ok = ok && envOk;

        // ④ 账面：核心域全物化 + 预生成方阵恰 25（恒等面非空转的域前提）：
        bool domainOk = ws.residentChunkCount() == 25;
        for (int cz = 0; domainOk && cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                domainOk = domainOk && ws.chunks().chunkMaterialized(cx, cz);
        if (!domainOk)
            diag += QStringLiteral("[domain n=%1] ").arg(ws.residentChunkCount());
        ok = ok && domainOk;

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2022c same-seed identity: fixed and sparse worlds agree"
                             " bitwise on heightAt/biomeIdAt across loaded and negative"
                             " coordinates (three-way with a direct TerrainGen), materialized"
                             " sparse chunks (negative ones included) match the authority chunk"
                             " buffers voxel-bitwise in id and state, and the inland center"
                             " chunk anchors its envelope to the same pure heightAt"
                          << (ok ? QString() : diag);
    });

    // ── 钉面：Fixed 默认参 / 禁触反探 / 单列权威 / 生命周期唯一入口 / QML 组件族禁出 ────
    runLeg(QStringLiteral("r2022d structure pins (comment-stripped source pins hold the sparse core on"
        " its single-authority seams: the WorldMode enum defaults to Fixed with the sparse"
        " constructor and params defaulting the spawn radius to 2, the unified"
        " existence predicates live only in the chunk manager, the sparse storage is keyed"
        " with materialization through the legacy Chunk construction path, the world side"
        " drives lifecycle exclusively through the guarded forwarder entry and fills columns"
        " only through the single terrain authority, the worldstore pair stays textually"
        " untouched by the sparse core, Main.qml and the streaming component family carry no"
        " sparse token, and the new C++-only face never enters QML)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① chunkmanager.h：模式枚举 + Fixed 默认 + 统一谓词声明 + 键控稀疏存储：
        const QStringList missCmh = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.h"), {
                SrcPin("mode enum authority", "enum class WorldMode : quint8", 1),
                SrcPin("fixed default mode member", "WorldMode m_mode = WorldMode::Fixed", 1),
                SrcPin("query predicate declared", "bool chunkMaterialized(int cx, int cz) const;", 1),
                SrcPin("write predicate declared", "bool chunkContentPresent(int cx, int cz) const;", 1),
                SrcPin("sparse init declared", "void reinitializeSparse(int coreWidth, int coreDepth, int height);", 1),
                SrcPin("materialize seam declared", "Chunk *ensureChunk(int cx, int cz);", 1),
                SrcPin("keyed sparse storage", "std::unordered_map<quint64, SparseSlot> m_sparse;", 1),
            });
        // ② chunkmanager.cpp：门分流 + 谓词单点 + 现行 Chunk 构造路径复用 + 键逆映射权威：
        const QStringList missCmc = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.cpp"), {
                SrcPin("sparse gate branches", "if (m_mode == WorldMode::Sparse)", 12),
                SrcPin("resident enumeration consults the queryable gate", "chunkLifecycleQueryable(kv.second.life)", 1),
                SrcPin("legacy chunk construction reused", "std::make_unique<Chunk>(cx * kSize, cz * kSize, m_height)", 2),
                SrcPin("packed key inverse via authority", "ChunkKey::fromPacked(", 1),
                SrcPin("zero allocation initialization", "m_sparse.clear()", 1),
            });
        // ③ world.h：sparse 构造面 + 默认参 + C++-only（禁 Q_INVOKABLE 形态）：
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("sparse params aggregate", "struct SparseWorldParams", 1),
                SrcPin("spawn radius default two", "int spawnPreGenerateRadius = 2;", 1),
                SrcPin("sparse constructor", "explicit World(const SparseWorldParams &sp, QObject *parent = nullptr);", 1),
                SrcPin("on-demand materialization seam", "bool loadChunkAt(int cx, int cz);", 1),
                SrcPin("mode read face", "bool isSparse() const", 1),
            });
        // ④ world.cpp：生成链 + 生命周期唯一入口 + 单列权威 + 写门 + 守卫族：
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("sparse generation entry", "void World::sparseGenerate()", 1),
                SrcPin("per-chunk chain", "void World::sparseGenerateChunk(int cx, int cz)", 1),
                SrcPin("lifecycle edge 1 via guarded entry", "setChunkLifecycle(cx, cz, ChunkLifecycle::Loading)", 1),
                SrcPin("lifecycle edge 2 via guarded entry", "setChunkLifecycle(cx, cz, ChunkLifecycle::Generated)", 1),
                SrcPin("lifecycle edge 3 via guarded entry", "setChunkLifecycle(cx, cz, ChunkLifecycle::Loaded)", 1),
                SrcPin("terrain authority consumption (fixed + sparse)", "m_terrain.fillTerrainColumn(", 2),
                SrcPin("world write gates consult the unified predicate", "m_chunks.chunkContentPresent(", 2),
                SrcPin("sparse guards on fixed-only entries", "if (isSparse())", 7),
                SrcPin("zero-allocation sparse init", "m_chunks.reinitializeSparse(", 1),
                SrcPin("resident enumeration delegates sparse ordering", "m_chunks.sparseResidentKeysOrdered()", 1),
            });
        for (const QString &m : missCmh + missCmc + missWh + missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 禁触反探（miss 非空 = 合规缺席）：worldstore 对 sparse 核全盲 + QML/组件族零记号：
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool storeBlind
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "SparseWorldParams")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "loadChunkAt")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "reinitializeSparse")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "loadChunkAt");
        const bool qmlBlind
            = forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "SparseWorldParams")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "loadChunkAt")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "isSparse");
        const bool componentBlind
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkstreamdriver.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkevictor.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/meshworker.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/backgroundgeneration.h"), "WorldMode")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "GenerationPolicy")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "ChunkStreamDriver")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "ChunkEvictor");
        const bool cppOnlyFace
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "Q_INVOKABLE bool isSparse")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "Q_INVOKABLE bool loadChunkAt")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkmanager.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkmanager.h"), "Q_PROPERTY");
        const bool negOk = storeBlind && qmlBlind && componentBlind && cppOnlyFace;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg store=%1 qml=%2 comp=%3 cppOnly=%4] ")
                        .arg(storeBlind).arg(qmlBlind).arg(componentBlind).arg(cppOnlyFace);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2022d structure pins: the sparse core lives on its"
                             " single-authority seams (Fixed-default mode enum, unified"
                             " predicates only in the chunk manager, keyed storage"
                             " materializing through the legacy Chunk construction, guarded"
                             " lifecycle entry, single terrain authority consumption, unified"
                             " write gates, sparse guards on fixed-only entries) while"
                             " worldstore, Main.qml, the streaming component family and the"
                             " QML face carry zero sparse tokens" << (ok ? QString() : diag);
    });
}
