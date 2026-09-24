#include "matrix_helpers.h"

// t1091 写门家族「核心域假设」同族清偿·第三批残项探针段（7 腿；filter 词 r2061；矩阵 775→782）。
// 置尾先例沿用（接 section60，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：t1089 五员（静默写门）+ t1090 四员（扫描门与盒域）之后同族收官批。三组九员：
//   ①entitymanager primed TNT tick / Mob tick 同形 cx<0||cz<0 列跳过（t1090 留池）+ entitystore
//   ItemEntity tick 同形门（任务书「entitymanager 三处」第三员经现场核实定位在 EntityStore——
//   EntityManager Item kind 仅遗留视觉中性化，现役掉落物物理在 EntityStore::tick）；②packGrowthCell
//   quint16 截断（t1089 留池，本单复核翻案为真缺口：population 增量面把已物化外环真实负坐标内容灌进
//   生长/流体/冰/火/燃烧索引，tick 回读幻影坐标 = 外环作物不长/流体不流/燃烧键每窗被整键摘除）+ 回读
//   链上的 tickFire 四门与 fireSupportedAt/fireWaterNeighborAt 邻域门（消费链承重面，合并清偿）；③五处
//   辅助固定盒门（fireRainExposedAt / igniteFlammableAt / isBurningAt / recheckAttachmentsAfterClear
//   含火把邻扫门 / recomputeRailConnections）。
//
// 修法选型（留痕）：Fixed 分支现行语句原样（零变化墙）；sparse = y 域两模式同构（有限高）+ x/z 无界；
//   读经 blockAt/stateAt/skyLightAt 物化门、写经 setBlock 家族 / 侧表键——扫描域无界化（t1090 裁定
//   先例），未加 chunkContentPresent（r2059g 物化门行计数钉 7 不动）；键 = t1090 packLeafCell 保符号
//   别名（位型单一权威，0x3FFFFFFu 掩码字面计数 4 不动）；实体门 = t1090 FallingBlock 两模式分流同式。
//
// 现场核实（逐员）：九员外环消费场景全部真实可达（玩家位 / 实体位 / 索引 tick / population 驱动）→
//   全确认零降级（含 packGrowthCell 复核翻案）。留池（本批现场新发现，禁顺手修）：tickRedstone
//   inBounds + goldenRailChainStep 盒 + recheckAttachments ① 族子钩子/柱坍盒门 + entitymanager mob
//   火/岩浆接触足印扫描 worldW/worldD 盒（批头锚注 + g 腿钉）。
//
// 腿面（逐员/逐组专属可单独归因，禁一刀切批修）：
//   r2061a primed TNT 外环承重墙（负坐标引燃 TNT 真管线受重力下落 + 落支撑停靠 + fuse 到期原位引爆
//     毁支撑 [旧门红载荷 = 冻在半空原地引爆、支撑不毁]）；r2061b Mob + Item 外环物理承重墙（mob 负列
//     重力下落着地 / 掉落物负列下落 [旧门红载荷 = 双双冻结半空]）；r2061c packGrowthCell 键域承重墙
//     （负坐标水平铺流动 + 负坐标木板点燃燃烧态存续 + 计时烧毁收口 [旧门红载荷 = 流体不流 + 燃烧键
//     首窗被整键摘除永烧不毁]）；r2061d 火辅助谓词族承重墙（负坐标点燃 + 燃烧查询 + 湿燃料防火带负
//     向水邻 + 雨露判：露天负列火确定性窗先灭、遮棚对照火存活 + 核心域对照）；r2061e 铁轨重连 + 附着
//     复检承重墙（负坐标铺轨连接位重算 + 负坐标清格火把脱落 + 核心域对照）；r2061f **fixed 世界零变化
//     墙**（primed TNT/Mob/Item 负列冻结 + fuse 到期冻结位引爆支撑不毁 + 点燃/燃烧查询/雨露判域外恒
//     假 + 域外放轨拒 + 核心域水流动回归柱）；r2061g 结构钉（二十七站点锚注各 count==1 + 别名/掩码/域
//     门行计数 + r2059/r2060 承重复钉 + 批头/留池锚注族 + entitystore 锚注）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2061_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘实体三员两模式分流（primed TNT + Mob + ItemEntity 还原旧无条件 continue + 摘三锚注）→
//     **恰红 = {r2061a, r2061b, r2061g: a/b 行为面 + g 实体锚注子面}**。c/d/e/f 不误伤（f 是 fixed
//     零变化墙——fixed 行为在摘/不摘分流下逐位同，天然免疫实体 NEG）。
//   NEG-2 摘 packGrowthCell 别名（还原旧 quint16 打包体）+ recomputeRailConnections 分流（还原旧
//     六比较单行 + 摘两锚注）→ **恰红 = {r2061c, r2061d, r2061e, r2061g: c 键回读行为面 + d 雨窗
//     行为面（火格快照回读骑键——键摘则负侧火格隐身，同链归因）+ e 铁轨面 + g 键域/铁轨锚注与别名
//     计数子面}**。a/b/f 不误伤。
//
// rig 纪律：sparse 构造缝 `World w{ {82, 48, 48, 96, 0} }`（section54 r2053c / section59/60 同门）+
//   loadChunkAt 按需物化；rig 坪取 y=79..84 高空（s82 地形/树冠带之上）；rig 基建全经 setBlock 主门
//   （sparse 分支已无界）；行为级腿不查信号只查栅格终态与实体位；坐标不作哨兵；火窗散布复刻逐字同
//   式（hashVoxel ^ 窗口序号，seed+坐标固定 → 命中窗恒定零 flake）。

namespace {

// sparse 宿主小世界：World 是 QObject（禁拷贝/移动）→ 各腿原地构造 `World w{ {82,48,48,96,0} };`
//   （section59/60 同门），随后按需 loadChunkAt 物化。

// fixed 宿主小世界 incantation（section59/60 同款四 setter；默认构造 World 原地配置）。
inline void initFixedResidualWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 推进实体 / primed TNT / mob 管线（section59/60 同款 tick 形态；600 拍 ≈ 9.6s 模拟时）。
inline void tickEntityResidual(EntityManager &em, World &w, int frames)
{
    const QVector3D farListener(4.5f, 80.5f, 4.5f);
    for (int t = 0; t < frames; ++t)
        em.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
}

// 推进掉落物管线（EntityStore::tick 直驱，kFireTickInterval 无关）。
inline void tickItemResidual(ItemEntityManager &iem, World &w, int frames)
{
    for (int t = 0; t < frames; ++t)
        iem.tick(0.016f, &w);
}

// tickFire 窗口驱动（5 调 = 1 窗，kFireTickInterval=5 由 r2061g 源钉锁死）。
inline void driveFireWindows(World &w, int windows)
{
    for (int c = 0; c < 5 * windows; ++c)
        w.tickFire();
}

// tickFire (a) 自熄掷骰的确定性复刻（PLAN §2-K）：hv = hashVoxel(seed^0xF177, x,y,z) ^ (窗口序号 ×
//   2654435761)——与 world.cpp tickFire (a) 逐字同式；**窗口序号 1 起**（tickFire 先 ++m_fireIntervalIndex
//   再开窗，真序列 = 1,2,3…）；返回首个命中窗口（无命中 -1）。
inline int firstFireExtinguishWindow(const World &w, int x, int y, int z, int pct)
{
    const int hseed = w.seed() ^ 0xF177;
    for (int k = 1; k <= 400; ++k) {
        const quint32 hv = w.hashVoxel(hseed, x, y, z) ^ (quint32(k) * 2654435761u);
        if (int(hv % 100u) < pct)
            return k;
    }
    return -1;
}

} // namespace

void MatrixRun::section61_write_gate_residual()
{
    // ── r2061a：primed TNT 外环承重墙（负列重力下落 + 落支撑停靠 + fuse 到期原位引爆毁支撑）────
    runLeg("r2061a primed-tnt outer-ring wall (in a sparse world with a materialized negative"
        " chunk a primed tnt entity driven through the real EntityManager tick pipeline falls"
        " under gravity onto its support and detonates at the landed position when the fuse"
        " expires, excavating the support stone below [old-gate red payload: the sign skip"
        " freezes the entity mid-air so it detonates in place and the support survives])",
        [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标列（t1091 靶面 = t1090 FallingBlock 同形）：支撑石 (-8,80,-8)，primed TNT 自
        //   (-8,84,-8) 引燃（短引信 0.8s 便于窗口内到爆）。旧门红载荷 = cx<0||cz<0 continue →
        //   重力/冲量积分整段跳过 → 冻在 84.5 原地引爆（下方支撑石距爆心 4.5 > 半径 3 不毁）。
        w.setBlock(-8, 80, -8, BR::Stone, 0);
        EntityManager em;
        em.spawnPrimedTnt(-8, 84, -8, 0.8f);
        const bool primed0 = em.primedCount() == 1;
        ok = ok && primed0;
        if (!primed0) diag += QStringLiteral("[primed0=%1]").arg(em.primedCount());
        tickEntityResidual(em, w, 30); // ~0.48s < fuse：重力面窗口
        const float midY = em.posAt(0).y();
        const bool fell = midY < 84.0f; // 旧门恒 84.5（冻结）；新门已落至支撑顶 ~81.5
        ok = ok && fell;
        if (!fell) diag += QStringLiteral("[fell midY=%1]").arg(midY);
        tickEntityResidual(em, w, 570); // 推满 fuse → 引爆 + 实体移除
        const bool detonated = em.primedCount() == 0;
        const bool crater = w.blockAt(-8, 80, -8) == BR::Air; // 落位引爆毁支撑（冻结位引爆则石存）
        const bool detOk = detonated && crater;
        ok = ok && detOk;
        if (!detOk)
            diag += QStringLiteral("[det primed=%1 support=%2]").arg(em.primedCount())
                        .arg(w.blockAt(-8, 80, -8));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061a primed-tnt outer-ring wall (in a sparse world with a materialized"
               " negative chunk a primed tnt entity driven through the real EntityManager tick"
               " pipeline falls under gravity onto its support and detonates at the landed"
               " position when the fuse expires, excavating the support stone below [old-gate"
               " red payload: the sign skip freezes the entity mid-air so it detonates in place"
               " and the support survives])"
            << (ok ? QString() : diag);
    });

    // ── r2061b：Mob + Item 外环物理承重墙（mob 负列下落着地 / 掉落物负列下落）────────────────
    runLeg("r2061b mob-and-item outer-ring physics wall (in a sparse world with a materialized"
        " negative chunk a mob spawned in a negative column falls under gravity and settles on"
        " its support platform, and a dropped item spawned in a negative column falls onto the"
        " same platform [old-gate red payload: both entities freeze at their spawn height"
        " forever])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负列 9×9 石坪（x,z ∈ [-12,-4], y=80）——游走不出坪、下落有着落。
        for (int x = -12; x <= -4; ++x)
            for (int z = -12; z <= -4; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        // Mob（MobTest=0 通用测试生物）：(-8,84,-8) 落坪。旧门红载荷 = resting/重力段 continue →
        //   冻在生成高度；新门 = 重力下落至坪顶 ~81。
        EntityManager em;
        const int mi = em.spawnMobTyped(-8, 84, -8, EntityManager::MobTest, QString(), 10);
        const bool mobSpawn = mi >= 0 && em.aliveAt(mi);
        ok = ok && mobSpawn;
        if (!mobSpawn) diag += QStringLiteral("[mobSpawn idx=%1]").arg(mi);
        tickEntityResidual(em, w, 60); // ~1s：0.5s 内落坪；游走漂移 ≤1.5 格不出 9×9 坪
        const float mobY = em.posAt(mi).y();
        const bool mobFell = em.aliveAt(mi) && mobY < 84.0f;
        ok = ok && mobFell;
        if (!mobFell) diag += QStringLiteral("[mob fell=%1 y=%2]").arg(em.aliveAt(mi)).arg(mobY);
        // Item（EntityStore::tick 现役物理）：(-8,84,-6) 落坪（与 mob 错格防拾取/合并面互扰）。
        ItemEntityManager iem;
        iem.spawnItem(-8, 84, -6, int(BR::Planks), 1);
        const bool itemSpawn = iem.liveCount() == 1;
        ok = ok && itemSpawn;
        if (!itemSpawn) diag += QStringLiteral("[itemSpawn=%1]").arg(iem.liveCount());
        tickItemResidual(iem, w, 200);
        const float itemY = iem.posAt(0).y();
        const bool itemFell = iem.aliveAt(0) && itemY < 84.0f;
        ok = ok && itemFell;
        if (!itemFell)
            diag += QStringLiteral("[item fell=%1 y=%2]").arg(iem.aliveAt(0)).arg(itemY);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061b mob-and-item outer-ring physics wall (in a sparse world with a"
               " materialized negative chunk a mob spawned in a negative column falls under"
               " gravity and settles on its support platform, and a dropped item spawned in a"
               " negative column falls onto the same platform [old-gate red payload: both"
               " entities freeze at their spawn height forever])"
            << (ok ? QString() : diag);
    });

    // ── r2061c：packGrowthCell 键域承重墙（负坐标水流铺展 + 负坐标燃烧态存续到烧毁收口）────────
    runLeg("r2061c growth-cell key wall (in a sparse world with a materialized negative chunk a"
        " negative-coordinate water source flows across its stone floor to the neighbours on"
        " both negative and positive sides [old-gate red payload: the quint16 key truncation"
        " reads back a phantom coordinate so the fluid tick never sees the cell], and a"
        " negative-coordinate plank block ignited into the burning state persists across"
        " windows and burns down to air through the flare-and-extinguish chain [old-gate red"
        " payload: the out-of-bounds defense in the burning pass removes the key on its first"
        " window so the block never burns])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 流体面：石坪 3×3 于 y=80（x,z ∈ [-9,-7]），水源 (-8,81,-8)。键保符号后 tickWaterFlow
        //   快照回读真坐标 → 平地水平铺展；旧键回读幻影坐标 → blockAt(65528,·)≠水 → 整格漏扫不流。
        for (int x = -9; x <= -7; ++x)
            for (int z = -9; z <= -7; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        const bool srcPlaced = w.setBlock(-8, 81, -8, BR::Water, 0);
        ok = ok && srcPlaced;
        if (!srcPlaced) diag += QStringLiteral("[src=%1]").arg(srcPlaced);
        for (int t = 0; t < 100; ++t) w.tickWaterFlow(); // ≈33 窗 ≥ 波前收敛（section57 同款）
        const bool flowNeg = w.blockAt(-9, 81, -8) == BR::Water && w.stateAt(-9, 81, -8) > 0;
        const bool flowPos = w.blockAt(-7, 81, -8) == BR::Water && w.stateAt(-7, 81, -8) > 0;
        ok = ok && flowNeg && flowPos;
        if (!(flowNeg && flowPos))
            diag += QStringLiteral("[flow neg=%1/%2 pos=%3/%4]").arg(flowNeg)
                        .arg(w.stateAt(-9, 81, -8)).arg(flowPos).arg(w.stateAt(-7, 81, -8));
        // (2) 燃烧面：浮空木板 (-8,84,-8)（周围全 air，无水邻）直燃进燃烧态 → 存续 → 10 窗烧毁
        //   （kBurnWindowsWood；烧穿位余烬火 next 窗失撑即灭 → 终态 Air）。旧门红载荷 = 燃烧键
        //   quint16 截断 → (d) 越界防御**整键摘除** → 永不烧毁。
        w.setBlock(-8, 84, -8, BR::Planks, 0);
        const bool ignited = w.igniteFlammableAt(-8, 84, -8);
        const bool burning0 = ignited && w.isBurningAt(-8, 84, -8);
        ok = ok && burning0;
        if (!burning0)
            diag += QStringLiteral("[burn ign=%1 q=%2]").arg(ignited)
                        .arg(w.isBurningAt(-8, 84, -8));
        driveFireWindows(w, 3); // 1.5s：仍在燃（计时未到）
        const bool stillPlanks = w.blockAt(-8, 84, -8) == BR::Planks
            && w.isBurningAt(-8, 84, -8);
        ok = ok && stillPlanks;
        if (!stillPlanks)
            diag += QStringLiteral("[mid b=%1 q=%2]").arg(w.blockAt(-8, 84, -8))
                        .arg(w.isBurningAt(-8, 84, -8));
        driveFireWindows(w, 14); // 累计 17 窗 ≥ 10 窗烧毁 + 余烬火失撑熄灭窗
        const bool burnedDown = w.blockAt(-8, 84, -8) == BR::Air;
        ok = ok && burnedDown;
        if (!burnedDown)
            diag += QStringLiteral("[final b=%1]").arg(w.blockAt(-8, 84, -8));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061c growth-cell key wall (in a sparse world with a materialized negative"
               " chunk a negative-coordinate water source flows across its stone floor to the"
               " neighbours on both negative and positive sides [old-gate red payload: the"
               " quint16 key truncation reads back a phantom coordinate so the fluid tick never"
               " sees the cell], and a negative-coordinate plank block ignited into the burning"
               " state persists across windows and burns down to air through the"
               " flare-and-extinguish chain [old-gate red payload: the out-of-bounds defense in"
               " the burning pass removes the key on its first window so the block never burns])"
            << (ok ? QString() : diag);
    });

    // ── r2061d：火辅助谓词族承重墙（点燃 + 燃烧查询 + 湿燃料负向水邻 + 雨露判确定性窗）────────
    runLeg("r2061d fire-auxiliary wall (in a sparse world with a materialized negative chunk"
        " flint-grade ignition of a negative-coordinate plank returns true and the burning"
        " query confirms it, ignition next to water on the negative side stays blocked by the"
        " wet-fuel firewall, an out-of-chunk ignition is rejected, and under rain the"
        " rain-exposure predicate holds for an open-sky negative fire which then"
        " deterministically extinguishes before its roofed control fire [old-gate red payload:"
        " ignition false, burning query false, wet check blind on negative neighbours, rain"
        " never reaches outer-ring fire])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 负坐标点燃 + 燃烧查询（核心域对照同链）：直燃三入口统一口径。
        w.setBlock(-8, 84, -8, BR::Planks, 0);
        const bool ignNeg = w.igniteFlammableAt(-8, 84, -8) && w.isBurningAt(-8, 84, -8);
        ok = ok && ignNeg;
        if (!ignNeg) diag += QStringLiteral("[ignNeg=%1]").arg(ignNeg);
        w.setBlock(-8, 84, -8, BR::Planks, 0); // 同 id 复写 → 燃烧作废（t843 链）复位本格
        w.setBlock(20, 84, 20, BR::Planks, 0);
        const bool ignCore = w.igniteFlammableAt(20, 84, 20) && w.isBurningAt(20, 84, 20);
        ok = ok && ignCore;
        if (!ignCore) diag += QStringLiteral("[ignCore=%1]").arg(ignCore);
        w.setBlock(20, 84, 20, BR::Planks, 0);
        // (2) 湿燃料防火带负向水邻：水在木板 **更负** 一侧（-X）→ 负向邻扫开门后才判湿。
        for (int x = -10; x <= -6; ++x)
            for (int z = -14; z <= -10; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(-9, 81, -12, BR::Water, 0);
        w.setBlock(-8, 81, -12, BR::Planks, 0);
        const bool wetBlocked = !w.igniteFlammableAt(-8, 81, -12);
        ok = ok && wetBlocked;
        if (!wetBlocked) diag += QStringLiteral("[wet=%1]").arg(wetBlocked);
        // (3) 未物化 chunk 拒燃（防御面恒真）。
        const bool ignGhost = !w.igniteFlammableAt(-40, 84, -40);
        ok = ok && ignGhost;
        if (!ignGhost) diag += QStringLiteral("[ghost=%1]").arg(ignGhost);
        // (4) 雨露判（fresh world 保 m_fireIntervalIndex=0 的复刻基准）：设雨后扫负列取降水列
        //   （群系纯函数，s82 确定）；露天火与遮棚对照火确定性窗先后熄灭。
        World w2{ { 82, 48, 48, 96, 0 } };
        const bool rig2 = w2.isSparse() && w2.loadChunkAt(-1, -1);
        ok = ok && rig2;
        if (!rig2) diag += QStringLiteral("[rig2] ");
        w2.setWeatherState(1); // Weather::Rain
        int rx = -99, rz = -99;
        for (int x = -14; x <= -2 && rx == -99; ++x)
            for (int z = -14; z <= -2; ++z)
                if (w2.isPrecipitatingAt(x, z)) { rx = x; rz = z; break; }
        const bool colFound = rx != -99;
        ok = ok && colFound;
        if (!colFound) { diag += QStringLiteral("[col none] "); }
        if (colFound) {
            // 遮棚对照位取「确定性先后」成立的最近错位：对照火 5%/窗首中窗须晚于露天火 40%/窗首
            //   中窗（纯哈希复刻预筛，s82 确定 → 零 flake；z+3 起步扫至 z+15 必有解，概率上成立）。
            const int kOpen = firstFireExtinguishWindow(w2, rx, 81, rz, 40);
            int roofOff = -1, kRoof = -1;
            for (int off = 3; off <= 15; ++off) {
                const int kr = firstFireExtinguishWindow(w2, rx, 81, rz + off, 5);
                if (kr > kOpen) { roofOff = off; kRoof = kr; break; }
            }
            const bool det = kOpen >= 0 && roofOff != -1; // 露天先灭（确定性先后，s82 恒定）
            ok = ok && det;
            if (!det)
                diag += QStringLiteral("[det kOpen=%1 roofOff=%2 kRoof=%3]").arg(kOpen).arg(roofOff).arg(kRoof);
            if (det) {
                w2.setBlock(rx, 80, rz, BR::Stone, 0);              // 露天火：立地于石面 y=81
                w2.setBlock(rx, 81, rz, BR::Fire, 0);
                w2.setBlock(rx, 80, rz + roofOff, BR::Stone, 0);    // 遮棚对照：同列错位格 + 顶盖
                w2.setBlock(rx, 81, rz + roofOff, BR::Fire, 0);
                w2.setBlock(rx, 82, rz + roofOff, BR::Stone, 0);
                const bool rainOpen = w2.fireRainExposedAt(rx, 81, rz);
                const bool roofShields = !w2.fireRainExposedAt(rx, 81, rz + roofOff);
                ok = ok && rainOpen && roofShields;
                if (!(rainOpen && roofShields))
                    diag += QStringLiteral("[rain open=%1 shield=%2]").arg(rainOpen).arg(roofShields);
                driveFireWindows(w2, kOpen - 1); // 1..kOpen-1 窗：两火俱在（首中窗之前全落空）
                const bool bothAlive = w2.blockAt(rx, 81, rz) == BR::Fire
                    && w2.blockAt(rx, 81, rz + roofOff) == BR::Fire;
                driveFireWindows(w2, 1);         // 第 kOpen 窗：露天灭、棚下仍燃
                const bool openDead = w2.blockAt(rx, 81, rz) == BR::Air;
                const bool roofAlive = w2.blockAt(rx, 81, rz + roofOff) == BR::Fire;
                const bool rainOk = bothAlive && openDead && roofAlive;
                ok = ok && rainOk;
                if (!rainOk)
                    diag += QStringLiteral("[rain alive=%1 dead=%2 roof=%3 open=%4 roofb=%5 kOpen=%6 roofOff=%7]")
                                .arg(bothAlive).arg(openDead).arg(roofAlive)
                                .arg(w2.blockAt(rx, 81, rz)).arg(w2.blockAt(rx, 81, rz + roofOff))
                                .arg(kOpen).arg(roofOff);
            }
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061d fire-auxiliary wall (in a sparse world with a materialized negative"
               " chunk flint-grade ignition of a negative-coordinate plank returns true and the"
               " burning query confirms it, ignition next to water on the negative side stays"
               " blocked by the wet-fuel firewall, an out-of-chunk ignition is rejected, and"
               " under rain the rain-exposure predicate holds for an open-sky negative fire"
               " which then deterministically extinguishes before its roofed control fire"
               " [old-gate red payload: ignition false, burning query false, wet check blind on"
               " negative neighbours, rain never reaches outer-ring fire])"
            << (ok ? QString() : diag);
    });

    // ── r2061e：铁轨重连 + 附着复检承重墙（负坐标铺轨连接位 + 负坐标清格火把脱落 + 核心对照）────
    runLeg("r2061e rail-and-attachment wall (in a sparse world with a materialized negative"
        " chunk placing two rails in a row on a negative-coordinate floor computes their"
        " connection bits through the edit hook chain [old-gate red payload: the connection"
        " recompute never runs so both rails stay isolated], and silently clearing the support"
        " under a negative-coordinate torch drops the torch [old-gate red payload: the"
        " attachment recheck is clamped and the torch floats]; the same two faces stay green in"
        " the core domain as the control)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // (1) 铁轨连接重算（负侧）：两轨并排 y=81 于石面 80 → 第二次放置经 checkRailOnEdit 链把
        //   两轨低 4 位连接位重算（含第一根回连）。旧门红载荷 = 连接重算被拒 → 双轨恒孤立 state 0。
        w.setBlock(-9, 80, -8, BR::Stone, 0);
        w.setBlock(-8, 80, -8, BR::Stone, 0);
        w.setBlock(-9, 81, -8, BR::Rail, 0);
        w.setBlock(-8, 81, -8, BR::Rail, 0);
        const bool railConn = (w.stateAt(-9, 81, -8) & 0x0F) != 0
            && (w.stateAt(-8, 81, -8) & 0x0F) != 0;
        ok = ok && railConn;
        if (!railConn)
            diag += QStringLiteral("[rail s1=%1 s2=%2]").arg(w.stateAt(-9, 81, -8))
                        .arg(w.stateAt(-8, 81, -8));
        // (2) 附着复检（负侧）：火把立于石面 → clearBlockSilent 清支撑 → 火把脱落。旧门红载荷 =
        //   recheckAttachmentsAfterClear 固定盒早退 → 火把悬空残留。
        w.setBlock(-8, 80, -12, BR::Stone, 0);
        w.setBlock(-8, 81, -12, BR::Torch, 0);
        const bool torchPlaced = w.blockAt(-8, 81, -12) == BR::Torch;
        const bool cleared = torchPlaced && w.clearBlockSilent(-8, 80, -12);
        const bool torchDropped = cleared && w.blockAt(-8, 81, -12) == BR::Air;
        ok = ok && torchDropped;
        if (!torchDropped)
            diag += QStringLiteral("[torch placed=%1 cleared=%2 left=%3]").arg(torchPlaced)
                        .arg(cleared).arg(w.blockAt(-8, 81, -12));
        // (3) 核心域对照（同两链在正坐标照常——分离「sparse 模式工作」与「外环门」）。
        w.setBlock(20, 80, 20, BR::Stone, 0);
        w.setBlock(21, 80, 20, BR::Stone, 0);
        w.setBlock(20, 81, 20, BR::Rail, 0);
        w.setBlock(21, 81, 20, BR::Rail, 0);
        const bool railCore = (w.stateAt(20, 81, 20) & 0x0F) != 0;
        w.setBlock(20, 80, 24, BR::Stone, 0);
        w.setBlock(20, 81, 24, BR::Torch, 0);
        w.clearBlockSilent(20, 80, 24);
        const bool torchCore = w.blockAt(20, 81, 24) == BR::Air;
        ok = ok && railCore && torchCore;
        if (!(railCore && torchCore))
            diag += QStringLiteral("[core rail=%1 torch=%2]").arg(railCore).arg(torchCore);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061e rail-and-attachment wall (in a sparse world with a materialized"
               " negative chunk placing two rails in a row on a negative-coordinate floor"
               " computes their connection bits through the edit hook chain [old-gate red"
               " payload: the connection recompute never runs so both rails stay isolated], and"
               " silently clearing the support under a negative-coordinate torch drops the"
               " torch [old-gate red payload: the attachment recheck is clamped and the torch"
               " floats]; the same two faces stay green in the core domain as the control)"
            << (ok ? QString() : diag);
    });

    // ── r2061f：fixed 世界零变化墙（实体负列冻结 + 冻结位引爆 + 辅助谓词域外恒假 + 核心回归柱）──
    runLeg("r2061f fixed-world zero-change wall (in a fixed world where the whole domain is the"
        " core all members keep their exact former behavior: a primed tnt and a dropped item in"
        " a negative column stay frozen at their spawn height, the primed tnt still detonates in"
        " place at fuse expiry while out-of-domain terrain stays untouched, ignition and burning"
        " and rain queries on negative coordinates return false and out-of-domain rail placement"
        " is rejected, and a core-domain water source still flows across its floor as the"
        " key-layout regression control)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedResidualWorld(w);
        // (1) primed TNT 负列：冻结（无重力）→ fuse 到期仍在冻结位引爆（fixed 域外不可置块，引爆
        //     零破坏面由 destroySphereSilent Fixed 域门承管——本腿只钉「无重力 + 引信照常走完」）。
        EntityManager em;
        em.spawnPrimedTnt(-8, 84, -8, 0.8f);
        tickEntityResidual(em, w, 30);
        const float tntY = em.posAt(0).y();
        const bool frozen = tntY == 84.5f; // Fixed 分支 continue 原样：pos 不动
        ok = ok && frozen;
        if (!frozen) diag += QStringLiteral("[tnt y=%1]").arg(tntY);
        tickEntityResidual(em, w, 570);
        const bool detInPlace = em.primedCount() == 0;
        ok = ok && detInPlace;
        if (!detInPlace)
            diag += QStringLiteral("[det primed=%1]").arg(em.primedCount());
        // (2) Item 负列冻结（EntityStore::tick Fixed 分支原样）。Mob 无 fixed 冻结面可钉——fixed 模式
        //     AI 位置钳制（finite world 合法行为）把 mob 恒保持在域内，Mob tick 门 Fixed 分支因此**结构
        //     性不可达**（零变化面由钳制结构保证，非本门承管）；sparse 侧 mob 门行为已由 r2061b 承重。
        ItemEntityManager iem;
        iem.spawnItem(-8, 84, -2, int(BR::Planks), 1);
        const float itemY0 = iem.posAt(0).y();
        tickItemResidual(iem, w, 200);
        const bool itemFrozen = iem.aliveAt(0) && iem.posAt(0).y() == itemY0;
        ok = ok && itemFrozen;
        if (!itemFrozen)
            diag += QStringLiteral("[item y0=%1 y=%2]").arg(itemY0).arg(iem.posAt(0).y());
        // (3) 辅助谓词域外恒假 + 域外放轨拒（Fixed 分支现行语句原样的行为级）。
        const bool auxFalse = !w.igniteFlammableAt(-8, 84, -8) && !w.isBurningAt(-8, 84, -8)
            && !w.fireRainExposedAt(-8, 84, -8);
        const bool railReject = !w.setBlock(-9, 81, -8, BR::Rail, 0);
        ok = ok && auxFalse && railReject;
        if (!(auxFalse && railReject))
            diag += QStringLiteral("[aux=%1 rail=%2]").arg(auxFalse).arg(railReject);
        // (4) 核心域回归柱（键位别名零影响——[0,32767) 域上新旧打包逐位同值）：正坐标水照常铺展。
        for (int x = 20; x <= 22; ++x)
            for (int z = 20; z <= 22; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(21, 81, 21, BR::Water, 0);
        for (int t = 0; t < 100; ++t) w.tickWaterFlow();
        const bool coreFlow = w.blockAt(20, 81, 21) == BR::Water && w.stateAt(20, 81, 21) > 0
            && w.blockAt(22, 81, 21) == BR::Water && w.stateAt(22, 81, 21) > 0;
        ok = ok && coreFlow;
        if (!coreFlow) diag += QStringLiteral("[coreFlow=%1]").arg(coreFlow);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061f fixed-world zero-change wall (in a fixed world where the whole domain"
               " is the core all members keep their exact former behavior: a primed tnt and a"
               " dropped item in a negative column stay frozen at their spawn height, the primed"
               " tnt still detonates in place at fuse expiry while out-of-domain terrain stays"
               " untouched, ignition and burning and rain queries on negative coordinates return"
               " false and out-of-domain rail placement is rejected, and a core-domain water"
               " source still flows across its floor as the key-layout regression control)"
            << (ok ? QString() : diag);
    });

    // ── r2061g：结构钉（二十七站点锚注 + 别名/掩码/域门行计数 + r2059/r2060 复钉 + 批头/留池族）────
    //   NEG 靶行按恰红设计收账：实体三锚注（NEG-1 摘即红）+ 键域别名/铁轨锚注（NEG-2 摘即红）。
    //   计数钉（别名体/掩码/域门行/y 门行）与双 NEG 靶行零交集者恒绿，作漂移防线。
    runLeg("r2061g structure pins (each t1091 split site keeps its unique anchor comment"
        " [twenty-four world.cpp sites plus the primed-tnt and mob anchors in entitymanager and"
        " the item-entity anchor in entitystore], the growth-key alias bodies and the preserved"
        " packLeafCell mask literal and the new sparse y-gate lines appear at their exact"
        " counts, the r2059 materialization-gate line and the r2060 y-gate and mask pins stay"
        " untouched, the t1089/t1090 anchor families and the world.h declaration notes remain,"
        " and the t1091 batch header plus its pool registrations and consumption note remain)",
        [&]() {
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
        QFile esf(srcRoot + QStringLiteral("/Entities/entitystore.cpp"));
        const QString sSrc = esf.open(QIODevice::ReadOnly) ? QString::fromUtf8(esf.readAll()) : QString();
        // (g1) t1091 十九站点唯一锚注（NEG 靶行）。
        const QStringList sites = {
            QStringLiteral("t1091 域门（primed TNT tick）两模式分流"),
            QStringLiteral("t1091 域门（Mob tick）两模式分流"),
            QStringLiteral("t1091 域门（ItemEntity tick）两模式分流"),
            QStringLiteral("t1091 键域（packGrowthCell）符号假设收口"),
            QStringLiteral("t1091 域门（tickFire 快照门）两模式分流"),
            QStringLiteral("t1091 域门（tickFire 燃料扫描门）两模式分流"),
            QStringLiteral("t1091 域门（tickFire 蔓延扫描门）两模式分流"),
            QStringLiteral("t1091 邻域门（tickFire 同态蔓延邻扫）两模式分流"),
            QStringLiteral("t1091 域门（tickFire 燃烧门）两模式分流"),
            QStringLiteral("t1091 邻域门（fireSupportedAt）两模式分流"),
            QStringLiteral("t1091 邻域门（fireWaterNeighborAt）两模式分流"),
            QStringLiteral("t1091 域门（fireRainExposedAt）两模式分流"),
            QStringLiteral("t1091 域门（igniteFlammableAt）两模式分流"),
            QStringLiteral("t1091 域门（isBurningAt）两模式分流"),
            QStringLiteral("t1091 域门（recheckAttachmentsAfterClear）两模式分流"),
            QStringLiteral("t1091 邻域门（recheckAttachmentsAfterClear 火把邻扫）两模式分流"),
            QStringLiteral("t1091 域门（recomputeRailConnections）两模式分流"),
            QStringLiteral("t1091 邻域门（tickWaterFlow 触岩浆邻扫）两模式分流"),
            QStringLiteral("t1091 域门（tickWaterFlow 冲刷邻格）两模式分流"),
            QStringLiteral("t1091 域门（tickWaterFlow 冲耕邻格）两模式分流"),
            QStringLiteral("t1091 邻域门（tickWaterFlow 源计数邻扫）两模式分流"),
            QStringLiteral("t1091 邻域门（tickWaterFlow 蒸发支撑邻扫）两模式分流"),
            QStringLiteral("t1091 邻域门（tickWaterFlow 水平蔓延邻扫）两模式分流"),
            QStringLiteral("t1091 邻域门（tickLavaFlow 触水邻扫A）两模式分流"),
            QStringLiteral("t1091 邻域门（tickLavaFlow 触水邻扫B）两模式分流"),
            QStringLiteral("t1091 邻域门（tickLavaFlow 蒸发支撑邻扫）两模式分流"),
            QStringLiteral("t1091 邻域门（tickLavaFlow 水平蔓延邻扫）两模式分流") };
        int g1Bad = 0;
        QString g1Which;
        for (const QString &s : sites) {
            const int cnt = wSrc.count(s) + (s.startsWith(QStringLiteral("t1091 域门（primed TNT"))
                    || s.startsWith(QStringLiteral("t1091 域门（Mob tick"))
                ? eSrc.count(s) : 0)
                + (s.startsWith(QStringLiteral("t1091 域门（ItemEntity")) ? sSrc.count(s) : 0);
            if (cnt != 1) { ++g1Bad; g1Which += QString::number(cnt) + QLatin1Char('x'); }
        }
        const bool g1 = g1Bad == 0;
        ok = ok && g1;
        if (!g1) diag += QStringLiteral("[g1 bad=%1 %2]").arg(g1Bad).arg(g1Which);
        // (g2) 别名体 / 掩码 / 新 y 门行计数（NEG-2 靶行：键域锚注 + 别名体——摘别名还原 quint16 体
        //     即 0）；其余计数为漂移防线（与双 NEG 零交集）。
        const int aliasPack = wSrc.count(QStringLiteral("return packLeafCell(x, y, z);"));
        const int aliasUnpack = wSrc.count(QStringLiteral("unpackLeafCell(k, x, y, z);"));
        const int mask = wSrc.count(QStringLiteral("0x3FFFFFFu"));          // r2060 f3 复钉 =4
        const int gateH = wSrc.count(QStringLiteral("} else if (y < 0 || y >= H) {"));   // =2
        const int gateNH = wSrc.count(QStringLiteral("} else if (ny < 0 || ny >= H) {")); // =6（火 3 + 流体 3）
        const int gateNmH = wSrc.count(QStringLiteral("} else if (ny < 0 || ny >= m_height) {")); // =3（含 t1090 级联既有 1）
        const int gateTmH = wSrc.count(QStringLiteral("} else if (ty < 0 || ty >= m_height) {")); // =1
        const bool g2 = aliasPack == 1 && aliasUnpack == 1 && mask == 4
            && gateH == 2 && gateNH == 6 && gateNmH == 3 && gateTmH == 1;
        ok = ok && g2;
        if (!g2)
            diag += QStringLiteral("[g2 pack=%1 unp=%2 mask=%3 yH=%4 nyH=%5 nymH=%6 tymH=%7]")
                        .arg(aliasPack).arg(aliasUnpack).arg(mask)
                        .arg(gateH).arg(gateNH).arg(gateNmH).arg(gateTmH);
        // (g3) r2059/r2060 承重复钉（零变化墙复钉——本批不得扰动）。
        const int matGate = wSrc.count(QStringLiteral(
            "if (!m_chunks.chunkContentPresent(floorDiv(x, Chunk::kSize), floorDiv(z, Chunk::kSize)))")); // =7
        const int c2gate = wSrc.count(QStringLiteral("} else if (y < 0 || y >= m_height) {")); // =3
        const int c2scan = wSrc.count(QStringLiteral("} else if (by < 0 || by >= m_height) {")); // =1
        const bool g3 = matGate == 7 && c2gate == 3 && c2scan == 1;
        ok = ok && g3;
        if (!g3)
            diag += QStringLiteral("[g3 mat=%1 gate=%2 scan=%3]").arg(matGate).arg(c2gate).arg(c2scan);
        // (g4) t1089/t1090 锚注族复钉（r2059g/r2060f 承重面回钉）+ world.h 注 ==6。
        const bool g4 = wSrc.count(QStringLiteral("t1089 域门（setBlockFromEntity）两模式分流")) == 1
            && wSrc.count(QStringLiteral("t1090 键域（packLeafCell）符号假设收口")) == 1
            && wSrc.count(QStringLiteral("t1090 域门（destroySphereSilent 扫描门）两模式分流")) == 1
            && eSrc.count(QStringLiteral("t1090 域门（FallingBlock tick）两模式分流")) == 1
            && hSrc.count(QStringLiteral("t1089：域门两模式分流")) == 6
            && hSrc.contains(QStringLiteral("kLeafDecayPct          = 1"));
        ok = ok && g4;
        if (!g4) diag += QStringLiteral("[g4 family]");
        // (g5) 批头锚注族 + 留池登记 + 清偿注 + kFireTickInterval 字面钉（腿 5 调/窗复刻基准）。
        const bool g5 = wSrc.contains(QStringLiteral("t1091 写门家族「核心域假设」同族清偿·第三批残项"))
            && wSrc.contains(QStringLiteral("t1090 清偿注（后续批落地回填）"))
            && wSrc.contains(QStringLiteral("留池登记（本批现场新发现，禁顺手修，后续批另立单）"))
            && wSrc.contains(QStringLiteral("仍留池"))
            && wSrc.contains(QStringLiteral("t1091 子钩子留池登记"))
            && wSrc.contains(QStringLiteral("tickRedstone inBounds"))
            && eSrc.contains(QStringLiteral("留池不修"))
            && hSrc.contains(QStringLiteral("kFireTickInterval  = 5"));
        ok = ok && g5;
        if (!g5) diag += QStringLiteral("[g5 hdr=%1 pool=%2 fire=%3]")
                            .arg(wSrc.contains(QStringLiteral(
                                "t1091 写门家族「核心域假设」同族清偿·第三批残项")))
                            .arg(wSrc.contains(QStringLiteral("仍留池")))
                            .arg(hSrc.contains(QStringLiteral("kFireTickInterval  = 5")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2061g structure pins (each t1091 split site keeps its unique anchor comment"
               " [twenty-four world.cpp sites plus the primed-tnt and mob anchors in entitymanager"
               " and the item-entity anchor in entitystore], the growth-key alias bodies and the"
               " preserved packLeafCell mask literal and the new sparse y-gate lines appear at"
               " their exact counts, the r2059 materialization-gate line and the r2060 y-gate"
               " and mask pins stay untouched, the t1089/t1090 anchor families and the world.h"
               " declaration notes remain, and the t1091 batch header plus its pool"
               " registrations and consumption note remain)"
            << (ok ? QString() : diag);
    });
}
