#include "matrix_helpers.h"

#include "meshbuilder.h" // R20.13 对照权威：MeshBuilder::build / ChunkMeshSnapshot / ChunkMeshData
#include "meshworker.h"  // r2020 被测：MeshWorker（D6 worker meshing 线程化执行器，生产零接线）

#include <QElapsedTimer>
#include <QVector3D>

#include <memory>
#include <thread>
#include <vector>

// D6 worker meshing 探针段（4 腿 r2020a-d；filter 词 "r2020"；矩阵 616→620）。置尾先例沿用
//（接 section22，runAll 末执行）。任务契约（用户开工单 D6「worker meshing 与流式同批排期」+
// §29.4 设计草案 R 节「meshing 瓶颈」风险条目）：组件先行零生产接线——MeshWorker
//（src/World/meshworker.h，header-only 非 QObject，r2012 backgroundgeneration.h 同门形态）消费
// ChunkMeshSnapshot 值、worker 线程唯一执行 MeshBuilder::build（R20.13 单一权威）、收割面交还
// owning ChunkMeshData；ChunkGeometry 现行同步 bake 路径零触碰（零接线由 r2020d 反探钉守）。
// 腿面：
//   r2020a 确定性等价承重墙：8 个形态各异快照（世界采集 inland/edge/waterOnly/greedy+AO +
//     手写异形 cross/流体水池/重负棋盘 + 空快照）逐快照 worker build 与主线程同步直调
//     MeshBuilder::build 输出逐位恒等（顶点缓冲/索引缓冲/统计三面）——单一权威不回流的实证面
//     （worker 执行体零加工零重排的透传性）；非空快照非空转（vertexCount>0）+ 空快照双路同空；
//   r2020b 队列纪律：容量压小（cap=0 退化确定性满载 + cap=2 重负在途拦截的满载）→ 满载
//     kErrQueueFull 可见拒绝、拒绝项不入账（acceptedCount 不含被拒项）；接受项恰好各产出一条
//     built（按 requestId 逐项对账，零丢零重）；收割后队清（pending/built 归零）；
//   r2020c 析构卫生：积压在途时析构（重负在途 + 2 排队）→ join 按时返回（deadline 断言，不挂
//     死）、丢弃经 teardownDroppedSink 测试缝可见（sink==2 恰等于析构时排队数——在途一项跑完
//     不入丢弃账）、析构后零回调零输出（结构性：组件无任何回调/信号面，r2020d 源码反探守）；
//     再构造再用（可重建性：全新实例全周期 accept→build→harvest 恰一对账）；
//   r2020d 结构钉 + 白名单修订自证 + 零接线反探 + 线程身份：meshworker.h 正面钉（submit/
//     takeBuilt/容量 64/单一权威执行体/线程原语/cv 唤醒/QObjectFree 钉/析构丢弃计数）+ 反探
//     （零 QObject 面/零 Qt 线程设施/零网格逻辑回流记号/零 World 读面）+ 修订后 t1023c 双文件
//     白名单的段内自证 sweep（thread 记号恰在 backgroundgeneration.h + meshworker.h 两落点，各
//     ≥1）+ 零生产接线（「MeshWorker」记号全 src 树只许出现在 meshworker.h；chunkgeometry.{h,cpp}
//     显式反探；Main.qml 零记号）+ 线程身份对账（执行线程 == 工作线程 != 调用者线程，r2012 同款）。
// 恰红面设计（先于腿文定稿；R20.11 纪律）：
//   NEG-1 摘「接受项恰一产出」（worker 侧 built 遗漏一条——threadLoop 产出 push 加 requestId
//     条件跳过）→ 恰红 r2020b（phase③ ids 20..31 对账缺一；r2020a ids 1..8 不含 20 不误伤，
//     r2020c/d 对账面不含 20 不误伤——恰红面收缩单腿）；
//   NEG-2 让 worker 执行体加工输出（build 返回后翻转首顶点颜色域 = 模拟「顺手复制/改写网格
//     逻辑」）→ 恰红 r2020a（逐位恒等墙破；r2020b/c 只对账 id 不比字节不误伤，r2020d 源码钉
//     不含该行记号不误伤——恰红面收缩单腿）。
// 防 flake（r2012 先例）：r2020a-c 三连跑全绿才算过。确定性设计：满载拒绝面靠「重负棋盘快照
//   在途拦截」（单 worker + 重负构建 ≫ 提交序列的微秒窗，三连跑验证）；全部断言为确定性终态
//   （逐位字节比对 / id 集合对账 / 计数器终值），无墙钟比值断言（仅析构 join 上界 deadline）。
// rig 世界 w 零接触（r2020a 自建 fresh 48×48×96 s82 小世界 + 天气双钉，section11+ 先例；
//   r2020b/c/d 零世界——手写快照 + 纯源码钉）。
void MatrixRun::section23_meshworker()
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
    // 字节视图帮手（r2013 同款：owning ChunkMeshData → 48B/Vtx 顶点 + 4B/quint32 索引）。
    const auto vtxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.vertices.constData()),
                          int(m.vertices.size() * int(sizeof(Vtx))));
    };
    const auto idxBytes = [](const ChunkMeshData &m) {
        return QByteArray(reinterpret_cast<const char *>(m.indices.constData()),
                          int(m.indices.size() * int(sizeof(quint32))));
    };
    // 空气袋选址（r2013 同款：chunk(1,1) 内部带，自格 + 6 邻全空气）。
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

    // ── 手写快照 rig（形态各异；域布局见 meshbuilder.h——pad=2 方域 20×20×H + 列顶 21×21）──
    const auto newSnap = [](int height, bool shadows) -> ChunkMeshSnapshot {
        ChunkMeshSnapshot s;
        s.height = height;
        s.shadowsEnabled = shadows;
        s.sunDir = QVector3D(0.4f, 0.6f, 0.4f); // 满影带（y≥0.40）→ PCF 列顶路径确定性进入
        const size_t n = size_t(ChunkMeshSnapshot::kDim) * size_t(ChunkMeshSnapshot::kDim)
            * size_t(height > 0 ? height : 0);
        s.blocks.assign(n, 0);
        s.states.assign(n, 0);
        s.skyLight.assign(n, 15);
        s.blockLight.assign(n, 0);
        s.columnTop.assign(size_t(ChunkMeshSnapshot::kTopDim) * size_t(ChunkMeshSnapshot::kTopDim), -1.0f);
        return s;
    };
    const auto setCell = [](ChunkMeshSnapshot &s, int lx, int ly, int lz, quint8 id, quint8 st,
                            quint8 sky) {
        const size_t i = ChunkMeshSnapshot::cellIndex(lx, ly, lz);
        s.blocks[i] = id;
        s.states[i] = st;
        s.skyLight[i] = sky;
    };
    const auto setTop = [](ChunkMeshSnapshot &s, int lx, int lz, float top) {
        s.columnTop[ChunkMeshSnapshot::topIndex(lx, lz)] = top;
    };
    // 异形/cross + 半透 rig：Stone 地板 + TallGrass cross 丛（PASS1 异形合批路径）+ 相邻双
    // Glass（半透面自剔/邻实体剔路径）。
    const auto makePartialSnap = [&]() {
        ChunkMeshSnapshot s = newSnap(24, true);
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz) {
                setCell(s, lx, 0, lz, quint8(BR::Stone), 0, 15);
                setTop(s, lx, lz, 1.0f);
            }
        const int grass[5][2] = { { 2, 3 }, { 5, 5 }, { 8, 2 }, { 11, 9 }, { 13, 13 } };
        for (const auto &g : grass)
            setCell(s, g[0], 1, g[1], quint8(BR::TallGrass), 0, 15);
        setCell(s, 4, 6, 4, quint8(BR::Glass), 0, 15);
        setCell(s, 5, 6, 4, quint8(BR::Glass), 0, 15);
        return s;
    };
    // 流体水池 rig：石壁池 + 内部变 state 水体（PASS2 流体变高面/流向路径）+ 单点 Lava（流体
    // id 分流对照）。
    const auto makeFluidSnap = [&]() {
        ChunkMeshSnapshot s = newSnap(24, true);
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz) {
                setCell(s, lx, 0, lz, quint8(BR::Stone), 0, 15);
                setTop(s, lx, lz, 1.0f);
            }
        for (int i = 2; i <= 13; ++i) {
            setCell(s, i, 1, 2, quint8(BR::Stone), 0, 15);
            setCell(s, i, 1, 13, quint8(BR::Stone), 0, 15);
            setCell(s, 2, 1, i, quint8(BR::Stone), 0, 15);
            setCell(s, 13, 1, i, quint8(BR::Stone), 0, 15);
        }
        for (int lx = 3; lx <= 12; ++lx)
            for (int lz = 3; lz <= 12; ++lz)
                setCell(s, lx, 1, lz, quint8(BR::Water), quint8((lx + lz) % 3), 15);
        setCell(s, 6, 2, 6, quint8(BR::Lava), 0, 15);
        return s;
    };
    // 重负棋盘快照（确定性满载拦截 / 析构卫生的在途拦截体）：16×16 半数实心棋盘柱 × 高 256
    // ——全部 6 邻空气 → 每实心格 6 面全见（~19.7 万面 / ~78.6 万顶点，阴影开 → PCF 逐顶点），
    // 单次构建数十毫秒 ≫ 提交序列微秒窗与轮询粒度（防 flake 的时序承重体，见头注；首跑 96 高
    // 版本构建过快 + Windows msleep ~15.6ms 粒度曾整窗漏过瞬态 pending 窗，即此两路加固）。
    const auto makeMonsterSnap = [&]() {
        constexpr int mH = 256;
        ChunkMeshSnapshot s = newSnap(mH, true);
        for (int ly = 0; ly < mH; ++ly)
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                    if (((lx + lz) % 2) == 0)
                        setCell(s, lx, ly, lz, quint8(BR::Stone), 0, 15);
        for (int lz = ChunkMeshSnapshot::kTopLo; lz < ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim; ++lz)
            for (int lx = ChunkMeshSnapshot::kTopLo; lx < ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim; ++lx)
                setTop(s, lx, lz,
                       (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && ((lx + lz) % 2) == 0) ? float(mH) : -1.0f);
        return s;
    };
    // 有界收割帮手：deadline 内轮询 takeBuilt（ms 级构建 ≫ 轮询粒度；deadline 只作挂死护栏）。
    const auto harvestOne = [](MeshWorker &wk, MeshBuiltItem &out, int deadlineMs) -> bool {
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < deadlineMs) {
            if (wk.takeBuilt(out))
                return true;
            QThread::msleep(1);
        }
        return wk.takeBuilt(out);
    };
    // 瞬态窗等待帮手：busy-yield 轮询（µs 级粒度——Windows QThread::msleep(1) 实际粒度
    //   ~15.6ms，首跑曾把「在途构建时长」级的瞬态 pending 窗整窗漏过）。在途拦截窗 = 重负快照
    //   构建时长（数十毫秒）≫ 本轮询粒度。
    const auto waitPending = [](MeshWorker &wk, int want, int deadlineMs) -> bool {
        QElapsedTimer tm;
        tm.start();
        for (;;) {
            if (wk.pendingCount() == want)
                return true;
            if (tm.elapsed() >= deadlineMs)
                return wk.pendingCount() == want;
            std::this_thread::yield();
        }
    };

    // ── r2020a：确定性等价承重墙（worker ≡ 同步直调逐位恒等——单一权威不回流的实证面）──────
    runLeg(QStringLiteral("r2020a determinism wall: eight shape-diverse snapshots (world-captured"
        " inland / world-edge / water-segment / greedy+AO bakes plus hand-built partial-cross,"
        " fluid-tank, heavy-checkerboard and the empty snapshot) are each built BOTH by the"
        " MeshWorker executor thread and by a synchronous direct MeshBuilder::build call on"
        " the caller thread, and the two outputs agree bit-for-bit on all three faces -"
        " vertex buffer, index buffer and the vertex/triangle stats - proving the worker is"
        " a pure passthrough of the single meshing authority (zero processing, zero"
        " reordering, no copied mesh logic); non-empty snapshots are non-vacuous (positive"
        " vertex counts) and the empty snapshot is empty on BOTH paths"), [&]() {
        bool ok = true;
        QString diag;

        // 形态各异快照集：4 个世界采集（真实地形/边界/流体段/贪婪+AO）+ 3 个手写（异形/流体/
        //   重负）+ 1 个空快照（height=0 → build 空输出语义）。
        struct Case
        {
            const char *name;
            ChunkMeshSnapshot snap;
            bool expectNonEmpty;
        };
        std::vector<Case> cases;
        {
            World wA;
            initTwin(wA);
            bool rigOk = true;
            const BR::Id rigIds[4] = { BR::Glass, BR::Ice, BR::Water, BR::Lava };
            for (int i = 0; i < 4 && rigOk; ++i) {
                const QPair<int, QPair<int, int>> p = findAirPocket(wA, i);
                rigOk = p.first >= 0 && wA.setBlock(p.second.first, p.first, p.second.second,
                                                    quint8(rigIds[i]), 0);
            }
            ok = ok && rigOk;
            if (!rigOk) diag += QStringLiteral("[rig] ");
            ChunkMeshBakeParams bake; // terrain 默认
            cases.push_back(Case{ "terrain-inland", captureChunkMeshSnapshot(WorldFacade(wA), 1, 1, bake), true });
            cases.push_back(Case{ "terrain-edge", captureChunkMeshSnapshot(WorldFacade(wA), 0, 0, bake), true });
            ChunkMeshBakeParams bakeW;
            bakeW.waterOnly = true;
            cases.push_back(Case{ "water-segment", captureChunkMeshSnapshot(WorldFacade(wA), 1, 1, bakeW), true });
            ChunkMeshBakeParams bakeGA;
            bakeGA.greedyMeshing = true;
            bakeGA.aoEnabled = true;
            cases.push_back(Case{ "greedy-ao", captureChunkMeshSnapshot(WorldFacade(wA), 1, 1, bakeGA), true });
        }
        cases.push_back(Case{ "partial-cross", makePartialSnap(), true });
        cases.push_back(Case{ "fluid-tank", makeFluidSnap(), true });
        cases.push_back(Case{ "heavy-checkerboard", makeMonsterSnap(), true });
        cases.push_back(Case{ "empty", ChunkMeshSnapshot{}, false });

        // 快照自持性前置（采集/手写合法；空快照刻意 invalid——build 空输出语义面）。
        for (size_t i = 0; i + 1 < cases.size(); ++i) {
            const bool v = cases[i].snap.valid();
            ok = ok && v;
            if (!v)
                diag += QStringLiteral("[snap %1 invalid] ").arg(QLatin1String(cases[i].name));
        }

        // 同步对照（主线程直调单一权威）先行定格，再全部交 worker 线程执行。
        std::vector<ChunkMeshData> syncMeshes;
        for (const Case &c : cases)
            syncMeshes.push_back(MeshBuilder::build(c.snap, MeshBuilder::Reason::Dirty));

        MeshWorker wk;
        bool subOk = true;
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto r = wk.submit(cases[i].snap, quint64(i + 1)); // requestId 1..8
            subOk = subOk && r.isOk();
        }
        ok = ok && subOk;
        if (!subOk) diag += QStringLiteral("[submit] ");

        // 逐条收割（FIFO 提交序）+ 三面逐位对账 + requestId 零丢零重。
        std::vector<bool> seen(cases.size(), false);
        int got = 0;
        bool cmpOk = true;
        for (size_t i = 0; i < cases.size(); ++i) {
            MeshBuiltItem item;
            if (!harvestOne(wk, item, 60000)) {
                cmpOk = false;
                diag += QStringLiteral("[harvest timeout after %1] ").arg(int(i));
                break;
            }
            ++got;
            const quint64 id = item.requestId;
            if (id < 1 || id > cases.size() || seen[size_t(id - 1)]) {
                cmpOk = false;
                diag += QStringLiteral("[id %1 dup/oor] ").arg(id);
                break;
            }
            seen[size_t(id - 1)] = true;
            const ChunkMeshData &m = syncMeshes[size_t(id - 1)];
            const bool eq = item.mesh.vertexCount == m.vertexCount
                && item.mesh.triangleCount == m.triangleCount
                && vtxBytes(item.mesh) == vtxBytes(m) && idxBytes(item.mesh) == idxBytes(m);
            const bool nonEmptyOk = cases[size_t(id - 1)].expectNonEmpty
                ? (m.vertexCount > 0 && item.mesh.vertexCount > 0)
                : (m.vertexCount == 0 && m.triangleCount == 0 && item.mesh.vertexCount == 0
                   && item.mesh.triangleCount == 0);
            if (!eq || !nonEmptyOk) {
                cmpOk = false;
                diag += QStringLiteral("[case %1 eq=%2 ne=%3 vc %4/%5] ")
                            .arg(QLatin1String(cases[size_t(id - 1)].name))
                            .arg(eq).arg(nonEmptyOk).arg(item.mesh.vertexCount).arg(m.vertexCount);
            }
        }
        bool ledgerOk = got == int(cases.size());
        for (bool b : seen)
            ledgerOk = ledgerOk && b;
        ledgerOk = ledgerOk && wk.acceptedCount() == quint64(cases.size())
            && wk.droppedCount() == 0 && wk.pendingCount() == 0 && wk.builtCount() == 0;
        ok = ok && cmpOk && ledgerOk;
        if (!ledgerOk) diag += QStringLiteral("[ledger got=%1] ").arg(got);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2020a determinism wall: eight shape-diverse snapshots built"
                             " through the MeshWorker executor thread and a synchronous direct"
                             " MeshBuilder::build call agree bit-for-bit on the vertex buffer,"
                             " the index buffer and the vertex/triangle stats (pure passthrough"
                             " of the single meshing authority - zero processing, zero"
                             " reordering), non-empty snapshots are non-vacuous, and the empty"
                             " snapshot is empty on both paths"
                          << (ok ? QString() : diag);
    });

    // ── r2020b：队列纪律（满载拒绝可见 + 拒绝项不入账 + 接受项恰一产出 + 收割后队清）────────
    runLeg(QStringLiteral("r2020b queue discipline: a squeezed capacity-0 queue rejects every"
        " submission with the visible kErrQueueFull code and accounts nothing (zero accepted,"
        " zero pending, zero built, nothing harvestable); a capacity-2 queue with a heavy"
        " checkerboard build in-flight deterministically saturates - two queued requests are"
        " accepted and the third is rejected with kErrQueueFull (rejected request never"
        " enters the ledger and never produces a built item) - and every ACCEPTED request"
        " produces exactly one built item reconciled by requestId ({10,11,12}, zero loss,"
        " zero duplicates, FIFO submission order) with the queues fully drained after the"
        " harvest; and a default capacity-64 worker takes a 12-request batch with each"
        " accepted request producing exactly one built item ({20..31}) and zero drops"), [&]() {
        bool ok = true;
        QString diag;

        // ① 退化容量 0（确定性满载面）：每提交必 kErrQueueFull；拒绝项不入账、零产出。
        const ChunkMeshSnapshot quick = newSnap(8, false);
        {
            MeshWorker wk0(nullptr, 0);
            const auto r1 = wk0.submit(quick, 100);
            const auto r2 = wk0.submit(quick, 101);
            MeshBuiltItem item;
            const bool full0 = !r1.isOk() && !r2.isOk()
                && r1.error().code == kErrQueueFull && r2.error().code == kErrQueueFull
                && wk0.acceptedCount() == 0 && wk0.pendingCount() == 0
                && wk0.builtCount() == 0 && wk0.droppedCount() == 0 && !wk0.takeBuilt(item);
            ok = ok && full0;
            if (!full0)
                diag += QStringLiteral("[cap0 r1=%1 r2=%2 acc=%3] ")
                            .arg(r1.isOk()).arg(r2.isOk()).arg(wk0.acceptedCount());
        }

        // ② 非零容量压小（cap=2）+ 重负在途拦截：确定性满载拒绝 + 恰一产出对账。
        const ChunkMeshSnapshot monster = makeMonsterSnap();
        {
            MeshWorker wkA(nullptr, 2);
            const auto rM = wkA.submit(monster, 10);
            // 等 M 出队入途（单 worker：pending 归零 ⟺ M 已被弹出在途；重负构建 ≫ 轮询粒度）。
            const bool inFlight = rM.isOk() && waitPending(wkA, 0, 10000);
            const auto r1 = wkA.submit(quick, 11);
            const auto r2 = wkA.submit(quick, 12);
            const auto r3 = wkA.submit(quick, 13); // 队 {11,12} 满载 → 可见拒绝（M 在途拦截）
            const bool satOk = inFlight && r1.isOk() && r2.isOk() && !r3.isOk()
                && r3.error().code == kErrQueueFull
                && wkA.acceptedCount() == 3 // 拒绝项（13）不入账
                && wkA.pendingCount() == 2;
            ok = ok && satOk;
            if (!satOk)
                diag += QStringLiteral("[sat inf=%1 r1=%2 r2=%3 r3=%4(code=%5) acc=%6 pend=%7] ")
                            .arg(inFlight).arg(r1.isOk()).arg(r2.isOk()).arg(r3.isOk())
                            .arg(r3.error().code).arg(wkA.acceptedCount()).arg(wkA.pendingCount());

            // 恰一产出对账：FIFO 收割恰 3 条 {10,11,12}，13 永不出现；收割后队清。
            std::vector<bool> seen3(3, false);
            int got = 0;
            bool recOk = true;
            for (int i = 0; i < 3; ++i) {
                MeshBuiltItem item;
                if (!harvestOne(wkA, item, 60000)) {
                    recOk = false;
                    diag += QStringLiteral("[harvest timeout %1] ").arg(i);
                    break;
                }
                ++got;
                if (item.requestId < 10 || item.requestId > 12 || seen3[size_t(item.requestId - 10)]) {
                    recOk = false;
                    diag += QStringLiteral("[id %1 dup/oor] ").arg(item.requestId);
                    break;
                }
                seen3[size_t(item.requestId - 10)] = true;
            }
            MeshBuiltItem extra;
            const bool drainOk = recOk && got == 3 && !wkA.takeBuilt(extra)
                && wkA.pendingCount() == 0 && wkA.builtCount() == 0 && wkA.droppedCount() == 0;
            ok = ok && drainOk;
            if (!drainOk) diag += QStringLiteral("[drain got=%1] ").arg(got);
        }

        // ③ 默认容量 64 批量恰一产出：12 连发全受理（队面永不及上界 → 确定性接受），收割恰
        //    12 条 {20..31} 零丢零重，零丢弃。
        {
            MeshWorker wkB; // 默认 cap 64（生产口径）
            bool subOk = true;
            for (int i = 0; i < 12; ++i)
                subOk = subOk && wkB.submit(newSnap(6, false), quint64(20 + i)).isOk();
            std::vector<bool> seen12(12, false);
            int got = 0;
            bool recOk = subOk;
            if (!subOk) diag += QStringLiteral("[batch submit] ");
            for (int i = 0; i < 12 && recOk; ++i) {
                MeshBuiltItem item;
                if (!harvestOne(wkB, item, 60000)) {
                    recOk = false;
                    diag += QStringLiteral("[batch harvest timeout %1] ").arg(i);
                    break;
                }
                ++got;
                if (item.requestId < 20 || item.requestId > 31
                    || seen12[size_t(item.requestId - 20)]) {
                    recOk = false;
                    diag += QStringLiteral("[batch id %1 dup/oor] ").arg(item.requestId);
                    break;
                }
                seen12[size_t(item.requestId - 20)] = true;
            }
            const bool batchOk = recOk && got == 12 && wkB.acceptedCount() == 12
                && wkB.droppedCount() == 0 && wkB.pendingCount() == 0 && wkB.builtCount() == 0;
            ok = ok && batchOk;
            if (!batchOk) diag += QStringLiteral("[batch got=%1] ").arg(got);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2020b queue discipline: the capacity-0 squeeze rejects every"
                             " submit with visible kErrQueueFull and accounts nothing; the"
                             " capacity-2 queue with a heavy in-flight build saturates"
                             " deterministically (two accepted, the third visibly rejected,"
                             " rejected request unaccounted and never built); every accepted"
                             " request produced exactly one built item (requestId-reconciled,"
                             " zero loss, zero duplicates) with queues drained after harvest;"
                             " the default capacity-64 worker ran a 12-request batch with"
                             " exactly-once outputs and zero drops"
                          << (ok ? QString() : diag);
    });

    // ── r2020c：析构卫生（积压在途析构 → join 有界 + 丢弃可见 + 零回调零输出 + 可重建性）────
    runLeg(QStringLiteral("r2020c destructor hygiene: destroying a worker with a heavy build"
        " in-flight plus a saturated two-request backlog (the third submit's visible"
        " kErrQueueFull rejection is the deterministic in-flight proof) returns within a"
        " generous deadline (bounded join, never stuck - the in-flight build completes, the"
        " backlog is discarded), the discard is VISIBLE through the teardown dropped-sink"
        " seam (sink == exactly the two queued requests; the in-flight one is not a drop -"
        " its built item was produced), and after destruction there is zero callback and"
        " zero output (structural: the component has no callback or signal surface at all -"
        " pinned by the r2020d source probes); a freshly constructed worker is immediately"
        " reusable through a full accept->build->harvest cycle with exactly-once"
        " reconciliation and clean counters"), [&]() {
        bool ok = true;
        QString diag;

        // ① 积压在途析构：sink 哨先写毒值 → 析构必须覆写（丢弃计数可见性的证据形态）。
        //    饱和型积压构造（cap=2，r2020b② 同门）：重负 M 出队入途后连发三条——第 3 条可见
        //    拒绝即「M 在途 + 队 {41,42} 满」的确定性证明（不追瞬态 pending 窗），此刻析构 →
        //    M 在途跑完（非丢弃）、{41,42} 丢弃入账，sink 恰 == 2。
        const ChunkMeshSnapshot monster = makeMonsterSnap();
        const ChunkMeshSnapshot quick = newSnap(8, false);
        quint64 sink = 0xDEADBEEFull;
        qint64 dtorMs = -1;
        bool observed = false;
        {
            std::unique_ptr<MeshWorker> wk(new MeshWorker(&sink, 2)); // 测试缝外哨 + 容量压小（生产零挂载/零参）
            const auto rM = wk->submit(monster, 40);
            const bool inFlight = rM.isOk() && waitPending(*wk, 0, 10000); // pending 归零 ⟺ M 出队入途
            const auto r1 = wk->submit(quick, 41);
            const auto r2 = wk->submit(quick, 42);
            const auto r3 = wk->submit(quick, 43); // 队 {41,42} 满载 → 可见拒绝（M 在途拦截）
            observed = inFlight && r1.isOk() && r2.isOk() && !r3.isOk()
                && r3.error().code == kErrQueueFull;
            QElapsedTimer dt;
            dt.start();
            wk.reset(); // 析构：stop + 丢弃计数（{41,42} 入账）+ join（M 跑完，有界返回）
            dtorMs = dt.elapsed();
        }
        const bool dtorOk = observed && dtorMs >= 0 && dtorMs < 30000 // join 按时返回（不挂死）
            && sink == 2; // 恰等于析构时排队数：{41,42} 可见丢弃；在途 M 非丢弃（产出已发生）
        ok = ok && dtorOk;
        if (!dtorOk)
            diag += QStringLiteral("[dtor obs=%1 ms=%2 sink=%3] ").arg(observed).arg(dtorMs).arg(sink);

        // ② 可重建性：全新实例即刻全周期可用（恰一对账 + 计数器从零起账）。
        bool recOk = true;
        {
            MeshWorker wk2; // 无外哨（nullptr 默认 = 纯行为组件）、默认容量
            std::vector<bool> seen3(3, false);
            int got = 0;
            for (int i = 0; i < 3; ++i)
                recOk = recOk && wk2.submit(quick, quint64(50 + i)).isOk();
            for (int i = 0; i < 3 && recOk; ++i) {
                MeshBuiltItem item;
                if (!harvestOne(wk2, item, 60000)) {
                    recOk = false;
                    diag += QStringLiteral("[rebuild harvest timeout %1] ").arg(i);
                    break;
                }
                ++got;
                if (item.requestId < 50 || item.requestId > 52 || seen3[size_t(item.requestId - 50)]) {
                    recOk = false;
                    diag += QStringLiteral("[rebuild id %1 dup/oor] ").arg(item.requestId);
                    break;
                }
                seen3[size_t(item.requestId - 50)] = true;
            }
            recOk = recOk && got == 3 && wk2.acceptedCount() == 3 && wk2.droppedCount() == 0
                && wk2.pendingCount() == 0 && wk2.builtCount() == 0;
        }
        ok = ok && recOk;
        if (!recOk) diag += QStringLiteral("[rebuild got-flags] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2020c destructor hygiene: destroying with a heavy build"
                             " in-flight plus a two-request backlog joins within the deadline"
                             " (never stuck), the discard is visible through the teardown"
                             " dropped-sink (exactly the two queued requests; the in-flight"
                             " build is not a drop), zero callback and zero output exist after"
                             " destruction (structural - no callback/signal surface), and a"
                             " fresh worker is immediately reusable with exactly-once outputs"
                             " and clean counters"
                          << (ok ? QString() : diag);
    });

    // ── r2020d：结构钉 + 白名单修订自证 + 零接线反探 + 线程身份（组件先行零生产接线守）──────
    runLeg(QStringLiteral("r2020d structure pins + amended threading-whitelist self-cert + zero"
        " production wiring + thread identity: comment-stripped pins hold the meshworker.h"
        " surface (submit/takeBuilt entries, capacity 64, the single-authority executor call"
        " MeshBuilder::build(t.snap, Reason::Dirty), the std::thread primitive, both cv wakes,"
        " the MeshBuiltItem QObjectFree pin and the destructor drop accounting) while reverse"
        " probes prove zero QObject surface (no Q_OBJECT/invokable/property/signal/emit),"
        " zero Qt threading facilities (QThread/QThreadPool/QtConcurrent/QMutex), zero copied"
        " mesh logic (face table/occlusion predicate/greedy mask/tile lookups) and zero World"
        " read face (no WorldFacade, no snapshot capture - capture stays on the caller thread);"
        " an in-section sweep re-certifies the amended dual-file threading whitelist (thread"
        " tokens live in exactly two sanctioned sites, backgroundgeneration.h plus"
        " meshworker.h, one or more each, nowhere else) and"
        " proves zero production wiring (the MeshWorker token appears in meshworker.h only -"
        " chunkgeometry.{h,cpp} explicitly probed, Main.qml clean); behaviorally the executor"
        " thread identity reconciles (worker thread != caller thread, observed exec thread =="
        " worker thread)"), [&]() {
        bool ok = true;
        QString diag;

        // ① meshworker.h 正面钉（pinSet 剥注释；根 = exe 相对 src/——r2013d 同款解析）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missMw = pinSet(
            srcRoot + QStringLiteral("/World/meshworker.h"), {
                SrcPin("r2020 worker class", "class MeshWorker", 1),
                SrcPin("r2020 submit entry", "Result<void> submit(const ChunkMeshSnapshot &snapshot, quint64 requestId)", 1),
                SrcPin("r2020 harvest entry", "bool takeBuilt(MeshBuiltItem &out)", 1),
                SrcPin("r2020 queue capacity bound", "kMaxQueuedTasks = 64", 1),
                SrcPin("r2020 queue-full visible rejection", "kErrQueueFull", 1), // 代码位恰 1（判据行；头注被剥）
                SrcPin("r2020 single authority executor", "MeshBuilder::build(t.snap, MeshBuilder::Reason::Dirty)", 1),
                SrcPin("r2020 thread primitive", "std::thread m_thread;", 1),
                SrcPin("r2020 cv wake on stop", "m_cv.notify_all()", 1),
                SrcPin("r2020 cv wake on submit", "m_cv.notify_one()", 1),
                SrcPin("r2020 item QObjectFree pin", "static_assert(QObjectFree<MeshBuiltItem>", 1),
                SrcPin("r2020 dtor drop accounting", "m_dropped += quint64(m_tasks.size());", 1),
                SrcPin("r2020 ctor test seams", "explicit MeshWorker(quint64 *teardownDroppedSink", 1),
            });
        for (const QString &m : missMw) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 反探（禁出——minCount=1 反探惯用法：miss 非空 = 合规缺席；剥注释器防注释误伤）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString mwH = srcRoot + QStringLiteral("/World/meshworker.h");
        const bool negOk = forbiddenAbsent(mwH, "Q_OBJECT")
            && forbiddenAbsent(mwH, "Q_INVOKABLE")
            && forbiddenAbsent(mwH, "Q_PROPERTY")
            && forbiddenAbsent(mwH, "QQuick3D")
            && forbiddenAbsent(mwH, "QThread")
            && forbiddenAbsent(mwH, "QThreadPool")
            && forbiddenAbsent(mwH, "QtConcurrent")
            && forbiddenAbsent(mwH, "QMutex")
            && forbiddenAbsent(mwH, "emit")
            && forbiddenAbsent(mwH, "signals")
            && forbiddenAbsent(mwH, "static const FaceDef kFaces")
            && forbiddenAbsent(mwH, "occludesNeighborFace")
            && forbiddenAbsent(mwH, "struct MaskEntry")
            && forbiddenAbsent(mwH, "tileFor")
            && forbiddenAbsent(mwH, "farmlandHydrBrightMul")
            && forbiddenAbsent(mwH, "WorldFacade")
            && forbiddenAbsent(mwH, "captureChunkMeshSnapshot");
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg-probe] ");

        // ③ 修订后双文件白名单段内自证（独立于 t1023c 修订腿的同语义 sweep——thread 记号恰在
        //    backgroundgeneration.h + meshworker.h 两落点，各 ≥1，此外零落点）。
        const QStringList tokens = {
            QStringLiteral("QThreadPool"), QStringLiteral("QThread"), QStringLiteral("QtConcurrent"),
            QStringLiteral("QFuture"), QStringLiteral("moveToThread"), QStringLiteral("std::thread"),
            QStringLiteral("std::async"),
        };
        int files = 0, unsanctioned = 0, bgHits = 0, mwHits = 0;
        QString sweepDetail;
        QDirIterator it(srcRoot, { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            ++files;
            const QString content = QString::fromUtf8(f.readAll());
            const QString rel = QDir(srcRoot).relativeFilePath(it.filePath());
            for (const QString &tok : tokens) {
                if (!content.contains(tok))
                    continue;
                if (rel == QStringLiteral("World/backgroundgeneration.h") && tok == QStringLiteral("std::thread"))
                    ++bgHits;
                else if (rel == QStringLiteral("World/meshworker.h") && tok == QStringLiteral("std::thread"))
                    ++mwHits;
                else {
                    ++unsanctioned;
                    sweepDetail += rel + QStringLiteral(":") + tok + QStringLiteral(" ");
                }
            }
        }
        const bool whitelistOk = files > 0 && unsanctioned == 0 && bgHits >= 1 && mwHits >= 1;
        ok = ok && whitelistOk;
        if (!whitelistOk)
            diag += QStringLiteral("[whitelist unsanctioned=%1 bg=%2 mw=%3 %4] ")
                        .arg(unsanctioned).arg(bgHits).arg(mwHits).arg(sweepDetail);

        // ④ 接线宿主白名单（§29.5-W4 同变更修订——纠偏留痕非放宽，t1023c/r2018d/r2019d 先例）：
        //    r2020 交付态 = 全树「执行器」记号只许出现在本头（组件先行零接线）；W4 起生产接线
        //    落地 = 接线宿主 gamesession.h 转正入白名单（unique_ptr 成员 + World 桥提交缝——
        //    计划原文授权的接线点）。World/chunkgeometry 双件/Main.qml 仍禁出（几何经 World 桥
        //    std::function 缝可达执行器，类型零泄漏；chunkgeometry 双件显式反探原样保留）。
        int mwSelfHits = 0, mwElsewhere = 0;
        QString wiringDetail;
        QDirIterator it2(srcRoot, { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                         QDir::Files, QDirIterator::Subdirectories);
        while (it2.hasNext()) {
            QFile f(it2.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString content = QString::fromUtf8(f.readAll());
            if (!content.contains(QStringLiteral("MeshWorker")))
                continue;
            const QString rel = QDir(srcRoot).relativeFilePath(it2.filePath());
            if (rel == QStringLiteral("World/meshworker.h"))
                ++mwSelfHits; // 组件本体（记号权威落点）
            else if (rel == QStringLiteral("Game/gamesession.h"))
                ; // W4 接线宿主（§29.5-W4 同变更转正——构造 + World 桥提交缝）
            else {
                ++mwElsewhere;
                wiringDetail += rel + QStringLiteral(" ");
            }
        }
        const QString cgc = srcRoot + QStringLiteral("/World/chunkgeometry.cpp");
        const QString cgh = srcRoot + QStringLiteral("/World/chunkgeometry.h");
        bool f3MwClean = true;
        QFile mf(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/.."))
                     .absoluteFilePath(QStringLiteral("src/ui/Main.qml")));
        if (mf.open(QIODevice::ReadOnly))
            f3MwClean = !QString::fromUtf8(mf.readAll()).contains(QStringLiteral("eshWorker"));
        const bool wiringOk = mwSelfHits >= 1 && mwElsewhere == 0 && f3MwClean
            && forbiddenAbsent(cgc, "MeshWorker") && forbiddenAbsent(cgh, "MeshWorker");
        ok = ok && wiringOk;
        if (!wiringOk)
            diag += QStringLiteral("[wiring self=%1 elsewhere=%2 qml=%3 %4] ")
                        .arg(mwSelfHits).arg(mwElsewhere).arg(f3MwClean).arg(wiringDetail);

        // ⑤ 线程身份行为钉（r2012 同款对账：执行线程 == 工作线程 != 调用者线程）。
        bool idOk = false;
        {
            MeshWorker wkId;
            const auto r = wkId.submit(newSnap(6, false), 60);
            MeshBuiltItem item;
            bool got = false;
            if (r.isOk()) {
                QElapsedTimer tm;
                tm.start();
                while (tm.elapsed() < 30000 && !got) {
                    got = wkId.takeBuilt(item);
                    if (!got)
                        QThread::msleep(1);
                }
            }
            idOk = r.isOk() && got && item.requestId == 60
                && wkId.workerThreadId() != std::this_thread::get_id()
                && wkId.observedExecThreadId() == wkId.workerThreadId();
        }
        ok = ok && idOk;
        if (!idOk) diag += QStringLiteral("[thread-id] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2020d structure pins + whitelist self-cert + wiring-host"
                             " whitelist (W4 amendment) + thread identity: the meshworker.h"
                             " surface holds under comment-stripped pins, reverse probes prove"
                             " zero QObject face, zero Qt threading facilities, zero copied"
                             " mesh logic and zero World read face; the in-section sweep"
                             " re-certifies the dual-file threading whitelist (std::thread"
                             " only in backgroundgeneration.h + meshworker.h) and the"
                             " token whitelist amended in the same change for the W4 wiring"
                             " host (token lives in meshworker.h + gamesession.h only - the"
                             " session host owns the executor and binds the World bridge"
                             " submit seam; World/chunkgeometry/Main.qml still token-free,"
                             " geometry reaches async meshing token-free via the bridge);"
                             " the executor thread identity reconciles against the caller"
                             " thread"
                          << (ok ? QString() : diag);
    });
}
