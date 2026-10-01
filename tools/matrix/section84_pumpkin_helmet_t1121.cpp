#include "matrix_helpers.h"

// t1121 南瓜戴盔·夜行者凝视面探针段(4 腿;filter 词 r2091;矩阵 897→901)。置尾先例沿用(接 section83,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 EntityManager rig,余纯源钉腿)。
//
// ── 四步核实裁定表(每步 era 真值实读留痕;2026-10-01)─────────────────────────────────────────
//   步一 1.0 可戴南瓜头?**era 确证交付**——雕刻南瓜页(承载 1.13 前南瓜方块史表,中文镜像 wiki 实读)
//     Java 版历史首组三行齐备:Alpha v1.2.0 preview「加入了南瓜」+「南瓜现在会在许多新添加的生物群系
//     中生成,也可用来合成南瓜灯」+**「南瓜可以被玩家戴在头上」**;至 1.0.0 基准无任何卸戴行(Beta 1.8
//     可再生/沼泽生成/矿井瓜种、1.0.0 雪傀儡/瓜种/斧速采——全为增益行)。工程用南瓜本牌(Pumpkin=100,
//     t1110 定谳 1.0 无雕刻品种——戴的就是南瓜本牌,无新 id)。
//   步二 戴南瓜压夜行者瞪视激怒?**era 确证交付(本单核心语义面)**——同史表 Beta 1.8 pre1 行实读:
//     **「现在当玩家戴着南瓜望向末影人时,末影人不会被激怒。」**(末影人同版入 Beta 1.8 pre1,压制面
//     与之同版即有)→ 1.0 基准内实有。工程瞪视激怒链在场(entitymanager aiNightwalker 瞪视判定 +
//     nearestEnragableNightwalker 最近者守卫),压制守卫插入该判定单一权威处;Game→Entities 走
//     setPlayerHeadBlock 注入(PlayerController 每 tick 读 Hotbar::armorBlockIdAt(0) 下发,同
//     setPlayerSight 门,Entities 层零装备槽直读——负面钉在案)。
//   步三 护甲值面:**era 确证 0 护甲点**——雕刻南瓜页用法节实读「雕刻南瓜可作为无防护数值的头盔穿戴」
//     (南瓜非护甲材质,1.0 护甲表皮1/锁2/铁2/金2/钻3 无瓜行)→ 工程天然对齐:ArmorRegistry::isArmor
//     (100)=false → totalArmorPoints 跳过 / damageArmor 跳过 / 附魔与铁砧面不达,零特例零新增。
//   步四 第一人称糊面 overlay:**era 确证交付**——1.0.0 client jar(官方 Mojang version manifest sha
//     锚 b679fea27f2284836202e9365e13a82552092e5d)实解包:misc/pumpkinblur.png **在场**(256×256 RGBA,
//     黑底 alpha 刻孔——第一人称瓜内视野遮挡面物理实证)。工程轻量通路交付:Main.qml 全屏 Canvas 叠层
//     (水下蓝雾 / t465 vignette 同层同模式;§9a 原创自绘暗橙黑纱+瓜棱+眼嘴缝,**非 MC 资产**);状态源
//     = 装备槽 0(表达式形式触碰 armorRevision,t498 教训同门)。呈现一致性:Main.qml 第三人称头位南瓜
//     头 Model(BlockCube 100,同 golem 头先例)+二背包面板装备格方块图标分流(MaterialIcon 方块 id 落
//     兜底木棒,先分流)+CharacterPreview3D 壳层让位(预览无图集管线,头部裸露,装备格图标承载)。
//   附带核实申报面(只核不交付):右键装备门 equipSelectedArmor 保持 isArmor 守卫不扩(1.0 无右键装备
//     交互,t377 护甲便利不外溢——南瓜入盔唯 Inventory 点击/拖放/存档门);Shift+左键 shift 装备链同守
//     不扩;护甲槽持久化(gatherPlayerState/applyPlayerState)raw id 透传,armorSetStack 谓词即唯一门
//     ——谓词扩则存档零新表 round-trip 即通(无新存档面申报)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训)──────────────────────────────────────────
//   NEG-1 = 摘压制守卫**取值式**(entitymanager.cpp aiNightwalker 内「const bool stareSuppressed =
//     m_playerHeadBlockValid && m_playerHeadBlock == int(BlockRegistry::Pumpkin);」整式改 false 常量,
//     守卫变量与消费点原地幸存→编译仍绿、压制永不答真;t1051 摘调用点形同门)→ 恰红 = {r2091b}(瞪视
//     行为断言全失;r2091a 装备门 / r2091c 不相干面 / r2091d 钉面均豁免不涉——d 不钉该取值式)。
//   NEG-2 = 摘接受谓词**南瓜行**(hotbar.cpp armorSlotAccepts 内「return slot == 0 && itemId ==
//     int(BlockRegistry::Pumpkin);」改 return false,谓词声明与其余行幸存→编译仍绿)→ 恰红 =
//     {r2091a}(装备柱断言全失;r2091b 压制柱经 setPlayerHeadBlock 直注不经装备门;r2091c/d 豁免)。
//   (双摘面互不重叠:entitymanager.cpp 取值式 vs hotbar.cpp 谓词行;a/b 各由专权腿持有。)

namespace {

// fixed 宿主小世界 incantation(section83 同款四 setter)。
inline void initPumpkinHelmetWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section83 同款)。
inline void layPumpkinHelmetPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section83 同款:applicationDirPath/../src)。
inline QString srcRootForPumpkinHelmetPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(NEG 摘面行锚 / QML 行锚——pinSet 剥注释会失配的针走本通路,section83 同款)。
inline bool rawContainsPumpkinHelmet(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 原始计数(负面钉:needle 在文件**全文**(含注释)出现次数;期望 0 = 零命中。
//   负面 needle 选词已核实与本段新增注释零重叠——entitymanager 两文件内不写 armorBlockIdAt /
//   hotbar.h 字面(注释措辞规避),正面读路面由 PlayerController 独占)。
inline int rawCountPumpkinHelmet(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    return QString::fromUtf8(f.readAll()).count(needle);
}

// 瞪视 rig 搭建(t829 同门 + t1029 setWanderFrozen 确定性缝):石坪 + 夜行者 spawnMobTypedYaw 固定
//   朝向(yaw=π → 前向 (0,+1) 即 +Z)+ 冻结游荡(yaw 恒定 → 瞪视几何全程恒定,headless 可密闭驱动——
//   t829 取舍声明翻转:随机 yaw 是唯一不可驱动源,冻结后消除)。40 tick 无视线沉降(重力照常,冻结≠
//   悬停),再自沉降位算头位(0.72 系数镜像 aiNightwalker)置玩家眼位于 -Z 侧 3 格、视线 +Z 直对头颅
//   (dot≈1.0>0.99;mobDot=+1>0.3——与生产同式同门)。sight 注入后由调用方 tick。
inline void setupPumpkinStareRig(World &w, EntityManager &em, int &nw, QVector3D &eye, int sx, int sz)
{
    initPumpkinHelmetWorld(w);
    layPumpkinHelmetPlatform(w, sx - 8, sx + 8, sz - 8, sz + 8);
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

// 瞪视窗驱动:至多 maxTicks 帧(16ms/帧)内返回是否激怒(首激怒即停)。world 必传(tick 内怕水/物理
//   分支解引用 world——t829 全程传真世界同门);targetable 显式参(观察者门腿传 false)。
inline bool drivePumpkinStare(EntityManager &em, World &w, int nw, const QVector3D &eye,
                              int maxTicks, bool targetable = true)
{
    for (int t = 0; t < maxTicks; ++t) {
        em.tick(0.016, &w, eye, 0.3f, 1.8f, targetable);
        if (em.enragedAt(nw))
            return true;
    }
    return false;
}

} // namespace

void MatrixRun::section84_pumpkin_helmet_t1121()
{
    // ── r2091a:装备柱(NEG-2 敏感面 = armorSlotAccepts 南瓜行)────────────────────────────────────
    //   谓词真链:南瓜入头盔位整栈保真(count 随入 / durability inert 0 / revision 恰 +1)+ 谓词单元面
    //   (非头盔位拒 / 玻璃拒 / 钻石头盔入头盔位真 / 错部位拒 / 清空恒真)+ 对照拒(玻璃写入 no-op /
    //   钻石头盔入胸甲槽 no-op)+ 0 护甲值面(totalArmorPoints 恒 0)+ damageArmor 零损耗零破损信号 +
    //   卸下还原(count 随槽)+ 右键门(equipSelectedArmor 对南瓜恒 false,选中槽物不动)+ 存档门同谓词
    //   复绿(再写整栈覆盖保真)+ NEG-2 摘面行本腿钉。
    runLeg("r2091a pumpkin helmet equipment column (the acceptance predicate admits the pumpkin"
        " block into the helmet slot only with the whole cursor stack kept and inert durability"
        " while other slots and glass and wrong-piece armor stay rejected, the worn pumpkin answers"
        " zero total armor points and takes no damage-armor wear with a silent break signal, the"
        " unequip round-trip returns the slot count, the right-click equip door keeps answering"
        " false for the pumpkin with the held stack untouched, a re-apply through the same door"
        " overwrites with the saved stack count, and the pumpkin acceptance row is pinned on file)",
        [&]() {
        bool ok = true;
        QString diag;
        Hotbar hb;
        const int pumpkin = int(BR::Pumpkin);
        const int diaHelm = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0;   // 钻石头盔(section01 同式)
        const int diaChest = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1;  // 钻石胸甲
        const int diaBoots = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 3;  // 钻石靴子
        // (1) 谓词单元面(单一权威直调):南瓜仅头盔位;清空恒真;玻璃恒拒;护甲按部位。
        const bool t1 =
            hb.armorSlotAccepts(0, pumpkin)
            && !hb.armorSlotAccepts(1, pumpkin) && !hb.armorSlotAccepts(2, pumpkin)
            && !hb.armorSlotAccepts(3, pumpkin)
            && hb.armorSlotAccepts(0, 0) && hb.armorSlotAccepts(2, 0)
            && !hb.armorSlotAccepts(0, int(BR::Glass))
            && hb.armorSlotAccepts(0, diaHelm) && !hb.armorSlotAccepts(1, diaHelm)
            && hb.armorSlotAccepts(1, diaChest) && !hb.armorSlotAccepts(0, diaBoots);
        ok = ok && t1;
        if (!t1) diag += QStringLiteral("[predicate t1=%1]").arg(t1);
        // (2) 真写入:南瓜整栈入头盔位(count 5 保真 / durability inert 0 / revision 恰 +1)。
        const int rev0 = hb.armorRevision();
        hb.armorSetStack(0, pumpkin, 5, -1, QVariantList{}, QString());
        const bool t2 = hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 5
                        && hb.armorDurabilityAt(0) == 0 && hb.armorRevision() == rev0 + 1;
        ok = ok && t2;
        if (!t2) diag += QStringLiteral("[equip t2=%1 rev=%2/%3]").arg(t2).arg(rev0).arg(hb.armorRevision());
        // (3) 0 护甲值面:佩戴中 totalArmorPoints 恒 0(非护甲 id 被求和面跳过——步三裁定零特例)。
        const bool t3 = hb.totalArmorPoints() == 0;
        // (4) 受击损耗面:damageArmor 对南瓜零作用(id 不变 / count 不变 / 零破损信号)。
        int brokenCount = 0;
        QObject::connect(&hb, &Hotbar::armorBroken, [&brokenCount](int) { ++brokenCount; });
        hb.damageArmor();
        const bool t4 = hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 5
                        && hb.armorDurabilityAt(0) == 0 && brokenCount == 0;
        ok = ok && t3 && t4;
        if (!(t3 && t4)) diag += QStringLiteral("[zeroArmor t3=%1 noWear t4=%2 broken=%3]").arg(t3).arg(t4).arg(brokenCount);
        // (5) 对照拒:玻璃入头盔位 no-op;钻石头盔入胸甲槽 no-op(既有守卫幸存)。
        hb.armorSetStack(0, int(BR::Glass), 3, -1, QVariantList{}, QString());
        hb.armorSetStack(1, diaHelm, 1, -1, QVariantList{}, QString());
        const bool t5 = hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 5
                        && hb.armorBlockIdAt(1) == 0;
        ok = ok && t5;
        if (!t5) diag += QStringLiteral("[rejections t5=%1]").arg(t5);
        // (6) 卸下还原 + 往返:清槽 → 空;再装备 count 7(存档门同谓词再入,整栈覆盖保真)。
        hb.armorSetStack(0, 0, 0);
        const bool t6a = hb.armorBlockIdAt(0) == 0 && hb.armorCountAt(0) == 0;
        hb.armorSetStack(0, pumpkin, 7, -1, QVariantList{}, QString());
        const bool t6 = t6a && hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 7;
        ok = ok && t6;
        if (!t6) diag += QStringLiteral("[unequip t6a=%1 t6=%2]").arg(t6a).arg(t6);
        // (7) 右键门幸存:手持南瓜 + equipSelectedArmor 恒 false(1.0 无右键装备;t377 护甲便利不外溢),
        //     选中槽物原样不动、装备槽不受扰。
        hb.setStack(3, pumpkin, 5, -1, QVariantList{}, QString());
        hb.setSelectedSlot(3);
        const bool equipped = hb.equipSelectedArmor();
        const bool t7 = !equipped && hb.blockIdAt(3) == pumpkin && hb.countAt(3) == 5
                        && hb.armorBlockIdAt(0) == pumpkin && hb.armorCountAt(0) == 7;
        ok = ok && t7;
        if (!t7) diag += QStringLiteral("[rightClick t7=%1 equipped=%2]").arg(t7).arg(equipped);
        // (8) NEG-2 摘面行本腿钉(hotbar.cpp 谓词南瓜行 raw)。
        const QString hbRoot = srcRootForPumpkinHelmetPins();
        const QStringList missA = pinSet(hbRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("pumpkin-accept-row", "return slot == 0 && itemId == int(BlockRegistry::Pumpkin);")});
        const bool t8 = missA.isEmpty();
        ok = ok && t8;
        if (!t8) diag += QStringLiteral("[pin t8=%1 %2]").arg(t8).arg(missA.join(QStringLiteral(",")));
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  r2091a diag:" << diag;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2091a pumpkin helmet equipment column (the acceptance predicate admits the pumpkin"
               " block into the helmet slot only with the whole cursor stack kept and inert durability"
               " while other slots and glass and wrong-piece armor stay rejected, the worn pumpkin answers"
               " zero total armor points and takes no damage-armor wear with a silent break signal, the"
               " unequip round-trip returns the slot count, the right-click equip door keeps answering"
               " false for the pumpkin with the held stack untouched, a re-apply through the same door"
               " overwrites with the saved stack count, and the pumpkin acceptance row is pinned on file)";
    });

    // ── r2091b:压制柱(NEG-1 敏感面 = stareSuppressed 取值式)────────────────────────────────────
    //   确定性瞪视 rig(spawnMobTypedYaw 固定 yaw + setWanderFrozen 冻结 → 几何恒定,headless 可密闭
    //   驱动):①未戴对照恰激怒(瞪视链真);②佩戴南瓜长窗(2× 瞪视窗)恒不激怒(压制面真);③窗中卸下
    //   转激怒(守卫卸下即失效,计时从头累积——归零分支语义);④钻石头盔对照恰激怒(仅南瓜压制,非任意
    //   头盔);⑤守卫取值式 + 消费点本腿钉(NEG-1 专权)。
    runLeg("r2091b pumpkin helmet stare suppression column (an unhelmeted control nightwalker"
        " enrages under a held eye-to-eye stare while a pumpkin-wearing player never enrages across"
        " a doubled stare window, a mid-window helmet removal restarts the accumulation and enrages"
        " inside the follow-up window, a diamond helmet control enrages so only the pumpkin"
        " suppresses, and the suppression guard rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        // ① 未戴对照:瞪视几何成立 → 激怒(瞪视链在场真)。
        bool ctlEnraged = false;
        int ctlTicks = -1;
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupPumpkinStareRig(w, em, nw, eye, 24, 24);
            for (int t = 0; t < 250; ++t) {
                em.tick(0.016, &w, eye, 0.3f, 1.8f, true);
                if (em.enragedAt(nw)) { ctlEnraged = true; ctlTicks = t; break; }
            }
        }
        ok = ok && ctlEnraged;
        if (!ctlEnraged) diag += QStringLiteral("[control enraged=%1]").arg(ctlEnraged);
        // ② 佩戴南瓜:同几何 + 头盔位南瓜 → 长窗(500 帧 = 2× 瞪视窗)恒不激怒。
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupPumpkinStareRig(w, em, nw, eye, 24, 24);
            em.setPlayerHeadBlock(int(BR::Pumpkin));
            const bool supEnraged = drivePumpkinStare(em, w, nw, eye, 500);
            ok = ok && !supEnraged;
            if (supEnraged) diag += QStringLiteral("[suppressed enraged=true]");
        }
        // ③ 窗中卸下转激怒:佩戴 90 帧(不足瞪视窗)→ 卸下(头位回 0)→ 续窗内激怒
        //     (守卫随卸下失效;归零分支语义 = 计时从头累积,不继承佩戴前残量)。
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupPumpkinStareRig(w, em, nw, eye, 24, 24);
            em.setPlayerHeadBlock(int(BR::Pumpkin));
            for (int t = 0; t < 90; ++t)
                em.tick(0.016, &w, eye, 0.3f, 1.8f, true);
            const bool wornCalm = !em.enragedAt(nw);
            em.setPlayerHeadBlock(0);
            const bool removalEnraged = drivePumpkinStare(em, w, nw, eye, 350);
            const bool t3 = wornCalm && removalEnraged;
            ok = ok && t3;
            if (!t3) diag += QStringLiteral("[removal wornCalm=%1 enraged=%2]").arg(wornCalm).arg(removalEnraged);
        }
        // ④ 钻石头盔对照:头盔位非南瓜(护甲 id)→ 照常激怒(仅南瓜压制,非任意头位物)。
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupPumpkinStareRig(w, em, nw, eye, 24, 24);
            em.setPlayerHeadBlock(int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0);
            const bool diaEnraged = drivePumpkinStare(em, w, nw, eye, 250);
            ok = ok && diaEnraged;
            if (!diaEnraged) diag += QStringLiteral("[diamond enraged=%1]").arg(diaEnraged);
        }
        // ⑤ NEG-1 摘面行本腿钉(entitymanager.cpp 取值式 + 消费点,raw 通路)。
        const QString emRoot = srcRootForPumpkinHelmetPins();
        const bool t5 = rawContainsPumpkinHelmet(emRoot + QStringLiteral("/Entities/entitymanager.cpp"),
                                                 QStringLiteral("const bool stareSuppressed = m_playerHeadBlockValid"))
                        && rawContainsPumpkinHelmet(emRoot + QStringLiteral("/Entities/entitymanager.cpp"),
                                                    QStringLiteral("if (!stareSuppressed && dot > 0.99f"));
        ok = ok && t5;
        if (!t5) diag += QStringLiteral("[pin t5=%1]").arg(t5);
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  r2091b diag:" << diag << "ctlTicks" << ctlTicks;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2091b pumpkin helmet stare suppression column (an unhelmeted control nightwalker"
               " enrages under a held eye-to-eye stare while a pumpkin-wearing player never enrages across"
               " a doubled stare window, a mid-window helmet removal restarts the accumulation and enrages"
               " inside the follow-up window, a diamond helmet control enrages so only the pumpkin"
               " suppresses, and the suppression guard rows are pinned on file)";
    });

    // ── r2091c:不相干面柱───────────────────────────────────────────────────────────────────────
    //   ①蹒跚者(敌对 melee)对佩戴南瓜玩家照常检测/追击/出手(mobAttackedPlayer 恰 ≥1——压制守卫不
    //     波及他 mob,Game 注入门为全 mob 共享而守卫只在夜行者瞪视判定内);②playerTargetable=false
    //     观察者门幸存(瞪视几何全备+未戴 → 长窗恒不激怒,既有 t290 门不被本单改写);③既有 enrage 门
    //     三行源钉幸存(最近者守卫 / 非激怒+视线有效门 / 视线移开归零);④右键装备门 isArmor 守卫行
    //     幸存源钉。
    runLeg("r2091c unrelated faces column (a shambler still detects chases and strikes a"
        " pumpkin-wearing player with at least one attack signal, the observer gate keeps a stare"
        " geometry nightwalker calm for a player non targetable across a long window, and the"
        " existing enrage gate rows plus the right-click armor gate row survive on file)", [&]() {
        bool ok = true;
        QString diag;
        // ① 蹒跚者照常出手(佩戴南瓜):真链 rig,近距生成 + 冻结免除(追击不走游荡缝)。
        int hits = 0;
        {
            World w;
            EntityManager em;
            initPumpkinHelmetWorld(w);
            layPumpkinHelmetPlatform(w, 12, 36, 12, 36);
            const int sh = em.spawnMobTypedYaw(24, 82, 27, EntityManager::MobShambler,
                                               QStringLiteral("#3a6b35"), 10, 3.14159265f);
            if (sh < 0) {
                ok = false;
                diag += QStringLiteral("[shambler spawn failed]");
            } else {
                em.setPlayerHeadBlock(int(BR::Pumpkin));
                const QVector3D pos = em.posAt(sh);
                const float halfH = em.halfHeightAt(sh);
                const QVector3D eye(pos.x(), pos.y() + halfH * 0.72f, pos.z() - 3.0f);
                em.setPlayerSight(eye, QVector3D(0.0f, 0.0f, 1.0f));
                QObject::connect(&em, &EntityManager::mobAttackedPlayer,
                                 [&hits](int, int, float, float) { ++hits; });
                for (int t = 0; t < 400 && hits == 0; ++t)
                    em.tick(0.016, &w, eye, 0.3f, 1.8f, true);
            }
        }
        ok = ok && hits >= 1;
        if (hits < 1) diag += QStringLiteral("[shambler hits=%1]").arg(hits);
        // ② 观察者门幸存:playerTargetable=false → 瞪视判定不达(敌对派发整体旁路,t290 门),长窗恒不激怒。
        {
            World w;
            EntityManager em;
            int nw = -1;
            QVector3D eye;
            setupPumpkinStareRig(w, em, nw, eye, 24, 24);
            const bool obsEnraged = drivePumpkinStare(em, w, nw, eye, 400, false);
            ok = ok && !obsEnraged;
            if (obsEnraged) diag += QStringLiteral("[observer enraged=true]");
        }
        // ③④ 既有门行源钉幸存(entitymanager.cpp 三门行 + hotbar.cpp 右键装备门行)。
        const QString emRoot = srcRootForPumpkinHelmetPins();
        const QStringList missC = pinSet(emRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("nearest-gate", "const int nearest = nearestEnragableNightwalker(m_playerEye, kNightwalkerStareRange);"),
            SrcPin("enraged-sight-gate", "if (!e.enraged && m_playerSightValid) {"),
            SrcPin("lookaway-reset", "e.enrageTimer = 0.0f;", 2)});
        const QStringList missC2 = pinSet(emRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("right-click-armor-gate", "if (!ArmorRegistry::isArmor(sel.id)) return false;")});
        const bool t34 = missC.isEmpty() && missC2.isEmpty();
        ok = ok && t34;
        if (!t34) diag += QStringLiteral("[pins t34=%1 %2 %3]").arg(t34).arg(missC.join(QStringLiteral(","))).arg(missC2.join(QStringLiteral(",")));
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  r2091c diag:" << diag;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2091c unrelated faces column (a shambler still detects chases and strikes a"
               " pumpkin-wearing player with at least one attack signal, the observer gate keeps a stare"
               " geometry nightwalker calm for a player non targetable across a long window, and the"
               " existing enrage gate rows plus the right-click armor gate row survive on file)";
    });

    // ── r2091d:结构钉族(NEG 双摘面豁免不钉——摘面行归 a/b 专权)────────────────────────────────
    //   注入链面(setter 声明 / 成员 / PlayerController 注入行)+ 呈现三面(Main.qml overlay + 南瓜头 +
    //   二面板图标分流 + 预览壳层让位)+ 二 QML 谓词调用行 + InventoryOps 守卫行 + 护甲槽持久化既有行
    //   (存档零新表)+ 死亡掉落链覆盖行(t755 raw id 透传)+ CMake 段行 + era 锚注自钉 + 双负面钉
    //   (Entities 层零装备槽直读 / 零 Game 层 include)+ 创造门派生行幸存。
    runLeg("r2091d structure pin column (the head block injection declaration member and player"
        " controller wiring rows, the overlay wiring and pumpkin head and both panel icon split"
        " rows and the preview shell yield row, both panel predicate call rows and the inventory"
        " ops guard row, the existing armor gather and apply persistence rows and the death drop"
        " chain row, the survival mode derivation row, the cmake section row and the era anchor"
        " notes are pinned, and the entities layer stays free of armor slot reads and of any game"
        " layer include)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPumpkinHelmetPins();
        // (1) 注入链面(entitymanager.h 声明 + 成员;playercontroller.cpp 注入行)。
        const QStringList miss1 = pinSet(root + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("setter-decl", "void setPlayerHeadBlock(int itemId);"),
            SrcPin("member", "int m_playerHeadBlock = 0;"),
            SrcPin("valid-flag", "bool m_playerHeadBlockValid = false;")});
        const QStringList miss2 = pinSet(root + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("injection-row", "m_entityManager->setPlayerHeadBlock(m_hotbar ? m_hotbar->armorBlockIdAt(0) : 0);"),
            SrcPin("survival-derivation", "m_mode == Survival, m_mode == Spectator,")});
        ok = ok && miss1.isEmpty() && miss2.isEmpty();
        if (!(miss1.isEmpty() && miss2.isEmpty()))
            diag += QStringLiteral("[injection %1 %2]").arg(miss1.join(QStringLiteral(","))).arg(miss2.join(QStringLiteral(",")));
        // (2) 呈现三面:Main.qml overlay 接线 + 南瓜头 Model + 盔甲壳互斥;二面板图标分流;预览壳层让位。
        const QString mainQml = root + QStringLiteral("/ui/Main.qml");
        const bool t2 =
            rawContainsPumpkinHelmet(mainQml, QStringLiteral("property int headBlockId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(0) : 0"))
            && rawContainsPumpkinHelmet(mainQml, QStringLiteral("visible: window.appState === \"playing\" && headBlockId === 100"))
            && rawContainsPumpkinHelmet(mainQml, QStringLiteral("id: playerArmorPumpkinHead"))
            && rawContainsPumpkinHelmet(mainQml, QStringLiteral("visible: headId === 100"))
            && rawContainsPumpkinHelmet(mainQml, QStringLiteral("visible: armId !== 0 && armId !== 100"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/ui/SurvivalInventory.qml"),
                                        QStringLiteral("visible: armId !== 0 && !root.hotbar.isArmor(armId)"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/ui/Inventory.qml"),
                                        QStringLiteral("visible: armId !== 0 && !root.hotbar.isArmor(armId)"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/ui/CharacterPreview3D.qml"),
                                        QStringLiteral("visible: root.headArmor !== 0 && root.headArmor !== 100"));
        ok = ok && t2;
        if (!t2) diag += QStringLiteral("[presentation t2=%1]").arg(t2);
        // (3) QML 谓词调用行 ×2 + InventoryOps 守卫行(单一权威消费面恰三处)。
        const bool t3 =
            rawContainsPumpkinHelmet(root + QStringLiteral("/ui/SurvivalInventory.qml"),
                                     QStringLiteral("if (!root.hotbar.armorSlotAccepts(index, heldId)) return"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/ui/Inventory.qml"),
                                        QStringLiteral("if (!root.hotbar.armorSlotAccepts(index, heldId)) return"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/ui/InventoryOps.js"),
                                        QStringLiteral("&& !root.hotbar.armorSlotAccepts(srcIdx, dst.id)) return"));
        ok = ok && t3;
        if (!t3) diag += QStringLiteral("[predicateCalls t3=%1]").arg(t3);
        // (4) 存档零新表面:护甲槽 gather/apply 既有行(gatherPlayerState / applyPlayerState,raw id
        //     透传——armorSetStack 谓词即唯一门,本单零存档表新增)+ 死亡掉落链覆盖行(t755 逐槽无
        //     isArmor 过滤 → 南瓜随链掉落)。
        const bool t4 =
            rawContainsPumpkinHelmet(mainQml, QStringLiteral("id: hotbarVM.armorBlockIdAt(k), count: hotbarVM.armorCountAt(k), durability: hotbarVM.armorDurabilityAt(k),"))
            && rawContainsPumpkinHelmet(mainQml, QStringLiteral("hotbarVM.armorSetStack(k, s.id, s.count,"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/Game/playercontroller.cpp"),
                                        QStringLiteral("dropStack(m_hotbar->armorBlockIdAt(i), m_hotbar->armorCountAt(i)"));
        ok = ok && t4;
        if (!t4) diag += QStringLiteral("[persistence t4=%1]").arg(t4);
        // (5) hotbar.h 谓词声明行 + CMake 段行 + era 锚注自钉(裁定表关键串在本段文件裸文本在场)。
        const bool t5 =
            rawContainsPumpkinHelmet(root + QStringLiteral("/Game/hotbar.h"),
                                     QStringLiteral("Q_INVOKABLE bool armorSlotAccepts(int slot, int itemId) const;"))
            && rawContainsPumpkinHelmet(root + QStringLiteral("/../CMakeLists.txt"),
                                        QStringLiteral("tools/matrix/section84_pumpkin_helmet_t1121.cpp"))
            && rawContainsPumpkinHelmet(QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/matrix/section84_pumpkin_helmet_t1121.cpp"),
                                        QStringLiteral("misc/pumpkinblur.png"))
            && rawContainsPumpkinHelmet(QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/matrix/section84_pumpkin_helmet_t1121.cpp"),
                                        QStringLiteral("南瓜可以被玩家戴在头上"));
        ok = ok && t5;
        if (!t5) diag += QStringLiteral("[decl+cmake+era t5=%1]").arg(t5);
        // (6) 双负面钉:Entities 层零装备槽直读(entitymanager.cpp/h 全文零 armorBlockIdAt 字面——
        //     正面读路面由 PlayerController 注入行独占)+ 零 Game 层 include(entitymanager 两文件零
        //     hotbar.h 字面)。负面 needle 选词已与本段新增注释措辞零重叠(规避在案)。
        const QString emCpp = root + QStringLiteral("/Entities/entitymanager.cpp");
        const QString emH = root + QStringLiteral("/Entities/entitymanager.h");
        const bool t6 = rawCountPumpkinHelmet(emCpp, QStringLiteral("armorBlockIdAt")) == 0
                        && rawCountPumpkinHelmet(emH, QStringLiteral("armorBlockIdAt")) == 0
                        && rawCountPumpkinHelmet(emCpp, QStringLiteral("hotbar.h")) == 0
                        && rawCountPumpkinHelmet(emH, QStringLiteral("hotbar.h")) == 0;
        ok = ok && t6;
        if (!t6) diag += QStringLiteral("[negative t6=%1 cppArmor=%2 hArmor=%3 cppInc=%4 hInc=%5]")
                            .arg(t6).arg(rawCountPumpkinHelmet(emCpp, QStringLiteral("armorBlockIdAt")))
                            .arg(rawCountPumpkinHelmet(emH, QStringLiteral("armorBlockIdAt")))
                            .arg(rawCountPumpkinHelmet(emCpp, QStringLiteral("hotbar.h")))
                            .arg(rawCountPumpkinHelmet(emH, QStringLiteral("hotbar.h")));
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  r2091d diag:" << diag;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2091d structure pin column (the head block injection declaration member and player"
               " controller wiring rows, the overlay wiring and pumpkin head and both panel icon split"
               " rows and the preview shell yield row, both panel predicate call rows and the inventory"
               " ops guard row, the existing armor gather and apply persistence rows and the death drop"
               " chain row, the survival mode derivation row, the cmake section row and the era anchor"
               " notes are pinned, and the entities layer stays free of armor slot reads and of any game"
               " layer include)";
    });
}
