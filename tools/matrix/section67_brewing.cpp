#include "matrix_helpers.h"

// t1097 酿造台 + 药水系统探针段（4 腿；filter 词 r2067；矩阵 806→810）。置尾先例沿用（接 section66，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig）。
// t1103 闪烁西瓜生存整链（+4 腿 r2073a-d，置尾追加；矩阵 828→832）：金粒 / 瓜种 / 瓜片物品段 +
//   瓜块 / 瓜茎方块段 + 瓜茎 crop 原型（茎蔓完整原型候选池登记，裁定注见 blockregistry.h MelonStem 行）
//   + 矿井池瓜种行 + 五条合成行 + 端到端生存链闭合（瓜子→瓜→瓣+金粒→闪烁西瓜→瞬间治疗）。
//   NEG 面与豁免设计（恰红归因先于腿文；段尾阴注同文）：
//   NEG-1 = 摘瓜生长结果门（world.cpp tickCropGrowth 成熟茎结果分支）→ 恰红 = {r2073b, r2073d}
//     （b 结果面 + d 链瓜源断链；生长序列 / 双门阴性 / 破茎掉种面不触摘面但 b 腿红；a/c 零世界零触达）。
//   NEG-2 = 摘环形合成行（recipe.cpp glistering_melon 行）→ 恰红 = {r2073c, r2073d}（c 环形命中面 +
//     d 链酿造原料断链；a 的金粒双向行与池行不在摘面，b 不触合成）。钉面双摘面全豁免（d 不钉两分支体）。
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
// t1101 喷溅药水（+4 腿 r2071a-d，置尾追加；矩阵 820→824）：火药三级酿造 + 投掷弹丸 + 范围效果结算。
//   NEG 面与豁免设计（恰红归因先于腿文）：
//   NEG-1 = 摘火药映射（brewingstore.cpp brewResult 火药门行 + splashPotionResult 本体一并摘——函数仅
//     gate 行一处调用、.h 声明幸存 → 编译仍绿）→ 恰红 = {r2071a}（静态表火药对 + 真驱转换断言全失；
//     摘面不含 isDrinkableItem / isSplashPotionItem → 饮面回归断言幸存绿）；b（投掷/半径，不经酿造）/
//     c（applySplashPotion 直调，静态映射族不在摘面）/ d（源钉不钉 gate 行与小表体 = 豁免面，只钉
//     .h 声明）均不受影响。
//   NEG-2 = 摘范围结算面（playercontroller.cpp applySplashPotion 本体的半径判定 + applyStatusEffect
//     结算尾段摘除——Q_INVOKABLE 声明 / splashEffectType / splashBaseSeconds 静态映射族幸存 → 编译
//     仍绿）→ 恰红 = {r2071b, r2071c}（b 真链半径注入断言 + c 直调注入断言全失；两腿的碎裂/信号/
//     静态映射断言不在摘面、各自部分幸存但腿体判 FAIL）；a / d（源钉不钉函数体 = 豁免面）均不受
//     影响。
//
// 任务契约：§14 池底重盘压轴大件最后一轮（机制等价 MC 1.0 splash potion；wiki 2026 实读口径：火药 +
//   成品药水 → 喷溅版（基础 6 + 延长 6 = 12 id 段尾追加 0x27B..0x286）；触地点半径 4 格（dSq<16）+
//   邻近线性衰减 d1 = 1 − dist/4 + 时长 int(ticks×d1)+1（秒化 = d1×base + 0.05）——喷溅时长 ≠ 饮用时长；
//   喷溅版不可饮不可再酿；即时效果喷溅 / modifier 对喷溅再酿候选池登记）。mob 侧范围面如实降级
//   候选池（核实裁定：本工程 mob 无通用效果注入系统，仅 slowTimer/fireTimer 硬编码面——禁硬造）。
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

// 源钉根路径（t1102 r2072 置尾腿共用：applicationDirPath/../src——既有腿局部 srcRoot 同式收拢成
//   匿名空间 helper，免每腿重抄三行）。
inline QString srcRootForPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 裸读源文件原文（块注释锚面——pinSet 剥注释不可锚时用；r2067d readRaw 同门收拢）。
inline QString rawSource(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
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
        const bool t2 = BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::AwkwardPotionId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::BlazePowderId, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::SpeedPotionId) == 0
                        && BrewingStore::brewResult(0, RecipeRegistry::WaterBottleId) == 0
                        && BrewingStore::brewResult(RecipeRegistry::AshWartId, 0) == 0;
        // lawful 钉修订沿革（t1102）：t2 首负例原为 SugarId × WaterBottleId == 0（t1097 时点的「错料」
        //   口径）。t1102 交付 1.0 水瓶直酿面（水瓶 + 糖 → 凡庸 MundanePotionId，wiki 2026 实读 1.0
        //   图谱既有行）后该对不再是负例 → 改钉 GlisteringMelonId × WaterBottleId == 0（闪烁西瓜 × 水
        //   瓶行 t1102 裁定不交付——1.0 纪元形态存疑不硬造，recipe.h 0x287 注留痕——故恒为零的 lawful
        //   负例；断言语义不变：仍是「错料 × 水瓶 → 0」家族）。
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
                         && int(BR::Count) == 160 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1105 曾 151→154
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
        const bool consts = int(BR::AtlasTileCount) == 209 // t1112 lawful 前移：207→209（蛋糕族 tile 207..208 追加）；t1105 lawful 前移：201→207（南瓜族 tile 201..205 + 炼药锅 206 追加；t1103 曾 195→201、t1097 曾 193→195）
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
            SrcPin("count row", "Count           = 160,", 1), // t1112 lawful 前移：钉文 157→160 随源行前移（栅栏门/玻璃板/蛋糕尾部追加）；沿革 t1111 lawful 前移：钉文 154→157 随源行前移
                //   t1105 lawful 前移：151→154（PumpkinStem=151/JackOLantern=152/Cauldron=153 尾部追加；
                //   t1103 曾 149→151、t1097 曾 148→149——追加不插中间存档契约，钉值随追加前移）。
            SrcPin("shape row", "ShapeBrewingStand = 13,", 1),
            SrcPin("boxes decl", "static int brewingStandShapeBoxes(quint8 state, BlockAABB *out, int cap);", 1),
            SrcPin("lit flag", "static constexpr quint8 BrewingStandStateLitFlag = 0x01;", 1),
            SrcPin("atlas", "static constexpr int AtlasTileCount = 209;", 1)});
                //   t1105 lawful 前移：201→207（南瓜族 tile 201..205 + 炼药锅 206 追加；t1103 曾 195→201、
                //   t1097 曾 193→195）。
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
                         && int(BR::Count) == 160; // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1105 曾 151→154
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
                         && int(BR::Count) == 160; // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1105 曾 151→154
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
            << (ok ? QString() : diag);
    });

    // ── r2071a：火药三级酿造转换行为柱（静态表 12 对 + 负例族 + 真驱一条 + 饮面回归；NEG-1 敏感面）──
    //   NEG-1（摘 brewingstore.cpp 火药门行 + splashPotionResult 本体）→ 恰红 = {r2071a}：静态表
    //   12 对断言 + 真驱转换断言全失（饮面回归断言 isDrinkableItem 面不在摘面 → 幸存绿；本腿以
    //   t1/t3 两段失 FAIL）；b / c（不经酿造转换）/ d（源钉豁免面）均不受影响。
    runLeg("r2071a gunpowder tertiary brewing conversion behavior column (the brew table maps"
        " gunpowder acting on each of the six finished effect potions to its splash variant and on"
        " each of the six extended potions to its extended splash variant, gunpowder on a water"
        " bottle or an awkward potion or the instant health potion or an already splash potion or a"
        " zero bottle answers zero with redstone and every other ingredient on splash potions"
        " answering zero too, a placed brewing stand driven through a full twenty-second cycle"
        " converts a speed potion to its splash variant on the ember powder fuel meter, and none of"
        " the twelve splash ids is a drinkable item while all twelve answer the splash predicate)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 静态表：火药 × 12 成品 → 喷溅版（统一经 brewResult 单一入口）。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SpeedPotionId) == RecipeRegistry::SplashSpeedPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::StrengthPotionId) == RecipeRegistry::SplashStrengthPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::FireResistancePotionId) == RecipeRegistry::SplashFireResistancePotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::RegenerationPotionId) == RecipeRegistry::SplashRegenerationPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::PoisonPotionId) == RecipeRegistry::SplashPoisonPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::WeaknessPotionId) == RecipeRegistry::SplashWeaknessPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedSpeedPotionId) == RecipeRegistry::SplashExtendedSpeedPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedStrengthPotionId) == RecipeRegistry::SplashExtendedStrengthPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedFireResistancePotionId) == RecipeRegistry::SplashExtendedFireResistancePotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedRegenerationPotionId) == RecipeRegistry::SplashExtendedRegenerationPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedPoisonPotionId) == RecipeRegistry::SplashExtendedPoisonPotionId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::ExtendedWeaknessPotionId) == RecipeRegistry::SplashExtendedWeaknessPotionId;
        // (2) 负例族：火药 × {水 / 粗制 / 瞬间治疗 / 喷溅再酿 / 零瓶} → 0；红石 / 他料 × 喷溅版 → 0。
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::AwkwardPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::InstantHealthPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashSpeedPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashExtendedWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, 0) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::SplashSpeedPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::SplashExtendedFireResistancePotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::SplashWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::SplashExtendedSpeedPotionId) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (3) 真驱转换：迅捷药水 + 火药 + 燃烬粉 → 20s 一轮 → 喷溅迅捷 + 耗 1 原料 + 计量 -1。
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24;
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::SpeedPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::GunpowderId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const int steps = int(20.6 / 0.05);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool driven = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::SplashSpeedPotionId
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        ok = ok && driven;
        if (!driven) diag += QStringLiteral("[driven id=%1 ing=%2 ops=%3]")
                                  .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                  .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                  .arg(store.fuelOpsAt(bxp, byp, bzp));
        // (4) 饮面回归（NEG-1 幸存面）：12 喷溅 id 全部不可饮 + 全部命中喷溅谓词（互斥即饮面回归）。
        const int splashIds[12] = {
            RecipeRegistry::SplashSpeedPotionId, RecipeRegistry::SplashStrengthPotionId,
            RecipeRegistry::SplashFireResistancePotionId, RecipeRegistry::SplashRegenerationPotionId,
            RecipeRegistry::SplashPoisonPotionId, RecipeRegistry::SplashWeaknessPotionId,
            RecipeRegistry::SplashExtendedSpeedPotionId, RecipeRegistry::SplashExtendedStrengthPotionId,
            RecipeRegistry::SplashExtendedFireResistancePotionId, RecipeRegistry::SplashExtendedRegenerationPotionId,
            RecipeRegistry::SplashExtendedPoisonPotionId, RecipeRegistry::SplashExtendedWeaknessPotionId };
        bool drinkNeg = true, predPos = true;
        for (int i = 0; i < 12; ++i) {
            if (PlayerController::isDrinkableItem(splashIds[i])) drinkNeg = false;
            if (!PlayerController::isSplashPotionItem(splashIds[i])) predPos = false;
        }
        ok = ok && drinkNeg && predPos;
        if (!(drinkNeg && predPos))
            diag += QStringLiteral("[drink drinkNeg=%1 predPos=%2]").arg(drinkNeg).arg(predPos);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2071a gunpowder tertiary brewing conversion behavior column (the brew table maps"
               " gunpowder acting on each of the six finished effect potions to its splash variant and on"
               " each of the six extended potions to its extended splash variant, gunpowder on a water"
               " bottle or an awkward potion or the instant health potion or an already splash potion or a"
               " zero bottle answers zero with redstone and every other ingredient on splash potions"
               " answering zero too, a placed brewing stand driven through a full twenty-second cycle"
               " converts a speed potion to its splash variant on the ember powder fuel meter, and none of"
               " the twelve splash ids is a drinkable item while all twelve answer the splash predicate)"
            << (ok ? QString() : diag);
    });

    // ── r2071b：投掷链真 rig + 范围注入柱（NEG-2 敏感面：真链半径注入断言）────────────────────────
    //   NEG-2（摘 applySplashPotion 本体半径判定 + 结算尾段）→ 恰红 = {r2071b, r2071c}：本腿链 C
    //   近距注入断言全失（链 A 投掷 / 碎裂信号 / 消耗断言不在摘面 → 幸存，唯效果面失 → 腿体 FAIL）。
    //   链 B 出圈无注入（NEG-2 下恒绿——无效果即断言面）。a / d 不受影响。
    runLeg("r2071b splash bottle throw chain and radius injection column (a real creative throw"
        " spawns a SplashBottle entity carrying the splash speed potion id whose shatter on the"
        " stone platform emits exactly one splashBottleBreak signal with the carried item id, the"
        " follow-up survival throw consumes exactly one bottle, a survival player standing at the"
        " impact cell receives the speed effect through the mirrored presentation routing at the"
        " proximity-decayed caliber while a survival player four-plus blocks away receives nothing,"
        " and a zero-item throw entry is impossible because the hotbar held item gates the branch)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 18, 44, 18, 30);
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
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线（t1030 rig 纪律）
        // 呈现层消费端镜像（t799 教训：真链语义由消费端动作完成——Main.qml onSplashBottleBreak 三面
        //   中机制面 = applySplashPotion；粒子 / 音频为呈现侧 rig 无消费面）。
        int breakCount = 0, breakX = -1, breakY = -1, breakZ = -1, breakItem = -1;
        int snapSpeedCount = 0, snapSpeedSecs = -1, snapSpeedLvl = -1;
        QObject::connect(&ents, &EntityManager::splashBottleBreak, &pc,
                         [&](int x, int y, int z, int itemId) {
                             ++breakCount; breakX = x; breakY = y; breakZ = z; breakItem = itemId;
                             pc.applySplashPotion(x, y, z, itemId); // 镜像 Main.qml 路由（Game 层结算）
                         });
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) {
                             for (const QVariant &v : l) {
                                 const QVariantMap m = v.toMap();
                                 if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectSpeed)) {
                                     ++snapSpeedCount;
                                     if (snapSpeedSecs < 0) {
                                         snapSpeedSecs = m.value(QStringLiteral("seconds")).toInt();
                                         snapSpeedLvl = m.value(QStringLiteral("level")).toInt();
                                     }
                                 }
                             }
                         });
        // 链 A：创造投掷（真 placeBlock 入口）——实体 + 载荷 + 挥手 + 创造不耗。
        hb.setStack(0, RecipeRegistry::SplashSpeedPotionId, 5, 0);
        hb.setSelectedSlot(0);
        const QVector3D feet(24.5f, 81.0f, 24.5f);
        const QVector3D look(1.0f, 0.0f, 0.0f);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 1 /* Creative */);
        pumpFor(17);
        pc.tick();
        int swingSeen = 0;
        QObject::connect(&pc, &PlayerController::swingArm, &pc, [&]() { ++swingSeen; });
        pc.placeBlock(); // 右键投掷（创造）
        int slot = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::SplashBottle)) { slot = i; break; }
        const bool thrownOk = slot >= 0
                              && ents.blockIdAt(slot) == RecipeRegistry::SplashSpeedPotionId // 载荷 = 喷溅 id
                              && hb.blockIdAt(0) == RecipeRegistry::SplashSpeedPotionId
                              && hb.countAt(0) == 5 /* 创造不耗 */ && swingSeen >= 1;
        ok = ok && thrownOk;
        if (!thrownOk)
            diag += QStringLiteral("[throw slot=%1 payload=%2 id=%3 n=%4 swing=%5]")
                        .arg(slot).arg(slot >= 0 ? ents.blockIdAt(slot) : -1)
                        .arg(hb.blockIdAt(0)).arg(hb.countAt(0)).arg(swingSeen);
        // 端到端触地碎（创造链这枚，水平掷出 ~7 tick 触坪）：恰一次 + 载荷原样 + 命中行 = 坪行。
        bool brokeOk = false;
        for (int t = 0; t < 40 && !brokeOk; ++t) {
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
            brokeOk = breakCount == 1;
        }
        const bool shatterOk = brokeOk && breakItem == RecipeRegistry::SplashSpeedPotionId
                               && breakY == 80 && !ents.aliveAt(slot);
        ok = ok && shatterOk;
        if (!shatterOk)
            diag += QStringLiteral("[shatter n=%1 item=%2 y=%3 alive=%4]")
                        .arg(breakCount).arg(breakItem).arg(breakY)
                        .arg(slot >= 0 ? ents.aliveAt(slot) : false);
        // 链 A2：生存重掷（越过放置 CD）——消耗 1 瓶 + 新瓶存在（t256 slot-reuse：不可按 count 断言）。
        hb.setStack(0, RecipeRegistry::SplashSpeedPotionId, 5, 0);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 2 /* Survival */);
        pumpFor(320);
        pc.tick();
        pc.placeBlock();
        int slot2 = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::SplashBottle)) { slot2 = i; break; }
        const bool survivalConsumed = hb.blockIdAt(0) == RecipeRegistry::SplashSpeedPotionId
                                      && hb.countAt(0) == 4 && slot2 >= 0;
        ok = ok && survivalConsumed;
        if (!survivalConsumed)
            diag += QStringLiteral("[surv cnt=%1 slot=%2]").arg(hb.countAt(0)).arg(slot2);
        // A2 瓶清场驱动（链间隔离：在飞瓶不驱尽会抢先撞坪发碎裂信号污染链 B 的「下一碎」归因——
        //   实测 A2 水平瓶落 (29,80,24) 先于链 B 直落瓶；其镜像 applySplashPotion 距 5.02 出圈 no-op，
        //   不污染效果快照）。
        const int breakAfterA2 = breakCount;
        for (int t = 0; t < 100 && breakCount == breakAfterA2; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        // 链 B（生存近距注入）：玩家坪心（脚位 24.5,81,24.5），瓶直落 (24.5,85,24.5) → 碎于
        //   (24,80,24)，命中格中心距脚位 0.5 → d1=0.875 → 秒 = 0.875×180+0.05 = 157.55 → 快照 ceil
        //   158（首拍闩存带 ±1 容差防 dt 相位毛刺）。镜像路由已挂（链 A 创造态 no-op 不污染）。
        hb.setStack(0, RecipeRegistry::SplashSpeedPotionId, 5, 0);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 2 /* Survival */);
        pumpFor(320);
        pc.tick();
        const int b3 = ents.spawnSplashBottle(QVector3D(24.5f, 85.0f, 24.5f), QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashSpeedPotionId);
        const int breakBase = breakCount;
        for (int t = 0; t < 100 && breakCount == breakBase; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        pc.tick(); // 一帧推进 → 快照发出（速度 157.55 → ceil 158）
        const bool closeInject = b3 >= 0 && breakCount == breakBase + 1
                                 && breakX == 24 && breakY == 80 && breakZ == 24
                                 && snapSpeedSecs >= 157 && snapSpeedSecs <= 159
                                 && snapSpeedLvl == 1;
        ok = ok && closeInject;
        if (!closeInject)
            diag += QStringLiteral("[close b=%1 n=%2 cell=%3,%4,%5 secs=%6 lvl=%7]")
                        .arg(b3).arg(breakCount - breakBase).arg(breakX).arg(breakY).arg(breakZ)
                        .arg(snapSpeedSecs).arg(snapSpeedLvl);

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2071b splash bottle throw chain and radius injection column (a real creative throw"
               " spawns a SplashBottle entity carrying the splash speed potion id whose shatter on the"
               " stone platform emits exactly one splashBottleBreak signal with the carried item id, the"
               " follow-up survival throw consumes exactly one bottle, a survival player standing at the"
               " impact cell receives the speed effect through the mirrored presentation routing at the"
               " proximity-decayed caliber while a survival player four-plus blocks away receives nothing,"
               " and a zero-item throw entry is impossible because the hotbar held item gates the branch)"
            << (ok ? QString() : diag);
    });

    // ── r2071c：范围结算直调柱（静态映射 12 对 + 半径 / 衰减 / 出圈 + 创造门；NEG-2 敏感面）─────────
    //   NEG-2（摘 applySplashPotion 结算尾段）→ 恰红 = {r2071c, r2071b}：注入断言全失；静态映射
    //   （splashEffectType / splashBaseSeconds）不在摘面 → 幸存绿（腿体因注入段 FAIL）。
    //   时长口径（dist=0.5 → d1=0.875）：speed 157.55→ceil 158 / regen 39.425→40 / weakness 78.8→79 /
    //   ext poison 78.8→79（dt≤0.05 窗内恒定，快照首拍闩存）；ext speed 420.05 落整数边界 → 带宽
    //   [420..421]。中距（dist=√9.25≈3.04 → d1≈0.240）：43.19→44。出圈（dist≥4.03）→ 零注入。
    runLeg("r2071c splash resolution direct-drive column (the effect type map sends each of the"
        " twelve splash ids to its status effect and the base seconds map holds the drinkable"
        " calibers one-eighty forty-five forty-five ninety and the extended four-eighty ninety"
        " ninety two-forty, a survival player at half a block from the impact cell receives the"
        " proximity-decayed speed regeneration weakness and extended poison calibers and the"
        " extended speed band, a mid-distance impact decays to the forty-four-second band, impacts"
        " at and beyond the four-block radius inject nothing, and a creative controller receives"
        " nothing at point-blank)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 静态映射 12 对（effect 枚举 + 基础秒数；NEG-2 幸存面）。
        const bool typeMap =
            PlayerController::splashEffectType(RecipeRegistry::SplashSpeedPotionId) == int(PlayerState::EffectSpeed)
            && PlayerController::splashEffectType(RecipeRegistry::SplashStrengthPotionId) == int(PlayerState::EffectStrength)
            && PlayerController::splashEffectType(RecipeRegistry::SplashFireResistancePotionId) == int(PlayerState::EffectFireResistance)
            && PlayerController::splashEffectType(RecipeRegistry::SplashRegenerationPotionId) == int(PlayerState::EffectRegeneration)
            && PlayerController::splashEffectType(RecipeRegistry::SplashPoisonPotionId) == int(PlayerState::EffectPoison)
            && PlayerController::splashEffectType(RecipeRegistry::SplashWeaknessPotionId) == int(PlayerState::EffectWeakness)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedSpeedPotionId) == int(PlayerState::EffectSpeed)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedStrengthPotionId) == int(PlayerState::EffectStrength)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedFireResistancePotionId) == int(PlayerState::EffectFireResistance)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedRegenerationPotionId) == int(PlayerState::EffectRegeneration)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedPoisonPotionId) == int(PlayerState::EffectPoison)
            && PlayerController::splashEffectType(RecipeRegistry::SplashExtendedWeaknessPotionId) == int(PlayerState::EffectWeakness)
            && PlayerController::splashEffectType(RecipeRegistry::SpeedPotionId) == 0; // 非喷溅 id → 0
        const bool secsMap =
            PlayerController::splashBaseSeconds(RecipeRegistry::SplashSpeedPotionId) == 180.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashStrengthPotionId) == 180.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashFireResistancePotionId) == 180.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashRegenerationPotionId) == 45.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashPoisonPotionId) == 45.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashWeaknessPotionId) == 90.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedSpeedPotionId) == 480.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedStrengthPotionId) == 480.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedFireResistancePotionId) == 480.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedRegenerationPotionId) == 90.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedPoisonPotionId) == 90.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::SplashExtendedWeaknessPotionId) == 240.0f
            && PlayerController::splashBaseSeconds(RecipeRegistry::WeaknessPotionId) == 0.0f; // 非喷溅 id → 0
        ok = ok && typeMap && secsMap;
        if (!(typeMap && secsMap)) diag += QStringLiteral("[map type=%1 secs=%2]").arg(typeMap).arg(secsMap);
        // (2) 直调注入 rig：玩家坪心 (24.5,81,24.5)（脚位），命中格 (24,80,24) 中心 = 脚下 0.5。
        World w;
        initFixedBrewWorld(w);
        layBrewPlatform(w, 18, 44, 18, 30);
        PlayerController pc;
        pc.setWorld(&w);
        int spdSecs = -1, spdLvl = -1, regSecs = -1, regLvl = -1;
        int wkSecs = -1, wkLvl = -1, poSecs = -1, poLvl = -1, spd2Secs = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc, [&](const QVariantList &l) {
            for (const QVariant &v : l) {
                const QVariantMap m = v.toMap();
                const int ty = m.value(QStringLiteral("type")).toInt();
                const int sc = m.value(QStringLiteral("seconds")).toInt();
                const int lv = m.value(QStringLiteral("level")).toInt();
                // 速度快照逐次闩存（链间 clearStatusEffects 置空快照 → 速度项仅在各自注入沿出现）：
                //   第 1 次 = 近距迅捷（158），第 2 次 = 延长迅捷带（420..421）；中距第 3 次不再闩。
                if (ty == int(PlayerState::EffectSpeed)) {
                    if (spdSecs < 0) { spdSecs = sc; spdLvl = lv; }
                    else if (spd2Secs < 0) spd2Secs = sc;
                }
                if (ty == int(PlayerState::EffectRegeneration) && regSecs < 0) { regSecs = sc; regLvl = lv; }
                if (ty == int(PlayerState::EffectWeakness) && wkSecs < 0) { wkSecs = sc; wkLvl = lv; }
                if (ty == int(PlayerState::EffectPoison) && poSecs < 0) { poSecs = sc; poLvl = lv; }
            }
        });
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pc.tick(); // 基线拍（空快照基线建立）
        // 逐例驱动：applySplashPotion → tick 出快照 → 闩存 → 清效果 → tick 归零（链间隔离）。
        const auto driveCase = [&](int itemId, int cx, int cy, int cz) {
            pc.applySplashPotion(cx, cy, cz, itemId);
            pc.tick();
            pc.clearStatusEffects();
            pc.tick();
        };
        // 近距 0.5（d1=0.875）：迅捷 158 / 再生 40 / 虚弱 79 / 延长中毒 79 / 延长迅捷带 [420..421]。
        driveCase(RecipeRegistry::SplashSpeedPotionId, 24, 80, 24);
        driveCase(RecipeRegistry::SplashRegenerationPotionId, 24, 80, 24);
        driveCase(RecipeRegistry::SplashWeaknessPotionId, 24, 80, 24);
        driveCase(RecipeRegistry::SplashExtendedPoisonPotionId, 24, 80, 24);
        driveCase(RecipeRegistry::SplashExtendedSpeedPotionId, 24, 80, 24);
        // 中距（格 (27,80,24) 中心距脚位 √9.25≈3.04 → d1≈0.240 → 43.19 → ceil 44）。
        driveCase(RecipeRegistry::SplashSpeedPotionId, 27, 80, 24);
        const bool calibers = spdSecs == 158 && spdLvl == 1
                              && regSecs == 40 && regLvl == 1
                              && wkSecs == 79 && wkLvl == 1
                              && poSecs == 79 && poLvl == 1
                              && spd2Secs >= 420 && spd2Secs <= 421;
        ok = ok && calibers;
        if (!calibers)
            diag += QStringLiteral("[cal spd=%1/%2 reg=%3/%4 wk=%5/%6 po=%7/%8 spd2=%9]")
                        .arg(spdSecs).arg(spdLvl).arg(regSecs).arg(regLvl)
                        .arg(wkSecs).arg(wkLvl).arg(poSecs).arg(poLvl).arg(spd2Secs);
        // (3) 出圈负例（dist ≥ 4.03 ≥ 半径 4.0）：近圈外沿 (28,80,24)（dist=√16.25≈4.03）与远圈
        //   (30,80,24)（dist≈6.02）→ 零注入（fresh pc2 隔离，防链间闩存串扰）。
        World w2;
        initFixedBrewWorld(w2);
        layBrewPlatform(w2, 18, 44, 18, 30);
        PlayerController pc2;
        pc2.setWorld(&w2);
        bool pc2Seen = false;
        QObject::connect(&pc2, &PlayerController::activeEffectsChanged, &pc2, [&](const QVariantList &l) {
            for (const QVariant &v : l)
                if (v.toMap().value(QStringLiteral("type")).toInt() == int(PlayerState::EffectSpeed))
                    pc2Seen = true;
        });
        pc2.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pc2.tick();
        pc2.applySplashPotion(28, 80, 24, RecipeRegistry::SplashSpeedPotionId);
        pc2.tick();
        pc2.applySplashPotion(30, 80, 24, RecipeRegistry::SplashExtendedStrengthPotionId);
        pc2.tick();
        ok = ok && !pc2Seen;
        if (pc2Seen) diag += QStringLiteral("[outrange seen=1]");
        // (4) 创造门（点脸命中 → 无效果；applyStatusEffect Survival 门内置同源）。
        pc2.setMode(PlayerController::Creative);
        pc2.applySplashPotion(24, 80, 24, RecipeRegistry::SplashSpeedPotionId);
        pc2.tick();
        ok = ok && !pc2Seen;
        if (pc2Seen) diag += QStringLiteral("[creative seen=1]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2071c splash resolution direct-drive column (the effect type map sends each of the"
               " twelve splash ids to its status effect and the base seconds map holds the drinkable"
               " calibers one-eighty forty-five forty-five ninety and the extended four-eighty ninety"
               " ninety two-forty, a survival player at half a block from the impact cell receives the"
               " proximity-decayed speed regeneration weakness and extended poison calibers and the"
               " extended speed band, a mid-distance impact decays to the forty-four-second band, impacts"
               " at and beyond the four-block radius inject nothing, and a creative controller receives"
               " nothing at point-blank)"
            << (ok ? QString() : diag);
    });

    // ── r2071d：喷溅族结构钉（id 段位 + Kind 尾追加 + 常量 + 呈现三面 + 源钉族 + 相邻族零污染）──────
    //   源钉 NEG 豁免面（t1100 同款设计）：不钉 brewResult 火药门行 / splashPotionResult 小表体
    //   （NEG-1 触达面）与 applySplashPotion 本体（NEG-2 触达面），保「恰红」单腿归因；映射 / 结算
    //   只钉声明与常量面（摘体不摘声明 → 编译仍绿、钉面幸存）。
    runLeg("r2071d structure pins (the twelve splash potions sit at 0x27B through 0x286 as a tail"
        " append right after ExtendedWeaknessPotionId 0x27A with the round-two family and the instant"
        " health potion untouched, the SplashBottle kind appends after GlimmerBottle, the splash"
        " radius and tick-floor constants hold at the MC 1.0 calibers with the drinkable duration"
        " families intact, the name face palette tail and icon cases and max stack 64 on both layers"
        " hold, the full wiring source pin family holds across the brewing mapping declaration and"
        " the entity spawn and break emission and the throw entry and the qml routing and delegate"
        " and icon cases and the particle burst and the audio declaration and the sound asset and"
        " generator rows, and the neighbouring glimmer emission and extended table row and 0x27A icon"
        " and drinkable tail are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位（12 连段尾追加 + 邻接族原值）+ Kind 尾追加。
        const bool ids = RecipeRegistry::SplashSpeedPotionId == 0x27B
                         && RecipeRegistry::SplashStrengthPotionId == 0x27C
                         && RecipeRegistry::SplashFireResistancePotionId == 0x27D
                         && RecipeRegistry::SplashRegenerationPotionId == 0x27E
                         && RecipeRegistry::SplashPoisonPotionId == 0x27F
                         && RecipeRegistry::SplashWeaknessPotionId == 0x280
                         && RecipeRegistry::SplashExtendedSpeedPotionId == 0x281
                         && RecipeRegistry::SplashExtendedStrengthPotionId == 0x282
                         && RecipeRegistry::SplashExtendedFireResistancePotionId == 0x283
                         && RecipeRegistry::SplashExtendedRegenerationPotionId == 0x284
                         && RecipeRegistry::SplashExtendedPoisonPotionId == 0x285
                         && RecipeRegistry::SplashExtendedWeaknessPotionId == 0x286
                         && RecipeRegistry::ExtendedWeaknessPotionId == 0x27A
                         && RecipeRegistry::InstantHealthPotionId == 0x274
                         && RecipeRegistry::SugarId == 0x26A
                         && RecipeRegistry::GlimmerBottleId == 0x263
                         && int(EntityManager::SplashBottle) == int(EntityManager::GlimmerBottle) + 1;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 常量族（t1101 两新 + 既有饮用时长族 / 酿造常量族不动）。
        const bool consts = PlayerController::kSplashRadiusBlocks == 4.0f
                            && PlayerController::kSplashTickFloorSec == 0.05f
                            && PlayerController::kPotionDurationSec == 180.0f
                            && PlayerController::kRegenPotionDurationSec == 45.0f
                            && PlayerController::kPoisonPotionDurationSec == 45.0f
                            && PlayerController::kWeaknessDurationSec == 90.0f
                            && PlayerController::kExtPotionDurationSec == 480.0f
                            && PlayerController::kRegenExtPotionDurationSec == 90.0f
                            && PlayerController::kPoisonExtPotionDurationSec == 90.0f
                            && PlayerController::kWeaknessExtDurationSec == 240.0f
                            && PlayerController::kInstantHealthHealHp == 4
                            && BrewingStore::kPowderFuelOps == 20
                            && BrewingStore::kBrewSecs == 20.0;
        ok = ok && consts;
        if (!consts) diag += QStringLiteral("[consts]");
        // (3) 呈现面：名面十二件 + 调色板尾十二连（延长族 0x27A 后连续）+ maxStack 双层面 64 + 材料段判定。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::SplashSpeedPotionId) == QStringLiteral("喷溅迅捷药水")
                            && hb.nameForBlock(RecipeRegistry::SplashStrengthPotionId) == QStringLiteral("喷溅力量药水")
                            && hb.nameForBlock(RecipeRegistry::SplashFireResistancePotionId) == QStringLiteral("喷溅火抗药水")
                            && hb.nameForBlock(RecipeRegistry::SplashRegenerationPotionId) == QStringLiteral("喷溅再生药水")
                            && hb.nameForBlock(RecipeRegistry::SplashPoisonPotionId) == QStringLiteral("喷溅中毒药水")
                            && hb.nameForBlock(RecipeRegistry::SplashWeaknessPotionId) == QStringLiteral("喷溅虚弱药水")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedSpeedPotionId) == QStringLiteral("喷溅迅捷药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedStrengthPotionId) == QStringLiteral("喷溅力量药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedFireResistancePotionId) == QStringLiteral("喷溅火抗药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedRegenerationPotionId) == QStringLiteral("喷溅再生药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedPoisonPotionId) == QStringLiteral("喷溅中毒药水（延长）")
                            && hb.nameForBlock(RecipeRegistry::SplashExtendedWeaknessPotionId) == QStringLiteral("喷溅虚弱药水（延长）");
        const QVariantList mats = hb.creativeMaterials();
        int lastExt = -1, firstSplash = -1, lastSplash = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::ExtendedWeaknessPotionId) lastExt = i;
            if (v == RecipeRegistry::SplashSpeedPotionId) firstSplash = i;
            if (v == RecipeRegistry::SplashExtendedWeaknessPotionId) lastSplash = i;
        }
        const bool paletteOk = lastExt >= 0 && firstSplash == lastExt + 1 && lastSplash == firstSplash + 11;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::SplashSpeedPotionId) == 64
                             && BR::maxStackSize(RecipeRegistry::SplashExtendedWeaknessPotionId) == 64
                             && hb.isMaterial(RecipeRegistry::SplashRegenerationPotionId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3/%4 stack=%5]")
                        .arg(nameOk).arg(lastExt).arg(firstSplash).arg(lastSplash).arg(stackOk);
        // (4) 全链源钉族（剥注释 pinSet 锚真实语句；NEG 触达面全豁免）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString rootDir = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absolutePath();
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("splash speed row", "static constexpr int SplashSpeedPotionId                = 0x27B;", 1),
            SrcPin("splash ext weak row", "static constexpr int SplashExtendedWeaknessPotionId     = 0x286;", 1)});
        const QStringList missRc = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("assert tail", "static_assert(RecipeRegistry::SplashSpeedPotionId                == 0x27B,", 1)});
        const QStringList missBsH = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.h"), {
            SrcPin("splash decl", "static int splashPotionResult(int potionId);", 1)});
        const QStringList missEmH = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("spawn decl", "Q_INVOKABLE int spawnSplashBottle(const QVector3D &origin, const QVector3D &vel, int itemId);", 1),
            SrcPin("kind enum tail append", "AbyssPearl, Bobber, GlimmerBottle, SplashBottle", 1),
            SrcPin("break signal decl", "void splashBottleBreak(int x, int y, int z, int itemId);", 1),
            SrcPin("lifetime const", "static constexpr float kSplashBottleLifetime   = 5.0f;", 1)});
        const QStringList missEm = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("spawn def unique", "int EntityManager::spawnSplashBottle(const QVector3D &origin, const QVector3D &vel, int itemId)", 1),
            SrcPin("kind assignment", "e.kind = SplashBottle;", 1),
            SrcPin("payload store", "e.blockId = itemId;", 1),
            SrcPin("break emission", "emit splashBottleBreak(cellX, cellY, cellZ, bottleItemId);", 1)});
        const QStringList missPcH = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("apply decl", "Q_INVOKABLE void applySplashPotion(int cx, int cy, int cz, int itemId);", 1),
            SrcPin("predicate decl", "static bool isSplashPotionItem(int itemId);", 1),
            SrcPin("type map decl", "Q_INVOKABLE static int splashEffectType(int itemId);", 1),
            SrcPin("radius const", "static constexpr float kSplashRadiusBlocks  = 4.0f;", 1),
            SrcPin("tickfloor const", "static constexpr float kSplashTickFloorSec  = 0.05f;", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("throw entry gate", "isSplashPotionItem(heldItemId)", 1),
            SrcPin("throw spawn call", "spawnSplashBottle(origin, look * kPlayerSplashBottleSpeed, heldItemId)", 1),
            SrcPin("throw speed constant", "constexpr float kPlayerSplashBottleSpeed = 12.0f;", 1)});
        const QStringList missMq = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("break route", "function onSplashBottleBreak(x, y, z, itemId)", 1),
            SrcPin("apply route", "player.applySplashPotion(x, y, z, itemId)", 1),
            SrcPin("delegate kind gate", "entKind === EntityManager.SplashBottle", 1),
            SrcPin("delegate icon id", "entityManager.blockIdAt(index)", 1)});
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 27B", "case 0x27B: drawPotion(", 1),
            SrcPin("icon 280", "case 0x280: drawPotion(", 1),
            SrcPin("icon 286", "case 0x286: drawPotion(", 1)});
        const QStringList missBp = pinSet(srcRoot + QStringLiteral("/ui/BlockParticles.qml"), {
            SrcPin("splash burst fn", "function burstSplashPotion(x, y, z, effectType) {", 1)});
        const QStringList missAmH = pinSet(srcRoot + QStringLiteral("/Audio/audiomanager.h"), {
            SrcPin("break sound decl", "Q_INVOKABLE void playSplashBreak();", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("name row", "return QStringLiteral(\"喷溅迅捷药水\")", 1),
            SrcPin("palette row", "int(RecipeRegistry::SplashExtendedWeaknessPotionId)", 1)});
        const bool d5 = missRh.isEmpty() && missRc.isEmpty() && missBsH.isEmpty() && missEmH.isEmpty()
            && missEm.isEmpty() && missPcH.isEmpty() && missPc.isEmpty() && missMq.isEmpty()
            && missMi.isEmpty() && missBp.isEmpty() && missAmH.isEmpty() && missHb.isEmpty();
        ok = ok && d5;
        if (!d5) {
            const QStringList allMiss = QStringList()
                << missRh << missRc << missBsH << missEmH << missEm << missPcH << missPc
                << missMq << missMi << missBp << missAmH << missHb;
            diag += QStringLiteral("[d5 %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (5) 声音资产在案（qrc 资源面：CMakeLists 行 + build_sounds 生成器函数）。
        const bool cmakeRows = pinSet(rootDir + QStringLiteral("/CMakeLists.txt"), {
            SrcPin("splash asset", "sounds/splash_break.wav", 1)}).isEmpty();
        const bool genFns = pinSet(rootDir + QStringLiteral("/tools/build_sounds.py"), {
            SrcPin("splash gen", "def gen_splash_break():", 1)}).isEmpty();
        ok = ok && cmakeRows && genFns;
        if (!(cmakeRows && genFns))
            diag += QStringLiteral("[assets cmake=%1 gen=%2]").arg(cmakeRows).arg(genFns);
        // (6) 相邻族零污染（蕴辉瓶发射 / 延长表行 / 0x27A 图标 / 饮面尾行原样）。
        const QStringList missNb = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("glimmer emission untouched", "emit glimmerBottleBreak(cellX, cellY, cellZ, totalXp);", 1)});
        const bool extRow = pinSet(srcRoot + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("extended weak row untouched", "return RecipeRegistry::ExtendedWeaknessPotionId;", 1)}).isEmpty();
        const bool icon27A = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 27A untouched", "case 0x27A: drawPotion(", 1)}).isEmpty();
        const bool drinkTail = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drinkable tail untouched", "|| itemId == RecipeRegistry::InstantHealthPotionId;", 1)}).isEmpty();
        const bool nb = missNb.isEmpty() && extRow && icon27A && drinkTail;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb %1 ext=%2 icon=%3 drink=%4]")
                             .arg(missNb.isEmpty()).arg(extRow).arg(icon27A).arg(drinkTail);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2071d structure pins (the twelve splash potions sit at 0x27B through 0x286 as a tail"
               " append right after ExtendedWeaknessPotionId 0x27A with the round-two family and the instant"
               " health potion untouched, the SplashBottle kind appends after GlimmerBottle, the splash"
               " radius and tick-floor constants hold at the MC 1.0 calibers with the drinkable duration"
               " families intact, the name face palette tail and icon cases and max stack 64 on both layers"
               " hold, the full wiring source pin family holds across the brewing mapping declaration and"
               " the entity spawn and break emission and the throw entry and the qml routing and delegate"
               " and icon cases and the particle burst and the audio declaration and the sound asset and"
               " generator rows, and the neighbouring glimmer emission and extended table row and 0x27A icon"
               " and drinkable tail are untouched)"
            << (ok ? QString() : diag);
    });

    // ── t1102 残项小批合集二（+4 腿 r2072a-d，置尾追加；矩阵 824→828）────────────────────────────
    // 任务契约：件一 水瓶直酿行（1.0 基准直酿面两行：水瓶 + 发酵蛛眼 → 虚弱——喷溅虚弱链前置 /
    //   水瓶 + 糖 → 凡庸 MundanePotionId 0x287 可饮无效果同粗制口径；兔子脚越纪元 / 稠厚原料缺口 /
    //   闪烁西瓜·红石→凡庸(延长) 纪元形态存疑不硬造，裁定留痕 recipe.h 0x287 注）+ 件二 压力板 kMc
    //   纪元标注勘误 + 数据修正（weighted 板 = MC 1.5 引入，1.0 的 71/72 = iron door / wooden plate，
    //   数据 71→147 / 72→148 双重冲突消解，零运行期消费者零破坏）+ 件四 食物完成 burp（MC 1.0 食毕
    //   随机 burp 通用反馈音，t1100 登记简化①补全：foodBurped 新信号与 potionDrunk 恒互斥）。
    // NEG 面与豁免设计（恰红归因先于腿文）：
    //   NEG-1 = 摘直酿行（brewingstore.cpp brewResult 水瓶分支两行新对一并摘——纯字面量行、无函数
    //     声明牵连 → 编译仍绿）→ 恰红 = {r2072a}（直酿两对静态断言 + 两真驱转换断言全失；凡庸饮面
    //     / 负例族断言不在摘面 → 部分幸存但腿体 FAIL）；b / c / d（源钉豁免面——不钉两行新对）均
    //     不受影响。
    //   NEG-2 = 摘食物 burp 面（playercontroller.cpp finishEating 的 foodBurped 触发行摘除——.h 信号
    //     声明 / Main.qml 路由幸存 → 编译仍绿）→ 恰红 = {r2072b}（食物完成 foodBurped 恰一断言失；
    //     饮面 potionDrunk 链不在摘面 → 部分幸存但腿体 FAIL）；a / c / d（源钉豁免面——不钉触发行）
    //     均不受影响。

    // ── r2072a：水瓶直酿转换行为柱（NEG-1 敏感面）─────────────────────────────────────────────────
    //   NEG-1（摘 brewResult 水瓶分支两行新对）→ 恰红 = {r2072a}：直酿两对静态断言 + 两真驱断言全失；
    //   凡庸饮面 / 负例族断言幸存但腿体判 FAIL。既有三小表零冲突负例族同柱断言（红石 / 火药门行「水
    //   → 0」既录负例 + 未交付行负例）。
    runLeg("r2072a water-bottle direct brewing conversion behavior column (the brew table maps a"
        " water bottle plus fermented spider eye to the weakness potion and plus sugar to the"
        " mundane potion while the ash-wort row still answers the awkward potion, the modifier"
        " gate rows keep answering zero on water bottles with gunpowder and redstone and the"
        " undelivered glistering-melon row stays zero, the direct-brew ingredients answer zero on"
        " extended and splash bottles, a placed brewing stand driven through a full twenty-second"
        " cycle converts water bottles to the mundane potion and to the weakness potion on the"
        " ember powder fuel meter, and the mundane potion is a drinkable item outside the splash"
        " predicate that drinks through the real chain with one empty bottle returned and no"
        " status effect raised)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 直酿两行 + 既录灰烬疣行不动。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::WaterBottleId) == RecipeRegistry::WeaknessPotionId
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::WaterBottleId) == RecipeRegistry::MundanePotionId
            && BrewingStore::brewResult(RecipeRegistry::AshWartId, RecipeRegistry::WaterBottleId) == RecipeRegistry::AwkwardPotionId;
        // (2) 负例族（与既有三小表零冲突 + 未交付行恒零）。
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::ExtendedWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::ExtendedWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::SplashWeaknessPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::SplashExtendedWeaknessPotionId) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (3) 真驱转换双链：水 + 糖 → 凡庸 / 水 + 发酵蛛眼 → 虚弱（各 20s 一轮 + 耗 1 原料 + 计量 -1）。
        const auto driveBrew = [&](int ingId, int expectId) {
            World w;
            initFixedBrewWorld(w);
            layBrewPlatform(w, 20, 30, 20, 30);
            PlayerController pc;
            pc.setWorld(&w);
            BrewingStore store;
            pc.setBrewingStore(&store);
            const int bxp = 24, byp = 81, bzp = 24;
            w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
            store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::WaterBottleId, 1);
            store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, ingId, 1);
            store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
            const int steps = int(20.6 / 0.05);
            for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
            return store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == expectId
                   && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                   && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        };
        const bool drivenMundane = driveBrew(RecipeRegistry::SugarId, RecipeRegistry::MundanePotionId);
        const bool drivenWeak = driveBrew(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::WeaknessPotionId);
        ok = ok && drivenMundane && drivenWeak;
        if (!(drivenMundane && drivenWeak))
            diag += QStringLiteral("[driven mundane=%1 weak=%2]").arg(drivenMundane).arg(drivenWeak);
        // (4) 凡庸饮面口径：谓词面（可饮 + 非喷溅）+ 真链饮毕（耗 1 + 返 1 空瓶 + potionDrunk 恰一 +
        //     零效果快照——无效果载体同粗制口径）。
        const bool preds = PlayerController::isDrinkableItem(RecipeRegistry::MundanePotionId)
                           && !PlayerController::isSplashPotionItem(RecipeRegistry::MundanePotionId);
        ok = ok && preds;
        if (!preds) diag += QStringLiteral("[preds]");
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
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线（t1030 rig 纪律）
        int drunkCount = 0, snapCount = 0;
        QObject::connect(&pc, &PlayerController::potionDrunk, &pc, [&](int itemId) {
            if (itemId == RecipeRegistry::MundanePotionId) ++drunkCount;
        });
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { if (!l.isEmpty()) ++snapCount; });
        hb.setStack(0, RecipeRegistry::MundanePotionId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        QElapsedTimer pumpT; pumpT.start();
        while (pumpT.elapsed() < 320)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        pc.tick();
        pc.beginEating();
        const bool eatingStarted = pc.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        pc.tick();
        int bottles = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottles += hb.countAt(i);
        const bool drinkOk = eatingStarted && drunkCount == 1 && snapCount == 0
                             && hb.blockIdAt(0) == RecipeRegistry::MundanePotionId && hb.countAt(0) == 1
                             && bottles == 1;
        ok = ok && drinkOk;
        if (!drinkOk)
            diag += QStringLiteral("[drink eat=%1 drunk=%2 snap=%3 n0=%4 b=%5]")
                        .arg(eatingStarted).arg(drunkCount).arg(snapCount).arg(hb.countAt(0)).arg(bottles);

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2072a water-bottle direct brewing conversion behavior column (the brew table maps a"
               " water bottle plus fermented spider eye to the weakness potion and plus sugar to the"
               " mundane potion while the ash-wort row still answers the awkward potion, the modifier"
               " gate rows keep answering zero on water bottles with gunpowder and redstone and the"
               " undelivered glistering-melon row stays zero, the direct-brew ingredients answer zero on"
               " extended and splash bottles, a placed brewing stand driven through a full twenty-second"
               " cycle converts water bottles to the mundane potion and to the weakness potion on the"
               " ember powder fuel meter, and the mundane potion is a drinkable item outside the splash"
               " predicate that drinks through the real chain with one empty bottle returned and no"
               " status effect raised)"
            << (ok ? QString() : diag);
    });

    // ── r2072b：食物完成 burp 行为柱（真链信号计数钉；NEG-2 敏感面）───────────────────────────────
    //   NEG-2（摘 finishEating 的 foodBurped 触发行）→ 恰红 = {r2072b}：食物完成 foodBurped 恰一断言
    //   全失；饮面 potionDrunk 链（链 B）不在摘面 → 部分幸存但腿体判 FAIL。计数钉非空转：foodBurped /
    //   potionDrunk 都经真链完成沿驱动（r2070b 计数钉同门）。
    runLeg("r2072b food completion burp behavior column (a real survival player finishing a bread"
        " eat through the full eat chain emits exactly one foodBurped signal and zero potionDrunk"
        " signals while consuming one bread, and a real survival drink of a water bottle emits"
        " exactly one potionDrunk and zero foodBurped with one empty bottle returned, the two"
        " completion edges staying mutually exclusive on both directions)",
        [&]() {
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
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线（t1030 rig 纪律）
        int foodBurpCount = 0, drunkCount = 0;
        QObject::connect(&pc, &PlayerController::foodBurped, &pc, [&](int itemId) {
            if (itemId == RecipeRegistry::BreadId) ++foodBurpCount;
        });
        QObject::connect(&pc, &PlayerController::potionDrunk, &pc, [&](int) { ++drunkCount; });
        const auto pumpMs = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 链 A：面包（食物完成沿）——真链食满 → foodBurped 恰一 + potionDrunk 零 + 耗 1。
        hb.setStack(0, RecipeRegistry::BreadId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pumpMs(320); // 越过放置 CD
        pc.tick();
        pc.beginEating();
        const bool eatingStarted = pc.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        pc.tick();
        const bool foodOk = eatingStarted && foodBurpCount == 1 && drunkCount == 0
                            && hb.blockIdAt(0) == RecipeRegistry::BreadId && hb.countAt(0) == 1;
        ok = ok && foodOk;
        if (!foodOk)
            diag += QStringLiteral("[food eat=%1 burp=%2 drunk=%3 n0=%4]")
                        .arg(eatingStarted).arg(foodBurpCount).arg(drunkCount).arg(hb.countAt(0));
        // 链 B：水瓶（饮用完成沿）——真链喝满 → potionDrunk 恰一 + foodBurped 仍零（计数不回退断言：
        //   链 A 后 foodBurpCount 恒 1，链 B 零增量）+ 返 1 空瓶。互斥双向：食面无饮沿 / 饮面无食沿。
        hb.setStack(0, RecipeRegistry::WaterBottleId, 2, 0);
        pumpMs(320); // 越过 finishEating 冷却 / eat CD
        pc.tick();
        pc.beginEating();
        const bool drinkingStarted = pc.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pc.tick();
        }
        pc.endEating();
        pc.tick();
        int bottles = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottles += hb.countAt(i);
        const bool drinkOk = drinkingStarted && drunkCount == 1 && foodBurpCount == 1 && bottles == 1
                             && hb.blockIdAt(0) == RecipeRegistry::WaterBottleId && hb.countAt(0) == 1;
        ok = ok && drinkOk;
        if (!drinkOk)
            diag += QStringLiteral("[drink eat=%1 burp=%2 drunk=%3 b=%4 n0=%5]")
                        .arg(drinkingStarted).arg(foodBurpCount).arg(drunkCount).arg(bottles).arg(hb.countAt(0));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2072b food completion burp behavior column (a real survival player finishing a bread"
               " eat through the full eat chain emits exactly one foodBurped signal and zero potionDrunk"
               " signals while consuming one bread, and a real survival drink of a water bottle emits"
               " exactly one potionDrunk and zero foodBurped with one empty bottle returned, the two"
               " completion edges staying mutually exclusive on both directions)"
            << (ok ? QString() : diag);
    });

    // ── r2072c：压力板 kMc 数据钉 + 资源包消费面回归柱（件二）────────────────────────────────────
    //   两 NEG 均不触达（行为钉走 mcBlockId 直读 + 资源包行源钉；标注锚 = 裸读块注释——pinSet 剥
    //   注释面不可锚，r2067d mcIdRow 裸读先例）。
    runLeg("r2072c pressure-plate migration mapping and resource-pack face column (the migration"
        " table sends the iron plate to one-forty-seven and the gold plate to one-forty-eight as"
        " the true weighted-plate ids with the stone plate at seventy and the wooden plate at"
        " seventy-two and the iron door at seventy-one so the historical double collisions are"
        " resolved, the brewing-stand neighbour mapping stays at one-seventeen, and the resource"
        " pack block-tile rows for the three plates hold their weighted naming untouched)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) kMc 数据钉：147/148 真值 + 石板 / 木板 / 铁门 1.0 真值不动 + 酿造台邻族不扰 +
        //     双重冲突消解断言。
        const bool mcIds = BR::mcBlockId(quint8(BR::IronPressurePlate)) == 147
                           && BR::mcBlockId(quint8(BR::GoldPressurePlate)) == 148
                           && BR::mcBlockId(quint8(BR::StonePressurePlate)) == 70
                           && BR::mcBlockId(quint8(BR::WoodPressurePlate)) == 72
                           && BR::mcBlockId(quint8(BR::IronDoor)) == 71
                           && BR::mcBlockId(quint8(BR::BrewingStand)) == 117
                           && BR::mcBlockId(quint8(BR::IronPressurePlate)) != BR::mcBlockId(quint8(BR::IronDoor))
                           && BR::mcBlockId(quint8(BR::GoldPressurePlate)) != BR::mcBlockId(quint8(BR::WoodPressurePlate));
        ok = ok && mcIds;
        if (!mcIds) diag += QStringLiteral("[mcIds]");
        // (2) 纪元标注锚（块注释裸读——剥注释面不可锚）。
        const QString brSrc = rawSource(srcRootForPins() + QStringLiteral("/Core/blockregistry.cpp"));
        const bool anchors = brSrc.contains(QStringLiteral("纪元三元组留痕"))
                             && brSrc.contains(QStringLiteral("/* iron_pressure_plate     */ 147,"))
                             && brSrc.contains(QStringLiteral("/* gold_pressure_plate     */ 148,"));
        ok = ok && anchors;
        if (!anchors) diag += QStringLiteral("[anchors]");
        // (3) 资源包消费面零破坏（t627 三 tile 行原样——weighted 命名族 = 机制对齐同源留痕）。
        const QStringList missRp = pinSet(srcRootForPins() + QStringLiteral("/Core/resourcepackmanager.cpp"), {
            SrcPin("stone tile row", "{154, QStringLiteral(\"stone_pressure_plate.png\")}", 1),
            SrcPin("heavy tile row", "{155, QStringLiteral(\"heavy_weighted_pressure_plate.png\")}", 1),
            SrcPin("light tile row", "{156, QStringLiteral(\"light_weighted_pressure_plate.png\")}", 1)});
        ok = ok && missRp.isEmpty();
        if (!missRp.isEmpty()) diag += QStringLiteral("[rp %1]").arg(missRp.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2072c pressure-plate migration mapping and resource-pack face column (the migration"
               " table sends the iron plate to one-forty-seven and the gold plate to one-forty-eight as"
               " the true weighted-plate ids with the stone plate at seventy and the wooden plate at"
               " seventy-two and the iron door at seventy-one so the historical double collisions are"
               " resolved, the brewing-stand neighbour mapping stays at one-seventeen, and the resource"
               " pack block-tile rows for the three plates hold their weighted naming untouched)"
            << (ok ? QString() : diag);
    });

    // ── r2072d：凡庸 id + 食物 burp 结构钉族（NEG 触达面全豁免）──────────────────────────────────
    //   源钉 NEG 豁免面（t1100/t1101 同款设计）：不钉 brewResult 水瓶分支两行新对（NEG-1 触达面）与
    //   finishEating foodBurped 触发行（NEG-2 触达面），保「恰红」单腿归因；只钉 id 段位 / 呈现面 /
    //   信号声明 / QML 路由 / 邻族零污染。
    runLeg("r2072d structure pins (the mundane potion sits at 0x287 as a tail append right after"
        " SplashExtendedWeaknessPotionId 0x286 with the splash family untouched, the name face and"
        " palette tail append and icon case and max stack 64 on both layers hold, the drinkable"
        " face holds the mundane row with the historical tail row verbatim and the pinned ash-wort"
        " ternary untouched, the food-burp wiring source pin family holds across the signal"
        " declaration and the qml route with the finishEating emit face unpinned for NEG"
        " attribution, and the neighbouring splash-weakness row and potion-drunk emit and"
        " drinkable tail are untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) id 段位（尾追加 + 邻接族原值）。
        const bool ids = RecipeRegistry::MundanePotionId == 0x287
                         && RecipeRegistry::SplashExtendedWeaknessPotionId == 0x286
                         && RecipeRegistry::SplashSpeedPotionId == 0x27B
                         && RecipeRegistry::ExtendedWeaknessPotionId == 0x27A
                         && RecipeRegistry::SugarId == 0x26A
                         && RecipeRegistry::AwkwardPotionId == 0x267;
        ok = ok && ids;
        if (!ids) diag += QStringLiteral("[ids]");
        // (2) 呈现面：名面一件 + 调色板尾追加（喷溅 12 连之后恰一位）+ maxStack 双层面 64 + 材料段判定。
        Hotbar hb;
        const bool nameOk = hb.nameForBlock(RecipeRegistry::MundanePotionId) == QStringLiteral("凡庸药水");
        const QVariantList mats = hb.creativeMaterials();
        int lastSplash = -1, mundaneIdx = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::SplashExtendedWeaknessPotionId) lastSplash = i;
            if (v == RecipeRegistry::MundanePotionId) mundaneIdx = i;
        }
        const bool paletteOk = lastSplash >= 0 && mundaneIdx == lastSplash + 1;
        const bool stackOk = hb.maxStackSize(RecipeRegistry::MundanePotionId) == 64
                             && BR::maxStackSize(RecipeRegistry::MundanePotionId) == 64
                             && hb.isMaterial(RecipeRegistry::MundanePotionId);
        ok = ok && nameOk && paletteOk && stackOk;
        if (!(nameOk && paletteOk && stackOk))
            diag += QStringLiteral("[face name=%1 palette=%2/%3 stack=%4]")
                        .arg(nameOk).arg(lastSplash).arg(mundaneIdx).arg(stackOk);
        // (3) 全链源钉族（剥注释 pinSet 锚真实语句；NEG 触达面全豁免：水瓶分支两行新对 / finishEating
        //     触发行不钉）。
        const QStringList missRh = pinSet(srcRootForPins() + QStringLiteral("/Game/recipe.h"), {
            SrcPin("mundane row", "static constexpr int MundanePotionId = 0x287;", 1)});
        const QStringList missRc = pinSet(srcRootForPins() + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("assert tail", "static_assert(RecipeRegistry::MundanePotionId", 1)});
        const QStringList missPcH = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("burp signal decl", "void foodBurped(int itemId);", 1)});
        const QStringList missPc = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drinkable mundane row", "|| itemId == RecipeRegistry::MundanePotionId", 1)});
        const QStringList missMq = pinSet(srcRootForPins() + QStringLiteral("/ui/Main.qml"), {
            SrcPin("burp route", "function onFoodBurped(itemId) {", 1)});
        const QStringList missMi = pinSet(srcRootForPins() + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 287", "case 0x287: drawPotion(", 1)});
        const QStringList missHb = pinSet(srcRootForPins() + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("name row", "return QStringLiteral(\"凡庸药水\")", 1),
            SrcPin("palette row", "int(RecipeRegistry::MundanePotionId)", 1)});
        const bool d5 = missRh.isEmpty() && missRc.isEmpty() && missPcH.isEmpty() && missPc.isEmpty()
            && missMq.isEmpty() && missMi.isEmpty() && missHb.isEmpty();
        ok = ok && d5;
        if (!d5) {
            const QStringList allMiss = QStringList()
                << missRh << missRc << missPcH << missPc << missMq << missMi << missHb;
            diag += QStringLiteral("[d5 %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (4) 邻族零污染（既录尾行 / 三元组行 / 喷溅行 / potionDrunk 发行原样——r2069d/r2071d/r2067d/
        //     r2070d 同锚互证）。
        const bool drinkTail = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drinkable tail untouched", "|| itemId == RecipeRegistry::InstantHealthPotionId;", 1)}).isEmpty();
        const bool ternary = pinSet(srcRootForPins() + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("awkward ternary untouched", "return ingredientId == RecipeRegistry::AshWartId ? RecipeRegistry::AwkwardPotionId : 0;", 1)}).isEmpty();
        const bool splashRow = pinSet(srcRootForPins() + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("splash ext weak row untouched", "return RecipeRegistry::SplashExtendedWeaknessPotionId;", 1)}).isEmpty();
        const bool drunkEmit = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("drunk emit untouched", "emit potionDrunk(eatenId);", 1)}).isEmpty();
        const bool nb = drinkTail && ternary && splashRow && drunkEmit;
        ok = ok && nb;
        if (!nb) diag += QStringLiteral("[nb drink=%1 ternary=%2 splash=%3 drunk=%4]")
                             .arg(drinkTail).arg(ternary).arg(splashRow).arg(drunkEmit);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2072d structure pins (the mundane potion sits at 0x287 as a tail append right after"
               " SplashExtendedWeaknessPotionId 0x286 with the splash family untouched, the name face and"
               " palette tail append and icon case and max stack 64 on both layers hold, the drinkable"
               " face holds the mundane row with the historical tail row verbatim and the pinned ash-wort"
               " ternary untouched, the food-burp wiring source pin family holds across the signal"
               " declaration and the qml route with the finishEating emit face unpinned for NEG"
               " attribution, and the neighbouring splash-weakness row and potion-drunk emit and"
               " drinkable tail are untouched)"
            << (ok ? QString() : diag);
    });

    // ── t1103 闪烁西瓜生存整链四腿（r2073a-d；NEG 双变异见 b/d 头注阴注；钉面 NEG 双面全豁免）──────
    //   阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
    //   matrix_r2073_neg{1,2}_{red,restore}.log）：
    //   NEG-1 摘瓜生长结果门（world.cpp tickCropGrowth 3b 成熟茎结果分支整段）→ **恰红 = {r2073b,
    //     r2073d}**（b 结果面 + d 链的瓜源断链——两腿吃瓜源；a/c 零世界零触达）。生长 0→7 序列面
    //     （不触结果分支）与掉落面幸存但腿红；r2072 五族不误伤（r2072a 直酿腿不走生长）。
    //   NEG-2 摘环形合成行（recipe.cpp glistering_melon 行）→ **恰红 = {r2073c, r2073d}**（c 环形
    //     命中面 + d 链的酿造原料断链；a 的金粒双向行/瓜种池行不在摘面，b 不触合成）。
    //   相邻族零污染：r2072 五族（凡庸 0x287 段位 / 直酿两行 / 双 burp 互斥 / 压力板映射 / 结构钉）
    //   全部零触碰——本组钉面只追加、摘面全豁免；凡庸调色板连续性钉（凡庸 == 喷溅尾 + 1）随尾追加
    //   自然保持。

    // ── r2073a：金粒双向合成 + 瓜种战利品行 + 瓜片转换/存储行为柱（纯表 + 池 roll；零世界）────────────
    //   NEG 面：本腿零 NEG 触达——摘结果分支（NEG-1）/ 摘环形行（NEG-2）均不伤本腿（金粒双向行 /
    //   瓜片转换行 / 瓜块存储行 / 池行行为级直调，不走生长与环形链）。
    runLeg("r2073a gold-nugget bidirectional crafting plus melon-seed mineshaft loot column (a gold"
        " ingot breaks down to exactly nine nuggets shapeless in both grids and nine nuggets rebuild"
        " the ingot in a full three-by-three, a melon slice converts one-to-one into a melon seed and"
        " nine slices build the melon block with wrong-ingredient pairs answering null with no"
        " consumption, and the mineshaft chest pool carries the melon-seed row with weight ten and a"
        " two-to-four count band that the thousand-seed deterministic sweep hits reproducibly inside"
        " the weight band while the dungeon pool stays untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 金锭 → 9 金粒（无序单原料：2×2 角放 + 3×3 心放双格命中；行名唯一身份 + 产率 9）。
        const int g2[9] = { RecipeRegistry::GoldIngotId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r2 = RecipeRegistry::match(g2, 2);
        const bool ingotDown = r2 && r2->outputId == RecipeRegistry::GoldNuggetsId
            && r2->outputCount == 9 && r2->shapeless
            && QLatin1String(r2->name) == QLatin1String("gold_nuggets_x9");
        const int g3[9] = { 0, 0, 0, 0, RecipeRegistry::GoldIngotId, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r3 = RecipeRegistry::match(g3, 3);
        const bool ingotDown3 = r3 && r3->outputId == RecipeRegistry::GoldNuggetsId
            && r3->outputCount == 9;
        ok = ok && ingotDown && ingotDown3;
        if (!(ingotDown && ingotDown3)) diag += QStringLiteral("[nugdown %1/%2]").arg(ingotDown).arg(ingotDown3);
        // (2) 9 金粒 → 1 金锭（3×3 满铺有序行；产率 1）。
        const int gn = RecipeRegistry::GoldNuggetsId;
        const int gI[9] = { gn, gn, gn, gn, gn, gn, gn, gn, gn };
        const RecipeRegistry::Recipe *rI = RecipeRegistry::match(gI, 3);
        const bool ingotUp = rI && rI->outputId == RecipeRegistry::GoldIngotId
            && rI->outputCount == 1 && !rI->shapeless
            && QLatin1String(rI->name) == QLatin1String("gold_ingot_from_nuggets");
        ok = ok && ingotUp;
        if (!ingotUp) diag += QStringLiteral("[nugup %1]").arg(rI ? rI->outputId : -1);
        // (3) 瓜片 → 1 瓜种（1:1 双格命中）+ 9 瓜片 → 瓜块（3×3 满铺；产物 = 方块段 Melon）。
        const int gS[9] = { RecipeRegistry::MelonSliceId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rS = RecipeRegistry::match(gS, 2);
        const bool sliceSeed = rS && rS->outputId == RecipeRegistry::MelonSeedsId
            && rS->outputCount == 1 && rS->shapeless
            && QLatin1String(rS->name) == QLatin1String("melon_seeds_from_slice");
        const int gSliceC[9] = { 0, 0, 0, 0, RecipeRegistry::MelonSliceId, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rS3 = RecipeRegistry::match(gSliceC, 3); // 3×3 心放同命中（单原料无序位置无关）
        const bool sliceSeed3 = rS3 && rS3->outputId == RecipeRegistry::MelonSeedsId && rS3->outputCount == 1;
        const int gs = RecipeRegistry::MelonSliceId;
        const int gM[9] = { gs, gs, gs, gs, gs, gs, gs, gs, gs };
        const RecipeRegistry::Recipe *rM = RecipeRegistry::match(gM, 3);
        const bool sliceBlock = rM && rM->outputId == int(BR::Melon) && rM->outputCount == 1
            && QLatin1String(rM->name) == QLatin1String("melon_from_slices");
        ok = ok && sliceSeed && sliceSeed3 && sliceBlock;
        if (!(sliceSeed && sliceSeed3 && sliceBlock))
            diag += QStringLiteral("[slice s=%1 s3=%2 blk=%3]").arg(sliceSeed).arg(sliceSeed3).arg(sliceBlock);
        // (4) 错料负例（match()==nullptr = 合成 UI 无效应不消耗权威面；r2062b 同门）：单粒不成行 /
        //     8 粒 + 糖心不成行 / 金锭+瓜片不成行 / 瓜片+金粒对不成行。
        const auto expectNoMatch = [&](const int gg[9], int w, const char *why) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(gg, w);
            if (r) diag += QStringLiteral("[%1 matched %2]").arg(QLatin1String(why)).arg(r->outputId);
            return r == nullptr;
        };
        const int n1[9] = { RecipeRegistry::GoldNuggetsId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const int nS[9] = { gn, gn, gn, gn, RecipeRegistry::SugarId, gn, gn, gn, gn };
        const int nI[9] = { 0, 0, 0, 0, RecipeRegistry::GoldIngotId, 0, RecipeRegistry::MelonSliceId, 0, 0 };
        const int nIS[9] = { RecipeRegistry::MelonSliceId, RecipeRegistry::GoldNuggetsId, 0, 0, 0, 0, 0, 0, 0 };
        const bool negs = expectNoMatch(n1, 2, "single-nugget")
            && expectNoMatch(nS, 3, "8-nugget-sugar-center")
            && expectNoMatch(nI, 3, "ingot-slice")
            && expectNoMatch(nIS, 2, "slice-nugget-pair");
        ok = ok && negs;
        if (!negs) diag += QStringLiteral("[negs]");
        // (5) 瓜种矿井池行逐字段（10 / 2..4）+ 千 seed 确定性扫描命中 + 复现 + 数量带 + 千 seed 宽频带
        //     （权重 10/117 ≈ 8.55%/roll × 8000 roll ≈ 684 期望；带 [450,950] 防塌零 / 爆涨两向漂移，
        //     r2062a 同门算式）+ 地牢池邻族零污染（行数 10 / 权重和 153 / 无瓜种行——r2062d 锚不动）。
        bool rowOk = false, seedHit = false, reproOk = true, rangeOk = true;
        int seedStacks = 0;
        for (const auto &e : LootTable::mineshaftChestPool())
            if (e.itemId == RecipeRegistry::MelonSeedsId)
                rowOk = e.weight == 10 && e.minCount == 2 && e.maxCount == 4;
        for (quint32 s = 0; s < 1000; ++s) {
            const auto stacks = LootTable::roll(LootTable::mineshaftChestPool(), 8, s);
            bool hit = false;
            for (const auto &st : stacks) {
                if (st.itemId == RecipeRegistry::MelonSeedsId) {
                    hit = true;
                    ++seedStacks;
                    if (st.count < 2 || st.count > 4) rangeOk = false;
                }
            }
            if (hit && !seedHit) {
                const auto again = LootTable::roll(LootTable::mineshaftChestPool(), 8, s);
                for (const auto &st : again)
                    if (st.itemId == RecipeRegistry::MelonSeedsId) { seedHit = true; break; }
            }
        }
        const bool bandOk = seedStacks >= 450 && seedStacks <= 950;
        int dRows = 0, dWeight = 0;
        bool dHasSeed = false;
        for (const auto &e : LootTable::dungeonChestPool()) {
            ++dRows;
            dWeight += e.weight;
            dHasSeed |= e.itemId == RecipeRegistry::MelonSeedsId;
        }
        const bool dungeonClean = dRows == 10 && dWeight == 153 && !dHasSeed;
        const bool lootOk = rowOk && seedHit && reproOk && rangeOk && bandOk && dungeonClean;
        ok = ok && lootOk;
        if (!lootOk)
            diag += QStringLiteral("[loot row=%1 hit=%2 rng=%3 stacks=%4 dungeon=%5]")
                        .arg(rowOk).arg(seedHit).arg(rangeOk).arg(seedStacks).arg(dungeonClean);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2073a gold-nugget bidirectional crafting plus melon-seed mineshaft loot column (a gold"
               " ingot breaks down to exactly nine nuggets shapeless in both grids and nine nuggets rebuild"
               " the ingot in a full three-by-three, a melon slice converts one-to-one into a melon seed and"
               " nine slices build the melon block with wrong-ingredient pairs answering null with no"
               " consumption, and the mineshaft chest pool carries the melon-seed row with weight ten and a"
               " two-to-four count band that the thousand-seed deterministic sweep hits reproducibly inside"
               " the weight band while the dungeon pool stays untouched)"
            << (ok ? QString() : diag);
    });

    // ── r2073b：瓜茎生长门 + 成熟结果行为柱（真 tick 泵 + 骰子镜像 + 真链破茎；NEG-1 敏感面）──────────
    //   NEG-1（摘 world.cpp tickCropGrowth 成熟茎结果分支）→ 恰红 = {r2073b, r2073d}：本腿结果面
    //   断言全失（生长 0→7 骰子镜像序列 / 双门阴性 / 破茎掉种面不触摘面仍绿但腿红）。本腿不破瓜块、
    //   不触合成 → NEG-2 不误伤。
    runLeg("r2073b melon-stem growth gate plus mature fruiting behavior column (the planted stem"
        " climbs the shared eight-age ladder through the farmland plus skylight plus deterministic"
        " scatter gates with the actual advance windows matching the mirrored dice exactly and"
        " monotonic single-step stages, a mature stem on stone ground and a stem ringed by solid"
        " blocks both refuse to fruit past their reachable dice-hit windows while the free-slot stem"
        " drops exactly one melon block onto the deterministic hash-picked neighbour cell with valid"
        " ground keeping the stem mature for repeated fruiting, and breaking the stem through the"
        " real mining chain yields exactly one melon seed)", [&]() {
        bool ok = true;
        QString diag;
        World wG;
        initFixedBrewWorld(wG);
        layBrewPlatform(wG, 12, 38, 20, 28);
        // 三块耕地垫：A 主株(24,81,24) 完整生长+结果（5×5 垫 → 4 邻空位 + 落地面全耕地）/
        //   B 石地对照(16,81,24)（单块耕地 → 4 邻落地面 = 石坪 → 落地面门拒）/ C 围石对照(32,81,24)
        //   （4 邻围石 → 槽位门拒）。
        for (int x = 22; x <= 26; ++x)
            for (int z = 22; z <= 26; ++z)
                wG.setBlock(x, 80, z, BR::Farmland, 0);
        wG.setBlock(16, 80, 24, BR::Farmland, 0);
        wG.setBlock(32, 80, 24, BR::Farmland, 0);
        wG.setBlock(24, 81, 24, BR::MelonStem, 0); // A：种下 state 0（种植映射行行为级走 r2073d 链 + d 结构钉）
        wG.setBlock(16, 81, 24, BR::MelonStem, 7); // B：直置成熟茎
        wG.setBlock(32, 81, 24, BR::MelonStem, 7); // C：直置成熟茎
        wG.setBlock(31, 81, 24, BR::Stone, 0);
        wG.setBlock(33, 81, 24, BR::Stone, 0);
        wG.setBlock(32, 81, 23, BR::Stone, 0);
        wG.setBlock(32, 81, 25, BR::Stone, 0);
        // 光照前置（开露天光 ≥ kCropMinLight=9；t1026a 同门预检）。
        const bool lightOk = wG.skyLightAt(24, 81, 24) >= 9 && wG.skyLightAt(16, 81, 24) >= 9;
        // 骰子镜像（world.cpp tickCropGrowth 同式：干耕地 1× 倍率 growPct=6、无雨；窗口序号自 0）。
        const auto diceHits = [&](int k, int stage, int cx, int cy, int cz) -> bool {
            const int mixedSeed = int(quint32(wG.seed()) ^ (quint32(k) * 0x9E3779B9u));
            return int(wG.hashVoxel(mixedSeed, cx, cy * 7 + stage, cz) & 0xFFFFu) % 100 < 6;
        };
        // (1) A 株生长序列镜像（阶段 0..6；3000 窗守卫 = 病态 seed 兜底，t1026a 同式）。
        QVector<int> simAdv;
        {
            int stage = 0;
            for (int k = 0; k < 3000 && stage < 7; ++k)
                if (diceHits(k, stage, 24, 81, 24)) { ++stage; simAdv.push_back(k); }
            if (stage < 7) simAdv.clear();
        }
        const bool simOk = !simAdv.isEmpty();
        // (2) 结果窗镜像：A 熟窗之后首个骰子命中窗（stage=7 恒项）+ 期望槽位（四向扫描哈希定起始向，
        //     +X/-X/+Z/-Z 固定序环绕；首空格 + 落地面耕地 → 唯一期望格——与 world.cpp 3b 同式）。
        int fruitWin = -1, expectX = -1, expectZ = -1;
        if (simOk) {
            const int k7 = simAdv.last();
            for (int k = k7 + 1; k < k7 + 3000 && fruitWin < 0; ++k)
                if (diceHits(k, 7, 24, 81, 24)) fruitWin = k;
            if (fruitWin > 0) {
                const int mixedSeed = int(quint32(wG.seed()) ^ (quint32(fruitWin) * 0x9E3779B9u));
                const quint32 h = wG.hashVoxel(mixedSeed, 24, 81 * 7 + 7, 24);
                const int start = int((h >> 16) & 3u);
                static constexpr int kDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int d = 0; d < 4; ++d) {
                    const int nx = 24 + kDir[(start + d) & 3][0];
                    const int nz = 24 + kDir[(start + d) & 3][1];
                    if (wG.blockAt(nx, 81, nz) == BR::Air && wG.blockAt(nx, 80, nz) == BR::Farmland) {
                        expectX = nx;
                        expectZ = nz;
                        break;
                    }
                }
            }
        }
        const int pumpEnd = (fruitWin > 0) ? fruitWin : (simOk ? simAdv.last() : 0);
        // (3) 真泵（25 tick = 1 窗；逐窗采样：升阶段序列 ≡ 镜像 + 单调）。
        bool monoOk = true;
        int prevStage = 0, advIdx = 0;
        for (int k = 0; k <= pumpEnd; ++k) {
            for (int c = 0; c < 25; ++c) wG.tickCropGrowth();
            const int st = wG.stateAt(24, 81, 24);
            if (st - prevStage > 1 || st - prevStage < 0 || st > 7) monoOk = false;
            if (advIdx < simAdv.size() && k == simAdv.at(advIdx) && st - prevStage == 1) ++advIdx;
            prevStage = st;
        }
        const bool seqOk = simOk && advIdx == simAdv.size() && prevStage == BR::WheatCropStageMax;
        // (4) 结果面：A 环恰 1 瓜 @期望格（NEG-1 摘结果分支 → 环空红）+ 茎保持成熟（复果口径）。
        const auto ringMelonCount = [&](int cx, int cz, int *mx, int *mz) {
            int n = 0;
            static constexpr int kR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto &o : kR)
                if (wG.blockAt(cx + o[0], 81, cz + o[1]) == BR::Melon) {
                    ++n;
                    if (mx) { *mx = cx + o[0]; *mz = cz + o[1]; }
                }
            return n;
        };
        int mx = -1, mz = -1;
        const int aMelons = ringMelonCount(24, 24, &mx, &mz);
        const bool fruitOk = fruitWin > 0 && expectX > 0 && aMelons == 1
                             && mx == expectX && mz == expectZ;
        const bool matureKept = wG.blockAt(24, 81, 24) == BR::MelonStem
                                && wG.stateAt(24, 81, 24) == BR::WheatCropStageMax;
        // (5) 真链破茎：持空手生存瞄 A 茎（站位避瓜——沿首条「环格非瓜」轴向退 2 格站，防射线先命中瓜）。
        PlayerController pcG;
        pcG.setWorld(&wG);
        Hotbar hbG;
        pcG.setHotbar(&hbG);
        QQuickWindow winG;
        pcG.setParentItem(winG.contentItem());
        pcG.grab();
        pcG.setSelectedBlock(int(BR::Air));
        QVector<int> dropIdsG, dropCntsG;
        const QMetaObject::Connection dropConnG = QObject::connect(
            &pcG, &PlayerController::spawnItem, &pcG,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIdsG.push_back(id);
                dropCntsG.push_back(count);
            });
        int standX = 24, standZ = 24;
        static constexpr int kDirB[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (const auto &o : kDirB) {
            const int rx = 24 + o[0], rz = 24 + o[1];
            if (!(rx == mx && rz == mz)) { standX = 24 + 2 * o[0]; standZ = 24 + 2 * o[1]; break; }
        }
        const auto aimG = [&](float fx, float fz, float ax, float ay, float az) {
            const float ex = fx, ey = 81.0f + 1.62f, ez = fz;
            const float dx = ax - ex, dy = ay - ey, dz = az - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcG.release();
            pcG.grab();
            pcG.loadSavedState(fx, 81.0f, fz, yaw, pitch, 2 /* Survival */);
            pcG.tick();
        };
        aimG(float(standX) + 0.5f, float(standZ) + 0.5f, 24.5f, 81.5f, 24.5f);
        pcG.beginMining();
        for (int i = 0; i < 6000 && wG.blockAt(24, 81, 24) != BR::Air; ++i) {
            QElapsedTimer dtw;
            dtw.start();
            while (dtw.elapsed() < 17)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pcG.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pcG.endMining();
        const bool stemGone = wG.blockAt(24, 81, 24) == BR::Air;
        const bool stemDrop = stemGone && dropIdsG.size() == 1
                              && dropIdsG.at(0) == RecipeRegistry::MelonSeedsId
                              && dropCntsG.at(0) == 1;
        // (6) B/C 可达域守卫泵（B/C 自有骰子流首个命中窗——摘落地面门 / 槽位门时该窗必出瓜）：
        //     破茎后再泵（茎已去 → A 环不再变化），泵毕双对照环恒 0 瓜。
        int bHit = -1, cHit = -1;
        for (int k = 0; k <= 3000 && (bHit < 0 || cHit < 0); ++k) {
            if (bHit < 0 && diceHits(k, 7, 16, 81, 24)) bHit = k;
            if (cHit < 0 && diceHits(k, 7, 32, 81, 24)) cHit = k;
        }
        int gateEnd = pumpEnd;
        if (bHit > gateEnd) gateEnd = bHit;
        if (cHit > gateEnd) gateEnd = cHit;
        for (int k = pumpEnd + 1; k <= gateEnd; ++k)
            for (int c = 0; c < 25; ++c) wG.tickCropGrowth();
        const bool gatesOk = ringMelonCount(16, 24, nullptr, nullptr) == 0
                             && ringMelonCount(32, 24, nullptr, nullptr) == 0;
        ok = ok && lightOk && monoOk && seqOk && fruitOk && matureKept && stemDrop && gatesOk;
        if (!ok)
            diag += QStringLiteral("[b light=%1 mono=%2 seq=%3 fruit=%4@(x%5,z%6,n=%7) kept=%8 drop=%9 gone=%10 gates=%11 win=%12]")
                        .arg(lightOk).arg(monoOk).arg(seqOk).arg(fruitOk)
                        .arg(mx).arg(mz).arg(aMelons).arg(matureKept).arg(stemDrop).arg(stemGone)
                        .arg(gatesOk).arg(fruitWin);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2073b melon-stem growth gate plus mature fruiting behavior column (the planted stem"
               " climbs the shared eight-age ladder through the farmland plus skylight plus deterministic"
               " scatter gates with the actual advance windows matching the mirrored dice exactly and"
               " monotonic single-step stages, a mature stem on stone ground and a stem ringed by solid"
               " blocks both refuse to fruit past their reachable dice-hit windows while the free-slot"
               " stem drops exactly one melon block onto the deterministic hash-picked neighbour cell"
               " with valid ground keeping the stem mature for repeated fruiting, and breaking the stem"
               " through the real mining chain yields exactly one melon seed)"
            << (ok ? QString() : diag);
    });

    // ── r2073c：环形合成 + 瓜片食物面行为柱（纯表 + 真链食环；NEG-2 敏感面）──────────────────────────
    //   NEG-2（摘 recipe.cpp glistering_melon 环形行）→ 恰红 = {r2073c, r2073d}：本腿环形命中面 +
    //   3×3 专属面全失（食物面 / 酿造行在不在摘面仍绿但腿红）。真链食环零世界语义 → NEG-1 不误伤。
    runLeg("r2073c glistering melon ring crafting plus slice food column (eight nuggets ringed"
        " around one center slice in a three-by-three grid crafts exactly one glistering melon under"
        " the unique row identity while the two-by-two grid and wrong-center pairs answer null with"
        " no consumption, the brewing table keeps answering the instant-health potion for the"
        " glistering melon on an awkward bottle and zero on water or foreign ingredients, and a real"
        " survival melon-slice eat emits exactly one foodBurped and zero potionDrunk while consuming"
        " one slice)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 环形命中：8 金粒环 + 中心瓜片 → 1 闪烁西瓜（3×3 有序；行名唯一身份；产率 1）。
        const int gn = RecipeRegistry::GoldNuggetsId;
        const int gR[9] = { gn, gn, gn, gn, RecipeRegistry::MelonSliceId, gn, gn, gn, gn };
        const RecipeRegistry::Recipe *rR = RecipeRegistry::match(gR, 3);
        const bool ringHit = rR && rR->outputId == RecipeRegistry::GlisteringMelonId
            && rR->outputCount == 1
            && QLatin1String(rR->name) == QLatin1String("glistering_melon");
        ok = ok && ringHit;
        if (!ringHit) diag += QStringLiteral("[ring %1]").arg(rR ? rR->outputId : -1);
        // (2) 3×3 专属 + 错料负例（2×2 容不下 8 环 → 四格不成行；糖心 / 金锭心 / 九粒满铺 → 各归各行或零）。
        const int n2[9] = { gn, gn, gn, RecipeRegistry::MelonSliceId, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r2c = RecipeRegistry::match(n2, 2);
        const bool notIn2x2 = r2c == nullptr;
        const int nSugar[9] = { gn, gn, gn, gn, RecipeRegistry::SugarId, gn, gn, gn, gn };
        const int nIngotC[9] = { gn, gn, gn, gn, RecipeRegistry::GoldIngotId, gn, gn, gn, gn };
        const bool wrongCenters = RecipeRegistry::match(nSugar, 3) == nullptr
                                  && RecipeRegistry::match(nIngotC, 3) == nullptr;
        ok = ok && notIn2x2 && wrongCenters;
        if (!(notIn2x2 && wrongCenters)) diag += QStringLiteral("[ringneg]");
        // (3) 酿造尾面（行在——t1099 既录；NEG-2 摘合成行不伤本面，c 的红纯由环形面）：
        //     闪烁西瓜 + 粗制 → 瞬间治疗；水瓶 / 瓜片 / 金粒底物 → 0（瓜链原料非酿造底物/非效果原料）。
        const bool brewTail = BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId,
                                                       RecipeRegistry::AwkwardPotionId)
                                  == RecipeRegistry::InstantHealthPotionId
            && BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId,
                                        RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::MelonSliceId,
                                        RecipeRegistry::AwkwardPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GoldNuggetsId,
                                        RecipeRegistry::AwkwardPotionId) == 0;
        ok = ok && brewTail;
        if (!brewTail) diag += QStringLiteral("[brew]");
        // (4) 食物表面：瓜片 +2（MC 1.0 原值）+ 瓜种 / 金粒非食物非饮品（foodHungerAmount==0 /
        //     isDrinkableItem==false——饮面回归三连）。
        const bool foodFace = PlayerController::foodHungerAmount(RecipeRegistry::MelonSliceId) == 2
            && PlayerController::foodHungerAmount(RecipeRegistry::MelonSeedsId) == 0
            && PlayerController::foodHungerAmount(RecipeRegistry::GoldNuggetsId) == 0
            && !PlayerController::isDrinkableItem(RecipeRegistry::MelonSliceId)
            && !PlayerController::isDrinkableItem(RecipeRegistry::MelonSeedsId)
            && !PlayerController::isDrinkableItem(RecipeRegistry::GoldNuggetsId);
        ok = ok && foodFace;
        if (!foodFace) diag += QStringLiteral("[foodface]");
        // (5) 真链食环：生存玩家长按吃瓜片 → foodBurped(瓜片) 恰一 + potionDrunk 零 + 耗 1（r2072b 链 A 同门；
        //     双完成沿互斥的食物向回归——t1102 burp 面随瓜片扩到新食物零改动零扰动）。
        World wE;
        initFixedBrewWorld(wE);
        layBrewPlatform(wE, 18, 44, 18, 30);
        EntityManager entsE;
        Hotbar hbE;
        PlayerController pcE;
        pcE.setWorld(&wE);
        pcE.setEntityManager(&entsE);
        pcE.setHotbar(&hbE);
        QQuickWindow probeE;
        pcE.setParentItem(probeE.contentItem());
        pcE.grab();
        pcE.setSelectedBlock(int(BR::Air));
        int burpSlice = 0, drunkE = 0;
        QObject::connect(&pcE, &PlayerController::foodBurped, &pcE, [&](int itemId) {
            if (itemId == RecipeRegistry::MelonSliceId) ++burpSlice;
        });
        QObject::connect(&pcE, &PlayerController::potionDrunk, &pcE, [&](int) { ++drunkE; });
        hbE.setStack(0, RecipeRegistry::MelonSliceId, 2, 0);
        pcE.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        QElapsedTimer pumpT;
        pumpT.start();
        while (pumpT.elapsed() < 320)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        pcE.tick();
        pcE.beginEating();
        const bool eatingStarted = pcE.eating();
        for (int i = 0; i < 44; ++i) {
            QThread::msleep(60);
            pcE.tick();
        }
        pcE.endEating();
        pcE.tick();
        const bool eatOk = eatingStarted && burpSlice == 1 && drunkE == 0
            && hbE.blockIdAt(0) == RecipeRegistry::MelonSliceId && hbE.countAt(0) == 1;
        ok = ok && eatOk;
        if (!eatOk)
            diag += QStringLiteral("[eat start=%1 burp=%2 drunk=%3 n0=%4]")
                        .arg(eatingStarted).arg(burpSlice).arg(drunkE).arg(hbE.countAt(0));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2073c glistering melon ring crafting plus slice food column (eight nuggets ringed"
               " around one center slice in a three-by-three grid crafts exactly one glistering melon"
               " under the unique row identity while the two-by-two grid and wrong-center pairs answer"
               " null with no consumption, the brewing table keeps answering the instant-health potion"
               " for the glistering melon on an awkward bottle and zero on water or foreign"
               " ingredients, and a real survival melon-slice eat emits exactly one foodBurped and"
               " zero potionDrunk while consuming one slice)"
            << (ok ? QString() : diag);
    });

    // ── r2073d：端到端生存链 + 结构钉族（NEG-1 敏感[瓜源] + NEG-2 敏感[环形]；钉面双面全豁免）────────
    //   链：瓜种 → 茎（生长 0→7 骰子镜像泵）→ 成熟结果（NEG-1 面）→ 真链破瓜块掉 3-7 瓣（NEG-2 邻面：
    //   摘瓣分支回落通用 1 瓣 → 区间断言红；本腿摘面 = 环形行 → 环合成为零红）→ 瓣 → 种回流 + 金锭 →
    //   9 金粒 → 环形合闪烁西瓜（NEG-2 面）→ brewResult 粗制 + 闪烁西瓜 → 瞬间治疗（生存链闭合钉）。
    //   结构钉：id 段位族 + 方块/映射/图集段位 + def 行逐字段 + 配方行/池行/接线源钉 + 呈现三面 +
    //   相邻族零污染。NEG 双摘面（world.cpp 结果分支体 / playercontroller.cpp 掉瓣分支体）零钉——
    //   行为柱归因 b/c/d 三腿。
    runLeg("r2073d survival end-to-end chain plus structure pins (a planted melon seed grows"
        " through the mirrored dice ladder to a mature stem that fruits one melon block on the"
        " farm pad, breaking that melon through the real mining chain drops three to seven slices,"
        " one slice converts back into a seed while one gold ingot breaks into nine nuggets, the"
        " eight-nugget ring around one slice crafts the glistering melon and the brew table answers"
        " the instant-health potion closing the survival chain, and the structure pin family holds"
        " across id segments and block mapping and atlas extents and def rows and craft and loot"
        " and wiring source pins and the presentation faces with the neighbouring mundane family"
        " untouched)", [&]() {
        bool ok = true;
        QString diag;
        // ── 行为面：端到端链 ──
        World wD;
        initFixedBrewWorld(wD);
        layBrewPlatform(wD, 12, 38, 20, 28);
        for (int x = 22; x <= 26; ++x)
            for (int z = 22; z <= 26; ++z)
                wD.setBlock(x, 80, z, BR::Farmland, 0);
        const int sx = 24, sy = 81, sz = 24;
        wD.setBlock(sx, sy, sz, BR::MelonStem, 0); // 瓜种 → 茎（种植映射行为级行由 r2073d 链直种 + 源钉）
        const auto diceD = [&](int k, int stage) -> bool {
            const int mixedSeed = int(quint32(wD.seed()) ^ (quint32(k) * 0x9E3779B9u));
            return int(wD.hashVoxel(mixedSeed, sx, sy * 7 + stage, sz) & 0xFFFFu) % 100 < 6;
        };
        int advWin = -1;
        {
            int stage = 0;
            for (int k = 0; k < 3000 && stage < 7; ++k)
                if (diceD(k, stage)) { ++stage; advWin = k; }
        }
        int fruitWinD = -1;
        if (advWin >= 0)
            for (int k = advWin + 1; k < advWin + 3000 && fruitWinD < 0; ++k)
                if (diceD(k, 7)) fruitWinD = k;
        const bool pumpOk = advWin >= 0 && fruitWinD > 0;
        for (int k = 0; k <= fruitWinD && pumpOk; ++k)
            for (int c = 0; c < 25; ++c) wD.tickCropGrowth();
        auto ringMelonD = [&](int *mx, int *mz) {
            static constexpr int kR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto &o : kR)
                if (wD.blockAt(sx + o[0], sy, sz + o[1]) == BR::Melon) {
                    if (mx) { *mx = sx + o[0]; *mz = sz + o[1]; }
                    return true;
                }
            return false;
        };
        int mxD = -1, mzD = -1;
        const bool melonGrown = pumpOk && ringMelonD(&mxD, &mzD);
        ok = ok && melonGrown;
        if (!melonGrown) diag += QStringLiteral("[grow adv=%1 fruit=%2]").arg(advWin).arg(fruitWinD);
        // 真链破瓜块（生存空手；NEG-2 邻面：摘 3-7 瓣分支 → 回落通用 1 瓣 → 区间红）。
        PlayerController pcD;
        pcD.setWorld(&wD);
        Hotbar hbD;
        pcD.setHotbar(&hbD);
        QQuickWindow winD;
        pcD.setParentItem(winD.contentItem());
        pcD.grab();
        pcD.setSelectedBlock(int(BR::Air));
        int sliceDrops = 0, sliceTotal = 0;
        const QMetaObject::Connection sliceConn = QObject::connect(
            &pcD, &PlayerController::spawnItem, &pcD,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                if (id == RecipeRegistry::MelonSliceId) { ++sliceDrops; sliceTotal += count; }
            });
        const auto aimD = [&](float fx, float fz, float ax, float ay, float az) {
            const float ex = fx, ey = float(sy) + 1.62f, ez = fz;
            const float dx = ax - ex, dy = ay - ey, dz = az - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcD.release();
            pcD.grab();
            pcD.loadSavedState(fx, float(sy), fz, yaw, pitch, 2 /* Survival */);
            pcD.tick();
        };
        aimD(float(sx) + 0.5f, float(sz) + 0.5f, float(mxD) + 0.5f, float(sy) + 0.5f, float(mzD) + 0.5f);
        pcD.beginMining();
        for (int i = 0; i < 6000 && wD.blockAt(mxD, sy, mzD) != BR::Air; ++i) {
            QElapsedTimer dtw;
            dtw.start();
            while (dtw.elapsed() < 17)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pcD.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pcD.endMining();
        const bool melonBroken = wD.blockAt(mxD, sy, mzD) == BR::Air;
        const bool sliceRange = sliceDrops == 1 && sliceTotal >= 3 && sliceTotal <= 7; // MC 1.0 3-7 口径
        ok = ok && melonBroken && sliceRange;
        if (!(melonBroken && sliceRange))
            diag += QStringLiteral("[melon broken=%1 drops=%2 total=%3]")
                        .arg(melonBroken).arg(sliceDrops).arg(sliceTotal);
        // 链下游（纯表）：瓣 → 种回流 + 金锭 → 9 金粒 → 环形合闪烁西瓜 → 酿造瞬间治疗（闭合钉）。
        const int gnD = RecipeRegistry::GoldNuggetsId;
        const int gSeedD[9] = { RecipeRegistry::MelonSliceId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const int gIngotD[9] = { RecipeRegistry::GoldIngotId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const int gRingD[9] = { gnD, gnD, gnD, gnD, RecipeRegistry::MelonSliceId, gnD, gnD, gnD, gnD };
        const bool chainOk = melonBroken
            && RecipeRegistry::match(gSeedD, 2) != nullptr
            && RecipeRegistry::match(gIngotD, 2) != nullptr
            && RecipeRegistry::match(gRingD, 3) != nullptr
            && BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId,
                                        RecipeRegistry::AwkwardPotionId)
                == RecipeRegistry::InstantHealthPotionId;
        ok = ok && chainOk;
        if (!chainOk) diag += QStringLiteral("[chain]");
        // ── 结构钉面（NEG 双摘面全豁免；剥注释 pinSet + 裸读锚）──
        // (S1) id / 段位 / 映射 / 图集（运行期 + 枚举钉）。
        Hotbar hbS;
        const bool idsOk = RecipeRegistry::GoldNuggetsId == 0x288
            && RecipeRegistry::MelonSeedsId == 0x289
            && RecipeRegistry::MelonSliceId == 0x28A
            && RecipeRegistry::MundanePotionId == 0x287 // 邻族零污染（t1102 段尾原值）
            && int(BR::Melon) == 149 && int(BR::MelonStem) == 150
            && int(BR::Count) == 160 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1105 曾 151→154
            && int(BR::AtlasTileCount) == 209 // t1112 lawful 前移：207→209（蛋糕族 tile 207..208 追加）；t1105 lawful 前移：201→207（南瓜族 tile 201..205 + 炼药锅 206 追加；t1103 曾 195→201）
            && BR::mcBlockId(quint8(BR::Melon)) == 103
            && BR::mcBlockId(quint8(BR::MelonStem)) == 105
            && hbS.maxStackSize(RecipeRegistry::GoldNuggetsId) == 64
            && hbS.maxStackSize(RecipeRegistry::MelonSeedsId) == 64
            && hbS.maxStackSize(RecipeRegistry::MelonSliceId) == 64
            && BR::maxStackSize(RecipeRegistry::MelonSliceId) == 64
            && hbS.isMaterial(RecipeRegistry::MelonSliceId);
        ok = ok && idsOk;
        if (!idsOk) diag += QStringLiteral("[ids]");
        // (S2) def 行逐字段（瓜块 / 瓜茎）。
        const auto &md = BR::def(BR::Melon);
        const auto &sd = BR::def(BR::MelonStem);
        const bool defOk = md.topTile == 195 && md.sideTile == 196 && md.solid
            && md.shape == int(BR::ShapeFull) && md.hardness == 1.0f
            && md.dropId == RecipeRegistry::MelonSliceId && md.dropCount == 1
            && !sd.solid && sd.shape == int(BR::ShapeNone) && sd.hardness == 0.0f
            && sd.topTile == 197 && sd.dropId == RecipeRegistry::MelonSeedsId;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (S3) 呈现三面（名面 / 调色板尾追加四连[喷溅尾→凡庸→金粒→瓜种→瓜片] / 图标 case）。
        const bool nameOkS = hbS.nameForBlock(RecipeRegistry::GoldNuggetsId) == QStringLiteral("金粒")
            && hbS.nameForBlock(RecipeRegistry::MelonSeedsId) == QStringLiteral("瓜种")
            && hbS.nameForBlock(RecipeRegistry::MelonSliceId) == QStringLiteral("瓜片");
        const QVariantList matsS = hbS.creativeMaterials();
        int lastSplashS = -1, mundS = -1, nugS = -1, seedS = -1, sliceS = -1;
        for (int i = 0; i < matsS.size(); ++i) {
            const int v = matsS.at(i).toInt();
            if (v == RecipeRegistry::SplashExtendedWeaknessPotionId) lastSplashS = i;
            if (v == RecipeRegistry::MundanePotionId) mundS = i;
            if (v == RecipeRegistry::GoldNuggetsId) nugS = i;
            if (v == RecipeRegistry::MelonSeedsId) seedS = i;
            if (v == RecipeRegistry::MelonSliceId) sliceS = i;
        }
        const bool palOkS = lastSplashS >= 0 && mundS == lastSplashS + 1 // r2072d 凡庸连续性钉保持
            && nugS == mundS + 1 && seedS == nugS + 1 && sliceS == seedS + 1;
        bool melonInBlocksS = false;
        const QVariantList blocksS = hbS.creativeBlocks();
        for (const QVariant &b : blocksS)
            melonInBlocksS |= b.toInt() == int(BR::Melon);
        ok = ok && nameOkS && palOkS && melonInBlocksS;
        if (!(nameOkS && palOkS && melonInBlocksS))
            diag += QStringLiteral("[face name=%1 pal=%2/%3/%4/%5 blk=%6]")
                        .arg(nameOkS).arg(lastSplashS).arg(mundS).arg(nugS).arg(sliceS)
                        .arg(melonInBlocksS);
        // (S4) 源钉族（NEG 双摘面零钉；world.h 茎并入锚为注释 → 裸读口径）。
        const QStringList missRh = pinSet(srcRootForPins() + QStringLiteral("/Game/recipe.h"), {
            SrcPin("nugget row", "static constexpr int GoldNuggetsId  = 0x288;", 1),
            SrcPin("seeds row", "static constexpr int MelonSeedsId   = 0x289;", 1),
            SrcPin("slice row", "static constexpr int MelonSliceId   = 0x28A;", 1)});
        const QStringList missRc = pinSet(srcRootForPins() + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("ring row body", "RecipeRegistry::MelonSliceId,  RecipeRegistry::GoldNuggetsId,", 1),
            SrcPin("assert nugget", "static_assert(RecipeRegistry::GoldNuggetsId", 1)});
        const QStringList missBs = pinSet(srcRootForPins() + QStringLiteral("/Game/brewingstore.cpp"), {
            SrcPin("glistering brew row", "return RecipeRegistry::InstantHealthPotionId;", 1)});
        const QStringList missLt = pinSet(srcRootForPins() + QStringLiteral("/Game/loottable.cpp"), {
            SrcPin("melon seed pool row", "{ RecipeRegistry::MelonSeedsId,   10, 2, 4 },", 1)});
        const QStringList missPc = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("slice food row", "RecipeRegistry::MelonSliceId)     return 2;", 1),
            SrcPin("planting row", "{ RecipeRegistry::MelonSeedsId, BlockRegistry::MelonStem }", 1)});
        const QStringList missHb = pinSet(srcRootForPins() + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("name nugget", "return QStringLiteral(\"金粒\")", 1),
            SrcPin("name seeds", "return QStringLiteral(\"瓜种\")", 1),
            SrcPin("name slice", "return QStringLiteral(\"瓜片\")", 1),
            SrcPin("palette nugget", "int(RecipeRegistry::GoldNuggetsId),", 1),
            SrcPin("blocks melon row", "int(BlockRegistry::Melon),", 1), // t1105 lawful 前移：瓜块行让位表尾（炼药锅行续尾追加），存在性钉不钉「最末」位
            SrcPin("blocks cauldron row", "int(BlockRegistry::Cauldron),", 1), // t1111 lawful 前移：炼药锅行让位表尾（砂岩楼梯行续尾追加 t1111），改「逗号行」存在性钉——t1105 瓜块行同门
            SrcPin("stem variant case", "case BlockRegistry::MelonStem:", 1)});
        const QStringList missMi = pinSet(srcRootForPins() + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("icon 288", "case 0x288: drawGoldNugget(); break", 1),
            SrcPin("icon 289", "case 0x289: drawMelonSeeds(); break", 1),
            SrcPin("icon 28A", "case 0x28A: drawMelonSlice(); break", 1)});
        const QStringList missBrH = pinSet(srcRootForPins() + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("count sentinel", "Count           = 160", 1), // t1112 lawful 前移：钉文 157→160 随源行前移（栅栏门/玻璃板/蛋糕尾部追加）；沿革 t1111 lawful 前移：钉文 154→157 随源行前移；t1105 曾 151→154
            SrcPin("atlas sentinel", "AtlasTileCount = 209", 1), // t1112 lawful 前移：207→209（蛋糕族 tile 207..208 追加）；t1105 lawful 前移：201→207（南瓜族 tile 201..205 + 炼药锅 206 追加）
            SrcPin("melon enum", "Melon            = 149", 1),
            SrcPin("stem enum", "MelonStem        = 150", 1)});
        const QStringList missBrC = pinSet(srcRootForPins() + QStringLiteral("/Core/blockregistry.cpp"), {
            SrcPin("melon def row", "{int(BlockRegistry::Melon),", 1),
            SrcPin("stem def row", "{int(BlockRegistry::MelonStem),", 1),
            SrcPin("stem cross row", "if (blockId == MelonStem) return true;", 1),
            SrcPin("stem tile case", "case MelonStem:", 1)});
        const QStringList missWc = pinSet(srcRootForPins() + QStringLiteral("/World/world.cpp"), {
            SrcPin("growth index row", "id == BR::MelonStem", 1),
            SrcPin("snapshot row", "|| b == BlockRegistry::MelonStem", 1),
            SrcPin("bonemeal row", "id == BlockRegistry::MelonStem", 1)});
        QFile whS(srcRootForPins() + QStringLiteral("/World/world.h"));
        const QString whTxt = whS.open(QIODevice::ReadOnly) ? QString::fromUtf8(whS.readAll()) : QString();
        // 中文锚必走 QStringLiteral（UTF-16 语义比较；QLatin1String 按 Latin-1 逐字节解码中文必失配）。
        const bool whAnchor = whTxt.contains(QStringLiteral("瓜茎（MelonStem）并入本 tick"));
        const bool pinsOk = missRh.isEmpty() && missRc.isEmpty() && missBs.isEmpty() && missLt.isEmpty()
            && missPc.isEmpty() && missHb.isEmpty() && missMi.isEmpty() && missBrH.isEmpty()
            && missBrC.isEmpty() && missWc.isEmpty() && whAnchor;
        ok = ok && pinsOk;
        if (!pinsOk) {
            const QStringList allMiss = QStringList()
                << missRh << missRc << missBs << missLt << missPc << missHb << missMi
                << missBrH << missBrC << missWc;
            diag += QStringLiteral("[pins %1%2]")
                        .arg(allMiss.join(QLatin1Char(',')))
                        .arg(whAnchor ? QString() : QStringLiteral("[world.h anchor]"));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2073d survival end-to-end chain plus structure pins (a planted melon seed grows"
               " through the mirrored dice ladder to a mature stem that fruits one melon block on the"
               " farm pad, breaking that melon through the real mining chain drops three to seven"
               " slices, one slice converts back into a seed while one gold ingot breaks into nine"
               " nuggets, the eight-nugget ring around one slice crafts the glistering melon and the"
               " brew table answers the instant-health potion closing the survival chain, and the"
               " structure pin family holds across id segments and block mapping and atlas extents"
               " and def rows and craft and loot and wiring source pins and the presentation faces"
               " with the neighbouring mundane family untouched)"
            << (ok ? QString() : diag);
    });

    // ── t1105 炼药锅 + 南瓜农作链四腿（r2075a-d；置尾追加；NEG 双变异见各腿头注阴注；钉面 NEG 双面全豁免）──
    //   在场性排查结论（派工前逐一全仓 grep 实读留痕）：① 炼药锅 t474-t961 大件族**零在场**
    //   （blockregistry 方块段 / 交互面 / 几何 / 图标全缺）→ 真缺形态交付；② 南瓜方块 Pumpkin=100
    //   既存（t482 刻面 + t638 朝向 + t510 造物头位），但南瓜种子物品 / 南瓜茎作物 / 南瓜灯方块 /
    //   南瓜→种子合成 / 灯合成 / 野生 patch worldgen 全缺 → 真缺形态交付（方块段三件尾部追加）。
    //   接棒吸收：前工程师树面 blockregistry.{h,cpp} 骨架（三 def 行 + kMc 三行 + Count 154 +
    //   CauldronStateLevelMask + AtlasTileCount 207）核实完整可用、逐字吸收；两处口径接棒修正
    //   （沿革注在案）：南瓜灯朝向写（同南瓜 t638——吸收稿「无 facing metadata」注与本工程 t638
    //   自身裁定矛盾）+ 矿井箱南瓜种子行不交付（1.0 实读无此行，吸收稿裁定与实读一致，采纳留痕）。
    //   NEG 面与豁免设计（恰红归因先于腿文；流敏感选择器教训 t1104 前置——摘面不改任何抽签 /
    //   消费次数）：
    //   NEG-1 = 炼药锅瓶取水位结算塌缩（playercontroller.cpp 炼药锅分支瓶取水面 setBlock 写
    //     quint8(level - 1) → quint8(level)：取瓶照常 / 予水瓶照常，唯水位不降——恒同值写无副作用形）
    //     → **恰红 = {r2075a}**（a 的逐次水位阶梯断言 3→2→1→0 全失；a 的桶灌满 / 空锅拒瓶 /
    //     创造不耗面不在摘面仍绿；b/c 零炼药锅触达；d 结构钉不钉该表达式 = 豁免）。
    //   NEG-2 = 南瓜茎结果面分流塌缩（world.cpp tickCropGrowth 3b 果 id 三元
    //     `(f.id == PumpkinStem) ? Pumpkin : Melon` → 恒 Melon：散布 roll / 四向扫描 / 消费次数全不动）
    //     → **恰红 = {r2075b}**（b 的环内结果方块 = Pumpkin 断言失——南瓜茎格将落西瓜；b 的生长
    //     骰子镜像 / 双门阴性 / 破茎掉种面不在摘面仍绿；r2073b/d 西瓜茎面不受影响[瓜茎恒 Melon
    //     语义不变]；a/c 零世界触达；d 不钉该三元 = 豁免）。
    //
    // 腿面与阴性面设计：
    //   r2075a 炼药锅水位交互行为柱（真 pc rig placeBlock 链：生存装水桶灌满 3 级 + 桶→空桶消耗 +
    //     瓶取 3 次水位逐次 3→2→1→0 + 每取予 1 水瓶 + 空锅拒瓶无效应不消耗不挥臂 + 创造灌满不耗桶 +
    //     空手无效应 + 真链挖掘破坏掉锅本体 + 水随方块消失）。
    //   r2075b 南瓜茎生长门 + 成熟结果行为柱（r2073b MelonStem 模板镜像第二实例：骰子镜像生长序列 +
    //     单调 + 石地 / 围石双门阴性 + 哈希定向落南瓜于期望格 + 茎保持成熟 + 真链破茎掉 1 南瓜种子；
    //     NEG-2 敏感面）。
    //   r2075c 南瓜生存入口 + 南瓜灯合成 + 光照面行为柱（南瓜 1:4 种子双向门[错料负例] + 南瓜+火把
    //     → 灯无序双格命中 + 灯产物 id/产率/名 + 光源面[真世界放置 → 邻格方块光 14] + 南瓜本体恒 0 光 +
    //     物品面[名 / maxStack 64 / 调色板在列]）。
    //   r2075d 结构钉族（0x28B 段尾 + 方块段 151..153/Count 154 + kMc 104/91/118 三行唯一性 +
    //     图集 207 + CauldronStateLevelMask + 三 def 行逐字段 + stateTileOverride 两茎同式 +
    //     isCrossBillboard/materialGroup/lightEmission 谓词面 + 两配方行 + worldgen 声明/双调用点 +
    //     接线源钉族[生长索引 / 掉落分支 / 种植映射 / 头位检测 / meshbuilder 双路由 / partial case /
    //     pack 映射行] + 呈现面[调色板 / 名面 / 图标 case / 形态组] + 相邻族零污染[149/150/0x28A/
    //     0x287/BrewingStand 148/tile 197 原值]）。

    // ── r2075a：炼药锅水位交互行为柱（真 pc rig placeBlock 链；NEG-1 敏感面）──────────────────────
    //   NEG-1（瓶取水位结算塌缩恒同值写）→ 恰红 = {r2075a}：本腿逐次水位阶梯断言全失；桶灌满 /
    //   空锅拒瓶 / 创造不耗面不在摘面仍绿。本腿不触南瓜族 → NEG-2 不误伤。
    runLeg("r2075a cauldron water-level interaction behavior column (a survival water bucket tops the"
        " cauldron up to exactly level three with the bucket flipping to empty while creative keeps"
        " its water bucket, three glass bottle takes drain the stored water one level at a time from"
        " three down to zero with one water bottle granted per take, an empty cauldron refuses a"
        " bottle with zero effect zero consumption and no swing, an empty hand answers nothing, and"
        " breaking the cauldron through the real mining chain drops the cauldron block itself with"
        " the water vanishing alongside)", [&]() {
        bool ok = true;
        QString diag;
        World wG;
        initFixedBrewWorld(wG);
        layBrewPlatform(wG, 12, 38, 20, 28);
        const int cx = 24, cy = 81, cz = 24; // 炼药锅落格
        wG.setBlock(cx, cy, cz, BR::Cauldron, 0); // 空锅（state=0）
        PlayerController pcG;
        pcG.setWorld(&wG);
        Hotbar hbG;
        pcG.setHotbar(&hbG);
        QQuickWindow winG;
        pcG.setParentItem(winG.contentItem());
        pcG.grab();
        pcG.setSelectedBlock(int(BR::Air)); // 材料/桶段物品真实接线
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 站位 (27.5, 81, 24.5) 瞄锅体中心（水平 3 格 + 竖 1.12 < kReach；同 r2073b aimG 同式）。
        const auto aimG = [&](float fx, float fz, int mode) {
            const float ex = fx, ey = 81.0f + 1.62f, ez = fz;
            const float dx = float(cx) + 0.5f - ex, dy = float(cy) + 0.5f - ey, dz = float(cz) + 0.5f - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcG.release();
            pcG.grab();
            pcG.loadSavedState(fx, 81.0f, fz, yaw, pitch, mode);
            pumpFor(17);
            pcG.tick();
        };
        const auto levelAt = [&]() {
            return wG.stateAt(cx, cy, cz) & quint8(BR::CauldronStateLevelMask);
        };
        // (1) 空手负例：生存空槽右键锅 → 无效应（level 恒 0 / 槽不变）。
        aimG(27.5f, 24.5f, 2 /* Survival */);
        hbG.setStack(0, 0, 0, 0);
        hbG.setSelectedSlot(0);
        pcG.placeBlock();
        pumpFor(60);
        const bool bareHandOk = levelAt() == 0 && hbG.selectedItemId() == 0;
        // (2) 生存装水桶灌满：level 0→3 + 桶→空桶（生存消耗口径）。
        hbG.setStack(0, RecipeRegistry::WaterBucketId, 1, 0);
        pcG.placeBlock();
        pumpFor(220);
        const bool fillOk = levelAt() == 3 && hbG.selectedItemId() == RecipeRegistry::BucketEmptyId;
        // (3) 三次瓶取：level 3→2→1→0 逐次 + 每取 1 水瓶入包 + 空瓶 3→0。
        hbG.setStack(0, RecipeRegistry::GlassBottleId, 3, 0);
        pumpFor(220); // 泵过 200ms 放置 CD（上一次 placeBlock 成功刷新 m_lastPlaceMs——灌满面同门）
        int bottlesSeen = 0;
        bool drainOk = true;
        for (int expect = 2; expect >= 0; --expect) {
            pcG.placeBlock();
            pumpFor(220);
            const int lvl = levelAt();
            bottlesSeen = 0;
            for (int s = 0; s < hbG.slotCount(); ++s)
                if (hbG.blockIdAt(s) == RecipeRegistry::WaterBottleId) bottlesSeen += hbG.countAt(s);
            // 末取（expect==0）后空瓶尽：slot0 由最后一枚水瓶落位（addStack 首空槽）→ 只断水位 + 总水瓶。
            if (lvl != expect || bottlesSeen != 3 - expect)
                { drainOk = false; break; }
            if (expect > 0 && (hbG.blockIdAt(0) != RecipeRegistry::GlassBottleId
                               || hbG.countAt(0) != expect))
                { drainOk = false; break; }
        }
        // (4) 空锅拒瓶：再予 1 空瓶右键 → 无效应不消耗（瓶仍在 1 / 水瓶总数不变 / level 恒 0 / 无挥臂）。
        //   （末取后 slot0 已被水瓶落位——setStack 覆写该槽，水瓶总数 3→2，拒瓶面断「不变」。）
        hbG.setStack(0, RecipeRegistry::GlassBottleId, 1, 0);
        int swingBefore = 0;
        const QMetaObject::Connection swingConn = QObject::connect(
            &pcG, &PlayerController::swingArm, &pcG, [&]() { ++swingBefore; });
        pumpFor(220); // 泵过 CD（第三次取成功刷新 m_lastPlaceMs）
        pcG.placeBlock();
        pumpFor(220);
        int bottlesAfterReject = 0;
        for (int s = 0; s < hbG.slotCount(); ++s)
            if (hbG.blockIdAt(s) == RecipeRegistry::WaterBottleId) bottlesAfterReject += hbG.countAt(s);
        // 拒瓶不变式：水瓶总数与拒前恒等（addStack 落位次序不进断言）+ 予瓶仍在 + 水位恒 0 + 无挥臂。
        const bool emptyRejectOk = levelAt() == 0 && hbG.blockIdAt(0) == RecipeRegistry::GlassBottleId
            && hbG.countAt(0) == 1 && bottlesAfterReject == bottlesSeen && swingBefore == 0;
        QObject::disconnect(swingConn);
        // (5) 创造灌满不耗桶：level 0 → 3 + 桶保持装水桶。
        aimG(27.5f, 24.5f, 1 /* Creative */);
        hbG.setStack(0, RecipeRegistry::WaterBucketId, 1, 0);
        pcG.placeBlock();
        pumpFor(220);
        const bool creativeFillOk = levelAt() == 3 && hbG.selectedItemId() == RecipeRegistry::WaterBucketId;
        // (6) 真链挖掘破坏：锅掉本体 + 格清空（水随方块消失）。
        aimG(27.5f, 24.5f, 2 /* Survival */);
        hbG.setStack(0, 0, 0, 0);
        QVector<int> dropIds, dropCnts;
        const QMetaObject::Connection dropConn = QObject::connect(
            &pcG, &PlayerController::spawnItem, &pcG,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIds.push_back(id);
                dropCnts.push_back(count);
            });
        pcG.beginMining();
        for (int i = 0; i < 9000 && wG.blockAt(cx, cy, cz) != BR::Air; ++i) {
            QElapsedTimer dtw;
            dtw.start();
            while (dtw.elapsed() < 17)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pcG.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pcG.endMining();
        QObject::disconnect(dropConn);
        const bool breakOk = wG.blockAt(cx, cy, cz) == BR::Air
            && dropIds.size() == 1 && dropIds.at(0) == int(BR::Cauldron)
            && dropCnts.at(0) == 1;
        ok = ok && bareHandOk && fillOk && drainOk && emptyRejectOk && creativeFillOk && breakOk;
        if (!ok)
            diag += QStringLiteral("[a bare=%1 fill=%2 drain=%3 reject=%4 creative=%5 break=%6 lvl=%7 drops=%8 bot=%9]")
                        .arg(bareHandOk).arg(fillOk).arg(drainOk).arg(emptyRejectOk)
                        .arg(creativeFillOk).arg(breakOk).arg(levelAt()).arg(dropIds.size())
                        .arg(bottlesAfterReject);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2075a cauldron water-level interaction behavior column (a survival water bucket"
               " tops the cauldron up to exactly level three with the bucket flipping to empty while"
               " creative keeps its water bucket, three glass bottle takes drain the stored water"
               " one level at a time from three down to zero with one water bottle granted per"
               " take, an empty cauldron refuses a bottle with zero effect zero consumption and no"
               " swing, an empty hand answers nothing, and breaking the cauldron through the real"
               " mining chain drops the cauldron block itself with the water vanishing alongside)"
            << (ok ? QString() : diag);
    });

    // ── r2075b：南瓜茎生长门 + 成熟结果行为柱（真 tick 泵 + 骰子镜像 + 真链破茎；NEG-2 敏感面）──────
    //   NEG-2（果 id 三元塌缩恒 Melon）→ 恰红 = {r2075b}：本腿环内结果 = Pumpkin 断言失（南瓜茎格
    //   将落西瓜）；生长骰子镜像 / 双门阴性 / 破茎掉种面不在摘面仍绿但腿红。r2073b/d 西瓜茎语义
    //   不变（恒 Melon）→ 不误伤。
    runLeg("r2075b pumpkin-stem growth gate plus mature fruiting behavior column (the planted stem"
        " climbs the shared eight-age ladder through the mirrored dice windows with monotonic"
        " single-step stages, a mature stem on stone ground and a stem ringed by solid blocks both"
        " refuse to fruit past their reachable dice-hit windows while the free-slot stem drops"
        " exactly one pumpkin block onto the deterministic hash-picked neighbour cell keeping the"
        " stem mature for repeated fruiting, and breaking the stem through the real mining chain"
        " yields exactly one pumpkin seed)", [&]() {
        bool ok = true;
        QString diag;
        World wG;
        initFixedBrewWorld(wG);
        layBrewPlatform(wG, 12, 38, 20, 28);
        // 三块耕地垫：A 主株(24,81,24) 完整生长+结果（5×5 垫）/ B 石地对照(16,81,24)（落地面=石坪拒）/
        //   C 围石对照(32,81,24)（4 邻围石拒）——r2073b MelonStem 模板镜像（同坐标 → 同骰子流）。
        for (int x = 22; x <= 26; ++x)
            for (int z = 22; z <= 26; ++z)
                wG.setBlock(x, 80, z, BR::Farmland, 0);
        wG.setBlock(16, 80, 24, BR::Farmland, 0);
        wG.setBlock(32, 80, 24, BR::Farmland, 0);
        wG.setBlock(24, 81, 24, BR::PumpkinStem, 0); // A：种下 state 0
        wG.setBlock(16, 81, 24, BR::PumpkinStem, 7); // B：直置成熟茎
        wG.setBlock(32, 81, 24, BR::PumpkinStem, 7); // C：直置成熟茎
        wG.setBlock(31, 81, 24, BR::Stone, 0);
        wG.setBlock(33, 81, 24, BR::Stone, 0);
        wG.setBlock(32, 81, 23, BR::Stone, 0);
        wG.setBlock(32, 81, 25, BR::Stone, 0);
        const bool lightOk = wG.skyLightAt(24, 81, 24) >= 9 && wG.skyLightAt(16, 81, 24) >= 9;
        // 骰子镜像（world.cpp tickCropGrowth 同式：干耕地 1× 倍率 growPct=6、无雨；窗口序号自 0）。
        const auto diceHits = [&](int k, int stage, int cx, int cy, int cz) -> bool {
            const int mixedSeed = int(quint32(wG.seed()) ^ (quint32(k) * 0x9E3779B9u));
            return int(wG.hashVoxel(mixedSeed, cx, cy * 7 + stage, cz) & 0xFFFFu) % 100 < 6;
        };
        // (1) A 株生长序列镜像（阶段 0..6；3000 窗守卫）。
        QVector<int> simAdv;
        {
            int stage = 0;
            for (int k = 0; k < 3000 && stage < 7; ++k)
                if (diceHits(k, stage, 24, 81, 24)) { ++stage; simAdv.push_back(k); }
            if (stage < 7) simAdv.clear();
        }
        const bool simOk = !simAdv.isEmpty();
        // (2) 结果窗镜像：熟窗后首个骰子命中窗 + 期望槽位（哈希定起始向 + 首空格落地面耕地）。
        int fruitWin = -1, expectX = -1, expectZ = -1;
        if (simOk) {
            const int k7 = simAdv.last();
            for (int k = k7 + 1; k < k7 + 3000 && fruitWin < 0; ++k)
                if (diceHits(k, 7, 24, 81, 24)) fruitWin = k;
            if (fruitWin > 0) {
                const int mixedSeed = int(quint32(wG.seed()) ^ (quint32(fruitWin) * 0x9E3779B9u));
                const quint32 h = wG.hashVoxel(mixedSeed, 24, 81 * 7 + 7, 24);
                const int start = int((h >> 16) & 3u);
                static constexpr int kDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int d = 0; d < 4; ++d) {
                    const int nx = 24 + kDir[(start + d) & 3][0];
                    const int nz = 24 + kDir[(start + d) & 3][1];
                    if (wG.blockAt(nx, 81, nz) == BR::Air && wG.blockAt(nx, 80, nz) == BR::Farmland) {
                        expectX = nx;
                        expectZ = nz;
                        break;
                    }
                }
            }
        }
        const int pumpEnd = (fruitWin > 0) ? fruitWin : (simOk ? simAdv.last() : 0);
        // (3) 真泵（25 tick = 1 窗；逐窗采样：升阶段序列 ≡ 镜像 + 单调）。
        bool monoOk = true;
        int prevStage = 0, advIdx = 0;
        for (int k = 0; k <= pumpEnd; ++k) {
            for (int c = 0; c < 25; ++c) wG.tickCropGrowth();
            const int st = wG.stateAt(24, 81, 24);
            if (st - prevStage > 1 || st - prevStage < 0 || st > 7) monoOk = false;
            if (advIdx < simAdv.size() && k == simAdv.at(advIdx) && st - prevStage == 1) ++advIdx;
            prevStage = st;
        }
        const bool seqOk = simOk && advIdx == simAdv.size() && prevStage == BR::WheatCropStageMax;
        // (4) 结果面：A 环恰 1 南瓜 @期望格（NEG-2 塌缩果 id → 环内是西瓜此处红）+ 茎保持成熟。
        const auto ringFruitScan = [&](int cx, int cz, int *mx, int *mz) {
            int n = 0;
            static constexpr int kR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto &o : kR) {
                const quint8 b = wG.blockAt(cx + o[0], 81, cz + o[1]);
                if (b == BR::Pumpkin || b == BR::Melon) { // 双计归因：塌缩面用「产物 id」断言精确切红
                    ++n;
                    if (mx) { *mx = cx + o[0]; *mz = cz + o[1]; }
                }
            }
            return n;
        };
        int mx = -1, mz = -1;
        const int ringHits = ringFruitScan(24, 24, &mx, &mz);
        const bool fruitOk = fruitWin > 0 && expectX > 0 && ringHits == 1
                             && mx == expectX && mz == expectZ
                             && wG.blockAt(mx, 81, mz) == BR::Pumpkin; // NEG-2 摘面：塌缩成 Melon 此处失
        const bool matureKept = wG.blockAt(24, 81, 24) == BR::PumpkinStem
                                && wG.stateAt(24, 81, 24) == BR::WheatCropStageMax;
        // (5) 真链破茎：持空手生存瞄 A 茎（站位避果——沿首条「环格非果」轴向退 2 格站）。
        PlayerController pcG;
        pcG.setWorld(&wG);
        Hotbar hbG;
        pcG.setHotbar(&hbG);
        QQuickWindow winG;
        pcG.setParentItem(winG.contentItem());
        pcG.grab();
        pcG.setSelectedBlock(int(BR::Air));
        QVector<int> dropIdsG;
        const QMetaObject::Connection dropConnG = QObject::connect(
            &pcG, &PlayerController::spawnItem, &pcG,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIdsG.push_back(id);
                dropIdsG.push_back(count);
            });
        int standX = 24, standZ = 24;
        static constexpr int kDirB[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (const auto &o : kDirB) {
            const int rx = 24 + o[0], rz = 24 + o[1];
            if (!(rx == mx && rz == mz)) { standX = 24 + 2 * o[0]; standZ = 24 + 2 * o[1]; break; }
        }
        const float exS = float(standX) + 0.5f, ezS = float(standZ) + 0.5f;
        const float dxS = 24.5f - exS, dyS = 81.5f - (81.0f + 1.62f), dzS = 24.5f - ezS;
        const float lenS = std::sqrt(dxS * dxS + dyS * dyS + dzS * dzS);
        pcG.release();
        pcG.grab();
        pcG.loadSavedState(exS, 81.0f, ezS,
                           std::atan2(-dxS, -dzS) * 57.2957795f,
                           std::asin(dyS / lenS) * 57.2957795f, 2 /* Survival */);
        pcG.tick();
        pcG.beginMining();
        for (int i = 0; i < 6000 && wG.blockAt(24, 81, 24) != BR::Air; ++i) {
            QElapsedTimer dtw;
            dtw.start();
            while (dtw.elapsed() < 17)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pcG.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pcG.endMining();
        QObject::disconnect(dropConnG);
        const bool stemGone = wG.blockAt(24, 81, 24) == BR::Air;
        const bool stemDrop = stemGone && dropIdsG.size() == 2
                              && dropIdsG.at(0) == RecipeRegistry::PumpkinSeedsId
                              && dropIdsG.at(1) == 1;
        // (6) B/C 可达域守卫泵（双对照环恒 0 果）。
        int bHit = -1, cHit = -1;
        for (int k = 0; k <= 3000 && (bHit < 0 || cHit < 0); ++k) {
            if (bHit < 0 && diceHits(k, 7, 16, 81, 24)) bHit = k;
            if (cHit < 0 && diceHits(k, 7, 32, 81, 24)) cHit = k;
        }
        int gateEnd = pumpEnd;
        if (bHit > gateEnd) gateEnd = bHit;
        if (cHit > gateEnd) gateEnd = cHit;
        for (int k = pumpEnd + 1; k <= gateEnd; ++k)
            for (int c = 0; c < 25; ++c) wG.tickCropGrowth();
        const bool gatesOk = ringFruitScan(16, 24, nullptr, nullptr) == 0
                             && ringFruitScan(32, 24, nullptr, nullptr) == 0;
        ok = ok && lightOk && monoOk && seqOk && fruitOk && matureKept && stemDrop && gatesOk;
        if (!ok)
            diag += QStringLiteral("[b light=%1 mono=%2 seq=%3 fruit=%4@(x%5,z%6,n=%7,id=%8) kept=%9 drop=%10 gone=%11 gates=%12]")
                        .arg(lightOk).arg(monoOk).arg(seqOk).arg(fruitOk)
                        .arg(mx).arg(mz).arg(ringHits)
                        .arg(mx > 0 ? wG.blockAt(mx, 81, mz) : -1).arg(matureKept)
                        .arg(stemDrop).arg(stemGone).arg(gatesOk);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2075b pumpkin-stem growth gate plus mature fruiting behavior column (the planted"
               " stem climbs the shared eight-age ladder through the mirrored dice windows with"
               " monotonic single-step stages, a mature stem on stone ground and a stem ringed by"
               " solid blocks both refuse to fruit past their reachable dice-hit windows while the"
               " free-slot stem drops exactly one pumpkin block onto the deterministic hash-picked"
               " neighbour cell keeping the stem mature for repeated fruiting, and breaking the"
               " stem through the real mining chain yields exactly one pumpkin seed)"
            << (ok ? QString() : diag);
    });

    // ── r2075c：南瓜生存入口 + 南瓜灯合成 + 光照面行为柱（纯表 + 真世界光照；NEG 双面均不触达）──────
    runLeg("r2075c pumpkin survival entry plus jack-o-lantern crafting plus light face column (a"
        " pumpkin breaks down into exactly four pumpkin seeds shapeless in both grids with wrong"
        " ingredient pairs answering null, a pumpkin plus a torch crafts the jack-o-lantern"
        " shapeless in both grids, the lantern answers light emission fifteen through the"
        " state-aware authority while the plain pumpkin and the cauldron stay dark, a placed"
        " lantern floods its neighbour cell to block light fourteen in a real world, and the"
        " pumpkin seed item reads back its display name stack limit sixty-four and palette"
        " presence)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 南瓜 → 4 南瓜种子（无序双格命中 + 行名身份 + 产率 4）。
        const int g2[9] = { int(BR::Pumpkin), 0, 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r2 = RecipeRegistry::match(g2, 2);
        const bool seedDown = r2 && r2->outputId == RecipeRegistry::PumpkinSeedsId
            && r2->outputCount == 4 && r2->shapeless
            && QLatin1String(r2->name) == QLatin1String("pumpkin_seeds_from_pumpkin");
        const int g3[9] = { 0, 0, 0, 0, int(BR::Pumpkin), 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r3 = RecipeRegistry::match(g3, 3);
        const bool seedDown3 = r3 && r3->outputId == RecipeRegistry::PumpkinSeedsId
            && r3->outputCount == 4;
        ok = ok && seedDown && seedDown3;
        if (!(seedDown && seedDown3)) diag += QStringLiteral("[seeds %1/%2]").arg(seedDown).arg(seedDown3);
        // (2) 南瓜灯：南瓜 + 火把（无序 2×2 + 3×3 异位双命中；产物 id / 产率 1 / 名）。
        const int gJ2[9] = { int(BR::Pumpkin), int(BR::Torch), 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rJ2 = RecipeRegistry::match(gJ2, 2);
        const bool jack2 = rJ2 && rJ2->outputId == int(BR::JackOLantern)
            && rJ2->outputCount == 1 && rJ2->shapeless
            && QLatin1String(rJ2->name) == QLatin1String("jack_o_lantern");
        const int gJ3[9] = { 0, 0, 0, 0, int(BR::Torch), 0, 0, 0, int(BR::Pumpkin) };
        const RecipeRegistry::Recipe *rJ3 = RecipeRegistry::match(gJ3, 3);
        const bool jack3 = rJ3 && rJ3->outputId == int(BR::JackOLantern) && rJ3->outputCount == 1;
        ok = ok && jack2 && jack3;
        if (!(jack2 && jack3)) diag += QStringLiteral("[jack %1/%2]").arg(jack2).arg(jack3);
        // (3) 错料负例：单种子不成行 / 种子+南瓜对不成行 / 火把+瓜种对不成行 / 双南瓜不成行。
        const auto expectNoMatch = [&](const int gg[9], int w, const char *why) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(gg, w);
            if (r) diag += QStringLiteral("[%1 matched %2]").arg(QLatin1String(why)).arg(r->outputId);
            return r == nullptr;
        };
        const int n1[9] = { RecipeRegistry::PumpkinSeedsId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const int nSP[9] = { RecipeRegistry::PumpkinSeedsId, int(BR::Pumpkin), 0, 0, 0, 0, 0, 0, 0 };
        const int nTS[9] = { int(BR::Torch), RecipeRegistry::PumpkinSeedsId, 0, 0, 0, 0, 0, 0, 0 };
        const int nPP[9] = { int(BR::Pumpkin), int(BR::Pumpkin), 0, 0, 0, 0, 0, 0, 0 };
        const bool negs = expectNoMatch(n1, 2, "single-seed")
            && expectNoMatch(nSP, 2, "seed-pumpkin-pair")
            && expectNoMatch(nTS, 2, "torch-seed-pair")
            && expectNoMatch(nPP, 2, "pumpkin-pair");
        ok = ok && negs;
        if (!negs) diag += QStringLiteral("[negs]");
        // (4) 光源面：状态感知权威（灯 15 与 state 无关）+ 南瓜 / 炼药锅恒 0。
        const bool emitOk = BR::lightEmission(quint8(BR::JackOLantern), 0) == 15
            && BR::lightEmission(quint8(BR::JackOLantern), 0x2) == 15
            && BR::lightEmission(quint8(BR::JackOLantern)) == 15
            && BR::lightEmission(quint8(BR::Pumpkin), 0) == 0
            && BR::lightEmission(quint8(BR::Cauldron), 0) == 0;
        ok = ok && emitOk;
        if (!emitOk) diag += QStringLiteral("[emit]");
        // (5) 真世界光照面：放置灯 → 邻格方块光 flood 14（setBlock lightSourceChanged 重 flood）+
        //     摘源回落 0。
        {
            World wL;
            initFixedBrewWorld(wL);
            layBrewPlatform(wL, 12, 38, 20, 28);
            wL.setBlock(24, 81, 24, BR::JackOLantern, 0);
            const quint8 litNear = wL.blockLightAt(25, 81, 24);
            wL.setBlock(24, 81, 24, BR::Air, 0);
            const quint8 litAfter = wL.blockLightAt(25, 81, 24);
            const bool floodOk = litNear == 14 && litAfter == 0;
            ok = ok && floodOk;
            if (!floodOk) diag += QStringLiteral("[flood %1->%2]").arg(litNear).arg(litAfter);
        }
        // (6) 物品面：南瓜种子名 / maxStack 64 双面 / 调色板在列。
        Hotbar hbC;
        bool palFound = false;
        const QVariantList pal = hbC.creativeMaterials();
        for (const QVariant &v : pal)
            if (v.toInt() == RecipeRegistry::PumpkinSeedsId) palFound = true;
        const bool itemOk = hbC.nameForBlock(RecipeRegistry::PumpkinSeedsId) == QStringLiteral("南瓜种子")
            && hbC.maxStackSize(RecipeRegistry::PumpkinSeedsId) == 64
            && BR::maxStackSize(RecipeRegistry::PumpkinSeedsId) == 64
            && hbC.isMaterial(RecipeRegistry::PumpkinSeedsId)
            && palFound;
        ok = ok && itemOk;
        if (!itemOk) diag += QStringLiteral("[item pal=%1]").arg(palFound);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2075c pumpkin survival entry plus jack-o-lantern crafting plus light face column"
               " (a pumpkin breaks down into exactly four pumpkin seeds shapeless in both grids"
               " with wrong ingredient pairs answering null, a pumpkin plus a torch crafts the"
               " jack-o-lantern shapeless in both grids, the lantern answers light emission"
               " fifteen through the state-aware authority while the plain pumpkin and the"
               " cauldron stay dark, a placed lantern floods its neighbour cell to block light"
               " fourteen in a real world, and the pumpkin seed item reads back its display name"
               " stack limit sixty-four and palette presence)"
            << (ok ? QString() : diag);
    });

    // ── r2075d：结构钉族（NEG 双摘面全豁免——不钉瓶取水位表达式与果 id 三元）────────────────────────
    runLeg("r2075d structure pin family (the pumpkin seed item closes the material segment at hex"
        " 28b tail-appended after the melon slice while the three blocks land at one fifty-one"
        " through one fifty-three with the count sentinel moving to one fifty-four and the atlas"
        " to two hundred seven tiles, the mapping table carries pumpkin stem one hundred four and"
        " jack-o-lantern ninety-one and cauldron one eighteen as unique rows, the three def rows"
        " hold every field from the tile tuples through the no-tool and pickaxe gates to the self"
        " and seed drops, the shared stage override and cross billboard and grass material and"
        " emission faces all route the new ids, both craft rows and the cauldron level mask and"
        " the worldgen patch declaration with both call sites and the wiring family across growth"
        " index and drop branch and planting map and golem head and mesher routing and pack"
        " mapping and the presentation faces pin their single call sites, and the neighbouring"
        " melon and mundane and brewing families keep their sentinels untouched)", [&]() {
        bool ok = true;
        QString diag;
        Hotbar hbS;
        // (S1) id / 段位 / 映射 / 图集（运行期 + 唯一性全表扫）。
        int mc104Rows = 0, mc91Rows = 0, mc118Rows = 0;
        for (int i = 0; i < int(BR::Count); ++i) {
            mc104Rows += BR::mcBlockId(quint8(i)) == 104 ? 1 : 0;
            mc91Rows += BR::mcBlockId(quint8(i)) == 91 ? 1 : 0;
            mc118Rows += BR::mcBlockId(quint8(i)) == 118 ? 1 : 0;
        }
        const bool idsOk = RecipeRegistry::PumpkinSeedsId == 0x28B
            && int(BR::PumpkinStem) == 151 && int(BR::JackOLantern) == 152
            && int(BR::Cauldron) == 153 && int(BR::Count) == 160 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）
            && int(BR::AtlasTileCount) == 209
            && mc104Rows == 1 && mc91Rows == 1 && mc118Rows == 1
            && BR::mcBlockId(quint8(BR::PumpkinStem)) == 104
            && BR::mcBlockId(quint8(BR::JackOLantern)) == 91
            && BR::mcBlockId(quint8(BR::Cauldron)) == 118
            && BR::mcBlockId(quint8(BR::Pumpkin)) == 86 // MC 1.0 pumpkin id 86（引擎 id 100 ≠ 映射值）
            && BR::CauldronStateLevelMask == 0x3
            && hbS.maxStackSize(int(BR::JackOLantern)) == 64
            && hbS.maxStackSize(int(BR::Cauldron)) == 64;
        ok = ok && idsOk;
        if (!idsOk) diag += QStringLiteral("[ids 104=%1 91=%2 118=%3]").arg(mc104Rows).arg(mc91Rows).arg(mc118Rows);
        // (S2) 谓词 / 阶段 / 权威面（两茎同式 + cross 路由 + 音色 + 发光 + stateTileOverride）。
        const bool predOk = BR::isCrossBillboard(quint8(BR::PumpkinStem))
            && !BR::isCrossBillboard(quint8(BR::Cauldron))
            && BR::materialGroup(quint8(BR::PumpkinStem)) == BR::GroupGrass
            && BR::materialGroup(quint8(BR::JackOLantern)) == BR::GroupGrass
            && BR::materialGroup(quint8(BR::Cauldron)) == BR::GroupStone
            && BR::stateTileOverride(quint8(BR::PumpkinStem), int(BR::PosX), 3) == 201 + 1
            && BR::stateTileOverride(quint8(BR::PumpkinStem), int(BR::PosX), 7) == 201 + 3
            && BR::stateTileOverride(quint8(BR::MelonStem), int(BR::PosX), 4) == 197 + 2;
        ok = ok && predOk;
        if (!predOk) diag += QStringLiteral("[pred]");
        // (S3) 配方两行 + 炼药锅交互源钉（剥注释 pinSet 锚真实语句；NEG-1 摘面表达式不钉 = 豁免）。
        const QString recCpp = srcRootForPins() + QStringLiteral("/Game/recipe.cpp");
        const QString pcCpp = srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp");
        const QStringList missRec = pinSet(recCpp, {
            SrcPin("seed row", "\"pumpkin_seeds_from_pumpkin\" }", 1),
            SrcPin("jack row", "\"jack_o_lantern\" }", 1)});
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty()) diag += QStringLiteral("[rec %1] ").arg(missRec.join(QLatin1Char(',')));
        const QStringList missPc = pinSet(pcCpp, {
            SrcPin("cauldron branch", "m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Cauldron", 1),
            SrcPin("drop branch", "else if (id == BlockRegistry::PumpkinStem)", 1),
            SrcPin("planting map", "{ RecipeRegistry::PumpkinSeedsId, BlockRegistry::PumpkinStem }", 1),
            SrcPin("golem head", "idByte == BlockRegistry::Pumpkin || idByte == BlockRegistry::JackOLantern", 1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty()) diag += QStringLiteral("[pc %1] ").arg(missPc.join(QLatin1Char(',')));
        // (S4) worldgen / 接线源钉族（生长索引 / 双调用点 / mesher 双路由 / partial case / pack 行）。
        const QString wH = rawSource(srcRootForPins() + QStringLiteral("/World/world.h"));
        const QString wCpp = rawSource(srcRootForPins() + QStringLiteral("/World/world.cpp"));
        const QString meshCpp = rawSource(srcRootForPins() + QStringLiteral("/World/meshbuilder.cpp"));
        const QString pbgCpp = rawSource(srcRootForPins() + QStringLiteral("/World/partialblockgeometry.cpp"));
        const QString rpCpp = rawSource(srcRootForPins() + QStringLiteral("/Core/resourcepackmanager.cpp"));
        const bool wiring = wH.contains(QLatin1String("void placePumpkinPatches(int wx0 = 0"))
            && wCpp.contains(QLatin1String("placePumpkinPatches(wx0, wx1, wz0, wz1);"))
            && wCpp.contains(QLatin1String("placePumpkinPatches(); // t1105"))
            && wCpp.contains(QLatin1String("|| id == BR::PumpkinStem // t1105"))
            && wCpp.contains(QLatin1String("|| b == BlockRegistry::PumpkinStem) { // t1105"))
            && meshCpp.contains(QLatin1String("b == BlockRegistry::Cauldron     // t1105"))
            && meshCpp.contains(QLatin1String("block == BlockRegistry::Pumpkin || block == BlockRegistry::JackOLantern"))
            && pbgCpp.contains(QLatin1String("case BlockRegistry::Cauldron: {"))
            && pbgCpp.contains(QLatin1String("case BlockRegistry::PumpkinStem: {"))
            && rpCpp.contains(QLatin1String("{205, QStringLiteral(\"pumpkin_face_on.png\")}"));
        ok = ok && wiring;
        if (!wiring) diag += QStringLiteral("[wiring]");
        // (S5) 呈现面（调色板尾三连 / 名面 / 图标 case / 形态组）。
        //   [lawful 修订 t1107/r2077] 调色板材料段尾前移：南瓜种子行自「段尾无逗号形」改「逗号行」
        //   （t1107 史莱姆球 0x28C 段尾追加为新尾行，t879 图集 201→207 / t1106 kMobTypeCount 同门
        //   沿革注）；尾行钉改 SlimeBallId。原 `int(RecipeRegistry::PumpkinSeedsId)  ` 段尾形退役。
        const QString hbCpp = rawSource(srcRootForPins() + QStringLiteral("/Game/hotbar.cpp"));
        const QString iconQml = rawSource(srcRootForPins() + QStringLiteral("/ui/MaterialIcon.qml"));
        //   [lawful 修订 t1111/r2081] 调色板方块段尾前移：炼药锅行自「段尾无逗号形」改「逗号行」
        //   （t1111 砂岩楼梯行续尾追加为新尾行——t1107 史莱姆球材料段同门沿革注）；尾行钉改
        //   SandstoneStairs。原 `int(BlockRegistry::Cauldron) }` 段尾形退役。
        //   [lawful 修订 t1112/r2082] 二次同门修订：砂岩楼梯行自「段尾无逗号形」改「逗号行」
        //   （t1112 蛋糕行续尾追加为新尾行——t1111 同门沿革注）；尾行钉改 Cake。原
        //   `int(BlockRegistry::SandstoneStairs) }` 段尾形退役。
        const bool pres = hbCpp.contains(QLatin1String("int(BlockRegistry::JackOLantern),"))
            && hbCpp.contains(QLatin1String("int(BlockRegistry::Cauldron),"))
            && hbCpp.contains(QLatin1String("int(BlockRegistry::SandstoneStairs),"))
            && hbCpp.contains(QLatin1String("int(RecipeRegistry::PumpkinSeedsId),"))
            && hbCpp.contains(QLatin1String("int(RecipeRegistry::SlimeBallId),"))
            && hbCpp.contains(QLatin1String("int(BlockRegistry::Cake) }"))
            && hbCpp.contains(QStringLiteral("南瓜种子")) // 中文 needle 须 UTF-16（QLatin1String 装不下汉字恒失配）
            && hbCpp.contains(QLatin1String("case BlockRegistry::PumpkinStem: // t1105"))
            && hbCpp.contains(QLatin1String("s = { 0, 1, 2, 3 };"))
            && iconQml.contains(QLatin1String("case 0x28B: drawPumpkinSeeds(); break"));
        ok = ok && pres;
        if (!pres) diag += QStringLiteral("[pres]");
        // (S6) 相邻族零污染（149/150/0x28A/0x287/BrewingStand 148/瓜茎 tile 基底原值）。
        const bool neigh = int(BR::Melon) == 149 && int(BR::MelonStem) == 150
            && RecipeRegistry::MelonSliceId == 0x28A
            && RecipeRegistry::MundanePotionId == 0x287
            && int(BR::BrewingStand) == 148
            && BR::stateTileOverride(quint8(BR::MelonStem), int(BR::PosX), 0) == 197;
        ok = ok && neigh;
        if (!neigh) diag += QStringLiteral("[neigh]");
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2075d structure pin family (the pumpkin seed item closes the material segment at"
               " hex 28b tail-appended after the melon slice while the three blocks land at one"
               " fifty-one through one fifty-three with the count sentinel moving to one fifty-four"
               " and the atlas to two hundred seven tiles, the mapping table carries pumpkin stem"
               " one hundred four and jack-o-lantern ninety-one and cauldron one eighteen as unique"
               " rows, the three def rows hold every field from the tile tuples through the no-tool"
               " and pickaxe gates to the self and seed drops, the shared stage override and cross"
               " billboard and grass material and emission faces all route the new ids, both craft"
               " rows and the cauldron level mask and the worldgen patch declaration with both call"
               " sites and the wiring family across growth index and drop branch and planting map"
               " and golem head and mesher routing and pack mapping and the presentation faces pin"
               " their single call sites, and the neighbouring melon and mundane and brewing"
               " families keep their sentinels untouched)"
            << (ok ? QString() : diag);
    });
}
