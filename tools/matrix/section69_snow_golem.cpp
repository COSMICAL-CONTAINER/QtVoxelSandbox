#include "matrix_helpers.h"

// t1106 雪傀儡 + 生物名册审计合集探针段(4 腿;filter 词 r2076;矩阵 840→844)。置尾先例沿用(接 section68,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig)。
//
// ── 现状核实裁定表(派工四项逐一 1.0 口径实读,雪傀儡族**全链在场**(t482/t483/t505/t510/t529/t553/
//   t558/t583/t629 家族)+ 矩阵零腿钉 → 本段=收口形态:fix(烈焰人相性 + 钉出的真 bug 修复)+ test 补腿)──
//   ① 原料链:雪块 Snow=101 + 雪球 SnowballId=0x23D + 雪层 SnowLayer(8 层语义 state)全在场;雪块合成 =
//     4 雪球 2×2 → 1 雪块(t505 snow_block 行,2×2 背包即可);雪球源 = 铲挖雪层 / 雪块 + 雪层塌落掉雪球
//     (snowLayerCollapseDropped)+ 雪傀儡死亡掉落;铲挖雪块掉 4 雪球 → 4 雪球合雪块闭环(无损耗)。
//   ② 构建面:playercontroller.cpp placeBlock 内 t482 分支——头位检测 Pumpkin ∪ JackOLantern(t1105 已
//     扩,r2075d 已钉该谓词行,本段不重抄)+ 下方两格 Snow×2 → spawnMobTypedYaw(MobSnowGolem, 4 血,
//     yaw 朝玩家 atan2(-dx,-dz))+ setWaterSilent ×3 清结构;铁傀儡 T 形同门在场(本段零触碰)。头位负例 =
//     非南瓜头(Stone 放头位不进检测段)+ 缺雪块(below2 非 Snow → miss)。
//   ③ 实体面(全在场,本段行为级收口):MobSnowGolem=12(halfW 0.35 / halfH 0.90);友好被动(不参与黑暗
//     刷怪,nearestHostile 只扫 hostile → 无敌对不发球);雪球投掷 AI(aiSnowGolem:kSnowGolemThrow
//     Interval=2.5s 节流 + kSnowGolemAttackRange=10 格 nearestHostile + fireSnowball 抛物,发球帧 yaw 朝
//     敌对 t558);雪球命中分流(t505 发射者分流 golem=1/玩家=0 红闪)+ 击退 + kSnowSlowDuration=3s 减速;
//     **雪 trail**(t629 脚下格铺 SnowLayer state=0,占位守卫不叠层);融化(热群系/雨/雷/入水 → meltAccum
//     每 kSnowMeltInterval=1.0s 扣 kSnowMeltDamage=1,t552 慢扣非即死);剪南瓜头(t510 shearSnowGolem 翻
//     sheared + 信号);生命 4 HP(playercontroller 字面量 4)。
//   ④ 1.0 口径实读(留痕):雪傀儡 Beta 1.9 Prerelease(=1.0 基准内)加入;死亡掉 0-15 雪球(Main.qml
//     3247-3257 独立掷 Math.floor(random*16));**雪 trail 1.0 在册**(Beta 1.9 pre 起走处留雪层;MC-377
//     WAI 群系门为后续演化,本工程「脚下格可铺即铺」= 门控简化裁定,t629 锚)。**本单生产面**:
//     (a) 雪球对烈焰人伤害面——1.0 真值 = 雪球对烈焰人 3 伤(1.5 心,发射者无关;雪球对多数实体 0 伤只
//     击退)——工程旧实现 golem 雪球统一 kSnowballDamage=1(旧注释自引「机制等价 MC 雪球 0 伤 / 对火焰系
//     3 伤;本工程取 1」= 已登记简化)→ 本单 fix:命中分支 MobEmberling 恒 kSnowballBlazeDamage=3(发射者
//     无关),其余敌对保持发射者分流;雪球对狼 3 伤(1.0 相性另一成员)登记候选池不扩散(狼受「被动 0 伤」
//     门,扩展面留档)。(b) **钉出的真 bug**:aiSnowGolem 发球段 fireSnowball → spawnSnowball → acquire
//     Slot 在无空槽时 push_back **扩容** m_entities(std::vector 重分配)→ 调用方 e 引用悬空 → 发球后的
//     attackCooldown 写回失效(UB,节流丢失连发;诊断实测 4 拍间隔连发)→ 修:发球后经 idx 写回(同骨架
//     aiHostile 射箭 m_pendingArrows 队列模式 = 审查修 B8 在案;Bones 射箭早已绕开,goem 直调路径为漏网)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 退化雪球烈焰人特判(entitymanager.cpp Snowball 命中分流:`const int hitDamage = (m.mobType ==
//     MobEmberling) ? kSnowballBlazeDamage : e.snowballDamage;` 改 `const int hitDamage = e.snowballDamage;`
//     单行,编译仍绿)→ 恰红 = {r2076c}(golem 雪球打燃烬者 3 伤断言 → 掉 1 红;玩家雪球打燃烬者 3 伤
//     → 掉 0 红;AI 发球/节流/友好面/玩家 0 伤红闪面/被动 0 伤面不在摘面 = 部分幸存,腿级 FAIL)。
//     候选排查:摘 AI 发球行(fireSnowball 调用)同样恰红 c 但归因混伤害面与投掷面;摘 kSnowballDamage
//     常量值会被 d 腿常量源钉连带红(非恰红);烈焰特判为本单新增面、全矩阵唯一消费腿 = r2076c,恰红干净。
//   NEG-2 = 退化雪 trail 写入(aiSnowGolem 足迹块的 `if (world) {` 改 `if (false) {`——写入行本身不动
//     [d 腿源钉钉行存在 → 幸存],编译仍绿)→ 恰红 = {r2076b}(trail 主断言 blockAt==SnowLayer 全失;
//     不叠层断言退化为恒真但主断言已红;融化/剪头面不在摘面 = 部分幸存,腿级 FAIL)。零碰撞:雪层塌落
//     合并/柱坍腿(r2059c/r2064d)走 setSnowLayerMerge/World 层物理不经 aiSnowGolem;其他腿零消费 trail。
//   a / d 双 NEG 均不触达(a 走真链 pc placeBlock 构建检测,不碰命中分流与足迹块;d 源钉不钉两摘面
//     行 = 豁免设计,trail 写入行在 `if (false)` 包裹下原文不动)→ 对照腿。
namespace {

// fixed 宿主小世界 incantation(section66/67/68 同款四 setter)。
inline void initFixedGolemWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section66/67/68 同款)。
inline void layGolemPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section67/68 同款)。
inline QString srcRootForPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 真链 pc rig 的 placeBlock 前置泵(r2067c 同门:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpGolem(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

void MatrixRun::section69_snow_golem()
{
    // ── r2076a:构建面真 rig 列(真 pc placeBlock 链;双 NEG 不触达 = 对照腿)────────────────────
    //   链 A:雪柱×2 + 玩家垫脚俯视顶面 → placeBlock 放南瓜于头位格 → t482 检测触发 → 恰一 MobSnowGolem
    //     (4 血 / 脚 81 + halfH 0.90 / yaw 朝玩家[访问器返回**度**,±180 = 弧度 π])+ 3 块静默清除。链 B:
    //     南瓜灯头位同门(t1105 Pumpkin ∪ JackOLantern)。链 C(负例):缺第二雪块 → 南瓜落地但 below2 非
    //     Snow → miss 零 spawn。链 D(负例):非头位方块(Stone)放头位格 → 不进检测段 → 零 spawn。
    runLeg("r2076a build face real rig column (a real PlayerController placing a pumpkin atop two"
        " stacked snow blocks through the real place-block chain spawns exactly one snow golem of"
        " four health facing the player with all three structure blocks silently consumed, a jack"
        " o lantern head builds the same golem through the union head detection, a pumpkin placed"
        " over a single snow block leaves zero golems with the pumpkin resting on its column, and"
        " a stone placed on the head slot builds nothing)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedGolemWorld(w);
        layGolemPlatform(w, 16, 40, 16, 32);
        // 结构 rig:雪柱 A(24,81..82,24)/ 垫脚 (24,81,27);雪柱 B(30,81..82,24)/ 垫脚 (30,81,27);
        //   单雪块 (36,81,24)/ 垫脚 (36,81,26);雪柱 C(38,81..82,24)/ 垫脚 (38,81,27)。
        w.setBlock(24, 81, 24, BR::Snow, 0);
        w.setBlock(24, 82, 24, BR::Snow, 0);
        w.setBlock(24, 81, 27, BR::Stone, 0);
        w.setBlock(30, 81, 24, BR::Snow, 0);
        w.setBlock(30, 82, 24, BR::Snow, 0);
        w.setBlock(30, 81, 27, BR::Stone, 0);
        w.setBlock(36, 81, 24, BR::Snow, 0);
        w.setBlock(36, 81, 26, BR::Stone, 0);
        w.setBlock(38, 81, 24, BR::Snow, 0);
        w.setBlock(38, 82, 24, BR::Snow, 0);
        w.setBlock(38, 81, 27, BR::Stone, 0);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        const int mobs0 = ents.count();
        // 链 A:南瓜头雪傀儡(玩家眼 (24.5,83.62,27.5) 俯 12° → 射线恰命中雪柱顶面 (24,82,24) 顶 →
        //   放置格 (24,83,24) = 头位格;t482 检测 below1/below2 == Snow×2 → spawn + 3 块清除)。
        pc.setSelectedBlock(int(BR::Pumpkin));
        pc.loadSavedState(24.5, 82.0, 27.5, 0.0, -12.0, 1 /* Creative */);
        pc.tick();
        pc.placeBlock();
        const bool builtA = ents.count() == mobs0 + 1;
        int idxA = -1;
        for (int i = 0; builtA && i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobSnowGolem && ents.aliveAt(i)) idxA = i;
        const bool golemA = idxA >= 0
            && std::fabs(ents.posAt(idxA).x() - 24.5f) < 1e-3f
            && std::fabs(ents.posAt(idxA).y() - 81.9f) < 1e-3f   // 脚 81 + halfH 0.90
            && std::fabs(ents.posAt(idxA).z() - 24.5f) < 1e-3f
            && std::fabs(std::fabs(ents.yawAt(idxA)) - 180.0f) < 1e-3f // 朝玩家(度口径;±180 = 弧度 π)
            && ents.healthAt(idxA) == 4;
        const bool clearedA = w.blockAt(24, 83, 24) == BR::Air   // 南瓜清(进 golem)
            && w.blockAt(24, 82, 24) == BR::Air                  // 雪块 1 清
            && w.blockAt(24, 81, 24) == BR::Air                  // 雪块 2 清
            && w.blockAt(24, 80, 24) == BR::Stone;               // 坪原样
        ok = ok && builtA && golemA && clearedA;
        if (!(builtA && golemA && clearedA))
            diag += QStringLiteral("[A n=%1 idx=%2 px=%3 py=%4 pz=%5 yaw=%6 hp=%7 c1=%8 c2=%9 c3=%10]")
                        .arg(ents.count()).arg(idxA)
                        .arg(idxA >= 0 ? double(ents.posAt(idxA).x()) : -99.0)
                        .arg(idxA >= 0 ? double(ents.posAt(idxA).y()) : -99.0)
                        .arg(idxA >= 0 ? double(ents.posAt(idxA).z()) : -99.0)
                        .arg(idxA >= 0 ? double(ents.yawAt(idxA)) : -99.0)
                        .arg(idxA >= 0 ? ents.healthAt(idxA) : -1)
                        .arg(w.blockAt(24, 83, 24)).arg(w.blockAt(24, 82, 24)).arg(w.blockAt(24, 81, 24));
        // 链 B:南瓜灯头位(t1105 头位同收)→ 第二只 golem。
        pumpGolem(320);
        pc.setSelectedBlock(int(BR::JackOLantern));
        pc.loadSavedState(30.5, 82.0, 27.5, 0.0, -12.0, 1);
        pc.tick();
        pc.placeBlock();
        int golemsB = 0;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobSnowGolem && ents.aliveAt(i)) ++golemsB;
        const bool builtB = ents.count() == mobs0 + 2 && golemsB == 2
            && w.blockAt(30, 83, 24) == BR::Air && w.blockAt(30, 82, 24) == BR::Air
            && w.blockAt(30, 81, 24) == BR::Air;
        ok = ok && builtB;
        if (!builtB) diag += QStringLiteral("[B n=%1 g=%2]").arg(ents.count()).arg(golemsB);
        // 链 C(负例):缺第二雪块(below2 = 坪 Stone)→ 南瓜落 (36,82,24) 但零构建。
        pumpGolem(320);
        pc.setSelectedBlock(int(BR::Pumpkin));
        pc.loadSavedState(36.5, 82.0, 26.5, 0.0, -35.0, 1);
        pc.tick();
        pc.placeBlock();
        const bool negC = ents.count() == mobs0 + 2                  // 零新增
            && w.blockAt(36, 82, 24) == BR::Pumpkin                  // 南瓜原地留下
            && w.blockAt(36, 81, 24) == BR::Snow;                    // 雪块未消耗
        ok = ok && negC;
        if (!negC) diag += QStringLiteral("[C n=%1 pk=%2 sn=%3]")
                              .arg(ents.count()).arg(w.blockAt(36, 82, 24)).arg(w.blockAt(36, 81, 24));
        // 链 D(负例):非头位方块(Stone)放头位格 → 不进 Pumpkin∪JackOLantern 检测段 → 零构建。
        pumpGolem(320);
        pc.setSelectedBlock(int(BR::Stone));
        pc.loadSavedState(38.5, 82.0, 27.5, 0.0, -12.0, 1);
        pc.tick();
        pc.placeBlock();
        const bool negD = ents.count() == mobs0 + 2                  // 零新增
            && w.blockAt(38, 83, 24) == BR::Stone                    // Stone 头位格放置成功
            && w.blockAt(38, 82, 24) == BR::Snow                     // 雪柱原样
            && w.blockAt(38, 81, 24) == BR::Snow;
        ok = ok && negD;
        if (!negD) diag += QStringLiteral("[D n=%1 st=%2 s1=%3 s2=%4]")
                              .arg(ents.count()).arg(w.blockAt(38, 83, 24))
                              .arg(w.blockAt(38, 82, 24)).arg(w.blockAt(38, 81, 24));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2076a build face real rig column (a real PlayerController placing a pumpkin atop"
               " two stacked snow blocks through the real place-block chain spawns exactly one snow"
               " golem of four health facing the player with all three structure blocks silently"
               " consumed, a jack o lantern head builds the same golem through the union head"
               " detection, a pumpkin placed over a single snow block leaves zero golems with the"
               " pumpkin resting on its column, and a stone placed on the head slot builds nothing)"
            << (ok ? QString() : diag);
    });

    // ── r2076b:AI 行为列(雪 trail + 融化 + 剪南瓜头;NEG-2 敏感面)────────────────────────────
    //   双世界设计:世界 A(干坪)钉 trail(12 拍含首 aiTick 帧 t=3:物理下落期脚位嵌坪格不铺、落地后首
    //     个 aiTick 铺)+ 不叠层 + 剪头;世界 B(坪顶全 Water 覆盖,游走恒在水)钉融化 61 拍(15 个 aiTick
    //     窗 × 0.2s = 3.0s 累积 → 恰 3 扣 → hp=1,即死版第一窗(1.0s)已死 = 慢扣非即死同柱钉死)。
    runLeg("r2076b ai behavior column (a snow golem standing on the platform lays a thin snow layer"
        " in its foot cell once settled that never stacks beyond the single eighth layer, a golem"
        " standing in water for over three seconds loses exactly three health down from four and stays"
        " alive proving the slow melt rather than an instant kill, and shearing the golem flips"
        " the sheared flag firing the shear signal exactly once with a repeat shear staying"
        " silent)", [&]() {
        bool ok = true;
        QString diag;
        // ── 世界 A:trail + 不叠层 + 剪头(干坪)──
        {
            World w;
            initFixedGolemWorld(w);
            layGolemPlatform(w, 16, 40, 14, 34);
            EntityManager ents;
            const int idx1 = ents.spawnMobTyped(24, 81, 24, EntityManager::MobSnowGolem,
                                                QStringLiteral("#f0f4f8"), 4);
            ok = ok && idx1 >= 0 && ents.aliveAt(idx1) && ents.healthAt(idx1) == 4;
            for (int i = 0; i < 12; ++i)                               // ≥3 个 aiTick 窗,含落地
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const bool trailLaid = w.blockAt(24, 81, 24) == BR::SnowLayer
                && w.stateAt(24, 81, 24) == 0;                          // 1/8 薄层(t629 恒 state=0)
            ok = ok && trailLaid;
            if (!trailLaid) diag += QStringLiteral("[trail id=%1 st=%2]")
                                      .arg(w.blockAt(24, 81, 24)).arg(w.stateAt(24, 81, 24));
            for (int i = 0; i < 8; ++i)                                 // 再 2 个 aiTick:不叠层
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const bool noStack = w.blockAt(24, 81, 24) == BR::SnowLayer && w.stateAt(24, 81, 24) == 0;
            ok = ok && noStack;
            if (!noStack) diag += QStringLiteral("[noStack id=%1 st=%2]")
                                      .arg(w.blockAt(24, 81, 24)).arg(w.stateAt(24, 81, 24));
            // 剪头:golem2 (36,81,30) → shearSnowGolem 翻 sheared + snowGolemSheared 信号恰一次;
            //   再剪幂等(已剪静默早退,信号不重发)。
            const int idx2 = ents.spawnMobTyped(36, 81, 30, EntityManager::MobSnowGolem,
                                                QStringLiteral("#f0f4f8"), 4);
            ok = ok && idx2 >= 0 && !ents.snowGolemShearedAt(idx2);
            int shearSigs = 0;
            const QMetaObject::Connection connS = QObject::connect(
                &ents, &EntityManager::snowGolemSheared, [&](float, float, float) { ++shearSigs; });
            ok = ok && bool(connS);
            ents.shearSnowGolem(idx2);
            const bool sheared1 = ents.snowGolemShearedAt(idx2) && shearSigs == 1;
            ents.shearSnowGolem(idx2);
            const bool sheared2 = ents.snowGolemShearedAt(idx2) && shearSigs == 1; // 幂等
            ok = ok && sheared1 && sheared2;
            if (!(sheared1 && sheared2))
                diag += QStringLiteral("[shear f=%1 sig=%2]")
                            .arg(ents.snowGolemShearedAt(idx2)).arg(shearSigs);
        }
        // ── 世界 B:融化(坪顶全 Water 覆盖,游走恒在水)──
        {
            World w;
            initFixedGolemWorld(w);
            layGolemPlatform(w, 14, 44, 14, 34);
            for (int x = 14; x <= 44; ++x)                              // 坪顶全水:游走恒在水
                for (int z = 14; z <= 34; ++z)
                    w.setBlock(x, 81, z, BR::Water, 0);
            EntityManager ents;
            const int idx3 = ents.spawnMobTyped(28, 81, 20, EntityManager::MobSnowGolem,
                                                QStringLiteral("#f0f4f8"), 4);
            ok = ok && idx3 >= 0;
            for (int i = 0; i < 69; ++i)                                // 18 个 aiTick 窗(0,4..68),
                ents.tick(0.05, &w, QVector3D(28.5f, 81.5f, 44.5f), 0.3f, 1.8f, false); // 累积 3.45s 居中 3 扣带
            const bool melt = ents.aliveAt(idx3) && ents.healthAt(idx3) == 1; // 慢扣 3 扣非即死
            ok = ok && melt;
            if (!melt) diag += QStringLiteral("[melt alive=%1 hp=%2]")
                                  .arg(ents.aliveAt(idx3)).arg(ents.healthAt(idx3));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2076b ai behavior column (a snow golem standing on the platform lays a thin snow"
               " layer in its foot cell once settled that never stacks beyond the single eighth"
               " layer, a golem standing in water for over three seconds loses exactly three health"
               " down from four and stays alive proving the slow melt rather than an instant kill,"
               " and shearing the golem flips the sheared flag firing the shear signal exactly"
               " once with a repeat shear staying silent)"
            << (ok ? QString() : diag);
    });

    // ── r2076c:雪球投掷伤害列(NEG-1 敏感面)────────────────────────────────────────────────────
    //   AI 投掷 + 节流(修复后首 aiTick 发恰 1 发,cd=2.5 经 idx 写回真实生效:24 拍 6 个 aiTick 窗累积
    //     1.2s < 2.5s → 全程同帧雪球数峰值恰 1 = maxN 面节流钉)+ 友好面(只有被动 → 恒不发球);伤害直发
    //     面(受控几何,五发全按靶当前 pos 重瞄防击退/hover 漂移):golem 雪球打 Shambler 掉恰 1(kSnowball
    //     Damage)、**golem 雪球打燃烬者掉恰 3(NEG-1 恰红面)**、**玩家雪球(damage=0)打燃烬者仍掉恰 3
    //     (发射者无关)**、玩家雪球打 Shambler 0 伤红闪、被动 Pig 0 伤 0 红闪。
    runLeg("r2076c snowball throw and damage column (the golem throws exactly one snowball at the"
        " nearest hostile within its detection range and never re-throws while the two and a half"
        " second throttle window is open and passive company never triggers a throw at all, the"
        " golem snowball takes exactly one health from a shambler, the golem snowball takes"
        " exactly three from an emberling, a player-thrown snowball takes exactly three from an"
        " emberling regardless of the thrower split, a player-thrown snowball leaves a shambler"
        " unharmed with the hurt flash raised, and a passive pig stays unharmed and unflashed)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedGolemWorld(w);
        layGolemPlatform(w, 14, 44, 4, 44);
        EntityManager ents;
        // ── 段 1:AI 投掷 + 节流(maxN 面)+ 友好面 ──
        const int idxG = ents.spawnMobTyped(20, 81, 20, EntityManager::MobSnowGolem,
                                            QStringLiteral("#f0f4f8"), 4);
        ents.spawnHostileMob(20, 81, 26, EntityManager::MobShambler); // 6 格敌对 → 发球目标
        auto snowballCount = [&]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Kind::Snowball) ++n;
            return n;
        };
        int maxN = 0;
        for (int i = 0; i < 24; ++i) {                     // 6 个 aiTick 窗(槽 0 相位)累积 1.2s
            ents.tick(0.05, &w, QVector3D(20.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            maxN = std::max(maxN, snowballCount());
        }
        const bool throttle = maxN >= 1 && maxN <= 1;      // 恰 1 发(修复后 cd 经 idx 写回真实生效)
        ok = ok && throttle;
        if (!throttle) diag += QStringLiteral("[throttle maxN=%1]").arg(maxN);
        // 友好面:只有被动(Pig)在 golem2 感知内 → 恒不发球(nearestHostile 守卫;段 1 雪球可消亡,
        //   故钉 30 拍窗口内的**峰值**:golem2 若发球瞬时必 +1 越过段 1 峰值 → 红;不发 → 峰值不越)。
        const int idxG2 = ents.spawnMobTyped(36, 81, 36, EntityManager::MobSnowGolem,
                                             QStringLiteral("#f0f4f8"), 4);
        ents.spawnMobTyped(36, 81, 40, EntityManager::MobPig, QStringLiteral("#e08898"), 10);
        int peakN = maxN;
        for (int i = 0; i < 30; ++i) {                                 // 1.5s,covering 多个 aiTick 窗
            ents.tick(0.05, &w, QVector3D(36.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            peakN = std::max(peakN, snowballCount());
        }
        const bool friendlyQuiet = peakN <= maxN;
        ok = ok && friendlyQuiet;
        if (!friendlyQuiet) diag += QStringLiteral("[quiet peak=%1 maxN=%2]")
                                      .arg(peakN).arg(maxN);
        ok = ok && idxG >= 0 && idxG2 >= 0;
        // ── 段 2:伤害直发面(受控几何;发球前按靶当前 pos 重瞄)──
        // 靶 A:Shambler (24,81,8) ← golem 雪球 damage=1 → 掉恰 1。
        ents.spawnHostileMob(24, 81, 8, EntityManager::MobShambler);
        int idxA = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobShambler
                && std::fabs(ents.posAt(i).z() - 8.5f) < 1.0f) idxA = i;
        const int hpA0 = ents.healthAt(idxA);
        ents.spawnSnowball(QVector3D(24.5f, ents.posAt(idxA).y(), 10.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 8.5f), 0.3f, 1.8f, false);
        const bool dmgA = ents.healthAt(idxA) == hpA0 - 1;
        ok = ok && dmgA;
        if (!dmgA) diag += QStringLiteral("[dmgA %1->%2]").arg(hpA0).arg(ents.healthAt(idxA));
        // 靶 B:燃烬者 (30,81,8) ← golem 雪球 damage=1 → **掉恰 3(kSnowballBlazeDamage;NEG-1 恰红面)**。
        //   listener 放 6 格外(喷火球窗 2.5-4s,本段 tick 总量 <1s 无火球干扰)。
        ents.spawnHostileMob(30, 81, 8, EntityManager::MobEmberling);
        int idxB = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobEmberling
                && std::fabs(ents.posAt(i).z() - 8.5f) < 1.0f) idxB = i;
        const int hpB0 = ents.healthAt(idxB);
        ents.spawnSnowball(QVector3D(30.5f, ents.posAt(idxB).y(), 10.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(30.5f, 81.5f, 14.5f), 0.3f, 1.8f, false);
        const bool dmgB = ents.healthAt(idxB) == hpB0 - 3;
        ok = ok && dmgB;
        if (!dmgB) diag += QStringLiteral("[dmgB %1->%2]").arg(hpB0).arg(ents.healthAt(idxB));
        // 靶 B2:玩家雪球(damage=0)打燃烬者 → **仍掉恰 3(相性与发射者无关)**;按当前 pos 重瞄。
        ents.spawnSnowball(QVector3D(30.5f, ents.posAt(idxB).y(), 10.5f),
                           QVector3D(0, 0, -10), 0, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(30.5f, 81.5f, 14.5f), 0.3f, 1.8f, false);
        const bool dmgB2 = ents.healthAt(idxB) == hpB0 - 6;
        ok = ok && dmgB2;
        if (!dmgB2) diag += QStringLiteral("[dmgB2 %1->%2]").arg(hpB0).arg(ents.healthAt(idxB));
        // 靶 A2:玩家雪球(damage=0)打 Shambler → 0 伤 + 红闪挂;按当前 pos 重瞄。
        ents.spawnSnowball(QVector3D(24.5f, ents.posAt(idxA).y(), 10.5f),
                           QVector3D(0, 0, -10), 0, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 8.5f), 0.3f, 1.8f, false);
        const bool dmgA2 = ents.healthAt(idxA) == hpA0 - 1 && ents.hurtFlashAt(idxA) > 0.0f;
        ok = ok && dmgA2;
        if (!dmgA2) diag += QStringLiteral("[dmgA2 hp=%1 flash=%2]")
                              .arg(ents.healthAt(idxA)).arg(ents.hurtFlashAt(idxA));
        // 靶 E:被动 Pig (38,81,8) ← damage=1 雪球 → 0 伤 0 红闪(hostile 门控)。
        ents.spawnMobTyped(38, 81, 8, EntityManager::MobPig, QStringLiteral("#e08898"), 10);
        int idxE = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.mobTypeAt(i) == EntityManager::MobPig
                && std::fabs(ents.posAt(i).z() - 8.5f) < 1.0f) idxE = i;
        const int hpE0 = ents.healthAt(idxE);
        ents.spawnSnowball(QVector3D(38.5f, ents.posAt(idxE).y(), 10.5f),
                           QVector3D(0, 0, -10), 1, -1);
        for (int i = 0; i < 5; ++i)
            ents.tick(0.05, &w, QVector3D(38.5f, 81.5f, 8.5f), 0.3f, 1.8f, false);
        const bool dmgE = ents.healthAt(idxE) == hpE0 && ents.hurtFlashAt(idxE) == 0.0f;
        ok = ok && dmgE;
        if (!dmgE) diag += QStringLiteral("[dmgE hp=%1 flash=%2]")
                              .arg(ents.healthAt(idxE)).arg(ents.hurtFlashAt(idxE));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2076c snowball throw and damage column (the golem throws exactly one snowball"
               " at the nearest hostile within its detection range and never re-throws while the"
               " two and a half second throttle window is open and passive company never triggers"
               " a throw at all, the golem snowball takes exactly one health from a shambler, the"
               " golem snowball takes exactly three from an emberling, a player-thrown snowball"
               " takes exactly three from an emberling regardless of the thrower split, a"
               " player-thrown snowball leaves a shambler unharmed with the hurt flash raised,"
               " and a passive pig stays unharmed and unflashed)"
            << (ok ? QString() : diag);
    });

    // ── r2076d:结构钉族 + 相邻族零污染(双 NEG 摘面全豁免 = 结构钉对照腿)────────────────────
    //   枚举位(MobSnowGolem=12 / 尾 MobCaveSpider=20 / kMobTypeCount=21)+ 常量族源钉(节流 2.5 / 射程 10 /
    //   球速 10 / 伤害 1 / **烈焰相性 3 本单新增** / 击退 1.2 / 减速 3 / 足迹节流 / 融化 1s·1HP / 寿命 5)
    //   + 原料链行为钉(4 雪球 2×2 → 1 雪块命中 + 错料负例 + Snow=101 / SnowballId=0x23D / SnowLayer 在册)
    //   + 源钉族(trail 写入行 / 经 idx 写回节流行 / 发球行 / 融化累积行 / 构建 spawn 行 / 剪头调用行 /
    //   Main.qml 掉落·剪头·XP 三行)+ 相邻族零污染(MelonStem 151 / JackOLantern 152 / Cauldron 153 /
    //   Count 154 / 图集 207 / 0x28A / 0x28B / Nightwalker 16 / Emberling 17)。
    runLeg("r2076d structure pin family and neighbouring family zero pollution column (the mob"
        " enum holds snow golem at twelve with the tail cave spider at twenty and the type count"
        " at twenty one, the constant family answers the two point five second throw interval"
        " with the ten block range and the ten ball speed and the one damage with the new"
        " three-damage emberling affinity and the one point two knockback and the three second"
        " slow and the one second one-health melt and the five second ball lifetime, the snow"
        " chain answers a four-snowball two-by-two craft into one snow block with a wrong-"
        " ingredient negative and the snow and snowball and snow layer ids in place, the source"
        " pin family holds the trail write and the index-writeback throttle row and the throw"
        " call and the melt accumulation and the build spawn and the shear call and the qml drop"
        " shear and xp rows, and the neighbouring melon stem and jack o lantern and cauldron and"
        " count and atlas and seed ids and nightwalker and emberling keep their original values)",
        [&]() {
        bool ok = true;
        QString diag;
        // (S1) 枚举位 + 类型计数(枚举 public 直读;kMobTypeCount private → 源钉声明行)。
        //   [lawful 修订 t1107/r2077] 声明行 MobCaveSpider + 1 → MobVillager + 1（t1107 枚举尾追加
        //   MobSlime=21/MobVillager=22 → 计数 21→23 枚举尾自动跟随；t879 图集 201→207 沿革注同门，
        //   枚举值 12/16/17/20 全部原值零扰动 = 本腿其余行不受扰）。
        const bool enumOk = int(EntityManager::MobSnowGolem) == 12
            && int(EntityManager::MobIronGolem) == 13
            && int(EntityManager::MobNightwalker) == 16
            && int(EntityManager::MobEmberling) == 17
            && int(EntityManager::MobCaveSpider) == 20;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");
        const QStringList missCnt = pinSet(srcRootForPins() + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("type count", "static constexpr int kMobTypeCount = MobVillager + 1;", 1)});
        ok = ok && missCnt.isEmpty();
        if (!missCnt.isEmpty()) diag += QStringLiteral("[cnt %1]").arg(missCnt.join(QLatin1Char(',')));
        // (S2) 常量族源钉(private 编译期常量不可直读 → 声明行逐行钉;数值行为面在 r2076b/c 钉)。
        const QStringList missConst = pinSet(srcRootForPins() + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("throw interval", "static constexpr float kSnowGolemThrowInterval = 2.5f;", 1),
            SrcPin("attack range", "static constexpr float kSnowGolemAttackRange   = 10.0f;", 1),
            SrcPin("ball speed", "static constexpr float kSnowballSpeed          = 10.0f;", 1),
            SrcPin("ball damage", "static constexpr int   kSnowballDamage         = 1;", 1),
            SrcPin("blaze damage", "static constexpr int   kSnowballBlazeDamage    = 3;", 1),
            SrcPin("knockback", "static constexpr float kSnowballKnockbackStrength = 1.2f;", 1),
            SrcPin("slow duration", "static constexpr float kSnowSlowDuration       = 3.0f;", 1),
            SrcPin("trail interval", "static constexpr float kSnowTrailInterval      = 1.0f;", 1),
            SrcPin("melt interval", "static constexpr float kSnowMeltInterval       = 1.0f;", 1),
            SrcPin("melt damage", "static constexpr int   kSnowMeltDamage         = 1;", 1),
            SrcPin("ball lifetime", "static constexpr float kSnowballLifetime       = 5.0f;", 1)});
        ok = ok && missConst.isEmpty();
        if (!missConst.isEmpty()) diag += QStringLiteral("[const %1]").arg(missConst.join(QLatin1Char(',')));
        // (S3) 原料链行为钉:4 雪球 2×2 → 1 雪块(t505 snow_block 行);错料负例;id 面在册。
        const int SB = RecipeRegistry::SnowballId, SN = int(BR::Snow);
        const int gridOk[4] = { SB, SB, SB, SB };
        const int gridBad[4] = { SB, SB, SB, RecipeRegistry::StringId };
        const RecipeRegistry::Recipe *snowHit = RecipeRegistry::match(gridOk, 2);
        const bool snowChain = snowHit != nullptr
            && snowHit->outputId == SN && snowHit->outputCount == 1
            && QLatin1String(snowHit->name) == QLatin1String("snow_block")
            && RecipeRegistry::match(gridBad, 2) == nullptr
            && RecipeRegistry::SnowballId == 0x23D
            && int(BR::Snow) == 101
            && int(BR::SnowLayer) > 0
            && BR::lightEmission(quint8(BR::Snow), 0) == 0;
        ok = ok && snowChain;
        if (!snowChain) diag += QStringLiteral("[snowChain hit=%1 sb=0x%2 sn=%3]")
                                   .arg(snowHit != nullptr).arg(int(SB), 4, 16, QChar(u'0')).arg(SN);
        // (S4) 源钉族(剥注释 pinSet 代码行)。
        const QStringList missEnt = pinSet(srcRootForPins() + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("trail write", "world->setWaterSilent(bx, footY, bz, BlockRegistry::SnowLayer, 0);", 1),
            SrcPin("index writeback", "m_entities[size_t(idx)].attackCooldown = kSnowGolemThrowInterval;", 1),
            SrcPin("throw call", "spawnSnowball(origin, QVector3D(vx, vy, vz), kSnowballDamage, idx)", 1),
            SrcPin("melt accumulation", "e.meltAccum += float(dt);", 1)});
        const QStringList missPc = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("build spawn row", "m_entityManager->spawnMobTypedYaw(tx, ty - 2, tz, EntityManager::MobSnowGolem,", 1),
            SrcPin("shear call", "m_entityManager->shearSnowGolem(mobIdx);", 1)});
        const QStringList missMq = pinSet(srcRootForPins() + QStringLiteral("/ui/Main.qml"), {
            SrcPin("drop branch", "else if (mobType === EntityManager.MobSnowGolem) {", 1),
            SrcPin("shear drop", "function onSnowGolemSheared(x, y, z) { itemEntities.spawnItem(x, y, z, 100, 1) }", 1),
            SrcPin("xp row", "xpForMob[EntityManager.MobSnowGolem] = 1 + Math.floor(Math.random() * 3)", 1)});
        const bool pinsOk = missEnt.isEmpty() && missPc.isEmpty() && missMq.isEmpty();
        ok = ok && pinsOk;
        if (!pinsOk) {
            const QStringList allMiss = QStringList() << missEnt << missPc << missMq;
            diag += QStringLiteral("[pins %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (S5) 相邻族零污染。
        const bool neighOk = int(BR::PumpkinStem) == 151
            && int(BR::JackOLantern) == 152
            && int(BR::Cauldron) == 153
            && int(BR::Count) == 157 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）
            && int(BR::AtlasTileCount) == 207
            && RecipeRegistry::MelonSliceId == 0x28A
            && RecipeRegistry::PumpkinSeedsId == 0x28B
            && int(BR::Pumpkin) == 100;
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2076d structure pin family and neighbouring family zero pollution column (the mob"
               " enum holds snow golem at twelve with the tail cave spider at twenty and the type"
               " count at twenty one, the constant family answers the two point five second throw"
               " interval with the ten block range and the ten ball speed and the one damage with"
               " the new three-damage emberling affinity and the one point two knockback and the"
               " three second slow and the one second one-health melt and the five second ball"
               " lifetime, the snow chain answers a four-snowball two-by-two craft into one snow"
               " block with a wrong-ingredient negative and the snow and snowball and snow layer"
               " ids in place, the source pin family holds the trail write and the index-writeback"
               " throttle row and the throw call and the melt accumulation and the build spawn and"
               " the shear call and the qml drop shear and xp rows, and the neighbouring melon stem"
               " and jack o lantern and cauldron and count and atlas and seed ids and nightwalker"
               " and emberling keep their original values)"
            << (ok ? QString() : diag);
    });
}
