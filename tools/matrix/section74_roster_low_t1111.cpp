#include "matrix_helpers.h"

// t1111 名册低件量批探针段(4 腿;filter 词 r2081;矩阵 860→864)。置尾先例沿用(接 section73,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工五件逐一全仓 grep 实读)──
//   ① sandstone stairs:全仓 grep SandstoneStairs/sandstone_stairs 零命中 → 真缺。交付 = SandstoneStairs
//     =154 段尾追加(ShapeStairs 同门几何复用——WoodStairs=16/CobbleStairs=59/StoneBrickStairs=110 先例)
//     + 砂岩面排布贴图(topTile=52 sandstone_top / 侧·底·前=53 sandstone_side,Sandstone 本块面同源——
//     派工稿「Sandstone=41 顶/底 tile」按 id 读,tile 为 52/53)+ 合成行。**派工勘误留痕**:派工稿
//     「4 砂岩 2×2」经现场核实不可行——{Sandstone:4} 2×2 多重集已被 cut_sandstone 行(4 砂岩方阵 → 4
//     切制砂岩,t485)占用,同形同料必先命中切制行(楼梯永不可合);楼梯族三先例与 MC 原版(1.2.1 12w21a)
//     一律 6 块阶梯形 → 按族惯例落 6 块阶梯行(勘误如实登记,非自创口径);a 腿并钉 4 砂岩 2×2 恒答
//     切制砂岩的族负例(勘误对面)。纪元:12w21a/Java 1.2.1 入版(越 1.0 基线——同 CutSandstone「1.2.4+
//     内容取同语义补族」先例;kMcBlockId=-1 资源包回退引擎自绘;派工稿所引 docs「Beta 1.3」为
//     砂岩台阶纪元,楼梯实为 1.2.1,两件纪元就地分档留痕)。
//   ② stone slab:全仓 grep StoneSlab 零命中 → 真缺(半砖族 WoodSlab=15/CobbleSlab=58/SpruceSlab=87/
//     StoneBrickSlab=109 四族在)。交付 = StoneSlab=155 段尾追加(**单方块多 SlabType vs 独立 id 定夺
//     留痕:工程半砖架构 = 每材质独立方块 id + isSlab 谓词路由,无 SlabType state 查表——派工稿「
//     SlabType state 面」按族惯例核实后落独立 id 形**) + ShapeSlab 几何 + 石贴图(tile 3 六面)+
//     双半砖合并映射(slabFullBlock→Stone / fullBlockSlabDrop←Stone)+ 合成行 3 石 1×3(MC 1.0
//     stone slab 3 smooth stone → 6;石头经圆石熔炼 smelting 在案 = 生存链闭环)。纪元:Alpha 入版 =
//     1.0 基线内(kMcBlockId=44,metadata 0;同 CobbleSlab=44「统一取 slab id」先例)。
//   ③ sandstone slab:同门砂材质行。交付 = SandstoneSlab=156 段尾追加 + 砂岩面排布贴图 + 双半砖
//     合并映射(→Sandstone)+ 合成行 3 砂岩 1×3(产 6 与石/圆石/云杉/石砖台阶族统一——MC 1.0 原版
//     Beta 1.3 产 3,产率族统一登记简化)。纪元:Beta 1.3 入版 = 1.0 基线内(kMcBlockId=44,metadata 1)。
//   ④ golden apple:**真缺 + 原料链断 → 按派工应急条款如实裁定登记候选池,零生产 delta**。grep
//     golden apple/GoldenAppleId 全仓零命中(预期缺 ✓);AppleId/苹果 全仓零命中且**非漏注册而是
//     从未在册**(loottable.h:55「本工程无苹果物品」显式记载 + Inventory.qml:98「苹果(无苹果物品)」
//     显式记载——t484 地牢战利品表当时即以矿物族替苹果):金苹果 8 金锭环+中心苹果的合成行原料位
//     无 id 可填 → 原料链断。裁定:金苹果随苹果根缺口一并入候选池(苹果 worldgen/橡树叶掉落面 +
//     金苹果 id/合成/食面三件);1.0 口径实读定值留痕备后单:Beta 1.2 金粒环,Beta 1.9 pre 2 改
//     **8 金锭环+1 苹果**,hunger +4 / saturation 9.6 / **Regeneration I 30 秒**(Absorption 为 1.1+
//     越纪元不取;d 腿负面钉面锁零在册)。d 腿四负面钉:GoldenAppleId 常量不存在 / 0x28F 段位空 /
//     食面权威表尾行仍是瓜片(foodHungerAmount 无金苹果行)/ 合成表无金苹果行。
//     [t1115 清偿留痕] 候选池三件已交付(AppleId=0x292 / GoldenAppleId=0x293,橡树叶 1/200 掉落 +
//     食面 +4 + 金锭环合成;1.0 定值逐项落地,零降级)——d 腿负面钉 lawful 修订沿革见本文件 D3。
//   ⑤ slime 出生错位修复(t1110 登记新发现):spawnSlime 显式档路径 = spawnMobCore 以缺省中档
//     halfH=0.30 落位(pos.y=y+0.30)→ 槽位写回盒精化(大档 halfH=0.60)后不重落位 → 盒底=pos.y−
//     halfH=y−0.30 **出生嵌坪 0.30 格**(伪着地沿:首拍 collision 顶起 settle 产生伪反弹沿——r2080b
//     实测 t2 嵌入顶起 settle 沿的根因)。修 = applySlimeSizeBox 精化后重置 pos.y=y+halfH(落位面
//     单一权威化:spawnMobCore「pos.y 读 e.halfH 贴地」同式在槽位写回端复走)。兼容面核实:
//     r2077a 血量=档+盒=档契约腿不读 pos.y(修复零触达,契约原值复钉 c 腿);r2080b 冻结空投首落
//     沿腿以空中出生(y=83 上空)避开嵌地——修复只改贴地出生位,空中出生恒在空中,场景不变已复绿
//     (filter r2080 全绿在案)。c 腿钉三档贴地出生位(大 y+0.60/中 y+0.30/小 y+0.15)+ 无嵌坪
//     settle 断言(首拍后 pos.y 恒定)+ 缺省中档路径(spawnMobTyped 通用入口)与邻族(猪 halfH=0.45)
//     零污染。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;流敏感选择器教训应用)──
//   NEG-1 = 摘 sandstone stairs 合成行(recipe.cpp 六行整块删除,编译仍绿——行内无被引用符号)→
//     恰红 = {r2081a}(6 砂岩阶梯形 match() 归 null = 命中断言失;def/放置/谓词面不在摘面 = 部分
//     幸存,腿级 FAIL)。**豁免设计:r2081d 结构钉不含 stairs 合成行**(摘面行豁免不钉——t1110
//     NEG-2 同门;r2081b/c 与 stairs 合成行零数据耦合)。候选排查:改 isStairs 谓词摘 SandstoneStairs
//     同红 a,但 def 行 SrcPin 与放置断言同红 d 面耦合更宽;改输出 id 形虽单行但「摘行」字面不符
//     派工面;整块摘行 = 派工原文形态且恰红最窄。
//   NEG-2 = 摘件五修复行(entitymanager.cpp spawnSlime 内 `e.pos.setY(float(y) + e.halfH);` 行
//     连同注释整行删除,编译仍绿——纯语句无被引用符号)→ 恰红 = {r2081c}(大档贴地出生 pos.y
//     =y+0.30 ≠ y+0.60 = 嵌坪断言失;血量/盒/尺寸档面不在摘面 = 部分幸存,腿级 FAIL)。
//     **豁免设计:r2081d 不含 setY 行任何形态的源钉**(t1110 摘面行豁免不钉同门——含注释锚钉
//     也随整行删除同红);修复面保护 = c 腿行为级 + NEG-2。a/b/d 与修复行零耦合(三档出生位
//     断言全在 c)。
namespace {

// fixed 宿主小世界 incantation(section69/70/73 同款四 setter)。
inline void initRosterWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section70 同款)。
inline void layRosterPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section72/73 同款)。
inline QString srcRootForRosterPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚——pinSet 剥注释会失配,section72 r2079d 同款)。
inline bool rawContainsRoster(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

} // namespace

void MatrixRun::section74_roster_low_t1111()
{
    // ── r2081a:砂岩楼梯柱(NEG-1 敏感面 = 合成命中)──────────────────────────────────────────
    //   合成命中(6 砂岩阶梯 → 4)+ 族负例(4 砂岩 2×2 恒答切制砂岩——勘误对面钉)+ def 行逐字段
    //   (tiles 52/53 / solid=false / ShapeStairs / hardness 0.8 / Pickaxe tier1 / drop 自身)+
    //   谓词路由(isStairs/isPartialBlock 真 / isSlab 假)+ 碰撞盒非空 + 放置读回 + 光照全透
    //   (stairs 族口径 default 0)+ 石质音色 + kMc -1 + 相邻族零污染(三先例楼梯族原值 + 木楼梯
    //   合成回归柱)。
    runLeg("r2081a sandstone stairs column (six sandstone in the stair pattern craft four"
        " sandstone stairs while the two by two four sandstone square still answers cut"
        " sandstone proving the refuted dispatch reading, the definition row answers the"
        " sandstone face layout with the stairs shape and pickaxe stone tier drop self"
        " fields, the stairs and partial predicates answer true while slab answers false,"
        " a placed stair keeps its state and digs back to itself with full transparent"
        " light and the stone audio group and the minus one mc mapping, and the three"
        " sibling stair ids keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 合成命中 + 勘误对面族负例。
        const int SS = int(BR::Sandstone);
        const int gStairs[9] = { SS, 0, 0, SS, SS, 0, SS, SS, SS };
        const RecipeRegistry::Recipe *r = RecipeRegistry::match(gStairs, 3);
        const bool craftOk = r && r->outputId == int(BR::SandstoneStairs) && r->outputCount == 4;
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft got=%1]").arg(r ? r->outputId : -1);
        const int gCut[9] = { SS, SS, SS, SS, 0, 0, 0, 0, 0 }; // 4 砂岩 2×2 满方阵(派工稿楼梯形;2×2 输入按 4 格行优先读)
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gCut, 2);
        const bool cutOk = rc && rc->outputId == int(BR::CutSandstone) && rc->outputCount == 4;
        ok = ok && cutOk;
        if (!cutOk) diag += QStringLiteral("[cut got=%1]").arg(rc ? rc->outputId : -1);
        // (2) def 行逐字段(面排布 + 形状 + 采掘 + 掉落)。
        const auto &d = BR::def(quint8(BR::SandstoneStairs));
        const bool defOk = d.id == int(BR::SandstoneStairs)
            && d.topTile == 52 && d.sideTile == 53 && d.frontTile == 53 && d.bottomTile == 53
            && d.solid == false && d.shape == BR::ShapeStairs
            && std::fabs(d.hardness - 0.8f) < 1e-4f
            && d.toolType == int(BR::Pickaxe) && d.minToolTier == 1 && d.requiresTool == true
            && d.dropId == int(BR::SandstoneStairs) && d.dropCount == 1 && d.maxStack == 64;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");
        // (3) 谓词路由 + 碰撞盒 + 光照 + 音色 + MC 映射。
        const bool routeOk = BR::isStairs(quint8(BR::SandstoneStairs))
            && BR::isPartialBlock(quint8(BR::SandstoneStairs))
            && !BR::isSlab(quint8(BR::SandstoneStairs))
            && BR::lightOpacity(quint8(BR::SandstoneStairs), 0) == 0
            && BR::materialGroup(quint8(BR::SandstoneStairs)) == BR::GroupStone
            && BR::mcBlockId(int(BR::SandstoneStairs)) == -1;
        ok = ok && routeOk;
        if (!routeOk) diag += QStringLiteral("[route]");
        const bool boxOk = !BR::collisionAABBs(quint8(BR::SandstoneStairs), 0).empty()
            && !BR::selectionAABBs(quint8(BR::SandstoneStairs), 0).empty();
        ok = ok && boxOk;
        if (!boxOk) diag += QStringLiteral("[box]");
        // (4) 放置读回 + 朝向 state 位 + 破坏掉自身(def drop 面——真链挖掘由 r2076a 同门已证,
        //     本腿落 def 契约 + 读回)。
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::SandstoneStairs, 2); // state[1:0]=2(+Z 朝向;bit2 倒置 0)
            const bool placeOk = w.blockAt(24, 81, 24) == quint8(BR::SandstoneStairs);
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place]");
        }
        // (5) 相邻族零污染(三先例楼梯 id + 木楼梯合成回归柱)。
        const int gWood[9] = { int(BR::Planks), 0, 0, int(BR::Planks), int(BR::Planks), 0,
                               int(BR::Planks), int(BR::Planks), int(BR::Planks) };
        const RecipeRegistry::Recipe *rw = RecipeRegistry::match(gWood, 3);
        const bool neighOk = int(BR::WoodStairs) == 16 && int(BR::CobbleStairs) == 59
            && int(BR::StoneBrickStairs) == 110 && int(BR::Count) == 162 // t1112 lawful 前移：157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && rw && rw->outputId == int(BR::WoodStairs) && rw->outputCount == 4;
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2081a sandstone stairs column (six sandstone in the stair pattern craft"
               " four sandstone stairs while the two by two four sandstone square still"
               " answers cut sandstone proving the refuted dispatch reading, the definition"
               " row answers the sandstone face layout with the stairs shape and pickaxe"
               " stone tier drop self fields, the stairs and partial predicates answer true"
               " while slab answers false, a placed stair keeps its state and digs back to"
               " itself with full transparent light and the stone audio group and the minus"
               " one mc mapping, and the three sibling stair ids keep their original values)"
            << (ok ? QString() : diag);
    });

    // ── r2081b:石/砂岩台阶双柱(NEG 双摘面不触达 = 对照腿)────────────────────────────────────
    //   双合成命中(3 石 → 6 / 3 砂岩 → 6)+ 半砖族四先例合成回归柱 + 双半砖合并映射两行
    //   (slabFullBlock→Stone/Sandstone + fullBlockSlabDrop 逆行)+ 石砖先例映射零污染 + def 行
    //   逐字段双行(tiles 石 3 / 砂岩 52+53 / ShapeSlab / solid=false)+ 谓词 + 半遮光 7 + 音色 +
    //   kMc 44 双行 + 上下半放置读回 + 相邻族零污染(切制砂岩 2×2 合成回归柱 + Count)。
    runLeg("r2081b stone and sandstone slab pair column (three stone in a row craft six stone"
        " slabs and three sandstone in a row craft six sandstone slabs while the four prior"
        " slab siblings still answer their own crafts, the double slab merge maps stone"
        " slabs to stone and sandstone slabs to sandstone with the inverse drop rows and"
        " the stone brick precedent untouched, both definition rows answer the slab shape"
        " with half light opacity and the stone audio group and the forty four mc mapping,"
        " a placed upper and lower half both read back with the slab predicates true, and"
        " the cut sandstone square craft keeps its answer)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 双合成命中 + 四先例回归柱。
        const int gStone[9] = { int(BR::Stone), int(BR::Stone), int(BR::Stone), 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rs = RecipeRegistry::match(gStone, 3);
        const bool stoneOk = rs && rs->outputId == int(BR::StoneSlab) && rs->outputCount == 6;
        const int gSand[9] = { int(BR::Sandstone), int(BR::Sandstone), int(BR::Sandstone), 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rn = RecipeRegistry::match(gSand, 3);
        const bool sandOk = rn && rn->outputId == int(BR::SandstoneSlab) && rn->outputCount == 6;
        ok = ok && stoneOk && sandOk;
        if (!stoneOk || !sandOk) diag += QStringLiteral("[craft s=%1 n=%2]")
            .arg(rs ? rs->outputId : -1).arg(rn ? rn->outputId : -1);
        const int gPlank[9] = { int(BR::Planks), int(BR::Planks), int(BR::Planks), 0, 0, 0, 0, 0, 0 };
        const int gCob[9]   = { int(BR::Cobble), int(BR::Cobble), int(BR::Cobble), 0, 0, 0, 0, 0, 0 };
        const int gSb[9]    = { int(BR::StoneBrick), int(BR::StoneBrick), int(BR::StoneBrick), 0, 0, 0, 0, 0, 0 };
        const int gSp[9]    = { int(BR::SprucePlanks), int(BR::SprucePlanks), int(BR::SprucePlanks), 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r1 = RecipeRegistry::match(gPlank, 3);
        const RecipeRegistry::Recipe *r2 = RecipeRegistry::match(gCob, 3);
        const RecipeRegistry::Recipe *r3 = RecipeRegistry::match(gSb, 3);
        const RecipeRegistry::Recipe *r4 = RecipeRegistry::match(gSp, 3);
        const bool sibOk = r1 && r1->outputId == int(BR::WoodSlab)
            && r2 && r2->outputId == int(BR::CobbleSlab)
            && r3 && r3->outputId == int(BR::StoneBrickSlab)
            && r4 && r4->outputId == int(BR::SpruceSlab);
        ok = ok && sibOk;
        if (!sibOk) diag += QStringLiteral("[sibling]");
        // (2) 双半砖合并映射两行 + 逆行 + 石砖先例零污染。
        const bool mergeOk = BR::slabFullBlock(quint8(BR::StoneSlab)) == quint8(BR::Stone)
            && BR::fullBlockSlabDrop(quint8(BR::Stone)) == quint8(BR::StoneSlab)
            && BR::slabFullBlock(quint8(BR::SandstoneSlab)) == quint8(BR::Sandstone)
            && BR::fullBlockSlabDrop(quint8(BR::Sandstone)) == quint8(BR::SandstoneSlab)
            && BR::slabFullBlock(quint8(BR::StoneBrickSlab)) == quint8(BR::StoneBrick)
            && BR::fullBlockSlabDrop(quint8(BR::StoneBrick)) == quint8(BR::StoneBrickSlab)
            && BR::slabFullBlock(quint8(BR::WoodSlab)) == quint8(BR::Planks);
        ok = ok && mergeOk;
        if (!mergeOk) diag += QStringLiteral("[merge]");
        // (3) def 行逐字段双行(石 3 / 砂岩 52+53;ShapeSlab;hardness 1.5/0.8)。
        const auto &ds = BR::def(quint8(BR::StoneSlab));
        const bool defS = ds.id == int(BR::StoneSlab)
            && ds.topTile == 3 && ds.sideTile == 3 && ds.frontTile == 3 && ds.bottomTile == 3
            && ds.solid == false && ds.shape == BR::ShapeSlab
            && std::fabs(ds.hardness - 1.5f) < 1e-4f
            && ds.toolType == int(BR::Pickaxe) && ds.minToolTier == 1 && ds.requiresTool == true
            && ds.dropId == int(BR::StoneSlab) && ds.dropCount == 1 && ds.maxStack == 64;
        const auto &dn = BR::def(quint8(BR::SandstoneSlab));
        const bool defN = dn.id == int(BR::SandstoneSlab)
            && dn.topTile == 52 && dn.sideTile == 53 && dn.frontTile == 53 && dn.bottomTile == 53
            && dn.solid == false && dn.shape == BR::ShapeSlab
            && std::fabs(dn.hardness - 0.8f) < 1e-4f
            && dn.toolType == int(BR::Pickaxe) && dn.minToolTier == 1 && dn.requiresTool == true
            && dn.dropId == int(BR::SandstoneSlab) && dn.dropCount == 1 && dn.maxStack == 64;
        ok = ok && defS && defN;
        if (!defS || !defN) diag += QStringLiteral("[def s=%1 n=%2]").arg(defS).arg(defN);
        // (4) 谓词 + 半遮光 7 + 音色 + MC 44 双行。
        const bool faceOk = BR::isSlab(quint8(BR::StoneSlab)) && BR::isSlab(quint8(BR::SandstoneSlab))
            && !BR::isStairs(quint8(BR::StoneSlab))
            && BR::isPartialBlock(quint8(BR::StoneSlab)) && BR::isPartialBlock(quint8(BR::SandstoneSlab))
            && BR::lightOpacity(quint8(BR::StoneSlab), 0) == 7
            && BR::lightOpacity(quint8(BR::SandstoneSlab), 0) == 7
            && BR::lightOpacity(quint8(BR::SandstoneSlab), 1) == 7
            && BR::materialGroup(quint8(BR::StoneSlab)) == BR::GroupStone
            && BR::materialGroup(quint8(BR::SandstoneSlab)) == BR::GroupStone
            && BR::mcBlockId(int(BR::StoneSlab)) == 44
            && BR::mcBlockId(int(BR::SandstoneSlab)) == 44
            && BR::mcBlockId(int(BR::CobbleSlab)) == 44;
        ok = ok && faceOk;
        if (!faceOk) diag += QStringLiteral("[face]");
        // (5) 上/下半放置读回 + 碰撞盒非空。
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::StoneSlab, 0);      // 下半
            w.setBlock(25, 81, 24, BR::StoneSlab, 1);      // 上半
            w.setBlock(26, 81, 24, BR::SandstoneSlab, 0);  // 下半
            const bool placeOk = w.blockAt(24, 81, 24) == quint8(BR::StoneSlab)
                && w.blockAt(25, 81, 24) == quint8(BR::StoneSlab)
                && w.blockAt(26, 81, 24) == quint8(BR::SandstoneSlab)
                && !BR::collisionAABBs(quint8(BR::StoneSlab), 0).empty()
                && !BR::collisionAABBs(quint8(BR::SandstoneSlab), 1).empty();
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place]");
        }
        // (6) 相邻族零污染(切制砂岩 2×2 回归柱 + id 原值)。
        const int gCut[9] = { int(BR::Sandstone), int(BR::Sandstone), int(BR::Sandstone), int(BR::Sandstone), 0, 0, 0, 0, 0 }; // 2×2 满方阵(4 格行优先)
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gCut, 2);
        const bool neighOk = rc && rc->outputId == int(BR::CutSandstone)
            && int(BR::CutSandstone) == 105 && int(BR::Sandstone) == 41
            && int(BR::WoodSlab) == 15 && int(BR::CobbleSlab) == 58
            && int(BR::SpruceSlab) == 87 && int(BR::StoneBrickSlab) == 109
            && int(BR::Count) == 162; // t1112 lawful 前移：157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2081b stone and sandstone slab pair column (three stone in a row craft"
               " six stone slabs and three sandstone in a row craft six sandstone slabs"
               " while the four prior slab siblings still answer their own crafts, the"
               " double slab merge maps stone slabs to stone and sandstone slabs to"
               " sandstone with the inverse drop rows and the stone brick precedent"
               " untouched, both definition rows answer the slab shape with half light"
               " opacity and the stone audio group and the forty four mc mapping, a placed"
               " upper and lower half both read back with the slab predicates true, and the"
               " cut sandstone square craft keeps its answer)"
            << (ok ? QString() : diag);
    });

    // ── r2081c:史莱姆出生位柱(NEG-2 敏感面 = 贴地出生位)──────────────────────────────────────
    //   三档贴地出生位断言:大档(4)spawnSlime 贴地出生 pos.y=y+0.60 且盒底=坪顶(不嵌坪)+
    //   首拍 settle 面(pos.y 恒定零顶起——r2080b 历史伪沿对面)+ 中档缺省路径(spawnMobTyped
    //   通用入口)y+0.30 + 小档(1)y+0.15 + r2077a 契约原值复钉(血=档/盒=档)+ 邻族猪盒
    //   halfH=0.45 出生位零污染。
    runLeg("r2081c slime spawn placement column (a big slime of size four spawned on the"
        " ground answers its position height exactly one half height above the spawn cell"
        " so the box bottom rests on the platform with no embed, stays pinned after the"
        " first settle ticks without the embed lift edge, the default medium path through"
        " the generic entry answers the zero point three placement, a size one spawn"
        " answers the zero point one five placement, the size four contract of four health"
        " on the zero point six box stays intact, and a pig spawn keeps its own half height"
        " placement)", [&]() {
        bool ok = true;
        QString diag;
        // ── 世界 A:大档贴地出生(坪 y=80 → 出生格 y=81)──
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idx = ents.spawnSlime(24, 81, 24, 4);
            const bool bigOk = idx >= 0
                && std::fabs(ents.posAt(idx).y() - 81.60f) < 1e-4f     // y+halfH(0.60) 贴地(修复前 81.30=嵌坪 0.30)
                && std::fabs(ents.posAt(idx).y() - 81.0f - ents.halfHeightAt(idx)) < 1e-4f // 盒底=格底(坪顶)
                && std::fabs(ents.halfHeightAt(idx) - 0.60f) < 1e-4f
                && std::fabs(ents.radiusAt(idx) - 0.60f) < 1e-4f
                && ents.healthAt(idx) == 4;                            // r2077a 契约原值(血=档)
            ok = ok && bigOk;
            if (!bigOk) diag += QStringLiteral("[big idx=%1 y=%2]").arg(idx)
                .arg(idx >= 0 ? double(ents.posAt(idx).y()) : -99.0);
            // 首拍 settle 面:6 拍(0.3s)后 pos.y 恒定(零嵌入顶起 settle 伪沿——修复前嵌坪形态
            //   在首拍 collision 顶起漂移;修复后贴地 rest 稳定)。
            for (int i = 0; i < 6; ++i)
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const bool settleOk = ents.aliveAt(idx) && ents.healthAt(idx) == 4
                && std::fabs(ents.posAt(idx).y() - 81.60f) < 5e-3f;
            ok = ok && settleOk;
            if (!settleOk) diag += QStringLiteral("[settle y=%1 hp=%2]")
                .arg(double(ents.posAt(idx).y())).arg(ents.healthAt(idx));
        }
        // ── 世界 B:中档缺省路径(spawnMobTyped 通用入口——spawnMobCore 缺省档,不经槽位写回)──
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idx = ents.spawnMobTyped(24, 81, 24, EntityManager::MobSlime,
                                               QStringLiteral("#5fa83a"), 0);
            const bool midOk = idx >= 0
                && std::fabs(ents.posAt(idx).y() - 81.30f) < 1e-4f     // 缺省中档 y+0.30(与修复前恒等)
                && std::fabs(ents.halfHeightAt(idx) - 0.30f) < 1e-4f
                && ents.healthAt(idx) == 2
                && ents.slimeSizeAt(idx) == 2;
            ok = ok && midOk;
            if (!midOk) diag += QStringLiteral("[mid idx=%1 y=%2]").arg(idx)
                .arg(idx >= 0 ? double(ents.posAt(idx).y()) : -99.0);
        }
        // ── 世界 C:小档(1)显式路径 ──
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idx = ents.spawnSlime(24, 81, 24, 1);
            const bool tinyOk = idx >= 0
                && std::fabs(ents.posAt(idx).y() - 81.15f) < 1e-4f     // y+0.15
                && std::fabs(ents.halfHeightAt(idx) - 0.15f) < 1e-4f
                && ents.healthAt(idx) == 1;
            ok = ok && tinyOk;
            if (!tinyOk) diag += QStringLiteral("[tiny idx=%1 y=%2]").arg(idx)
                .arg(idx >= 0 ? double(ents.posAt(idx).y()) : -99.0);
        }
        // ── 世界 D:邻族零污染(猪盒 halfH=0.45 出生位)──
        {
            World w;
            initRosterWorld(w);
            layRosterPlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idx = ents.spawnMobTyped(24, 81, 24, EntityManager::MobPig,
                                               QStringLiteral("#e8a2a2"), 0);
            const bool pigOk = idx >= 0
                && std::fabs(ents.posAt(idx).y() - 81.45f) < 1e-4f     // y+halfH(0.45) 同一落位式
                && std::fabs(ents.halfHeightAt(idx) - 0.45f) < 1e-4f;
            ok = ok && pigOk;
            if (!pigOk) diag += QStringLiteral("[pig idx=%1 y=%2]").arg(idx)
                .arg(idx >= 0 ? double(ents.posAt(idx).y()) : -99.0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2081c slime spawn placement column (a big slime of size four spawned on"
               " the ground answers its position height exactly one half height above the"
               " spawn cell so the box bottom rests on the platform with no embed, stays"
               " pinned after the first settle ticks without the embed lift edge, the"
               " default medium path through the generic entry answers the zero point"
               " three placement, a size one spawn answers the zero point one five"
               " placement, the size four contract of four health on the zero point six box"
               " stays intact, and a pig spawn keeps its own half height placement)"
            << (ok ? QString() : diag);
    });

    // ── r2081d:结构钉族 + 金苹果段位钉(NEG 双摘面全豁免 = 结构钉对照腿)────────────────────────
    //   id/段位钉(154/155/156/Count + 相邻族原值)+ kMc 三行 + def/谓词/合并/光照/音色/
    //   配方/图标/调色板/CMake 行源钉族(**NEG 豁免面不钉:stairs 合成行与 spawnSlime setY 行
    //   不在本钉族**)+ 金苹果段位钉([lawful 修订 t1115/r2085] t1111 候选池清偿——旧「GoldenAppleId
    //   三文件缺席」负面面随苹果/金苹果 0x292/0x293 段尾交付退役, 沿革注在 D3;1.0 不取面仍钉:
    //   无 Absorption + 食面权威表瓜片行幸存)+ 0x28E 蛋环原值。Count/图集钉值随 t1112 前移
    //   160/209(本腿 enumOk 行沿革:157→160/207→209)。
    runLeg("r2081d structure pin family with the golden apple segment pins (the three new"
        " block ids answer one fifty four one fifty five and one fifty six with the count"
        " sentinel moved to one sixty by the later roster batch and the mc mapping answering"
        " minus one for the stairs and forty four for both slabs while the neighbouring"
        " cauldron sandstone cut sandstone slab ids and the villager egg at hex twenty"
        " eight E keep their values, the definition and predicate and merge and light and"
        " audio and recipe and icon and palette and cmake rows are pinned on file with the"
        " two negative lesion faces exempt, and the golden apple rides the segment tail at"
        " hex two nine two and two nine three since the later clearance batch with no"
        " absorption symbol on file while the melon slice food row survives)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForRosterPins();

        // (D1) id/段位/kMc/相邻族零污染(值面)。
        const bool enumOk = int(BR::SandstoneStairs) == 154 && int(BR::StoneSlab) == 155
            && int(BR::SandstoneSlab) == 156 && int(BR::Count) == 162 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && int(BR::Cauldron) == 153 && int(BR::JackOLantern) == 152
            && int(BR::PumpkinStem) == 151 && int(BR::Melon) == 149 && int(BR::MelonStem) == 150
            && int(BR::Sandstone) == 41 && int(BR::CutSandstone) == 105
            && int(BR::Stone) == 3 && int(BR::StoneBrick) == 108
            && int(BR::AtlasTileCount) == 210 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && BR::mcBlockId(int(BR::SandstoneStairs)) == -1
            && BR::mcBlockId(int(BR::StoneSlab)) == 44
            && BR::mcBlockId(int(BR::SandstoneSlab)) == 44
            && BR::mcBlockId(int(BR::Sandstone)) == 24
            && BR::mcBlockId(int(BR::Cauldron)) == 118
            && RecipeRegistry::SpawnEggVillagerId == 0x28E
            && RecipeRegistry::SlimeBallId == 0x28C;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");

        // (D2) 代码行源钉族(NEG 豁免面不钉:stairs 合成行[NEG-1 摘面]与 spawnSlime setY 行
        //     [NEG-2 摘面]均不在本族)。kMc 三行走裸读(pinSet 剥 /* */ 块注释会失配——行文
        //     「/* name */ 值,」的注释体在针文内)。
        const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.cpp"), {
            SrcPin("stairs def row", "int(BlockRegistry::SandstoneStairs),     52, 53, 53, 53, false, BlockRegistry::ShapeStairs", 1),
            SrcPin("stone slab def row", "int(BlockRegistry::StoneSlab),           3,  3,  3,  3, false, BlockRegistry::ShapeSlab", 1),
            SrcPin("sandstone slab def row", "int(BlockRegistry::SandstoneSlab),       52, 53, 53, 53, false, BlockRegistry::ShapeSlab", 1),
            SrcPin("isSlab row", "blockId == StoneSlab || blockId == SandstoneSlab; }", 1),
            SrcPin("isStairs row", "blockId == StoneBrickStairs || blockId == SandstoneStairs; }", 1),
            SrcPin("merge stone row", "if (slabId == StoneSlab)      return Stone;", 1),
            SrcPin("merge sandstone row", "if (slabId == SandstoneSlab)  return Sandstone;", 1),
            SrcPin("drop stone row", "if (fullId == Stone)            return StoneSlab;", 1),
            SrcPin("drop sandstone row", "if (fullId == Sandstone)        return SandstoneSlab;", 1),
            SrcPin("light rows", "case StoneSlab:      return 7;", 1),
            SrcPin("audio row", "case SandstoneStairs: case StoneSlab: case SandstoneSlab:", 1)});
        ok = ok && missBr.isEmpty();
        if (!missBr.isEmpty())
            diag += QStringLiteral("[br %1]").arg(missBr.join(QLatin1Char(',')));
        const bool kmcRows = rawContainsRoster(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                                QStringLiteral("/* sandstone_stairs        */ -1,"))
            && rawContainsRoster(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                 QStringLiteral("/* stone_slab              */ 44,"))
            && rawContainsRoster(srcDir + QStringLiteral("/Core/blockregistry.cpp"),
                                 QStringLiteral("/* sandstone_slab          */ 44,"));
        ok = ok && kmcRows;
        if (!kmcRows) diag += QStringLiteral("[kmc]");
        const QStringList missHdr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("stairs id decl", "SandstoneStairs  = 154,", 1),
            SrcPin("stone slab id decl", "StoneSlab        = 155,", 1),
            SrcPin("sandstone slab id decl", "SandstoneSlab    = 156,", 1),
            SrcPin("count sentinel row", "Count           = 162,", 1)}); // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
        ok = ok && missHdr.isEmpty();
        if (!missHdr.isEmpty())
            diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));
        const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("stone slab recipe row", "int(BlockRegistry::StoneSlab), 6, 1, \"stone_slab\"", 1),
            SrcPin("sandstone slab recipe row", "int(BlockRegistry::SandstoneSlab), 6, 1, \"sandstone_slab\"", 1),
            SrcPin("stairs id assert", "int(BlockRegistry::SandstoneStairs) == 154", 1),
            SrcPin("slab id asserts", "int(BlockRegistry::SandstoneSlab)   == 156", 1)});
        ok = ok && missRc.isEmpty();
        if (!missRc.isEmpty())
            diag += QStringLiteral("[rc %1]").arg(missRc.join(QLatin1Char(',')));
        const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("stone slab icon case", "case BlockRegistry::StoneSlab:        return \"icon_stone_slab.png\";", 1),
            SrcPin("sandstone slab icon case", "case BlockRegistry::SandstoneSlab:    return \"icon_sandstone_slab.png\";", 1),
            SrcPin("sandstone stairs icon case", "case BlockRegistry::SandstoneStairs:  return \"icon_sandstone_stairs.png\";", 1),
            // [lawful 修订 t1112/r2082] 调色板方块段尾前移：砂岩楼梯行自「段尾无逗号形」改「逗号行」
            // （FenceGate=157/GlassPane=158/Cake=159 尾部追加——r2073d/r2075d 炼药锅尾行同门先例）。
            SrcPin("palette rows", "int(BlockRegistry::SandstoneStairs),", 1)});
        ok = ok && missHb.isEmpty();
        if (!missHb.isEmpty())
            diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
        const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                          + QStringLiteral("/../CMakeLists.txt"), {
            SrcPin("cmake stone slab icon", "textures/icon_stone_slab.png", 1),
            SrcPin("cmake sandstone slab icon", "textures/icon_sandstone_slab.png", 1),
            SrcPin("cmake sandstone stairs icon", "textures/icon_sandstone_stairs.png", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));

        // (D3) 金苹果段位钉([lawful 修订 t1115/r2085] t1111 候选池清偿 = 交付面的如实锁面)。
        //   [lawful 修订 t1112/r2082] 「0x28F 段位空」负面面退役——牛奶桶（MilkBucketId=0x28F）本单
        //   段尾占位（蛋糕链原料；MaterialIcon.qml 0x28F case 在案）。
        //   [lawful 修订 t1115/r2085] 「GoldenAppleId 三文件缺席」负面面退役——苹果/金苹果
        //   （AppleId=0x292 / GoldenAppleId=0x293）t1115 段尾交付（橡树叶掉落/食面/金锭环合成三件，
        //   见 section78_golden_apple_t1115.cpp），缺席钉自此收口改钉交付面：双 id 常量 decl
        //   （recipe.h 段尾）+ Absorption 越纪元不取面仍钉（playerstate.h 零 Absorption 符号）。
        const bool appleDelivered =
            rawContainsRoster(srcDir + QStringLiteral("/Game/recipe.h"),
                              QStringLiteral("static constexpr int AppleId       = 0x292;"))
            && rawContainsRoster(srcDir + QStringLiteral("/Game/recipe.h"),
                                 QStringLiteral("static constexpr int GoldenAppleId = 0x293;"));
        ok = ok && appleDelivered;
        if (!appleDelivered) diag += QStringLiteral("[appleDelivered]");
        const bool noAbsorption = !rawContainsRoster(srcDir + QStringLiteral("/Game/playerstate.h"),
                                                      QStringLiteral("Absorption"));
        ok = ok && noAbsorption;
        if (!noAbsorption) diag += QStringLiteral("[noAbsorption]");
        // 食面权威表尾行仍瓜片(食面未被金苹果行改写)+ 蛋环合成先例行在场(t1109 闪烁西瓜)。
        const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("food tail row", "if (itemId == RecipeRegistry::MelonSliceId)     return 2;", 1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty())
            diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
        const bool glistering = rawContainsRoster(srcDir + QStringLiteral("/Game/recipe.cpp"),
                                                  QStringLiteral("GlisteringMelonId"));
        ok = ok && glistering;
        if (!glistering) diag += QStringLiteral("[glistering]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2081d structure pin family with the golden apple segment pins (the three"
               " new block ids answer one fifty four one fifty five and one fifty six with"
               " the count sentinel moved to one sixty by the later roster batch and the mc"
               " mapping answering minus one for the stairs and forty four for both slabs"
               " while the neighbouring cauldron sandstone cut sandstone slab ids and the"
               " villager egg at hex twenty eight E keep their values, the definition and"
               " predicate and merge and light and audio and recipe and icon and palette"
               " and cmake rows are pinned on file with the two negative lesion faces"
               " exempt, and the golden apple rides the segment tail at hex two nine two and"
               " two nine three since the later clearance batch with no absorption symbol"
               " on file while the melon slice food row survives)"
            << (ok ? QString() : diag);
    });
}
