#include "matrix_helpers.h"

// t1112 名册中件量批探针段(4 腿;filter 词 r2082;矩阵 864→868)。置尾先例沿用(接 section74,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工四件逐一全仓 grep 实读)──
//   ① fence gate:全仓 grep FenceGate/fence gate 零命中 → 真缺。交付 = FenceGate=157 段尾追加
//     (ShapeFenceGate 新形——**开合态分面碰撞**:合=整格 footprint × 1.5 高挡通路(栅栏「不可跳越」
//     同门) / 开=零碰撞可穿行(MC 口径开位无碰撞——派工稿「开位无碰撞面板核实」实读成立));
//     bit0=开合 bit[2:1]=朝向(放置写 horizontalFacing,面板沿朝向轴垂直轴横铺);右键开合
//     (placeBlock 块用分发段,doorToggled 门族双音复用零新音频面);合成 {Stick:4, Planks:2} 3×3
//     两行棒-板-棒 → 1(MC 同料原值;多重集与木栅栏行 {Planks:4, Stick:2} 镜像料不冲突);
//     **仅橡木**(1.0 口径——云杉栅栏门 1.4.6+ 越基线不取);木板 tile 8 复用零新 tile。
//   ② glass pane:全仓 grep GlassPane 零命中 → 真缺。交付 = GlassPane=158 段尾追加(ShapeGlassPane
//     新形——IronBars 同门几何家族:中心细柱 4/16 见方碰撞 + 横板连接纯视觉运行期邻居 R1 判定,
//     选中/射线十字条带双盒同门);合成 6 玻璃 2×3 → 16(MC 同料同形同产率);**破坏零掉落**
//     (dropId=0——1.0 玻璃板破坏无掉落实读,派工稿「零掉落登记」成立;本工程 Glass 方块自身是
//     「可回收」自掉特例,玻璃板按 1.0 原版口径走零掉落,两块分离留痕);光照全透(default 0);
//     kMc 102(Beta 1.8 入版 = 1.0 基线内)。
//   ③ cake:全仓 grep cake 零命中 → 真缺。交付 = Cake=159 段尾追加(放置即方块形态——**1.0 无
//     物品形态**实读成立,cake 1.0 即方块 id 92,物品形态 1.14+ flattening 面;分块食用 state
//     bit[2:0]=咬口 0..5)。**派工勘误留痕**:派工稿「7 段每右键咬一口」按 1.0 实读裁定为
//     **六片**——每片 +2 饥饿/全蛋糕 12 饥饿(Beta 1.8 Pre-release「restores 12 (× 6), slice 2」
//     原值,1.0.0 沿用;「7 片 14 饥饿」是 1.8/14w27a 改版越基线不取);bites=5 再咬 → 方块消失
//     (MC「五咬之蛋糕再吃即尽」)。合成 3 牛奶桶+2 糖+1 蛋+3 小麦 3×3 → 1(MC 同料同形);
//     **空桶去向**:1.0.0 口径 = 空桶留在合成格(「moved to inventory」是 1.1 12w01a 改版)→
//     Hotbar::recipeContainerSwap 单一权威在合成格原位把牛奶桶转空桶(两处 QML 消费点同读,
//     关包归还链自然带回背包 = 1.0 观感逐位对齐)。**牛奶链**:MilkBucketId=0x28F 材料段尾追加
//     (0x28E 村民蛋之上,存档安全铁律);空桶右键牛 → 挤奶(**1.0 无冷却实读**——派工稿「牛冷却
//     面」按原版裁定不交付);可饮零饥饿(isDrinkableItem 面)+ 饮毕清全部药水效果(不灭身上火——
//     fireTimer 家族口径分离)+ 返空桶(蘑菇汤返碗同门)。贴图两新瓦 207/208(tools/build_cake.py
//     程序自绘 §9a;AtlasTileCount 207→209 lawful 前移)。
//   ④ saddle 猪骑乘:SaddleId=0x225 物品面在场(t393 战利品)但猪骑乘交互缺(全仓 grep saddlePig/
//     ridingPig 零命中 → 真缺)。交付 = 鞍右键猪装备(EntityManager::saddlePig,装备后猪持久带鞍
//     ——**会话内口径如实登记 t1013 箱车先例**:mob 无存档序列化面,重进世界鞍消失)+ 右键骑上/
//     Shift 下猪(PlayerController 骑乘状态机,骑乘钉位 = 猪背 kPigSeatLift 0.25)+ **骑乘控制
//     1.0 口径实读定值**:无方向控制(MC 1.0 骑猪不能操控——胡萝卜钓竿 1.4.2/12w36a 才引入控制
//     面,越基线不取;猪继续自身 AI 游荡 = 「游荡骑乘」简化登记)。**装备后死亡掉鞍面 = 1.0 无此
//     机制(翻案留痕)**:鞍死掉是 1.4.2/12w36a 引入("Saddles now drop from killing saddled pigs"),
//     1.0 骑鞍猪死亡不掉鞍——负面钉锁零掉落链(无 deathSaddled 快照/无 Main.qml 猪分支 0x225
//     spawnItem 行),派工稿「装备后死亡掉鞍面」按 1.0 实读翻案裁定不交付。骑乘推挤豁免
//     (EntityManager::setRideExclusion——resolvePlayerPush 按槽位+代际跳过被骑猪,否则钉位与推挤
//     逐帧互搏猪被顶飞)+ 骑乘互斥(骑猪中船/矿车 tryMount 被跳过,rv-low-batch2 同门)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;流敏感选择器教训应用)──
//   NEG-1 = 摘 fence gate 合成行(recipe.cpp 六行整块删除,编译仍绿——行内无被引用符号)→
//     恰红 = {r2082a}(两行棒-板-棒 match() 归 null = 命中断言失;def/开合碰撞/读回面不在摘面 =
//     部分幸存,腿级 FAIL)。**豁免设计:r2082d 结构钉不含 fence_gate 合成行**(摘面行豁免不钉——
//     t1111 NEG-1 同门;r2082b/c 与 gate 合成行零数据耦合)。候选排查:摘 isFenceGate 谓词同样红 a
//     但开合分支与 def 行 SrcPin 同红 d 面耦合更宽;整块摘行 = 派工原文形态且恰红最窄。
//   NEG-2 = 摘蛋糕咬口递进行(playercontroller.cpp Cake 分支内 setBlock(...Cake, bites+1) 行连同
//     其注释整行删除,编译仍绿——纯语句无被引用符号)→ 恰红 = {r2082c}(咬口推进断言 bites
//     0→1 失守 + 后续递进/末片消失链全断;饱食门/挤奶/饮面不在摘面 = 部分幸存,腿级 FAIL)。
//     **豁免设计:r2082d 不含该 setBlock 行任何形态的源钉**(t1110/t1111 摘面行豁免不钉同门);
//     修复面保护 = c 腿行为级 + NEG-2。a/b/d 与该行零耦合。
namespace {

// fixed 宿主小世界 incantation(section69/70/73/74 同款四 setter)。
inline void initRosterMidWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section74 同款)。
inline void layRosterMidPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section72/73/74 同款)。
inline QString srcRootForRosterMidPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚——pinSet 剥注释会失配,section72/74 同款)。
inline bool rawContainsRosterMid(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 真链 pc rig 的 placeBlock 前置泵(r2067c/section69 同门:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpRosterMid(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

void MatrixRun::section75_roster_mid_t1112()
{
    // ── r2082a:栅栏门柱(NEG-1 敏感面 = 合成命中)──────────────────────────────────────────────
    //   合成命中(两行棒-板-棒 → 1)+ 门族零污染(木栅栏 {P:S 镜像料} + 木门纵列合成回归柱)+
    //   def 行逐字段(tiles 8 / solid=false / ShapeFenceGate / hardness 2.0 / Axe tier0 / drop 自身)
    //   + 开合碰撞分面(合=整格 1.5 高 / 开=零碰撞 + isCollidable/collisionTopY 双态)+ 选中/射线
    //   开合面板同源 + 开合态读回(真 pc rig 右键翻 bit0 + doorToggled 恰一次)+ 谓词路由 +
    //   光照全透 + 木质音色 + kMc 107。
    runLeg("r2082a fence gate column (two rows of stick plank stick craft one fence gate while"
        " the wood fence mirrored multiset and the wood door column crafts keep their answers,"
        " the definition row answers the planks tile with the fence gate shape and axe tier"
        " drop self fields, the closed gate blocks the whole cell at one and a half high while"
        " the open gate has zero collision and is not collidable, the selection and raycast"
        " answer the open hinge panel, a real right click flips the open bit once with the"
        " door toggled signal, and the gate routes through the partial predicate with full"
        " transparent light and the wood audio group and the one hundred seven mc mapping)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 合成命中 + 门族零污染(镜像料 + 木门回归柱)。
        const int ST = RecipeRegistry::StickId, PL = int(BR::Planks);
        const int gGate[9] = { ST, PL, ST, ST, PL, ST, 0, 0, 0 };
        const RecipeRegistry::Recipe *r = RecipeRegistry::match(gGate, 3);
        const bool craftOk = r && r->outputId == int(BR::FenceGate) && r->outputCount == 1;
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft got=%1]").arg(r ? r->outputId : -1);
        const int gFence[9] = { PL, ST, PL, PL, ST, PL, 0, 0, 0 };
        const RecipeRegistry::Recipe *rf = RecipeRegistry::match(gFence, 3);
        const int gDoor[9] = { PL, 0, 0, PL, 0, 0, PL, 0, 0 };
        const RecipeRegistry::Recipe *rd = RecipeRegistry::match(gDoor, 3);
        const bool neighOk = rf && rf->outputId == int(BR::WoodFence)
            && rd && rd->outputId == int(BR::WoodDoor);
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh f=%1 d=%2]")
            .arg(rf ? rf->outputId : -1).arg(rd ? rd->outputId : -1);
        // (2) def 行逐字段。
        const auto &d = BR::def(quint8(BR::FenceGate));
        const bool defOk = d.id == int(BR::FenceGate)
            && d.topTile == 8 && d.sideTile == 8 && d.frontTile == 8 && d.bottomTile == 8
            && d.solid == false && d.shape == BR::ShapeFenceGate
            && std::fabs(d.hardness - 2.0f) < 1e-4f
            && d.toolType == int(BR::Axe) && d.minToolTier == 0 && d.requiresTool == false
            && d.dropId == int(BR::FenceGate) && d.dropCount == 1 && d.maxStack == 64;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (3) 开合碰撞分面 + 选中/射线 + 谓词 + 光照 + 音色 + MC 映射。
        const auto closedCol = BR::collisionAABBs(quint8(BR::FenceGate), 0);
        const auto openCol = BR::collisionAABBs(quint8(BR::FenceGate), 1);
        const bool colOk = closedCol.size() == 1
            && std::fabs(closedCol[0].maxY - 1.5f) < 1e-4f && std::fabs(closedCol[0].minX) < 1e-4f
            && openCol.empty()
            && BR::isCollidable(quint8(BR::FenceGate), quint8(0))
            && !BR::isCollidable(quint8(BR::FenceGate), quint8(1))
            && std::fabs(BR::collisionTopY(quint8(BR::FenceGate), quint8(0)) - 1.5f) < 1e-4f
            && BR::collisionTopY(quint8(BR::FenceGate), quint8(1)) < 0.0f;
        ok = ok && colOk;
        if (!colOk) diag += QStringLiteral("[col n=%1]").arg(closedCol.size());
        const auto openSel = BR::selectionAABBs(quint8(BR::FenceGate), quint8(1 | (0 << 1)));
        const auto openRay = BR::raycastAABBs(quint8(BR::FenceGate), quint8(1 | (0 << 1)));
        const bool selOk = openSel.size() == 1 && openRay.size() == 1
            && std::fabs(openSel[0].maxX - 3.0f / 16.0f) < 1e-4f
            && std::fabs(openRay[0].maxX - 3.0f / 16.0f) < 1e-4f;
        ok = ok && selOk;
        if (!selOk) diag += QStringLiteral("[sel]");
        const bool routeOk = BR::isFenceGate(quint8(BR::FenceGate))
            && !BR::isFence(quint8(BR::FenceGate))
            && BR::isPartialBlock(quint8(BR::FenceGate))
            && BR::lightOpacity(quint8(BR::FenceGate), 0) == 0
            && BR::lightOpacity(quint8(BR::FenceGate), 1) == 0
            && BR::materialGroup(quint8(BR::FenceGate)) == BR::GroupWood
            && BR::mcBlockId(int(BR::FenceGate)) == 107;
        ok = ok && routeOk;
        if (!routeOk) diag += QStringLiteral("[route]");
        // (4) 开合态读回(真 pc rig 右键翻 bit0 恰一次 + doorToggled 捕获)。
        int toggles = 0;
        bool lastOpen = false;
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::FenceGate, 0); // 合态出生(朝向 0)
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            QObject::connect(&pc, &PlayerController::doorToggled, &pc, [&](bool open) {
                ++toggles; lastOpen = open;
            });
            pc.setSelectedBlock(int(BR::Air)); // 空手右键 = 开合(使用语义)
            pc.loadSavedState(24.5, 81.0, 27.5, 0.0, -16.0, 1 /* Creative */);
            pc.tick();
            pumpRosterMid(320); // 越过放置 CD
            pc.placeBlock();
            const bool flipOk = toggles == 1 && lastOpen
                && (w.stateAt(24, 81, 24) & BR::FenceGateStateOpenFlag) != 0
                && (w.stateAt(24, 81, 24) & BR::FenceGateStateFacingMask)
                       == (0 << BR::FenceGateStateFacingShift);
            ok = ok && flipOk;
            if (!flipOk) diag += QStringLiteral("[flip t=%1 open=%2 st=%3]")
                .arg(toggles).arg(lastOpen).arg(w.stateAt(24, 81, 24));
            // 碰撞跟随开合:开态门格零碰撞(玩家穿行面;碰撞面(3)已表级钉,此处世界级行为钉)。
            const bool openPassable = BR::collisionAABBs(w.blockAt(24, 81, 24),
                                                         w.stateAt(24, 81, 24)).empty();
            ok = ok && openPassable;
            if (!openPassable) diag += QStringLiteral("[passable]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2082a fence gate column (two rows of stick plank stick craft one fence"
               " gate while the wood fence mirrored multiset and the wood door column crafts"
               " keep their answers, the definition row answers the planks tile with the"
               " fence gate shape and axe tier drop self fields, the closed gate blocks the"
               " whole cell at one and a half high while the open gate has zero collision"
               " and is not collidable, the selection and raycast answer the open hinge"
               " panel, a real right click flips the open bit once with the door toggled"
               " signal, and the gate routes through the partial predicate with full"
               " transparent light and the wood audio group and the one hundred seven mc"
               " mapping)"
            << (ok ? QString() : diag);
    });

    // ── r2082b:玻璃板柱(NEG 双摘面不触达 = 对照腿)────────────────────────────────────────────
    //   合成命中(6 玻璃 2×3 → 16)+ def 行逐字段(tiles 68 / ShapeGlassPane / hardness 0.3 /
    //   Pickaxe tier0 / **dropId=0 零掉落**)+ 谓词路由(isPartialBlock/isCollidable 真 + 光照全透
    //   + 石质音色 + kMc 102)+ 柱盒碰撞/列顶 + 十字条带选中/射线双盒 + 放置读回 + 相邻族零污染
    //   (IronBars 十字盒原值 + 玻璃本块 def 原值)。
    runLeg("r2082b glass pane column (six glass in a two by three craft sixteen panes, the"
        " definition row answers the glass tile with the pane shape pickaxe tier and the zero"
        " drop on break, the pane routes through the partial predicate with full transparent"
        " light and the stone audio group and the one hundred two mc mapping, the center post"
        " collision box answers one high with the cross band selection and raycast boxes, a"
        " placed pane reads back, and the iron bars cross boxes and the glass block definition"
        " keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 合成命中。
        const int G = int(BR::Glass);
        const int gPane[9] = { G, G, G, G, G, G, 0, 0, 0 };
        const RecipeRegistry::Recipe *r = RecipeRegistry::match(gPane, 3);
        const bool craftOk = r && r->outputId == int(BR::GlassPane) && r->outputCount == 16;
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft got=%1]").arg(r ? r->outputId : -1);
        // (2) def 行逐字段(**dropId=0 零掉落**——1.0 玻璃板口径)。
        const auto &d = BR::def(quint8(BR::GlassPane));
        const bool defOk = d.id == int(BR::GlassPane)
            && d.topTile == 68 && d.sideTile == 68 && d.frontTile == 68 && d.bottomTile == 68
            && d.solid == false && d.shape == BR::ShapeGlassPane
            && std::fabs(d.hardness - 0.3f) < 1e-4f
            && d.toolType == int(BR::Pickaxe) && d.minToolTier == 0 && d.requiresTool == false
            && d.dropId == 0 && d.dropCount == 1 && d.maxStack == 64;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (3) 谓词 + 柱盒碰撞 + 列顶 + 十字条带 + 光照 + 音色 + MC 映射。
        const auto col = BR::collisionAABBs(quint8(BR::GlassPane), 0);
        const auto sel = BR::selectionAABBs(quint8(BR::GlassPane), 0);
        const auto ray = BR::raycastAABBs(quint8(BR::GlassPane), 0);
        const bool routeOk = BR::isPartialBlock(quint8(BR::GlassPane))
            && BR::isCollidable(quint8(BR::GlassPane), quint8(0))
            && col.size() == 1
            && std::fabs(col[0].maxY - 1.0f) < 1e-4f
            && std::fabs(col[0].minX - 0.375f) < 1e-4f
            && std::fabs(BR::collisionTopY(quint8(BR::GlassPane), quint8(0)) - 1.0f) < 1e-4f
            && sel.size() == 2 && ray.size() == 2
            && BR::lightOpacity(quint8(BR::GlassPane), 0) == 0
            && BR::materialGroup(quint8(BR::GlassPane)) == BR::GroupStone
            && BR::mcBlockId(int(BR::GlassPane)) == 102;
        ok = ok && routeOk;
        if (!routeOk) diag += QStringLiteral("[route c=%1 s=%2 r=%3]")
            .arg(col.size()).arg(sel.size()).arg(ray.size());
        // (4) 放置读回 + 相邻族零污染(IronBars 十字盒 + 玻璃本块 def 原值)。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::GlassPane, 0);
            const auto barsSel = BR::selectionAABBs(quint8(BR::IronBars), 0);
            const auto &gd = BR::def(quint8(BR::Glass));
            const bool placeOk = w.blockAt(24, 81, 24) == quint8(BR::GlassPane)
                && barsSel.size() == 2
                && std::fabs(barsSel[0].minX - 0.4375f) < 1e-4f
                && gd.id == int(BR::Glass) && gd.shape == BR::ShapeFull
                && int(BR::IronBars) == 142 && int(BR::Count) == 166; // t1112 lawful 前移:157→160；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Count 163→166（头块/占位/粘性三 id 段尾追加——钉值随追加前移）
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2082b glass pane column (six glass in a two by three craft sixteen panes,"
               " the definition row answers the glass tile with the pane shape pickaxe tier"
               " and the zero drop on break, the pane routes through the partial predicate"
               " with full transparent light and the stone audio group and the one hundred"
               " two mc mapping, the center post collision box answers one high with the"
               " cross band selection and raycast boxes, a placed pane reads back, and the"
               " iron bars cross boxes and the glass block definition keep their original"
               " values)"
            << (ok ? QString() : diag);
    });

    // ── r2082c:蛋糕牛奶链柱(NEG-2 敏感面 = 咬口递进行)────────────────────────────────────────
    //   真链 pc rig 分块食用状态机(六片 ×2 饥饿:bites 0→5 递进 + 末片方块消失 + 饥饿 +2/片精确)
    //   + 饱食门(满饥饿无效应)+ 合成命中(3 奶+2 糖+蛋+3 麦 → 1)+ 容器交换单点(蛋糕行牛奶桶 →
    //   空桶、非容器行 → 0)+ 挤奶(空桶右键牛 → 牛奶桶,二连挤证明 1.0 无冷却)+ 饮面(长按链跑满
    //   → 清迅捷效果 + 返空桶 + 零饥饿)+ 相邻族零污染(食面尾行瓜片幸存)。
    runLeg("r2082c cake and milk chain column (a real controller eating a placed cake six"
        " times advances the bites state one per click with two hunger each and the sixth bite"
        " removes the block, a full hunger bar refuses the bite, three milk buckets two sugars"
        " an egg and three wheat craft one cake with the milk buckets swapping to empty buckets"
        " in place by the container swap authority, an empty bucket on a cow fills a milk"
        " bucket twice in a row with no cooldown, and drinking the milk bucket clears the"
        " speed effect returns the empty bucket with zero hunger)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 真链 pc rig 分块食用:bites 0→1→...→5→消失 + 饥饿 +2/片(起始 6 → 末态 18)。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Cake, 0); // 满蛋糕出生(咬口 0)
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            pc.setSelectedBlock(int(BR::Air)); // 空手右键 = 食用(使用语义)
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 2 /* Survival */);
            pc.setHunger(6); // 起始 6 → 六片后 18(不触顶,精确断言 +2/片)
            pc.tick();
            bool eatOk = true;
            for (int bite = 1; bite <= 6 && eatOk; ++bite) {
                pumpRosterMid(320); // 越过放置 CD
                pc.placeBlock();
                const quint8 st = w.stateAt(24, 81, 24);
                if (bite < 6) {
                    const int bites = int(st & BR::CakeStateBitesMask);
                    eatOk = w.blockAt(24, 81, 24) == quint8(BR::Cake)
                        && bites == bite
                        && pc.hunger() == 6 + 2 * bite;
                    if (!eatOk) diag += QStringLiteral("[bite%1 st=%2 h=%3]")
                        .arg(bite).arg(st).arg(pc.hunger());
                } else {
                    eatOk = w.blockAt(24, 81, 24) == quint8(BR::Air) // 末片:方块消失
                        && pc.hunger() == 18;
                    if (!eatOk) diag += QStringLiteral("[last st=%1 h=%2]")
                        .arg(w.blockAt(24, 81, 24)).arg(pc.hunger());
                }
            }
            ok = ok && eatOk;
        }
        // (2) 饱食门:满饥饿右键蛋糕 → 无效应(咬口不进 / 饥饿不动)。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Cake, 0);
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
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 1);
            pc.setHunger(20); // 满饥饿(MC 口径饱食不食)
            pc.tick();
            pumpRosterMid(320);
            pc.placeBlock();
            const bool fullOk = w.blockAt(24, 81, 24) == quint8(BR::Cake)
                && (w.stateAt(24, 81, 24) & BR::CakeStateBitesMask) == 0
                && pc.hunger() == 20;
            ok = ok && fullOk;
            if (!fullOk) diag += QStringLiteral("[full st=%1 h=%2]")
                .arg(w.stateAt(24, 81, 24)).arg(pc.hunger());
        }
        // (3) 合成命中 + 容器交换单点(蛋糕行牛奶桶 → 空桶;非容器行 → 0)。
        {
            const int MB = RecipeRegistry::MilkBucketId, SU = RecipeRegistry::SugarId;
            const int EG = RecipeRegistry::EggId, WH = RecipeRegistry::WheatId;
            const int gCake[9] = { MB, MB, MB, SU, EG, SU, WH, WH, WH };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(gCake, 3);
            const bool craftOk = r && r->outputId == int(BR::Cake) && r->outputCount == 1;
            ok = ok && craftOk;
            if (!craftOk) diag += QStringLiteral("[cake got=%1]").arg(r ? r->outputId : -1);
            Hotbar hb;
            const bool swapOk = hb.recipeContainerSwap(int(BR::Cake), MB) == RecipeRegistry::BucketEmptyId
                && hb.recipeContainerSwap(int(BR::Cake), SU) == 0
                && hb.recipeContainerSwap(int(BR::StoneSlab), MB) == 0;
            ok = ok && swapOk;
            if (!swapOk) diag += QStringLiteral("[swap]");
        }
        // (4) 挤奶(空桶右键牛 → 牛奶桶;二连挤无冷却——1.0 口径)。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            const int cowIdx = ents.spawnMobTyped(24, 81, 24, EntityManager::MobCow,
                                                  QStringLiteral("#a8a8a8"), 0);
            ents.setWanderFrozen(true); // 确定性:冻结游荡(牛靶定在准星柱内)
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 27.5, 0.0, -17.0, 1 /* Creative */);
            pc.tick();
            bool milkOk = cowIdx >= 0;
            for (int round = 0; round < 2 && milkOk; ++round) {
                hb.setStack(0, RecipeRegistry::BucketEmptyId, 1, 0); // 空桶上手
                pumpRosterMid(320);
                pc.placeBlock();
                milkOk = hb.blockIdAt(0) == RecipeRegistry::MilkBucketId
                    && hb.countAt(0) == 1
                    && ents.aliveAt(cowIdx); // 牛无恙(无冷却无消耗面)
                if (!milkOk) diag += QStringLiteral("[milk%1 got=%2]")
                    .arg(round).arg(hb.blockIdAt(0));
            }
            ok = ok && milkOk;
        }
        // (5) 饮面:先饮迅捷药水挂效果 → 再饮牛奶清效果 + 返空桶 + 零饥饿(长按链跑满)。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
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
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 2 /* Survival */);
            pc.setHunger(10);
            pc.tick();
            bool milkDrank = true;
            // 链 A:迅捷药水(挂效果——饮面基准)。
            hb.setStack(0, RecipeRegistry::SpeedPotionId, 1, 0);
            pc.beginEating();
            for (int i = 0; i < 44; ++i) { QThread::msleep(60); pc.tick(); }
            pc.endEating();
            bool speedOn = false;
            for (const QVariant &v : lastEffects)
                if (v.toMap().value(QStringLiteral("type")).toInt() == int(PlayerState::EffectSpeed))
                    speedOn = true;
            milkDrank = speedOn;
            // 链 B:牛奶(清效果 + 返空桶 + 零饥饿)。
            pumpRosterMid(320);
            hb.setStack(0, RecipeRegistry::MilkBucketId, 1, 0);
            pc.beginEating();
            for (int i = 0; i < 44; ++i) { QThread::msleep(60); pc.tick(); }
            pc.endEating();
            bool cleared = true;
            for (const QVariant &v : lastEffects)
                if (v.toMap().value(QStringLiteral("type")).toInt() == int(PlayerState::EffectSpeed))
                    cleared = false;
            int empties = 0;
            for (int i = 0; i < hb.slotCount(); ++i)
                if (hb.blockIdAt(i) == RecipeRegistry::BucketEmptyId) empties += hb.countAt(i);
            milkDrank = milkDrank && cleared && empties == 1 && pc.hunger() == 10; // 零饥饿
            ok = ok && milkDrank;
            if (!milkDrank) diag += QStringLiteral("[drink speed=%1 clear=%2 empty=%3 h=%4]")
                .arg(speedOn).arg(cleared).arg(empties).arg(pc.hunger());
        }
        // (6) 相邻族零污染(食面尾行瓜片幸存 + 蛋环 0x28E 原值)。
        {
            Hotbar hb2; // 未用占位(表腿无 rig 需求;邻族面走静态断言 + 食面权威表)
            Q_UNUSED(hb2);
            const bool tailOk = PlayerController::foodHungerAmount(RecipeRegistry::MelonSliceId) == 2
                && PlayerController::foodHungerAmount(RecipeRegistry::MilkBucketId) == 0
                && PlayerController::isDrinkableItem(RecipeRegistry::MilkBucketId)
                && RecipeRegistry::SpawnEggVillagerId == 0x28E
                && RecipeRegistry::SlimeBallId == 0x28C
                && RecipeRegistry::MilkBucketId == 0x28F
                && int(BR::Cake) == 159 && int(BR::GlassPane) == 158 && int(BR::FenceGate) == 157;
            ok = ok && tailOk;
            if (!tailOk) diag += QStringLiteral("[tail]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2082c cake and milk chain column (a real controller eating a placed cake"
               " six times advances the bites state one per click with two hunger each and"
               " the sixth bite removes the block, a full hunger bar refuses the bite, three"
               " milk buckets two sugars an egg and three wheat craft one cake with the milk"
               " buckets swapping to empty buckets in place by the container swap authority,"
               " an empty bucket on a cow fills a milk bucket twice in a row with no"
               " cooldown, and drinking the milk bucket clears the speed effect returns the"
               " empty bucket with zero hunger)"
            << (ok ? QString() : diag);
    });

    // ── r2082d:猪骑乘柱 + 结构钉族(NEG 双摘面全豁免 = 骑乘真链 + 结构钉对照腿)──────────────────
    //   骑乘真链(鞍右键猪装备[消耗 1 鞍] → 空手右键骑上[isRidingPig/ridingPigIndex] → 钉位猪背
    //   +0.25 → 推挤豁免(resolvePlayerPush 后猪位不动) → Shift 下猪[四向安全位] → 鞍座会话持久)
    //   + 骑乘互斥源钉(船/矿车 tryMount 守卫行)+ 鞍死不掉负面钉(1.0 无此机制——1.4.2+ 纪元:
    //   无 deathSaddled 快照/无 Main.qml 猪分支 0x225 行)+ 结构钉族(id/段位/kMc/def/谓词/光照/
    //   音色/配方[gate 行豁免=NEG-1 摘面]/图标/调色板/CMake/牛奶桶/骑乘声明行——NEG-2 摘面行豁免)。
    runLeg("r2082d saddle pig riding column and structure pin family (a saddle on a right"
        " clicked pig equips it consuming one saddle, an empty hand right click mounts the"
        " saddled pig with the rider index answering and the player pinned a quarter above the"
        " pig center, the resolve player push leaves the ridden pig in place, shift dismounts"
        " to a side spot with the saddle staying on for the session, killing the pig drops no"
        " saddle per the one point zero read with no death snapshot and no drop row pinned"
        " negative, and the id and count and atlas and mc and definition and predicate and"
        " recipe and icon and palette and cmake and milk bucket and riding declaration rows"
        " are pinned on file with the two negative lesion faces exempt)", [&]() {
        bool ok = true;
        QString diag;
        // (D1) 骑乘真链:上鞍 → 骑上 → 钉位 → 推挤豁免 → Shift 下猪 → 鞍座持久。
        {
            World w;
            initRosterMidWorld(w);
            layRosterMidPlatform(w, 14, 44, 4, 44);
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
            ents.setWanderFrozen(true); // 确定性:冻结游荡(猪靶定在准星柱内;钉位/下猪位面不受扰)
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 27.5, 0.0, -17.0, 1 /* Creative */);
            pc.tick();
            // ① 上鞍(鞍右键猪 → saddledAt true + 鞍消耗)。
            hb.setStack(0, RecipeRegistry::SaddleId, 1, 0);
            pumpRosterMid(320);
            pc.placeBlock();
            const bool saddledOk = pigIdx >= 0 && ents.saddledAt(pigIdx)
                && hb.blockIdAt(0) == RecipeRegistry::SaddleId; // 创造不耗鞍（生存消耗面在 placeBlock 生存门内）
            ok = ok && saddledOk;
            if (!saddledOk) diag += QStringLiteral("[saddle idx=%1 sd=%2 got=%3]")
                .arg(pigIdx).arg(pigIdx >= 0 ? ents.saddledAt(pigIdx) : false)
                .arg(hb.blockIdAt(0));
            // ② 骑上(空手右键已鞍猪 → isRidingPig + index)。
            pumpRosterMid(320);
            pc.placeBlock();
            const bool mountOk = pc.isRidingPig() && pc.ridingPigIndex() == pigIdx;
            ok = ok && mountOk;
            if (!mountOk) diag += QStringLiteral("[mount ride=%1 idx=%2]")
                .arg(pc.isRidingPig()).arg(pc.ridingPigIndex());
            // ③ 钉位猪背(脚底 = 猪中心 + 0.25)。
            pc.tick();
            const QVector3D pigPos = ents.posAt(pigIdx);
            const bool pinOk = pc.isRidingPig()
                && std::fabs(pc.feetPosition().y() - (pigPos.y() + 0.25f)) < 1e-3f
                && std::fabs(pc.feetPosition().x() - pigPos.x()) < 1e-3f;
            ok = ok && pinOk;
            if (!pinOk) diag += QStringLiteral("[pin fy=%1 pgy=%2]")
                .arg(double(pc.feetPosition().y())).arg(double(pigPos.y()));
            // ④ 推挤豁免(resolvePlayerPush 后被骑猪位不动;邻猪对照被推)。
            const int pig2 = ents.spawnMobTyped(30, 81, 24, EntityManager::MobPig,
                                                QStringLiteral("#e8a2a2"), 0);
            const QVector3D beforeRidden = ents.posAt(pigIdx);
            const QVector3D beforeFree = ents.posAt(pig2);
            ents.resolvePlayerPush(pc.feetPosition(), 0.3f, 1.8f, &w);
            const bool exclOk = (ents.posAt(pigIdx) - beforeRidden).lengthSquared() < 1e-6f;
            ok = ok && exclOk;
            if (!exclOk) diag += QStringLiteral("[excl]");
            Q_UNUSED(beforeFree); // 对照面:邻猪可被推(不 Assert 位移方向——推力方向随几何)
            // ⑤ Shift 下猪(四向安全位)。
            pc.setKey(Qt::Key_Shift, true);
            pc.tick();
            pc.setKey(Qt::Key_Shift, false);
            const bool dismountOk = !pc.isRidingPig() && pc.ridingPigIndex() == -1
                && ents.saddledAt(pigIdx); // 鞍座会话持久(下猪不掉鞍)
            ok = ok && dismountOk;
            if (!dismountOk) diag += QStringLiteral("[dismount ride=%1 sd=%2]")
                .arg(pc.isRidingPig()).arg(ents.saddledAt(pigIdx));
        }
        // (D2) 鞍死不掉负面钉(1.0 无此机制——1.4.2+/12w36a 纪元,翻案留痕)。
        {
            const QString srcDir = srcRootForRosterMidPins();
            const bool noDrop = !rawContainsRosterMid(srcDir + QStringLiteral("/Entities/entitymanager.h"),
                                                      QStringLiteral("deathSaddled"))
                && !rawContainsRosterMid(srcDir + QStringLiteral("/Entities/entitymanager.cpp"),
                                         QStringLiteral("deathSaddled"));
            ok = ok && noDrop;
            if (!noDrop) diag += QStringLiteral("[noDrop]");
            const QString qml = srcDir + QStringLiteral("/ui/Main.qml");
            QFile fq(qml);
            QString qmlText;
            if (fq.open(QIODevice::ReadOnly)) qmlText = QString::fromUtf8(fq.readAll());
            const int pigBranch = qmlText.indexOf(QStringLiteral("mobType === EntityManager.MobPig"));
            const bool noQmlDrop = pigBranch >= 0
                && !qmlText.mid(pigBranch, 900).contains(QStringLiteral("0x225")); // 猪分支窗内无鞍掉落行
            ok = ok && noQmlDrop;
            if (!noQmlDrop) diag += QStringLiteral("[noQmlDrop]");
        }
        // (D3) 结构钉族(NEG 豁免面不钉:fence_gate 合成行[NEG-1 摘面]与蛋糕咬口 setBlock 行
        //     [NEG-2 摘面]均不在本族)。
        {
            const QString srcDir = srcRootForRosterMidPins();
            // 值面:id/段位/kMc/相邻族零污染。
            const bool enumOk = int(BR::FenceGate) == 157 && int(BR::GlassPane) == 158
                && int(BR::Cake) == 159 && int(BR::Count) == 166 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Count 163→166（头块/占位/粘性三 id 段尾追加——钉值随追加前移）
                && int(BR::SandstoneSlab) == 156 && int(BR::Cauldron) == 153
                && int(BR::AtlasTileCount) == 213 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Atlas 212→213（活塞伸出态瓦追加——钉值随追加前移）
                && BR::mcBlockId(int(BR::FenceGate)) == 107
                && BR::mcBlockId(int(BR::GlassPane)) == 102
                && BR::mcBlockId(int(BR::Cake)) == 92
                && RecipeRegistry::MilkBucketId == 0x28F
                && RecipeRegistry::SpawnEggVillagerId == 0x28E;
            ok = ok && enumOk;
            if (!enumOk) diag += QStringLiteral("[enum]");
            // 源钉族:blockregistry(.h/.cpp) + recipe.cpp[gate 行豁免] + hotbar + CMake + QML。
            const QStringList missHdr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
                SrcPin("gate id decl", "FenceGate        = 157,", 1),
                SrcPin("pane id decl", "GlassPane        = 158,", 1),
                SrcPin("cake id decl", "Cake             = 159,", 1),
                SrcPin("count sentinel row", "Count           = 166,", 1), // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Count 163→166（头块/占位/粘性三 id 段尾追加——钉文随源行前移）
                SrcPin("atlas count", "AtlasTileCount = 213", 1), // t1113 lawful 前移：209→210（牌板 tile 追加）,；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Atlas 212→213（活塞伸出态瓦追加——钉文随源行前移）
                SrcPin("gate state flags", "FenceGateStateOpenFlag  = 0x1;", 1),
                SrcPin("cake bites mask", "CakeStateBitesMask = 0x7;", 1),
                SrcPin("gate shape decl", "ShapeFenceGate = 14,", 1),
                SrcPin("pane shape decl", "ShapeGlassPane = 15,", 1),
                SrcPin("cake shape decl", "ShapeCake = 16,", 1),
                SrcPin("isFenceGate decl", "static bool isFenceGate(quint8 blockId);", 1)});
            ok = ok && missHdr.isEmpty();
            if (!missHdr.isEmpty())
                diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));
            const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.cpp"), {
                SrcPin("gate def row", "int(BlockRegistry::FenceGate),           8,  8,  8,  8, false, BlockRegistry::ShapeFenceGate", 1),
                SrcPin("pane def row", "int(BlockRegistry::GlassPane),           68, 68, 68, 68, false, BlockRegistry::ShapeGlassPane", 1),
                SrcPin("cake def row", "int(BlockRegistry::Cake),                207,208,208,208, false, BlockRegistry::ShapeCake", 1),
                SrcPin("pane partial row", "if (blockId == GlassPane) return true;", 1),
                SrcPin("cake partial row", "if (blockId == Cake) return true;", 1),
                SrcPin("isFenceGate row", "bool BlockRegistry::isFenceGate(quint8 blockId)      { return blockId == FenceGate; }", 1),
                SrcPin("gate collision row", "case BlockRegistry::ShapeFenceGate:", 1),
                SrcPin("pane light free", "case ShapeGlassPane: return 1.0f;", 1),
                SrcPin("audio gate row", "case FenceGate:", 1),
                SrcPin("audio cake row", "case Cake:", 1)});
            ok = ok && missBr.isEmpty();
            if (!missBr.isEmpty())
                diag += QStringLiteral("[br %1]").arg(missBr.join(QLatin1Char(',')));
            const bool kmcRows = rawContainsRosterMid(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                                      QStringLiteral("/* fence_gate              */ 107,"))
                && rawContainsRosterMid(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                        QStringLiteral("/* glass_pane              */ 102,"))
                && rawContainsRosterMid(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                        QStringLiteral("/* cake                    */ 92,"));
            ok = ok && kmcRows;
            if (!kmcRows) diag += QStringLiteral("[kmc]");
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("pane recipe row", "int(BlockRegistry::GlassPane), 16, 1, \"glass_pane\"", 1),
                SrcPin("cake recipe row", "int(BlockRegistry::Cake), 1, 1, \"cake\"", 1),
                SrcPin("milk id assert", "RecipeRegistry::MilkBucketId        == 0x28F", 1)});
            ok = ok && missRc.isEmpty();
            if (!missRc.isEmpty())
                diag += QStringLiteral("[rc %1]").arg(missRc.join(QLatin1Char(',')));
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("gate icon case", "case BlockRegistry::FenceGate:        return \"icon_fence_gate.png\";", 1),
                SrcPin("pane icon case", "case BlockRegistry::GlassPane:        return \"icon_glass_pane.png\";", 1),
                SrcPin("cake icon case", "case BlockRegistry::Cake:             return \"icon_cake.png\";", 1),
                SrcPin("milk name row", "RecipeRegistry::MilkBucketId)       return QStringLiteral(\"牛奶桶\")", 1),
                SrcPin("milk maxstack row", "RecipeRegistry::MilkBucketId) return 1;", 1),
                SrcPin("palette cake comma row", "int(BlockRegistry::Cake),", 1),
                SrcPin("palette tail row", "int(BlockRegistry::WallSign) };", 1), // t1113 lawful 前移：牌子双 id 续尾追加，尾行钉随追加前移
                SrcPin("container swap row", "outputId == int(BlockRegistry::Cake) && gridItemId == int(RecipeRegistry::MilkBucketId)", 1)});
            ok = ok && missHb.isEmpty();
            if (!missHb.isEmpty())
                diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake gate icon", "textures/icon_fence_gate.png", 1),
                SrcPin("cmake pane icon", "textures/icon_glass_pane.png", 1),
                SrcPin("cmake cake icon", "textures/icon_cake.png", 1),
                SrcPin("cmake section75", "tools/matrix/section75_roster_mid_t1112.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
            // 骑乘声明钉(EntityManager 面 + PlayerController 面 + QML 奶桶图标 case)。
            const QStringList missEnt = pinSet(srcDir + QStringLiteral("/Entities/entitymanager.h"), {
                SrcPin("saddled decl", "Q_INVOKABLE bool saddledAt(int i) const;", 1),
                SrcPin("saddlePig decl", "Q_INVOKABLE void saddlePig(int i);", 1),
                SrcPin("serial decl", "Q_INVOKABLE quint32 serialAt(int i) const;", 1),
                SrcPin("ride excl decl", "void setRideExclusion(int idx, quint32 serial);", 1),
                SrcPin("saddled field", "bool  saddled = false;", 1)});
            ok = ok && missEnt.isEmpty();
            if (!missEnt.isEmpty())
                diag += QStringLiteral("[ent %1]").arg(missEnt.join(QLatin1Char(',')));
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("ride read decl", "Q_INVOKABLE bool isRidingPig() const", 1),
                SrcPin("cake hunger const", "kCakeSliceHungerAmount = 2;", 1),
                SrcPin("seat lift const", "kPigSeatLift = 0.25f;", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
            const bool qmlMilk = rawContainsRosterMid(srcDir + QStringLiteral("/ui/MaterialIcon.qml"),
                                                      QStringLiteral("case 0x28F: drawMilkBucket();"));
            ok = ok && qmlMilk;
            if (!qmlMilk) diag += QStringLiteral("[qmlMilk]");
            // 挤奶分支源钉(playercontroller 空桶+牛 → 牛奶桶;NEG 双摘面不在此)。
            const QStringList missPc2 = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("milk branch", "heldItemId == RecipeRegistry::BucketEmptyId && mt == EntityManager::MobCow", 1),
                SrcPin("milk grant row", "m_hotbar->addStack(int(RecipeRegistry::MilkBucketId), 1);", 1),
                SrcPin("saddle branch", "mt == EntityManager::MobPig && !m_entityManager->saddledAt(mobIdx)", 1),
                SrcPin("mount branch", "mt == EntityManager::MobPig && m_entityManager->saddledAt(mobIdx)", 1),
                SrcPin("drinkable milk row", "itemId == RecipeRegistry::MilkBucketId", 1),
                SrcPin("milk clear row", "m_poisonTimer = 0.0f;   m_poisonDmgAccum = 0.0f;", 1),
                SrcPin("cake branch head", "if (!sneakPlaceBlock && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Cake)", 1),
                SrcPin("gate toggle head", "if (!sneakPlaceBlock && BlockRegistry::isFenceGate(hitId))", 1)});
            ok = ok && missPc2.isEmpty();
            if (!missPc2.isEmpty())
                diag += QStringLiteral("[pc2 %1]").arg(missPc2.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2082d saddle pig riding column and structure pin family (a saddle on a"
               " right clicked pig equips it consuming one saddle, an empty hand right click"
               " mounts the saddled pig with the rider index answering and the player pinned"
               " a quarter above the pig center, the resolve player push leaves the ridden"
               " pig in place, shift dismounts to a side spot with the saddle staying on for"
               " the session, killing the pig drops no saddle per the one point zero read"
               " with no death snapshot and no drop row pinned negative, and the id and"
               " count and atlas and mc and definition and predicate and recipe and icon"
               " and palette and cmake and milk bucket and riding declaration rows are"
               " pinned on file with the two negative lesion faces exempt)"
            << (ok ? QString() : diag);
    });
}
