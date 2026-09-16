// tools/matrix/section24_qmldynamization.cpp —— §29.4-P4 QML 动态化探针段（置尾先例沿用：
// runAll 末执行，filter 词 "r2021"；4 腿 r2021a-d；矩阵 620→624）。
//
// 任务契约（docs/refactor-plan-2026-09-08.md §29.4「P. 相位拆分」P4 原文：「ChunkGeometry
// Repeater 固定网格 → 动态 slot 池；QML 变更面在此相位集中、单独阴性钉守护」——全 R20 唯一
// 允许动 QML 的相位）。被测面 = 渲染侧数据驱动的最小 QML 面：
//   「固定世界观感零变化（承重墙）」→ r2021a：驻留集枚举 ≡ 旧固定网格循环序逐项恒等
//     （3×3 方阵 + 2×3 非方阵双形态——非方阵下 cz 外 cx 内与 cx 外 cz 内序可判别）；
//     revision 初始 0 + 编辑零沿；枚举键可直接驱动 ChunkGeometry（meshable 消费契约）。
//   「模型变化 → slot 池随模型增删」→ r2021b（C++ 模型响应性）：合法转移沿的 revision /
//     发射计数精确对账（⑥移出 +1 / ⑦集外零 bump / ③重加 +1 / ⑤④集内零 bump / ⑧加回 +1 /
//     非法转移拒绝零沿）+ 枚举随增删（移出消失、重加归位）。
//   「QML real-chain 响应性（headless 实证，不依赖实机）」→ r2021c：真 QQmlEngine × 真
//     World × setData wrapper（slot 池镜像，Main.qml rebuildChunkSlotPool 同逻辑最小复刻，
//     既有 QML 真链 harness 家族同门）——revision 沿直达 QML Connections handler、池随
//     模型增删、QVariantList 键经真引擎值转换。
//   「新增 QML 面逐一配钉 + 玩法路径零触碰 + QML 变更面集中」→ r2021d：world.h 三新成员
//     + world.cpp 单点枚举权威正面钉（剥注释）；Main.qml 消费面精确计数钉 + 旧固定网格
//     循环反探退役 + setChunkLifecycle 禁入 QML（零生命周期决策面延伸）+ 玩法路径标记在位
//     （onTicked 桥 / enterWorld / startGame——零迁移阴性钉家族语义面复钉）。
// 阴性恰红面（设计先于腿文，docs 关单同步）：
//   NEG-1 摘「模型变更发射沿」（setChunkLifecycle bump 分支 false && 前缀 = 分支永不可达）
//     → 恰红 {r2021b, r2021c}（revision/发射计数 + real-chain 池跟随；r2021a 恒等面与
//     r2021d 结构钉不误伤——针文本在位、行为面红 = 被摘语义本体）。
//   NEG-2 摘「枚举与旧网格的恒等口径」（world.cpp 单点权威双循环 cx/cz 互换 = 排序错位）
//     → 恰红 {r2021a, r2021b, r2021c}（首版声明 {r2021a} 单腿——实测 r2021b 的「重加归位
//     canonical index / 枚举复原」与 r2021c 镜像的 slots 对账同样锚在旧网格 cz 主序基线数组
//     上：恒等口径本就是三腿共同消费的被测语义本体，非连带伤[各红 diag 逐一核对该构conjunction]；
//     r2021d 不钉循环嵌套文本不误伤。红面超首版声明按 t1053/t1054 先例改声明留痕，非放宽——
//     三腿锚定基线的断言一个不削）。
// 置尾先例沿用：本段自建 fresh 小世界（48×48×96 s82 三体 + 32×48×96 s82 非方阵一体；
// section06 t973 同款 incantation + 天气双钉），rig 世界 w 零接触。零接线承重：P1-P3/D6
// 组件（策略/驱动/卸载/worker meshing）零进入生产路径 → 生产零 setLifecycle 调用 →
// revision 恒 0 = 固定世界零变化的结构性根据（r2021a 实证）。
// 腿名纪律：腿名/diag 文本零跨任务 filter 词元（r2018 P2 双向污染教训——本段只含 r2021 词元）。
#include "matrix_helpers.h"

void MatrixRun::section24_qmldynamization()
{
    // fresh 小世界 incantation（section11+/section22 先例：48×48×96 s82 + 天气双钉）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 旧固定网格枚举序镜像（Main.qml t276 双循环 cz 外 cx 内——r2021a 的恒等口径基准；
    // 2×3 非方阵下与 cx 外 cz 内序可判别：cx 主序首三项会是 (0,0),(0,1),(0,2)）。
    const QVector<QPair<int, int>> kGrid33 = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 },
        { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 } };
    const QVector<QPair<int, int>> kGrid23 = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 },
        { 1, 2 } };
    // 枚举读面镜像（C++ 侧消费形态与 QML 相同：count + keyAt(i) 逐项拉取——禁绕过读面直取
    // chunks()，否则 QML 消费契约失察）。
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
    const auto keysEq = [](const QVector<QPair<int, int>> &a, const QVector<QPair<int, int>> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i) != b.at(i))
                return false;
        return true;
    };

    // ── r2021a：固定世界零变化承重墙（枚举恒等 + revision 零沿 + 键可驱动 mesher）──────
    runLegMulti({ "r2021a fixed-world bit-identity wall (P4 QML dynamization): the resident-set"
        " enumeration on fresh streaming-off worlds equals the legacy fixed-grid loop order"
        " item-for-item (3x3 square AND 2x3 non-square, the latter discriminating cz-major"
        " from cx-major), count equals chunksX*chunksZ, revision starts at 0 and block"
        " edits emit zero revision edges (production has zero lifecycle transitions -"
        " P1-P3/D6 components are unwired), out-of-range keyAt returns an empty list, a"
        " second enumeration is bit-identical (no hidden state), and every enumerated key"
        " drives a ChunkGeometry to a non-empty mesh (the QML slot-pool consumption"
        " contract: enumerated keys are directly usable as cx/cz)" }, [&]() {
        bool ok = true;
        QString diag;

        World w33; // 3×3 方阵（fresh 48×48×96 s82）
        freshWorld48(w33);
        World w23; // 2×3 非方阵（32 宽 × 48 深 → chunksX=2, chunksZ=3；序判别形态）
        w23.setWidth(32);
        w23.setDepth(48);
        w23.setHeight(96);
        w23.setSeed(82);
        w23.setWeatherState(0);
        w23.setWeatherRemainingSec(3600.0f);

        // 信号沿计数器（发射面绝对计数——编辑零沿的判据）。
        int em33 = 0, em23 = 0;
        QObject::connect(&w33, &World::residentChunkRevisionChanged,
            [&em33]() { ++em33; });
        QObject::connect(&w23, &World::residentChunkRevisionChanged,
            [&em23]() { ++em23; });

        const int n33 = w33.residentChunkCount();
        const int n23 = w23.residentChunkCount();
        const bool cntOk = n33 == 9 && n33 == w33.chunksX() * w33.chunksZ()
            && n23 == 6 && n23 == w23.chunksX() * w23.chunksZ();
        ok = ok && cntOk;
        if (!cntOk)
            diag += QStringLiteral("[count n33=%1 n23=%2] ").arg(n33).arg(n23);

        const auto order33 = enumerateModel(w33);
        const auto order23 = enumerateModel(w23);
        const bool ordOk = keysEq(order33, kGrid33) && keysEq(order23, kGrid23);
        ok = ok && ordOk;
        if (!ordOk) {
            QString s;
            for (const auto &k : order23)
                s += QStringLiteral("(%1,%2)").arg(k.first).arg(k.second);
            diag += QStringLiteral("[order33eq=%1 order23eq=%2 seq23=%3] ")
                        .arg(keysEq(order33, kGrid33))
                        .arg(keysEq(order23, kGrid23))
                        .arg(s);
        }

        const bool revOk = w33.residentChunkRevision() == 0 && w23.residentChunkRevision() == 0;
        ok = ok && revOk;
        if (!revOk)
            diag += QStringLiteral("[rev0 %1/%2] ")
                        .arg(w33.residentChunkRevision())
                        .arg(w23.residentChunkRevision());

        // 越界契约：keyAt(-1) / keyAt(count) → 空 QVariantList（QML 循环天然不越界的双保险面）。
        const bool oobOk = w33.residentChunkKeyAt(-1).isEmpty()
            && w33.residentChunkKeyAt(n33).isEmpty()
            && w23.residentChunkKeyAt(-1).isEmpty()
            && w23.residentChunkKeyAt(n23).isEmpty();
        ok = ok && oobOk;
        if (!oobOk)
            diag += QStringLiteral("[oob] ");

        // 编辑零沿（承重墙行为面）：放 + 破标记列（显式回读防静默拒绝，t1051 选址纪律）——
        //   纯 setBlock 编辑绝不触驻留集（revision 不动、零发射）。
        int editSite = -1;
        for (int z = 4; z < 12 && editSite < 0; ++z) {
            const int h = w33.heightAt(4, z);
            if (h >= 0 && h + 2 < w33.height() && w33.blockAt(4, h + 1, z) == 0)
                editSite = h + 1;
        }
        bool editsOk = editSite > 0;
        if (editsOk) {
            w33.setBlock(4, editSite, 4, quint8(BR::Stone), 0);
            editsOk = w33.blockAt(4, editSite, 4) == quint8(BR::Stone);
            w33.setBlock(4, editSite, 4, quint8(BR::Air), 0);
            editsOk = editsOk && w33.blockAt(4, editSite, 4) == quint8(BR::Air)
                && w33.residentChunkRevision() == 0 && em33 == 0 && em23 == 0;
        }
        ok = ok && editsOk;
        if (!editsOk)
            diag += QStringLiteral("[edits site=%1 rev=%2 em=%3/%4] ")
                        .arg(editSite)
                        .arg(w33.residentChunkRevision())
                        .arg(em33)
                        .arg(em23);

        // 幂等：二次枚举与首次逐位一致（无隐状态、无惰性缓存漂移）。
        const bool idemOk = keysEq(enumerateModel(w33), order33)
            && keysEq(enumerateModel(w23), order23);
        ok = ok && idemOk;
        if (!idemOk)
            diag += QStringLiteral("[idem] ");

        // 键可驱动 mesher（QML 消费契约）：逐枚举键喂 ChunkGeometry（w23 全 6 键 + w33 首
        //   末两键）→ vertexCount > 0（fresh 世界每 chunk 均有地形 mesh；空 chunk 世界无）。
        bool meshableOk = true;
        {
            ChunkGeometry geo;
            geo.setWorld(&w23);
            for (int i = 0; i < n23 && meshableOk; ++i) {
                const auto k = order23.at(i);
                geo.setCx(k.first);
                geo.setCz(k.second);
                if (geo.vertexCount() <= 0)
                    meshableOk = false;
            }
            geo.setWorld(&w33);
            for (int i = 0; i < n33 && meshableOk; i += n33 - 1) { // 首 (0,0) + 末 (2,2)
                const auto k = order33.at(i);
                geo.setCx(k.first);
                geo.setCz(k.second);
                if (geo.vertexCount() <= 0)
                    meshableOk = false;
            }
        }
        ok = ok && meshableOk;
        if (!meshableOk)
            diag += QStringLiteral("[meshable] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2021a fixed-world bit-identity wall (P4 QML dynamization):"
                             " the resident-set enumeration on fresh streaming-off worlds"
                             " equals the legacy fixed-grid loop order item-for-item (3x3"
                             " square AND 2x3 non-square, the latter discriminating cz-major"
                             " from cx-major), count equals chunksX*chunksZ, revision starts"
                             " at 0 and block edits emit zero revision edges (production has"
                             " zero lifecycle transitions - P1-P3/D6 components are"
                             " unwired), out-of-range keyAt returns an empty list, a second"
                             " enumeration is bit-identical (no hidden state), and every"
                             " enumerated key drives a ChunkGeometry to a non-empty mesh"
                             " (the QML slot-pool consumption contract: enumerated keys are"
                             " directly usable as cx/cz)"
                          << (ok ? QString()
                                 : QStringLiteral("diag n33=%1 rev33=%2 n23=%3 rev23=%4"
                                                  " ord33=%5 ord23=%6 oob=%7 edits=%8"
                                                  " em33=%9 em23=%10 mesh=%11 idem=%12 %13")
                                       .arg(n33)
                                       .arg(w33.residentChunkRevision())
                                       .arg(n23)
                                       .arg(w23.residentChunkRevision())
                                       .arg(keysEq(order33, kGrid33))
                                       .arg(keysEq(order23, kGrid23))
                                       .arg(oobOk)
                                       .arg(editsOk)
                                       .arg(em33)
                                       .arg(em23)
                                       .arg(meshableOk)
                                       .arg(idemOk)
                                       .arg(diag));
    });

    // ── r2021b：模型响应性（revision/发射沿精确对账 + 枚举随增删）─────────────────────
    runLegMulti({ "r2021b model responsiveness (P4 QML dynamization): legal lifecycle"
        " transitions move the resident set with exact revision/emission accounting -"
        " Loaded->Evicting removes exactly one key (revision +1, one emission, enumeration"
        " shrinks in order), Evicting->Absent is outside the set and bumps nothing,"
        " the Absent->Loading->Generated->Loaded re-add chain bumps exactly once at"
        " Generated->Loaded and the key returns to its canonical cz-major index,"
        " Loaded<->Active are intra-set moves with zero edges, direct Loaded->Absent is"
        " rejected with zero edges, and Evicting->Loaded (evict-cancel add-back) bumps"
        " once; enumeration follows every membership change item-for-item" }, [&]() {
        bool ok = true;
        QString diag;

        World w;
        freshWorld48(w);
        int em = 0;
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&em]() { ++em; });

        const bool baseOk = w.residentChunkCount() == 9
            && w.residentChunkRevision() == 0 && em == 0;
        ok = ok && baseOk;
        if (!baseOk)
            diag += QStringLiteral("[base n=%1 rev=%2 em=%3] ")
                        .arg(w.residentChunkCount())
                        .arg(w.residentChunkRevision())
                        .arg(em);

        // ⑥ 移出 (1,1)：revision +1、恰一发射、枚举恰缺 (1,1)、余键相对序保持。
        const bool evOk = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
        const auto afterRemove = enumerateModel(w);
        bool rmOk = evOk && w.residentChunkRevision() == 1 && em == 1
            && w.residentChunkCount() == 8;
        for (const auto &k : kGrid33) {
            const bool present = afterRemove.contains(k);
            if (k == QPair<int, int>(1, 1))
                rmOk = rmOk && !present;
            else
                rmOk = rmOk && present;
        }
        ok = ok && rmOk;
        if (!rmOk)
            diag += QStringLiteral("[rm ev=%1 rev=%2 em=%3 n=%4] ")
                        .arg(evOk)
                        .arg(w.residentChunkRevision())
                        .arg(em)
                        .arg(w.residentChunkCount());

        // ⑦ Evicting→Absent：集外沿零 bump（revision 只追成员集，不追全转移史）。
        const bool abOk = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
        const bool outOk = abOk && w.residentChunkRevision() == 1 && em == 1
            && w.residentChunkCount() == 8;
        ok = ok && outOk;
        if (!outOk)
            diag += QStringLiteral("[out ab=%1 rev=%2 em=%3] ")
                        .arg(abOk)
                        .arg(w.residentChunkRevision())
                        .arg(em);

        // ①②③ 重加链：仅 ③ Generated→Loaded 跨驻留边界 → 全链恰 +1；(1,1) 归位 cz 主序
        //   index 4 = { (0,0),(1,0),(2,0),(0,1),(1,1),... } 的正席。
        const bool l1 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading);
        const bool l2 = l1 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated);
        const bool midOk = !l1 || w.residentChunkRevision() == 1; // ①② 途中零 bump
        const bool l3 = l2 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
        const auto afterReadd = enumerateModel(w);
        const bool addOk = l1 && l2 && l3 && midOk
            && w.residentChunkRevision() == 2 && em == 2
            && w.residentChunkCount() == 9
            && keysEq(afterReadd, kGrid33)
            && afterReadd.indexOf(QPair<int, int>(1, 1)) == 4;
        ok = ok && addOk;
        if (!addOk)
            diag += QStringLiteral("[add %1%2%3 mid=%4 rev=%5 em=%6 n=%7 idx=%8] ")
                        .arg(l1).arg(l2).arg(l3).arg(midOk)
                        .arg(w.residentChunkRevision())
                        .arg(em)
                        .arg(w.residentChunkCount())
                        .arg(afterReadd.indexOf(QPair<int, int>(1, 1)));

        // ⑤④ 集内平移：Loaded→Active→Loaded 零 bump 零发射（驻留集 = {Loaded, Active}）。
        const bool a1 = w.setChunkLifecycle(2, 2, ChunkLifecycle::Active);
        const bool intra1Ok = a1 && w.residentChunkRevision() == 2 && em == 2
            && w.residentChunkCount() == 9;
        const bool a2 = w.setChunkLifecycle(2, 2, ChunkLifecycle::Loaded);
        const bool intraOk = intra1Ok && a2 && w.residentChunkRevision() == 2 && em == 2;
        ok = ok && intraOk;
        if (!intraOk)
            diag += QStringLiteral("[intra %1%2 rev=%3 em=%4] ")
                        .arg(a1).arg(a2)
                        .arg(w.residentChunkRevision())
                        .arg(em);

        // 非法转移：Loaded→Absent 直跳被守卫拒 → 零沿零变化。
        const bool rej = w.setChunkLifecycle(2, 2, ChunkLifecycle::Absent);
        const bool rejOk = !rej && w.residentChunkRevision() == 2 && em == 2
            && w.residentChunkCount() == 9;
        ok = ok && rejOk;
        if (!rejOk)
            diag += QStringLiteral("[rej acc=%1 rev=%2 em=%3] ")
                        .arg(rej)
                        .arg(w.residentChunkRevision())
                        .arg(em);

        // ⑥⑧ 加回边：(0,0) 选中驱逐（-1）后取消驱逐（+1）→ 枚举复原、revision 对账。
        const bool e1 = w.setChunkLifecycle(0, 0, ChunkLifecycle::Evicting);
        const auto duringEvict = enumerateModel(w);
        const bool e2 = e1 && w.setChunkLifecycle(0, 0, ChunkLifecycle::Loaded);
        const bool cancelOk = e1 && e2
            && w.residentChunkRevision() == 4 && em == 4
            && w.residentChunkCount() == 9
            && !duringEvict.contains(QPair<int, int>(0, 0))
            && keysEq(enumerateModel(w), kGrid33);
        ok = ok && cancelOk;
        if (!cancelOk)
            diag += QStringLiteral("[cancel %1%2 rev=%3 em=%4 n=%5] ")
                        .arg(e1).arg(e2)
                        .arg(w.residentChunkRevision())
                        .arg(em)
                        .arg(w.residentChunkCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2021b model responsiveness (P4 QML dynamization): legal"
                             " lifecycle transitions move the resident set with exact"
                             " revision/emission accounting - Loaded->Evicting removes"
                             " exactly one key (revision +1, one emission, enumeration"
                             " shrinks in order), Evicting->Absent is outside the set and"
                             " bumps nothing, the Absent->Loading->Generated->Loaded"
                             " re-add chain bumps exactly once at Generated->Loaded and the"
                             " key returns to its canonical cz-major index, Loaded<->Active"
                             " are intra-set moves with zero edges, direct Loaded->Absent is"
                             " rejected with zero edges, and Evicting->Loaded"
                             " (evict-cancel add-back) bumps once; enumeration follows"
                             " every membership change item-for-item"
                          << (ok ? QString()
                                 : QStringLiteral("diag rev=%1 em=%2 n=%3 %4")
                                       .arg(w.residentChunkRevision())
                                       .arg(em)
                                       .arg(w.residentChunkCount())
                                       .arg(diag));
    });

    // ── r2021c：QML real-chain 响应性（真 QQmlEngine × 真 World × slot 池镜像）──────────
    //   既有 QML 真链 harness 家族（section03）同门：setData wrapper（最小复刻 Main.qml
    //   rebuildChunkSlotPool 的消费逻辑）+ 真 World 经 context property 注入——revision 沿
    //   直达 QML Connections handler、QVariantList 键经真引擎值转换、池随模型增删重建。
    runLegMulti({ "r2021c qml real-chain responsiveness (P4 QML dynamization; real-chain"
        " harness family): a real QQmlEngine loads a setData wrapper mirroring Main.qml's"
        " slot-pool consumption (residentChunkCount + residentChunkKeyAt enumeration,"
        " Connections on the revision notify) against a real streaming-off World - the"
        " initial pool build enumerates all nine keys through the real engine, each C++"
        " side membership transition (remove and re-add of two different chunks) reaches"
        " the QML handler synchronously and rebuilds the pool so the slot list follows the"
        " model item-for-item, and the engine stays warning-free throughout (clean"
        " surface: no unknown-method or conversion noise)" }, [&]() {
        bool ok = true;
        QString diag;

        World w;
        freshWorld48(w);
        int em = 0;
        QObject::connect(&w, &World::residentChunkRevisionChanged, [&em]() { ++em; });

        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 既有真链先例同款：防磁盘缓存重定向
        QQmlEngine engine;
        int qmlWarnings = 0;
        QString firstWarning;
        QObject::connect(&engine, &QQmlEngine::warnings,
            [&qmlWarnings, &firstWarning](const QList<QQmlError> &list) {
                for (const QQmlError &e : list) {
                    ++qmlWarnings;
                    if (firstWarning.isEmpty())
                        firstWarning = e.toString();
                }
            });
        engine.rootContext()->setContextProperty(QStringLiteral("r2021World"), &w);

        // wrapper：Main.qml rebuildChunkSlotPool 消费逻辑的最小镜像（count + keyAt 逐项拉取
        //   → slots 数组；revision 沿 → rebuildPool）。真引擎里 QVariantList→JS array 值转换
        //   与 Connections 沿语义都在链路上（headless 可断言面）。
        QQmlComponent wrapComp(&engine);
        wrapComp.setData(R"QML(import QtQuick
Item {
    property var world
    property var slots: []
    property int rebuilds: 0
    property string qmlErr: ""
    function rebuildPool() {
        var ks = []
        try {
            var n = world.residentChunkCount()
            for (var i = 0; i < n; ++i)
                ks.push(world.residentChunkKeyAt(i))
        } catch (e) {
            qmlErr = "" + e
        }
        slots = ks
        rebuilds++
    }
    Connections {
        target: world
        function onResidentChunkRevisionChanged() { rebuildPool() }
    }
}
)QML", QUrl());
        QQuickItem *pool = nullptr;
        if (wrapComp.isError()) {
            ok = false;
            diag += QStringLiteral("[wrap %1] ").arg(wrapComp.errorString());
        } else {
            // 初参注入 world（创建期 target 已定义 → Connections 无「signal not found」
            //   创建期警告；创建后再 setProperty 会先经 undefined target 一轮噪声）。
            pool = qobject_cast<QQuickItem *>(wrapComp.createWithInitialProperties(
                { { QStringLiteral("world"), QVariant::fromValue<QObject *>(&w) } }));
            if (!pool) {
                ok = false;
                diag += QStringLiteral("[wrap-create] ");
            } else {
                pool->setParent(&engine); // 引擎析构兜底
            }
        }

        // 槽池读取镜像（C++ 侧对 QML slots 属性的断言视图）。
        const auto readSlots = [pool]() {
            QVector<QPair<int, int>> keys;
            if (!pool)
                return keys;
            const QVariantList arr = pool->property("slots").toList();
            for (const QVariant &v : arr) {
                const QVariantList k = v.toList();
                if (k.size() == 2)
                    keys.append({ k.at(0).toInt(), k.at(1).toInt() });
            }
            return keys;
        };

        if (pool) {
            QMetaObject::invokeMethod(pool, "rebuildPool"); // 初建（world 初参已注入）
            const auto initial = readSlots();
            const bool buildOk = initial.size() == 9 && keysEq(initial, kGrid33)
                && pool->property("rebuilds").toInt() == 1
                && pool->property("qmlErr").toString().isEmpty();
            ok = ok && buildOk;
            if (!buildOk)
                diag += QStringLiteral("[build n=%1 rb=%2 err=%3] ")
                            .arg(initial.size())
                            .arg(pool->property("rebuilds").toInt())
                            .arg(pool->property("qmlErr").toString());

            // 移出 (1,1)：C++ 发射沿同步直达 QML handler（同线程直连）→ 池缩到 8。
            const bool ev = w.setChunkLifecycle(1, 1, ChunkLifecycle::Evicting);
            const auto afterRm = readSlots();
            const bool rmOk = ev && afterRm.size() == 8
                && !afterRm.contains(QPair<int, int>(1, 1))
                && pool->property("rebuilds").toInt() == 2;
            ok = ok && rmOk;
            if (!rmOk)
                diag += QStringLiteral("[rm ev=%1 n=%2 rb=%3] ")
                            .arg(ev)
                            .arg(afterRm.size())
                            .arg(pool->property("rebuilds").toInt());

            // ⑦ 补离集 + ①②③ 重加链（Evicting 不能直跳 Loading，合法路径必经 ⑦ 回
            //   Absent——r2021b 同序）：池复原九键、(1,1) 归位（QML 侧响应随沿重建）。
            const bool a0 = w.setChunkLifecycle(1, 1, ChunkLifecycle::Absent);
            const bool l1 = a0 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loading);
            const bool l2 = l1 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Generated);
            const bool l3 = l2 && w.setChunkLifecycle(1, 1, ChunkLifecycle::Loaded);
            const auto afterAdd = readSlots();
            const bool addOk = a0 && l1 && l2 && l3 && afterAdd.size() == 9
                && keysEq(afterAdd, kGrid33)
                && pool->property("rebuilds").toInt() == 3;
            ok = ok && addOk;
            if (!addOk)
                diag += QStringLiteral("[add %1%2%3%4 n=%5 rb=%6] ")
                            .arg(a0).arg(l1).arg(l2).arg(l3)
                            .arg(afterAdd.size())
                            .arg(pool->property("rebuilds").toInt());

            // 第二 chunk (2,2) 移出+加回：池随第二次增删再走两轮（沿可重复消费、无一次触发疲劳）。
            const bool e1 = w.setChunkLifecycle(2, 2, ChunkLifecycle::Evicting);
            const auto afterRm2 = readSlots();
            const bool e2 = e1 && w.setChunkLifecycle(2, 2, ChunkLifecycle::Loaded);
            const auto afterAdd2 = readSlots();
            const bool round2Ok = e1 && e2 && afterRm2.size() == 8 && afterAdd2.size() == 9
                && keysEq(afterAdd2, kGrid33)
                && pool->property("rebuilds").toInt() == 5;
            ok = ok && round2Ok;
            if (!round2Ok)
                diag += QStringLiteral("[r2 %1%2 n=%3/%4 rb=%5] ")
                            .arg(e1).arg(e2)
                            .arg(afterRm2.size())
                            .arg(afterAdd2.size())
                            .arg(pool->property("rebuilds").toInt());

            // 对账：QML 侧 rebuilds == C++ 侧发射数 + 初建一次（沿不丢不重的账面闭合）。
            const bool ledgersOk = pool->property("rebuilds").toInt() == em + 1;
            ok = ok && ledgersOk;
            if (!ledgersOk)
                diag += QStringLiteral("[ledger qml=%1 cpp=%2] ")
                            .arg(pool->property("rebuilds").toInt())
                            .arg(em);
        }

        const bool cleanOk = qmlWarnings == 0 && pool != nullptr;
        ok = ok && cleanOk;
        if (!cleanOk)
            diag += QStringLiteral("[warn n=%1 %2] ").arg(qmlWarnings).arg(firstWarning);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2021c qml real-chain responsiveness (P4 QML dynamization;"
                             " real-chain harness family): a real QQmlEngine loads a setData"
                             " wrapper mirroring Main.qml's slot-pool consumption"
                             " (residentChunkCount + residentChunkKeyAt enumeration,"
                             " Connections on the revision notify) against a real"
                             " streaming-off World - the initial pool build enumerates all"
                             " nine keys through the real engine, each C++ side membership"
                             " transition (remove and re-add of two different chunks)"
                             " reaches the QML handler synchronously and rebuilds the pool"
                             " so the slot list follows the model item-for-item, and the"
                             " engine stays warning-free throughout (clean surface: no"
                             " unknown-method or conversion noise)"
                          << (ok ? QString()
                                 : QStringLiteral("diag rb=%1 warn=%2 %3")
                                       .arg(pool ? pool->property("rebuilds").toInt() : -1)
                                       .arg(qmlWarnings)
                                       .arg(diag));
    });

    // ── r2021d：钉面（新 QML 面逐一正面钉 + 旧网格退役反探 + 玩法路径零触碰 + 集中）──────
    runLegMulti({ "r2021d surface pins (P4 QML dynamization): comment-stripped source pins"
        " hold the new minimal resident-set face on World (one Q_PROPERTY revision, two"
        " Q_INVOKABLE enumerators, the single notify emit, the membership-edge bump"
        " predicate, and the single cz-major enumeration authority in world.cpp), the"
        " legacy fixed-grid loop text is retired from Main.qml while the model-driven"
        " consumption is present with exact occurrence counts (count/keyAt x1,"
        " revision-changed handler x1, pool rebuild entry x3), Main.qml stays silent on"
        " the lifecycle decision face (no setChunkLifecycle - the standing zero-decision"
        " surface extends to the render model) and keeps its gameplay-path markers"
        " (onTicked bridge, enterWorld, startGame) with zero GameSession mentions"
        " (the standing zero-migration negative pin)" }, [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① world.h 新面正面钉（每新增 Q_PROPERTY / Q_INVOKABLE 一钉；剥注释）。
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("r2021 revision property",
                    "Q_PROPERTY(int residentChunkRevision READ residentChunkRevision"
                    " NOTIFY residentChunkRevisionChanged)", 1),
                SrcPin("r2021 count invokable",
                    "Q_INVOKABLE int residentChunkCount() const", 1),
                SrcPin("r2021 keyAt invokable",
                    "Q_INVOKABLE QVariantList residentChunkKeyAt(int index) const", 1),
                SrcPin("r2021 single notify emit", "emit residentChunkRevisionChanged()", 1),
                SrcPin("r2021 membership-edge bump predicate",
                    "chunkLifecycleQueryable(from) != chunkLifecycleQueryable(to)", 1),
            });
        // ② world.cpp 枚举序单一权威正面钉（函数签名 + 驻留谓词消费——不钉循环嵌套文本，
        //    NEG-2 恰红面收缩到 r2021a 的设计面）。
        const QStringList missWc = pinSet(
            srcRoot + QStringLiteral("/World/world.cpp"), {
                SrcPin("r2021 enumeration authority",
                    "QVector<QPair<int, int>> World::residentChunkKeysOrdered() const", 1),
                SrcPin("r2021 enumeration consults the resident predicate",
                    "chunkLifecycleQueryable(m_chunks.lifecycleAt(cx, cz))", 1),
            });
        for (const QString &m : missWh + missWc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ Main.qml 消费面精确计数（正面）+ 旧网格退役反探（miss 非空 = 合规缺席）。
        const QStringList missQmlPos = pinSet(
            srcRoot + QStringLiteral("/ui/Main.qml"), {
                SrcPin("r2021 qml consumes residentChunkCount", "residentChunkCount", 1),
                SrcPin("r2021 qml consumes residentChunkKeyAt", "residentChunkKeyAt", 1),
                SrcPin("r2021 qml revision handler", "onResidentChunkRevisionChanged", 1),
                SrcPin("r2021 qml pool rebuild entry (definition + onCompleted +"
                       " revision handler)",
                    "rebuildChunkSlotPool", 3),
                SrcPin("r2021 qml initial build routed through the pool",
                    "Component.onCompleted: { window.rebuildChunkSlotPool() }", 1),
            });
        for (const QString &m : missQmlPos) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString mainPath = srcRoot + QStringLiteral("/ui/Main.qml");
        const bool qmlNegOk = forbiddenAbsent(mainPath, "setChunkLifecycle")
            && forbiddenAbsent(mainPath, "const nx = theWorld.chunksX")
            && forbiddenAbsent(mainPath, "for (let cz = 0; cz < nz; ++cz)");
        ok = ok && qmlNegOk;
        if (!qmlNegOk)
            diag += QStringLiteral("[qml-neg] ");

        // ④ 玩法路径零触碰（桥接面 / 玩法入口在位 + 既有零迁移阴性钉复述——裸 contains 只做
        //    「禁出字面」单向检查）。
        const QStringList missGame = pinSet(mainPath, {
            SrcPin("r2021 gameplay bridge intact: worldClock onTicked", "function onTicked(dt)", 1),
            SrcPin("r2021 gameplay entry intact: enterWorld", "function enterWorld(", 1),
            SrcPin("r2021 gameplay entry intact: startGame", "function startGame()", 1),
        });
        for (const QString &m : missGame) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        bool noSessionOk = false;
        QFile mf(mainPath);
        if (mf.open(QIODevice::ReadOnly)) {
            noSessionOk = !QString::fromUtf8(mf.readAll())
                               .contains(QLatin1String("GameSession"));
        }
        ok = ok && noSessionOk;
        if (!noSessionOk)
            diag += QStringLiteral("[zero-migration] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2021d surface pins (P4 QML dynamization): comment-stripped"
                             " source pins hold the new minimal resident-set face on World"
                             " (one Q_PROPERTY revision, two Q_INVOKABLE enumerators, the"
                             " single notify emit, the membership-edge bump predicate, and"
                             " the single cz-major enumeration authority in world.cpp), the"
                             " legacy fixed-grid loop text is retired from Main.qml while"
                             " the model-driven consumption is present with exact"
                             " occurrence counts (count/keyAt x1, revision-changed handler"
                             " x1, pool rebuild entry x3), Main.qml stays silent on the"
                             " lifecycle decision face (no setChunkLifecycle - the standing"
                             " zero-decision surface extends to the render model) and keeps"
                             " its gameplay-path markers (onTicked bridge, enterWorld,"
                             " startGame) with zero GameSession mentions (standing"
                             " zero-migration negative pin)"
                          << (ok ? QString()
                                 : QStringLiteral("diag wh=%1 wc=%2 qmlPos=%3 qmlNeg=%4"
                                                  " game=%5 %6")
                                       .arg(missWh.isEmpty())
                                       .arg(missWc.isEmpty())
                                       .arg(missQmlPos.isEmpty())
                                       .arg(qmlNegOk)
                                       .arg(missGame.isEmpty() && noSessionOk)
                                       .arg(diag));
    });
}
