#include "matrix_helpers.h"

#include "chunklifecycle.h"  // r2034d：sparse 会话柱的生命周期读面（Loaded 收敛判据）
#include "gamesession.h"     // r2034d：sparse 会话执行器柱（不双起线程的行为面对照）
#include "meshbuilder.h"     // 对照面：captureChunkMeshSnapshot → MeshBuilder::build（同步直调权威）
#include "streamingbridge.h" // r2034d：收割宿主生产链（enterWorld fixed 分支使能 + pumpTick 薄收割槽）
#include "worldclock.h"      // r2034d：泵拍源装配（ticked 连接宿主——腿内不发拍，pumpTick 手动驱动）
#include "worldstore.h"      // r2034d：enterWorld 装配面（openWorld fresh 临时库）

#include <QElapsedTimer>
#include <QThread> // 真线程收敛节奏（deadline 轮询——r2024/r2020 防 flake 先例）

#include <thread> // r2034d：线程身份对账（执行 == 工作线程 != 调用者——r2012a/r2020 同门）

// ── §29.7 t1060 fixed 世界 bake 异步化——C1 完全体 探针段（4 腿；filter 词 r2034；矩阵 667→667+N）
// 置尾先例沿用（接 section34，runAll 末执行）。任务契约（docs/refactor-plan-2026-09-08.md §29.7）：
//   「fixed 异步 ≡ 同步逐位等价墙」→ r2034a（fixed 使能世界 bake 走 World 惰性执行器 → 收割拍
//     应用后网格与同步直调 capture+build 逐位恒等——W4 等价墙的 fixed 端到端化；多形态快照抽样：
//     地形 / 流体[水段] / 异形[cross 并入 terrain 段 PASS1] / 世界边界）；
//   「风暴场景主线程单帧应用有界 + 队列排干收敛」→ r2034b（注入 dayMul 跨门强制全段 rebake：
//     提交数 == 非空段数、等 build 完成后单次收割恰 kFixedMeshHarvestPerBeat 条[上界断言]、
//     后续收割拍排干收敛、终态网格逐位 == 同步参照）；
//   「env 回退 + 回退与卫生」→ r2034c（QTVOXEL_SYNC_BAKE≠0 → 使能拒绝 + 全同步内联[旧行为
//     逐位]——专用 env 令跑取证；使能容量缝压 1 → 满载 kErrQueueFull 可见拒绝 → 同步内联回退
//     计数可见 + 网格仍正确产出；在途析构 join 有界 + 再构造再用——r2020c 析构卫生语义复用）；
//   「结构钉」→ r2034d（env 缝正面钉 / 惰性单例源序钉 / 单 MeshWorker 实例 + 线程身份行为钉 /
//     fixed 与流式不双起线程行为钉 / 收割宿主单点收口扫略 + 生产链行为柱[enterWorld fixed 分支
//     使能 + StreamingBridge::pumpTick 薄收割] / MeshBuilder 单一权威反探兼容 / F3 stream 行
//     fixed 侧推送面正面钉）。
// 选型对照（头注释立证面 = world.{h,cpp}/streamingbridge.{h,cpp}/meshworker.h/chunkgeometry 注）：
//   执行器归属 = World 侧惰性单例（fixed 模式专属；与 W4 流式会话执行器模式互斥——并存不可能，
//     「fixed 与流式不重复起线程」结构性成立）；收割宿主 = StreamingBridge::pumpTick 挂
//     WorldClock::ticked 既有拍的薄收割槽（单拍应用有界 = 风暴摊平；ChunkGeometry 帧驱动拉取
//     案被否——500 段各自拉取与收割拍单点收口纪律相悖）。
// 恰红面设计（先于腿文定稿；双变异双还原，存证 build/ 终名日志 matrix_r2034_neg{1,2}_{red,restore}.log）：
//   变异一（NEG-1）= 摘收割应用（world.cpp harvestBuiltChunkMeshes 排干循环 while 条件 false &&
//     前缀——built 永不交付/灌注，applied 恒 0）→ 声明红面 {r2034a, r2034b, r2034c, r2034d}
//     [任务书候选面 = {a,b}；实测扩面如实修订留痕：c 相 1/3 的收敛面与 d ③单实例柱/⑥生产链
//     柱同住收割通路——收割即被测语义本体，diag 逐腿核对均为「pend=N ∧ harv=N 排干超时 /
//     bound first=0 / 队列不排空」的被摘语义本体，非连带伤]。r2026a 不误伤（同步内联路径
//     不可达收割拍——实跑 EXIT=0）；r2026b/c 不误伤（sparse 收割拍在 gamesession.h
//     pumpStreamingTick ⑤ 段 / 手工排干——独立落点，实跑 4P）；r2020 实跑 5P（组件级）。
//   变异二（NEG-2）= 篡改 worker 输出（world.cpp harvestBuiltChunkMeshes 交付前位扰动——
//     requestId 段位域 bits 13-15 == 1[水段] 且非空时首顶点 u += 0.25f；段位选点 = 红面精确
//     收缩到含水段形态的等价柱——r2034b 全 terrain 段[段位 0]不触）→ 声明红面 {r2034a}
//     [实测恰红单腿 = 声明面精确兑现：水段形态逐位比对失配（diag bytes water va=180 vb=180
//     vm=180——计数面照旧存活，扰动是值级非结构级）；地形/异形/边界形态与 b/c/d 全部不误伤
//     （实跑 3P）]。r2026b 不误伤（sparse 手工排干不经 fixed 收割拍，实跑 4P）；r2020 实跑 5P。
// 防 flake 三连跑：真线程收敛全部走「阈值锚 deadline 轮询」（等 fixedMeshHarvestableCount()==N
//   的阈值面，非瞬态窗——r2020c Windows 定时器粒度教训），断言面 = 确定性终态（同 seed 物化 +
//   同步编辑 + 字节比对 + 恰等计数）。满载柱 = 容量缝 0 退化满载（每提交必 kErrQueueFull
//   ——r2020b 同门；零时序依赖，重负在途拦截型因棋盘高面数段逐顶点 PCF 列扫秒级徒增腿时长
//   而弃用——过程坑留痕）。
// 时长控制：fixed 96×96×96 s82（6×6 chunk——r2034b 需 M=36 段 > kFixedMeshHarvestPerBeat=16
//   以兑现上界断言）；其余 48×48×96 s82 小世界。
void MatrixRun::section35_fixed_async_bake()
{
    // fixed 小世界 incantation（r2013/r2026 族同款：setter 各自 generate，末次 setSeed 收口全量再生）。
    const auto makeFixed = [](World &w, int side) {
        w.setWidth(side);
        w.setDepth(side);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 字节视图帮手（r2013a/r2026 同款：owning ChunkMeshData → QQuick3DGeometry 同布局 QByteArray）。
    const auto vtxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.vertices.constData()),
                          int(m.vertices.size() * int(sizeof(Vtx))));
    };
    const auto idxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.indices.constData()),
                          int(m.indices.size() * int(sizeof(quint32))));
    };
    // 柱顶选址（r2026 族同款，域限定单 chunk 内部）：第一根「顶格非空气且顶上一格空气」的柱。
    const auto findColumnTopInChunk = [](World &w, int cx, int cz) -> QPair<int, QPair<int, int>> {
        const int x0 = cx * 16 + 3, z0 = cz * 16 + 3; // chunk 内部带（离边界 ≥3，pad 域内自洽）
        for (int x = x0; x < x0 + 10; ++x)
            for (int z = z0; z < z0 + 10; ++z)
                for (int y = w.height() - 2; y >= 1; --y)
                    if (w.blockAt(x, y, z) != 0)
                        return w.blockAt(x, y + 1, z) == 0
                            ? QPair<int, QPair<int, int>>(y + 1, QPair<int, int>(x, z))
                            : QPair<int, QPair<int, int>>(-1, QPair<int, int>(-1, -1));
        return QPair<int, QPair<int, int>>(-1, QPair<int, int>(-1, -1));
    };
    // 收割收敛轮询（每轮薄收割一次 + 阈值判定：在途注册表排干 ∧ 执行器 built 账清——收割与
    //   构建竞速不靠瞬态窗，靠「收割拍持续泵 + 终态阈值」有界收敛；r2020c 定时器粒度教训）。
    const auto convergeHarvest = [](World &w, int deadlineMs, QString &diag) {
        QElapsedTimer t;
        t.start();
        for (;;) {
            w.harvestBuiltChunkMeshes();
            if (w.pendingChunkMeshJobCount() == 0 && w.fixedMeshHarvestableCount() == 0)
                return true;
            if (t.elapsed() > deadlineMs) {
                diag += QStringLiteral("[harvest-timeout %1ms pend=%2 harv=%3] ")
                            .arg(deadlineMs)
                            .arg(w.pendingChunkMeshJobCount())
                            .arg(w.fixedMeshHarvestableCount());
                return false;
            }
            QThread::msleep(1);
        }
    };
    // 「等 build 全部完成」阈值锚（收割拍**不**启动——r2034b 单拍上界断言的等 build 完成对锚）。
    const auto waitHarvestable = [](World &w, int want, int deadlineMs, QString &diag) {
        QElapsedTimer t;
        t.start();
        while (w.fixedMeshHarvestableCount() < want) {
            if (t.elapsed() > deadlineMs) {
                diag += QStringLiteral("[build-timeout %1ms harv=%2/%3] ")
                            .arg(deadlineMs)
                            .arg(w.fixedMeshHarvestableCount())
                            .arg(want);
                return false;
            }
            QThread::msleep(1);
        }
        return true;
    };
    const bool envSyncBake = qEnvironmentVariable("QTVOXEL_SYNC_BAKE").toInt() != 0;

    // ── r2034a：fixed 异步 ≡ 同步逐位等价承重墙（多形态端到端）──────────────────────────
    runLeg(QStringLiteral("r2034a fixed async-vs-sync equivalence load-bearing wall (an enabled"
        " fixed world bakes through the World-owned lazy executor thread and the bounded"
        " harvest beat: terrain on an inland chunk, a water-only segment over a hand-filled"
        " basin, a terrain segment carrying a cross cutout block folded into PASS 1, and a"
        " world-edge chunk all converge, and every applied mesh matches the direct"
        " capture+build authority on a bare sync twin world bit-for-bit (full vertex/index"
        " buffers + stats, the W4 caliber end-to-ended on the fixed side); the executor is"
        " the single World-owned instance running on its own worker thread, the F3 stream"
        " line gains the fixed-side submit/harvest pushes and the win-line worker column"
        " moves, and the resident chunk revision shows zero drift"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗基线（计数差分防跨腿残留）

        // 使能世界 A + 裸同步孪生 B（同 seed 同 dims 同编辑序——r2023a 确定性同门孪生）。
        World wA, wB;
        makeFixed(wA, 48);
        makeFixed(wB, 48);
        // 内容编辑（先于使能/挂几何——世界无监听者零烘培；A/B 同序同坐标逐位同果）。
        const auto topIn = findColumnTopInChunk(wA, 1, 1); // 内陆柱顶（放 Stone 塔）
        const auto topCross = findColumnTopInChunk(wA, 1, 0); // cross 形态柱顶（放 TallGrass）
        bool siteOk = topIn.first > 0 && topCross.first > 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[site in=%1 cross=%2] ").arg(topIn.first).arg(topCross.first);
        for (int h = 0; h < 3 && siteOk; ++h) { // 内陆 3 格石塔（地形形态改造）
            siteOk = wA.setBlock(topIn.second.first, topIn.first + h, topIn.second.second,
                          quint8(BR::Stone), 0)
                && wB.setBlock(topIn.second.first, topIn.first + h, topIn.second.second,
                          quint8(BR::Stone), 0);
        }
        siteOk = siteOk && wA.setBlock(topCross.second.first, topCross.first, topCross.second.second,
                                quint8(BR::TallGrass), 0)
            && wB.setBlock(topCross.second.first, topCross.first, topCross.second.second,
                                quint8(BR::TallGrass), 0);
        // 水池（chunk (0,1) 内部 3×3 悬空水格 y=height-2[高空空气域——地形高度带下方，Mesher
        //   只看邻域规则与写序，悬空性不影响等价语义]；A/B 同序同坐标）。
        bool waterPlaced = true;
        for (int dx = 0; dx < 3 && waterPlaced; ++dx)
            for (int dz = 0; dz < 3 && waterPlaced; ++dz) {
                const int x = 0 * 16 + 5 + dx, z = 1 * 16 + 5 + dz;
                const int ytop = wA.height() - 2;
                waterPlaced = wA.blockAt(x, ytop, z) == 0
                    && wA.setBlock(x, ytop, z, quint8(BR::Water), 0)
                    && wB.blockAt(x, ytop, z) == 0
                    && wB.setBlock(x, ytop, z, quint8(BR::Water), 0);
            }
        ok = ok && waterPlaced;
        if (!waterPlaced) diag += QStringLiteral("[water] ");

        // 使能（env 回退跑 = 拒绝 → 本腿退化为同步恒等面仍成立；常态跑 = 真异步面）。
        const bool enabled = wA.enableFixedAsyncBake();
        const bool enableOk = enabled == !envSyncBake;
        ok = ok && enableOk;
        if (!enableOk) diag += QStringLiteral("[enable=%1 env=%2] ").arg(enabled).arg(envSyncBake);

        // 四形态几何（A 走异步；B 同步直烘对照——r2026a apply-before-return 口径）。
        ChunkGeometry gAterr, gAcross, gAwater, gAedge, gBterr, gBcross, gBwater, gBedge;
        gAwater.setWaterOnly(true);
        gBwater.setWaterOnly(true);
        gAterr.setWorld(&wA); gAterr.setCx(1); gAterr.setCz(1); // 内陆（石塔）
        gAcross.setWorld(&wA); gAcross.setCx(1); gAcross.setCz(0); // 异形（TallGrass 并入 PASS1）
        gAwater.setWorld(&wA); gAwater.setCx(0); gAwater.setCz(1); // 流体（水段）
        gAedge.setWorld(&wA); gAedge.setCx(0); gAedge.setCz(0); // 世界边界（OOB=空气剔除面）
        gBterr.setWorld(&wB); gBterr.setCx(1); gBterr.setCz(1);
        gBcross.setWorld(&wB); gBcross.setCx(1); gBcross.setCz(0);
        gBwater.setWorld(&wB); gBwater.setCx(0); gBwater.setCz(1);
        gBedge.setWorld(&wB); gBedge.setCx(0); gBedge.setCz(0);
        gAterr.refreshMesh(); gAcross.refreshMesh(); gAwater.refreshMesh(); gAedge.refreshMesh();
        gBterr.refreshMesh(); gBcross.refreshMesh(); gBwater.refreshMesh(); gBedge.refreshMesh();
        const bool bSync = gBterr.vertexCount() > 0 && gBcross.vertexCount() > 0
            && gBedge.vertexCount() > 0; // B 侧 apply-before-return 即时可见（同步口径本体）
        ok = ok && bSync;
        if (!bSync) diag += QStringLiteral("[twin-sync v=%1/%2/%3] ").arg(gBterr.vertexCount())
                               .arg(gBcross.vertexCount()).arg(gBedge.vertexCount());

        // 收割收敛（异步侧；水段顶点可空恒可——等价性由字节比对承载，非空转由 A 侧计数承载）。
        const bool convOk = envSyncBake ? true : convergeHarvest(wA, 30000, diag);
        ok = ok && convOk;
        // A 侧非空见证：内陆/异形/边界三形态终态顶点 > 0（水段以水格在场的字节比对承载）。
        const bool nonEmpty = gAterr.vertexCount() > 0 && gAcross.vertexCount() > 0
            && gAedge.vertexCount() > 0;
        ok = ok && nonEmpty;
        if (!nonEmpty) diag += QStringLiteral("[a-empty v=%1/%2/%3 w=%4] ").arg(gAterr.vertexCount())
                                  .arg(gAcross.vertexCount()).arg(gAedge.vertexCount())
                                  .arg(gAwater.vertexCount());

        // 逐位比对（异步收割应用 ≡ 同步直调权威——全顶点/索引缓冲 + 统计；水段 bake 镜像 waterOnly）。
        struct SegCheck { ChunkGeometry *ga; ChunkGeometry *gb; int cx; int cz; bool water;
            const char *name; };
        const SegCheck checks[4] = {
            { &gAterr, &gBterr, 1, 1, false, "terrain(1,1)" },
            { &gAcross, &gBcross, 1, 0, false, "cross(1,0)" },
            { &gAwater, &gBwater, 0, 1, true, "water(0,1)" },
            { &gAedge, &gBedge, 0, 0, false, "edge(0,0)" },
        };
        int compared = 0;
        bool bytesOk = true;
        for (const SegCheck &c : checks) {
            ChunkMeshBakeParams bake;
            bake.waterOnly = c.water;
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(wB), c.cx, c.cz, bake);
            const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
            const bool eq = c.ga->vertexCount() == c.gb->vertexCount()
                && c.gb->vertexCount() == mesh.vertexCount
                && c.ga->triangleCount() == mesh.triangleCount
                && c.ga->vertexData() == vtxBytes(mesh) && c.ga->indexData() == idxBytes(mesh);
            bytesOk = bytesOk && eq;
            ++compared;
            if (!eq)
                diag += QStringLiteral("[bytes %1 va=%2 vb=%3 vm=%4] ").arg(QLatin1String(c.name))
                            .arg(c.ga->vertexCount()).arg(c.gb->vertexCount()).arg(mesh.vertexCount);
        }
        ok = ok && bytesOk && compared == 4;

        // 执行器面：单实例在活 + 线程身份（执行 == 工作线程 != 调用者——r2012a/r2020 同门；
        //   const 读面两访问器——提交/收割已由几何链端到端行使；env 回退跑执行器按设计缺席）。
        const MeshWorker *mw = wA.fixedMeshWorker();
        const bool execOk = envSyncBake || (mw != nullptr
            && mw->workerThreadId() != std::this_thread::get_id()
            && mw->observedExecThreadId() == mw->workerThreadId());
        ok = ok && execOk;
        if (!execOk) diag += QStringLiteral("[exec mw=%1] ").arg(mw != nullptr);

        // F3 面：fixed 侧 sub/mesh 域推送可见（>0）+ win 行 worker 列应用计数 > 0。
        const qint64 subN = fp->countValue("streamSub"), meshN = fp->countValue("streamMesh");
        const qint64 workerN = fp->countValue("meshNworker");
        const bool f3Ok = envSyncBake
            ? (subN == 0 && meshN == 0 && workerN == 0)
            : (subN >= 4 && meshN >= 1 && workerN >= 1);
        ok = ok && f3Ok;
        if (!f3Ok) diag += QStringLiteral("[f3 sub=%1 mesh=%2 worker=%3] ").arg(subN).arg(meshN).arg(workerN);

        // 驻留集零漂移（网格作业零生命周期触达——r2026b 同门）。
        const int rev = wA.residentChunkRevision();
        ok = ok && rev == 0; // fixed 世界驻留集恒全网格零沿（r2021 口径）
        if (rev != 0) diag += QStringLiteral("[rev=%1] ").arg(rev);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2034a fixed async-vs-sync equivalence load-bearing wall:"
                             " the enabled fixed world bakes four form-diverse segments"
                             " (terrain / water / cross / world-edge) through the"
                             " World-owned lazy executor and the bounded harvest beat,"
                             " and every applied mesh matches the direct capture+build"
                             " authority on a bare sync twin bit-for-bit; single worker"
                             " instance on its own thread, F3 stream sub/mesh and win"
                             " worker columns move, resident revision zero-drift"
                          << (ok ? QString() : diag);
    });

    // ── r2034b：风暴摊平（dayMul 跨门强制全段 rebake → 提交数=段数 + 单拍应用有界 + 排干收敛）──
    runLeg(QStringLiteral("r2034b dawn storm flattening (a 6x6-chunk fixed world with terrain"
        " geometry on every chunk: after the first bake converges, an injected dayMul"
        " threshold crossing forces every non-empty segment to resubmit - the F3 window"
        " gains exactly one accepted submission per non-empty segment [36/36], builds drain"
        " on the worker thread while the harvest beat stays idle, then ONE bounded harvest"
        " call applies exactly kFixedMeshHarvestPerBeat segments [upper-bound assertion],"
        " every subsequent beat applies at most the cap, the backlog drains to zero and the"
        " total applied equals the submission count, and the converged meshes match the"
        " direct capture+build authority at the new dayMul bit-for-bit; the resident"
        " revision stays at zero throughout)"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗基线

        World w;
        makeFixed(w, 96); // 6×6 chunk（M=36 段 > kFixedMeshHarvestPerBeat=16——上界断言兑现前提）
        const bool enabled = w.enableFixedAsyncBake();
        ok = ok && enabled == !envSyncBake;
        if (envSyncBake) {
            // env 回退跑：风暴面不可达（无执行器）——退化为「使能拒绝 + 门关」一致性即收口。
            const bool gateClosed = !w.chunkMeshAsyncActive();
            ok = ok && gateClosed;
            if (!gateClosed) diag += QStringLiteral("[env-gate] ");
        } else {
            const int kChunks = w.chunksX() * w.chunksZ(); // 36
            QVector<ChunkGeometry *> geos;
            for (int cz = 0; cz < w.chunksZ(); ++cz)
                for (int cx = 0; cx < w.chunksX(); ++cx) {
                    ChunkGeometry *g = new ChunkGeometry();
                    g->setWorld(&w);
                    g->setCx(cx);
                    g->setCz(cz);
                    g->refreshMesh(); // 首建（fresh 世界 chunk 非脏——恰 1 提交/段）
                    geos.append(g);
                }
            const bool conv1 = convergeHarvest(w, 30000, diag);
            ok = ok && conv1;
            // 非空段账：首建后顶点 > 0 的段数 M（地形段每 chunk 必非空——bedrock 顶面在场）。
            int nonEmpty = 0;
            for (ChunkGeometry *g : geos)
                if (g->vertexCount() > 0)
                    ++nonEmpty;
            const bool mOk = nonEmpty == kChunks;
            ok = ok && mOk;
            if (!mOk) diag += QStringLiteral("[M=%1/%2] ").arg(nonEmpty).arg(kChunks);

            // 风暴注入：dayMul 跨量化门（|Δ| ≥ 0.03）——非空段各自恰 1 次重烘提交；空段 setter 早退。
            fp->flush(); // 风暴窗起点（首建提交不入户）
            const qint64 sub0 = fp->countValue("streamSub");
            for (ChunkGeometry *g : geos)
                g->setDayMul(0.5f);
            const bool buildDone = waitHarvestable(w, nonEmpty, 30000, diag);
            ok = ok && buildDone; // 等值锚：收割拍未启动，built 账恰 = 提交数 M（构建全完成）
            const qint64 subDelta = fp->countValue("streamSub") - sub0;
            const bool submitOk = subDelta == nonEmpty;
            ok = ok && submitOk; // 提交数 == 段数（全部受理——36 ≤ 队列 64 恒不触满载）
            if (!submitOk)
                diag += QStringLiteral("[submit %1/%2] ").arg(subDelta).arg(nonEmpty);

            // 单拍应用上界：恰一次收割 → applied == kFixedMeshHarvestPerBeat（M > cap 兑现）。
            const int firstBeat = w.harvestBuiltChunkMeshes();
            const bool boundOk = firstBeat == World::kFixedMeshHarvestPerBeat
                && firstBeat <= World::kFixedMeshHarvestPerBeat
                && w.fixedMeshHarvestableCount() == nonEmpty - firstBeat;
            ok = ok && boundOk;
            if (!boundOk)
                diag += QStringLiteral("[bound first=%1 cap=%2 left=%3] ").arg(firstBeat)
                            .arg(World::kFixedMeshHarvestPerBeat)
                            .arg(w.fixedMeshHarvestableCount());

            // 后续拍排干收敛：每拍 ≤ cap、总应用 == 提交数、终态账清（pending ∧ harvestable 双零）。
            int totalApplied = firstBeat, beats = 1;
            while (totalApplied < nonEmpty && beats < 1000) {
                const int beat = w.harvestBuiltChunkMeshes();
                if (beat > World::kFixedMeshHarvestPerBeat) {
                    diag += QStringLiteral("[beat-over %1] ").arg(beat);
                    ok = false;
                    break;
                }
                totalApplied += beat;
                ++beats;
            }
            const bool drained = totalApplied == nonEmpty && w.pendingChunkMeshJobCount() == 0
                && w.fixedMeshHarvestableCount() == 0;
            ok = ok && drained;
            if (!drained)
                diag += QStringLiteral("[drain total=%1/%2 pend=%3 harv=%4] ").arg(totalApplied)
                            .arg(nonEmpty).arg(w.pendingChunkMeshJobCount())
                            .arg(w.fixedMeshHarvestableCount());

            // 终态逐位 == 同步参照（direct capture+build @ dayMul 0.5——抽样内陆/边界/中心三形态）。
            struct SegCheck { ChunkGeometry *geo; int cx; int cz; const char *name; };
            const SegCheck checks[3] = {
                { geos[0], 0, 0, "edge(0,0)" },
                { geos[7], 1, 1, "inland(1,1)" },
                { geos[geos.size() - 1], w.chunksX() - 1, w.chunksZ() - 1, "far(5,5)" },
            };
            for (const SegCheck &c : checks) {
                ChunkMeshBakeParams bake;
                bake.dayMul = 0.5f; // 镜像风暴后的烘焙状态（采集定格口径——r2026b 同门）
                const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(w), c.cx, c.cz, bake);
                const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
                const bool eq = c.geo->vertexCount() == mesh.vertexCount
                    && c.geo->triangleCount() == mesh.triangleCount
                    && c.geo->vertexData() == vtxBytes(mesh)
                    && c.geo->indexData() == idxBytes(mesh);
                ok = ok && eq;
                if (!eq)
                    diag += QStringLiteral("[final %1 vc=%2/%3] ").arg(QLatin1String(c.name))
                                .arg(c.geo->vertexCount()).arg(mesh.vertexCount);
            }

            // F3 面：风暴窗 mesh 域 == 总应用；win 行 worker 列 ≥ 风暴应用（窗基线在风暴 flush
            //   处清零——首建应用已随 flush 出窗，窗内恰 = 风暴 36 交付）；驻留零沿。
            const bool f3Ok = fp->countValue("streamMesh") >= totalApplied
                && fp->countValue("meshNworker") >= totalApplied
                && w.residentChunkRevision() == 0;
            ok = ok && f3Ok;
            if (!f3Ok)
                diag += QStringLiteral("[f3 mesh=%1 tot=%2 worker=%3 rev=%4] ")
                            .arg(fp->countValue("streamMesh")).arg(totalApplied)
                            .arg(fp->countValue("meshNworker")).arg(w.residentChunkRevision());

            qDeleteAll(geos);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2034b dawn storm flattening: the dayMul threshold crossing"
                             " resubmits exactly the non-empty segment count, builds drain on"
                             " the worker while the harvest beat stays idle, one bounded"
                             " harvest applies exactly the per-beat cap, later beats drain"
                             " the backlog to exact reconciliation, converged meshes match"
                             " the direct capture+build authority at the new dayMul"
                             " bit-for-bit, and the resident revision stays at zero"
                          << (ok ? QString() : diag);
    });

    // ── r2034c：回退与卫生（env 全同步 / 满载内联回退 / 析构卫生）────────────────────────
    runLeg(QStringLiteral("r2034c fallback + hygiene (phase 1 the env kill-switch read once per"
        " process decides the enable face: with QTVOXEL_SYNC_BAKE unset the enable succeeds"
        " and the gate opens on the desired flag, while with it set the enable refuses, the"
        " gate stays closed and a refresh bakes apply-before-return with no executor ever"
        " constructed [the old inline path bitwise]; phase 2 the capacity seam at zero"
        " degenerates to deterministic queue-full - every submission is visibly refused and"
        " falls back inline synchronously with apply-before-return byte-equal meshes, the"
        " fallback counter counting exactly the refusals and no registry entries left;"
        " phase 3 destroying the world with grid jobs in flight"
        " joins within the deadline and a fresh enabled world is immediately reusable"
        " [destructor hygiene, the mesh-worker component leg semantics reused]"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush();

        // 相 1：env 回退门（进程 env 定面；两分支各自确定——env 令跑取证同步面）。
        World w1;
        makeFixed(w1, 48);
        const bool enabled = w1.enableFixedAsyncBake();
        const bool gateOk = enabled == !envSyncBake && w1.chunkMeshAsyncActive() == enabled
            && w1.fixedMeshWorker() == nullptr; // 惰性：使能后未提交仍零线程
        ok = ok && gateOk;
        if (!gateOk)
            diag += QStringLiteral("[gate en=%1 act=%2 mw=%3 env=%4] ").arg(enabled)
                        .arg(w1.chunkMeshAsyncActive()).arg(w1.fixedMeshWorker() != nullptr)
                        .arg(envSyncBake);
        {
            ChunkGeometry g;
            g.setWorld(&w1);
            g.setCx(1);
            g.setCz(1);
            g.refreshMesh();
            const bool syncFace = envSyncBake
                ? g.vertexCount() > 0 // env=1：apply-before-return（旧路径逐位）
                : convergeHarvest(w1, 30000, diag) && g.vertexCount() > 0;
            ok = ok && syncFace;
            if (!syncFace) diag += QStringLiteral("[face v=%1] ").arg(g.vertexCount());
        }

        // 相 2：满载内联回退（容量缝 0 = 退化满载——每提交必 kErrQueueFull，零时序依赖；
        //   r2020b「cap=0 退化确定性满载」同门。普通地形段即可——拒绝确定性来自容量而非
        //   在途拦截，重负构件非必需[棋盘高面数段的逐顶点 PCF 列扫为秒级，徒增腿时长]）。
        World w2;
        makeFixed(w2, 48);
        const bool en2 = w2.enableFixedAsyncBake(0); // 退化满载缝（env 回退跑按设计拒绝）
        ok = ok && en2 == !envSyncBake;
        ChunkGeometry g0, g1, g2, g3;
        g0.setWorld(&w2); g0.setCx(1); g0.setCz(1);
        g1.setWorld(&w2); g1.setCx(0); g1.setCz(0);
        g2.setWorld(&w2); g2.setCx(0); g2.setCz(1);
        g3.setWorld(&w2); g3.setCx(0); g3.setCz(2);
        fp->flush(); // 满载窗起点
        const qint64 fb0 = fp->countValue("meshNsyncFallback");
        g0.refreshMesh(); // 满载拒 → 同步内联回退（重负网格 ms 级在主线程当帧完成）
        g1.refreshMesh();       // 同上
        g2.refreshMesh();       // 同上
        g3.refreshMesh();       // 同上
        const int fallback = int(fp->countValue("meshNsyncFallback") - fb0);
        // 双模确定性：常态跑 = 退化满载 4 提交全拒 + 回退计数恰 4 + 零在途注册；env 回退跑 =
        //   无执行器可拒 → 全部同步内联（门不入按门同步非回退——计数恒 0 为 env 态正确面）。
        const bool fullOk = g0.vertexCount() > 0
            && g1.vertexCount() > 0 && g2.vertexCount() > 0 && g3.vertexCount() > 0
            && w2.pendingChunkMeshJobCount() == 0
            && fallback == (envSyncBake ? 0 : 4);
        ok = ok && fullOk;
        if (!fullOk)
            diag += QStringLiteral("[full fb=%1 v=%2/%3/%4/%5] ").arg(fallback)
                        .arg(g0.vertexCount()).arg(g1.vertexCount())
                        .arg(g2.vertexCount()).arg(g3.vertexCount());
        // 回退产出正确性柱：g1/g3 字节 == 直调权威（回退 = 旧同步内联逐位保留）。
        if (fullOk) {
            const ChunkMeshSnapshot s1 = captureChunkMeshSnapshot(WorldFacade(w2), 0, 0,
                                                                  ChunkMeshBakeParams{});
            const ChunkMeshData m1 = MeshBuilder::build(s1, MeshBuilder::Reason::Dirty);
            const ChunkMeshSnapshot s3 = captureChunkMeshSnapshot(WorldFacade(w2), 0, 2,
                                                                  ChunkMeshBakeParams{});
            const ChunkMeshData m3 = MeshBuilder::build(s3, MeshBuilder::Reason::Dirty);
            const bool fbBytes = g1.vertexData() == vtxBytes(m1)
                && g1.indexData() == idxBytes(m1)
                && g3.vertexData() == vtxBytes(m3) && g3.indexData() == idxBytes(m3);
            ok = ok && fbBytes;
            if (!fbBytes) diag += QStringLiteral("[full-bytes] ");
        }

        // 相 3：析构卫生（使能世界 + 在途/积压网格作业 → 世界析构 join 有界 + 再构造再用
        //   ——r2020c 析构卫生语义复用；计时域 = 构造到作用域闭合全程墙钟）。
        QElapsedTimer dtorTimer;
        dtorTimer.start();
        {
            World w3;
            makeFixed(w3, 48);
            const bool en3 = w3.enableFixedAsyncBake(1); // 容量 1：首件在途 + 次件排队/拒两态并存
            ChunkGeometry gm;
            gm.setWorld(&w3);
            gm.setCx(1);
            gm.setCz(1);
            gm.refreshMesh(); // 提交（在途或排队——析构丢弃/跑完两态皆 join 有界）
            ChunkGeometry gn;
            gn.setWorld(&w3);
            gn.setCx(0);
            gn.setCz(0);
            gn.refreshMesh();
        } // w3 析构：执行器 stop + 队内未开工请求逐条丢弃计数 + join 有界绝不挂死
        const bool dtorBounded = dtorTimer.elapsed() < 10000; // join 有界（重负构建 ms 级）
        ok = ok && dtorBounded;
        if (!dtorBounded) diag += QStringLiteral("[dtor %1ms] ").arg(dtorTimer.elapsed());
        // 再构造再用（全周期恰一生命周期——析构不残留全局态）：
        World w4;
        makeFixed(w4, 48);
        const bool en4 = w4.enableFixedAsyncBake();
        ChunkGeometry gr;
        gr.setWorld(&w4);
        gr.setCx(1);
        gr.setCz(1);
        gr.refreshMesh();
        // 双模确定性：env 回退跑 en4 按设计为 false（世界保持全同步——顶点即时可见即卫生面）；
        //   常态跑先收敛后验顶点（异步应用时序——收敛先行，顶点判据殿后）。
        const bool reuseOk = en4 == !envSyncBake
            && (envSyncBake || convergeHarvest(w4, 30000, diag)) && gr.vertexCount() > 0;
        ok = ok && reuseOk;
        if (!reuseOk) diag += QStringLiteral("[reuse en=%1 v=%2] ").arg(en4).arg(gr.vertexCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2034c fallback + hygiene: the env kill-switch decides the"
                             " enable face (refuse = gate closed = inline bitwise, success ="
                             " gate open with no executor until first submit), the capacity"
                             " seam at zero degenerates to deterministic queue-full (every"
                             " submission visibly refused, inline fallback applies"
                             " byte-equal meshes, counter exact, registry clean), and"
                             " destruction with grid jobs in flight joins within the"
                             " deadline with a fresh enabled world immediately reusable"
                          << (ok ? QString() : diag);
    });

    // ── r2034d：结构钉（env 缝正面钉 + 惰性单例源序 + 单实例/线程身份 + 不双起线程 + 收割宿主
    //    单点收口 + 生产链行为柱 + 单一权威反探兼容 + F3 推送面正面钉）────────────────────
    runLeg(QStringLiteral("r2034d structure pins (source pins on the env kill-switch read-once"
        " seam, the enable/lazy-construction/harvest faces and the F3 fixed-side push sites;"
        " source-order pins: the desired flag is set in enable before the executor is ever"
        " constructed lazily inside the submit seam, and construction is gated on the null"
        " worker [lazy singleton]; behavioral single-instance pin - two bakes reuse the same"
        " executor pointer and run on one worker thread distinct from the caller; no-double-"
        "thread pin - a sparse streaming world owns its session executor while the World-side"
        " fixed executor stays null; the fixed harvest is a single drain point across src/"
        " [world decl+impl, bridge header+impl only] with the production chain exercised"
        " live: a real fixed-world entry through StreamingBridge::enterWorld enables async"
        " and StreamingBridge::pumpTick harvests the built meshes; MeshBuilder single-"
        "authority reverse probes hold on world.cpp; Main.qml stays token-free)"), [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① 正面钉（pinSet 剥注释；minCount 对实际出现次数核）。
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("r2034 env read-once seam", "static const bool syncBake", 1),
                SrcPin("r2034 env variable face", "qEnvironmentVariable(\"QTVOXEL_SYNC_BAKE\")", 1),
                SrcPin("r2034 enable impl", "bool World::enableFixedAsyncBake", 1),
                SrcPin("r2034 lazy ensure impl", "MeshWorker *World::ensureFixedMeshWorker", 1),
                SrcPin("r2034 lazy construction", "std::make_unique<MeshWorker>(", 1),
                SrcPin("r2034 harvest impl", "int World::harvestBuiltChunkMeshes", 1),
                SrcPin("r2034 fixed drain", "m_fixedMeshWorker->takeBuilt(built)", 1),
                SrcPin("r2034 deliver route", "deliverBuiltChunkMesh(built.requestId, std::move(built.mesh))", 1),
                SrcPin("r2034 stream-sub push", "addCount(\"streamSub\", 1)", 1),
                SrcPin("r2034 stream-mesh push", "addCount(\"streamMesh\", applied)", 1),
                SrcPin("r2034 submit-seam lazy gate", "!m_chunkMeshBuildSink && m_fixedAsyncBakeDesired", 1),
            });
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("r2034 enable decl", "bool enableFixedAsyncBake(int meshWorkerQueueCapacity = -1);", 1),
                SrcPin("r2034 desired read", "bool fixedAsyncBakeDesired() const { return m_fixedAsyncBakeDesired; }", 1),
                SrcPin("r2034 executor read", "const MeshWorker *fixedMeshWorker() const { return m_fixedMeshWorker.get(); }", 1),
                SrcPin("r2034 harvest decl", "int harvestBuiltChunkMeshes();", 1),
                SrcPin("r2034 per-beat cap", "static constexpr int kFixedMeshHarvestPerBeat = 16;", 1),
                SrcPin("r2034 ownership member", "std::unique_ptr<MeshWorker> m_fixedMeshWorker;", 1),
            });
        const QStringList missSb = pinSet(
            srcRoot + QStringLiteral("/Game/streamingbridge.cpp"), {
                SrcPin("r2034 host enable", "world->enableFixedAsyncBake();", 1),
                SrcPin("r2034 host pump hook", "ensurePumpHook(clock);", 2),
                SrcPin("r2034 host harvest", "m_fixedWorld->harvestBuiltChunkMeshes();", 1),
            });
        for (const QString &m : missWc + missWh + missSb) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 源序钉：使能置标先于惰性构造（文件序）+ 构造体在 null 门之后（惰性单例形态）+
        //    提交缝先查门后 ensure（执行序）。
        QFile wcFile(srcRoot + QStringLiteral("/World/world.cpp"));
        QString wcRaw;
        if (wcFile.open(QIODevice::ReadOnly))
            wcRaw = QString::fromUtf8(wcFile.readAll());
        const int enablePos = wcRaw.indexOf(QStringLiteral("m_fixedAsyncBakeDesired = true;"));
        const int mkPos = wcRaw.indexOf(QStringLiteral("std::make_unique<MeshWorker>("));
        const int nullGatePos = wcRaw.indexOf(QStringLiteral("if (!m_fixedMeshWorker) {"));
        const int seamGatePos = wcRaw.indexOf(QStringLiteral("!m_chunkMeshBuildSink && m_fixedAsyncBakeDesired"));
        const int ensureCallPos = wcRaw.indexOf(QStringLiteral("ensureFixedMeshWorker();"));
        const bool orderOk = enablePos >= 0 && mkPos > enablePos && nullGatePos > enablePos
            && nullGatePos < mkPos && seamGatePos >= 0 && ensureCallPos > seamGatePos;
        ok = ok && orderOk;
        if (!orderOk)
            diag += QStringLiteral("[order en=%1 mk=%2 gate=%3 seam=%4 call=%5] ")
                        .arg(enablePos).arg(mkPos).arg(nullGatePos).arg(seamGatePos).arg(ensureCallPos);

        // ③ 单实例 + 线程身份行为钉：两杆烘培复用同一执行器指针，单工作线程 != 调用者线程。
        World w;
        makeFixed(w, 48);
        w.enableFixedAsyncBake();
        ChunkGeometry g1;
        g1.setWorld(&w);
        g1.setCx(1);
        g1.setCz(1);
        g1.refreshMesh();
        const MeshWorker *mw1 = w.fixedMeshWorker();
        g1.setCx(2); // 同几何换 chunk 再提交（第二次异步烘培）
        g1.refreshMesh();
        const MeshWorker *mw2 = w.fixedMeshWorker();
        // 双模确定性：env 回退跑 = 门关 + 执行器缺席；常态跑 = 单实例 + 线程身份。
        const bool singleOk = envSyncBake
            ? (!w.chunkMeshAsyncActive() && w.fixedMeshWorker() == nullptr)
            : (convergeHarvest(w, 30000, diag) && mw1 != nullptr && mw1 == mw2
                && mw1->workerThreadId() != std::this_thread::get_id()
                && mw1->observedExecThreadId() == mw1->workerThreadId());
        ok = ok && singleOk;
        if (!singleOk) diag += QStringLiteral("[single same=%1] ").arg(mw1 == mw2 && mw1 != nullptr);

        // ④ 不双起线程行为钉：sparse 会话世界 = 会话执行器在、World 侧执行器恒 null。
        {
            World::SparseWorldParams sp;
            sp.seed = 42;
            sp.coreWidth = 80;
            sp.coreDepth = 80;
            sp.height = 96;
            sp.spawnPreGenerateRadius = 0;
            World ws(sp);
            GameSession gs(ws);
            const bool noDouble = gs.meshWorker() != nullptr && !ws.fixedAsyncBakeDesired()
                && ws.fixedMeshWorker() == nullptr
                && gs.meshWorker()->workerThreadId() != std::this_thread::get_id();
            ok = ok && noDouble;
            if (!noDouble)
                diag += QStringLiteral("[nodouble sess=%1 wside=%2] ")
                            .arg(gs.meshWorker() != nullptr)
                            .arg(ws.fixedMeshWorker() != nullptr);
        }

        // ⑤ 收割宿主单点收口：全 src 树 harvestBuiltChunkMeshes( 恰 {world.h 声明, world.cpp
        //    实现, streamingbridge.h 注述, streamingbridge.cpp 生产调用} 四文件（世界/几何/QML
        //    零第二收割点）。
        QStringList harvestFiles;
        QDirIterator itH(srcRoot, { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                         QDir::Files, QDirIterator::Subdirectories);
        while (itH.hasNext()) {
            QFile f(itH.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("harvestBuiltChunkMeshes(")))
                harvestFiles << QDir(srcRoot).relativeFilePath(itH.filePath());
        }
        harvestFiles.sort();
        const bool singleDrainOk = harvestFiles.size() == 4
            && harvestFiles.contains(QStringLiteral("World/world.h"))
            && harvestFiles.contains(QStringLiteral("World/world.cpp"))
            && harvestFiles.contains(QStringLiteral("Game/streamingbridge.h"))
            && harvestFiles.contains(QStringLiteral("Game/streamingbridge.cpp"));
        ok = ok && singleDrainOk;
        if (!singleDrainOk)
            diag += QStringLiteral("[drain %1] ").arg(harvestFiles.join(QLatin1Char(',')));

        // ⑥ 生产链行为柱（收割宿主活体面）：真桥 fixed 进入分支使能 + pumpTick 薄收割交付。
        bool hostOpened = false, hostOk = false;
        {
            StreamingBridge &bridge = *StreamingBridge::instance();
            bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）
            // 临时库路径（绝对路径直用——r2015/r2027/r2028 段同门；fresh + 用后即删，saves/ 零触碰）。
            const QString db = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_r2034_host_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(db); // fresh
            World wh;
            makeFixed(wh, 48);
            WorldStore store;
            hostOpened = store.openWorld(db) && store.isOpen();
            store.setWorld(&wh);
            WorldClock clock;
            PlayerController pc;
            const bool entered = hostOpened
                && !bridge.enterWorld(&wh, &store, &clock, &pc, db, 82); // fixed 链恒 false
            // 双模确定性：常态跑 = 使能 + 门开（pumpTick 薄收割交付）；env 回退跑 = 使能被拒
            //   + 门关 + 几何同步内联（收割拍零动作——生产宿主链在回退态同样自洽）。
            const bool wiredOk = entered
                && wh.fixedAsyncBakeDesired() == !envSyncBake
                && wh.chunkMeshAsyncActive() == !envSyncBake;
            ChunkGeometry gp;
            gp.setWorld(&wh);
            gp.setCx(1);
            gp.setCz(1);
            gp.refreshMesh();
            // 生产收割拍：bridge.pumpTick()（无会话 → 薄收割槽）轮询至段非空（宿主单点交付）。
            QElapsedTimer t;
            t.start();
            while (gp.vertexCount() == 0 && t.elapsed() < 30000) {
                bridge.pumpTick();
                QThread::msleep(1);
            }
            hostOk = wiredOk && gp.vertexCount() > 0
                && wh.pendingChunkMeshJobCount() == 0;
            store.closeWorld(); // 连接关-用-收口（临时库用后即删的解锁前置）
        } // wh/world store/clock/pc 全消亡——bridge 侧 QPointer/连接随对象析构自动归零
        ok = ok && hostOpened && hostOk;
        if (!hostOk || !hostOpened)
            diag += QStringLiteral("[host opened=%1 ok=%2] ").arg(hostOpened).arg(hostOk);
        QFile::remove(QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_r2034_host_%1.sqlite").arg(QCoreApplication::applicationPid())));

        // ⑦ 单一权威反探兼容（r2013d 复述面）：world.cpp 零网格本体回流（构建唯一落点 =
        //    meshbuilder，World 侧只编排）。
        const QStringList missRev = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("forbidden build call", "MeshBuilder::build", 1),
                SrcPin("forbidden kFaces", "static const FaceDef kFaces", 1),
                SrcPin("forbidden occlude", "occludesNeighborFace", 1),
            });
        const bool revOk = missRev.size() == 3
            && !missRev.first().startsWith(QStringLiteral("<file-unreadable"));
        ok = ok && revOk;
        if (!revOk) diag += QStringLiteral("[rev] ");

        // ⑧ QML 零触碰反探：Main.qml 零新面记号（宿主/使能/门全不入 QML——收割宿主 C++ 槽挂接）。
        QFile qmlFile(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/.."))
                           .absoluteFilePath(QStringLiteral("src/ui/Main.qml")));
        QString qml;
        if (qmlFile.open(QIODevice::ReadOnly))
            qml = QString::fromUtf8(qmlFile.readAll());
        const bool qmlOk = !qml.contains(QStringLiteral("enableFixedAsyncBake"))
            && !qml.contains(QStringLiteral("harvestBuiltChunkMeshes"))
            && !qml.contains(QStringLiteral("fixedMeshWorker"))
            && qml.contains(QStringLiteral("rebuildChunkSlotPool")); // P4 池消费面原样在位
        ok = ok && qmlOk;
        if (!qmlOk) diag += QStringLiteral("[qml] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2034d structure pins: the env read-once seam, the"
                             " enable/lazy-construction/harvest faces and the F3 fixed-side"
                             " push sites are positively pinned; lazy-singleton source order"
                             " holds; two bakes reuse one executor on one worker thread; a"
                             " sparse streaming world keeps the World-side executor null (no"
                             " double threading); the fixed harvest is a single drain point"
                             " across src/ and the production chain is exercised live through"
                             " StreamingBridge::enterWorld + pumpTick; world.cpp stays free of"
                             " mesh-body tokens; Main.qml stays token-free"
                          << (ok ? QString() : diag);
    });
}
