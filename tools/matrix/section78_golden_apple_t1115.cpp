#include "matrix_helpers.h"

// t1115 golden apple 完整链探针段(5 腿;filter 词 r2085;矩阵 876→881)。置尾先例沿用(接 section77,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工三面逐一全仓 grep 实读)──
//   ① apple 根物品:全仓 grep AppleId/golden apple 零命中(t1111 d 腿负面钉既录真缺——loottable.h
//     「本工程无苹果物品」/ Inventory.qml「苹果(无苹果物品)」显式记载面)→ 真缺。交付 = AppleId=
//     0x292 / GoldenAppleId=0x293 材料段尾追加(0x291 填充地图之上,追加不插中间 = 存档安全铁律)。
//     1.0 定值:apple hunger +4/sat 2.4;golden apple hunger +4/sat 9.6(工程无饱和度细分面取饥饿
//     值口径,瓜片 t1103 同门);golden apple 食毕 Regen I 30s(Absorption 1.1+ 越纪元不取)。
//     **获取面 = 橡树叶掉落 1/200**:工程橡树叶既有掉落面实读 = PlayerController::dropLeafDrops
//     (树苗 10%/木棒 8%,t379;**LeafDecay 消亡面零掉落** = t305 既录简化——派工「破坏/消亡两路径」
//     按既录面裁定苹果只入破坏路径留痕,1.0 消亡同表面不扩);苹果面仅**橡树叶**携带(云杉叶 1.0
//     无苹果面——t714 云杉叶同分流族只共享树苗/木棒面,签名 +leafId 门)。1.0 地牢箱金苹果行不取
//     (t484 矿物族替代面既录,零 loot delta 候选池登记留痕)。
//   ② golden apple 物品:段尾追加 + 食面(foodHungerAmount 单一权威 +4)+ 效果面(finishEating
//     applyStatusEffect EffectRegeneration 30s——再生药水 kRegenPotionDurationSec=45s 同管线不同
//     时长,新常量 kGoldenAppleRegenDurationSec=30s;Survival 门内置;再生脉冲 2.5s 同源)。
//     金苹果进食时长无加成(1.0 全食物统一 kEatDuration 1.6s;per-item 时长 1.9+ 面不取,负面钉锁)。
//   ③ 合成面:8 金锭环+1 苹果心 → 1 金苹果(Beta 1.2 金块环初版 → Beta 1.9 pre2 改 8 金锭 = 1.0.0
//     在册形态)。多重集 {GoldIngot:8, Apple:1} 全表唯一(熔炉 8 圆石缺心/地图 8 纸缺心/画作 8 棒+
//     心毛/闪烁西瓜 8 金粒+心片——同形环行异料逐格 id 比对零冲突,**未撞行零降级**,t1113 sign
//     降级先例不触发)。kMc 面不扩(mcMaterialId 越界 -1 → MaterialIcon 自绘回退)。
//   t1116 沿革:金苹果行已入矿井箱池(loottable.cpp 段尾追加,权重 1 / 恒 1 件)——r2085e 原「战利品
//     零苹果」缺席钉随交付退役改钉「零生苹果 + 金苹果行交付面」(缺席面随交付关闭,t1111 金苹果缺席
//     钉 t1115 退役同门),行位沿革见 loottable.h t1116 注。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;摘调用点非摘守卫体 t1051 教训应用)──
//   NEG-1 = 摘 playercontroller.cpp dropLeafDrops 苹果块的 emit spawnItem(AppleId) 单语句行
//     (块壳空转,编译仍绿——guard 行/常量/签名不在摘面)→ 恰红 = {r2085a}(橡树叶直调采样:
//     摘行后苹果恒零掉 = 腿级 FAIL;树苗/木棒族面与云杉零苹果门不在摘面 = 幸存)。**豁免设计:
//     r2085b/c/d/e 均不含该 emit 行任何形态的源钉**(t1114 摘面行豁免不钉同门;r2085e 钉
//     dropLeafDrops 的 decl 行/impl 头行/call 行——均非摘面行,NEG-1 摘行后原值幸存)。
//   NEG-2 = 摘 playercontroller.cpp finishEating 金苹果分支的 applyStatusEffect 单语句行
//     (块壳空转,编译仍绿——if 守卫行/常量/食物/饥饿/消耗面不在摘面)→ 恰红 = {r2085c}(再生
//     效果快照断言:摘行后 snapshot 恒无 EffectRegeneration = 腿级 FAIL;饥饿 +4 面幸存但腿以
//     合取归红)。**豁免设计:r2085a/b/d/e 均不含该语句行任何形态的源钉**(同门;r2085e 钉
//     finishEating 外的 foodHungerAmount 行与常量行——非摘面行幸存)。
namespace {

// fixed 宿主小世界 incantation(section69..77 同款四 setter)。
inline void initRosterAppleWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(85);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section76/77 同款)。
inline void layRosterApplePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 真链 pc rig 的节拍驱动器(section75/76 同门:60ms 墙钟 × pc.tick(),dt 钳 0.05 —— 进食 1.6s
//   满 = 32 tick,44 拍越窗;再生脉冲 2.5s = 50 tick,追加拍数覆盖首脉冲)。
inline void pumpRosterApple(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
inline void driveRosterAppleTicks(PlayerController &pc, int beats)
{
    for (int i = 0; i < beats; ++i) {
        pc.tick();
        pumpRosterApple(60);
    }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section72..77 同款)。
inline QString srcRootForRosterApplePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚/负面面——pinSet 剥注释会失配,section72..77 同款)。
inline bool rawContainsRosterApple(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

} // namespace

void MatrixRun::section78_golden_apple_t1115()
{
    // ── r2085a:苹果掉落柱(NEG-1 敏感面 = dropLeafDrops 苹果 emit 行)─────────────────────────
    //   橡树叶直调大样本(6000 采样,λ=30,带 [1,90] 双向拒错) + 树苗/木棒族面幸存(10%/8% 先例行
    //   零污染) + 云杉叶零苹果门(600 采样恒零 = oak only 负例) + 消亡路径零苹果(world.cpp 零
    //   AppleId 原始含负钉 = t305「自然衰减无掉落」既录面) + 概率常量值面(kLeafAppleDropDenom=200)。
    runLeg("r2085a apple drop column (breaking oak leaves answers apples at the one in two"
        " hundred face across a large direct drive sample while saplings and sticks keep"
        " their own rows, spruce leaves never answer apples per the oak only gate, and the"
        " natural decay path stays apple free on file)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 橡树叶大样本直调(NEG-1 敏感面:摘 emit 行 → apples 恒 0 = FAIL)。
        int apples = 0, saplings = 0, sticks = 0, spruceApples = 0;
        {
            World w;
            initRosterAppleWorld(w);
            layRosterApplePlatform(w, 4, 44, 4, 44);
            // 叶田:橡树叶 20×20 + 云杉叶 20×20(x 4..23 / 26..45 不重叠;叶不掉落自清除——
            //   dropLeafDrops 纯掷骰不写世界)。
            for (int i = 0; i < 20; ++i)
                for (int j = 0; j < 20; ++j) {
                    w.setBlock(4 + i, 82, 6 + j, BR::Leaves, 0);
                    w.setBlock(26 + i, 82, 6 + j, BR::SpruceLeaves, 0);
                }
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QObject::connect(&pc, &PlayerController::spawnItem, &pc,
                             [&](int, int, int, int id, int count,
                                 const QVariantList &, const QString &, int) {
                if (id == int(RecipeRegistry::AppleId))            apples += count;
                else if (id == int(RecipeRegistry::SaplingItemId)) saplings += count;
                else if (id == int(RecipeRegistry::StickId))       sticks += count;
            });
            // 橡树叶 6000 采样(λ = 6000/200 = 30):带断言 [1,90] 双向拒错(P(0)≈9e-14,P(>90)≈0)。
            for (int i = 0; i < 6000; ++i)
                pc.dropLeafDrops(4 + (i % 20), 82, 6 + (i / 20 % 20), quint8(BR::Leaves));
            // 云杉叶 600 采样(λ 若随族 = 3):oak only 门 → 恒 0(确定性负例)。
            for (int i = 0; i < 600; ++i)
                pc.dropLeafDrops(26 + (i % 20), 82, 6 + (i / 20 % 20), quint8(BR::SpruceLeaves));
        }
        const bool dropOk = apples >= 1 && apples <= 90      // 苹果面(NEG-1 摘行 → 0 = 恰红)
            && saplings >= 1                                 // 树苗族面幸存(10% 先例零污染)
            && sticks >= 1                                   // 木棒族面幸存(8% 先例零污染)
            && spruceApples == 0;                            // 云杉零苹果门(oak only)
        ok = ok && dropOk;
        if (!dropOk) diag += QStringLiteral("[drop a=%1 s=%2 st=%3 sp=%4]")
            .arg(apples).arg(saplings).arg(sticks).arg(spruceApples);
        // (2) 概率常量值面 + 消亡路径零苹果负钉(world.cpp 零 AppleId——t305 既录面)。
        {
            const QString srcDir = srcRootForRosterApplePins();
            const bool constOk = rawContainsRosterApple(srcDir + QStringLiteral("/Game/playercontroller.h"),
                                                        QStringLiteral("kLeafAppleDropDenom = 200"));
            ok = ok && constOk;
            if (!constOk) diag += QStringLiteral("[const]");
            const bool decayClean = !rawContainsRosterApple(srcDir + QStringLiteral("/World/world.cpp"),
                                                            QStringLiteral("AppleId"));
            ok = ok && decayClean;
            if (!decayClean) diag += QStringLiteral("[decay]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2085a apple drop column (breaking oak leaves answers apples at the one in"
               " two hundred face across a large direct drive sample while saplings and"
               " sticks keep their own rows, spruce leaves never answer apples per the oak"
               " only gate, and the natural decay path stays apple free on file)"
            << (ok ? QString() : diag);
    });

    // ── r2085b:苹果食物柱(食物值面 + 真链进食)───────────────────────────────────────────────
    //   foodHungerAmount 单一权威(+4 苹果/+4 金苹果/瓜片 +2 幸存行) + 非可饮面(双 id 均不入
    //   isDrinkableItem) + 真链进食 rig(长按 1.6s 满 → 饥饿 +4 + 生存消耗 1 件 + foodBurped 恰
    //   一次 + potionDrunk 零) + 名面(苹果)。
    runLeg("r2085b apple food column (the apple answers four hunger as a non drinkable food"
        " with the golden apple and the melon slice rows beside it, a real controller eating"
        " the held apple advances hunger by four and consumes the stack with the food burp"
        " and no potion drink signal, and the name row answers the apple)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 食物值面(单一权威表)+ 非可饮面。
        const bool tableOk = PlayerController::foodHungerAmount(int(RecipeRegistry::AppleId)) == 4
            && PlayerController::foodHungerAmount(int(RecipeRegistry::GoldenAppleId)) == 4
            && PlayerController::foodHungerAmount(int(RecipeRegistry::MelonSliceId)) == 2 // 瓜片幸存行
            && !PlayerController::isDrinkableItem(int(RecipeRegistry::AppleId))
            && !PlayerController::isDrinkableItem(int(RecipeRegistry::GoldenAppleId));
        ok = ok && tableOk;
        if (!tableOk) diag += QStringLiteral("[table]");
        // (2) 真链进食 rig(Survival;长按链跑满 → 饥饿 10→14 + 消耗 1 件 + burp 恰一次)。
        {
            World w;
            initRosterAppleWorld(w);
            layRosterApplePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            int burps = 0, drinks = 0;
            QObject::connect(&pc, &PlayerController::foodBurped, &pc, [&](int) { ++burps; });
            QObject::connect(&pc, &PlayerController::potionDrunk, &pc, [&](int) { ++drinks; });
            pc.setSelectedBlock(int(BR::Air)); // 空手物品右键 = 食用(使用语义)
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 2 /* Survival */);
            pc.setHunger(10);
            pc.tick();
            hb.setStack(0, int(RecipeRegistry::AppleId), 1, 0); // 苹果上手
            pc.beginEating();
            driveRosterAppleTicks(pc, 44); // 44 拍 × 60ms(dt 钳 0.05)≥ 1.6s 进食窗
            pc.endEating();
            const bool eatOk = pc.hunger() == 14                       // +4 精确
                && hb.countAt(0) == 0                                  // 生存消耗 1 件
                && burps == 1                                          // 食物 burp 恰一次
                && drinks == 0;                                        // 非可饮 → 零 potionDrunk
            ok = ok && eatOk;
            if (!eatOk) diag += QStringLiteral("[eat h=%1 n=%2 b=%3 d=%4]")
                .arg(pc.hunger()).arg(hb.countAt(0)).arg(burps).arg(drinks);
        }
        // (3) 名面。
        {
            Hotbar hb;
            const bool nameOk = hb.nameForBlock(int(RecipeRegistry::AppleId)) == QStringLiteral("苹果");
            ok = ok && nameOk;
            if (!nameOk) diag += QStringLiteral("[name]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2085b apple food column (the apple answers four hunger as a non drinkable"
               " food with the golden apple and the melon slice rows beside it, a real"
               " controller eating the held apple advances hunger by four and consumes the"
               " stack with the food burp and no potion drink signal, and the name row"
               " answers the apple)"
            << (ok ? QString() : diag);
    });

    // ── r2085c:金苹果食物柱(NEG-2 敏感面 = finishEating applyStatusEffect 行)─────────────────
    //   真链进食 rig(饥饿 +4 + 消耗 1 件) + Regen I 30s 效果快照(EffectRegeneration + level 1 +
    //   seconds 带 [25,30]) + 再生脉冲(healed 恰至少一次于 2.5s 时钟) + 创造门面(Creative 挂零 +
    //   不消耗)。
    runLeg("r2085c golden apple food column (a real controller eating the held golden apple"
        " advances hunger by four and gains regeneration one for thirty seconds with the"
        " heal pulse arriving on the two point five second clock, and the creative mode gate"
        " keeps the effect off and the stack whole)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 生存真链:饥饿 +4 + 效果快照 + 再生脉冲(NEG-2 敏感面:摘 applyStatusEffect 行 →
        //     快照恒无 EffectRegeneration = FAIL)。
        {
            World w;
            initRosterAppleWorld(w);
            layRosterApplePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            QVariantList lastEffects;
            QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                             [&](const QVariantList &l) { lastEffects = l; });
            int heals = 0;
            QObject::connect(&pc, &PlayerController::healed, &pc, [&](int) { ++heals; });
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 2 /* Survival */);
            pc.setHunger(8);
            pc.tick();
            hb.setStack(0, int(RecipeRegistry::GoldenAppleId), 1, 0); // 金苹果上手
            pc.beginEating();
            driveRosterAppleTicks(pc, 44);  // 进食满(32 拍)+ 12 拍效果推进
            pc.endEating();
            driveRosterAppleTicks(pc, 60);  // 追加 3.0s:再生累积跨 2.5s 首脉冲
            bool regenOn = false;
            int regenLevel = 0, regenSecs = 0;
            for (const QVariant &v : lastEffects) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("type")).toInt() == int(PlayerState::EffectRegeneration)) {
                    regenOn = true;
                    regenLevel = m.value(QStringLiteral("level")).toInt();
                    regenSecs = m.value(QStringLiteral("seconds")).toInt();
                }
            }
            const bool eatOk = pc.hunger() == 12                        // +4 精确
                && hb.countAt(0) == 0                                   // 生存消耗 1 件
                && regenOn                                              // Regen 面(NEG-2 摘行 → 恒 false)
                && regenLevel == 1                                      // I 级
                && regenSecs >= 25 && regenSecs <= 30                   // 30s 档(快照时刻 ~26-30)
                && heals >= 1;                                          // 2.5s 脉冲到达
            ok = ok && eatOk;
            if (!eatOk) diag += QStringLiteral("[eat h=%1 n=%2 on=%3 lv=%4 s=%5 he=%6]")
                .arg(pc.hunger()).arg(hb.countAt(0)).arg(regenOn)
                .arg(regenLevel).arg(regenSecs).arg(heals);
        }
        // (2) 创造门面(Creative:applyStatusEffect Survival 门内置 → 挂零;创造不消耗)。
        {
            World w;
            initRosterAppleWorld(w);
            layRosterApplePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            QVariantList lastEffects;
            QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                             [&](const QVariantList &l) { lastEffects = l; });
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 1 /* Creative */);
            pc.tick();
            hb.setStack(0, int(RecipeRegistry::GoldenAppleId), 1, 0);
            pc.beginEating();
            driveRosterAppleTicks(pc, 44);
            pc.endEating();
            driveRosterAppleTicks(pc, 6);
            bool creativeClean = true;
            for (const QVariant &v : lastEffects)
                if (v.toMap().value(QStringLiteral("type")).toInt() == int(PlayerState::EffectRegeneration))
                    creativeClean = false;
            creativeClean = creativeClean && hb.countAt(0) == 1;        // 创造不消耗
            ok = ok && creativeClean;
            if (!creativeClean) diag += QStringLiteral("[creative fx=%1 n=%2]")
                .arg(lastEffects.size()).arg(hb.countAt(0));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2085c golden apple food column (a real controller eating the held golden"
               " apple advances hunger by four and gains regeneration one for thirty seconds"
               " with the heal pulse arriving on the two point five second clock, and the"
               " creative mode gate keeps the effect off and the stack whole)"
            << (ok ? QString() : diag);
    });

    // ── r2085d:合成柱(8 金锭环命中 + 环孪生零污染 + 心片权威负例)─────────────────────────────
    //   {GoldIngot:8, Apple:1} 命中 1 金苹果(3×3 仅工作台;2×2 负例) + 四环孪生各行其答(熔炉/
    //   地图/画作/闪烁西瓜——多重集零污染) + 心片权威负例(金锭环+瓜片心 / 金锭环+金粒心 → 无行)。
    runLeg("r2085d craft column (the eight gold ingot ring with the apple core answers one"
        " golden apple at the three by three table only, the furnace ring and the paper ring"
        " and the painting ring and the glistering melon ring keep their own answers, and a"
        " melon slice core or a nugget core under the ingot ring answers nothing)", [&]() {
        bool ok = true;
        QString diag;
        const int GI = int(RecipeRegistry::GoldIngotId);
        const int AP = int(RecipeRegistry::AppleId);
        const int GN = int(RecipeRegistry::GoldNuggetsId);
        const int MS = int(RecipeRegistry::MelonSliceId);
        const int PA = int(RecipeRegistry::PaperId);
        const int CO = int(BR::Cobble);
        const int ST = int(RecipeRegistry::StickId);
        const int WO = int(BR::Wool);
        // (1) 命中面:8 金锭环 + 1 苹果心 → 1 金苹果(3×3 门:2×2 负例)。
        const int gGA[9] = { GI, GI, GI, GI, AP, GI, GI, GI, GI };
        const auto *rGA = RecipeRegistry::match(gGA, 3);
        const bool craftOk = rGA && rGA->outputId == int(RecipeRegistry::GoldenAppleId)
            && rGA->outputCount == 1
            && RecipeRegistry::match(gGA, 2) == nullptr;                 // 3×3 仅工作台门
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft id=%1]")
            .arg(rGA ? rGA->outputId : -1);
        // (2) 环孪生零污染(四环各行其答——多重集/逐格 id 比对零遮蔽)。
        const int gFurn[9]  = { CO, CO, CO, CO, 0,  CO, CO, CO, CO };    // 熔炉:8 圆石缺心
        const int gMap[9]   = { PA, 0,  PA, PA, 0,  PA, PA, 0,  PA };    // 地图:8 纸缺心
        const int gPaint[9] = { ST, ST, ST, ST, WO, ST, ST, ST, ST };    // 画作:8 棒+心毛
        const int gGlist[9] = { GN, GN, GN, GN, MS, GN, GN, GN, GN };    // 闪烁西瓜:8 金粒+心片
        const auto *rFurn  = RecipeRegistry::match(gFurn, 3);
        const auto *rMap   = RecipeRegistry::match(gMap, 3);
        const auto *rPaint = RecipeRegistry::match(gPaint, 3);
        const auto *rGlist = RecipeRegistry::match(gGlist, 3);
        const bool twinOk = rFurn && rFurn->outputId == int(BR::Furnace)
            && rMap && rMap->outputId == int(RecipeRegistry::EmptyMapId)
            && rPaint && rPaint->outputId == int(RecipeRegistry::PaintingId)
            && rGlist && rGlist->outputId == int(RecipeRegistry::GlisteringMelonId)
            && rGlist->outputId != int(RecipeRegistry::GoldenAppleId);
        ok = ok && twinOk;
        if (!twinOk) diag += QStringLiteral("[twin f=%1 m=%2 p=%3 g=%4]")
            .arg(rFurn ? rFurn->outputId : -1).arg(rMap ? rMap->outputId : -1)
            .arg(rPaint ? rPaint->outputId : -1).arg(rGlist ? rGlist->outputId : -1);
        // (3) 心片权威负例(金锭环 + 异心 → 无行——逐格 id 权威,同形异料不撞)。
        int gMelonCore[9] = { GI, GI, GI, GI, MS, GI, GI, GI, GI };      // 金锭环+瓜片心
        int gNuggetCore[9] = { GI, GI, GI, GI, GN, GI, GI, GI, GI };     // 金锭环+金粒心
        const bool coreOk = RecipeRegistry::match(gMelonCore, 3) == nullptr
            && RecipeRegistry::match(gNuggetCore, 3) == nullptr;
        ok = ok && coreOk;
        if (!coreOk) diag += QStringLiteral("[core]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2085d craft column (the eight gold ingot ring with the apple core answers"
               " one golden apple at the three by three table only, the furnace ring and the"
               " paper ring and the painting ring and the glistering melon ring keep their"
               " own answers, and a melon slice core or a nugget core under the ingot ring"
               " answers nothing)"
            << (ok ? QString() : diag);
    });

    // ── r2085e:结构钉族(NEG 双摘面全豁免 = 值面 + 源钉对照腿)────────────────────────────────
    //   值面(双 id / 填充地图·牛奶桶原位 / 图集零变更 / kMc -1 双行 / maxStack 64 / 名面 / 调色板)
    //   + 源钉族(recipe.h/.cpp + hotbar + playercontroller .h/.cpp[NEG-1 emit 行与 NEG-2
    //   applyStatusEffect 行豁免不钉] + MaterialIcon + CMake) + 四负面钉(无 Absorption / 无
    //   kMc 行 / 无进食时长加成符号 / 消亡+战利品双零苹果)。
    runLeg("r2085e structure pin family (the two ids ride the material segment tail with the"
        " filled map and the milk bucket in place and the atlas unchanged, the recipe and"
        " hotbar and controller and icon and cmake rows are pinned on file with the two"
        " negative lesion faces exempt, no absorption symbol exists per the one point one"
        " plus read, no kmc mapping row exists for either id, no eat duration bonus symbol"
        " exists, and the decay path stays apple free while the loot table answers only"
        " the golden apple row)", [&]() {
        bool ok = true;
        QString diag;
        // (E1) 值面:双 id 段位 / 原位邻族 / 图集零变更(物品零新瓦) / kMc -1 / maxStack 64 /
        //      名面 / 调色板双 id 在册。
        {
            Hotbar hbPal;
            const QVariantList pal = hbPal.creativeMaterials();
            const bool enumOk = int(RecipeRegistry::AppleId) == 0x292
                && int(RecipeRegistry::GoldenAppleId) == 0x293
                && int(RecipeRegistry::FilledMapId) == 0x291             // 段尾追加序(填充地图原位)
                && int(RecipeRegistry::MilkBucketId) == 0x28F
                && int(BR::AtlasTileCount) == 210                        // 物品面零新瓦(t1113 尾钉原值)
                && int(BR::Count) == 162
                && int(BR::StandingSign) == 160 && int(BR::WallSign) == 161
                && RecipeRegistry::mcMaterialId(int(RecipeRegistry::AppleId)) == -1
                && RecipeRegistry::mcMaterialId(int(RecipeRegistry::GoldenAppleId)) == -1
                && hbPal.maxStackSize(int(RecipeRegistry::AppleId)) == 64
                && hbPal.maxStackSize(int(RecipeRegistry::GoldenAppleId)) == 64
                && hbPal.nameForBlock(int(RecipeRegistry::GoldenAppleId)) == QStringLiteral("金苹果")
                && pal.contains(QVariant(int(RecipeRegistry::AppleId)))
                && pal.contains(QVariant(int(RecipeRegistry::GoldenAppleId)));
            ok = ok && enumOk;
            if (!enumOk) diag += QStringLiteral("[enum]");
        }
        // (E2) 源钉族(NEG-1 emit 行 / NEG-2 applyStatusEffect 行均豁免不钉——恰红归因全权在 a/c)。
        {
            const QString srcDir = srcRootForRosterApplePins();
            const QStringList missRh = pinSet(srcDir + QStringLiteral("/Game/recipe.h"), {
                SrcPin("apple id decl", "static constexpr int AppleId       = 0x292;", 1),
                SrcPin("golden id decl", "static constexpr int GoldenAppleId = 0x293;", 1)});
            ok = ok && missRh.isEmpty();
            if (!missRh.isEmpty())
                diag += QStringLiteral("[rh %1]").arg(missRh.join(QLatin1Char(',')));
            // 裁定锚注(t1115 实读裁定表在 recipe.h 行注——注释体锚走 raw 含,pinSet 剥注释失配)。
            const bool rulingAnchor = rawContainsRosterApple(srcDir + QStringLiteral("/Game/recipe.h"),
                                                             QStringLiteral("t1115"));
            ok = ok && rulingAnchor;
            if (!rulingAnchor) diag += QStringLiteral("[rhAnchor]");
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("golden apple recipe row", "RecipeRegistry::GoldenAppleId, 1, 1, \"golden_apple\" },", 1),
                SrcPin("apple id assert", "RecipeRegistry::AppleId       == 0x292", 1),
                SrcPin("golden id assert", "RecipeRegistry::GoldenAppleId == 0x293", 1)});
            ok = ok && missRc.isEmpty();
            if (!missRc.isEmpty())
                diag += QStringLiteral("[rc %1]").arg(missRc.join(QLatin1Char(',')));
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("apple name row", "RecipeRegistry::AppleId)            return QStringLiteral(\"苹果\")", 1),
                SrcPin("golden name row", "RecipeRegistry::GoldenAppleId)      return QStringLiteral(\"金苹果\")", 1),
                SrcPin("palette apple row", "int(RecipeRegistry::AppleId),", 1),
                SrcPin("palette golden row", "int(RecipeRegistry::GoldenAppleId)", 1)});
            ok = ok && missHb.isEmpty();
            if (!missHb.isEmpty())
                diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
            const QStringList missPcH = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("apple denom row", "static constexpr int kLeafAppleDropDenom = 200;", 1),
                SrcPin("golden regen row", "static constexpr float kGoldenAppleRegenDurationSec = 30.0f;", 1),
                SrcPin("drop leaf decl", "void dropLeafDrops(int x, int y, int z, quint8 leafId);", 1)});
            ok = ok && missPcH.isEmpty();
            if (!missPcH.isEmpty())
                diag += QStringLiteral("[pcH %1]").arg(missPcH.join(QLatin1Char(',')));
            // 效果枚举 verbatim 钉（StatusEffect 单点权威在 playerstate.h——无 Absorption 符号的
            //   枚举面锁：枚举行逐字在册即 Absorption 不在枚举内）。
            //   [lawful 修订 t1120] 枚举尾追加 EffectInstantHeal（喷溅瞬间治疗纯映射位，非时序效果
            //   不入快照——Absorption 面仍不在枚举内，本钉锁面语义不变；枚举尾前移沿革注，
            //   t1097→t1099→t1120 同门）。
            //   [lawful 修订 t1122] 枚举尾再追加 EffectInstantDamage（喷溅瞬间伤害纯映射位，同门
            //   纪律——Absorption 面仍不在枚举内，本钉锁面语义不变；枚举尾前移沿革续，t1120→t1122）。
            const QStringList missPsH = pinSet(srcDir + QStringLiteral("/Game/playerstate.h"), {
                SrcPin("effect enum verbatim", "enum StatusEffect { EffectNone = 0, EffectPoison, EffectSlowness, EffectFire, EffectSpeed, EffectStrength, EffectFireResistance, EffectRegeneration, EffectWeakness, EffectInstantHeal, EffectInstantDamage };", 1)});
            ok = ok && missPsH.isEmpty();
            if (!missPsH.isEmpty())
                diag += QStringLiteral("[psH %1]").arg(missPsH.join(QLatin1Char(',')));
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("apple food row", "if (itemId == RecipeRegistry::AppleId)       return 4;", 1),
                SrcPin("golden food row", "if (itemId == RecipeRegistry::GoldenAppleId) return 4;", 1),
                SrcPin("drop leaf impl head", "void PlayerController::dropLeafDrops(int x, int y, int z, quint8 leafId)", 1),
                SrcPin("drop leaf call row", "dropLeafDrops(x, y, z, brokenId);", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
            const QStringList missMi = pinSet(srcDir + QStringLiteral("/ui/MaterialIcon.qml"), {
                SrcPin("apple case row", "case 0x292: drawRedApple(); break", 1),
                SrcPin("golden case row", "case 0x293: drawGoldenApple(); break", 1)});
            ok = ok && missMi.isEmpty();
            if (!missMi.isEmpty())
                diag += QStringLiteral("[mi %1]").arg(missMi.join(QLatin1Char(',')));
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake section78", "tools/matrix/section78_golden_apple_t1115.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (E3) 四负面钉(1.0 实读裁定锁面):无 Absorption(1.1+ 越纪元——枚举verbatim钉 + 双文件
        //      零符号)+ 无 kMc 行(值面 -1 已断,双 id 常量不入 itemFilenameMap 面)+ 无进食时长
        //      加成符号(全食物统一 kEatDuration)+ 消亡路径零苹果 + 战利品表零**生**苹果。
        //      t1116 lawful 修订(原「战利品零苹果」缺席钉随金苹果行入池退役——缺席面随交付关闭,
        //      t1111 金苹果缺席钉退役同门):战利品表零生苹果(RawSearch 字面「RecipeRegistry::AppleId」
        //      零命中——「RecipeRegistry::GoldenAppleId」不含该子串,金苹果行是唯一苹果面)+ 金苹果
        //      行交付面(权重 1 / 恒 1 件)在册。
        {
            const QString srcDir = srcRootForRosterApplePins();
            const bool noAbsorption =
                !rawContainsRosterApple(srcDir + QStringLiteral("/Game/playerstate.h"),
                                        QStringLiteral("Absorption"))
                && !rawContainsRosterApple(srcDir + QStringLiteral("/Game/playercontroller.cpp"),
                                           QStringLiteral("Absorption"));
            ok = ok && noAbsorption;
            if (!noAbsorption) diag += QStringLiteral("[noAbsorption]");
            const bool noDuration = !rawContainsRosterApple(srcDir + QStringLiteral("/Game/playercontroller.h"),
                                                            QStringLiteral("kGoldenAppleEatDuration"))
                && !rawContainsRosterApple(srcDir + QStringLiteral("/Game/playercontroller.cpp"),
                                           QStringLiteral("GoldenAppleDuration"));
            ok = ok && noDuration;
            if (!noDuration) diag += QStringLiteral("[noDuration]");
            const bool noPlainApple =
                !rawContainsRosterApple(srcDir + QStringLiteral("/Game/loottable.cpp"),
                                        QStringLiteral("RecipeRegistry::AppleId"));
            bool goldenRow = false;
            for (const auto &e : LootTable::mineshaftChestPool())
                if (e.itemId == int(RecipeRegistry::GoldenAppleId))
                    goldenRow = e.weight == 1 && e.minCount == 1 && e.maxCount == 1;
            const bool noLootApple = noPlainApple && goldenRow;
            ok = ok && noLootApple;
            if (!noLootApple) diag += QStringLiteral("[noLoot plain=%1 golden=%2]")
                .arg(noPlainApple).arg(goldenRow);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2085e structure pin family (the two ids ride the material segment tail"
               " with the filled map and the milk bucket in place and the atlas unchanged,"
               " the recipe and hotbar and controller and icon and cmake rows are pinned on"
               " file with the two negative lesion faces exempt, no absorption symbol exists"
               " per the one point one plus read, no kmc mapping row exists for either id,"
               " no eat duration bonus symbol exists, and the decay path and the loot table"
               " stay apple free)"
            << (ok ? QString() : diag);
    });
}