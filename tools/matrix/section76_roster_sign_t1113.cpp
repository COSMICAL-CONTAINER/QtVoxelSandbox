#include "matrix_helpers.h"

// t1113 名册大件批首单探针段(4 腿;filter 词 r2083;矩阵 868→872)。置尾先例沿用(接 section75,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工三面逐一全仓 grep 实读)──
//   ① 方块面:全仓 grep SignId/StandingSign/WallSign 零命中 → 真缺。交付 = StandingSign=160 /
//     WallSign=161 段尾追加(kMc 63/68 双行——MC 1.0 站牌/挂墙牌本就是两个方块 id,两 id 都取;
//     Alpha 入版 = 1.0 基线内)。**放置时编辑一次**(placeBlock signPlaced 信号 → 呈现层编辑面板);
//     **state 编码裁定**:MC 1.0 站牌是 16 向旋转(4 bit metadata 0..15)实读留痕,本工程登记简化
//     取 4 向(painting/door/chest 全族惯例——16 向斜置板面需非轴对齐 quad 渲染面,越 mesher 轴
//     对齐盒几何面;斜向观感差登记待实机,非静默偏离)。**右键再编辑是 1.8+ 面实读留痕不取**
//     (useBlock 无牌子分支,负面钉锁)。**wall sign 挂墙变体取**(派工留裁定权——实读两 id 都在
//     1.0,放置 ny=0 侧面点击自动选挂墙形态,玩家侧单物品语义保持)。无碰撞(ShapeNone 玩家可
//     穿过)+ 板面选中/射线盒(signBoardBoxes 单一解码——非零盒,Painting 同门「ShapeNone + id
//     特例盒」模式)。破坏掉牌子物品(dropId=自身;**文本随破丢失**——1.0 口径掉落物无文本面)。
//     失撑脱落(World::checkSignSupportOnEdit 写入钩子族,checkPaintingSupportOnEdit 同门)。
//   ② 文字编辑 UI 面:放置时录入(Main.qml SignEdit 面板,4 行 TextInput × 15 字符 maximumLength,
//     chatInput 纯 QtQuick TextInput 选型同门)。**取消/确认语义实读裁定**:1.0 无独立取消语义
//     ——关面板即存文本(含空文本),牌子无论是否打字都保持放置;Done / Esc / 尾行 Enter 三路同门
//     closeSignEdit。零 MC 专名(「牌子」通用词;文本内容是用户输入)。
//   ③ 存档文本面:牌子文本 = 方块附挂数据(state 仅 8 bit 放不下)。**存储门实读**:容器族同门在
//     (ChestStore/HopperStore 坐标键控 + worldstore 表落盘)→ 同门接入:SignStore(Game 层) +
//     worldstore sign_texts 表(saveAll 第 9 参 / loadSigns)。**派工勘误留痕(接入过程发现的
//     链缺口)**:t1097 曾给 WorldStore::saveAll 加第 8 参 brewingStands 但 SaveRequest/SaveBridge
//     链未跟——Main.qml 传的第 12 参在旧 11 参签名下被静默丢弃,酿造内容实际从未经统一保存链落
//     盘;本单接牌子文本同门顺带补正(载荷加字段 + 桥加缺省参 + 转发行补齐,旧 caller 逐位不变)。
//   合成面**派工勘误留痕(降级裁定——t1111 金苹果同门)**:MC 1.0 sign 配方 = 6 木板 2 行 × 3 列
//     满阵 → 1,但该网格与 1.0 活板门配方(6 板 2×3 → 2,本表 t134 行在册)**模式全等**——vanilla
//     自身即同格对(靠匹配器注册序裁定,现代版手工摆 6 板答牌子);工程匹配器先答先得(matchExact
//     包围盒归一化 + 首行命中)+ t802 全表自匹配审计**禁遮蔽行** → 两行不可能共存 → {Planks:6}
//     2×3 归属维持早注册活板门行(追加不插行 + 不动他人行),牌子合成面如实降级候选池,获取面 =
//     创造调色板双 id(r2083a 反证面 + r2083d 配方行负面钉承载)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;摘调用点非摘守卫体 t1051 教训应用)──
//   NEG-1 = 摘 Main.qml closeSignEdit 的 signStore.setText(...) 调用行(两语句行删除,编译不涉
//     C++;QML 行无被引用符号)→ 恰红 = {r2083c}(文本链柱 raw 源钉「signStore.setText(signX,
//     signY, signZ,」失配 = 腿级 FAIL;C++ SignStore 行为面不在摘面 = 幸存)。**豁免设计:
//     r2083d 结构钉族不含该行**(摘面行豁免不钉——t1111/t1112 同门;c 腿行外钉面与 d 零重叠)。
//   NEG-2 = 摘 playercontroller.cpp 牌子放置态分支的站牌朝向写入行(placeState = quint8(
//     (horizontalFacing() & 3) ^ 1);单语句行删除,编译仍绿——else 块空)→ 恰红 = {r2083a}
//     (放置真链朝向断言:站牌落格 state 须 = horizontalFacing^1 = 2,摘行后落 placeState=0 恒
//     值 ≠ 2 = 腿级 FAIL;挂墙朝向行/预检/写入行不在摘面 = b 幸存)。**豁免设计:r2083d 不含
//     该语句行任何形态的源钉**(t1110/t1111/t1112 摘面行豁免不钉同门)。
namespace {

// fixed 宿主小世界 incantation(section69..75 同款四 setter)。
inline void initRosterSignWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(83);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section75 同款)。
inline void layRosterSignPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section72..75 同款)。
inline QString srcRootForRosterSignPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚——pinSet 剥注释会失配,section72..75 同款)。
inline bool rawContainsRosterSign(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 真链 pc rig 的 placeBlock 前置泵(r2067c/section75 同门:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpRosterSign(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

void MatrixRun::section76_roster_sign_t1113()
{
    // ── r2083a:站牌柱(NEG-2 敏感面 = 放置朝向写入)──────────────────────────────────────────
    //   合成面**反证**(6 木板 2 行 → 恒答活板门 = {Planks:6} 2×3 网格归属早注册活板门行的勘误在案
    //   ——r2081 楼梯反证面同门;牌子合成面勘误沿革见 recipe.cpp t1113 注[t1128 翻案重写——降级
    //   裁定撤销、配方行在册,本反证钉语义仍真])  + 木门形状孪生零污染
    //   (门纵列各行其答)+ 调色板双 id 在册钉 + def 行逐字段(tiles 209 / solid=false / ShapeNone /
    //   hardness 1.0 / Axe tier0 / drop 自身)+ 零碰撞/列顶 + 站牌板面盒(朝向 state 驱动)+ 谓词路由
    //   + 光照全透 + 木质音色 + kMc 63 + 放置真链(站牌落格 state = horizontalFacing^1 + signPlaced
    //   恰一次)+ 站牌拒放面(底面悬空点 → 拒)。
    //   [t1128 lawful 修订——仅腿文名与段注:外部审查翻案后「6 板 2×3 不可合成牌子」旧论证撤回
    //   (era 真配方 6 板+底棍与活板门不同形,配方行已在册),本腿「6 板 2×3 恒答活板门」反证钉
    //   语义仍真原样幸存(活板门行未被扰),勘误沿革见 recipe.cpp t1113 注重写段。]
    runLeg("r2083a standing sign column (the six plank two by three grid still answers the"
        " trapdoor row which keeps its own answer under the first match law while the sign"
        " recipe row now exists beside it as a different shape and the wood door twin keeps"
        " its own answer and both sign ids sit in the"
        " creative palette, the definition row answers the sign board tile with no collision"
        " and the axe tier drop self fields, the selection and raycast answer the state driven"
        " standing board box, a real placement writes the standing form facing away from the"
        " player exactly once with the sign placed signal, and a bottom face click over open"
        " air refuses)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 合成面反证 + 木门形状孪生零污染 + 调色板在册钉。
        const int PL = int(BR::Planks);
        const int gSign[9] = { PL, PL, PL, PL, PL, PL, 0, 0, 0 };      // 2 行 × 3 列(= 活板门网格)
        const int gDoor[9] = { PL, 0, 0, PL, 0, 0, PL, 0, 0 };         // 3 行 × 2 列(门纵列)
        const RecipeRegistry::Recipe *r = RecipeRegistry::match(gSign, 3);
        const RecipeRegistry::Recipe *rd = RecipeRegistry::match(gDoor, 3);
        const bool craftOk = r && r->outputId == int(BR::WoodTrapdoor) && r->outputCount == 2
            && rd && rd->outputId == int(BR::WoodDoor)
            && RecipeRegistry::recipeCount() > 0;                      // 全表自匹配审计归 t802 在册
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft s=%1 d=%2]")
            .arg(r ? r->outputId : -1).arg(rd ? rd->outputId : -1);
        // 调色板双 id 在册(牌子获取面 = 创造调色板;Hotbar::creativeBlocks 尾两行)。
        Hotbar hbPal;
        const QVariantList pal = hbPal.creativeBlocks();
        const bool palOk = pal.contains(QVariant(int(BR::StandingSign)))
            && pal.contains(QVariant(int(BR::WallSign)));
        ok = ok && palOk;
        if (!palOk) diag += QStringLiteral("[pal]");
        // (2) def 行逐字段(**dropId=自身 掉牌子物品**——1.0 口径文本随破丢失面在 SignStore 侧)。
        const auto &d = BR::def(quint8(BR::StandingSign));
        const bool defOk = d.id == int(BR::StandingSign)
            && d.topTile == 209 && d.sideTile == 209 && d.frontTile == 209 && d.bottomTile == 209
            && d.solid == false && d.shape == BR::ShapeNone
            && std::fabs(d.hardness - 1.0f) < 1e-4f
            && d.toolType == int(BR::Axe) && d.minToolTier == 0 && d.requiresTool == false
            && d.dropId == int(BR::StandingSign) && d.dropCount == 1 && d.maxStack == 64;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (3) 零碰撞 + 列顶 -1 + 板面盒(朝向 state 驱动)+ 谓词 + 光照 + 音色 + MC 映射。
        const auto col = BR::collisionAABBs(quint8(BR::StandingSign), 2);
        const auto selF2 = BR::selectionAABBs(quint8(BR::StandingSign), quint8(2)); // 朝向 2(+Z)
        const auto rayF2 = BR::raycastAABBs(quint8(BR::StandingSign), quint8(2));
        const auto selF0 = BR::selectionAABBs(quint8(BR::StandingSign), quint8(0)); // 朝向 0(+X)
        const bool boxOk = col.empty()                                            // 无碰撞可穿过
            && !BR::isCollidable(quint8(BR::StandingSign), quint8(0))
            && BR::collisionTopY(quint8(BR::StandingSign), quint8(0)) < 0.0f
            && selF2.size() == 1 && rayF2.size() == 1
            && std::fabs(selF2[0].minX - 2.0f / 16.0f) < 1e-4f   // 宽向 X 2..14/16
            && std::fabs(selF2[0].maxX - 14.0f / 16.0f) < 1e-4f
            && std::fabs(selF2[0].minZ - 7.0f / 16.0f) < 1e-4f   // 厚向 Z 居中 7..9/16
            && std::fabs(rayF2[0].minZ - 7.0f / 16.0f) < 1e-4f   // 射线盒同源(选中/射线单源)
            && std::fabs(selF0[0].minZ - 2.0f / 16.0f) < 1e-4f   // 朝向 0 → 厚向 X、宽向 Z
            && std::fabs(selF0[0].minX - 7.0f / 16.0f) < 1e-4f
            && std::fabs(selF0[0].maxY - 1.0f) < 1e-4f           // 板面贴格顶(下沿 4/16)
            && std::fabs(selF0[0].minY - 4.0f / 16.0f) < 1e-4f;
        ok = ok && boxOk;
        if (!boxOk) diag += QStringLiteral("[box]");
        const bool routeOk = BR::isSign(quint8(BR::StandingSign))
            && !BR::isPartialBlock(quint8(BR::StandingSign))     // 段外显式路由(isPartialBlock 谓词不收)
            && BR::lightOpacity(quint8(BR::StandingSign), 0) == 0
            && BR::materialGroup(quint8(BR::StandingSign)) == BR::GroupWood
            && BR::mcBlockId(int(BR::StandingSign)) == 63
            && int(BR::Count) == 162;                            // t1113 lawful 前移:160→162
        ok = ok && routeOk;
        if (!routeOk) diag += QStringLiteral("[route]");
        // (4) 放置真链(站牌落格 state = horizontalFacing^1 + signPlaced 恰一次)。
        int signPlacedCount = 0;
        int px = -1, py = -1, pz = -1;
        {
            World w;
            initRosterSignWorld(w);
            layRosterSignPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            QObject::connect(&pc, &PlayerController::signPlaced, &pc, [&](int x, int y, int z) {
                ++signPlacedCount; px = x; py = y; pz = z;
            });
            pc.setSelectedBlock(int(BR::StandingSign));
            // 眼位 (24.5,82.62,26.5) yaw0 pitch-45 → 穿 (24,81,25) → 底面命中 (24,80,24) 顶面
            //   (t1112 首轮教训:方块/实体射线同用眼位 feet+1.62,准星几何按眼位推)。
            pc.loadSavedState(24.5, 81.0, 26.5, 0.0, -45.0, 1 /* Creative */);
            pc.tick();
            pumpRosterSign(320); // 越过放置 CD
            pc.placeBlock();
            // yaw0 → horizontalFacing()=3(-Z) → 站牌板面朝玩家反向 = 3^1 = 2(+Z)(NEG-2 敏感面:
            //   摘朝向写入行 → placeState 落 0 恒值 ≠ 2 → 本断言红)。
            const bool placeOk = w.blockAt(24, 81, 24) == quint8(BR::StandingSign)
                && w.stateAt(24, 81, 24) == 2
                && signPlacedCount == 1 && px == 24 && py == 81 && pz == 24;
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place id=%1 st=%2 n=%3 @%4,%5,%6]")
                .arg(w.blockAt(24, 81, 24)).arg(w.stateAt(24, 81, 24))
                .arg(signPlacedCount).arg(px).arg(py).arg(pz);
        }
        // (5) 站牌拒放面(底面悬空点 → 下方非完整支撑 → 拒不挥不落)。
        {
            World w;
            initRosterSignWorld(w);
            layRosterSignPlatform(w, 14, 44, 4, 44);
            w.setBlock(30, 84, 31, BR::Stone, 0); // 悬空浮石(底面 y=84;目标格 (30,83,31) 下方 Air)
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            int refused = 0;
            QObject::connect(&pc, &PlayerController::signPlaced, &pc, [&]() { ++refused; });
            pc.setSelectedBlock(int(BR::StandingSign));
            // 眼位 (30.5,82.62,30.5) yaw180 pitch+45 → 上前方向 (0,+.707,+.707) → 命中浮石底面
            //   (ny=-1) → 目标 (30,83,31) → 下方 (30,82,31) Air → 站牌拒。
            pc.loadSavedState(30.5, 81.0, 30.5, 180.0, 45.0, 1 /* Creative */);
            pc.tick();
            pumpRosterSign(320);
            pc.placeBlock();
            const bool refuseOk = w.blockAt(30, 83, 31) == quint8(BR::Air) && refused == 0;
            ok = ok && refuseOk;
            if (!refuseOk) diag += QStringLiteral("[refuse id=%1 n=%2]")
                .arg(w.blockAt(30, 83, 31)).arg(refused);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2083a standing sign column (the six plank two by three grid still answers the"
               " trapdoor row which keeps its own answer under the first match law while the sign"
               " recipe row now exists beside it as a different shape and the wood door twin keeps"
               " its own answer and both sign ids sit"
               " in the creative palette, the definition row answers the sign board tile with"
               " no collision and the axe tier drop self fields, the selection and raycast"
               " answer the state driven standing board box, a real placement writes the"
               " standing form facing away from the player exactly once with the sign placed"
               " signal, and a bottom face click over open air refuses)"
            << (ok ? QString() : diag);
    });

    // ── r2083b:挂墙牌柱(NEG 双摘面不触达 = 对照腿)──────────────────────────────────────────
    //   def 行逐字段 + 挂墙板面盒(贴墙 + 满宽)双朝向 + 真链侧面放置(命中墙面 ny=0 → WallSign 落格
    //   state = 墙面外法线 + signPlaced 恰一次)+ 失撑脱落(拆墙 → 牌格当场清 Air)+ 挂墙拒放面
    //   (命中格非完整立方 → 拒)+ 相邻族零污染(门/板/蛋糕 kMc 与选中盒原值)。
    runLeg("r2083b wall sign column (the wall definition row keeps the same fields as the"
        " standing sign with its own sixty eight mapping, the selection and raycast boxes hug"
        " the attached wall face full width, a real side face placement writes the wall form"
        " facing along the wall outward normal exactly once, breaking the wall pops the sign"
        " cell to air, a click on a non full cube side refuses, and the neighbouring fence"
        " gate pane and cake families keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        // (1) def 行逐字段(挂墙牌全字段与站牌同表行族,仅 id/名异)+ kMc 68。
        const auto &d = BR::def(quint8(BR::WallSign));
        const bool defOk = d.id == int(BR::WallSign)
            && d.topTile == 209 && d.sideTile == 209 && d.frontTile == 209 && d.bottomTile == 209
            && d.solid == false && d.shape == BR::ShapeNone
            && std::fabs(d.hardness - 1.0f) < 1e-4f
            && d.toolType == int(BR::Axe) && d.minToolTier == 0 && d.requiresTool == false
            && d.dropId == int(BR::WallSign) && d.dropCount == 1 && d.maxStack == 64
            && BR::mcBlockId(int(BR::WallSign)) == 68
            && BR::isSign(quint8(BR::WallSign));
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (2) 挂墙板面盒(贴墙 + 满宽)双朝向 + 站牌对照(挂墙满宽 vs 站牌 12/16 内缩)。
        const auto selW0 = BR::selectionAABBs(quint8(BR::WallSign), quint8(0)); // 朝向 0(+X)→ 墙在 -X
        const auto rayW0 = BR::raycastAABBs(quint8(BR::WallSign), quint8(0));
        const auto selW1 = BR::selectionAABBs(quint8(BR::WallSign), quint8(1)); // 朝向 1(-X)→ 墙在 +X
        const auto selS2 = BR::selectionAABBs(quint8(BR::StandingSign), quint8(2)); // 站牌对照
        const bool boxOk = selW0.size() == 1 && rayW0.size() == 1 && selW1.size() == 1
            && std::fabs(selW0[0].minX - 0.0f) < 1e-4f          // 贴墙:x[0,2/16]
            && std::fabs(selW0[0].maxX - 2.0f / 16.0f) < 1e-4f
            && std::fabs(selW0[0].minZ - 0.0f) < 1e-4f          // 宽向满贯格 [0,1]
            && std::fabs(selW0[0].maxZ - 1.0f) < 1e-4f
            && std::fabs(rayW0[0].maxX - 2.0f / 16.0f) < 1e-4f  // 射线盒同源
            && std::fabs(selW1[0].minX - 14.0f / 16.0f) < 1e-4f // 朝向 1 → x[14/16,1]
            && std::fabs(selW1[0].maxX - 1.0f) < 1e-4f
            && std::fabs(selS2[0].minZ - 7.0f / 16.0f) < 1e-4f  // 站牌对照:厚向居中非贴墙
            && std::fabs(selS2[0].minX - 2.0f / 16.0f) < 1e-4f; // 站牌宽向 12/16 内缩(非满贯)
        ok = ok && boxOk;
        if (!boxOk) diag += QStringLiteral("[box]");
        // (3) 真链侧面放置(命中墙面 ny=0 → WallSign 落格 state = 墙面外法线)。
        int signPlacedCount = 0;
        int px = -1, py = -1, pz = -1;
        {
            World w;
            initRosterSignWorld(w);
            layRosterSignPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Stone, 0); // 附着墙柱
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            QObject::connect(&pc, &PlayerController::signPlaced, &pc, [&](int x, int y, int z) {
                ++signPlacedCount; px = x; py = y; pz = z;
            });
            pc.setSelectedBlock(int(BR::StandingSign)); // 手持站牌 id——形态由命中面分流(单物品语义)
            // 眼位 (24.5,82.62,27.5) yaw0 pitch-16 → 命中墙柱 +Z 面 (24,81,24)(ny=0) → 目标
            //   (24,81,25) → 挂墙形态(state = m_hitNz>0 → 2;板面朝 +Z、墙在 -Z 侧)。
            pc.loadSavedState(24.5, 81.0, 27.5, 0.0, -16.0, 1 /* Creative */);
            pc.tick();
            pumpRosterSign(320);
            pc.placeBlock();
            const bool placeOk = w.blockAt(24, 81, 25) == quint8(BR::WallSign)
                && w.stateAt(24, 81, 25) == 2
                && signPlacedCount == 1 && px == 24 && py == 81 && pz == 25;
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place id=%1 st=%2 n=%3 @%4,%5,%6]")
                .arg(w.blockAt(24, 81, 25)).arg(w.stateAt(24, 81, 25))
                .arg(signPlacedCount).arg(px).arg(py).arg(pz);
            // (4) 失撑脱落:拆墙 → 正 4 邻挂墙牌(墙面 == 被拆格)当场清 Air(钩子族行为钉)。
            w.setBlock(24, 81, 24, BR::Air, 0);
            const bool popOk = w.blockAt(24, 81, 25) == quint8(BR::Air);
            ok = ok && popOk;
            if (!popOk) diag += QStringLiteral("[pop id=%1]")
                .arg(w.blockAt(24, 81, 25));
        }
        // (5) 挂墙拒放面(命中格非完整立方 → 拒):木门板侧面(ny=0)命中 → mechLadderSupportBlock
        //     (WoodDoor)=false → 拒不落。
        {
            World w;
            initRosterSignWorld(w);
            layRosterSignPlatform(w, 14, 44, 4, 44);
            w.setBlock(36, 81, 36, BR::WoodDoor, 0); // 合态门(facing 0 → 板贴 +X 边)
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            int refused = 0;
            QObject::connect(&pc, &PlayerController::signPlaced, &pc, [&]() { ++refused; });
            pc.setSelectedBlock(int(BR::StandingSign));
            // 眼位 (39.5,82.62,36.5) yaw180 pitch-16 → -X 方向命中门板 +X 面 (36,81,36)(ny=0)
            //   → 挂墙形态 → 支撑门非完整立方 → 拒。
            pc.loadSavedState(39.5, 81.0, 36.5, 180.0, -16.0, 1 /* Creative */);
            pc.tick();
            pumpRosterSign(320);
            pc.placeBlock();
            const bool refuseOk = w.blockAt(37, 81, 36) == quint8(BR::Air) && refused == 0;
            ok = ok && refuseOk;
            if (!refuseOk) diag += QStringLiteral("[refuse id=%1 n=%2]")
                .arg(w.blockAt(37, 81, 36)).arg(refused);
        }
        // (6) 相邻族零污染(门/板/蛋糕 kMc 原值 + 栅栏门开合盒原值 + 蛋糕矮盒原值)。
        {
            const auto gateSel = BR::selectionAABBs(quint8(BR::FenceGate), quint8(1)); // 开态门板
            const auto cakeCol = BR::collisionAABBs(quint8(BR::Cake), 0);
            const bool neighOk = BR::mcBlockId(int(BR::FenceGate)) == 107
                && BR::mcBlockId(int(BR::GlassPane)) == 102
                && BR::mcBlockId(int(BR::Cake)) == 92
                && int(BR::FenceGate) == 157 && int(BR::GlassPane) == 158 && int(BR::Cake) == 159
                && gateSel.size() == 1
                && std::fabs(gateSel[0].maxX - 3.0f / 16.0f) < 1e-4f
                && cakeCol.size() == 1
                && std::fabs(cakeCol[0].maxY - 0.5f) < 1e-4f
                && BR::mcBlockId(int(BR::WoodDoor)) == 64;
            ok = ok && neighOk;
            if (!neighOk) diag += QStringLiteral("[neigh]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2083b wall sign column (the wall definition row keeps the same fields as the"
               " standing sign with its own sixty eight mapping, the selection and raycast"
               " boxes hug the attached wall face full width, a real side face placement"
               " writes the wall form facing along the wall outward normal exactly once,"
               " breaking the wall pops the sign cell to air, a click on a non full cube side"
               " refuses, and the neighbouring fence gate pane and cake families keep their"
               " original values)"
            << (ok ? QString() : diag);
    });

    // ── r2083c:文本链柱(NEG-1 敏感面 = Main.qml 存文本调用行)────────────────────────────────
    //   SignStore 行为面(写 4 行读回 + 15 字符截断 + revision 自增 + 全空清条目 + clearSign +
    //   双牌独立 + allSigns/loadAll round-trip)+ 放置→录入→读回真链(rig 放牌 → signPlaced 坐标
    //   → setText → lineAt 四行)+ Main.qml 接线 raw 钉(路由行/存文本行/持久化行/实例行——
    //   **NEG-1 摘面行在本腿钉**,d 腿零重叠)。
    runLeg("r2083c text chain column (the sign store answers four written lines back with per"
        " line clamping to fifteen chars, a revision bump, an all empty write clearing the"
        " entry, an independent second sign, and an all signs load round trip into a fresh"
        " store, a real placement hands the coordinates to the store whose four lines read"
        " back, and the qml wiring rows route the placed sign into the edit panel that saves"
        " on close and loads per world)", [&]() {
        bool ok = true;
        QString diag;
        // (1) SignStore 行为面(写读回 + 截断 + revision + 全空清条目 + clearSign + 双牌独立)。
        {
            SignStore ss;
            const bool emptyOk = ss.entryCount() == 0
                && ss.lineAt(1, 2, 3, 0).isEmpty(); // 无条目 → 空串兜底
            ok = ok && emptyOk;
            if (!emptyOk) diag += QStringLiteral("[empty]");
            ss.setText(1, 2, 3,
                       QStringLiteral("第一行文本"),
                       QStringLiteral("line two"),
                       QStringLiteral("third line here"),
                       QStringLiteral("4th"));
            const int rev1 = ss.revision();
            const bool writeOk = ss.entryCount() == 1
                && ss.lineAt(1, 2, 3, 0) == QStringLiteral("第一行文本")
                && ss.lineAt(1, 2, 3, 1) == QStringLiteral("line two")
                && ss.lineAt(1, 2, 3, 2) == QStringLiteral("third line here")
                && ss.lineAt(1, 2, 3, 3) == QStringLiteral("4th")
                && ss.revision() > 0;
            ok = ok && writeOk;
            if (!writeOk) diag += QStringLiteral("[write]");
            // 15 字符截断(20 字入 → 前 15 字存;C++ 侧双保险,QML maximumLength 为一)。
            const QString long20 = QStringLiteral("0123456789abcdefghij");
            ss.setText(1, 2, 3, long20, QString(), QString(), QString());
            const bool clampOk = ss.lineAt(1, 2, 3, 0) == long20.left(15)
                && ss.lineAt(1, 2, 3, 0).size() == 15
                && ss.revision() >= rev1; // 覆写仍自增(rev 单调)
            ok = ok && clampOk;
            if (!clampOk) diag += QStringLiteral("[clamp len=%1]")
                .arg(ss.lineAt(1, 2, 3, 0).size());
            // 全空写 → 清条目(不落孤儿空键;曾打过字再全清 → 条目移除)。
            const int revBefore = ss.revision();
            ss.setText(1, 2, 3, QString(), QString(), QString(), QString());
            const bool clearEmptyOk = ss.entryCount() == 0
                && ss.lineAt(1, 2, 3, 0).isEmpty()
                && ss.revision() > revBefore;
            ok = ok && clearEmptyOk;
            if (!clearEmptyOk) diag += QStringLiteral("[clearEmpty]");
            // 双牌独立(坐标键控;clearSign 单点移除)。
            ss.setText(5, 6, 7, QStringLiteral("A"), QString(), QString(), QString());
            ss.setText(8, 9, 10, QStringLiteral("B"), QString(), QString(), QString());
            ss.clearSign(5, 6, 7);
            const bool indepOk = ss.entryCount() == 1
                && ss.lineAt(8, 9, 10, 0) == QStringLiteral("B")
                && ss.lineAt(5, 6, 7, 0).isEmpty();
            ok = ok && indepOk;
            if (!indepOk) diag += QStringLiteral("[indep]");
            // allSigns 形状 + loadAll round-trip(fresh store 读回同文本)。
            ss.setText(11, 12, 13, QStringLiteral(" Persistence"), QStringLiteral("ok"),
                       QString(), QString());
            const QVariantList all = ss.allSigns();
            SignStore ss2;
            ss2.loadAll(all);
            const bool rtOk = all.size() == 2
                && ss2.lineAt(8, 9, 10, 0) == QStringLiteral("B")
                && ss2.lineAt(11, 12, 13, 0) == QStringLiteral(" Persistence")
                && ss2.lineAt(11, 12, 13, 1) == QStringLiteral("ok")
                && ss2.entryCount() == 2;
            ok = ok && rtOk;
            if (!rtOk) diag += QStringLiteral("[rt n=%1]").arg(all.size());
        }
        // (2) 放置→录入→读回真链(rig 放牌 → signPlaced 坐标 → store 写入该坐标 → 读回四行)。
        {
            World w;
            initRosterSignWorld(w);
            layRosterSignPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            SignStore ss;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            int sx = -1, sy = -1, sz = -1;
            QObject::connect(&pc, &PlayerController::signPlaced, &pc, [&](int x, int y, int z) {
                sx = x; sy = y; sz = z;
                // 呈现层 closeSignEdit 同款:坐标寻址写入四行(1.0 关面板即存)。
                ss.setText(x, y, z, QStringLiteral("Shop"), QStringLiteral("- wheat 3"),
                           QStringLiteral("- iron 5"), QString());
            });
            pc.setSelectedBlock(int(BR::StandingSign));
            pc.loadSavedState(24.5, 81.0, 26.5, 0.0, -45.0, 1 /* Creative */);
            pc.tick();
            pumpRosterSign(320);
            pc.placeBlock();
            const bool flowOk = sx == 24 && sy == 81 && sz == 24
                && ss.lineAt(24, 81, 24, 0) == QStringLiteral("Shop")
                && ss.lineAt(24, 81, 24, 1) == QStringLiteral("- wheat 3")
                && ss.lineAt(24, 81, 24, 2) == QStringLiteral("- iron 5")
                && ss.lineAt(24, 81, 24, 3).isEmpty()
                && ss.entryCount() == 1;
            ok = ok && flowOk;
            if (!flowOk) diag += QStringLiteral("[flow @%1,%2,%3]").arg(sx).arg(sy).arg(sz);
        }
        // (3) Main.qml 接线 raw 钉(**NEG-1 摘面行在本腿钉**;d 腿零重叠——t1111/t1112 摘面行豁免
        //     不钉同门,唯本腿持有摘面钉 = 恰红归因唯一)。
        {
            const QString qml = srcRootForRosterSignPins() + QStringLiteral("/ui/Main.qml");
            const bool wiringOk = rawContainsRosterSign(qml,
                    QStringLiteral("function onSignPlaced(x, y, z) { window.openSignEdit(x, y, z) }"))
                && rawContainsRosterSign(qml,
                    QStringLiteral("signStore.setText(signX, signY, signZ,")) // NEG-1 摘面行
                && rawContainsRosterSign(qml,
                    QStringLiteral("signStore.loadAll(worldStore.loadSigns())"))
                && rawContainsRosterSign(qml, QStringLiteral("SignStore { id: signStore }"))
                && rawContainsRosterSign(qml, QStringLiteral("function closeSignEdit()"));
            ok = ok && wiringOk;
            if (!wiringOk) diag += QStringLiteral("[wiring]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2083c text chain column (the sign store answers four written lines back"
               " with per line clamping to fifteen chars, a revision bump, an all empty write"
               " clearing the entry, an independent second sign, and an all signs load round"
               " trip into a fresh store, a real placement hands the coordinates to the store"
               " whose four lines read back, and the qml wiring rows route the placed sign"
               " into the edit panel that saves on close and loads per world)"
            << (ok ? QString() : diag);
    });

    // ── r2083d:结构钉族(NEG 双摘面全豁免 = 值面 + 源钉对照腿)────────────────────────────────
    //   值面(id/段位/图集/kMc/相邻族零污染)+ 源钉族(blockregistry .h/.cpp + recipe + hotbar +
    //   playercontroller .h/.cpp + world .h/.cpp + worldstore + savebridge/savecoordinator +
    //   CMake——NEG-1 摘面行[Main.qml setText]与 NEG-2 摘面行[站牌朝向写入]均不在本族)+ 负面钉
    //   (1.0 无再编辑面[useBlock 无牌子分支] + 16 向旋转不取[登记简化锁 4 向])。
    runLeg("r2083d structure pin family (the id and count and atlas and mc and definition and"
        " predicate and recipe and icon and palette and controller and support hook and world"
        " store and save bridge and cmake rows are pinned on file with the two negative lesion"
        " faces exempt, no right click re-edit branch exists per the one point zero read, and"
        " the sixteen way rotation stays unregistered per the four way simplification)",
        [&]() {
        bool ok = true;
        QString diag;
        // (D1) 值面:id/段位/图集/kMc/相邻族零污染。
        {
            const bool enumOk = int(BR::StandingSign) == 160 && int(BR::WallSign) == 161
                && int(BR::Count) == 162
                && int(BR::AtlasTileCount) == 210
                && BR::mcBlockId(int(BR::StandingSign)) == 63
                && BR::mcBlockId(int(BR::WallSign)) == 68
                && int(BR::FenceGate) == 157 && int(BR::GlassPane) == 158 && int(BR::Cake) == 159
                && BR::mcBlockId(int(BR::FenceGate)) == 107
                && BR::mcBlockId(int(BR::GlassPane)) == 102
                && BR::mcBlockId(int(BR::Cake)) == 92
                && RecipeRegistry::MilkBucketId == 0x28F;
            ok = ok && enumOk;
            if (!enumOk) diag += QStringLiteral("[enum]");
        }
        // (D2) 源钉族(NEG-1/NEG-2 摘面行均豁免不钉——恰红归因全权在 a/c)。
        {
            const QString srcDir = srcRootForRosterSignPins();
            const QStringList missHdr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
                SrcPin("standing id decl", "StandingSign     = 160,", 1),
                SrcPin("wall id decl", "WallSign         = 161,", 1),
                SrcPin("count sentinel row", "Count           = 162,", 1),
                SrcPin("atlas count", "AtlasTileCount = 210", 1),
                SrcPin("sign facing mask", "SignStateFacingMask  = 0x3;", 1),
                SrcPin("isSign decl", "static bool isSign(quint8 blockId);", 1)});
            ok = ok && missHdr.isEmpty();
            if (!missHdr.isEmpty())
                diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));
            const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.cpp"), {
                SrcPin("standing def row", "int(BlockRegistry::StandingSign),      209,209,209,209, false, BlockRegistry::ShapeNone", 1),
                SrcPin("wall def row", "int(BlockRegistry::WallSign),          209,209,209,209, false, BlockRegistry::ShapeNone", 1),
                SrcPin("selection board row", "return signBoardBoxes(state, blockId == WallSign);", 2),
                SrcPin("audio sign row", "case StandingSign: case WallSign:", 1)});
            ok = ok && missBr.isEmpty();
            if (!missBr.isEmpty())
                diag += QStringLiteral("[br %1]").arg(missBr.join(QLatin1Char(',')));
            const bool kmcRows = rawContainsRosterSign(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                                       QStringLiteral("/* standing_sign           */ 63,"))
                && rawContainsRosterSign(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                         QStringLiteral("/* wall_sign               */ 68,"));
            ok = ok && kmcRows;
            if (!kmcRows) diag += QStringLiteral("[kmc]");
            // 配方面(降级裁定):id 断言行在(放置链 canonical 锚) + **配方行负面钉**(无牌子合成行
            //   ——{Planks:6} 2×3 与活板门行模式全等,t802 禁遮蔽审计下不可共存,降级裁定见配方表注)。
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("standing id assert", "int(BlockRegistry::StandingSign)    == 160", 1),
                SrcPin("wall id assert", "int(BlockRegistry::WallSign)        == 161", 1),
                SrcPin("degradation ruling row", "t1113", 1)});
            ok = ok && missRc.isEmpty();
            if (!missRc.isEmpty())
                diag += QStringLiteral("[rc %1]").arg(missRc.join(QLatin1Char(',')));
            // [t1128 lawful 修订——外部审查翻案] 旧负面钉「牌子配方行不在册」翻转：「牌子行在册」
            //   (era jar 定谳 6 板+底中棍 → 1,与活板门不同形,t1113 降级裁定撤销,工件
            //   build/t1128_jar_crafting_map_sign.txt;沿革注同见 recipe.cpp t1113 注重写段)。
            const bool signRow = rawContainsRosterSign(srcDir + QStringLiteral("/Game/recipe.cpp"),
                                                       QStringLiteral("int(BlockRegistry::StandingSign), 1, 1, \"standing_sign\""));
            ok = ok && signRow;
            if (!signRow) diag += QStringLiteral("[signRow]");
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("sign icon case", "case BlockRegistry::StandingSign:     return \"icon_sign.png\";", 1),
                SrcPin("wall icon case", "case BlockRegistry::WallSign:         return \"icon_sign.png\";", 1),
                SrcPin("palette standing row", "int(BlockRegistry::StandingSign),", 1),
                SrcPin("palette wall row", "int(BlockRegistry::WallSign) };", 1)});
            ok = ok && missHb.isEmpty();
            if (!missHb.isEmpty())
                diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
            const QStringList missPcH = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("signPlaced decl", "void signPlaced(int x, int y, int z);", 1)});
            ok = ok && missPcH.isEmpty();
            if (!missPcH.isEmpty())
                diag += QStringLiteral("[pcH %1]").arg(missPcH.join(QLatin1Char(',')));
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("place state branch head", "    } else if (BlockRegistry::isSign(quint8(m_selectedBlock))) {", 1),
                // [t1123 lawful 修订] 计数 2→1（沿革：原句式为牌子 + 漏斗双分支同句）。用户实测翻案
                //   「漏斗贴方块左右放置排料口方向反」——MC 真值排料口指向被点方块（t1080 原裁定
                //   沿命中面外法线为外向误读），playercontroller.cpp 漏斗分支写入行改取反法线映射
                //   （本行不再匹配该句式，归 t1123/r2093a 专权钉）；牌子挂墙朝向行原样幸存 → 本钉
                //   仅剩牌子分支一处。
                SrcPin("wall facing row", "placeState = quint8(m_hitNx > 0 ? 0 : m_hitNx < 0 ? 1 : m_hitNz > 0 ? 2 : 3);", 1),
                SrcPin("pre check head", "if (BlockRegistry::isSign(quint8(m_selectedBlock))) {", 1),
                SrcPin("write branch head", "} else if (BlockRegistry::isSign(idByte)) {", 1),
                SrcPin("wall form pick row", "quint8(BlockRegistry::WallSign)", 1),
                SrcPin("emit row", "emit signPlaced(tx, ty, tz);", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
            const QStringList missWh = pinSet(srcDir + QStringLiteral("/World/world.h"), {
                SrcPin("support hook decl", "void checkSignSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id);", 1)});
            ok = ok && missWh.isEmpty();
            if (!missWh.isEmpty())
                diag += QStringLiteral("[wh %1]").arg(missWh.join(QLatin1Char(',')));
            const QStringList missWc = pinSet(srcDir + QStringLiteral("/World/world.cpp"), {
                SrcPin("support hook impl", "void World::checkSignSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id)", 1),
                SrcPin("hook call row", "checkSignSupportOnEdit(x, y, z, oldId, id);", 2),
                SrcPin("hook call silent row", "checkSignSupportOnEdit(x, y, z, lightOldId, id);", 1)});
            ok = ok && missWc.isEmpty();
            if (!missWc.isEmpty())
                diag += QStringLiteral("[wc %1]").arg(missWc.join(QLatin1Char(',')));
            const QStringList missWs = pinSet(srcDir + QStringLiteral("/World/worldstore.h"), {
                SrcPin("saveAll sign param", "const QVariantList &signs = {});", 1),
                SrcPin("loadSigns decl", "Q_INVOKABLE QVariantList loadSigns() const;", 1),
                SrcPin("writeSigns decl", "bool writeSigns(const QVariantList &signs);", 1)});
            ok = ok && missWs.isEmpty();
            if (!missWs.isEmpty())
                diag += QStringLiteral("[ws %1]").arg(missWs.join(QLatin1Char(',')));
            const bool wsCpp = rawContainsRosterSign(srcDir + QStringLiteral("/World/worldstore.cpp"),
                                                     QStringLiteral("bool WorldStore::writeSigns(const QVariantList &signs)"))
                && rawContainsRosterSign(srcDir + QStringLiteral("/World/worldstore.cpp"),
                                         QStringLiteral("FROM sign_texts"))
                && rawContainsRosterSign(srcDir + QStringLiteral("/World/worldstore.cpp"),
                                         QStringLiteral("signStore.loadAll"));
            ok = ok && wsCpp;
            if (!wsCpp) diag += QStringLiteral("[wsCpp]");
            const bool bridgeRows = rawContainsRosterSign(srcDir + QStringLiteral("/Game/savebridge.cpp"),
                                                          QStringLiteral("req.signs = signs;"))
                && rawContainsRosterSign(srcDir + QStringLiteral("/Game/savebridge.cpp"),
                                         QStringLiteral("req.brewingStands = brewingStands;"))
                // t1129 lawful 修订：转发尾词元随单事务原子化改道（saveAll 直调 → writeWorldPart
                // 事务内转发），尾词元 `req.brewingStands, req.signs))` 同位换形携沿革注。
                && rawContainsRosterSign(srcDir + QStringLiteral("/World/savecoordinator.cpp"),
                                         QStringLiteral("req.brewingStands, req.signs))"));
            ok = ok && bridgeRows;
            if (!bridgeRows) diag += QStringLiteral("[bridge]");
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake signstore src", "src/Game/signstore.cpp", 2), // 主目标 + 矩阵目标两列
                SrcPin("cmake section76", "tools/matrix/section76_roster_sign_t1113.cpp", 1),
                SrcPin("cmake sign icon", "textures/icon_sign.png", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (D3) 负面钉(1.0 实读裁定锁面):useBlock 无牌子再编辑分支(右键再编辑 1.8+ 面不取)+
        //     16 向旋转不取(登记简化锁 4 向——无 16 向掩码符号)。
        {
            const QString srcDir = srcRootForRosterSignPins();
            const bool noReedit = !rawContainsRosterSign(srcDir + QStringLiteral("/Game/playercontroller.cpp"),
                                                         QStringLiteral("isSign(hitId)"));
            ok = ok && noReedit;
            if (!noReedit) diag += QStringLiteral("[noReedit]");
            const bool noRot16 = !rawContainsRosterSign(srcDir + QStringLiteral("/Core/blockregistry.h"),
                                                        QStringLiteral("SignStateRot"));
            ok = ok && noRot16;
            if (!noRot16) diag += QStringLiteral("[noRot16]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2083d structure pin family (the id and count and atlas and mc and definition"
               " and predicate and recipe and icon and palette and controller and support hook"
               " and world store and save bridge and cmake rows are pinned on file with the"
               " two negative lesion faces exempt, no right click re-edit branch exists per"
               " the one point zero read, and the sixteen way rotation stays unregistered per"
               " the four way simplification)"
            << (ok ? QString() : diag);
    });
}
