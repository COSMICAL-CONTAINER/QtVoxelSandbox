#include "matrix_helpers.h"

#include "chunkstore.h"      // 被测面：D2 标志落库（rig flag() 经真桥 flagNewWorldStreaming）
#include "chunklifecycle.h"  // 被测面：六态读（收敛判据）
#include "streamingbridge.h" // 被测面：W5b 真链进入（QML 单例 instance = 生产同对象）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1073 流式外环内容三合一诊断修复探针段（4 腿；filter 词 r2046；矩阵 715→715+N）。置尾先例
// 沿用（接 section46，runAll 末执行，rig 世界零接触）。
//
// 任务契约（用户 2026-09-20 真机首测无限世界：外环 chunk 无树 / 无矿洞 / 地面无生物——核心域内
// 正常，用户对照明确）。根因链（读码 + 域算术实证，三症状两根）：
//   根因一（外环 population 全 no-op → 无树 + 无矿洞，同根）：sparse population 窗口重放的候选
//     域归一恒钳入 [0,m_width)×[0,m_depth)（= 核心域 160×160）——外环锚 chunk 的 scaffold 窗
//     （锚 ±1 chunk）∩ 核心域恒空 / 缺自身列 → 全部 (b) 级 pass 候选循环为空；且 setVoxelIfAir /
//     carveSphere / tryOre / worm lattice / 五 lattice pass 的「核心域让位检查」构成第二、三道
//     硬拒 → 外环 chunk = 纯地形（零树 / 零矿 / 零 carve / 零植物；首外环 chunk 仅核心缘树冠
//     ≤3 列溢入）。修复 = populationWindow() 归一唯一权威 + m_popWindowExtended 扩展域标志
//     （锚在核心域 chunk 盒外 → 窗原样不钳 + 让位检查跳过；写入仍由 ChunkManager population
//     写域钳制 [锚 ±1 chunk] + 未物化写门双重守卫 → 每 chunk 内容 = 其自身锚重放的纯函数，
//     跨锚溢写恒被 scaffold 拆卸 / 快照恢复丢弃 = 顺序无关，「两序同空」铁律不破；核心内锚恒
//     走旧钳制分支 = r2022c/r2023 族 fixed 恒等钉的零变化墙）。
//   根因二（外环零刷怪，独立）：EntityManager 黑暗刷怪调度把候选列钳在核心盒 [0,width)×[0,depth)
//     ——width/depth 在 sparse 世界是核心域尺寸非域界；玩家出核 > kSpawnMaxDist(40) 后全部候选
//     被拒 = 外环零刷怪（且 AI 位置钳制 [ehw, dim-ehw] 是直接赋值——外环 mob 首个 AI 拍被拽回
//     核心盒边；sunBurnExposureAt 核心盒早退 = 外环敌对永不被日光点燃）。修复 = 域门单一权威
//     columnInPlayableDomain（fixed 核心盒原样 / sparse 平面无界）+ aiClampDomainMax（sparse =
//     无界大数界），调用面：spawn 候选门 / findShadeTarget / sunBurnExposureAt / AI 钳制界 /
//     teleportEntity 界。mob 存活域自限：kFarDespawn(56) ≤ 生成半径(64) → mob 永不驻留未物化区。
// 腿面设计（先复现后修——本段先于修复落地时红面 = r2046b 外环三签名全零 + r2046c 外环零刷怪；
// 修复后四腿全绿）：
//   「fixed 零变化墙」→ r2046a（双 generate 逐位恒等[固定世界世界生成回归柱] + 归一/域门单一
//     权威钉[populationWindow 核心钳制分支 + columnInPlayableDomain fixed 分支在位] + 旧钳制
//     面反探禁出[17 处内联归一 / spawn 候选旧盒门已退役]）——两 NEG 均不误伤；
//   「外环内容承重墙」→ r2046b（生产尺寸核心 160×160 真链进入 → 玩家走查至出核 6 chunk →
//     收敛窗 49 键全 Loaded → 外环 49 chunk 树/矿/carve 三签名聚合 + 逐 chunk 矿/carve 下限 →
//     走回核心驱逐外环 → 走回重物化逐位恒等[扩展域重放的确定性柱]）——NEG-1（populationWindow
//     扩展分支摘除）恰红；
//   「刷怪域承重」→ r2046c（真链进入 → 玩家深出核 [300,80]（候选环 [24,40] 全在核心盒外）→
//     黑夜 tickHostileLife 周期 → 外环自然刷怪 ≥1 且全部出核 → 白昼外环 Shambler 日光燃烧 +
//     AI 拍后位置不被拽回核心盒[钳制修复的行为柱]）——NEG-2（columnInPlayableDomain sparse
//     分支翻 false）恰红；
//   「结构钉」→ r2046d（扩展域置位单点[sparsePopulateChunk 锚域判定] + 五 lattice pass 窗参数
//     与 ext 带族 + setVoxelIfAir / carveSphere / tryOre 扩展域三让位面 + 域门调用面 + 旧门反探
//     族）——两 NEG 均不误伤（钉调用面 / 声明面，不钉被 NEG 摘除的分支字面）。
// 阴性面设计（双变异双还原，存证 build/ 终名日志 matrix_r2046_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘 populationWindow 扩展分支（world.cpp `if (m_popWindowExtended) {` 变异为
//     `if (false && m_popWindowExtended) {`）→ 声明红面 {r2046b}[外环三签名聚合归零]。r2046a
//     不误伤（钉的是核心钳制分支与函数存在性）；r2046c 不误伤（刷怪域与 population 域解耦）；
//     r2046d 不误伤（钉置位单点与让位面，不钉该分支字面）。
//   NEG-2 摘刷怪域门 sparse 分支（entitymanager.cpp `if (world->isSparse()) return true;` 变异
//     为 `return false;`）→ 声明红面 {r2046c}[外环自然刷怪恒零 + 外环日光燃烧恒假]。r2046a/b
//     不误伤（世界侧零涉）；r2046d 不误伤（钉 columnInPlayableDomain 调用面与函数存在性）。
// 时长控制：两真链腿各收敛 49 键（deadline 有界防 flake 不挂死）；真临时库 fresh + 用后即删
//   （QDir::temp() pid 键名，绝对路径直用，绝不触 saves/）；fixed 宿主 48×48×96 s82。
void MatrixRun::section47_streaming_outer_content()
{
    constexpr int kW = 48, kD = 48, kHh = 96, kSeed = 82; // fixed 宿主小世界（3×3 chunk）
    constexpr int kCoreBig = 160;                          // 真机生产核心域（10×10 chunk）
    constexpr int kH = 96;
    constexpr int kGenR = 4; // P1 默认生成半径（生产入口实值）

    // 源码钉根（应用 exe 同级 src/ —— section09 先例同式派生）。
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
    const QString worldCpp = srcRoot + QStringLiteral("/World/world.cpp");
    const QString worldH = srcRoot + QStringLiteral("/World/world.h");
    const QString emCpp = srcRoot + QStringLiteral("/Entities/entitymanager.cpp");

    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2035 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2046_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 单 chunk 体素快照（16×16 列 × 全 y 带 id+state——扩展域重放逐位柱承载面，r2035c 同款）。
    const auto snapChunk = [](World &w, int cx, int cz) {
        QVector<QPair<int, int>> vox;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kH; ++y)
                    vox.append({ w.blockAt(cx * 16 + lx, y, cz * 16 + lz),
                        w.stateAt(cx * 16 + lx, y, cz * 16 + lz) });
        return vox;
    };
    const auto chunkDiffCount = [](World &w, int cx, int cz,
                                   const QVector<QPair<int, int>> &snap) {
        int diffs = 0, i = 0;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kH; ++y, ++i)
                    if (w.blockAt(cx * 16 + lx, y, cz * 16 + lz) != snap[i].first
                        || w.stateAt(cx * 16 + lx, y, cz * 16 + lz) != snap[i].second)
                        ++diffs;
        return diffs;
    };
    // 方窗键集（中心 ± 半径切比雪夫域——请求集尺寸的测试侧权威，r2035 同款）。
    const auto windowKeys = [](int ccx, int ccz, int r) {
        QVector<QPair<int, int>> keys;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                keys.append({ ccx + dx, ccz + dz });
        return keys;
    };
    // 收敛轮询：桥泵拍驱动（生产泵同体）直到 keys 全部 Loaded 或超时（r2035 同族节奏）。
    const auto convergeLoaded = [&](StreamingBridge &bridge, World &w,
                                    const QVector<QPair<int, int>> &keys, int deadlineMs,
                                    QString &diag) {
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
                int miss = 0;
                for (const auto &k : keys)
                    if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded)
                        ++miss;
                diag += QStringLiteral("[timeout %1ms miss=%2/%3] ")
                            .arg(deadlineMs)
                            .arg(miss)
                            .arg(keys.size());
                return false;
            }
            bridge.pumpTick();
            QThread::msleep(2);
        }
    };
    // 外环内容三签名扫描（树 / 矿 / carve 气）：地下带 [5, top-8) 计矿与 carve 气（地表保护带
    // 之下——洞口竖井 / 湖盆之上的地表空气不计入 carve 签名）+ 树签名含地表上方 ~9 格树冠带。
    struct ChunkContent
    {
        int trees = 0;
        int ores = 0;
        int carveAir = 0;
    };
    const auto isTreeBlock = [](quint8 b) {
        return b == BR::Log || b == BR::SpruceLog || b == BR::Leaves || b == BR::SpruceLeaves;
    };
    const auto isOreBlock = [](quint8 b) {
        return b == BR::CoalOre || b == BR::IronOre || b == BR::CopperOre || b == BR::GoldOre
            || b == BR::DiamondOre || b == BR::LapisOre || b == BR::RedstoneOre;
    };
    const auto scanChunk = [&](World &w, int cx, int cz) {
        ChunkContent c;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx) {
                const int x = cx * 16 + lx, z = cz * 16 + lz;
                const int top = std::min(w.heightAt(x, z), kH - 1); // 纯 worldgen 地表（无界纯函数）
                for (int y = 5; y < top - 8 && y < kH; ++y) {
                    const quint8 b = w.blockAt(x, y, z);
                    if (isTreeBlock(b))
                        ++c.trees;
                    else if (isOreBlock(b))
                        ++c.ores;
                    else if (b == BR::Air)
                        ++c.carveAir;
                }
                for (int y = top + 1; y <= top + 9 && y < kH; ++y) // 树冠带（地表上方）
                    if (isTreeBlock(w.blockAt(x, y, z)))
                        ++c.trees;
            }
        return c;
    };
    // 真链引擎装配（真 QQmlEngine + 上下文注入 + setData wrapper；真链进入分流经真桥单例）。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeRig = [](Rig &rig) {
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
    // 进入分流镜像（Main.qml enterWorld 分流行同式——五参类型化指针过真引擎转换链）。
    function enter() {
        entered = r2046Bridge.enterWorld(r2046World, r2046Store, r2046Clock, r2046Player, file, seed)
    }
    // 创建链标志传递镜像（WorldList 勾选行同式）。
    function flag(cw, cd) { return r2046Bridge.flagNewWorldStreaming(file, cw, cd) }
}
)QML", QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine);
    };
    const auto setRigCtx = [](Rig &rig, const char *name, QObject *obj) {
        rig.engine.rootContext()->setContextProperty(QLatin1String(name), obj);
    };
    const auto setRigFile = [](Rig &rig, const QString &file, int seed) {
        if (!rig.wrap)
            return;
        rig.wrap->setProperty("file", file);
        rig.wrap->setProperty("seed", seed);
    };
    const auto rigEnter = [](Rig &rig) {
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "enter");
        return rig.wrap ? rig.wrap->property("entered").toBool() : false;
    };

    // ── r2046a：fixed 世界零变化墙（双 generate 逐位恒等 + 归一/域门单一权威钉 + 旧钳制反探）──
    runLeg(QStringLiteral("r2046a fixed-world zero-change wall (two fixed worlds sharing seed and"
        " dims generate the same region bitwise identically in id and state, the population"
        " window normalizer stays the single authority with its core-clamp branch intact, the"
        " entity spawn domain gate keeps its fixed-box branch and its dark-spawn scheduler call"
        " site, and the retired per-pass inline clamps are absent from the world source)"), [&]() {
        bool ok = true;
        QString diag;

        // 双 generate 柱：两个同 seed 同 dims 固定世界 → chunk (1,1) 全带 id+state 逐位恒等。
        World w1;
        w1.setWidth(kW);
        w1.setDepth(kD);
        w1.setHeight(kHh);
        w1.setSeed(kSeed);
        World w2;
        w2.setWidth(kW);
        w2.setDepth(kD);
        w2.setHeight(kHh);
        w2.setSeed(kSeed);
        const auto s1 = snapChunk(w1, 1, 1);
        int diffs = 0, i = 0;
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = 0; y < kHh; ++y, ++i)
                    if (w2.blockAt(16 + lx, y, 16 + lz) != s1[i].first
                        || w2.stateAt(16 + lx, y, 16 + lz) != s1[i].second)
                        ++diffs;
        const bool genOk = diffs == 0 && s1.size() == 16 * 16 * kHh;
        ok = ok && genOk;
        if (!genOk)
            diag += QStringLiteral("[gen diffs=%1 n=%2] ").arg(diffs).arg(s1.size());

        // 单一权威钉：归一 helper 在位 + 核心钳制分支在位（NEG-1 摘扩展分支不触此字面）。
        const QStringList missWorld = pinSet(worldCpp,
            { SrcPin("population window normalizer def", "World::PopulationWindow World::populationWindow", 1),
                SrcPin("core-clamp branch", "w.xHi = std::min(wx1, m_width);", 1),
                SrcPin("extended flag set at anchor test", "m_popWindowExtended = (cx < 0 || cz < 0", 1),
                SrcPin("voxel-if-air extended guard", "if (!m_popWindowExtended", 1) });
        const QStringList missWorldH = pinSet(worldH,
            { SrcPin("population window type", "struct PopulationWindow", 1),
                SrcPin("extended flag member", "bool m_popWindowExtended = false;", 1) });
        // 域门钉：fixed 分支在位（NEG-2 摘 sparse 分支不触此字面）+ 刷怪调度调用面在位
        //（tickSpawners 的核心盒扫描门刻意保留——刷怪笼方块只生成于核心锚定结构，属登记口径；
        // 本钉只认调度器调用面）。
        const QStringList missEm = pinSet(emCpp,
            { SrcPin("spawn domain gate def", "static bool columnInPlayableDomain", 1),
                SrcPin("fixed-box branch", "return x >= 0 && z >= 0 && x < world->width() && z < world->depth();", 1),
                SrcPin("gate at dark-spawn scheduler", "if (!columnInPlayableDomain(world, cx, cz)) continue;", 1) });
        const bool pinOk = missWorld.isEmpty() && missWorldH.isEmpty() && missEm.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1%2%3] ")
                        .arg(missWorld.join(QLatin1Char(',')))
                        .arg(missWorldH.join(QLatin1Char(',')))
                        .arg(missEm.join(QLatin1Char(',')));

        // 反探禁出：旧 17 处内联归一已退役（剥注释后零命中；刷怪门反探由上方调度器调用面正钉
        // 承接——tickSpawners 的盒门为登记保留面，不作反探对象）。
        QFile wf(worldCpp);
        QString worldSrc;
        if (wf.open(QIODevice::ReadOnly))
            worldSrc = QString::fromUtf8(wf.readAll());
        const bool oldClampGone = !worldSrc.contains(
            QLatin1String("const int xLo = win ? std::max(wx0, 0) : 0;"));
        ok = ok && oldClampGone;
        if (!oldClampGone)
            diag += QStringLiteral("[anti clamp=%1] ").arg(oldClampGone);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2046a fixed-world zero-change wall (two fixed worlds sharing"
                             " seed and dims generate the same region bitwise identically, the"
                             " population window normalizer stays the single authority with its"
                             " core-clamp branch intact, the entity spawn domain gate keeps its"
                             " fixed-box branch and its dark-spawn scheduler call site, and the"
                             " retired per-pass inline clamps are absent)"
                          << (ok ? QString() : diag);
    });

    // ── r2046b：外环内容承重墙（生产尺寸真链走查出核：外环树/矿/carve 齐备 + 走回重物化逐位）──
    runLeg(QStringLiteral("r2046b outer-ring content load-bearing wall (a production-size"
        " streaming core entered through the real bridge walks the real player six chunks"
        " beyond the core and the converged request window materializes with tree, ore and"
        " carved-cave signatures present in aggregate and per-chunk ore/cave minima, and"
        " walking back to evict the outer ring then returning re-materializes the sampled"
        " outer chunk bitwise identical to its first materialization)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        const QString db = tempDb("b");
        QFile::remove(db); // fresh
        World w;
        w.setWidth(kCoreBig);
        w.setDepth(kCoreBig);
        w.setHeight(kH);
        w.setSeed(kSeed);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2046Bridge", &bridge);
        setRigCtx(rig, "r2046World", &w);
        setRigCtx(rig, "r2046Store", &store);
        setRigCtx(rig, "r2046Clock", &clock);
        setRigCtx(rig, "r2046Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // 创建链标志 + 真链进入（预生成中心 (5,5)，25 chunk）。
        setRigFile(rig, db, kSeed);
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreBig),
                Q_ARG(QVariant, kCoreBig));
        const bool entered = rigEnter(rig);
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive() && w.residentChunkCount() == 25;
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 res=%3] ")
                        .arg(flagged.toBool())
                        .arg(entered)
                        .arg(w.residentChunkCount());

        // 首次走查：玩家落 (16,5)（核心 chunk 盒 0..9 之外 6 chunk）；泵拍承接 + 收敛 cheb ≤ 3。
        pc.setWorld(&w);
        pc.loadSavedState(256.5f, 70.0f, 80.5f, 0.0f, 0.0f, 0); // floorDiv16 = (16,5)
        pc.tick();
        bridge.pumpTick();
        const int pcx = 16, pcz = 5;
        const QVector<QPair<int, int>> winKeys = windowKeys(pcx, pcz, 3);
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, winKeys, 150000, dConv);
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[conv %1]").arg(dConv);

        // 外环内容三签名：采样窗全 49 chunk（chunk 盒 13..19 × 2..8 = 全部出核 ≥ 3 chunk）。
        int totTrees = 0, totOres = 0, totCarve = 0;
        int perChunkMinOre = 1 << 30, perChunkMinCarve = 1 << 30, chunksWithTrees = 0;
        for (const auto &k : winKeys) {
            const ChunkContent c = scanChunk(w, k.first, k.second);
            totTrees += c.trees;
            totOres += c.ores;
            totCarve += c.carveAir;
            perChunkMinOre = std::min(perChunkMinOre, c.ores);
            perChunkMinCarve = std::min(perChunkMinCarve, c.carveAir);
            if (c.trees > 0)
                ++chunksWithTrees;
        }
        // 聚合阈值（宽松带宽：群系 / 海域分布方差下恒真——修复后实测聚合量级见探针基线记录；
        // NEG-1 摘扩展域后聚合恰全零 = 阴性恰红面）。
        const bool aggOk = totTrees >= 20 && totOres >= 200 && totCarve >= 100
            && chunksWithTrees >= 2;
        ok = ok && aggOk;
        if (!aggOk)
            diag += QStringLiteral("[agg trees=%1 ores=%2 carve=%3 treeChunks=%4/%5] ")
                        .arg(totTrees)
                        .arg(totOres)
                        .arg(totCarve)
                        .arg(chunksWithTrees)
                        .arg(winKeys.size());
        // 逐 chunk 下限（矿/carve 域密度高——每 chunk 至少可观测；零 = 钳制病灶的逐 chunk 签名）。
        const bool perOk = perChunkMinOre >= 1 && perChunkMinCarve >= 1;
        ok = ok && perOk;
        if (!perOk)
            diag += QStringLiteral("[per minOre=%1 minCarve=%2] ")
                        .arg(perChunkMinOre)
                        .arg(perChunkMinCarve);

        // 扩展域重放确定性柱：快照外环代表 chunk (13,5) → 走到 (8,2)（(13,5) 距新锚 cheb 5：
        // > gen 半径 4 入驱逐域、≤ scan 窗 6 入决策域——P1 决策面显式有界，过远的 chunk 不入
        // toEvict 域）→ (13,5) 驱逐擦槽 → 走回 (16,5) 重物化 → 全逐位恒等（id+state 双数组
        // 16×16×H，r2035c 同款全逐位口径）。
        const auto snapOuter = snapChunk(w, 13, 5);
        pc.loadSavedState(128.5f, 70.0f, 32.5f, 0.0f, 0.0f, 0); // floorDiv16 = (8,2)
        pc.tick();
        bridge.pumpTick();
        QString dEvict;
        const bool evictConverged = convergeLoaded(bridge, w, windowKeys(8, 2, 3), 150000, dEvict);
        const bool outerEvicted = w.chunks().lifecycleAt(13, 5) == ChunkLifecycle::Absent
            && w.chunks().chunk(13, 5) == nullptr;
        ok = ok && evictConverged && outerEvicted;
        if (!evictConverged || !outerEvicted)
            diag += QStringLiteral("[evict c=%1 gone=%2 l=%3 %4] ")
                        .arg(evictConverged)
                        .arg(w.chunks().chunk(13, 5) == nullptr)
                        .arg(int(w.chunks().lifecycleAt(13, 5)))
                        .arg(dEvict);

        pc.loadSavedState(256.5f, 70.0f, 80.5f, 0.0f, 0.0f, 0); // 走回 (16,5)
        pc.tick();
        bridge.pumpTick();
        QString dBack;
        const bool backConverged = convergeLoaded(bridge, w, winKeys, 150000, dBack);
        const int diffOuter = chunkDiffCount(w, 13, 5, snapOuter);
        const bool regenOk = backConverged && diffOuter == 0;
        ok = ok && regenOk;
        if (!regenOk)
            diag += QStringLiteral("[back c=%1 diffs=%2 %3] ")
                        .arg(backConverged)
                        .arg(diffOuter)
                        .arg(dBack);

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2046b outer-ring content load-bearing wall (a production-size"
                             " streaming core entered through the real bridge walks the real"
                             " player six chunks beyond the core and the converged window"
                             " materializes with tree, ore and carved-cave signatures in"
                             " aggregate and per-chunk minima, and walking back to evict the"
                             " outer ring then returning re-materializes the sampled chunk"
                             " bitwise identical to its first materialization)"
                          << (ok ? QString() : diag);
    });

    // ── r2046c：刷怪域承重（外环黑夜自然刷怪 + 外环日光燃烧 + AI 钳制不拽核）────────────────
    runLeg(QStringLiteral("r2046c outer-ring spawn domain load-bearing wall (a streaming world"
        " with the real player parked deep outside the core where the whole dark-spawn ring"
        " lies beyond the core box naturally spawns hostiles at night with every spawn outside"
        " the core box, a directly placed undead at an outer-ring surface column ignites in"
        " daylight through the same domain gate, and its position stays in the outer ring"
        " across AI frames instead of being clamped back to the core box)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld();

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        w.setWidth(kCoreBig);
        w.setDepth(kCoreBig);
        w.setHeight(kH);
        w.setSeed(kSeed);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2046Bridge", &bridge);
        setRigCtx(rig, "r2046World", &w);
        setRigCtx(rig, "r2046Store", &store);
        setRigCtx(rig, "r2046Clock", &clock);
        setRigCtx(rig, "r2046Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        setRigFile(rig, db, kSeed);
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreBig),
                Q_ARG(QVariant, kCoreBig));
        const bool entered = rigEnter(rig);
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive() && w.residentChunkCount() == 25;
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 res=%3] ")
                        .arg(flagged.toBool())
                        .arg(w.residentChunkCount());

        // 玩家深出核：(300.5, 80.5) = chunk (18,5)；候选环 [24,40] ⊆ x [260,341] —— 全在核心盒
        // [0,160) 之外 → 「存在刷怪」即「外环可刷」的直接行为证据；生成半径 4 窗 x [224,368)
        // ⊇ 候选环 = 候选列全部已物化（未物化列由 air/支撑/光照门天然拒，不构成本腿假绿面）。
        const int pcx = 18, pcz = 5;
        const float pfx = 300.5f, pfz = 80.5f;
        const float py = float(w.heightAt(300, 80)) + 3.0f;
        pc.setWorld(&w);
        pc.loadSavedState(pfx, py, pfz, 0.0f, 0.0f, 0);
        pc.tick();
        bridge.pumpTick();
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, windowKeys(pcx, pcz, 3), 150000, dConv);
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[conv %1]").arg(dConv);

        // 黑夜自然刷怪：直接驱动敌对生命周期（生产链 PlayerController::step 同一槽体；skyBrightness
        // 0 = 夜语义——有效光恒 < 阈值，光照门不拒）。每调用 dt=2.5 ≥ kSpawnInterval = 一完整刷怪
        // 周期（kSpawnAttempts=8 次环内选点）；cap 满足前 ≥1 刷即收（每周期 spawn 1 只后 break）。
        EntityManager em;
        const QVector3D playerPos(pfx, py, pfz);
        int cycles = 0;
        for (; cycles < 32 && em.hostileCount() == 0; ++cycles)
            em.tickHostileLife(2.5, &w, playerPos, 0.0f);
        const bool spawned = em.hostileCount() >= 1 && cycles < 32;
        ok = ok && spawned;
        if (!spawned)
            diag += QStringLiteral("[night cycles=%1 hostiles=%2] ")
                        .arg(cycles)
                        .arg(em.hostileCount());
        // 外环性：候选环几何保证全部出核 → 所有存活敌对 XZ 必在核心盒外（x > 200 宽裕带）。
        bool allOutside = em.hostileCount() >= 1;
        for (int i = 0; i < em.cap() && allOutside; ++i) {
            if (!em.aliveAt(i) || !em.isHostileAt(i))
                continue;
            if (em.posAt(i).x() <= 200.0f)
                allOutside = false;
        }
        ok = ok && allOutside;
        if (!allOutside)
            diag += QStringLiteral("[outside some hostile <= 200 x] ");

        // 外环日光燃烧：直接放置蹒跚者（确定性 undead——绕开自然刷怪的类型骰，专钉域门日光面）于
        // 已物化外环地表列；昼语义（skyBrightness 1.0）下经 tick() 推进 m_tickPhase（aiTick 节流
        // 沿）→ 曝晒燃烧翻位。旧码 sunBurnExposureAt 核心盒早退 = 恒不燃烧（NEG-2 恰红面）。
        const int sx = 320, sz = 90;
        const int sh = w.heightAt(sx, sz);
        em.spawnHostileMob(sx, sh + 1, sz, EntityManager::MobShambler);
        bool burning = false;
        for (int f = 0; f < 60 && !burning; ++f) {
            em.tick(0.05, &w, playerPos, 0.3f, 1.8f, false, false, 1.0f);
            em.tickHostileLife(0.05, &w, playerPos, 1.0f);
            for (int i = 0; i < em.cap() && !burning; ++i)
                if (em.aliveAt(i) && em.isHostileAt(i) && em.isBurningAt(i))
                    burning = true;
        }
        ok = ok && burning;
        if (!burning)
            diag += QStringLiteral("[day no hostile burning at outer column] ");

        // AI 钳制柱：燃烧 Shambler 经若干 AI 拍后仍在外环（旧钳制 [ehw, dim-ehw] 直接赋值会把
        // 外环 mob 首拍拽回核心盒边 x≈160 → 位置出核即红）。
        bool stayedOutside = false;
        for (int i = 0; i < em.cap(); ++i) {
            if (!em.aliveAt(i) || !em.isHostileAt(i))
                continue;
            if (em.posAt(i).x() > 200.0f)
                stayedOutside = true;
        }
        ok = ok && stayedOutside;
        if (!stayedOutside)
            diag += QStringLiteral("[clamp mob yanked toward core box] ");

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2046c outer-ring spawn domain load-bearing wall (a streaming"
                             " world with the real player parked deep outside the core where the"
                             " whole dark-spawn ring lies beyond the core box naturally spawns"
                             " hostiles at night with every spawn outside the core box, a"
                             " directly placed undead at an outer-ring surface column ignites in"
                             " daylight, and its position stays in the outer ring across AI"
                             " frames instead of being clamped back to the core box)"
                          << (ok ? QString() : diag);
    });

    // ── r2046d：结构钉（扩展域置位单点 + lattice 带族 + 域门调用面 + 反探族）──────────────────
    runLeg(QStringLiteral("r2046d structure pins (the extended-domain flag is decided at exactly"
        " one anchor test inside the sparse population replay and reset after it, the five"
        " lattice passes carry window parameters with the extended band bound and the anchor"
        " boundary checks disabled exactly under the extension, the carve and ore write"
        " primitives keep their extended-domain guards next to the y-domain checks, the entity"
        " domain gate is called from the spawn scheduler, the daylight sampler and the shade"
        " seeker, and the retired core-clamp lattice loops are absent)"), [&]() {
        bool ok = true;
        QString diag;

        // 世界侧：置位单点 + 复位单点 + 五 lattice pass 窗参数形态 + 扩展带 / 让位面。
        const QStringList missWorld = pinSet(worldCpp,
            { SrcPin("extended flag single set", "m_popWindowExtended = (cx < 0 || cz < 0", 1),
                SrcPin("extended flag single reset", "m_popWindowExtended = false;", 1),
                SrcPin("gravel window params", "void World::placeGravelPockets(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("entrance window params", "void World::carveCaveEntrances(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("water pool window params", "void World::placeUndergroundWaterPools(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("lava lake window params", "void World::placeLavaLakes(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("surface lake window params", "void World::placeSurfaceLakes(int wx0, int wx1, int wz0, int wz1)", 1),
                SrcPin("extended lattice band marker", "constexpr int kBand = 16;", 5),
                SrcPin("sphere/discard extended guards", "if (!m_popWindowExtended", 3) });
        const QStringList missWorldH = pinSet(worldH,
            { SrcPin("window normalizer decl", "PopulationWindow populationWindow(int wx0, int wx1, int wz0, int wz1) const;", 1),
                SrcPin("five windowed decls", "int wx0 = 0, int wx1 = 0, int wz0 = 0, int wz1 = 0);", 22) });
        // Entities 侧：域门三调用面 + 钳制界两调用面 + AI 界 helper。
        const QStringList missEm = pinSet(emCpp,
            { SrcPin("gate def", "static bool columnInPlayableDomain", 1),
                SrcPin("gate at spawn scheduler", "if (!columnInPlayableDomain(world, cx, cz)) continue;", 1),
                SrcPin("gate at daylight sampler", "!columnInPlayableDomain(world, sx, sz)", 1),
                SrcPin("gate at shade seeker", "if (!columnInPlayableDomain(world, x, z)) continue;", 1),
                SrcPin("ai clamp helper", "static float aiClampDomainMax", 1),
                SrcPin("ai clamp at tick pass-through", "const float worldW = aiClampDomainMax(world, world->width());", 1),
                SrcPin("ai clamp at teleport", "const float wW = aiClampDomainMax(world, world->width())", 1) });
        const bool pinOk = missWorld.isEmpty() && missWorldH.isEmpty() && missEm.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1%2%3] ")
                        .arg(missWorld.join(QLatin1Char(',')))
                        .arg(missWorldH.join(QLatin1Char(',')))
                        .arg(missEm.join(QLatin1Char(',')));

        // 反探禁出：旧核心盒 lattice 循环界（gravel/pools/lava/lakes/entrances 的 `bx < m_width`
        // 原始形态已被 ext 带族置换——五 pass 内零命中）。
        QFile wf(worldCpp);
        QString worldSrc;
        if (wf.open(QIODevice::ReadOnly))
            worldSrc = QString::fromUtf8(wf.readAll());
        const bool oldLatticeGone = !worldSrc.contains(
            QLatin1String("for (int bx = kPocketGrid / 2; bx < m_width; bx += kPocketGrid) {"))
            && !worldSrc.contains(QLatin1String(
                "for (int bx = kPoolGrid / 2; bx < m_width; bx += kPoolGrid) {"))
            && !worldSrc.contains(QLatin1String(
                "for (int bx = kLakeGrid / 2; bx < m_width; bx += kLakeGrid) {"))
            && !worldSrc.contains(QLatin1String(
                "for (int bx = kEntranceGrid / 2; bx < m_width; bx += kEntranceGrid) {"));
        ok = ok && oldLatticeGone;
        if (!oldLatticeGone)
            diag += QStringLiteral("[anti old lattice loop present] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2046d structure pins (the extended-domain flag is decided at"
                             " exactly one anchor test inside the sparse population replay and"
                             " reset after it, the five lattice passes carry window parameters"
                             " with the extended band bound and boundary checks disabled exactly"
                             " under the extension, the carve and ore write primitives keep"
                             " their extended-domain guards, the entity domain gate is called"
                             " from the spawn scheduler, the daylight sampler and the shade"
                             " seeker, and the retired core-clamp lattice loops are absent)"
                          << (ok ? QString() : diag);
    });
}
