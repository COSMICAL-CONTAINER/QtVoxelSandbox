// tools/matrix/section44_residual_pair.cpp —— t1069 残余清偿合集 探针段（4 腿 r2043a-d；
// filter 词 r2043；矩阵 703→707）。置尾先例沿用（接 section43，runAll 末执行）。rig 世界
// w 零接触；真实用户 saves/ 零触碰（本段零临时库——fixed 小世界自足）。
//
// 任务契约（dev-plan t1069 两件一单）：
//   件一 = 入口分歧腿口径收窄（t1062 关单登记后续腿修订）：t1062 修掉在途邻读域漂移后，
//     入口分歧腿（预生成中心 vs 出生 chunk）的走回重生成承重口径由「地形面逐列恒等
//     （heightmap 基准）+ 半径内存活逐位」收窄为**全逐位**（含块缘 ±8 列带的矿脉/树冠边界
//     带）——修复成果转化为更强断言；旧宽口径退役留痕（腿名/PASS 文本如实化）。
//   件二 = Main.qml patch 时 `_meshSyncQueue` 全清改精确摘除（t1063 关单登记微优化）：
//     patchChunkSlotPool 旧体每次 patch 全清渐进同步队列（被销组悬空 geometry 引用防面，
//     欠账稳态扫描 ≤16 tick 重填）——改为只摘被销组条目、幸存组条目原序保留（旧全清把幸存
//     组已排队欠账一并作废 = 无害但浪费的重复渐进重建）。正确性铁律：①销组条目必须摘净
//     （悬空引用防面不弱化）②稳态扫描兜底语义不变 ③两消费端（重建窗口刷新 / 渐进同步
//     kick 全量重填）零触碰。
//
// 被测面拆解（三柱互补——section39/41 抽取先例 × section38 沿发生器合体）：Main.qml 是
//   Window × Quick3D 全量装配面 headless 不可整载，但 patchChunkSlotPool 单函数可**原文抽取**
//   （brace 配平——section41 extractBackoffWrapper 同门）嵌入最小环境 harness（根 Item id:
//   window 供生产体 `window.` 前缀解析 + 真世界 context 注入 theWorld + 段/geometry 身份件
//   镜像 _createChunkGroup + 队列归属诊断视图）→ **生产语句逐字执行**。比镜像 wrapper 强一档：
//   镜像测算法同构体（section38 同门），抽取测生产体本身——件二的行为柱因此直接钉在
//   Main.qml 语义上，生产体任何队列语义回归（含 NEG-1 全清回退）行为级即红。
//   「收窄承重」→ 源码钉（收窄后 regen 门逐字在场——摘带断言变异即红）+ 固定世界 comparator
//   结合面行为柱（全逐位比对在**旧 heightmap 基准盲区**的块缘带体素腐败必须被捕获 = 新断言
//   域的敏感面实证；正面对照 = 未腐快照零 diff）。
//   「结构钉」→ 纯源码钉（摘除谓词族 + 全清写点精确计数 + 两消费端契约句 + 稳态扫描兜底族
//   + 段入口 comparator 锚 + PASS 文本如实化锚 + 退役留痕锚）。
//
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   NEG-1（Main.qml 本体）= 摘除退化全清（patch 域精确摘除块回退 `window._meshSyncQueue
//     = []`）→ 声明红面 {r2043c, r2043d}（c 行为柱：幸存组队列项被全清 → 保留计数恰 4 ≠ 0
//     红 + 幸存 identity 柱红；d 摘除谓词源钉红 + 全清写点计数顶钉 [≥3] 红。r2043a 不误伤
//     ——池路径断言与队列零交集：全清不改池终态/churn/指针复用面；r2043b 不误伤——段源零
//     触碰）。
//   NEG-2（段文件本体）= 摘带断言（收窄 regen 门的 `diffFar == 0 &&` 变异 `diffFar >= 0 &&`
//     ——矿/树冠带承重被中和，diffFar 计算仍被消费 = 零编译告警的语义摘除）→ 声明红面
//     {r2043b}（门逐字钉红；comparator 行锚在 d 钉原样 → d 不误伤；行为柱 a/c 不误伤）。
//   阴性日志：build/ 下四件 matrix_r2043_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 腿名纪律：腿名/diag 文本零跨任务 filter 词元（r2018 P2 双向污染教训——本段只含 r2043 词元；
//   相邻族以「入口分歧腿 / 池 patch 段 / 时代切换段」指称，跨段词元只出现在钉 needle 与被钉
//   文件路径实参里，不进任何输出行）。
#include "matrix_helpers.h"

#include "chunklifecycle.h" // 被测面：六态转移驱动（驻留集沿发生器——section38 同门）

// ── 生产环境 harness 头（生产 patchChunkSlotPool 原文体所需最小自由变量面）────────────────
// id: window = 生产 Window id 的 harness 镜像（生产体 `window.` 前缀零改写解析到根 Item）；
// theWorld = context 注入真世界（生产 unqualified theWorld 同名）；段/geometry 身份件 =
// 生产 Model/ChunkGeometry 的身份镜像（组创建契约同构：段序 5/6 段、组首 geometry 进 geos）。
static const char *kPatchHarnessQml = R"QML(import QtQuick
Item {
    id: window
    property bool chunksBuilt: false
    property bool cutoutSegmentRestored: false        // t860 折叠默认（生产同值 → 每组 5 段）
    property var terrainGeos: []
    property var chunkObjects: []
    property var chunkKeys: []
    property var _meshSyncQueue: []                   // 渐进同步队列镜像（条目 = geometry 身份件）
    property int resets: 0                            // 账本失配兜底计数（本腿场景恒 0 = 兜底未达钉）
    property int statsRefreshes: 0                    // recomputeMeshStats 消费计数
    property int visibilityRefreshes: 0               // _refreshChunkVisibility 消费计数
    function resetChunkSlotPool() { resets++ }
    function recomputeMeshStats() { statsRefreshes++ }
    function _refreshChunkVisibility() { visibilityRefreshes++ }
    // 初建镜像（生产 rebuildChunkSlotPool 同构；尾步行玩家 chunk 缓存为 UI 面不在生产体被测域）。
    function rebuildChunkSlotPool() {
        if (window.chunksBuilt) return
        window.chunksBuilt = true
        var geos = [], objs = [], keys = []
        var n = theWorld.residentChunkCount()
        for (var i = 0; i < n; ++i) {
            var k = theWorld.residentChunkKeyAt(i)
            keys.push(k)
            window._createChunkGroup(k[0], k[1], geos, objs)
        }
        window.terrainGeos = geos
        window.chunkObjects = objs
        window.chunkKeys = keys
        window.recomputeMeshStats()
    }
    // 组创建镜像（生产 _createChunkGroup 同构：段序 5/6 段、组首 geometry 同时进 geos；
    //   生产 Model.geometry → 身份件 geometry，owner 反向指回段 = 队列条目归属诊断面）。
    function _createChunkGroup(cx, cz, geos, objs) {
        var segN = window.cutoutSegmentRestored ? 6 : 5
        for (var s = 0; s < segN; ++s) {
            var seg = segComp.createObject(null, { cx: cx, cz: cz, si: s })
            seg.geometry = geoComp.createObject(null, { owner: seg })
            objs.push(seg)
            if (s === 0) geos.push(seg.geometry)
        }
    }
    // 队列条目归属诊断视图（「patch 后 queued 只含幸存组」柱取数面——生产 ChunkGeometry 无
    //   owner 概念，本函数只在 harness 身份件上有意义，生产体零触碰）。
    function queueOwnerKeys() {
        var out = []
        for (var i = 0; i < window._meshSyncQueue.length; ++i)
            out.push(window._meshSyncQueue[i].owner.cx + "," + window._meshSyncQueue[i].owner.cz)
        return out
    }
    Component {
        id: segComp
        QtObject {
            property int cx: 0
            property int cz: 0
            property int si: 0
            property var geometry: null
        }
    }
    Component {
        id: geoComp
        QtObject { property var owner: null }
    }
    // 生产接线镜像（Main.qml 驻留沿 Connections 同构——沿时刻同步调生产 patch 体，section38
    //   同门；无此接线则沿后池/队列零动作 = 行为柱空转）。
    Connections {
        target: theWorld
        function onResidentChunkRevisionChanged() { window.patchChunkSlotPool() }
    }
)QML";

void MatrixRun::section44_residual_pair()
{
    constexpr int kW = 48, kD = 48, kH = 96, kSeed = 82; // fixed 小世界（3×3 chunk；section38 同族）
    constexpr int kResident = 9;                          // 3×3 固定网格全驻留
    constexpr int kStep = 5;                              // segmentsPerChunk 折叠默认（cutout 关）

    const QString projRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absolutePath();
    const QString srcRoot = projRoot + QStringLiteral("/src");
    const QString matrixRoot = projRoot + QStringLiteral("/tools/matrix");

    // fresh 小世界 incantation（section38 同款：48×48×96 s82 + 天气双钉——转换掷骰不进探针窗）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
        w.setWeatherState(0);              // Weather::Clear
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };

    // 生产 patchChunkSlotPool 原文抽取（brace 配平——section41 extractBackoffWrapper 同门）：
    //   生产体缺席/改名/体破损即 false（响亮红）。注释/字符串内 ASCII 花括号会被同计——
    //   现体注释内一对 {} 恰配平（"cx,cz" → { segs…geo } 行），失配时 comp.isError() 兜底红。
    const auto extractPatchFn = [&srcRoot](QString &out, QString &why) {
        QFile f(srcRoot + QStringLiteral("/ui/Main.qml"));
        const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                        : QString();
        const int iFn = src.indexOf(QStringLiteral("function patchChunkSlotPool()"));
        if (iFn < 0) {
            why = QStringLiteral("patch fn missing (pre-fix shape)");
            return false;
        }
        const int iOpen = src.indexOf(QLatin1Char('{'), iFn);
        int depth = 0;
        for (int i = iOpen; i >= 0 && i < src.size(); ++i) {
            const QChar c = src.at(i);
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}') && --depth == 0) {
                out = src.mid(iFn, i - iFn + 1);
                break;
            }
        }
        if (out.isEmpty()) {
            why = QStringLiteral("brace walk failed");
            return false;
        }
        return true;
    };

    // 真链 harness 装配（真 QQmlEngine + 告警计数 + theWorld 注入 + 抽取体落位；section41 同门）。
    struct PatchRig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makePatchRig = [&extractPatchFn](PatchRig &rig, World &w) {
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 真链先例同款：防磁盘缓存重定向
        QObject::connect(&rig.engine, &QQmlEngine::warnings, &rig.engine,
            [&rig](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++rig.warnings;
                    if (rig.firstWarning.isEmpty())
                        rig.firstWarning = e.toString();
                }
            });
        QString fn;
        QString why;
        if (!extractPatchFn(fn, why)) {
            rig.firstWarning = why; // 抽取失败 → wrap 恒空 → 腿红（诊断面）
            return;
        }
        rig.engine.rootContext()->setContextProperty(QStringLiteral("theWorld"), &w);
        QQmlComponent comp(&rig.engine);
        const QString qml = QString::fromLatin1(kPatchHarnessQml) + QLatin1Char('\n') + fn
            + QStringLiteral("\n}\n");
        comp.setData(qml.toUtf8(), QUrl());
        if (comp.isError()) {
            rig.firstWarning = comp.errorString();
            return;
        }
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine); // 引擎析构兜底
    };

    // ── C++ 断言视图帮手（键账本对账 / 段指针复用 / 队列条目身份）────────────────────────
    const auto ledgerKeys = [](QQuickItem *wrap) {
        QVector<QPair<int, int>> out;
        const QVariantList ks = wrap->property("chunkKeys").toList();
        for (const QVariant &k : ks) {
            const QVariantList kk = k.toList();
            if (kk.size() == 2)
                out.append({ kk.at(0).toInt(), kk.at(1).toInt() });
        }
        return out;
    };
    // 驻留枚举（真世界权威读面——对账参照；生产同款 count+keyAt 消费形态）。
    const auto residentKeys = [](World &w) {
        QVector<QPair<int, int>> out;
        const int n = w.residentChunkCount();
        for (int i = 0; i < n; ++i) {
            const QVariantList k = w.residentChunkKeyAt(i);
            if (k.size() == 2)
                out.append({ k.at(0).toInt(), k.at(1).toInt() });
        }
        return out;
    };
    const auto keysEq = [](const QVector<QPair<int, int>> &a, const QVector<QPair<int, int>> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i) != b.at(i))
                return false;
        return true;
    };
    // 组段指针捕获（复用柱：组 g 的 step 段 QObject* 列表——section38 同门）。
    const auto groupSegs = [](QQuickItem *wrap, int g, int step) {
        QList<QObject *> out;
        const QVariantList objs = wrap->property("chunkObjects").toList();
        for (int s = 0; s < step; ++s)
            out << objs.at(g * step + s).value<QObject *>();
        return out;
    };
    const auto sameSegs = [](const QList<QObject *> &a, const QList<QObject *> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i) != b.at(i))
                return false;
        return true;
    };
    // 组 geometry 列表（队列条目种子 = 生产 kick/稳态扫描同款 seg.geometry 拉取形态）。
    const auto groupGeos = [](QQuickItem *wrap, int g, int step) {
        QVariantList out;
        const QVariantList objs = wrap->property("chunkObjects").toList();
        for (int s = 0; s < step; ++s) {
            QObject *seg = objs.at(g * step + s).value<QObject *>();
            if (seg)
                out << seg->property("geometry");
        }
        return out;
    };
    const auto queuePtrs = [](QQuickItem *wrap) {
        QList<QObject *> out;
        const QVariantList q = wrap->property("_meshSyncQueue").toList();
        for (const QVariant &v : q)
            out << v.value<QObject *>();
        return out;
    };
    const auto setQueue = [](QQuickItem *wrap, const QVariantList &geos) {
        wrap->setProperty("_meshSyncQueue", geos);
    };
    const auto varListToPtrs = [](const QVariantList &v) {
        QList<QObject *> out;
        for (const QVariant &e : v)
            out << e.value<QObject *>();
        return out;
    };
    const auto countIn = [](const QList<QObject *> &hay, const QList<QObject *> &needles) {
        int n = 0;
        for (QObject *nd : needles)
            if (hay.contains(nd))
                ++n;
        return n;
    };
    // 队列条目归属键（harness queueOwnerKeys 视图 → 键对列表）。
    const auto queueOwners = [](QQuickItem *wrap) {
        QVector<QPair<int, int>> out;
        QVariant res;
        if (QMetaObject::invokeMethod(wrap, "queueOwnerKeys", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, res))) {
            const QVariantList owners = res.toList();
            for (const QVariant &o : owners) {
                const QStringList parts = o.toString().split(QLatin1Char(','));
                if (parts.size() == 2)
                    out.append({ parts.at(0).toInt(), parts.at(1).toInt() });
            }
        }
        return out;
    };
    const auto ownersInLedger = [](const QVector<QPair<int, int>> &owners,
                                   const QVector<QPair<int, int>> &ledger) {
        for (const auto &o : owners) {
            bool found = false;
            for (const auto &k : ledger)
                if (k == o) {
                    found = true;
                    break;
                }
            if (!found)
                return false;
        }
        return true;
    };
    // 头注裸 contains（退役留痕锚在注释体——pinSet 剥注释会失配，raw 先例 section43 同门）。
    const auto rawContains = [](const QString &path, const char *needle) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        return QString::fromUtf8(f.readAll()).contains(QString::fromUtf8(needle));
    };

    // ── r2043a：既有行为零变化墙（相邻族计数同基线源钉 + 生产 patch 体路径行为逐位）────────
    runLeg(QStringLiteral("t1069 r2043a zero-change wall for existing behavior (the three adjacent"
        " probe families keep their exact four-leg entry counts in source and the differential"
        " pool patch path keeps its production behavior bit-identical: the verbatim patch body"
        " drives the initial build, a single-key eviction and a re-add against a real world,"
        " after every step the pool ledger equals the resident enumeration item for item with"
        " exactly five segments per group, surviving groups keep their segment objects across"
        " edges they are not part of, the shifted group returns to its canonical index with its"
        " objects while the re-added group lands fresh at its own canonical index, the stats and"
        " visibility consumers fire exactly once per membership-changing patch, an empty sync"
        " queue stays empty through every patch, and the fallback reset stays unreached)"),
        [&]() {
        bool ok = true;
        QString diag;

        // ① 相邻族腿名计数源钉（filter 计数同基线的源结构面；套件级跑数另证于验证链日志）。
        //    needle 含跨段词元 = 被钉源码事实（不进任何输出行）；pin id 保持词元洁净。
        const QStringList miss36 = pinSet(matrixRoot + QStringLiteral("/section36_streaming_entry.cpp"),
            { SrcPin("t1069 entry-family leg count", "runLeg(QStringLiteral(\"r2035", 4) });
        const QStringList miss38 = pinSet(matrixRoot + QStringLiteral("/section38_slot_pool_patch.cpp"),
            { SrcPin("t1069 pool-family leg count", "runLeg(QStringLiteral(\"r2037", 4) });
        const QStringList miss40 = pinSet(matrixRoot + QStringLiteral("/section40_slot_pool_fixed_switch.cpp"),
            { SrcPin("t1069 era-family leg count", "runLeg(QStringLiteral(\"r2039", 4) });
        for (const QString &m : miss36 + miss38 + miss40) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② Main.qml 池路径契约钉（件二不改池语义面的结构面——NEG-1 全清回退不触此列）。
        const QStringList missPool = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("t1069 pool groups ledger", "const groups = {}", 1),
            SrcPin("t1069 pool geos rebind", "window.terrainGeos = geos", 2),
            SrcPin("t1069 pool keys rebind", "window.chunkKeys = newKeys", 1),
            SrcPin("t1069 pool stats gate", "if (created > 0 || destroyed > 0) {", 1),
        });
        for (const QString &m : missPool) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 生产 patch 体路径行为逐位（原文抽取执行——初建/单键驱逐/重加全链）。
        World w;
        freshWorld48(w);
        PatchRig rig;
        makePatchRig(rig, w);
        if (!rig.wrap) {
            ok = false;
            diag += QStringLiteral("[rig %1] ").arg(rig.firstWarning);
        }

        if (rig.wrap) {
            QMetaObject::invokeMethod(rig.wrap, "patchChunkSlotPool"); // 池未成型兜底 → 初建
            const int n0 = w.residentChunkCount();
            const bool buildOk = w.residentChunkRevision() == 0
                && rig.wrap->property("chunksBuilt").toBool()
                && rig.wrap->property("chunkKeys").toList().size() == n0
                && rig.wrap->property("chunkObjects").toList().size() == n0 * kStep
                && rig.wrap->property("terrainGeos").toList().size() == n0
                && keysEq(ledgerKeys(rig.wrap), residentKeys(w))
                && rig.wrap->property("statsRefreshes").toInt() == 1
                && rig.wrap->property("visibilityRefreshes").toInt() == 0;
            ok = ok && buildOk;
            if (!buildOk)
                diag += QStringLiteral("[build n=%1 keys=%2 objs=%3 geos=%4 led=%5 st=%6 vis=%7] ")
                            .arg(n0)
                            .arg(rig.wrap->property("chunkKeys").toList().size())
                            .arg(rig.wrap->property("chunkObjects").toList().size())
                            .arg(rig.wrap->property("terrainGeos").toList().size())
                            .arg(keysEq(ledgerKeys(rig.wrap), residentKeys(w)))
                            .arg(rig.wrap->property("statsRefreshes").toInt())
                            .arg(rig.wrap->property("visibilityRefreshes").toInt());

            // 单键驱逐（⑥ 即发即消费——生产 Connections 同步消费同门）：池缩 5 段、账本 ≡
            // 驻留枚举、幸存组指针逐位不动、空队列保持空、消费面各恰一次。
            QList<QObject *> g0 = groupSegs(rig.wrap, 0, kStep);
            const bool ev1 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
            QList<QObject *> g0b = groupSegs(rig.wrap, 0, kStep);
            QList<QObject *> gShift = groupSegs(rig.wrap, 4, kStep); // 移出窗内 (2,1) 占位 4
            const bool rmOk = ev1 && w.residentChunkCount() == kResident - 1
                && rig.wrap->property("chunkObjects").toList().size() == (kResident - 1) * kStep
                && keysEq(ledgerKeys(rig.wrap), residentKeys(w))
                && sameSegs(g0, g0b)
                && rig.wrap->property("_meshSyncQueue").toList().isEmpty()
                && rig.wrap->property("statsRefreshes").toInt() == 2
                && rig.wrap->property("visibilityRefreshes").toInt() == 1
                && rig.wrap->property("resets").toInt() == 0;
            ok = ok && rmOk;
            if (!rmOk)
                diag += QStringLiteral("[rm ev=%1 n=%2 led=%3 reuse=%4 q=%5 st=%6 vis=%7 rs=%8] ")
                            .arg(ev1)
                            .arg(w.residentChunkCount())
                            .arg(keysEq(ledgerKeys(rig.wrap), residentKeys(w)))
                            .arg(sameSegs(g0, g0b))
                            .arg(rig.wrap->property("_meshSyncQueue").toList().size())
                            .arg(rig.wrap->property("statsRefreshes").toInt())
                            .arg(rig.wrap->property("visibilityRefreshes").toInt())
                            .arg(rig.wrap->property("resets").toInt());

            // 重加链（⑦ 回离集 → ①②③ 重物化，恰 ③ 一条沿）：组数复原、(2,1) 回 canonical
            // index 5 且对象原样（规范序重排复用）、index 4 = 重加组新对象。
            const bool a0 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
            const bool l1 = a0 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading);
            const bool l2 = l1 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated);
            const bool l3 = l2 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
            QList<QObject *> g5post = groupSegs(rig.wrap, 5, kStep);
            QList<QObject *> g4post = groupSegs(rig.wrap, 4, kStep);
            const bool readdOk = a0 && l1 && l2 && l3 && w.residentChunkCount() == kResident
                && keysEq(ledgerKeys(rig.wrap), residentKeys(w))
                && sameSegs(gShift, g5post) && !sameSegs(gShift, g4post)
                && sameSegs(g0, groupSegs(rig.wrap, 0, kStep))
                && rig.wrap->property("statsRefreshes").toInt() == 3
                && rig.wrap->property("visibilityRefreshes").toInt() == 2
                && rig.wrap->property("resets").toInt() == 0;
            ok = ok && readdOk;
            if (!readdOk)
                diag += QStringLiteral("[readd %1%2%3%4 n=%5 led=%6 shift5=%7 shift4=%8 st=%9 vis=%10] ")
                            .arg(a0).arg(l1).arg(l2).arg(l3)
                            .arg(w.residentChunkCount())
                            .arg(keysEq(ledgerKeys(rig.wrap), residentKeys(w)))
                            .arg(sameSegs(gShift, g5post))
                            .arg(sameSegs(gShift, g4post))
                            .arg(rig.wrap->property("statsRefreshes").toInt())
                            .arg(rig.wrap->property("visibilityRefreshes").toInt());
        }

        const bool cleanOk = rig.warnings == 0 && rig.wrap != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1069 r2043a zero-change wall: the adjacent probe families keep"
                             " their four-leg entry counts and the verbatim production patch"
                             " body keeps the pool path bit-identical through initial build,"
                             " single-key eviction and re-add (ledger equals the resident"
                             " enumeration item for item, survivors keep their objects, the"
                             " shifted group returns at its canonical index, consumers fire"
                             " exactly once per membership patch, the empty sync queue stays"
                             " empty and the fallback reset stays unreached)"
                          << (ok ? QString() : diag);
    });

    // ── r2043b：口径收窄承重（收窄门逐字源钉 + 旧基准盲区敏感面行为柱 + 退役留痕锚）────────
    runLeg(QStringLiteral("t1069 r2043b narrowed bitwise load-bearing wall (the walk-back"
        " regeneration gate in the entry-divergence leg pins the full per-voxel comparison of"
        " the evicted pregenerate chunk in source with the retirement trace note present, and a"
        " fixed-world comparator harness proves the narrowed assertion domain binds beyond the"
        " retired column-height basis: the pristine snapshot copy compares clean, corrupting a"
        " copy voxel deep inside the block-edge band below the height basis is flagged exactly"
        " once, corrupting the topmost voxel of an opposite-edge band column is flagged exactly"
        " once, corrupting a core-column deep voxel is flagged exactly once, and each probe"
        " starts from a fresh copy so the three sensitivities are independent)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 收窄门逐字源钉（NEG-2 摘带断言的恰红靶心）+ 退役留痕锚（注释体 → raw contains）。
        const QStringList missGate = pinSet(
            matrixRoot + QStringLiteral("/section36_streaming_entry.cpp"),
            { SrcPin("t1069 narrow regen gate", "const bool regenOk = diffFar == 0 && diffNear == 0 && heightOk;", 1) });
        for (const QString &m : missGate) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool traceOk = rawContains(matrixRoot + QStringLiteral("/section36_streaming_entry.cpp"),
            "t1069 口径收窄");
        ok = ok && traceOk;
        if (!traceOk)
            diag += QStringLiteral("[trace missing] ");

        // ② comparator 敏感面行为柱（新断言域的承重实证——旧「列高基准」对体素腐败恒盲）。
        struct Vox
        {
            QVector<quint8> vox;
            QVector<quint8> sts;
        };
        const auto snap = [](World &w, int cx, int cz) {
            Vox s;
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
        const auto diffCount = [](const Vox &s, World &w, int cx, int cz) {
            int d = 0;
            for (int y = 0; y < kH; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        const int i = (y * 16 + lz) * 16 + lx;
                        if (s.vox[i] != w.blockAt(cx * 16 + lx, y, cz * 16 + lz)
                            || s.sts[i] != w.stateAt(cx * 16 + lx, y, cz * 16 + lz))
                            ++d;
                    }
            return d;
        };
        const auto corrupt = [](Vox &s, int lx, int lz, int y) {
            const int i = (y * 16 + lz) * 16 + lx;
            s.vox[i] = s.vox[i] == quint8(0) ? quint8(1) : quint8(0);
        };

        World w;
        freshWorld48(w);
        const Vox pristine = snap(w, 1, 1);

        // 探针 A：块缘带（lx=0 距边 0 列）+ 列高基准盲区（yA < heightmapAt = 旧宽口径恒盲区）。
        const int hA = w.heightmapAt(16 + 0, 16 + 8);
        const int yA = qBound(1, hA - 6, kH - 2);
        Vox pa = pristine;
        corrupt(pa, 0, 8, yA);
        // 探针 B：对面缘带列（lx=15 距边 0 列）柱顶体素（树冠/地表正脸）。
        Vox pb = pristine;
        int yTop = -1;
        for (int y = kH - 1; y >= 0; --y) {
            const int i = (y * 16 + 4) * 16 + 15;
            if (pb.vox[i] != quint8(0)) {
                yTop = y;
                break;
            }
        }
        if (yTop >= 0)
            corrupt(pb, 15, 4, yTop);
        // 探针 C：核心列对照（lx=8/lz=8 距边 ≥8 = 带外）——全逐位域不只在带内。
        const int hC = w.heightmapAt(24, 24);
        const int yC = qBound(1, hC - 6, kH - 2);
        Vox pc = pristine;
        corrupt(pc, 8, 8, yC);

        const int d0 = diffCount(pristine, w, 1, 1);
        const int dA = diffCount(pa, w, 1, 1);
        const int dB = diffCount(pb, w, 1, 1);
        const int dC = diffCount(pc, w, 1, 1);
        const bool sensOk = d0 == 0 && dA == 1 && dB == 1 && dC == 1 && yTop >= 0 && yA < hA;
        ok = ok && sensOk;
        if (!sensOk)
            diag += QStringLiteral("[sens d0=%1 dA=%2 dB=%3 dC=%4 yTop=%5 yA=%6 hA=%7] ")
                        .arg(d0).arg(dA).arg(dB).arg(dC).arg(yTop).arg(yA).arg(hA);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1069 r2043b narrowed bitwise load-bearing wall: the entry-"
                             "divergence leg pins the full per-voxel walk-back gate in source"
                             " with its retirement trace, and the comparator harness proves the"
                             " narrowed domain binds beyond the retired column-height basis"
                             " (pristine copy clean, band-deep, band-top and core-column"
                             " corruptions each flagged exactly once from independent copies)"
                          << (ok ? QString() : diag);
    });

    // ── r2043c：精确摘除承重（生产 patch 体原文执行：幸存条目保留计数 + 销组摘净 + 只含幸存组）──
    runLeg(QStringLiteral("t1069 r2043c precise-removal load-bearing leg (the verbatim production"
        " patch body runs against a real world with a pre-seeded sync queue: a membership patch"
        " that destroys one group keeps exactly the four survivor entries with their objects"
        " untouched while every one of the five destroyed-group entries is purged to exactly"
        " zero residue, the queue owner set stays a subset of the resident ledger, a"
        " no-destruction patch leaves the queue byte-identical with the re-created group never"
        " entering it, a second eviction purges only the evicted group's own three entries down"
        " to the single survivor, and the four segments retained through the first patch are the"
        " measured progressive rebuild work the retired full-clear would have discarded)"),
        [&]() {
        bool ok = true;
        QString diag;

        World w;
        freshWorld48(w);
        PatchRig rig;
        makePatchRig(rig, w);
        if (!rig.wrap) {
            ok = false;
            diag += QStringLiteral("[rig %1] ").arg(rig.firstWarning);
        }

        if (rig.wrap) {
            QMetaObject::invokeMethod(rig.wrap, "patchChunkSlotPool"); // 初建兜底（9 组）
            // canonical 序：0=(0,0) 4=(1,1) 8=(2,2)。队列种子 = 模拟 kick/稳态扫描已排队：
            // 被销组 (1,1) 全 5 段 + 幸存组 (0,0) 三段 + 幸存组 (2,2) 一段，共 9 条。
            const QVariantList doomedSeed = groupGeos(rig.wrap, 4, kStep);
            const QVariantList survivorAVar = groupGeos(rig.wrap, 0, kStep);
            const QVariantList survivorBVar = groupGeos(rig.wrap, 8, kStep);
            QVariantList seed = doomedSeed;
            seed << survivorAVar.at(0) << survivorAVar.at(1) << survivorAVar.at(2)
                 << survivorBVar.at(0);
            const QList<QObject *> doomedPtrs = varListToPtrs(doomedSeed);
            const QList<QObject *> keepA = varListToPtrs(
                QVariantList { survivorAVar.at(0), survivorAVar.at(1), survivorAVar.at(2) });
            const QList<QObject *> keepB = varListToPtrs(QVariantList { survivorBVar.at(0) });
            setQueue(rig.wrap, seed);
            const bool seedOk = rig.wrap->property("_meshSyncQueue").toList().size() == 9;
            ok = ok && seedOk;
            if (!seedOk)
                diag += QStringLiteral("[seed n=%1] ")
                            .arg(rig.wrap->property("_meshSyncQueue").toList().size());

            // 沿一：驱逐 (1,1)（销组）→ 幸存 4 条原序保留 + 销组 5 条摘净零残留 + 归属 ⊆ 账本。
            const bool ev1 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
            const QList<QObject *> q1 = queuePtrs(rig.wrap);
            const bool ev1Ok = ev1 && w.residentChunkCount() == kResident - 1
                && q1.size() == 4
                && countIn(q1, keepA) == 3 && countIn(q1, keepB) == 1
                && countIn(q1, doomedPtrs) == 0
                && ownersInLedger(queueOwners(rig.wrap), ledgerKeys(rig.wrap))
                && keysEq(ledgerKeys(rig.wrap), residentKeys(w));
            ok = ok && ev1Ok;
            if (!ev1Ok)
                diag += QStringLiteral("[ev1 ev=%1 q=%2 keepA=%3 keepB=%4 residue=%5 owners=%6 led=%7] ")
                            .arg(ev1)
                            .arg(q1.size())
                            .arg(countIn(q1, keepA))
                            .arg(countIn(q1, keepB))
                            .arg(countIn(q1, doomedPtrs))
                            .arg(ownersInLedger(queueOwners(rig.wrap), ledgerKeys(rig.wrap)))
                            .arg(keysEq(ledgerKeys(rig.wrap), residentKeys(w)));

            // 沿二：重加 (1,1)（建组无销组）→ 队列逐位不动（同四对象），新组 geometry 从不入队。
            const bool a0 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
            const bool l1 = a0 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading);
            const bool l2 = l1 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated);
            const bool l3 = l2 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
            const QVariantList freshGeos = groupGeos(rig.wrap, 4, kStep); // 重加组 canonical 归位 4
            const QList<QObject *> freshPtrs = varListToPtrs(freshGeos);
            const QList<QObject *> q2 = queuePtrs(rig.wrap);
            const bool readdOk = a0 && l1 && l2 && l3 && w.residentChunkCount() == kResident
                && q2.size() == 4
                && countIn(q2, keepA) == 3 && countIn(q2, keepB) == 1
                && countIn(q2, doomedPtrs) == 0 && countIn(q2, freshPtrs) == 0
                && ownersInLedger(queueOwners(rig.wrap), ledgerKeys(rig.wrap))
                && rig.wrap->property("resets").toInt() == 0;
            ok = ok && readdOk;
            if (!readdOk)
                diag += QStringLiteral("[readd %1%2%3%4 q=%5 keepA=%6 keepB=%7 fresh=%8 owners=%9 rs=%10] ")
                            .arg(a0).arg(l1).arg(l2).arg(l3)
                            .arg(q2.size())
                            .arg(countIn(q2, keepA))
                            .arg(countIn(q2, keepB))
                            .arg(countIn(q2, freshPtrs))
                            .arg(ownersInLedger(queueOwners(rig.wrap), ledgerKeys(rig.wrap)))
                            .arg(rig.wrap->property("resets").toInt());

            // 沿三：驱逐 (0,0)（幸存组入销域）→ 只摘它自己的三条 → 队列恰剩 (2,2) 一条。
            const bool ev2 = w.setChunkLifecycle(0, 0, ChunkLifecycle::Evicting);
            const QList<QObject *> q3 = queuePtrs(rig.wrap);
            const bool ev2Ok = ev2 && w.residentChunkCount() == kResident - 1
                && q3.size() == 1 && countIn(q3, keepB) == 1 && countIn(q3, keepA) == 0
                && countIn(q3, doomedPtrs) == 0
                && ownersInLedger(queueOwners(rig.wrap), ledgerKeys(rig.wrap))
                && keysEq(ledgerKeys(rig.wrap), residentKeys(w));
            ok = ok && ev2Ok;
            if (!ev2Ok)
                diag += QStringLiteral("[ev2 ev=%1 q=%2 keepB=%3 keepA=%4 residue=%5 led=%6] ")
                            .arg(ev2)
                            .arg(q3.size())
                            .arg(countIn(q3, keepB))
                            .arg(countIn(q3, keepA))
                            .arg(countIn(q3, doomedPtrs))
                            .arg(keysEq(ledgerKeys(rig.wrap), residentKeys(w)));

            // 消费面账目闭合：四次成员变化 patch（初建 + 三沿）各恰一次 stats/visibility；沿一
            // 保留的 4 段 = 旧全清口径会丢弃的渐进重建工作量（计量面：4 « 队列全空重扫）。
            const bool accountOk = rig.wrap->property("statsRefreshes").toInt() == 4
                && rig.wrap->property("visibilityRefreshes").toInt() == 3
                && rig.wrap->property("resets").toInt() == 0;
            ok = ok && accountOk;
            if (!accountOk)
                diag += QStringLiteral("[account st=%1 vis=%2 rs=%3] ")
                            .arg(rig.wrap->property("statsRefreshes").toInt())
                            .arg(rig.wrap->property("visibilityRefreshes").toInt())
                            .arg(rig.wrap->property("resets").toInt());
        }

        const bool cleanOk = rig.warnings == 0 && rig.wrap != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1069 r2043c precise-removal load-bearing leg: the verbatim"
                             " production patch body keeps exactly the four survivor queue"
                             " entries with their objects through the destroying patch, purges"
                             " the five destroyed-group entries to zero residue, keeps queue"
                             " owners inside the resident ledger, leaves the queue untouched on"
                             " the no-destruction patch with the re-created group never entering"
                             " it, purges only its own three entries on the second eviction, and"
                             " the four retained segments are the measured rebuild work saved"
                          << (ok ? QString() : diag);
    });

    // ── r2043d：结构钉（摘除谓词源钉 + 全清写点精确计数 + 两消费端/稳态兜底零触碰 + 段锚）────
    runLeg(QStringLiteral("t1069 r2043d structure pin (the main QML keeps the precise-removal"
        " predicate family verbatim - the doomed-geometry set, the per-segment collection in the"
        " destroy loop, the guarded keep-filter and the queue rebind - the retired full-clear now"
        " lives at exactly two sites with the producer entry and the world-transition reset, the"
        " two pool consumers keep their contract sentences and the steady-state fallback keeps"
        " its scan phase, owed-entry scan, enqueue and pump recheck, and the entry-divergence leg"
        " keeps its comparator anchor with the band-inclusive pass text and leg-name anchors)"),
        [&]() {
        bool ok = true;
        QString diag;

        // ① 摘除谓词族源钉（NEG-1 恰红靶心——退化为全清即失配）。
        // ② 全清写点精确计数：下界 ≥2（kick 全量重填 + 换代 reset）与上界 <3（patch 域已退役）
        //    双向夹出「恰两处」——第三处回潮即红。
        // ③ 两消费端 + 稳态兜底契约句零触碰（reverse probe：句在 = 未被顺手改写）。
        const QStringList missQml = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("t1069 doomed set", "const doomedGeos = []", 1),
            SrcPin("t1069 doomed collect", "doomedGeos.push(grp.segs[s].geometry)", 1),
            SrcPin("t1069 removal guard", "if (doomedGeos.length > 0) {", 1),
            SrcPin("t1069 keep filter", "if (doomedGeos.indexOf(stale[qi]) < 0) kept.push(stale[qi])", 1),
            SrcPin("t1069 queue rebind", "window._meshSyncQueue = kept", 1),
            SrcPin("t1069 full-clear count floor", "window._meshSyncQueue = []", 2),
            SrcPin("t1069 producer entry", "function kickWorldMeshSync()", 1),
            SrcPin("t1069 producer voiding", "seg.geometry.clearMesh()", 1),
            SrcPin("t1069 producer enqueue", "window._meshSyncQueue.push(groups[g].segs[gi])", 1),
            SrcPin("t1069 visibility consumer entry", "function _refreshChunkVisibility()", 1),
            SrcPin("t1069 visibility inRange write", "o.chunkInRange = inRange", 1),
            SrcPin("t1069 steady scan phase", "window._meshSyncScanPhase = (window._meshSyncScanPhase + 1) % 16", 1),
            SrcPin("t1069 steady owed scan", "if (geo.deferredRebuildPending() || (geo.lightStale() && geo.vertexCount > 0))", 1),
            SrcPin("t1069 steady enqueue", "window._meshSyncQueue.push(geo)", 1),
            SrcPin("t1069 pump recheck", "if (g.vertexCount > 0 && !g.deferredRebuildPending() && !g.lightStale()) continue", 1),
        });
        for (const QString &m : missQml) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList overClear = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"),
            { SrcPin("t1069 full-clear count ceiling", "window._meshSyncQueue = []", 3) });
        if (overClear.isEmpty()) {
            ok = false;
            diag += QStringLiteral("[full-clear sites >=3, want exactly 2] ");
        }

        // ④ 段入口新断言锚（comparator 行 + 近代表行 + PASS 文本如实化 + 腿名带域锚）。
        const QStringList miss36 = pinSet(
            matrixRoot + QStringLiteral("/section36_streaming_entry.cpp"), {
            SrcPin("t1069 comparator anchor", "const int diffFar = chunkDiffCount(w, 6, 6, snapFar);", 1),
            SrcPin("t1069 near-diff anchor", "const int diffNear = chunkDiffCount(w, 3, 3, snapNear);", 1),
            SrcPin("t1069 pass honesty anchor", "tree-canopy boundary bands", 1),
            SrcPin("t1069 leg-name band anchor", "boundary-band ore veins and tree canopies", 1),
            });
        for (const QString &m : miss36) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 退役留痕锚（注释体 → raw contains；pinSet 剥注释会失配——section43 raw 先例）。
        const bool trace36 = rawContains(matrixRoot + QStringLiteral("/section36_streaming_entry.cpp"),
            "t1069 口径收窄");
        const bool traceQml = rawContains(srcRoot + QStringLiteral("/ui/Main.qml"), "t1069 精确摘除");
        ok = ok && trace36 && traceQml;
        if (!trace36 || !traceQml)
            diag += QStringLiteral("[trace 36=%1 qml=%2] ").arg(trace36).arg(traceQml);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1069 r2043d structure pin: the removal predicate family stays"
                             " verbatim, the retired full-clear lives at exactly two sites, both"
                             " pool consumers and the steady-state fallback keep their contract"
                             " sentences, and the entry-divergence leg keeps its comparator"
                             " anchor, band-inclusive pass text and retirement traces"
                          << (ok ? QString() : diag);
    });
}
