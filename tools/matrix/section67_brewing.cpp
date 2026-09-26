#include "matrix_helpers.h"

// t1097 酿造台 + 药水系统探针段（4 腿；filter 词 r2067；矩阵 806→810）。置尾先例沿用（接 section66，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig）。
// t1099 药水效果链第二轮（+4 腿 r2069a-d，置尾追加；矩阵 812→816）：五链核实裁定与 NEG 豁免面设计
//   见各腿头注——NEG-1（摘发酵蛛眼 brew 行）恰红 {r2069a} / NEG-2（摘 finishEating 分流块）恰红 {r2069b}，
//   r2069c 直调效果机制面（两 NEG 均不触达）、r2069d 结构钉（不钉两 NEG 触达面 = 豁免设计）。
// t1100 酿造第三轮（+4 腿 r2070a-d，置尾追加；矩阵 816→820）：红石延长二级酿造面 + 药水呈现层小件。
//   NEG 面与豁免设计（恰红归因先于腿文）：
//   NEG-1 = 摘二级酿造映射（brewingstore.cpp brewResult 红石门行 + extendedPotionResult 本体一并摘——
//     函数仅 gate 行一处调用、.h 声明幸存 → 编译仍绿，行为柱恰红）→ 恰红 = {r2070a}（静态表红石对 +
//     真驱转换断言全失）；b（瓶直入 hotbar 饮用，不经酿造 tick）/ c（applyStatusEffect 直调）/
//     d（源钉不钉 gate 行与小表体 = 豁免面，只钉 .h 声明）均不受影响。
//   NEG-2 = 摘 finishEating 延长分流链（t1100 六行 else-if）→ 恰红 = {r2070b}（延长秒数断言全失；
//     isDrinkableItem 早退面不在摘除面 → 饮用仍消耗/返瓶/发 potionDrunk，唯效果时长面失）；a / c /
//     d（源钉不钉分流链 = 豁免面）均不受影响。
//
// 任务契约：§14 池底重盘压轴大件第一轮（机制等价 MC 1.0 brewing stand + 药水基础链；wiki 2026 实读
//   口径：燃料 = 烈焰粉 1 粉 20 次 / 单次 400 ticks = 20s / 一次转换所有合格瓶位、瓶原位变换；基础链 =
//   水瓶 + 地狱疣 → 粗制 → 效果材料 → 效果药水）。范围裁定（倾向 (a) 分层交付，recipe.h 留痕）：第一轮
//   交付酿造台方块 + 玻璃瓶 / 水瓶 + 灰烬疣（§9 原创名；第一轮创造专属——无下界维度，候选池登记）+
//   粗制 / 迅捷 / 力量药水 + 糖 + BrewingStore + C++ 酿造 tick + BrewingUI + 饮用面；其余 1.0 效果
//   （火抗 / 再生 / 中毒 / 虚弱 / 瞬间治疗）登记后续轮。
//
// 腿面与阴性面设计（恰红归因先于腿文）：
//   r2067a 酿造机制承重墙（scanBrewingStands 直编：水瓶 + 灰烬疣 20s 酿成粗制 + 燃烬粉 20 次计量
//     每完成一次操作 -1[一次操作=一批转换全部合格瓶位] + 计量归零补燃烧新粉重置 20 + 三瓶同酿 +
//     瓶栈数量保留 + 无原料 / 无合格瓶位进度复位 + 连续酿造 + 亮标 bit0 跨 0 翻转）。
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
        " operations and decrements exactly once per completed operation with a three-bottle batch"
        " still counting as one operation and a fresh powder burnt with the meter reset to twenty"
        " when the next advance finds the meter at zero, three bottles brew simultaneously from a"
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
                          && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1; // t1097-fix MC 20 次口径：每完成一次操作计量 -1（一次操作=一批转换全部合格瓶位）
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
        drive(20.5); // 一轮：1 份原料转换全部 5 瓶（跨 3 槽栈）——**一次操作**（一批），计量仅再 -1。
        const bool threeAtOnce =
            store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == 3
            && store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion1) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion1) == 1
            && store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion2) == RecipeRegistry::AwkwardPotionId
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotPotion2) == 2
            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0 // 原料耗尽
            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 2; // 两轮操作各 -1（本批三瓶仍只算一次操作）
        ok = ok && threeAtOnce;
        if (!threeAtOnce) diag += QStringLiteral("[three id0=%1 c0=%2 id1=%3 id2=%4 ing=%5 ops=%6]")
                                      .arg(store.slotIdAt(bxp, byp, bzp, 0)).arg(store.slotCountAt(bxp, byp, bzp, 0))
                                      .arg(store.slotIdAt(bxp, byp, bzp, 1)).arg(store.slotIdAt(bxp, byp, bzp, 2))
                                      .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                      .arg(store.fuelOpsAt(bxp, byp, bzp));
        // 原料耗尽再驱 → 进度保持 0（不再推进；燃料不空烧——fuelOps 不动）。
        const int opsBeforeIdle = store.fuelOpsAt(bxp, byp, bzp);
        drive(21.0);
        const bool idleQuiet = store.brewProgressAt(bxp, byp, bzp) == 0.0
                               && store.fuelOpsAt(bxp, byp, bzp) == opsBeforeIdle;
        ok = ok && idleQuiet;
        if (!idleQuiet) diag += QStringLiteral("[idle prog=%1 ops=%2->%3]")
                                    .arg(store.brewProgressAt(bxp, byp, bzp)).arg(opsBeforeIdle)
                                    .arg(store.fuelOpsAt(bxp, byp, bzp));
        // 场景 ③：计量满口径（t1097-fix）——计量归零后下一次推进消耗第 2 粉重置 20（fuelCnt 1→0 口径
        //   换算到本场景 = fuelCnt 2→1）。粗制瓶 + 糖链（连续两操作可续）；播种计量 = store Q_INVOKABLE
        //   直写（合法状态面——fuelOps 本就是 store 持久状态，非引擎外缝）。
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::AwkwardPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::SugarId, 3);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 2);
        store.setFuelOps(bxp, byp, bzp, 1); // 播种：计量仅剩 1 次
        drive(20.5); // op1：计量 1→0（fuelCnt 仍 2——粉未烧），粗制→迅捷 ×1
        const bool drainToZero = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::SpeedPotionId
                                 && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 2
                                 && store.fuelOpsAt(bxp, byp, bzp) == 0
                                 && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel) == 2;
        ok = ok && drainToZero;
        if (!drainToZero) diag += QStringLiteral("[drain id=%1 ing=%2 ops=%3 fuel=%4]")
                                      .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                      .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                      .arg(store.fuelOpsAt(bxp, byp, bzp))
                                      .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel));
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::AwkwardPotionId, 1); // 续瓶（玩家换入下一批）
        drive(20.5); // op2：计量 0 + 有合格瓶位 → 补燃烧第 2 粉（fuelCnt 2→1）重置 20 → 完成后 19
        const bool refillAtZero = store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel) == 1
                                  && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1
                                  && store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::SpeedPotionId
                                  && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 1;
        ok = ok && refillAtZero;
        if (!refillAtZero) diag += QStringLiteral("[refill fuel=%1 ops=%2 id=%3 ing=%4]")
                                       .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel))
                                       .arg(store.fuelOpsAt(bxp, byp, bzp))
                                       .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                       .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2067a brewing mechanics load-bearing wall (a water bottle plus an ash wart in a"
               " placed brewing stand completes one 20-second brew that converts the bottle in place"
               " to the awkward potion and consumes exactly one ingredient, the ember powder fuel"
               " meter starts at twenty operations and decrements exactly once per completed"
               " operation with a three-bottle batch still counting as one operation and a fresh"
               " powder burnt with the meter reset to twenty when the next advance finds the meter"
               " at zero, three bottles brew simultaneously from a single ingredient with stack"
               " counts preserved, progress resets to zero when the ingredient runs out or no slot"
               " is eligible, consecutive brews chain on remaining ingredient, and the lit state bit"
               " flips on brewing start and end)"
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

    // ── r2069a：第二轮酿造转换行为柱（静态表五链 + 负例 + 真驱一条；NEG-1 敏感面）─────────────────
    //   NEG-1（摘 brewingstore.cpp 发酵蛛眼 brew 行两行）→ 恰红 = {r2069a}：本腿静态表中毒链断言 +
    //   真驱转换断言全失；b（不经 brewResult——瓶直入 hotbar）/ c（applyStatusEffect 直调）/
    //   d（源钉不钉发酵蛛眼行——NEG-1 豁免面）均不受影响。
    runLeg("r2069a round-two brewing conversion behavior column (the brew result table maps awkward"
        " plus magma cream to fire resistance and awkward plus ghast tear to regeneration and"
        " awkward plus spider eye to poison and awkward plus fermented spider eye to weakness and"
        " awkward plus glistering melon to instant health with every wrong-ingredient or"
        " potion-bottle or zero pair answering zero, the fermented spider eye crafting row matches"
        " from a shapeless two-by-two of spider eye brown mushroom and sugar, and a placed brewing"
        " stand driven through a full twenty-second cycle converts an awkward bottle to the weakness"
        " potion on the ember powder fuel meter)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 静态表五链 + 负例（错料 / 药水瓶作瓶 / 零 id 对 → 0）。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::MagmaCreamId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::FireResistancePotionId
            && BrewingStore::brewResult(RecipeRegistry::GhastTearId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::RegenerationPotionId
            && BrewingStore::brewResult(RecipeRegistry::SpiderEyeId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::PoisonPotionId
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::WeaknessPotionId
            && BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::InstantHealthPotionId;
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::MagmaCreamId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SpiderEyeId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::PoisonPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::MagmaCreamId, RecipeRegistry::WeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SpiderEyeId, RecipeRegistry::FireResistancePotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::InstantHealthPotionId) == 0
            && BrewingStore::brewResult(0, RecipeRegistry::AwkwardPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::MagmaCreamId, 0) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (2) 发酵蛛眼合成行行为命中（shapeless 2×2 {蛛眼, 褐菇(方块115), 糖} → 发酵蛛眼）。
        bool rFermOk = false;
        {
            const int g2[4] = { RecipeRegistry::SpiderEyeId, int(BR::BrownMushroom), RecipeRegistry::SugarId, 0 };
            const int g3[9] = { 0, RecipeRegistry::SpiderEyeId, 0,
                                int(BR::BrownMushroom), RecipeRegistry::SugarId, 0,
                                0, 0, 0 };
            const auto *r2 = RecipeRegistry::match(g2, 2);
            const auto *r3 = RecipeRegistry::match(g3, 3);
            rFermOk = r2 && r2->outputId == RecipeRegistry::FermentedSpiderEyeId && r2->outputCount == 1
                      && r3 && r3->outputId == RecipeRegistry::FermentedSpiderEyeId;
        }
        ok = ok && rFermOk;
        if (!rFermOk) diag += QStringLiteral("[fermented r2=%1]").arg(rFermOk);
        // (3) 真驱转换：粗制瓶 + 发酵蛛眼 + 燃烬粉 → 20s 一轮 → 虚弱药水（表行接线到 scanBrewingStands）。
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24;
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::AwkwardPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::FermentedSpiderEyeId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const int steps = int(20.6 / 0.05);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool driven = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::WeaknessPotionId
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        ok = ok && driven;
        if (!driven) diag += QStringLiteral("[driven id=%1 ing=%2 ops=%3]")
                                  .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                  .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                  .arg(store.fuelOpsAt(bxp, byp, bzp));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2069a round-two brewing conversion behavior column (the brew result table maps awkward"
               " plus magma cream to fire resistance and awkward plus ghast tear to regeneration and"
               " awkward plus spider eye to poison and awkward plus fermented spider eye to weakness and"
               " awkward plus glistering melon to instant health with every wrong-ingredient or"
               " potion-bottle or zero pair answering zero, the fermented spider eye crafting row matches"
               " from a shapeless two-by-two of spider eye brown mushroom and sugar, and a placed brewing"
               " stand driven through a full twenty-second cycle converts an awkward bottle to the weakness"
               " potion on the ember powder fuel meter)"
            << (ok ? QString() : diag);
    });

    // ── r2069b：第二轮饮用链行为柱（finishEating 真链；NEG-2 敏感面）─────────────────────────────
    //   NEG-2（摘 finishEating t1099 else-if 分流块）→ 恰红 = {r2069b}：虚弱快照 + 瞬间治疗回血断言
    //   全失；a（静态表 / 真驱，不经 finishEating）/ c（applyStatusEffect 直调）/ d（源钉不钉分流块——
    //   NEG-2 豁免面）均不受影响。
    runLeg("r2069b round-two drink chain behavior column (a real survival player drinking the instant"
        " health potion through the full eat chain emits exactly one four-point heal with no"
        " persistent effect entry left in the snapshot and one empty bottle returned, and drinking"
        " the weakness potion raises the weakness effect on the active-effects snapshot at level one"
        " with the ninety-second caliber while consuming one potion)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 18, 44, 18, 30);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air));
        // 快照 / 回血捕获（链间累积——按分段窗口断言，见各链内局部基线）。
        int healedSum = 0, healedCount = 0;
        QObject::connect(&pc, &PlayerController::healed, &pc, [&](int hp) {
            ++healedCount; healedSum += hp;
        });
        int lastSize = -1;
        bool weaknessSeen = false;
        int weaknessLevel = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) {
            lastSize = l.size();
            for (const QVariant &v : l) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectWeakness)) {
                    weaknessSeen = true;
                    weaknessLevel = m.value(QStringLiteral("level")).toInt();
                }
            }
        });
        const auto pump = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 链 A：瞬间治疗药水（生存）——真链喝满 → healed(4) 恰一次 + 快照零持续项 + 返 1 空瓶。
        hb.setStack(0, RecipeRegistry::InstantHealthPotionId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pump(320); // 越过放置 CD
        pc.tick();
        const int healedBase = healedCount;
        pc.beginEating();
        const bool eatingStarted = pc.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        int bottlesA = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesA += hb.countAt(i);
        lastSize = -1;
        pc.tick(); // 一帧推进 → 快照刷新（瞬间治疗不应产生任何持续项）
        const bool instantOk = eatingStarted
                               && healedCount - healedBase == 1 && healedSum == 4 // 恰一次 4HP
                               && lastSize <= 0 // 无 timer → 快照恒空（空→空不发信号，lastSize 留 -1 同过）
                               && bottlesA == 1; // 返 1 空瓶
        ok = ok && instantOk;
        if (!instantOk) diag += QStringLiteral("[instant eat=%1 hc=%2 hs=%3 size=%4 b=%5]")
                                    .arg(eatingStarted).arg(healedCount - healedBase).arg(healedSum)
                                    .arg(lastSize).arg(bottlesA);
        // 链 B：虚弱药水（生存）——真链喝满 → EffectWeakness 快照挂上（level 1）+ 耗 1。
        hb.setStack(0, RecipeRegistry::WeaknessPotionId, 2, 0); // 选中槽仍 0（链 A 未换槽）
        pump(320); // 越过 finishEating 冷却 / eat CD
        pc.tick();
        pc.beginEating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        pc.tick();
        const bool weakOk = weaknessSeen && weaknessLevel == 1
                            && hb.blockIdAt(0) == RecipeRegistry::WeaknessPotionId
                            && hb.countAt(0) == 1; // 耗 1（剩 1）
        ok = ok && weakOk;
        if (!weakOk) diag += QStringLiteral("[weak seen=%1 lvl=%2 n0=%3]")
                                  .arg(weaknessSeen).arg(weaknessLevel).arg(hb.countAt(0));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2069b round-two drink chain behavior column (a real survival player drinking the instant"
               " health potion through the full eat chain emits exactly one four-point heal with no"
               " persistent effect entry left in the snapshot and one empty bottle returned, and drinking"
               " the weakness potion raises the weakness effect on the active-effects snapshot at level one"
               " with the ninety-second caliber while consuming one potion)"
            << (ok ? QString() : diag);
    });

    // ── r2069c：第二轮效果机制柱（applyStatusEffect 直调：火抗两相免疫 / 再生脉冲 / 中毒复用 / 到期）──
    //   双 pc 隔离设计：pc1 专跑火抗柱（真岩浆点燃基线）；pc2 专跑再生 / 中毒 / 虚弱柱。隔离根因 =
    //   t238 饥饿回血面在饱腹态每 4s 也 emit healed(1)（同信号异源）——再生脉冲断言窗口必须整个落在
    //   pc2 累计 4.0s 之前，否则饥饿回血脉冲混入无法归因（pc2 再生段在累计 ~3.4s 内收口，安全边际）。
    //   另：loadSavedState 会清全部效果态（t1099 复位面）→ 火抗各相不可借读档重摆位——pc1 一次摆位，
    //   岩浆格点燃后落坪余焰续烧（kFireDuration=8s 余量覆盖全程），免疫面用「余焰 + 外部 EffectFire」双门。
    runLeg("r2069c round-two effect mechanics column (fire resistance first proves the burn baseline"
        " by taking fire damage standing in lava then grants full immunity against both residual and"
        " externally applied flame with zero further fire damage events and a clean snapshot entry,"
        " and the burn returns once the resistance has expired, while on a fresh controller"
        " regeneration pulses exactly one hit point per two and a half seconds and clears on expiry,"
        " the poison potion rides the existing poison damage face at the one-point-two-five second"
        " caliber, and the weakness snapshot carries the applied level)", [&]() {
        bool ok = true;
        QString diag;
        const auto pumpTicks = [](PlayerController &p, int iters) {
            for (int i = 0; i < iters; ++i) { QThread::msleep(60); p.tick(); } // 每拍 0.05s 模拟时（dt 钳制上界）
        };
        // (1) 火抗柱（pc1）：基线烧伤 → 免疫（余焰 + 外部点燃双门）→ 到期复燃。
        World w1;
        initFixedBrewWorld(w1);
        layBrewPlatform(w1, 20, 30, 20, 30);
        w1.setBlock(24, 81, 24, BR::Lava, 0); // 岩浆格（坪顶上空一格；玩家沉落坪顶后余焰续烧 ~8s）
        PlayerController pc1;
        pc1.setWorld(&w1);
        int fireEvents = 0;
        QObject::connect(&pc1, &PlayerController::fallDamageTaken, &pc1, [&](int hp, int cause) {
            if (cause == int(PlayerState::Fire)) fireEvents += hp; // 火伤事件（hp 恒 1/次）
        });
        int fireResSeen = 0, fireResLevel = -1;
        QObject::connect(&pc1, &PlayerController::activeEffectsChanged, &pc1, [&](const QVariantList &l) {
            for (const QVariant &v : l) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectFireResistance)) {
                    ++fireResSeen;
                    fireResLevel = m.value(QStringLiteral("level")).toInt();
                }
            }
        });
        pc1.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */); // 脚位 81 = 岩浆格（非固体 → 首拍点燃后落坪）
        // 相 0（基线）：无免疫 → 点燃 + 1s 周期火伤 ≥1（火烧面真值前置自证）。
        pumpTicks(pc1, 40); // 2.0s
        const int baseline = fireEvents;
        ok = ok && baseline >= 1;
        if (baseline < 1) diag += QStringLiteral("[baseline=%1]").arg(baseline);
        // 相 1（免疫双门）：火抗 60s → 余焰火伤归零 + 外部施加 EffectFire 亦零火伤 + 快照 level 1。
        pc1.applyStatusEffect(PlayerState::EffectFireResistance, 60.0f, 1);
        pc1.tick();
        ok = ok && fireResSeen >= 1 && fireResLevel == 1;
        if (!(fireResSeen >= 1 && fireResLevel == 1))
            diag += QStringLiteral("[snap seen=%1 lvl=%2]").arg(fireResSeen).arg(fireResLevel);
        pumpTicks(pc1, 30); // 1.5s：余焰火伤被火伤门全挡
        pc1.applyStatusEffect(PlayerState::EffectFire, 8.0f, 1); // 外部点燃（免疫期 → 火伤门挡）
        pumpTicks(pc1, 30); // 1.5s
        const int duringImmunity = fireEvents - baseline;
        ok = ok && duringImmunity == 0;
        if (duringImmunity != 0) diag += QStringLiteral("[immune delta=%1]").arg(duringImmunity);
        // 相 2（到期复燃）：免疫缩到 0.15s → 泵 0.5s 到期 → 再施加 EffectFire → 火伤回归 ≥1。
        pc1.applyStatusEffect(PlayerState::EffectFireResistance, 0.15f, 1);
        pumpTicks(pc1, 10); // 0.5s > 0.15s：免疫到期
        pc1.applyStatusEffect(PlayerState::EffectFire, 8.0f, 1); // 免疫已过 → 火伤恢复
        fireEvents = 0; // 段基线重开
        pumpTicks(pc1, 30); // 1.5s：≥1 次 1s 周期火伤
        ok = ok && fireEvents >= 1;
        if (fireEvents < 1) diag += QStringLiteral("[reignite=%1]").arg(fireEvents);
        // (2) 再生 / 中毒 / 虚弱柱（pc2 fresh——饥饿回血 4s 边界避让，见腿头注）。
        World w2;
        initFixedBrewWorld(w2);
        layBrewPlatform(w2, 20, 30, 20, 30);
        PlayerController pc2;
        pc2.setWorld(&w2);
        int regenPulses = 0, regenSum = 0;
        int regenSeenCount = 0;
        int weakLvl = -1;
        QObject::connect(&pc2, &PlayerController::healed, &pc2, [&](int hp) { ++regenPulses; regenSum += hp; });
        QObject::connect(&pc2, &PlayerController::activeEffectsChanged, &pc2, [&](const QVariantList &l) {
            bool hasRegen = false;
            for (const QVariant &v : l) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectRegeneration)) hasRegen = true;
                if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectWeakness))
                    weakLvl = m.value(QStringLiteral("level")).toInt();
            }
            if (hasRegen) ++regenSeenCount;
        });
        pc2.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pc2.tick();
        // (2a) 再生脉冲：3.0s 再生 → 窗口内恰 1 次 healed(1)（2.5s 脉冲；窗口收在累计 ~3.4s < 4.0s
        //      饥饿回血边界——纯药水源零混入）+ 到期后快照不再含再生项。
        const int healedBase = regenPulses;
        pc2.applyStatusEffect(PlayerState::EffectRegeneration, 3.0f, 1);
        pumpTicks(pc2, 60); // 3.0s
        ok = ok && regenPulses - healedBase == 1 && regenSum - healedBase == 1;
        if (!(regenPulses - healedBase == 1 && regenSum - healedBase == 1))
            diag += QStringLiteral("[regen pulses=%1 sum=%2]").arg(regenPulses - healedBase).arg(regenSum - healedBase);
        regenSeenCount = 0;
        pc2.tick(); // 到期后（timer 已清）→ 快照刷新无再生项
        ok = ok && regenSeenCount == 0;
        if (regenSeenCount != 0) diag += QStringLiteral("[regen expired still visible]");
        // (2b) 中毒复用面：中毒药水走既有 m_poisonTimer 毒面（1.25s 周期扣血信号真值）。
        int poisonHits = 0;
        QObject::connect(&pc2, &PlayerController::poisonDamageTaken, &pc2, [&](int) { ++poisonHits; });
        pc2.applyStatusEffect(PlayerState::EffectPoison, 2.0f, 1);
        pumpTicks(pc2, 34); // 1.7s → ≥1 次 1.25s 周期扣血
        ok = ok && poisonHits >= 1;
        if (poisonHits < 1) diag += QStringLiteral("[poison hits=%1]").arg(poisonHits);
        // (2c) 虚弱等级快照：applyStatusEffect 挂 II 级 → 快照 level 2。
        pc2.applyStatusEffect(PlayerState::EffectWeakness, 0.4f, 2);
        pc2.tick();
        ok = ok && weakLvl == 2;
        if (weakLvl != 2) diag += QStringLiteral("[weak lvl=%1]").arg(weakLvl);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2069c round-two effect mechanics column (fire resistance first proves the burn baseline"
               " by taking fire damage standing in lava then grants full immunity against both residual and"
               " externally applied flame with zero further fire damage events and a clean snapshot entry,"
               " and the burn returns once the resistance has expired, while on a fresh controller"
               " regeneration pulses exactly one hit point per two and a half seconds and clears on expiry,"
               " the poison potion rides the existing poison damage face at the one-point-two-five second"
               " caliber, and the weakness snapshot carries the applied level)"
            << (ok ? QString() : diag);
    });

    // ── r2069d：第二轮结构钉（id 段位 + 枚举 + 常量族 + 呈现三面 + 源钉族 + 相邻族零污染）──────────
    //   源钉 NEG 豁免面：不钉 finishEating 分流块（NEG-2 触达面）与发酵蛛眼 brew 行（NEG-1 触达面），
    //   保「恰红」单腿归因（r2067d 同款设计）。
    runLeg("r2069d structure pins (the ten round-two brewing items sit at 0x26B through 0x274 as a"
        " tail append right after SugarId 0x26A with glimmer 0x263 and the awkward-speed-strength"
        " trio untouched, the status effect enum appends fire resistance then regeneration then"
        " weakness after strength, the round-two duration and interval and penalty and heal"
        " constants hold at the MC 1.0 caliber, the brew table round-two rows and the fermented"
        " eye crafting row and the round-two source pin family hold across the drinkable face"
        " and the effect application cases and the fire immunity gates and the snapshot tail and"
        " the icon routing and the spider eye drops, and the neighbouring glimmer and sugar and"
        " ember-powder faces are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位（十连段尾追加 + 邻接族原值）。
        const bool ids = RecipeRegistry::MagmaCreamId == 0x26B
                         && RecipeRegistry::GhastTearId == 0x26C
                         && RecipeRegistry::SpiderEyeId == 0x26D
                         && RecipeRegistry::FermentedSpiderEyeId == 0x26E
                         && RecipeRegistry::GlisteringMelonId == 0x26F
                         && RecipeRegistry::FireResistancePotionId == 0x270
                         && RecipeRegistry::RegenerationPotionId == 0x271
                         && RecipeRegistry::PoisonPotionId == 0x272
                         && RecipeRegistry::WeaknessPotionId == 0x273
                         && RecipeRegistry::InstantHealthPotionId == 0x274
                         && RecipeRegistry::SugarId == 0x26A
                         && RecipeRegistry::GlimmerBottleId == 0x263
                         && int(BR::BrewingStand) == 148
                         && int(BR::Count) == 149;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 枚举尾追加序（t1097 序不变 + t1099 三连）。
        const bool enumOk = int(PlayerState::EffectStrength) == int(PlayerState::EffectFire) + 2
                            && int(PlayerState::EffectFireResistance) == int(PlayerState::EffectStrength) + 1
                            && int(PlayerState::EffectRegeneration) == int(PlayerState::EffectStrength) + 2
                            && int(PlayerState::EffectWeakness) == int(PlayerState::EffectStrength) + 3;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");
        // (3) 常量族（t1099 六新 + t1097 三旧不动）。
        const bool consts = PlayerController::kRegenPotionDurationSec == 45.0f
                            && PlayerController::kRegenPotionIntervalSec == 2.5f
                            && PlayerController::kPoisonPotionDurationSec == 45.0f
                            && PlayerController::kWeaknessDurationSec == 90.0f
                            && PlayerController::kWeaknessMeleePenaltyPerLevel == 4.0f
                            && PlayerController::kInstantHealthHealHp == 4
                            && PlayerController::kPotionDurationSec == 180.0f
                            && PlayerController::kSpeedBoostPerLevel == 0.20f
                            && PlayerController::kStrengthBonusPerLevel == 1.30f
                            && BrewingStore::kPowderFuelOps == 20
                            && BrewingStore::kBrewSecs == 20.0;
        ok = ok && consts;
        if (!consts) diag += QStringLiteral("[consts]");
        // (4) 呈现面：名面十件 + 调色板尾十连（糖 0x26A 后连续）+ maxStack 64。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::MagmaCreamId) == QStringLiteral("岩浆膏")
                            && hb.nameForBlock(RecipeRegistry::GhastTearId) == QStringLiteral("幽灵泪")
                            && hb.nameForBlock(RecipeRegistry::SpiderEyeId) == QStringLiteral("蜘蛛眼")
                            && hb.nameForBlock(RecipeRegistry::FermentedSpiderEyeId) == QStringLiteral("发酵蛛眼")
                            && hb.nameForBlock(RecipeRegistry::GlisteringMelonId) == QStringLiteral("闪烁西瓜")
                            && hb.nameForBlock(RecipeRegistry::FireResistancePotionId) == QStringLiteral("火抗药水")
                            && hb.nameForBlock(RecipeRegistry::RegenerationPotionId) == QStringLiteral("再生药水")
                            && hb.nameForBlock(RecipeRegistry::PoisonPotionId) == QStringLiteral("中毒药水")
                            && hb.nameForBlock(RecipeRegistry::WeaknessPotionId) == QStringLiteral("虚弱药水")
                            && hb.nameForBlock(RecipeRegistry::InstantHealthPotionId) == QStringLiteral("瞬间治疗药水");
        const QVariantList mats = hb.creativeMaterials();
        int sugarIdx = -1, firstR2 = -1, lastR2 = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::SugarId) sugarIdx = i;
            if (v == RecipeRegistry::MagmaCreamId) firstR2 = i;
            if (v == RecipeRegistry::InstantHealthPotionId) lastR2 = i;
        }
        const bool paletteOk = sugarIdx >= 0 && firstR2 == sugarIdx + 1 && lastR2 == firstR2 + 9;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::MagmaCreamId) == 64
                             && hb.maxStackSize(RecipeRegistry::InstantHealthPotionId) == 64
                             && hb.isMaterial(RecipeRegistry::SpiderEyeId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3/%4 stack=%5]")
                        .arg(nameOk).arg(sugarIdx).arg(firstR2).arg(lastR2).arg(stackOk);
        // (5) 全链源钉族（NEG 豁免面外；剥注释 pinSet 锚真实语句）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("magma row", "static constexpr int MagmaCreamId       = 0x26B;", 1),
            SrcPin("melon potion row", "static constexpr int InstantHealthPotionId  = 0x274;", 1),
            SrcPin("spider eye row", "static constexpr int SpiderEyeId        = 0x26D;", 1)});
        const QStringList missRc = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("assert tail", "static_assert(RecipeRegistry::InstantHealthPotionId  == 0x274,", 1),
            SrcPin("fermented row", "\"fermented_spider_eye\"", 1)});
        const QStringList missBs = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("table fire res", "return RecipeRegistry::FireResistancePotionId;", 1),
            SrcPin("table instant", "return RecipeRegistry::InstantHealthPotionId;", 1)});
        const QStringList missPs = pinSet(srcRoot + QStringLiteral("/Game/playerstate.h"), {
            SrcPin("enum tail", "EffectSpeed, EffectStrength, EffectFireResistance, EffectRegeneration, EffectWeakness", 1)});
        const QStringList missPcH = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("regen interval", "static constexpr float kRegenPotionIntervalSec = 2.5f;", 1),
            SrcPin("weak penalty", "static constexpr float kWeaknessMeleePenaltyPerLevel = 4.0f;", 1),
            SrcPin("instant heal", "static constexpr int   kInstantHealthHealHp = 4;", 1),
            SrcPin("fire res member", "float m_fireResTimer = 0.0f;", 1),
            SrcPin("regen member", "float m_regenPotionTimer = 0.0f;", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drinkable tail", "|| itemId == RecipeRegistry::InstantHealthPotionId;", 1),
            SrcPin("apply fire res", "case PlayerState::EffectFireResistance:", 1),
            SrcPin("apply weak", "case PlayerState::EffectWeakness:", 1),
            SrcPin("ignition gate", "if (touchingLava && m_fireResTimer <= 0.0f) {", 1),
            SrcPin("damage gate", "if (!earlyExtinguished && m_fireResTimer <= 0.0f)", 1),
            SrcPin("snapshot tail", "if (m_weakTimer > 0.0f) {", 1),
            SrcPin("weak hook", "dmg = std::max(0.0f, dmg - kWeaknessMeleePenaltyPerLevel", 1),
            SrcPin("clear tail", "m_weakTimer = 0.0f;", 1)});
        const QStringList missMq = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("fire res icon", "qrc:/textures/icon_effect_fireresistance.png", 1),
            SrcPin("weakness icon", "qrc:/textures/icon_effect_weakness.png", 1),
            SrcPin("spider eye drop", "if (Math.random() < 1 / 3) itemEntities.spawnItem(x, y, z, 0x26D, 1)", 2)});
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("magma case", "case 0x26B: drawMagmaCream(); break", 1),
            SrcPin("weak potion case", "case 0x273: drawPotion(", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("name row", "return QStringLiteral(\"岩浆膏\")", 1),
            SrcPin("palette row", "int(RecipeRegistry::InstantHealthPotionId)", 1)});
        const bool d5 = missRh.isEmpty() && missRc.isEmpty() && missBs.isEmpty() && missPs.isEmpty()
            && missPcH.isEmpty() && missPc.isEmpty() && missMq.isEmpty() && missMi.isEmpty()
            && missHb.isEmpty();
        ok = ok && d5;
        if (!d5) {
            const QStringList allMiss = QStringList()
                << missRh << missRc << missBs << missPs << missPcH << missPc << missMq << missMi << missHb;
            diag += QStringLiteral("[d5 %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (6) 相邻族零污染（蕴辉瓶投掷 / 糖名面 / 燃烬粉 brew 行 / 洞蛛线掉落原样）。
        const QStringList missNb = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("ember powder row untouched", "return RecipeRegistry::StrengthPotionId;", 1)});
        const bool sugarName = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("sugar name untouched", "return QStringLiteral(\"糖\")", 1)}).isEmpty();
        const bool caveString = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("cave spider string untouched", "if (Math.random() < 0.5) itemEntities.spawnItem(x, y, z, 0x219, 1)", 1)}).isEmpty();
        const bool nb = missNb.isEmpty() && sugarName && caveString;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb ember=%1 sugar=%2 cave=%3]")
                             .arg(missNb.isEmpty()).arg(sugarName).arg(caveString);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2069d structure pins (the ten round-two brewing items sit at 0x26B through 0x274 as a"
               " tail append right after SugarId 0x26A with glimmer 0x263 and the awkward-speed-strength"
               " trio untouched, the status effect enum appends fire resistance then regeneration then"
               " weakness after strength, the round-two duration and interval and penalty and heal"
               " constants hold at the MC 1.0 caliber, the brew table round-two rows and the fermented"
               " eye crafting row and the round-two source pin family hold across the drinkable face"
               " and the effect application cases and the fire immunity gates and the snapshot tail and"
               " the icon routing and the spider eye drops, and the neighbouring glimmer and sugar and"
               " ember-powder faces are untouched)"
            << (ok ? QString() : diag);
    });

    // ── r2070a：红石延长二级酿造行为柱（brewResult 红石对全表 + 负例 + 真驱一条；NEG-1 敏感面）─────
    //   NEG-1（摘 brewingstore.cpp 红石门行 + extendedPotionResult 本体）→ 恰红 = {r2070a}：静态表
    //   六对断言 + 真驱转换断言全失；b（瓶直入 hotbar，不经酿造 tick）/ c（applyStatusEffect 直调）/
    //   d（源钉豁免面——不钉 gate 行与小表体）均不受影响。表断言统一走 brewResult 单一入口（不直调
    //   extendedPotionResult——摘本体后矩阵目标零引用，链接面零缺口）。
    runLeg("r2070a redstone extended secondary brewing behavior column (the brew table answers the"
        " extended variant for redstone acting on each of the six finished effect potions, redstone"
        " on a water bottle or an awkward potion or the instant health potion or an already extended"
        " potion or a zero bottle answers zero with every other ingredient on extended potions"
        " answering zero too, and a placed brewing stand driven through a full twenty-second cycle"
        " converts a weakness potion to its extended variant on the ember powder fuel meter while a"
        " redstone-versus-awkward stand stays idle without burning fuel)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 静态表：红石 × 六成品 → 延长版（统一经 brewResult 单一入口）。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::SpeedPotionId) == RecipeRegistry::ExtendedSpeedPotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::StrengthPotionId) == RecipeRegistry::ExtendedStrengthPotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::FireResistancePotionId) == RecipeRegistry::ExtendedFireResistancePotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::RegenerationPotionId) == RecipeRegistry::ExtendedRegenerationPotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::PoisonPotionId) == RecipeRegistry::ExtendedPoisonPotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::WeaknessPotionId) == RecipeRegistry::ExtendedWeaknessPotionId;
        // (2) 负例：红石 × {水 / 粗制 / 瞬间治疗[即时不可延长] / 延长版再酿 / 零瓶} → 0；他料 × 延长版 → 0。
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::AwkwardPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::InstantHealthPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::ExtendedSpeedPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, 0) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::ExtendedSpeedPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::MagmaCreamId, RecipeRegistry::ExtendedWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::ExtendedFireResistancePotionId) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (3) 真驱转换：虚弱药水 + 红石 + 燃烬粉 → 20s 一轮 → 延长虚弱 + 耗 1 原料 + 计量 -1。
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24;
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::WeaknessPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::RedstoneId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const int steps = int(20.6 / 0.05);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool driven = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::ExtendedWeaknessPotionId
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        ok = ok && driven;
        if (!driven) diag += QStringLiteral("[driven id=%1 ing=%2 ops=%3]")
                                  .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                  .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                  .arg(store.fuelOpsAt(bxp, byp, bzp));
        // (4) 负例真驱：粗制 + 红石 → 无合格瓶位 → 进度保持 0 + 燃料零空烧（粉计数 / 计量均不动）。
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::AwkwardPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::RedstoneId, 1);
        const int opsBeforeIdle = store.fuelOpsAt(bxp, byp, bzp);
        const int powderBeforeIdle = store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool idle = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::AwkwardPotionId
                          && store.brewProgressAt(bxp, byp, bzp) == 0.0
                          && store.fuelOpsAt(bxp, byp, bzp) == opsBeforeIdle
                          && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel) == powderBeforeIdle;
        ok = ok && idle;
        if (!idle) diag += QStringLiteral("[idle id=%1 prog=%2 ops=%3->%4 pw=%5->%6]")
                              .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                              .arg(store.brewProgressAt(bxp, byp, bzp))
                              .arg(opsBeforeIdle).arg(store.fuelOpsAt(bxp, byp, bzp))
                              .arg(powderBeforeIdle).arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotFuel));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2070a redstone extended secondary brewing behavior column (the brew table answers the"
               " extended variant for redstone acting on each of the six finished effect potions, redstone"
               " on a water bottle or an awkward potion or the instant health potion or an already extended"
               " potion or a zero bottle answers zero with every other ingredient on extended potions"
               " answering zero too, and a placed brewing stand driven through a full twenty-second cycle"
               " converts a weakness potion to its extended variant on the ember powder fuel meter while a"
               " redstone-versus-awkward stand stays idle without burning fuel)"
            << (ok ? QString() : diag);
    });

    // ── r2070b：延长版饮用行为柱 + 饮用音效呈现柱（finishEating 真链；NEG-2 敏感面）────────────────
    //   NEG-2（摘 finishEating t1100 延长分流链）→ 恰红 = {r2070b}：延长秒数（90 / 240）断言全失
    //   （isDrinkableItem 早退面不在 NEG-2 摘除面 → 饮用仍消耗 / 返瓶 / 发 potionDrunk，唯效果时长面
    //   失）；a / c / d（源钉豁免面——不钉分流链）均不受影响。
    //   饮用音效呈现柱：drinkGulp == 5/次（节拍 0..4 五沿；dt 钳 0.05 → 恰 32 tick 满，节拍确定性）+
    //   potionDrunk 每 1 次/饮毕（非空转钉——计数经真链完成沿驱动）。快照 seconds 断言 =
    //   buildActiveEffects 首拍 ceil 整秒面（90/240 整值直落，窗口安全）。
    runLeg("r2070b extended potion drink chain behavior and presentation column (a real survival"
        " player drinking the extended regeneration potion through the full eat chain raises a"
        " regeneration snapshot entry at level one with the ninety-second extended caliber while"
        " drinking the extended weakness potion raises weakness at the two-hundred-fortieth-second"
        " caliber, each completed drink fires exactly five drink-gulp beats and one drink-finished"
        " signal carrying the potion id, and each drink consumes one potion in survival and returns"
        " one empty bottle)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 18, 44, 18, 30);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air));
        // 快照首拍闩存（seconds 只取首拍——后续整秒衰减不改闩存）+ gulp / 饮毕计数（跨链累积，
        //   分段基线差分断言）。
        int regenSeconds = -1, regenLevel = -1;
        int weakSeconds = -1, weakLevel = -1;
        int gulpCount = 0, drunkCount = 0, lastDrunkId = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) {
            for (const QVariant &v : l) {
                const QVariantMap m = v.toMap();
                const int ty = m.value(QStringLiteral("type")).toInt();
                if (ty == int(PlayerState::EffectRegeneration) && regenSeconds < 0) {
                    regenSeconds = m.value(QStringLiteral("seconds")).toInt();
                    regenLevel = m.value(QStringLiteral("level")).toInt();
                }
                if (ty == int(PlayerState::EffectWeakness) && weakSeconds < 0) {
                    weakSeconds = m.value(QStringLiteral("seconds")).toInt();
                    weakLevel = m.value(QStringLiteral("level")).toInt();
                }
            }
        });
        QObject::connect(&pc, &PlayerController::drinkGulp, &pc, [&](float, float, float, int) { ++gulpCount; });
        QObject::connect(&pc, &PlayerController::potionDrunk, &pc, [&](int id) { ++drunkCount; lastDrunkId = id; });
        const auto pump = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 链 A：延长再生药水（生存）——EffectRegeneration 挂上 + 首拍 seconds=90 + level 1 + 5 gulp +
        //   1 饮毕 + 耗 1 + 返 1 空瓶。
        hb.setStack(0, RecipeRegistry::ExtendedRegenerationPotionId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pump(320); // 越过放置 CD
        pc.tick();
        pc.beginEating();
        const bool eatingStartedA = pc.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        const int gulpBaseA = gulpCount;
        int bottlesA = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesA += hb.countAt(i);
        pc.tick(); // 一帧推进 → 快照发出
        const bool regenOk = eatingStartedA && !pc.eating()
                             && regenSeconds == 90 && regenLevel == 1
                             && gulpBaseA == 5
                             && drunkCount == 1 && lastDrunkId == RecipeRegistry::ExtendedRegenerationPotionId
                             && bottlesA == 1
                             && hb.blockIdAt(0) == RecipeRegistry::ExtendedRegenerationPotionId
                             && hb.countAt(0) == 1; // 耗 1（剩 1）
        ok = ok && regenOk;
        if (!regenOk) diag += QStringLiteral("[regen secs=%1 lvl=%2 gulp=%3 drunk=%4 id=%5 b=%6 n0=%7]")
                                  .arg(regenSeconds).arg(regenLevel).arg(gulpBaseA).arg(drunkCount)
                                  .arg(lastDrunkId).arg(bottlesA).arg(hb.countAt(0));
        // 链 B：延长虚弱药水（生存）——EffectWeakness 挂上 + 首拍 seconds=240 + 本段 5 gulp（累计 10）+
        //   饮毕计数 2（末 id = 延长虚弱）。
        hb.setStack(0, RecipeRegistry::ExtendedWeaknessPotionId, 2, 0); // 选中槽仍 0（链 A 未换槽）
        pump(320); // 越过 finishEating 冷却 / eat CD
        pc.tick();
        pc.beginEating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        pc.tick();
        int bottlesB = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesB += hb.countAt(i);
        const bool weakOk = weakSeconds == 240 && weakLevel == 1
                            && gulpCount - gulpBaseA == 5
                            && drunkCount == 2 && lastDrunkId == RecipeRegistry::ExtendedWeaknessPotionId
                            && bottlesB == 2 // 链 A 返 1 + 链 B 返 1
                            && hb.blockIdAt(0) == RecipeRegistry::ExtendedWeaknessPotionId
                            && hb.countAt(0) == 1; // 耗 1（剩 1）
        ok = ok && weakOk;
        if (!weakOk) diag += QStringLiteral("[weak secs=%1 lvl=%2 gulp=%3 drunk=%4 id=%5 b=%6 n0=%7]")
                                  .arg(weakSeconds).arg(weakLevel).arg(gulpCount - gulpBaseA)
                                  .arg(drunkCount).arg(lastDrunkId).arg(bottlesB).arg(hb.countAt(0));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2070b extended potion drink chain behavior and presentation column (a real survival"
               " player drinking the extended regeneration potion through the full eat chain raises a"
               " regeneration snapshot entry at level one with the ninety-second extended caliber while"
               " drinking the extended weakness potion raises weakness at the two-hundred-fortieth-second"
               " caliber, each completed drink fires exactly five drink-gulp beats and one drink-finished"
               " signal carrying the potion id, and each drink consumes one potion in survival and returns"
               " one empty bottle)"
            << (ok ? QString() : diag);
    });

    // ── r2070c：效果粒子呈现柱（applyStatusEffect 直调驱动；双 NEG 均不触达 = 对照腿）───────────────
    //   发射节奏：kEffectParticleIntervalSec=0.5s，dt 钳 0.05 → 每 10 tick 一沿（确定性）；效果到期
    //   （fxActive 翻假）→ 累积器归零 → 到期后零再发。主效果选取（多效果同显取枚举序最小者）同柱钉。
    runLeg("r2070c effect particle presentation column (a survival controller with a live"
        " regeneration effect emits effect particles on the half-second cadence carrying the"
        " regeneration type and falls silent once the effect expires, and with weakness and"
        " regeneration both live the particles carry the lower-enum primary effect type)", [&]() {
        bool ok = true;
        QString diag;
        const auto pumpTicks = [](PlayerController &p, int iters) {
            for (int i = 0; i < iters; ++i) { QThread::msleep(60); p.tick(); } // 每拍 0.05s 模拟时（dt 钳制上界）
        };
        // (1) 再生单效果：2.4s 效果 → 4 沿（0.5/1.0/1.5/2.0s；到期沿不再发）→ 到期后 1.2s 零新增。
        World w1;
        initFixedBrewWorld(w1);
        layBrewPlatform(w1, 20, 30, 20, 30);
        PlayerController pc1;
        pc1.setWorld(&w1);
        int regenFx = 0, otherFx = 0;
        QObject::connect(&pc1, &PlayerController::effectParticle, &pc1, [&](float, float, float, int ty) {
            if (ty == int(PlayerState::EffectRegeneration)) ++regenFx;
            else ++otherFx;
        });
        pc1.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pc1.applyStatusEffect(PlayerState::EffectRegeneration, 2.4f, 1);
        pumpTicks(pc1, 48); // 2.4s = 恰效果窗
        ok = ok && regenFx == 4 && otherFx == 0;
        if (!(regenFx == 4 && otherFx == 0))
            diag += QStringLiteral("[regen fx=%1 other=%2]").arg(regenFx).arg(otherFx);
        const int regenAfterWindow = regenFx + otherFx;
        pumpTicks(pc1, 24); // 1.2s 到期后窗
        ok = ok && regenFx + otherFx == regenAfterWindow;
        if (regenFx + otherFx != regenAfterWindow)
            diag += QStringLiteral("[tail delta=%1]").arg(regenFx + otherFx - regenAfterWindow);
        // (2) 主效果选取（pc2 fresh 隔离累积器）：虚弱 + 再生同显 → 携 EffectRegeneration（枚举序最小）。
        World w2;
        initFixedBrewWorld(w2);
        layBrewPlatform(w2, 20, 30, 20, 30);
        PlayerController pc2;
        pc2.setWorld(&w2);
        int primRegen = 0, primOther = 0;
        QObject::connect(&pc2, &PlayerController::effectParticle, &pc2, [&](float, float, float, int ty) {
            if (ty == int(PlayerState::EffectRegeneration)) ++primRegen;
            else ++primOther;
        });
        pc2.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pc2.applyStatusEffect(PlayerState::EffectWeakness, 1.2f, 1);
        pc2.applyStatusEffect(PlayerState::EffectRegeneration, 1.2f, 1);
        pumpTicks(pc2, 24); // 1.2s = 恰效果窗
        ok = ok && primRegen == 2 && primOther == 0;
        if (!(primRegen == 2 && primOther == 0))
            diag += QStringLiteral("[primary regen=%1 other=%2]").arg(primRegen).arg(primOther);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2070c effect particle presentation column (a survival controller with a live"
               " regeneration effect emits effect particles on the half-second cadence carrying the"
               " regeneration type and falls silent once the effect expires, and with weakness and"
               " regeneration both live the particles carry the lower-enum primary effect type)"
            << (ok ? QString() : diag);
    });

    // ── r2070d：第三轮结构钉（id 段位 + 延长常量族 + 呈现三面 + 源钉族 + 相邻族零污染）──────────────
    //   源钉 NEG 豁免面（r2069d 同款设计）：不钉 brewResult 红石门行 / extendedPotionResult 小表体
    //   （NEG-1 触达面）与 finishEating 延长分流链（NEG-2 触达面），保「恰红」单腿归因；二级表钉
    //   只钉 .h 声明面（摘本体不摘声明 → 编译仍绿、钉面幸存）。
    runLeg("r2070d structure pins (the six extended potions sit at 0x275 through 0x27A as a tail"
        " append right after InstantHealthPotionId 0x274 with the round-two family and the"
        " awkward-speed-strength trio untouched, the extended duration constants hold at the MC 1.0"
        " extended calibers with the round-one and round-two families intact, the name face palette"
        " tail and icon cases and max stack 64 hold, the secondary table declaration and the"
        " drinkable extension face and the presentation signal family hold at source, the audio"
        " clips and the qml routing face hold, and the neighbouring round-two instant-health row"
        " and the ember strength row and the magma name are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位（六连段尾追加 + 邻接族原值）。
        const bool ids = RecipeRegistry::ExtendedSpeedPotionId == 0x275
                         && RecipeRegistry::ExtendedStrengthPotionId == 0x276
                         && RecipeRegistry::ExtendedFireResistancePotionId == 0x277
                         && RecipeRegistry::ExtendedRegenerationPotionId == 0x278
                         && RecipeRegistry::ExtendedPoisonPotionId == 0x279
                         && RecipeRegistry::ExtendedWeaknessPotionId == 0x27A
                         && RecipeRegistry::InstantHealthPotionId == 0x274
                         && RecipeRegistry::SugarId == 0x26A
                         && RecipeRegistry::GlimmerBottleId == 0x263
                         && int(BR::BrewingStand) == 148
                         && int(BR::Count) == 149;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 延长常量族（六新 [四常量] + t1097/t1099 族不动）。
        const bool consts = PlayerController::kExtPotionDurationSec == 480.0f
                            && PlayerController::kRegenExtPotionDurationSec == 90.0f
                            && PlayerController::kPoisonExtPotionDurationSec == 90.0f
                            && PlayerController::kWeaknessExtDurationSec == 240.0f
                            && PlayerController::kEffectParticleIntervalSec == 0.5f
                            && PlayerController::kPotionDurationSec == 180.0f
                            && PlayerController::kRegenPotionDurationSec == 45.0f
                            && PlayerController::kPoisonPotionDurationSec == 45.0f
                            && PlayerController::kWeaknessDurationSec == 90.0f
                            && PlayerController::kInstantHealthHealHp == 4
                            && BrewingStore::kPowderFuelOps == 20
                            && BrewingStore::kBrewSecs == 20.0;
        ok = ok && consts;
        if (!consts) diag += QStringLiteral("[consts]");
        // (3) 呈现面：名面六件 + 调色板尾六连（瞬间治疗 0x274 后连续）+ maxStack 64 + 材料段判定。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::ExtendedSpeedPotionId) == QStringLiteral("迅捷药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::ExtendedStrengthPotionId) == QStringLiteral("力量药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::ExtendedFireResistancePotionId) == QStringLiteral("火抗药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::ExtendedRegenerationPotionId) == QStringLiteral("再生药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::ExtendedPoisonPotionId) == QStringLiteral("中毒药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::ExtendedWeaknessPotionId) == QStringLiteral("虚弱药水（延长）");
        const QVariantList mats = hb.creativeMaterials();
        int lastR2 = -1, firstExt = -1, lastExt = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::InstantHealthPotionId) lastR2 = i;
            if (v == RecipeRegistry::ExtendedSpeedPotionId) firstExt = i;
            if (v == RecipeRegistry::ExtendedWeaknessPotionId) lastExt = i;
        }
        const bool paletteOk = lastR2 >= 0 && firstExt == lastR2 + 1 && lastExt == firstExt + 5;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::ExtendedSpeedPotionId) == 64
                             && hb.maxStackSize(RecipeRegistry::ExtendedWeaknessPotionId) == 64
                             && hb.isMaterial(RecipeRegistry::ExtendedRegenerationPotionId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3/%4 stack=%5]")
                        .arg(nameOk).arg(lastR2).arg(firstExt).arg(lastExt).arg(stackOk);
        // (4) 全链源钉族（剥注释 pinSet 锚真实语句；NEG 触达面全豁免）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("ext speed row", "static constexpr int ExtendedSpeedPotionId          = 0x275;", 1),
            SrcPin("ext weak row", "static constexpr int ExtendedWeaknessPotionId       = 0x27A;", 1)});
        const QStringList missRc = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("assert tail", "static_assert(RecipeRegistry::ExtendedSpeedPotionId          == 0x275,", 1)});
        const QStringList missBsH = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.h"), {
            SrcPin("secondary decl", "static int extendedPotionResult(int potionId);", 1)});
        const QStringList missPcH = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("ext duration", "static constexpr float kExtPotionDurationSec = 480.0f;", 1),
            SrcPin("regen ext", "static constexpr float kRegenExtPotionDurationSec = 90.0f;", 1),
            SrcPin("poison ext", "static constexpr float kPoisonExtPotionDurationSec = 90.0f;", 1),
            SrcPin("weak ext", "static constexpr float kWeaknessExtDurationSec = 240.0f;", 1),
            SrcPin("fx interval", "static constexpr float kEffectParticleIntervalSec = 0.5f;", 1),
            SrcPin("fx accum member", "float m_effectParticleAccum = 0.0f;", 1),
            SrcPin("gulp signal", "void drinkGulp(float x, float y, float z, int itemId);", 1),
            SrcPin("drunk signal", "void potionDrunk(int itemId);", 1),
            SrcPin("fx signal", "void effectParticle(float x, float y, float z, int effectType);", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drinkable ext switch", "case RecipeRegistry::ExtendedWeaknessPotionId:", 1),
            SrcPin("gulp beat split", "emit drinkGulp(mouth.x(), mouth.y(), mouth.z(), m_hotbar->selectedItemId());", 1),
            SrcPin("drunk emit", "emit potionDrunk(eatenId);", 1),
            SrcPin("fx emit", "emit effectParticle(pp.x(), pp.y(), pp.z(), primary);", 1),
            SrcPin("fx clear face", "m_effectParticleAccum = 0.0f;", 3)});
        const QStringList missMq = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("gulp route", "audio.playDrinkGulp()", 1),
            SrcPin("burp route", "audio.playBurp()", 1),
            SrcPin("fx route", "particleLoader.item.burstEffect(x, y, z, effectType)", 1)});
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 275", "case 0x275: drawPotion(", 1),
            SrcPin("icon 276", "case 0x276: drawPotion(", 1),
            SrcPin("icon 277", "case 0x277: drawPotion(", 1),
            SrcPin("icon 278", "case 0x278: drawPotion(", 1),
            SrcPin("icon 279", "case 0x279: drawPotion(", 1),
            SrcPin("icon 27A", "case 0x27A: drawPotion(", 1)});
        const QStringList missBp = pinSet(srcRoot + QStringLiteral("/ui/BlockParticles.qml"), {
            SrcPin("burst effect fn", "function burstEffect(x, y, z, effectType) {", 1),
            SrcPin("effect color fn", "function effectColor(type) {", 1)});
        const QStringList missAmH = pinSet(srcRoot + QStringLiteral("/Audio/audiomanager.h"), {
            SrcPin("gulp decl", "Q_INVOKABLE void playDrinkGulp();", 1),
            SrcPin("burp decl", "Q_INVOKABLE void playBurp();", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("name row", "return QStringLiteral(\"迅捷药水（延长）\")", 1),
            SrcPin("palette row", "int(RecipeRegistry::ExtendedWeaknessPotionId)", 1)});
        const bool d5 = missRh.isEmpty() && missRc.isEmpty() && missBsH.isEmpty() && missPcH.isEmpty()
            && missPc.isEmpty() && missMq.isEmpty() && missMi.isEmpty() && missBp.isEmpty()
            && missAmH.isEmpty() && missHb.isEmpty();
        ok = ok && d5;
        if (!d5) {
            const QStringList allMiss = QStringList()
                << missRh << missRc << missBsH << missPcH << missPc << missMq << missMi
                << missBp << missAmH << missHb;
            diag += QStringLiteral("[d5 %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (5) 声音资产在案（qrc 资源面：CMakeLists 双行 + build_sounds 生成器双函数）。
        const QString rootDir = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absolutePath();
        const bool cmakeRows = pinSet(rootDir + QStringLiteral("/CMakeLists.txt"), {
            SrcPin("gulp asset", "sounds/drink_gulp.wav", 1),
            SrcPin("burp asset", "sounds/burp.wav", 1)}).isEmpty();
        const bool genFns = pinSet(rootDir + QStringLiteral("/tools/build_sounds.py"), {
            SrcPin("gulp gen", "def gen_drink_gulp():", 1),
            SrcPin("burp gen", "def gen_burp():", 1)}).isEmpty();
        ok = ok && cmakeRows && genFns;
        if (!(cmakeRows && genFns))
            diag += QStringLiteral("[assets cmake=%1 gen=%2]").arg(cmakeRows).arg(genFns);
        // (6) 相邻族零污染（第二轮瞬间治疗 brew 行 / 第一轮力量 brew 行 / 岩浆膏名面 / 0x274 图标原样）。
        const QStringList missNb = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("round-two instant row untouched", "return RecipeRegistry::InstantHealthPotionId;", 1),
            SrcPin("round-one strength row untouched", "return RecipeRegistry::StrengthPotionId;", 1)});
        const bool magmaName = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("magma name untouched", "return QStringLiteral(\"岩浆膏\")", 1)}).isEmpty();
        const bool icon274 = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 274 untouched", "case 0x274: drawPotion(", 1)}).isEmpty();
        const bool nb = missNb.isEmpty() && magmaName && icon274;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb rows=%1 magma=%2 icon=%3]")
                             .arg(missNb.isEmpty()).arg(magmaName).arg(icon274);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2070d structure pins (the six extended potions sit at 0x275 through 0x27A as a tail"
               " append right after InstantHealthPotionId 0x274 with the round-two family and the"
               " awkward-speed-strength trio untouched, the extended duration constants hold at the MC 1.0"
               " extended calibers with the round-one and round-two families intact, the name face palette"
               " tail and icon cases and max stack 64 hold, the secondary table declaration and the"
               " drinkable extension face and the presentation signal family hold at source, the audio"
               " clips and the qml routing face hold, and the neighbouring round-two instant-health row"
               " and the ember strength row and the magma name are untouched)"
            << (ok ? QString() : diag);
    });
}
