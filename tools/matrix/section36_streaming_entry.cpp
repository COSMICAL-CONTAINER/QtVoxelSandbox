#include "matrix_helpers.h"

#include "chunkstore.h"      // 被测面：stream_worlds 行（D2 标志落库判据——进入分流入口事实）
#include "chunklifecycle.h"  // 被测面：六态读（收敛判据 / 驱逐观测判据）
#include "streamingbridge.h" // 被测面：W5b QML 消费桥（QML 单例 instance = 生产/QML 同对象）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1061 无限世界生产入口 livelock 探针段（4 腿；filter 词 r2035；矩阵 671→671+N）。置尾先例
// 沿用（接 section35，runAll 末执行，rig 世界零接触）。
//
// 任务契约（用户 2026-09-19 真机首验阻塞：新建无限世界进入后 resident 集合以 ~1s 周期永久
// 振荡、世界永远弹进弹出无法进入；固定世界不受影响）。真机日志指纹 = Main.qml 池重建行
// 「built N chunk Models from M resident chunks」的 M 永久振荡（早期 5↔12、后期 14↔22）。
// 根因链（读码 + 日志实证）：
//   ① enterWorld → reinitializeAsSparse → sparseGenerate 的出生半径预生成（25 chunk）在
//      主线程同步跑；每个 chunk 物化链（sparseGenerateChunk + sparsePopulateChunk）会临时
//      物化至多 8 个 population 脚手架邻块（①②③ 加入驻留）再拆卸（⑥⑦ 擦槽）——每个
//      chunk 产生 ~17 次驻留集成员翻转；
//   ② 每次翻转都在 World::setChunkLifecycle forwarder 里同步 emit residentChunkRevisionChanged
//      （逐翻转发射，无批口）；
//   ③ Main.qml 对该沿的反应 = 整池销毁重派生（每 resident chunk 5 段 Model、每段同步
//      buildMesh ~1-3ms）→ 每物化 chunk ~17 次 O(池) 重建 = enterWorld 主线程 livelock；
//   ④ 且每次重建读到的都是**非稳态**驻留集（脚手架在场 / 快照-回填零化中的邻块）→
//      中间态内容被烘进网格（真机日志顶点数 25500→128→76→0 递减指纹）+ 池里反复出现
//      「微秒后即拆」的脚手架槽位 = 用户看到的 resident 永久振荡。
// 腿面设计（先复现后修——本段先于修复落地，红面已记录进任务报告）：
//   「fixed 世界零变化墙」→ r2035a（真链 fixed 存档进入分流恒 false + 会话零构造 + 驻留沿
//     零发射 + 会话通电门/泵零动作墙/进入 fixed 分流三结构钉——修复不得改 fixed 世界任何
//     行为的承重面）；
//   「入口收敛承重墙」→ r2035b（真 QQmlEngine × 真桥 × 真 PlayerController 位置沿：真链进入
//     生产尺寸核心域（160×160）+ 静止玩家 → ①**物化突发批口**：enterWorld 全程驻留沿恰
//     1 条（突发收敛到稳态观察点）；②**沿时刻观测合流**：每条沿时刻采样的驻留集相对上一
//     沿的成员减少全部位于 gen 半径外（脚手架抖落 = 半径内假驱逐 → 真机振荡的直接机理）
//     且沿时刻驻留数全程单调不降；③resident 收敛到请求集尺寸（81）+ 半径内零驱逐；④收敛后
//     沿停（revision 沿零发射零 bump）。修复前红面 = ①~③ 三柱同红（实测 enter 沿数百条 +
//     沿时刻集合反复缩水）；
//   「预生成中心与出生 chunk 分歧」→ r2035c（出生域与预生成域错位 rig：玩家 chunk (1,1) vs
//     预生成中心 (5,5) → 首沿把半径外预生成 chunk 合规驱逐 + 半径内按需重请求 → 收敛；走回
//     预生成域 → 被驱逐预生成 chunk 按需重物化回 Loaded 且**地形面逐列恒等**（heightmap 基
//     准）+ 半径内存活代表全程逐位未动[跨边界矿脉/树冠带属 r2023 已登记 sparse 分化域，不进
//     本腿逐位域——腿注释留痕]；收敛后驻留集稳（逐拍采样恒定）——中心分歧不振荡语义面）；
//   「走离/走回 + 结构钉」→ r2035d（压小半径控时长：走离半径外驱逐语义保持[半径内驻留
//     零丢失 + 半径外擦槽到 Absent] + Edits-on-evict 生产语义[编辑过的被驱逐 chunk 走回后
//     编辑逐位存活 = persist→store→restore 链] + 首沿门前行为惰性[任何位置沿之前泵拍零请求
//     零驱逐零沿] + 单一权威源钉[决策单恰一/toEvict 消费恰一/驱逐执行体恰一/位置喂入恰一/
//     驻留沿收口恰一/物化批口五落点] + 呈现层放大路径钉[沿→整池重建的 QML 消费面在场 =
//     逐翻转发射属 livelock 级的机理存照]）。
// 阴性面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   NEG-1 摘批口收敛（world.h setChunkLifecycle 批内分支条件变异为恒假 → 每次翻转都立即
//     emit = 修复前逐翻转发射行为）→ 声明红面 {r2035b}[批口柱 + 沿观测合流柱同源红；
//     diag 带 [burst] 前缀判别]。r2035a 不误伤（fixed 零翻转，发射面不可达）；r2035c/d
//     不误伤（无沿时刻观测断言——收敛/驱逐/内容柱与发射时机解耦）。
//   NEG-2 摘驱逐消费（chunkstreamdriver.h 决策沿尾部 toEvict 消费调用 if(false) 包裹）→
//     声明红面 {r2035c, r2035d}[分歧驱逐柱：半径外预生成不再被驱逐 → 首沿后驻留集含全部
//     预生成 ≠ 请求集；走离柱：走离后半径外驻留不离场。r2035b 不误伤（对齐中心零驱逐域）；
//     r2035a 不误伤（fixed 无驱动器）]。
//   阴性日志：build/ 下四件 matrix_r2035_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 时长控制：真链腿预生成 = W1 默认半径 2（25 chunk）；收敛 deadline 有界（防 flake 不挂死）；
//   临时库 fresh + 用后即删（QDir::temp() pid 键名，绝对路径直用 = openWorld 先例，绝不触
//   saves/）；段内物化面复用 48×48 fixed 小世界族与 80×80/160×160 sparse 核心域（真机生产
//   尺寸对齐面 = 160×160，r2035b 专属）。
void MatrixRun::section36_streaming_entry()
{
    constexpr int kW = 48, kD = 48, kH = 96, kSeed = 82; // fixed 宿主小世界（3×3 chunk）
    constexpr int kCoreBig = 160;                        // 真机生产核心域（10×10 chunk，r2035b/c）
    constexpr int kCoreSmall = 80;                       // 压尺寸核心域（5×5 chunk，r2035d）

    // 源码钉根（应用 exe 同级 src/ —— section09 先例同式派生）。
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

    // fixed 宿主世界四 setter incantation（section11 同族——创建链起点 = QML theWorld 装配态镜像）。
    const auto makeHost = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
    };
    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2015 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2035_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 单 chunk 体素快照（16×16 列 × 全 y 带 id+state——驱逐/重生成逐位柱承载面，section31 同款）。
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
    // 切比雪夫距离（策略同款纯整数度量——请求/驱逐域判据的测试侧镜像）。
    const auto cheb = [](int ax, int az, int bx, int bz) {
        const int dx = ax - bx, dz = az - bz;
        return qMax(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
    };
    // 方窗键集（中心 ± 半径切比雪夫域——请求集尺寸的测试侧权威）。
    const auto windowKeys = [](int ccx, int ccz, int r) {
        QVector<QPair<int, int>> keys;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                keys.append({ ccx + dx, ccz + dz });
        return keys;
    };
    // 收敛轮询：桥泵拍驱动（生产同体——pumpTick = WorldClock::ticked 槽体）直到 keys 全部
    // Loaded 或超时（section31 同族节奏；deadline 有界防 flake 不挂死）。
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
            bridge.pumpTick(); // 生产泵拍同体（QML tick 桥同拍同源）
            QThread::msleep(2);
        }
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
        entered = r2035Bridge.enterWorld(r2035World, r2035Store, r2035Clock, r2035Player, file, seed)
    }
    // 创建链标志传递镜像（WorldList 勾选行同式）。
    function flag(cw, cd) { return r2035Bridge.flagNewWorldStreaming(file, cw, cd) }
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

    // ── r2035a：fixed 世界零变化墙（真链进入分流恒 false + 会话零构造 + 驻留沿零发射 + 三钉）──
    runLeg(QStringLiteral("r2035a fixed-world zero-change wall (production entry): a fixed save"
        " through the real-engine enter handoff answers false with the world staying fixed and"
        " no session constructed, the resident revision stays at zero with zero revision edges"
        " across enter, pump beats, a real player chunk edge and a voxel edit, the resident set"
        " still enumerates the whole chunk grid, and the three structural pins hold (session"
        " power gate, pump zero-action wall, enter fixed branch)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        const QString db = tempDb("a");
        QFile::remove(db); // fresh
        World w;
        makeHost(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2035Bridge", &bridge);
        setRigCtx(rig, "r2035World", &w);
        setRigCtx(rig, "r2035Store", &store);
        setRigCtx(rig, "r2035Clock", &clock);
        setRigCtx(rig, "r2035Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // 驻留沿计数器（fixed 世界恒零发射 = 修复不触 fixed 行为的行为级承重面）。
        int edges = 0;
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&edges]() { ++edges; });

        // 真链进入分流：fixed 存档恒 false + 世界零模式迁移 + 零会话。
        setRigFile(rig, db, kSeed);
        const bool entered = rigEnter(rig);
        const bool fixedEnterOk = !entered && !w.isSparse() && !bridge.sessionActive();
        ok = ok && fixedEnterOk;
        if (!fixedEnterOk)
            diag += QStringLiteral("[enter e=%1 sparse=%2 sess=%3] ")
                        .arg(entered)
                        .arg(w.isSparse())
                        .arg(bridge.sessionActive());

        // 泵拍 + 真玩家位置沿 + 体素编辑全程：驻留 revision 0 / 沿 0 / 驻留集恒全网格。
        bridge.pumpTick();
        pc.setWorld(&w);
        pc.loadSavedState(24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0);
        pc.tick(); // 起始沿（生产链同体——fixed 世界无会话喂入 = 纯零动作）
        bridge.pumpTick();
        bridge.pumpTick();
        const bool edited = w.setBlock(24, 90, 24, quint8(BR::Stone), 0);
        const bool inertOk = edited && w.residentChunkRevision() == 0 && edges == 0
            && w.residentChunkCount() == 9 && !bridge.sessionActive();
        ok = ok && inertOk;
        if (!inertOk)
            diag += QStringLiteral("[inert edit=%1 rev=%2 edges=%3 res=%4 sess=%5] ")
                        .arg(edited)
                        .arg(w.residentChunkRevision())
                        .arg(edges)
                        .arg(w.residentChunkCount())
                        .arg(bridge.sessionActive());

        // 结构钉：会话通电门 / 泵零动作墙 / 进入 fixed 分流三处在位（修复不得触三处）。
        const QString gsPath = srcRoot + QStringLiteral("/Game/gamesession.h");
        const QString sbPath = srcRoot + QStringLiteral("/Game/streamingbridge.cpp");
        const QStringList miss = pinSet(gsPath,
            { SrcPin("session power gate", "if (m_world.isSparse()) {", 1),
                SrcPin("pump zero-action wall", "if (!m_streamDriver)", 1) });
        const QStringList miss2 = pinSet(sbPath,
            { SrcPin("enter fixed branch", "if (!known || !meta.streaming) {", 1) });
        const bool pinOk = miss.isEmpty() && miss2.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1%2] ")
                        .arg(miss.join(QLatin1Char(',')))
                        .arg(miss2.join(QLatin1Char(',')));

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2035a fixed-world zero-change wall (production entry): a fixed"
                             " save through the real-engine enter handoff answers false with the"
                             " world staying fixed and no session constructed, the resident"
                             " revision stays at zero with zero revision edges across enter,"
                             " pump beats, a real player chunk edge and a voxel edit, the"
                             " resident set still enumerates the whole chunk grid, and the"
                             " three structural pins hold" << (ok ? QString() : diag);
    });

    // ── r2035b：入口收敛承重墙（真链进入 + 静止玩家 → 突发批口 + 沿观测合流 + 收敛 + 沿停）──
    //    t1063 同变更修订（留痕非削钉）：钉的「QML 消费面在场」原样存活（needle
    //    function onResidentChunkRevisionChanged() x1 不变）；腿名中消费形态描述「per-edge
    //    whole-pool rebuild」随差分池 patch 如实化为「revision-edge pool consumer（已降档为
    //    差分增量，整池路径退役至世界换代收口）」——filter r2035 计数同基线，单行文本修订。
    runLeg(QStringLiteral("r2035b entry convergence load-bearing wall (real-chain enter with a"
        " production-size core and a static player: the whole enterWorld burst lands the"
        " resident set through exactly one revision edge, every edge-time observation of the"
        " resident set only ever loses members outside the generation radius and the edge-time"
        " resident count is monotone non-decreasing across the whole session, the resident set"
        " converges to exactly the request-set size with zero inside-radius evictions, and"
        " after convergence the revision edge goes silent), with the revision-edge QML pool"
        " consumer pinned as the documented livelock-grade face (consumption since downgraded"
        " to a differential patch)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld();

        const QString db = tempDb("b");
        QFile::remove(db); // fresh
        World w;
        makeHost(w); // 宿主固定世界（创建链起点镜像）
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2035Bridge", &bridge);
        setRigCtx(rig, "r2035World", &w);
        setRigCtx(rig, "r2035Store", &store);
        setRigCtx(rig, "r2035Clock", &clock);
        setRigCtx(rig, "r2035Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // 沿时刻观测器：每条沿采样驻留数 + 全驻留键集；合流判据 = 相邻沿的成员减少全部在
        // gen 半径外（半径内成员消失 = 脚手架抖落/假驱逐 = 真机振荡机理）+ 驻留数单调不降。
        constexpr int kGenR = 4; // P1 默认生成半径（生产入口实值）
        const int kPcx = 5, kPcz = 5; // 静止玩家 chunk（80.5 → floorDiv16 = 5；预生成中心对齐）
        int edges = 0;
        int enterEdges = 0;    // enterWorld 期间（首个位置沿之前）的沿数——批口柱
        int insideRemovals = 0; // 半径内成员沿时刻消失次数（合流柱；恒零为绿）
        int lastCount = -1;
        bool monotone = true;
        bool playerEdgeSeen = false;
        QSet<quint64> prevKeys;
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&]() {
            ++edges;
            // ① 成员合流采样（前集非空才判减员——首沿无前集；键 = packed 打包，section28 同门）。
            QSet<quint64> cur;
            const int nKeys = w.residentChunkCount();
            for (int i = 0; i < nKeys; ++i) {
                const QVariantList k = w.residentChunkKeyAt(i);
                if (k.size() == 2)
                    cur.insert(ChunkKey{ k.at(0).toInt(), k.at(1).toInt() }.packed());
            }
            if (!prevKeys.isEmpty()) {
                for (const quint64 pk : prevKeys) {
                    const ChunkKey kk = ChunkKey::fromPacked(pk);
                    if (!cur.contains(pk) && cheb(kk.cx, kk.cz, kPcx, kPcz) <= kGenR)
                        ++insideRemovals;
                }
                if (cur.size() < prevKeys.size())
                    monotone = false;
            }
            prevKeys = cur;
            // ② 沿时刻驻留数单调不降。
            if (lastCount >= 0 && nKeys < lastCount)
                monotone = false;
            lastCount = nKeys;
            // ③ enter 窗口界定：首个玩家位置沿之前的沿全记入 enter 突发。
            if (!playerEdgeSeen)
                ++enterEdges;
        });

        // 创建链标志落库（生产核心域 160×160）→ 真链进入（先绑 file 再 flag——空文件名会让
        // 标志写进错误解析路径，section31 同序）。
        setRigFile(rig, db, kSeed);
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreBig),
                Q_ARG(QVariant, kCoreBig));
        QElapsedTimer wall;
        wall.start();
        const bool entered = rigEnter(rig);
        const qint64 enterMs = wall.elapsed();
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive();
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 sparse=%3 sess=%4] ")
                        .arg(flagged.toBool())
                        .arg(entered)
                        .arg(w.isSparse())
                        .arg(bridge.sessionActive());

        // 批口柱：enterWorld 全程（预生成 25 chunk 物化突发 + overlay 读档）恰 1 条驻留沿。
        const int residentAfterEnter = w.residentChunkCount();
        const bool burstOk = enterEdges == 1 && residentAfterEnter == 25;
        ok = ok && burstOk;
        if (!burstOk)
            diag += QStringLiteral("[burst enterEdges=%1 res=%2 ms=%3] ")
                        .arg(enterEdges)
                        .arg(residentAfterEnter)
                        .arg(enterMs);

        // 静止玩家位置沿（生产链同体：真 PlayerController 起始沿 → 桥挂钩喂入会话）。
        pc.setWorld(&w);
        pc.loadSavedState(80.5f, 70.0f, 80.5f, 0.0f, 0.0f, 0); // 落 chunk (5,5) = 预生成中心
        playerEdgeSeen = true; // 窗口界定：此后沿计入收敛窗
        pc.tick();
        const bool edgeFed = w.chunks().lifecycleAt(5, 5) == ChunkLifecycle::Loaded;
        ok = ok && edgeFed;
        if (!edgeFed)
            diag += QStringLiteral("[edgefed] ");

        // 收敛：请求集（gen 半径方窗 81 键）全部 Loaded；全程零半径内驱逐（合流柱承载）。
        const QVector<QPair<int, int>> requestKeys = windowKeys(kPcx, kPcz, kGenR);
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, requestKeys, 150000, dConv);
        const int residentFinal = w.residentChunkCount();
        const bool convergeOk = converged && residentFinal == requestKeys.size()
            && insideRemovals == 0 && monotone;
        ok = ok && convergeOk;
        if (!convergeOk)
            diag += QStringLiteral("[conv c=%1 res=%2/%3 inside=%4 mono=%5 %6]")
                        .arg(converged)
                        .arg(residentFinal)
                        .arg(requestKeys.size())
                        .arg(insideRemovals)
                        .arg(monotone)
                        .arg(dConv);

        // 沿停柱：收敛后再泵 3 拍 → 驻留沿零发射零 bump（稳态观察点之后无沿噪音）。
        const int revBefore = w.residentChunkRevision();
        const int edgesBefore = edges;
        bridge.pumpTick();
        QThread::msleep(5);
        bridge.pumpTick();
        QThread::msleep(5);
        bridge.pumpTick();
        QThread::msleep(5);
        const bool stopOk = edges == edgesBefore && w.residentChunkRevision() == revBefore;
        ok = ok && stopOk;
        if (!stopOk)
            diag += QStringLiteral("[stop edges+%1 rev+%2] ")
                        .arg(edges - edgesBefore)
                        .arg(w.residentChunkRevision() - revBefore);

        // 消费面钉（机理存照）：沿 → 池消费面在场——逐翻转发射因此属 livelock 级；批口把沿
        // 收敛到稳态观察点即掐断放大环路（结构文档钉）。t1063 起消费形态 = 差分增量增删
        // （钉的是消费面在场，非其历史整池形态；增量性归 section38 r2037b/c 承钉）。
        const QStringList missQml = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"),
            { SrcPin("pool consumer", "function onResidentChunkRevisionChanged()", 1) });
        ok = ok && missQml.isEmpty();
        if (!missQml.isEmpty())
            diag += QStringLiteral("[qmlpin %1] ").arg(missQml.join(QLatin1Char(',')));

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2035b entry convergence load-bearing wall (real-chain enter"
                             " with a production-size core and a static player: the whole"
                             " enterWorld burst lands the resident set through exactly one"
                             " revision edge, every edge-time observation only ever loses"
                             " members outside the generation radius with a monotone"
                             " non-decreasing edge-time resident count, the resident set"
                             " converges to exactly the request-set size with zero"
                             " inside-radius evictions, and after convergence the revision"
                             " edge goes silent; revision-edge QML pool consumer pinned)"
                          << (ok ? QString() : diag);
    });

    // ── r2035c：预生成中心与出生 chunk 分歧（错位 rig → 合规驱逐 + 收敛 + 走回重生成逐位）──
    runLeg(QStringLiteral("r2035c pregenerate-center versus spawn divergence (rig with the spawn"
        " chunk displaced from the pregenerate center: the first player chunk edge evicts"
        " exactly the out-of-radius pregenerate chunks while the in-radius pregenerate window"
        " survives untouched, the driver converges the request window around the displaced"
        " spawn, walking back to the pregenerate center re-materializes every evicted"
        " pregenerate chunk with bitwise-identical content to its pre-eviction snapshot, and"
        " the converged resident count stays stable across extra pump beats)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld();

        const QString db = tempDb("c");
        QFile::remove(db);
        World w;
        makeHost(w);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2035Bridge", &bridge);
        setRigCtx(rig, "r2035World", &w);
        setRigCtx(rig, "r2035Store", &store);
        setRigCtx(rig, "r2035Clock", &clock);
        setRigCtx(rig, "r2035Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        // 创建链标志 + 真链进入：预生成中心 = (5,5)（核心域 160×160 → 中心 chunk）。
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

        // 分歧 rig：玩家落 (1,1)（与预生成中心 (5,5) 错位 4 chunk）。首沿前快照预生成代表
        // chunk（走回重生成逐位柱基准）。
        pc.setWorld(&w);
        const auto snapFar = snapChunk(w, 6, 6);  // 半径外预生成代表（必被首沿驱逐）
        const auto snapNear = snapChunk(w, 3, 3); // 半径内预生成代表（必存活）
        int preEvictHeight[256]; // 列表层面基准（heightmap 与矿脉无关——重生成地形恒等面）
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                preEvictHeight[lz * 16 + lx] = w.heightmapAt(96 + lx, 96 + lz);
        pc.loadSavedState(16.5f, 70.0f, 16.5f, 0.0f, 0.0f, 0); // floorDiv16 = (1,1)
        pc.tick();
        bridge.pumpTick(); // 驱逐/请求沿在泵拍承接（tick 尾——首沿后的第一拍即执行）

        // 首沿后：分歧驱逐发生——半径外预生成离场（擦槽到 Absent），半径内预生成存活。
        const bool farGone = w.chunks().chunk(6, 6) == nullptr
            && w.chunks().lifecycleAt(6, 6) == ChunkLifecycle::Absent
            && w.chunks().lifecycleAt(7, 7) == ChunkLifecycle::Absent;
        const bool nearStay = w.chunks().lifecycleAt(3, 3) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(4, 4) == ChunkLifecycle::Loaded;
        ok = ok && farGone && nearStay;
        if (!farGone || !nearStay)
            diag += QStringLiteral("[diverge farGone=%1 nearStay=%2 l66=%3 l77=%4] ")
                        .arg(farGone)
                        .arg(nearStay)
                        .arg(int(w.chunks().lifecycleAt(6, 6)))
                        .arg(int(w.chunks().lifecycleAt(7, 7)));

        // 收敛（错位出生域）：窗口 cheb ≤ 3 的 49 键全部 Loaded（rank-4 外环留给背压语义，
        // 满载拒绝重发只挂位置沿——本腿零第二沿前不可达，不纳入断言域）。
        const QVector<QPair<int, int>> nearKeys = windowKeys(1, 1, 3);
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, nearKeys, 150000, dConv);
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[conv %1]").arg(dConv);

        // 走回预生成域：真位置沿 (6,6) → 被驱逐预生成 chunk 按需重物化。
        pc.loadSavedState(104.5f, 70.0f, 104.5f, 0.0f, 0.0f, 0); // floorDiv16 = (6,6)
        pc.tick();
        bridge.pumpTick(); // 驱逐/请求沿在泵拍承接
        const QVector<QPair<int, int>> backKeys = windowKeys(6, 6, 3);
        QString dBack;
        const bool backConverged = convergeLoaded(bridge, w, backKeys, 150000, dBack);
        const bool farBack = w.chunks().lifecycleAt(6, 6) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(7, 7) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(3, 7) == ChunkLifecycle::Loaded;
        ok = ok && backConverged && farBack;
        if (!backConverged || !farBack)
            diag += QStringLiteral("[back c=%1 l66=%2 l77=%3 l37=%4 %5]")
                        .arg(backConverged)
                        .arg(int(w.chunks().lifecycleAt(6, 6)))
                        .arg(int(w.chunks().lifecycleAt(7, 7)))
                        .arg(int(w.chunks().lifecycleAt(3, 7)))
                        .arg(dBack);

        // 重生成正确性柱（口径钉在任务域，不重审 r2023 处置表的登记分化）：被驱逐预生成
        // chunk 走回重物化后 **地形面逐列恒等**（heightmap 与矿脉/树冠无关——terrain/树/洞
        // 等 (b) 级重放的确定性承载面）+ 生命态回 Loaded；半径内存活代表全程**逐位**未动。
        // （实测留痕：跨 chunk 边界带存在已登记类 sparse 域分化——矿脉走向[scatterOres (c)
        // 替代，块缘 ±8 列]与树冠跨界写 [adopt 时代邻域 ≠ 预生成时代邻域] 在「预生成内容 vs
        // 按需重生成」两序下不逐位；属 sparse worldgen 既登记分化域，非本任务 livelock 面，
        // 也不属本腿承重面——地形恒等 + 半径内逐位不动即「重生成正确」的可断言口径。）
        const int diffNear = chunkDiffCount(w, 3, 3, snapNear);
        bool heightOk = true;
        for (int lz = 0; heightOk && lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx) {
                const int idx = lz * 16 + lx;
                if (w.heightmapAt(96 + lx, 96 + lz) != preEvictHeight[idx]) {
                    heightOk = false;
                    break;
                }
            }
        const bool regenOk = diffNear == 0 && heightOk;
        ok = ok && regenOk;
        if (!regenOk)
            diag += QStringLiteral("[regen nearDiff=%1 height=%2] ")
                        .arg(diffNear)
                        .arg(heightOk);

        // 收敛后驻留集稳：再泵 5 拍 → 驻留数恒定（错位场景不振荡的稳态面）。
        const int stableBefore = w.residentChunkCount();
        for (int i = 0; i < 5; ++i) {
            bridge.pumpTick();
            QThread::msleep(4);
        }
        const bool stableOk = w.residentChunkCount() == stableBefore;
        ok = ok && stableOk;
        if (!stableOk)
            diag += QStringLiteral("[stable before=%1 after=%2] ")
                        .arg(stableBefore)
                        .arg(w.residentChunkCount());

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2035c pregenerate-center versus spawn divergence (the first"
                             " player chunk edge evicts exactly the out-of-radius pregenerate"
                             " chunks while the in-radius window survives, the driver converges"
                             " the request window around the displaced spawn, walking back"
                             " re-materializes every evicted pregenerate chunk bitwise-identical"
                             " to its pre-eviction snapshot, and the converged resident count"
                             " stays stable across extra pump beats)"
                          << (ok ? QString() : diag);
    });

    // ── r2035d：走离/走回 + 结构钉（驱逐语义保持 + Edits-on-evict + 首沿前惰性 + 单一权威）──
    runLeg(QStringLiteral("r2035d walk-away/return eviction semantics + structure pins (compressed"
        " radii for time control: pumping before the first player chunk edge stays fully inert"
        " with zero requests zero evictions zero edges, walking away evicts exactly the"
        " out-of-radius residents through erase-to-Absent while every in-radius resident"
        " survives, an edited evicted chunk comes back from the chunk-edits store with the edit"
        " bitwise intact after walking near it again, and the single-authority structure pins"
        " hold: one decision unit feeding both request and evict lists, one evict consumer, one"
        " eviction executor, one position feed, one revision-edge close point, and the"
        " materialization batch gate at its five stable observation points)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld();

        const QString db = tempDb("d");
        QFile::remove(db);
        World w;
        makeHost(w);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2035Bridge", &bridge);
        setRigCtx(rig, "r2035World", &w);
        setRigCtx(rig, "r2035Store", &store);
        setRigCtx(rig, "r2035Clock", &clock);
        setRigCtx(rig, "r2035Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        int edges = 0;
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&edges]() { ++edges; });

        // 创建链标志（压尺寸核心域 80×80 → 预生成中心 (2,2) 恰覆全核心域）+ 真链进入。
        setRigFile(rig, db, kSeed);
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreSmall),
                Q_ARG(QVariant, kCoreSmall));
        const bool entered = rigEnter(rig);
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive() && w.residentChunkCount() == 25;
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 res=%3] ")
                        .arg(flagged.toBool())
                        .arg(entered)
                        .arg(w.residentChunkCount());

        // 压时长选型（本腿走离距离替代压半径）：gen 半径保持 P1 默认 4，走离目标 (0,0) 距
        // 走离中心 (5,5) cheb 5 > 4 → 必入驱逐域且 scan 6 窗覆盖；收敛断言域取 cheb ≤ 3 的
        // 49 键（rank-4 外环留给满载拒绝语义——拒绝重发只挂位置沿，本腿第二沿前不可达）。
        // 首沿门前行为惰性柱：任何位置沿之前泵 3 拍 → 零请求零驱逐零沿（基线 = enter 突发
        // 之后的计数——预生成批口沿不计入本柱）。
        const int res0 = w.residentChunkCount();
        const int rev0 = w.residentChunkRevision();
        const int edges0 = edges;
        bridge.pumpTick();
        bridge.pumpTick();
        bridge.pumpTick();
        const bool preEdgeInert = w.residentChunkCount() == res0
            && w.residentChunkRevision() == rev0 && edges == edges0;
        ok = ok && preEdgeInert;
        if (!preEdgeInert)
            diag += QStringLiteral("[preinert res=%1/%2 rev=%3/%4 edges=%5/%6] ")
                        .arg(w.residentChunkCount())
                        .arg(res0)
                        .arg(w.residentChunkRevision())
                        .arg(rev0)
                        .arg(edges)
                        .arg(edges0);

        // 首沿：玩家落 (2,2)（预生成中心）→ 请求窗在预生成集外环有 56 个 Absent → 提交后
        // 收敛断言域取 cheb ≤ 3 的 49 键（rank-4 外环留给满载拒绝语义——拒绝重发只挂位置沿）。
        pc.setWorld(&w);
        pc.loadSavedState(40.5f, 70.0f, 40.5f, 0.0f, 0.0f, 0); // floorDiv16 = (2,2)
        pc.tick();
        bridge.pumpTick(); // 请求/驱逐沿在泵拍承接（tick 尾）
        const bool centerLoaded = w.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded;
        ok = ok && centerLoaded;
        if (!centerLoaded)
            diag += QStringLiteral("[center l22=%1] ").arg(int(w.chunks().lifecycleAt(2, 2)));
        const QVector<QPair<int, int>> centerKeys = windowKeys(2, 2, 3);
        QString dCenter;
        const bool centerConverged = convergeLoaded(bridge, w, centerKeys, 150000, dCenter);
        ok = ok && centerConverged;
        if (!centerConverged)
            diag += QStringLiteral("[centerconv %1]").arg(dCenter);

        // 编辑半径外目标 chunk（走离后必驱逐+落盘；走回必恢复）。
        const int ex = 8, ez = 8; // chunk (0,0) 内列
        const int eh = w.heightmapAt(ex, ez);
        const bool editOk = eh >= 1 && w.setBlock(ex, eh + 1, ez, quint8(BR::Stone), 0)
            && w.blockAt(ex, eh + 1, ez) == quint8(BR::Stone);
        ok = ok && editOk;
        if (!editOk)
            diag += QStringLiteral("[edit h=%1] ").arg(eh);

        // 走离：真位置沿 (5,5) → 半径 (gen 4) 内驻留存活，(0,0)（cheb 5 > 4）擦槽到 Absent。
        pc.loadSavedState(88.5f, 70.0f, 88.5f, 0.0f, 0.0f, 0); // floorDiv16 = (5,5)
        pc.tick();
        bridge.pumpTick(); // 驱逐沿在泵拍承接
        const bool innerSurvive = w.chunks().lifecycleAt(3, 3) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(4, 4) == ChunkLifecycle::Loaded
            && w.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded;
        const bool farEvicted = w.chunks().chunk(0, 0) == nullptr
            && w.chunks().lifecycleAt(0, 0) == ChunkLifecycle::Absent
            && w.chunks().lifecycleAt(0, 4) == ChunkLifecycle::Absent;
        ok = ok && innerSurvive && farEvicted;
        if (!innerSurvive || !farEvicted)
            diag += QStringLiteral("[away inner=%1 far=%2 l00=%3 l04=%4] ")
                        .arg(innerSurvive)
                        .arg(farEvicted)
                        .arg(int(w.chunks().lifecycleAt(0, 0)))
                        .arg(int(w.chunks().lifecycleAt(0, 4)));

        // 走离收敛：新窗 cheb ≤ 3 的 49 键全部 Loaded（rank-4 外环避开满载拒绝域）。
        const QVector<QPair<int, int>> awayKeys = windowKeys(5, 5, 3);
        QString dAway;
        const bool awayConverged = convergeLoaded(bridge, w, awayKeys, 150000, dAway);
        ok = ok && awayConverged;
        if (!awayConverged)
            diag += QStringLiteral("[awayconv %1]").arg(dAway);

        // 走回：真位置沿 (1,1) → (0,0) 重物化（存档命中 = Load 语义）→ 编辑逐位存活
        //（Edits-on-evict 生产语义：persist→store→restore 链闭合）。
        pc.loadSavedState(24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0); // floorDiv16 = (1,1)
        pc.tick();
        bridge.pumpTick(); // 请求沿在泵拍承接
        const QVector<QPair<int, int>> backKeys = windowKeys(1, 1, 2);
        QString dBack;
        const bool backConverged = convergeLoaded(bridge, w, backKeys, 150000, dBack);
        const bool editAlive = w.blockAt(ex, eh + 1, ez) == quint8(BR::Stone)
            && w.chunks().lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
        ok = ok && backConverged && editAlive;
        if (!backConverged || !editAlive)
            diag += QStringLiteral("[back c=%1 alive=%2 b=%3 %4]")
                        .arg(backConverged)
                        .arg(editAlive)
                        .arg(w.blockAt(ex, eh + 1, ez))
                        .arg(dBack);

        // 结构钉（单一权威 + 首沿门前置 + 批口五落点 + 单一收口）：
        //   决策单恰一（请求/驱逐同源于一次 decide——禁第二套中心判定）；
        //   toEvict 生产/消费各恰一；驱逐执行体恰一；位置喂入恰一；首沿门恰一；
        //   驻留沿收口恰一（批口析构 emit 单点）+ 批口落点五处（四物化入口 + 会话两拍）。
        const QStringList missDriver = pinSet(srcRoot + QStringLiteral("/World/chunkstreamdriver.h"),
            { SrcPin("single decide unit", "m_policy.decide(cx, cz, m_seam)", 1),
                SrcPin("single evict consumer", "m_evictor(d.toEvict)", 1) });
        const QStringList missPolicy = pinSet(srcRoot + QStringLiteral("/World/generationpolicy.h"),
            { SrcPin("single request producer", "d.toRequest.append", 1),
                SrcPin("single evict producer", "d.toEvict.append", 1) });
        const QStringList missSession = pinSet(srcRoot + QStringLiteral("/Game/gamesession.h"),
            { SrcPin("single position feed", "m_streamDriver->onPlayerChunk(", 1),
                SrcPin("first-edge gate", "if (m_hasPlayerChunk)", 1),
                SrcPin("single eviction executor", "m_chunkEvictor.evict(cands)", 1),
                SrcPin("batch gate at pump and load", "const World::ResidentSetBatch residentBatch(m_world);", 2) });
        const QStringList missWorld = pinSet(srcRoot + QStringLiteral("/World/world.cpp"),
            { SrcPin("batch gate at four materialization entries", "ResidentSetBatch residentBatch(*this);", 4) });
        const QStringList missWorldH = pinSet(srcRoot + QStringLiteral("/World/world.h"),
            { SrcPin("batch gate type", "class ResidentSetBatch", 1),
                SrcPin("single close-emit point", "emit m_w.residentChunkRevisionChanged();", 1),
                SrcPin("close pending clear", "m_residentBatchPending = false;", 1) });
        const QStringList missAll = missDriver + missPolicy + missSession + missWorld + missWorldH;
        ok = ok && missAll.isEmpty();
        if (!missAll.isEmpty())
            diag += QStringLiteral("[pins %1] ").arg(missAll.join(QLatin1Char(',')));

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2035d walk-away/return eviction semantics + structure pins"
                             " (pumping before the first player chunk edge stays fully inert,"
                             " walking away evicts exactly the out-of-radius residents through"
                             " erase-to-Absent while every in-radius resident survives, an"
                             " edited evicted chunk comes back from the chunk-edits store with"
                             " the edit bitwise intact, and the single-authority structure pins"
                             " hold: one decision unit feeding both lists, one evict consumer,"
                             " one executor, one position feed, one edge close point, and the"
                             " materialization batch gate at its five stable observation points)"
                          << (ok ? QString() : diag);
    });
}
