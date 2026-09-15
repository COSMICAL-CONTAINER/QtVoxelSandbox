#include "matrix_helpers.h"

#include "chunklifecycle.h" // R20.10 被测：六态类型 + 转移表单一权威（经 world.h → chunkmanager.h 亦可达，显式 include 表明被测面）
#include "generationjob.h"  // R20.10b 联动面：失败恢复边⑨的两路投递点（同步泵 r2010ba / pumpAsync r2010bb）
#include "worldfacade.h"    // R20.10 联动面：三门（chunkExistsAt/chunkDirtyAt/chunkFluidOnlyDirtyAt）生命周期接线

// R20.10 Chunk lifecycle 探针段（6 腿 r2010a-d + r2010ba/bb；filter 词 "r2010"；矩阵
// 570→574，R20.10b 595→597，band 597±1 内）。置尾先例沿用（接 section13，runAll 末执行）；
// r2010a 纯图腿零世界（独立 ChunkManager 网格），r2010b-d 自建 fresh 小世界 48×48×96 seed 82
// + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)（section11+ 先例），r2010ba/bb
// 裸 3×3 ChunkManager + 裸 scheduler（零世界零 tick 零 RNG），对 rig 世界 w 零接触。任务契约
//（docs/refactor-plan-2026-09-08.md §29.3 R20.10 原文四验收）：
//   「先实现 Absent、Loading、Generated、Active、Loaded、Evicting」→ r2010a（六态全图：枚举域
//     六值 + 独立编码的 8 合法边表 vs 转移权威 36 对逐对互证 + 守卫式 setter 在真网格上同表；
//     合法路径 Loaded→Active→Loaded→Evicting→Absent→Loading→Generated→Loaded 走通全部 8 边
//     访遍全部 6 态 + Evicting→Loaded 取消边单独走）；
//   「当前固定 10×10 世界仍可运行」→ r2010b（默认稳态实证：fresh 世界全部 chunk lifecycle ==
//     Loaded（等价现行「常驻已加载」）+ Facade 存在门 3×3 全 true + 与裸脏标记 pass-through
//     等价 + 编辑破/放往返后栅格 id+state 逐位恒等且态表不动——玩家路径零变化）；
//   「Chunk 可以卸载并重新加载」→ r2010c（端到端：Loaded→Evicting→Absent 两步卸载 → Facade
//     三门（存在/脏/流体专用脏）恒 false → 邻 chunk 编辑而被观测 chunk 顶点数不变（mesher 门
//     跳过）→ Absent→Loading→Generated→Loaded 三步重载 → 三门恢复 + 栅格逐位恒等（含卸载窗
//     内的编辑）→ 门复_live（脏门再建顶点差分）；
//   「没有未保存数据被静默丢失」+「QML 不再决定 Chunk 的真实生命周期」→ r2010d（存档往返：
//     存①→卸载 (2,2)→存②（worldstore 直读 chunk blob 豁免域不受态影响——卸载态下 saveAll 照
//     常序列化全部 chunk）→ 新世界 beginLoad+loadChunks+finishLoad 回读 → 双世界栅格逐位恒等
//     + store 未动生命周期态；源码钉：侧表/守卫/门谓词/C++ forwarder 正面钉 + Q_INVOKABLE/
//     Q_PROPERTY/Main.qml「ChunkLifecycle」三反探（QML 零生命周期访问面））。
// **Edits-on-evict 选型登记（docs dev-plan R20.10 关单同文）**：plan 未定义驱逐时编辑语义 →
//   最小解释 = Evicting/Absent 只表达状态可达（数据保留在内存，路由读写不受生命周期门控——
//   worldgen/store 豁免域直读 blob 的前提），**不实现落盘驱逐**（数据保留策略登记为后续单）。
//   r2010c/r2010d 按此断言：卸载窗内编辑保留、重载后逐位恢复、存②照常带出全部 chunk。
// 阴性轮登记：摘 ChunkManager::setLifecycle 的转移守卫**调用点**（`if
//   (!chunkLifecycleTransitionLegal(...)) return false;` 两行摘除——t1051 敏训：摘调用点，
//   勿用 false && 前缀把条件守卫变无条件执行）→ 非法转移被接受 → r2010a 恰红（36 对互证腿），
//   其余腿（合法路径/默认稳态/卸载重载/存档往返）不受扰。
// **R20.10b 失败恢复边⑨（审计 #9 ①放行单——R20.11 登记欠账「失败停 Loading，恢复登记后续」
//   的闭合）**：六态表 8→9 边（新增⑨ Loading→Absent，语义选型见 chunklifecycle.h 头注释——
//   失败 outcome **实际投递**才转移；epoch 过期/取消的丢弃面不投递故不转移；重请求=新 job，
//   与 R20.12「epoch 丢弃旧任务」同门；自动重试策略登记非目标）。两路投递点同走一边（单一
//   权威，禁两份转移逻辑）：同步泵（r2010ba）+ pumpAsync 收割（r2010bb 脚本化异步 worker 走
//   R20.12 缝——BackgroundGenerationWorker 纯函数恒成功面，失败注入用 isAsynchronous() 测试
//   替身）。表计数变化 → r2010a 既有计数腿**同变更修订**（纠偏非放宽：9 边/27 非法 + 走图
//   加⑨段）；r2011d ③（section15）「失败停 Loading」旧钉同步改写（Absent+重请求可行）。
//   固定世界零变化：失败注入打在默认稳态 Loaded chunk 上，边①/边⑨双双被守卫拒 → 表逐位
//   不动（r2010ba④/r2010bb④ 失败注入版实证）。
void MatrixRun::section14_chunklifecycle()
{
    // ── 共享 rig：fresh 小世界构造 + 确定性天气钉（section12 initTwin 同款）────────────────
    const auto initTwin = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 放置列选址：heightAt 顶 +1 恒空的第一列（确定性扫描；section12 placeCol 同款）。
    const auto placeCol = [](World &w, int x0, int z0) -> QPair<int, int> {
        for (int dz = 0; dz < 8; ++dz) {
            const int z = z0 + dz;
            const int h = w.heightAt(x0, z);
            if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h + 1, z) == 0)
                return QPair<int, int>(x0, z);
        }
        qFatal("r2010 rig: no placeable column near (%d,%d)", x0, z0);
        return QPair<int, int>(-1, -1);
    };
    // 全栅格 id+state 快照（48×48×96 双通道堆快照；逐位恒等核对用）。
    const auto snapGrid = [](World &w) {
        QVector<quint8> g;
        const int n = w.width() * w.depth() * w.height();
        g.resize(n * 2);
        int i = 0;
        for (int x = 0; x < w.width(); ++x)
            for (int z = 0; z < w.depth(); ++z)
                for (int y = 0; y < w.height(); ++y) {
                    g[i++] = w.blockAt(x, y, z);
                    g[i++] = w.stateAt(x, y, z);
                }
        return g;
    };
    const auto gridEqual = [](World &a, World &b, QString &why) {
        if (a.width() != b.width() || a.depth() != b.depth() || a.height() != b.height()) {
            why = QStringLiteral("dims %1x%2x%3 vs %4x%5x%6")
                      .arg(a.width()).arg(a.depth()).arg(a.height())
                      .arg(b.width()).arg(b.depth()).arg(b.height());
            return false;
        }
        for (int x = 0; x < a.width(); ++x)
            for (int z = 0; z < a.depth(); ++z)
                for (int y = 0; y < a.height(); ++y)
                    if (a.blockAt(x, y, z) != b.blockAt(x, y, z)
                        || a.stateAt(x, y, z) != b.stateAt(x, y, z)) {
                        why = QStringLiteral("delta@(%1,%2,%3) %4/%5 vs %6/%7")
                                  .arg(x).arg(y).arg(z)
                                  .arg(a.blockAt(x, y, z)).arg(a.stateAt(x, y, z))
                                  .arg(b.blockAt(x, y, z)).arg(b.stateAt(x, y, z));
                        return false;
                    }
        return true;
    };
    // R20.10b 新腿帮手：chunk 键 packed（section15/16 同款）。
    const auto pk = [](int cx, int cz) { return ChunkKey{ cx, cz }.packed(); };

    // ── r2010a：六态全图——枚举域 + 独立边表互证 + 守卫式 setter 同表（零世界）───────────
    //   （R20.10b 同变更修订：9 边/27 非法 + 走图加⑨失败恢复段——纠偏非放宽，随失败边扩表）
    runLeg(QStringLiteral("r2010a six-state graph authority (R20.10 Chunk lifecycle): the"
        " ChunkLifecycle enum has exactly six states Absent/Loading/Generated/Active/Loaded/"
        "Evicting, an independently encoded table of the nine legal edges (Absent->Loading,"
        " Loading->Generated, Generated->Loaded, Loaded->Active, Active->Loaded, Loaded->"
        "Evicting, Evicting->Absent, Evicting->Loaded, Loading->Absent) matches the transition"
        " authority over all 36 from/to pairs (the other 27 including self-transitions are"
        " illegal), a bare"
        " 3x3 ChunkManager grid starts every chunk in the resident-Loaded default steady"
        " state with out-of-bounds reading Absent and rejecting writes, the full legal walk"
        " Loaded->Active->Loaded->Evicting->Absent->Loading->Generated->Loaded traverses all"
        " eight original edges and all six states plus the cancel edge, and the R20.10b"
        " failure-recovery edge Loading->Absent walks on its own before re-walking the"
        " request chain, and a guarded setter positioned in each of the six states accepts"
        " exactly the documented targets and rejects every other transition without state"
        " change"), [&]() {
        bool ok = true;
        QString diag;

        // ① 枚举域恰六态（Count 哨兵 = 6）：
        const bool enumOk = int(ChunkLifecycle::Count) == 6;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum count=%1] ").arg(int(ChunkLifecycle::Count));

        // ② 独立编码的合法边表（腿自持真相——与 chunklifecycle.h 单一权威互证防双漂移；
        //    R20.10b 同变更修订：⑨ 失败恢复边入表）：
        const QPair<ChunkLifecycle, ChunkLifecycle> edges[] = {
            { ChunkLifecycle::Absent, ChunkLifecycle::Loading },       // ① requestLoad
            { ChunkLifecycle::Loading, ChunkLifecycle::Generated },    // ② generateDone
            { ChunkLifecycle::Generated, ChunkLifecycle::Loaded },     // ③ promoteToResident
            { ChunkLifecycle::Loaded, ChunkLifecycle::Active },        // ④ activate
            { ChunkLifecycle::Active, ChunkLifecycle::Loaded },        // ⑤ deactivate
            { ChunkLifecycle::Loaded, ChunkLifecycle::Evicting },      // ⑥ requestEvict
            { ChunkLifecycle::Evicting, ChunkLifecycle::Absent },      // ⑦ finishEvict
            { ChunkLifecycle::Evicting, ChunkLifecycle::Loaded },      // ⑧ cancelEvict
            { ChunkLifecycle::Loading, ChunkLifecycle::Absent },       // ⑨ generateFailed（R20.10b 失败恢复）
        };
        const auto edgeLegal = [&](ChunkLifecycle f, ChunkLifecycle t) {
            for (const auto &e : edges)
                if (e.first == f && e.second == t)
                    return true;
            return false;
        };
        bool tableOk = true;
        ChunkLifecycle badFrom = ChunkLifecycle::Absent, badTo = ChunkLifecycle::Absent;
        bool badPure = false, badInv = false;
        for (int fi = 0; fi < int(ChunkLifecycle::Count) && tableOk; ++fi) {
            for (int ti = 0; ti < int(ChunkLifecycle::Count) && tableOk; ++ti) {
                const ChunkLifecycle f = ChunkLifecycle(fi), t = ChunkLifecycle(ti);
                const bool pure = chunkLifecycleTransitionLegal(f, t);
                const bool want = edgeLegal(f, t);
                if (pure != want) { // 权威 ↔ 独立表 双向一致（多边/少边都红）
                    tableOk = false;
                    badFrom = f; badTo = t; badPure = pure;
                }
            }
        }
        // 门谓词独立核：恰 {Loaded, Active} 可查询。
        bool qOk = true;
        for (int si = 0; si < int(ChunkLifecycle::Count); ++si) {
            const ChunkLifecycle s = ChunkLifecycle(si);
            const bool want = (s == ChunkLifecycle::Loaded || s == ChunkLifecycle::Active);
            qOk = qOk && chunkLifecycleQueryable(s) == want;
        }
        tableOk = tableOk && qOk;
        if (!tableOk)
            diag += QStringLiteral("[table %1->%2 pure=%3 q=%4] ")
                        .arg(chunkLifecycleName(badFrom)).arg(chunkLifecycleName(badTo))
                        .arg(badPure).arg(qOk);
        ok = ok && tableOk;

        // ③ 裸 3×3 网格：默认稳态全 Loaded + 越界 Absent/写拒：
        ChunkManager mgr(48, 48, 32); // 3×3 chunk（16×16 列 × 3）
        bool steadyOk = mgr.chunksX() == 3 && mgr.chunksZ() == 3 && mgr.chunkCount() == 9;
        for (int cz = 0; cz < 3 && steadyOk; ++cz)
            for (int cx = 0; cx < 3 && steadyOk; ++cx)
                if (mgr.lifecycleAt(cx, cz) != ChunkLifecycle::Loaded) {
                    steadyOk = false;
                    diag += QStringLiteral("[steady (%1,%2)=%3] ")
                                .arg(cx).arg(cz)
                                .arg(chunkLifecycleName(mgr.lifecycleAt(cx, cz)));
                }
        steadyOk = steadyOk && mgr.lifecycleAt(-1, 0) == ChunkLifecycle::Absent
            && mgr.lifecycleAt(3, 3) == ChunkLifecycle::Absent
            && !mgr.setLifecycle(-1, 0, ChunkLifecycle::Loading)
            && !mgr.setLifecycle(3, 0, ChunkLifecycle::Evicting);
        if (!steadyOk) diag += QStringLiteral("[oob] ");
        ok = ok && steadyOk;

        // ④ 全 8 边合法路径走通（访遍 6 态）+ 取消边单独走：
        const auto step = [&](int cx, int cz, ChunkLifecycle to) {
            const ChunkLifecycle pre = mgr.lifecycleAt(cx, cz);
            const bool r = mgr.setLifecycle(cx, cz, to);
            if (!r || mgr.lifecycleAt(cx, cz) != to) {
                diag += QStringLiteral("[walk (%1,%2) %3->%4 r=%5 now=%6] ")
                            .arg(cx).arg(cz).arg(chunkLifecycleName(pre))
                            .arg(chunkLifecycleName(to)).arg(r)
                            .arg(chunkLifecycleName(mgr.lifecycleAt(cx, cz)));
                return false;
            }
            return true;
        };
        // (1,1)：Loaded→Active→Loaded→Evicting→Absent→Loading→Generated→Loaded（8 边全走，
        //   其中 Evicting→Absent 走完成边、Absent→Loading 起重载链）。
        bool walkOk = step(1, 1, ChunkLifecycle::Active)
            && step(1, 1, ChunkLifecycle::Loaded)
            && step(1, 1, ChunkLifecycle::Evicting)
            && step(1, 1, ChunkLifecycle::Absent)
            && step(1, 1, ChunkLifecycle::Loading)
            && step(1, 1, ChunkLifecycle::Generated)
            && step(1, 1, ChunkLifecycle::Loaded);
        // (0,1)：取消边 Evicting→Loaded 单独走（驱逐途中又被需要）。
        walkOk = walkOk && step(0, 1, ChunkLifecycle::Evicting)
            && step(0, 1, ChunkLifecycle::Loaded);
        // (1,1) 续：失败恢复边⑨单独走（R20.10b）——Loading 直接回 Absent，随后重走 ①②③
        //    （失败后重请求 = 新链路，六态图内自洽闭环）：
        walkOk = walkOk && step(1, 1, ChunkLifecycle::Evicting)
            && step(1, 1, ChunkLifecycle::Absent)
            && step(1, 1, ChunkLifecycle::Loading)
            && step(1, 1, ChunkLifecycle::Absent) // ⑨ generateFailed（失败恢复）
            && step(1, 1, ChunkLifecycle::Loading)
            && step(1, 1, ChunkLifecycle::Generated)
            && step(1, 1, ChunkLifecycle::Loaded);
        ok = ok && walkOk;
        if (!walkOk) diag += QStringLiteral("[walk] ");

        // ⑤ 守卫式 setter 六态 × 6 目标逐对核（每次探测前经合法路径**重布点**到目标态——
        //   首版腿在合法转移被接受后直接续扫：态已移动，后续「非法」比较用陈旧前置态 = 假红
        //   形态登记；重布点让每组 (state→target) 独立成立）。六 chunk 分驻，互不串扰：
        //   从 Loaded（原生稳态）出发的合法路径——回程各态出路汇于 Loaded（Absent/Loading 经
        //   Generated 晋升，Evicting 经取消，Active 经去激活）。
        const auto resetLoaded = [&](int cx, int cz) {
            switch (mgr.lifecycleAt(cx, cz)) {
            case ChunkLifecycle::Loaded: return true;
            case ChunkLifecycle::Active:
            case ChunkLifecycle::Evicting:
            case ChunkLifecycle::Generated:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Loaded);
            case ChunkLifecycle::Loading:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Generated)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Loaded);
            case ChunkLifecycle::Absent:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Loading)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Generated)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Loaded);
            case ChunkLifecycle::Count: break;
            }
            return false;
        };
        const auto positionTo = [&](int cx, int cz, ChunkLifecycle target) {
            if (!resetLoaded(cx, cz))
                return false;
            switch (target) {
            case ChunkLifecycle::Loaded: return true;
            case ChunkLifecycle::Active:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Active);
            case ChunkLifecycle::Evicting:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Evicting);
            case ChunkLifecycle::Absent:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Evicting)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Absent);
            case ChunkLifecycle::Loading:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Evicting)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Absent)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Loading);
            case ChunkLifecycle::Generated:
                return mgr.setLifecycle(cx, cz, ChunkLifecycle::Evicting)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Absent)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Loading)
                    && mgr.setLifecycle(cx, cz, ChunkLifecycle::Generated);
            case ChunkLifecycle::Count: break;
            }
            return false;
        };
        const struct { int cx, cz; ChunkLifecycle st; } sites[] = {
            { 0, 0, ChunkLifecycle::Loaded },
            { 1, 0, ChunkLifecycle::Active },
            { 2, 0, ChunkLifecycle::Evicting },
            { 0, 1, ChunkLifecycle::Absent },
            { 2, 1, ChunkLifecycle::Loading },
            { 0, 2, ChunkLifecycle::Generated },
        };
        bool guardOk = true;
        for (const auto &s : sites) {
            for (int ti = 0; ti < int(ChunkLifecycle::Count); ++ti) {
                const ChunkLifecycle t = ChunkLifecycle(ti);
                if (!positionTo(s.cx, s.cz, s.st)) { // 布点自证（合法路径走不到 = 图自相矛盾）
                    guardOk = false;
                    diag += QStringLiteral("[position (%1,%2) -> %3 failed from %4] ")
                                .arg(s.cx).arg(s.cz).arg(chunkLifecycleName(s.st))
                                .arg(chunkLifecycleName(mgr.lifecycleAt(s.cx, s.cz)));
                    continue;
                }
                const bool want = edgeLegal(s.st, t);
                const bool got = mgr.setLifecycle(s.cx, s.cz, t);
                if (got != want) { // 接受面恰 = 表（含自转移恒拒）
                    guardOk = false;
                    diag += QStringLiteral("[guard (%1,%2) %3->%4 got=%5 want=%6] ")
                                .arg(s.cx).arg(s.cz).arg(chunkLifecycleName(s.st))
                                .arg(chunkLifecycleName(t)).arg(got).arg(want);
                }
                if (!want && mgr.lifecycleAt(s.cx, s.cz) != s.st) { // 拒绝不改态
                    guardOk = false;
                    diag += QStringLiteral("[mutate (%1,%2) %3->%4 changed] ")
                                .arg(s.cx).arg(s.cz).arg(chunkLifecycleName(s.st))
                                .arg(chunkLifecycleName(t));
                }
            }
        }
        ok = ok && guardOk;

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010a six-state graph authority: six states, an"
                             " independently encoded 9-edge table matches the transition"
                             " authority over all 36 pairs (27 illegal incl. self), a bare"
                             " 3x3 grid starts resident-Loaded with OOB=Absent/write-"
                             "reject, the full legal walk traverses all eight original"
                             " edges and all 6 states plus the cancel edge and the R20.10b"
                             " failure-recovery edge Loading->Absent, and the guarded"
                             " setter in each state accepts exactly the documented targets"
                             " rejecting the rest without mutation"
                          << (ok ? QString() : diag);
    });

    // ── r2010b：固定世界默认稳态（验收 1「当前固定 10×10 世界仍可运行」零变化实证）─────────
    runLeg(QStringLiteral("r2010b fixed-world default steady state (R20.10 acceptance 'the"
        " current fixed world still runs' - zero behavior change): a fresh 48x48x96 s82"
        " world puts every one of its 3x3 chunks in the resident-Loaded lifecycle (bit-"
        " equivalent to the pre-R20.10 always-resident world) right after the regenerate"
        " chain, the WorldFacade existence gate answers true for all nine chunks while the"
        " dirty gate stays a pass-through of the raw chunk dirty flag, a place-then-break"
        " edit round trip returns true both ways, leaves the full id+state grid bit-"
        " identical to the pre-edit snapshot, fires the worldChanged signal as before and"
        " leaves the lifecycle table untouched (all Loaded) - the player path, QML and"
        " render behavior are observably unchanged in the default steady state"), [&]() {
        bool ok = true;
        QString diag;

        World wB;
        initTwin(wB); // 三 setter 各 regenerate 一次——末态即 recreate 链后的稳态

        // ① 3×3 全 Loaded（默认稳态 = 常驻已加载）：
        bool lifeOk = wB.chunksX() == 3 && wB.chunksZ() == 3;
        for (int cz = 0; cz < wB.chunksZ() && lifeOk; ++cz)
            for (int cx = 0; cx < wB.chunksX() && lifeOk; ++cx)
                if (wB.chunks().lifecycleAt(cx, cz) != ChunkLifecycle::Loaded) {
                    lifeOk = false;
                    diag += QStringLiteral("[life (%1,%2)=%3] ")
                                .arg(cx).arg(cz)
                                .arg(chunkLifecycleName(wB.chunks().lifecycleAt(cx, cz)));
                }
        ok = ok && lifeOk;

        // ② Facade 存在门 3×3 全 true + 脏门 = 裸脏标记 pass-through（稳态下逐位同 R20.08）：
        WorldFacade f(wB);
        bool gateOk = true;
        for (int cz = 0; cz < wB.chunksZ() && gateOk; ++cz)
            for (int cx = 0; cx < wB.chunksX() && gateOk; ++cx) {
                const Chunk *c = wB.chunks().chunk(cx, cz);
                const bool wantDirty = c && c->dirty();
                const bool wantFluid = c && c->fluidOnlyDirty();
                if (!f.chunkExistsAt(cx, cz) || f.chunkDirtyAt(cx, cz) != wantDirty
                    || f.chunkFluidOnlyDirtyAt(cx, cz) != wantFluid) {
                    gateOk = false;
                    diag += QStringLiteral("[gate (%1,%2) exists=%3 dirty=%4/%5 fluid=%6/%7] ")
                                .arg(cx).arg(cz).arg(f.chunkExistsAt(cx, cz))
                                .arg(f.chunkDirtyAt(cx, cz)).arg(wantDirty)
                                .arg(f.chunkFluidOnlyDirtyAt(cx, cz)).arg(wantFluid);
                }
            }
        ok = ok && gateOk;
        if (!gateOk) diag += QStringLiteral("[gates] ");

        // ③ 编辑破/放往返零变化：全栅格快照 → 放 Stone（列顶 +1）→ 破回 Air → 栅格逐位恒等
        //    + worldChanged 照常 + 态表不动：
        const QPair<int, int> p = placeCol(wB, 20, 20);
        const int h = wB.heightAt(p.first, p.second);
        const bool siteOk = h >= 0 && p.first >= 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[site] ");
        if (siteOk) {
            const QVector<quint8> before = snapGrid(wB);
            int changed = 0;
            QObject::connect(&wB, &World::worldChanged, &wB, [&]() { ++changed; });
            const bool placed = wB.setBlock(p.first, h + 1, p.second, quint8(BR::Stone));
            const bool broke = wB.setBlock(p.first, h + 1, p.second, quint8(BR::Air));
            const bool editOk = placed && broke && changed >= 2;
            ok = ok && editOk;
            if (!editOk)
                diag += QStringLiteral("[edit placed=%1 broke=%2 changed=%3] ")
                            .arg(placed).arg(broke).arg(changed);

            const QVector<quint8> after = snapGrid(wB);
            const bool gridOk = before == after;
            ok = ok && gridOk;
            if (!gridOk) diag += QStringLiteral("[grid-drift] ");

            bool steady2Ok = true;
            for (int cz = 0; cz < wB.chunksZ() && steady2Ok; ++cz)
                for (int cx = 0; cx < wB.chunksX() && steady2Ok; ++cx)
                    steady2Ok = steady2Ok
                        && wB.chunks().lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
            ok = ok && steady2Ok;
            if (!steady2Ok) diag += QStringLiteral("[life-drift] ");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010b fixed-world default steady state: every chunk of a"
                             " fresh world sits in resident-Loaded (bit-equivalent to the"
                             " always-resident pre-R20.10 world), facade gates answer"
                             " true/dirty pass-through for all nine chunks, a place-break"
                             " round trip leaves the grid bit-identical with signals and"
                             " the lifecycle table unchanged - player path observably"
                             " unchanged"
                          << (ok ? QString() : diag);
    });

    // ── r2010c：卸载 → 三门 false → mesher 门跳过 → 重载 → 恢复（验收 2 + Facade 联动）─────
    runLeg(QStringLiteral("r2010c unload/reload end-to-end through the facade gates (R20.10"
        " acceptance 'a chunk can be unloaded and reloaded'): in a fresh world chunk (2,2)"
        " builds a mesh baseline (stone then tall grass grows the vertex count), the two-"
        " step unload Loaded->Evicting->Absent flips all three facade gates off (existence,"
        " dirty, fluid-only dirty all false while the neighbor chunk stays queryable), an"
        " in-chunk edit during the unload window lands in memory (raw routing is ungated -"
        " the no-drop minimal interpretation) but leaves the watched vertex count unchanged"
        " (the mesher gate skips the non-queryable chunk), the three-step reload"
        " Absent->Loading->Generated->Loaded restores the gates, the full id+state grid is"
        " bit-identical to the end-of-window snapshot (the pending edit survives), and the"
        " gate is live again (removing the tall grass rebuilds and shrinks the vertex"
        " count)"), [&]() {
        bool ok = true;
        QString diag;

        World wC;
        initTwin(wC);
        WorldFacade f(wC);

        // 被观测 chunk (2,2)（角 chunk，结构性选址：40/16=2 双轴同腔；lx=8/9 离边 ≥7）双柱：
        const auto findAir = [](World &w, int x, int z) {
            for (int y = 40; y < w.height() - 2; ++y)
                if (w.blockAt(x, y, z) == 0 && w.blockAt(x, y + 1, z) == 0)
                    return y;
            return -1;
        };
        const int ye40 = findAir(wC, 40, 40);
        const int ye41 = findAir(wC, 41, 41);
        const bool siteOk = ye40 > 0 && ye41 > 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[site ye40=%1 ye41=%2] ").arg(ye40).arg(ye41);

        ChunkGeometry geo;
        geo.setWorld(&wC);
        geo.setCx(2);
        geo.setCz(2);

        if (siteOk) {
            // ① 基线：放 Stone（同步首建）→ 放 TallGrass（cross 顶点差分）：
            wC.setBlock(40, ye40, 40, quint8(BR::Stone), 0);
            const int v0 = geo.vertexCount();
            wC.setBlock(40, ye40 + 1, 40, quint8(BR::TallGrass), 0);
            const int v1 = geo.vertexCount();
            const bool baseOk = v0 > 0 && v1 > v0;
            ok = ok && baseOk;
            if (!baseOk) diag += QStringLiteral("[base v0=%1 v1=%2] ").arg(v0).arg(v1);

            // ② 卸载（Loaded→Evicting→Absent 两步；中间态自证）：
            const bool ev = wC.setChunkLifecycle(2, 2, ChunkLifecycle::Evicting);
            const bool midEv = wC.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Evicting;
            const bool ab = wC.setChunkLifecycle(2, 2, ChunkLifecycle::Absent);
            const bool unloadOk = ev && midEv && ab
                && wC.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent;
            ok = ok && unloadOk;
            if (!unloadOk)
                diag += QStringLiteral("[unload ev=%1 mid=%2 ab=%3 now=%4] ")
                            .arg(ev).arg(midEv).arg(ab)
                            .arg(chunkLifecycleName(wC.chunks().lifecycleAt(2, 2)));

            // ③ 三门恒 false + 邻 chunk 不受扰：
            const bool gatesOff = !f.chunkExistsAt(2, 2) && !f.chunkDirtyAt(2, 2)
                && !f.chunkFluidOnlyDirtyAt(2, 2) && f.chunkExistsAt(1, 2);
            ok = ok && gatesOff;
            if (!gatesOff)
                diag += QStringLiteral("[gatesOff e=%1 d=%2 fl=%3 nb=%4] ")
                            .arg(f.chunkExistsAt(2, 2)).arg(f.chunkDirtyAt(2, 2))
                            .arg(f.chunkFluidOnlyDirtyAt(2, 2)).arg(f.chunkExistsAt(1, 2));

            // ④ 卸载窗内编辑（裸路由不受态门控——不落盘驱逐选型）+ mesher 门跳过：
            const QVector<quint8> preWindow = snapGrid(wC);
            const bool landed = wC.setBlock(41, ye41, 41, quint8(BR::Stone), 0);
            const bool kept = wC.blockAt(41, ye41, 41) == quint8(BR::Stone)
                && wC.blockAt(40, ye40, 40) == quint8(BR::Stone);
            const int v2 = geo.vertexCount(); // 门跳过：被观测 chunk 不可查询 → 顶点不动
            const bool skipOk = landed && kept && v2 == v1;
            ok = ok && skipOk;
            if (!skipOk)
                diag += QStringLiteral("[skip landed=%1 kept=%2 v1=%3 v2=%4] ")
                            .arg(landed).arg(kept).arg(v1).arg(v2);
            const QVector<quint8> endWindow = snapGrid(wC); // 重载逐位恒等的基准（含窗内编辑）

            // ⑤ 重载（Absent→Loading→Generated→Loaded 三步；中间态逐步自证）：
            const bool l1 = wC.setChunkLifecycle(2, 2, ChunkLifecycle::Loading);
            const bool m1 = wC.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loading;
            const bool l2 = wC.setChunkLifecycle(2, 2, ChunkLifecycle::Generated);
            const bool m2 = wC.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Generated;
            const bool l3 = wC.setChunkLifecycle(2, 2, ChunkLifecycle::Loaded);
            const bool reloadOk = l1 && m1 && l2 && m2 && l3
                && wC.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Loaded;
            ok = ok && reloadOk;
            if (!reloadOk)
                diag += QStringLiteral("[reload %1%2%3%4%5%6] ")
                            .arg(l1).arg(m1).arg(l2).arg(m2).arg(l3)
                            .arg(chunkLifecycleName(wC.chunks().lifecycleAt(2, 2)));

            // ⑥ 门恢复 + 栅格逐位恒等（窗内编辑保留）。注意：窗内编辑的脏标记已被 World::
            //    setBlock 的 post-emit clearAllDirty 清掉（t155g 既有行为——mesher 门跳过重建
            //    也照清全表，本单不改）→ 重载后 chunkDirtyAt 恒 false 是**既有语义**，此处不
            //    断言其真（首版腿在此假红，登记）；挂起编辑的可见性由 ⑦ 的重建差分承担。
            const bool gatesOn = f.chunkExistsAt(2, 2)
                && wC.blockAt(41, ye41, 41) == quint8(BR::Stone)
                && wC.blockAt(40, ye40, 40) == quint8(BR::Stone);
            const QVector<quint8> afterReload = snapGrid(wC);
            const bool gridOk = afterReload == endWindow;
            ok = ok && gatesOn && gridOk;
            if (!gatesOn) diag += QStringLiteral("[gatesOn e=%1 kept41=%2 kept40=%3] ")
                                    .arg(f.chunkExistsAt(2, 2))
                                    .arg(wC.blockAt(41, ye41, 41) == quint8(BR::Stone))
                                    .arg(wC.blockAt(40, ye40, 40) == quint8(BR::Stone));
            if (!gridOk) diag += QStringLiteral("[grid-drift] ");

            // ⑦ 门复 live：摘 TallGrass → 重建触发（存在门+脏门双真）→ 顶点数**移动**（窗内
            //    Stone41 一并进重建；方向取决于挂起编辑净量——首版断言 v3<v2 假红登记）：
            wC.setBlock(40, ye40 + 1, 40, quint8(BR::Air), 0);
            const int v3 = geo.vertexCount();
            const bool liveOk = v3 != v2;
            ok = ok && liveOk;
            if (!liveOk) diag += QStringLiteral("[live v2=%1 v3=%2] ").arg(v2).arg(v3);

            Q_UNUSED(preWindow);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010c unload/reload end-to-end: two-step unload flips all"
                             " three facade gates off (neighbor unaffected), an in-window"
                             " edit lands in memory but the watched vertex count stays put"
                             " (mesher gate skips), three-step reload restores the gates"
                             " and the grid bit-identical with the pending edit, and the"
                             " gate rebuilds again afterwards"
                          << (ok ? QString() : diag);
    });

    // ── r2010d：源码钉（QML 零生命周期访问面）+ worldstore 往返（验收 3+4；豁免域不破坏）────
    runLeg(QStringLiteral("r2010d structure pins + worldstore round-trip with an unloaded"
        " chunk (R20.10 acceptance 'no unsaved data silently lost' + 'QML no longer decides"
        " chunk lifecycle'): comment-stripped source pins hold the lifecycle wiring (side"
        " table and transition guard in the chunk manager, the six-state type authority,"
        " all three facade gates consulting the lifecycle predicate, the C++-only world"
        " forwarder) with reverse probes proving no Q_INVOKABLE or Q_PROPERTY ever touches"
        " the lifecycle surface and Main.qml never mentions ChunkLifecycle; behaviorally a"
        " save-unload-save-load cycle keeps every voxel: saveAll lands the full world,"
        " chunk (2,2) is then unloaded (Evicting->Absent) and a second saveAll still"
        " serializes all nine chunks (the store exemption domain reads chunk blobs"
        " directly, blind to lifecycle state), a fresh world loads the store and finishes"
        " bit-identical to the live world (both edits intact), and the store left the"
        " lifecycle table exactly as it was"), [&]() {
        bool ok = true;
        QString diag;

        // ① 源码钉根（exe 相对 src/——r2008c 同款解析）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };

        // ② 正面钉：侧表 / 守卫 / 六态权威 / 门谓词 / C++ forwarder：
        const QStringList missCm = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.h"), {
                SrcPin("r2010 lifecycle side table in the chunk manager",
                    "std::vector<ChunkLifecycle> m_lifecycle", 1),
                SrcPin("r2010 guarded transition entry declared", "bool setLifecycle(", 1),
                SrcPin("r2010 lifecycle query declared", "ChunkLifecycle lifecycleAt(", 1),
            });
        const QStringList missCmc = pinSet(
            srcRoot + QStringLiteral("/World/chunkmanager.cpp"), {
                SrcPin("r2010 transition guard wired at the single write point",
                    "chunkLifecycleTransitionLegal(", 1),
                SrcPin("r2010 recreate seeds the default resident-Loaded steady state",
                    "ChunkLifecycle::Loaded", 1),
                SrcPin("r2010 side table exercised (assign + read + guard + write)",
                    "m_lifecycle", 3),
            });
        const QStringList missCl = pinSet(
            srcRoot + QStringLiteral("/World/chunklifecycle.h"), {
                SrcPin("r2010 six-state type authority", "enum class ChunkLifecycle", 1),
                SrcPin("r2010 transition authority defined", "chunkLifecycleTransitionLegal", 1),
                SrcPin("r2010 gate predicate authority defined", "chunkLifecycleQueryable", 1),
            });
        const QStringList missWf = pinSet(
            srcRoot + QStringLiteral("/World/worldfacade.h"), {
                SrcPin("r2010 all three mesher gates consult the lifecycle",
                    "chunkLifecycleQueryable(", 3),
            });
        const QStringList missWh = pinSet(
            srcRoot + QStringLiteral("/World/world.h"), {
                SrcPin("r2010 C++-only lifecycle forwarder on World", "setChunkLifecycle", 1),
            });
        for (const QString &m : missCm + missCmc + missCl + missWf + missWh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 阴性反探（minCount=1 空转钉恒不红——反探惯用法）：生命周期面零 QML 访问
        //    （Q_INVOKABLE/Q_PROPERTY 不沾 chunklifecycle/chunkmanager；World forwarder 非
        //    Q_INVOKABLE；Main.qml 零 ChunkLifecycle 提及——QML 零迁移主线不变量）：
        const bool qmlNegOk
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/chunklifecycle.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkmanager.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunklifecycle.h"), "Q_PROPERTY")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/chunkmanager.h"), "Q_PROPERTY")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"),
                   "Q_INVOKABLE bool setChunkLifecycle")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "hunkLifecycle");
        ok = ok && qmlNegOk;
        if (!qmlNegOk) diag += QStringLiteral("[qml-surface] ");

        // ④ worldstore 往返（存①→卸载→存②→回读）：豁免域直读 chunk blob 不受态影响 →
        //    「没有未保存数据被静默丢失」行为级实证：
        World wS1;
        initTwin(wS1);
        const QPair<int, int> pA = placeCol(wS1, 4, 4);   // chunk (0,0)
        const QPair<int, int> pB = placeCol(wS1, 40, 40); // chunk (2,2)
        const int hA = wS1.heightAt(pA.first, pA.second);
        const int hB = wS1.heightAt(pB.first, pB.second);
        const bool sitesOk = hA >= 0 && hB >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");
        if (sitesOk) {
            wS1.setBlock(pA.first, hA + 1, pA.second, quint8(BR::Stone), 0);
            wS1.setBlock(pB.first, hB + 1, pB.second, quint8(BR::Stone), 0);

            WorldStore store;
            store.setWorld(&wS1);
            const QString db = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_r2010_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(db);

            // 存①（基线全量）：
            const bool save1 = store.openWorld(db)
                && store.saveAll(QStringLiteral("r2010rig"));
            store.closeWorld();
            // 卸载 (2,2)：
            const bool ev = wS1.setChunkLifecycle(2, 2, ChunkLifecycle::Evicting);
            const bool ab = wS1.setChunkLifecycle(2, 2, ChunkLifecycle::Absent);
            // 存②（(2,2) 处于 Absent 态——store 豁免域照常序列化全部 chunk）：
            const bool save2 = store.openWorld(db)
                && store.saveAll(QStringLiteral("r2010rig"));
            store.closeWorld();
            const bool saveOk = save1 && ev && ab && save2;
            ok = ok && saveOk;
            if (!saveOk)
                diag += QStringLiteral("[save s1=%1 ev=%2 ab=%3 s2=%4] ")
                            .arg(save1).arg(ev).arg(ab).arg(save2);

            // 回读：新世界 beginLoad + loadChunks + finishLoad。**store.setWorld(&wS2) 必须在
            //   loadChunks 前**——loadChunks 写入的是 store 持有的 m_world（首版腿漏改绑 →
            //   blob 回灌进 wS1 自身、wS2 恒零填充 = 假红，登记）：
            World wS2;
            wS2.setWidth(48);
            wS2.setDepth(48);
            wS2.setHeight(96);
            wS2.beginLoad(82); // 零填充分区网格（loadChunks 前置，同 Main.qml 载入流）
            store.setWorld(&wS2);
            const bool opened = store.openWorld(db);
            const int n = opened ? store.loadChunks() : -1;
            store.closeWorld();
            wS2.finishLoad();
            QFile::remove(db);

            QString why;
            const bool roundOk = opened && n == 9 && gridEqual(wS1, wS2, why);
            ok = ok && roundOk;
            if (!roundOk)
                diag += QStringLiteral("[round opened=%1 n=%2 %3] ").arg(opened).arg(n).arg(why);

            // store 未动生命周期态（豁免域隔离的结构面实证）：
            const bool lifeUntouched
                = wS1.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent
                && wS1.chunks().lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
            ok = ok && lifeUntouched;
            if (!lifeUntouched)
                diag += QStringLiteral("[life-touched 22=%1 00=%2] ")
                            .arg(chunkLifecycleName(wS1.chunks().lifecycleAt(2, 2)))
                            .arg(chunkLifecycleName(wS1.chunks().lifecycleAt(0, 0)));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010d structure pins + store round-trip: source pins hold"
                             " the lifecycle wiring with reverse probes proving zero QML"
                             " lifecycle surface (no Q_INVOKABLE/Q_PROPERTY, Main.qml"
                             " silent), a save-unload-save-load cycle keeps every voxel"
                             " bit-identical (the exemption-domain store serializes all"
                             " nine chunks regardless of lifecycle state) and leaves the"
                             " lifecycle table untouched"
                          << (ok ? QString() : diag);
    });

    // ── r2010ba：R20.10b 失败恢复边⑨——同步泵投递面转移 + 重请求可行 + 丢弃面不转移 + 失败
    //    注入零变化（裸 3×3 网格 + 裸 scheduler，零世界零 tick 零 RNG——确定性终态断言）────
    runLeg(QStringLiteral("r2010ba sync-pump failure-recovery edge (R20.10b): a failing worker"
        " on an Absent chunk (routed via legal Evicting edges) is observed in Loading at"
        " execution (edge 1), delivers its Failed outcome verbatim (code 42 with message)"
        " and recovers to Absent through the new failure edge (edge 9) - the chunk is"
        " immediately re-requestable and a fresh job (a new request id) walks edges 1-2"
        " again to Generated; a canceled-before-run request and a stale (epoch-bumped)"
        " request both leave their chunks untouched at Absent (drop faces never execute,"
        " never deliver, never transition); a failure injected on a default-steady-Loaded"
        " chunk still delivers its error outcome but leaves the nine-entry lifecycle table"
        " bit-untouched (edges 1 and 9 both guard-rejected - fixed-world zero change holds"
        " under failure injection); the failure edge is pinned in the transition authority"
        " and the store stays lifecycle-blind"), [&]() {
        bool ok = true;
        QString diag;

        // 同步失败注入 worker（轨迹 + 执行时刻生命周期观察 + 失败集；section15 TraceWorker 同族）：
        class SyncFailWorker : public GenerationWorker
        {
        public:
            QVector<quint64> execKeys;  // 执行序 packed key（轨迹）
            QVector<int> execLifecycle; // 执行时刻 lifecycleAt 观察（-1 = 无挂点）
            QSet<quint64> failKeys;     // 命中 → Result::fail(42, "synthetic worker failure")
            const ChunkManager *observe = nullptr;

            Result<void> execute(const GenerationRequest &req) override
            {
                execKeys.append(req.key.packed());
                execLifecycle.append(
                    observe ? int(observe->lifecycleAt(req.key.cx, req.key.cz)) : -1);
                if (failKeys.contains(req.key.packed()))
                    return Result<void>::fail(42, "synthetic worker failure");
                return Result<void>::ok();
            }
        };

        // ① 失败注入 → outcome 实投 → 边⑨：
        ChunkManager mgr(48, 48, 32); // 3×3（默认稳态全 Loaded 自证）
        bool steadyOk = mgr.chunksX() == 3 && mgr.chunksZ() == 3;
        for (int cz = 0; cz < 3 && steadyOk; ++cz)
            for (int cx = 0; cx < 3 && steadyOk; ++cx)
                steadyOk = steadyOk && mgr.lifecycleAt(cx, cz) == ChunkLifecycle::Loaded;
        ok = ok && steadyOk;
        if (!steadyOk) diag += QStringLiteral("[steady] ");

        GenerationScheduler sched(&mgr);
        SyncFailWorker w;
        w.observe = &mgr;
        sched.setWorker(&w);

        // ① (2,2) 经合法边⑥⑦布到 Absent → 失败 submit → pump：执行时实测 Loading（边①先于
        //    失败）→ outcome（Error 42 原样穿透）→ 终态 Absent（边⑨ 失败恢复）：
        const bool route22 = mgr.setLifecycle(2, 2, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(2, 2, ChunkLifecycle::Absent);
        w.failKeys.insert(pk(2, 2));
        const auto r1 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 });
        sched.pump();
        GenerationJobOutcome o1;
        const bool failOk = route22 && r1.isOk() && w.execKeys.size() == 1
            && w.execKeys[0] == pk(2, 2)
            && w.execLifecycle[0] == int(ChunkLifecycle::Loading) // 边①先于失败
            && sched.outcomeCount() == 1 && sched.takeOutcome(o1)
            && o1.requestId == r1.value() && isError(o1.error) && o1.error.code == 42
            && o1.error.message
            && qstrcmp(o1.error.message, "synthetic worker failure") == 0
            && mgr.lifecycleAt(2, 2) == ChunkLifecycle::Absent; // 边⑨ 失败恢复
        ok = ok && failOk;
        if (!failOk)
            diag += QStringLiteral("[fail exec=%1 e1=%2 out=%3 life=%4] ")
                        .arg(w.execKeys.size())
                        .arg(w.execLifecycle.value(0, -1))
                        .arg(sched.outcomeCount())
                        .arg(int(mgr.lifecycleAt(2, 2)));

        // ② 重请求可行：清失败集 → 新 submit（新 requestId = 新 job）→ pump → 边①②重走到
        //    Generated（失败恢复语义只到「回 Absent 可再请求」，自动重试策略不在面内）：
        w.failKeys.clear();
        const auto r2 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 2, 2 });
        sched.pump();
        GenerationJobOutcome o2;
        const bool retryOk = r2.isOk() && r2.value() != r1.value()
            && w.execKeys.size() == 2 && w.execKeys[1] == pk(2, 2)
            && sched.outcomeCount() == 1 && sched.takeOutcome(o2)
            && o2.requestId == r2.value() && !isError(o2.error)
            && mgr.lifecycleAt(2, 2) == ChunkLifecycle::Generated;
        ok = ok && retryOk;
        if (!retryOk)
            diag += QStringLiteral("[retry id=%1 exec=%2 out=%3 life=%4] ")
                        .arg(r2.value()).arg(w.execKeys.size())
                        .arg(sched.outcomeCount()).arg(int(mgr.lifecycleAt(2, 2)));

        // ③ 丢弃面不转移：取消（执行前）与过期（epoch bump 后）都零执行零投递，chunk 恒
        //    Absent（六态表无取消/过期回退——「不推进」语义与边⑨投递面判据正交）：
        const bool route01 = mgr.setLifecycle(0, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(0, 1, ChunkLifecycle::Absent);
        const auto r3 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 1 });
        const bool c3 = sched.cancel(r3.value());
        const bool route12 = mgr.setLifecycle(1, 2, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(1, 2, ChunkLifecycle::Absent);
        const auto r4 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 2 });
        const int execBefore = w.execKeys.size();
        sched.setWorldEpoch(1); // r4 提交于 epoch 0 → 泵时按过期丢弃
        sched.pump();
        GenerationJobOutcome odrop;
        bool dropOk = route01 && r3.isOk() && c3 && route12 && r4.isOk()
            && w.execKeys.size() == execBefore // 两 job 都零执行
            && sched.outcomeCount() == 0 && !sched.takeOutcome(odrop)
            && mgr.lifecycleAt(0, 1) == ChunkLifecycle::Absent // 取消不推进
            && mgr.lifecycleAt(1, 2) == ChunkLifecycle::Absent; // 过期不推进
        sched.setWorldEpoch(0); // 还原 epoch（后续相位在初始 epoch 域断言）
        ok = ok && dropOk;
        if (!dropOk)
            diag += QStringLiteral("[drop exec=%1/%2 out=%3 01=%4 12=%5] ")
                        .arg(w.execKeys.size()).arg(execBefore)
                        .arg(sched.outcomeCount())
                        .arg(int(mgr.lifecycleAt(0, 1)))
                        .arg(int(mgr.lifecycleAt(1, 2)));

        // ④ 失败注入零变化：默认稳态 Loaded chunk 上失败 outcome 照常实投（同步泵执行 ⟹ 有活
        //    别名 ⟹ 投递），但边①/边⑨双双被守卫拒 → 生命周期表 9 格逐位不动（固定世界零变化
        //    在失败注入下保持——「零变化」不依赖「无失败」假设）：
        std::array<ChunkLifecycle, 9> lifeBefore {};
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                lifeBefore[size_t(cx + 3 * cz)] = mgr.lifecycleAt(cx, cz);
        w.failKeys.insert(pk(0, 0));
        const auto r5 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 0 });
        sched.pump();
        GenerationJobOutcome o5;
        bool fixedOk = r5.isOk() && w.execKeys.size() == execBefore + 1
            && sched.outcomeCount() == 1 && sched.takeOutcome(o5)
            && o5.requestId == r5.value() && isError(o5.error) && o5.error.code == 42
            && mgr.lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
        for (int cz = 0; cz < 3 && fixedOk; ++cz)
            for (int cx = 0; cx < 3 && fixedOk; ++cx)
                fixedOk = fixedOk
                    && mgr.lifecycleAt(cx, cz) == lifeBefore[size_t(cx + 3 * cz)];
        ok = ok && fixedOk;
        if (!fixedOk)
            diag += QStringLiteral("[fixed exec=%1 life00=%2] ")
                        .arg(w.execKeys.size()).arg(int(mgr.lifecycleAt(0, 0)));
        w.failKeys.clear();

        // ⑤ 源码钉：转移权威含⑨（剥注释后锚真实分派语句）；worldstore 零生命周期接触
        //   （豁免域隔离——worldstore 头/体零 Lifecycle/setLifecycle 记号）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QStringList missCl = pinSet(
            srcRoot + QStringLiteral("/World/chunklifecycle.h"), {
                SrcPin("r2010b failure-recovery edge in the transition authority",
                    "|| to == ChunkLifecycle::Absent", 1),
            });
        for (const QString &m : missCl) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool storeNegOk
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.h"), "Lifecycle")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/worldstore.cpp"), "setLifecycle");
        ok = ok && storeNegOk;
        if (!storeNegOk) diag += QStringLiteral("[store-neg] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010ba sync-pump failure-recovery edge: a failing worker"
                             " is observed in Loading at execution, delivers its Failed"
                             " outcome verbatim and recovers to Absent through edge 9,"
                             " then an immediate re-request (new id) walks edges 1-2 to"
                             " Generated; canceled and stale drop faces never execute,"
                             " deliver or transition; a failure injected on a default-"
                             " Loaded chunk delivers its outcome but leaves the nine-entry"
                             " table bit-untouched; the edge is pinned in the authority and"
                             " the store stays lifecycle-blind"
                          << (ok ? QString() : diag);
    });

    // ── r2010bb：R20.10b 失败恢复边⑨——pumpAsync 收割投递面转移（R20.12 缝）+ 交接后过期
    //    失败停 Loading（丢弃面不转移）+ 失败注入零变化 + worker 侧零生命周期驱动（反探）──
    //    BackgroundGenerationWorker 纯函数恒成功面 → 失败注入用脚本化 isAsynchronous() 测试
    //    替身走同一异步协议（r2011c 可替换性同门——零线程零时序，完成记录按脚本 FIFO 交付）。
    runLeg(QStringLiteral("r2010bb async-harvest failure-recovery edge (R20.10b, R20.12 seam):"
        " a scripted asynchronous worker takes an Absent chunk's job at handout (edge 1"
        " fires - the chunk reads Loading deterministically before any harvest), a scripted"
        " failure completion delivers its Failed outcome on the caller thread and the chunk"
        " recovers to Absent through the failure edge (edge 9), an immediate re-request"
        " walks edges 1-2 again to Generated; a failure completed AFTER an epoch bump is"
        " dropped at delivery (zero outcomes) and its chunk deliberately stays at Loading"
        " (the drop face never transitions - the same gate as the R20.12 epoch discard); a"
        " delivered failure on a default-steady-Loaded chunk leaves the nine-entry table"
        " bit-untouched (both edges guard-rejected); both scheduler delivery points share"
        " one delivery-face judge (source pins) and the worker side never drives the"
        " lifecycle itself (comment-stripped reverse probe)"), [&]() {
        bool ok = true;
        QString diag;

        // 脚本化异步 worker（R20.12 缝的测试替身；交接记录 + 脚本化完成 FIFO）：
        class ScriptedAsyncWorker : public GenerationWorker
        {
        public:
            QVector<GenerationRequest> handed;     // 交接序（submitAsync 受理记录）
            QVector<quint64> handedJobIds;         // 交接序 jobId（脚本完成记录对账键）
            struct Done
            {
                quint64 jobId = 0;
                Error error{};
            };
            QVector<Done> script; // 完成脚本（takeCompletedAsync FIFO 交付）

            bool isAsynchronous() const override { return true; }
            // 同步 execute 缝：async-only（BackgroundGenerationWorker 同款防御——直调可见 fail）：
            Result<void> execute(const GenerationRequest &req) override
            {
                Q_UNUSED(req);
                return Result<void>::fail(201, "ScriptedAsyncWorker is async-only (use submitAsync)");
            }
            Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
            {
                handed.append(req);
                handedJobIds.append(jobId);
                return Result<void>::ok();
            }
            bool takeCompletedAsync(CompletedGeneration &out) override
            {
                if (script.isEmpty())
                    return false;
                const Done d = script.takeFirst();
                out = CompletedGeneration{ d.jobId, d.error };
                return true;
            }
        };

        ChunkManager mgr(48, 48, 32); // 3×3（默认稳态全 Loaded——r2010ba 同款自证省略，④快照承担）
        GenerationScheduler sched(&mgr);
        ScriptedAsyncWorker w;
        sched.setWorker(&w);

        // ① (1,1) 经合法边⑥⑦布到 Absent → submit → 泵一轮（交接边①：终态 Loading——交接无
        //    条件先于收割，确定性非时序）→ 脚本失败完成 → 泵收割：outcome 实投 + 边⑨回 Absent：
        const bool route11 = mgr.setLifecycle(1, 1, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(1, 1, ChunkLifecycle::Absent);
        const auto r1 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        sched.pump();
        const bool handoutOk = route11 && r1.isOk() && w.handed.size() == 1
            && w.handed[0].key == ChunkKey{ 1, 1 }
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Loading; // 边①交接时已取
        w.script.append({ w.handedJobIds[0], Error{ 42, "synthetic async failure" } });
        sched.pump();
        GenerationJobOutcome o1;
        const bool failOk = handoutOk && sched.outcomeCount() == 1 && sched.takeOutcome(o1)
            && o1.requestId == r1.value() && isError(o1.error) && o1.error.code == 42
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Absent // 边⑨ 失败恢复
            && sched.inFlightJobCount() == 0;
        ok = ok && failOk;
        if (!failOk)
            diag += QStringLiteral("[fail hand=%1 out=%2 life=%3 inf=%4] ")
                        .arg(w.handed.size()).arg(sched.outcomeCount())
                        .arg(int(mgr.lifecycleAt(1, 1))).arg(sched.inFlightJobCount());

        // ② 重请求（新 job 新 id）→ 交接（边① Loading）→ 脚本成功完成 → 收割边② → Generated：
        const auto r2 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 1, 1 });
        sched.pump(); // 交接相
        const bool hand2 = r2.isOk() && r2.value() != r1.value() && w.handed.size() == 2
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Loading;
        w.script.append({ w.handedJobIds[1], Error{} });
        sched.pump(); // 收割相
        GenerationJobOutcome o2;
        const bool retryOk = hand2 && sched.outcomeCount() == 1 && sched.takeOutcome(o2)
            && o2.requestId == r2.value() && !isError(o2.error)
            && mgr.lifecycleAt(1, 1) == ChunkLifecycle::Generated;
        ok = ok && retryOk;
        if (!retryOk)
            diag += QStringLiteral("[retry hand=%1 out=%2 life=%3] ")
                        .arg(w.handed.size()).arg(sched.outcomeCount())
                        .arg(int(mgr.lifecycleAt(1, 1)));

        // ③ 丢弃面不转移（语义保持）：交接后才 bump epoch 的失败完成在收割时被静默丢弃
        //    （零 outcome）→ chunk **停 Loading**（边⑨不取——世界已换代，旧任务产物连同状态
        //    一并作废，与「epoch 丢弃旧任务」同门）：
        const bool route02 = mgr.setLifecycle(0, 2, ChunkLifecycle::Evicting)
            && mgr.setLifecycle(0, 2, ChunkLifecycle::Absent);
        const auto r3 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 2 });
        sched.pump(); // 交接（边① → Loading）
        const bool hand3 = route02 && r3.isOk() && w.handed.size() == 3
            && mgr.lifecycleAt(0, 2) == ChunkLifecycle::Loading;
        sched.setWorldEpoch(7); // 交接后才换代（R20.12 验收③时序）
        w.script.append({ w.handedJobIds[2], Error{ 42, "synthetic async failure" } });
        sched.pump(); // 收割：deliver 过滤丢弃（delivered=0）→ 边⑨不取
        GenerationJobOutcome odrop;
        bool dropOk = hand3 && sched.outcomeCount() == 0 && !sched.takeOutcome(odrop)
            && mgr.lifecycleAt(0, 2) == ChunkLifecycle::Loading // 停 Loading（丢弃面不转移）
            && sched.inFlightJobCount() == 0 && sched.pendingJobCount() == 0;
        ok = ok && dropOk;
        if (!dropOk)
            diag += QStringLiteral("[drop out=%1 life=%2 inf=%3 pj=%4] ")
                        .arg(sched.outcomeCount()).arg(int(mgr.lifecycleAt(0, 2)))
                        .arg(sched.inFlightJobCount()).arg(sched.pendingJobCount());

        // ④ 失败注入零变化（epoch 7 域）：默认稳态 Loaded chunk 的失败 outcome 照常实投（别名
        //    epoch 7 = 当前），边①/边⑨双被守卫拒 → 表 9 格逐位不动：
        std::array<ChunkLifecycle, 9> lifeBefore {};
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                lifeBefore[size_t(cx + 3 * cz)] = mgr.lifecycleAt(cx, cz);
        const auto r4 = sched.submit(GenerationJobKind::Generate, ChunkKey{ 0, 0 });
        sched.pump(); // 交接相（边①被拒：Loaded→Loading 非法 → 停 Loaded）
        const bool hand4 = r4.isOk() && w.handed.size() == 4
            && mgr.lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
        w.script.append({ w.handedJobIds[3], Error{ 42, "synthetic async failure" } });
        sched.pump(); // 收割相：outcome 实投（error）但边⑨被拒
        GenerationJobOutcome o4;
        bool fixedOk = hand4 && sched.outcomeCount() == 1 && sched.takeOutcome(o4)
            && o4.requestId == r4.value() && isError(o4.error)
            && mgr.lifecycleAt(0, 0) == ChunkLifecycle::Loaded;
        for (int cz = 0; cz < 3 && fixedOk; ++cz)
            for (int cx = 0; cx < 3 && fixedOk; ++cx)
                fixedOk = fixedOk
                    && mgr.lifecycleAt(cx, cz) == lifeBefore[size_t(cx + 3 * cz)];
        ok = ok && fixedOk;
        if (!fixedOk)
            diag += QStringLiteral("[fixed out=%1 life00=%2] ")
                        .arg(sched.outcomeCount()).arg(int(mgr.lifecycleAt(0, 0)));

        // ⑤ 源码钉：两路投递点同一判据（单一语义，禁两份转移逻辑）+ worker 侧零生命周期驱动
        //   （反探——backgroundgeneration.h 代码面零 setLifecycle 记号，生命周期归 scheduler）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QStringList missGj = pinSet(
            srcRoot + QStringLiteral("/World/generationjob.h"), {
                SrcPin("r2010b failure edge driven at both delivery points (single judge)",
                    "delivered > 0 && m_chunks", 2),
                SrcPin("r2010b delivery-face judge captured at both sites",
                    "const int delivered = deliver(", 2),
            });
        for (const QString &m : missGj) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool workerNegOk = forbiddenAbsent(
            srcRoot + QStringLiteral("/World/backgroundgeneration.h"), "setLifecycle");
        ok = ok && workerNegOk;
        if (!workerNegOk) diag += QStringLiteral("[worker-neg] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2010bb async-harvest failure-recovery edge: a scripted"
                             " async handout takes edge 1 (Loading), a delivered failure"
                             " completion recovers the chunk to Absent through edge 9 and"
                             " the re-request walks edges 1-2 to Generated, a post-bump"
                             " failure completion is dropped at delivery leaving the chunk"
                             " parked at Loading (drop face never transitions), a delivered"
                             " failure on a default-Loaded chunk leaves the table bit-"
                             "untouched, both delivery points share one judge (pins), and"
                             " the worker side never drives lifecycle"
                          << (ok ? QString() : diag);
    });
}
