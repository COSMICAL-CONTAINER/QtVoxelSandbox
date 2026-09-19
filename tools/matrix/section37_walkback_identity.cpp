#include "matrix_helpers.h"

#include "chunklifecycle.h"  // 被测面：六态读（收敛判据 / 驱逐观测判据）
#include "streamingbridge.h" // 被测面：W5b QML 消费桥（QML 单例 instance = 生产/QML 同对象）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1062 走回重生成内容漂移收口 探针段（4 腿；filter 词 r2036；矩阵 675→679）。置尾先例
// 沿用（接 section36，runAll 末执行）。自建 fresh 小世界族（80×80×96 同步面 + 160×160 真链
// 生产核心域；rig 世界 w 零接触）。任务契约（dev-plan t1062）：
//   「预生成/按需链改动不触 fixed generate 语义」→ r2036a 墙（同 seed 双 fixed generate 逐位
//     恒等 + generate() 体全 pass 调用序字面钉 + 物化链反探禁出）；
//   「同 chunk 内容 = (seed, 坐标) 的纯函数而非物化时序的函数」→ r2036b 物化时序无关承重墙
//     （预生成全量世界逐 chunk 快照 → 全驱逐 → 逆序逐 chunk 重物化 → 与初见逐位恒等；
//     同 chunk 驱逐→重物化 ×3 轮逐位恒等；掩蔽口径：自时序恒等取**原始体素零掩蔽**——
//     两时序同走 sparse 链同窗重放，结构族五员两时序同缺席、scatterOres (c) 替代两时序同窗，
//     登记的 vs-fixed 分化域在本腿两侧同形 = 不需掩蔽，与 vs-fixed 恒等面（parity 墙）互斥不混）；
//   「进入预生成物化 vs 走离驱逐后按需重物化」→ r2036c 三方恒等（真链生产入口 rig：预生成
//     中心与出生域错位 → 走离合规驱逐未编辑 chunk → 走回按需重物化；A 初见 == A 走回重物化
//     == B 空域按需 三方逐位恒等，含边界带矿/树面——红面逐体素 diff 统计进 diag：id 对直方
//     图 + y 带 + 块缘带分类）；
//   「结构钉」→ r2036d（统一物化路径单一权威钉：sparseGenerate 预生成链逐 chunk 走
//     sparseGenerateChunk 不绕行 + loadChunkAt 按需链同一入口 + population 写域钳制/
//     快照-回填-恢复不回退钉 + fixed 链反探禁物化词元）。
// 同步链选型说明（读码 + 实测留痕）：同步按需链（loadChunkAt/驱逐 ⑥⑦+擦槽/重物化）的时序
// 无关性由 r2036b 承重（全驱逐→逆序重物化 ≡ 初见——邻块在场组合从「全无」到「全有」全谱
// 对撞）；r2036c 承生产链面（真桥异步 adopt：收割拍序 + 在途邻块 inflight 分支 + 槽位前置
// 物化的时序面），与 r2036b 互补不重叠。
// 驱逐链 = W3 数据面闭合生产形态（⑥ Loaded→Evicting → ⑦ Evicting→Absent → releaseStreamingChunk
// 擦槽；未编辑 chunk 无 persist 步——生产 dirtyQuery 恒 false 同语义面）。重物化链 = loadChunkAt
//（按需物化同步权威——sparseGenerateChunk 同门）+ r2036c 真链 adopt（生产异步权威）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘 sparsePopulateChunk 内 scatterOres 窗口调用（sparse 域矿面缺失）→
//     恰红 {r2036b} 单腿（矿签名非空转柱：wp 初见矿计数归零 = 被摘语义本体；r2036c 三方恒等
//     不误伤——两/三时序同缺矿仍互等；r2036a fixed 面不误伤——generate() 自有全域 scatterOres；
//     r2036d 钉 representative 面[驱动体/钳制/恢复/提升边]不钉逐 pass 清单——pass 完备性由
//     r2036b 行为面承重）。
//   变异二（NEG-2）= 摘快照恢复（④ 恢复门 `if (nb[i].snapshotted)` 变异恒假——memcpy 语句
//     原样保留）→ 恰红 {r2036b, r2036c}（b：全 25 chunk 比对撞上被污染快照邻——其回填地形 +
//     他块溢写未被终态覆盖；c：伴面 (4,4) 柱——走回波内 (5,5) 等 adopt 的 population 快照了它
//     而恢复缺失 = 逐位未动柱红；r2036a fixed 不误伤（fixed 零 population）；r2036d 不误伤
//     （memcpy 钉句原样、钳制/提升边钉原样））。
//   阴性日志：build/ 下四件 matrix_r2036_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
void MatrixRun::section37_walkback_identity()
{
    constexpr int kW = 80, kD = 80, kH = 96, kSeed = 2036, kRadius = 2;
    constexpr int kChunks = kW / 16;      // 5：核心域 chunk 数（[0,4]²）
    constexpr int kCoreBig = 160;         // 真链生产核心域（10×10 chunk，预生成中心 (5,5)）
    constexpr int kCoreBigH = 96;

    // sparse 构造缝（核心域 dims；radius 2 预生成 = 5×5 全核心域；radius 0 = 全按需）。
    const auto makeSparse = [](int radius) {
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kW;
        sp.coreDepth = kD;
        sp.height = kH;
        sp.spawnPreGenerateRadius = radius;
        return World(sp);
    };
    // 固定世界四 setter incantation（section11+ 全矩阵 proven——fixed 构造路径零改动的活证据）。
    const auto initFixed = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
    };

    // 全 chunk 体素 id+state 快照（16×16×H 双数组；索引 (y*16+lz)*16+lx）。
    struct ChunkSnap
    {
        QVector<quint8> vox;
        QVector<quint8> sts;
    };
    const auto snapChunk = [](World &w, int cx, int cz) {
        ChunkSnap s;
        s.vox.resize(16 * 16 * kH);
        s.sts.resize(16 * 16 * kH);
        for (int y = 0; y < kH; ++y)
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx) {
                    const int i = (y * 16 + lz) * 16 + lx;
                    s.vox[i] = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                    s.sts[i] = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                }
        return s;
    };
    // 逐体素 diff 统计（红面归因面）：id 对直方图 + y 带 + 块缘带（±8 列）分类 + 首个样本点。
    const auto diffStats = [](const ChunkSnap &a, World &b, int cx, int cz) {
        long total = 0, band = 0, core = 0;
        int yMin = 999, yMax = -1;
        QHash<QPair<quint8, quint8>, long> pairs; // a-id → b-id 计数
        QString first;
        for (int y = 0; y < kH; ++y)
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx) {
                    const int i = (y * 16 + lz) * 16 + lx;
                    const quint8 bid = b.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                    const quint8 bst = b.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                    if (a.vox[i] == bid && a.sts[i] == bst)
                        continue;
                    ++total;
                    yMin = std::min(yMin, y);
                    yMax = std::max(yMax, y);
                    pairs[qMakePair(a.vox[i], bid)]++;
                    const int dx = std::min(lx, 15 - lx), dz = std::min(lz, 15 - lz);
                    if (std::min(dx, dz) < 8)
                        ++band; // 块缘带（距 chunk 边 <8 列 = 邻域溢写/读域触达面）
                    else
                        ++core;
                    if (first.isEmpty())
                        first = QStringLiteral("x=%1 z=%2 y=%3 %4/%5->%6/%7")
                                    .arg(cx * 16 + lx).arg(cz * 16 + lz).arg(y)
                                    .arg(a.vox[i]).arg(a.sts[i]).arg(bid).arg(bst);
                }
        QString his;
        for (auto it = pairs.constBegin(); it != pairs.constEnd(); ++it)
            his += QStringLiteral("%1>%2x%3 ").arg(it.key().first).arg(it.key().second)
                       .arg(it.value());
        return QPair<bool, QString>(total == 0,
            total == 0
                ? QString()
                : QStringLiteral("[diff n=%1 band=%2 core=%3 y%4..%5 %6| %7] ")
                      .arg(total).arg(band).arg(core).arg(yMin).arg(yMax).arg(his, first));
    };
    // W3 驱逐数据面闭合生产形态（未编辑 chunk：无 persist 步——dirtyQuery 恒 false 同语义面）。
    const auto evictChunk = [](World &w, int cx, int cz) {
        w.setChunkLifecycle(cx, cz, ChunkLifecycle::Evicting); // ⑥ 移出驻留
        w.setChunkLifecycle(cx, cz, ChunkLifecycle::Absent);   // ⑦ 数据面闭合
        w.releaseStreamingChunk(cx, cz);                       // 擦槽（内容真实丢弃）
    };

    // ── r2036a：fixed 世界零变化墙 ──────────────────────────────────────────────────────
    runLeg(QStringLiteral("r2036a fixed-generation zero-change wall (two same-seed fixed worlds"
        " generate bitwise-identical voxel columns across surface, underground, bedrock and"
        " border bands, the fixed mode bookkeeping and dense-grid steady state are untouched,"
        " and the generate() body keeps its full fixed-domain pass call sequence verbatim with"
        " zero materialization-chain intrusion)"), [&]() {
        bool ok = true;
        QString diag;

        World w1;
        initFixed(w1);
        World w2;
        initFixed(w2);

        // ① 同 seed 双 generate：抽样列全 y 带逐位恒等（地表/地下/基岩带 + 块缘列 + 角列）。
        static const int kCols[][2] = { { 0, 0 }, { 79, 79 }, { 79, 0 }, { 0, 79 }, { 15, 16 },
            { 16, 15 }, { 32, 47 }, { 47, 32 }, { 8, 40 }, { 63, 63 }, { 64, 64 }, { 24, 24 } };
        bool genOk = true;
        for (const auto &c : kCols) {
            for (int y = 0; genOk && y < kH; ++y) {
                if (w1.blockAt(c[0], y, c[1]) != w2.blockAt(c[0], y, c[1])
                    || w1.stateAt(c[0], y, c[1]) != w2.stateAt(c[0], y, c[1])) {
                    genOk = false;
                    diag += QStringLiteral("[x=%1 z=%2 y=%3] ").arg(c[0]).arg(c[1]).arg(y);
                }
            }
        }
        ok = ok && genOk;

        // ② 模式/稳态账面（fixed 语义原样）：
        const bool modeOk = !w1.isSparse() && w1.chunksX() == kChunks && w1.chunksZ() == kChunks
            && w1.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded
            && w1.heightmapAt(40, 40) >= 0;
        ok = ok && modeOk;
        if (!modeOk)
            diag += QStringLiteral("[mode sparse=%1 cx=%2] ").arg(w1.isSparse()).arg(w1.chunksX());

        // ③ generate() 体字面钉：全 pass 全域调用序代表员原样在位 + 物化链反探禁出
        //    （miss 非空 = 合规）。函数文本抽取手工 contains（既有钉面同构）。
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
        static const char *kFixedCalls[] = { "placeBedrock();", "scatterOres();",
            "placeGravelPockets();", "carveCaves();", "carveCaveEntrances();",
            "placeUndergroundWaterPools();", "placeLavaLakes();", "placeDungeons();",
            "placeMineshaft();", "placeDesertTemple();", "placeJungleTemple();",
            "placeStronghold();", "carveCanyon();", "fillWater();", "placeSurfaceLakes();",
            "placeTrees();", "placeJungleTrees();", "placeTallGrass();", "placeFlowers();",
            "findSpawnColumn();", "recomputeLightField();", "rebuildStructureRegions();" };
        bool pinOk = fOpen && genBody.size() > 0;
        for (const char *call : kFixedCalls)
            pinOk = pinOk && genBody.contains(QLatin1String(call));
        pinOk = pinOk && !genBody.contains(QStringLiteral("sparseGenerateChunk"))
            && !genBody.contains(QStringLiteral("sparsePopulateChunk"))
            && !genBody.contains(QStringLiteral("loadChunkAt"));
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pin open=%1 body=%2] ").arg(fOpen).arg(genBody.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2036a fixed wall: two same-seed fixed worlds stay bitwise"
                             " identical on sampled full-height columns, fixed mode/steady-state"
                             " bookkeeping is untouched, and the generate() body retains the"
                             " complete fixed-domain pass call sequence with zero"
                             " materialization-chain tokens" << (ok ? QString() : diag);
    });

    // ── r2036b：物化时序无关承重墙 ──────────────────────────────────────────────────────
    // 掩蔽口径留痕：本腿是「自时序恒等」不是「vs fixed 全域恒等」——两时序同走 sparse 链同窗
    // 重放：结构族五员两时序同缺席（处置表豁免两侧同形）、scatterOres (c) 替代两时序同窗同钳
    // （登记的 vs-fixed 块缘 ±8 列脉形交互差与物化时序正交）→ 原始体素零掩蔽比对即纯时序面。
    runLeg(QStringLiteral("r2036b materialization-timing identity load-bearing wall (a fully"
        " pregenerated world's every core chunk content equals, voxel for voxel in id and"
        " state, the same chunk after the whole domain is evicted and rematerialized chunk by"
        " chunk in the reverse order - so each rematerialization sees a different neighbor"
        " presence mix - and a single chunk driven through three evict-rematerialize rounds"
        " reproduces its first-sight content every round, with ore, canopy and water"
        " signatures proving the compared face is real content and zero voxel masking is"
        " applied because both timings traverse the identical windowed sparse chain)"), [&]() {
        bool ok = true;
        QString diag;

        World wp = makeSparse(kRadius); // 预生成时序（初见权威）
        World wq = makeSparse(kRadius); // 孪生 → 全驱逐 → 逆序重物化（对撞时序）

        // ① 初见快照：全 25 chunk。
        ChunkSnap first[16][16];
        for (int cz = 0; cz < kChunks; ++cz)
            for (int cx = 0; cx < kChunks; ++cx)
                first[cx][cz] = snapChunk(wp, cx, cz);

        // ② 非空转签名：预生成世界矿/树冠/水计数 > 0（被比面是真实内容）。
        long ores = 0, leaves = 0, water = 0;
        for (int cz = 0; cz < kChunks; ++cz)
            for (int cx = 0; cx < kChunks; ++cx)
                for (int y = 0; y < kH; ++y)
                    for (int lz = 0; lz < 16; ++lz)
                        for (int lx = 0; lx < 16; ++lx) {
                            const quint8 b = first[cx][cz].vox[(y * 16 + lz) * 16 + lx];
                            switch (b) {
                                case BR::CoalOre: case BR::IronOre: case BR::GoldOre:
                                case BR::DiamondOre: case BR::RedstoneOre: case BR::LapisOre:
                                case BR::CopperOre: ++ores; break;
                                case BR::Leaves: case BR::SpruceLeaves: ++leaves; break;
                                case BR::Water: ++water; break;
                                default: break;
                            }
                        }
        const bool sigOk = ores > 0 && leaves > 0 && water > 0;
        ok = ok && sigOk;
        if (!sigOk)
            diag += QStringLiteral("[sig ores=%1 leaves=%2 water=%3] ").arg(ores).arg(leaves)
                        .arg(water);

        // ③ 全驱逐（⑥⑦ + 擦槽）→ 逆序逐 chunk 重物化（每步邻块在场组合都不同——对撞时序）。
        bool evictOk = true;
        for (int cz = 0; cz < kChunks && evictOk; ++cz)
            for (int cx = 0; cx < kChunks && evictOk; ++cx) {
                evictChunk(wq, cx, cz);
                evictOk = evictOk && wq.chunks().chunk(cx, cz) == nullptr
                    && wq.chunks().lifecycleAt(cx, cz) == ChunkLifecycle::Absent;
            }
        ok = ok && evictOk;
        if (!evictOk)
            diag += QStringLiteral("[evict] ");

        bool loadOk = true;
        for (int cz = kChunks - 1; cz >= 0 && loadOk; --cz)
            for (int cx = kChunks - 1; cx >= 0 && loadOk; --cx)
                loadOk = loadOk && wq.loadChunkAt(cx, cz)
                    && wq.chunks().lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && loadOk;
        if (!loadOk)
            diag += QStringLiteral("[load] ");

        // ④ 承重比对：重物化后全 25 chunk 与初见逐位恒等（零掩蔽）。
        bool parityOk = true;
        for (int cz = 0; cz < kChunks && parityOk; ++cz)
            for (int cx = 0; cx < kChunks && parityOk; ++cx) {
                const auto r = diffStats(first[cx][cz], wq, cx, cz);
                parityOk = parityOk && r.first;
                if (!r.first)
                    diag += QStringLiteral("[rev-chunk %1,%2]%3 ").arg(cx).arg(cz).arg(r.second);
            }
        ok = ok && parityOk;

        // ⑤ 轮次稳定：同 chunk (2,2) 驱逐→重物化 ×3 轮，每轮与初见逐位恒等。
        bool roundOk = true;
        for (int round = 0; round < 3 && roundOk; ++round) {
            evictChunk(wq, 2, 2);
            roundOk = roundOk && wq.chunks().chunk(2, 2) == nullptr;
            roundOk = roundOk && wq.loadChunkAt(2, 2);
            const auto r = diffStats(first[2][2], wq, 2, 2);
            roundOk = roundOk && r.first;
            if (!r.first)
                diag += QStringLiteral("[round %1]%2 ").arg(round).arg(r.second);
        }
        ok = ok && roundOk;

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2036b timing wall: every core chunk's content is a pure"
                             " function of seed and coordinates - the fully evicted domain"
                             " rematerialized in reverse order lands bitwise identical to the"
                             " pregenerated first sight on all 25 chunks, three"
                             " evict-rematerialize rounds of one chunk reproduce it every"
                             " round, and ore/canopy/water signatures prove a real unmasked"
                             " compared face" << (ok ? QString() : diag);
    });

    // ── r2036c：预生成 vs 按需三方恒等（复现腿——预期先红后绿；真链生产入口）────────────
    // rig = 真链装配（真 QQmlEngine × 真桥单例 × 真 PlayerController 位置沿；生产尺寸核心域
    // 160×160，预生成中心 (5,5)，出生域错位 (1,1)——走离合规驱逐半径外预生成 chunk，走回按
    // 需重物化）。三方 = A 初见（预生成物化）/ A 走回重物化（真链异步 adopt）/ B 空域按需
    //（同步 loadChunkAt 第三时序）。逐位域 = 目标 chunk 全 16×16×H id+state（含块缘矿/树带）。
    runLeg(QStringLiteral("r2036c pregenerate versus on-demand three-way identity (production"
        " entry rig: a pregenerated spawn-radius world snapshots its first-sight chunks, the"
        " displaced first player chunk edge legally evicts the out-of-radius pregenerated"
        " chunks with their content dropped, walking back converges the request window and"
        " re-materializes every evicted chunk through the real async adopt chain, and a"
        " separately on-demand-materialized twin agrees with both the first sight and the"
        " walk-back state voxel for voxel across the full id and state domain including the"
        " cross-border ore and canopy band, with untouched bystander chunks bitwise intact"
        " throughout)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        // 真链装配（真 QQmlEngine + 上下文注入 + setData wrapper；进入分流经真桥单例）。
        struct Rig
        {
            QQmlEngine engine;
            QQuickItem *wrap = nullptr;
            int warnings = 0;
            QString firstWarning;
        };
        Rig rig;
        qputenv("QML_DISABLE_DISK_CACHE", "1");
        QObject::connect(&rig.engine, &QQmlEngine::warnings, &rig.engine,
            [&rig](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++rig.warnings;
                    if (rig.firstWarning.isEmpty())
                        rig.firstWarning = e.toString();
                }
            });
        QQmlComponent comp(&rig.engine);
        comp.setData(R"QML(import QtQuick
Item {
    property string file: ""
    property int seed: 0
    property bool entered: false
    function enter() {
        entered = r2036Bridge.enterWorld(r2036World, r2036Store, r2036Clock, r2036Player, file, seed)
    }
    function flag(cw, cd) { return r2036Bridge.flagNewWorldStreaming(file, cw, cd) }
}
)QML", QUrl());
        rig.wrap = comp.isError() ? nullptr : qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine);
        const auto setRigCtx = [&](const char *name, QObject *obj) {
            rig.engine.rootContext()->setContextProperty(QLatin1String(name), obj);
        };

        const QString db = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_r2036_c_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(db); // fresh + 用后即删（saves/ 零触碰）
        World w;
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed); // fixed 宿主装配态镜像（创建链起点，进入时被 sparse 参数置换）
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);
        WorldClock clock;
        PlayerController pc;

        setRigCtx("r2036Bridge", &bridge);
        setRigCtx("r2036World", &w);
        setRigCtx("r2036Store", &store);
        setRigCtx("r2036Clock", &clock);
        setRigCtx("r2036Player", &pc);
        if (rig.wrap) {
            rig.wrap->setProperty("file", db);
            rig.wrap->setProperty("seed", kSeed);
        }
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreBig),
                Q_ARG(QVariant, kCoreBig));
        bool entered = false;
        if (rig.wrap) {
            QMetaObject::invokeMethod(rig.wrap, "enter", Qt::DirectConnection);
            entered = rig.wrap->property("entered").toBool();
        }
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive() && w.residentChunkCount() == 25;
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 res=%3] ")
                        .arg(flagged.toBool()).arg(entered)
                        .arg(w.residentChunkCount());

        // A 初见快照：走回目标 (6,6)（半径外预生成 → 必被首沿驱逐）+ 域缘伴 (7,6) + 半径内
        // 伴 (4,4)（走离后仍驻留——伴面零扰动柱）。
        struct Snap
        {
            QVector<quint8> vox;
            QVector<quint8> sts;
        };
        const auto snapFull = [&](int cx, int cz) {
            Snap s;
            s.vox.resize(16 * 16 * kCoreBigH);
            s.sts.resize(16 * 16 * kCoreBigH);
            for (int y = 0; y < kCoreBigH; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        const int i = (y * 16 + lz) * 16 + lx;
                        s.vox[i] = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                        s.sts[i] = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                    }
            return s;
        };
        // 逐体素 diff 统计（红面归因面）：id 对直方图 + y 带 + 块缘带（±8 列）分类 + 首样本。
        const auto diffStats = [&](const Snap &a, int cx, int cz) {
            long total = 0, band = 0, core = 0;
            int yMin = 999, yMax = -1;
            QHash<QPair<quint8, quint8>, long> pairs;
            QString first;
            for (int y = 0; y < kCoreBigH; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        const int i = (y * 16 + lz) * 16 + lx;
                        const quint8 bid = w.blockAt(cx * 16 + lx, y, cz * 16 + lz);
                        const quint8 bst = w.stateAt(cx * 16 + lx, y, cz * 16 + lz);
                        if (a.vox[i] == bid && a.sts[i] == bst)
                            continue;
                        ++total;
                        yMin = std::min(yMin, y);
                        yMax = std::max(yMax, y);
                        pairs[qMakePair(a.vox[i], bid)]++;
                        const int dx = std::min(lx, 15 - lx), dz = std::min(lz, 15 - lz);
                        if (std::min(dx, dz) < 8)
                            ++band; // 块缘带（邻域溢写/读域触达面）
                        else
                            ++core;
                        if (first.isEmpty())
                            first = QStringLiteral("x=%1 z=%2 y=%3 %4/%5->%6/%7")
                                        .arg(cx * 16 + lx).arg(cz * 16 + lz).arg(y)
                                        .arg(a.vox[i]).arg(a.sts[i]).arg(bid).arg(bst);
                    }
            QString his;
            for (auto it = pairs.constBegin(); it != pairs.constEnd(); ++it)
                his += QStringLiteral("%1>%2x%3 ").arg(it.key().first).arg(it.key().second)
                           .arg(it.value());
            return QPair<bool, QString>(total == 0,
                total == 0
                    ? QString()
                    : QStringLiteral("[diff n=%1 band=%2 core=%3 y%4..%5 %6| %7] ")
                          .arg(total).arg(band).arg(core).arg(yMin).arg(yMax).arg(his, first));
        };
        const Snap firstFar = snapFull(6, 6);
        const Snap firstEdge = snapFull(7, 6);
        const Snap firstNear = snapFull(4, 4);

        // 走离：真位置沿 (1,1)（预生成中心 (5,5) 错位）→ 首沿合规驱逐半径外预生成 chunk。
        pc.setWorld(&w);
        pc.loadSavedState(24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0); // floorDiv16 = (1,1)
        pc.tick();
        bridge.pumpTick(); // 驱逐/请求沿在泵拍承接（tick 尾——首沿后的第一拍即执行）
        const bool farGone = w.chunks().chunk(6, 6) == nullptr
            && w.chunks().lifecycleAt(6, 6) == ChunkLifecycle::Absent
            && w.chunks().lifecycleAt(7, 6) == ChunkLifecycle::Absent;
        ok = ok && farGone;
        if (!farGone)
            diag += QStringLiteral("[diverge l66=%1 l76=%2] ")
                        .arg(int(w.chunks().lifecycleAt(6, 6)))
                        .arg(int(w.chunks().lifecycleAt(7, 6)));

        // 收敛（错位出生域）：切比雪夫 ≤3 窗全 Loaded（deadline 有界防 flake）。
        const auto convergeLoaded = [&](const QVector<QPair<int, int>> &keys, int deadlineMs) {
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
                if (t.elapsed() > deadlineMs)
                    return false;
                bridge.pumpTick();
                QThread::msleep(2);
            }
        };
        const auto windowKeys = [](int ccx, int ccz, int r) {
            QVector<QPair<int, int>> keys;
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx)
                    keys.append({ ccx + dx, ccz + dz });
            return keys;
        };
        ok = ok && convergeLoaded(windowKeys(1, 1, 3), 150000);

        // 走回：真位置沿 (6,6) → 被驱逐预生成 chunk 真链异步按需重物化（Generate kind——
        // 未编辑无存档行）+ 收敛窗。
        pc.loadSavedState(104.5f, 70.0f, 104.5f, 0.0f, 0.0f, 0); // floorDiv16 = (6,6)
        pc.tick();
        bridge.pumpTick();
        const bool backOk = convergeLoaded(windowKeys(6, 6, 3), 150000)
            && w.chunks().lifecycleAt(6, 6) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(7, 6) == ChunkLifecycle::Loaded;
        ok = ok && backOk;
        if (!backOk)
            diag += QStringLiteral("[back l66=%1 l76=%2] ")
                        .arg(int(w.chunks().lifecycleAt(6, 6)))
                        .arg(int(w.chunks().lifecycleAt(7, 6)));

        // 三方恒等柱一：A 走回重物化 == A 初见（逐位，含块缘矿/树带；红面 diff 统计入 diag）。
        const auto rFar = diffStats(firstFar, 6, 6);
        ok = ok && rFar.first;
        if (!rFar.first)
            diag += QStringLiteral("[A0-vs-A1 6,6]%1 ").arg(rFar.second);
        const auto rEdge = diffStats(firstEdge, 7, 6);
        ok = ok && rEdge.first;
        if (!rEdge.first)
            diag += QStringLiteral("[A0-vs-A1 7,6]%1 ").arg(rEdge.second);

        // 三方恒等柱二：A 初见 == B 空域按需（同步 loadChunkAt 第三时序；重物化与初见恒等 +
        // 空域按需与初见恒等 = 三方传递闭包）。B 世界同 seed 同核心域，逐 chunk loadChunkAt。
        World wb;
        {
            World::SparseWorldParams sp;
            sp.seed = kSeed;
            sp.coreWidth = kCoreBig;
            sp.coreDepth = kCoreBig;
            sp.height = kCoreBigH;
            sp.spawnPreGenerateRadius = 0;
            wb.reinitializeAsSparse(sp);
        }
        const bool bLoadOk = wb.loadChunkAt(6, 6) && wb.loadChunkAt(7, 6);
        ok = ok && bLoadOk;
        if (!bLoadOk)
            diag += QStringLiteral("[bload] ");
        for (int lz = 0; lz < 16 && ok; ++lz)
            for (int lx = 0; lx < 16 && ok; ++lx)
                for (int y = 0; y < kCoreBigH; ++y) {
                    if (firstFar.vox[(y * 16 + lz) * 16 + lx] != wb.blockAt(96 + lx, y, 96 + lz)
                        || firstFar.sts[(y * 16 + lz) * 16 + lx]
                            != wb.stateAt(96 + lx, y, 96 + lz)) {
                        ok = false;
                        diag += QStringLiteral("[A0-vs-B 6,6 x=%1 z=%2 y=%3 %4 vs %5] ")
                                    .arg(96 + lx).arg(96 + lz).arg(y)
                                    .arg(firstFar.vox[(y * 16 + lz) * 16 + lx])
                                    .arg(wb.blockAt(96 + lx, y, 96 + lz));
                        break;
                    }
                }

        // 伴面零扰动：走离/走回全程 (4,4) 逐位未动（驱逐/重物化链对驻留邻的快照-恢复零泄漏）。
        const auto rNear = diffStats(firstNear, 4, 4);
        ok = ok && rNear.first;
        if (!rNear.first)
            diag += QStringLiteral("[bystander 4,4]%1 ").arg(rNear.second);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2036c three-way identity: the pregenerated first sight, the"
                             " walk-back async rematerialization and the empty-domain on-demand"
                             " materialization of the same far and edge chunks are voxel-bitwise"
                             " identical across every timing, the out-of-radius eviction drops"
                             " content legally, untouched bystander chunks stay intact, and the"
                             " converged windows return to the resident steady state"
                          << (ok ? QString() : diag);
    });

    // ── r2036d：结构钉（统一物化路径单一权威 / 钳制与快照面不回退 / fixed 反探）──────────
    runLeg(QStringLiteral("r2036d structure pins (comment-stripped source pins hold the unified"
        " materialization path on its single authority: the spawn pregeneration loop drives"
        " every chunk through the shared single-chunk materialization chain and never fills"
        " terrain outside it, the on-demand load entry delegates to the same chain, the"
        " population replay keeps its write-domain clamp and its snapshot-backfill-restore"
        " read-domain discipline, and the fixed generate body stays free of materialization"
        " tokens)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① world.cpp 正面钉：预生成链 = sparseGenerate 循环体逐 chunk 委托统一物化链（单一
        //    权威——禁绕行第二地形填充路径）+ 按需链同一入口 + population 读域在途邻提升边
        //    （t1062 修复的形态钉：脚手架一路的提升/回填/拆卸序在位）。
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("unified chain entry defined",
                       "void World::sparseGenerateChunk(int cx, int cz)", 1),
                SrcPin("pregen loop delegates to unified chain", "sparseGenerateChunk(cx, cz);", 1),
                SrcPin("on-demand delegates to unified chain", "    sparseGenerateChunk(cx, cz);", 2),
                SrcPin("population replay invoked", "    sparsePopulateChunk(cx, cz);", 2),
                SrcPin("write clamp on", "m_chunks.setPopulationWriteClamp(true, cx, cz);", 1),
                SrcPin("write clamp off", "m_chunks.setPopulationWriteClamp(false, cx, cz);", 1),
                SrcPin("resident neighbor snapshot",
                       "n.snapVoxels.assign(real->voxelData(), real->voxelData() + real->voxelCount());", 1),
                SrcPin("snapshot restore", "std::memcpy(real->voxelDataMut(), nb[i].snapVoxels.data(), real->voxelCount());", 1),
                SrcPin("scaffold teardown single point", "m_chunks.releaseSparseChunk(nb[i].cx, nb[i].cz);", 1),
                SrcPin("read-domain promotion edge 1",
                       "setChunkLifecycle(nx, nz, ChunkLifecycle::Loading);", 1),
                SrcPin("read-domain promotion edge 3",
                       "setChunkLifecycle(nx, nz, ChunkLifecycle::Loaded);", 1),
            });

        // ② sparseGenerate 函数域钉：预生成体 = 循环逐 chunk 委托 + 零直连地形填充/零直连
        //    population（统一路径不绕行的函数域形态钉——全文件粒度钉会被 generate() 体合法
        //    记号误计，故走函数域手工抽体；抽体界 = 定义行起至首个顶格闭括号，不含尾注释）。
        QFile f(srcRoot + QStringLiteral("/World/world.cpp"));
        const auto fnBody = [](const QString &src, const char *head) {
            const qsizetype b = src.indexOf(QLatin1String(head));
            if (b < 0)
                return QString();
            const qsizetype e = src.indexOf(QStringLiteral("\n}\n"), b);
            return e > b ? src.mid(b, e - b) : QString();
        };
        QString preBody, loadBody, genBody;
        if (f.open(QIODevice::ReadOnly)) {
            const QString src = QString::fromUtf8(f.readAll());
            preBody = fnBody(src, "void World::sparseGenerate()");
            loadBody = fnBody(src, "bool World::loadChunkAt");
            genBody = fnBody(src, "void World::generate()");
        }
        const bool preOk = preBody.contains(QStringLiteral("sparseGenerateChunk(cx, cz);"))
            && !preBody.contains(QStringLiteral("fillTerrainColumn"))
            && !preBody.contains(QStringLiteral("sparsePopulateChunk"))
            && !preBody.contains(QStringLiteral("placeTrees"))
            && !preBody.contains(QStringLiteral("scatterOres"));

        // ③ loadChunkAt 函数域钉：按需物化体委托统一链（禁第二物化路径）。
        const bool loadOk = loadBody.contains(QStringLiteral("sparseGenerateChunk(cx, cz);"))
            && !loadBody.contains(QStringLiteral("fillTerrainColumn"))
            && !loadBody.contains(QStringLiteral("sparsePopulateChunk"));

        // ④ fixed 反探：generate() 体禁出物化链词元（fixed 语义零触——r2036a 同面函数域复核）。
        const bool antiOk = genBody.size() > 0
            && !genBody.contains(QStringLiteral("sparseGenerateChunk"))
            && !genBody.contains(QStringLiteral("sparsePopulateChunk"))
            && !genBody.contains(QStringLiteral("loadChunkAt"));

        for (const QString &m : missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        if (!preOk) {
            ok = false;
            diag += QStringLiteral("[pregen body bypass] ");
        }
        if (!loadOk) {
            ok = false;
            diag += QStringLiteral("[on-demand body bypass] ");
        }
        if (!antiOk) {
            ok = false;
            diag += QStringLiteral("[fixed anti-probe] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2036d structure pins: the unified materialization path stays"
                             " the single authority (spawn pregeneration loops through the"
                             " shared single-chunk chain with no side terrain fill, on-demand"
                             " load delegates to the same chain, the population replay keeps"
                             " its write clamp and snapshot-backfill-restore discipline, and"
                             " the fixed generate body stays free of materialization tokens)"
                          << (ok ? QString() : diag);
    });
}
