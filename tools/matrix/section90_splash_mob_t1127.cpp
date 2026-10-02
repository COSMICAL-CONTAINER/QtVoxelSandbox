#include "matrix_helpers.h"

// t1127 喷溅瞬间族 mob 结算面探针段(4 腿;filter 词 r2097;矩阵 920→924)。置尾先例沿用(接 section89,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc / 投掷瓶 rig,余纯源钉腿)。
//
// ── 交付面(t1101 降级载体「mob 效果注入系统」首切片;喷溅瞬间族 mob 结算 = applySplashPotion 同函数
//    分域)───────────────────────────────────────────────────────────────────────────────────────
//   era 取证四步(官方 1.0.0 client jar 全量反汇编,工件 build/t1127_jar_potion_ab.txt〔ab=EntityPotion
//   onImpact〕/ build/t1127_jar_abg_instant.txt〔abg=Potion 即时两族 + pm=DamageSource 旁甲旗〕/
//   build/t1127_jar_nq_heal_attack.txt〔nq=EntityLiving heal/attack/甲减免三原文〕留痕):
//   ①喷溅 onImpact 命中面 = 半径内**全部活体**(getEntitiesWithinAABB(nq〔EntityLiving〕, 本体 AABB
//     expand(4,2,4)) + getDistanceSqToEntity dSq<16)——含 mob 与玩家;直接命中实体 d1=1.0 子面工程
//     碎裂信号未携带命中者 → 如实降级候选(碎裂格与命中者相邻,距离带自然取近心值)。
//   ②亡灵反转真值 = Potion.a(abg.a(nq,nq,int,double)):heal×非亡灵→healEntity(nq.a_(I)) / heal×亡灵
//     →attackEntityFrom(pm.l〔magic〕) / harm×非亡灵→attackEntityFrom(pm.b=indirectMagic) / harm×亡灵
//     →healEntity;amt=(int)(d×(6<<amp)+0.5) 半进位两向对称。亡灵名册测定:era nq.av()Z 覆写族 =
//     {僵尸, 骷髅, 僵尸猪人};工程 1.0 对标 mob 无猪人 → 权威谓词 isUndeadFamily 已有名册
//     {Shambler, Bones, BabyShambler} 恰为等价覆盖(review0830 #26 单一权威,本切片零新名册)。
//   ③mob 数值面 = 同一 d1 邻近缩放 + magic 伤**绕甲**(pm.h() 置 n 旗 → nq.c 盔甲段跳过;工程 mob
//     无甲减免面=全链生伤直扣,殊途同归如实留痕) + 治疗钳上限(nq.a_(I)V health+=amt 钳 maxHealth,
//     health≤0 零操作);致死走既有死亡链(damageEntity→deathTimer→mobDied 掉落/Slime 分裂自然承接,
//     零新码);era attackEntityFrom hurtTime 差额窗(magic 不豁免)——工程 mob 无免疫帧面(damageEntity
//     直扣,全 mob 伤害路径通用降级面非本切片缺口),如实登记候选。
//   ④投掷碰撞面 = 弹丸命中实体/触地**同一 onImpact 单点结算**(工程 SplashBottle tick 命中 mob AABB
//     与触地同沿单发 splashBottleBreak——碰撞检测在场性:瓶/箭/雪球/蛋 vs mob AABB 面既有;mob 结算
//     挂碎裂沿与玩家面并行同构,选型留痕)。
//   实现要点(单一权威):半径/衰减/基值/半进位与玩家面**同函数分域同源**(kSplashRadiusBlocks /
//   kInstant*Hp / int(d1×base+0.5) 零复制;entitymanager 零 kSplashRadiusBlocks 引用=负面钉);坐标 =
//   mob 脚位到命中格中心(玩家面同基;era 为 AABB 最近点距,工程取中心距家族一致,如实登记);治疗向 =
//   EntityManager::healEntity 新通用单点(钳上限;healTamedPet 驯宠流控门语义分立不动);伤害向 =
//   damageEntity 直扣(致死既有链);创造门不在 mob 面(era 投掷者模式不参与 mob 结算,与玩家自伤面
//   创造无敌分域);时长族效果 mob 面仍为 t1101 降级候选本切片不触。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训;双摘面互不重叠;双 NEG 均编译仍绿形)──────────
//   NEG-1 = 摘 mob 结算调用点形(playercontroller.cpp applySplashPotion 内「if ((eff ==
//     EffectInstantHeal || eff == EffectInstantDamage) && m_entityManager) { ... }」整块移除——玩家段
//     既有行零动、healEntity 声明/定义幸存(Q_INVOKABLE 成员零 unused 告警)→ 编译仍绿零告警;行为面 =
//     mob 结算全失)→ 恰红 = 三元 {r2097a, r2097b, r2097c}(a 结算柱行破坏言 + 守卫/结算行钉失;b
//     投掷碰撞柱 mob 面断言全失;c 反转柱行为面全失 + 摘面行钉失——如实三元声明,t1126 双元先例的
//     按量申报门)。d 豁免(d 所钉行全在摘块外:头注 raw 锚 / healEntity 面 / 谓词定义 / CMake 行 /
//     QML 路由行)。
//   NEG-2 = 值替换亡灵反转判定行(playercontroller.cpp「const bool undead =
//     EntityManager::isUndeadFamily(m_entityManager->mobTypeAt(i));」整式改 false 常量,undead 变量与
//     healDirection 消费点原地幸存 → 编译仍绿 = 亡灵判定恒否 = 一切 mob 按非亡灵结算)→ 恰红 = 单元
//     {r2097c}(反转柱全部断言翻转失 + 判定行钉失;a/b 所驱皆非亡灵 mob[pig]——false 判定对非亡灵
//     恰为真值,行为面零触达故绿;d 豁免)。
//   (双摘面互不重叠:NEG-1 摘整块含判定行 → c 对 NEG-1 亦红但归因 = 块移除面(行钉失 + 行为失),
//     NEG-2 行值替换时 a/b/d 全绿——恰红归因唯一;a 钉的守卫/结算行在 NEG-2 下原地幸存。)

namespace {

// fixed 宿主小世界 incantation(section83 同款四 setter;seed 92..95 与既族解耦)。
inline void initSplashMobWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section83 同款)。
inline void laySplashMobPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section82 同款:applicationDirPath/../src)。
inline QString srcRootForSplashMobPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚 / 摘面行——pinSet 剥注释会失配,section82 同款)。
inline bool rawContainsSplashMob(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// mob 沉降驱动(冻结 wander + 40 tick 0.016s;playerPos 远抛出 AI 追击域——敌对三族入场不追不射)。
inline void settleSplashMob(EntityManager &em, World &w, const QVector3D &farP)
{
    em.setWanderFrozen(true);
    for (int t = 0; t < 40; ++t)
        em.tick(0.016, &w, farP, 0.3f, 1.8f, false);
}

} // namespace

void MatrixRun::section90_splash_mob_t1127()
{
    // ── r2097a:mob 结算柱(非亡灵面 + 门族 + 死亡链;NEG-1 摘面行本腿持有)────────────────────────
    //   直调真链(镜像 Main.qml 路由语义)。mob 落位 = 格中心 + halfH(spawnMobTyped x+0.5/z+0.5、
    //   pos.y = 支撑面 + halfH——pig halfH=0.45 → 脚位 (26.5,81.45,26.5)):同格结算距 0.05 → 近满带
    //   恰 6;非亡灵 pig 伤害恰值 → 恰一次扣血红闪;第二击致死 → 既有死亡链(mobDied 延迟 emit 恰一次
    //   mobType/坐标对);治疗面中带恰值 + 钳上限恰值 + 满血零操作;出圈零结算 + 衰减带恰值 3;
    //   创造门照常受效(era 投掷者模式不参与 mob 结算);喷溅水瓶零结算;尸体零结算。
    //   NEG-1 摘面行本腿钉(守卫行 + 治疗向/伤害向结算行)。
    runLeg("r2097a splash mob resolution column (a direct drive of the splash instant damage on a"
        " non undead mob inside the radius answers exactly the close band six with one red flash"
        " and a second drive kills through the existing deferred death chain emitting mob died"
        " exactly once with the right type and cell, the heal face answers the mid band and clamps"
        " at max health and stays a no-op at full health, an out of radius mob answers nothing"
        " while an inner decay band answers three, a creative thrower still lands on the mob, the"
        " water bottle resolves nothing, and a corpse resolves nothing, with the resolution entry"
        " and both mutation rows pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initSplashMobWorld(w, 92);
        laySplashMobPlatform(w, 10, 38, 10, 38);
        EntityManager ents;
        PlayerController pc;
        const QVector3D farP(6.5f, 81.0f, 6.5f); // 远离全部结算格(AI 追击域外 + 玩家面出圈)
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        int diedCount = 0, diedType = -1, diedX = -1, diedY = -1, diedZ = -1;
        QObject::connect(&ents, &EntityManager::mobDied, &ents,
                         [&](int x, int y, int z, int mobType, bool, bool, int, bool) {
                             ++diedCount; diedType = mobType; diedX = x; diedY = y; diedZ = z;
                         });
        pc.loadSavedState(farP.x(), farP.y(), farP.z(), 0.0f, 0.0f, 2 /* Survival */);
        // 活体 pig(非亡灵)贴地冻结:沉降后脚位恰格中心 + halfH(26.5, 81+0.45, 26.5)——x/z 冻结无漂移
        //   (确定性面;y 带 0.02 容差防 halfH 浮点尾差)。
        int pig = ents.spawnMobTyped(26, 81, 26, EntityManager::MobPig,
                                     QStringLiteral("#e8a2a2"), 10);
        settleSplashMob(ents, w, farP);
        const QVector3D pigPos = ents.posAt(pig);
        const bool posOk = ents.aliveAt(pig) && !ents.deadAt(pig)
                           && pigPos.x() == 26.5f && pigPos.z() == 26.5f
                           && std::abs(pigPos.y() - 81.45f) < 0.02f;
        ok = ok && posOk;
        if (!posOk)
            diag += QStringLiteral("[pos %1 %2 %3 alive=%4]")
                        .arg(pigPos.x()).arg(pigPos.y()).arg(pigPos.z()).arg(ents.aliveAt(pig));
        // 伤害面:同格近满带恰值(结算格 = 自身格 (26,81,26),中心 (26.5,81.5,26.5) 到脚位距 0.05 →
        //   d1≈0.9875 → int(5.925+0.5)=6)+ 恰一次扣血红闪(damageEntity 直扣绕甲)。
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool dmgOk = ents.healthAt(pig) == 4 && ents.hurtFlashAt(pig) > 0.0f
                           && !ents.deadAt(pig);
        ok = ok && dmgOk;
        if (!dmgOk)
            diag += QStringLiteral("[dmg hp=%1 flash=%2]")
                        .arg(ents.healthAt(pig)).arg(ents.hurtFlashAt(pig));
        // 致死 → 既有死亡链:第二击 5-5=0 → 致死瞬间死亡态(dead + health 0,t449 快照面),延迟
        //   mobDied 恰一次(死亡链自然承接;槽在 mobDied 后释放——断言只走信号面 + tick 前死亡态)。
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool enteredDeath = ents.deadAt(pig) && ents.healthAt(pig) == 0;
        for (int t = 0; t < 60 && diedCount == 0; ++t)
            ents.tick(0.05f, &w, farP, 0.3f, 1.8f, false);
        const bool deathOk = enteredDeath
                             && diedCount == 1 && diedType == int(EntityManager::MobPig)
                             && diedX == 26 && diedY == 81 && diedZ == 26;
        ok = ok && deathOk;
        if (!deathOk)
            diag += QStringLiteral("[death entered=%1 n=%2 t=%3 cell=%4,%5,%6]")
                        .arg(enteredDeath).arg(diedCount)
                        .arg(diedType).arg(diedX).arg(diedY).arg(diedZ);
        // 尸体零结算(era attackEntityFrom / healEntity 双 health≤0 免疫同口径;槽已释放/尸态均被
        //   活体门挡下):再结算零新死亡信号。
        const int corpseDied = diedCount;
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool corpseOk = diedCount == corpseDied;
        ok = ok && corpseOk;
        if (!corpseOk)
            diag += QStringLiteral("[corpse hp=%1 n=%2]").arg(ents.healthAt(pig)).arg(diedCount);
        // 治疗面:新 pig 中带恰值(劈 8 至 2 → +6 = 8 不触钳)→ 钳上限(劈 2 至 6 → +6=12→10)→
        //   满血零操作(early-out 恒 10)。
        int pig2 = ents.spawnMobTyped(26, 81, 26, EntityManager::MobPig,
                                      QStringLiteral("#e8a2a2"), 10);
        settleSplashMob(ents, w, farP);
        ents.damageEntity(pig2, 8); // 10→2
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool healMidOk = ents.healthAt(pig2) == 8;
        ents.damageEntity(pig2, 2); // 8→6
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool healCapOk = ents.healthAt(pig2) == 10; // 6+6=12 → 钳 10(healEntity 单点钳制式)
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool healFullOk = ents.healthAt(pig2) == 10; // 满血早退恒 10
        ok = ok && healMidOk && healCapOk && healFullOk;
        if (!healMidOk || !healCapOk || !healFullOk)
            diag += QStringLiteral("[heal mid=%1 cap=%2 full=%3]").arg(healMidOk)
                        .arg(healCapOk).arg(healFullOk);
        // 出圈零结算 + 衰减带恰值 3:pig3 在 (33,81,26) 脚位 (33.5,81.45,26.5)——结算格 (26,81,26)
        //   距 7.0 ≥ 4 → 零;格 (31,81,26) 中心距 √(4+0.0025)≈2.0006 → d1≈0.4999 → int(3.499)=3。
        int pig3 = ents.spawnMobTyped(33, 81, 26, EntityManager::MobPig,
                                      QStringLiteral("#e8a2a2"), 10);
        settleSplashMob(ents, w, farP);
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool outOk = ents.healthAt(pig3) == 10;
        pc.applySplashPotion(31, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool band2Ok = ents.healthAt(pig3) == 7; // 10-3
        ok = ok && outOk && band2Ok;
        if (!outOk || !band2Ok)
            diag += QStringLiteral("[band out=%1 b2=%2]").arg(outOk).arg(band2Ok);
        // 创造门照常受效(era 投掷者模式不参与 mob 结算——与玩家自伤面创造无敌分域):
        //   自身格结算 → 近满带 6。
        pc.loadSavedState(farP.x(), farP.y(), farP.z(), 0.0f, 0.0f, 1 /* Creative */);
        const int hpBeforeCreative = ents.healthAt(pig3);
        pc.applySplashPotion(33, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool creativeOk = ents.healthAt(pig3) == hpBeforeCreative - 6;
        ok = ok && creativeOk;
        if (!creativeOk)
            diag += QStringLiteral("[creative %1->%2]").arg(hpBeforeCreative).arg(ents.healthAt(pig3));
        // 喷溅水瓶零结算(eff==0 → 玩家段早退,mob 面同静默——era 无效果表零结算同口径)。
        pc.applySplashPotion(33, 81, 26, RecipeRegistry::SplashWaterBottleId);
        const bool waterOk = ents.healthAt(pig3) == hpBeforeCreative - 6;
        ok = ok && waterOk;
        if (!waterOk)
            diag += QStringLiteral("[water hp=%1]").arg(ents.healthAt(pig3));
        // NEG-1 摘面行本腿钉(守卫行 + 治疗向/伤害向结算行——三行同在摘面块内)。
        const QString pcC = srcRootForSplashMobPins() + QStringLiteral("/Game/playercontroller.cpp");
        const QStringList missNeg = pinSet(pcC, {
            SrcPin("mob branch guard",
                   "if ((eff == PlayerState::EffectInstantHeal || eff == PlayerState::EffectInstantDamage)", 1),
            SrcPin("heal mutation row", "m_entityManager->healEntity(i, amt);", 1),
            SrcPin("damage mutation row", "m_entityManager->damageEntity(i, amt);", 1)});
        ok = ok && missNeg.isEmpty();
        if (!missNeg.isEmpty())
            diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2097a splash mob resolution column (a direct drive of the splash instant damage"
               " on a non undead mob inside the radius answers exactly the close band six with one"
               " red flash and a second drive kills through the existing deferred death chain"
               " emitting mob died exactly once with the right type and cell, the heal face answers"
               " the mid band and clamps at max health and stays a no-op at full health, an out of"
               " radius mob answers nothing while an inner decay band answers three, a creative"
               " thrower still lands on the mob, the water bottle resolves nothing, and a corpse"
               " resolves nothing, with the resolution entry and both mutation rows pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2097b:投掷碰撞柱(真 rig 双碎裂沿;NEG 双摘面豁免不钉)──────────────────────────────────
    //   链 1 命中实体碎裂:瓶落 pig 头顶 → AABB 命中即碎(工程碰撞检测在场)恰一次碎裂,碎裂格 =
    //     命中盒顶穿出格 (26,82,26)(瓶格心 x=26.5 落 pig 命中盒 y∈[80.7,82.2] 顶沿)——结算距
    //     1.05 → d1≈0.7375 → 恰 4(瓶体命中自身 0 伤 = 唯一伤源是碎裂沿结算——双扣必红);
    //   链 2 触地碎裂近体:瓶落 (29,84,26)(x=29 出 pig 命中盒 x∈[25.8,27.2] 纯触地碎)→ 结算照达
    //     (era 触地 onImpact 同点)碎裂格 (29,80,26) 中心到脚位距 √(4+0.9025)≈2.214 → 恰 3;
    //   链 3 触地碎裂远体:远处碎裂零波及。
    runLeg("r2097b splash throw collision column (a real bottle dropped onto a mob breaks on the"
        " mob hit exactly once and the area resolution lands exactly four so the bottle body"
        " itself dealt zero direct damage, a bottle landing on the ground away from the mob box"
        " still resolves through the same break edge for exactly three, and a far ground break"
        " resolves nothing on the mob)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initSplashMobWorld(w, 93);
        laySplashMobPlatform(w, 10, 38, 10, 38);
        EntityManager ents;
        PlayerController pc;
        const QVector3D farP(6.5f, 81.0f, 6.5f);
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        // 呈现层消费端镜像(r2092c 同门:Main.qml onSplashBottleBreak 机制面 = applySplashPotion)。
        int breakCount = 0;
        QObject::connect(&ents, &EntityManager::splashBottleBreak, &pc,
                         [&](int x, int y, int z, int itemId) {
                             ++breakCount;
                             pc.applySplashPotion(x, y, z, itemId);
                         });
        pc.loadSavedState(farP.x(), farP.y(), farP.z(), 0.0f, 0.0f, 2 /* Survival */);
        // 链 1:命中实体碎裂(瓶自 pig 头顶直落 → AABB 命中即碎;碎裂格 (26,82,26) → 结算恰 4)。
        int pig = ents.spawnMobTyped(26, 81, 26, EntityManager::MobPig,
                                     QStringLiteral("#e8a2a2"), 10);
        settleSplashMob(ents, w, farP);
        const int b1 = ents.spawnSplashBottle(QVector3D(26.5f, 84.0f, 26.5f),
                                              QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantDamagePotionId);
        for (int t = 0; t < 100 && breakCount == 0; ++t)
            ents.tick(0.05f, &w, farP, 0.3f, 1.8f, false);
        // 恰值 4 = 唯一伤源是碎裂沿结算(瓶体命中 0 伤——双扣则 10-8=2 必红);mobDied 零(10-4=6 活)。
        const bool hitOk = b1 >= 0 && breakCount == 1 && ents.healthAt(pig) == 6
                           && ents.hurtFlashAt(pig) > 0.0f && !ents.deadAt(pig);
        ok = ok && hitOk;
        if (!hitOk)
            diag += QStringLiteral("[hit b=%1 brk=%2 hp=%3 flash=%4 dead=%5]")
                        .arg(b1).arg(breakCount).arg(ents.healthAt(pig))
                        .arg(ents.hurtFlashAt(pig)).arg(ents.deadAt(pig));
        // 链 2:触地碎裂近体(瓶落 (28,84,26)——x=28 出 pig 命中盒 x∈[25.8,27.2] 纯触地碎;碎裂格 =
        //   支撑块格 (28,80,26),中心 (28.5,80.5,26.5) 到脚位 (26.5,81.45,26.5) 距 √(2.25+0.9025)
        //   ≈1.7755 → d1≈0.5561 → int(3.837)=3——触地沿结算照达,era 触地 onImpact 同点)。
        int pig2 = ents.spawnMobTyped(26, 81, 26, EntityManager::MobPig,
                                      QStringLiteral("#e8a2a2"), 10);
        settleSplashMob(ents, w, farP);
        const int b2 = ents.spawnSplashBottle(QVector3D(28.0f, 84.0f, 26.0f),
                                              QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantDamagePotionId);
        for (int t = 0; t < 100 && breakCount == 1; ++t)
            ents.tick(0.05f, &w, farP, 0.3f, 1.8f, false);
        const bool groundOk = b2 >= 0 && breakCount == 2 && ents.healthAt(pig2) == 7;
        ok = ok && groundOk;
        if (!groundOk)
            diag += QStringLiteral("[ground b=%1 brk=%2 hp=%3]")
                        .arg(b2).arg(breakCount).arg(ents.healthAt(pig2));
        // 链 3:触地碎裂远体(远处 (10,80,20) 碎裂,pig2 距 ≥ 15 → 零波及)。
        const int b3 = ents.spawnSplashBottle(QVector3D(10.0f, 84.0f, 20.0f),
                                              QVector3D(0.0f, -6.0f, 0.0f),
                                              RecipeRegistry::SplashInstantDamagePotionId);
        for (int t = 0; t < 100 && breakCount == 2; ++t)
            ents.tick(0.05f, &w, farP, 0.3f, 1.8f, false);
        const bool farOk = b3 >= 0 && breakCount == 3 && ents.healthAt(pig2) == 7;
        ok = ok && farOk;
        if (!farOk)
            diag += QStringLiteral("[far b=%1 brk=%2 hp=%3]")
                        .arg(b3).arg(breakCount).arg(ents.healthAt(pig2));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2097b splash throw collision column (a real bottle dropped onto a mob breaks on"
               " the mob hit exactly once and the area resolution lands exactly four so the bottle"
               " body itself dealt zero direct damage, a bottle landing on the ground away from the"
               " mob box still resolves through the same break edge for exactly three, and a far"
               " ground break resolves nothing on the mob)"
            << (ok ? QString() : diag);
    });

    // ── r2097c:反转柱(undead 三族名册 + 双向恰值;NEG-2 摘面行本腿钉)────────────────────────────
    //   亡灵反转(era Potion.a 真值)。Shambler/Bones halfH=0.9 → 自身格结算距 0.4 → d1=0.9 → 恰 5;
    //     BabyShambler halfH=0.45 → 距 0.05 → 恰 6。瞬间治疗×Shambler(僵尸等价)→ 伤恰值;瞬间伤害×
    //     Shambler → 愈中带恰值 + 钳上限;瞬间治疗×Bones(骷髅等价)→ 伤恰值(第二亡灵 kind);瞬间治疗×
    //     BabyShambler(幼体僵尸等价)→ 伤恰值(第三亡灵 kind 名册宽度);反转致死 → 既有死亡链恰一次;
    //     判定行钉 = NEG-2 摘面行本腿持有。
    runLeg("r2097c undead inversion column (the splash instant health on a full health shambler"
        " damages it for exactly the close band five through the magic path, the splash instant"
        " damage heals an injured shambler by the band and clamps at max health, the same"
        " inversion holds for the bones kind and the baby shambler kind, and a killing inversion"
        " drives the existing deferred death chain exactly once, with the inversion decision row"
        " pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initSplashMobWorld(w, 94);
        laySplashMobPlatform(w, 10, 38, 10, 38);
        EntityManager ents;
        PlayerController pc;
        const QVector3D farP(6.5f, 81.0f, 6.5f); // 敌对三族追击域外(≥16 格)——冻结不追不射
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        int diedCount = 0, diedType = -1;
        QObject::connect(&ents, &EntityManager::mobDied, &ents,
                         [&](int, int, int, int mobType, bool, bool, int, bool) {
                             ++diedCount; diedType = mobType;
                         });
        pc.loadSavedState(farP.x(), farP.y(), farP.z(), 0.0f, 0.0f, 2 /* Survival */);
        // 面 1:瞬间治疗×满血 Shambler → 反转伤恰 5(halfH=0.9 → 自身格距 0.4 → d1=0.9 →
        //   int(5.9)=5;magic 直扣——红闪在 damageEntity 链)。
        int sh = ents.spawnMobTyped(26, 81, 26, EntityManager::MobShambler,
                                    QStringLiteral("#3f6f4f"), 10);
        settleSplashMob(ents, w, farP);
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool healToDmgOk = ents.healthAt(sh) == 5 && ents.hurtFlashAt(sh) > 0.0f
                                 && !ents.deadAt(sh);
        ok = ok && healToDmgOk;
        if (!healToDmgOk)
            diag += QStringLiteral("[h2d hp=%1 flash=%2]")
                        .arg(ents.healthAt(sh)).arg(ents.hurtFlashAt(sh));
        // 面 2:瞬间伤害×受伤 Shambler → 反转愈中带:先劈 2 至 3HP → +5 = 8(不触钳)→ 再击钳上限
        //   (8+5=13→10)。
        ents.damageEntity(sh, 2); // 5→3
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool dmgToHealMidOk = ents.healthAt(sh) == 8;
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantDamagePotionId);
        const bool dmgToHealCapOk = ents.healthAt(sh) == 10; // 13 → 钳 10
        ok = ok && dmgToHealMidOk && dmgToHealCapOk;
        if (!dmgToHealMidOk || !dmgToHealCapOk)
            diag += QStringLiteral("[d2h mid=%1 cap=%2 hp=%3]")
                        .arg(dmgToHealMidOk).arg(dmgToHealCapOk).arg(ents.healthAt(sh));
        // 面 3:反转致死 → 既有死亡链恰一次(1-5 → 致死瞬间死亡态;槽 mobDied 后释放——断言只走
        //   信号面 + tick 前死亡态)。
        ents.damageEntity(sh, 9); // 10→1
        pc.applySplashPotion(26, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool killEntered = ents.deadAt(sh) && ents.healthAt(sh) == 0;
        for (int t = 0; t < 60 && diedCount == 0; ++t)
            ents.tick(0.05f, &w, farP, 0.3f, 1.8f, false);
        const bool killOk = killEntered && diedCount == 1
                            && diedType == int(EntityManager::MobShambler);
        ok = ok && killOk;
        if (!killOk)
            diag += QStringLiteral("[kill entered=%1 n=%2 t=%3]")
                        .arg(killEntered).arg(diedCount).arg(diedType);
        // 面 4:Bones(骷髅等价)同反转(halfH=0.9 同带):瞬间治疗×满血 → 伤恰 5。
        int bo = ents.spawnMobTyped(30, 81, 26, EntityManager::MobBones,
                                    QStringLiteral("#c9c9c9"), 10);
        settleSplashMob(ents, w, farP);
        pc.applySplashPotion(30, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool bonesOk = ents.healthAt(bo) == 5;
        ok = ok && bonesOk;
        if (!bonesOk)
            diag += QStringLiteral("[bones hp=%1]").arg(ents.healthAt(bo));
        // 面 5:BabyShambler(幼体僵尸等价)同反转(名册宽度面;halfH=0.45 → 自身格距 0.05 → 恰 6):
        //   瞬间治疗×满血 → 伤恰 6。
        int ba = ents.spawnMobTyped(34, 81, 26, EntityManager::MobBabyShambler,
                                    QStringLiteral("#3f6f4f"), 10);
        settleSplashMob(ents, w, farP);
        pc.applySplashPotion(34, 81, 26, RecipeRegistry::SplashInstantHealthPotionId);
        const bool babyOk = ents.healthAt(ba) == 4; // 10-6(名册宽度面:幼体 halfH=0.45 同格带 6)
        ok = ok && babyOk;
        if (!babyOk)
            diag += QStringLiteral("[baby hp=%1]").arg(ents.healthAt(ba));
        // NEG-2 摘面行本腿钉(亡灵反转判定行)。
        const QString pcC = srcRootForSplashMobPins() + QStringLiteral("/Game/playercontroller.cpp");
        const QStringList missNeg2 = pinSet(pcC, {
            SrcPin("inversion decision row",
                   "const bool undead = EntityManager::isUndeadFamily(m_entityManager->mobTypeAt(i));", 1)});
        ok = ok && missNeg2.isEmpty();
        if (!missNeg2.isEmpty())
            diag += QStringLiteral("[neg2 %1]").arg(missNeg2.join(QLatin1Char(',')));

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2097c undead inversion column (the splash instant health on a full health"
               " shambler damages it for exactly the close band five through the magic path, the"
               " splash instant damage heals an injured shambler by the band and clamps at max"
               " health, the same inversion holds for the bones kind and the baby shambler kind,"
               " and a killing inversion drives the existing deferred death chain exactly once,"
               " with the inversion decision row pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2097d:结构钉族(NEG 双摘面豁免不钉——d 所钉行全在摘块外)────────────────────────────────
    //   单一权威 Census:healEntity 声明/定义行 + 钳上限式恰两处(healEntity + healTamedPet 双子——
    //     第三份即红)+ isUndeadFamily 定义行 + 亡灵白名单体行恰两处(谓词 + 日光白名单既有孪生)+
    //     kSplashRadiusBlocks 声明行恰一处(半径单一权威)+ applySplashPotion 声明行 + eff 映射行 +
    //     Entities 层零 kSplashRadiusBlocks 引用(负面钉——半径不入低层)+ CMake 段行 + QML 路由行 +
    //     QML 五禁词零命中(负面钉)+ 头注/工件 era raw 锚。
    runLeg("r2097d structure pin family (the heal authority declares exactly one general heal"
        " entity face whose clamp formula lives at exactly two twins with the legacy tamed pet"
        " flow gate, the undead family predicate keeps its single definition with its daylight"
        " whitelist twin, the splash radius constant stays declared exactly once on the game side"
        " and never leaks into the entities layer, the resolution entry mapping row and the qml"
        " routing row and the cmake section row all hold, the five banned qml tokens answer zero,"
        " and the era artifact anchors survive raw)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = srcRootForSplashMobPins();
        const QString pcH = srcRoot + QStringLiteral("/Game/playercontroller.h");
        const QString pcC = srcRoot + QStringLiteral("/Game/playercontroller.cpp");
        const QString emH = srcRoot + QStringLiteral("/Entities/entitymanager.h");
        const QString emC = srcRoot + QStringLiteral("/Entities/entitymanager.cpp");
        const QString qml = srcRoot + QStringLiteral("/ui/Main.qml");
        const QString cmk = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/../CMakeLists.txt"); // section79 同款根定位
        // playercontroller.h:半径声明行恰一 + applySplashPotion 声明行(NEG 双面外)。
        const QStringList missPcH = pinSet(pcH, {
            SrcPin("radius constant row", "static constexpr float kSplashRadiusBlocks  = 4.0f;", 1),
            SrcPin("splash entry decl", "Q_INVOKABLE void applySplashPotion(int cx, int cy, int cz, int itemId);", 1)});
        ok = ok && missPcH.isEmpty();
        if (!missPcH.isEmpty())
            diag += QStringLiteral("[pcH %1]").arg(missPcH.join(QLatin1Char(',')));
        // playercontroller.cpp:eff 映射行恰一(玩家/mob 两面共用入口行——NEG-1 摘块外幸存)+
        //   头注 era 工件锚 raw(块注释 pinSet 剥不可锚)。
        const QStringList missPcC = pinSet(pcC, {
            SrcPin("effect mapping row", "const int eff = splashEffectType(itemId);", 1)});
        const bool eraAnchorPotion = rawContainsSplashMob(
            pcC, QStringLiteral("build/t1127_jar_potion_ab.txt"));
        const bool headAnchor = rawContainsSplashMob(pcC, QStringLiteral("[t1127 扩 mob 结算分支]"));
        ok = ok && missPcC.isEmpty() && eraAnchorPotion && headAnchor;
        if (!missPcC.isEmpty() || !eraAnchorPotion || !headAnchor)
            diag += QStringLiteral("[pcC %1 era=%2 head=%3]")
                        .arg(missPcC.join(QLatin1Char(','))).arg(eraAnchorPotion).arg(headAnchor);
        // entitymanager.h:healEntity 声明行(NEG-1 摘块外——Q_INVOKABLE 成员零 unused 告警幸存)+
        //   头锚 raw。
        const QStringList missEmH = pinSet(emH, {
            SrcPin("heal authority decl", "Q_INVOKABLE void healEntity(int i, int amount);", 1)});
        const bool healAnchorH = rawContainsSplashMob(emC, QStringLiteral("t1127 通用治疗单点"))
                                 && rawContainsSplashMob(emH, QStringLiteral("t1127 通用治疗单点"));
        ok = ok && missEmH.isEmpty() && healAnchorH;
        if (!missEmH.isEmpty() || !healAnchorH)
            diag += QStringLiteral("[emH %1 anchor=%2]")
                        .arg(missEmH.join(QLatin1Char(','))).arg(healAnchorH);
        // entitymanager.cpp:healEntity 定义行 + 钳上限式恰两处(healEntity + healTamedPet——第三份
        //   散布即红)+ isUndeadFamily 定义行 + 白名单体行恰两处(谓词 + 日光白名单既有孪生)+
        //   era 工件锚 raw。
        const QStringList missEmC = pinSet(emC, {
            SrcPin("heal authority def", "void EntityManager::healEntity(int i, int amount)", 1),
            SrcPin("clamp formula census", "e.health = std::min(e.maxHealth, e.health + amount);", 2),
            SrcPin("undead predicate def", "bool EntityManager::isUndeadFamily(int mobType)", 1),
            SrcPin("undead roster census",
                   "return mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler;", 2)});
        const bool eraAnchorNq = rawContainsSplashMob(
            emC, QStringLiteral("build/t1127_jar_nq_heal_attack.txt"));
        ok = ok && missEmC.isEmpty() && eraAnchorNq;
        if (!missEmC.isEmpty() || !eraAnchorNq)
            diag += QStringLiteral("[emC %1 era=%2]")
                        .arg(missEmC.join(QLatin1Char(','))).arg(eraAnchorNq);
        // 负面钉:Entities 层零 kSplashRadiusBlocks 引用(半径不入低层——单一权威守卫)。
        const bool radiusLeak = rawContainsSplashMob(emC, QStringLiteral("kSplashRadiusBlocks"));
        ok = ok && !radiusLeak;
        if (radiusLeak)
            diag += QStringLiteral("[radius-leak]");
        // CMake 段行 + QML 路由行 + QML 五禁词零命中(负面钉;r2090d 家族复钉)。
        const QStringList missCmk = pinSet(cmk, {
            SrcPin("cmake section row", "tools/matrix/section90_splash_mob_t1127.cpp", 1)});
        const QStringList missQml = pinSet(qml, {
            SrcPin("qml routing row", "player.applySplashPotion(x, y, z, itemId)", 1)});
        const bool banGameSession = rawContainsSplashMob(qml, QStringLiteral("GameSession"));
        const bool banMeshWorker = rawContainsSplashMob(qml, QStringLiteral("MeshWorker"));
        const bool banLifecycle = rawContainsSplashMob(qml, QStringLiteral("setChunkLifecycle"));
        const bool banEvictor = rawContainsSplashMob(qml, QStringLiteral("ChunkEvictor"));
        const bool banDriver = rawContainsSplashMob(qml, QStringLiteral("ChunkStreamDriver"));
        ok = ok && missCmk.isEmpty() && missQml.isEmpty()
             && !banGameSession && !banMeshWorker && !banLifecycle && !banEvictor && !banDriver;
        if (!missCmk.isEmpty() || !missQml.isEmpty() || banGameSession || banMeshWorker
            || banLifecycle || banEvictor || banDriver)
            diag += QStringLiteral("[doors cmk=%1 qml=%2 ban=%3%4%5%6%7]")
                        .arg(missCmk.join(QLatin1Char(','))).arg(missQml.join(QLatin1Char(',')))
                        .arg(banGameSession).arg(banMeshWorker).arg(banLifecycle)
                        .arg(banEvictor).arg(banDriver);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2097d structure pin family (the heal authority declares exactly one general"
               " heal entity face whose clamp formula lives at exactly two twins with the legacy"
               " tamed pet flow gate, the undead family predicate keeps its single definition with"
               " its daylight whitelist twin, the splash radius constant stays declared exactly"
               " once on the game side and never leaks into the entities layer, the resolution"
               " entry mapping row and the qml routing row and the cmake section row all hold, the"
               " five banned qml tokens answer zero, and the era artifact anchors survive raw)"
            << (ok ? QString() : diag);
    });
}
