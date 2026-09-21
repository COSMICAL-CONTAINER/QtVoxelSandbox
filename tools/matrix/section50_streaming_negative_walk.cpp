#include "matrix_helpers.h"

#include "chunkgeometry.h"  // 被测面：fixed 网格段（refreshMesh 提交 + 顶点应用读面）
#include "chunklifecycle.h" // 被测面：六态读（收敛判据 + 失败签名分域）
#include "streamingbridge.h" // 被测面：W5b 真链进入 + 挂钩生命周期（QML 单例 = 生产同对象）

#include <QElapsedTimer>
#include <QThread> // 真线程收割节奏（converge 轮询 msleep）

// t1076 大核流式世界负向走查零收敛（复现定界 + 挂钩悬垂假幂等收口；3 腿；filter 词 r2049；
// 矩阵 727→730）。置尾先例沿用（接 section49，runAll 末执行，rig 世界零接触）。
//
// 任务契约（t1074 agent 实测记录转 P0）：160 核生产尺寸流式世界，真桥（StreamingBridge/
// GameSession pump 生产链）向负向走查至 chunk(-3,5) 附近，150 秒不收敛：res 恒 25 全 Absent、
// 会话在活、无驱逐无采用。对照：48 核小世界同形态负向走查（r2039b/r2047c）正常收敛；正向出核
// 走查（r2046b/r2047b）正常。
//
// 根因链（本单复现定界实证——二分数据推翻「负坐标/大窗专属病灶」假设）：
//   · 定界表（六变体同机同轮：负向直跳 (-3,5) / 负向锚 (-2,1) / 无泵走查 / 远负 (-6,5) /
//     深负象限 (-8,-8) / 正向对照 (16,5)）：变体是否收敛与方向、核尺寸、幅值全部无关，唯一
//     判别量 = 本次进入的源对象地址是否与「上一次被挂源」的悬垂记忆指针同址（同址即复现
//     res 恒 25 全 Absent 签名：fp sub=0 c=0 rej=0 out=0 ad=0、会话在活、泵拍照跑；正向对照
//     v6 同址时同样红）。
//   · 病灶本体：StreamingBridge::ensurePumpHook / ensureFeedHook 以「源指针相等 = 已挂同一源」
//     做幂等早退，而 detachWorld 只拆会话不清挂钩记忆——被挂源对象先于本桥消亡时（矩阵腿间 /
//     换世界换对象接线），Qt 连接随发送者析构自动断开，m_pumpClock/m_feedPlayer 沦为悬垂值；
//     新源对象落在同址（复用栈地址 / placement new 重建）即被误判「已挂」早退，真实连接已死
//     → playerChunkChanged 位置沿 / clock.ticked 泵拍静默断链 → GameSession 永远收不到玩家
//     chunk → 驱动器零决策零提交零驱逐零采用（t1074 实测签名逐项对上）。t1074 观察的
//     「负向/大核」相关性 = 该 agent rig 内腿序/帧布局的同址巧合，非病灶本体。
//   · 生产影响面：app 单 clock/单 pc 长活 → 现网复现需「源对象销毁重建 + 同址复用」前置
//     （测试 rig 必然、未来多会话/换对象接线可能）——挂钩生命周期本就该 = 会话生命周期
//     （两钩只在 enterWorld 内再挂），拆会话即拆挂钩是最小生命周期对齐收口。
//
// 腿面设计（先复现后修——本段先于修复落地时三腿全红 = 复现体 + 修复承重 + 结构钉；修复后全绿）：
//   「负向走查收敛（双真链进入）」→ r2049a（生产尺寸核心 160×160 真链两次进入，进入 2 的
//     PlayerController 经 placement new 在同一缓冲**确定性同址重建**（悬垂记忆指针 × 地址复用
//     病灶的构造性触发体——不依赖帧布局运气）：进入 1 出生预生成 25 驻留断言 + 玩家沿 cz=5 逐
//     chunk 负向走查 (4,5)→(-3,5)（每站有界泵拍）→ 终站 ±2 方窗 25 键（t1074 观测口径）60s
//     收敛；进入 2 同址重进 → 走查同签名 150s 收敛（t1074 实测 deadline 原值）——修复前进
//     入 2 恒红[悬垂假幂等吞位置沿]，修复后两进全绿）；失败签名分域 diag = 六态直方 + F3 流
//     式行差分（sub/can/rej/out/ad/p）+ 驻留键样本 + loadChunkAt 兜底探针（只落 diag 不入断言）。
//   「fixed 世界零变化墙 + 生产泵挂钩跨进入活性」→ r2049b（fixed 存档真链两次进入，进入 2 的
//     WorldClock 同址重建：分流恒 false + 会话零构造 + 世界恒 fixed + 驻留 9 格；每进入内
//     enableFixedAsyncBake + 网格段 refreshMesh 提交 → 真时钟 QTimer→ticked→[桥挂钩]→pumpTick
//     →harvest 生产链路交付顶点（processEvents 轮询 4s 上界）——挂钩断链时「已构建未收割」账
//     面增长而顶点恒 0；进入 2 = 挂钩悬垂假幂等的泵半边承重面，修复前恒红）。
//   「结构钉」→ r2049c（detachWorld 四行挂钩退役正面钉 + 双钩幂等守卫「连接活性同证」新形
//     正面钉 + 旧指针相等早退两字面反探禁出）。
// 阴性面设计（双变异双还原，手工 Edit 做/手工 Edit 还原，禁 git checkout/restore；存证 build/
//   终名日志 matrix_t1076_neg{1,2}_{red,restore}.log；各变异 = 该半边修复的两件**全摘**——
//   detachWorld 退役行 + ensureHook 幂等守卫联言，二者互为冗余防御，单摘其一被另一件掩盖）：
//   NEG-1 摘喂入半边（注释掉 detachWorld 的 m_feedConn disconnect + m_feedConn 句柄清零 +
//     m_feedPlayer = nullptr 三行，且 ensureFeedHook 守卫回退指针相等旧形）→ 声明红面
//     {r2049a, r2049c}[进入 2 位置沿断链不收敛 + feed 退役/守卫钉失配]。r2049b 不误伤（fixed
//     进入不挂喂入钩）；r2049a 进入 1 不误伤（首挂无悬垂可比）。
//   NEG-2 摘泵拍半边（注释掉 detachWorld 的 m_pumpConn disconnect + 句柄清零 + m_pumpClock =
//     nullptr 三行，且 ensurePumpHook 守卫回退指针相等旧形）→ 声明红面 {r2049b, r2049c}[进入 2
//     泵挂钩断链顶点恒 0 + pump 退役/守卫钉失配]。r2049a 不误伤（腿体直调 bridge.pumpTick 不经
//     时钟挂钩，喂入半边仍被保留的退役+联言守卫双件保护）。
// 时长控制：修复后每真链腿秒级收敛（deadline 为红面已知的上界防 flake 不挂死）；真临时库
//   fresh + 用后即删（QDir::temp() pid 键名，绝对路径直用，绝不触 saves/）；fixed 宿主
//   48×48×96 s82（3×3 chunk，r2035a 同门）。
void MatrixRun::section50_streaming_negative_walk()
{
    constexpr int kCoreBig = 160; // 真机生产核心域（10×10 chunk；t1074 实测形态）
    constexpr int kH = 96;
    constexpr int kSeed = 82;

    // 临时库路径（pid 键名 + 腿标；fresh + 用后即删，saves/ 零触碰；r2047 段同门绝对路径）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2049_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };
    // 方窗键集（中心 ± 半径切比雪夫域——请求集尺寸的测试侧权威，r2047 同款）。
    const auto windowKeys = [](int ccx, int ccz, int r) {
        QVector<QPair<int, int>> keys;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                keys.append({ ccx + dx, ccz + dz });
        return keys;
    };
    // F3 流式行差分基线（t1059 行的只读投影；每进入窗前快照，超时差分落 diag）。
    struct FpWin
    {
        qint64 sub = 0, can = 0, rej = 0, out = 0, ad = 0, p = 0;
    };
    const auto fpSnap = []() {
        FpWin f;
        f.sub = FrameProfiler::instance()->countValue("streamSub");
        f.can = FrameProfiler::instance()->countValue("streamCan");
        f.rej = FrameProfiler::instance()->countValue("streamRej");
        f.out = FrameProfiler::instance()->countValue("streamOut");
        f.ad = FrameProfiler::instance()->countValue("streamAdopt");
        f.p = FrameProfiler::instance()->countValue("streamPump");
        return f;
    };
    // 收敛轮询：桥泵拍驱动（生产泵同体直调——腿内泵活性与挂钩解耦，挂钩承重在 r2049b）直到
    // keys 全部 Loaded 或超时（r2047 同族节奏 + 分域 diag）。
    const auto convergeLoaded = [&](StreamingBridge &bridge, World &w,
                                    const QVector<QPair<int, int>> &keys, int deadlineMs,
                                    const FpWin &base, QString &diag) {
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
                int nAbsent = 0, nLoading = 0, nGenerated = 0, nEvicting = 0, nActive = 0;
                for (const auto &k : keys) {
                    switch (w.chunks().lifecycleAt(k.first, k.second)) {
                    case ChunkLifecycle::Absent: ++nAbsent; break;
                    case ChunkLifecycle::Loading: ++nLoading; break;
                    case ChunkLifecycle::Generated: ++nGenerated; break;
                    case ChunkLifecycle::Evicting: ++nEvicting; break;
                    case ChunkLifecycle::Active: ++nActive; break;
                    case ChunkLifecycle::Loaded: break;
                    case ChunkLifecycle::Count: break;
                    }
                }
                const FpWin now = fpSnap();
                diag += QStringLiteral("[timeout %1ms res=%2 sess=%3 win a/l/g/e/act=%4/%5/%6/%7/%8"
                                       " of %9 fp s=%10 c=%11 r=%12 out=%13 ad=%14 p=%15] ")
                            .arg(deadlineMs)
                            .arg(w.residentChunkCount())
                            .arg(bridge.sessionActive())
                            .arg(nAbsent)
                            .arg(nLoading)
                            .arg(nGenerated)
                            .arg(nEvicting)
                            .arg(nActive)
                            .arg(keys.size())
                            .arg(now.sub - base.sub)
                            .arg(now.can - base.can)
                            .arg(now.rej - base.rej)
                            .arg(now.out - base.out)
                            .arg(now.ad - base.ad)
                            .arg(now.p - base.p);
                // 驻留键样本（前 6 键——驻留集漂移的判别读面；公开 Q_INVOKABLE 读面）。
                QString sample;
                const int resN = w.residentChunkCount();
                for (int i = 0; i < resN && i < 6; ++i) {
                    const QVariantList kk = w.residentChunkKeyAt(i);
                    if (kk.size() == 2)
                        sample += QStringLiteral("%1,%2 ").arg(kk.at(0).toInt()).arg(
                            kk.at(1).toInt());
                }
                diag += QStringLiteral("[resSample %1] ").arg(sample);
                return false;
            }
            bridge.pumpTick();
            QThread::msleep(2);
        }
    };
    // rig 上下文名集（r2049 前缀双进入共用；section 函数域声明——lambda 返回类型可见性）。
    struct RigCtxNames
    {
        QString bridge, world, store, clock, player;
    };
    // 真链引擎装配（真 QQmlEngine + 上下文注入 + setData wrapper；真链进入分流经真桥单例）。
    struct Rig
    {
        QQmlEngine engine;
        QQuickItem *wrap = nullptr;
        int warnings = 0;
        QString firstWarning;
    };
    const auto makeRig = [](Rig &rig, const QString &prefix) {
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
        const QString qml = QStringLiteral(
            "import QtQuick\n"
            "Item {\n"
            "    property string file: \"\"\n"
            "    property int seed: 0\n"
            "    property bool entered: false\n"
            "    function enter() {\n"
            "        entered = %1Bridge.enterWorld(%1World, %1Store, %1Clock, %1Player, file,"
            " seed)\n"
            "    }\n"
            "    function flag(cw, cd) { return %1Bridge.flagNewWorldStreaming(file, cw, cd) }\n"
            "}\n").arg(prefix);
        comp.setData(qml.toUtf8(), QUrl());
        if (comp.isError())
            return;
        rig.wrap = qobject_cast<QQuickItem *>(comp.create());
        if (rig.wrap)
            rig.wrap->setParent(&rig.engine);
    };
    const auto setRigCtx = [](Rig &rig, const QString &name, QObject *obj) {
        rig.engine.rootContext()->setContextProperty(name, obj);
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
    const auto ctxNames = [](const QString &prefix) {
        RigCtxNames n;
        n.bridge = prefix + QStringLiteral("Bridge");
        n.world = prefix + QStringLiteral("World");
        n.store = prefix + QStringLiteral("Store");
        n.clock = prefix + QStringLiteral("Clock");
        n.player = prefix + QStringLiteral("Player");
        return n;
    };

    // ── r2049a：负向走查收敛（双真链进入；进入 2 源对象同址重建 = 确定性病灶触发体）──────────
    // pcBuf/clockBuf（腿体自有缓冲）每次进入 placement new 构造 + 进入尾显式析构 → 两次进入的
    // 源对象地址逐字节相同（构造性保证，不依赖帧布局运气）。修复前第二次进入的 ensureHook 被
    // 悬垂记忆指针的「已挂同一源」早退误判吞掉重挂（真实连接已死）。
    const auto runStreamEntry = [&](const char *tag, const QString &prefix, int deadlineMs,
                                    unsigned char *pcBuf, QString &diag) {
        StreamingBridge &bridge = *StreamingBridge::instance();
        const QString db = tempDb(tag);
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
        PlayerController *pc = new (pcBuf) PlayerController(); // 同址重建（确定性触发体）
        Rig rig;
        makeRig(rig, prefix);
        const RigCtxNames n = ctxNames(prefix);
        setRigCtx(rig, n.bridge, &bridge);
        setRigCtx(rig, n.world, &w);
        setRigCtx(rig, n.store, &store);
        setRigCtx(rig, n.clock, &clock);
        setRigCtx(rig, n.player, pc);
        bool entryOk = false;
        do {
            if (!rig.wrap) {
                diag += QStringLiteral("[%1 rig] ").arg(QLatin1String(tag));
                break;
            }

            // 创建链标志 + 真链进入（预生成中心 (5,5)，25 chunk）。
            setRigFile(rig, db, kSeed);
            QVariant flagged(false);
            QMetaObject::invokeMethod(rig.wrap, "flag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, flagged), Q_ARG(QVariant, kCoreBig),
                Q_ARG(QVariant, kCoreBig));
            const bool entered = rigEnter(rig);
            const bool enteredOk = flagged.toBool() && entered && w.isSparse()
                && bridge.sessionActive() && w.residentChunkCount() == 25;
            if (!enteredOk) {
                diag += QStringLiteral("[%1 enter flag=%2 e=%3 res=%4] ")
                            .arg(QLatin1String(tag))
                            .arg(flagged.toBool())
                            .arg(entered)
                            .arg(w.residentChunkCount());
                break;
            }

            // 负向逐 chunk 走查：cz=5 行、cx 4→-3 每站一停（floorDiv16 换格沿经真
            // PlayerController tick 链喂会话——生产位置源同链），每站有界泵拍 25 拍 ×2ms
            //（走查态消费生产收割链）。
            const FpWin base = fpSnap();
            bool walked = true;
            pc->setWorld(&w); // 位置源重挂 + 换格沿哨兵复位（首 tick 即起始沿）
            for (int cx = 4; cx >= -3 && walked; --cx) {
                pc->loadSavedState(float(cx * 16) + 0.5f, 70.0f, float(5 * 16) + 0.5f, 0.0f, 0.0f,
                                   0);
                pc->tick(); // 位置沿：playerChunkChanged → notePlayerChunk（生产直连链——挂钩死则断）
                bridge.pumpTick(); // 立即一拍（沿承接拍——生产 tick 尾泵同体）
                for (int burst = 0; burst < 25; ++burst) {
                    bridge.pumpTick();
                    QThread::msleep(2);
                }
                if (!bridge.sessionActive())
                    walked = false; // 会话意外消亡（防御读面——diag 落账）
            }
            if (!walked) {
                diag += QStringLiteral("[%1 walk aborted sess=%2] ")
                            .arg(QLatin1String(tag))
                            .arg(bridge.sessionActive());
            }

            // 终站 (-3,5) 静置收敛：±2 方窗 25 键（t1074 观测口径「res 恒 25 全 Absent」同窗）。
            const QVector<QPair<int, int>> winKeys = windowKeys(-3, 5, 2);
            QString dConv;
            entryOk = walked && convergeLoaded(bridge, w, winKeys, deadlineMs, base, dConv);
            if (!entryOk) {
                diag += QStringLiteral("[conv %1%2]").arg(QLatin1String(tag)).arg(dConv);
                // 超时诊断面（不计断言）：仍 Absent 的窗首键走 loadChunkAt 生产缝兜底——物化链
                // 在该键是否本就健康（成功 = 病灶在挂钩/驱动链；失败 = 物化链本身病）。只改 diag。
                for (const auto &k : winKeys) {
                    if (w.chunks().lifecycleAt(k.first, k.second) != ChunkLifecycle::Absent)
                        continue;
                    const bool fb = w.loadChunkAt(k.first, k.second);
                    diag += QStringLiteral("[fallback key=%1,%2 ok=%3 life=%4] ")
                                .arg(k.first)
                                .arg(k.second)
                                .arg(fb)
                                .arg(int(w.chunks().lifecycleAt(k.first, k.second)));
                    break; // 首个 Absent 键一枚即可判别
                }
            }
            if (rig.warnings != 0) {
                entryOk = false;
                diag += QStringLiteral("[%1 warn n=%2 %3] ")
                            .arg(QLatin1String(tag))
                            .arg(rig.warnings)
                            .arg(rig.firstWarning);
            }
        } while (false);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删
        pc->~PlayerController(); // 同址重建体进入尾显式析构（下一进入同缓冲重构造）
        return entryOk;
    };

    runLeg(QStringLiteral("r2049a negative-direction walk convergence across two real-chain"
        " entries (a production-size streaming core entered through the real bridge twice with"
        " the player controller rebuilt at the identical address for the second entry: each"
        " entry spawns with its twenty-five pregenerated resident chunks, walks the real player"
        " chunk by chunk in the negative x direction to chunk minus-three five with a bounded"
        " pump burst at each stop, and the stationary two-chunk radius window at the final stop"
        " converges to fully loaded within the production deadline)"), [&]() {
        bool ok = true;
        QString diag;
        alignas(PlayerController) unsigned char pcBuf[sizeof(PlayerController)];
        // 进入 1：首挂无悬垂可比（修复前后均收敛 = 负向走查本体的对照承重）。
        const bool e1 = runStreamEntry("e1", QStringLiteral("r2049"), 60000, pcBuf, diag);
        ok = ok && e1;
        if (!e1)
            diag += QStringLiteral("[e1] ");
        // 进入 2：同址重建重进（悬垂记忆指针 × 同址复用 = 病灶触发面；修复前恒红）。
        const bool e2 = runStreamEntry("e2", QStringLiteral("r2049"), 150000, pcBuf, diag);
        ok = ok && e2;
        if (!e2)
            diag += QStringLiteral("[e2] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2049a negative-direction walk convergence across two real-chain"
                             " entries (a production-size streaming core entered through the real"
                             " bridge twice with the player controller rebuilt at the identical"
                             " address for the second entry: each entry spawns with its"
                             " twenty-five pregenerated resident chunks, walks the real player"
                             " chunk by chunk in the negative x direction to chunk minus-three"
                             " five with a bounded pump burst at each stop, and the stationary"
                             " two-chunk radius window at the final stop converges to fully"
                             " loaded within the production deadline)"
                          << (ok ? QString() : diag);
    });

    // ── r2049b：fixed 世界零变化墙 + 生产泵挂钩跨进入活性（真时钟链路交付）────────────────────
    // clockBuf 同址重建（进入 2 WorldClock 落同一地址）。fixed 存档（无流式标志 = 走既有 fixed
    // 进入链）每进入断言：分流恒 false + 会话零构造 + 世界恒 fixed + 驻留 9 格（fixed 零变化
    // 行为级墙）；泵挂钩活性经生产真链路验证：enableFixedAsyncBake + ChunkGeometry refreshMesh
    // 提交 → 真时钟 QTimer→onTick→ticked→[桥挂钩]→pumpTick→harvest→顶点应用（processEvents
    // 轮询 4s 上界）。挂钩死时已构建账增长而顶点恒 0 = 进入 2 的修复承重面（NEG-2 红面）。
    // QTVOXEL_SYNC_BAKE 环境回退下 bake 全同步内联（顶点即得、挂钩不经由）→ 活性面如实降级为
    // 同步柱（阴性轮不在此环境跑）。
    const auto runFixedEntry = [&](const char *tag, const QString &prefix, unsigned char *clockBuf,
                                   QString &diag) {
        StreamingBridge &bridge = *StreamingBridge::instance();
        const QString db = tempDb(tag);
        QFile::remove(db); // fresh（无流式标志 = fixed 存档）
        World w;
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(kH);
        w.setSeed(kSeed);
        WorldStore store;
        if (!store.openWorld(db) || !store.isOpen()) {
            diag += QStringLiteral("[%1 open] ").arg(QLatin1String(tag));
            QFile::remove(db);
            return false;
        }
        store.setWorld(&w);

        WorldClock *clock = new (clockBuf) WorldClock(); // 同址重建（确定性触发体）
        PlayerController pc;
        Rig rig;
        makeRig(rig, prefix);
        const RigCtxNames n = ctxNames(prefix);
        setRigCtx(rig, n.bridge, &bridge);
        setRigCtx(rig, n.world, &w);
        setRigCtx(rig, n.store, &store);
        setRigCtx(rig, n.clock, clock);
        setRigCtx(rig, n.player, &pc);
        bool entryOk = false;
        do {
            if (!rig.wrap) {
                diag += QStringLiteral("[%1 rig] ").arg(QLatin1String(tag));
                break;
            }

            // 真链进入分流：fixed 存档恒 false + 会话零构造 + 世界零模式迁移。
            setRigFile(rig, db, kSeed);
            const bool entered = rigEnter(rig);
            const bool fixedEnterOk = !entered && !w.isSparse() && !bridge.sessionActive()
                && w.residentChunkCount() == 9;
            if (!fixedEnterOk) {
                diag += QStringLiteral("[%1 enter e=%2 sparse=%3 sess=%4 res=%5] ")
                            .arg(QLatin1String(tag))
                            .arg(entered)
                            .arg(w.isSparse())
                            .arg(bridge.sessionActive())
                            .arg(w.residentChunkCount());
                break;
            }

            // 泵挂钩活性（生产真链路）：使能异步烘培 + 段提交 → 真时钟拍驱动收割 → 顶点应用。
            const bool envSyncBake = qEnvironmentVariable("QTVOXEL_SYNC_BAKE").toInt() != 0;
            const bool enabled = w.enableFixedAsyncBake();
            if (enabled != !envSyncBake) {
                diag += QStringLiteral("[%1 bake enable=%2 env=%3] ")
                            .arg(QLatin1String(tag))
                            .arg(enabled)
                            .arg(envSyncBake);
                break;
            }
            bool hookAlive = false;
            qint64 builtWaiting = -1;
            ChunkGeometry *g = new ChunkGeometry();
            g->setWorld(&w);
            g->setCx(1);
            g->setCz(1);
            g->refreshMesh(); // fresh 世界 chunk 非脏——恰 1 提交（r2034b 同款）
            if (envSyncBake) {
                hookAlive = g->vertexCount() > 0; // 同步内联回退：顶点即得（挂钩不经由——如实降级）
            } else {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() <= 4000) {
                    QCoreApplication::processEvents(); // 真时钟 QTimer 拍投递（无 exec 进程）
                    if (g->vertexCount() > 0) {
                        hookAlive = true;
                        break;
                    }
                    QThread::msleep(10);
                }
                builtWaiting = w.fixedMeshHarvestableCount(); // 诊断：挂钩死时「已构建未收割」账
            }
            delete g;
            if (!hookAlive) {
                diag += QStringLiteral("[%1 pumpHook vertex=0 built=%2] ")
                            .arg(QLatin1String(tag))
                            .arg(builtWaiting);
                break;
            }
            entryOk = true;
        } while (false);

        bridge.detachWorld();
        store.closeWorld();
        QFile::remove(db); // 用后即删
        clock->~WorldClock(); // 同址重建体进入尾显式析构（下一进入同缓冲重构造）
        return entryOk;
    };

    runLeg(QStringLiteral("r2049b fixed-world zero-change wall and production pump-hook liveness"
        " across two real-chain entries (a fixed save enters through the real bridge twice with"
        " the world clock rebuilt at the identical address for the second entry, each entry"
        " answering false with the world staying fixed and no session constructed over the"
        " nine-chunk resident grid, and each entry proves the clock-to-pump hook alive on the"
        " production path by delivering an asynchronously baked terrain segment through the real"
        " world clock timer into the bridge pump harvest)"), [&]() {
        bool ok = true;
        QString diag;
        alignas(WorldClock) unsigned char clockBuf[sizeof(WorldClock)];
        const bool f1 = runFixedEntry("f1", QStringLiteral("r2049"), clockBuf, diag);
        ok = ok && f1;
        if (!f1)
            diag += QStringLiteral("[f1] ");
        const bool f2 = runFixedEntry("f2", QStringLiteral("r2049"), clockBuf, diag);
        ok = ok && f2;
        if (!f2)
            diag += QStringLiteral("[f2] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2049b fixed-world zero-change wall and production pump-hook"
                             " liveness across two real-chain entries (a fixed save enters through"
                             " the real bridge twice with the world clock rebuilt at the identical"
                             " address for the second entry, each entry answering false with the"
                             " world staying fixed and no session constructed over the nine-chunk"
                             " resident grid, and each entry proves the clock-to-pump hook alive"
                             " on the production path by delivering an asynchronously baked"
                             " terrain segment through the real world clock timer into the bridge"
                             " pump harvest)"
                          << (ok ? QString() : diag);
    });

    // ── r2049c：结构钉（挂钩退役四行 + 幂等守卫新形 + 旧早退字面反探禁出）────────────────────
    runLeg(QStringLiteral("r2049c structure pins (detaching the world retires both the pump and"
        " the feed hook connections with their memoized source pointers reset, the idempotence"
        " guards of both hooks additionally require a live connection beside the same source,"
        " and the retired pointer-only early-return guards are absent)"), [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString sbCpp = srcRoot + QStringLiteral("/Game/streamingbridge.cpp");

        // 正钉：detachWorld 挂钩退役（NEG-1 摘喂入半边 / NEG-2 摘泵拍半边各恰红对应钉）。
        const QStringList miss = pinSet(sbCpp,
            { SrcPin("pump hook connection retired",
                     "QObject::disconnect(m_pumpConn);\n        m_pumpConn = QMetaObject::Connection();", 1),
                SrcPin("pump hook memo reset", "m_pumpClock = nullptr;", 1),
                SrcPin("feed hook connection retired",
                     "QObject::disconnect(m_feedConn);\n        m_feedConn = QMetaObject::Connection();", 1),
                SrcPin("feed hook memo reset", "m_feedPlayer = nullptr;", 1),
                SrcPin("pump guard live-connection conj",
                     "if (!clock || (m_pumpClock == clock && m_pumpConn))", 1),
                SrcPin("feed guard live-connection conj",
                     "if (!player || (m_feedPlayer == player && m_feedConn))", 1) });
        const bool pinOk = miss.isEmpty();
        ok = ok && pinOk;
        if (!pinOk)
            diag += QStringLiteral("[pins %1] ").arg(miss.join(QLatin1Char(',')));

        // 反探禁出：旧「指针相等即已挂」早退两字面（修复后任一在场 = 假幂等面回退）。
        QFile f(sbCpp);
        QString src;
        if (f.open(QIODevice::ReadOnly))
            src = QString::fromUtf8(f.readAll());
        const bool oldGone = !src.contains(QLatin1String("if (!clock || m_pumpClock == clock)"))
            && !src.contains(QLatin1String("if (!player || m_feedPlayer == player)"));
        ok = ok && oldGone;
        if (!oldGone)
            diag += QStringLiteral("[anti old pointer-only guard present] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2049c structure pins (detaching the world retires both the pump and"
                             " the feed hook connections with their memoized source pointers"
                             " reset, the idempotence guards of both hooks additionally require a"
                             " live connection beside the same source, and the retired"
                             " pointer-only early-return guards are absent)"
                          << (ok ? QString() : diag);
    });
}
