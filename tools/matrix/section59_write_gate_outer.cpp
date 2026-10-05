#include "matrix_helpers.h"

// t1089 写门五员同族清偿探针段（7 腿；filter 词 r2059；矩阵 762→769）。置尾先例沿用（接 section58，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：t1074 外环光照修复发现的同族病灶第二面——「核心域假设」泄漏进**静默写门家族**。固定盒
// （六比较 x<0 || … || x>=m_width 早退）钳制在 sparse 无限世界把已物化外环 chunk（负坐标/出核）的
// 静默写整体吞掉。本批五员：setBlockSilent / setBlockFromEntity / setSnowLayerMerge / clearBlockSilent /
// setWaterSilent（t1085 刚在 setWaterSilent caller 侧加过固化逻辑——solidifyKeys 四守卫 + noteFluidWrite
// 增量索引收敛语义是承重契约，本单不动其写路径，只分流域门）。修法 = 5 参数 setBlock 写门既定模板
// 逐字同式（world.cpp 七处两模式分流家族同门）：Fixed 分支现行语句原样（零变化墙）/ sparse 分支 y 域
// 两模式同构（有限高）+ x/z 无界（统一物化门 chunkContentPresent 拒未物化，拒写零副作用）。
//
// 现场核实（逐员，核实先行——降级七先例，某员可能非缺口）：五员外环消费场景全部真实可达（玩家位/
// 实体位/索引 tick 驱动）→ **五员全确认零降级**：
//   · setBlockSilent ← 踩踏回土（playercontroller 8448 玩家位 / entitymanager 8828 mob 位）+ 耕地顶
//     放置回土（playercontroller 5388）——玩家/mob 在外环即触发；hoe 经 setBlock 主门已无界 → 负坐标
//     耕地今天就能存在，回土被旧门吞 = 耕地永不回土。
//   · setBlockFromEntity ← FallingBlock 着地（entitymanager 7608/7610/7661）——外环挖支撑触发沙/砾/
//     雪层坍落，着地写被旧门拒但实体照常移除 = 方块凭空消失。⚠️ 生产管线负侧行为被 FallingBlock
//     tick 的 cx<0||cz<0 continue 符号面闸住（远侧=正出核行为已达；负侧须等该面清偿）——本段 b 腿
//     的负侧直调面是门本体的授权面（门对 caller 无感知），该符号面已登记 t1089 留池。
//   · setSnowLayerMerge ← 塌落雪层叠层合并（entitymanager 7643 唯一调用）——外环雪柱坍落合并写被
//     旧门拒 = 合并层数丢失（同 b 员的管线符号面登记）。
//   · clearBlockSilent ← TNT 点火清原块（playercontroller 3985/4695 + entitymanager 5710 水下链式）
//     + 采掘摘矿井箱（clearMineshaftChest ← playercontroller 470）——玩家外环右键 TNT / 挖矿井箱即
//     触发（population 窗口重放在外环 chunk 照常生成矿井）。
//   · setWaterSilent ← 调用面最宽：桶舀水（playercontroller 4062/4073）/ 羊吃草 / 雪傀儡铺雪 / 睡莲
//     撞击 / 探测轨置清位 / 爆炸附着物清理 / 冰冻融化 / 作物甘蔗耕地浆果生长应用 / 画作多格写 /
//     余烬门域清 / 流体 tick 应用 pass——玩家·实体位与索引驱动的格在外环全可达。
//
// 腿面（每员独立腿可单独归因，禁一刀切批修）：
//   r2059a setBlockSilent 外环承重墙（负坐标踩踏回土 Farmland→Dirt 行为级 + 核心域正坐标对照 + 未
//     物化拒 + 无变化早退）；r2059b setBlockFromEntity 外环承重墙（**出核远 chunk 行为级沙落**：
//     EntityManager 真管线下落体着地 Sand 落格 [旧门红载荷 = 方块凭空消失] + 远 chunk 直调 + 负坐标
//     直调授权面 + 未物化拒 + occ 守卫保持）；r2059c setSnowLayerMerge 外环承重墙（**出核远 chunk
//     行为级雪层塌落合并**：FallingBlockState(SnowLayer) 落既有层 state 0+携带 2 层 → 合并 state 3
//     + 负坐标直调 + 非 SnowLayer 防御拒 + 未物化拒）；r2059d clearBlockSilent 外环承重墙（负坐标
//     TNT 点火清原块 + 核心域对照 + 未物化拒 + y 域门）；r2059e setWaterSilent 外环承重墙（负坐标
//     舀水 Water→Air + 非流体系统写 Grass→Dirt [羊吃草同形] + 无变化早退 [t1085 收敛守卫保持面] +
//     未物化拒 + y 域门 + 核心域对照）；r2059f **fixed 世界零变化墙**（48×48×96 s82 fixed 全域=核心
//     域：五员域内写真 + 域外（负坐标/≥dims/y 域外）恒拒零副作用——Fixed 分支逐字原样的行为级）；
//     r2059g 结构钉（五员 t1089 锚注各 count==1 [NEG 靶行——摘员分流即红] + sparse 统一物化门行
//     count==7 [2 setBlock + 5 本批；y 域门行为多族共享基建文本不作计数钉] + 批头锚注族[五员全
//     确认零降级/留池登记] + world.h 声明注 count==6）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2059_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘 setBlockFromEntity + setSnowLayerMerge 两员分流（域门整体还原旧六比较固定盒两行 +
//     摘除 t1089 锚注）→ **恰红 = {r2059b, r2059c, r2059g: b/c 锚注子面 + g3 计数子面}**：
//     b/c 行为级红载荷 = 远 chunk 沙落不落格 / 雪层合并不写（旧门拒）；g1 相应两锚注 count 0；
//     g3 sparse 物化门行 7→5。a/d/e/f 不误伤（未动）；g4/g5 不误伤（批头/声明注未动）。
//   NEG-2 摘 setWaterSilent 分流（同式还原 + 摘锚注）→ **恰红 = {r2059e, r2059g: e 锚注子面 +
//     g3 计数子面}**：e 红载荷 = 负坐标舀水恒 false 水格不退；g1 e 锚注 count 0；g3 7→6。
//     a/b/c/d/f 不误伤（未动）。
//
// rig 纪律：sparse 构造缝 `World w{ {82, 48, 48, 96, 0} }`（SparseWorldParams 零预生成，section54
//   r2053c 同门）+ loadChunkAt 按需物化（r2047c 负侧先例同门）；rig 坪取 y=79/80 高空（s82 地形/
//   树冠带之上，section54 y=80 零 worldgen 接触先例）；rig 基建全经 setBlock 主门（sparse 分支已
//   无界，负坐标写入 = r2047 时代已证面）；坐标不作哨兵；行为级腿不查光照/信号只查栅格终态。

namespace {

// fixed 宿主小世界 incantation（section58 同款四 setter）。
inline void initFixedWallWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 推进下落体管线（section02 同款 tick 形态；600 拍 ≈ 9.6s 模拟时——3 格落差充足裕量）。
inline void tickFalling(EntityManager &em, World &w)
{
    const QVector3D farListener(4.5f, 80.5f, 4.5f);
    for (int t = 0; t < 600; ++t)
        em.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
}

inline int aliveFalling(const EntityManager &em)
{
    int n = 0;
    for (int i = 0; i < em.count(); ++i)
        if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::FallingBlock)) ++n;
    return n;
}

} // namespace

void MatrixRun::section59_write_gate_outer()
{
    // ── r2059a：setBlockSilent 外环承重墙（负坐标踩踏回土行为级 + 对照 + 未物化拒 + 无变化早退）──
    runLeg("r2059a setBlockSilent outer-ring wall (in a sparse world with a materialized negative"
        " chunk the silent trample revert writes farmland back to dirt at a negative coordinate and"
        " reports true, the core-domain control cell reverts the same way, an unmaterialized"
        " chunk coordinate is rejected with zero side effect, and rewriting the same id+state"
        " early-returns false [no-change guard intact])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } }; // sparse 构造缝：核心 48×48 h96 s82 零预生成
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标面（t1089 靶面）：Farmland（hoe 经 setBlock 主门置——主门已无界，负坐标耕地今天可达）
        //   → setBlockSilent 踩踏回土 Dirt。旧门红载荷 = 恒 false + 耕地永不回土。
        w.setBlock(-8, 80, -8, BR::Farmland, 0);
        const bool negRet = w.setBlockSilent(-8, 80, -8, BR::Dirt, 0);
        const bool negOk = negRet && w.blockAt(-8, 80, -8) == BR::Dirt;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg ret=%1 b=%2]").arg(negRet).arg(w.blockAt(-8, 80, -8));
        // 核心域正坐标对照（域内基本行为，旧门两态皆过——分离「sparse 模式工作」与「外环域门」）。
        w.setBlock(4, 80, 4, BR::Farmland, 0);
        const bool posRet = w.setBlockSilent(4, 80, 4, BR::Dirt, 0);
        const bool posOk = posRet && w.blockAt(4, 80, 4) == BR::Dirt;
        ok = ok && posOk;
        if (!posOk)
            diag += QStringLiteral("[pos ret=%1 b=%2]").arg(posRet).arg(w.blockAt(4, 80, 4));
        // 未物化拒（统一物化门面：无界 ≠ 全收——chunk (-3,-3) 从未物化 → 拒 + 零副作用）。
        const bool rejRet = w.setBlockSilent(-40, 80, -40, BR::Dirt, 0);
        const bool rejOk = !rejRet && w.blockAt(-40, 80, -40) == BR::Air;
        ok = ok && rejOk;
        if (!rejOk)
            diag += QStringLiteral("[rej ret=%1 b=%2]").arg(rejRet).arg(w.blockAt(-40, 80, -40));
        // 无变化早退（同 id+state 再写 → false；域门放行后的收敛守卫保持面）。
        const bool ncOk = !w.setBlockSilent(-8, 80, -8, BR::Dirt, 0);
        ok = ok && ncOk;
        if (!ncOk) diag += QStringLiteral("[nochange ret=true] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059a setBlockSilent outer-ring wall (in a sparse world with a materialized"
               " negative chunk the silent trample revert writes farmland back to dirt at a"
               " negative coordinate and reports true, the core-domain control cell reverts the"
               " same way, an unmaterialized chunk coordinate is rejected with zero side effect,"
               " and rewriting the same id+state early-returns false [no-change guard intact])"
            << (ok ? QString() : diag);
    });

    // ── r2059b：setBlockFromEntity 外环承重墙（出核远 chunk 行为级沙落 + 直调两面 + 拒面 + occ 保持）──
    runLeg("r2059b setBlockFromEntity outer-ring wall (a falling sand entity driven through the"
        " real EntityManager pipeline lands beyond the streaming core and the block appears at"
        " the landing cell [old-gate red payload: the entity is removed yet the block vanishes],"
        " a direct far-chunk gate call places gravel, a direct negative-coordinate gate call"
        " places sand as the caller-agnostic authority face [production negative-side behavior"
        " stays gated by the registered FallingBlock tick sign-face pool item], an"
        " unmaterialized coordinate is rejected, and the air/water occupancy guard still"
        " rejects an occupied target)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(6, 6) && w.loadChunkAt(-1, -1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 行为级（出核远 chunk）：chunk (6,6) = 世界 x 96..111（核心 48 = chunk 0..2 之外 3 chunk）。
        //   支撑坪 → 真管线下落体 → 着地写经 setBlockFromEntity(100,81,100)。旧门红载荷 = x>=48 恒拒
        //   → 实体照常移除而方块凭空消失（t1089 病灶主诉形态）。
        w.setBlock(100, 80, 100, BR::Stone, 0);
        EntityManager em;
        em.spawnFallingBlock(100, 84, 100, int(BR::Sand));
        tickFalling(em, w);
        const bool landed = w.blockAt(100, 81, 100) == BR::Sand && aliveFalling(em) == 0;
        ok = ok && landed;
        if (!landed)
            diag += QStringLiteral("[land b=%1 alive=%2]").arg(w.blockAt(100, 81, 100)).arg(aliveFalling(em));
        // 远 chunk 直调门面：(104,81,104) 空气格 → 砾着地（同语义直写，非管线复现）。
        w.setBlock(104, 80, 104, BR::Stone, 0);
        const bool farRet = w.setBlockFromEntity(104, 81, 104, BR::Gravel);
        const bool farOk = farRet && w.blockAt(104, 81, 104) == BR::Gravel;
        ok = ok && farOk;
        if (!farOk)
            diag += QStringLiteral("[far ret=%1 b=%2]").arg(farRet).arg(w.blockAt(104, 81, 104));
        // 负坐标直调授权面（门本体对 caller 无感知；生产负侧管线行为由留池的 FallingBlock tick
        //   符号面闸住——登记面不硬扛，门面按两模式契约断言）。
        const bool negRet = w.setBlockFromEntity(-12, 81, -12, BR::Sand);
        const bool negOk = negRet && w.blockAt(-12, 81, -12) == BR::Sand;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg ret=%1 b=%2]").arg(negRet).arg(w.blockAt(-12, 81, -12));
        // 未物化拒（chunk (-3,-3) Absent → false 零副作用）。
        const bool rejOk = !w.setBlockFromEntity(-40, 81, -40, BR::Sand);
        ok = ok && rejOk;
        if (!rejOk) diag += QStringLiteral("[rej ret=true] ");
        // occ 守卫保持面（两模式分流不触守卫）：目标格 Stone → false。
        const bool occOk = !w.setBlockFromEntity(104, 80, 104, BR::Sand);
        ok = ok && occOk;
        if (!occOk) diag += QStringLiteral("[occ ret=true] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059b setBlockFromEntity outer-ring wall (a falling sand entity driven through"
               " the real EntityManager pipeline lands beyond the streaming core and the block"
               " appears at the landing cell [old-gate red payload: the entity is removed yet the"
               " block vanishes], a direct far-chunk gate call places gravel, a direct"
               " negative-coordinate gate call places sand as the caller-agnostic authority face"
               " [production negative-side behavior stays gated by the registered FallingBlock"
               " tick sign-face pool item], an unmaterialized coordinate is rejected, and the"
               " air/water occupancy guard still rejects an occupied target)"
            << (ok ? QString() : diag);
    });

    // ── r2059c：setSnowLayerMerge 外环承重墙（出核远 chunk 行为级雪层塌落合并 + 直调/防御/拒面）────
    runLeg("r2059c setSnowLayerMerge outer-ring wall (a falling snow-layer entity carrying 3"
        " layers collapses onto an existing one-layer snow layer beyond the streaming core and"
        " the merged stack keeps 4 layers as state 3 [old-gate red payload: the falling entity"
        " is removed and the merge is lost], a direct negative-coordinate merge writes state 4,"
        " merging onto a non-snow-layer cell is refused, and an unmaterialized coordinate is"
        " rejected)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(6, 6) && w.loadChunkAt(-1, -1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 行为级（出核远 chunk）：既有雪层 state 0（1 层）于 (100,80,100)，携 3 层（state 2）塌落体
        //   自 (100,84,100) 落 → 合并 = min(1+3, 8) = 4 层 → state 3。旧门红载荷 = 合并写被拒，
        //   下落体照常移除 = 层数丢失。
        w.setBlock(100, 79, 100, BR::Stone, 0);
        w.setBlock(100, 80, 100, BR::SnowLayer, 0);
        EntityManager em;
        em.spawnFallingBlockState(100, 84, 100, int(BR::SnowLayer), 2);
        tickFalling(em, w);
        const bool merged = w.blockAt(100, 80, 100) == BR::SnowLayer
            && w.stateAt(100, 80, 100) == 3 && aliveFalling(em) == 0;
        ok = ok && merged;
        if (!merged)
            diag += QStringLiteral("[merge b=%1 st=%2 alive=%3]").arg(w.blockAt(100, 80, 100))
                        .arg(w.stateAt(100, 80, 100)).arg(aliveFalling(em));
        // 负坐标直调授权面：( -12,81,-12 ) 既有雪层 state 1（2 层）→ 合并写 state 4（5 层）。
        w.setBlock(-12, 80, -12, BR::Stone, 0);
        w.setBlock(-12, 81, -12, BR::SnowLayer, 1);
        const bool negRet = w.setSnowLayerMerge(-12, 81, -12, quint8(4));
        const bool negOk = negRet && w.blockAt(-12, 81, -12) == BR::SnowLayer
            && w.stateAt(-12, 81, -12) == 4;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg ret=%1 st=%2]").arg(negRet).arg(w.stateAt(-12, 81, -12));
        // 防御拒保持面（两模式分流不触防御）：非 SnowLayer 格 → false。
        const bool defOk = !w.setSnowLayerMerge(-12, 80, -12, quint8(2)); // 该格 Stone
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def ret=true] ");
        // 未物化拒。
        const bool rejOk = !w.setSnowLayerMerge(-40, 81, -40, quint8(2));
        ok = ok && rejOk;
        if (!rejOk) diag += QStringLiteral("[rej ret=true] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059c setSnowLayerMerge outer-ring wall (a falling snow-layer entity carrying 3"
               " layers collapses onto an existing one-layer snow layer beyond the streaming core"
               " and the merged stack keeps 4 layers as state 3 [old-gate red payload: the"
               " falling entity is removed and the merge is lost], a direct negative-coordinate"
               " merge writes state 4, merging onto a non-snow-layer cell is refused, and an"
               " unmaterialized coordinate is rejected)"
            << (ok ? QString() : diag);
    });

    // ── r2059d：clearBlockSilent 外环承重墙（负坐标点火清原块 + 对照 + 未物化拒 + y 域门）──────────
    runLeg("r2059d clearBlockSilent outer-ring wall (in a sparse world with a materialized"
        " negative chunk the ignition-purpose silent clear removes a TNT block at a negative"
        " coordinate and reports true, the core-domain control cell clears the same way, an"
        " unmaterialized coordinate is rejected, and the y-domain gate still refuses an"
        " out-of-height coordinate [two-mode-isomorphic finite height])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标面（t1089 靶面）：TNT 于 (-8,80,-8) → 点火清原块（t490fix 语义：无条件覆盖 Air）。
        //   旧门红载荷 = 恒 false → 引燃态实体与原 TNT 方块同格叠加。
        w.setBlock(-8, 80, -8, BR::TntBlock, 0);
        const bool negRet = w.clearBlockSilent(-8, 80, -8);
        const bool negOk = negRet && w.blockAt(-8, 80, -8) == BR::Air;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg ret=%1 b=%2]").arg(negRet).arg(w.blockAt(-8, 80, -8));
        // 核心域正坐标对照。
        w.setBlock(4, 80, 4, BR::TntBlock, 0);
        const bool posRet = w.clearBlockSilent(4, 80, 4);
        const bool posOk = posRet && w.blockAt(4, 80, 4) == BR::Air;
        ok = ok && posOk;
        if (!posOk)
            diag += QStringLiteral("[pos ret=%1 b=%2]").arg(posRet).arg(w.blockAt(4, 80, 4));
        // 未物化拒。
        const bool rejOk = !w.clearBlockSilent(-40, 80, -40);
        ok = ok && rejOk;
        if (!rejOk) diag += QStringLiteral("[rej ret=true] ");
        // y 域门（两模式同构有限高）：y=96 → false。
        const bool yOk = !w.clearBlockSilent(-8, 96, -8);
        ok = ok && yOk;
        if (!yOk) diag += QStringLiteral("[y ret=true] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059d clearBlockSilent outer-ring wall (in a sparse world with a materialized"
               " negative chunk the ignition-purpose silent clear removes a TNT block at a"
               " negative coordinate and reports true, the core-domain control cell clears the"
               " same way, an unmaterialized coordinate is rejected, and the y-domain gate still"
               " refuses an out-of-height coordinate [two-mode-isomorphic finite height])"
            << (ok ? QString() : diag);
    });

    // ── r2059e：setWaterSilent 外环承重墙（负坐标舀水/非流体写 + 无变化早退 [t1085 守卫保持面] + 拒面）──
    runLeg("r2059e setWaterSilent outer-ring wall (in a sparse world with a materialized negative"
        " chunk the universal silent state write scoops a water source to air at a negative"
        " coordinate and reports true, a non-fluid system write grass-to-dirt lands the same way"
        " [sheep-eat shape], rewriting the same id+state early-returns false [t1085 no-change"
        " convergence guard intact], an unmaterialized coordinate and an out-of-height y are"
        " rejected, and the core-domain control scoop still works)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标舀水面（playercontroller 空桶同形：setWaterSilent(Air)）。旧门红载荷 = 恒 false，
        //   外环水舀不动。
        w.setBlock(-8, 80, -8, BR::Water, 0);
        const bool scoopRet = w.setWaterSilent(-8, 80, -8, BR::Air, 0);
        const bool scoopOk = scoopRet && w.blockAt(-8, 80, -8) == BR::Air;
        ok = ok && scoopOk;
        if (!scoopOk)
            diag += QStringLiteral("[scoop ret=%1 b=%2]").arg(scoopRet).arg(w.blockAt(-8, 80, -8));
        // 非流体系统写面（羊吃草同形 Grass→Dirt——通用静默 state 写入口语义）。
        w.setBlock(-12, 80, -12, BR::Grass, 0);
        const bool dirtRet = w.setWaterSilent(-12, 80, -12, BR::Dirt, 0);
        const bool dirtOk = dirtRet && w.blockAt(-12, 80, -12) == BR::Dirt;
        ok = ok && dirtOk;
        if (!dirtOk)
            diag += QStringLiteral("[dirt ret=%1 b=%2]").arg(dirtRet).arg(w.blockAt(-12, 80, -12));
        // 无变化早退（t1085 收敛守卫保持面：稳态零写入 → 停扫语义的域内形态）。
        const bool ncOk = !w.setWaterSilent(-12, 80, -12, BR::Dirt, 0);
        ok = ok && ncOk;
        if (!ncOk) diag += QStringLiteral("[nochange ret=true] ");
        // 未物化拒 + y 域门。
        const bool rejOk = !w.setWaterSilent(-40, 80, -40, BR::Water, 0)
            && !w.setWaterSilent(-8, 96, -8, BR::Air, 0);
        ok = ok && rejOk;
        if (!rejOk) diag += QStringLiteral("[rej ret=true] ");
        // 核心域正坐标对照。
        w.setBlock(4, 80, 4, BR::Water, 0);
        const bool posRet = w.setWaterSilent(4, 80, 4, BR::Air, 0);
        const bool posOk = posRet && w.blockAt(4, 80, 4) == BR::Air;
        ok = ok && posOk;
        if (!posOk)
            diag += QStringLiteral("[pos ret=%1 b=%2]").arg(posRet).arg(w.blockAt(4, 80, 4));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059e setWaterSilent outer-ring wall (in a sparse world with a materialized"
               " negative chunk the universal silent state write scoops a water source to air at"
               " a negative coordinate and reports true, a non-fluid system write grass-to-dirt"
               " lands the same way [sheep-eat shape], rewriting the same id+state early-returns"
               " false [t1085 no-change convergence guard intact], an unmaterialized coordinate"
               " and an out-of-height y are rejected, and the core-domain control scoop still"
               " works)"
            << (ok ? QString() : diag);
    });

    // ── r2059f：fixed 世界零变化墙（五员域内写真 + 域外恒拒零副作用——Fixed 分支逐字原样的行为级）──
    runLeg("r2059f fixed-world zero-change wall (in a fixed world where the whole domain is the"
        " core all five silent write gates keep their exact former behavior: in-domain writes"
        " succeed with their effect and out-of-domain coordinates - negative, at or beyond the"
        " world dimensions, and out-of-height - are rejected for every member with zero side"
        " effects, pinning the verbatim fixed-domain branch at behavior level)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedWallWorld(w);
        for (int x = 16; x <= 31; ++x)
            for (int z = 16; z <= 31; ++z)
                w.setBlock(x, 79, z, BR::Stone, 0); // 高空坪（地形/树冠带之上，section58 同式 grounding）
        // setBlockSilent：域内真 + 域外（负/≥dims/y 域外）恒拒。
        const bool a1 = w.setBlockSilent(20, 80, 20, BR::Dirt, 0)
            && w.blockAt(20, 80, 20) == BR::Dirt;
        const bool a2 = !w.setBlockSilent(-1, 80, 20, BR::Dirt, 0)
            && !w.setBlockSilent(20, 80, -1, BR::Dirt, 0)
            && !w.setBlockSilent(48, 80, 20, BR::Dirt, 0)
            && !w.setBlockSilent(20, 80, 48, BR::Dirt, 0)
            && !w.setBlockSilent(20, -1, 20, BR::Dirt, 0)
            && !w.setBlockSilent(20, 96, 20, BR::Dirt, 0)
            && w.blockAt(-1, 80, 20) == BR::Air && w.blockAt(48, 80, 20) == BR::Air;
        ok = ok && a1 && a2;
        if (!(a1 && a2))
            diag += QStringLiteral("[a1=%1 a2=%2]").arg(a1).arg(a2);
        // setBlockFromEntity：域内 occ 内写真 + 域外恒拒。
        const bool b1 = w.setBlockFromEntity(21, 80, 20, BR::Sand)
            && w.blockAt(21, 80, 20) == BR::Sand;
        const bool b2 = !w.setBlockFromEntity(-1, 80, 20, BR::Sand)
            && !w.setBlockFromEntity(48, 80, 20, BR::Sand)
            && !w.setBlockFromEntity(21, -1, 20, BR::Sand)
            && !w.setBlockFromEntity(21, 96, 20, BR::Sand);
        ok = ok && b1 && b2;
        if (!(b1 && b2))
            diag += QStringLiteral("[b1=%1 b2=%2]").arg(b1).arg(b2);
        // setSnowLayerMerge：域内既有层合并真 + 域外恒拒。
        w.setBlock(22, 80, 20, BR::SnowLayer, 0);
        const bool c1 = w.setSnowLayerMerge(22, 80, 20, quint8(3))
            && w.stateAt(22, 80, 20) == 3;
        const bool c2 = !w.setSnowLayerMerge(-1, 80, 20, quint8(3))
            && !w.setSnowLayerMerge(48, 80, 20, quint8(3))
            && !w.setSnowLayerMerge(22, 96, 20, quint8(3));
        ok = ok && c1 && c2;
        if (!(c1 && c2))
            diag += QStringLiteral("[c1=%1 c2=%2]").arg(c1).arg(c2);
        // clearBlockSilent：域内清块真 + 域外恒拒。
        w.setBlock(23, 80, 20, BR::TntBlock, 0);
        const bool d1 = w.clearBlockSilent(23, 80, 20) && w.blockAt(23, 80, 20) == BR::Air;
        const bool d2 = !w.clearBlockSilent(-1, 80, 20)
            && !w.clearBlockSilent(48, 80, 20)
            && !w.clearBlockSilent(23, -1, 20);
        ok = ok && d1 && d2;
        if (!(d1 && d2))
            diag += QStringLiteral("[d1=%1 d2=%2]").arg(d1).arg(d2);
        // setWaterSilent：域内舀水真 + 域外恒拒 + 无变化早退保持。
        w.setBlock(24, 80, 20, BR::Water, 0);
        const bool e1 = w.setWaterSilent(24, 80, 20, BR::Air, 0)
            && w.blockAt(24, 80, 20) == BR::Air;
        const bool e2 = !w.setWaterSilent(-1, 80, 20, BR::Air, 0)
            && !w.setWaterSilent(24, 80, 48, BR::Air, 0)
            && !w.setWaterSilent(24, 96, 20, BR::Air, 0);
        w.setBlock(25, 80, 20, BR::Water, 0);
        const bool e3 = !w.setWaterSilent(25, 80, 20, BR::Water, 0); // 无变化（同 id 同 state）
        ok = ok && e1 && e2 && e3;
        if (!(e1 && e2 && e3))
            diag += QStringLiteral("[e1=%1 e2=%2 e3=%3]").arg(e1).arg(e2).arg(e3);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059f fixed-world zero-change wall (in a fixed world where the whole domain is"
               " the core all five silent write gates keep their exact former behavior: in-domain"
               " writes succeed with their effect and out-of-domain coordinates - negative, at or"
               " beyond the world dimensions, and out-of-height - are rejected for every member"
               " with zero side effects, pinning the verbatim fixed-domain branch at behavior"
               " level)"
            << (ok ? QString() : diag);
    });

    // ── r2059g：结构钉（五员锚注 + 两模式分流行计数 + 批头锚注族 + world.h 声明注族）──────────────
    //   NEG 靶行按恰红设计收账：员锚注（NEG-1 摘 b/c / NEG-2 摘 e 即红）+ sparse 门行计数（两 NEG
    //   均红）。批头/声明注族与双 NEG 靶行零交集。
    runLeg("r2059g structure pins (each of the five members keeps its unique t1089 two-mode"
        " split anchor comment, the sparse materialization-gate line appears exactly seven"
        " times [two pre-existing setBlock splits plus the five members of this batch], the"
        " batch header anchors [five-member verification with zero degradations and the pool"
        " registration] stay in place, and the world.h declaration notes appear for all five"
        " members plus the pair note)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile wcf(srcRoot + QStringLiteral("/World/world.cpp"));
        const QString wSrc = wcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wcf.readAll()) : QString();
        QFile whf(srcRoot + QStringLiteral("/World/world.h"));
        const QString hSrc = whf.open(QIODevice::ReadOnly) ? QString::fromUtf8(whf.readAll()) : QString();
        // (g1) 五员唯一锚注（NEG 靶行——摘员分流即连锚注一起摘 → count 0 = 红面按员归因）。
        const QStringList members = {
            QStringLiteral("setBlockFromEntity"), QStringLiteral("setSnowLayerMerge"),
            QStringLiteral("clearBlockSilent"), QStringLiteral("setWaterSilent"),
            QStringLiteral("setBlockSilent") };
        int g1Bad = 0;
        QString g1Which;
        for (const QString &m : members) {
            const int cnt = wSrc.count(QStringLiteral("t1089 域门（%1）两模式分流").arg(m));
            if (cnt != 1) { ++g1Bad; g1Which += m + QLatin1Char('=') + QString::number(cnt) + QLatin1Char(' '); }
        }
        const bool g1 = g1Bad == 0;
        ok = ok && g1;
        if (!g1) diag += QStringLiteral("[g1 bad=%1 %2]").arg(g1Bad).arg(g1Which);
        // (g2) Fixed 分支逐字原样面：六比较固定盒行在场（12 = 2 既有 setBlock + 5 本批 Fixed 分支
        //     + 5 留池单行门——行文本同串；摘员分流还原旧两行仍含该串 → 非 NEG 判别面，判别归 g1/g3）。
        const bool g2 = wSrc.count(QStringLiteral(
            "if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)")) == 12;
        ok = ok && g2;
        if (!g2)
            diag += QStringLiteral("[g2 cnt=%1]").arg(wSrc.count(QStringLiteral(
                "if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)")));
        // (g3) 两模式分流行计数（NEG 判别面）：sparse 统一物化门行 == 7（2 setBlock + 5 本批）。
        //     该行是写门 sparse 分支的独有标记（读面用 chunkMaterialized 门，不同串）。y 域门行
        //     「if (y < 0 || y >= m_height)」为多族共享基建文本（本批 5 + 既有 10）不作计数钉。
        //     NEG-1 摘两员 → 5；NEG-2 摘一员 → 6。
        const int gateCnt = wSrc.count(QStringLiteral(
            "chunkContentPresent(floorDiv(x, Chunk::kSize), floorDiv(z, Chunk::kSize))"));
        const bool g3 = gateCnt == 8; // t1135 lawful 前移：7→8（活塞写格域门行 pistonCellWritable 追加——统一物化门谓词同串新员，写门家族行随追加前移）
        ok = ok && g3;
        if (!g3) diag += QStringLiteral("[g3 gate=%1]").arg(gateCnt);
        // (g4) 批头锚注族（降级员登记钉 + 留池登记钉）：五员全确认零降级 + 留池登记在场。
        const bool g4 = wSrc.contains(QStringLiteral("t1089 写门五员同族清偿"))
            && wSrc.contains(QStringLiteral("五员全\n//   确认零降级"))
            && wSrc.contains(QStringLiteral("留池登记（禁顺手修，后续批另立单）"));
        ok = ok && g4;
        if (!g4)
            diag += QStringLiteral("[g4 hdr=%1 zero=%2 pool=%3]")
                        .arg(wSrc.contains(QStringLiteral("t1089 写门五员同族清偿")))
                        .arg(wSrc.contains(QStringLiteral("五员全\n//   确认零降级")))
                        .arg(wSrc.contains(QStringLiteral("留池登记（禁顺手修，后续批另立单）")));
        // (g5) world.h 声明注族：五员各一条 t1089 域门注（4 参数 setBlockFromEntity 携全式、5 参数
        //     携同 4 参数式、merge 携同式、clear/water/silent 携全式）= 6 处。
        const bool g5 = hSrc.count(QStringLiteral("t1089：域门两模式分流")) == 6;
        ok = ok && g5;
        if (!g5)
            diag += QStringLiteral("[g5 cnt=%1]").arg(hSrc.count(QStringLiteral("t1089：域门两模式分流")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2059g structure pins (each of the five members keeps its unique t1089 two-mode"
               " split anchor comment, the sparse materialization-gate line appears exactly seven"
               " times [two pre-existing setBlock splits plus the five members of this batch], the"
               " batch header anchors [five-member verification with zero degradations and the pool"
               " registration] stay in place, and the world.h declaration notes appear for all five"
               " members plus the pair note)"
            << (ok ? QString() : diag);
    });
}
