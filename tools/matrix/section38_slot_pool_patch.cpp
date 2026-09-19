// tools/matrix/section38_slot_pool_patch.cpp —— t1063 Main.qml 差分池 patch 探针段（4 腿；
// filter 词 r2037；矩阵 679→683）。置尾先例沿用（接 section37，runAll 末执行）。rig 世界
// w 零接触；r2037a-c 真 QQmlEngine × 真 World × 池镜像 wrapper（r2021c 同门 harness 家族
// ——Main.qml 池消费逻辑最小复刻 + builds/destroys/patches 计数断言视图），r2037d 纯源码钉。
//
// 任务契约（dev-plan t1063）：r2021「整池重派生」选型的生产接线降档——驻留集沿的池重建从
// 「整池销毁重派生」改为「差分增量增删」：
//   「fixed 世界零变化」→ r2037a 墙（真桥进入握手恒 false + 驻留零沿 + 池零 patch 零重建 +
//     全体段 Model 指针逐位不动——fixed 成员集恒定 = diff 恒空 = 逐位旧行为）；
//   「增量增删的终态正确性」→ r2037b 增量等价承重墙（构造增删沿序列 → 每沿后池与「整池
//     重建参照」逐项恒等[keys 账本 ≡ 驻留枚举 / 每键 5 段 / 组首全局对齐 / 段组合] + 幸存组
//     指针复用柱 + 重加组归位 canonical index + 新对象柱）；
//   「增量性本身」→ r2037c 增量性计量腿（每沿 create/destroy 的段 Model 数 = 恰变化键的组
//     规模[单键沿 churn 5 « 整池 45]；批口双键突发收敛一条沿恰 churn 10——t1061 批口生产
//     形态同门；12 沿走查模拟 build−destroy 账本闭合到池尺寸 + 计数与发射数对账）；
//   「结构钉」→ r2037d（revision Connections 恰一处 + 增量入口路由 + 组创建单点化 + 组身份
//     账本属性 + 步长单源表达式三面同源 + 两消费端契约句零触碰反探 + 世界换代全量重派生
//     收口面[定义 + 流式进入路由函数域钉] + 玩法路径标记在位 + 生命周期决策禁入 QML）。
//
// 被测面拆解（为何 b/c 用镜像 wrapper 而 d 钉 Main.qml 本体）：Main.qml 是 Window × Quick3D
// 全量装配面，headless 不可整载；真链 harness 家族先例（r2021c）= 消费逻辑最小复刻镜像 ×
// 真 World（沿直达真引擎 Connections、QVariantList 键过真转换链）。镜像承「算法行为」、
// 源码钉承「生产接线形状」，两柱互补：NEG-1 变异 Main.qml 本体（沿路由回归全量重派生）→
// 恰红 r2037d（源码钉柱）；NEG-2 变异镜像 wrapper 的复用分支（增量性丢失）→ 恰红
// {r2037b, r2037c}（行为柱）。两变异两个声明红面，互不重叠。
//
// 同变更钉面修订清单（t1063 落场，留痕非削钉——本段随附的两处修订均在本工作区提交内）：
//   ① r2021d（section24）：Main.qml 消费面精确计数三针随差分 patch 修订——count/keyAt x1→x2
//     （初建 + 差分 patch 各一消费点）、pool rebuild entry x3→x4（定义 + onCompleted +
//     patch 未成型兜底 + reset 尾调）；针文本与腿名同步如实化。修订后 r2021 计数 = 同基线
//     （修订行除外，逐行归因见提交正文）。
//   ② r2035b（section36）：消费面钉 needle（function onResidentChunkRevisionChanged() x1）
//     原样存活；腿名中「per-edge whole-pool rebuild」消费形态描述随差分 patch 如实化（钉
//     的是消费面在场，非其历史形态）——filter r2035 计数同基线，单行文本修订归因留痕。
//   ③ section29/section35 的 Main.qml 裸 contains（rebuildChunkSlotPool 在位）不受影响（针
//     仍在，零修订）。
//
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   NEG-1（Main.qml 本体）= 沿路由回归全量重派生（handler 体 window.patchChunkSlotPool() →
//     window.resetChunkSlotPool()）→ 恰红 {r2037d}（edge-routes-the-incremental-patch 计数
//     针 + handler 抽体钉双红；r2037a 不误伤——fixed 零沿，handler 不可达；r2021d/r2035b 不
//     误伤——handler 在场针与 rebuild 入口计数针均不含路由目标）。
//   NEG-2（本段镜像 wrapper）= 复用分支短路（if (grp) → if (false)，每沿全组重建）→ 恰红
//     {r2037b, r2037c}（b 幸存组指针复用柱红 / c churn 账本红[每沿销毁 = 整池而非变化键组
//     规模]；参照恒等柱不红——全重建终态仍逐项对；r2037a 不误伤[fixed 零沿 patch 不可达]；
//     r2037d 不误伤[Main.qml 零触碰]）。
//   阴性日志：build/ 下四件 matrix_r2037_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 腿名纪律：腿名/diag 文本零跨任务 filter 词元（r2018 P2 双向污染教训——本段只含 r2037 词元）。
#include "matrix_helpers.h"

#include "chunklifecycle.h"  // 被测面：六态转移驱动（增删沿序列的发生器）
#include "streamingbridge.h" // 被测面：真桥进入握手（fixed 恒 false——r2037a 承重面）

// 池镜像 wrapper（Main.qml 池消费逻辑最小复刻——r2021c 真链 harness 同门）：
//   buildAll = 初建镜像（驻留枚举逐键 step 段）；patchPool = 差分增量镜像（keys 账本 diff →
//   幸存组原对象复用 / 缺组补建 / 多组销毁 / 规范序重排）；resetPool = 全量重派生镜像。
//   builds/destroys/patches/resets 计数与 lastCreated/lastDestroyed 单沿增量 = C++ 断言视图。
//   段对象 = QtObject{cx,cz,si}（si = 组内段序——组组合/组首对齐的身份载体；生产的段 Model
//   换成轻量身份件，消费算法同构）。enter() = 真桥进入分流镜像（仅 r2037a 调用）。
static const char *kPoolWrapperQml = R"QML(import QtQuick
Item {
    property var world
    property string file: ""
    property int seed: 0
    property bool entered: false
    property var slots: []
    property var keys: []
    property int step: 5
    property bool builtOnce: false
    property int builds: 0
    property int destroys: 0
    property int patches: 0
    property int resets: 0
    property int lastCreated: 0
    property int lastDestroyed: 0
    function makeSeg(cx, cz, si) {
        builds++
        return segComp.createObject(null, { cx: cx, cz: cz, si: si })
    }
    function buildAll() {
        var objs = [], ks = []
        var n = world.residentChunkCount()
        for (var i = 0; i < n; ++i) {
            var k = world.residentChunkKeyAt(i)
            ks.push(k)
            for (var s = 0; s < step; ++s)
                objs.push(makeSeg(k[0], k[1], s))
        }
        slots = objs
        keys = ks
        builtOnce = true
    }
    function patchPool() {
        if (!builtOnce) { buildAll(); return }
        var newKeys = []
        var n = world.residentChunkCount()
        for (var i = 0; i < n; ++i)
            newKeys.push(world.residentChunkKeyAt(i))
        if (keys.length * step !== slots.length) { resetPool(); return }
        var groups = {}
        for (var g = 0; g < keys.length; ++g) {
            var segs = []
            for (var s = 0; s < step; ++s)
                segs.push(slots[g * step + s])
            groups[keys[g][0] + "," + keys[g][1]] = { segs: segs }
        }
        var objs = []
        var created = 0
        for (var i2 = 0; i2 < newKeys.length; ++i2) {
            var kk = newKeys[i2][0] + "," + newKeys[i2][1]
            var grp = groups[kk]
            if (grp) {
                delete groups[kk]
                for (var s2 = 0; s2 < step; ++s2)
                    objs.push(grp.segs[s2])
            } else {
                for (var s3 = 0; s3 < step; ++s3)
                    objs.push(makeSeg(newKeys[i2][0], newKeys[i2][1], s3))
                created++
            }
        }
        var destroyedNow = 0
        for (var rest in groups) {
            var rem = groups[rest]
            for (var s4 = 0; s4 < rem.segs.length; ++s4) {
                rem.segs[s4].destroy()
                destroys++
            }
            destroyedNow++
        }
        slots = objs
        keys = newKeys
        lastCreated = created
        lastDestroyed = destroyedNow
        patches++
    }
    function resetPool() {
        for (var i = 0; i < slots.length; ++i) {
            slots[i].destroy()
            destroys++
        }
        slots = []
        keys = []
        builtOnce = false
        resets++
        buildAll()
    }
    function enter() {
        entered = r2037Bridge.enterWorld(r2037World, r2037Store, r2037Clock, r2037Player, file, seed)
    }
    Component {
        id: segComp
        QtObject {
            property int cx: 0
            property int cz: 0
            property int si: 0
        }
    }
    Connections {
        target: world
        function onResidentChunkRevisionChanged() { patchPool() }
    }
}
)QML";

void MatrixRun::section38_slot_pool_patch()
{
    constexpr int kW = 48, kD = 48, kH = 96, kSeed = 82;
    constexpr int kResident = 9; // 3×3 固定网格全驻留（48/16 = 3；r2021a 同款方阵口径）
    constexpr int kStep = 5;     // segmentsPerChunk 折叠默认（cutout 关；生产同值）

    // fresh 小世界 incantation（section11/section24 同族：48×48×96 s82 + 天气双钉）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };

    // 池镜像真链装配（真 QQmlEngine + 沿计数 + setData wrapper；world 初参创建期注入）。
    struct PoolRig
    {
        QQmlEngine engine;
        QQuickItem *pool = nullptr;
        int warnings = 0;
        QString firstWarning;
        int edges = 0;
    };
    const auto makePool = [](PoolRig &rig, World &w) {
        qputenv("QML_DISABLE_DISK_CACHE", "1");
        QObject::connect(&rig.engine, &QQmlEngine::warnings, &rig.engine,
            [&rig](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++rig.warnings;
                    if (rig.firstWarning.isEmpty())
                        rig.firstWarning = e.toString();
                }
            });
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&rig]() { ++rig.edges; });
        // 五上下文全量注入（enter 镜像的桥/库/钟/玩家引用在 a 腿消费；b/c 腿零调用零噪声）。
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2037World"), &w);
        QQmlComponent comp(&rig.engine);
        comp.setData(kPoolWrapperQml, QUrl());
        if (comp.isError())
            return;
        rig.pool = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
            { { QStringLiteral("world"), QVariant::fromValue<QObject *>(&w) } }));
        if (rig.pool)
            rig.pool->setParent(&rig.engine); // 引擎析构兜底
    };
    // 驻留枚举镜像（C++ 侧与 QML 同款消费形态：count + keyAt 逐项拉取——禁绕过读面直取 chunks()）。
    const auto enumerateModel = [](World &w) {
        QVector<QPair<int, int>> keys;
        const int n = w.residentChunkCount();
        for (int i = 0; i < n; ++i) {
            const QVariantList k = w.residentChunkKeyAt(i);
            if (k.size() != 2)
                return QVector<QPair<int, int>>();
            keys.append({ k.at(0).toInt(), k.at(1).toInt() });
        }
        return keys;
    };
    // 整池重建参照对账：池终态 ≡ 「对当前驻留集跑一次 buildAll 会产出的形态」逐项恒等——
    //   keys 账本 ≡ 驻留枚举（逐项）、slots ≡ 每键 step 段（组 g = 枚举 g、段 si = 组内序、
    //   组首下标 = g*step 全局对齐）。红面 why 携首个失配点（归因入 diag）。
    const auto poolMatchesReference = [](QQuickItem *pool, World &w, QString &why) {
        const int step = pool->property("step").toInt();
        const QVariantList ks = pool->property("keys").toList();
        // 命名注意：C++ 侧禁用 `slots` 作标识符（Qt 宏展开），属性名字符串不受限。
        const QVariantList segVars = pool->property("slots").toList();
        const int n = w.residentChunkCount();
        if (ks.size() != n) {
            why = QStringLiteral("keys=%1 model=%2").arg(ks.size()).arg(n);
            return false;
        }
        if (segVars.size() != n * step) {
            why = QStringLiteral("slots=%1 want=%2").arg(segVars.size()).arg(n * step);
            return false;
        }
        for (int g = 0; g < n; ++g) {
            const QVariantList k = w.residentChunkKeyAt(g);
            const QVariantList lk = ks.at(g).toList();
            if (lk.size() != 2 || lk.at(0).toInt() != k.at(0).toInt()
                || lk.at(1).toInt() != k.at(1).toInt()) {
                why = QStringLiteral("ledger g=%1").arg(g);
                return false;
            }
            for (int s = 0; s < step; ++s) {
                QObject *seg = segVars.at(g * step + s).value<QObject *>();
                if (!seg || seg->property("cx").toInt() != k.at(0).toInt()
                    || seg->property("cz").toInt() != k.at(1).toInt()
                    || seg->property("si").toInt() != s) {
                    why = QStringLiteral("seg g=%1 s=%2").arg(g).arg(s);
                    return false;
                }
            }
        }
        return true;
    };
    // 组段指针捕获（复用柱：跨沿指针逐位不动 = 幸存组原 Model 复用的行为级证据）。
    const auto groupSegs = [](QQuickItem *pool, int g) {
        const int step = pool->property("step").toInt();
        const QVariantList segVars = pool->property("slots").toList();
        QList<QObject *> out;
        for (int s = 0; s < step; ++s)
            out << segVars.at(g * step + s).value<QObject *>();
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

    // ── r2037a：fixed 世界零变化墙（真桥进入握手恒 false + 驻留零沿 + 池零 patch 零重建）──
    runLeg(QStringLiteral("r2037a fixed-world zero-change wall (differential pool): a"
        " streaming-off world through the real bridge enter handoff answers false with the"
        " world staying fixed and no session constructed, the mirrored slot pool stays"
        " untouched with zero revision edges across the handoff, pump beats and voxel edits,"
        " the patch and rebuild counters stay at their initial-build values with every pooled"
        " segment pointer identical, and the engine stays warning-free"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        const QString db = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_r2037_a_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(db); // fresh（无流式标志行 → 握手恒 false）
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(db) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);
        WorldClock clock;
        PlayerController pc;

        PoolRig rig;
        makePool(rig, w);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2037Bridge"), &bridge);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2037Store"), &store);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2037Clock"), &clock);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2037Player"), &pc);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll"); // 初建（world 初参已注入）
            const QVariantList slots0 = rig.pool->property("slots").toList();
            QList<QObject *> segs0;
            for (const QVariant &v : slots0)
                segs0 << v.value<QObject *>();
            const int builds0 = rig.pool->property("builds").toInt();

            // 真桥进入握手（真引擎 wrapper 调用——五参类型化指针过真转换链）：fixed 存档恒
            // false + 世界零模式迁移 + 零会话。
            rig.pool->setProperty("file", db);
            rig.pool->setProperty("seed", kSeed);
            QMetaObject::invokeMethod(rig.pool, "enter");
            const bool entered = rig.pool->property("entered").toBool();
            const bool handOk = !entered && !w.isSparse() && !bridge.sessionActive();
            ok = ok && handOk;
            if (!handOk)
                diag += QStringLiteral("[hand e=%1 sparse=%2 sess=%3] ")
                            .arg(entered)
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive());

            // 泵拍 + 体素编辑 + 玩家沿：fixed 成员集恒定 → 零沿 → 差分路径零可达。
            bridge.pumpTick();
            pc.setWorld(&w);
            pc.loadSavedState(24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0);
            pc.tick();
            bridge.pumpTick();
            const bool edited = w.setBlock(24, 90, 24, quint8(BR::Stone), 0);
            QString why;
            const QVariantList slots1 = rig.pool->property("slots").toList();
            QList<QObject *> segs1;
            for (const QVariant &v : slots1)
                segs1 << v.value<QObject *>();
            const bool inertOk = edited && w.residentChunkRevision() == 0 && rig.edges == 0
                && w.residentChunkCount() == kResident && !bridge.sessionActive();
            ok = ok && inertOk;
            if (!inertOk)
                diag += QStringLiteral("[inert edit=%1 rev=%2 edges=%3 res=%4 sess=%5] ")
                            .arg(edited)
                            .arg(w.residentChunkRevision())
                            .arg(rig.edges)
                            .arg(w.residentChunkCount())
                            .arg(bridge.sessionActive());

            // 池零扰动柱：patches=0 / resets=0 / destroys=0 / builds=初建值 / 段指针逐位不动 /
            // 终态与整池重建参照逐项恒等。
            const bool poolOk = rig.pool->property("patches").toInt() == 0
                && rig.pool->property("resets").toInt() == 0
                && rig.pool->property("destroys").toInt() == 0
                && rig.pool->property("builds").toInt() == builds0
                && builds0 == kResident * kStep
                && sameSegs(segs0, segs1) && poolMatchesReference(rig.pool, w, why);
            ok = ok && poolOk;
            if (!poolOk)
                diag += QStringLiteral("[pool p=%1 rs=%2 d=%3 b=%4 ptr=%5 ref=%6 %7] ")
                            .arg(rig.pool->property("patches").toInt())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(rig.pool->property("destroys").toInt())
                            .arg(rig.pool->property("builds").toInt())
                            .arg(sameSegs(segs0, segs1))
                            .arg(poolMatchesReference(rig.pool, w, why))
                            .arg(why);
        }

        const bool cleanOk = rig.warnings == 0 && rig.pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        QFile::remove(db); // 用后即删（saves/ 零触碰）

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2037a fixed wall: the streaming-off enter handoff answers"
                             " false with the world staying fixed and no session, the mirrored"
                             " slot pool stays untouched (zero revision edges across handoff,"
                             " pump beats, a real player chunk edge and a voxel edit), patch"
                             " and rebuild counters stay at their initial-build values with"
                             " every pooled segment pointer identical" << (ok ? QString() : diag);
    });

    // ── r2037b：增量等价承重墙（每沿后池 ≡ 整池重建参照 + 幸存组复用柱 + 重加归位柱）────
    runLeg(QStringLiteral("r2037b incremental equivalence load-bearing wall (real-chain pool"
        " mirror): a real QQmlEngine drives the differential slot-pool consumption against a"
        " real world through a membership edge sequence - after every edge the pool matches"
        " the whole-rebuild reference item for item (the key ledger equals the resident"
        " enumeration, each group carries exactly five segments with globally aligned group"
        " heads, segments compose in group order), surviving groups keep their exact segment"
        " objects across edges they are not part of, every re-added group returns at its"
        " canonical index with freshly created segments while never-removed groups keep their"
        " objects, and the engine stays warning-free"), [&]() {
        bool ok = true;
        QString diag;

        World w;
        freshWorld48(w);
        PoolRig rig;
        makePool(rig, w);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        // 沿驱动的对账视图：每条沿后（handler 同线程同步跑完）池必须已与参照逐项恒等。
        const auto expectRef = [&](const char *tag) {
            QString why;
            const bool refOk = rig.pool && poolMatchesReference(rig.pool, w, why);
            ok = ok && refOk;
            if (!refOk)
                diag += QStringLiteral("[%1 ref %2] ").arg(QLatin1String(tag)).arg(why);
        };

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll");
            const int n0 = w.residentChunkCount();
            const bool buildOk = n0 == kResident
                && rig.pool->property("keys").toList().size() == n0
                && rig.pool->property("slots").toList().size() == n0 * kStep
                && rig.pool->property("patches").toInt() == 0;
            ok = ok && buildOk;
            if (!buildOk)
                diag += QStringLiteral("[build n=%1] ").arg(n0);
            expectRef("init");

            // ① 单键移出（⑥ Loaded→Evicting，沿即发即消费）：池缩到 8 组、参照恒等、幸存组
            //    （组 0 = (0,0)、组 8 = (2,2)）段指针跨沿逐位不动（复用柱——整池重派生下必变）。
            QList<QObject *> g0 = groupSegs(rig.pool, 0);
            QList<QObject *> g8 = groupSegs(rig.pool, 8);
            const bool ev1 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
            QList<QObject *> g0b = groupSegs(rig.pool, 0);
            QList<QObject *> g8b = groupSegs(rig.pool, 7);
            const bool rmOk = ev1 && rig.edges == 1
                && w.residentChunkCount() == kResident - 1
                && rig.pool->property("slots").toList().size() == (kResident - 1) * kStep
                && sameSegs(g0, g0b) && sameSegs(g8, g8b);
            ok = ok && rmOk;
            if (!rmOk)
                diag += QStringLiteral("[rm ev=%1 edges=%2 n=%3 reuse=%4/%5] ")
                            .arg(ev1)
                            .arg(rig.edges)
                            .arg(w.residentChunkCount())
                            .arg(sameSegs(g0, g0b))
                            .arg(sameSegs(g8, g8b));
            expectRef("after-rm");

            expectRef("after-rm");
            // 移出窗内 (2,1) 占位 index 4（其 canonical 位 = 5）——重加前捕获，作重排复用柱。
            const QList<QObject *> gShift = groupSegs(rig.pool, 4);

            // ② 重加链（⑦ 回离集 → ①②③ 重物化，恰 ③ 一条沿）：组数复原 9、(1,1) 归位
            //    canonical index 4、该组段对象为新建（销毁真实发生）；被移位挤开的 (2,1) 组
            //    （移出窗内占位 index 4 → 重加后回 canonical index 5）段对象原样——规范序
            //    重排复用柱（整池重派生下必换新对象）。
            const bool a0 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
            const bool l1 = a0 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading);
            const bool l2 = l1 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated);
            const bool l3 = l2 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
            QList<QObject *> g0c = groupSegs(rig.pool, 0);
            const bool readdOk = a0 && l1 && l2 && l3 && rig.edges == 2
                && w.residentChunkCount() == kResident
                && sameSegs(g0, g0c);
            ok = ok && readdOk;
            if (!readdOk)
                diag += QStringLiteral("[readd %1%2%3%4 edges=%5 n=%6] ")
                            .arg(a0).arg(l1).arg(l2).arg(l3)
                            .arg(rig.edges)
                            .arg(w.residentChunkCount());
            expectRef("after-readd");
            // (2,1) 回 canonical index 5 且对象原样（重排复用）；index 4 = 重加的 (1,1) 新对象。
            const QList<QObject *> g5post = groupSegs(rig.pool, 5);
            const QList<QObject *> g4post = groupSegs(rig.pool, 4);
            const bool reorderOk = sameSegs(gShift, g5post) && !sameSegs(gShift, g4post);
            ok = ok && reorderOk;
            if (!reorderOk)
                diag += QStringLiteral("[reorder shift==5:%1 shift==4:%2] ")
                            .arg(sameSegs(gShift, g5post))
                            .arg(sameSegs(gShift, g4post));

            // ③ 多键沿序列（双键先后移出 → 逆序重加）：全程参照恒等 + 首组对象跨四沿不动。
            g0 = groupSegs(rig.pool, 0);
            const bool e1 = w.setChunkLifecycle(0, 2, ChunkLifecycle::Evicting); // canonical 6
            expectRef("mid-multi");
            const bool e2 = e1 && w.setChunkLifecycle(2, 0, ChunkLifecycle::Evicting); // canonical 2
            const bool b1 = e2 && w.setChunkLifecycle(2, 0, ChunkLifecycle::Absent)
                && w.setChunkLifecycle(2, 0, ChunkLifecycle::Loading)
                && w.setChunkLifecycle(2, 0, ChunkLifecycle::Generated)
                && w.setChunkLifecycle(2, 0, ChunkLifecycle::Loaded);
            const bool b2 = b1 && w.setChunkLifecycle(0, 2, ChunkLifecycle::Absent)
                && w.setChunkLifecycle(0, 2, ChunkLifecycle::Loading)
                && w.setChunkLifecycle(0, 2, ChunkLifecycle::Generated)
                && w.setChunkLifecycle(0, 2, ChunkLifecycle::Loaded);
            QList<QObject *> g0d = groupSegs(rig.pool, 0);
            const bool multiOk = e1 && e2 && b1 && b2 && rig.edges == 6
                && w.residentChunkCount() == kResident && sameSegs(g0, g0d);
            ok = ok && multiOk;
            if (!multiOk)
                diag += QStringLiteral("[multi %1%2%3%4 edges=%5 n=%6 reuse=%7] ")
                            .arg(e1).arg(e2).arg(b1).arg(b2)
                            .arg(rig.edges)
                            .arg(w.residentChunkCount())
                            .arg(sameSegs(g0, g0d));
            expectRef("after-multi");

            // ④ 首组键 (0,0) 移出+重加 ×2（canonical 首位的组级增删；每轮恰 2 条沿）。
            bool cycleOk = true;
            for (int round = 0; round < 2 && cycleOk; ++round) {
                const bool ev = w.setChunkLifecycle(0, 0, ChunkLifecycle::Evicting);
                cycleOk = cycleOk && ev && rig.edges == 7 + round * 2;
                cycleOk = cycleOk && w.setChunkLifecycle(0, 0, ChunkLifecycle::Absent)
                    && w.setChunkLifecycle(0, 0, ChunkLifecycle::Loading)
                    && w.setChunkLifecycle(0, 0, ChunkLifecycle::Generated)
                    && w.setChunkLifecycle(0, 0, ChunkLifecycle::Loaded);
                cycleOk = cycleOk && rig.edges == 8 + round * 2
                    && w.residentChunkCount() == kResident;
                expectRef(round == 0 ? "cycle1" : "cycle2");
            }
            ok = ok && cycleOk;
            if (!cycleOk)
                diag += QStringLiteral("[cycle edges=%1 n=%2] ")
                            .arg(rig.edges)
                            .arg(w.residentChunkCount());

            // ⑤ 枚举口径闭合：keys 账本 ≡ C++ 侧驻留枚举逐项（终态）。
            const auto model = enumerateModel(w);
            const QVariantList ks = rig.pool->property("keys").toList();
            bool enumOk = ks.size() == model.size();
            for (int i = 0; enumOk && i < model.size(); ++i) {
                const QVariantList lk = ks.at(i).toList();
                enumOk = lk.size() == 2 && lk.at(0).toInt() == model.at(i).first
                    && lk.at(1).toInt() == model.at(i).second;
            }
            ok = ok && enumOk;
            if (!enumOk)
                diag += QStringLiteral("[enum n=%1/%2] ").arg(ks.size()).arg(model.size());
        }

        const bool cleanOk = rig.warnings == 0 && rig.pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2037b incremental equivalence wall: after every membership"
                             " edge the differentially patched pool matches the whole-rebuild"
                             " reference item for item (key ledger equals the enumeration,"
                             " five segments per group with aligned heads), surviving groups"
                             " keep their exact segment objects, re-added groups return at"
                             " their canonical index with fresh segments"
                          << (ok ? QString() : diag);
    });

    // ── r2037c：增量性计量腿（每沿 churn = 恰变化键组规模 + 批口突发 + 走查模拟账本闭合）──
    runLeg(QStringLiteral("r2037c incrementality accounting leg (real-chain pool mirror): each"
        " membership edge creates and destroys exactly the changed keys' group scale of"
        " segment objects - a single-key edge churns five segments and never the whole pool,"
        " a batched two-key materialization burst converges to one revision edge that churns"
        " exactly ten, a multi-round remove-and-re-add walk keeps every per-edge churn exact"
        " before an add-back pair restores the full domain, and the build-minus-destroy"
        " ledger closes to the pooled size with patches reconciling to the emission count"), [&]() {
        bool ok = true;
        QString diag;

        World w;
        freshWorld48(w);
        PoolRig rig;
        makePool(rig, w);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll");
            const int builds0 = rig.pool->property("builds").toInt();
            const bool initOk = builds0 == kResident * kStep
                && rig.pool->property("patches").toInt() == 0;
            ok = ok && initOk;
            if (!initOk)
                diag += QStringLiteral("[init b=%1] ").arg(builds0);

            // ① 单键沿：恰销毁 5 段、零创建（churn 5 « 整池 45——增量性本体）。
            const int d0 = rig.pool->property("destroys").toInt();
            const bool ev = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
            const int d1 = rig.pool->property("destroys").toInt();
            const bool rmOk = ev && rig.edges == 1 && d1 - d0 == kStep
                && rig.pool->property("builds").toInt() == builds0
                && rig.pool->property("lastDestroyed").toInt() == 1
                && rig.pool->property("lastCreated").toInt() == 0;
            ok = ok && rmOk;
            if (!rmOk)
                diag += QStringLiteral("[rm ev=%1 d=%2->%3 b=%4 ld=%5 lc=%6] ")
                            .arg(ev)
                            .arg(d0).arg(d1)
                            .arg(rig.pool->property("builds").toInt())
                            .arg(rig.pool->property("lastDestroyed").toInt())
                            .arg(rig.pool->property("lastCreated").toInt());

            // ② 重加沿：恰创建 5 段、零销毁。
            const int b0 = rig.pool->property("builds").toInt();
            const bool a0 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
            const bool reOk = a0 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading)
                && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated)
                && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
            const int b1 = rig.pool->property("builds").toInt();
            const bool addOk = reOk && rig.edges == 2 && b1 - b0 == kStep
                && rig.pool->property("destroys").toInt() == d1
                && rig.pool->property("lastCreated").toInt() == 1;
            ok = ok && addOk;
            if (!addOk)
                diag += QStringLiteral("[add %1 edges=%2 b=%3->%4 d=%5] ")
                            .arg(reOk)
                            .arg(rig.edges)
                            .arg(b0).arg(b1)
                            .arg(rig.pool->property("destroys").toInt());

            // ③ 批口双键突发（t1061 生产形态同门：批内翻转逐次 ++revision、收口恰一条沿）
            //    → 差分路径单沿 churn 恰 10（两变化键 × 组规模）。
            const int d2 = rig.pool->property("destroys").toInt();
            const int revBefore = w.residentChunkRevision();
            int edgesBefore = rig.edges;
            {
                World::ResidentSetBatch batch(w);
                const bool x1 = w.setChunkLifecycle(0, 1, ChunkLifecycle::Evicting);
                const bool x2 = x1 && w.setChunkLifecycle(1, 0, ChunkLifecycle::Evicting);
                ok = ok && x1 && x2;
            }
            const int d3 = rig.pool->property("destroys").toInt();
            const bool batchOk = rig.edges == edgesBefore + 1
                && w.residentChunkRevision() == revBefore + 2
                && d3 - d2 == 2 * kStep
                && rig.pool->property("lastDestroyed").toInt() == 2;
            ok = ok && batchOk;
            if (!batchOk)
                diag += QStringLiteral("[batch e+%1 d=%2->%3 rev+%4 ld=%5] ")
                            .arg(rig.edges - edgesBefore)
                            .arg(d2).arg(d3)
                            .arg(w.residentChunkRevision() - revBefore)
                            .arg(rig.pool->property("lastDestroyed").toInt());

            // ④ 走查模拟：6 键 × (移出沿 + 同键重加链) × 3 轮 = 36 沿，逐沿 churn 恰 5；
            //    收尾把批口双键重加（2 条链沿，各恰创建 5）→ 驻留回 9 组满员。账本闭合
            //    builds − destroys == 池段数 + patches == 发射数。
            edgesBefore = rig.edges;
            const int walkKeys[][2] = { { 2, 2 }, { 0, 0 }, { 2, 0 }, { 0, 2 }, { 1, 2 }, { 2, 1 } };
            const int walkN = 6; // 每轮移出/重加对数（键 ⊆ 批口后仍驻留集——被移键须在场可移）
            const int walkRounds = 3;
            bool walkOk = true;
            int churn = 0;
            QString walkTrace; // 红面归因：断点轮次/键/转移接受/沿数/build-destroy 增量
            int eExpect = edgesBefore;
            for (int round = 0; round < walkRounds && walkOk; ++round) {
                for (int i = 0; i < walkN && walkOk; ++i) {
                    const int cx = walkKeys[i][0], cz = walkKeys[i][1];
                    const int dB = rig.pool->property("builds").toInt();
                    const int dD = rig.pool->property("destroys").toInt();
                    // 移出沿：销毁恰 5（churn = 恰变化键组规模，非整池）。
                    ++eExpect;
                    const bool moved = w.setChunkLifecycle(cx, cz, ChunkLifecycle::Evicting);
                    const int cB0 = rig.pool->property("builds").toInt();
                    const int cD0 = rig.pool->property("destroys").toInt();
                    walkOk = moved && rig.edges == eExpect
                        && cB0 - dB == 0 && cD0 - dD == kStep;
                    churn += (cB0 - dB) + (cD0 - dD);
                    if (!walkOk) {
                        walkTrace += QStringLiteral("[r%1 i%2 %3,%4 rm mv%5 e%6/%7 b+%8 d+%9]")
                            .arg(round).arg(i).arg(cx).arg(cz).arg(moved)
                            .arg(rig.edges).arg(eExpect).arg(cB0 - dB).arg(cD0 - dD);
                        break;
                    }
                    // 同键重加链（⑦①②③）：创建恰 5。
                    ++eExpect;
                    const bool chain = w.setChunkLifecycle(cx, cz, ChunkLifecycle::Absent)
                        && w.setChunkLifecycle(cx, cz, ChunkLifecycle::Loading)
                        && w.setChunkLifecycle(cx, cz, ChunkLifecycle::Generated)
                        && w.setChunkLifecycle(cx, cz, ChunkLifecycle::Loaded);
                    const int cB1 = rig.pool->property("builds").toInt();
                    const int cD1 = rig.pool->property("destroys").toInt();
                    walkOk = chain && rig.edges == eExpect
                        && cB1 - cB0 == kStep && cD1 - cD0 == 0;
                    churn += (cB1 - cB0) + (cD1 - cD0);
                    if (!walkOk) {
                        walkTrace += QStringLiteral("[r%1 i%2 %3,%4 re ch%5 e%6/%7 b+%8 d+%9]")
                            .arg(round).arg(i).arg(cx).arg(cz).arg(chain)
                            .arg(rig.edges).arg(eExpect).arg(cB1 - cB0).arg(cD1 - cD0);
                        break;
                    }
                }
            }
            // 批口双键收尾重加（生产驱逐→走回重物化的池形态：两条链沿各恰创建 5）。
            const int bZ0 = rig.pool->property("builds").toInt();
            const bool z1 = walkOk
                && w.setChunkLifecycle(1, 0, ChunkLifecycle::Absent)
                && w.setChunkLifecycle(1, 0, ChunkLifecycle::Loading)
                && w.setChunkLifecycle(1, 0, ChunkLifecycle::Generated)
                && w.setChunkLifecycle(1, 0, ChunkLifecycle::Loaded);
            ++eExpect;
            const int bZ1 = rig.pool->property("builds").toInt();
            const bool z2 = z1
                && w.setChunkLifecycle(0, 1, ChunkLifecycle::Absent)
                && w.setChunkLifecycle(0, 1, ChunkLifecycle::Loading)
                && w.setChunkLifecycle(0, 1, ChunkLifecycle::Generated)
                && w.setChunkLifecycle(0, 1, ChunkLifecycle::Loaded);
            ++eExpect;
            const int bZ2 = rig.pool->property("builds").toInt();
            churn += (bZ1 - bZ0) + (bZ2 - bZ1);
            walkOk = walkOk && z1 && z2 && rig.edges == eExpect
                && bZ1 - bZ0 == kStep && bZ2 - bZ1 == kStep
                && w.residentChunkCount() == kResident;
            ok = ok && walkOk;
            if (!walkOk)
                diag += QStringLiteral("[walk edges=%1/%2 n=%3]%4 ")
                            .arg(rig.edges)
                            .arg(eExpect)
                            .arg(w.residentChunkCount())
                            .arg(walkTrace);

            // 账本闭合：builds − destroys == 终态池段数（9 组满员 45 段）；patches == 发射数；
            //    churn 总账 = 36 走查沿 × 5 + 收尾 2 沿 × 5。
            const int bTot = rig.pool->property("builds").toInt();
            const int dTot = rig.pool->property("destroys").toInt();
            const int slotsN = rig.pool->property("slots").toList().size();
            const bool ledgerOk = bTot - dTot == slotsN && slotsN == kResident * kStep
                && rig.pool->property("patches").toInt() == rig.edges
                && churn == (walkRounds * walkN * 2 + 2) * kStep;
            ok = ok && ledgerOk;
            if (!ledgerOk)
                diag += QStringLiteral("[ledger b=%1 d=%2 slots=%3 p=%4 e=%5 churn=%6/%7] ")
                            .arg(bTot).arg(dTot).arg(slotsN)
                            .arg(rig.pool->property("patches").toInt())
                            .arg(rig.edges)
                            .arg(churn)
                            .arg((walkRounds * walkN * 2 + 2) * kStep);
        }

        const bool cleanOk = rig.warnings == 0 && rig.pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2037c incrementality accounting: every membership edge churns"
                             " exactly the changed keys' group scale (five per single-key edge,"
                             " ten through one batched burst edge), the remove-and-re-add walk"
                             " keeps per-edge churn exact before the add-back pair restores"
                             " the full domain, the build-minus-destroy ledger closes to the"
                             " pooled size and patches reconcile with emissions"
                          << (ok ? QString() : diag);
    });

    // ── r2037d：结构钉（消费面恰一处 + 增量入口 + 组对齐源钉 + 消费端零触碰 + 玩法路径）──
    runLeg(QStringLiteral("r2037d structure pins (comment-stripped source pins hold the"
        " differential pool consumption on Main.qml: exactly one revision Connections handler"
        " routing the incremental patch entry, the patch entry defined once with the group"
        " creation single authority and the pool ledger property, the group stride"
        " single-switch expression shared by the patch and both consumers whose contract"
        " lines stay verbatim, the world-swap full-rebuild reset defined once and routed only"
        " from the streaming enter branch, and the gameplay path markers intact with zero"
        " lifecycle decision tokens)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString mainPath = srcRoot + QStringLiteral("/ui/Main.qml");

        // ① 正面精确计数：revision Connections 恰一处 + 增量入口（定义 + 沿路由各恰一）+
        //    组创建单点化 + 组身份账本 + 步长单源三面同源（patch + 两消费端）。
        const QStringList missQml = pinSet(mainPath, {
            SrcPin("revision consumer exactly one", "function onResidentChunkRevisionChanged()", 1),
            SrcPin("incremental patch entry defined", "function patchChunkSlotPool()", 1),
            SrcPin("edge routes the incremental patch", "window.patchChunkSlotPool()", 1),
            SrcPin("group creation single authority", "function _createChunkGroup(", 1),
            SrcPin("pool ledger property declared", "property var chunkKeys: []", 1),
            SrcPin("group stride single-switch source", "cutoutSegmentRestored ? 6 : 5", 3),
            SrcPin("consumer visibility entry intact", "function _refreshChunkVisibility()", 1),
            SrcPin("consumer group-head count intact",
                "if (inRange && (i % segmentsPerChunk) === 0) ++vis", 1),
            SrcPin("consumer mesh-sync entry intact", "function kickWorldMeshSync()", 1),
            SrcPin("consumer group stride walk intact",
                "for (let i = 0; i < objs.length; i += step)", 1),
            SrcPin("world-swap full rebuild entry defined", "function resetChunkSlotPool()", 1),
            SrcPin("gameplay bridge intact: worldClock onTicked", "function onTicked(dt)", 1),
            SrcPin("gameplay entry intact: enterWorld", "function enterWorld(", 1),
            SrcPin("gameplay entry intact: startGame", "function startGame()", 1),
        });
        for (const QString &m : missQml) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 函数域抽体钉（结构解析非裸计数——QML 无 static_assert，剥注释后按缩进界抽体）：
        //    沿 handler 体只路由差分入口（全量销毁循环退役）；patch 体保组对齐四要素；reset
        //    体保全量重派生要素；enterWorld 体只此一处路由 reset（流式分支收口）。
        QFile mf(mainPath);
        QString handlerBody, patchBody, resetBody, enterBody;
        if (mf.open(QIODevice::ReadOnly)) {
            const QString src = QString::fromUtf8(mf.readAll());
            const auto fnBody = [&src](const char *head, const char *closeSentinel) {
                const qsizetype b = src.indexOf(QLatin1String(head));
                if (b < 0)
                    return QString();
                const qsizetype e = src.indexOf(QLatin1String(closeSentinel), b);
                return e > b ? src.mid(b, e - b) : QString();
            };
            // handler 体 = 位于 Connections 块内（8 空格缩进闭括号）；patch/reset/enterWorld
            // 体 = 窗口级函数（4 空格缩进闭括号）。
            handlerBody = fnBody("function onResidentChunkRevisionChanged()", "\n            }");
            patchBody = fnBody("function patchChunkSlotPool()", "\n    }");
            resetBody = fnBody("function resetChunkSlotPool()", "\n    }");
            enterBody = fnBody("function enterWorld(", "\n    }");
        }
        const bool handlerOk = handlerBody.contains(QLatin1String("window.patchChunkSlotPool()"))
            && !handlerBody.contains(QLatin1String("destroy"))
            && !handlerBody.contains(QLatin1String("rebuildChunkSlotPool"));
        const bool patchOk = patchBody.contains(QLatin1String("_createChunkGroup("))
            && patchBody.contains(QLatin1String(".destroy()"))
            && patchBody.contains(QLatin1String("window.chunkKeys = newKeys"))
            && patchBody.contains(QLatin1String("cutoutSegmentRestored ? 6 : 5"))
            && patchBody.contains(QLatin1String("groups["));
        const bool resetOk = resetBody.contains(QLatin1String("rebuildChunkSlotPool()"))
            && resetBody.contains(QLatin1String("chunksBuilt = false"))
            && resetBody.contains(QLatin1String("destroy()"));
        const bool enterOk = enterBody.contains(QLatin1String("resetChunkSlotPool"))
            && enterBody.contains(QLatin1String("StreamingBridge.enterWorld"));
        if (!handlerOk) {
            ok = false;
            diag += QStringLiteral("[handler-body] ");
        }
        if (!patchOk) {
            ok = false;
            diag += QStringLiteral("[patch-body] ");
        }
        if (!resetOk) {
            ok = false;
            diag += QStringLiteral("[reset-body] ");
        }
        if (!enterOk) {
            ok = false;
            diag += QStringLiteral("[enter-body] ");
        }

        // ③ 生命周期决策禁入 QML（r2021d 零决策面延伸复钉——miss 非空 = 合规缺席）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool negOk = forbiddenAbsent(mainPath, "setChunkLifecycle");
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[lifecycle-in-qml] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2037d structure pins: Main.qml keeps exactly one revision"
                             " handler routing the differential patch, the group creation"
                             " single authority plus the ledger property, the stride"
                             " single-switch source shared with both untouched consumers,"
                             " the world-swap reset routed only from the streaming enter"
                             " branch, and the gameplay markers with zero lifecycle tokens"
                          << (ok ? QString() : diag);
    });
}
