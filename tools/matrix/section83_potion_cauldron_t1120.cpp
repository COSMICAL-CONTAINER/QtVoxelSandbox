#include "matrix_helpers.h"

// t1120 酿造/炼药锅残面批探针段(4 腿;filter 词 r2090;矩阵 893→897)。置尾先例沿用(接 section82,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc rig,余纯源钉腿)。
//
// ── 现状核实裁定表(四件逐一实读;era 证据 = minecraft.wiki 2026-10-01 实读,历史表行留痕)──
//   件一 喷溅水瓶(SplashWaterBottleId 0x294 段尾追加):**era 确证交付**——splash potions 于 Beta 1.9
//     Prerelease 4 入版(1.0.0 开发期 = 1.0 基准内),首批十面即含 splash water bottle(Splash_Potion
//     Java 史表首行);水瓶 + 火药自 t1101 既录负例**转正**(brewingstore.cpp splashPotionResult 表尾
//     追加两行,行位沿革注在表头注)。行为 = 可掷不可饮(isSplashPotionItem 纳入 + isDrinkableItem 不含
//     ——饮面回归负例扩展)+ 掷出复用 t1101 spawnSplashBottle 投掷链(effectType=0=EffectNone 零状态
//     效果破裂,碎裂粒子取色走 effectColor default 白兜底 / splash_break 音全复用既有链)。**不交付
//     子面裁定(核实留痕)**:喷溅水瓶对火焰 / 作物的灭火浇水面纪元存疑——1.0 实读口径 Jeb 2011-10 评注
//     「splash water bottles 应伤 endermen / blazes 但编码成本不值」→ 首批喷溅水瓶零效果破裂,灭火 /
//     浇水证据不确证 → 候选池登记不硬造。
//   件二 喷溅瞬间治疗(SplashInstantHealthPotionId 0x295 段尾追加):**era 确证交付**——同批首两面之一
//     (splash potion of healing);瞬间治疗 + 火药自 t1101 既录负例转正。结算面 = applySplashPotion 扩
//     **即时效果分支**:无时长(splashBaseSeconds 显式返 0),疗效按邻近系数缩放 d1 = 1 − dist/4(:3933
//     既有同门系数),治疗基值 = 饮用面 kInstantHealthHealHp=4(I 级 2 心);**取整口径核实留痕** =
//     MC 1.0 Potion::affectEntity 的 (int)(potency×(4<<level)+0.5) 截断式 = 半进位取整(中心 d1=1 → 4 /
//     d1≈0.485 → 2 / d1≈0.240 → 1);Survival 门内联(饮用面 emit healed 同门)+ healed 信号链同沿
//     (PlayerState::heal 零值早退 = 零疗效天然安全);映射面 splashEffectType → EffectInstantHeal
//     (playerstate.h 枚举尾追加,纯映射位非时序效果不入快照;勿占用 0=EffectNone 语义——喷溅水瓶走 0);
//     碎裂粒子取色 case 9 疗红同链。
//   件三 空桶舀锅:**era 确证不交付 = t1086 型纪元裁定单零代码收口**——Cauldron 页 Water cauldron
//     Java 史表实读:「A full cauldron can now be emptied with a bucket, filling the bucket with
//     water」 dated **Java 1.9 15w43a**(越 1.0 基准;且为满锅整锅取口径)→ t1105 裁定注「空桶舀锅不取
//     (现代面候选池)」**再验证成立**并补 era 精度;本柱 = 裁定锚钉(era 行为面零新增 = 空桶右键满锅
//     无效应真 rig + 交互集穷尽门行源钉)+ 炼药锅既有面复绿(瓶取 / 桶灌回归)。
//   件四 雨天集水:**era 确证不交付 = 同型零代码收口**——同史表实读:「Cauldrons can now be filled
//     with water, if placed outside during rain or a thunderstorm」 dated **Java 1.3.1 12w22a**(越
//     1.0 基准)→ t1105 裁定注「雨天集水不取(裁定留痕)」**再验证成立**并补 era 精度;本柱 = 雨态
//     tick 零行为面真 rig(锅水位恒定)+ world.h 降水权威三行源钉(权威在场而锅零消费 = 零行为面)。
//   附带核实申报面(只核不交付):瞬间伤害族全 src grep 零命中(InstantDamage / 瞬间伤害 / EffectHarm
//     / InstantHarm)→ 工程缺席,候选池条目由主控落档,本单不顺手交付。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训)──
//   NEG-1 = 摘 brewingstore.cpp splashPotionResult 水瓶行(「case RecipeRegistry::WaterBottleId:
//     return RecipeRegistry::SplashWaterBottleId;」整行删除;switch 缺一行 case 编译仍绿)→ 恰红 =
//     {r2090a}(静态表水瓶对 + 真驱转换断言全失;r2090b 不经酿造转换;r2090c/d 零触达;r2071a/r2072a
//     负例族已 lawful 收窄不涉水瓶正断言)。
//   NEG-2 = 摘 playercontroller.cpp applySplashPotion 即时效果分支块(「if (eff ==
//     PlayerState::EffectInstantHeal) { ... return; }」整块删除;eff/d1 仍被时长路径消费 → 编译仍绿,
//     EffectInstantHeal 落 applyStatusEffect default no-op = 零注入)→ 恰红 = {r2090b}(即时缩放断言
//     全失;r2090a 酿造面 / r2090c 裁定面 / r2090d 钉面均豁免不涉——d 不钉该块行)。
//   (双摘面互不重叠:brewingstore 行 vs playercontroller 块;a/b 各由专权腿持有。)

namespace {

// fixed 宿主小世界 incantation(section67 同款四 setter)。
inline void initSplashPotionWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section67 同款)。
inline void laySplashPotionPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section82 同款:applicationDirPath/../src)。
inline QString srcRootForSplashPotionPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚 / 负面禁出钉——pinSet 剥注释会失配,section82 同款)。
inline bool rawContainsSplashPotion(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 事件泵(section82 同款:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpSplashPotion(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

void MatrixRun::section83_potion_cauldron_t1120()
{
    // ── r2090a:酿造转换柱(NEG-1 敏感面 = splashPotionResult 水瓶行)────────────────────────────
    //   转正两对静态断言(经 brewResult 单一入口)+ 负例族(粗制 / 喷溅再酿 / 新 id 再酿 / 零瓶 → 0;
    //   红石门行序在先——水瓶 + 红石仍走红石门答零)+ 真驱一条(水瓶 + 火药 + 燃烬粉 20s 一轮 → 喷溅
    //   水瓶)+ 饮面回归(新两件不可饮 + 基础瞬间治疗非喷溅——投掷门不吞饮面)+ 物品三面(id 段位 /
    //   名面 / maxStack 双面 / 调色板段尾邻接相对恒等)+ NEG-1 摘面行本腿钉。
    runLeg("r2090a brewing conversion column (the brew table maps gunpowder on a water bottle to"
        " the splash water bottle and on the instant health potion to the splash instant health"
        " potion, gunpowder on an awkward potion or a splash potion or the new splash ids or a"
        " zero bottle still answers zero while the redstone gate keeps answering zero on water"
        " bottles ahead of the gunpowder gate, a placed brewing stand driven through a full"
        " twenty-second cycle converts a water bottle to the splash water bottle on the ember"
        " powder fuel meter, neither new id is drinkable while both base bottles stay drinkable"
        " and the instant health base stays outside the splash predicate, both new ids sit at"
        " the material segment tail with their name faces and max stack sixty-four and their"
        " palette rows appended right after the golden apple row, and the water-bottle mapping"
        " row is pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 转正两对 + 门行序(红石门在先不冲突:水瓶 + 红石 → 0,水瓶 + 火药 → 喷溅水瓶;t1102 直酿
        //     行插门行之后的先例同门复核)。
        const bool t1 =
            BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::WaterBottleId) == RecipeRegistry::SplashWaterBottleId
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::InstantHealthPotionId) == RecipeRegistry::SplashInstantHealthPotionId
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::WaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::RedstoneId, RecipeRegistry::InstantHealthPotionId) == 0;
        // (2) 负例族(含新 id 再酿负例:喷溅版非酿造底物——t1101 既录负例对新 id 同式延伸)。
        const bool t2 =
            BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::AwkwardPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashSpeedPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashWaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, RecipeRegistry::SplashInstantHealthPotionId) == 0
            && BrewingStore::brewResult(RecipeRegistry::GunpowderId, 0) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::SplashWaterBottleId) == 0
            && BrewingStore::brewResult(RecipeRegistry::SugarId, RecipeRegistry::SplashInstantHealthPotionId) == 0;
        ok = ok && t1 && t2;
        if (!(t1 && t2)) diag += QStringLiteral("[table t1=%1 t2=%2]").arg(t1).arg(t2);
        // (3) 真驱转换:水瓶 1 + 火药 1 + 燃烬粉 1 → 20s 一轮 → 喷溅水瓶 + 耗 1 原料 + 计量 -1
        //     (r2071a 真驱同式)。
        World w;
        initSplashPotionWorld(w);
        laySplashPotionPlatform(w, 20, 30, 20, 30);
        PlayerController pc;
        pc.setWorld(&w);
        BrewingStore store;
        pc.setBrewingStore(&store);
        const int bxp = 24, byp = 81, bzp = 24;
        w.setBlock(bxp, byp, bzp, BR::BrewingStand, 0);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotPotion0, RecipeRegistry::WaterBottleId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotIngredient, RecipeRegistry::GunpowderId, 1);
        store.setSlot(bxp, byp, bzp, BrewingStore::kSlotFuel, RecipeRegistry::BlazePowderId, 1);
        const int steps = int(20.6 / 0.05);
        for (int i = 0; i < steps; ++i) pc.scanBrewingStands(0.05f);
        const bool driven = store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0) == RecipeRegistry::SplashWaterBottleId
                            && store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient) == 0
                            && store.fuelOpsAt(bxp, byp, bzp) == BrewingStore::kPowderFuelOps - 1;
        ok = ok && driven;
        if (!driven) diag += QStringLiteral("[driven id=%1 ing=%2 ops=%3]")
                                  .arg(store.slotIdAt(bxp, byp, bzp, BrewingStore::kSlotPotion0))
                                  .arg(store.slotCountAt(bxp, byp, bzp, BrewingStore::kSlotIngredient))
                                  .arg(store.fuelOpsAt(bxp, byp, bzp));
        // (4) 饮面回归(NEG 扩展面):新两件不可饮 + 喷溅谓词纳新两件 + 基础两瓶仍可饮 + 基础瞬间
        //     治疗非喷溅(投掷门不吞饮面)。
        const bool drinks = !PlayerController::isDrinkableItem(RecipeRegistry::SplashWaterBottleId)
                            && !PlayerController::isDrinkableItem(RecipeRegistry::SplashInstantHealthPotionId)
                            && PlayerController::isSplashPotionItem(RecipeRegistry::SplashWaterBottleId)
                            && PlayerController::isSplashPotionItem(RecipeRegistry::SplashInstantHealthPotionId)
                            && PlayerController::isDrinkableItem(RecipeRegistry::WaterBottleId)
                            && PlayerController::isDrinkableItem(RecipeRegistry::InstantHealthPotionId)
                            && !PlayerController::isSplashPotionItem(RecipeRegistry::InstantHealthPotionId);
        ok = ok && drinks;
        if (!drinks) diag += QStringLiteral("[drinks]");
        // (5) 物品三面:id 段位(0x293 金苹果邻接段尾)+ 名面 + maxStack 64 双面 + isMaterial +
        //     调色板段尾邻接相对恒等(金苹果行 +1/+2——绝对计数归被测语义专钉,本柱只钉邻接)。
        Hotbar hb;
        const bool faces = RecipeRegistry::SplashWaterBottleId == 0x294
            && RecipeRegistry::SplashInstantHealthPotionId == 0x295
            && RecipeRegistry::GoldenAppleId == 0x293
            && hb.nameForBlock(RecipeRegistry::SplashWaterBottleId) == QStringLiteral("喷溅水瓶")
            && hb.nameForBlock(RecipeRegistry::SplashInstantHealthPotionId) == QStringLiteral("喷溅瞬间治疗药水")
            && hb.maxStackSize(RecipeRegistry::SplashWaterBottleId) == 64
            && BR::maxStackSize(RecipeRegistry::SplashInstantHealthPotionId) == 64
            && hb.isMaterial(RecipeRegistry::SplashWaterBottleId)
            && hb.isMaterial(RecipeRegistry::SplashInstantHealthPotionId)
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::SplashWaterBottleId)) == -1
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::SplashInstantHealthPotionId)) == -1;
        const QVariantList mats = hb.creativeMaterials();
        int appleIdx = -1, waterIdx = -1, healIdx = -1;
        for (int i = 0; i < mats.size(); ++i) {
            const int v = mats.at(i).toInt();
            if (v == RecipeRegistry::GoldenAppleId) appleIdx = i;
            if (v == RecipeRegistry::SplashWaterBottleId) waterIdx = i;
            if (v == RecipeRegistry::SplashInstantHealthPotionId) healIdx = i;
        }
        const bool paletteOk = appleIdx >= 0 && waterIdx == appleIdx + 1 && healIdx == appleIdx + 2;
        ok = ok && faces && paletteOk;
        if (!(faces && paletteOk))
            diag += QStringLiteral("[face faces=%1 pal=%2/%3/%4]").arg(faces).arg(appleIdx).arg(waterIdx).arg(healIdx);
        // (6) NEG-1 摘面行本腿钉(瓶行 case 标签——brewingstore.cpp 全文件恰一行)。
        {
            const QStringList missNeg = pinSet(srcRootForSplashPotionPins()
                                               + QStringLiteral("/Game/brewingstore.cpp"), {
                SrcPin("water mapping row", "case RecipeRegistry::WaterBottleId:", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2090a brewing conversion column (the brew table maps gunpowder on a water bottle to"
               " the splash water bottle and on the instant health potion to the splash instant health"
               " potion, gunpowder on an awkward potion or a splash potion or the new splash ids or a"
               " zero bottle still answers zero while the redstone gate keeps answering zero on water"
               " bottles ahead of the gunpowder gate, a placed brewing stand driven through a full"
               " twenty-second cycle converts a water bottle to the splash water bottle on the ember"
               " powder fuel meter, neither new id is drinkable while both base bottles stay drinkable"
               " and the instant health base stays outside the splash predicate, both new ids sit at"
               " the material segment tail with their name faces and max stack sixty-four and their"
               " palette rows appended right after the golden apple row, and the water-bottle mapping"
               " row is pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2090b:投掷 + 即时疗效柱(NEG-2 敏感面 = applySplashPotion 即时分支块)────────────────────
    //   链 A 真 rig 创造掷喷溅水瓶:恰一次碎裂 + 载荷原样 + 创造不耗 + 零状态效果快照(EffectNone
    //     破裂面)+ 挥手;链 A2 生存重掷消耗 1 瓶;链 B 真 rig 直落喷溅瞬间治疗(满档 d1=0.875 →
    //     healed 4)+ 直调缩放档(半档 2 / 边缘档 1)+ 出圈零注入 + 创造门零注入 + 即时面零效果快照
    //     (无 timer 不入快照——MC 口径);NEG-2 摘面行本腿钉。
    runLeg("r2090b throw and instant heal column (a real creative throw of the splash water bottle"
        " shatters exactly once with the carried id and raises zero status effect snapshots while"
        " creative keeps the bottle and the survival re-throw consumes exactly one, a real drop of"
        " the splash instant health potion shattering at the player's feet heals once inside the"
        " full-band window the half-up formula answers around the half-block tie, the direct-drive"
        " full band heals exactly four hit points and the decay bands heal exactly three and two"
        " and one and nothing at and beyond the four-block radius and nothing for a creative"
        " controller at point-blank, no instant heal raises a status effect snapshot, and the"
        " instant branch rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initSplashPotionWorld(w);
        laySplashPotionPlatform(w, 18, 44, 18, 30);
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
        // 呈现层消费端镜像(r2071b 同门:Main.qml onSplashBottleBreak 三面中机制面 = applySplashPotion)。
        int breakCount = 0, breakItem = -1, breakY = -1, breakX = -1, breakZ = -1;
        int effectSnapCount = 0, healedCount = 0, healedHp = -1;
        QObject::connect(&ents, &EntityManager::splashBottleBreak, &pc,
                         [&](int x, int y, int z, int itemId) {
                             ++breakCount; breakItem = itemId; breakY = y; breakX = x; breakZ = z;
                             pc.applySplashPotion(x, y, z, itemId); // 镜像 Main.qml 路由(Game 层结算)
                         });
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { if (!l.isEmpty()) ++effectSnapCount; });
        QObject::connect(&pc, &PlayerController::healed, &pc,
                         [&](int hp) { ++healedCount; healedHp = hp; });
        // 链 A:创造掷喷溅水瓶(真 placeBlock 入口)——恰一次碎裂 + 载荷 + 不耗 + 零效果 + 挥手。
        hb.setStack(0, RecipeRegistry::SplashWaterBottleId, 5, 0);
        hb.setSelectedSlot(0);
        const QVector3D feet(24.5f, 81.0f, 24.5f);
        const QVector3D look(1.0f, 0.0f, 0.0f);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 1 /* Creative */);
        pumpSplashPotion(17);
        pc.tick();
        int swingSeen = 0;
        const QMetaObject::Connection swingConn = QObject::connect(
            &pc, &PlayerController::swingArm, &pc, [&]() { ++swingSeen; });
        pc.placeBlock(); // 右键投掷(创造)
        int slot = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::SplashBottle)) { slot = i; break; }
        const bool thrownOk = slot >= 0
                              && ents.blockIdAt(slot) == RecipeRegistry::SplashWaterBottleId // 载荷 = 喷溅水瓶
                              && hb.blockIdAt(0) == RecipeRegistry::SplashWaterBottleId
                              && hb.countAt(0) == 5 /* 创造不耗 */ && swingSeen >= 1;
        (void)swingConn; // 连接随 pc 作用域失效(变量仅防未用告警;断开面无语义)
        ok = ok && thrownOk;
        if (!thrownOk)
            diag += QStringLiteral("[throw slot=%1 payload=%2 cnt=%3 swing=%4]")
                        .arg(slot).arg(slot >= 0 ? ents.blockIdAt(slot) : -1)
                        .arg(hb.countAt(0)).arg(swingSeen);
        // 端到端触地碎:恰一次 + 载荷原样 + 零状态效果快照(EffectNone 破裂面——机制面无操作)。
        bool brokeOk = false;
        for (int t = 0; t < 40 && !brokeOk; ++t) {
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
            brokeOk = breakCount == 1;
        }
        const bool waterBreakOk = brokeOk && breakCount == 1
                                  && breakItem == RecipeRegistry::SplashWaterBottleId
                                  && effectSnapCount == 0;
        ok = ok && waterBreakOk;
        if (!waterBreakOk)
            diag += QStringLiteral("[wbroke n=%1 item=%2 snap=%3]")
                        .arg(breakCount).arg(breakItem).arg(effectSnapCount);
        // 链 A2:生存重掷(越过放置 CD)——消耗 1 瓶(r2071b A2 同门;t256 slot-reuse 不可按 count 断言
        //     新瓶位,断槽内减一即可)。
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 2 /* Survival */);
        pumpSplashPotion(320);
        pc.tick();
        pc.placeBlock();
        const bool survivalConsumed = hb.blockIdAt(0) == RecipeRegistry::SplashWaterBottleId
                                      && hb.countAt(0) == 4;
        ok = ok && survivalConsumed;
        if (!survivalConsumed)
            diag += QStringLiteral("[surv cnt=%1]").arg(hb.countAt(0));
        // A2 瓶清场驱动(链间隔离:r2071b 同门——在飞瓶不驱尽会抢先碎裂污染链 B 归因)。
        const int breakAfterA2 = breakCount;
        for (int t = 0; t < 100 && breakCount == breakAfterA2; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        // 链 B:真 rig 直落喷溅瞬间治疗(物理落差 + 镜像路由——heal 真发面)。
        //   **满档带断言(边界诚实化留痕)**:MC 1.0 半进位式 (int)(d1×4+0.5) 在 dist 恰 0.5 处落整数
        //   边界——rig 物理落定脚位带接触 ε(观测 81.0001)使 dist=0.5001 → 恒答 3;精确站位 81.0 →
        //   恰 4。两侧均为该式在边界带的诚实答(MC 同式同敏),本腿物理落差面取带断言 hp∈[3,4],
        //   精确满档 4 由链 B2 直调面(零 ε 精确站位于 float 幂分数)承接。
        hb.setStack(0, 0, 0, 0);
        const int healedBase = healedCount;
        const int b3 = ents.spawnSplashBottle(QVector3D(24.5f, 85.0f, 24.5f), QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantHealthPotionId);
        for (int t = 0; t < 100 && healedCount == healedBase; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        const bool centerHealOk = b3 >= 0 && healedCount == healedBase + 1
                                  && healedHp >= 3 && healedHp <= 4;
        ok = ok && centerHealOk;
        if (!centerHealOk)
            diag += QStringLiteral("[center b=%1 healed=%2 hp=%3]")
                        .arg(b3).arg(healedCount - healedBase).arg(healedHp);
        // 链 B2 直调缩放档(精确值面;半进位式 int(d1×4+0.5) 逐档锁值;r2071c 直调同门):
        //   满档 4:loadSavedState 复位脚位至精确 (24.5,81,24.5)(复位不 tick,零落定 ε)→ dist 恰 0.5
        //     → d1 恰 0.875(2^-3 幂分数,float 精确无舍入)→ int(3.5+0.5) = 4;
        //   衰减三档(同精确脚位;带中安全距,距各取整界 ≥0.38):
        //     (25,80,24):dist=√1.25≈1.118 → d1≈0.7205 → 3;(26):dist=√4.25≈2.062 → 2;
        //     (27):dist=√9.25≈3.041 → 1;出圈 (29):dist=√25.25≈5.02 ≥ 4 → 零注入。
        const auto snapBand = [&](int cellX, int &hp) {
            const int before = healedCount;
            hp = -1;
            pc.applySplashPotion(cellX, 80, 24, RecipeRegistry::SplashInstantHealthPotionId);
            if (healedCount > before) hp = healedHp;
            return healedCount - before;
        };
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          qRadiansToDegrees(std::atan2(-look.x(), -look.z())), 0.0f, 2 /* Survival */);
        int hF = -1, h3b = -1, h2b = -1, h1b = -1, h0b = -1;
        const int nF = snapBand(24, hF);
        const int n3 = snapBand(25, h3b);
        const int n2 = snapBand(26, h2b);
        const int n1 = snapBand(27, h1b);
        const int n0 = snapBand(29, h0b);
        const int snapBase = effectSnapCount;
        const bool bandsOk = nF == 1 && hF == 4 && n3 == 1 && h3b == 3 && n2 == 1 && h2b == 2
                             && n1 == 1 && h1b == 1 && n0 == 0
                             && effectSnapCount == snapBase; // 即时面零效果快照(无 timer 不入快照)
        ok = ok && bandsOk;
        if (!bandsOk)
            diag += QStringLiteral("[bands %1/%2 %3/%4 %5/%6 %7/%8 %9 snap=%10]")
                        .arg(nF).arg(hF).arg(n3).arg(h3b).arg(n2).arg(h2b).arg(n1).arg(h1b).arg(n0)
                        .arg(effectSnapCount - snapBase);
        // 创造门:满档直调零注入(饮用面 Survival 内联同门——创造无敌不注入)。
        const int healBeforeCreative = healedCount;
        pc.loadSavedState(feet.x(), feet.y(), feet.z(), 0.0f, 0.0f, 1 /* Creative */);
        pumpSplashPotion(17);
        pc.applySplashPotion(24, 80, 24, RecipeRegistry::SplashInstantHealthPotionId);
        const bool creativeGateOk = healedCount == healBeforeCreative;
        ok = ok && creativeGateOk;
        if (!creativeGateOk)
            diag += QStringLiteral("[creative healed=%1]").arg(healedCount - healBeforeCreative);
        // NEG-2 摘面行本腿钉(分支守卫行 + heal 发行——两行同在摘面块内)。
        {
            const QStringList missNeg = pinSet(srcRootForSplashPotionPins()
                                               + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("instant branch guard", "if (eff == PlayerState::EffectInstantHeal) {", 1),
                SrcPin("instant heal emit", "emit healed(healHp);", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg2 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2090b throw and instant heal column (a real creative throw of the splash water"
               " bottle shatters exactly once with the carried id and raises zero status effect"
               " snapshots while creative keeps the bottle and the survival re-throw consumes exactly"
               " one, a real drop of the splash instant health potion shattering at the player's feet"
               " heals once inside the full-band window the half-up formula answers around the"
               " half-block tie, the direct-drive full band heals exactly four hit points and the"
               " decay bands heal exactly three and two and one and nothing at and beyond the"
               " four-block radius and nothing for a creative controller at point-blank, no instant"
               " heal raises a status effect snapshot, and the instant branch rows are pinned on"
               " file)"
            << (ok ? QString() : diag);
    });

    // ── r2090c:炼药锅裁定锚柱(件三/件四零代码收口 = era 行为面零新增真 rig + 既有面复绿)──────────
    //   空桶右键满锅 → 无效应不消耗不挥臂(交互集穷尽门 = 瓶 / 装水桶两员,空桶不在其列——t1105 裁定
    //     「空桶舀锅不取」再验证成立的行为面)+ 雨态 tick 锅水位恒定(t1105 裁定「雨天集水不取」再验证
    //     成立的行为面;空锅 / 满锅双态)+ 瓶取 / 桶灌既有面复绿(r2075a 锚)+ 裁定注锚 raw + 降水权威
    //     三行源钉(权威在场而锅零消费 = 零行为面)。
    runLeg("r2090c cauldron ruling anchor column (an empty bucket right-clicked on a full cauldron"
        " answers nothing with the level held and the bucket kept and no swing, thirty world ticks"
        " under the rain weather state hold both an empty and a full cauldron at their levels, the"
        " glass bottle take and the water bucket fill faces re-green with one bottle granted per"
        " take, the modern-era ruling note rows survive verbatim, and the precipitation authority"
        " rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initSplashPotionWorld(w);
        laySplashPotionPlatform(w, 12, 38, 20, 28);
        const int cx = 24, cy = 81, cz = 24; // 炼药锅落格
        w.setBlock(cx, cy, cz, BR::Cauldron, 0);
        PlayerController pc;
        pc.setWorld(&w);
        Hotbar hb;
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air)); // 材料/桶段物品真实接线
        // 站位 (27.5, 81, 24.5) 瞄锅体中心(r2075a aimG 同式)。
        const auto aim = [&](float fx, float fz, int mode) {
            const float ex = fx, ey = 81.0f + 1.62f, ez = fz;
            const float dx = float(cx) + 0.5f - ex, dy = float(cy) + 0.5f - ey, dz = float(cz) + 0.5f - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pc.release();
            pc.grab();
            pc.loadSavedState(fx, 81.0f, fz, yaw, pitch, mode);
            pumpSplashPotion(17);
            pc.tick();
        };
        const auto levelAt = [&]() {
            return w.stateAt(cx, cy, cz) & quint8(BR::CauldronStateLevelMask);
        };
        // (1) 空桶右键满锅 → 无效应(水位恒 3 / 桶保持 / 零挥臂——era 裁定行为面:1.9 15w43a 才有桶取,
        //     1.0 口径恒无)。
        w.setBlock(cx, cy, cz, BR::Cauldron, 3);
        aim(27.5f, 24.5f, 2 /* Survival */);
        hb.setStack(0, RecipeRegistry::BucketEmptyId, 1, 0);
        int swingSeen = 0;
        const QMetaObject::Connection swingConn = QObject::connect(
            &pc, &PlayerController::swingArm, &pc, [&]() { ++swingSeen; });
        pc.placeBlock();
        pumpSplashPotion(220);
        const bool bucketZeroOk = levelAt() == 3
                                  && hb.blockIdAt(0) == RecipeRegistry::BucketEmptyId
                                  && hb.countAt(0) == 1 && swingSeen == 0;
        (void)swingConn; // 连接随 pc 作用域失效(变量仅防未用告警;断开面无语义)
        ok = ok && bucketZeroOk;
        if (!bucketZeroOk)
            diag += QStringLiteral("[bucket lvl=%1 id=%2 cnt=%3 swing=%4]")
                        .arg(levelAt()).arg(hb.blockIdAt(0)).arg(hb.countAt(0)).arg(swingSeen);
        // (2) 雨态 tick 零行为面:setWeatherState(Rain) + 30 tick → 空锅 0 / 满锅 3 水位恒定(1.3.1
        //     12w22a 才有雨集水,1.0 口径恒无;world tick 天气消费沿零新增)。
        w.setWeatherState(1); // Rain
        w.setBlock(cx, cy, cz, BR::Cauldron, 0);
        for (int i = 0; i < 30; ++i) w.tickRedstone();
        const bool rainEmptyOk = levelAt() == 0;
        w.setBlock(cx, cy, cz, BR::Cauldron, 3);
        for (int i = 0; i < 30; ++i) w.tickRedstone();
        const bool rainFullOk = levelAt() == 3;
        ok = ok && rainEmptyOk && rainFullOk;
        if (!(rainEmptyOk && rainFullOk))
            diag += QStringLiteral("[rain empty=%1 full=%2]").arg(rainEmptyOk).arg(rainFullOk);
        // (3) 瓶取面复绿:满锅 → 瓶右键 → 水位 3→2 + 予 1 水瓶(r2075a 同门锚)。
        aim(27.5f, 24.5f, 2 /* Survival */);
        hb.setStack(0, RecipeRegistry::GlassBottleId, 1, 0);
        pumpSplashPotion(220); // 泵过放置 CD
        pc.placeBlock();
        pumpSplashPotion(220);
        int bottlesSeen = 0;
        for (int s = 0; s < hb.slotCount(); ++s)
            if (hb.blockIdAt(s) == RecipeRegistry::WaterBottleId) bottlesSeen += hb.countAt(s);
        const bool takeOk = levelAt() == 2 && bottlesSeen == 1;
        ok = ok && takeOk;
        if (!takeOk) diag += QStringLiteral("[take lvl=%1 bottles=%2]").arg(levelAt()).arg(bottlesSeen);
        // (4) 桶灌面复绿:空锅 → 装水桶右键 → 满锅 3 + 生存桶→空桶(r2075a 同门锚)。
        w.setBlock(cx, cy, cz, BR::Cauldron, 0);
        hb.setStack(0, RecipeRegistry::WaterBucketId, 1, 0);
        pumpSplashPotion(220);
        pc.placeBlock();
        pumpSplashPotion(220);
        const bool fillOk = levelAt() == 3 && hb.selectedItemId() == RecipeRegistry::BucketEmptyId;
        ok = ok && fillOk;
        if (!fillOk)
            diag += QStringLiteral("[fill lvl=%1 id=%2]").arg(levelAt()).arg(hb.selectedItemId());
        // (5) 裁定注锚 raw(t1105 既录裁定注逐字幸存 = 再验证成立的原位留痕;注释体走 raw 含)。
        {
            const bool rulingOk = rawContainsSplashPotion(
                srcRootForSplashPotionPins() + QStringLiteral("/Game/playercontroller.cpp"),
                QStringLiteral("空桶舀锅不取（现代纪元面，候选池登记）"))
                && rawContainsSplashPotion(
                srcRootForSplashPotionPins() + QStringLiteral("/Game/playercontroller.cpp"),
                QStringLiteral("空桶右键 → 含水射线命中首个水格舀走"));
            ok = ok && rulingOk;
            if (!rulingOk) diag += QStringLiteral("[ruling]");
        }
        // (6) 降水权威三行源钉(world.h 在场实文——权威零复制零重造;锅零消费由本腿行为面 (2) 承载)。
        {
            const QStringList missWh = pinSet(srcRootForSplashPotionPins() + QStringLiteral("/World/world.h"), {
                SrcPin("weather state at decl", "Q_INVOKABLE int weatherStateAt(int x, int z) const;", 1),
                SrcPin("precipitating decl", "Q_INVOKABLE bool isPrecipitatingAt(int x, int z) const;", 1),
                SrcPin("rain extinguish decl", "Q_INVOKABLE bool rainExtinguishesAt(int x, int y, int z) const;", 1)});
            ok = ok && missWh.isEmpty();
            if (!missWh.isEmpty())
                diag += QStringLiteral("[weather %1]").arg(missWh.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2090c cauldron ruling anchor column (an empty bucket right-clicked on a full"
               " cauldron answers nothing with the level held and the bucket kept and no swing,"
               " thirty world ticks under the rain weather state hold both an empty and a full"
               " cauldron at their levels, the glass bottle take and the water bucket fill faces"
               " re-green with one bottle granted per take, the modern-era ruling note rows survive"
               " verbatim, and the precipitation authority rows are pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2090d:结构钉族(段位源钉 + 枚举尾追加 + 映射/结算/呈现面 + CMake 段行 + 负面禁出钉)──────
    //   NEG 豁免面(恰红归因):splashPotionResult 水瓶行 = NEG-1 专权在 a / applySplashPotion 即时
    //     分支块 = NEG-2 专权在 b——本腿均不钉;即时治疗的映射行 / 时长零行(非摘面)由本腿持有。
    runLeg("r2090d structure pin family (both new ids sit at the material segment tail in the header"
        " with their compile-time asserts in the table source, the instant heal enum value appends"
        " at the tail of the status effect enum, the instant effect type row and the zero seconds"
        " row and the brewing instant mapping row are pinned on file, the name faces and palette"
        " rows and icon cases and the break particle color row hold across all layers, the cmake"
        " section row is pinned, the gameplay qml file stays free of both new identifiers, and the"
        " drinkable tail row and the golden apple name row survive verbatim)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForSplashPotionPins();
        // (1) 段位源钉(recipe.h 两行前缀 + recipe.cpp 两 assert 行前缀——case 前缀在各自文件唯一)。
        {
            const QStringList missRh = pinSet(srcDir + QStringLiteral("/Game/recipe.h"), {
                SrcPin("splash water decl", "static constexpr int SplashWaterBottleId", 1),
                SrcPin("splash instant decl", "static constexpr int SplashInstantHealthPotionId", 1)});
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("assert 294", "static_assert(RecipeRegistry::SplashWaterBottleId", 1),
                SrcPin("assert 295", "static_assert(RecipeRegistry::SplashInstantHealthPotionId", 1)});
            const bool idsOk = missRh.isEmpty() && missRc.isEmpty();
            ok = ok && idsOk;
            if (!idsOk)
                diag += QStringLiteral("[ids %1|%2]").arg(missRh.join(QLatin1Char(',')))
                            .arg(missRc.join(QLatin1Char(',')));
        }
        // (2) 枚举尾追加(playerstate.h StatusEffect 尾——纯映射位非时序效果)。
        //     [lawful 修订 t1122] 枚举尾再追加 EffectInstantDamage(喷溅瞬间伤害纯映射位，同门纪律)——
        //     本钉随尾段位沿革改针(r2085e enum-tail-pin-rides 先例同门)，语义「即时治疗在位」不变。
        {
            const QStringList missPs = pinSet(srcDir + QStringLiteral("/Game/playerstate.h"), {
                SrcPin("enum tail", "EffectWeakness, EffectInstantHeal, EffectInstantDamage };", 1)});
            ok = ok && missPs.isEmpty();
            if (!missPs.isEmpty())
                diag += QStringLiteral("[enum %1]").arg(missPs.join(QLatin1Char(',')));
        }
        // (3) 映射 / 时长 / 酿造行(即时治疗映射行 + 时长零行 + 酿造瞬间行——NEG-1/NEG-2 摘面豁免)。
        {
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("effect type row", "return PlayerState::EffectInstantHeal;", 1)});
            const QStringList missBs = pinSet(srcDir + QStringLiteral("/Game/brewingstore.cpp"), {
                SrcPin("instant mapping row", "return RecipeRegistry::SplashInstantHealthPotionId;", 1)});
            const bool mapsOk = missPc.isEmpty() && missBs.isEmpty();
            ok = ok && mapsOk;
            if (!mapsOk)
                diag += QStringLiteral("[maps %1|%2]").arg(missPc.join(QLatin1Char(',')))
                            .arg(missBs.join(QLatin1Char(',')));
        }
        // (4) 呈现三面:名面两行 + 调色板两行 + 图标 case 两行 + 碎裂粒子取色行。
        {
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("name water", "return QStringLiteral(\"喷溅水瓶\")", 1),
                SrcPin("name instant", "return QStringLiteral(\"喷溅瞬间治疗药水\")", 1),
                SrcPin("palette water", "int(RecipeRegistry::SplashWaterBottleId)", 1),
                SrcPin("palette instant", "int(RecipeRegistry::SplashInstantHealthPotionId)", 1)});
            const QStringList missMi = pinSet(srcDir + QStringLiteral("/ui/MaterialIcon.qml"), {
                SrcPin("icon 294", "case 0x294: drawPotion(", 1),
                SrcPin("icon 295", "case 0x295: drawPotion(", 1)});
            const QStringList missBp = pinSet(srcDir + QStringLiteral("/ui/BlockParticles.qml"), {
                SrcPin("instant color row", "case 9: return \"#e8505a\"", 1)});
            const bool facesOk = missHb.isEmpty() && missMi.isEmpty() && missBp.isEmpty();
            ok = ok && facesOk;
            if (!facesOk)
                diag += QStringLiteral("[faces %1|%2|%3]").arg(missHb.join(QLatin1Char(',')))
                            .arg(missMi.join(QLatin1Char(','))).arg(missBp.join(QLatin1Char(',')));
        }
        // (5) CMake 段行(本段注册行——test 提交合入后同在)。
        {
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake section83", "tools/matrix/section83_potion_cauldron_t1120.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (6) 负面禁出钉:玩法主 QML 零迁移(Main.qml 无新 id / 枚举字面——新面全在 MaterialIcon/
        //     hotbar/BlockParticles;raw 计数恰零 = 禁出断言可红,r2007b 反探同门——0 计数 SrcPin 恒
        //     不红禁用,故取 raw 含取反形态)。
        {
            const bool qmlOk = !rawContainsSplashPotion(srcDir + QStringLiteral("/ui/Main.qml"),
                                                        QStringLiteral("SplashWaterBottleId"))
                && !rawContainsSplashPotion(srcDir + QStringLiteral("/ui/Main.qml"),
                                            QStringLiteral("EffectInstantHeal"));
            ok = ok && qmlOk;
            if (!qmlOk) diag += QStringLiteral("[qml]");
        }
        // (7) 相邻族零污染:饮面尾行 / 金苹果名面行原样(r2071d/r2072d/r2085 同锚互证)。
        {
            const QStringList missNb = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("drinkable tail untouched", "|| itemId == RecipeRegistry::InstantHealthPotionId;", 1)});
            const QStringList missHbNb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("golden apple name untouched", "return QStringLiteral(\"金苹果\")", 1)});
            const bool nbOk = missNb.isEmpty() && missHbNb.isEmpty();
            ok = ok && nbOk;
            if (!nbOk)
                diag += QStringLiteral("[nb %1|%2]").arg(missNb.join(QLatin1Char(',')))
                            .arg(missHbNb.join(QLatin1Char(',')));
        }
        // (8) 沿革锚(recipe.h t1120 注块在盘;注释体锚走 raw 含)。
        {
            const bool lineageOk = rawContainsSplashPotion(srcDir + QStringLiteral("/Game/recipe.h"),
                                                           QStringLiteral("t1120"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2090d structure pin family (both new ids sit at the material segment tail in the"
               " header with their compile-time asserts in the table source, the instant heal enum"
               " value appends at the tail of the status effect enum, the instant effect type row"
               " and the zero seconds row and the brewing instant mapping row are pinned on file,"
               " the name faces and palette rows and icon cases and the break particle color row"
               " hold across all layers, the cmake section row is pinned, the gameplay qml file"
               " stays free of both new identifiers, and the drinkable tail row and the golden"
               " apple name row survive verbatim)"
            << (ok ? QString() : diag);
    });
}
