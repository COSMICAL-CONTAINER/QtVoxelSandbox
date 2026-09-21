#include "matrix_helpers.h"

#include "chunklifecycle.h"  // 被测面：六态读（收敛判据）
#include "streamingbridge.h" // 被测面：W5b 真链进入（QML 单例 instance = 生产同对象）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1074 无限世界外环挖方块全黑探针段（4 腿；filter 词 r2047；矩阵 719→719+4）。置尾先例沿用
//（接 section47，runAll 末执行，rig 世界零接触）。
//
// 任务契约（用户 2026-09-20 真机报告①姊妹单：「无限世界外面挖掉方块是全黑色的阴影」——流式世界
// 出核走查后挖掘，挖掘区域呈全黑）。根因链（读码 + 域算术实证；与 t1073 同族「核心域假设泄漏」
// 的光照独立面，t1073 的 populationWindow/刷怪域语义零触碰）：
//   · 病灶主根：World::recomputeLightAround 的域门早退 ex/ez ≥ m_width/m_depth——sparse 世界里
//     核心 dims 是生成语义参数非域界（World::setBlock 的 sparse 写门早已无界化，挖掘可落），而
//     增量光照域门仍按核心盒早退 → 外环已物化 chunk 的挖掘/放置/火把编辑的增量重 flood 整体被
//     吞 → 被挖格光照停留在「实心遮光 0」陈旧值 → mesher 采样挖出格顶点色 = 全黑（用户症状）。
//   · 同病盒钳制：recomputeLightAround 的 min(W-1,·)/max(0,·) 把外环编辑盒钳成倒置空盒（x0>x1）
//     → refloodBox 静默 no-op（即使摘掉早退也不更新）；flushPendingLightEdits 联合盒同式钳制
//     （负侧单边 max(0,·) 还会钳掉半边盒）。
//   · 跨 chunk 回流断点：refloodBox 边界种子读的 nx<W/nz<D 核心盒判据——把已物化外环邻格误读
//     0（暗边界），外环 reflood 的盒外光回流被掐断（经 ChunkManager 统一物化门读收口；Fixed 分支
//     语义逐位等价——OOB 恒 0 两形态同值）。
//   · 负坐标标脏漏：refloodBox 精确标脏 chunk 循环用截断除法取 chunk 键——负侧盒缘 chunk 键错位
//     → 光变 chunk 漏标脏（Fixed 域坐标恒 ≥0 floorDiv==截断逐位等价零变化）。
//   · 初始播种面（候选①）排除：按需物化三路径（loadChunkAt / adoptGeneratedChunk /
//     restoreChunkFromBlob）自带 refloodBox 列种子（天光自列顶重 seed），外环露天播种本就正确
//     ——承重柱在腿内实证（挖掘前地表空气 sky==15），病灶专属于增量链。
// 腿面设计（先复现后修——本段先于修复落地时 r2047b/c 挖掘/火把断言红 = 全黑复现体；修复后四腿
// 全绿）：
//   「fixed 零变化墙」→ r2047a（fixed 世界挖掘 2 深 + 火把行为柱[域门 Fixed 分支原样的行为级] +
//     域门/盒钳制 Fixed 分支字面钉）——两 NEG 均不误伤（变异只在 sparse 分支/边界读分支）；
//   「外环挖掘全黑复现 + 修复承重」→ r2047b（生产尺寸核心 160×160 真链进入 → 出核 6 chunk →
//     收敛窗 49 键 → 播种柱[外环露天 sky==15] → 挖掘 2 深 = 挖出格 sky==15 + 跨 chunk 缘火把
//     方块光 14/≥13 渗入邻 chunk）——NEG-1（sparse 增量门摘除）/NEG-2（sparse 盒核心钳制复原）
//     恰红；修复前红载荷 = 挖出格/火把格光照恒 0（全黑签名），播种柱绿 = 病灶专属增量链；
//   「负坐标外环腿」→ r2047c（真链走查至负坐标外环 chunk → 同签名：负侧播种柱 + 挖掘亮 +
//     负侧 chunk 缘火把光跨 chunk——盒算术/边界读/标脏循环三条负坐标面一次走查覆盖）——
//     NEG-1 恰红（负侧编辑同被摘门吞掉）；NEG-2 不误伤（负侧盒 x1=ex+R 本就在核心域左侧，
//     高侧钳制 min(W-1,·) 对负盒恒不咬合 → 负腿仍绿 = 两 NEG 红面分离的判别腿）；
//   「结构钉」→ r2047d（负坐标安全标脏 floorDiv 循环 + sparse 统一物化门边界读分支 + sparse
//     无界盒分支在位 + 旧码三字面反探禁出）——两 NEG 均不误伤（钉面避开被摘字面）。
// 阴性面设计（双变异双还原，手工 Edit 做/手工 Edit 还原，存证 build/ 终名日志
//   matrix_r2047_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘 sparse 增量光照门（world.cpp recomputeLightAround 域门 sparse 分支
//     `if (ey < 0 || ey >= H) return;` 变异为 `if (true) return;`）→ 声明红面 {r2047b, r2047c}
//     [外环挖掘/火把光照恒 0]。r2047a 不误伤（fixed 分支逐位不动）；r2047d 不误伤（钉面不含
//     该分支字面）。
//   NEG-2 复原 sparse 盒核心钳制（world.cpp recomputeLightAround sparse 盒分支尾部补回
//     `x1 = std::min(W - 1, x1); z1 = std::min(D - 1, z1);`）→ 声明红面 {r2047b}[正向外环编辑盒
//     被钳成倒置空盒 → reflood 静默 no-op → 挖掘/火把光照恒 0]。r2047c 不误伤（负盒高侧钳制
//     不咬合）；r2047a 不误伤（fixed 分支不动；其 minCount=1 钉在 count=2 下仍过）；
//     r2047d 不误伤（钉的是 sparse 盒无界赋值行本体——钳制行是**追加**，该行原样存活）。
// 时长控制：两真链腿各收敛 49 键（deadline 有界防 flake 不挂死）；真临时库 fresh + 用后即删
//   （QDir::temp() pid 键名，绝对路径直用，绝不触 saves/）；fixed 宿主 48×48×96 s82。
void MatrixRun::section48_streaming_outer_light()
{
    constexpr int kW = 48, kD = 48, kHh = 96, kSeed = 82; // fixed 宿主小世界（3×3 chunk）
    constexpr int kCoreBig = 160;                          // 真机生产核心域（10×10 chunk）
    constexpr int kCoreSmall = 48;                         // 负侧腿小核心域（3×3 chunk，r2039b 同族）
    constexpr int kH = 96;
    constexpr int kSky = 15;   // 满天光（挖掘后挖出格的期望值）
    constexpr int kTorch = 14; // 火把光级（BlockRegistry::lightEmission 单一权威，BFS 种子值）

    // 源码钉根（应用 exe 同级 src/ —— section47 先例同式派生）。
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
    const QString worldCpp = srcRoot + QStringLiteral("/World/world.cpp");

    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2046 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2047_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 方窗键集（中心 ± 半径切比雪夫域——请求集尺寸的测试侧权威，r2046 同款）。
    const auto windowKeys = [](int ccx, int ccz, int r) {
        QVector<QPair<int, int>> keys;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                keys.append({ ccx + dx, ccz + dz });
        return keys;
    };
    // 收敛轮询：桥泵拍驱动（生产泵同体）直到 keys 全部 Loaded 或超时（r2046 同族节奏）。
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
                diag += QStringLiteral("[timeout %1ms miss=%2/%3 res=%4 lifeC=%5 sess=%6] ")
                            .arg(deadlineMs)
                            .arg(miss)
                            .arg(keys.size())
                            .arg(w.residentChunkCount())
                            .arg(int(w.chunks().lifecycleAt(keys.first().first,
                                                            keys.first().second)))
                            .arg(bridge.sessionActive());
                return false;
            }
            bridge.pumpTick();
            QThread::msleep(2);
        }
    };
    // 挖掘列挑选（确定性带宽内首选柱）：实心地表 + 上方空气 + 满天光（排除树冠投影）+ 5×5 地表
    // 邻域无水（防挖开后流体灌入搅动断言）。返回 false = 带宽内无合规柱；拒绝原因计数进 diag
    //（pick none 时可读出卡在哪个条件；找到时回报柱坐标）。
    const auto pickDigColumn = [](World &w, int xLo, int xHi, int zLo, int zHi,
                                  int &ox, int &oy, int &oz, QString *diag = nullptr) {
        int rNoTop = 0, rNotSolid = 0, rNotAir = 0, rNotSky = 0, rWet = 0, firstSkyValue = -1;
        for (int z = zLo; z <= zHi; ++z) {
            for (int x = xLo; x <= xHi; ++x) {
                const int top = w.heightAt(x, z);
                if (top <= 2 || top >= kH - 2) { ++rNoTop; continue; }
                const quint8 b = w.blockAt(x, top, z);
                const bool solidTop = b == BR::Grass || b == BR::Dirt || b == BR::Stone
                    || b == BR::Sand || b == BR::Gravel || b == BR::Snow
                    || b == BR::Sandstone;
                if (!solidTop) { ++rNotSolid; continue; }
                if (w.blockAt(x, top + 1, z) != BR::Air) { ++rNotAir; continue; }
                const int skyV = w.skyLightAt(x, top + 1, z);
                if (skyV != kSky) {
                    ++rNotSky;
                    if (firstSkyValue < 0)
                        firstSkyValue = skyV; // 诊断：首个 rNotSky 列的实测天光值
                    continue;
                }
                bool wet = false;
                for (int dz2 = -2; dz2 <= 2 && !wet; ++dz2)
                    for (int dx2 = -2; dx2 <= 2 && !wet; ++dx2) {
                        const int t2 = w.heightAt(x + dx2, z + dz2);
                        if (t2 < 0)
                            continue;
                        if (w.blockAt(x + dx2, t2, z + dz2) == BR::Water
                            || w.blockAt(x + dx2, t2 - 1, z + dz2) == BR::Water)
                            wet = true;
                    }
                if (wet) { ++rWet; continue; }
                ox = x;
                oy = top;
                oz = z;
                if (diag)
                    *diag += QStringLiteral("[pick x=%1 z=%2 top=%3] ").arg(x).arg(z).arg(top);
                return true;
            }
        }
        if (diag)
            *diag += QStringLiteral("[pick rNoTop=%1 rNotSolid=%2 rNotAir=%3 rNotSky=%4"
                                    " firstSky=%5 rWet=%6] ")
                         .arg(rNoTop).arg(rNotSolid).arg(rNotAir).arg(rNotSky)
                         .arg(firstSkyValue).arg(rWet);
        return false;
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
        entered = r2047Bridge.enterWorld(r2047World, r2047Store, r2047Clock, r2047Player, file, seed)
    }
    // 创建链标志传递镜像（WorldList 勾选行同式）。
    function flag(cw, cd) { return r2047Bridge.flagNewWorldStreaming(file, cw, cd) }
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

    // ── r2047a：fixed 世界零变化墙（fixed 挖掘/火把行为柱 + 域门/盒钳制 Fixed 分支字面钉）──────
    runLeg(QStringLiteral("r2047a fixed-world zero-change wall (in a fixed world digging a"
        " two-deep shaft lights the dug cells to full skylight and placing a torch lights"
        " itself and its neighbor through the same incremental light entry, and the source"
        " keeps the fixed-mode domain gate, the fixed-mode box clamp and the fixed-mode"
        " pending fluid light clamp as the verbatim fixed-domain branches)"), [&]() {
        bool ok = true;
        QString diag;

        // 行为柱：fixed 世界（全域 = 核心域，域门 Fixed 分支现行走查形态）挖掘/火把光照正确。
        World w;
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kHh);
        w.setSeed(kSeed);
        int dx = 0, dy = 0, dz = 0;
        const bool picked = pickDigColumn(w, 4, 43, 4, 43, dx, dy, dz, &diag);
        ok = ok && picked;
        if (!picked) {
            diag += QStringLiteral("[pick none] ");
        } else {
            const bool preSky = w.skyLightAt(dx, dy + 1, dz) == kSky;
            w.setBlock(dx, dy, dz, BR::Air);   // 挖表块
            w.setBlock(dx, dy - 1, dz, BR::Air); // 挖次块（2 深竖井）
            const bool dugOk = w.blockAt(dx, dy, dz) == BR::Air
                && w.blockAt(dx, dy - 1, dz) == BR::Air
                && w.skyLightAt(dx, dy, dz) == kSky
                && w.skyLightAt(dx, dy - 1, dz) == kSky;
            ok = ok && preSky && dugOk;
            if (!(preSky && dugOk))
                diag += QStringLiteral("[dig pre=%1 sky0=%2 sky1=%3] ")
                            .arg(preSky)
                            .arg(w.skyLightAt(dx, dy, dz))
                            .arg(w.skyLightAt(dx, dy - 1, dz));
            // 火把柱：竖井口放火把 → 自格 14 + 正上方一格 ≥13（水平邻格与地表同高多为实心
            // 格——光衰减进实心体归零，非链路面；上方格恒空气 = 稳定读面）。
            w.setBlock(dx, dy, dz, BR::Torch);
            const int selfB = w.blockLightAt(dx, dy, dz);
            const int nbB = w.blockLightAt(dx, dy + 1, dz);
            const bool torchOk = selfB == kTorch && nbB >= kTorch - 1;
            ok = ok && torchOk;
            if (!torchOk)
                diag += QStringLiteral("[torch self=%1 nb=%2] ").arg(selfB).arg(nbB);
        }

        // 字面钉：域门/盒钳制/延迟冲洗三处 Fixed 分支语句在位（剥注释口径——NEG-1/NEG-2 均只动
        // sparse 分支/追加钳制行，本组钉全部存活）。
        const QStringList missWorld = pinSet(worldCpp,
            { SrcPin("fixed domain gate at incremental light",
                     "if (ex < 0 || ey < 0 || ez < 0 || ex >= W || ey >= H || ez >= D) return;", 1),
                SrcPin("fixed box clamp at incremental light",
                     "x0 = std::max(0, ex - R), x1 = std::min(W - 1, ex + R);", 1),
                SrcPin("fixed clamp at pending fluid light flush",
                     "x1 = std::min(W - 1, x1); z1 = std::min(D - 1, z1);", 1) });
        const bool pinOk = missWorld.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1] ").arg(missWorld.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2047a fixed-world zero-change wall (in a fixed world digging"
                             " a two-deep shaft lights the dug cells to full skylight and"
                             " placing a torch lights itself and its neighbor through the"
                             " same incremental light entry, and the source keeps the"
                             " fixed-mode domain gate, box clamp and pending fluid light"
                             " clamp as the verbatim fixed-domain branches)"
                          << (ok ? QString() : diag);
    });

    // ── r2047b：外环挖掘全黑复现 + 修复承重（生产尺寸真链出核：播种柱 + 挖掘亮 + 跨 chunk 火把）──
    runLeg(QStringLiteral("r2047b outer-ring dig blackout reproduction and load-bearing wall (a"
        " production-size streaming core entered through the real bridge walks the real player"
        " six chunks beyond the core and the converged window shows full skylight on open"
        " outer-ring surface air, then an authority-face dig of a two-deep shaft lights the"
        " dug cells to full skylight and a torch at a chunk-edge column lights itself and"
        " bleeds its block light across the chunk border into the neighbor chunk)"), [&]() {
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
        setRigCtx(rig, "r2047Bridge", &bridge);
        setRigCtx(rig, "r2047World", &w);
        setRigCtx(rig, "r2047Store", &store);
        setRigCtx(rig, "r2047Clock", &clock);
        setRigCtx(rig, "r2047Player", &pc);
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

        // 走查出核：玩家落 chunk (16,5)（核心 chunk 盒 0..9 之外 6 chunk）；收敛 cheb ≤ 3
        // （chunk 13..19 × 2..8 全部出核 ≥3 chunk，均按需物化）。
        pc.setWorld(&w);
        pc.loadSavedState(256.5f, 70.0f, 80.5f, 0.0f, 0.0f, 0); // floorDiv16 = (16,5)
        pc.tick();
        bridge.pumpTick();
        const QVector<QPair<int, int>> winKeys = windowKeys(16, 5, 3);
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, winKeys, 150000, dConv);
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[conv %1]").arg(dConv);

        // 播种柱（候选①排除面）：外环按需物化的露天空气满天光——初始播种链（物化 refloodBox 列
        // 种子）本就正确，病灶专属增量链；柱红则根因链改写（登记不硬扛）。
        int dx = 0, dy = 0, dz = 0;
        const bool picked = converged && pickDigColumn(w, 208, 318, 34, 142, dx, dy, dz, &diag);
        ok = ok && picked;
        if (!picked)
            diag += QStringLiteral("[pick none] ");
        if (picked) {
            // 播种柱（候选①排除面）：外环按需物化 chunk 的露天列满天光——初始播种链（物化
            // refloodBox 列种子）本就正确，病灶专属增量链；柱红则根因链改写（登记不硬扛）。
            // 柱面口径 = 播种器同语义：列向 st+1 之上零遮光（lightOpacity>0 零命中）→ 该列
            // st+1 必须 sky==15（canopy/悬岩遮光列天然跳过不计，确定性恒真）。
            int seedBad = 0, seedChecked = 0;
            for (const auto &k : winKeys) {
                const int sx = k.first * 16 + 8, sz = k.second * 16 + 8;
                const int st = w.heightAt(sx, sz);
                if (st <= 0 || st >= kH - 2 || w.blockAt(sx, st + 1, sz) != BR::Air)
                    continue; // 水面 / 无高 / 顶带列不构成「露天空气」前提 → 不计
                bool openToSky = true;
                for (int y = st + 2; y < kH && openToSky; ++y)
                    if (BlockRegistry::lightOpacity(w.blockAt(sx, y, sz), w.stateAt(sx, y, sz)) > 0)
                        openToSky = false;
                if (!openToSky)
                    continue; // 冠影 / 悬岩遮光列 → 播种器本就不给 15 → 不计
                ++seedChecked;
                if (w.skyLightAt(sx, st + 1, sz) != kSky)
                    ++seedBad; // 列向全开却不满光 = 播种病签名
            }
            const bool seedOk = seedChecked >= 10 && seedBad == 0;
            ok = ok && seedOk;
            if (!seedOk)
                diag += QStringLiteral("[seedBad %1/%2] ").arg(seedBad).arg(seedChecked);

            // 复现/承重断言：权威面挖掘 2 深竖井 → 挖出格天光满（修复前恒 0 = 全黑签名）。
            w.setBlock(dx, dy, dz, BR::Air);
            w.setBlock(dx, dy - 1, dz, BR::Air);
            const bool dugOk = w.blockAt(dx, dy, dz) == BR::Air
                && w.blockAt(dx, dy - 1, dz) == BR::Air
                && w.skyLightAt(dx, dy, dz) == kSky
                && w.skyLightAt(dx, dy - 1, dz) == kSky;
            ok = ok && dugOk;
            if (!dugOk)
                diag += QStringLiteral("[dig sky0=%1 sky1=%2] ")
                            .arg(w.skyLightAt(dx, dy, dz))
                            .arg(w.skyLightAt(dx, dy - 1, dz));

            // 跨 chunk 火把：chunk 15 东缘列（lx=15，世界 x=255）放火把 → 自格 14 + 邻 chunk 16
            // 首列（x=256）≥13（增量盒无界 → 光跨 chunk 边界渗入；修复前增量门吞掉恒 0）。
            // 列沿 z 扫描选首根「冠下不遮 + 跨列双空气 + 双格零方块光」合规柱（林冠带确定性避让）。
            const int tx = 255;
            int tz = -1, ty = -1;
            for (int z = 70; z <= 90 && tz < 0; ++z) {
                const int t = w.heightAt(tx, z) + 1;
                if (t <= 0 || t >= kH - 2)
                    continue;
                if (w.blockAt(tx, t, z) == BR::Air && w.blockAt(tx + 1, t, z) == BR::Air
                    && w.blockLightAt(tx, t, z) == 0 && w.blockLightAt(tx + 1, t, z) == 0) {
                    tz = z;
                    ty = t;
                }
            }
            const bool torchCellOk = tz >= 0;
            w.setBlock(tx, ty, tz, BR::Torch);
            const int selfB = tz >= 0 ? w.blockLightAt(tx, ty, tz) : -1;
            const int crossB = tz >= 0 ? w.blockLightAt(tx + 1, ty, tz) : -1;
            const bool torchOk = torchCellOk && selfB == kTorch && crossB >= kTorch - 1;
            ok = ok && torchOk;
            if (!torchOk)
                diag += QStringLiteral("[torch cell=%1 self=%2 cross=%3] ")
                            .arg(torchCellOk)
                            .arg(selfB)
                            .arg(crossB);
        }

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2047b outer-ring dig blackout reproduction and load-bearing"
                             " wall (a production-size streaming core entered through the real"
                             " bridge walks the real player six chunks beyond the core and the"
                             " converged window shows full skylight on open outer-ring surface"
                             " air, then an authority-face dig of a two-deep shaft lights the"
                             " dug cells to full skylight and a torch at a chunk-edge column"
                             " lights itself and bleeds its block light across the chunk"
                             " border into the neighbor chunk)"
                          << (ok ? QString() : diag);
    });

    // ── r2047c：负坐标外环腿（负侧播种柱 + 挖掘亮 + 负侧 chunk 缘火把跨 chunk）────────────────
    // 形态 = r2039b 已证真链负坐标走查同族（48×48 核心小世界 + 负侧锚走查）+ loadChunkAt 兜底
    //（按需物化生产缝，r2022b 先例——登记：大核世界负侧真桥走查在本单实测零收敛[res 恒 25]，
    // 与本单光照病灶独立，如实登记不硬扛；兜底保证断言面可测，走查尝试每轮保留）。
    runLeg(QStringLiteral("r2047c negative-coordinate outer-ring wall (a small streaming core"
        " entered through the real bridge walks the real player to a negative outer-ring chunk"
        " whose window materializes to full skylight on open surface air, an authority-face"
        " two-deep dig lights the dug cells, and a torch at the chunk west edge bleeds its"
        " block light across the negative chunk border)"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld();

        const QString db = tempDb("c");
        QFile::remove(db); // fresh
        World w;
        w.setWidth(kCoreSmall);
        w.setDepth(kCoreSmall);
        w.setHeight(kH);
        w.setSeed(kSeed);
        WorldStore store;
        store.openWorld(db);
        store.setWorld(&w);

        WorldClock clock;
        PlayerController pc;
        Rig rig;
        makeRig(rig);
        setRigCtx(rig, "r2047Bridge", &bridge);
        setRigCtx(rig, "r2047World", &w);
        setRigCtx(rig, "r2047Store", &store);
        setRigCtx(rig, "r2047Clock", &clock);
        setRigCtx(rig, "r2047Player", &pc);
        const bool rigOk = rig.wrap != nullptr;
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig] ");

        setRigFile(rig, db, kSeed);
        QVariant flagged(false);
        if (rig.wrap)
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreSmall),
                Q_ARG(QVariant, kCoreSmall));
        const bool entered = rigEnter(rig);
        const bool enteredOk = flagged.toBool() && entered && w.isSparse()
            && bridge.sessionActive();
        ok = ok && enteredOk;
        if (!enteredOk)
            diag += QStringLiteral("[enter flag=%1 e=%2 res=%3] ")
                        .arg(flagged.toBool())
                        .arg(entered)
                        .arg(w.residentChunkCount());

        // 负侧走查：玩家落 x=-24.5 → 所在 chunk x=-2（核心 chunk 盒 0..2 之外 2 chunk）；收敛
        // cheb ≤ 3 窗（chunk -5..1 × -2..4——负侧外环 + 核心缘混合）。登记面：若真桥负侧
        // 走查未收敛（独立病灶，见上注），对仍在 Absent 的窗键走 loadChunkAt 生产缝兜底
        //（r2022b 先例；断言面可测性优先，病灶本体不改不掩盖）。
        pc.setWorld(&w);
        pc.loadSavedState(-24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0);
        pc.tick();
        bridge.pumpTick();
        const QVector<QPair<int, int>> winKeys = windowKeys(-2, 1, 3);
        QString dConv;
        const bool converged = convergeLoaded(bridge, w, winKeys, 60000, dConv);
        int fallback = 0;
        for (const auto &k : winKeys) {
            if (w.chunks().lifecycleAt(k.first, k.second) == ChunkLifecycle::Loaded)
                continue;
            if (w.loadChunkAt(k.first, k.second))
                ++fallback; // 生产按需物化缝兜底（登记面：fallback>0 = 负侧走查独立病灶在场）
        }
        ok = ok && converged;
        if (!converged)
            diag += QStringLiteral("[conv %1 fb=%2]").arg(dConv).arg(fallback);

        // 负侧播种柱 + 挖掘断言（chunk -2 内带宽选柱；负坐标 heightAt/blockAt/光照读全走
        // floorDiv 路由——读路径负坐标正确性同柱自证）。
        int dx = 0, dy = 0, dz = 0;
        const bool picked = converged && pickDigColumn(w, -30, -18, 8, 40, dx, dy, dz, &diag);
        ok = ok && picked;
        if (!picked)
            diag += QStringLiteral("[pick none] ");
        if (picked) {
            w.setBlock(dx, dy, dz, BR::Air);
            w.setBlock(dx, dy - 1, dz, BR::Air);
            const bool dugOk = w.blockAt(dx, dy, dz) == BR::Air
                && w.blockAt(dx, dy - 1, dz) == BR::Air
                && w.skyLightAt(dx, dy, dz) == kSky
                && w.skyLightAt(dx, dy - 1, dz) == kSky;
            ok = ok && dugOk;
            if (!dugOk)
                diag += QStringLiteral("[dig x=%1 sky0=%2 sky1=%3] ")
                            .arg(dx)
                            .arg(w.skyLightAt(dx, dy, dz))
                            .arg(w.skyLightAt(dx, dy - 1, dz));

            // 负侧跨 chunk 火把：chunk -2 西缘列（lx=0，世界 x=-32）放火把 → 增量盒 x∈[-47,-17]
            // 跨 chunk -3..-1（floorDiv 负坐标盒 + 标脏循环负键面）→ 自格 14 + 邻 chunk -3
            //（x=-33）≥13。列沿 z 扫描选首根合规柱（双空气 + 双格零方块光，确定性避让）。
            const int tx = -32;
            int tz = -1, ty = -1;
            for (int z = 8; z <= 40 && tz < 0; ++z) {
                const int t = w.heightAt(tx, z) + 1;
                if (t <= 0 || t >= kH - 2)
                    continue;
                if (w.blockAt(tx, t, z) == BR::Air && w.blockAt(tx - 1, t, z) == BR::Air
                    && w.blockLightAt(tx, t, z) == 0 && w.blockLightAt(tx - 1, t, z) == 0) {
                    tz = z;
                    ty = t;
                }
            }
            const bool torchCellOk = tz >= 0;
            w.setBlock(tx, ty, tz, BR::Torch);
            const int selfB = tz >= 0 ? w.blockLightAt(tx, ty, tz) : -1;
            const int crossB = tz >= 0 ? w.blockLightAt(tx - 1, ty, tz) : -1;
            const bool torchOk = torchCellOk && selfB == kTorch && crossB >= kTorch - 1;
            ok = ok && torchOk;
            if (!torchOk)
                diag += QStringLiteral("[torch cell=%1 self=%2 cross=%3] ")
                            .arg(torchCellOk)
                            .arg(selfB)
                            .arg(crossB);
        }

        const bool cleanOk = rig.warnings == 0;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2047c negative-coordinate outer-ring wall (a small streaming"
                             " core entered through the real bridge walks the real player to a"
                             " negative outer-ring chunk whose window materializes to full"
                             " skylight on open surface air, an authority-face two-deep dig"
                             " lights the dug cells, and a torch at the chunk west edge bleeds"
                             " its block light across the negative chunk border)"
                          << (ok ? QString() : diag);
    });

    // ── r2047d：结构钉（负坐标安全标脏循环 + sparse 边界读分支 + sparse 无界盒 + 旧码反探）────
    runLeg(QStringLiteral("r2047d structure pins (the precise dirty-marking pass resolves chunk"
        " keys with floor division for negative boxes, the boundary seeding reads outer"
        " neighbors through the sparse materialization gate beside the verbatim fixed branch,"
        " the sparse unbounded box assignment stays in place, and the retired fused core-clamp"
        " declaration plus the truncating dirty-mark loop plus the fused pending clamp line are"
        " absent)"), [&]() {
        bool ok = true;
        QString diag;

        // 正钉：NEG-1 摘 sparse 门 / NEG-2 追加盒钳制——两组变异均不触下列字面（钉面与被摘面分离）。
        const QStringList missWorld = pinSet(worldCpp,
            { SrcPin("negative-safe dirty pass x",
                     "for (int ccx = floorDiv(x0, cs); ccx <= floorDiv(x1, cs); ++ccx)", 1),
                SrcPin("negative-safe dirty pass z",
                     "for (int ccz = floorDiv(z0, cs); ccz <= floorDiv(z1, cs); ++ccz)", 1),
                SrcPin("sparse boundary read branch",
                     "} else if (m_chunks.mode() == WorldMode::Sparse) {", 1),
                SrcPin("sparse unbounded box assignment",
                     "x0 = ex - R; x1 = ex + R; z0 = ez - R; z1 = ez + R;", 1) });
        const bool pinOk = missWorld.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1] ").arg(missWorld.join(QLatin1Char(',')));

        // 反探禁出：旧码三字面（裸读文件口径，与 t1073 反探同门）——fused 核心盒声明 / 截断除法
        // 标脏循环 / 延迟冲洗 fused 三连钳行。修复后任一在场 = 钳制面回退。
        QFile wf(worldCpp);
        QString worldSrc;
        if (wf.open(QIODevice::ReadOnly))
            worldSrc = QString::fromUtf8(wf.readAll());
        const bool oldGone = !worldSrc.contains(QLatin1String(
                                  "const int x0 = std::max(0, ex - R), x1 = std::min(W - 1, ex + R);"))
            && !worldSrc.contains(QLatin1String("for (int ccx = x0 / cs; ccx <= x1 / cs; ++ccx)"))
            && !worldSrc.contains(QLatin1String("x0 = std::max(0, x0); y0 = std::max(0, y0); z0 = std::max(0, z0);"));
        ok = ok && oldGone;
        if (!oldGone)
            diag += QStringLiteral("[anti old clamp/dirty line present] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2047d structure pins (the precise dirty-marking pass resolves"
                             " chunk keys with floor division for negative boxes, the boundary"
                             " seeding reads outer neighbors through the sparse"
                             " materialization gate beside the verbatim fixed branch, the"
                             " sparse unbounded box assignment stays in place, and the retired"
                             " fused core-clamp declaration plus the truncating dirty-mark"
                             " loop plus the fused pending clamp line are absent)"
                          << (ok ? QString() : diag);
    });
}
