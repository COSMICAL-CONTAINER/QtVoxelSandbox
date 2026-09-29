#include "matrix_helpers.h"

// t1110 名册审计 + 小缺口清偿合集探针段(4 腿;filter 词 r2080;矩阵 856→860)。置尾先例沿用(接
//   section72, runAll 末执行, rig 世界零接触——各腿自建 fresh 小世界 / EntityManager 直驱)。
//
// ── 现状核实裁定表(派工三件逐一 1.0 口径实读, 本段=两清偿一翻案)──
//   件一(1.0 方块/物品名册全量审计) = 只读核实 → 主控落 docs(本段零腿, 不在此展开; 翻案一: 剪刀
//     Shears=0x110 在场 + Beta 1.7 = 1.0.0 基准内 → t1105/t1109 两处「1.0 无剪刀」表述判误)。
//   件二(雪球对狼 3 伤, t1106 登记候选清偿) = **派工前提翻案(证据链三源直读, 如实留痕不硬造)**:
//     ① b1.7.3 反编译源(jacobo-mc/mc_b1.7.3_release EntitySnowball/EntityWolf 直读): 雪球命中调
//       attackEntityFrom(thrower, 0)——基础伤恒 0; EntityWolf.attackEntityFrom 对「非玩家非箭」源
//       折半 (dmg+1)/2——**整型时代 (0+1)/2 = 0**, 狼掉 0 血。
//     ② 1.18.1 官方映射源(Wolf.java/Snowball.java 直读): 雪球 `int i = entity instanceof Blaze ?
//       3 : 0; entity.hurt(thrown, i)`——狼仍 0 入; Wolf.hurt 同一折半在浮点时代变 (0+1)/2 = **0.5
//       HP** = MC-72151「snow golem's snowballs damage wolves」(2013 报案 → 24w06a 修复移除)。
//     ③ 1.0.0 基准伤害面为整型(attackEntityFrom(DamageSource, int)) → **1.0 口径雪球对狼 0 伤**
//       (狼吃 0 伤 hurt 流程: 击退 + 受击态, 无掉血); 「对狼 3 伤与烈焰人同族」= t1106 从烈焰数
//       外推的误读——任何版本雪球从未对狼 3 伤(烈焰人专属 3 伤 Beta 1.9 pre 引入, 狼不在列)。
//     → 本单交付 = **翻案收口**: 生产面零改动(不加 Wolf 分流——kSnowballWolfDamage 常量不存在,
//       负面 contains 钉; 命中分流保持 Emberling 唯一特判), 现行行为(狼 = 被动 0 伤 0 红闪 + 击退
//       + 减速, t505「打任意生物都击退」门)恰为 1.0 正确口径, r2080a 负面行为柱钉死。
//   件三(slime/villager 音效, t1107 登记后续清偿) = 生产交付: ①两新程序合成 clip
//     (mob_idle_slime.wav = 低频软体噗 squish / mob_idle_villager.wav = 中频鼻音短哼 hrmm,
//     build_sounds.py gen_slime_squish/gen_villager_hrmm 参数留痕, 三面注册铁律 loadClip/initSound/
//     析构 uninit 全入); ②接线: 受击面 playMobHurt(21/22) + idle 面 playMobAmbient(21/22) 各加
//     MobSlime/MobVillager 别名(t952 19→4 / t1012③ 20→7 别名先例同门, 防落 generic 兜底); ③弹跳
//     着地面 = 新语义信号 EntityManager::mobBounced(tick 共享物理段着地沿 resting false→true,
//     per-bounce 恰一次——贴地重钉帧幂等重入不重发; kAudioRange 听者范围门同 mobAmbient) →
//     Main.qml onMobBounced → AudioManager::playMobBounced(仅 MobSlime 有声, 其余 mob 着地静默 =
//     MC 无着地音语义)。死亡音面核实留痕: mob 死亡走 mobDied → 掉落/呈现链, 音频面无死亡专属路
//     由(受击音即死亡前最后一声 = t248 起既有口径), 本单不扩(登记非目标)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文; 流敏感教训同门: 摘面不改流)──
//   NEG-1 = **反向退化(翻案前提反演)**: 若当年按误读实现——entitymanager.cpp Snowball 命中段把
//     `if (m.hostile) {` 扩成 `if (m.hostile || m.mobType == MobWolf) {` 且三元
//     `(m.mobType == MobEmberling)` 扩成 `(m.mobType == MobEmberling || m.mobType == MobWolf)`
//     (两行, 编译仍绿)→ 恰红 = {r2080a}(野狼/驯服狼 hp 不变断言翻红; 烈焰相性/猪被动/玩家红闪
//     面不在摘面 = 部分幸存, 腿级 FAIL)。r2076c 不触达(其面无狼)。候选排查: 只摘 hostile 门不加
//     三元 → 狼吃 e.snowballDamage(玩家 0 伤) → hp 不变面部分幸存 → 归因不干净; 两行齐变 = 完整
//     误读形态, 恰红干净。摘面行豁免不钉(r2080d 不含 hostile 门行与三元行)。
//   NEG-2 = 摘件三着地触发面(entitymanager.cpp 着地沿 `emit mobBounced(int(MobSlime));` 行前加
//     `if (false) ` 单行, 编译仍绿)→ 恰红 = {r2080b}(首跳着地恰 1 信号断言归零; 长窗 O(bounces)
//     下界同步失守; hurt 计数面 r2080c / 结构钉面 r2080d 不在摘面 = 对照腿幸存)。摘面行豁免不钉
//     (r2080d 不含 emit 行; AudioManager 侧声明/注册/路由钉全数幸存 = 「摘 Entities 发射面, 音频
//     层消费面原样在」的分层归因面)。
//   [t1111 lawful 注] spawnSlime 出生错位已修复(entitymanager.cpp:盒精化后重置 pos.y)——本段
//     r2080b 注文中「出生错位 (r2077a 既有口径)」「空中出生避开嵌地」两处描述自此为历史沿革
//     (修复后贴地出生亦不嵌坪); 腿场景不变已复绿(空中出生底沿仍悬空, 首落沿恰 1 语义零变),
//     原注释就地保留作沿革留痕。
namespace {

// fixed 宿主小世界 incantation(section69/70 同款四 setter)。
inline void initAudioWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间, 上空 y 81..92 清 Air; section69 同款)。
inline void layAudioPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式: applicationDirPath/../src; section69 同款)。
inline QString srcRootForAudioPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

} // namespace

void MatrixRun::section73_audio_t1110()
{
    // ── r2080a:狼雪球零相性翻案柱(NEG-1 敏感面; t1106 登记候选按 1.0 实读翻案收口)──────────────
    //   五面: ①野狼 ← golem 雪球(dmg=1) → hp 不变 + 红闪 0 + 减速挂(命中落地见证; 1.0 整型折半
    //     (0+1)/2=0 = 无伤害, 击退/减速族照挂); ②野狼 ← 玩家雪球(dmg=0) → 同面; ③驯服狼
    //     (setTameRollOverride(0) 缝必成驯服) ← golem + 玩家两雪球 → hp 不变 + 红闪 0(1.0 折半
    //     变换不分驯野——两态同一 wolf transform, 翻案面覆盖驯服狼防误伤自家宠物); ④烈焰相性回归
    //     (共享门区守护): 玩家雪球打燃烬者仍恰 3 伤; ⑤猪被动回归: hp/红闪双 0。
    runLeg("r2080a wolf snowball zero affinity refutation column (a wild wolf hit by a golem"
        " snowball loses no health with no hurt flash while the slow lands proving the hit"
        " connected, a wild wolf hit by a player thrown snowball answers the same zero damage,"
        " a tamed wolf tamed through the deterministic roll seam loses no health from either"
        " thrower's snowball with no hurt flash, a player thrown snowball still takes exactly"
        " three health from an emberling proving the shared hostile dispatch unchanged, and a"
        " passive pig stays unharmed and unflashed)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initAudioWorld(w);
        layAudioPlatform(w, 14, 44, 4, 44);
        EntityManager ents;
        ents.setWanderFrozen(true); // 野狼 aiWander 冻结(位移不扰断言; 雪球减速/击退照常)
        // ① 野狼 A (24,81,24) ← golem 雪球 dmg=1 → hp 不变 + 红闪 0 + 减速挂。
        const int idxA = ents.spawnMobTyped(24, 81, 24, EntityManager::MobWolf,
                                            QStringLiteral("#c8ccd4"), 10);
        const int hpA0 = ents.healthAt(idxA);
        ents.spawnSnowball(QVector3D(24.5f, ents.posAt(idxA).y(), 26.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 20.5f), 0.3f, 1.8f, false);
        const bool s1 = ents.healthAt(idxA) == hpA0
            && ents.hurtFlashAt(idxA) == 0.0f
            && ents.isSlowedAt(idxA);
        ok = ok && s1;
        if (!s1) diag += QStringLiteral("[s1 hp=%1->%2 flash=%3 slow=%4]")
            .arg(hpA0).arg(ents.healthAt(idxA))
            .arg(double(ents.hurtFlashAt(idxA))).arg(ents.isSlowedAt(idxA));
        // ② 野狼 B (36,81,24) ← 玩家雪球 dmg=0 → 同面。
        const int idxB = ents.spawnMobTyped(36, 81, 24, EntityManager::MobWolf,
                                            QStringLiteral("#c8ccd4"), 10);
        const int hpB0 = ents.healthAt(idxB);
        ents.spawnSnowball(QVector3D(36.5f, ents.posAt(idxB).y(), 26.5f),
                           QVector3D(0, 0, -10), 0, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(36.5f, 81.5f, 20.5f), 0.3f, 1.8f, false);
        const bool s2 = ents.healthAt(idxB) == hpB0
            && ents.hurtFlashAt(idxB) == 0.0f
            && ents.isSlowedAt(idxB);
        ok = ok && s2;
        if (!s2) diag += QStringLiteral("[s2 hp=%1->%2 flash=%3 slow=%4]")
            .arg(hpB0).arg(ents.healthAt(idxB))
            .arg(double(ents.hurtFlashAt(idxB))).arg(ents.isSlowedAt(idxB));
        // ③ 驯服狼 C (24,81,34): 缝接管 setTameRollOverride(0) 必成驯服 → golem + 玩家两雪球 →
        //   hp 不变 + 红闪 0(1.0 折半变换不分驯野; 听者随侧防跟随位移——驯服狼跟随门=主人远距,
        //   听者贴位即留守)。
        const int idxC = ents.spawnMobTyped(24, 81, 34, EntityManager::MobWolf,
                                            QStringLiteral("#c8ccd4"), 10);
        ents.setTameRollOverride(0);
        const bool tamedOk = ents.tameWolf(idxC) && ents.wolfTamedAt(idxC);
        ents.setTameRollOverride(-1);
        ok = ok && tamedOk;
        if (!tamedOk) diag += QStringLiteral("[tame tamed=%1]").arg(ents.wolfTamedAt(idxC));
        const int hpC0 = ents.healthAt(idxC);
        ents.spawnSnowball(QVector3D(24.5f, ents.posAt(idxC).y(), 36.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 34.5f), 0.3f, 1.8f, false);
        const bool s3a = ents.healthAt(idxC) == hpC0 && ents.hurtFlashAt(idxC) == 0.0f;
        ents.spawnSnowball(QVector3D(24.5f, ents.posAt(idxC).y(), 36.5f),
                           QVector3D(0, 0, -10), 0, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 34.5f), 0.3f, 1.8f, false);
        const bool s3 = s3a && ents.healthAt(idxC) == hpC0
            && ents.hurtFlashAt(idxC) == 0.0f && ents.wolfTamedAt(idxC);
        ok = ok && s3;
        if (!s3) diag += QStringLiteral("[s3 a=%1 hp=%2->%3 flash=%4]")
            .arg(s3a).arg(hpC0).arg(ents.healthAt(idxC))
            .arg(double(ents.hurtFlashAt(idxC)));
        // ④ 烈焰相性回归(共享门区守护; NEG-1 两行摘面同域): 玩家雪球打燃烬者 → 恰 3 伤。
        ents.spawnHostileMob(38, 81, 34, EntityManager::MobEmberling);
        int idxE = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobEmberling
                && std::fabs(ents.posAt(i).z() - 34.5f) < 1.0f) idxE = i;
        const int hpE0 = ents.healthAt(idxE);
        ents.spawnSnowball(QVector3D(38.5f, ents.posAt(idxE).y(), 36.5f),
                           QVector3D(0, 0, -10), 0, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(38.5f, 81.5f, 40.5f), 0.3f, 1.8f, false);
        const bool s4 = ents.healthAt(idxE) == hpE0 - 3;
        ok = ok && s4;
        if (!s4) diag += QStringLiteral("[s4 %1->%2]").arg(hpE0).arg(ents.healthAt(idxE));
        // ⑤ 猪被动回归: hp/红闪双 0(t505 既有口径不受本单影响)。
        ents.spawnMobTyped(38, 81, 14, EntityManager::MobPig, QStringLiteral("#f0a8b0"), 10);
        int idxP = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobPig
                && std::fabs(ents.posAt(i).z() - 14.5f) < 1.0f) idxP = i;
        const int hpP0 = ents.healthAt(idxP);
        ents.spawnSnowball(QVector3D(38.5f, ents.posAt(idxP).y(), 16.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(38.5f, 81.5f, 14.5f), 0.3f, 1.8f, false);
        const bool s5 = ents.healthAt(idxP) == hpP0 && ents.hurtFlashAt(idxP) == 0.0f;
        ok = ok && s5;
        if (!s5) diag += QStringLiteral("[s5 %1->%2 flash=%3]")
            .arg(hpP0).arg(ents.healthAt(idxP)).arg(double(ents.hurtFlashAt(idxP)));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2080a wolf snowball zero affinity refutation column (a wild wolf hit by a"
               " golem snowball loses no health with no hurt flash while the slow lands proving"
               " the hit connected, a wild wolf hit by a player thrown snowball answers the same"
               " zero damage, a tamed wolf tamed through the deterministic roll seam loses no"
               " health from either thrower's snowball with no hurt flash, a player thrown"
               " snowball still takes exactly three health from an emberling proving the shared"
               " hostile dispatch unchanged, and a passive pig stays unharmed and unflashed)"
            << (ok ? QString() : diag);
    });

    // ── r2080b:史莱姆弹跳着地信号柱(NEG-2 敏感面; 禁空转钉——恰一次 + O(bounces) 双界)──────────
    //   三阶段: ①冻结空投(setWanderFrozen(true) 自出生即冻 → 永不起跳; 空投 (24,83,24) un-rest
    //   + 落体 + 首落沿恰 1——spawnSlime 默认档 pos.y 以 halfH=0.30 置位后写回 0.60 的出生错位
    //   (r2077a 既有口径)在空中出生下零嵌地 settle 伪沿); ②解冻首跳(wanderTimer 出生值 0 → 首个
    //   aiTick 即起跳, 0.9s 窗恰再 +1); ③长窗 20s 包络 [10,22](per-bounce 恰一次双界)。冻结期
    //   playerTargetable=false 零接触伤, 落差 2.7 格 < 3 摔伤阈零伤害, 末段存活断言。
    runLeg("r2080b slime bounce land signal column (a big slime frozen from birth dropped from"
        " the air answers exactly one bounce signal for its single landing while a resting"
        " emitter would have re-fired every recheck, unfreezing fires the first hop whose"
        " landing adds exactly one more signal inside the zero point nine second window, a"
        " further twenty second window keeps the total between ten and twenty two proving one"
        " signal per landing rather than one per tick, and the slime stays alive through the"
        " whole run)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initAudioWorld(w);
        layAudioPlatform(w, 14, 44, 4, 44);
        EntityManager ents;
        // 空中出生 (29,83,24): spawnSlime 默认档 pos.y 以 halfH=0.30 置位后写回 0.60 的出生错位
        //   (r2077a 既有口径)会让贴地出生嵌坪 → 首拍嵌入顶起 settle 产生伪沿(实测 t2)——空中出生
        //   底base悬空避开嵌地面, 首落沿=唯一入窗沿(2.7 格落差 < 3 摔伤阈零伤害)。出生近坪心
        //   (29.5,24.5) = 20s 游荡包络漂移 ≤13 格不坠缘。
        const int idx = ents.spawnSlime(29, 83, 24, 4);
        ents.setWanderFrozen(true); // 自出生即冻(aiSlime 顶部早退=永不起跳; 阶段 2 解冻)
        ok = ok && idx >= 0 && ents.aliveAt(idx) && ents.healthAt(idx) == 4;
        if (!(idx >= 0 && ents.aliveAt(idx)))
            diag += QStringLiteral("[spawn idx=%1]").arg(idx);
        int bounces = 0;
        const QMetaObject::Connection conn = QObject::connect(
            &ents, &EntityManager::mobBounced,
            [&](int mobType) { if (mobType == int(EntityManager::MobSlime)) ++bounces; });
        ok = ok && bool(conn);
        // 阶段 1(冻结空投): setWanderFrozen(true) 自出生即冻(aiSlime 顶部早退=永不起跳)→ 空投
        //   un-rest + 自由落体 + 首落沿**恰 1**(贴地重钉帧幂等重入不重发; 退化成 per-rest-recheck
        //   重发将 ~7×)。30 拍(1.5s)窗: 落体 ~0.35s + aiTick 离散 ≤0.2s 全数入窗。
        for (int i = 0; i < 30; ++i)
            ents.tick(0.05, &w, QVector3D(29.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
        const bool drop1 = bounces == 1;
        ok = ok && drop1;
        if (!drop1) diag += QStringLiteral("[drop1 bounces=%1]").arg(bounces);
        // 阶段 2(解冻首跳): wanderTimer 出生值 0 → 解冻后首个 aiTick 即起跳 → 空中 ~0.54s 落地
        //   → 恰再 +1(次跳落 ≥首跳落 +resting 0.5 +air 0.54 ≈ +1.04s 在 0.9s 窗外)。
        ents.setWanderFrozen(false);
        for (int i = 0; i < 18; ++i)
            ents.tick(0.05, &w, QVector3D(29.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
        const bool hop1 = bounces == 2;
        ok = ok && hop1;
        if (!hop1) diag += QStringLiteral("[hop1 bounces=%1]").arg(bounces);
        // 阶段 3(长窗包络 +400 拍=20s): 累计 [10,22](周期 ∈[0.5,1.5]+air0.54 → 20s 9..19 跳 +前
        //   2 沿; per-tick 退化 ≥300 / per-aiTick 退化 ≥80 双向分离)。
        for (int i = 0; i < 400; ++i)
            ents.tick(0.05, &w, QVector3D(29.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
        const bool envelope = bounces >= 10 && bounces <= 22;
        ok = ok && envelope;
        if (!envelope) diag += QStringLiteral("[envelope bounces=%1]").arg(bounces);
        const bool aliveOk = ents.aliveAt(idx) && ents.healthAt(idx) == 4
            && std::fabs(ents.posAt(idx).x() - 29.5f) < 13.0f
            && std::fabs(ents.posAt(idx).z() - 24.5f) < 13.0f; // 坪内游荡未坠缘
        ok = ok && aliveOk;
        if (!aliveOk) diag += QStringLiteral("[alive alive=%1 hp=%2 x=%3 z=%4]")
            .arg(ents.aliveAt(idx)).arg(ents.healthAt(idx))
            .arg(double(ents.posAt(idx).x())).arg(double(ents.posAt(idx).z()));
        QObject::disconnect(conn);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2080b slime bounce land signal column (a big slime frozen from birth dropped"
               " from the air answers exactly one bounce signal for its single landing while a"
               " resting emitter would have re-fired every recheck, unfreezing fires the first"
               " hop whose landing adds exactly one more signal inside the zero point nine"
               " second window, a further twenty second window keeps the total between ten and"
               " twenty two proving one signal per landing rather than one per tick, and the"
               " slime stays alive through the whole run)"
            << (ok ? QString() : diag);
    });

    // ── r2080c:受击信号恰一次计数柱(hurt 触发链真驱; 禁空转钉)────────────────────────────────
    //   真链 pc rig(r2076a 同门: QQuickWindow+grab+loadSavedState 相机)+ pc.beginMining() 左键按
    //   下沿(mob 优先分支 findMobHit → attackMob → emit mobAttacked——attackMob 私有, 经由公共入
    //   口真驱)。三面: ①瞄史莱姆一击 → mobAttacked(21,·) 恰 1(冷却 0.5s 起); ②冷却内二击静默
    //   (仍 1; 同控制器零泵直证冷却门); ③同类零基线新控制器二击史莱姆恰再 +1(大档 4 血两击余血
    //   存活断言并列——死亡链不二次触发)+ 村民一击恰 1。**登记**: 冷却越窗面用同类新实例承载
    //   (headless 泵对 pc.tick 真实墙钟衰减的承载断言不成立——泵衰减对 0.5s 窗实测不闭合, 载体
    //   面如实改道; 计数钉 = 真链信号流非空转)。
    runLeg("r2080c hurt signal exact count column (a real crosshair attack on a big slime fires"
        " the mob attacked signal with the slime type exactly once, an immediate second attack"
        " on the same controller inside the cooldown window stays silent, a zero-baseline fresh"
        " controller of the same class lands exactly one more signal with the slime still"
        " alive, and a villager crosshair attack fires the signal with the villager type"
        " exactly once)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initAudioWorld(w);
        layAudioPlatform(w, 14, 44, 4, 44);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        const int idxS = ents.spawnSlime(24, 81, 24, 4); // 大档 4 血(两击 ≤2 伤余血存活)
        const int idxV = ents.spawnMobTyped(34, 81, 24, EntityManager::MobVillager,
                                            QStringLiteral("#8a6a4a"), 0);
        ents.setWanderFrozen(true); // 史莱姆不跳 + 村民不游荡(准星全程钉靶)
        ok = ok && idxS >= 0 && idxV >= 0 && ents.aliveAt(idxS) && ents.aliveAt(idxV);
        if (!(idxS >= 0 && idxV >= 0)) diag += QStringLiteral("[spawn s=%1 v=%2]").arg(idxS).arg(idxV);
        int sigSlime = 0, sigVillager = 0;
        const auto tally = [&](int mobType) {
            if (mobType == int(EntityManager::MobSlime)) ++sigSlime;
            if (mobType == int(EntityManager::MobVillager)) ++sigVillager;
        };
        const QMetaObject::Connection c1 = QObject::connect(
            &pc, &PlayerController::mobAttacked, tally);
        // ── 瞄史莱姆(玩家 (24.5,81,28) yaw 0 俯角 -20.7° → 射线穿史莱姆中心; 创造免疫接触伤)──
        pc.loadSavedState(24.5, 81.0, 28.0, 0.0, -20.7f, 1 /* Creative */);
        pc.tick();
        // 一击: 恰 1(冷却 0.5s 起; mob 3.74 格 < 方块命中距 → mob 优先分支)。
        pc.beginMining();
        const bool hit1 = sigSlime == 1;
        ok = ok && hit1;
        if (!hit1) diag += QStringLiteral("[hit1 sigSlime=%1]").arg(sigSlime);
        // 冷却内二击: 静默(仍 1)。
        pc.beginMining();
        const bool cooldown = sigSlime == 1;
        ok = ok && cooldown;
        if (!cooldown) diag += QStringLiteral("[cooldown sigSlime=%1]").arg(sigSlime);
        // ── 零基线新控制器二击史莱姆 + 村民一击(同构造法同信号计数; 单向事件流 tally 同源)──
        {
            PlayerController pc2;
            pc2.setWorld(&w);
            pc2.setEntityManager(&ents);
            pc2.setHotbar(&hb);
            QQuickWindow win2;
            pc2.setParentItem(win2.contentItem());
            pc2.grab();
            const QMetaObject::Connection c2 = QObject::connect(
                &pc2, &PlayerController::mobAttacked, tally);
            pc2.loadSavedState(24.5, 81.0, 28.0, 0.0, -20.7f, 1);
            pc2.tick();
            pc2.beginMining();
            const bool hit2 = sigSlime == 2 && ents.aliveAt(idxS);
            ok = ok && hit2;
            if (!hit2) diag += QStringLiteral("[hit2 sigSlime=%1 alive=%2]")
                .arg(sigSlime).arg(ents.aliveAt(idxS));
            // ── 村民同构(第三实例): 瞄 (34.5,81,27.5) yaw 0 俯角 -13.5° → 射线穿村民中心──
            PlayerController pc3;
            pc3.setWorld(&w);
            pc3.setEntityManager(&ents);
            pc3.setHotbar(&hb);
            QQuickWindow win3;
            pc3.setParentItem(win3.contentItem());
            pc3.grab();
            const QMetaObject::Connection c3 = QObject::connect(
                &pc3, &PlayerController::mobAttacked, tally);
            pc3.loadSavedState(34.5, 81.0, 27.5, 0.0, -13.5f, 1);
            pc3.tick();
            pc3.beginMining();
            const bool villagerOk = sigVillager == 1;
            ok = ok && villagerOk;
            if (!villagerOk) diag += QStringLiteral("[villager sigVillager=%1]").arg(sigVillager);
            QObject::disconnect(c2);
            QObject::disconnect(c3);
        }
        QObject::disconnect(c1);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2080c hurt signal exact count column (a real crosshair attack on a big slime"
               " fires the mob attacked signal with the slime type exactly once, an immediate"
               " second attack on the same controller inside the cooldown window stays silent,"
               " a zero-baseline fresh controller of the same class lands exactly one more"
               " signal with the slime still alive, and a villager crosshair attack fires the"
               " signal with the villager type exactly once)"
            << (ok ? QString() : diag);
    });

    // ── r2080d:结构钉族 + 相邻族零污染(双 NEG 摘面行豁免不钉)──────────────────────────────────
    //   信号面(decl 行) + 翻案负面钉(狼相性常量不存在 = contains 取反) + 音频三面注册铁律
    //   (两 Clip 声明/loadClip/initSound/析构 uninit 全入) + 路由别名族(playMobHurt/playMobAmbient
    //   21/22 分流 + playMobBounced 单件 + Main.qml 路由) + 资产链(CMake qrc 两行 + build_sounds
    //   两生成器两注册行) + 相邻族零污染(mobIdleClips[8] 原值 / 洞穴蜘蛛别名行 / drink·burp·splash
    //   三旧单件行 / MobSlime 21·MobVillager 22·kMobTypeCount / 0x28C·0x28E / 烈焰相性常量 3 原值;
    //   hostile 门行与三元行 = NEG-1 摘面豁免, emit 行 = NEG-2 摘面豁免)。
    runLeg("r2080d structure pin family and neighbouring family zero pollution column (the"
        " entity header holds the bounce signal declaration with no wolf snowball damage"
        " constant anywhere in the entity sources and the emberling affinity constant still"
        " reading three, the audio manager holds both new clip declarations with their load"
        " init and destructor rows and the hurt and ambient and bounced routing rows, the qml"
        " holds the bounce route, the build files hold both sound asset rows and both generator"
        " functions with their registration rows, and the neighbouring idle clip array and cave"
        " spider alias and drink burp splash singles and mob ids and egg ids and type count"
        " keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = srcRootForAudioPins();
        // (S1) 信号声明行(entitymanager.h)。
        const QStringList missSig = pinSet(
            srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("bounced signal decl", "void mobBounced(int mobType);", 1)});
        ok = ok && missSig.isEmpty();
        if (!missSig.isEmpty()) diag += QStringLiteral("[sig %1]").arg(missSig.join(QLatin1Char(',')));
        // (S2) 翻案负面钉: 狼雪球相性常量在 Entities 双源**不存在**(翻案收口的结构面——未按误读
        //   实现的证明; NEG-1 复用烈焰常量不加新常量故幸存)。
        {
            QFile fH(srcRoot + QStringLiteral("/Entities/entitymanager.h"));
            QFile fC(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
            const bool noConst = fH.open(QIODevice::ReadOnly)
                && fC.open(QIODevice::ReadOnly)
                && !QString::fromUtf8(fH.readAll()).contains(QLatin1String("kSnowballWolfDamage"))
                && !QString::fromUtf8(fC.readAll()).contains(QLatin1String("kSnowballWolfDamage"));
            ok = ok && noConst;
            if (!noConst) diag += QStringLiteral("[wolfConst]");
        }
        // (S3) 烈焰相性常量原值 3(共享门区邻族回归; 声明行逐字节钉, 数值行为面 r2080a s4 钉)。
        const QStringList missBlaze = pinSet(
            srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("blaze affinity const",
                   "static constexpr int   kSnowballBlazeDamage    = 3;", 1)});
        ok = ok && missBlaze.isEmpty();
        if (!missBlaze.isEmpty()) diag += QStringLiteral("[blaze %1]").arg(missBlaze.join(QLatin1Char(',')));
        // (S4) 音频面: 两新 Clip 声明 + 三面注册铁律行 + 路由族。
        const QStringList missAudio = pinSet(
            srcRoot + QStringLiteral("/Audio/audiomanager.cpp"), {
            SrcPin("slime clip decl", "Clip mobIdleSlimeClip{\":/sounds/mob_idle_slime.wav\"};", 1),
            SrcPin("villager clip decl", "Clip mobIdleVillagerClip{\":/sounds/mob_idle_villager.wav\"};", 1),
            SrcPin("slime loadClip", "d->loadClip(d->mobIdleSlimeClip);", 1),
            SrcPin("villager loadClip", "d->loadClip(d->mobIdleVillagerClip);", 1),
            SrcPin("slime initSound", "d->initSound(d->mobIdleSlimeClip);", 1),
            SrcPin("villager initSound", "d->initSound(d->mobIdleVillagerClip);", 1),
            SrcPin("slime uninit", "ma_sound_uninit(&d->mobIdleSlimeClip.sound);", 1),
            SrcPin("villager uninit", "ma_sound_uninit(&d->mobIdleVillagerClip.sound);", 1),
            SrcPin("slime hurt route", "d->replay(d->mobIdleSlimeClip, m_volume * 0.9f);", 2),
            SrcPin("slime ambient route", "d->replay(d->mobIdleSlimeClip, m_volume * 0.85f);", 1),
            SrcPin("villager hurt route", "d->replay(d->mobIdleVillagerClip, m_volume * 0.9f);", 1),
            SrcPin("villager ambient route", "d->replay(d->mobIdleVillagerClip, m_volume * 0.85f);", 1),
            SrcPin("bounced gate", "if (mobType != 21) return;", 1)});
        ok = ok && missAudio.isEmpty();
        if (!missAudio.isEmpty()) diag += QStringLiteral("[audio %1]").arg(missAudio.join(QLatin1Char(',')));
        const QStringList missAudioH = pinSet(
            srcRoot + QStringLiteral("/Audio/audiomanager.h"), {
            SrcPin("bounced invokable", "Q_INVOKABLE void playMobBounced(int mobType);", 1)});
        ok = ok && missAudioH.isEmpty();
        if (!missAudioH.isEmpty()) diag += QStringLiteral("[audioH %1]").arg(missAudioH.join(QLatin1Char(',')));
        // (S5) Main.qml 路由行。
        const QStringList missQml = pinSet(
            srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("bounced qml route", "function onMobBounced(mobType) { audio.playMobBounced(mobType) }", 1)});
        ok = ok && missQml.isEmpty();
        if (!missQml.isEmpty()) diag += QStringLiteral("[qml %1]").arg(missQml.join(QLatin1Char(',')));
        // (S6) 资产链: CMake qrc 两行 + build_sounds.py 两生成器两注册行(.cmake/.py 各按注释剥离)。
        const QStringList missCmake = pinSet(
            QDir(QCoreApplication::applicationDirPath()
                 + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("CMakeLists.txt")), {
            SrcPin("slime asset", "sounds/mob_idle_slime.wav", 1),
            SrcPin("villager asset", "sounds/mob_idle_villager.wav", 1)});
        ok = ok && missCmake.isEmpty();
        if (!missCmake.isEmpty()) diag += QStringLiteral("[cmake %1]").arg(missCmake.join(QLatin1Char(',')));
        const QStringList missGen = pinSet(
            QDir(QCoreApplication::applicationDirPath()
                 + QStringLiteral("/../tools")).absoluteFilePath(QStringLiteral("build_sounds.py")), {
            SrcPin("slime gen", "def gen_slime_squish():", 1),
            SrcPin("villager gen", "def gen_villager_hrmm():", 1),
            SrcPin("slime reg", "clips.append((\"mob_idle_slime\", gen_slime_squish))", 1),
            SrcPin("villager reg", "clips.append((\"mob_idle_villager\", gen_villager_hrmm))", 1)});
        ok = ok && missGen.isEmpty();
        if (!missGen.isEmpty()) diag += QStringLiteral("[gen %1]").arg(missGen.join(QLatin1Char(',')));
        // (S7) 相邻族零污染(枚举位/蛋表/id 表直读 + 旧单件行源钉)。
        const bool enumOk = int(EntityManager::MobSlime) == 21
            && int(EntityManager::MobVillager) == 22
            && RecipeRegistry::SlimeBallId == 0x28C
            && RecipeRegistry::SpawnEggVillagerId == 0x28E
            && int(BR::Count) == 162; // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");
        const QStringList missNb = pinSet(
            srcRoot + QStringLiteral("/Audio/audiomanager.cpp"), {
            SrcPin("idle array 8", "Clip mobIdleClips[8]", 1),
            SrcPin("cave spider alias", "if (mobType == 20) mobType = 7;", 1),
            SrcPin("baby shambler alias", "if (idx == 19) idx = 4;", 1),
            SrcPin("cave spider ambient alias", "if (idx == 20) idx = 7;", 1),
            SrcPin("drink clip row", "Clip drinkGulpClip{\":/sounds/drink_gulp.wav\"};", 1),
            SrcPin("burp clip row", "Clip burpClip{\":/sounds/burp.wav\"};", 1),
            SrcPin("splash clip row", "Clip splashBreakClip{\":/sounds/splash_break.wav\"};", 1)});
        ok = ok && missNb.isEmpty();
        if (!missNb.isEmpty()) diag += QStringLiteral("[nb %1]").arg(missNb.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2080d structure pin family and neighbouring family zero pollution column"
               " (the entity header holds the bounce signal declaration with no wolf snowball"
               " damage constant anywhere in the entity sources and the emberling affinity"
               " constant still reading three, the audio manager holds both new clip"
               " declarations with their load init and destructor rows and the hurt and ambient"
               " and bounced routing rows, the qml holds the bounce route, the build files hold"
               " both sound asset rows and both generator functions with their registration"
               " rows, and the neighbouring idle clip array and cave spider alias and drink"
               " burp splash singles and mob ids and egg ids and type count keep their original"
               " values)"
            << (ok ? QString() : diag);
    });
}
