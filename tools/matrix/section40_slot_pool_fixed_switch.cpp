#include "matrix_helpers.h"

#include "chunklifecycle.h"  // 被测面：走查收敛判据（Loaded 态读）
#include "streamingbridge.h" // 被测面：真桥进入握手 / 流式标志 / 会话 detach（t1065 切换链发生器）
#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1065 流式→固定世界切换池残留收口 探针段（4 腿 r2039a-d；filter 词 r2039；矩阵 687→687+N）。
// 置尾先例沿用（接 section39，runAll 末执行）；真临时库（pid 键名，测试自清理）+ 真桥单例 +
// 真 QQmlEngine；真实用户 saves/ 零触碰。任务契约（dev-plan t1065）：streaming 会话退出后进
// fixed 世界——reinitializeAsFixed 整表直设不发驻留沿 → Main.qml 池残留流式时代键集（核心域外/
// 负坐标组在固定网格外显空、固定网格缺键组无 Model 不显）。修法选型 = 主控方向 (a)：fixed 进入
// 链显式池重置（QML 池时代标记 poolStreamingEra 承担「换代过」记忆，与 t1063 流式分支收口对称；
// 不往 C++ 加沿语义 / 不新增 QML 模式读面——r2039d 钉死此选型）。
//   「fixed-only 零变化墙」→ r2039a（固定会话真链进入恒 false + 驻留零沿 + 池零 patch 零重派生
//     + 段指针逐位不动 + 时代标记恒 false——二次进入同墙，fixed 进入链逐位旧行为延续）；
//   「切换承重墙」→ r2039b（真链：流式会话真玩家走查产出负坐标驻留键 → detach 会话 → 进 fixed
//     世界 → 时代门触发恰一次全量重派生 → 池与固定网格逐位一致[键集账本 ≡ 驻留枚举 / 每键
//     5 段 / 组首对齐 / 负坐标组清零 / churn 账本闭合]）；
//   「往返腿」→ r2039c（fixed→流式→fixed→流式全链：fixed 存档臂[hasChunks 路径]与流式臂交替，
//     每次换代后池 ≡ 当前模式整池重建参照 + fixed 两次键集逐位一致 + 流式再进入换代收口不回退
//     [重派生恰一次 + 差分 patch 路径仍活]）；
//   「结构钉」→ r2039d（Main.qml 时代标记单点落账 / fixed 收口恰一处且序正确 / enterWorld 体
//     函数域抽体钉 + 两池消费端体零触碰反探 + 生命周期决策与 C++ 模式读面禁入 QML 反探 +
//     world.cpp reinitializeAsFixed 体零沿语义钉——选型 (a) 的立证面）。
// 被测面拆解（section38 同门双柱）：Main.qml 是 Window × Quick3D 全量装配面 headless 不可整载 →
//   r2039a-c 真 QQmlEngine × 真 World × 池镜像 wrapper（Main.qml enterWorld 分流 + t1063/t1065
//   两收口的最小复刻 + 计数断言视图），r2039d 纯源码钉承「生产接线形状」。恰红面双柱互补：
//   NEG-1 变异 Main.qml 本体（摘 fixed 收口门）→ 恰红 r2039d；NEG-2 变异镜像 wrapper 的时代门
//   （门恒 false）→ 恰红 {r2039b, r2039c}（行为柱；r2039a 不误伤——fixed-only 时代门不可达）。
// 腿名纪律：腿名/diag 文本零跨任务 filter 词元（r2018 P2 双向污染教训——本段只含 r2039 词元）。
void MatrixRun::section40_slot_pool_fixed_switch()
{
    constexpr int kW = 48, kD = 48, kH = 96, kSeed = 82; // fixed 小世界（3×3 chunk；section38 同族）
    constexpr int kResident = 9;                          // 3×3 固定网格全驻留
    constexpr int kStep = 5;                              // segmentsPerChunk 折叠默认（cutout 关）

    // fresh 小世界 incantation（section38 同款：48×48×96 s82 + 天气双钉）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kH);
        w.setSeed(kSeed);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名 + 腿标；fresh 由腿内 QFile::remove 保证 + 用后即删，saves/ 零触碰）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2039_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };

    // 池镜像 wrapper（Main.qml 池消费逻辑最小复刻——section38 同门镜像家族，含 t1063 流式收口
    //   与 t1065 fixed 收口两换代面的镜像）：buildAll/patchPool/resetPool 与 section38 逐位同构
    //   （patchPool = revision 沿差分增量；resetPool = 全量重派生），enter() = enterWorld 分流
    //   镜像（真桥五参类型化指针调用 + 流式臂重派生落账 / fixed 臂内容装载后时代门重派生），
    //   eraStreaming 计数视图 = C++ 断言面。
    static const char *kPoolWrapperQml = R"QML(import QtQuick
Item {
    property var world
    property var store
    property string file: ""
    property int seed: 0
    property bool entered: false
    property var slots: []
    property var keys: []
    property int step: 5
    property bool builtOnce: false
    property bool eraStreaming: false
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
        var streaming = r2039Bridge.enterWorld(world, store, r2039Clock, r2039Player, file, seed)
        entered = streaming
        if (streaming) {
            resetPool()
            eraStreaming = true
        } else {
            if (store.hasChunks()) {
                world.beginLoad(seed)
                store.loadChunks()
                world.finishLoad()
            } else {
                world.regenerate(seed)
            }
            if (eraStreaming) {
                eraStreaming = false
                resetPool()
            }
        }
    }
    function flag(cw, cd) { return r2039Bridge.flagNewWorldStreaming(file, cw, cd) }
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

    // 池镜像真链装配（真 QQmlEngine + 沿计数 + 桥/钟/玩家上下文注入；world/store 初参创建期注入）。
    struct PoolRig
    {
        QQmlEngine engine;
        QQuickItem *pool = nullptr;
        int warnings = 0;
        QString firstWarning;
        int edges = 0;
    };
    const auto makePool = [](PoolRig &rig, World &w, WorldStore &store, WorldClock &clock,
                            PlayerController &pc) {
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
        // 桥/钟/玩家上下文注入（enter 镜像五参调用消费；store/world 经初参创建期注入）。
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2039Bridge"),
                                                     StreamingBridge::instance());
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2039Clock"), &clock);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("r2039Player"), &pc);
        QQmlComponent comp(&rig.engine);
        comp.setData(kPoolWrapperQml, QUrl());
        if (comp.isError())
            return;
        rig.pool = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
            { { QStringLiteral("world"), QVariant::fromValue<QObject *>(&w) },
                { QStringLiteral("store"), QVariant::fromValue<QObject *>(&store) } }));
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
    // 整池重建参照对账（section38 同款）：池终态 ≡ 「对当前驻留集跑一次 buildAll 会产出的形态」
    //   逐项恒等——keys 账本 ≡ 驻留枚举（逐项）、slots ≡ 每键 step 段（组 g = 枚举 g、段 si =
    //   组内序、组首下标 = g*step 全局对齐）。红面 why 携首个失配点（归因入 diag）。
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
    // 段指针捕获（逐位不动柱：跨调用指针序列恒等 = 池零扰动 / 复用的行为级证据）。
    const auto allSegs = [](QQuickItem *pool) {
        const QVariantList segVars = pool->property("slots").toList();
        QList<QObject *> out;
        for (const QVariant &v : segVars)
            out << v.value<QObject *>();
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
    // 键集账本快照（往返腿逐位一致柱：键集账本扁平化字节序列——顺序敏感 = 规范序口径）。
    const auto keyLedger = [](QQuickItem *pool) {
        QByteArray led;
        const QVariantList ks = pool->property("keys").toList();
        for (const QVariant &v : ks) {
            const QVariantList k = v.toList();
            if (k.size() == 2) {
                led.append(reinterpret_cast<const char *>(&k.at(0)), sizeof(int));
                led.append(reinterpret_cast<const char *>(&k.at(1)), sizeof(int));
            }
        }
        return led;
    };
    // 收敛轮询（section31 同款节奏）：桥泵拍驱动直到 keys 全 Loaded 或超时（防 flake 不挂死）。
    const auto convergeLoaded = [](StreamingBridge &bridge, World &w,
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
                QString miss;
                for (const auto &k : keys)
                    if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Loaded)
                        miss += QStringLiteral("(%1,%2)=%3 ")
                                    .arg(k.first)
                                    .arg(k.second)
                                    .arg(int(w.chunks().lifecycleAt(k.first, k.second)));
                diag += QStringLiteral("[timeout %1ms miss: %2] ").arg(deadlineMs).arg(miss);
                return false;
            }
            bridge.pumpTick(); // 生产泵拍同体（WorldClock::ticked 槽体——QML tick 桥同拍同源）
            QThread::msleep(2);
        }
    };
    // 腿间/腿尾复位缝（singleton 跨腿共享——入口归零 + 用后即删帮手）。
    const auto cleanupDbs = [](const QStringList &dbs) {
        for (const QString &d : dbs)
            QFile::remove(d);
    };

    // ── r2039a：fixed-only 零变化墙（真链进入恒 false + 驻留零沿 + 池零 patch 零重派生 +
    //    时代标记恒 false；二次进入同墙——fixed 进入链逐位旧行为延续）──────────────────────
    runLeg(QStringLiteral("r2039a fixed-only zero-change wall (a fixed-only session through the"
        " real bridge enter handoff answers false twice with the world staying fixed and no"
        " session, the mirrored slot pool stays untouched across both handoffs and their"
        " content reloads with zero revision edges, zero patches, zero resets and every"
        " pooled segment pointer identical, the era flag never leaves false so the fixed-entry"
        " convergence never fires, and the engine stays warning-free"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝（singleton 跨腿共享——入口归零）

        const QString db = tempDb("a");
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
        makePool(rig, w, store, clock, pc);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll"); // 初建（world 初参已注入）
            const QList<QObject *> segs0 = allSegs(rig.pool);
            const int builds0 = rig.pool->property("builds").toInt();

            // 两轮真链进入（fresh 库两轮都走「新世界 regenerate」臂——fixed-only 会话全形态）：
            //   恒 false + 世界零模式迁移 + 零会话 + 驻留零沿。
            bool handOk = true;
            for (int round = 0; round < 2 && handOk; ++round) {
                rig.pool->setProperty("file", db);
                rig.pool->setProperty("seed", kSeed);
                QMetaObject::invokeMethod(rig.pool, "enter");
                const bool entered = rig.pool->property("entered").toBool();
                handOk = !entered && !w.isSparse() && !bridge.sessionActive() && rig.edges == 0;
                if (!handOk)
                    diag += QStringLiteral("[hand r=%1 e=%2 sparse=%3 sess=%4 edges=%5] ")
                                .arg(round)
                                .arg(entered)
                                .arg(w.isSparse())
                                .arg(bridge.sessionActive())
                                .arg(rig.edges);
            }
            ok = ok && handOk;

            // 池零扰动柱：patches/resets/destroys 恒 0 / builds = 初建值 / 时代标记恒 false
            //   （fixed 收口门不可达）/ 段指针逐位不动 / 终态与整池重建参照逐项恒等。
            const bool eraStillFalse = !rig.pool->property("eraStreaming").toBool();
            const QList<QObject *> segs1 = allSegs(rig.pool);
            QString why;
            const bool poolOk = rig.pool->property("patches").toInt() == 0
                && rig.pool->property("resets").toInt() == 0
                && rig.pool->property("destroys").toInt() == 0
                && rig.pool->property("builds").toInt() == builds0
                && builds0 == kResident * kStep && eraStillFalse && sameSegs(segs0, segs1)
                && poolMatchesReference(rig.pool, w, why);
            ok = ok && poolOk;
            if (!poolOk)
                diag += QStringLiteral("[pool p=%1 rs=%2 d=%3 b=%4 era=%5 ptr=%6 ref=%7 %8] ")
                            .arg(rig.pool->property("patches").toInt())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(rig.pool->property("destroys").toInt())
                            .arg(rig.pool->property("builds").toInt())
                            .arg(rig.pool->property("eraStreaming").toBool())
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
                          << "| r2039a fixed-only wall: a fixed-only session through the real"
                             " bridge enter handoff answers false twice with the world staying"
                             " fixed and no session, the mirrored slot pool stays untouched"
                             " across both handoffs and their content reloads (zero revision"
                             " edges, zero patches, zero resets, every pooled segment pointer"
                             " identical), the era flag never leaves false so the fixed-entry"
                             " convergence never fires, and the engine stays warning-free"
                          << (ok ? QString() : diag);
    });

    // ── r2039b：切换承重墙（真链：流式走查产负坐标驻留键 → detach → 进 fixed → 时代门全量
    //    重派生一次 → 池与固定网格逐位一致 + 负坐标组清零 + churn 账本闭合）──────────────────
    runLeg(QStringLiteral("r2039b streaming-to-fixed switch load-bearing wall (real chain: a"
        " streaming session converges real walked chunks including a negative-coordinate key"
        " into the mirrored pool through membership edges, the session detaches, entering a"
        " fixed save on the same world object silently resets the resident set to the fixed"
        " grid without a single edge, the era-gated full re-derivation fires exactly once, and"
        " the pool then matches the fixed-grid whole-rebuild reference item for item with the"
        " ledger equal to the resident enumeration, negative keys gone, every group carrying"
        " five segments at an aligned head, and the churn ledger closing to the rebuilt pool"
        " size"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝

        const QString dbS = tempDb("b_stream");
        const QString dbF = tempDb("b_fixed");
        QFile::remove(dbS);
        QFile::remove(dbF); // 双 fresh（流式标志库 + fixed 空库）
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(dbS) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);
        WorldClock clock;
        PlayerController pc;

        PoolRig rig;
        makePool(rig, w, store, clock, pc);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll"); // 初建（fixed 9 键基线）

            // ① 流式会话：标志落库 → 真链进入（sparse + 通电 + 预生成 25 chunk 全 Loaded）。
            rig.pool->setProperty("file", dbS);
            rig.pool->setProperty("seed", kSeed);
            QVariant flagged(false);
            QMetaObject::invokeMethod(rig.pool, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kW), Q_ARG(QVariant, kD));
            QMetaObject::invokeMethod(rig.pool, "enter");
            const bool enteredS = rig.pool->property("entered").toBool();
            const bool sparseOk = flagged.toBool() && enteredS && w.isSparse()
                && bridge.sessionActive() && w.residentChunkCount() == 25
                && rig.pool->property("eraStreaming").toBool()
                && rig.pool->property("resets").toInt() == 1;
            ok = ok && sparseOk;
            if (!sparseOk)
                diag += QStringLiteral("[sparse flag=%1 e=%2 sparse=%3 sess=%4 res=%5 era=%6 rs=%7] ")
                            .arg(flagged.toBool())
                            .arg(enteredS)
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(w.residentChunkCount())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(rig.pool->property("resets").toInt());
            QString why0;
            ok = ok && poolMatchesReference(rig.pool, w, why0);
            if (!poolMatchesReference(rig.pool, w, why0))
                diag += QStringLiteral("[sparse-ref %1] ").arg(why0);

            // ② 真玩家走查产出负坐标驻留键（chunk (-2,1)：x=-24.5 → floorDiv16 = -2）。
            pc.setWorld(&w);
            pc.loadSavedState(-24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0);
            pc.tick(); // 起始沿 → playerChunkChanged(-2,1) → 桥挂钩喂入会话
            const int revBefore = w.residentChunkRevision();
            const QVector<QPair<int, int>> walkKeys{ { -2, 1 } };
            QString dWalk;
            const bool walked = convergeLoaded(bridge, w, walkKeys, 45000, dWalk);
            bool negInPool = false;
            const QVariantList ksWalk = rig.pool->property("keys").toList();
            for (const QVariant &v : ksWalk) {
                const QVariantList k = v.toList();
                if (k.size() == 2 && k.at(0).toInt() < 0)
                    negInPool = true;
            }
            QString whyW;
            const bool walkOk = walked && w.residentChunkRevision() > revBefore && negInPool
                && rig.pool->property("eraStreaming").toBool()
                && poolMatchesReference(rig.pool, w, whyW);
            ok = ok && walkOk;
            if (!walkOk)
                diag += QStringLiteral("[walk conv=%1 rev+%2 neg=%3 era=%4 ref=%5 %6] %7")
                            .arg(walked)
                            .arg(w.residentChunkRevision() - revBefore)
                            .arg(negInPool)
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(poolMatchesReference(rig.pool, w, whyW))
                            .arg(whyW)
                            .arg(dWalk);

            // ③ 退出到菜单等价（会话终结，世界对象保持 sparse 残留态）→ 换 fixed 存档再进。
            bridge.detachWorld();
            const int slotsBefore = rig.pool->property("slots").toList().size();
            const int buildsBefore = rig.pool->property("builds").toInt();
            const int destroysBefore = rig.pool->property("destroys").toInt();
            const int resetsBefore = rig.pool->property("resets").toInt();
            const int patchesBefore = rig.pool->property("patches").toInt();
            const bool detOk = !bridge.sessionActive() && w.isSparse() && slotsBefore > 0;
            ok = ok && detOk;
            if (!detOk)
                diag += QStringLiteral("[detach sess=%1 sparse=%2 slots=%3] ")
                            .arg(bridge.sessionActive())
                            .arg(w.isSparse())
                            .arg(slotsBefore);
            store.openWorld(dbF); // 同一 WorldStore 换库（生产 worldStore 常驻单件换文件同构）
            rig.pool->setProperty("file", dbF);
            rig.pool->setProperty("seed", kSeed);
            QMetaObject::invokeMethod(rig.pool, "enter");
            const bool enteredF = rig.pool->property("entered").toBool();

            // ④ 收口柱：fixed 握手 false + 世界已归位 fixed + 零会话 + 时代门清账（恰一次
            //    重派生）/ 池与固定网格逐位一致（键账本 ≡ 驻留枚举 9 键、每键 5 段、组首对齐）
            //    / 负坐标组清零 / churn 账本闭合（销毁 = 流式池全量、新建 = fixed 池全量）。
            const QVariantList ksFixed = rig.pool->property("keys").toList();
            bool negLeft = false;
            int fixedDomain = 0;
            for (const QVariant &v : ksFixed) {
                const QVariantList k = v.toList();
                if (k.size() == 2) {
                    if (k.at(0).toInt() < 0 || k.at(1).toInt() < 0)
                        negLeft = true;
                    if (k.at(0).toInt() >= 0 && k.at(0).toInt() < 3 && k.at(1).toInt() >= 0
                        && k.at(1).toInt() < 3)
                        ++fixedDomain;
                }
            }
            QString whyF;
            const int resetsAfter = rig.pool->property("resets").toInt();
            const int buildsAfter = rig.pool->property("builds").toInt();
            const int destroysAfter = rig.pool->property("destroys").toInt();
            const bool convOk = !enteredF && !w.isSparse() && !bridge.sessionActive()
                && !rig.pool->property("eraStreaming").toBool()
                && resetsAfter == resetsBefore + 1
                && rig.pool->property("patches").toInt() == patchesBefore // fixed 进入零驻留沿
                && ksFixed.size() == kResident && fixedDomain == kResident && !negLeft
                && rig.pool->property("slots").toList().size() == kResident * kStep
                && poolMatchesReference(rig.pool, w, whyF)
                && buildsAfter - buildsBefore == kResident * kStep
                && destroysAfter - destroysBefore == slotsBefore;
            ok = ok && convOk;
            if (!convOk)
                diag += QStringLiteral("[conv e=%1 sparse=%2 sess=%3 era=%4 rs=%5/%6 p=%7"
                                       " keys=%8 dom=%9 neg=%10 ref=%11 b+%12 d+%13/%14 %15] ")
                            .arg(enteredF)
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(resetsAfter)
                            .arg(resetsBefore + 1)
                            .arg(rig.pool->property("patches").toInt())
                            .arg(ksFixed.size())
                            .arg(fixedDomain)
                            .arg(negLeft)
                            .arg(poolMatchesReference(rig.pool, w, whyF))
                            .arg(buildsAfter - buildsBefore)
                            .arg(destroysAfter - destroysBefore)
                            .arg(slotsBefore)
                            .arg(whyF);
        }

        const bool cleanOk = rig.warnings == 0 && rig.pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        cleanupDbs({ dbS, dbF }); // 用后即删（saves/ 零触碰）

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2039b streaming-to-fixed switch wall: a streaming session"
                             " converges real walked chunks including a negative-coordinate key"
                             " into the mirrored pool, the session detaches, entering a fixed"
                             " save on the same world object silently resets the resident set"
                             " to the fixed grid without a single edge, the era-gated full"
                             " re-derivation fires exactly once, and the pool then matches the"
                             " fixed-grid whole-rebuild reference item for item with negative"
                             " keys gone and the churn ledger closing to the rebuilt pool size"
                          << (ok ? QString() : diag);
    });

    // ── r2039c：往返腿（fixed→流式→fixed→流式全链：每次换代后池 ≡ 当前模式参照 + fixed 两轮
    //    键集逐位一致 + 流式再进入换代收口不回退[重派生恰一次 + 差分 patch 路径仍活]）──────────
    runLeg(QStringLiteral("r2039c round-trip consistency leg (fixed to streaming to fixed to"
        " streaming across one persistent world object and one persistent pool: the first"
        " fixed session lands a baseline ledger that the mid-chain fixed re-entry reproduces"
        " bit for item through the saved-chunks arm, every switch converges the pool to the"
        " current mode's whole-rebuild reference, each era-gated convergence fires exactly one"
        " reset with the flag clearing, and the streaming re-entry keeps its own unconditional"
        " re-derivation alive with the differential patch path still consuming membership"
        " edges afterward"), [&]() {
        bool ok = true;
        QString diag;

        StreamingBridge &bridge = *StreamingBridge::instance();
        bridge.detachWorld(); // 腿间复位缝

        const QString dbA = tempDb("c_fixed");
        const QString dbS = tempDb("c_stream");
        QFile::remove(dbA);
        QFile::remove(dbS); // 双 fresh
        World w;
        freshWorld48(w);
        WorldStore store;
        const bool opened = store.openWorld(dbA) && store.isOpen();
        ok = ok && opened;
        if (!opened)
            diag += QStringLiteral("[open] ");
        store.setWorld(&w);
        WorldClock clock;
        PlayerController pc;

        PoolRig rig;
        makePool(rig, w, store, clock, pc);
        if (!rig.pool) {
            ok = false;
            diag += QStringLiteral("[pool-create] ");
        }

        if (rig.pool) {
            QMetaObject::invokeMethod(rig.pool, "buildAll");

            // ① fixed 首轮（fresh → regenerate 臂）：fixed 基线账本 + 时代标记 false + 零重派生。
            rig.pool->setProperty("file", dbA);
            rig.pool->setProperty("seed", kSeed);
            QMetaObject::invokeMethod(rig.pool, "enter");
            QString whyA;
            const bool arm1Ok = !rig.pool->property("entered").toBool()
                && !w.isSparse() && !rig.pool->property("eraStreaming").toBool()
                && rig.pool->property("resets").toInt() == 0
                && rig.pool->property("keys").toList().size() == kResident
                && poolMatchesReference(rig.pool, w, whyA);
            ok = ok && arm1Ok;
            if (!arm1Ok)
                diag += QStringLiteral("[arm1 e=%1 sparse=%2 era=%3 rs=%4 ref=%5 %6] ")
                            .arg(rig.pool->property("entered").toBool())
                            .arg(w.isSparse())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(poolMatchesReference(rig.pool, w, whyA))
                            .arg(whyA);
            const QByteArray ledgerFixed0 = keyLedger(rig.pool);
            // fixed 存档落盘（第二轮回进走「已存地形」臂——beginLoad/loadChunks/finishLoad 面）。
            const bool savedA = store.saveAll(QStringLiteral("r2039c"));
            ok = ok && savedA && store.hasChunks();
            if (!savedA || !store.hasChunks())
                diag += QStringLiteral("[saveA s=%1 chunks=%2] ")
                            .arg(savedA)
                            .arg(store.hasChunks());

            // ② 流式进入：标志落库 → 真链进入 → 走查产负坐标键（池经差分沿吸收）。
            store.openWorld(dbS);
            rig.pool->setProperty("file", dbS);
            QVariant flagged(false);
            QMetaObject::invokeMethod(rig.pool, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kW), Q_ARG(QVariant, kD));
            QMetaObject::invokeMethod(rig.pool, "enter");
            const bool arm2Ok = flagged.toBool() && rig.pool->property("entered").toBool()
                && w.isSparse() && bridge.sessionActive()
                && rig.pool->property("eraStreaming").toBool()
                && rig.pool->property("resets").toInt() == 1
                && w.residentChunkCount() == 25;
            ok = ok && arm2Ok;
            if (!arm2Ok)
                diag += QStringLiteral("[arm2 flag=%1 e=%2 sparse=%3 sess=%4 era=%5 rs=%6 res=%7] ")
                            .arg(flagged.toBool())
                            .arg(rig.pool->property("entered").toBool())
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(w.residentChunkCount());
            pc.setWorld(&w);
            pc.loadSavedState(-24.5f, 70.0f, 24.5f, 0.0f, 0.0f, 0);
            pc.tick();
            QString dWalk1;
            const bool walked1 = convergeLoaded(bridge, w, { { -2, 1 } }, 45000, dWalk1);
            QString whyS1;
            ok = ok && walked1 && poolMatchesReference(rig.pool, w, whyS1);
            if (!walked1 || !poolMatchesReference(rig.pool, w, whyS1))
                diag += QStringLiteral("[arm2-walk conv=%1 ref=%2 %3] %4")
                            .arg(walked1)
                            .arg(poolMatchesReference(rig.pool, w, whyS1))
                            .arg(whyS1)
                            .arg(dWalk1);

            // ③ 回进 fixed 存档（hasChunks 臂）：时代门重派生恰一次 → 池键集与 ① 基线逐位一致。
            bridge.detachWorld(); // 会话终结（世界对象保持 sparse 残留）
            const int resetsBefore3 = rig.pool->property("resets").toInt();
            store.openWorld(dbA);
            rig.pool->setProperty("file", dbA);
            QMetaObject::invokeMethod(rig.pool, "enter");
            const int loaded3 = rig.pool->property("entered").toBool() ? -1 : 1;
            QString whyF3;
            const bool arm3Ok = loaded3 > 0 && !w.isSparse() && !bridge.sessionActive()
                && !rig.pool->property("eraStreaming").toBool()
                && rig.pool->property("resets").toInt() == resetsBefore3 + 1
                && keyLedger(rig.pool) == ledgerFixed0
                && rig.pool->property("keys").toList().size() == kResident
                && poolMatchesReference(rig.pool, w, whyF3);
            ok = ok && arm3Ok;
            if (!arm3Ok)
                diag += QStringLiteral("[arm3 e=%1 sparse=%2 sess=%3 era=%4 rs=%5/%6 led=%7"
                                       " ref=%8 %9] ")
                            .arg(loaded3)
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(resetsBefore3 + 1)
                            .arg(keyLedger(rig.pool) == ledgerFixed0)
                            .arg(poolMatchesReference(rig.pool, w, whyF3))
                            .arg(whyF3);

            // ④ 流式再进入：换代收口不回退（流式臂无条件重派生恰一次 + 时代落账）→ 走查沿后
            //    差分 patch 路径仍活（patches 增量 > 0 且池恒 ≡ 参照）。
            store.openWorld(dbS);
            rig.pool->setProperty("file", dbS);
            const int resetsBefore4 = rig.pool->property("resets").toInt();
            const int patches4 = rig.pool->property("patches").toInt();
            QMetaObject::invokeMethod(rig.pool, "enter");
            const bool arm4EnterOk = rig.pool->property("entered").toBool() && w.isSparse()
                && bridge.sessionActive()
                && rig.pool->property("eraStreaming").toBool()
                && rig.pool->property("resets").toInt() == resetsBefore4 + 1
                && w.residentChunkCount() == 25;
            ok = ok && arm4EnterOk;
            if (!arm4EnterOk)
                diag += QStringLiteral("[arm4 e=%1 sparse=%2 sess=%3 era=%4 rs=%5/%6 res=%7] ")
                            .arg(rig.pool->property("entered").toBool())
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(rig.pool->property("resets").toInt())
                            .arg(resetsBefore4 + 1)
                            .arg(w.residentChunkCount());
            pc.loadSavedState(55.5f, 70.0f, 55.5f, 0.0f, 0.0f, 0); // 走向 chunk (3,3)
            pc.tick();
            QString dWalk2;
            const bool walked2 = convergeLoaded(bridge, w, { { 3, 3 } }, 45000, dWalk2);
            const int patches5 = rig.pool->property("patches").toInt();
            QString whyS5;
            const bool arm4Ok = walked2 && patches5 > patches4
                && rig.pool->property("eraStreaming").toBool()
                && poolMatchesReference(rig.pool, w, whyS5);
            ok = ok && arm4Ok;
            if (!arm4Ok)
                diag += QStringLiteral("[arm4-walk conv=%1 p=%2/%3 era=%4 ref=%5 %6] %7")
                            .arg(walked2)
                            .arg(patches4)
                            .arg(patches5)
                            .arg(rig.pool->property("eraStreaming").toBool())
                            .arg(poolMatchesReference(rig.pool, w, whyS5))
                            .arg(whyS5)
                            .arg(dWalk2);
        }

        const bool cleanOk = rig.warnings == 0 && rig.pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(rig.warnings).arg(rig.firstWarning);

        bridge.detachWorld(); // 腿尾复位
        store.closeWorld();
        cleanupDbs({ dbA, dbS }); // 用后即删（saves/ 零触碰）

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2039c round-trip consistency: fixed to streaming to fixed to"
                             " streaming across one persistent world object and one persistent"
                             " pool - the first fixed session lands a baseline ledger that the"
                             " mid-chain fixed re-entry reproduces bit for item through the"
                             " saved-chunks arm, every switch converges the pool to the current"
                             " mode's whole-rebuild reference with exactly one reset per"
                             " convergence, and the streaming re-entry keeps its own"
                             " unconditional re-derivation alive with the differential patch"
                             " path still consuming membership edges"
                          << (ok ? QString() : diag);
    });

    // ── r2039d：结构钉（Main.qml 时代标记单点落账 + fixed 收口恰一处 + 函数域抽体钉 + 两消费端
    //    零触碰反探 + 生命周期/C++ 模式读面禁入 QML 反探 + reinitializeAsFixed 体零沿语义钉）──
    runLeg(QStringLiteral("r2039d structure pins (the fixed-entry pool convergence lives at"
        " exactly one gated point in the enter handoff: the era flag is declared once and"
        " written only by the two enter branches with the streaming arm landing true right"
        " after the streaming re-derivation and the fixed arm clearing it immediately before"
        " its own, the extracted enter body keeps both re-derivation call sites with the"
        " content load ahead of the fixed convergence, both pool consumers keep their contract"
        " lines with zero era tokens in their bodies, the QML surface stays blind to lifecycle"
        " decisions and to the C++ mode read face, and the fixed mode migration body in the"
        " world source emits no edge at all - the fix owns the staleness in the presentation"
        " ledger instead of adding engine semantics"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString mainPath = srcRoot + QStringLiteral("/ui/Main.qml");
        const QString worldCpp = srcRoot + QStringLiteral("/World/world.cpp");

        // ① Main.qml 正面精确计数：时代标记声明恰一 + 两臂写入各恰一 + 门恰一 + 既有收口/
        //    分流/消费面原样在场。
        const QStringList missQml = pinSet(mainPath, {
            SrcPin("era flag declared once", "property bool poolStreamingEra: false", 1),
            SrcPin("streaming arm lands the flag", "window.poolStreamingEra = true", 1),
            SrcPin("fixed arm clears the flag", "window.poolStreamingEra = false", 1),
            SrcPin("fixed convergence gate", "if (window.poolStreamingEra) {", 1),
            SrcPin("streaming re-derivation entry", "window.resetChunkSlotPool()", 2),
            SrcPin("world-swap full rebuild entry defined", "function resetChunkSlotPool()", 1),
            SrcPin("enter handoff branch",
                "if (StreamingBridge.enterWorld(theWorld, worldStore, worldClock, player, file,"
                " seed)) {", 1),
            SrcPin("consumer visibility entry intact", "function _refreshChunkVisibility()", 1),
            SrcPin("consumer mesh-sync entry intact", "function kickWorldMeshSync()", 1),
            SrcPin("gameplay entry intact: enterWorld", "function enterWorld(", 1),
        });
        for (const QString &m : missQml) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 函数域抽体钉（brace/缩进界抽体——结构解析非裸计数）：enterWorld 体保「流式落账 →
        //    fixed 内容装载 → 时代门清账 → 重派生」序；两消费端体零时代词元（零触碰反探）且
        //    契约句原样。
        QString enterBody, visBody, kickBody;
        {
            QFile mf(mainPath);
            if (mf.open(QIODevice::ReadOnly)) {
                const QString src = QString::fromUtf8(mf.readAll());
                const auto fnBody = [&src](const char *head, const char *closeSentinel) {
                    const qsizetype b = src.indexOf(QLatin1String(head));
                    if (b < 0)
                        return QString();
                    const qsizetype e = src.indexOf(QLatin1String(closeSentinel), b);
                    return e > b ? src.mid(b, e - b) : QString();
                };
                // enterWorld / 两消费端 = 窗口级函数（4 空格缩进闭括号）。
                enterBody = fnBody("function enterWorld(", "\n    }");
                visBody = fnBody("function _refreshChunkVisibility(", "\n    }");
                kickBody = fnBody("function kickWorldMeshSync(", "\n    }");
            }
        }
        const int iTrue = enterBody.indexOf(QLatin1String("window.poolStreamingEra = true"));
        const int iFalse = enterBody.indexOf(QLatin1String("window.poolStreamingEra = false"));
        const int iGate = enterBody.indexOf(QLatin1String("if (window.poolStreamingEra) {"));
        const int iLoad = enterBody.indexOf(QLatin1String("theWorld.finishLoad()"));
        const int iReset = enterBody.lastIndexOf(QLatin1String("window.resetChunkSlotPool()"));
        const int iResetFirst = enterBody.indexOf(QLatin1String("window.resetChunkSlotPool()"));
        const bool enterOk = !enterBody.isEmpty()
            && enterBody.count(QLatin1String("poolStreamingEra")) == 3
            && enterBody.count(QLatin1String("window.resetChunkSlotPool()")) == 2
            && iTrue >= 0 && iResetFirst < iTrue      // 流式臂：重派生在前、落账紧随
            && iGate >= 0 && iFalse > iGate           // fixed 臂：门内先清账
            && iLoad >= 0 && iLoad < iGate            // fixed 臂：内容装载先于收口
            && iReset > iFalse;                       // 收口末位：清账后重派生
        if (!enterOk) {
            ok = false;
            diag += QStringLiteral("[enter-body t=%1 f=%2 gate=%3 load=%4 reset=%5/%6] ")
                        .arg(iTrue).arg(iFalse).arg(iGate).arg(iLoad)
                        .arg(iResetFirst).arg(iReset);
        }
        const bool consumersOk = !visBody.isEmpty() && !kickBody.isEmpty()
            && !visBody.contains(QLatin1String("poolStreamingEra"))
            && !kickBody.contains(QLatin1String("poolStreamingEra"))
            && visBody.contains(QLatin1String("if (inRange && (i % segmentsPerChunk) === 0) ++vis"))
            && kickBody.contains(QLatin1String("for (let i = 0; i < objs.length; i += step)"))
            && kickBody.contains(QLatin1String("_meshSyncQueue.push("));
        if (!consumersOk) {
            ok = false;
            diag += QStringLiteral("[consumer-bodies vis=%1 kick=%2] ")
                        .arg(!visBody.isEmpty())
                        .arg(!kickBody.isEmpty());
        }

        // ③ 禁入反探（miss 非空 = 合规缺席）：生命周期决策 + C++ 模式读面不入 QML——时代标记
        //    是纯呈现层账本，选型 (a) 不往 QML 暴露 isSparse / 不加模式沿。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool qmlBlind = forbiddenAbsent(mainPath, "setChunkLifecycle")
            && forbiddenAbsent(mainPath, "isSparse");
        ok = ok && qmlBlind;
        if (!qmlBlind)
            diag += QStringLiteral("[qml-blind] ");

        // ④ C++ 零沿语义钉：reinitializeAsFixed 体保持静默（无 emit / 无驻留沿词元）——换代
        //    陈旧性由呈现层账本收口，不往引擎加第二套沿语义（选型立证面）。
        {
            QFile wf(worldCpp);
            QString body;
            if (wf.open(QIODevice::ReadOnly)) {
                const QString src = QString::fromUtf8(wf.readAll());
                const qsizetype b = src.indexOf(
                    QLatin1String("void World::reinitializeAsFixed(int width, int depth, int height)"));
                if (b >= 0) {
                    const qsizetype e = src.indexOf(QLatin1String("\n}"), b);
                    if (e > b)
                        body = src.mid(b, e - b);
                }
            }
            const bool silentOk = !body.isEmpty()
                && body.contains(QLatin1String("reinitializeFixed("))
                && !body.contains(QLatin1String("emit "))
                && !body.contains(QLatin1String("residentChunkRevision"));
            ok = ok && silentOk;
            if (!silentOk)
                diag += QStringLiteral("[reinit-body n=%1] ").arg(body.size());
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2039d structure pins: the fixed-entry pool convergence lives"
                             " at exactly one gated point in the enter handoff (era flag"
                             " declared once, written only by the two enter branches, the"
                             " extracted enter body keeping both re-derivation sites with the"
                             " content load ahead of the gated clear-then-rebuild), both pool"
                             " consumers keep their contract lines with zero era tokens, the"
                             " QML surface stays blind to lifecycle decisions and to the C++"
                             " mode read face, and the fixed mode migration body emits no edge"
                             " at all"
                          << (ok ? QString() : diag);
    });
}
