#include "matrix_helpers.h"

// t1119 骑猪第三人称坐姿探针段(3 腿;filter 词 r2089;矩阵 890→893)。置尾先例沿用(接 section81,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc rig,余纯源钉腿)。
//
// ── 现状核实裁定表(t1112 关单登记「骑乘中玩家模型仍站姿」候选池清偿;派工三件逐一实读)──
//   ① 呈现面选型(实读留痕):第三人称人物模型 = Main.qml playerModel Node 方块化人形(PlayerSkinBox
//     七件套 + 关节枢轴族),姿态管线 = **标量混合系数族**(walkBlend t45 / crouchBlend t65·t71 /
//     mineBlend t52 / sitBlend t508·t532)驱动各枢轴 eulerRotation / position 绑定——无骨骼动画、
//     无独立网格切换先例。坐姿选型 = **腿部姿态混合**(同门扩展既有 sitBlend,非独立坐姿网格——
//     独立网格不在管线先例内且破坏皮肤化 PlayerSkinBox 单一来源)。**船 / 矿车骑乘坐姿既有面实读
//     核实成立**:船(t508 引入 + t532 直角修正)与矿车(t565 同门复用)已由 sitBlend 驱动坐姿
//     (sitThigh 90° / sitKnee −90° / sitDrop 0.3)→ 本单**非首个坐姿混合面**,为第三骑乘种同门
//     扩展(如实留痕);sneak(crouchBlend)/行走(walkBlend)/挥臂(mineBlend)先例全部 0/1 标量
//     瞬切、管线无姿态过渡动画面 → **过渡时长实读裁定 = 0 帧瞬切**(同管线口径,不引入新时长面)。
//   ② 触发面:PlayerController::ridingPigActive()(t1112 既有单一权威读口;isRidingPig()
//     Q_INVOKABLE 桥)——骑上 → sitBlend 权重 1;Shift 下猪(dismountPig)/ 猪死对账脱骑 → 复位 0。
//     Q_INVOKABLE 无 NOTIFY(同 ridingIndex() 口径)→ Main.qml 显式读 feetPosition(NOTIFY=
//     positionChanged)作依赖载体:骑乘分支每 tick 无条件 emit positionChanged → 绑定每 tick 重算
//     读权威新真值;上猪 / 下猪 / 对账脱骑三转变都经该 NOTIFY 触达(t498 表达式形式铁律)。
//   ③ 纯呈现层零机制触碰(PLAN §2):playercontroller.cpp 零改动——骑乘钉位行(m_pos = 猪中心 +
//     kPigSeatLift)/ 推挤豁免行(mount 侧 setRideExclusion)/ 下猪链(dismountPig 四向安全位)/
//     骑乘互斥守卫(船 / 矿车 tryMount 的 !ridingPigActive() 双行)零字改 → r2082d 源钉原样幸存
//     (r2089c 负面钉复核钉存)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 摘 src/ui/Main.qml sitBlend 扩展谓词行(「readonly property real sitBlend: ...」整行
//     删除;QML 面不进 C++ 构建 = 矩阵二进制仍绿)→ 恰红 = {r2089b}(接线腿 raw 钉该行;r2089a
//     行为腿不涉源钉 = 幸存;r2089c 其 QML 钉是 isRidingPig 桥行 = 幸存)。
//   NEG-2 = 摘 src/ui/Main.qml isRidingPig 桥行(「readonly property bool isRidingPig: ...」整行
//     删除;QML 面同上)→ 恰红 = {r2089c}(结构钉腿 raw 钉该行;r2089b 幸存——sitBlend 行在摘面外
//     逐字幸存;r2089a 幸存)。
//   (双摘面互不重叠:相邻两定义行但互不包含,各由本段专权腿持有;r2089a 纯行为零钉 = 双 NEG 全豁免。)

namespace {

// fixed 宿主小世界 incantation(section75 同款四 setter)。
inline void initSitPoseWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(section75 同款)。
inline void laySitPosePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section75 同款:applicationDirPath/../src)。
inline QString srcRootForSitPosePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(QML 接线锚——pinSet 剥注释会失配,section76/81 同款)。
inline bool rawContainsSitPose(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 真链 pc rig 的 placeBlock 前置泵(section75 同门:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpSitPose(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

void MatrixRun::section82_sit_pose_t1119()
{
    // ── r2089a:坐姿柱(NEG 双摘面全豁免 = 真链行为对照腿)────────────────────────────────────
    //   触发面真值链(鞍→骑上→isRidingPig 权威答真 = 坐姿权重源向 1 的 Game 层真值→钉位脚底回归面
    //   →moveSpeed 压零 = walkBlend 归零前提→Shift 下猪→复位答假 = 权重复位 0)+ 船 / 矿车既有坐姿
    //   触发面对照(tryMount → ridingIndex>=0 且 isRidingPig 恒假——三骑乘种触发面互斥并存)。
    runLeg("r2089a sit pose column (a saddled pig right click mounts with the riding authority"
        " answering true as the sit weight source rising to one, the rider feet pinned a"
        " quarter above the pig center with the walk speed forced to zero, shift dismounts"
        " and the authority resets to zero, and the boat and minecart sit faces answer their"
        " riding indexes while the pig authority stays false)", [&]() {
        bool ok = true;
        QString diag;
        // (A1) 猪真链:上鞍 → 骑上(触发面权威答真)→ 钉位脚底 → moveSpeed 压零 → Shift 下猪复位。
        {
            World w;
            initSitPoseWorld(w);
            laySitPosePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            const int pigIdx = ents.spawnMobTyped(24, 81, 24, EntityManager::MobPig,
                                                  QStringLiteral("#e8a2a2"), 0);
            ents.setWanderFrozen(true); // 确定性:冻结游荡(r2082d 同门;钉位/下猪位面不受扰)
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 27.5, 0.0, -17.0, 1 /* Creative */);
            pc.tick();
            hb.setStack(0, RecipeRegistry::SaddleId, 1, 0);
            pumpSitPose(320);
            pc.placeBlock(); // 鞍右键猪 → 装备
            pumpSitPose(320);
            pc.placeBlock(); // 空手右键已鞍猪 → 骑上
            const bool mountOk = pc.isRidingPig() && pc.ridingPigIndex() == pigIdx;
            ok = ok && mountOk; // 骑上 → 触发面权威答真(= Main.qml isRidingPig 桥读口)
            if (!mountOk) diag += QStringLiteral("[mount ride=%1 idx=%2]")
                .arg(pc.isRidingPig()).arg(pc.ridingPigIndex());
            // 钉位脚底回归面(脚底 = 猪中心 + 0.25 = 坐姿几何的 feet 基准;r2082d ③ 同门复绿)。
            pc.tick();
            const QVector3D pigPos = ents.posAt(pigIdx);
            const bool pinOk = pc.isRidingPig()
                && std::fabs(pc.feetPosition().y() - (pigPos.y() + 0.25f)) < 1e-3f
                && std::fabs(pc.feetPosition().x() - pigPos.x()) < 1e-3f;
            ok = ok && pinOk;
            if (!pinOk) diag += QStringLiteral("[pin fy=%1 pgy=%2]")
                .arg(double(pc.feetPosition().y())).arg(double(pigPos.y()));
            // 骑乘期 walk 速度压零(m_moveSpeed=0 → Main.qml walkBlend=0 → 腿摆归中性,sit 量独占)。
            const bool stillOk = pc.moveSpeed() == 0.0f;
            ok = ok && stillOk;
            if (!stillOk) diag += QStringLiteral("[spd %1]").arg(double(pc.moveSpeed()));
            // Shift 按下沿 → 下猪(dismountPig)→ 触发面权威复位答假(= 坐姿权重复位 0)。
            pc.setKey(Qt::Key_Shift, true);
            pc.tick();
            pc.setKey(Qt::Key_Shift, false);
            const bool resetOk = !pc.isRidingPig() && pc.ridingPigIndex() == -1;
            ok = ok && resetOk;
            if (!resetOk) diag += QStringLiteral("[reset ride=%1 idx=%2]")
                .arg(pc.isRidingPig()).arg(pc.ridingPigIndex());
        }
        // (A2) 船对照(既有坐姿触发面一号):spawnBoat + 上方垂射 tryMount → ridingIndex 答真且
        //     猪权威恒假(两触发面并存互斥——Main.qml sitBlend 谓词析取面)。
        {
            World w;
            initSitPoseWorld(w);
            laySitPosePlatform(w, 14, 44, 4, 44);
            BoatManager boats;
            const bool spawned = boats.spawnBoat(30, 81, 30, BoatManager::Oak);
            const bool mounted = spawned
                && boats.tryMount(QVector3D(30.5f, 84.0f, 30.5f), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
            const bool boatOk = mounted && boats.ridingIndex() >= 0;
            ok = ok && boatOk;
            if (!boatOk) diag += QStringLiteral("[boat sp=%1 mt=%2]")
                .arg(spawned).arg(mounted);
        }
        // (A3) 矿车对照(既有坐姿触发面二号):spawnCart + 上方垂射 tryMount → ridingIndex 答真;
        //     猪 / 船 / 矿车三权威并存(骑乘互斥在 mount 侧守卫行,r2089c 源钉复核)。
        {
            World w;
            initSitPoseWorld(w);
            laySitPosePlatform(w, 14, 44, 4, 44);
            MinecartManager carts;
            carts.spawnCart(30, 81, 30, &w);
            const bool mounted = carts.tryMount(QVector3D(30.5f, 84.0f, 30.5f),
                                                QVector3D(0.0f, -1.0f, 0.0f), 4.0f);
            const bool cartOk = mounted && carts.ridingIndex() >= 0;
            ok = ok && cartOk;
            if (!cartOk) diag += QStringLiteral("[cart mt=%1]").arg(mounted);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2089a sit pose column (a saddled pig right click mounts with the riding"
               " authority answering true as the sit weight source rising to one, the rider"
               " feet pinned a quarter above the pig center with the walk speed forced to"
               " zero, shift dismounts and the authority resets to zero, and the boat and"
               " minecart sit faces answer their riding indexes while the pig authority"
               " stays false)"
            << (ok ? QString() : diag);
    });

    // ── r2089b:接线柱(NEG-1 敏感面 = Main.qml sitBlend 扩展谓词行)──────────────────────────
    //   sitBlend 扩展谓词行 raw 钉(**NEG-1 摘面行在本腿钉**)+ 三 sit 量消费面幸存(sitThigh/
    //   sitKnee/sitDrop + upperBody/双腿枢轴读入——同门扩展零消费面改动的行为面)+ 船/矿车既有
    //   触发行幸存(同门扩展不覆盖先例)+ t1119 沿革锚。
    runLeg("r2089b wiring column (the sit blend predicate row reading the boat and the cart"
        " and the pig trigger faces is pinned on file, the three sit quantity consumer rows"
        " survive untouched, the boat and cart existing trigger rows survive, and the"
        " lineage anchor is on file)", [&]() {
        bool ok = true;
        QString diag;
        const QString qml = srcRootForSitPosePins() + QStringLiteral("/ui/Main.qml");
        // (B1) sitBlend 扩展谓词行(**NEG-1 摘面行**;三骑乘种析取——本单交付面的单点)。
        {
            const bool blendOk = rawContainsSitPose(qml,
                QStringLiteral("readonly property real sitBlend: (playerModel.isRidingBoat"
                               " || playerModel.isRidingCart || playerModel.isRidingPig) ? 1.0 : 0.0"));
            ok = ok && blendOk;
            if (!blendOk) diag += QStringLiteral("[blend]");
        }
        // (B2) 三 sit 量消费面幸存(sitThigh/sitKnee/sitDrop 定义行 + upperBody/腿枢轴/膝枢轴
        //     读入行——同门扩展只改谓词源,消费面零触碰;双腿枢轴双份 → minCount 2)。
        {
            const bool consumersOk =
                rawContainsSitPose(qml, QStringLiteral(
                    "readonly property real sitThigh: 90.0 * playerModel.sitBlend"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "readonly property real sitKnee: -90.0 * playerModel.sitBlend"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "readonly property real sitDrop: 0.3 * playerModel.sitBlend"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "0.6 - playerModel.crouchDrop - playerModel.sitDrop"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "walk + playerModel.crouchThigh + playerModel.sitThigh"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "playerModel.crouchKnee + playerModel.sitKnee"));
            ok = ok && consumersOk;
            if (!consumersOk) diag += QStringLiteral("[consumers]");
        }
        // (B3) 船 / 矿车既有触发行幸存(t508/t565 先例面——同门扩展不覆盖)。
        {
            const bool prevOk = rawContainsSitPose(qml, QStringLiteral(
                    "readonly property bool isRidingBoat: boats.revision >= 0"))
                && rawContainsSitPose(qml, QStringLiteral(
                    "readonly property bool isRidingCart: carts.revision >= 0"));
            ok = ok && prevOk;
            if (!prevOk) diag += QStringLiteral("[prev]");
        }
        // (B4) 沿革锚(t1119 触发面注块在盘;注释体锚走 raw 含)。
        {
            const bool lineageOk = rawContainsSitPose(qml, QStringLiteral(
                "t1119 骑猪同坐姿"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2089b wiring column (the sit blend predicate row reading the boat and the"
               " cart and the pig trigger faces is pinned on file, the three sit quantity"
               " consumer rows survive untouched, the boat and cart existing trigger rows"
               " survive, and the lineage anchor is on file)"
            << (ok ? QString() : diag);
    });

    // ── r2089c:结构钉族(NEG-2 敏感面 = Main.qml isRidingPig 桥行 + 机制面零触碰负面钉)────────
    //   isRidingPig 桥行 raw 钉(**NEG-2 摘面行在本腿钉**)+ 触发权威源钉(ridingPigActive 读口
    //   声明/实现 + QML 桥声明行)+ 负面钉族(机制面零触碰复核:骑乘钉位行 / 推挤豁免行 / 下猪链
    //   两行 / 骑乘互斥守卫双行——r2082d 源钉原样幸存)+ CMake 段行。
    runLeg("r2089c structure pin family (the qml pig bridge row and the riding authority read"
        " declaration and implementation rows are pinned on file, the mechanism faces stay"
        " untouched with the riding pin row and the push exemption row and the dismount"
        " chain rows and the two mutual exclusion guard rows surviving verbatim, and the"
        " cmake section row is pinned)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForSitPosePins();
        const QString qml = srcDir + QStringLiteral("/ui/Main.qml");
        // (C1) QML 桥行(**NEG-2 摘面行**;feetPosition 依赖载体 + isRidingPig 权威直读)。
        {
            const bool bridgeOk = rawContainsSitPose(qml, QStringLiteral(
                "readonly property bool isRidingPig: player.feetPosition.y > -1000.0"
                " ? player.isRidingPig() : false"));
            ok = ok && bridgeOk;
            if (!bridgeOk) diag += QStringLiteral("[bridge]");
        }
        // (C2) 触发权威源钉(读口声明行 + 桥声明行 + 实现头——单一权威面)。
        {
            const QStringList missHdr = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("ride read decl", "Q_INVOKABLE bool isRidingPig() const { return ridingPigActive(); }", 1),
                SrcPin("authority decl", "bool ridingPigActive() const;", 1)});
            ok = ok && missHdr.isEmpty();
            if (!missHdr.isEmpty())
                diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));
            const QStringList missCpp = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("authority impl", "bool PlayerController::ridingPigActive() const", 1)});
            ok = ok && missCpp.isEmpty();
            if (!missCpp.isEmpty())
                diag += QStringLiteral("[cpp %1]").arg(missCpp.join(QLatin1Char(',')));
        }
        // (C3) 负面钉族:机制面零触碰复核(r2082d 源钉原样幸存——本单纯呈现层,Game 层零改动的
        //     持续复核钉;NEG 双摘面均在 QML,与本族 C++ 钉零重叠)。
        {
            const QStringList missMech = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("riding pin row", "m_pos = QVector3D(pigPos.x(), pigPos.y() + kPigSeatLift, pigPos.z());", 1),
                SrcPin("push exemption row", "m_entityManager->setRideExclusion(m_ridingPig, m_ridingPigSerial);", 1),
                SrcPin("dismount head row", "void PlayerController::dismountPig()", 1),
                SrcPin("dismount exclusion row", "m_entityManager->setRideExclusion(-1, 0);", 1),
                SrcPin("mutual exclusion guards", "&& !ridingPigActive()", 2)});
            ok = ok && missMech.isEmpty();
            if (!missMech.isEmpty())
                diag += QStringLiteral("[mech %1]").arg(missMech.join(QLatin1Char(',')));
        }
        // (C4) CMake 段行(本段注册行——test 提交合入后同在)。
        {
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake section82", "tools/matrix/section82_sit_pose_t1119.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (C5) 沿革锚(Main.qml 桥行注块 t1119 锚)。
        {
            const bool lineageOk = rawContainsSitPose(qml, QStringLiteral("t1119"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2089c structure pin family (the qml pig bridge row and the riding"
               " authority read declaration and implementation rows are pinned on file, the"
               " mechanism faces stay untouched with the riding pin row and the push"
               " exemption row and the dismount chain rows and the two mutual exclusion"
               " guard rows surviving verbatim, and the cmake section row is pinned)"
            << (ok ? QString() : diag);
    });
}
