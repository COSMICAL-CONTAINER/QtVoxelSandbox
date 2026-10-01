#include "matrix_helpers.h"

// t1122 瞬间伤害族探针段(4 腿;filter 词 r2092;矩阵 901→905)。置尾先例沿用(接 section84,runAll 末执行,
//   rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc / drop rig,余纯源钉腿)。
//
// ── 四步核实裁定表(每步 era 真值实读留痕;2026-10-01)─────────────────────────────────────────
//   步一 1.0 瞬间伤害药水实有?**era 确证交付(本单核心语义面)**——2026-10-01 minecraft.wiki 与 fandom
//     镜像双端 bot 门不可达(curl 实测 challenge/超时),按 t1121 先例以**官方 1.0.0 client jar 实解包 +
//     javap 字节码反汇编**为权威(mojang version manifest v2 → 1.0 包 75062586 → client sha1
//     b679fea27f2284836202e9365e13a82552092e5d = t1121 南瓜糊面同锚,sha1 实算复核一致):① lang/en_US.lang
//     物理在场 `potion.harm=Instant Damage` + `potion.harm.postfix=of Harming`(名面)与
//     `death.magic=%1$s was killed by magic`(魔法死因面);② Potion 类(abg)静态装配 id 7 =「potion.harm」
//     bad=true 液色 4393481(暗红系),即时施放路径 `attackEntityFrom(DamageSource.magic, 6 << amplifier)`
//     ——**伤害基值 6(3 心,I 级)jar 实证**,与现代 wiki I 级值同值(era 无歧义);伤害/治疗不对称留痕:
//     jar 实证 1.0 即时两族同式 6<<level(治疗面 t1120 交付值 4 = 现代 wiki I 级口径,如实并档不翻案,
//     邻面零修订)。
//   步二 酿造行:**era 确证交付**——瞬间伤害 = 瞬间治疗 + 发酵蛛眼(腐化剂对**成品药水**改性,非经
//     awkward 链;基酒门 = 直接对 0x274 瞬间治疗酿造)。jar 证伤害药水 1.0 实有(步一)+ 腐化图谱面与
//     t1102 水瓶直酿虚弱行 / t1120 闪烁西瓜治疗行既读图谱同源(腐化剂 = 发酵蛛眼,工程既有原料);II 级
//     (辉光岩强化)1.0 实有但工程 II 级药水管线上不存在(t1101 候选池「强化二级链」既录)→ 候选池登记
//     不硬造,本单只交付 I 级族。行位段尾追加不插中间(awkward 五链之后、函数尾 return 0 之前);
//     红 / 火**门行在先**已拦截(治疗 + 红石 → 0 / 治疗 + 火药 → 喷溅治疗,t1120 红石门序先例同门)。
//   步三 喷溅面:**era 确证交付**——瞬间伤害 + 火药 → 喷溅瞬间伤害(Beta 1.9 pre4 喷溅族,t1120 既读
//     era 注在案;火药修饰门 t1101 十四行同门)。**player-only 语义 = t1101 先例同门**(工程 splash 结算
//     = 玩家侧 applySplashPotion 玩家唯一目标——喷溅伤害对掷出者自身生效,mob 面载体 = t1101 已登记
//     降级候选池,沿用如实登记)。结算 = applySplashPotion 即时分支镜像(t1120 EffectInstantHeal 分支
//     旁同门):邻近系数 d1 同门、取整口径 = jar 实证 `(int)(potency × 6 + 0.5)` 半进位(中心 d1=1 → 6 /
//     d1=0.875 → 5 / 出圈 0;伤害面 jar 与 wiki 同值,era 无歧义)。
//   步四 副作用面:**jar 实证在场、工程如实登记不硬造**——亡灵反向治疗(isEntityUndead 门内 instant
//     heal ↔ magic attack 互换,abg 类 a(nq,int) 双分支字节码实证)1.0 实有;工程 mob 无 undead 效果面
//     (t1101 降级既录)→ 候选池登记,玩家非亡灵恒伤害口径不受影响。死亡链一致性面交付:自伤致死死因
//     = 新 DeathCause::Magic(枚举尾追加;1.0 death.magic jar 物理在场实证——步一①)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训)──────────────────────────────────────────
//   NEG-1 = 摘酿造腐化行(brewingstore.cpp brewResult 内「if (bottleId == RecipeRegistry::
//     InstantHealthPotionId) return ingredientId == RecipeRegistry::FermentedSpiderEyeId ?
//     RecipeRegistry::InstantDamagePotionId : 0;」两行整枝删除;bottleId=瞬间治疗落函数尾 return 0,
//     编译仍绿、红/火门行与既有行幸存)→ 恰红 = {r2092a}(静态转换对 + 真驱断言 + 摘面行本腿钉全失;
//     r2092b 饮用面不经酿造转换;r2092c 喷溅面经火药门小表不经本枝;r2092d 钉面豁免——d 不钉该枝)。
//   NEG-2 = 摘 applySplashPotion 即时伤害分支块(playercontroller.cpp「if (eff == PlayerState::
//     EffectInstantDamage) { ... return; }」整块删除;eff/d1 仍被时长路径消费 → 编译仍绿,
//     EffectInstantDamage 落 applyStatusEffect default no-op = 零伤害注入;kInstantDamageHurtHp 仍被
//     饮用行消费 → 未用告警零)→ 恰红 = {r2092c}(碎裂 self 伤 + 直调缩放档断言全失;r2092a 酿造面 /
//     r2092b 饮用面 / r2092d 钉面均豁免不涉——d 不钉该块行,魔法伤信号声明与饮用发行行归 b/d 持有)。
//   (双摘面互不重叠:brewingstore 两行 vs playercontroller 分支块;a/c 各由专权腿持有。)

namespace {

// fixed 宿主小世界 incantation(section83 同款四 setter)。
inline void initInstantDamageWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section83 同款)。
inline void layInstantDamagePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section83 同款:applicationDirPath/../src)。
inline QString srcRootForInstantDamagePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(era 锚注 / 负面禁出钉——pinSet 剥注释会失配,section83 同款)。
inline bool rawContainsInstantDamage(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 事件泵(section83 同款:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpInstantDamage(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 真链饮满驱动(r2069b 同款:beginEating + 44×60ms tick + endEating,越 kEatDuration 1.6s)。
inline void drinkInstantDamage(PlayerController &pc)
{
    pc.beginEating();
    for (int i = 0; i < 44; ++i) {
        QThread::msleep(60);
        pc.tick();
    }
    pc.endEating();
}

} // namespace

void MatrixRun::section85_instant_damage_t1122()
{
    // ── r2092a:酿造转换柱(NEG-1 敏感面 = brewResult 腐化行)────────────────────────────────────
    //   对偶腐化静态断言(经 brewResult 单一入口)+ 门行序在先(治疗 + 红石仍 0 / 治疗 + 火药仍喷溅
    //     治疗,t1120 既钉门序不动)+ 负例族(伤害瓶再酿 / 非对偶料 / 喷溅再酿 / 零瓶 → 0)+ 真驱一条
    //     (瞬间治疗 + 发酵蛛眼 + 燃烬粉 20s 一轮 → 瞬间伤害)+ 饮面回归(新两件:饮面互斥喷溅面——
    //     伤害药水可饮 / 喷溅伤害不可饮)+ 物品三面(名面 / maxStack / isMaterial / pack 回退)+ NEG-1
    //     摘面行本腿钉。
    runLeg("r2092a brewing conversion column (the brew table corrupts an instant health potion with"
        " the fermented spider eye into the instant damage potion while the redstone gate keeps"
        " answering zero on the healing base and the gunpowder gate keeps mapping it to the splash"
        " heal ahead of the corruption row, the damage bottle rebrewing or a wrong ingredient or"
        " the splash id rebrewing or a zero bottle all answer zero, a placed brewing stand driven"
        " through a full twenty-second cycle converts the healing potion to the damage potion on"
        " the ember powder fuel meter, the drinkable and splash predicates keep the drink-splash"
        " mutual exclusion for both new ids with the base bottles intact, both new ids carry the"
        " name faces with max stack sixty-four outside the resource pack table, and the"
        " corruption row is pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 对偶腐化对 + 门行序(红石门 / 火药门在先——治疗底物两既钉映射零扰动)。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::InstantHealthPotionId) == RecipeRegistry::InstantDamagePotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::InstantHealthPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::InstantHealthPotionId) == RecipeRegistry::SplashInstantHealthPotionId;
        // (2) 负例族(伤害瓶非酿造底物 / 非对偶料 / 喷溅再酿 / 零瓶;水瓶直酿虚弱与 awkward 五链复绿)。
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::InstantDamagePotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::InstantHealthPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GlisteringMelonId, RecipeRegistry::InstantDamagePotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashInstantDamagePotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, 0) == 0
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::WaterBottleId) == RecipeRegistry::WeaknessPotionId
            && BrewingStore::brewResult(RecipeRegistry::FermentedSpiderEyeId, RecipeRegistry::AwkwardPotionId) == RecipeRegistry::WeaknessPotionId;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (3) 真驱转换:瞬间治疗 1 + 发酵蛛眼 1 + 燃烬粉 1 → 20s 一轮 → 瞬间伤害 + 耗 1 原料 +
        //     计量 -1(r2090a 真驱同式)。
        World w;
        initInstantDamageWorld(w);
        layInstantDamagePlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24;
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::InstantHealthPotionId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::FermentedSpiderEyeId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const int steps = int(20.6 / 0.05);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool driven = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::InstantDamagePotionId
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        ok = ok && driven;
        if (!driven) diag += QStringLiteral("[driven id=%1 ing=%2 ops=%3]")
                                  .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                  .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                  .arg(store.fuelOpsAt(bxp, byp, bzp));
        // (4) 饮面回归:伤害药水可饮 / 喷溅伤害不可饮 + 喷溅谓词纳新 / 基础治疗两向原样(饮-掷互斥门
        //     不吞饮面,t1120 同门)。
        const bool drinks = PlayerController::isDrinkableItem(RecipeRegistry::InstantDamagePotionId)
                            && !PlayerController::isDrinkableItem(RecipeRegistry::SplashInstantDamagePotionId)
                            && PlayerController::isSplashPotionItem(RecipeRegistry::SplashInstantDamagePotionId)
                            && !PlayerController::isSplashPotionItem(RecipeRegistry::InstantDamagePotionId)
                            && PlayerController::isDrinkableItem(RecipeRegistry::InstantHealthPotionId)
                            && !PlayerController::isSplashPotionItem(RecipeRegistry::InstantHealthPotionId);
        ok = ok && drinks;
        if (!drinks) diag += QStringLiteral("[drinks]");
        // (5) 物品三面:名面两件 + maxStack 64 双面 + isMaterial + mcMaterialId 越表界回退。
        Hotbar hb;
        const bool faces = hb.nameForBlock(RecipeRegistry::InstantDamagePotionId) == QStringLiteral("瞬间伤害药水")
            && hb.nameForBlock(RecipeRegistry::SplashInstantDamagePotionId) == QStringLiteral("喷溅瞬间伤害药水")
            && hb.maxStackSize(RecipeRegistry::InstantDamagePotionId) == 64
            && BR::maxStackSize(RecipeRegistry::SplashInstantDamagePotionId) == 64
            && hb.isMaterial(RecipeRegistry::InstantDamagePotionId)
            && hb.isMaterial(RecipeRegistry::SplashInstantDamagePotionId)
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::InstantDamagePotionId)) == -1
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::SplashInstantDamagePotionId)) == -1;
        ok = ok && faces;
        if (!faces) diag += QStringLiteral("[faces]");
        // (6) NEG-1 摘面行本腿钉(腐化行真枝——brewingstore.cpp 全文件恰一行)。
        {
            const QStringList missNeg = pinSet(srcRootForInstantDamagePins()
                                               + QStringLiteral("/Game/brewingstore.cpp"), {
                SrcPin("corruption row", "return ingredientId == RecipeRegistry::FermentedSpiderEyeId ? RecipeRegistry::InstantDamagePotionId : 0;", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2092a brewing conversion column (the brew table corrupts an instant health potion"
               " with the fermented spider eye into the instant damage potion while the redstone gate"
               " keeps answering zero on the healing base and the gunpowder gate keeps mapping it to"
               " the splash heal ahead of the corruption row, the damage bottle rebrewing or a wrong"
               " ingredient or the splash id rebrewing or a zero bottle all answer zero, a placed"
               " brewing stand driven through a full twenty-second cycle converts the healing potion"
               " to the damage potion on the ember powder fuel meter, the drinkable and splash"
               " predicates keep the drink-splash mutual exclusion for both new ids with the base"
               " bottles intact, both new ids carry the name faces with max stack sixty-four outside"
               " the resource pack table, and the corruption row is pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2092b:饮用自伤柱(真链;NEG 豁免面 = 不钉 applySplashPotion 伤害分支)─────────────────────
    //   链 A 真链生存饮满:恰一次 magicDamageTaken(6, Magic) + potionDrunk 统一沿 + 生存耗 1 + 返 1
    //     空瓶 + 零效果快照(即时效果无 timer 不入快照);链 B 死亡链:低血量饮毕 → dead + deathCause
    //     == Magic + deathCauseText 文案(1.0 death.magic jar 实证同源);链 C 创造门:零发行不耗不返;
    //     饮面两行源钉(finishEating 分流行 + isDrinkableItem case 行——非 NEG 摘面,本腿持有)。
    runLeg("r2092b drink self-harm column (a real survival player drinking the instant damage potion"
        " through the full eat chain takes exactly one magic hit of six hit points with the magic"
        " death cause carried on the signal and one burp along the drunk edge and one empty bottle"
        " returned with one potion consumed and zero persistent effect entries, a low-health drink"
        " of the second potion dies with the dead flag the magic death cause and the magic death"
        " text, a creative drink raises nothing and keeps the stack unconsumed with no bottle"
        " granted, and the drink dispatch rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initInstantDamageWorld(w);
        layInstantDamagePlatform(w, 18, 44, 18, 30);
        EntityManager ents;
        Hotbar hb;
        PlayerState ps;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线(t1030 rig 纪律)
        // 呈现层消费端镜像(Main.qml onMagicDamageTaken 同门:直走 takeDamage(hp, cause),绕甲不磨甲)。
        int magicCount = 0, lastHp = -1, lastCause = -1;
        QObject::connect(&pc, &PlayerController::magicDamageTaken, &pc,
                         [&](int hp, int cause) {
                             ++magicCount; lastHp = hp; lastCause = cause;
                             ps.takeDamage(hp, cause);
                         });
        int drunkCount = 0;
        QObject::connect(&pc, &PlayerController::potionDrunk, &pc, [&](int) { ++drunkCount; });
        int effectSnapCount = 0;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { if (!l.isEmpty()) ++effectSnapCount; });
        // 链 A:生存真链饮满(槽 0 两件)→ 恰一次 (6, Magic) + 饮毕沿 + 耗 1 + 返 1 空瓶 + 零快照。
        hb.setStack(0, RecipeRegistry::InstantDamagePotionId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pumpInstantDamage(320); // 越过放置 CD
        pc.tick();
        const int magicBase = magicCount, drunkBase = drunkCount;
        drinkInstantDamage(pc);
        int bottlesA = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesA += hb.countAt(i);
        const bool aOk = magicCount == magicBase + 1 && lastHp == int(PlayerController::kInstantDamageHurtHp)
                         && lastCause == int(PlayerState::Magic)
                         && drunkCount == drunkBase + 1
                         && hb.blockIdAt(0) == RecipeRegistry::InstantDamagePotionId && hb.countAt(0) == 1
                         && bottlesA == 1
                         && effectSnapCount == 0; // 即时效果无 timer 不入快照(MC 口径)
        ok = ok && aOk;
        if (!aOk)
            diag += QStringLiteral("[A magic=%1 hp=%2 cause=%3 drunk=%4 cnt=%5 b=%6 snap=%7]")
                        .arg(magicCount - magicBase).arg(lastHp).arg(lastCause)
                        .arg(drunkCount - drunkBase).arg(hb.countAt(0)).arg(bottlesA).arg(effectSnapCount);
        // 链 B:死亡链(第二件饮毕致死)——低血量 4 → magic 6 → dead + deathCause Magic + 文案
        //     「被魔法夺去生命」(1.0 death.magic jar 实证同源;takeDamage 死因记录链一致性)。
        ps.setHealth(4);
        hb.setStack(0, RecipeRegistry::InstantDamagePotionId, 1, 0); // 选中槽仍 0(链 A 未换槽)
        pumpInstantDamage(320); // 越过 finishEating 冷却 / eat CD
        pc.tick();
        drinkInstantDamage(pc);
        int bottlesB = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesB += hb.countAt(i);
        const bool bOk = magicCount == magicBase + 2
                         && ps.dead() && ps.deathCause() == int(PlayerState::Magic)
                         && ps.deathCauseText() == QStringLiteral("被魔法夺去生命");
        ok = ok && bOk;
        if (!bOk)
            diag += QStringLiteral("[B magic=%1 dead=%2 cause=%3 text=%4]")
                        .arg(magicCount - magicBase).arg(ps.dead()).arg(ps.deathCause())
                        .arg(ps.deathCauseText());
        // 链 C:创造门(链间隔离:dead ps 对 takeDamage 早退零扰)——零发行不耗不返。
        //     瓶基线取链 B 后现算(链 B 生存饮毕自带 +1 空瓶,链 C 门断言只看本链增量)。
        pc.release();
        pc.grab();
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 1 /* Creative */);
        pumpInstantDamage(320);
        pc.tick();
        hb.setStack(0, RecipeRegistry::InstantDamagePotionId, 3, 0);
        const int magicBeforeC = magicCount, bottlesBeforeC = bottlesB;
        drinkInstantDamage(pc);
        int bottlesC = 0;
        for (int i = 0; i < hb.slotCount(); ++i)
            if (hb.blockIdAt(i) == RecipeRegistry::GlassBottleId) bottlesC += hb.countAt(i);
        const bool cOk = magicCount == magicBeforeC
                         && hb.blockIdAt(0) == RecipeRegistry::InstantDamagePotionId && hb.countAt(0) == 3
                         && bottlesC == bottlesBeforeC;
        ok = ok && cOk;
        if (!cOk)
            diag += QStringLiteral("[C magic=%1 cnt=%2 b=%3]")
                        .arg(magicCount - magicBeforeC).arg(hb.countAt(0)).arg(bottlesC - bottlesBeforeC);
        // 饮面两行源钉(finishEating 分流行两行 + isDrinkableItem case 行——本单新增非 NEG 面)。
        {
            const QStringList missPc = pinSet(srcRootForInstantDamagePins()
                                              + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("drink dispatch row", "else if (eatenId == RecipeRegistry::InstantDamagePotionId && m_mode == Survival)", 1),
                SrcPin("drink emit row", "emit magicDamageTaken(kInstantDamageHurtHp, PlayerState::Magic);", 1),
                SrcPin("drinkable case row", "case RecipeRegistry::InstantDamagePotionId:", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pins %1]").arg(missPc.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2092b drink self-harm column (a real survival player drinking the instant damage"
               " potion through the full eat chain takes exactly one magic hit of six hit points"
               " with the magic death cause carried on the signal and one burp along the drunk edge"
               " and one empty bottle returned with one potion consumed and zero persistent effect"
               " entries, a low-health drink of the second potion dies with the dead flag the magic"
               " death cause and the magic death text, a creative drink raises nothing and keeps"
               " the stack unconsumed with no bottle granted, and the drink dispatch rows are"
               " pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2092c:喷溅柱(NEG-2 敏感面 = applySplashPotion 即时伤害分支块)──────────────────────────
    //   链 A 真 rig 直落喷溅瞬间伤害(物理落差 + 镜像路由):恰一次碎裂 + self 伤恰 5(dist 0.5 →
    //     d1 0.875 → int(5.75) 两侧稳态,heal 面的整数边界带在此式无 tie)+ 零效果快照;链 B 直调
    //     缩放档(精确值面:int(d1×6+0.5) = 6.5−1.5×dist 逐档锁值):近心档 6 / 半格档 5 / 衰减
    //     4 / 3 / 斜向档 2(dist=√8.25)/ 1 / 出圈 0;创造门直调零发行;NEG-2 摘面行本腿钉。
    runLeg("r2092c splash instant damage column (a real drop of the splash instant damage potion"
        " shattering at the player's feet takes exactly one magic hit of five hit points with no"
        " status effect snapshot raised, the direct-drive bands answer six at close center and five"
        " at the half-block distance and four and three and two and one across the decay bands and"
        " nothing at and beyond the four-block radius, a creative controller at close center takes"
        " nothing, and the instant damage branch rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initInstantDamageWorld(w);
        layInstantDamagePlatform(w, 18, 44, 18, 30);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air)); // 材料段物品真实接线(t1030 rig 纪律)
        // 呈现层消费端镜像(r2090b 同门:Main.qml onSplashBottleBreak 三面中机制面 = applySplashPotion
        //   + onMagicDamageTaken 机制面 = takeDamage(hp, cause))。
        int breakCount = 0;
        int magicCount = 0, lastHp = -1, lastCause = -1;
        QObject::connect(&ents, &EntityManager::splashBottleBreak, &pc,
                         [&](int x, int y, int z, int itemId) {
                             ++breakCount;
                             pc.applySplashPotion(x, y, z, itemId); // 镜像 Main.qml 路由(Game 层结算)
                         });
        QObject::connect(&pc, &PlayerController::magicDamageTaken, &pc,
                         [&](int hp, int cause) { ++magicCount; lastHp = hp; lastCause = cause; });
        int effectSnapCount = 0;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { if (!l.isEmpty()) ++effectSnapCount; });
        // 链 A:真 rig 直落(物理落差;脚位精确 81.0 复位不 tick 零落定 ε → dist 恰 0.5 → 恰 5)。
        pc.loadSavedState(24.5f, 81.0f, 24.5f, 0.0f, 0.0f, 2 /* Survival */);
        const int b3 = ents.spawnSplashBottle(QVector3D(24.5f, 85.0f, 24.5f), QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantDamagePotionId);
        for (int t = 0; t < 100 && magicCount == 0; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        const bool dropOk = b3 >= 0 && breakCount == 1 && magicCount == 1
                            && lastHp == 5 && lastCause == int(PlayerState::Magic)
                            && effectSnapCount == 0; // 即时面零效果快照(无 timer 不入快照)
        ok = ok && dropOk;
        if (!dropOk)
            diag += QStringLiteral("[drop b=%1 brk=%2 magic=%3 hp=%4 cause=%5 snap=%6]")
                        .arg(b3).arg(breakCount).arg(magicCount).arg(lastHp).arg(lastCause).arg(effectSnapCount);
        // 链 B 直调缩放档(精确值面;半进位式 int(d1×6+0.5) = 6.5 − 1.5×dist 逐档锁值;r2090b 同门):
        //   半格带:(24.5,81,24.5) 脚位零 ε → dist 恰 0.5 → 5;衰减档(t1120 衰减格同排):
        //     (25,80,24):dist=√1.25≈1.118 → 4;(26,80,24):dist=√4.25≈2.062 → 3;
        //     (26,80,26) 斜向:dist=√8.25≈2.872 → 2;(27,80,24):dist=√9.25≈3.041 → 1;出圈
        //     (29,80,24):dist=√20.5≈4.53 ≥ 4 → 0;近心档:脚位 (24.5,80.75,24.5)(直调复位不 tick)
        //     → dist 恰 0.25 → 6.125 → 6(各档 tie 距 ≥0.31 均稳)。
        const auto snapBand = [&](float fy, int cellX, int cellZ, int &hp) {
            const int before = magicCount;
            hp = -1;
            pc.loadSavedState(24.5f, fy, 24.5f, 0.0f, 0.0f, 2 /* Survival */);
            pc.applySplashPotion(cellX, 80, cellZ, RecipeRegistry::SplashInstantDamagePotionId);
            if (magicCount > before) hp = lastHp;
            return magicCount - before;
        };
        int d6 = -1, d5 = -1, d4 = -1, d3 = -1, d2 = -1, d1a = -1, d0 = -1;
        const int n6 = snapBand(80.75f, 24, 24, d6);
        const int n5 = snapBand(81.0f, 24, 24, d5);
        const int n4 = snapBand(81.0f, 25, 24, d4);
        const int n3 = snapBand(81.0f, 26, 24, d3);
        const int n2 = snapBand(81.0f, 26, 26, d2);
        const int n1a = snapBand(81.0f, 27, 24, d1a);
        const int n0 = snapBand(81.0f, 29, 24, d0);
        const bool bandsOk = n6 == 1 && d6 == 6 && n5 == 1 && d5 == 5 && n4 == 1 && d4 == 4
                             && n3 == 1 && d3 == 3 && n2 == 1 && d2 == 2 && n1a == 1 && d1a == 1
                             && n0 == 0;
        ok = ok && bandsOk;
        if (!bandsOk)
            diag += QStringLiteral("[bands %1/%2 %3/%4 %5/%6 %7/%8 %9/%10 %11/%12 %13]")
                        .arg(n6).arg(d6).arg(n5).arg(d5).arg(n4).arg(d4).arg(n3).arg(d3)
                        .arg(n2).arg(d2).arg(n1a).arg(d1a).arg(n0);
        // 创造门:近心档直调零发行(饮用面 Survival 内联同门——创造无敌不发行)。
        const int magicBeforeCreative = magicCount;
        pc.loadSavedState(24.5f, 80.75f, 24.5f, 0.0f, 0.0f, 1 /* Creative */);
        pc.applySplashPotion(24, 80, 24, RecipeRegistry::SplashInstantDamagePotionId);
        const bool creativeGateOk = magicCount == magicBeforeCreative;
        ok = ok && creativeGateOk;
        if (!creativeGateOk)
            diag += QStringLiteral("[creative magic=%1]").arg(magicCount - magicBeforeCreative);
        // NEG-2 摘面行本腿钉(分支守卫行 + 伤害发行行——两行同在摘面块内)。
        {
            const QStringList missNeg = pinSet(srcRootForInstantDamagePins()
                                               + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("damage branch guard", "if (eff == PlayerState::EffectInstantDamage) {", 1),
                SrcPin("damage emit row", "emit magicDamageTaken(dmgHp, PlayerState::Magic);", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg2 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2092c splash instant damage column (a real drop of the splash instant damage"
               " potion shattering at the player's feet takes exactly one magic hit of five hit"
               " points with no status effect snapshot raised, the direct-drive bands answer six at"
               " close center and five at the half-block distance and four and three and one and"
               " one across the decay bands and nothing at and beyond the four-block radius, a"
               " creative controller at close center takes nothing, and the instant damage branch"
               " rows are pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2092d:结构钉族(NEG 双摘面豁免不钉——腐化行归 a / applySplashPotion 伤害分支归 c 专权)────
    //   段位源钉(decl + assert)+ 双枚举尾追加(StatusEffect / DeathCause)+ 死因文案行 + 映射行 /
    //   时长零行 / 酿造表尾行 + 信号与常量声明行 + 呈现三面(名 / 调色板 / 图标 / 碎裂色)+ Main.qml
    //   魔法伤路由行 + CMake 段行 + era 锚注自钉(jar 证据串)+ 负面禁出钉(Main.qml 零新 id 字面——
    //   r2090d 禁出门同门扩面)+ 相邻族零污染(饮面尾行 / t1120 表行 / t1120 枚举尾钉幸存)。
    runLeg("r2092d structure pin family (both new ids sit at the material segment tail in the header"
        " with their compile-time asserts in the table source, the instant damage enum value and"
        " the magic death cause append at the tails of their enums with the magic death text row"
        " on file, the effect type row and the zero seconds row and the splash mapping row and the"
        " signal and constant declarations hold, the name faces and palette rows and icon cases"
        " and the break particle color row hold across all layers, the qml magic damage route and"
        " the cmake section row are pinned with the era jar anchors, the gameplay qml file stays"
        " free of both new identifiers, and the drinkable tail row and the splash heal mapping row"
        " survive verbatim)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForInstantDamagePins();
        // (1) 段位源钉(recipe.h 两行 decl 前缀 + recipe.cpp 两 assert 行前缀——case 前缀在各自文件唯一)。
        {
            const QStringList missRh = pinSet(srcDir + QStringLiteral("/Game/recipe.h"), {
                SrcPin("instant damage decl", "static constexpr int InstantDamagePotionId", 1),
                SrcPin("splash damage decl", "static constexpr int SplashInstantDamagePotionId", 1)});
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("assert 296", "static_assert(RecipeRegistry::InstantDamagePotionId", 1),
                SrcPin("assert 297", "static_assert(RecipeRegistry::SplashInstantDamagePotionId", 1)});
            const bool idsOk = missRh.isEmpty() && missRc.isEmpty();
            ok = ok && idsOk;
            if (!idsOk)
                diag += QStringLiteral("[ids %1|%2]").arg(missRh.join(QLatin1Char(',')))
                            .arg(missRc.join(QLatin1Char(',')));
        }
        // (2) 双枚举尾追加(StatusEffect 尾 + DeathCause 尾——纯映射位 / 尾追加纪律)+ 死因文案行。
        {
            const QStringList missPs = pinSet(srcDir + QStringLiteral("/Game/playerstate.h"), {
                SrcPin("effect enum tail", "EffectWeakness, EffectInstantHeal, EffectInstantDamage };", 1),
                SrcPin("death cause enum tail", "AbyssPearlTp, Anvil, Slime, Magic };", 1)});
            const QStringList missPsC = pinSet(srcDir + QStringLiteral("/Game/playerstate.cpp"), {
                SrcPin("magic death case", "case Magic:", 1),
                SrcPin("magic death text", "return QStringLiteral(\"被魔法夺去生命\");", 1)});
            const bool enumOk = missPs.isEmpty() && missPsC.isEmpty();
            ok = ok && enumOk;
            if (!enumOk)
                diag += QStringLiteral("[enum %1|%2]").arg(missPs.join(QLatin1Char(',')))
                            .arg(missPsC.join(QLatin1Char(',')));
        }
        // (3) 映射 / 时长 / 酿造表行 + 信号与常量声明行(NEG 摘面行豁免:腐化行归 a / 分支块归 c)。
        {
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("effect type row", "return PlayerState::EffectInstantDamage;", 1)});
            const QStringList missBs = pinSet(srcDir + QStringLiteral("/Game/brewingstore.cpp"), {
                SrcPin("splash mapping row", "return RecipeRegistry::SplashInstantDamagePotionId;", 1)});
            const QStringList missPcH = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("signal decl", "void magicDamageTaken(int hp, int cause);", 1),
                SrcPin("damage const", "static constexpr int   kInstantDamageHurtHp = 6;", 1)});
            const bool mapsOk = missPc.isEmpty() && missBs.isEmpty() && missPcH.isEmpty();
            ok = ok && mapsOk;
            if (!mapsOk)
                diag += QStringLiteral("[maps %1|%2|%3]").arg(missPc.join(QLatin1Char(',')))
                            .arg(missBs.join(QLatin1Char(','))).arg(missPcH.join(QLatin1Char(',')));
        }
        // (4) 呈现三面:名面两行 + 调色板两行 + 图标 case 两行 + 碎裂粒子取色行。
        {
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("name damage", "return QStringLiteral(\"瞬间伤害药水\")", 1),
                SrcPin("name splash damage", "return QStringLiteral(\"喷溅瞬间伤害药水\")", 1),
                SrcPin("palette damage", "int(RecipeRegistry::InstantDamagePotionId)", 1),
                SrcPin("palette splash damage", "int(RecipeRegistry::SplashInstantDamagePotionId)", 1)});
            const QStringList missMi = pinSet(srcDir + QStringLiteral("/ui/MaterialIcon.qml"), {
                SrcPin("icon 296", "case 0x296: drawPotion(", 1),
                SrcPin("icon 297", "case 0x297: drawPotion(", 1)});
            const QStringList missBp = pinSet(srcDir + QStringLiteral("/ui/BlockParticles.qml"), {
                SrcPin("instant damage color row", "case 10: return \"#c04838\"", 1)});
            const bool facesOk = missHb.isEmpty() && missMi.isEmpty() && missBp.isEmpty();
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[faces %1|%2|%3]").arg(missHb.join(QLatin1Char(',')))
                            .arg(missMi.join(QLatin1Char(','))).arg(missBp.join(QLatin1Char(',')));
        }
        // (5) Main.qml 魔法伤路由行 + CMake 段行 + era 锚注自钉(jar 证据串在本段文件裸文本在场)。
        {
            const bool routeOk = rawContainsInstantDamage(srcDir + QStringLiteral("/ui/Main.qml"),
                                                          QStringLiteral("function onMagicDamageTaken(hp, cause)"))
                && rawContainsInstantDamage(srcDir + QStringLiteral("/ui/Main.qml"),
                                            QStringLiteral("playerState.takeDamage(hp, cause)"))
                && pinSet(QCoreApplication::applicationDirPath()
                          + QStringLiteral("/../CMakeLists.txt"), {
                    SrcPin("cmake section85", "tools/matrix/section85_instant_damage_t1122.cpp", 1)}).isEmpty()
                && rawContainsInstantDamage(QCoreApplication::applicationDirPath()
                                            + QStringLiteral("/../tools/matrix/section85_instant_damage_t1122.cpp"),
                                            QStringLiteral("was killed by magic"))
                && rawContainsInstantDamage(QCoreApplication::applicationDirPath()
                                            + QStringLiteral("/../tools/matrix/section85_instant_damage_t1122.cpp"),
                                            QStringLiteral("potion.harm"));
            ok = ok && routeOk;
            if (!routeOk) diag += QStringLiteral("[route+cmake+era]");
        }
        // (6) 负面禁出钉:玩法主 QML 零迁移(Main.qml 无新 id / 枚举字面——新面全在 MaterialIcon/
        //     hotbar/BlockParticles;r2090d t1120 禁出门同门扩面,raw 计数恰零)。
        {
            const bool qmlOk = !rawContainsInstantDamage(srcDir + QStringLiteral("/ui/Main.qml"),
                                                         QStringLiteral("InstantDamagePotionId"))
                && !rawContainsInstantDamage(srcDir + QStringLiteral("/ui/Main.qml"),
                                             QStringLiteral("EffectInstantDamage"));
            ok = ok && qmlOk;
            if (!qmlOk) diag += QStringLiteral("[qml]");
        }
        // (7) 相邻族零污染:饮面尾行 / t1120 喷溅治疗映射行 / t1120 图标行原样(r2070d/r2090d 同锚互证)。
        {
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("drinkable tail untouched", "|| itemId == RecipeRegistry::InstantHealthPotionId;", 1)});
            const QStringList missBs = pinSet(srcDir + QStringLiteral("/Game/brewingstore.cpp"), {
                SrcPin("splash heal row untouched", "return RecipeRegistry::SplashInstantHealthPotionId;", 1)});
            const QStringList missMi = pinSet(srcDir + QStringLiteral("/ui/MaterialIcon.qml"), {
                SrcPin("icon 295 untouched", "case 0x295: drawPotion(", 1)});
            const bool nbOk = missPc.isEmpty() && missBs.isEmpty() && missMi.isEmpty();
            ok = ok && nbOk;
            if (!nbOk)
                diag += QStringLiteral("[nb %1|%2|%3]").arg(missPc.join(QLatin1Char(',')))
                            .arg(missBs.join(QLatin1Char(','))).arg(missMi.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2092d structure pin family (both new ids sit at the material segment tail in the"
               " header with their compile-time asserts in the table source, the instant damage enum"
               " value and the magic death cause append at the tails of their enums with the magic"
               " death text row on file, the effect type row and the zero seconds row and the splash"
               " mapping row and the signal and constant declarations hold, the name faces and"
               " palette rows and icon cases and the break particle color row hold across all"
               " layers, the qml magic damage route and the cmake section row are pinned with the"
               " era jar anchors, the gameplay qml file stays free of both new identifiers, and the"
               " drinkable tail row and the splash heal mapping row survive verbatim)"
            << (ok ? QString() : diag);
    });
}
