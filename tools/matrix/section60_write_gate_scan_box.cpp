#include "matrix_helpers.h"

// t1090 写门家族「核心域假设」同族清偿·后续批探针段（6 腿；filter 词 r2060；矩阵 769→775）。
// 置尾先例沿用（接 section59，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：t1074 外环光照 / t1089 静默写门之后「核心域假设」同族第三面——**扫描门与盒域钳制**。
// 固定盒（六比较 / 双比较早退）钳制在 sparse 无限世界把已物化外环（负坐标/出核）的扫描与光照回灌
// 整体吞掉。本批四员（t1089 留池登记三员 + FallingBlock tick 链面合并）：①destroySphereSilent 核心
// 盒扫描门 + 其回灌盒钳制（外环弹坑该毁不毁 / 光照不回灌）；②decayLeavesAround 扫描门 + tickLeafDecay
// 回灌盒钳制 + packLeafCell quint16 截断（外环树叶腐朽不跑：拒入队 + 键幻影 + 回灌 no-op 三面）；
// ③重力级联邻域门（checkGravityBlockOnEdit / dropGravityColumn / cascadeGravityAround 种子·邻域门 +
// 两处回灌盒钳制——外环沙柱悬空残留）；④FallingBlock tick 负坐标列跳过（entitymanager——与③同一条
// 生产链：级联清格 → gravityBlockFell → spawnFallingBlock → 本 tick 下落着地；只开 world.cpp 半边会
// 把「方块悬空」换成「方块清了、实体永冻」的负侧回归 → 两 face 合并清偿如实登记）。
//
// 修法选型（留痕）：Fixed 分支现行语句原样（零变化墙）；sparse 扫描门 = **扫描域无界化**（非逐格
// 物化门过滤——读 blockAt / 写 m_chunks.setBlock 已由 ChunkManager 统一物化门自带过滤，逐格再查是
// 重复权威）；盒域 = y 同构钳有限高 + x/z 无界（t1074 recomputeLightAround 盒公式先例）；refloodBox
// 退化盒防御（t1074 NEG-2 真崩兑出面）原样保持（f 腿钉）。
//
// 现场核实（逐员，核实先行——t1089 五员全真缺口先例）：四员外环消费场景全部真实可达（玩家位 /
//   实体位 / 索引 tick 驱动）→ **四员全确认零降级**。留池（本批现场新发现，禁顺手修）：primed TNT
//   tick 与 Mob/Item tick 的同形 cx<0||cz<0 跳过（entitymanager）——非级联链承重面，批头锚注登记。
//
// 腿面（逐员专属可单独归因，禁一刀切批修）：
//   r2060a destroySphereSilent 外环承重墙（负坐标弹坑毁块行为级 + 弹坑天光回灌 15 + 拟合破坏向量负
//     坐标 + 球外缘完整 + 未物化球空返回 + y 域门空返回 + 核心域对照）；r2060b 树叶腐朽外环承重墙
//     （负坐标叶入队 + hashVoxel 确定性窗口驱动渐退 + 支撑对照叶永留 + 叶位/叶下天光 13→15 回灌
//     [packLeafCell 保符号往返的行为级]）；r2060c 重力级联外环承重墙（②支撑破坏柱坍 + ③26 邻浮沙
//     连锁 + 坍落柱天光回灌 15）；r2060d **fixed 世界零变化墙**（爆炸域内真/域外空/y 域外空 + 级联
//     域内真/域外直调零副作用 + 叶衰域内真/域外扫零入队 + 落体域内着地/负列冻结——Fixed 分支逐字
//     原样的行为级）；r2060e FallingBlock 实体负坐标列着地承重墙（真管线落体在已物化负列着地还原
//     [旧门红载荷 = 实体永冻半空]）；r2060f 结构钉（十站点锚注各 count==1 + 稀疏无界盒行计数 + 包
//     键位型计数 + 退化盒防御钉 + t1089 五员锚注零变化复钉 + t1074 无界盒复钉 + 批头/留池锚注族）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2060_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘 destroySphereSilent 扫描门 + 回灌盒两模式分流（还原旧六比较单行 + 旧钳制两行 + 摘两
//     锚注）→ **恰红 = {r2060a, r2060f: a 行为面 + f 扫描门锚注/无界盒行计数子面}**。b/c/d/e 不误伤。
//   NEG-2 摘重力级联链四站点两模式分流（checkGravityBlockOnEdit + dropGravityColumn + cascade 两门
//     + entitymanager FallingBlock skip 还原旧行 + 摘锚注）→ **恰红 = {r2060c, r2060e, r2060f:
//     c 行为面 + e 实体面 + f 级联锚注族/y 门行计数/实体锚注子面}**。a/b/d 不误伤。
//
// rig 纪律：sparse 构造缝 `World w{ {82, 48, 48, 96, 0} }`（section54 r2053c / section59 同门）+
//   loadChunkAt 按需物化（r2047c 负侧先例）；rig 坪取 y=79..84 高空（s82 地形/树冠带之上）；rig 基
//   建全经 setBlock 主门（sparse 分支已无界）；行为级腿不查信号只查栅格终态与光值；坐标不作哨兵。

namespace {

// fixed 宿主小世界 incantation（section59 同款四 setter）。
inline void initFixedScanWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 推进下落体管线（section59 同款 tick 形态；600 拍 ≈ 9.6s 模拟时——4 格落差充足裕量）。
inline void tickFallingScan(EntityManager &em, World &w)
{
    const QVector3D farListener(4.5f, 80.5f, 4.5f);
    for (int t = 0; t < 600; ++t)
        em.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
}

inline int aliveFallingScan(const EntityManager &em)
{
    int n = 0;
    for (int i = 0; i < em.count(); ++i)
        if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::FallingBlock)) ++n;
    return n;
}

// tickLeafDecay 散布窗口的确定性复刻（PLAN §2-K）：mixedSeed = seed ^ (窗口序号 × 0x9E3779B9)，
//   命中判据 = int(hashVoxel & 0xFFFF) % 100 < kLeafDecayPct(=1)——与 world.cpp tickLeafDecay 逐字同
//   式（kLeafDecayPct 为私有常量，字面 1 由 r2060f 源钉「kLeafDecayPct          = 1」锁死）。
//   返回首个命中窗口序号（无命中 -1）；固定 seed+坐标 → 恒定命中序号 = 零 flake。
inline int firstDecayWindow(const World &w, int x, int y, int z)
{
    for (int k = 0; k < 4000; ++k) {
        const int mixed = int(quint32(w.seed()) ^ (quint32(k) * 0x9E3779B9u));
        if (int(w.hashVoxel(mixed, x, y, z) & 0xFFFFu) % 100 < 1)
            return k;
    }
    return -1;
}

// 推进恰 (n+1) 个判定窗（节流 kLeafDecayTickInterval=4 调/窗：窗口 k 在第 4(k+1) 次调用开）。
inline void driveLeafWindows(World &w, int lastWindow)
{
    for (int c = 0; c < 4 * (lastWindow + 1); ++c)
        w.tickLeafDecay();
}

} // namespace

void MatrixRun::section60_write_gate_scan_box()
{
    // ── r2060a：destroySphereSilent 外环承重墙（负坐标弹坑毁块 + 天光回灌 + 空返回面 + 核心域对照）──
    runLeg("r2060a destroySphereSilent outer-ring wall (in a sparse world with a materialized"
        " negative chunk a centered explosion excavates the negative-coordinate crater to air"
        " and reports destroyed voxels with negative x, the crater column re-floods to full"
        " skylight [old-gate red payload: the crater survives and the clamped reflood box"
        " no-ops], rim cells outside the sphere stay stone, a sphere centered in an"
        " unmaterialized chunk and an out-of-height sphere both return empty with zero side"
        " effect, and the core-domain control crater excavates the same way)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } }; // sparse 构造缝：核心 48×48 h96 s82 零预生成
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标弹坑（t1090 靶面）：实心石台 5×3×5 于 x,z ∈ [-10,-6], y ∈ [79,81]，球心 (-8,80,-8)
        //   r=2.5。旧门红载荷 = 域外格全部漏扫（弹坑原样）+ 回灌盒被钳（负侧盒倒置 → 退化盒防御
        //   no-op，光不回灌）。
        for (int x = -10; x <= -6; ++x)
            for (int y = 79; y <= 81; ++y)
                for (int z = -10; z <= -6; ++z)
                    w.setBlock(x, y, z, BR::Stone, 0);
        ok = ok && w.skyLightAt(-8, 80, -8) == 0; // 爆前石格内恒暗（回灌面基准）
        const auto dv = w.destroySphereSilent(-8, 80, -8, 2.5f);
        const bool centerGone = w.blockAt(-8, 80, -8) == BR::Air;
        bool negDv = false;
        for (const auto &d : dv)
            if (d.x < 0) { negDv = true; break; }
        const bool skyFlooded = w.skyLightAt(-8, 80, -8) == 15; // 弹坑开放天光（回灌盒无界化面）
        const bool rimIntact = w.blockAt(-10, 81, -10) == BR::Stone
            && w.blockAt(-6, 79, -6) == BR::Stone; // 球外缘（dist²=9 > 2.5²）不波及
        const bool negOk = centerGone && negDv && skyFlooded && rimIntact;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg ctr=%1 dv=%2 negdv=%3 sky=%4 rim=%5]")
                        .arg(w.blockAt(-8, 80, -8)).arg(int(dv.size())).arg(negDv)
                        .arg(w.skyLightAt(-8, 80, -8)).arg(rimIntact);
        // 未物化球：chunk (-3,-3) Absent → blockAt 物化门读 0 = air 天然跳过 → 空返回零副作用。
        const auto dv2 = w.destroySphereSilent(-40, 80, -40, 2.5f);
        const bool rejOk = dv2.empty() && w.blockAt(-40, 80, -40) == BR::Air;
        ok = ok && rejOk;
        if (!rejOk)
            diag += QStringLiteral("[rej n=%1 b=%2]").arg(int(dv2.size()))
                        .arg(w.blockAt(-40, 80, -40));
        // y 域门（两模式同构有限高）：y=200 ≥ h96 → 空返回。
        const auto dv3 = w.destroySphereSilent(-8, 200, -8, 2.5f);
        const bool yOk = dv3.empty();
        ok = ok && yOk;
        if (!yOk) diag += QStringLiteral("[y n=%1]").arg(int(dv3.size()));
        // 核心域正坐标对照（域内基本行为——分离「sparse 模式工作」与「外环扫描门」）。
        for (int x = 2; x <= 6; ++x)
            for (int y = 79; y <= 81; ++y)
                for (int z = 2; z <= 6; ++z)
                    w.setBlock(x, y, z, BR::Stone, 0);
        const auto dv4 = w.destroySphereSilent(4, 80, 4, 2.5f);
        const bool posOk = w.blockAt(4, 80, 4) == BR::Air && !dv4.empty()
            && w.skyLightAt(4, 80, 4) == 15;
        ok = ok && posOk;
        if (!posOk)
            diag += QStringLiteral("[pos ctr=%1 n=%2 sky=%3]").arg(w.blockAt(4, 80, 4))
                        .arg(int(dv4.size())).arg(w.skyLightAt(4, 80, 4));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060a destroySphereSilent outer-ring wall (in a sparse world with a"
               " materialized negative chunk a centered explosion excavates the"
               " negative-coordinate crater to air and reports destroyed voxels with negative"
               " x, the crater column re-floods to full skylight [old-gate red payload: the"
               " crater survives and the clamped reflood box no-ops], rim cells outside the"
               " sphere stay stone, a sphere centered in an unmaterialized chunk and an"
               " out-of-height sphere both return empty with zero side effect, and the"
               " core-domain control crater excavates the same way)"
            << (ok ? QString() : diag);
    });

    // ── r2060b：树叶腐朽外环承重墙（负坐标叶入队 + 确定性渐退 + 对照叶永留 + 天光回灌）──────────
    runLeg("r2060b leaf-decay outer-ring wall (in a sparse world with a materialized negative"
        " chunk an unsupported leaf at a negative coordinate enters the progressive decay"
        " queue after the neighbouring log is cleared and deterministically decays across the"
        " replicated hash windows [old-gate red payload: the fixed-box scan gate never enqueues"
        " it and the quint16 key truncation would read back a phantom coordinate], a"
        " log-supported control leaf stays forever, and the decayed cell and the cell beneath"
        " it re-flood from attenuated skylight to full 15)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // rig：L=(-10,80,-10) 待破原木；K=(-6,80,-10) 对照叶支撑原木；A=(-12,80,-10) 靶叶（L 破后
        //   4 格内无原木 = 失撑）；B=(-8,80,-10) 对照叶（K 距 2 ≤ 4 = 有支撑）。
        w.setBlock(-10, 80, -10, BR::Log, 0);
        w.setBlock(-6, 80, -10, BR::Log, 0);
        w.setBlock(-12, 80, -10, BR::Leaves, 0);
        w.setBlock(-8, 80, -10, BR::Leaves, 0);
        // 叶 opacity=1 → 叶为列 first-opaque → 叶下天光衰减 <15（回灌面基准）。
        const int preSky = w.skyLightAt(-12, 79, -10);
        const bool preOk = preSky < 15;
        ok = ok && preOk;
        if (!preOk) diag += QStringLiteral("[preSky=%1]").arg(preSky);
        // 破 L + 失撑扫描（生产形态 = playercontroller 破原木直调；旧门红载荷 = 负坐标叶被扫描门
        //   拒入队，且 quint16 键截断会把负坐标包成幻影坐标回读他格）。
        w.setBlock(-10, 80, -10, BR::Air, 0);
        w.decayLeavesAround(-10, 80, -10);
        // 确定性渐退：复刻散布哈希找 A 的首个命中窗（seed 82 + 坐标固定 → 恒定，零 flake），推窗。
        const int hit = firstDecayWindow(w, -12, 80, -10);
        const bool hitOk = hit >= 0;
        ok = ok && hitOk;
        if (!hitOk) { diag += QStringLiteral("[hit=-1] "); }
        else {
            driveLeafWindows(w, hit);
            const bool decayed = w.blockAt(-12, 80, -10) == BR::Air;
            const bool kept = w.blockAt(-8, 80, -10) == BR::Leaves; // 对照叶有原木支撑 → 永留
            const bool flooded = w.skyLightAt(-12, 79, -10) == 15
                && w.skyLightAt(-12, 80, -10) == 15; // 叶位/叶下回灌满天光（回灌盒无界化面）
            const bool decOk = decayed && kept && flooded;
            ok = ok && decOk;
            if (!decOk)
                diag += QStringLiteral("[dec ctr=%1 kept=%2 skyU=%3 skyA=%4 hit=%5]")
                            .arg(w.blockAt(-12, 80, -10))
                            .arg(w.blockAt(-8, 80, -10) == BR::Leaves)
                            .arg(w.skyLightAt(-12, 79, -10)).arg(w.skyLightAt(-12, 80, -10))
                            .arg(hit);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060b leaf-decay outer-ring wall (in a sparse world with a materialized"
               " negative chunk an unsupported leaf at a negative coordinate enters the"
               " progressive decay queue after the neighbouring log is cleared and"
               " deterministically decays across the replicated hash windows [old-gate red"
               " payload: the fixed-box scan gate never enqueues it and the quint16 key"
               " truncation would read back a phantom coordinate], a log-supported control"
               " leaf stays forever, and the decayed cell and the cell beneath it re-flood"
               " from attenuated skylight to full 15)"
            << (ok ? QString() : diag);
    });

    // ── r2060c：重力级联外环承重墙（②支撑破坏柱坍 + ③26 邻浮沙连锁 + 坍落柱天光回灌）──────────
    runLeg("r2060c gravity-cascade outer-ring wall (in a sparse world with a materialized"
        " negative chunk destroying the support under a negative-coordinate sand column"
        " collapses the whole column to air [old-gate red payload: the column floats], the"
        " collapsed column re-floods to full skylight, and an edit adjacent to a floating sand"
        " block placed via the entity landing write collapses it through the 26-neighbour"
        " cascade scan)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // ② 支撑破坏柱坍（t799 语义在负侧）：支撑石 (-8,80,-8) + 沙柱 81..83。旧门红载荷 =
        //   checkGravityBlockOnEdit / dropGravityColumn 固定盒早退 → 沙柱悬空残留。
        w.setBlock(-8, 80, -8, BR::Stone, 0);
        w.setBlock(-8, 81, -8, BR::Sand, 0);
        w.setBlock(-8, 82, -8, BR::Sand, 0);
        w.setBlock(-8, 83, -8, BR::Sand, 0);
        ok = ok && w.skyLightAt(-8, 81, -8) == 0; // 爆前沙格恒暗（回灌面基准）
        w.setBlock(-8, 80, -8, BR::Air, 0);       // 拆支撑 → ② 直接上方支线
        const bool colGone = w.blockAt(-8, 81, -8) == BR::Air && w.blockAt(-8, 82, -8) == BR::Air
            && w.blockAt(-8, 83, -8) == BR::Air;
        const bool colSky = w.skyLightAt(-8, 81, -8) == 15; // 坍落柱回灌（非批收口盒无界化面）
        const bool colOk = colGone && colSky;
        ok = ok && colOk;
        if (!colOk)
            diag += QStringLiteral("[col %1/%2/%3 sky=%4]").arg(w.blockAt(-8, 81, -8))
                        .arg(w.blockAt(-8, 82, -8)).arg(w.blockAt(-8, 83, -8))
                        .arg(w.skyLightAt(-8, 81, -8));
        // ③ 26 邻域级联（t930 语义在负侧）：石 S=(-6,81,-6) 先落位（落位时 ③ 扫描无浮沙 = no-op），
        //   浮沙 F=(-7,81,-6) 经实体着地写 setBlockFromEntity（不经重力钩子 → 保持悬浮），再拆 S →
        //   ③ 从编辑格 26 邻扫到 F 失撑 → 整格清。旧门红载荷 = 种子门/邻域门拒负坐标 → F 永浮。
        w.setBlock(-6, 81, -6, BR::Stone, 0);
        const bool fPlaced = w.setBlockFromEntity(-7, 81, -6, BR::Sand)
            && w.blockAt(-7, 81, -6) == BR::Sand;
        ok = ok && fPlaced;
        if (!fPlaced) diag += QStringLiteral("[fPlace ret=%1]").arg(fPlaced);
        w.setBlock(-6, 81, -6, BR::Air, 0); // 编辑 S → ③ 26 邻复检
        const bool cascadeOk = w.blockAt(-7, 81, -6) == BR::Air;
        ok = ok && cascadeOk;
        if (!cascadeOk)
            diag += QStringLiteral("[casc F=%1]").arg(w.blockAt(-7, 81, -6));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060c gravity-cascade outer-ring wall (in a sparse world with a materialized"
               " negative chunk destroying the support under a negative-coordinate sand column"
               " collapses the whole column to air [old-gate red payload: the column floats],"
               " the collapsed column re-floods to full skylight, and an edit adjacent to a"
               " floating sand block placed via the entity landing write collapses it through"
               " the 26-neighbour cascade scan)"
            << (ok ? QString() : diag);
    });

    // ── r2060d：fixed 世界零变化墙（四员 Fixed 分支逐字原样的行为级）───────────────────────────
    runLeg("r2060d fixed-world zero-change wall (in a fixed world where the whole domain is"
        " the core all four members keep their exact former behavior: an in-domain explosion"
        " excavates while negative-center and out-of-height spheres return empty, an in-domain"
        " sand column collapses on support removal while out-of-domain cascade calls are"
        " zero-effect no-ops, an in-domain leaf decays across the replicated windows while an"
        " out-of-domain decay scan enqueues nothing, and a falling block lands in-domain but"
        " stays frozen in a negative column)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedScanWorld(w);
        // 爆炸：域内真 + 负心空 + y 域外空。
        for (int x = 18; x <= 22; ++x)
            for (int y = 79; y <= 81; ++y)
                for (int z = 18; z <= 22; ++z)
                    w.setBlock(x, y, z, BR::Stone, 0);
        const auto dv1 = w.destroySphereSilent(20, 80, 20, 2.5f);
        const bool a1 = w.blockAt(20, 80, 20) == BR::Air && !dv1.empty();
        const auto dv2 = w.destroySphereSilent(-8, 80, -8, 2.5f);
        const auto dv3 = w.destroySphereSilent(20, 200, 20, 2.5f);
        const bool a2 = dv2.empty() && dv3.empty();
        ok = ok && a1 && a2;
        if (!(a1 && a2))
            diag += QStringLiteral("[a1=%1 a2=%2 n2=%3 n3=%4]").arg(a1).arg(a2)
                        .arg(int(dv2.size())).arg(int(dv3.size()));
        // 级联：域内支撑破坏柱坍真 + 域外直调零副作用（零 gravityBlockFell 发射）。
        w.setBlock(30, 80, 20, BR::Stone, 0);
        w.setBlock(30, 81, 20, BR::Sand, 0);
        w.setBlock(30, 82, 20, BR::Sand, 0);
        int fellCount = 0;
        const QMetaObject::Connection fellConn = QObject::connect(
            &w, &World::gravityBlockFell,
            [&fellCount](int, int, int, int) { ++fellCount; });
        w.setBlock(30, 80, 20, BR::Air, 0);
        const bool b1 = w.blockAt(30, 81, 20) == BR::Air && w.blockAt(30, 82, 20) == BR::Air
            && fellCount == 2;
        const int fellBefore = fellCount;
        w.checkGravityBlockOnEdit(-1, 80, 20, BR::Stone, BR::Air); // 域外直调 → 门早退零副作用
        w.dropGravityColumn(-1, 80, 20);
        w.cascadeGravityAround(-1, 80, 20);
        const bool b2 = fellCount == fellBefore;
        ok = ok && b1 && b2;
        if (!(b1 && b2))
            diag += QStringLiteral("[b1=%1 b2=%2 fell=%3/%4]").arg(b1).arg(b2)
                        .arg(fellBefore).arg(fellCount);
        QObject::disconnect(fellConn);
        // 叶衰：域内真（确定性窗驱动）+ 域外扫零入队（空队列 → tick 全程 no-op）。
        w.setBlock(20, 80, 40, BR::Log, 0);
        w.setBlock(22, 80, 40, BR::Leaves, 0); // 距 L 2、距其余原木 >4 → 失撑候选
        w.setBlock(20, 80, 40, BR::Air, 0);
        w.decayLeavesAround(20, 80, 40);
        const int hit = firstDecayWindow(w, 22, 80, 40);
        const bool c1 = hit >= 0;
        if (c1) driveLeafWindows(w, hit);
        const bool c2 = c1 && w.blockAt(22, 80, 40) == BR::Air;
        w.decayLeavesAround(-20, 80, -20); // 域外中心（负坐标）→ 扫描盒全域外 → 零入队
        for (int c = 0; c < 40; ++c)
            w.tickLeafDecay(); // 空队列 → 早退（若域外格被误入队则此处会清掉远处叶 = 红面）
        ok = ok && c2;
        if (!c2) diag += QStringLiteral("[c1=%1 ctr=%2 hit=%3]").arg(c1)
                            .arg(w.blockAt(22, 80, 40)).arg(hit);
        // 落体：域内着地真 + 负列冻结（Fixed 分支 skip 原样）。
        w.setBlock(34, 80, 20, BR::Stone, 0);
        {
            EntityManager em;
            em.spawnFallingBlock(34, 84, 20, int(BR::Sand));
            em.spawnFallingBlock(-8, 84, -8, int(BR::Sand));
            tickFallingScan(em, w);
            const bool d1 = w.blockAt(34, 81, 20) == BR::Sand;
            const bool d2 = w.blockAt(-8, 81, -8) == BR::Air && aliveFallingScan(em) == 1;
            ok = ok && d1 && d2;
            if (!(d1 && d2))
                diag += QStringLiteral("[d1=%1 d2=%2 alive=%3]").arg(d1).arg(d2)
                            .arg(aliveFallingScan(em));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060d fixed-world zero-change wall (in a fixed world where the whole domain"
               " is the core all four members keep their exact former behavior: an in-domain"
               " explosion excavates while negative-center and out-of-height spheres return"
               " empty, an in-domain sand column collapses on support removal while"
               " out-of-domain cascade calls are zero-effect no-ops, an in-domain leaf decays"
               " across the replicated windows while an out-of-domain decay scan enqueues"
               " nothing, and a falling block lands in-domain but stays frozen in a negative"
               " column)"
            << (ok ? QString() : diag);
    });

    // ── r2060e：FallingBlock 实体负坐标列着地承重墙（级联链 entitymanager 面）──────────────────
    runLeg("r2060e falling-block negative-column landing wall (in a sparse world with a"
        " materialized negative chunk a falling sand entity driven through the real"
        " EntityManager tick pipeline falls through a negative column and lands on its support"
        " with the block restored at the cell above [old-gate red payload: the tick's"
        " negative-column sign skip freezes the entity mid-air forever])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标列（t1090 靶面 = t1089 b/c 员的生产链闸门）：支撑石 (-8,80,-8)，落体自 (-8,84,-8)。
        //   旧门红载荷 = cx<0||cz<0 continue → 实体永冻半空（方块既不清也不着地）。
        w.setBlock(-8, 80, -8, BR::Stone, 0);
        EntityManager em;
        em.spawnFallingBlock(-8, 84, -8, int(BR::Sand));
        tickFallingScan(em, w);
        const bool landed = w.blockAt(-8, 81, -8) == BR::Sand && aliveFallingScan(em) == 0;
        ok = ok && landed;
        if (!landed)
            diag += QStringLiteral("[land b=%1 alive=%2]").arg(w.blockAt(-8, 81, -8))
                        .arg(aliveFallingScan(em));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060e falling-block negative-column landing wall (in a sparse world with a"
               " materialized negative chunk a falling sand entity driven through the real"
               " EntityManager tick pipeline falls through a negative column and lands on its"
               " support with the block restored at the cell above [old-gate red payload: the"
               " tick's negative-column sign skip freezes the entity mid-air forever])"
            << (ok ? QString() : diag);
    });

    // ── r2060f：结构钉（十站点锚注 + 无界盒/包键计数 + 退化盒防御 + t1089/t1074 零变化复钉 + 留池）──
    //   NEG 靶行按恰红设计收账：a 员锚注/无界盒行（NEG-1 摘即红）+ 级联锚注族/y 门行计数/实体锚注
    //   （NEG-2 摘即红）。退化盒防御与 t1089/t1074 复钉与双 NEG 靶行零交集。
    runLeg("r2060f structure pins (each t1090 split site keeps its unique anchor comment"
        " [ten world.cpp sites plus the entitymanager FallingBlock tick site], the sparse"
        " unbounded-box assignments and the sign-preserving leaf-key bit fields appear at"
        " their exact counts, the refloodBox degenerate-box defense from t1074 stays exactly"
        " once, the five t1089 member anchors and the world.h declaration notes stay untouched,"
        " the t1074 sparse unbounded recompute-light box and boundary-read branch stay in"
        " place, and the batch header plus the pool registration and its t1090 consumption note"
        " remain)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile wcf(srcRoot + QStringLiteral("/World/world.cpp"));
        const QString wSrc = wcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wcf.readAll()) : QString();
        QFile whf(srcRoot + QStringLiteral("/World/world.h"));
        const QString hSrc = whf.open(QIODevice::ReadOnly) ? QString::fromUtf8(whf.readAll()) : QString();
        QFile emf(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
        const QString eSrc = emf.open(QIODevice::ReadOnly) ? QString::fromUtf8(emf.readAll()) : QString();
        // (f1) 十站点唯一锚注（NEG 靶行——摘员分流即连锚注一起摘 → count 0 = 红面按站点归因）。
        const QStringList sites = {
            QStringLiteral("t1090 域门（destroySphereSilent 扫描门）两模式分流"),
            QStringLiteral("t1090 盒域（destroySphereSilent 回灌盒）两模式分流"),
            QStringLiteral("t1090 域门（decayLeavesAround 扫描门）两模式分流"),
            QStringLiteral("t1090 盒域（tickLeafDecay 回灌盒）两模式分流"),
            QStringLiteral("t1090 域门（dropGravityColumn）两模式分流"),
            QStringLiteral("t1090 盒域（dropGravityColumn 回灌盒）两模式分流"),
            QStringLiteral("t1090 域门（checkGravityBlockOnEdit）两模式分流"),
            QStringLiteral("t1090 域门（cascadeGravityAround 种子门）两模式分流"),
            QStringLiteral("t1090 邻域门（cascadeGravityAround 26 邻扫描）两模式分流"),
            QStringLiteral("t1090 盒域（cascadeGravityAround 回灌盒）两模式分流"),
            QStringLiteral("t1090 键域（packLeafCell）符号假设收口") };
        int f1Bad = 0;
        QString f1Which;
        for (const QString &s : sites) {
            const int cnt = wSrc.count(s);
            if (cnt != 1) { ++f1Bad; f1Which += QString::number(cnt) + QLatin1Char('x'); }
        }
        const int emAnchor = eSrc.count(QStringLiteral("t1090 域门（FallingBlock tick）两模式分流"));
        const bool f1 = f1Bad == 0 && emAnchor == 1;
        ok = ok && f1;
        if (!f1)
            diag += QStringLiteral("[f1 bad=%1 %2 em=%3]").arg(f1Bad).arg(f1Which).arg(emAnchor);
        // (f2) sparse 无界盒/门行计数（NEG 判别面）：destroySphere+tickLeafDecay 回灌盒负侧赋值行
        //     ==2（NEG-1 摘 a 员 → 1）；级联族三站点 y 同构门行 ==3（NEG-2 摘级联 → 0）；destroySphere
        //     扫描门 y 同构 continue 行 ==1；dropGravity 非批盒无界行 ==1；级联联合盒无界行 ==1。
        const int c2box = wSrc.count(QStringLiteral("bx0 = minX - 1; bz0 = minZ - 1;"));
        const int c2gate = wSrc.count(QStringLiteral("} else if (y < 0 || y >= m_height) {"));
        const int c2scan = wSrc.count(QStringLiteral("} else if (by < 0 || by >= m_height) {"));
        const int c2dg = wSrc.count(QStringLiteral("lx0 = x - R; lx1 = x + R;"));
        const int c2cg = wSrc.count(QStringLiteral("bx0 = m_gravLightX0 - R; bx1 = m_gravLightX1 + R;"));
        const bool f2 = c2box == 2 && c2gate == 3 && c2scan == 1 && c2dg == 1 && c2cg == 1;
        ok = ok && f2;
        if (!f2)
            diag += QStringLiteral("[f2 box=%1 gate=%2 scan=%3 dg=%4 cg=%5]")
                        .arg(c2box).arg(c2gate).arg(c2scan).arg(c2dg).arg(c2cg);
        // (f3) 包键位型计数（键域收口面）：0x3FFFFFFu 掩码 ==4（pack x/z + unpack x/z）。
        const int mask = wSrc.count(QStringLiteral("0x3FFFFFFu"));
        const bool f3 = mask == 4;
        ok = ok && f3;
        if (!f3) diag += QStringLiteral("[f3 mask=%1]").arg(mask);
        // (f4) 退化盒防御钉（t1074 NEG-2 真崩兑出面原样保持——崩溃防线非域假设病灶）。全文件恰 2：
        //     refloodBox 本尊（t1074 面）+ flushPendingLightEdits 同族防御（既有），任一缺席即红。
        const int degen = wSrc.count(QStringLiteral("if (x0 > x1 || y0 > y1 || z0 > z1)"));
        const bool f4 = degen == 2
            && wSrc.contains(QStringLiteral("if (x0 > x1 || y0 > y1 || z0 > z1)\n        return 0;"));
        ok = ok && f4;
        if (!f4) diag += QStringLiteral("[f4 degen=%1 early=%2]").arg(degen)
                            .arg(wSrc.contains(QStringLiteral(
                                "if (x0 > x1 || y0 > y1 || z0 > z1)\n        return 0;")));
        // (f5) t1089 五员零变化复钉（r2059g 承重面回钉）：五员锚注各 count==1 + world.h 声明注 ==6。
        const QStringList t1089Members = {
            QStringLiteral("setBlockFromEntity"), QStringLiteral("setSnowLayerMerge"),
            QStringLiteral("clearBlockSilent"), QStringLiteral("setWaterSilent"),
            QStringLiteral("setBlockSilent") };
        int f5Bad = 0;
        for (const QString &m : t1089Members)
            if (wSrc.count(QStringLiteral("t1089 域门（%1）两模式分流").arg(m)) != 1) ++f5Bad;
        const bool f5 = f5Bad == 0 && hSrc.count(QStringLiteral("t1089：域门两模式分流")) == 6;
        ok = ok && f5;
        if (!f5) diag += QStringLiteral("[f5 bad=%1 h=%2]").arg(f5Bad)
                            .arg(hSrc.count(QStringLiteral("t1089：域门两模式分流")));
        // (f6) t1074 承重复钉（r2047d 面）：recomputeLightAround 无界盒行 ≥1 + sparse 边界读分支 ≥1。
        const bool f6 = wSrc.count(QStringLiteral(
            "x0 = ex - R; x1 = ex + R; z0 = ez - R; z1 = ez + R;")) >= 1
            && wSrc.count(QStringLiteral(
                "} else if (m_chunks.mode() == WorldMode::Sparse) {")) >= 1;
        ok = ok && f6;
        if (!f6) diag += QStringLiteral("[f6 box=%1 br=%2]")
                            .arg(wSrc.count(QStringLiteral(
                                "x0 = ex - R; x1 = ex + R; z0 = ez - R; z1 = ez + R;")))
                            .arg(wSrc.count(QStringLiteral(
                                "} else if (m_chunks.mode() == WorldMode::Sparse) {")));
        // (f7) 批头锚注族 + 留池登记/清偿注（world.cpp）+ entitymanager 留池不修注 + 散布分母钉
        //     （kLeafDecayPct 声明在 world.h——r2060b 确定性窗驱动的字面 1 与声明值互钉）。
        const bool f7 = wSrc.contains(QStringLiteral("t1090 写门家族「核心域假设」同族清偿·后续批"))
            && wSrc.contains(QStringLiteral("t1090 清偿注（后续批落地回填）"))
            && wSrc.contains(QStringLiteral("仍留池"))
            && eSrc.contains(QStringLiteral("留池不修"))
            && hSrc.contains(QStringLiteral("kLeafDecayPct          = 1"));
        ok = ok && f7;
        if (!f7)
            diag += QStringLiteral("[f7 hdr=%1 note=%2 pool=%3 empool=%4 pct=%5]")
                        .arg(wSrc.contains(QStringLiteral("t1090 写门家族「核心域假设」同族清偿·后续批")))
                        .arg(wSrc.contains(QStringLiteral("t1090 清偿注（后续批落地回填）")))
                        .arg(wSrc.contains(QStringLiteral("仍留池")))
                        .arg(eSrc.contains(QStringLiteral("留池不修")))
                        .arg(hSrc.contains(QStringLiteral("kLeafDecayPct          = 1")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2060f structure pins (each t1090 split site keeps its unique anchor comment"
               " [ten world.cpp sites plus the entitymanager FallingBlock tick site], the"
               " sparse unbounded-box assignments and the sign-preserving leaf-key bit fields"
               " appear at their exact counts, the refloodBox degenerate-box defense from t1074"
               " stays exactly once, the five t1089 member anchors and the world.h declaration"
               " notes stay untouched, the t1074 sparse unbounded recompute-light box and"
               " boundary-read branch stay in place, and the batch header plus the pool"
               " registration and its t1090 consumption note remain)"
            << (ok ? QString() : diag);
    });
}
