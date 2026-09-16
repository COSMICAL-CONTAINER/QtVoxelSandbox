#include "matrix_helpers.h"

#include "generationpolicy.h" // §29.4-P1 被测：GenerationPolicy 流式激活策略层骨架（纯决策值组件）

// §29.4-P1 GenerationPolicy 探针段（4 腿 r2016a-d；filter 词 "r2016"；矩阵 598→602，band 600±2 内；
// r2017 增 r2017d setParams 规整 twin 腿 → 606，见尾注）。
// 置尾先例沿用（接 section19，runAll 末执行）；全段**零世界**（r2011a-c 纯请求模型腿同款——
// 被测面是纯函数决策组件，缝 = 测试内 QMap 合成生命周期表，rig 世界 w 零接触，无 RNG 无 tick
// 无时间源）。任务契约（docs/refactor-plan-2026-09-08.md §29.4「P1 策略层骨架」+ D1/D4/D5 建议列）：
//   「固定世界默认关（零变化不变量延续）」→ r2016a（**本单承重墙**：streamingEnabled=false
//     （含默认构造）时任意玩家位 × 任意缝（全 Absent / 全驻留 / 混合 / null）→ toRequest/
//     toEvict 恒空——任何输入；缺省参数本身就是关）；
//   「Absent-only 决策：哪些 chunk 该请求」→ r2016b（enabled + 手工布置的混合缝表：toRequest
//     恰 = 生成半径内（含边界）Absent 集合；priority 升序 + 同距 (cx,cz) 字典序全确定性；
//     非.Absent 五态[Generated/Loaded/Active/Loading/Evicting]零请求；恰 = gen 半径的边界格
//     含入；第二场景换玩家位钉坐标偏移正确性）；
//   「哪些 chunk 该卸载」+ D4「[render, gen] 环带不驱逐」→ r2016c（半径外驻留格逐个恰入
//     toEvict 一次、距离降序（最远先卸）同距字典序；环带驻留格**不**驱逐（D4 承重断言）；
//     Absent/Loading/Evicting 态零驱逐；渲染半径单独调小（1→0）/抬到与 gen 相等都不改变
//     toEvict——驱逐只看 gen 半径）；
//   「纯决策无头可测」+ 值/结构纪律 → r2016d（同输入两次 decide 逐位恒等 + 双实例恒等 =
//     纯函数无隐状态；gen < render 越界入参被规整后决策与显式 gen==render 逐位一致 +
//     负值/上界归一化字段钉；源码钉——本头禁 QObject 基类 / 禁线程原语 / 禁 World*/
//     ChunkManager* 指针（「禁出」钉走 minCount=1 反探 miss 非空=合规，r2007b/r2011d 先例，
//     pinSet 剥注释防注释提及误伤）+ world.{h,cpp}/gamesession.h/Main.qml 零 GenerationPolicy
//     引用 = P1 零调用点迁移零 QML 触碰的结构性事实）。
// 阴性轮恰红面设计（**先于腿文定稿**，R20.11 收缩纪律；存证 matrix_r2016_neg.log 含还原段）：
//   NEG1 摘默认关短路——generationpolicy.h decide() 入口 `if (!m_params.streamingEnabled)`
//     前置 `false &&`（= disabled 路径也产出决策）→ **恰红 r2016a 单腿**（全 Absent 缝下
//     toRequest 非空 / 全驻留缝下 toEvict 非空）。r2016b/c 全 enabled 不受扰（短路不触 enabled
//     路径）；r2016d 只钉相对恒等（双 decide 恒等/双实例恒等/规整两侧同变仍相等）与结构钉，
//     不钉 disabled 绝对空面 → 不误伤。
//   NEG2 摘驱逐半径守卫——toEvict 收集环 `if (cheb(dx, dz) <= gen) continue;` 前置 `false &&`
//     （= 扫描窗内全部驻留格入 toEvict，不再过滤半径）→ **恰红 r2016c 单腿**（环带/gen 内
//     驻留格现身 toEvict + 精确序失配）。r2016a 短路在 → 恒空不红；r2016b 只钉 toRequest
//     绝对面、零 toEvict 断言（R20.11 纪律：辅助面禁绝对计数交叉感染）→ 不红；r2016d 相对
//     恒等两侧同变仍恒等 → 不红。
// 确定性口径：被测组件纯同步纯函数（无 RNG/无时间源/无 tick/无隐状态）；缝表手工布置
//   （腿选址纪律：结构性布点，勿靠地形扫描碰运气），全部断言静态可预测。
void MatrixRun::section20_generationpolicy()
{
    // ── 段内共享帮手（零世界；缝 = 合成生命周期表）──────────────────────────────────────
    // 缝表键：(cx,cz) → 唯一 qint64（负坐标同等合法）。
    const auto kkey = [](int cx, int cz) { return (qint64(cx) << 32) | quint32(cz); };
    // 合成生命周期表缝：表缺席格默认 Absent（ChunkManager::lifecycleAt 越界 → Absent 同款
    // 语义——「槽位无可用内容」）。
    const auto tableSeam = [&kkey](const QMap<qint64, ChunkLifecycle> &t) {
        return [&t, &kkey](int cx, int cz) -> ChunkLifecycle {
            return t.value(kkey(cx, cz), ChunkLifecycle::Absent);
        };
    };
    // diag 帮手：条目列表紧凑串（腿 diag 带判别信息纪律）。
    const auto reqStr = [](const QVector<GenerationPolicyRequest> &v) {
        QString s;
        for (const auto &r : v)
            s += QStringLiteral("(%1,%2)p%3 ").arg(r.cx).arg(r.cz).arg(r.priority);
        return s;
    };
    const auto evStr = [](const QVector<GenerationPolicyEvict> &v) {
        QString s;
        for (const auto &e : v)
            s += QStringLiteral("(%1,%2) ").arg(e.cx).arg(e.cz);
        return s;
    };

    // ── r2016a：默认关零变化（承重墙）——disabled 任意输入 → 两列表恒空─────────────────────
    runLeg(QStringLiteral("r2016a default-off zero change (section 29.4 P1 load-bearing wall:"
        " 'fixed world stays off by default'): with streamingEnabled=false (including the"
        " default-constructed params object whose every field is the off-by-default shape),"
        " decide() returns two empty lists for every player position and every seam - an"
        " all-Absent seam, an all-Loaded resident seam, a hand-placed mixed seam and a null"
        " seam (fail-safe: no information, no decision) alike; the params default itself is"
        " pinned (streaming disabled, radii at their documented defaults) so the fixed-world"
        " zero-change invariant is structural, not incidental"), [&]() {
        bool ok = true;
        QString diag;

        // ① 默认参数本身就是「关」形态（默认值正面钉）：
        const GenerationPolicyParams def;
        const bool defOk = !def.streamingEnabled && def.generationRadiusChunks == 4
            && def.renderRadiusChunks == 4 && def.scanExtentChunks == 6;
        ok = ok && defOk;
        if (!defOk)
            diag += QStringLiteral("[def en=%1 g=%2 r=%3 s=%4] ")
                        .arg(def.streamingEnabled)
                        .arg(def.generationRadiusChunks)
                        .arg(def.renderRadiusChunks)
                        .arg(def.scanExtentChunks);

        // ② 三个 disabled 策略实例：默认构造 / 默认参数显式 / 非默认半径显式关：
        GenerationPolicy pDefault;
        GenerationPolicy pExplicit{ GenerationPolicyParams{} };
        GenerationPolicy pWeird{ GenerationPolicyParams(false, 2, 0, 9) };

        // ③ 缝族：全 Absent / 全驻留（Loaded）/ 手工混合 / null：
        //   （表必须具名存活——tableSeam 返回体按引用捕获，临 map 悬垂 = UB）
        QMap<qint64, ChunkLifecycle> emptyT;
        const auto allAbsent = tableSeam(emptyT);
        const auto allLoaded = [](int, int) -> ChunkLifecycle { return ChunkLifecycle::Loaded; };
        QMap<qint64, ChunkLifecycle> mixedT;
        mixedT.insert(kkey(0, 0), ChunkLifecycle::Absent);
        mixedT.insert(kkey(1, 0), ChunkLifecycle::Generated);
        mixedT.insert(kkey(-3, 2), ChunkLifecycle::Evicting);
        mixedT.insert(kkey(9, 9), ChunkLifecycle::Active);
        const auto mixed = tableSeam(mixedT);
        const GenerationPolicy::ChunkSeamFn nullSeam{};

        // ④ 3 策略 × 3 缝 × 3 玩家位 全组合：toRequest/toEvict 恒空：
        const GenerationPolicy *policies[3] = { &pDefault, &pExplicit, &pWeird };
        const GenerationPolicy::ChunkSeamFn seams[3] = { allAbsent, allLoaded, mixed };
        const int positions[3][2] = { { 0, 0 }, { 5, -7 }, { -100, 42 } };
        for (int pi = 0; pi < 3 && ok; ++pi) {
            for (int si = 0; si < 3; ++si) {
                for (const auto &pos : positions) {
                    const GenerationPolicyDecision d
                        = policies[pi]->decide(pos[0], pos[1], seams[si]);
                    const bool empty = d.toRequest.isEmpty() && d.toEvict.isEmpty();
                    if (!empty) {
                        ok = false;
                        diag += QStringLiteral("[p%1 s%2 at(%3,%4) req=%5 ev=%6] ")
                                    .arg(pi)
                                    .arg(si)
                                    .arg(pos[0])
                                    .arg(pos[1])
                                    .arg(reqStr(d.toRequest), evStr(d.toEvict));
                    }
                }
            }
        }

        // ⑤ null 缝 fail-safe（enabled 实例同门——无信息零决策）：
        GenerationPolicy pOn{ GenerationPolicyParams(true, 2, 1, 3) };
        const GenerationPolicyDecision dn = pOn.decide(0, 0, nullSeam);
        const bool nullOk = dn.toRequest.isEmpty() && dn.toEvict.isEmpty();
        ok = ok && nullOk;
        if (!nullOk)
            diag += QStringLiteral("[null req=%1 ev=%2] ")
                        .arg(reqStr(dn.toRequest), evStr(dn.toEvict));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2016a default-off zero change: with streamingEnabled=false"
                             " (including default-constructed params) decide() yields two"
                             " empty lists for every position and every seam (all-Absent,"
                             " all-Loaded, mixed, null), and the defaults themselves are the"
                             " off shape - the fixed-world zero-change invariant is structural"
                          << (ok ? QString() : diag);
    });

    // ── r2016b：激活请求面——toRequest 恰 = 生成半径内 Absent 集（含边界），全序确定────────
    runLeg(QStringLiteral("r2016b activation request face (section 29.4 P1 'which chunks to"
        " request' Absent-only): with streaming enabled, gen radius 2 and a hand-placed"
        " mixed lifecycle table around the origin, toRequest is exactly the Absent set"
        " within the closed gen disk - the player cell at priority 0, the ring-1 Absent"
        " cells, and the boundary cells at Chebyshev distance exactly 2 included; priority"
        " is strictly ascending with same-distance ties in (cx,cz) lexicographic order"
        " (fully deterministic output); none of the five non-Absent states resident inside"
        " the disk (Generated/Loaded/Active/Loading/Evicting) is ever requested; Absent"
        " cells beyond the gen radius are not requested; a second scenario re-seats the"
        " player at (3,-2) with gen 1 and pins the position offset (requests computed"
        " relative to the player, not the origin)"), [&]() {
        bool ok = true;
        QString diag;

        // 场景 1：玩家原点，gen=2 render=1 scan=4，5×5 窗全表手工布（五态齐备）：
        QMap<qint64, ChunkLifecycle> t1;
        t1.insert(kkey(-2, -2), ChunkLifecycle::Loaded);
        t1.insert(kkey(-1, -2), ChunkLifecycle::Generated);
        t1.insert(kkey(0, -2), ChunkLifecycle::Active);
        t1.insert(kkey(1, -2), ChunkLifecycle::Evicting);
        t1.insert(kkey(2, -2), ChunkLifecycle::Evicting);
        t1.insert(kkey(-2, -1), ChunkLifecycle::Absent);  // → 请求（d2 边界）
        t1.insert(kkey(-1, -1), ChunkLifecycle::Absent);  // → 请求（d1）
        t1.insert(kkey(0, -1), ChunkLifecycle::Absent);   // → 请求（d1）
        t1.insert(kkey(1, -1), ChunkLifecycle::Generated);
        t1.insert(kkey(2, -1), ChunkLifecycle::Absent);   // → 请求（d2 边界）
        t1.insert(kkey(-2, 0), ChunkLifecycle::Loading);
        t1.insert(kkey(-1, 0), ChunkLifecycle::Absent);   // → 请求（d1）
        t1.insert(kkey(0, 0), ChunkLifecycle::Absent);    // → 请求（d0 玩家格）
        t1.insert(kkey(1, 0), ChunkLifecycle::Generated);
        t1.insert(kkey(2, 0), ChunkLifecycle::Absent);    // → 请求（d2 边界）
        t1.insert(kkey(-2, 1), ChunkLifecycle::Generated);
        t1.insert(kkey(-1, 1), ChunkLifecycle::Loaded);
        t1.insert(kkey(0, 1), ChunkLifecycle::Loaded);
        t1.insert(kkey(1, 1), ChunkLifecycle::Generated);
        t1.insert(kkey(2, 1), ChunkLifecycle::Absent);    // → 请求（d2 边界）
        t1.insert(kkey(-2, 2), ChunkLifecycle::Absent);   // → 请求（d2 边界角）
        t1.insert(kkey(-1, 2), ChunkLifecycle::Generated);
        t1.insert(kkey(0, 2), ChunkLifecycle::Generated);
        t1.insert(kkey(1, 2), ChunkLifecycle::Loaded);
        t1.insert(kkey(2, 2), ChunkLifecycle::Loaded);
        t1.insert(kkey(3, 0), ChunkLifecycle::Absent);    // d3 > gen：不请求（半径外 Absent）

        const GenerationPolicyParams par1(true, 2, 1, 4);
        const bool parOk = par1.generationRadiusChunks == 2 && par1.renderRadiusChunks == 1
            && par1.scanExtentChunks == 4; // 合规入参规整恒等
        ok = ok && parOk;
        GenerationPolicy p1{ par1 };
        const GenerationPolicyDecision d1 = p1.decide(0, 0, tableSeam(t1));

        const QVector<GenerationPolicyRequest> want1 = {
            { 0, 0, 0 },                                            // d0
            { -1, -1, 1 }, { -1, 0, 1 }, { 0, -1, 1 },              // d1（字典序）
            { -2, -1, 2 }, { -2, 2, 2 }, { 2, -1, 2 }, { 2, 0, 2 }, // d2 边界（字典序）
            { 2, 1, 2 },
        };
        const bool exact1 = d1.toRequest == want1;
        ok = ok && exact1;
        if (!exact1)
            diag += QStringLiteral("[s1 want=%1 got=%2] ")
                        .arg(reqStr(want1), reqStr(d1.toRequest));

        // 序谓词复断言（diag 更利落）：priority 非降；同距 (cx,cz) 严格字典递增：
        bool orderOk = true;
        for (int i = 1; i < d1.toRequest.size() && orderOk; ++i) {
            const auto &a = d1.toRequest[i - 1];
            const auto &b = d1.toRequest[i];
            orderOk = b.priority >= a.priority
                && (b.priority > a.priority
                       || (b.cx > a.cx || (b.cx == a.cx && b.cz > a.cz)));
        }
        ok = ok && orderOk;
        if (!orderOk)
            diag += QStringLiteral("[order %1] ").arg(reqStr(d1.toRequest));

        // 边界含入 + 驻留格零请求（判别性点名；全集已由精确比对覆盖）：
        const int residentCells[8][2] = { { 1, 0 }, { 0, 1 }, { 0, -2 }, { -2, 0 },
            { 1, -2 }, { 2, -2 }, { 1, 1 }, { 2, 2 } };
        bool residentFree = true;
        for (const auto &c : residentCells)
            for (const auto &r : d1.toRequest)
                residentFree = residentFree && !(r.cx == c[0] && r.cz == c[1]);
        const bool boundaryIn = d1.toRequest.contains(GenerationPolicyRequest{ 2, 0, 2 })
            && d1.toRequest.contains(GenerationPolicyRequest{ -2, 2, 2 });
        ok = ok && residentFree && boundaryIn;
        if (!residentFree || !boundaryIn)
            diag += QStringLiteral("[residentFree=%1 boundaryIn=%2] ").arg(residentFree).arg(boundaryIn);

        // 场景 2：玩家 (3,-2)，gen=1——坐标偏移正确性（相对玩家非相对原点）。
        //   3×3 窗九格全布（未列格默认 Absent——场景 1 同纪律，不靠缺席默认凑断言）：
        QMap<qint64, ChunkLifecycle> t2;
        t2.insert(kkey(3, -2), ChunkLifecycle::Absent);  // → 请求 p0
        t2.insert(kkey(4, -2), ChunkLifecycle::Generated);
        t2.insert(kkey(2, -2), ChunkLifecycle::Loaded);
        t2.insert(kkey(3, -1), ChunkLifecycle::Absent);  // → 请求 p1（d1 边界）
        t2.insert(kkey(3, -3), ChunkLifecycle::Loading);
        t2.insert(kkey(2, -3), ChunkLifecycle::Generated);
        t2.insert(kkey(4, -3), ChunkLifecycle::Loaded);
        t2.insert(kkey(2, -1), ChunkLifecycle::Evicting);
        t2.insert(kkey(4, -1), ChunkLifecycle::Active);
        GenerationPolicy p2{ GenerationPolicyParams(true, 1, 0, 3) };
        const GenerationPolicyDecision d2 = p2.decide(3, -2, tableSeam(t2));
        const QVector<GenerationPolicyRequest> want2 = { { 3, -2, 0 }, { 3, -1, 1 } };
        const bool exact2 = d2.toRequest == want2;
        ok = ok && exact2;
        if (!exact2)
            diag += QStringLiteral("[s2 want=%1 got=%2] ")
                        .arg(reqStr(want2), reqStr(d2.toRequest));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2016b activation request face: toRequest is exactly the"
                             " Absent set inside the closed gen disk (player cell p0, ring-1"
                             " cells, distance-2 boundary cells included), priority strictly"
                             " ascending with (cx,cz) lexicographic ties, all five non-Absent"
                             " states never requested, beyond-radius Absent cells not"
                             " requested, and a re-seated player pins the offset semantics"
                          << (ok ? QString() : diag);
    });

    // ── r2016c：驱逐面——半径外驻留格恰入一次、距离降序；环带不驱逐（D4）；渲染半径无关─────
    runLeg(QStringLiteral("r2016c eviction face (section 29.4 P1 'which chunks to evict' + D4"
        " '[render, gen] annulus is never evicted'): with streaming enabled, gen radius 3,"
        " scan extent 5 and residents planted beyond the radius, toEvict contains each"
        " beyond-gen resident exactly once in descending distance order (farthest first)"
        " with (cx,cz) lexicographic ties; resident cells in the [render=1, gen=3] annulus"
        " and inside it are never evicted (D4 load-bearing); Absent/Loading/Evicting cells"
        " beyond the radius are never evicted; shrinking the render radius alone (1 -> 0)"
        " or raising it to equal gen leaves toEvict bit-identical - eviction looks only at"
        " the generation radius"), [&]() {
        bool ok = true;
        QString diag;

        QMap<qint64, ChunkLifecycle> t;
        // 半径外驻留格（驱逐候选，距离各异含同距）：
        t.insert(kkey(4, 0), ChunkLifecycle::Generated);  // d4
        t.insert(kkey(3, 4), ChunkLifecycle::Generated);  // d4（与 (4,0) 同距 → 字典序）
        t.insert(kkey(5, 1), ChunkLifecycle::Loaded);     // d5
        t.insert(kkey(0, -5), ChunkLifecycle::Active);    // d5
        t.insert(kkey(-5, -5), ChunkLifecycle::Loaded);   // d5
        // [render=1, gen=3] 环带驻留格（D4：永不驱逐）：
        t.insert(kkey(1, 0), ChunkLifecycle::Active);     // d1
        t.insert(kkey(2, 2), ChunkLifecycle::Generated);  // d2
        t.insert(kkey(-2, 1), ChunkLifecycle::Loaded);    // d2
        t.insert(kkey(3, 0), ChunkLifecycle::Loaded);     // d3
        // gen 内驻留格（更不驱逐）：
        t.insert(kkey(0, 0), ChunkLifecycle::Loaded);     // d0
        // 半径外非驻留态（恒不入 toEvict）：
        t.insert(kkey(4, 4), ChunkLifecycle::Absent);     // d4
        t.insert(kkey(0, 5), ChunkLifecycle::Loading);    // d5
        t.insert(kkey(-4, 5), ChunkLifecycle::Evicting);  // d5

        GenerationPolicy p{ GenerationPolicyParams(true, 3, 1, 5) };
        const GenerationPolicyDecision d = p.decide(0, 0, tableSeam(t));

        // 期望序：d5 三格字典序在前，d4 两格殿后（最远先卸 + 同距字典序）：
        const QVector<GenerationPolicyEvict> want = {
            { -5, -5 }, { 0, -5 }, { 5, 1 }, { 3, 4 }, { 4, 0 },
        };
        const bool exact = d.toEvict == want;
        ok = ok && exact;
        if (!exact)
            diag += QStringLiteral("[want=%1 got=%2] ").arg(evStr(want), evStr(d.toEvict));

        // 逐个恰一次（无重复）：
        QSet<qint64> uniq;
        for (const auto &e : d.toEvict)
            uniq.insert(kkey(e.cx, e.cz));
        const bool once = uniq.size() == d.toEvict.size() && d.toEvict.size() == 5;
        ok = ok && once;
        if (!once)
            diag += QStringLiteral("[once uniq=%1 n=%2] ").arg(uniq.size()).arg(d.toEvict.size());

        // 环带 + gen 内驻留格零驱逐（D4 承重点名）+ 半径外非驻留态零驱逐：
        const int forbidden[8][2] = { { 1, 0 }, { 2, 2 }, { -2, 1 }, { 3, 0 }, { 0, 0 },
            { 4, 4 }, { 0, 5 }, { -4, 5 } };
        bool guardOk = true;
        for (const auto &c : forbidden)
            for (const auto &e : d.toEvict)
                guardOk = guardOk && !(e.cx == c[0] && e.cz == c[1]);
        ok = ok && guardOk;
        if (!guardOk)
            diag += QStringLiteral("[guard %1] ").arg(evStr(d.toEvict));

        // 渲染半径单独调小 / 抬平都不改 toEvict（驱逐只看 gen 半径——D4 双参数解耦）。
        //   字段面：render ∈ [0, gen] 内合法值原样保留（0 与 3 都不越界）；> gen 才收敛
        //   （r2016d ② 的越界面管收敛，此处管「解耦」——渲染半径取值不改变驱逐单）：
        GenerationPolicy pLow{ GenerationPolicyParams(true, 3, 0, 5) };
        GenerationPolicy pEq{ GenerationPolicyParams(true, 3, 3, 5) };
        const bool normOk = pLow.params().renderRadiusChunks == 0
            && pEq.params().renderRadiusChunks == 3;
        const GenerationPolicyDecision dLow = pLow.decide(0, 0, tableSeam(t));
        const GenerationPolicyDecision dEq = pEq.decide(0, 0, tableSeam(t));
        const bool renderFree = normOk && dLow.toEvict == d.toEvict && dEq.toEvict == d.toEvict;
        ok = ok && renderFree;
        if (!renderFree)
            diag += QStringLiteral("[render norm=%1 low=%2 eq=%3] ")
                        .arg(normOk)
                        .arg(evStr(dLow.toEvict), evStr(dEq.toEvict));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2016c eviction face: each beyond-gen resident enters toEvict"
                             " exactly once, farthest first with lexicographic ties; annulus"
                             " and inside residents are never evicted (D4), non-resident"
                             " states beyond the radius are never evicted, and changing the"
                             " render radius alone never moves toEvict (generation radius is"
                             " the only eviction bound)"
                          << (ok ? QString() : diag);
    });

    // ── r2016d：确定性 + 规整一致性 + 结构钉（相对恒等面——阴性轮不误伤承重设计）────────────
    runLeg(QStringLiteral("r2016d determinism + normalization + structure pins (section 29.4 P1"
        " 'pure decision, headless-testable'): the same inputs decide()d twice through one"
        " policy and once each through two identically-parameterized instances produce"
        " bit-identical decisions (pure function, no hidden state) over a seam whose both"
        " lists are provably non-empty; out-of-range params (render above gen, scan below"
        " gen, negative radii, gen above the quint8 priority domain) are normalized at"
        " construction and the normalized decisions match the explicitly-in-range twins"
        " field-for-field and decision-for-decision; comment-stripped source pins hold the"
        " seam type, the decide signature, the off-by-default field and the QObjectFree"
        " compile-time pins while reverse probes prove the header carries no QObject base"
        " or QML surface, no thread primitive and no World/ChunkManager pointer, and"
        " world.{h,cpp} / gamesession.h / Main.qml never mention the policy (P1 zero"
        " call-site migration, zero QML touch)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 确定性（纯函数无隐状态）：同实例两次 + 双实例一次，三张决策逐位恒等；
        //    缝表双列表均非空（恒等非空转——空对空恒等无证明力）：
        QMap<qint64, ChunkLifecycle> td;
        td.insert(kkey(1, -1), ChunkLifecycle::Absent);   // → toRequest 非空
        td.insert(kkey(0, -1), ChunkLifecycle::Generated);
        td.insert(kkey(1, -3), ChunkLifecycle::Loaded);
        td.insert(kkey(4, -1), ChunkLifecycle::Generated);  // d3 > gen=2 → toEvict 非空
        td.insert(kkey(-3, -4), ChunkLifecycle::Active);    // d4 → toEvict 非空
        td.insert(kkey(4, 4), ChunkLifecycle::Absent);      // d4 半径外 Absent（不驱逐）
        const GenerationPolicyParams parD(true, 2, 2, 4);
        GenerationPolicy pA{ parD };
        GenerationPolicy pB{ parD };
        const GenerationPolicyDecision dA1 = pA.decide(1, -1, tableSeam(td));
        const GenerationPolicyDecision dA2 = pA.decide(1, -1, tableSeam(td));
        const GenerationPolicyDecision dB1 = pB.decide(1, -1, tableSeam(td));
        const bool nonVacuous = !dA1.toRequest.isEmpty() && !dA1.toEvict.isEmpty();
        const bool detOk = nonVacuous && dA1 == dA2 && dA1 == dB1;
        ok = ok && detOk;
        if (!detOk)
            diag += QStringLiteral("[det vac=%1 eq12=%2 eqB=%3 req=%4 ev=%5] ")
                        .arg(nonVacuous)
                        .arg(dA1 == dA2)
                        .arg(dA1 == dB1)
                        .arg(reqStr(dA1.toRequest), evStr(dA1.toEvict));

        // ② 规整一致性：gen < render / scan < gen 越界入参被规整 → 与显式合规双生逐位一致：
        const GenerationPolicyParams parOut(true, 2, 5, 1); // render 5→2, scan 1→2
        const GenerationPolicyParams parIn(true, 2, 2, 2);
        const bool fieldsOk = parOut.generationRadiusChunks == 2 && parOut.renderRadiusChunks == 2
            && parOut.scanExtentChunks == 2 && parIn.renderRadiusChunks == 2;
        GenerationPolicy pOut{ parOut };
        GenerationPolicy pIn{ parIn };
        const GenerationPolicyDecision dOut = pOut.decide(1, -1, tableSeam(td));
        const GenerationPolicyDecision dIn = pIn.decide(1, -1, tableSeam(td));
        const bool normDecide = dOut == dIn;
        ok = ok && fieldsOk && normDecide;
        if (!fieldsOk || !normDecide)
            diag += QStringLiteral("[norm f=%1 d=%2] ").arg(fieldsOk).arg(normDecide);

        // ③ 负值归一化：全负入参 → 全 0（窗口 = 玩家自格）：
        GenerationPolicy pNeg{ GenerationPolicyParams(true, -3, -7, -1) };
        const bool negFields = pNeg.params().generationRadiusChunks == 0
            && pNeg.params().renderRadiusChunks == 0 && pNeg.params().scanExtentChunks == 0;
        QMap<qint64, ChunkLifecycle> tZero;
        tZero.insert(kkey(0, 0), ChunkLifecycle::Absent);
        const GenerationPolicyDecision dZero = pNeg.decide(0, 0, tableSeam(tZero));
        const bool negOk = negFields && dZero.toRequest.size() == 1
            && dZero.toRequest.first() == GenerationPolicyRequest{ 0, 0, 0 }
            && dZero.toEvict.isEmpty();
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg f=%1 req=%2] ")
                        .arg(negFields)
                        .arg(reqStr(dZero.toRequest));

        // ④ 上界钳制：gen 超 quint8 priority 域 → 255（与 GenerationRequest::priority 同域）：
        const GenerationPolicyParams parClamp(true, 1000, 1000, 1000);
        const bool clampOk = parClamp.generationRadiusChunks == 255
            && parClamp.renderRadiusChunks == 255 && parClamp.scanExtentChunks == 255;
        ok = ok && clampOk;
        if (!clampOk)
            diag += QStringLiteral("[clamp g=%1 r=%2 s=%3] ")
                        .arg(parClamp.generationRadiusChunks)
                        .arg(parClamp.renderRadiusChunks)
                        .arg(parClamp.scanExtentChunks);

        // ⑤ 编译期复述（r2015d/r2011d 腿内复钉先例——头钉被删即双红）：
        static_assert(QObjectFree<GenerationPolicy>, "r2016d: policy must not carry QObject");
        static_assert(QObjectFree<GenerationPolicyParams>, "r2016d: params must not carry QObject");
        static_assert(QObjectFree<GenerationPolicyDecision>, "r2016d: decision must not carry QObject");
        static_assert(std::is_trivially_copyable_v<GenerationPolicyRequest>,
            "r2016d: request entry stays trivially copyable");
        static_assert(std::is_trivially_copyable_v<GenerationPolicyEvict>,
            "r2016d: evict entry stays trivially copyable");

        // ⑥ 源码钉（pinSet 剥注释）+ 反探（minCount=1 空转钉惯用法——miss 非空 = 合规缺席）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src")); // r2011d/r2015d 同款解析（exe 相对 src/）
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QStringList missGp = pinSet(
            srcRoot + QStringLiteral("/World/generationpolicy.h"), {
                SrcPin("r2016 policy class", "class GenerationPolicy", 1),
                SrcPin("r2016 pure seam type",
                    "using ChunkSeamFn = std::function<ChunkLifecycle(int cx, int cz)>", 1),
                SrcPin("r2016 decide signature",
                    "GenerationPolicyDecision decide(int playerCx, int playerCz, const"
                    " ChunkSeamFn &querySeam) const", 1),
                SrcPin("r2016 off-by-default field", "bool streamingEnabled = false", 1),
                SrcPin("r2016 decision QObjectFree pin",
                    "static_assert(QObjectFree<GenerationPolicyDecision>", 1),
            });
        for (const QString &m : missGp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool negProbeOk
            = forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), ": public QObject")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "Q_OBJECT")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "Q_INVOKABLE")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "Q_PROPERTY")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "QThread")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "std::thread")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "QMutex")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "World *")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/generationpolicy.h"), "ChunkManager *");
        ok = ok && negProbeOk;
        if (!negProbeOk) diag += QStringLiteral("[neg-probe] ");

        // ⑦ P1 零调用点迁移 / QML 零触碰（r2011 零接线先例——结构性事实）：
        const bool unwired = forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "GenerationPolicy")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "GenerationPolicy")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/gamesession.h"), "GenerationPolicy")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "GenerationPolicy");
        ok = ok && unwired;
        if (!unwired) diag += QStringLiteral("[unwired] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2016d determinism + normalization + structure pins: repeated"
                             " and cross-instance decisions are bit-identical over a"
                             " non-vacuous seam, out-of-range params normalize to their"
                             " in-range twins at construction (fields and decisions), the"
                             " priority domain is clamped, and source pins plus reverse"
                             " probes keep the header QObject-free, thread-free, seam-pure"
                             " and the codebase unwired with zero QML mention"
                          << (ok ? QString() : diag);
    });

    // ── r2017d：setParams 规整唯一路径（r2017 fix D，agent-review-2026-09-16 Info
    //    「GenerationPolicy setParams 不再规整」）——r2016d 规整验证族的同变更扩展（独立腿
    //    落地，filter 词 r2017；r2016d 本体不动 = 构造规整绿色回归柱）：
    //    ①越界双生 twin：setParams(越界) ≡ 同参构造——字段逐位恒等 + 决策逐位恒等（此前
    //      setParams 裸赋值可破不变量：gen>255 时 decide 的 priority=quint8(cheb) 回绕）；
    //      判别格 = (300,0) Absent（仅在未规整的 ±400 越界窗内可见/优先级 quint8(300)=44
    //      回绕）+ (255,0) Absent（规整后恰在窗界 → priority 255 无回绕钉）；
    //    ②合规值 setParams 生效：字段读回一致 + 决策与同参构造 twin 逐位一致（双列表非空
    //      = 恒等非空转）；
    //    ③负值双生：全负入参 setParams ≡ 构造（全 0 归一）；
    //    ④默认关形状经 setParams 存活（decide 恒空——零变化不变量的参数面不被 setParams 破）；
    //    ⑤源码钉：构造与 setParams 双写共享 normalized 路径。
    //    相对恒等纪律（R20.11）：twin 两侧同变仍恒等；绝对列表内容只钉 ① 的判别格（被摘
    //    语义本体）。本单 NEG 轮登记为 NEG-A/B/C 三处主修；fix D 以双生断言自锚（摘规整
    //    唯一路径 → ①③ 字段恒等面恰红）。
    runLeg(QStringLiteral("r2017d setParams normalization twin (r2017 fix D, agent-review"
        " 2026-09-16 Info 'setParams no longer normalizes'): setParams with out-of-range"
        " params is bit-identical to constructing with the same params - fields (gen 300"
        " clamps to 255, negative render to 0, scan into [gen,255]) and decisions alike"
        " over a seam where an Absent cell at (300,0) is visible only through the"
        " unnormalized +-400 window (the quint8 priority wraparound discriminator) and"
        " an Absent cell at (255,0) pins an exact priority-255 request with no wrap;"
        " in-range setParams takes effect (fields read back, decisions identical to the"
        " constructed twin over a non-vacuous seam); all-negative setParams twins the"
        " constructor's all-zero normalization; the default-off shape survives setParams"
        " (decide stays empty); source pins hold both the constructor and setParams on"
        " the shared normalize path"), [&]() {
        bool ok = true;
        QString diag;

        // 缝帮手（同段先例）：合成生命周期表 + 条目 diag。
        const auto kkey = [](int cx, int cz) { return (qint64(cx) << 32) | quint32(cz); };
        const auto tableSeam = [&kkey](const QMap<qint64, ChunkLifecycle> &t) {
            return [&t, &kkey](int cx, int cz) -> ChunkLifecycle {
                return t.value(kkey(cx, cz), ChunkLifecycle::Absent);
            };
        };
        const auto reqStr = [](const QVector<GenerationPolicyRequest> &v) {
            QString s;
            for (const auto &r : v)
                s += QStringLiteral("(%1,%2)p%3 ").arg(r.cx).arg(r.cz).arg(r.priority);
            return s;
        };
        const auto evStr = [](const QVector<GenerationPolicyEvict> &v) {
            QString s;
            for (const auto &e : v)
                s += QStringLiteral("(%1,%2) ").arg(e.cx).arg(e.cz);
            return s;
        };
        const auto fieldsEq = [](const GenerationPolicyParams &a,
                                     const GenerationPolicyParams &b) {
            return a.streamingEnabled == b.streamingEnabled
                && a.generationRadiusChunks == b.generationRadiusChunks
                && a.renderRadiusChunks == b.renderRadiusChunks
                && a.scanExtentChunks == b.scanExtentChunks;
        };

        // ① 越界双生 twin（setParams vs 构造）：字段 + 决策逐位恒等：
        GenerationPolicy pSet;
        pSet.setParams(GenerationPolicyParams(true, 300, -2, 400)); // Info 原病灶面：gen>255
        const GenerationPolicy pCtor{ GenerationPolicyParams(true, 300, -2, 400) };
        const bool twinFields = fieldsEq(pSet.params(), pCtor.params())
            && pSet.params().generationRadiusChunks == 255
            && pSet.params().renderRadiusChunks == 0 && pSet.params().scanExtentChunks == 255;

        // 大缝（自定义 lambda——勿用 tableSeam：其缺席默认 Absent 会让 ±255 窗全 Absent）：
        //   |cx|,|cz| ≤ 300 全 Loaded（填满规整后 ±255 窗），窗外 Absent，(300,0)/(255,0)
        //   特判 Absent——(300,0) 仅未规整 ±400 越界窗可见（回绕判别格），(255,0) 规整后
        //   恰窗界 → priority 255 无回绕钉。
        const auto wideSeam = [](int cx, int cz) -> ChunkLifecycle {
            if ((cx == 300 && cz == 0) || (cx == 255 && cz == 0))
                return ChunkLifecycle::Absent;
            return (cx >= -300 && cx <= 300 && cz >= -300 && cz <= 300)
                ? ChunkLifecycle::Loaded
                : ChunkLifecycle::Absent;
        };
        const GenerationPolicyDecision dSet = pSet.decide(0, 0, wideSeam);
        const GenerationPolicyDecision dCtor = pCtor.decide(0, 0, wideSeam);
        const bool twinDecide = dSet == dCtor && dSet.toRequest.size() == 1
            && dSet.toRequest.first() == GenerationPolicyRequest{ 255, 0, 255 }
            && dSet.toEvict.isEmpty(); // 恰 (255,0)p255 一请求（无回绕）+ 零驱逐
        ok = ok && twinFields && twinDecide;
        if (!twinFields || !twinDecide) {
            QString reqHead, evHead;
            for (int i = 0; i < qMin(3, dSet.toRequest.size()); ++i)
                reqHead += reqStr(dSet.toRequest.mid(i, 1));
            for (int i = 0; i < qMin(3, dSet.toEvict.size()); ++i)
                evHead += evStr(dSet.toEvict.mid(i, 1));
            diag += QStringLiteral("[twin f=%1 d=%2 reqN=%3 evN=%4 head=%5 %6] ")
                        .arg(twinFields).arg(twinDecide)
                        .arg(dSet.toRequest.size()).arg(dSet.toEvict.size())
                        .arg(reqHead, evHead);
        }

        // ② 合规值 setParams 生效（双列表非空 = 恒等非空转）：
        QMap<qint64, ChunkLifecycle> t7;
        t7.insert(kkey(1, -1), ChunkLifecycle::Absent);   // → toRequest（gen 窗内）
        t7.insert(kkey(0, -1), ChunkLifecycle::Generated);
        t7.insert(kkey(1, -3), ChunkLifecycle::Loaded);
        t7.insert(kkey(4, -1), ChunkLifecycle::Generated); // d3 > gen → toEvict
        t7.insert(kkey(-3, -4), ChunkLifecycle::Active);   // d4 → toEvict
        t7.insert(kkey(4, 4), ChunkLifecycle::Absent);     // 半径外 Absent（不请求不驱逐）
        GenerationPolicy pOk;
        pOk.setParams(GenerationPolicyParams(true, 2, 1, 4));
        const GenerationPolicy pOkTwin{ GenerationPolicyParams(true, 2, 1, 4) };
        const bool inFields = fieldsEq(pOk.params(), pOkTwin.params())
            && pOk.params().generationRadiusChunks == 2 && pOk.params().renderRadiusChunks == 1
            && pOk.params().scanExtentChunks == 4;
        const GenerationPolicyDecision dOk = pOk.decide(1, -1, tableSeam(t7));
        const GenerationPolicyDecision dOkTwin = pOkTwin.decide(1, -1, tableSeam(t7));
        const bool inDecide = dOk == dOkTwin && !dOk.toRequest.isEmpty()
            && !dOk.toEvict.isEmpty();
        ok = ok && inFields && inDecide;
        if (!inFields || !inDecide)
            diag += QStringLiteral("[in f=%1 d=%2 req=%3 ev=%4] ")
                        .arg(inFields).arg(inDecide)
                        .arg(reqStr(dOk.toRequest), evStr(dOk.toEvict));

        // ③ 负值双生：全负入参 setParams ≡ 构造（全 0 归一 + 窗 = 玩家自格）：
        GenerationPolicy pNeg;
        pNeg.setParams(GenerationPolicyParams(true, -3, -7, -1));
        const GenerationPolicy pNegTwin{ GenerationPolicyParams(true, -3, -7, -1) };
        const bool negFields = fieldsEq(pNeg.params(), pNegTwin.params())
            && pNeg.params().generationRadiusChunks == 0
            && pNeg.params().renderRadiusChunks == 0 && pNeg.params().scanExtentChunks == 0;
        QMap<qint64, ChunkLifecycle> tZero;
        tZero.insert(kkey(0, 0), ChunkLifecycle::Absent);
        const bool negDecide = pNeg.decide(0, 0, tableSeam(tZero))
            == pNegTwin.decide(0, 0, tableSeam(tZero));
        ok = ok && negFields && negDecide;
        if (!negFields || !negDecide)
            diag += QStringLiteral("[neg f=%1 d=%2] ").arg(negFields).arg(negDecide);

        // ④ 默认关形状经 setParams 存活（decide 恒空——零变化参数面不被 setParams 破）：
        GenerationPolicy pOff;
        pOff.setParams(GenerationPolicyParams{});
        const GenerationPolicyDecision dOff = pOff.decide(0, 0, tableSeam(t7));
        const bool offOk = !pOff.params().streamingEnabled && dOff.toRequest.isEmpty()
            && dOff.toEvict.isEmpty();
        ok = ok && offOk;
        if (!offOk) diag += QStringLiteral("[off] ");

        // ⑤ 源码钉：规整唯一路径三站齐备（剥注释后 normalized( 恰 3 = static 定义 + 构造
        //    调用 + setParams 调用——摘任一走线即红）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src"));
        const QStringList missGp = pinSet(
            srcRoot + QStringLiteral("/World/generationpolicy.h"), {
                SrcPin("r2017 ctor and setParams share the normalize path", "normalized(", 3),
            });
        for (const QString &m : missGp) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2017d setParams normalization twin: out-of-range setParams"
                             " is field- and decision-identical to the constructed twin"
                             " (gen 300 -> 255 with the (300,0) wraparound discriminator"
                             " gone and an exact priority-255 request at the window edge),"
                             " in-range setParams takes effect identically over a non-"
                             " vacuous seam, all-negative setParams twins the constructor,"
                             " the default-off shape survives setParams, and both write"
                             " paths are pinned on the shared normalize (fix D, review"
                             " 2026-09-16 Info)"
                          << (ok ? QString() : diag);
    });
}
