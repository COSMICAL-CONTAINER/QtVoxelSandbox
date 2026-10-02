#include "matrix_helpers.h"

// t1125 小件合集探针段(4 腿;filter 词 r2095;矩阵 912→916)。置尾先例沿用(接 section87,runAll 末执行,
//   rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc / drop / 瞪视 rig,余纯源钉腿)。
//
// ── 件一 瞬间治疗 1.0 原值勘正 4→6(era-first 纪律执行)────────────────────────────────────────
//   依据(t1122 jar 旁证 → t1125 主控全权裁定勘正执行,t1115 golden apple 1.0 原值先例同门):
//     **era 真值本单独立复核**(不止读 t1122 记录)——build/era_100_unpacked_t1122/ 官方 1.0.0 client
//     jar(shaanchor b679fea2,t1121/t1122 同锚)全量 javap 反汇编(928 类 → build/t1125_jar_disasm/,
//     Potion 类 abg 单文件 build/t1125_jar_abg.txt):abg.a(nq,nq,int,double)〔= Potion::affectEntity〕
//     治疗分支字节码 `dload potency; bipush 6; iload level; ishl; i2d; dmul; ldc2_w 0.5d; dadd; d2i;
//     istore; invokevirtual nq.a_:(I)V〔= heal〕`——**治疗基值 = 6<<level(3 心,I 级)jar 实证**,与伤害
//     分支 `pm.l〔DamageSource.magic〕+ (6<<level)` 同式对称;旧值 4 = 现代 wiki I 级口径(t1097-era
//     交付早于 era-取证纪律成熟)。旧 4 值带 4/3/2/1/0 退役,新 6 值带(半进位逐档实算):
//     d1=1 → 6 / 0.875 → 5 / 0.75 → 5 / 0.625 → 4 / 0.5 → 3 / 0.375 → 2 / 0.25 → 2 / 0.125 → 1;
//     rig 几何带(脚位 24.5,81,24.5 直调 24/25/26/27/29 格)= 5/4/3/1/0(r2090b lawful 修订同带)。
//   波及面全链 lawful(逐处携沿革注):①playercontroller.h 常量行 4→6(行注 era 锚 + 沿革注);
//     ②applySplashPotion 治疗分支(int(d1×6+0.5),代码经常量自动跟随,注释带重推);③饮用面
//     finishEating healed(kInstantHealthHealHp) 常量引用自动跟随(grep 全 src「heal(4」「×4+0.5」
//     「回 4HP」族零硬编码残留——驯服宠物 4HP 喂食回血为不相关同形字面,零触达);④名面/注释族
//     hotbar 两行 + recipe.h 行 + MaterialIcon case 行 4HP→6HP;⑤jar 旁证注(playercontroller.cpp
//     头注 t1122 段 + section85 头注)更新为「已勘正执行」口径;⑥矩阵腿 r2069b 饮面值断言与腿文 +
//     r2090b 带 4/3/2/1/0 → 5/4/3/1/0 + section67 常量行钉 4→6(全部 lawful 修订携沿革注)。
//   附带核实:金苹果再生面零波及(kGoldenAppleRegenDurationSec=30 再生族独立常量,r2095a 幸存钉)。
//
// ── 件二 南瓜灯佩戴面 era 四问裁定 = **零代码裁定单收口**(核心问 era 确证「不可戴」,t1120 件三/件四
//    先例;era 证据 = 本单全量反汇编工件 build/t1125_jar_disasm/,三 gate 类逐字节码留痕)──────────
//   问① 1.0 可戴南瓜灯头上?**era 确证 = 否**——装备槽接受谓词类 pi(SlotArmor,extends vv〔Slot〕,
//     ctor (gd〔ContainerPlayer〕, de, int, int, int, int armorType)):isItemValid 字节码 =
//     `instanceof agi〔ItemArmor〕→ armorType == slot;else itemID〔acy.bM〕== yy.ba.bM〔Block
//     .pumpkin.blockID = 86〕&& slot == 0`——**南瓜本牌(86)头盔位特收,南瓜灯 yy.bf(91)零引用**;
//     全 jar 引用面复核:yy.bf 仅 afp(方块注册数组)/ago/ny/sl 三类引用,无一在装备/瞪视/糊面 gate
//     (引用面清单 build/t1125_jar_disasm 反汇编 grep 留痕)。工程 armorSlotAccepts 南瓜行即 era 忠实
//     现状,零代码(t1121 谓词幸存钉归本段 c 腿)。
//   问② 佩戴发光机制?**era 缺席**——1.0 无玩家位置动态光源机制(光引擎 = 方块静态光照;手持火把
//     亦不发光),南瓜灯光 15 权威面 = **放置方块**面(t1105 已交付,yy 注册 litpumpkin fconst_1 亮度
//     在案);佩戴发光子面无 era 依据,且因问①不可戴而物理不可达 → 候选池登记不硬造,工程动态光源
//     缺席 = era 忠实。
//   问③ 瞪视压制同盖南瓜灯?**era 确证 = 否**——夜行者(末影人)类 aii(extends zo,carriable 布尔
//     数组 c:[Z] 在案确认)瞪视判定法 d(vi〔EntityPlayer〕)首分支:`armorInventory[3] != null &&
//     itemID == yy.ba.bM〔86〕→ return false〔压制〕`——**仅南瓜本牌,无 yy.bf**。工程守卫
//     m_playerHeadBlock == Pumpkin 即 era 忠实,零代码;本段 c 腿以 152 注入不压制行为面 + 守卫行钉
//     承载裁定锚。
//   问④ 第一人称糊面同覆南瓜灯?**era 确证 = 否**——HUD 类 qd(extends ht〔GuiComponent〕,引用
//     /gui/icons.png 与 %blur%/misc/pumpkinblur.png)糊面 gate 字节码:`thirdPersonView == 0〔ki.E〕&&
//     (helmet = x.e(3)) != null && helmet.itemID〔dk.c〕== yy.ba.bM〔86〕`——**仅南瓜本牌**。
//     工程 Main.qml pumpkinVision visible 谓词 headBlockId === 100 即 era 忠实,零代码(幸存钉归
//     本段 c 腿;r2090d 负面禁出钉不涉 152 字面——本单零 QML 迁移)。
//   **裁定门结论**:核心问①era 确证「不可戴」→ 件二零代码收口(era 证据行钉 + 裁定注 + 工程
//     三面南瓜独行现状幸存钉);不硬造任何 152 佩戴/压制/糊面/发光行为。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训;双摘面互不重叠;双 NEG 均编译仍绿形)──────────
//   NEG-1 = 值替换 applySplashPotion 治疗分支常量消费(playercontroller.cpp「int(d1 * float(
//     kInstantHealthHealHp) + 0.5f)」→「int(d1 * 0 + 0.5f)」;常量仍被饮用行消费 → 编译仍绿零告警;
//     healed(0) 落 PlayerState::heal 零值早退 = 零疗效,行为面 = 喷溅治疗全带归零)→ 恰红 = 双元
//     {r2090b, r2095b}(如实双元声明,t1124 NEG-2 双元先例同门:r2090b 行为带面 + r2095b 行为带面与
//     摘面行钉;两者对喷溅治疗带各持断言,恰红归因 = 带断言全失 + 钉面失)。r2069b 豁免(饮面不经
//     本行)/ r2095a 豁免(不钉该行)/ r2095c/d 豁免(零触达)。
//   NEG-2 = 值替换瞪视压制守卫取值式(entitymanager.cpp「const bool stareSuppressed =
//     m_playerHeadBlockValid && m_playerHeadBlock == int(BlockRegistry::Pumpkin);」整式改 false 常量,
//     守卫变量与消费点原地幸存 → 编译仍绿 = 压制永不答真;t1121 NEG-1 同形)→ 恰红 = 双元
//     {r2091b, r2095c}(r2091b 瞪视行为断言全失 + raw 钉失;r2095c 守卫行钉失——152 不压制行为面在
//     摘面后恰仍绿[压制恒 false → 不压制恒真],恰红归因 = 钉面失,不与 r2091b 行为面重叠虚增)。
//     r2095a/b/d 豁免(零触达)。
//   (双摘面互不重叠:playercontroller.cpp 治疗分支值 vs entitymanager.cpp 守卫式;b/c 各由专权腿持有。)

namespace {

// fixed 宿主小世界 incantation(section83 同款四 setter;seed 83/84/91 与既族解耦)。
inline void initHealValueWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section83 同款)。
inline void layHealValuePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section82 同款:applicationDirPath/../src)。
inline QString srcRootForHealValuePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚 / 措辞勘正面——pinSet 剥注释会失配,section82 同款)。
inline bool rawContainsHealValue(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 事件泵(section82 同款:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpHealValue(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 瞪视 rig 搭建(section84 setupPumpkinStareRig 同门:石坪 + 夜行者 spawnMobTypedYaw 固定朝向 +
//   setWanderFrozen 冻结游走 + 40 tick 沉降 + 头位 0.72 系数镜像 + 玩家眼位 -Z 侧 3 格视线 +Z 直对;
//   seed/坐标自定解耦,几何同式)。
inline void setupHealValueStareRig(World &w, EntityManager &em, int &nw, QVector3D &eye, int sx, int sz)
{
    initHealValueWorld(w, 91);
    layHealValuePlatform(w, sx - 8, sx + 8, sz - 8, sz + 8);
    em.setWanderFrozen(true);
    nw = em.spawnMobTypedYaw(sx, 82, sz, EntityManager::MobNightwalker,
                             QStringLiteral("#2a1f2a"), 10, 3.14159265f);
    const QVector3D far(-1000.0f, 90.0f, -1000.0f);
    for (int t = 0; t < 40; ++t)
        em.tick(0.016, &w, far, 0.3f, 1.8f, true);
    const QVector3D pos = em.posAt(nw);
    const float halfH = em.halfHeightAt(nw);
    const QVector3D head(pos.x(), pos.y() + halfH * 0.72f, pos.z());
    eye = QVector3D(head.x(), head.y(), head.z() - 3.0f);
    em.setPlayerSight(eye, QVector3D(0.0f, 0.0f, 1.0f));
}

// 瞪视窗驱动(section84 drivePumpkinStare 同门:至多 maxTicks 帧(16ms/帧)内返回是否激怒)。
inline bool driveHealValueStare(EntityManager &em, World &w, int nw, const QVector3D &eye,
                                int maxTicks)
{
    for (int t = 0; t < maxTicks; ++t) {
        em.tick(0.016, &w, eye, 0.3f, 1.8f, true);
        if (em.enragedAt(nw))
            return true;
    }
    return false;
}

} // namespace

void MatrixRun::section88_heal_value_jack_t1125()
{
    // ── r2095a:治疗饮用柱(件一勘正面 = 饮毕恰 6HP;NEG-1 豁免面 = 不钉喷溅分支行)──────────────────
    //   真链喝满(r2069b 同门 rig):生存饮瞬间治疗药水 → healed 恰一次 6HP(勘正后常量引用自动跟随面)
    //     + 快照零持续项(即时效果无 timer)+ 返 1 空瓶 + 槽内耗 1;金苹果再生面零波及(时长常量行 +
    //     分流行幸存钉——再生族独立常量不随勘正);常量行 =6 钉 + 饮用发行行钉 + 头注 era 锚 raw 钉
    //     (6<<level 在场 = 勘正沿革注锚)。
    runLeg("r2095a instant heal drink column (a real survival player drinking the instant health"
        " potion through the full eat chain emits exactly one six-point heal with no persistent"
        " effect entry left in the snapshot and one empty bottle returned with the slot consuming"
        " exactly one, the golden apple regeneration face stays untouched with its own thirty"
        " second duration constant and dispatch row pinned, the corrected constant row answers"
        " six with the drink emission row and the header era anchor holding, and the old"
        " four-point caliber leaves no hardcoded residue on the drink face)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initHealValueWorld(w, 83);
        layHealValuePlatform(w, 18, 44, 18, 30);
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
        int healedSum = 0, healedCount = 0;
        QObject::connect(&pc, &PlayerController::healed, &pc, [&](int hp) {
            ++healedCount; healedSum += hp;
        });
        int lastSize = -1;
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { lastSize = l.size(); });
        hb.setStack(0, RecipeRegistry::InstantHealthPotionId, 2, 0);
        pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
        pumpHealValue(320); // 越过放置 CD
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
        pc.tick(); // 一帧推进 → 快照刷新(瞬间治疗不产生任何持续项)
        // 勘正核心断言:饮毕恰 6HP(t1125 4→6;旧 4 值律此必红)。
        const bool instantOk = eatingStarted
                               && healedCount - healedBase == 1 && healedSum == 6 // 恰一次 6HP(t1125 勘正)
                               && lastSize <= 0 // 无 timer → 快照恒空(空→空不发信号,lastSize 留 -1 同过)
                               && bottlesA == 1 // 返 1 空瓶
                               && hb.blockIdAt(0) == RecipeRegistry::InstantHealthPotionId
                               && hb.countAt(0) == 1; // 槽内耗 1
        ok = ok && instantOk;
        if (!instantOk) diag += QStringLiteral("[instant eat=%1 hc=%2 hs=%3 size=%4 b=%5 cnt=%6]")
                                    .arg(eatingStarted).arg(healedCount - healedBase).arg(healedSum)
                                    .arg(lastSize).arg(bottlesA).arg(hb.countAt(0));
        // 金苹果再生面零波及(勘正不触再生族——时长常量行 + 食毕分流行幸存钉)。
        const QString pcH = srcRootForHealValuePins() + QStringLiteral("/Game/playercontroller.h");
        const QString pcC = srcRootForHealValuePins() + QStringLiteral("/Game/playercontroller.cpp");
        const QStringList missGold = pinSet(pcH, {
            SrcPin("golden apple duration row",
                   "static constexpr float kGoldenAppleRegenDurationSec = 30.0f;", 1)});
        const QStringList missGoldCpp = pinSet(pcC, {
            SrcPin("golden apple dispatch row",
                   "if (eatenId == RecipeRegistry::GoldenAppleId) {", 1)});
        ok = ok && missGold.isEmpty() && missGoldCpp.isEmpty();
        if (!missGold.isEmpty() || !missGoldCpp.isEmpty())
            diag += QStringLiteral("[gold %1 %2]").arg(missGold.join(QLatin1Char(',')))
                        .arg(missGoldCpp.join(QLatin1Char(',')));
        // 勘正结构钉:常量行 =6 + 饮用发行行 + 头注 era 锚 raw(6<<level 在场)。
        const QStringList missConst = pinSet(pcH, {
            SrcPin("heal constant row six", "static constexpr int   kInstantHealthHealHp = 6;", 1)});
        const QStringList missDrink = pinSet(pcC, {
            SrcPin("drink emit row", "emit healed(kInstantHealthHealHp);", 1)});
        const bool eraAnchor = rawContainsHealValue(pcH, QStringLiteral("6<<level"));
        ok = ok && missConst.isEmpty() && missDrink.isEmpty() && eraAnchor;
        if (!missConst.isEmpty() || !missDrink.isEmpty() || !eraAnchor)
            diag += QStringLiteral("[pins %1 %2 era=%3]").arg(missConst.join(QLatin1Char(',')))
                        .arg(missDrink.join(QLatin1Char(','))).arg(eraAnchor);

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2095a instant heal drink column (a real survival player drinking the instant"
               " health potion through the full eat chain emits exactly one six-point heal with no"
               " persistent effect entry left in the snapshot and one empty bottle returned with"
               " the slot consuming exactly one, the golden apple regeneration face stays"
               " untouched with its own thirty second duration constant and dispatch row pinned,"
               " the corrected constant row answers six with the drink emission row and the"
               " header era anchor holding, and the old four-point caliber leaves no hardcoded"
               " residue on the drink face)"
            << (ok ? QString() : diag);
    });

    // ── r2095b:喷溅新带柱(NEG-1 敏感面 = 治疗分支常量消费行本腿钉 + 行为带面)─────────────────────
    //   [t1125 lawful 修订同源] 勘正后新带:真 rig 直落恰 5(ε 两侧同值,半进位 4/5 界移 dist=1/3)+
    //     直调缩放档 5/4/3/1/0(r2090b lawful 修订同带互证)+ 创造门零注入 + 即时面零效果快照;
    //     NEG-1 摘面行本腿钉(分支守卫行 + 常量消费行 + heal 发行行)。
    runLeg("r2095b splash heal new band column (a real drop of the splash instant health potion"
        " shattering at the player's feet heals exactly five hit points on both sides of the"
        " landing epsilon, the direct-drive bands answer five four three one and nothing at and"
        " beyond the four-block radius mirroring the revised neighbor band, the creative gate"
        " injects nothing at point-blank, no instant heal raises a status effect snapshot, and"
        " the instant branch rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initHealValueWorld(w, 84);
        layHealValuePlatform(w, 18, 44, 18, 30);
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
        pc.setSelectedBlock(int(BR::Air));
        int effectSnapCount = 0, healedCount = 0, healedHp = -1;
        QObject::connect(&ents, &EntityManager::splashBottleBreak, &pc,
                         [&](int x, int y, int z, int itemId) {
                             pc.applySplashPotion(x, y, z, itemId); // 镜像 Main.qml 路由(Game 层结算)
                         });
        QObject::connect(&pc, &PlayerController::activeEffectsChanged, &pc,
                         [&](const QVariantList &l) { if (!l.isEmpty()) ++effectSnapCount; });
        QObject::connect(&pc, &PlayerController::healed, &pc,
                         [&](int hp) { ++healedCount; healedHp = hp; });
        // 生存落位(物理落定 ε 同 r2090b 观测形——带断言两侧同值恒 5,落点带内不敏)。
        const QVector3D feet(24.5f, 81.0f, 24.5f);
        pc.loadSavedState(feet.x(), feet.y(), feet.z(), 0.0f, 0.0f, 2 /* Survival */);
        pumpHealValue(320);
        pc.tick();
        // 链 B:真 rig 直落喷溅瞬间治疗(物理落差 + 镜像路由——heal 真发面)。
        const int healedBase = healedCount;
        const int b3 = ents.spawnSplashBottle(QVector3D(24.5f, 85.0f, 24.5f), QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantHealthPotionId);
        for (int t = 0; t < 100 && healedCount == healedBase; ++t)
            ents.tick(0.05f, &w, farL, 0.3f, 1.8f, false);
        // 勘正新值:ε 两侧(0.5001 / 0.5)均恰 5(半进位式 int(d1×6+0.5) 的 4/5 取整界 = dist 1/3,
        //   观测落点带内安全距;旧 4 值律此必红)。
        const bool centerHealOk = b3 >= 0 && healedCount == healedBase + 1 && healedHp == 5;
        ok = ok && centerHealOk;
        if (!centerHealOk)
            diag += QStringLiteral("[center b=%1 healed=%2 hp=%3]")
                        .arg(b3).arg(healedCount - healedBase).arg(healedHp);
        // 链 B2 直调缩放档(精确值面;半进位式 int(d1×6+0.5) 逐档锁值;r2090b 修订带同门互证):
        //   满档 5:loadSavedState 复位精确 (24.5,81,24.5) → dist 恰 0.5 → d1 恰 0.875 → int(5.25+0.5)=5;
        //   衰减三档:(25,80,24):dist=√1.25≈1.118 → d1≈0.7205 → 4;(26):√4.25≈2.062 → 3;
        //   (27):√9.25≈3.041 → 1;出圈 (29):√25.25≈5.02 ≥ 4 → 零注入。
        const auto snapBand = [&](int cellX, int &hp) {
            const int before = healedCount;
            hp = -1;
            pc.applySplashPotion(cellX, 80, 24, RecipeRegistry::SplashInstantHealthPotionId);
            if (healedCount > before) hp = healedHp;
            return healedCount - before;
        };
        pc.loadSavedState(feet.x(), feet.y(), feet.z(),
                          0.0f, 0.0f, 2 /* Survival */);
        int hF = -1, h3b = -1, h2b = -1, h1b = -1, h0b = -1;
        const int nF = snapBand(24, hF);
        const int n3 = snapBand(25, h3b);
        const int n2 = snapBand(26, h2b);
        const int n1 = snapBand(27, h1b);
        const int n0 = snapBand(29, h0b);
        const int snapBase = effectSnapCount;
        const bool bandsOk = nF == 1 && hF == 5 && n3 == 1 && h3b == 4 && n2 == 1 && h2b == 3
                             && n1 == 1 && h1b == 1 && n0 == 0 // t1125 勘正新带 5/4/3/1/0
                             && effectSnapCount == snapBase; // 即时面零效果快照(无 timer 不入快照)
        ok = ok && bandsOk;
        if (!bandsOk)
            diag += QStringLiteral("[bands %1/%2 %3/%4 %5/%6 %7/%8 %9 snap=%10]")
                        .arg(nF).arg(hF).arg(n3).arg(h3b).arg(n2).arg(h2b).arg(n1).arg(h1b).arg(n0)
                        .arg(effectSnapCount - snapBase);
        // 创造门:满档直调零注入(饮用面 Survival 内联同门——创造无敌不注入)。
        const int healBeforeCreative = healedCount;
        pc.loadSavedState(feet.x(), feet.y(), feet.z(), 0.0f, 0.0f, 1 /* Creative */);
        pumpHealValue(17);
        pc.applySplashPotion(24, 80, 24, RecipeRegistry::SplashInstantHealthPotionId);
        const bool creativeGateOk = healedCount == healBeforeCreative;
        ok = ok && creativeGateOk;
        if (!creativeGateOk)
            diag += QStringLiteral("[creative healed=%1]").arg(healedCount - healBeforeCreative);
        // NEG-1 摘面行本腿钉(分支守卫行 + 常量消费行 + heal 发行行)。
        {
            const QStringList missNeg = pinSet(srcRootForHealValuePins()
                                               + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("instant branch guard", "if (eff == PlayerState::EffectInstantHeal) {", 1),
                SrcPin("instant heal formula row",
                       "const int healHp = int(d1 * float(kInstantHealthHealHp) + 0.5f);", 1),
                SrcPin("instant heal emit", "emit healed(healHp);", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2095b splash heal new band column (a real drop of the splash instant health"
               " potion shattering at the player's feet heals exactly five hit points on both"
               " sides of the landing epsilon, the direct-drive bands answer five four three one"
               " and nothing at and beyond the four-block radius mirroring the revised neighbor"
               " band, the creative gate injects nothing at point-blank, no instant heal raises a"
               " status effect snapshot, and the instant branch rows are pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2095c:件二裁定锚柱(era 四问零代码收口 = 工程三面南瓜独行现状幸存钉 + 152 行为面)──────────
    //   装备谓词面:南瓜(100)入头盔位真 / 错位拒;南瓜灯(152)**全槽位拒**(era 问①不可戴——工程谓词
    //     即 era 忠实现状);armorSetStack 写入门同谓词(152 写入 no-op / 南瓜整栈保真)。瞪视面:南瓜
    //     注入压制真(r2091b 专权面复锚)+ 南瓜灯 152 注入**不压制**(era 问③——守卫 id 字面独行即
    //     era 忠实);NEG-2 摘面行本腿钉(守卫取值式行)。呈现面钉:pumpkinVision visible 行 +
    //     playerArmorPumpkinHead visible 行(era 问④糊面南瓜独行;152 字面零 QML 迁移——r2090d 负面
    //     禁出钉不涉)。id 本牌幸存钉:JackOLantern = 152(t1105 光源族既存,本单零新 id)。
    runLeg("r2095c jack o lantern wearable ruling anchor column (the acceptance predicate admits"
        " the pumpkin on the helmet slot only and rejects the jack o lantern on every armor slot"
        " with the write door mirroring the predicate so a jack o lantern write stays a no-op"
        " while the pumpkin stack round-trips, a pumpkin head injection suppresses the nightwalker"
        " stare while a jack o lantern injection never suppresses, the pumpkin vision and the"
        " third person pumpkin head rows stay pinned on the pumpkin literal alone, the jack o"
        " lantern block id row holds at one hundred fifty two, and the suppression guard row is"
        " pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 装备谓词面(真链 Hotbar 直驱;152 全槽拒 = era 问①裁定行为面)。
        Hotbar hb;
        const int pumpkin = int(BR::Pumpkin);
        const int jack = int(BR::JackOLantern); // 152 本牌(t1105 既存,零新 id)
        const bool predOk =
            hb.armorSlotAccepts(0, pumpkin)
            && !hb.armorSlotAccepts(1, pumpkin) && !hb.armorSlotAccepts(2, pumpkin)
            && !hb.armorSlotAccepts(3, pumpkin)
            && !hb.armorSlotAccepts(0, jack) && !hb.armorSlotAccepts(1, jack)
            && !hb.armorSlotAccepts(2, jack) && !hb.armorSlotAccepts(3, jack)
            && hb.armorSlotAccepts(0, 0) // 清空恒真
            && !hb.armorSlotAccepts(0, int(BR::Glass)); // 非瓜方块拒(对照)
        ok = ok && predOk;
        if (!predOk) diag += QStringLiteral("[pred %1]").arg(predOk);
        // 写入门同谓词:152 写入 no-op / 南瓜整栈 round-trip 保真。
        hb.armorSetStack(0, jack, 3, 0, QVariantList(), QString());
        const bool jackWriteRejected = hb.armorBlockIdAt(0) == 0;
        hb.armorSetStack(0, pumpkin, 5, 0, QVariantList(), QString());
        const bool pumpkinRoundTrip = hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 5;
        hb.armorSetStack(0, 0, 0, 0, QVariantList(), QString());
        const bool unequipOk = hb.armorBlockIdAt(0) == 0;
        ok = ok && jackWriteRejected && pumpkinRoundTrip && unequipOk;
        if (!(jackWriteRejected && pumpkinRoundTrip && unequipOk))
            diag += QStringLiteral("[write jack=%1 pump=%2 uneq=%3]")
                        .arg(jackWriteRejected).arg(pumpkinRoundTrip).arg(unequipOk);
        // (2) 瞪视面:南瓜注入压制真(r2091b 专权面复锚)+ 152 注入不压制(era 问③)。
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupHealValueStareRig(w, em, nw, eye, 24, 24);
            em.setPlayerHeadBlock(pumpkin);
            const bool supEnraged = driveHealValueStare(em, w, nw, eye, 500);
            ok = ok && !supEnraged;
            if (supEnraged) diag += QStringLiteral("[pumpkin enraged=true]");
        }
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupHealValueStareRig(w, em, nw, eye, 28, 24);
            em.setPlayerHeadBlock(jack); // 152 头位注入(era 物理不可达形态——守卫 id 字面直验)
            const bool jackEnraged = driveHealValueStare(em, w, nw, eye, 250);
            ok = ok && jackEnraged;
            if (!jackEnraged) diag += QStringLiteral("[jack enraged=false]");
        }
        // (3) 裁定锚钉族:armorSlotAccepts 南瓜行 + 瞪视守卫行(NEG-2 摘面行本腿钉)+ 双 QML visible 行
        //     + id 本牌行。
        const QString root = srcRootForHealValuePins();
        const QStringList missHb = pinSet(root + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("armor pumpkin row", "return slot == 0 && itemId == int(BlockRegistry::Pumpkin);", 1)});
        const QStringList missEm = pinSet(root + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("stare guard row", "&& m_playerHeadBlock == int(BlockRegistry::Pumpkin);", 1)});
        const QStringList missQml = pinSet(root + QStringLiteral("/ui/Main.qml"), {
            SrcPin("pumpkin vision row",
                   "visible: window.appState === \"playing\" && headBlockId === 100", 1),
            SrcPin("pumpkin head row", "visible: headId === 100", 1)});
        const QStringList missBr = pinSet(root + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("jack o lantern id row", "JackOLantern     = 152,", 1)});
        ok = ok && missHb.isEmpty() && missEm.isEmpty() && missQml.isEmpty() && missBr.isEmpty();
        if (!missHb.isEmpty() || !missEm.isEmpty() || !missQml.isEmpty() || !missBr.isEmpty())
            diag += QStringLiteral("[pins %1 %2 %3 %4]").arg(missHb.join(QLatin1Char(',')))
                        .arg(missEm.join(QLatin1Char(','))).arg(missQml.join(QLatin1Char(',')))
                        .arg(missBr.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2095c jack o lantern wearable ruling anchor column (the acceptance predicate"
               " admits the pumpkin on the helmet slot only and rejects the jack o lantern on"
               " every armor slot with the write door mirroring the predicate so a jack o lantern"
               " write stays a no-op while the pumpkin stack round-trips, a pumpkin head"
               " injection suppresses the nightwalker stare while a jack o lantern injection"
               " never suppresses, the pumpkin vision and the third person pumpkin head rows stay"
               " pinned on the pumpkin literal alone, the jack o lantern block id row holds at"
               " one hundred fifty two, and the suppression guard row is pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2095d:结构钉族 + 措辞勘正面(NEG 双摘面豁免不钉——摘面行归 b/c 专权)──────────────────────
    //   段位行(CMake)+ 相邻族幸存钉(伤害常量行 = t1122 族邻行 / 治疗 id 行 / 名面行)+ 措辞勘正
    //     raw 面(4HP→6HP 四文件 + 喷溅式注释锚)+ 段位沿革锚(section85 erratum 注 raw)。
    runLeg("r2095d structure pin and wording correction column (the cmake section row holds, the"
        " neighbor instant damage constant and the instant health potion id row and the name row"
        " survive, the corrected six hit point wording family is present across the hotbar rows"
        " and the recipe row and the icon case row, the splash formula comment anchor carries the"
        " new base, and the erratum anchor note in the instant damage section holds)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForHealValuePins();
        // (1) 相邻族幸存钉(剥注释口径——真语句锚)。
        const QStringList missH = pinSet(root + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("instant damage constant row", "static constexpr int   kInstantDamageHurtHp = 6;", 1)});
        const QStringList missRh = pinSet(root + QStringLiteral("/Game/recipe.h"), {
            SrcPin("instant health id row", "static constexpr int InstantHealthPotionId  = 0x274;", 1)});
        const QStringList missHb = pinSet(root + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("heal potion name row", "return QStringLiteral(\"瞬间治疗药水\");", 1)});
        const bool cmakeOk = pinSet(QCoreApplication::applicationDirPath()
                                        + QStringLiteral("/../CMakeLists.txt"),
                                    { SrcPin("cmake section row",
                                             "tools/matrix/section88_heal_value_jack_t1125.cpp", 1)})
                                       .isEmpty();
        ok = ok && missH.isEmpty() && missRh.isEmpty() && missHb.isEmpty() && cmakeOk;
        if (!missH.isEmpty() || !missRh.isEmpty() || !missHb.isEmpty() || !cmakeOk)
            diag += QStringLiteral("[pins %1 %2 %3 cmake=%4]").arg(missH.join(QLatin1Char(',')))
                        .arg(missRh.join(QLatin1Char(','))).arg(missHb.join(QLatin1Char(',')))
                        .arg(cmakeOk);
        // (2) 措辞勘正 raw 面(注释体锚——pinSet 剥注释会失配,本面 = 勘正沿革承载)。
        const QString hbCpp = root + QStringLiteral("/Game/hotbar.cpp");
        const QString rhH = root + QStringLiteral("/Game/recipe.h");
        const QString miQml = root + QStringLiteral("/ui/MaterialIcon.qml");
        const QString pcCpp = root + QStringLiteral("/Game/playercontroller.cpp");
        const QString pcH = root + QStringLiteral("/Game/playercontroller.h");
        const bool wordingOk =
            rawContainsHealValue(hbCpp, QStringLiteral("饮毕即回 6HP")) // 调色板 + 名面两行
            && rawContainsHealValue(rhH, QStringLiteral("饮毕即回 6HP"))
            && rawContainsHealValue(miQml, QStringLiteral("饮毕即回 6HP"))
            && rawContainsHealValue(pcCpp, QStringLiteral("(int)(potency×6+0.5)")) // 头注 + 行注
            && rawContainsHealValue(pcH, QStringLiteral("t1125 jar 实证 6<<level 勘正"));
        ok = ok && wordingOk;
        if (!wordingOk) diag += QStringLiteral("[wording]");
        // (3) 段位沿革锚:section85 头注 erratum 注(治疗面旁证 → 勘正执行)在场。
        const bool erratumOk = rawContainsHealValue(
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/../tools/matrix/section85_instant_damage_t1122.cpp"),
            QStringLiteral("已升级勘正执行"));
        ok = ok && erratumOk;
        if (!erratumOk) diag += QStringLiteral("[erratum]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2095d structure pin and wording correction column (the cmake section row"
               " holds, the neighbor instant damage constant and the instant health potion id row"
               " and the name row survive, the corrected six hit point wording family is present"
               " across the hotbar rows and the recipe row and the icon case row, the splash"
               " formula comment anchor carries the new base, and the erratum anchor note in the"
               " instant damage section holds)"
            << (ok ? QString() : diag);
    });
}
