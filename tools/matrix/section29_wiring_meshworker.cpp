#include "matrix_helpers.h"

#include "chunklifecycle.h"      // 被测面：ChunkLifecycle（3×3 Loaded 收敛判据 + revision 沿零漂移）
#include "gamesession.h"         // 被测面：W4 接线宿主（执行器构造 + World 桥提交缝 + 收割拍）
#include "meshbuilder.h"         // 对照面：captureChunkMeshSnapshot → MeshBuilder::build（同步直调权威）
#include "meshworker.h"          // 被测面：独立执行器（r2026c 手工收割相；kErrMeshWorkerStopped 常量）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep——r2024/r2020 防 flake 先例）

// §29.5-W4 bake→worker 网格化 探针段（4 腿；filter 词 r2026；矩阵 640→640+N）。置尾先例沿用
//（接 section28，runAll 末执行）。任务契约（docs/refactor-plan-2026-09-08.md §29.5.2 W4 原文）：
//   「ChunkGeometry bake 段改提交快照 + 收割应用」→ r2026a/b（fixed 零变化墙 + sparse 异步
//     等价承重墙：收割应用后网格数据与同步直调 MeshBuilder::build 逐位恒等——r2020a 端到端化）；
//   「同步内联回退开关」→ r2026c（满载/已停拒绝 → 内联回退计数可见、网格仍正确产出；在途
//     重复提交 → 最新快照胜断言——路由层注销 miss + 交付回调自证双保险）；
//   「F3 mesh 行加 worker 列」→ r2026d（frameprofiler.cpp win 行新列正面钉 + 行为级 flush
//     报告钉）+ 结构钉（QML 零触碰反探 / fixed 零构造反探 / MeshBuilder 单一权威反探兼容
//     [r2013d]/收割拍单点收口）。
// 选型对照（头注释立证面 = gamesession.h/chunkgeometry.{h,cpp}/world.{h,cpp}/meshworker.h）：
//   所有权 = GameSession 流式会话（bake 发起点经 World 桥 std::function 缝可达 + 收割拍 =
//     pumpStreamingTick 同宿）；收割点 = tick 尾单点（非阻塞轮询、延迟 ≤ 一 tick、QML 零改动）；
//   路由 = World 注册表（requestId → 交付回调，直接回调式）——QML 面零新暴露。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘收割应用（gamesession.h pumpStreamingTick ⑤ 段 if (false && m_meshWorker)
//     前缀——built 永不交付/灌注）→ 声明红面 {r2026b}[异步链路端到端承重：几何 vertexCount
//     恒 0 收敛超时 + 逐位比对无对象]。r2026a 不误伤（fixed 世界零 worker，路径不可达）；
//     r2026c 不误伤（独立执行器 + 手工排干，不经会话收割拍——独立性即本腿设计）；r2026d 不
//     误伤（源钉 = 剥注释 token 在位，if (false) 前缀不剥文本）。
//   变异二（NEG-2）= 摘同步回退（chunkgeometry.cpp submitMeshJobAsync 拒绝路径 return false →
//     return true——谎报受理，满载即丢）→ 声明红面 {r2026c}[回退面断言：meshNsyncFallback
//     +1 且网格同步产出——变异后计数照走但无回退应用 → vertexCount 恒 0]。r2026a 不误伤
//    （fixed 世界不进异步门）；r2026b 不误伤（提交全部被接受，拒绝路径不可达）；r2026d 不
//     误伤（meshNsyncFallback 计数 token 仍在位）。
// 防 flake 三连跑：真线程腿（r2026b/c）收敛全部走 deadline 轮询（不靠瞬态窗等待——r2020c
//   Windows 定时器粒度教训），断言面 = 确定性终态（同 seed 物化 + 同步编辑 + 字节比对）。
// 时长控制：sparse 世界 radius 0 预生成 + 驱动半径压小（1,1,3）——9 chunk 窗收敛秒级。
void MatrixRun::section29_wiring_meshworker()
{
    constexpr int kWS = 80, kDS = 80, kH = 96, kSeed = 42; // sparse 核心域（W2/W3 段同族）
    constexpr int kWF = 48, kDF = 48, kHF = 96, kSeedF = 82; // fixed 小世界（3×3 chunk，r2013 先例）

    const auto makeSparse = []() {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kWS;
        sp.coreDepth = kDS;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0; // 零预生成（中心 chunk 仍由 W1 语义物化——r2024 过程坑①）
        return World(sp);
    };
    const auto initTwin = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(kSeedF);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 字节视图帮手（r2013a 同款：owning ChunkMeshData → 与 QQuick3DGeometry 同布局 QByteArray）。
    const auto vtxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.vertices.constData()),
                          int(m.vertices.size() * int(sizeof(Vtx))));
    };
    const auto idxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.indices.constData()),
                          int(m.indices.size() * int(sizeof(quint32))));
    };
    // 收敛轮询（真线程收割节奏不定——deadline 有界，不靠睡等；每轮 stepTick(0.11) = 恰 1 整
    //   tick = 1 个 tick 尾收割拍）。
    const auto converge = [&](GameSession &gs, const std::function<bool()> &ready,
                              int deadlineMs, QString &diag) {
        QElapsedTimer t;
        t.start();
        while (!ready()) {
            if (t.elapsed() > deadlineMs) {
                diag += QStringLiteral("[timeout %1ms] ").arg(deadlineMs);
                return false;
            }
            gs.stepTick(0.11);
            QThread::msleep(2);
        }
        return true;
    };
    // 柱顶选址（r2013a findColumnTop 同款，域限定单 chunk 内部）：第一根「顶格非空气且顶上
    //   一格空气」的柱 → 放置必改 mesh（新方块 6 邻至少顶面朝空气）。
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

    // ── r2026a：fixed 世界零变化墙（全同步内联 + 无 worker 构造 + F3 计数恒零）─────────────
    runLeg(QStringLiteral("r2026a fixed-world zero-change wall (the async bake path is gated on"
        " the World bridge sink, which only a sparse streaming session binds - a fixed-world"
        " GameSession constructs no mesh executor [D2 zero-activity wall continues], the"
        " bridge stays unbound, and every bake stays inline-synchronous: refreshMesh applies"
        " the mesh BEFORE returning, an edit rebuilds synchronously within setBlock, the"
        " applied bytes match a direct capture+build bit-for-bit [the R20.13 extraction"
        " byte-comparison caliber], and the F3 worker/fallback counters stay at zero with"
        " zero harvested items"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗基线（计数差分防跨腿残留）

        World w;
        initTwin(w);
        GameSession gs(w);
        const bool gateOk = !gs.streamingActive() && gs.meshWorker() == nullptr
            && !w.chunkMeshAsyncActive() && w.pendingChunkMeshJobCount() == 0
            && w.droppedBuiltMeshCount() == 0;
        ok = ok && gateOk;
        if (!gateOk) diag += QStringLiteral("[gate sa=%1 mw=%2 async=%3] ")
                                  .arg(gs.streamingActive())
                                  .arg(gs.meshWorker() != nullptr)
                                  .arg(w.chunkMeshAsyncActive());

        // 同步内联承重：refreshMesh 返回即应用（无收割拍可达），字节与直调权威恒等。
        ChunkGeometry geo;
        geo.setWorld(&w);
        geo.setCx(1);
        geo.setCz(1);
        geo.refreshMesh();
        ChunkMeshBakeParams bake; // 默认值 = geo 成员默认值逐位对齐（meshbuilder.h 头注契约）
        const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(w), 1, 1, bake);
        const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
        const bool syncOk = geo.vertexCount() > 0 && geo.vertexCount() == mesh.vertexCount
            && geo.triangleCount() == mesh.triangleCount && geo.vertexData() == vtxBytes(mesh)
            && geo.indexData() == idxBytes(mesh);
        ok = ok && syncOk;
        if (!syncOk)
            diag += QStringLiteral("[sync vc %1/%2] ").arg(geo.vertexCount()).arg(mesh.vertexCount);

        // 编辑即时重建照旧同步：setBlock 返回时顶点已更新（W4 零改动面——破/放当帧语义不动）。
        const auto top = findColumnTopInChunk(w, 1, 1);
        const bool siteOk = top.first > 0;
        int beforeVerts = -1;
        bool editOk = false;
        if (siteOk) {
            beforeVerts = geo.vertexCount();
            const bool placed = w.setBlock(top.second.first, top.first, top.second.second,
                                           quint8(BR::Stone), 0);
            editOk = placed && geo.vertexCount() != beforeVerts;
        }
        ok = ok && siteOk && editOk;
        if (!siteOk || !editOk)
            diag += QStringLiteral("[edit site=%1 placed-delta v%2->%3] ")
                        .arg(siteOk).arg(beforeVerts).arg(geo.vertexCount());

        // F3 计数面：worker/回退列恒 0 + 零收割（fixed 世界连执行器都没有）。
        const bool countsOk = fp->countValue("meshNworker") == 0
            && fp->countValue("meshNsyncFallback") == 0 && fp->countValue("meshNstale") == 0
            && gs.meshHarvestedCount() == 0;
        ok = ok && countsOk;
        if (!countsOk) diag += QStringLiteral("[counts w=%1 fb=%2 h=%3] ")
                                  .arg(fp->countValue("meshNworker"))
                                  .arg(fp->countValue("meshNsyncFallback"))
                                  .arg(gs.meshHarvestedCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2026a fixed-world zero-change wall: no mesh executor"
                             " constructed, bridge unbound, every bake inline-synchronous"
                             " (apply-before-return + sync edit rebuild + bit-equal bytes"
                             " vs the direct capture+build authority) and the F3"
                             " worker/fallback/stale counters with the session harvest"
                             " count all stay at zero"
                          << (ok ? QString() : diag);
    });

    // ── r2026b：异步等价承重墙（sparse 流式世界：bake 走 worker → 收割应用 ≡ 同步直调逐位）──
    runLeg(QStringLiteral("r2026b async equivalence load-bearing wall (a sparse streaming world"
        " with a real GameSession [real mesh executor thread]: after the driver materializes"
        " the 3x3 window, four ChunkGeometry segments - terrain on three chunks plus a water"
        " segment - submit snapshots through the World bridge and their meshes arrive via the"
        " tick-tail harvest beat; after convergence every applied mesh matches a direct"
        " capture+build on the same world bit-for-bit (full vertex/index buffers + stats,"
        " the component-level bit-equality wall end-to-ended through the production wiring),"
        " the QML pool consumption face shows zero drift (resident chunk revision unchanged,"
        " F3 vertex/triangle stats equal), and the executor books reconcile exactly"
        " [accepted == pending + queued + harvested, applied == 4, harvest == applied +"
        " visible drops]"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗基线

        World ws = makeSparse();
        GameSession gs(ws);
        const bool wireOk = gs.meshWorker() != nullptr && ws.chunkMeshAsyncActive()
            && ws.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded; // radius-0 中心预生成（W1 语义）
        ok = ok && wireOk;
        if (!wireOk) diag += QStringLiteral("[wire mw=%1 async=%2 life=%3] ")
                                  .arg(gs.meshWorker() != nullptr)
                                  .arg(ws.chunkMeshAsyncActive())
                                  .arg(int(ws.chunks().lifecycleAt(2, 2)));

        // 3×3 窗物化（半径压小 (1,1,3)——生成窗 3×3；P5 调参缝同 W2 先例）。
        gs.configureStreamingRadii(1, 1, 3);
        gs.notePlayerChunk(2, 2);
        QVector<QPair<int, int>> keys;
        for (int cx = 1; cx <= 3; ++cx)
            for (int cz = 1; cz <= 3; ++cz)
                keys.append({ cx, cz });
        const bool loadedOk = converge(gs, [&ws, &keys]() {
            for (const auto &k : keys)
                if (ws.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded)
                    return false;
            return true;
        }, 30000, diag);
        if (!loadedOk) {
            QString miss;
            for (const auto &k : keys)
                miss += QStringLiteral("(%1,%2)=%3 ").arg(k.first).arg(k.second)
                            .arg(int(ws.chunks().lifecycleAt(k.first, k.second)));
            diag += QStringLiteral("[loaded %1] ").arg(miss);
        }
        ok = ok && loadedOk;

        // 四段几何（3 terrain chunk + 同 chunk 水段——段位折叠使 requestId 互异的承重面）。
        const int rev0 = ws.residentChunkRevision();
        const qint64 appliedBase = fp->countValue("meshNworker"); // 应用计数基线（含空网格应用）
        ChunkGeometry gA, gB, gC, gW;
        gW.setWaterOnly(true); // setWorld 前设段位（无世界时仅记值不构建——setter 早退链）
        gA.setWorld(&ws); gA.setCx(2); gA.setCz(2);
        gB.setWorld(&ws); gB.setCx(1); gB.setCz(2);
        gC.setWorld(&ws); gC.setCx(3); gC.setCz(3);
        gW.setWorld(&ws); gW.setCx(2); gW.setCz(2);
        gA.refreshMesh(); gB.refreshMesh(); gC.refreshMesh(); gW.refreshMesh();

        // 收敛 = 3 地形段顶点 > 0 + 应用计数恰 +4（水段在该 seed 无水 → 产出空网格恒 0 顶点
        //   ——应用事实由 worker 列计数承载，顶点数不可作其收敛判据）。
        const bool appliedOk = converge(gs, [&]() {
            return gA.vertexCount() > 0 && gB.vertexCount() > 0 && gC.vertexCount() > 0
                && fp->countValue("meshNworker") - appliedBase >= 4;
        }, 30000, diag);
        if (!appliedOk)
            diag += QStringLiteral("[applied vA=%1 vB=%2 vC=%3 vW=%4 pend=%5 acc=%6 harv=%7 drop=%8]")
                        .arg(gA.vertexCount()).arg(gB.vertexCount()).arg(gC.vertexCount())
                        .arg(gW.vertexCount())
                        .arg(ws.pendingChunkMeshJobCount())
                        .arg(gs.meshWorker()->acceptedCount())
                        .arg(gs.meshHarvestedCount())
                        .arg(ws.droppedBuiltMeshCount());
        ok = ok && appliedOk;

        // 逐位比对（收割应用 ≡ 同步直调权威——全顶点/索引缓冲 + 统计；r2020a 端到端化）。
        struct SegCheck { ChunkGeometry *geo; int cx; int cz; bool water; const char *name; };
        const SegCheck checks[4] = {
            { &gA, 2, 2, false, "terrain(2,2)" },
            { &gB, 1, 2, false, "terrain(1,2)" },
            { &gC, 3, 3, false, "terrain(3,3)" },
            { &gW, 2, 2, true, "water(2,2)" },
        };
        int compared = 0;
        bool bytesOk = true;
        for (const SegCheck &c : checks) {
            ChunkMeshBakeParams bake; // 默认 = geo 成员默认（水段镜像 waterOnly）
            bake.waterOnly = c.water;
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(ws), c.cx, c.cz, bake);
            const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
            const bool eq = c.geo->vertexCount() == mesh.vertexCount
                && c.geo->triangleCount() == mesh.triangleCount
                && c.geo->vertexData() == vtxBytes(mesh) && c.geo->indexData() == idxBytes(mesh);
            bytesOk = bytesOk && eq;
            ++compared;
            if (!eq)
                diag += QStringLiteral("[bytes %1 vc %2/%3] ")
                            .arg(QLatin1String(c.name))
                            .arg(c.geo->vertexCount()).arg(mesh.vertexCount);
        }
        ok = ok && bytesOk && compared == 4;

        // QML 池消费面零漂移：网格作业零生命周期触达（revision 沿不动）+ F3 统计已逐位恒等。
        const bool revOk = ws.residentChunkRevision() == rev0;
        ok = ok && revOk;
        if (!revOk) diag += QStringLiteral("[rev %1->%2] ").arg(rev0).arg(ws.residentChunkRevision());

        // 账面对账（恰一产出铁律的生产面）：应用恰 4；收割 = 应用 + 可见丢弃（setWorld 触发的
        //   被 refreshMesh 注销的先行提交，上界 = 2 提交/几何 × 4 几何）；执行器 accepted ==
        //   pending + queued + 收割。
        const qint64 appliedN = fp->countValue("meshNworker") - appliedBase;
        const int dropped = ws.droppedBuiltMeshCount();
        const int harvested = gs.meshHarvestedCount();
        const MeshWorker *mw = gs.meshWorker();
        const bool booksOk = appliedN == 4 && dropped >= 0 && dropped <= 8
            && harvested == 4 + dropped;
        const bool booksReal = quint64(mw->acceptedCount())
            == quint64(mw->pendingCount()) + quint64(mw->builtCount()) + quint64(harvested);
        ok = ok && booksOk && booksReal;
        if (!booksOk || !booksReal)
            diag += QStringLiteral("[books applied=%1 dropped=%2 harvested=%3 acc=%4 p=%5 b=%6] ")
                        .arg(appliedN).arg(dropped).arg(harvested)
                        .arg(mw->acceptedCount()).arg(mw->pendingCount()).arg(mw->builtCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2026b async equivalence load-bearing wall: sparse streaming"
                             " world bakes through the real executor thread and the tick-tail"
                             " harvest beat, four segment meshes converge and match the"
                             " direct capture+build authority bit-for-bit, the resident"
                             " revision edge shows zero drift, and the executor books"
                             " reconcile exactly (accepted == pending + queued + harvested,"
                             " applied == 4, harvest == applied + visible drops)"
                          << (ok ? QString() : diag);
    });

    // ── r2026c：回退与卫生（满载/已停拒绝 → 内联回退；在途重复提交 → 最新快照胜）──────────
    runLeg(QStringLiteral("r2026c fallback + hygiene (phase 1 latest-snapshot-wins over a real"
        " standalone executor with a hand-drained harvest - deliberately not the session"
        " pump, keeping this leg independent of the harvest wiring: submit A then submit B"
        " while A is in flight cancels A at the routing registry, the drained delivery"
        " misses A [visible drop +1] and applies B, and the applied bytes equal a direct"
        " build of the final world; phase 2 a queue-full refusal falls back inline"
        " synchronously [apply-before-return, bytes still bit-equal, meshNsyncFallback +1,"
        " no pending left]; phase 3 a stopped-executor refusal [kErrMeshWorkerStopped]"
        " falls back identically; phase 4 unbinding the sink recovers the plain"
        " synchronous gate with NO fallback counter; phase 5 cancel hygiene empties the"
        " routing registry)"), [&]() {
        bool ok = true;
        QString diag;
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗基线

        // 相 0：rig——sparse 世界 + 独立执行器 + 手工 sink（(2,2) 由 W1 radius-0 预生成承载）。
        World ws = makeSparse();
        MeshWorker wk; // 独立执行器（不经 GameSession——收割由本腿手工排干）
        ws.setChunkMeshBuildSink(
            [&wk](const ChunkMeshSnapshot &s, quint64 id) { return wk.submit(s, id); });
        const bool rigOk = ws.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded
            && ws.chunkMeshAsyncActive();
        ok = ok && rigOk;
        ChunkGeometry geo;
        geo.setWorld(&ws);
        geo.setCx(2);
        geo.setCz(2);
        // 手工收割泵（交付谓词驱动——收敛到断言事实本身，无队列瞬态窗：r2020c 定时器粒度教训）。
        const auto pumpUntil = [&](const std::function<bool()> &done) {
            QElapsedTimer t;
            t.start();
            for (;;) {
                MeshBuiltItem it;
                while (wk.takeBuilt(it))
                    ws.deliverBuiltChunkMesh(it.requestId, std::move(it.mesh));
                if (done() || t.elapsed() > 30000)
                    break;
                QThread::msleep(1);
            }
        };
        geo.refreshMesh(); // 先行收口（setWorld 可能触发的提交一并排掉）——计数基线从稳态起算
        pumpUntil([&]() {
            return geo.vertexCount() > 0 && ws.pendingChunkMeshJobCount() == 0
                && wk.pendingCount() == 0 && wk.builtCount() == 0;
        });
        const bool settled = geo.vertexCount() > 0 && ws.pendingChunkMeshJobCount() == 0;
        ok = ok && settled;

        // 相 1：latest-snapshot-wins（编辑 A → worldChanged 自动提交 A → 编辑 B → 自动提交 B
        //   注销 A → 排干）。生产触发面 = 编辑的 onWorldChanged 链（setBlock → worldChanged →
        //   异步提交）——显式 refreshMesh 不再叠加提交（每次多余提交都会成为注销 miss 面）。
        const auto topA = findColumnTopInChunk(ws, 2, 2);
        const bool siteAOk = topA.first > 0 && ws.blockAt(topA.second.first, topA.first, topA.second.second) == 0;
        const qint64 applied0 = fp->countValue("meshNworker");
        const int dropped0 = ws.droppedBuiltMeshCount();
        bool winOk = false;
        if (siteAOk && settled) {
            const bool placeA = ws.setBlock(topA.second.first, topA.first, topA.second.second,
                                            quint8(BR::Stone), 0);
            // 提交 A 已由 worldChanged 沿自动发起
            const auto topB = findColumnTopInChunk(ws, 2, 2); // A 放置后仍能找到下一柱顶
            const bool siteBOk = topB.first > 0
                && (topB.second != topA.second || topB.first != topA.first);
            const bool placeB = siteBOk
                && ws.setBlock(topB.second.first, topB.first, topB.second.second,
                               quint8(BR::Cobble), 0);
            // 提交 B 已由 worldChanged 沿自动发起（注销 A——latest-snapshot-wins 路由层半边）
            pumpUntil([&]() {
                return (ws.droppedBuiltMeshCount() - dropped0) == 1
                    && (fp->countValue("meshNworker") - applied0) == 1;
            });
            ChunkMeshBakeParams bake;
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(ws), 2, 2, bake);
            const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
            winOk = placeA && placeB && ws.droppedBuiltMeshCount() - dropped0 == 1
                && fp->countValue("meshNworker") - applied0 == 1
                && geo.vertexCount() == mesh.vertexCount && geo.vertexData() == vtxBytes(mesh)
                && geo.indexData() == idxBytes(mesh) && ws.pendingChunkMeshJobCount() == 0
                && geo.vertexCount() > 0;
        }
        ok = ok && siteAOk && winOk;
        if (!siteAOk || !winOk)
            diag += QStringLiteral("[win siteA=%1 dropped=%2 applied=%3] ")
                        .arg(siteAOk)
                        .arg(ws.droppedBuiltMeshCount() - dropped0)
                        .arg(fp->countValue("meshNworker") - applied0);

        // 相 2：满载拒绝 → 内联回退（stub sink 恒 kErrQueueFull——拒绝面同 Result 路径）。
        //   先 clearMesh 清零（防相 1 终态网格与回退产物同态 mask 非空转——回退必须可观测地
        //   重新应用：顶点 0 → 同步产出 > 0）。
        ws.setChunkMeshBuildSink([](const ChunkMeshSnapshot &, quint64) {
            return Result<void>::fail(kErrQueueFull, "r2026c stub: queue full");
        });
        const qint64 fb0 = fp->countValue("meshNsyncFallback");
        geo.clearMesh();
        geo.refreshMesh();
        {
            ChunkMeshBakeParams bake;
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(ws), 2, 2, bake);
            const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
            const bool fullOk = fp->countValue("meshNsyncFallback") - fb0 == 1
                && geo.vertexCount() == mesh.vertexCount && geo.vertexCount() > 0
                && geo.vertexData() == vtxBytes(mesh) && geo.indexData() == idxBytes(mesh)
                && ws.pendingChunkMeshJobCount() == 0;
            ok = ok && fullOk;
            if (!fullOk)
                diag += QStringLiteral("[full fb=%1 vc=%2/%3] ")
                            .arg(fp->countValue("meshNsyncFallback") - fb0)
                            .arg(geo.vertexCount()).arg(mesh.vertexCount);
        }

        // 相 3：执行器已停拒绝 → 同款回退（kErrMeshWorkerStopped = 202 执行域；同款清零防 mask）。
        ws.setChunkMeshBuildSink([](const ChunkMeshSnapshot &, quint64) {
            return Result<void>::fail(kErrMeshWorkerStopped, "r2026c stub: executor stopped");
        });
        geo.clearMesh();
        geo.refreshMesh();
        {
            ChunkMeshBakeParams bake;
            const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(ws), 2, 2, bake);
            const ChunkMeshData mesh = MeshBuilder::build(snap, MeshBuilder::Reason::Dirty);
            const bool stoppedOk = fp->countValue("meshNsyncFallback") - fb0 == 2
                && geo.vertexCount() == mesh.vertexCount && geo.vertexData() == vtxBytes(mesh)
                && geo.indexData() == idxBytes(mesh);
            ok = ok && stoppedOk;
            if (!stoppedOk)
                diag += QStringLiteral("[stopped fb=%1] ")
                            .arg(fp->countValue("meshNsyncFallback") - fb0);
        }

        // 相 4：解绑 sink → 回固定全同步门（gate 不入 → 回退计数不动——按门同步非回退）。
        ws.setChunkMeshBuildSink({});
        {
            const qint64 fb1 = fp->countValue("meshNsyncFallback");
            const bool gateClosed = !ws.chunkMeshAsyncActive();
            geo.refreshMesh();
            const bool unboundOk = gateClosed
                && fp->countValue("meshNsyncFallback") - fb1 == 0 && geo.vertexCount() > 0;
            ok = ok && gateClosed && unboundOk;
            if (!gateClosed || !unboundOk)
                diag += QStringLiteral("[unbound gate=%1 fb=%2] ")
                            .arg(ws.chunkMeshAsyncActive())
                            .arg(fp->countValue("meshNsyncFallback") - fb1);
        }

        // 相 5：注销卫生——在途注册表经生产缝清空（refreshMesh 提交 → clearMesh 显式注销）。
        ws.setChunkMeshBuildSink(
            [&wk](const ChunkMeshSnapshot &s, quint64 id) { return wk.submit(s, id); });
        {
            geo.refreshMesh(); // 提交一个在途（worker 已排干 → 必被接受 → 注册表恰 1）
            const int pending1 = ws.pendingChunkMeshJobCount();
            geo.clearMesh(); // 生产缝：清空 = 显式注销在途（W4 clearMesh 注销半边）+ 置空（t972）
            pumpUntil([&]() {
                return ws.pendingChunkMeshJobCount() == 0 && wk.pendingCount() == 0
                    && wk.builtCount() == 0;
            });
            const bool hygOk = pending1 == 1
                && ws.pendingChunkMeshJobCount() == 0 && geo.vertexCount() == 0;
            ok = ok && hygOk;
            if (!hygOk)
                diag += QStringLiteral("[hyg pending1=%1 now=%2 v=%3] ")
                            .arg(pending1)
                            .arg(ws.pendingChunkMeshJobCount())
                            .arg(geo.vertexCount());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2026c fallback + hygiene: latest-snapshot-wins over a real"
                             " standalone executor (A cancelled at the registry = visible"
                             " drop, B applied bit-equal), queue-full and stopped-executor"
                             " refusals fall back inline synchronously with the fallback"
                             " counter visible and correct meshes, unbinding the sink"
                             " restores the plain synchronous gate without fallback"
                             " counting, and cancel/clear hygiene empties the routing"
                             " registry"
                          << (ok ? QString() : diag);
    });

    // ── r2026d：结构钉（F3 新列正面钉 + 行为级 flush 报告钉 + QML 零触碰 + 单一权威兼容反探
    //    + 收割拍单点收口 + fixed 零构造源序钉 + World 桥面钉）─────────────────────────────
    runLeg(QStringLiteral("r2026d structure pins (the F3 win-line gains a worker column right"
        " after the mesh bracket - positive source pins on the format expression and the"
        " meshNworker counter in both frameprofiler.cpp and chunkgeometry.cpp, plus a"
        " behavioral flush-report pin 'w])  worker 0  world' on a quiescent window; QML"
        " zero-touch reverse probes [Main.qml carries none of the bridge method names,"
        " still no executor token, pool wiring intact]; single-authority compatibility"
        " reverse probes on chunkgeometry.cpp [no mesh-body tokens flowed back, the"
        " extraction delegate + capture lines still stand x2]; the harvest beat is a single"
        " drain point [takeBuilt callers across src/ are exactly meshworker.h +"
        " gamesession.h]; the fixed-world zero-construction gate is pinned by source"
        " order [make_unique sits after the isSparse gate]; and the World bridge face is"
        " pinned in world.h/world.cpp token-free)"), [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① 正面钉（pinSet 剥注释；minCount 对实际出现次数核）。
        const QStringList missGs = pinSet(
            srcRoot + QStringLiteral("/Game/gamesession.h"), {
                SrcPin("r2026 executor constructed", "std::make_unique<MeshWorker>()", 1),
                SrcPin("r2026 bridge submit seam", "setChunkMeshBuildSink", 1),
                SrcPin("r2026 executor submit forward", "m_meshWorker->submit(snap, requestId)", 1),
                SrcPin("r2026 harvest drain", "takeBuilt(built)", 1),
                SrcPin("r2026 deliver route", "deliverBuiltChunkMesh(built.requestId, std::move(built.mesh))", 1),
                SrcPin("r2026 harvest counter", "++m_meshHarvestedCount", 1),
            });
        const QStringList missCg = pinSet(
            srcRoot + QStringLiteral("/World/chunkgeometry.cpp"), {
                SrcPin("r2026 async submit half", "bool ChunkGeometry::submitMeshJobAsync(RebuildReason reason)", 1),
                SrcPin("r2026 async submit entry", "submitMeshJobAsync(reason)", 1),
                SrcPin("r2026 shared apply", "applyChunkMeshData", 3),
                SrcPin("r2026 request id derive", "deriveMeshJobRequestId", 2),
                SrcPin("r2026 worker column counter", "meshNworker", 1),
                SrcPin("r2026 fallback counter", "meshNsyncFallback", 1),
                SrcPin("r2026 stale counter", "meshNstale", 1),
                SrcPin("r2026 latest-wins gate", "deliveredId != self->m_pendingRequestId", 1),
                SrcPin("r2026 pointer guard", "QPointer<ChunkGeometry> self(this)", 1),
            });
        const QStringList missFp = pinSet(
            srcRoot + QStringLiteral("/Core/frameprofiler.cpp"), {
                SrcPin("r2026 win-line worker column", "+ \"  worker \" + QString::number(meshNWorker)", 1),
                SrcPin("r2026 worker column counter read", "meshReasonN(\"meshNworker\")", 1),
            });
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("r2026 bridge gate", "bool chunkMeshAsyncActive() const { return bool(m_chunkMeshBuildSink); }", 1),
                SrcPin("r2026 bridge submit decl", "Result<void> submitChunkMeshJob(const ChunkMeshSnapshot &snap, quint64 requestId,", 1),
                SrcPin("r2026 bridge deliver decl", "void deliverBuiltChunkMesh(quint64 requestId, ChunkMeshData &&mesh)", 1),
                SrcPin("r2026 bridge cancel decl", "void cancelChunkMeshJob(quint64 requestId)", 1),
                SrcPin("r2026 bridge dropped read", "int droppedBuiltMeshCount() const", 1),
            });
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("r2026 submit impl", "Result<void> World::submitChunkMeshJob", 1),
                SrcPin("r2026 deliver impl", "void World::deliverBuiltChunkMesh", 1),
                SrcPin("r2026 cancel impl", "void World::cancelChunkMeshJob", 1),
            });
        for (const QString &m : missGs + missCg + missFp + missWh + missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 固定零构造源序钉：执行器构造在 isSparse 门之后（裸 indexOf——构造缝 D2 墙的形态面）。
        QFile gsFile(srcRoot + QStringLiteral("/Game/gamesession.h"));
        QString gsRaw;
        if (gsFile.open(QIODevice::ReadOnly))
            gsRaw = QString::fromUtf8(gsFile.readAll());
        const int gatePos = gsRaw.indexOf(QStringLiteral("if (m_world.isSparse()) {"));
        const int mkPos = gsRaw.indexOf(QStringLiteral("std::make_unique<MeshWorker>()"));
        const bool orderOk = gatePos >= 0 && mkPos > gatePos;
        ok = ok && orderOk;
        if (!orderOk) diag += QStringLiteral("[order gate=%1 mk=%2] ").arg(gatePos).arg(mkPos);

        // ③ 收割拍单点收口：全 src 树 takeBuilt( 调用面 = {meshworker.h 组件本体, gamesession.h
        //    收割拍} 两文件恰尽（世界/几何/QML 零第二收割点）。
        QStringList takeBuiltFiles;
        QDirIterator itTb(srcRoot, { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                          QDir::Files, QDirIterator::Subdirectories);
        while (itTb.hasNext()) {
            QFile f(itTb.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("takeBuilt(")))
                takeBuiltFiles << QDir(srcRoot).relativeFilePath(itTb.filePath());
        }
        takeBuiltFiles.sort();
        const bool singleDrainOk = takeBuiltFiles.size() == 2
            && takeBuiltFiles.contains(QStringLiteral("World/meshworker.h"))
            && takeBuiltFiles.contains(QStringLiteral("Game/gamesession.h"));
        ok = ok && singleDrainOk;
        if (!singleDrainOk) diag += QStringLiteral("[drain %1] ").arg(takeBuiltFiles.join(QLatin1Char(',')));

        // ④ QML 零触碰反探 + 执行器/桥记号反探（裸 contains——注释也算，r2020d 同口径）。
        QFile qmlFile(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/.."))
                           .absoluteFilePath(QStringLiteral("src/ui/Main.qml")));
        QString qml;
        if (qmlFile.open(QIODevice::ReadOnly))
            qml = QString::fromUtf8(qmlFile.readAll());
        const bool qmlOk = !qml.contains(QStringLiteral("eshWorker"))
            && !qml.contains(QStringLiteral("deliverBuiltChunkMesh"))
            && !qml.contains(QStringLiteral("submitChunkMeshJob"))
            && !qml.contains(QStringLiteral("chunkMeshAsyncActive"))
            && !qml.contains(QStringLiteral("meshNworker"))
            && qml.contains(QStringLiteral("rebuildChunkSlotPool")); // P4 池消费面原样在位
        ok = ok && qmlOk;
        if (!qmlOk) diag += QStringLiteral("[qml] ");

        // ⑤ World/chunkgeometry 记号零出（桥面 token-free——执行器类型零泄漏进 World/Renderer）。
        const auto tokenAbsent = [&srcRoot](const QString &rel) {
            QFile f(srcRoot + QStringLiteral("/") + rel);
            return !f.open(QIODevice::ReadOnly)
                || !QString::fromUtf8(f.readAll()).contains(QStringLiteral("MeshWorker"));
        };
        const bool tokenOk = tokenAbsent(QStringLiteral("/World/world.h"))
            && tokenAbsent(QStringLiteral("/World/world.cpp"))
            && tokenAbsent(QStringLiteral("/World/chunkgeometry.h"))
            && tokenAbsent(QStringLiteral("/World/chunkgeometry.cpp"));
        ok = ok && tokenOk;
        if (!tokenOk) diag += QStringLiteral("[token] ");

        // ⑥ 单一权威兼容反探（r2013d 复述面）：网格本体禁回流 chunkgeometry.cpp。
        const QStringList missRev = pinSet(
            srcRoot + QStringLiteral("/World/chunkgeometry.cpp"), {
                SrcPin("forbidden kFaces", "static const FaceDef kFaces", 1),
                SrcPin("forbidden occlude", "occludesNeighborFace", 1),
                SrcPin("forbidden mask", "struct MaskEntry", 1),
            });
        const bool revOk = missRev.size() == 3
            && !missRev.first().startsWith(QStringLiteral("<file-unreadable"));
        ok = ok && revOk;
        if (!revOk) diag += QStringLiteral("[rev] ");

        // ⑦ F3 新列行为级正面钉：静默窗 flush 报告含 'w])  worker 0  world'（列恒在——fixed
        //    世界恒 0 的逐字段口径；既有 mesh/world 段文本逐字保留）。
        FrameProfiler *fp = FrameProfiler::instance();
        fp->flush(); // 清窗
        fp->tickFrame();
        fp->flush(); // 静默窗报告
        const bool f3Ok = fp->report().contains(QStringLiteral("w])  worker 0  world"));
        ok = ok && f3Ok;
        if (!f3Ok) diag += QStringLiteral("[f3] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2026d structure pins: the F3 win-line worker column is"
                             " positively pinned at source and behavior (quiescent flush"
                             " reports 'w]) worker 0  world'), QML stays zero-touch, the"
                             " harvest beat is the single drain point across src/, the"
                             " fixed-world zero-construction gate holds by source order,"
                             " the World bridge stays token-free, and no mesh logic flowed"
                             " back into chunkgeometry.cpp"
                          << (ok ? QString() : diag);
    });
}
