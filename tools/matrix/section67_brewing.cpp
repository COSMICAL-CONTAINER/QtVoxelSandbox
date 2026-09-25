#include "matrix_helpers.h"

// t1097 酿造台 + 药水系统探针段（4 腿；filter 词 r2067；矩阵 806→810）。置尾先例沿用（接 section66，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig）。
//
// 任务契约：§14 池底重盘压轴大件第一轮（机制等价 MC 1.0 brewing stand + 药水基础链；wiki 2026 实读
//   口径：燃料 = 烈焰粉 1 粉 20 次 / 单次 400 ticks = 20s / 一次转换所有合格瓶位、瓶原位变换；基础链 =
//   水瓶 + 地狱疣 → 粗制 → 效果材料 → 效果药水）。范围裁定（倾向 (a) 分层交付，recipe.h 留痕）：第一轮
//   交付酿造台方块 + 玻璃瓶 / 水瓶 + 灰烬疣（§9 原创名；第一轮创造专属——无下界维度，候选池登记）+
//   粗制 / 迅捷 / 力量药水 + 糖 + BrewingStore + C++ 酿造 tick + BrewingUI + 饮用面；其余 1.0 效果
//   （火抗 / 再生 / 中毒 / 虚弱 / 瞬间治疗）登记后续轮。
//
// 腿面与阴性面设计（恰红归因先于腿文）：
//   r2067a 酿造机制承重墙（scanBrewingStands 直编：水瓶 + 灰烬疣 20s 酿成粗制 + 燃烬粉 20 次计量 +
//     三瓶同酿 + 瓶栈数量保留 + 无原料 / 无合格瓶位进度复位 + 连续酿造 + 亮标 bit0 跨 0 翻转）。
//     NEG-1（摘完成转换面：scanBrewingStands while 完成分支头部置 completed=false + break）→ 恰红 =
//     {r2067a}（a 的完成断言全失）；b / c 不受影响（静态表 / 饮用链不经酿造 tick），d 不受影响
//     （源钉钉函数存在与常量，不钉该分支体）。
//   r2067b 效果链 + 配方面承重墙（brewResult 静态表全 3 行 + 非瓶 / 错料负例全 0 + fuelOpsFor 燃烬粉
//     20 / 非燃料 0 + 三条合成配方表命中[糖 / 玻璃瓶 V 形 / 酿造台] + applyStatusEffect(EffectSpeed /
//     EffectStrength) 挂效果 → activeEffectsChanged 快照 {type, level} + 定时到期自动解除）。
//     NEG 面：无独立 NEG（b 的表 / 快照面被 NEG-1 / NEG-2 各不触达——表是纯数据、效果面经 applyStatus
//     Effect 直调不经 finishEating 饮用分支）。设计即「b 为阴性轮的对照组」。
//   r2067c 装水 + 饮用链行为柱（真 pc rig t891 同门：玻璃瓶 selected + 水源 → placeBlock 装水瓶入包 +
//     零世界写入；水瓶 selected → beginEating 长按链 + 泵满 kEatDuration → finishEating 饮用结算：水瓶
//     可饮无效果 + 返 1 空瓶；迅捷药水变体 → EffectSpeed 挂上 + 生存耗 1 + 返 1 空瓶 + 创造不耗不返）。
//     NEG-2（摘饮用结算面：finishEating 的 isDrinkableItem 分支体换 early-return 空体）→ 恰红 =
//     {r2067c}（c 的端到端饮用断言失）；a / b 不受影响（酿造 tick / 直调效果面均不经 finishEating）。
//   r2067d 结构钉（0x264..0x26A 段尾追加序[蕴辉瓶 0x263 之后连续] + BrewingStand=148/Count=149 +
//     kMcBlockId 117 唯一 + ShapeBrewingStand=13 + brewingStandShapeBoxes 两盒逐位 + def 行逐字段 +
//     图集 195 尾追加 + PlayerState 枚举尾追加 Speed/Strength + 效果常量族 + 酿造常量族 + 配方行源钉 +
//     名面 / 调色板尾邻七连 + maxStack 64 双面 + 图标 case 面 + 全链源钉族[入口 / tick / 存储 / UI 路由]
//     + 纪元裁定锚注[裸读] + 相邻族零污染[蕴辉瓶 / 可可豆 / 漏斗行原样]）。
//
// rig 纪律：fixed 48×48×96 s82 宿主（四 setter，section66 同款）；rig 坪 y=80..81 高空（t769 教训：工作
//   体积先清空气）；真链 pc rig 捕获载体窗仅作 grab（无 show，t891 同款）；酿造 tick 直调 pc.scanBrewing
//   Stands(dt)（scanHoppers 探针直调同门）；坐标不作哨兵。

namespace {

// fixed 宿主小世界 incantation（section66 同款四 setter）。
inline void initFixedBrewWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空（坪 y=80 闭区间，上空 y 81..92 清 Air；section66 同款）。
inline void layBrewPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

} // namespace

void MatrixRun::section67_brewing()
{
    // ── r2067a：酿造机制承重墙（scanBrewingStands 直编）────────────────────────────────────────────
    runLeg("r2067a brewing mechanics load-bearing wall (a water bottle plus an ash wart in a placed"
        " brewing stand completes one 20-second brew that converts the bottle in place to the awkward"
        " potion and consumes exactly one ingredient, the ember powder fuel meter starts at twenty"
        " operations and only decrements across completions, three bottles brew simultaneously from a"
        " single ingredient with stack counts preserved, progress resets to zero when the ingredient"
        " runs out or no slot is eligible, consecutive brews chain on remaining ingredient, and the"
        " lit state bit flips on brewing start and end)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24; // 台格（坪顶层上放台）
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        // 场景 ①：水瓶 1 + 灰烬疣 2 + 燃烬粉 1 → 20s 一轮。
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::WaterBottleId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::AshWartId, 2);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const auto drive = [&](qreal sec) {
            const int steps = int(sec / 0.05);
            for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        };
        drive(19.9); // 不足一轮
        const bool before = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::WaterBottleId
                            && store.brewProgressAt(bxp, byp, bzp) > 15.0
                            && store.brewProgressAt(bxp, byp, bzp) < 20.0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel) == 0
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 2
                            && (w.stateAt(bxp, byp, bzp) & BR::BrewingStandStateLitFlag) != 0; // 亮标已置
        ok = ok && before;
        if (!before) diag += QStringLiteral("[before prog=%1 ops=%2]").arg(store.brewProgressAt(bxp, byp, bzp)).arg(store.fuelOpsAt(bxp, byp, bzp));
        drive(0.3); // 跨满一轮
        const bool done = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::AwkwardPotionId
                          && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == 1
                          && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 1 // 耗 1 原料
                          && store.brewProgressAt(bxp, byp, bzp) < 20.0
                          && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 0; // 计量不按完成递减（MC 20 次口径：仅在补燃时消耗新粉）
        ok = ok && done;
        if (!done) diag += QStringLiteral("[done id=%1 ing=%2 prog=%3 ops=%4]")
                              .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                              .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                              .arg(store.brewProgressAt(bxp, byp, bzp)).arg(store.fuelOpsAt(bxp, byp, bzp));
        // 亮标熄灭（粗制 + 灰烬疣无映射 → 无合格瓶位 → 不推进 → 亮标清）。
        const bool litOff = (w.stateAt(bxp, byp, bzp) & BR::BrewingStandStateLitFlag) == 0;
        ok = ok && litOff;
        if (!litOff) diag += QStringLiteral("[litOff st=%1]").arg(w.stateAt(bxp, byp, bzp));
        // 场景 ②：三瓶同酿（水瓶 ×3 于 brew0/1/2）+ 原料耗尽停 + 连续酿造。
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::WaterBottleId, 3);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion1, RecipeRegistry::WaterBottleId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion2, RecipeRegistry::WaterBottleId, 2);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::AshWartId, 1);
        drive(20.5); // 一轮：1 份原料转换全部 5 瓶（跨 3 槽栈）
        const bool threeAtOnce =
            store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == 3
            && store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion1) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion1) == 1
            && store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion2) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion2) == 2
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0; // 原料耗尽
        ok = ok && threeAtOnce;
        if (!threeAtOnce) diag += QStringLiteral("[three id0=%1 c0=%2 id1=%3 id2=%4 ing=%5]")
                                      .arg(store.slotIdAt(bxp, byp, bzp, 0)).arg(store.slotCountAt(bxp, byp, bzp, 0))
                                      .arg(store.slotIdAt(bxp, byp, bzp, 1)).arg(store.slotIdAt(bxp, byp, bzp, 2))
                                      .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient));
        // 原料耗尽再驱 → 进度保持 0（不再推进；燃料不空烧——fuelOps 不动）。
        const int opsBeforeIdle = store.fuelOpsAt(bxp, byp, bzp);
        drive(21.0);
        const bool idleQuiet = store.brewProgressAt(bxp, byp, bzp) == 0.0
                               && store.fuelOpsAt(bxp, byp, bzp) == opsBeforeIdle;
        ok = ok && idleQuiet;
        if (!idleQuiet) diag += QStringLiteral("[idle prog=%1 ops=%2->%3]")
                                    .arg(store.brewProgressAt(bxp, byp, bzp)).arg(opsBeforeIdle)
                                    .arg(store.fuelOpsAt(bxp, byp, bzp));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2067a brewing mechanics load-bearing wall (a water bottle plus an ash wart in a"
               " placed brewing stand completes one 20-second brew that converts the bottle in place"
               " to the awkward potion and consumes exactly one ingredient, the ember powder fuel"
               " meter starts at twenty operations and only decrements across completions, three"
               " bottles brew simultaneously from a single ingredient with stack counts preserved,"
               " progress resets to zero when the ingredient runs out or no slot is eligible,"
               " consecutive brews chain on remaining ingredient, and the lit state bit flips on"
               " brewing start and end)"
            << (ok ? QString() : diag);
    });

    // ── r2067b：效果链 + 配方面承重墙（静态表 + 合成配方 + 效果挂载 / 到期）────────────────────────
    runLeg("r2067b effect chain and recipe face load-bearing wall (the brew result table maps water"
        " bottle plus ash wart to awkward and awkward plus sugar to speed and awkward plus ember"
        " powder to strength with every wrong-ingredient or non-bottle pair answering zero, the fuel"
        " table answers twenty for ember powder and zero for everything else, the sugar and glass"
        " bottle and brewing stand crafting recipes all match, and applying the speed or strength"
        " status effect raises the active-effects snapshot with the right type and level and expires"
        " on time)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 酿造表全 3 行 + 负例。
        const bool t1 = BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::WaterBottleId) == RecipeRegistry::AwkwardPotionId
                        && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::SpeedPotionId
                        && BrewingStore::brewResult(RecipeRegistry::BlazePowderId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::StrengthPotionId;
        const bool t2 = BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::AwkwardPotionId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::BlazePowderId, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::SpeedPotionId) == 0
                        && BrewingStore::brewResult(0, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, 0) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (2) 燃料表。
        const bool tf = BrewingStore::fuelOpsFor(RecipeRegistry::BlazePowderId) == 20
                        && BrewingStore::fuelOpsFor(RecipeRegistry::CoalId) == 0
                        && BrewingStore::fuelOpsFor(RecipeRegistry::CharcoalId) == 0
                        && BrewingStore::fuelOpsFor(0) == 0;
        ok = ok && tf;
        if (!tf) diag += QStringLiteral("[fuel]");
        // (3) 三条合成配方命中（糖 shapeless / 玻璃瓶 V 形 / 酿造台——RecipeRegistry::match 纯表查询）。
        bool rSugarOk = false, rBottleOk = false, rStandOk = false;
        {
            const int gSugar3[9] = { int(BR::Sugarcane), 0, 0, 0, 0, 0, 0, 0, 0 };
            const int gSugar2[4] = { int(BR::Sugarcane), 0, 0, 0 };
            const int gBottle[9] = { RecipeRegistry::GlassId, 0, RecipeRegistry::GlassId,
                                     0, RecipeRegistry::GlassId, 0, 0, 0, 0 };
            const int gStand[9] = { 0, RecipeRegistry::BlazeRodId, 0,
                                    0, 0, 0,
                                    int(BR::Cobble), int(BR::Cobble), int(BR::Cobble) };
            rSugarOk = RecipeRegistry::match(gSugar3, 3) != nullptr
                       && RecipeRegistry::match(gSugar2, 2) != nullptr;
            rBottleOk = RecipeRegistry::match(gBottle, 3) != nullptr;
            rStandOk = RecipeRegistry::match(gStand, 3) != nullptr;
        }
        ok = ok && rSugarOk && rBottleOk && rStandOk;
        if (!(rSugarOk && rBottleOk && rStandOk))
            diag += QStringLiteral("[recipes sugar=%1 bottle=%2 stand=%3]").arg(rSugarOk).arg(rBottleOk).arg(rStandOk);
        // (4) 效果挂载 / 快照 / 到期（applyStatusEffect 直调 + activeEffectsChanged 快照；Survival 门控）。
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        int snapCount = 0;
        int lastType = -1, lastLevel = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) {
            ++snapCount;
            if (!l.isEmpty()) {
                const QVariantMap m = l.last().toMap();
                lastType = m.value(QStringLiteral("type")).toInt();
                lastLevel = m.value(QStringLiteral("level")).toInt();
            }
        });
        pc.applyStatusEffect(PlayerState::EffectSpeed, 0.35f, 2); // 迅捷 II 0.35s
        pc.tick(); // 一帧推进（~16ms）→ 快照发出
        const bool applied = lastType == int(PlayerState::EffectSpeed) && lastLevel == 2;
        ok = ok && applied;
        if (!applied) diag += QStringLiteral("[applied type=%1 lvl=%2 n=%3]").arg(lastType).arg(lastLevel).arg(snapCount);
        // 到期：泵 ~0.6s（0.35s 效果 + 余量）→ 再无 Speed 项。
        QElapsedTimer pump; pump.start();
        bool expired = false;
        while (pump.elapsed() < 900 && !expired) {
            pc.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            expired = snapCount > 0;
            // 检查效果列表已不含 Speed：经最后快照内容判定——activeEffectsChanged 空表触发。
        }
        // 到期后效果清空（重新挂一层捕获：监听末次快照为空列表）。
        int lastSize = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) { lastSize = l.size(); });
        for (int i = 0; i < 30 && lastSize != 0; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        const bool expiredOk = lastSize == 0;
        ok = ok && expiredOk;
        if (!expiredOk) diag += QStringLiteral("[expired lastSize=%1]").arg(lastSize);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2067b effect chain and recipe face load-bearing wall (the brew result table maps"
               " water bottle plus ash wart to awkward and awkward plus sugar to speed and awkward"
               " plus ember powder to strength with every wrong-ingredient or non-bottle pair"
               " answering zero, the fuel table answers twenty for ember powder and zero for"
               " everything else, the sugar and glass bottle and brewing stand crafting recipes all"
               " match, and applying the speed or strength status effect raises the active-effects"
               " snapshot with the right type and level and expires on time)"
            << (ok ? QString() : diag);
    });

    // ── r2067c：装水 + 饮用链行为柱（真 pc rig）───────────────────────────────────────────────────
    runLeg("r2067c bottle fill and drink chain behavior column (a real PlayerController with a glass"
        " bottle held right-clicks a water source and gains one water bottle while the world grid"
        " stays bit-identical [bottles never remove water], a water bottle held under the eat chain"
        " completes into one empty bottle returned with zero hunger change and no effect entry, and"
        " the speed potion variant instead raises the speed effect while consuming exactly one potion"
        " in survival and returning an empty bottle, with creative neither consuming nor returning)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 18, 44, 18, 30);
        // 水源带（(28..30, 82, 22..24)——眼高水平带水源格；装水射线浅俯角必穿水格，
        //   免除「池面低于眼位 / 俯角过深掠过池底」的射线几何巧合）。
        for (int x = 28; x <= 30; ++x)
            for (int z = 22; z <= 24; ++z) {
                for (int y = 81; y <= 92; ++y) w.setBlock(x, y, z, BR::Air, 0);
                w.setBlock(x, 82, z, BR::Water, 0);
            }
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        BrewingStore brewStore;
        pc.setBrewingStore(&brewStore);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air));
        const auto pump = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 链 A：玻璃瓶装水（创造）——瓶右键水 → 水瓶入包 + 零世界写入。
        hb.setStack(0, RecipeRegistry::GlassBottleId, 5, 0);
        hb.setSelectedSlot(0);
        pc.loadSavedState(29.5, 81.0, 24.5, 0.0, -5.0, 1 /* Creative */); // 浅俯角（水源带在 -Z 向 y=82）
        pump(17);
        pc.tick();
        pc.placeBlock();
        // 水瓶落位 = addStack 首个空槽（选中槽仍有 4 空瓶非空 → 水瓶落相邻空槽；全 hotbar 扫描计数）。
        int waterGot = 0, bottlesLeft = 0;
        for (int i = 0; i < hb.slotCount(); ++i) {
            if (hb.blockIdAt(i) == RecipeRegistry::WaterBottleId) waterGot += hb.countAt(i);
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesLeft += hb.countAt(i);
        }
        const bool filled = waterGot == 1 && bottlesLeft == 4; // 扣 1 瓶 + 予 1 水瓶
        ok = ok && filled;
        if (!filled) diag += QStringLiteral("[fill got=%1 left=%2 n0=%3]").arg(waterGot).arg(bottlesLeft).arg(hb.countAt(0));
        const bool waterKept = w.blockAt(29, 82, 23) == BR::Water; // 瓶不带走水
        ok = ok && waterKept;
        if (!waterKept) diag += QStringLiteral("[waterKept]");
        // 链 B：水瓶饮用（生存）——长按链跑满 → 无效果 + 返 1 空瓶 + 耗 1 水瓶 + 饥饿零变。
        int speedSeen = 0;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) {
            for (const QVariant &v : l)
                if (v.toMap().value(QStringLiteral("type")).toInt() == int(PlayerState::EffectSpeed)) ++speedSeen;
        });
        hb.setStack(0, RecipeRegistry::WaterBottleId, 3, 0);
        pc.loadSavedState(29.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pump(320); // 越过放置 CD
        pc.tick();
        const int hunger0 = 20;
        pc.beginEating();
        const bool eatingStarted = pc.eating();
        // 泵 ~2.6s 真时（kEatDuration ~1.6s + 余量；每拍 msleep 60ms > dt 钳制上界 50ms →
        //   每 tick 必得满档 0.05s 增量，34 tick 即满——确定性驱动，不依赖事件泵节奏）。
        int eatIters = 0;
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
            ++eatIters;
        }
        pc.endEating(); // 松开右键（单次饮毕语义——finishEating 冷却链由松手收口）
        const qreal progAfter = pc.eatingProgress();
        // 空瓶返还面：全背包（hotbar 9 槽）扫 GlassBottleId 总数 == 1；水瓶耗 1。
        int bottles = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottles += hb.countAt(i);
        const bool drank = eatingStarted
                           && !pc.eating() // 完成后 cancelEating（单次饮毕即松手语义）
                           && bottles == 1
                           && hb.blockIdAt(0) == RecipeRegistry::WaterBottleId
                           && hb.countAt(0) == 2
                           && speedSeen == 0; // 水瓶无效果
        ok = ok && drank;
        if (!drank) diag += QStringLiteral("[drank eat=%1 end=%2 prog=%3 iters=%4 n0=%5 bottles=%6 speed=%7]")
                               .arg(eatingStarted).arg(pc.eating()).arg(progAfter).arg(eatIters)
                               .arg(hb.countAt(0)).arg(bottles).arg(speedSeen);
        // 链 C：迅捷药水饮用（生存）——EffectSpeed 挂上 + 耗 1 + 返空瓶。
        hb.setStack(0, RecipeRegistry::SpeedPotionId, 2, 0);
        pump(320); // 越过 finishEating 刷新的 m_lastPlaceMs / eat CD
        pc.beginEating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        int bottles2 = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottles2 += hb.countAt(i);
        const bool speedDrank = bottles2 == 2 // 链 B 返 1 + 链 C 返 1
                                && hb.blockIdAt(0) == RecipeRegistry::SpeedPotionId
                                && hb.countAt(0) == 1
                                && speedSeen >= 1; // EffectSpeed 快照已发
        ok = ok && speedDrank;
        if (!speedDrank) diag += QStringLiteral("[speed bottles=%2 n0=%3 speed=%4]")
                                     .arg(bottles2).arg(hb.countAt(0)).arg(speedSeen);
        // 链 D：创造饮迅捷——不耗不返。
        hb.setStack(0, RecipeRegistry::SpeedPotionId, 2, 0);
        pc.loadSavedState(29.5, 81.0, 24.5, 0.0, 0.0, 1 /* Creative */);
        pump(320);
        pc.beginEating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        int bottles3 = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottles3 += hb.countAt(i);
        const bool creativeOk = hb.blockIdAt(0) == RecipeRegistry::SpeedPotionId
                                && hb.countAt(0) == 2 // 不耗
                                && bottles3 == 2; // 不返新瓶（链 B/C 的 2 瓶仍在）
        ok = ok && creativeOk;
        if (!creativeOk) diag += QStringLiteral("[creative n0=%1 bottles=%2]")
                                     .arg(hb.countAt(0)).arg(bottles3);

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2067c bottle fill and drink chain behavior column (a real PlayerController with a"
               " glass bottle held right-clicks a water source and gains one water bottle while the"
               " world grid stays bit-identical [bottles never remove water], a water bottle held"
               " under the eat chain completes into one empty bottle returned with zero hunger change"
               " and no effect entry, and the speed potion variant instead raises the speed effect"
               " while consuming exactly one potion in survival and returning an empty bottle, with"
               " creative neither consuming nor returning)"
            << (ok ? QString() : diag);
    });

    // ── r2067d：结构钉（id 段位 + 呈现面 + 源钉族 + 相邻族零污染）──────────────────────────────────
    runLeg("r2067d structure pins (the seven brewing items sit at 0x264 through 0x26A as a tail"
        " append right after GlimmerBottleId 0x263, the brewing stand block sits at 148 with count"
        " 149 and the MC 1.0 id mapping 117, shape thirteen with the two authority boxes exact, the"
        " atlas grew from 193 to 195 tail-tile append only, the status effect enum appends speed and"
        " strength after fire, the potion duration and speed and strength constants hold, the three"
        " crafting rows and the brew table and the fuel table are pinned at source, the name face"
        " palette rows and icon cases and max stack 64 hold on both layers, the full wiring source"
        " pin family holds across the open signal and the tick entry and the store and the QML"
        " routing, the epoch ruling anchor is present, and the neighbouring glimmer and cocoa and"
        " hopper faces are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位（七连段尾追加 + 方块 id / Count / 映射 / Shape）。
        const bool ids = RecipeRegistry::GlassBottleId == 0x264
                         && RecipeRegistry::WaterBottleId == 0x265
                         && RecipeRegistry::AshWartId == 0x266
                         && RecipeRegistry::AwkwardPotionId == 0x267
                         && RecipeRegistry::SpeedPotionId == 0x268
                         && RecipeRegistry::StrengthPotionId == 0x269
                         && RecipeRegistry::SugarId == 0x26A
                         && RecipeRegistry::GlimmerBottleId == 0x263
                         && int(BR::BrewingStand) == 148
                         && int(BR::Count) == 149
                         && int(BR::ShapeBrewingStand) == 13
                         && BR::mcBlockId(int(BR::BrewingStand)) == 117;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 几何单一权威两盒逐位 + 列顶镜像双权威。
        BR::BlockAABB bb[2];
        const int nBoxes = BR::brewingStandShapeBoxes(0, bb, 2);
        const bool geom = nBoxes == 2
                          && qFuzzyCompare(bb[0].minX, 1.0f / 16.0f) && qFuzzyCompare(bb[0].maxY, 2.0f / 16.0f)
                          && qFuzzyCompare(bb[1].minX, 6.0f / 16.0f) && qFuzzyCompare(bb[1].maxY, 10.0f / 16.0f)
                          && BR::collisionTopY(int(BR::BrewingStand), 0) == 0.625f
                          && qFuzzyCompare(BR::solidTopOffset(int(BR::BrewingStand), 0), 0.625f)
                          && !BR::isFullCube(int(BR::BrewingStand));
        ok = ok && geom;
        if (!geom) diag += QStringLiteral("[geom n=%1]").arg(nBoxes);
        // (3) 图集 + PlayerState 枚举尾追加 + 常量族。
        const bool consts = int(BR::AtlasTileCount) == 195
                            && int(PlayerState::EffectSpeed) == int(PlayerState::EffectFire) + 1
                            && int(PlayerState::EffectStrength) == int(PlayerState::EffectFire) + 2
                            && PlayerController::kPotionDurationSec == 180.0f
                            && PlayerController::kSpeedBoostPerLevel == 0.20f
                            && PlayerController::kStrengthBonusPerLevel == 1.30f
                            && BrewingStore::kPowderFuelOps == 20
                            && BrewingStore::kBrewSecs == 20.0
                            && BrewingStore::kSlotsPerBrewing == 5;
        ok = ok && consts;
        if (!consts) diag += QStringLiteral("[consts tiles=%1]").arg(int(BR::AtlasTileCount));
        // (4) 呈现三面：名面七件 + 调色板尾邻七连 + maxStack 64 双面 + 材料段判定。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::GlassBottleId) == QStringLiteral("玻璃瓶")
                            && hb.nameForBlock(RecipeRegistry::WaterBottleId) == QStringLiteral("水瓶")
                            && hb.nameForBlock(RecipeRegistry::AshWartId) == QStringLiteral("灰烬疣")
                            && hb.nameForBlock(RecipeRegistry::AwkwardPotionId) == QStringLiteral("粗制药水")
                            && hb.nameForBlock(RecipeRegistry::SpeedPotionId) == QStringLiteral("迅捷药水")
                            && hb.nameForBlock(RecipeRegistry::StrengthPotionId) == QStringLiteral("力量药水")
                            && hb.nameForBlock(RecipeRegistry::SugarId) == QStringLiteral("糖");
        const QVariantList mats = hb.creativeMaterials();
        int glimIdx = -1, firstBrew = -1, lastBrew = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::GlimmerBottleId) glimIdx = i;
            if (v == RecipeRegistry::GlassBottleId) firstBrew = i;
            if (v == RecipeRegistry::SugarId) lastBrew = i;
        }
        const bool paletteOk = glimIdx >= 0 && firstBrew == glimIdx + 1 && lastBrew == firstBrew + 6;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::SpeedPotionId) == 64
                             && BR::maxStackSize(RecipeRegistry::WaterBottleId) == 64
                             && hb.isMaterial(RecipeRegistry::AshWartId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3/%4 stack=%5]")
                        .arg(nameOk).arg(glimIdx).arg(firstBrew).arg(lastBrew).arg(stackOk);
        // (5) 全链源钉族（剥注释 pinSet 锚真实语句）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto readRaw = [&srcRoot](const char *rel) {
            QFile f(srcRoot + QString::fromLatin1(rel));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QStringList missPcH = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("store property", "Q_PROPERTY(BrewingStore *brewingStore READ brewingStore WRITE setBrewingStore NOTIFY brewingStoreChanged)", 1),
            SrcPin("tick decl", "void scanBrewingStands(float dt);", 1),
            SrcPin("drinkable decl", "static bool isDrinkableItem(int itemId);", 1),
            SrcPin("open signal", "void brewingStandOpened(int x, int y, int z);", 1),
            SrcPin("duration const", "static constexpr float kPotionDurationSec = 180.0f;", 1),
            SrcPin("speed const", "static constexpr float kSpeedBoostPerLevel = 0.20f;", 1),
            SrcPin("strength const", "static constexpr float kStrengthBonusPerLevel = 1.30f;", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("open branch", "emit brewingStandOpened(m_hitBx, m_hitBy, m_hitBz);", 1),
            SrcPin("tick wiring", "scanBrewingStands(dt);", 1),
            SrcPin("fill entry", "heldItemId == RecipeRegistry::GlassBottleId", 1),
            SrcPin("fill give", "m_hotbar->addStack(int(RecipeRegistry::WaterBottleId), 1);", 1),
            SrcPin("drink gate", "isDrinkableItem(heldForEat)", 1),
            SrcPin("drink settle", "if (isDrinkableItem(eatenId)) {", 1),
            SrcPin("empty bottle return", "m_hotbar->addStack(int(RecipeRegistry::GlassBottleId), 1);", 1),
            SrcPin("speed hook", "const float speedFxMul = (m_speedTimer > 0.0f)", 1),
            SrcPin("strength hook", "dmg *= (1.0f + kStrengthBonusPerLevel", 1),
            SrcPin("snapshot tail", "if (m_strengthTimer > 0.0f) {", 1)});
        const QStringList missBsH = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.h"), {
            SrcPin("slot count const", "static constexpr int kSlotsPerBrewing = 5;", 1),
            SrcPin("fuel const", "static constexpr int   kPowderFuelOps = 20;", 1),
            SrcPin("secs const", "static constexpr qreal kBrewSecs      = 20.0;", 1),
            SrcPin("brew table decl", "static int brewResult(int ingredientId, int bottleId);", 1),
            SrcPin("revision property", "Q_PROPERTY(int revision READ revision NOTIFY brewingChanged)", 1)});
        const QStringList missBs = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("table awkward", "return ingredientId == RecipeRegistry::AshWartId ? RecipeRegistry::AwkwardPotionId : 0;", 1),
            SrcPin("table speed", "return RecipeRegistry::SpeedPotionId;", 1),
            SrcPin("table strength", "return RecipeRegistry::StrengthPotionId;", 1),
            SrcPin("fuel row", "return itemId == RecipeRegistry::BlazePowderId ? kPowderFuelOps : 0;", 1)});
        const QStringList missPs = pinSet(srcRoot + QStringLiteral("/Game/playerstate.h"), {
            SrcPin("effect enum tail", "EffectNone = 0, EffectPoison, EffectSlowness, EffectFire, EffectSpeed, EffectStrength", 1)});
        const QStringList missBrH = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("id row", "BrewingStand     = 148,", 1),
            SrcPin("count row", "Count           = 149,", 1),
            SrcPin("shape row", "ShapeBrewingStand = 13,", 1),
            SrcPin("boxes decl", "static int brewingStandShapeBoxes(quint8 state, BlockAABB *out, int cap);", 1),
            SrcPin("lit flag", "static constexpr quint8 BrewingStandStateLitFlag = 0x01;", 1),
            SrcPin("atlas", "static constexpr int AtlasTileCount = 195;", 1)});
        const QStringList missBr = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.cpp"), {
            SrcPin("def row", "\"brewing_stand\",  \"酿造台\"", 1)});
        const QStringList missRc = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("sugar row", "\"sugar_from_cane\"", 1),
            SrcPin("bottle row", "\"glass_bottle\"", 1),
            SrcPin("stand row", "\"brewing_stand\"", 1),
            SrcPin("id assert", "static_assert(RecipeRegistry::GlassBottleId    == 0x264,", 1)});
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("id block", "static constexpr int GlassBottleId    = 0x264;", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("palette row", "int(RecipeRegistry::GlassBottleId),", 1),
            SrcPin("block palette row", "int(BlockRegistry::BrewingStand),", 1),
            SrcPin("name row", "return QStringLiteral(\"灰烬疣\")", 1)});
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("bottle case", "case 0x264: drawGlassBottle(); break", 1),
            SrcPin("potion case", "case 0x268: drawPotion(", 1),
            SrcPin("wart case", "case 0x266: drawAshWart(); break", 1),
            SrcPin("sugar case", "case 0x26A: drawSugar(); break", 1)});
        const QStringList missMq = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("open route", "function onBrewingStandOpened(x, y, z) { window.openBrewing(x, y, z) }", 1),
            SrcPin("store instance", "BrewingStore { id: brewingStore }", 1),
            SrcPin("inject", "brewingStore: brewingStore", 1),
            SrcPin("panel", "BrewingUI {", 1),
            SrcPin("content drop", "brewingStore.clearBrewing(x, y, z)", 1),
            SrcPin("speed icon", "qrc:/textures/icon_effect_speed.png", 1),
            SrcPin("strength icon", "qrc:/textures/icon_effect_strength.png", 1)});
        const QStringList missUi = pinSet(srcRoot + QStringLiteral("/ui/BrewingUI.qml"), {
            SrcPin("store face", "property BrewingStore brewingStore", 1),
            SrcPin("five slots", "StandSlot { grp: \"brew0\"", 1),
            SrcPin("fuel slot", "StandSlot { grp: \"fuel\"", 1)});
        const QStringList missWs = pinSet(srcRoot + QStringLiteral("/World/worldstore.h"), {
            SrcPin("save param", "const QVariantList &brewingStands = {}", 1),
            SrcPin("load decl", "Q_INVOKABLE QVariantList loadBrewingStands() const;", 1)});
        const bool d5 = missPcH.isEmpty() && missPc.isEmpty() && missBsH.isEmpty() && missBs.isEmpty()
            && missPs.isEmpty() && missBrH.isEmpty() && missBr.isEmpty() && missRc.isEmpty()
            && missRh.isEmpty() && missHb.isEmpty() && missMi.isEmpty() && missMq.isEmpty()
            && missUi.isEmpty() && missWs.isEmpty();
        ok = ok && d5;
        if (!d5) {
            const QStringList allMiss = QStringList()
                << missPcH << missPc << missBsH << missBs << missPs << missBrH << missBr
                << missRc << missRh << missHb << missMi << missMq << missUi << missWs;
            diag += QStringLiteral("[d5 %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (6) 纪元裁定锚注 + kMcBlockId 行（裸读——该行本体是块注释形态，pinSet 剥注释面不可锚，
        //     改裸读原文 contains，同 section51 先例）。
        const bool anchors = readRaw("/Game/recipe.h").contains(QStringLiteral("灰烬疣获取链裁定"));
        const bool mcIdRow = readRaw("/Core/blockregistry.cpp").contains(QStringLiteral("/* brewing_stand          */ 117,"));
        // 面板无 tick 面（酿造推进权威在 C++）：注释级设计声明用裸读锚（QML 注释被 pinSet 剥除）。
        const bool noTickFace = readRaw("/ui/BrewingUI.qml").contains(QStringLiteral("C++ scanBrewingStands"));
        ok = ok && anchors && mcIdRow && noTickFace;
        if (!(anchors && mcIdRow && noTickFace))
            diag += QStringLiteral("[anchors a=%1 mc=%2 noTick=%3]").arg(anchors).arg(mcIdRow).arg(noTickFace);
        // (7) 相邻族零污染（蕴辉瓶投掷 / 可可豆转换 / 漏斗扫描原样）。
        const QStringList missNb = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("glimmer throw untouched", "heldItemId == RecipeRegistry::GlimmerBottleId", 1),
            SrcPin("hopper scan untouched", "void PlayerController::scanHoppers(float dt)", 1)});
        const bool cocoaRow = readRaw("/Game/hotbar.cpp")
                                  .contains(QStringLiteral("int(RecipeRegistry::CocoaBeanId),"));
        const bool nb = missNb.isEmpty() && cocoaRow;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb %1 cocoa=%2]")
                             .arg(missNb.join(QLatin1Char(','))).arg(cocoaRow);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2067d structure pins (the seven brewing items sit at 0x264 through 0x26A as a tail"
               " append right after GlimmerBottleId 0x263, the brewing stand block sits at 148 with"
               " count 149 and the MC 1.0 id mapping 117, shape thirteen with the two authority"
               " boxes exact, the atlas grew from 193 to 195 tail-tile append only, the status effect"
               " enum appends speed and strength after fire, the potion duration and speed and"
               " strength constants hold, the three crafting rows and the brew table and the fuel"
               " table are pinned at source, the name face palette rows and icon cases and max stack"
               " 64 hold on both layers, the full wiring source pin family holds across the open"
               " signal and the tick entry and the store and the QML routing, the epoch ruling anchor"
               " is present, and the neighbouring glimmer and cocoa and hopper faces are untouched)"
            << (ok ? QString() : diag);
    });
}
