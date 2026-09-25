#include "matrix_helpers.h"

// t1096 蕴辉瓶（投掷释经验）探针段（4 腿；filter 词 r2066；矩阵 802→806）。置尾先例沿用（接 section65，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig）。
//
// 任务契约：§14 池底重盘登记（t1094 复盘新增候选；机制等价 MC 1.0「投掷后碎裂释放经验球的玻璃瓶」，
//   Java 1.0.0 正式版内物品）。三语义：①右键投掷 → 抛物弹丸（同蛋 / 雪球家族：初速 12、世界重力 28、
//   视线朝向）；②触方块 / 活体 mob 即碎——**未击中任何实体也照碎**（触地即碎，MC 移动对象被截停即
//   onImpact 口径）；③碎裂释放经验球，总量 3 + rand(5) + rand(5) ∈ [3,11]（均值 7；wiki「3–11
//   experience」口径），按 canonical XP split 阈值链拆球。交付面：GlimmerBottleId 0x263（材料段尾追加）
//   + Kind::GlimmerBottle（枚举尾追加）+ spawnGlimmerBottle / glimmerBottleBreak + XpOrbManager::
//   spawnOrbsForTotal（拆球收口 Entities 层）+ 右键投掷分支（playercontroller，创造不耗 / 生存消耗 1）+
//   名面「蕴辉瓶」+ 创造调色板尾行 + MaterialIcon 自绘（drawGlimmerBottle）。零方块 / 零图集（BR::Count
//   148 / AtlasTileCount 193 原值反探钉）。
//
// 生存获取面纪元裁定（recipe.h 留痕）：1.0 无合成配方；村民交易（牧师售瓶）= 1.3.1+ 机制且本工程无
//   村民系统 → **创造专属**如实登记（不虚构合成 / 战利品来源）。
//
// 腿面与阴性面设计（恰红归因先于腿文）：
//   r2066a 右键投掷行为柱（真 pc 发射链 t891 同门：出生点 = 眼位 + 视前 0.5 / 水平匀速 0.6 格每 tick =
//     初速 12 × dt / 垂直落差逐 tick 严格递增 = 重力 / 创造不耗 / 生存消耗 1 / 挥手 / 端到端触地碎 +
//     信号总量 ∈ [3,11]）。
//     NEG-1（摘投掷入口：playercontroller 蕴辉瓶分支整段注释）→ 恰红 = {r2066a, r2066d}（a 无实体
//     生成 + d 入口源钉失配），r2066b/c 不受影响（EntityManager / XpOrbManager 直编面零涉入
//     playercontroller）。
//   r2066b 触地即碎承重墙（直编探针：无任何 mob 的半空下落 → 恰在触地 tick 碎 + 命中格精确
//     (24,80,24) + 零世界变更；mob 触碰变体：瓶生于 mob 中心 → 首 tick 碎 + mob 满血零红闪 =
//     0 伤害 0 击退语义）。
//     NEG-2（摘释放调用：entitymanager tick 内 emit glimmerBottleBreak 换 Q_UNUSED no-op）→ 恰红 =
//     {r2066a, r2066b, r2066d}（a 的端到端阶段断言「触地碎恰一次信号」、b 的恰一次信号断言、d 的
//     释放接口源钉三面同失）；r2066c 不受影响（XpOrbManager 直编不经 emit）。**a 亦红为实测订正**
//     （初稿头部误记 {b, d}——a 的末段本就含信号恰一次断言，NEG 实跑后据实修正归因）。
//   r2066c XP 释放面（XpOrbManager::spawnOrbsForTotal 直编：totals 0..15 守恒 sum==total + canonical
//     split 逐值精确 + 非法 total<=0 no-op 防御 + glimmerBottleXpTotal 确定性 seed roll：256 seed 全
//     [3,11] + 两端 3/11 均可达 + 同 seed 复现）。
//   r2066d 结构钉（0x263 段尾追加序 + Kind 枚举尾追加 + 名面 / 调色板尾邻 / maxStack 双层面 64 +
//     投掷入口 / 释放链 / 拆球阈值 / QML 路由 / QML delegate / 图标 case 源钉族（剥注释 pinSet）+
//     纪元裁定锚注（裸读）+ 零方块 / 零图集反探 + 相邻族零污染[蛋 / 雪球 / 可可豆行原样]）。
//
// rig 纪律：fixed 48×48×96 s82 宿主（四 setter，section65 同款）；rig 坪取 y=80..81 高空（s82 地形 /
//   树冠带之上，t769 教训：探针工作体积先清空气——本段坪上 y 81..92 全清）；真链 pc rig 的捕获载体窗
//   仅作指针捕获（无 show，t891 同款）；rig 基建全经 setBlock 主门；坐标不作哨兵。

namespace {

// fixed 宿主小世界 incantation（section65 同款四 setter；默认构造 World 原地配置）。
inline void initFixedBottleWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空（t769 教训：工作体积确定性清空，地形巧合逐出测试变量）。坪 = y=80（x/z
//   px0..px1 / pz0..pz1 闭区间），上空 y 81..92 清 Air。
inline void layBottlePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

} // namespace

void MatrixRun::section66_glimmer_bottle()
{
    // ── r2066a：右键投掷行为柱（真 pc 发射链 + 初速 / 重力 / 朝向 + 消耗口径）────────────────────
    runLeg("r2066a right-click throw behavior column (a real PlayerController creative throw spawns a"
        " GlimmerBottle entity whose birth position is eye + look*0.5 along the thrown direction, whose"
        " horizontal advance per tick is constant at speed*dt and whose vertical drop strictly increases"
        " per tick [gravity], with the hotbar stack untouched and the swing arm fired in creative, the"
        " follow-up survival throw consumes exactly one bottle, and the first bottle eventually shatters"
        " on the stone platform with a single glimmerBottleBreak signal carrying a total in [3,11])",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBottleWorld(w);
        layBottlePlatform(w, 18, 44, 18, 30);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        // 捕获载体窗（t891 同门）：headless 无指针锁定 → 窗仅作 grab/setCaptured 载体，无 show。
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线（t1040 rig 加固同款）
        hb.setStack(0, RecipeRegistry::GlimmerBottleId, 5, 0);
        hb.setSelectedSlot(0);
        // 创造链：脚位 (24.5, 81, 24.5)（坪顶）+ 水平朝 +X（yaw=-90°，pitch=0）。
        const QVector3D feet(24.5f, 81.0f, 24.5f);
        const QVector3D look(1.0f, 0.0f, 0.0f);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 1 /* Creative */);
        pumpFor(17);
        pc.tick(); // settle（碰撞落位）
        int swingSeen = 0;
        QObject::connect(&pc, &PlayerController::swingArm, &pc, [&]() { ++swingSeen; });
        int breakCount = 0, breakX = -1, breakY = -1, breakZ = -1, breakXp = -1;
        QObject::connect(&ents, &EntityManager::glimmerBottleBreak, &ents,
                         [&](int x, int y, int z, int xp) {
                             ++breakCount; breakX = x; breakY = y; breakZ = z; breakXp = xp;
                         });
        const int beforeCount = ents.count();
        pc.placeBlock(); // 右键投掷（创造）
        int slot = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::GlimmerBottle)) { slot = i; break; }
        const bool spawnedOk = slot >= 0 && ents.count() > beforeCount
                               && hb.blockIdAt(0) == RecipeRegistry::GlimmerBottleId
                               && hb.countAt(0) == 5 /* 创造不耗 */ && swingSeen >= 1;
        ok = ok && spawnedOk;
        if (!spawnedOk)
            diag += QStringLiteral("[spawn slot=%1 cnt=%2 id=%3 n=%4 swing=%5]")
                        .arg(slot).arg(ents.count()).arg(hb.blockIdAt(0)).arg(hb.countAt(0)).arg(swingSeen);
        // 飞行物理（初速 / 朝向 / 重力行为级）：出生点 = 眼位 + look*0.5；水平匀速 12*0.05=0.6 / tick；
        //   垂直落差逐 tick 严格递增（重力累积）。前三 tick 不触地（眼高 1.62 + 0.5 缓冲）。
        const QVector3D eye = feet + QVector3D(0.0f, 1.62f, 0.0f);
        const QVector3D p0 = ents.posAt(slot);
        const bool birthOk = slot >= 0
            && std::abs(p0.x() - (eye.x() + look.x() * 0.5f)) < 1e-2f
            && std::abs(p0.y() - (eye.y() + look.y() * 0.5f)) < 1e-2f
            && std::abs(p0.z() - (eye.z() + look.z() * 0.5f)) < 1e-2f;
        ok = ok && birthOk;
        if (!birthOk) diag += QStringLiteral("[birth %1 %2 %3]").arg(p0.x()).arg(p0.y()).arg(p0.z());
        const QVector3D p1 = [&] { ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false); return ents.posAt(slot); }();
        const QVector3D p2 = [&] { ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false); return ents.posAt(slot); }();
        const QVector3D p3 = [&] { ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false); return ents.posAt(slot); }();
        const float adv1 = p1.x() - p0.x(), adv2 = p2.x() - p1.x(), adv3 = p3.x() - p2.x();
        const float dr1 = p0.y() - p1.y(), dr2 = p1.y() - p2.y(), dr3 = p2.y() - p3.y();
        // 初速 12（0.6±0.05 / tick）+ 水平匀速（逐 tick 差 <1e-3）+ 垂直落差为正且严格递增（重力）。
        const bool physOk = adv1 > 0.55f && adv1 < 0.65f
            && std::abs(adv2 - adv1) < 1e-3f && std::abs(adv3 - adv2) < 1e-3f
            && dr1 > 0.0f && dr2 > dr1 && dr3 > dr2;
        ok = ok && physOk;
        if (!physOk)
            diag += QStringLiteral("[phys adv=%1/%2/%3 dr=%4/%5/%6]")
                        .arg(adv1).arg(adv2).arg(adv3).arg(dr1).arg(dr2).arg(dr3);
        // 端到端触地碎（创造链这枚）：坪顶 y=81 起 1.62 格落差 → ~7 tick 内触坪碎；信号恰一次 +
        //   总量 ∈ [3,11] + 命中行 = 坪行 y=80（触地即碎格语义：瓶穿透至实心格 floor）。
        bool brokeOk = false;
        for (int t = 0; t < 30 && !brokeOk; ++t) {
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
            brokeOk = breakCount == 1;
        }
        const bool signalOk = brokeOk && breakXp >= 3 && breakXp <= 11 && breakY == 80
                              && !ents.aliveAt(slot);
        ok = ok && signalOk;
        if (!signalOk)
            diag += QStringLiteral("[signal n=%1 xp=%2 cell=%3,%4,%5 alive=%6]")
                        .arg(breakCount).arg(breakXp).arg(breakX).arg(breakY).arg(breakZ)
                        .arg(ents.aliveAt(slot));
        // 生存链：换 Survival 重掷（> 放置 CD 200ms 泵——链 A 发射已刷新 m_lastPlaceMs）。
        hb.setStack(0, RecipeRegistry::GlimmerBottleId, 5, 0);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 2 /* Survival */);
        pumpFor(320);
        pc.tick();
        const int before2 = ents.count();
        pc.placeBlock();
        // t256 slot-reuse：链 A 瓶已碎释放槽 → 链 B 瓶大概率复用同槽 → count 单调不升。存活判定改
        //   「掷后存在活体 GlimmerBottle 槽」（t891 链 B 注释同款教训：不可按 count 增长断言新弹）。
        int slot2 = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::GlimmerBottle)) { slot2 = i; break; }
        const bool survivalConsumed = hb.blockIdAt(0) == RecipeRegistry::GlimmerBottleId
                                      && hb.countAt(0) == 4 && slot2 >= 0;
        ok = ok && survivalConsumed;
        if (!survivalConsumed)
            diag += QStringLiteral("[surv n=%1->%2 slot=%3 cnt=%4]")
                        .arg(5).arg(hb.countAt(0)).arg(slot2).arg(ents.count());
        // 零世界变更抽查（投掷不放置方块）：坪面 / 飞行走廊抽格仍 Stone / Air。
        const bool worldQuiet = w.blockAt(24, 80, 24) == BR::Stone
                                && w.blockAt(26, 82, 24) == BR::Air
                                && w.blockAt(30, 81, 24) == BR::Air;
        ok = ok && worldQuiet;
        if (!worldQuiet) diag += QStringLiteral("[worldQuiet]");

        probeWin.deleteLater(); // 捕获载体窗随探针作用域收尾（t891 配对纪律）
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2066a right-click throw behavior column (a real PlayerController creative throw"
               " spawns a GlimmerBottle entity whose birth position is eye + look*0.5 along the"
               " thrown direction, whose horizontal advance per tick is constant at speed*dt and"
               " whose vertical drop strictly increases per tick [gravity], with the hotbar stack"
               " untouched and the swing arm fired in creative, the follow-up survival throw"
               " consumes exactly one bottle, and the first bottle eventually shatters on the"
               " stone platform with a single glimmerBottleBreak signal carrying a total in [3,11])"
            << (ok ? QString() : diag);
    });

    // ── r2066b：触地即碎承重墙（无实体也碎 + mob 触碰 0 伤害 + 命中格精确 + 零世界变更）────────────
    runLeg("r2066b shatter-on-touch load-bearing wall (a bottle dropped through empty air with zero"
        " mobs in the world still shatters exactly on its ground-contact tick at the precise solid"
        " cell [touch ground breaks even without hitting any entity], the world grid is bit-identical"
        " around the rig, and a bottle born inside a mob's body shatters on the first tick with the"
        " mob left at full health and zero hurt flash [no damage, no knockback])", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBottleWorld(w);
        layBottlePlatform(w, 20, 30, 20, 30);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        int breakCount = 0, breakX = -1, breakY = -1, breakZ = -1, breakXp = -1;
        QObject::connect(&ents, &EntityManager::glimmerBottleBreak, &ents,
                         [&](int x, int y, int z, int xp) {
                             ++breakCount; breakX = x; breakY = y; breakZ = z; breakXp = xp;
                         });
        // (1) 无实体也碎（触地即碎）：坪上空 (24.5, 85, 24.5) 静止下落（vy0=-6），全坪零 mob。落距
        //     4 格（85→81 触坪判定在 next 落入实心格 80 的 tick）→ 恰在触地 tick 碎，命中格 = 实心格
        //     (24, 80, 24) 精确（穿透格 floor 口径，同珍珠落点约定）。
        const int b1 = ents.spawnGlimmerBottle(QVector3D(24.5f, 85.0f, 24.5f), QVector3D(0.0f, -6.0f, 0.0f));
        const bool solo = b1 >= 0 && ents.liveCount() == 1; // 全坪零 mob 前置（仅瓶自身）
        bool fell = false;
        for (int t = 0; t < 100 && !fell; ++t) {
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
            fell = !ents.aliveAt(b1);
        }
        const bool groundBreak = solo && fell && breakCount == 1
                                 && breakX == 24 && breakY == 80 && breakZ == 24
                                 && breakXp >= 3 && breakXp <= 11;
        ok = ok && groundBreak;
        if (!groundBreak)
            diag += QStringLiteral("[ground solo=%1 fell=%2 n=%3 cell=%4,%5,%6 xp=%7]")
                        .arg(solo).arg(fell).arg(breakCount).arg(breakX).arg(breakY)
                        .arg(breakZ).arg(breakXp);
        // (2) 零世界变更：坪面仍 Stone / 上空仍 Air（碎裂不产生 / 不破坏任何方块）。
        const bool worldQuiet = w.blockAt(24, 80, 24) == BR::Stone
                                && w.blockAt(24, 81, 24) == BR::Air
                                && w.blockAt(26, 80, 26) == BR::Stone;
        ok = ok && worldQuiet;
        if (!worldQuiet) diag += QStringLiteral("[worldQuiet]");
        // (3) mob 触碰（0 伤害 0 击退语义）：MobTest mob 立坪上 → 瓶生于 mob 中心 + 微升 → 首 tick
        //     next==出生点仍在 mob 外扩 AABB 内 → 碎；mob 满血 + 零红闪（MC 该弹丸 onImpact 只释
        //     经验不攻击）。
        ents.spawnMob(26, 81, 26);
        int mobSlot = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Mob)) { mobSlot = i; break; }
        const int hp0 = ents.healthAt(mobSlot);
        const QVector3D mobPos = ents.posAt(mobSlot);
        const int beforeCount = ents.count();
        const int b2 = ents.spawnGlimmerBottle(mobPos + QVector3D(0.0f, 0.05f, 0.0f),
                                               QVector3D(0.0f, 0.0f, 0.0f));
        bool mobBroke = false;
        for (int t = 0; t < 10 && !mobBroke; ++t) {
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
            mobBroke = b2 >= 0 && !ents.aliveAt(b2);
        }
        const bool mobOk = mobSlot >= 0 && mobBroke && breakCount == 2
                           && ents.aliveAt(mobSlot)
                           && ents.healthAt(mobSlot) == hp0 && hp0 > 0
                           && ents.hurtFlashAt(mobSlot) <= 0.0f
                           && ents.count() == beforeCount + 1; // 仅瓶自身占一槽（slot-reuse 单调：碎后 count 不降；经验球不在本管理器）
        ok = ok && mobOk;
        if (!mobOk)
            diag += QStringLiteral("[mob slot=%1 broke=%2 n=%3 hp=%4/%5 flash=%6]")
                        .arg(mobSlot).arg(mobBroke).arg(breakCount).arg(hp0)
                        .arg(mobSlot >= 0 ? ents.healthAt(mobSlot) : -1)
                        .arg(mobSlot >= 0 ? ents.hurtFlashAt(mobSlot) : -1.0f);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2066b shatter-on-touch load-bearing wall (a bottle dropped through empty air"
               " with zero mobs in the world still shatters exactly on its ground-contact tick at"
               " the precise solid cell [touch ground breaks even without hitting any entity], the"
               " world grid is bit-identical around the rig, and a bottle born inside a mob's body"
               " shatters on the first tick with the mob left at full health and zero hurt flash"
               " [no damage, no knockback])"
            << (ok ? QString() : diag);
    });

    // ── r2066c：XP 释放面（拆球守恒 + canonical split 逐值 + 防御 no-op + 确定性 seed roll 信封）──────
    runLeg("r2066c xp release face (spawnOrbsForTotal conserves the total exactly for every amount"
        " 0..15, the orb multiset matches the canonical XP split chain value by value [1,3,7,15"
        " thresholds], non-positive totals are a counted no-op, and the seeded roll behind the"
        " bottle's total stays within [3,11] for 256 seeds with both endpoints reached and exact"
        " same-seed reproducibility)", [&]() {
        bool ok = true;
        QString diag;
        XpOrbManager orbs;
        // canonical split 参照表（阈值链 ≥247/123/63/31/15/7/3/else 1 对 0..15 的精确分割序列）。
        const QList<QList<int>> expect = {
            {},                       // 0
            { 1 },                    // 1
            { 1, 1 },                 // 2
            { 3 },                    // 3
            { 3, 1 },                 // 4
            { 3, 1, 1 },              // 5
            { 3, 3 },                 // 6
            { 7 },                    // 7
            { 7, 1 },                 // 8
            { 7, 1, 1 },              // 9
            { 7, 3 },                 // 10
            { 7, 3, 1 },              // 11
            { 7, 3, 1, 1 },           // 12
            { 7, 3, 3 },              // 13
            { 7, 7 },                 // 14
            { 15 },                   // 15
        };
        bool conserveOk = true, splitOk = true, amountsCanonical = true;
        int base = 0; // 本腿独占管理器 + 全程零释放 → 槽位严格按序追加（slot-reuse LIFO 无扰动）
        for (int total = 0; total <= 15; ++total) {
            const int c0 = orbs.count();
            orbs.spawnOrbsForTotal(20 + total, 40, 20, total);
            const int spawned = orbs.count() - c0;
            const QList<int> &want = expect[total];
            if (spawned != want.size()) { splitOk = false; break; }
            int sum = 0;
            for (int k = 0; k < spawned; ++k) {
                const int idx = base + k;
                if (!orbs.aliveAt(idx)) { splitOk = false; break; }
                const int amt = orbs.amountAt(idx);
                sum += amt;
                if (!want.contains(amt)) amountsCanonical = false;
                // 逐值精确（spawn 序 == 分割序，纯确定性）：同位比对。
                if (amt != want[k]) splitOk = false;
            }
            if (sum != total) conserveOk = false;
            base += spawned;
        }
        ok = ok && conserveOk && splitOk && amountsCanonical;
        if (!(conserveOk && splitOk && amountsCanonical))
            diag += QStringLiteral("[split cons=%1 exact=%2 canon=%3]").arg(conserveOk).arg(splitOk)
                        .arg(amountsCanonical);
        // 防御 no-op：total<=0 不产球（while 自然不进）。
        const int c0 = orbs.count();
        orbs.spawnOrbsForTotal(30, 40, 30, 0);
        orbs.spawnOrbsForTotal(30, 40, 30, -7);
        const bool noOp = orbs.count() == c0;
        ok = ok && noOp;
        if (!noOp) diag += QStringLiteral("[noop %1->%2]").arg(c0).arg(orbs.count());
        // 确定性 seed roll 信封（GlimmerBottle 经验总量的真值源函数）：256 seed 全 [3,11] + 两端均可达
        //   + 同 seed 复现（QRandomGenerator(seed) 确定性，LootTable::roll seed 重载同门）。
        bool inRange = true, reproOk = true;
        int sawMin = 99, sawMax = -1;
        for (quint32 s = 0; s < 256; ++s) {
            const int v = EntityManager::glimmerBottleXpTotal(s);
            if (v < 3 || v > 11) inRange = false;
            if (v < sawMin) sawMin = v;
            if (v > sawMax) sawMax = v;
            if (EntityManager::glimmerBottleXpTotal(s) != v) reproOk = false;
        }
        const bool envelope = inRange && reproOk && sawMin == 3 && sawMax == 11;
        ok = ok && envelope;
        if (!envelope)
            diag += QStringLiteral("[envelope in=%1 repro=%2 min=%3 max=%4]")
                        .arg(inRange).arg(reproOk).arg(sawMin).arg(sawMax);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2066c xp release face (spawnOrbsForTotal conserves the total exactly for every"
               " amount 0..15, the orb multiset matches the canonical XP split chain value by value"
               " [1,3,7,15 thresholds], non-positive totals are a counted no-op, and the seeded roll"
               " behind the bottle's total stays within [3,11] for 256 seeds with both endpoints"
               " reached and exact same-seed reproducibility)"
            << (ok ? QString() : diag);
    });

    // ── r2066d：结构钉（id 段位 + 呈现三面 + 源钉族 + 零方块/零图集反探 + 相邻族零污染）──────────────
    runLeg("r2066d structure pins (GlimmerBottleId sits at 0x263 as the material-section tail append"
        " right after CocoaBeanId 0x262 with the Kind enum appended after Bobber, the name face reads"
        " the original name with the palette row adjacent after the cocoa bean row and the stack cap"
        " 64 on both layers, the full wiring source-pin family holds across the throw entry, the"
        " entity spawn and break emission, the orb split chain, the QML routing and delegate and icon"
        " case, zero blocks and zero atlas tiles were added, and the neighbouring egg snowball and"
        " cocoa faces are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位 + 段尾追加序（可可豆 0x262 → 蕴辉瓶 0x263；Kind 枚举 Bobber 之后尾追加）。
        const bool ids = RecipeRegistry::GlimmerBottleId == 0x263
                         && RecipeRegistry::CocoaBeanId == 0x262
                         && int(EntityManager::GlimmerBottle) == int(EntityManager::Bobber) + 1;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 呈现三面：名面原创名 + 调色板尾邻 + maxStack 双层面 64 + 材料段判定。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::GlimmerBottleId) == QStringLiteral("蕴辉瓶");
        int cocoaIdx = -1, bottleIdx = -1;
        const QVariantList mats = hb.creativeMaterials();
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::CocoaBeanId) cocoaIdx = i;
            if (v == RecipeRegistry::GlimmerBottleId) bottleIdx = i;
        }
        const bool paletteOk = cocoaIdx >= 0 && bottleIdx == cocoaIdx + 1;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::GlimmerBottleId) == 64
                             && BR::maxStackSize(RecipeRegistry::GlimmerBottleId) == 64
                             && hb.isMaterial(RecipeRegistry::GlimmerBottleId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3 stack=%4]")
                        .arg(nameOk).arg(cocoaIdx).arg(bottleIdx).arg(stackOk);
        // (3) 零方块 / 零图集反探（t1092/t1095 同门）：方块总数与图集瓦片数原值不动。
        const bool zeroWorld = int(BR::Count) == 149 && int(BR::AtlasTileCount) == 195; // t1097 lawful 前移：148→149 / 193→195（酿造台方块 + tile 193..194 追加——本反探钉随段尾追加前移）
        ok = ok && zeroWorld;
        if (!zeroWorld) diag += QStringLiteral("[zeroWorld count=%1 tiles=%2]")
                                    .arg(int(BR::Count)).arg(int(BR::AtlasTileCount));
        // (4) 全链源钉族（剥注释 pinSet 锚真实语句；QML 注释同样按 // / /* */ 剥）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto readRaw = [&srcRoot](const char *rel) {
            QFile f(srcRoot + QString::fromLatin1(rel));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("throw entry condition", "heldItemId == RecipeRegistry::GlimmerBottleId", 1),
            SrcPin("throw spawn call", "spawnGlimmerBottle(origin, look * kPlayerGlimmerBottleSpeed)", 1),
            SrcPin("throw speed constant", "constexpr float kPlayerGlimmerBottleSpeed = 12.0f;", 1)});
        const QStringList missEm = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("spawn def unique", "int EntityManager::spawnGlimmerBottle(const QVector3D &origin, const QVector3D &vel)", 1),
            SrcPin("kind assignment", "e.kind = GlimmerBottle;", 1),
            SrcPin("break emission", "emit glimmerBottleBreak(cellX, cellY, cellZ, totalXp);", 1),
            SrcPin("runtime roll coupling", "glimmerBottleXpTotal(QRandomGenerator::global()->generate())", 1)});
        const QStringList missEmH = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("spawn decl", "Q_INVOKABLE int spawnGlimmerBottle(const QVector3D &origin, const QVector3D &vel);", 1),
            SrcPin("kind enum tail append", "AbyssPearl, Bobber, GlimmerBottle", 1),
            SrcPin("roll fn decl", "static int glimmerBottleXpTotal(quint32 seed);", 1),
            SrcPin("break signal decl", "void glimmerBottleBreak(int x, int y, int z, int totalXp);", 1)});
        const QStringList missXp = pinSet(srcRoot + QStringLiteral("/Entities/xporbmanager.cpp"), {
            SrcPin("split def unique", "void XpOrbManager::spawnOrbsForTotal(int x, int y, int z, int total)", 1),
            SrcPin("split threshold 7", "else if (remaining >= 7)   piece = 7;", 1)});
        const QStringList missXpH = pinSet(srcRoot + QStringLiteral("/Entities/xporbmanager.h"), {
            SrcPin("split decl", "Q_INVOKABLE void spawnOrbsForTotal(int x, int y, int z, int total);", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("palette row", "int(RecipeRegistry::GlimmerBottleId)", 1),
            SrcPin("name row", "return QStringLiteral(\"蕴辉瓶\")", 1)});
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("id def row", "static constexpr int GlimmerBottleId = 0x263;", 1)});
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon case", "case 0x263: drawGlimmerBottle(); break", 1),
            SrcPin("icon draw fn", "const drawGlimmerBottle = () => {", 1)});
        const QStringList missMq = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("qml xp routing", "xpOrbs.spawnOrbsForTotal(x, y, z, totalXp)", 1),
            SrcPin("qml delegate kind gate", "entKind === EntityManager.GlimmerBottle", 1),
            SrcPin("qml delegate icon id", "materialId: 0x263", 1)});
        const bool d4 = missPc.isEmpty() && missEm.isEmpty() && missEmH.isEmpty() && missXp.isEmpty()
            && missXpH.isEmpty() && missHb.isEmpty() && missRh.isEmpty() && missMi.isEmpty()
            && missMq.isEmpty();
        ok = ok && d4;
        if (!d4)
            diag += QStringLiteral("[d4 pc=%1 em=%2 emh=%3 xp=%4 xph=%5 hb=%6 rh=%7 mi=%8 mq=%9]")
                        .arg(missPc.join(QLatin1Char(',')), missEm.join(QLatin1Char(',')),
                             missEmH.join(QLatin1Char(',')), missXp.join(QLatin1Char(',')),
                             missXpH.join(QLatin1Char(',')), missHb.join(QLatin1Char(',')),
                             missRh.join(QLatin1Char(',')), missMi.join(QLatin1Char(',')))
                        .arg(missMq.join(QLatin1Char(',')));
        // (5) 纪元裁定锚注（裸读文件 raw contains——注释体锚文本，同 section51 先例）。
        const QString rhTxt = readRaw("/Game/recipe.h");
        const bool anchors = rhTxt.contains(QStringLiteral("生存获取面纪元裁定（核实留痕）"))
            && rhTxt.contains(QStringLiteral("t1096 蕴辉瓶（glimmer bottle：材料段 0x263"));
        ok = ok && anchors;
        if (!anchors) diag += QStringLiteral("[anchors]");
        // (6) 相邻族零污染（源钉复钉）：蛋 / 雪球 spawn 原样 + 可可豆调色板行仍携尾逗号（瓶行为新尾行）。
        const QStringList missNb = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("egg spawn untouched", "int EntityManager::spawnEgg(const QVector3D &origin, const QVector3D &vel)", 1),
            SrcPin("snowball spawn untouched", "int EntityManager::spawnSnowball(const QVector3D &origin, const QVector3D &vel, int damage, int thrower)", 1)});
        const bool cocoaRow = readRaw("/Game/hotbar.cpp")
                                  .contains(QStringLiteral("int(RecipeRegistry::CocoaBeanId),"));
        const bool nb = missNb.isEmpty() && cocoaRow;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb %1 cocoa=%2]")
                             .arg(missNb.join(QLatin1Char(','))).arg(cocoaRow);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2066d structure pins (GlimmerBottleId sits at 0x263 as the material-section tail"
               " append right after CocoaBeanId 0x262 with the Kind enum appended after Bobber, the"
               " name face reads the original name with the palette row adjacent after the cocoa"
               " bean row and the stack cap 64 on both layers, the full wiring source-pin family"
               " holds across the throw entry, the entity spawn and break emission, the orb split"
               " chain, the QML routing and delegate and icon case, zero blocks and zero atlas"
               " tiles were added, and the neighbouring egg snowball and cocoa faces are untouched)"
            << (ok ? QString() : diag);
    });
}
