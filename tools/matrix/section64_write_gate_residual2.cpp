#include "matrix_helpers.h"

// t1094 写门家族「核心域假设」同族清偿·残面批探针段（7 腿；filter 词 r2064；矩阵 790→797）。
// 置尾先例沿用（接 section63，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：t1091 批头留池四组残面逐组核实后清偿（现场核实 = 调用面盘点 → 外环可达性 → 旧门后果
//   形态，四组全确认真缺口零降级）：①tickRedstone inBounds（recomputePowerLocal 统一域 lambda——
//   Phase A seedDust 播种 / Phase B addReceiver 登记 / addTorch 反相与全部回插面的负侧收口；
//   m_powerDirty 回读链：notePowerWrite 经 t1091 保符号键把已物化外环真实格灌进脏集，本 lambda 却把
//   它们幻影化丢弃）；②goldenRailChainStep px/pz 盒（链 BFS / 熄灭波前 / 反向走查的负侧步进恒断）；
//   ③recheckAttachments ① 族子钩子/柱坍盒门（清单十员：checkDeadBush/FlowerMushroom/PressurePlate/
//   SnowLayer/TrapdoorDoor/Painting/checkRailOnEdit 失撑分支/dropCactusColumn/dropSugarcaneColumn/
//   checkFireOnEdit 6 邻扫门 + 现场核实新增链承重面两员：checkCactusOnEdit ④ 邻接碎柱邻扫门是
//   dropCactusColumn 的外环可达承重面、dropUnsupportedDoorsAbove 是 checkTrapdoorDoorSupportOnEdit
//   判定面的级联执行收口——t1090 合并清偿先例「只开一半 = 半开状态」）；④entitymanager mob 火/岩浆
//   接触足印扫描 worldW/worldD 盒（AABB 主扫 + 站顶分支——sparse 下 aiClampDomainMax 哨兵使正侧早已
//   无界、负侧 nx<0/nz<0 仍被钳；中心列快速路径不受影响，与 t1091 留池注口径一致）。
//
// 修法选型（留痕）：红石/金轨盒 = 扫描门无界化（t1090 裁定先例——读写均经物化门，per-cell
//   chunkContentPresent 重复单一权威零行为增益，r2059g 物化门行计数钉 7 不动）；附着复检族 = t1091
//   扫描门同型（Fixed 分支现行语句原样——零变化墙；sparse = y 域两模式同构（有限高）+ x/z 无界）；
//   mob 足印扫描 = t1090 FallingBlock 模板（!world->isSparse() 分流，Fixed 原样）。数值 id 零改动、
//   零图集、零 QML 玩法路径迁移；不碰 t1089/t1090/t1091 已修站点（r2059/r2060/r2061 族腿回归承重）。
//
// 腿面（逐组/逐员专属可单独归因，禁一刀切批修）：
//   r2064a 红石外环承重墙（负坐标红石块+粉+灯经真 tickRedstone 点亮 [旧门红载荷 = inBounds lambda
//     拒负 → 粉域/接收器登记整体幻影化 → 灯不亮] + 负坐标动力轨链链步点亮第二根 [旧门红载荷 =
//     goldenRailChainStep 负侧步进断 → 链第二根永不亮] + 核心域对照）；r2064b 柱坍族承重墙（负坐标
//     仙人掌/甘蔗整柱失撑坍落 [旧门红载荷 = 柱坍函数盒门 → 柱残留悬空]）；r2064c 单格附着族承重墙
//     （负坐标蘑菇/压力板/枯灌木失撑脱落 [旧门红载荷 = 各员盒门 → 附着物残留]）；r2064d 板/雪/轨/画/
//     火族承重墙（负坐标活板门级联掉落 + 雪层柱坍 + 铁轨失撑掉落 + 画作整画掉落 + 立地火失撑即时熄
//     [旧门红载荷 = 各员盒门 → 残留旧态]）；r2064e mob 火/岩浆足印承重墙（负坐标燃块旁 mob 足印
//     列接触点燃扣血 + 核心域同构对照 [旧门红载荷 = AABB 门钳负列 → 恒不点燃]）；r2064f **fixed 世界
//     零变化墙**（域内红石电路点亮 + mob 火接触点燃回归柱 + 域外写恒拒 + 负列 mob 冻结血量恒满）；
//     r2064g 结构钉（十九站点锚注各 count==1 + 批头/清偿注锚族 + t1094 域门/邻扫门词根计数 + r2059/
//     r2060/r2061 承重复钉）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2064_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘组①②④（recomputePowerLocal inBounds lambda 还原旧六比较单行体 + goldenRailChainStep
//     还原旧盒门 + mob AABB/站顶两门还原旧无条件钳 + 摘锚注①②④）→ **恰红 = {r2064a, r2064e,
//     r2064g: a 红石/金轨行为面 + e mob 行为面 + g 对应锚注与词根计数子面}**。b/c/d/f 不误伤（f 是
//     fixed 零变化墙——fixed 行为在摘/不摘下逐位同，天然免疫 sparse NEG）。
//   NEG-2 摘组③（还原组 3 全部子钩子/柱坍/链面盒门 + 摘对应锚注）→ **恰红 = {r2064b, r2064c,
//     r2064d, r2064g: b/c/d 附着复检行为面 + g 对应锚注与词根计数子面}**。a/e/f 不误伤。
//
// rig 纪律：sparse 构造缝 `World w{ {82, 48, 48, 96, 0} }`（section54 r2053c / section59-61 同门）+
//   loadChunkAt 按需物化；rig 坪取 y=79..84 高空（s82 地形/树冠带之上）；rig 基建全经 setBlock 主门
//   （sparse 分支已无界）；行为级腿不查信号只查栅格终态与实体血量；坐标不作哨兵。

namespace {

// fixed 宿主小世界 incantation（section61 同款四 setter；默认构造 World 原地配置）。
inline void initFixedResidual2World(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 推进实体 / mob 管线（section61 同款 tick 形态；200 拍 = 3.2s 模拟时 ≈ 4 个火伤窗）。
inline void tickEntityResidual2(EntityManager &em, World &w, int frames)
{
    const QVector3D farListener(4.5f, 80.5f, 4.5f);
    for (int t = 0; t < frames; ++t)
        em.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
}

} // namespace

void MatrixRun::section64_write_gate_residual2()
{
    // ── r2064a：红石外环承重墙（负坐标电路点亮 + 负坐标动力轨链链步点亮 + 核心域对照）──────────
    runLeg("r2064a redstone outer-ring wall (in a sparse world with a materialized negative"
        " chunk a redstone block feeding dust feeding a lamp on negative coordinates lights the"
        " lamp through the real tickRedstone pipeline [old-gate red payload: the inBounds lambda"
        " drops negative anchors and neighbours so the dust region stays empty and the lamp never"
        " switches on], and a powered rail chain on negative coordinates propagates the powered"
        " bit to the second rail [old-gate red payload: the chain-step box rejects the negative"
        " step so the second rail stays dark]; the same two circuits stay green in the core"
        " domain as the control)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 灯面：红石块 (-8,81,-8) → 粉 (-7,81,-8) → 灯 (-6,81,-8)，y=81 高空石坪。
        //   旧门红载荷 = seedDust/addReceiver 的 inBounds 拒负 → region/receivers 空 → 灯不亮。
        for (int x = -9; x <= -6; ++x)
            for (int z = -9; z <= -6; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(-8, 81, -8, BR::RedstoneBlock, 0);
        w.setBlock(-7, 81, -8, BR::RedstoneDust, 0);
        w.setBlock(-6, 81, -8, BR::RedstoneLamp, 0);
        tickN(w, 3);
        const bool lampOn = (w.stateAt(-6, 81, -8) & BR::RedstoneLampStateOnFlag) != 0;
        ok = ok && lampOn;
        if (!lampOn)
            diag += QStringLiteral("[lamp s=%1]").arg(w.stateAt(-6, 81, -8));
        // (2) 金轨链面：红石块 (-8,81,-5) → 直供轨 A (-7,81,-5) → 链步轨 B (-6,81,-5)。
        //   旧门红载荷 = goldenRailChainStep px=-6<0 拒 → B 不入链恒暗。
        w.setBlock(-8, 81, -5, BR::RedstoneBlock, 0);
        w.setBlock(-7, 81, -5, BR::GoldenRail, 0);
        w.setBlock(-6, 81, -5, BR::GoldenRail, 0);
        tickN(w, 3);
        const bool railA = (w.stateAt(-7, 81, -5) & BR::GoldenRailStateOnFlag) != 0;
        const bool railB = (w.stateAt(-6, 81, -5) & BR::GoldenRailStateOnFlag) != 0;
        ok = ok && railA && railB;
        if (!(railA && railB))
            diag += QStringLiteral("[rail A=%1 B=%2]").arg(w.stateAt(-7, 81, -5))
                        .arg(w.stateAt(-6, 81, -5));
        // (3) 核心域对照（同两链在正坐标照常——分离「sparse 模式工作」与「外环门」）。
        for (int x = 20; x <= 22; ++x)
            for (int z = 20; z <= 22; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(20, 81, 20, BR::RedstoneBlock, 0);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::RedstoneLamp, 0);
        w.setBlock(20, 81, 22, BR::RedstoneBlock, 0);
        w.setBlock(21, 81, 22, BR::GoldenRail, 0);
        w.setBlock(22, 81, 22, BR::GoldenRail, 0);
        tickN(w, 3);
        const bool lampCore = (w.stateAt(22, 81, 20) & BR::RedstoneLampStateOnFlag) != 0;
        const bool railCore = (w.stateAt(22, 81, 22) & BR::GoldenRailStateOnFlag) != 0;
        ok = ok && lampCore && railCore;
        if (!(lampCore && railCore))
            diag += QStringLiteral("[core lamp=%1 rail=%2]").arg(lampCore).arg(railCore);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064a redstone outer-ring wall (in a sparse world with a materialized negative"
               " chunk a redstone block feeding dust feeding a lamp on negative coordinates lights"
               " the lamp through the real tickRedstone pipeline [old-gate red payload: the"
               " inBounds lambda drops negative anchors and neighbours so the dust region stays"
               " empty and the lamp never switches on], and a powered rail chain on negative"
               " coordinates propagates the powered bit to the second rail [old-gate red payload:"
               " the chain-step box rejects the negative step so the second rail stays dark]; the"
               " same two circuits stay green in the core domain as the control)"
            << (ok ? QString() : diag);
    });

    // ── r2064b：柱坍族承重墙（负坐标仙人掌/甘蔗整柱失撑坍落）──────────────────────────────────
    runLeg("r2064b column-collapse wall (in a sparse world with a materialized negative chunk"
        " silently clearing the block under a two-tall negative cactus topples the whole cactus"
        " column to air [old-gate red payload: the drop-column box swallows the outer-ring"
        " collapse so both segments float], and silently clearing the block under a two-tall"
        " negative sugarcane topples the whole cane column to air [same red payload])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 仙人掌柱（y=80 石坪 x,z∈[-9,-7]；柱 (-8,81..82,-8)）。
        for (int x = -9; x <= -7; ++x)
            for (int z = -9; z <= -7; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(-8, 81, -8, BR::Cactus, 0);
        w.setBlock(-8, 82, -8, BR::Cactus, 0);
        const bool cactusPlaced = w.blockAt(-8, 82, -8) == BR::Cactus;
        const bool cactusCleared = cactusPlaced && w.clearBlockSilent(-8, 80, -8);
        const bool cactusDropped = cactusCleared && w.blockAt(-8, 81, -8) == BR::Air
            && w.blockAt(-8, 82, -8) == BR::Air;
        ok = ok && cactusDropped;
        if (!cactusDropped)
            diag += QStringLiteral("[cactus p=%1 c=%2 b1=%3 b2=%4]").arg(cactusPlaced)
                        .arg(cactusCleared).arg(w.blockAt(-8, 81, -8)).arg(w.blockAt(-8, 82, -8));
        // (2) 甘蔗柱（柱 (-8,81..82,-6)；与仙人掌错列防单次复检交叉）。
        w.setBlock(-8, 80, -6, BR::Stone, 0);
        w.setBlock(-8, 81, -6, BR::Sugarcane, 0);
        w.setBlock(-8, 82, -6, BR::Sugarcane, 0);
        const bool canePlaced = w.blockAt(-8, 82, -6) == BR::Sugarcane;
        const bool caneCleared = canePlaced && w.clearBlockSilent(-8, 80, -6);
        const bool caneDropped = caneCleared && w.blockAt(-8, 81, -6) == BR::Air
            && w.blockAt(-8, 82, -6) == BR::Air;
        ok = ok && caneDropped;
        if (!caneDropped)
            diag += QStringLiteral("[cane p=%1 c=%2 b1=%3 b2=%4]").arg(canePlaced)
                        .arg(caneCleared).arg(w.blockAt(-8, 81, -6)).arg(w.blockAt(-8, 82, -6));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064b column-collapse wall (in a sparse world with a materialized negative"
               " chunk silently clearing the block under a two-tall negative cactus topples the"
               " whole cactus column to air [old-gate red payload: the drop-column box swallows"
               " the outer-ring collapse so both segments float], and silently clearing the block"
               " under a two-tall negative sugarcane topples the whole cane column to air [same"
               " red payload])"
            << (ok ? QString() : diag);
    });

    // ── r2064c：单格附着族承重墙（负坐标蘑菇/压力板/枯灌木失撑脱落）────────────────────────────
    runLeg("r2064c single-cell attachment wall (in a sparse world with a materialized negative"
        " chunk silently clearing the support under a negative mushroom drops the mushroom, the"
        " support under a negative wooden pressure plate drops the plate, and the support under"
        " a negative dead bush drops the bush [old-gate red payload: each member's own box gate"
        " swallows the outer-ring recheck so the attachment floats])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 蘑菇面（checkFlowerMushroomOnEdit；t507 全族 id==Air 口径）。
        w.setBlock(-8, 80, -12, BR::Stone, 0);
        w.setBlock(-8, 81, -12, BR::BrownMushroom, 0);
        const bool mushPlaced = w.blockAt(-8, 81, -12) == BR::BrownMushroom;
        const bool mushCleared = mushPlaced && w.clearBlockSilent(-8, 80, -12);
        const bool mushDropped = mushCleared && w.blockAt(-8, 81, -12) == BR::Air;
        ok = ok && mushDropped;
        if (!mushDropped)
            diag += QStringLiteral("[mush p=%1 c=%2 b=%3]").arg(mushPlaced).arg(mushCleared)
                        .arg(w.blockAt(-8, 81, -12));
        // (2) 压力板面（checkPressurePlateOnEdit）。
        w.setBlock(-8, 80, -10, BR::Stone, 0);
        w.setBlock(-8, 81, -10, BR::WoodPressurePlate, 0);
        const bool platePlaced = w.blockAt(-8, 81, -10) == BR::WoodPressurePlate;
        const bool plateCleared = platePlaced && w.clearBlockSilent(-8, 80, -10);
        const bool plateDropped = plateCleared && w.blockAt(-8, 81, -10) == BR::Air;
        ok = ok && plateDropped;
        if (!plateDropped)
            diag += QStringLiteral("[plate p=%1 c=%2 b=%3]").arg(platePlaced).arg(plateCleared)
                        .arg(w.blockAt(-8, 81, -10));
        // (3) 枯灌木面（checkDeadBushOnEdit）。
        w.setBlock(-8, 80, -14, BR::Stone, 0);
        w.setBlock(-8, 81, -14, BR::DeadBush, 0);
        const bool bushPlaced = w.blockAt(-8, 81, -14) == BR::DeadBush;
        const bool bushCleared = bushPlaced && w.clearBlockSilent(-8, 80, -14);
        const bool bushDropped = bushCleared && w.blockAt(-8, 81, -14) == BR::Air;
        ok = ok && bushDropped;
        if (!bushDropped)
            diag += QStringLiteral("[bush p=%1 c=%2 b=%3]").arg(bushPlaced).arg(bushCleared)
                        .arg(w.blockAt(-8, 81, -14));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064c single-cell attachment wall (in a sparse world with a materialized"
               " negative chunk silently clearing the support under a negative mushroom drops the"
               " mushroom, the support under a negative wooden pressure plate drops the plate,"
               " and the support under a negative dead bush drops the bush [old-gate red payload:"
               " each member's own box gate swallows the outer-ring recheck so the attachment"
               " floats])"
            << (ok ? QString() : diag);
    });

    // ── r2064d：板/雪/轨/画/火族承重墙（活板门级联 + 雪层柱坍 + 轨失撑 + 画掉落 + 立地火失撑熄）──
    runLeg("r2064d plate-snow-rail-painting-fire wall (in a sparse world with a materialized"
        " negative chunk clearing the support under a negative trapdoor cascades the trapdoor"
        " down to air, clearing the support under a negative snow layer topples the layer, the"
        " support under a negative rail drops the rail, replacing the wall behind a negative"
        " painting with air takes the painting with it, and breaking the block under a negative"
        " standing fire extinguishes it on the spot [old-gate red payload: each member's own box"
        " gate swallows the outer-ring recheck so the attachment keeps its stale state])",
        [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 布局：五个面全在已物化 chunk (-1,-1)（x,z ∈ [-16,-1]）内，x/z 错位防单次复检交叉。
        // (1) 活板门面（checkTrapdoorDoorSupportOnEdit → dropUnsupportedDoorsAbove 级联执行收口）。
        w.setBlock(-8, 80, -16, BR::Stone, 0);
        w.setBlock(-8, 81, -16, BR::WoodTrapdoor, 0);
        const bool trapPlaced = w.blockAt(-8, 81, -16) == BR::WoodTrapdoor;
        const bool trapCleared = trapPlaced && w.clearBlockSilent(-8, 80, -16);
        const bool trapDropped = trapCleared && w.blockAt(-8, 81, -16) == BR::Air;
        ok = ok && trapDropped;
        if (!trapDropped)
            diag += QStringLiteral("[trap p=%1 c=%2 b=%3]").arg(trapPlaced).arg(trapCleared)
                        .arg(w.blockAt(-8, 81, -16));
        // (2) 雪层面（checkSnowLayerOnEdit 柱坍为携带层数的下落实体信号；rig 只查栅格终态）。
        w.setBlock(-6, 80, -16, BR::Stone, 0);
        w.setBlock(-6, 81, -16, BR::SnowLayer, 0);
        const bool snowPlaced = w.blockAt(-6, 81, -16) == BR::SnowLayer;
        const bool snowCleared = snowPlaced && w.clearBlockSilent(-6, 80, -16);
        const bool snowDropped = snowCleared && w.blockAt(-6, 81, -16) == BR::Air;
        ok = ok && snowDropped;
        if (!snowDropped)
            diag += QStringLiteral("[snow p=%1 c=%2 b=%3]").arg(snowPlaced).arg(snowCleared)
                        .arg(w.blockAt(-6, 81, -16));
        // (3) 铁轨失撑面（checkRailOnEdit 失撑分支——oldId 非轨且正上方是轨且本格已非满顶支撑）。
        w.setBlock(-4, 80, -16, BR::Stone, 0);
        w.setBlock(-4, 81, -16, BR::Rail, 0);
        const bool railPlaced = w.blockAt(-4, 81, -16) == BR::Rail;
        const bool railCleared = railPlaced && w.clearBlockSilent(-4, 80, -16);
        const bool railDropped = railCleared && w.blockAt(-4, 81, -16) == BR::Air;
        ok = ok && railDropped;
        if (!railDropped)
            diag += QStringLiteral("[rail p=%1 c=%2 b=%3]").arg(railPlaced).arg(railCleared)
                        .arg(w.blockAt(-4, 81, -16));
        // (4) 画片面（checkPaintingSupportOnEdit：face=0 → 墙格 = 画格 -X 邻；清墙 → 整画掉落）。
        w.setBlock(-8, 81, -14, BR::Stone, 0);
        w.setBlock(-7, 81, -14, BR::Painting, 0);
        const bool paintingPlaced = w.blockAt(-7, 81, -14) == BR::Painting;
        const bool wallCleared = paintingPlaced && w.clearBlockSilent(-8, 81, -14);
        const bool paintingDropped = wallCleared && w.blockAt(-7, 81, -14) == BR::Air;
        ok = ok && paintingDropped;
        if (!paintingDropped)
            diag += QStringLiteral("[painting p=%1 c=%2 b=%3]").arg(paintingPlaced)
                        .arg(wallCleared).arg(w.blockAt(-7, 81, -14));
        // (5) 立地火失撑面（checkFireOnEdit 6 邻扫：经 4/5 参数 setBlock 主门清支撑 → 火当场灭）。
        w.setBlock(-6, 80, -14, BR::Stone, 0);
        w.setBlock(-6, 81, -14, BR::Fire, 0);
        const bool firePlaced = w.blockAt(-6, 81, -14) == BR::Fire;
        const bool supBroken = firePlaced && w.setBlock(-6, 80, -14, BR::Air, 0);
        const bool fireDead = supBroken && w.blockAt(-6, 81, -14) == BR::Air;
        ok = ok && fireDead;
        if (!fireDead)
            diag += QStringLiteral("[fire p=%1 c=%2 b=%3]").arg(firePlaced).arg(supBroken)
                        .arg(w.blockAt(-6, 81, -14));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064d plate-snow-rail-painting-fire wall (in a sparse world with a materialized"
               " negative chunk clearing the support under a negative trapdoor cascades the"
               " trapdoor down to air, clearing the support under a negative snow layer topples"
               " the layer, the support under a negative rail drops the rail, replacing the wall"
               " behind a negative painting with air takes the painting with it, and breaking the"
               " block under a negative standing fire extinguishes it on the spot [old-gate red"
               " payload: each member's own box gate swallows the outer-ring recheck so the"
               " attachment keeps its stale state])"
            << (ok ? QString() : diag);
    });

    // ── r2064e：mob 火/岩浆足印承重墙（负坐标燃块旁足印列接触点燃 + 核心域同构对照）──────────────
    runLeg("r2064e mob fire-footprint wall (in a sparse world with a materialized negative chunk"
        " a mob resting on a negative stone floor right next to a burning plank block takes fire"
        " contact damage through the footprint AABB scan whose own-column cells cover the burning"
        " column [old-gate red payload: the worldW/worldD box clamps negative footprint columns"
        " so the mob never ignites and keeps full health], and the same layout ignites in the"
        " core domain as the control)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 负侧：石坪 y=80（x,z∈[-13,-3]），木板 (-7,81,-8) 立坪点燃，mob (-8,81,-8) 落坪
        //   （MobTest halfW=0.5 → 自身列 xHi=-7 覆盖燃块列；中心列 -8 不命中 → 主扫描承重面）。
        //   t1029 setWanderFrozen 冻结缝圈定 mob（AI 冻结 = 零游走/零受击 panic 位移；火接触扫描
        //   在 AI 分派之外的 tick 主段照常跑，冻结 ≠ 悬停——重力/ resting 照常）。窗 60 拍 = 0.96s：
        //   首火伤恰在点燃后 0.75s（第 ~47 拍）落地 → hp==9 是点燃的精确指纹（摔伤/任意伤害面
        //   结构性不可达——只认火接触扣血）。
        for (int x = -13; x <= -3; ++x)
            for (int z = -13; z <= -3; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(-7, 81, -8, BR::Planks, 0);
        const bool ignited = w.igniteFlammableAt(-7, 81, -8) && w.isBurningAt(-7, 81, -8);
        ok = ok && ignited;
        if (!ignited) diag += QStringLiteral("[ign=%1]").arg(ignited);
        EntityManager em;
        em.setWanderFrozen(true);
        const int mi = em.spawnMobTyped(-8, 81, -8, EntityManager::MobTest, QString(), 10);
        const bool mobSpawn = mi >= 0 && em.aliveAt(mi);
        ok = ok && mobSpawn;
        if (!mobSpawn) diag += QStringLiteral("[mobSpawn idx=%1]").arg(mi);
        tickEntityResidual2(em, w, 60);
        const bool burning = mobSpawn && em.aliveAt(mi) && em.healthAt(mi) == 9;
        ok = ok && burning;
        if (!burning)
            diag += QStringLiteral("[burn alive=%1 hp=%2]").arg(em.aliveAt(mi))
                        .arg(em.healthAt(mi));
        // (2) 核心域对照（同构布局正坐标照常点燃——分离「sparse 模式工作」与「外环门」）。
        for (int x = 16; x <= 26; ++x)
            for (int z = 16; z <= 26; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(21, 81, 21, BR::Planks, 0);
        const bool ignCore = w.igniteFlammableAt(21, 81, 21) && w.isBurningAt(21, 81, 21);
        ok = ok && ignCore;
        if (!ignCore) diag += QStringLiteral("[ignCore=%1]").arg(ignCore);
        const int ci = em.spawnMobTyped(20, 81, 21, EntityManager::MobTest, QString(), 10);
        const bool coreSpawn = ci >= 0 && em.aliveAt(ci);
        ok = ok && coreSpawn;
        if (!coreSpawn) diag += QStringLiteral("[coreSpawn idx=%1]").arg(ci);
        tickEntityResidual2(em, w, 60);
        const bool coreBurning = coreSpawn && em.aliveAt(ci) && em.healthAt(ci) == 9;
        ok = ok && coreBurning;
        if (!coreBurning)
            diag += QStringLiteral("[coreBurn alive=%1 hp=%2]").arg(em.aliveAt(ci))
                        .arg(em.healthAt(ci));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064e mob fire-footprint wall (in a sparse world with a materialized negative"
               " chunk a mob resting on a negative stone floor right next to a burning plank"
               " block takes fire contact damage through the footprint AABB scan whose own-column"
               " cells cover the burning column [old-gate red payload: the worldW/worldD box"
               " clamps negative footprint columns so the mob never ignites and keeps full"
               " health], and the same layout ignites in the core domain as the control)"
            << (ok ? QString() : diag);
    });

    // ── r2064f：fixed 世界零变化墙（域内红石/mob 火接触回归柱 + 域外写恒拒 + 负列 primed TNT 冻结）──
    runLeg("r2064f fixed-world zero-change wall (in a fixed world where the whole domain is the"
        " core all members keep their exact former behavior: a core redstone circuit still lights"
        " its lamp, a core mob still ignites next to a burning block, writes on negative"
        " coordinates are still rejected outright, and a primed tnt dropped in a negative column"
        " stays frozen at its spawn height)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedResidual2World(w);
        // (1) 域内红石回归柱（Fixed 分支原样——域内照常点亮）。
        w.setBlock(20, 80, 20, BR::Stone, 0);
        w.setBlock(21, 80, 20, BR::Stone, 0);
        w.setBlock(22, 80, 20, BR::Stone, 0);
        w.setBlock(20, 81, 20, BR::RedstoneBlock, 0);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::RedstoneLamp, 0);
        tickN(w, 3);
        const bool lampOn = (w.stateAt(22, 81, 20) & BR::RedstoneLampStateOnFlag) != 0;
        ok = ok && lampOn;
        if (!lampOn) diag += QStringLiteral("[lamp s=%1]").arg(w.stateAt(22, 81, 20));
        // (2) 域外写恒拒（Fixed 分支现行语句原样的行为级；写门域外拒 → 无外环附着物可清）。
        const bool outerRejected = !w.setBlock(-9, 81, -8, BR::Stone, 0)
            && w.blockAt(-9, 81, -8) == BR::Air
            && !w.clearBlockSilent(-8, 80, -8);
        ok = ok && outerRejected;
        if (!outerRejected) diag += QStringLiteral("[outer=%1]").arg(outerRejected);
        // (3) 域内 mob 火接触回归柱（Fixed 分支原样——足印扫描在域内照常承管；60 拍精确指纹同 e 腿）。
        for (int x = 16; x <= 26; ++x)
            for (int z = 16; z <= 26; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(21, 81, 20, BR::Planks, 0);
        const bool ign = w.igniteFlammableAt(21, 81, 20) && w.isBurningAt(21, 81, 20);
        EntityManager em2;
        em2.setWanderFrozen(true);
        const int ci = em2.spawnMobTyped(20, 81, 20, EntityManager::MobTest, QString(), 10);
        tickEntityResidual2(em2, w, 60);
        const bool coreBurning = ign && ci >= 0 && em2.aliveAt(ci) && em2.healthAt(ci) == 9;
        ok = ok && coreBurning;
        if (!coreBurning)
            diag += QStringLiteral("[burn ign=%1 hp=%2]").arg(ign)
                        .arg(ci >= 0 ? em2.healthAt(ci) : -1);
        // (4) 负列 primed TNT 冻结复钉（Fixed 分支 continue 原样；r2061f 同款承重面——TNT 无 AI
        //     位置钳制干扰，冻结面行为确定性成立。mob 负列无 fixed 冻结面可钉：AI 钳制在 t1091
        //     Mob tick 门之前把位置拽回域内 = 门 Fixed 分支结构性不可达，t1091 批已如实登记）。
        EntityManager em3;
        em3.spawnPrimedTnt(-8, 84, -8, 5.0f); // 长引信：窗内不到爆，只钉冻结面
        const bool tntSpawn = em3.primedCount() == 1;
        const float tntY0 = tntSpawn ? em3.posAt(0).y() : -1.0f;
        tickEntityResidual2(em3, w, 60);
        const bool frozen = tntSpawn && em3.primedCount() == 1 && em3.posAt(0).y() == tntY0;
        ok = ok && frozen;
        if (!frozen)
            diag += QStringLiteral("[frozen cnt=%1 y0=%2 y=%3]").arg(em3.primedCount())
                        .arg(tntY0).arg(em3.primedCount() > 0 ? em3.posAt(0).y() : -1.0f);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064f fixed-world zero-change wall (in a fixed world where the whole domain"
               " is the core all members keep their exact former behavior: a core redstone"
               " circuit still lights its lamp, a core mob still ignites next to a burning block,"
               " writes on negative coordinates are still rejected outright, and a primed tnt"
               " dropped in a negative column stays frozen at its spawn height)"
            << (ok ? QString() : diag);
    });

    // ── r2064g：结构钉（十九站点锚注 + 批头/清偿注锚族 + 词根计数 + r2059/r2060/r2061 承重复钉）────
    //   NEG 靶行按恰红设计收账：组①②④ 锚注（NEG-1 摘即红）+ 组③ 锚注（NEG-2 摘即红）；其余
    //   计数钉与双 NEG 零交集者恒绿，作漂移防线。
    runLeg("r2064g structure pins (each t1094 split site keeps its unique anchor comment"
        " [seventeen world.cpp gates plus the mob footprint and stand-on-top anchors in"
        " entitymanager], the batch header plus its two repayment notes and the entitymanager"
        " repayment anchor remain, the t1094 gate and neighbour-scan word roots appear at their"
        " exact counts, and the r2059 materialization-gate line, the r2060/r2061 y-gate and mask"
        " pins and the prior anchor families stay untouched)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile wcf(srcRoot + QStringLiteral("/World/world.cpp"));
        const QString wSrc = wcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wcf.readAll()) : QString();
        QFile emf(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
        const QString eSrc = emf.open(QIODevice::ReadOnly) ? QString::fromUtf8(emf.readAll()) : QString();
        // (g1) t1094 十九站点唯一锚注（NEG 靶行）。
        const QStringList sites = {
            QStringLiteral("t1094 域门（tickRedstone inBounds）"),
            QStringLiteral("t1094 域门（goldenRailChainStep）两模式分流"),
            QStringLiteral("t1094 邻扫门（checkFireOnEdit）两模式分流"),
            QStringLiteral("t1094 域门（dropCactusColumn）两模式分流"),
            QStringLiteral("t1094 邻扫门（checkCactusOnEdit 邻接碎柱）两模式分流"),
            QStringLiteral("t1094 域门（checkDeadBushOnEdit）两模式分流"),
            QStringLiteral("t1094 域门（checkFlowerMushroomOnEdit）两模式分流"),
            QStringLiteral("t1094 域门（checkPressurePlateOnEdit）两模式分流"),
            QStringLiteral("t1094 域门（checkTrapdoorDoorSupportOnEdit）两模式分流"),
            QStringLiteral("t1094 邻扫门（checkTrapdoorDoorSupportOnEdit 依附侧邻）两模式分流"),
            QStringLiteral("t1094 邻扫门（checkTrapdoorDoorSupportOnEdit 侧板邻扫）两模式分流"),
            QStringLiteral("t1094 域门（dropUnsupportedDoorsAbove）两模式分流"),
            QStringLiteral("t1094 域门（dropSugarcaneColumn）两模式分流"),
            QStringLiteral("t1094 域门（checkSnowLayerOnEdit）两模式分流"),
            QStringLiteral("t1094 域门（checkRailOnEdit 失撑分支）两模式分流"),
            QStringLiteral("t1094 域门（checkPaintingSupportOnEdit）两模式分流"),
            QStringLiteral("t1094 邻扫门（checkPaintingSupportOnEdit 画邻扫）两模式分流"),
            QStringLiteral("t1094 域门（mob 火/岩浆接触 AABB 足印扫描）"),
            QStringLiteral("t1094 邻扫门（mob 火/岩浆站顶足印扫）") };
        int g1Bad = 0;
        QString g1Which;
        for (const QString &s : sites) {
            const int cnt = wSrc.count(s) + eSrc.count(s);
            if (cnt != 1) { ++g1Bad; g1Which += QString::number(cnt) + QLatin1Char('x'); }
        }
        const bool g1 = g1Bad == 0;
        ok = ok && g1;
        if (!g1) diag += QStringLiteral("[g1 bad=%1 %2]").arg(g1Bad).arg(g1Which);
        // (g2) 批头锚注族 + 清偿回填注（NEG 靶行：锚注摘除处 g1 已红；本面钉批头/回填注在场）。
        const bool g2 = wSrc.contains(QStringLiteral(
                            "t1094 写门家族「核心域假设」同族清偿·残面批"))
            && wSrc.count(QStringLiteral("t1094 清偿注（后续批落地回填）")) == 2
            && eSrc.contains(QStringLiteral("已由 t1094 残面批清偿（回填注）"))
            && wSrc.contains(QStringLiteral("仍留池"));
        ok = ok && g2;
        if (!g2) diag += QStringLiteral("[g2 hdr]");
        // (g3) 词根计数（NEG 靶行：摘锚注即变数；其余为漂移防线）。wGate =14 = 12 站点锚注
        //     + 2 处链面交叉引用（checkCactusOnEdit/checkTrapdoorDoorSupportOnEdit 锚注内的
        //     「t1094 域门」引用字样——链面合并清偿的文档面，计数钉一并锁定）。
        const int wGate = wSrc.count(QStringLiteral("t1094 域门"));       // =14（world.cpp）
        const int wScan = wSrc.count(QStringLiteral("t1094 邻扫门"));     // =5（world.cpp）
        const int eGate = eSrc.count(QStringLiteral("t1094 域门"));       // =1（entitymanager）
        const int eScan = eSrc.count(QStringLiteral("t1094 邻扫门"));     // =1（entitymanager）
        const bool g3 = wGate == 14 && wScan == 5 && eGate == 1 && eScan == 1;
        ok = ok && g3;
        if (!g3)
            diag += QStringLiteral("[g3 wGate=%1 wScan=%2 eGate=%3 eScan=%4]")
                        .arg(wGate).arg(wScan).arg(eGate).arg(eScan);
        // (g4) r2059/r2060/r2061 承重复钉（零变化墙复钉——本批不得扰动）。
        const int matGate = wSrc.count(QStringLiteral(
            "if (!m_chunks.chunkContentPresent(floorDiv(x, Chunk::kSize), floorDiv(z, Chunk::kSize)))")); // =7
        const int c2gate = wSrc.count(QStringLiteral("} else if (y < 0 || y >= m_height) {")); // =3
        const int c2scan = wSrc.count(QStringLiteral("} else if (by < 0 || by >= m_height) {")); // =1
        const int gateTmH = wSrc.count(QStringLiteral("} else if (ty < 0 || ty >= m_height) {")); // =1
        const int aliasPack = wSrc.count(QStringLiteral("return packLeafCell(x, y, z);")); // =1
        const int mask = wSrc.count(QStringLiteral("0x3FFFFFFu"));          // =4
        const bool g4 = matGate == 7 && c2gate == 3 && c2scan == 1 && gateTmH == 1
            && aliasPack == 1 && mask == 4;
        ok = ok && g4;
        if (!g4)
            diag += QStringLiteral("[g4 mat=%1 gate=%2 scan=%3 tmH=%4 pack=%5 mask=%6]")
                        .arg(matGate).arg(c2gate).arg(c2scan).arg(gateTmH).arg(aliasPack).arg(mask);
        // (g5) 既有批锚注族回钉（t1089/t1090/t1091 零触碰——同族回归承重）。
        const bool g5 = wSrc.count(QStringLiteral("t1091 域门（recheckAttachmentsAfterClear）两模式分流")) == 1
            && wSrc.count(QStringLiteral("t1091 邻域门（recheckAttachmentsAfterClear 火把邻扫）两模式分流")) == 1
            && wSrc.count(QStringLiteral("t1091 域门（recomputeRailConnections）两模式分流")) == 1
            && wSrc.count(QStringLiteral("t1090 域门（destroySphereSilent 扫描门）两模式分流")) == 1
            && wSrc.count(QStringLiteral("t1089 域门（setBlockFromEntity）两模式分流")) == 1
            && eSrc.count(QStringLiteral("t1091 域门（Mob tick）两模式分流")) == 1;
        ok = ok && g5;
        if (!g5) diag += QStringLiteral("[g5 family]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2064g structure pins (each t1094 split site keeps its unique anchor comment"
               " [seventeen world.cpp gates plus the mob footprint and stand-on-top anchors in"
               " entitymanager], the batch header plus its two repayment notes and the"
               " entitymanager repayment anchor remain, the t1094 gate and neighbour-scan word"
               " roots appear at their exact counts, and the r2059 materialization-gate line, the"
               " r2060/r2061 y-gate and mask pins and the prior anchor families stay untouched)"
            << (ok ? QString() : diag);
    });
}
