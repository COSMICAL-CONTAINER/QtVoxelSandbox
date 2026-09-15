#include "matrix_helpers.h"

#include "meshbuilder.h" // R20.13 被测：MeshBuilder（网格算法单一权威）+ ChunkMeshSnapshot/ChunkMeshData
#include "terraingen.h"  // r2013d worldgen 锚（TerrainGen 纯函数对照——零漂移抽核）

#include <QDir>
#include <QVector3D>

// R20.13 MeshBuilder 探针段（4 腿 r2013a-d；filter 词 "r2013"；矩阵 582→586，band 584±2 内）。
// 置尾先例沿用（接 section16，runAll 末执行）。r2013a-c 自建 fresh 小世界 48×48×96 seed 82
//（section11+ 先例 incantation × 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)——零 RNG
// 进窗）+ 流体/半透 rig 四件（Glass/Ice/Water/Lava 各一，散布 chunk(1,1) 内部 20..29 带）让六段
// 比对非平凡；r2013d 零世界（纯源码钉 + 裸空快照）。rig 世界 w 零接触。
// 任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.13 原文五验收）：
//   「旧 QtQuick3DAdapter 可以消费新结果」+「网格视觉和顶点统计与旧路径一致」→ r2013a 承重墙：
//     9 段路由变体（terrain/water/lava/glass/ice/cutoutOnly/cutout 恢复态/greedy/AO）× 2 chunk
//     （内陆 (1,1) + 世界边 (0,0)——边界剔除对 void），旧路径（ChunkGeometry 直建 →
//     vertexData/indexData 读回）与新路径（captureChunkMeshSnapshot → MeshBuilder::build）逐字节
//     比对（顶点 48B/Vtx 逐位 + 索引逐位 + 顶点/三角计数），外加已知小场景精确顶点差
//     （自由柱放 Stone = 5 可见面 × 4v = Δ20v/Δ30i，双路径同断言）；
//   「输入是 snapshot」→ r2013b：快照自持性（采集后底层世界改动——破表顶 + 放 Stone——已采快照
//     再构建输出逐位不变）+ 拷贝独立（copy-construct 后 identical + 输出恒等）+ 重采集非空转
//     （世界已改 → 输出必变）+ accessor OOB 语义与 World 逐位同面（y≥H 天光 15 / 域外空气 /
//     列顶 -1）；
//   「输出是 owning ChunkMeshData」（统计自持半边）+ 计数穿透 → r2013c：FrameProfiler meshN 族
//     经 MeshBuilder::build 照走（Dirty/Sun/Water 各 +1、meshN +3，差分断言）+ ChunkGeometry
//     消费面回归（r2008c 同款：in-chunk 编辑顶点即时增长 ×2 + out-of-chunk 编辑跳过且零计数）；
//   「MeshBuilder 不依赖 QQuick3D」+ 搬移单一权威 → r2013d：pinSet 结构钉（QObjectFree ×2/
//     搬移件在 meshbuilder.cpp 落位/chunkgeometry 委托面 + r2008c 三门常驻）+ 反探（meshbuilder
//     禁出 QQuick3D；chunkgeometry 禁回流 kFaces/FaceDef/occludesNeighborFace/MaskEntry/
//     stripFrames/railProbeDelta）+ 空快照空输出（!haveChunk 旧路径等价）+ worldgen 零漂移锚
//     （heightAt/biomeIdAt vs TerrainGen 纯函数——完整恒等族由 worldgen/determin/re-gen 常驻腿承担）。
// 钉纪律（R20.11 恰红面设计 + R20.08 反探惯例延续）：禁出钉走 minCount=1 反探（miss 非空=合规
//   缺席，pinSet 剥注释器防注释误伤）；行为腿只钉相对恒等（旧路径 ≡ 新路径）与确定性差分。
// 阴性轮（matrix_r2013_neg.log 存证 + Edit 反向还原）：NEG1 摘 AO 因子（meshbuilder.cpp aoCorner
//   返 1.0）→ t1023b（既有 AO 顶点腿）+ r2013a（AO 变体逐位比对）恰红、非 AO 变体不误伤；
//   NEG2 顶点序翻转（MeshBuilder::build 返回前 std::reverse 顶点缓冲）→ r2013a 全变体恰红
//   （计数不变的纯位序扰动——专验逐字节比对非计数比对）。各 Edit 反向还原 → filter r2013 回绿。
// 防 flake：纯同步零线程（快照采集 + build 全在调用线程；不触 t1023c 线程原语白名单）；
//   全部断言为确定性终态（同 seed 同坐标 worldgen 纯函数 + 同步编辑 + 字节比对），无时序面。
void MatrixRun::section17_meshbuilder()
{
    constexpr int kW = 48, kD = 48, kH = 96;

    // 共享 rig incantation（section11+ 同款：fresh 小世界 + 确定性天气钉）。
    const auto initTwin = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 字节视图帮手（owning ChunkMeshData → 与 QQuick3DGeometry::vertexData/indexData 同布局的
    // QByteArray——48B/Vtx 顶点 + 4B/quint32 索引，逐字节比对用）。
    const auto vtxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.vertices.constData()),
                          int(m.vertices.size() * int(sizeof(Vtx))));
    };
    const auto idxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.indices.constData()),
                          int(m.indices.size() * int(sizeof(quint32))));
    };
    // 空气袋选址（确定性扫描）：chunk(1,1) 内部带 x,z ∈ [18,29]（离 chunk 边 ≥2 → pad 域不跨邻
    // chunk），y ∈ [40, H-2] 高空带（seed 82 地表 ~57-71 + 树冠 → 40 起向上扫，必越过地形/树冠）。
    // 要求自格 + 6 邻全空气 → 在此放置的方块 6 面全见（悬浮立方，Δ 恰 24v/36i 精确可算）。
    const auto findAirPocket = [](World &w, int skipCols) -> QPair<int, QPair<int, int>> {
        int skipped = 0;
        for (int x = 18; x < 30; ++x)
            for (int z = 18; z < 30; ++z)
                for (int y = 40; y < w.height() - 2; ++y) {
                    if (w.blockAt(x, y, z) != 0)
                        continue;
                    if (w.blockAt(x + 1, y, z) != 0 || w.blockAt(x - 1, y, z) != 0
                        || w.blockAt(x, y + 1, z) != 0 || w.blockAt(x, y - 1, z) != 0
                        || w.blockAt(x, y, z + 1) != 0 || w.blockAt(x, y, z - 1) != 0)
                        continue;
                    if (skipped++ < skipCols)
                        continue;
                    return QPair<int, QPair<int, int>>(y, QPair<int, int>(x, z));
                }
        return QPair<int, QPair<int, int>>(-1, QPair<int, int>(-1, -1));
    };
    // 柱顶选址（确定性扫描）：x,z ∈ [20,28] 第一根「顶格非空气且顶上一格空气」的柱（破顶必改
    // terrain mesh——顶面朝空气必可见；放顶 +1 必出新面）。
    const auto findColumnTop = [](World &w) -> QPair<int, QPair<int, int>> {
        for (int x = 20; x < 29; ++x)
            for (int z = 20; z < 29; ++z)
                for (int y = w.height() - 2; y >= 1; --y)
                    if (w.blockAt(x, y, z) != 0)
                        return w.blockAt(x, y + 1, z) == 0
                            ? QPair<int, QPair<int, int>>(y, QPair<int, int>(x, z))
                            : QPair<int, QPair<int, int>>(-1, QPair<int, int>(-1, -1));
        return QPair<int, QPair<int, int>>(-1, QPair<int, int>(-1, -1));
    };

    // ── r2013a：端到端逐位等价（验收④⑤承重墙：旧 Adapter 消费新结果 + 顶点统计一致）──────
    runLeg(QStringLiteral("r2013a end-to-end bit-equality (R20.13 acceptance 'the old"
        " QtQuick3DAdapter consumes the new result' + 'mesh visuals and vertex stats match the"
        " old path' - the load-bearing wall): over a fresh 48x48x96 s82 world with a"
        " glass/ice/water/lava rig in the interior band, nine segment-routing variants"
        " (terrain default, water, lava, glass, ice, cutoutOnly, cutout-unfolded, greedy,"
        " AO) x two chunks (inland (1,1) and world-edge (0,0) where boundary culling meets"
        " the void) are built through BOTH paths - the legacy adapter (ChunkGeometry"
        " direct-drive, reading back vertexData/indexData) and the new pipeline"
        " (captureChunkMeshSnapshot -> MeshBuilder::build) - and agree bit-for-bit on"
        " vertex count, triangle count, the full 48-byte-per-vertex buffer and the full"
        " index buffer; a known micro-scene pins the exact delta: placing one floating Stone"
        " in an air pocket (all six faces visible) grows BOTH paths by exactly 24 vertices /"
        " 36 indices"), [&]() {
        bool ok = true;
        QString diag;

        World wM;
        initTwin(wM);

        // 流体/半透 rig：水/岩浆/玻璃/冰各一（四个不同空气袋——悬浮放置，段比对非平凡；
        //   solid=false 族不遮挡地形邻面 → terrain 段拓扑不受扰；无 tick → 流体静态不扩散）。
        bool rigOk = true;
        const BR::Id rigIds[4] = { BR::Glass, BR::Ice, BR::Water, BR::Lava };
        for (int i = 0; i < 4 && rigOk; ++i) {
            const QPair<int, QPair<int, int>> p = findAirPocket(wM, i);
            rigOk = p.first >= 0 && wM.setBlock(p.second.first, p.first, p.second.second,
                                                quint8(rigIds[i]), 0);
        }
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig] ");

        // 9 段路由变体 × 2 chunk（内陆 + 世界边——边界剔除对 void）双路径逐位比对。
        struct Variant {
            const char *name;
            bool waterOnly, lavaOnly, glassOnly, iceOnly, cutoutOnly, unfolded, greedy, ao;
        };
        const Variant vars[] = {
            { "terrain",    false, false, false, false, false, false, false, false },
            { "water",      true,  false, false, false, false, false, false, false },
            { "lava",       false, true,  false, false, false, false, false, false },
            { "glass",      false, false, true,  false, false, false, false, false },
            { "ice",        false, false, false, true,  false, false, false, false },
            { "cutoutOnly", false, false, false, false, true,  false, false, false },
            { "unfolded",   false, false, false, false, false, true,  false, false },
            { "greedy",     false, false, false, false, false, false, true,  false },
            { "ao",         false, false, false, false, false, false, false, true },
        };
        long compared = 0;
        for (int ci = 0; ci < 2; ++ci) {
            const int cx = (ci == 0) ? 1 : 0, cz = 1;
            for (const Variant &v : vars) {
                // 旧路径：ChunkGeometry 直建（setter/refreshMesh 触发一次 buildMesh → 灌 QQuick3D）。
                ChunkGeometry geo;
                geo.setWorld(&wM);
                geo.setCx(cx);
                geo.setCz(cz);
                if (v.waterOnly) geo.setWaterOnly(true);
                if (v.lavaOnly) geo.setLavaOnly(true);
                if (v.glassOnly) geo.setGlassOnly(true);
                if (v.iceOnly) geo.setIceOnly(true);
                if (v.cutoutOnly) geo.setCutoutOnly(true);
                if (v.unfolded) geo.setCutoutFolded(false);
                if (v.greedy) geo.setGreedyMeshing(true);
                if (v.ao) geo.setAoEnabled(true);
                if (!v.waterOnly && !v.lavaOnly && !v.glassOnly && !v.iceOnly && !v.cutoutOnly
                    && !v.unfolded && !v.greedy && !v.ao)
                    geo.refreshMesh(); // terrain 默认变体：无 setter 触发 → 显式 Dirty 首建

                // 新路径：采集（bake 镜像 geo 状态——成员默认值逐位对齐）→ MeshBuilder::build。
                ChunkMeshBakeParams bake;
                bake.waterOnly = v.waterOnly;
                bake.lavaOnly = v.lavaOnly;
                bake.glassOnly = v.glassOnly;
                bake.iceOnly = v.iceOnly;
                bake.cutoutOnly = v.cutoutOnly;
                bake.cutoutFolded = !v.unfolded;
                bake.greedyMeshing = v.greedy;
                bake.aoEnabled = v.ao;
                const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(wM), cx, cz, bake);
                const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);

                const bool eq = geo.vertexCount() == mesh.vertexCount
                    && geo.triangleCount() == mesh.triangleCount
                    && geo.vertexData() == vtxBytes(mesh)
                    && geo.indexData() == idxBytes(mesh);
                if (!eq)
                    diag += QStringLiteral("[v %1 c(%2,%3) vc %4/%5 tc %6/%7 vB %8 iB %9] ")
                                .arg(QLatin1String(v.name)).arg(cx).arg(cz)
                                .arg(geo.vertexCount()).arg(mesh.vertexCount)
                                .arg(geo.triangleCount()).arg(mesh.triangleCount)
                                .arg(geo.vertexData() == vtxBytes(mesh))
                                .arg(geo.indexData() == idxBytes(mesh));
                ok = ok && eq;
                ++compared;
            }
        }
        ok = ok && compared == 18;
        if (compared != 18) diag += QStringLiteral("[compared %1] ").arg(compared);

        // 已知小场景（非平凡性承底）：空气袋中放悬浮 Stone = 6 面全见（自格 + 6 邻全空气）
        //   → Δ恰 24 顶点 / 36 索引；双路径同断言（旧路径经 dirty-gate 即时重建链，新路径重采集）。
        const QPair<int, QPair<int, int>> kp = findAirPocket(wM, 4); // 跳过 rig 四袋
        const bool siteOk = kp.first >= 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[known-site y=%1 (%2,%3)] ").arg(kp.first).arg(kp.second.first).arg(kp.second.second);
        if (siteOk) {
            ChunkGeometry geo;
            geo.setWorld(&wM);
            geo.setCx(1);
            geo.setCz(1);
            geo.refreshMesh();
            const int v0 = geo.vertexCount(), t0 = geo.triangleCount();
            ChunkMeshBakeParams bake; // terrain 默认
            const ChunkMeshData mBefore
                = MeshBuilder::build(captureChunkMeshSnapshot(WorldFacade(wM), 1, 1, bake),
                                     MeshBuilder::Reason::Dirty);
            wM.setBlock(kp.second.first, kp.first, kp.second.second, quint8(BR::Stone), 0); // 脏 → 即时重建
            const int v1 = geo.vertexCount(), t1 = geo.triangleCount();
            const ChunkMeshData mAfter
                = MeshBuilder::build(captureChunkMeshSnapshot(WorldFacade(wM), 1, 1, bake),
                                     MeshBuilder::Reason::Dirty);
            // triangleCount 口径 = 三角面数（idx/3）：悬浮立方 6 面 × 2 三角 = Δ12 三角（= 36 索引）。
            const bool deltaOk = v0 > 0 && v1 - v0 == 24 && t1 - t0 == 12
                && mAfter.vertexCount - mBefore.vertexCount == 24
                && mAfter.triangleCount - mBefore.triangleCount == 12;
            ok = ok && deltaOk;
            if (!deltaOk)
                diag += QStringLiteral("[delta geo %1->%2 (%3) mesh %4->%5 (%6)] ")
                            .arg(v0).arg(v1).arg(t1 - t0)
                            .arg(mBefore.vertexCount).arg(mAfter.vertexCount)
                            .arg(mAfter.triangleCount - mBefore.triangleCount);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2013a end-to-end bit-equality: nine segment-routing variants"
                             " x two chunks (inland + world edge) built through the legacy"
                             " ChunkGeometry adapter and the new snapshot->MeshBuilder"
                             " pipeline agree bit-for-bit on vertex/triangle counts and the"
                             " full vertex/index buffers, and a known micro-scene pins the"
                             " exact +24v/+12t (36 idx) delta on both paths - the adapter consumes"
                             " the new owning result with visuals and stats unchanged"
                          << (ok ? QString() : diag);
    });

    // ── r2013b：快照自持性（验收②：输入是 snapshot——自持 / 可独立复制 / OOB 同语义）────────
    runLeg(QStringLiteral("r2013b snapshot self-containment (R20.13 acceptance 'the input is a"
        " snapshot'): a ChunkMeshSnapshot captured from a fresh world keeps answering"
        " identically after the underlying world is later edited (break a surface top, place"
        " a Stone on another column) - rebuilding from the SAME snapshot byte-reproduces the"
        " pre-edit mesh (vertex/index buffers and counts) - while a fresh recapture DOES"
        " reflect the edits (the identity assertions are not vacuous); an independent copy"
        " (copy-construct) is identical to its source and builds the same mesh; and the"
        " snapshot accessors mirror World out-of-bounds semantics exactly (air beyond the"
        " region and outside y range, skyLight 15 above the world, blockLight 0 on top,"
        " column top -1 far outside the capture region)"), [&]() {
        bool ok = true;
        QString diag;

        World wS;
        initTwin(wS);
        ChunkMeshBakeParams bake; // terrain 默认

        const ChunkMeshSnapshot s1 = captureChunkMeshSnapshot(WorldFacade(wS), 1, 1, bake);
        const ChunkMeshData m1 = MeshBuilder::build(s1, MeshBuilder::Reason::Dirty);
        bool baseOk = s1.valid() && m1.vertexCount > 0;
        ok = ok && baseOk;
        if (!baseOk) diag += QStringLiteral("[base valid=%1 v=%2] ").arg(s1.valid()).arg(m1.vertexCount);

        // ① 拷贝独立（R20.06 值纪律：拷贝即深拷贝——identical + 同输出）。
        const ChunkMeshSnapshot s1c = s1;
        const ChunkMeshData m1c = MeshBuilder::build(s1c, MeshBuilder::Reason::Dirty);
        const bool copyOk = s1.identical(s1c) && m1c.vertexCount == m1.vertexCount
            && vtxBytes(m1c) == vtxBytes(m1) && idxBytes(m1c) == idxBytes(m1);
        ok = ok && copyOk;
        if (!copyOk) diag += QStringLiteral("[copy ident=%1 vc=%2] ")
                                 .arg(s1.identical(s1c)).arg(m1c.vertexCount);

        // ② 底层世界改动不影响已采快照（自持性本体）：破柱顶 + 空气袋放 Stone → 同快照再构建逐位不变。
        const QPair<int, QPair<int, int>> pb = findColumnTop(wS); // 破位：柱顶（顶面朝空气必可见 → 必改 mesh）
        const QPair<int, QPair<int, int>> pp = findAirPocket(wS, 0); // 放位：空气袋（6 面全新 → 必改 mesh）
        const bool editSitesOk = pb.first >= 1 && pp.first >= 0
            && wS.blockAt(pb.second.first, pb.first, pb.second.second) != 0;
        ok = ok && editSitesOk;
        if (!editSitesOk)
            diag += QStringLiteral("[edit-sites b=(%1@%2,%3) p=(%4@%5,%6)] ")
                        .arg(pb.first).arg(pb.second.first).arg(pb.second.second)
                        .arg(pp.first).arg(pp.second.first).arg(pp.second.second);
        if (editSitesOk) {
            wS.setBlock(pb.second.first, pb.first, pb.second.second, quint8(BR::Air), 0); // 破柱顶
            wS.setBlock(pp.second.first, pp.first, pp.second.second, quint8(BR::Stone), 0); // 空气袋放 Stone
            const ChunkMeshData m2 = MeshBuilder::build(s1, MeshBuilder::Reason::Dirty);
            const bool selfOk = m2.vertexCount == m1.vertexCount
                && m2.triangleCount == m1.triangleCount
                && vtxBytes(m2) == vtxBytes(m1) && idxBytes(m2) == idxBytes(m1);
            ok = ok && selfOk;
            if (!selfOk)
                diag += QStringLiteral("[self vc %1/%2 vB %3] ")
                            .arg(m2.vertexCount).arg(m1.vertexCount)
                            .arg(vtxBytes(m2) == vtxBytes(m1));

            // ③ 非空转对照：重采集必反映编辑（输出与 m1 不同——恒等断言非恒真）。
            const ChunkMeshSnapshot s2 = captureChunkMeshSnapshot(WorldFacade(wS), 1, 1, bake);
            const ChunkMeshData m3 = MeshBuilder::build(s2, MeshBuilder::Reason::Dirty);
            const bool differs = m3.vertexCount != m1.vertexCount
                || m3.triangleCount != m1.triangleCount || vtxBytes(m3) != vtxBytes(m1);
            ok = ok && differs;
            if (!differs) diag += QStringLiteral("[nonvacuous] ");
        }

        // ④ accessor OOB 语义与 World 逐位同面（域内=采集真值、域外=World 越界面）。
        const bool oobOk = s1.blockAtWorld(-100, 5, -100) == 0
            && s1.blockAtWorld(10, -1, 10) == 0 && s1.blockAtWorld(10, kH + 3, 10) == 0
            && s1.stateAtWorld(-100, 5, -100) == 0
            && s1.skyLightAt(24, kH + 3, 24) == 15 // 世界顶之上 = 开阔天空
            && s1.skyLightAt(24, -1, 24) == 0 && s1.skyLightAt(-100, 5, -100) == 0
            && s1.blockLightAt(24, kH + 3, 24) == 0
            && s1.columnTopAt(s1.originX - 100, s1.originZ - 100) == -1.0f // 域外列顶 → 不遮挡
            && s1.columnTopAt(s1.originX + 8, s1.originZ + 8) >= -1.0f;    // 域内合法读
        ok = ok && oobOk;
        if (!oobOk) diag += QStringLiteral("[oob] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2013b snapshot self-containment: the captured dense snapshot"
                             " byte-reproduces its mesh after the world is edited underneath"
                             " (self-contained), an independent copy is identical and builds"
                             " the same mesh, a fresh recapture does reflect the edits (non-"
                             "vacuous), and accessor OOB semantics mirror World exactly"
                          << (ok ? QString() : diag);
    });

    // ── r2013c：FrameProfiler 计数穿透 + ChunkGeometry 消费面回归（验收③统计自持 + ④零变化）──
    runLeg(QStringLiteral("r2013c profiler count penetration + adapter consumption regression"
        " (R20.13 'owning ChunkMeshData' stats half + 'old adapter consumes new result'):"
        " driving MeshBuilder::build directly increments the FrameProfiler event counters"
        " through the new path exactly once per reason (meshNdirty/meshNsun/meshNwater +1"
        " each, meshN +3, diff-asserted around the three builds); the migrated"
        " ChunkGeometry consumption chain keeps the r2008c contract bit-for-bit - an"
        " in-chunk edit rebuilds immediately (vertex count grows when Stone lands, grows"
        " again under a TallGrass), an out-of-chunk edit leaves the vertex count unchanged"
        " AND increments no dirty counter (the chunk-scoped dirty gate skips without"
        " counting)"), [&]() {
        bool ok = true;
        QString diag;

        World wC;
        initTwin(wC);
        FrameProfiler *fp = FrameProfiler::instance();

        // ① 计数穿透（差分断言——窗口内前置计数与本腿三次直驱 build 隔离）。
        const qint64 n0 = fp->countValue("meshN");
        const qint64 d0 = fp->countValue("meshNdirty");
        const qint64 s0 = fp->countValue("meshNsun");
        const qint64 w0 = fp->countValue("meshNwater");
        ChunkMeshBakeParams bake; // terrain 默认
        const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(wC), 1, 1, bake);
        MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
        MeshBuilder::build(snap, MeshBuilder::Reason::Sun);
        MeshBuilder::build(snap, MeshBuilder::Reason::Water);
        const bool countsOk = fp->countValue("meshNdirty") - d0 == 1
            && fp->countValue("meshNsun") - s0 == 1
            && fp->countValue("meshNwater") - w0 == 1
            && fp->countValue("meshN") - n0 == 3;
        ok = ok && countsOk;
        if (!countsOk)
            diag += QStringLiteral("[counts d+%1 s+%2 w+%3 n+%4] ")
                        .arg(fp->countValue("meshNdirty") - d0)
                        .arg(fp->countValue("meshNsun") - s0)
                        .arg(fp->countValue("meshNwater") - w0)
                        .arg(fp->countValue("meshN") - n0);

        // ② 消费面回归（r2008c 同款选址：chunk(1,1) 内部柱 x=24,z=24，离 chunk 边 ≥2）。
        ChunkGeometry geo;
        geo.setWorld(&wC);
        geo.setCx(1);
        geo.setCz(1);
        int ye = -1;
        for (int y = 40; y < wC.height() - 2; ++y) {
            if (wC.blockAt(24, y, 24) == 0 && wC.blockAt(24, y + 1, 24) == 0) {
                ye = y;
                break;
            }
        }
        const bool siteOk = ye > 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[mesher-site y=%1] ").arg(ye);
        if (siteOk) {
            const qint64 dB = fp->countValue("meshNdirty");
            wC.setBlock(24, ye, 24, quint8(BR::Stone), 0); // 落石：脏 chunk(1,1) → 同步首建（委托链）
            const int v0 = geo.vertexCount();
            const qint64 d1 = fp->countValue("meshNdirty");
            wC.setBlock(24, ye + 1, 24, quint8(BR::TallGrass), 0); // cross 折叠顶点差分
            const int v1 = geo.vertexCount();
            const qint64 d2 = fp->countValue("meshNdirty");
            wC.setBlock(4, ye, 4, quint8(BR::Stone), 0); // chunk(0,0) 编辑：本 chunk 非脏
            const int v2 = geo.vertexCount();            // → 脏门拒重建
            const qint64 d3 = fp->countValue("meshNdirty"); // 跳过 = 零计数
            const bool gateOk = v0 > 0 && v1 > v0 && v2 == v1
                && d1 == dB + 1 && d2 == d1 + 1 && d3 == d2;
            ok = ok && gateOk;
            if (!gateOk)
                diag += QStringLiteral("[gate v0=%1 v1=%2 v2=%3 d %4/%5/%6/%7] ")
                            .arg(v0).arg(v1).arg(v2).arg(dB).arg(d1).arg(d2).arg(d3);
            // 还原（世界清洁——后续腿自建世界不受扰；本腿先于还原的计数差分已完成）。
            wC.setBlock(24, ye + 1, 24, quint8(BR::Air), 0);
            wC.setBlock(24, ye, 24, quint8(BR::Air), 0);
            wC.setBlock(4, ye, 4, quint8(BR::Air), 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2013c count penetration + adapter regression: the profiler"
                             " counters advance exactly once per reason through the new"
                             " MeshBuilder path (diff-asserted), and the migrated"
                             " ChunkGeometry chain keeps the r2008c contract - in-chunk"
                             " edits rebuild immediately (Stone, then TallGrass growth),"
                             " out-of-chunk edits skip with zero rebuilds and zero dirty"
                             " counts - render behavior unchanged"
                          << (ok ? QString() : diag);
    });

    // ── r2013d：结构钉 + 反探 + 空快照空输出 + worldgen 零漂移锚（验收① + 单一权威）─────────
    runLeg(QStringLiteral("r2013d structure pins + reverse probes + empty-snapshot semantics +"
        " worldgen anchor (R20.13 acceptance 'MeshBuilder does not depend on QQuick3D' + the"
        " single-authority extraction contract): comment-stripped pins hold the QObject-free"
        " value discipline (static_assert pins for ChunkMeshSnapshot and ChunkMeshData), the"
        " moved mesh body living in meshbuilder.cpp only (face table, leaf-occlusion"
        " predicate, tile/farmland lookups, greedy MaskEntry, the three per-face loops,"
        " profiler reason counters, the shared sunShadowColumnTop seam), and the"
        " chunkgeometry delegation surface (capture + MeshBuilder::build call, the three"
        " facade gates still standing); reverse probes prove the mesh body did NOT flow back"
        " into chunkgeometry.cpp and that neither meshbuilder file ever mentions QQuick3D;"
        " a default-constructed snapshot builds an empty mesh (the no-chunk legacy path"
        " equivalence); and heightAt/biomeIdAt on a fresh world still match the TerrainGen"
        " pure authority column-for-column (zero worldgen drift)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 源码钉（pinSet 剥注释；根 = exe 相对 src/——r2008c 同款解析）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missH = pinSet(
            srcRoot + QStringLiteral("/World/meshbuilder.h"), {
                SrcPin("r2013 snapshot type", "struct ChunkMeshSnapshot", 1),
                SrcPin("r2013 owning output type", "struct ChunkMeshData", 1),
                SrcPin("r2013 builder type", "class MeshBuilder", 1),
                SrcPin("r2013 build entry", "static ChunkMeshData build(const ChunkMeshSnapshot &snap, Reason reason)", 1),
                SrcPin("r2013 capture entry", "ChunkMeshSnapshot captureChunkMeshSnapshot(const WorldFacade &world, int cx, int cz,", 1),
                SrcPin("r2013 snapshot QObjectFree pin", "static_assert(QObjectFree<ChunkMeshSnapshot>", 1),
                SrcPin("r2013 output QObjectFree pin", "static_assert(QObjectFree<ChunkMeshData>", 1),
                SrcPin("r2013 column-top domain width", "kTopDim = 21", 1),
                SrcPin("r2013 pad domain width", "kDim = kChunk + 2 * kPad", 1),
            });
        const QStringList missCpp = pinSet(
            srcRoot + QStringLiteral("/World/meshbuilder.cpp"), {
                SrcPin("r2013 dirty counter via new path", "FrameProfiler::instance()->count(\"meshNdirty\")", 1),
                SrcPin("r2013 sun counter via new path", "FrameProfiler::instance()->count(\"meshNsun\")", 1),
                SrcPin("r2013 water counter via new path", "FrameProfiler::instance()->count(\"meshNwater\")", 1),
                SrcPin("r2013 partial append moved", "PartialBlockGeometry::append(", 1),
                SrcPin("r2013 greedy mask moved", "struct MaskEntry", 1),
                SrcPin("r2013 face table moved", "static const FaceDef kFaces[6]", 1),
                SrcPin("r2013 three per-face loops moved", "for (int f = 0; f < 6; ++f)", 3),
                SrcPin("r2013 tile lookup moved", "static int tileFor(quint8 block, int face, quint8 state)", 1),
                SrcPin("r2013 farmland lookup moved", "static float farmlandHydrBrightMul(quint8 hydr)", 1),
                SrcPin("r2013 leaf predicate moved", "static bool occludesNeighborFace(quint8 nb)", 1),
                SrcPin("r2013 shared shadow seam consumed", "VoxelLight::sunShadowColumnTop(", 1),
            });
        const QStringList missCg = pinSet(
            srcRoot + QStringLiteral("/World/chunkgeometry.cpp"), {
                SrcPin("r2013 adapter delegates to builder", "MeshBuilder::build(snap, MeshBuilder::Reason(int(reason)))", 1),
                SrcPin("r2013 adapter captures snapshot", "captureChunkMeshSnapshot(WorldFacade(*m_world), m_cx, m_cz, bake)", 1),
                SrcPin("r2008 mesher dirty gate via facade (standing)", "chunkDirty(m_cx, m_cz)", 2),
                SrcPin("r2008 mesher existence gate via facade (standing)", "chunkExists(m_cx, m_cz)", 1),
                SrcPin("r2008 mesher fluid-only gate via facade (standing)", "chunkFluidOnlyDirty(m_cx, m_cz)", 1),
            });
        const QStringList missVl = pinSet(
            srcRoot + QStringLiteral("/World/voxellight.h"), {
                SrcPin("r2013 shadow column-top seam", "sunShadowColumnTop(const QVector3D &sunDir, bool shadowsEnabled,", 1),
                SrcPin("r2013 seam consumes provider", "const float top = columnTop(x0 + xi, z0 + zi);", 1),
                SrcPin("r2013 world path delegates to seam", "return sunShadowColumnTop(sunDir, shadowsEnabled,", 1),
            });
        for (const QString &m : missH + missCpp + missCg + missVl) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 反探（禁出/不回流——minCount=1 反探惯用法：miss 非空 = 合规缺席；剥注释器防注释误伤）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString mbh = srcRoot + QStringLiteral("/World/meshbuilder.h");
        const QString mbc = srcRoot + QStringLiteral("/World/meshbuilder.cpp");
        const QString cgc = srcRoot + QStringLiteral("/World/chunkgeometry.cpp");
        const bool negOk = forbiddenAbsent(mbh, "QQuick3D")      // 验收①：MeshBuilder 不依赖 QQuick3D
            && forbiddenAbsent(mbc, "QQuick3D")
            && forbiddenAbsent(cgc, "static const FaceDef kFaces") // 网格本体不回流（搬移非复制）
            && forbiddenAbsent(cgc, "occludesNeighborFace")
            && forbiddenAbsent(cgc, "struct MaskEntry")
            && forbiddenAbsent(cgc, "stripFrames")
            && forbiddenAbsent(cgc, "railProbeDelta")
            && forbiddenAbsent(cgc, "FarmlandHydrationMask");
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg-probe] ");

        // ③ 空快照空输出（!haveChunk 旧路径等价——计数照走、产出空 mesh）。
        const ChunkMeshData empty = MeshBuilder::build(ChunkMeshSnapshot{}, MeshBuilder::Reason::Dirty);
        const bool emptySemOk = empty.vertexCount == 0 && empty.triangleCount == 0
            && empty.vertices.isEmpty() && empty.indices.isEmpty();
        ok = ok && emptySemOk;
        if (!emptySemOk)
            diag += QStringLiteral("[empty v=%1 t=%2] ").arg(empty.vertexCount).arg(empty.triangleCount);

        // ④ worldgen 零漂移锚（抽核；完整恒等族 = worldgen/determin/re-gen 常驻腿）。
        World wG;
        initTwin(wG);
        const TerrainGen::Dims dims{ kW, kD, kH };
        TerrainGen gen(82, dims);
        bool wgOk = true;
        const int probes[4][2] = { { 7, 7 }, { 24, 13 }, { 40, 42 }, { 31, 5 } };
        for (const auto &p : probes) {
            wgOk = wgOk && wG.heightAt(p[0], p[1]) == gen.heightAt(p[0], p[1])
                && wG.biomeIdAt(p[0], p[1]) == int(gen.biomeComputeAt(p[0], p[1]));
        }
        ok = ok && wgOk;
        if (!wgOk) diag += QStringLiteral("[worldgen] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2013d structure pins + reverse probes + empty semantics +"
                             " worldgen anchor: the QObject-free snapshot/output pins, the"
                             " mesh body anchored in meshbuilder.cpp only, and the"
                             " chunkgeometry delegation surface all hold under"
                             " comment-stripped probes; reverse probes prove no mesh logic"
                             " flowed back and no QQuick3D mention exists in the builder; a"
                             " default snapshot builds empty; worldgen columns still match"
                             " the TerrainGen authority"
                          << (ok ? QString() : diag);
    });
}
