#include "matrix_helpers.h"

// t1114 名册大件批第二单探针段(4 腿;filter 词 r2084;矩阵 872→876)。置尾先例沿用(接 section76,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工三面逐一全仓 grep 实读)──
//   ① 物品面:全仓 grep MapStore/EmptyMapId/FilledMapId/empty_map 配方行零命中 → 真缺。交付 =
//     EmptyMapId=0x290 / FilledMapId=0x291 材料段尾追加(0x28F 牛奶桶之上,追加不插中间 = 存档
//     安全铁律)。**派工勘误留痕三面(源码注 + 本腿文 + 提交注)**:①1.0 无「空地图」独立物品
//     (1.0 唯一地图物品 id 358 合成即得持手即绘;空地图 id 395 = 1.6+ 面不取)——两形态拆分 =
//     工程激活链简化(负面钉锁非 1.0 原生面);②合成无罗盘芯(罗盘芯 = 12w34a/1.4.2+ 面,实读
//     留痕不取——8 纸环行零 CompassId)[**t1128 外部审查翻案:本 ② 读法失真撤销**——era jar 定谳
//     sl=CraftingManager 偏移 3187..3248 罗盘芯 1.0.0 在册(工件 build/t1128_jar_crafting_map_sign.txt
//     留痕),配方勘误为罗盘+8 纸环 → r2084a 合成网格行随本翻案 lawful 修订(沿革注见该腿)];③**maxStack 64 可堆叠(派工预期「填充地图不可堆叠」
//     翻案)**——地图不可堆叠是 12w34a/1.4.2+ 面(per-map 数据时代),1.0 地图可堆叠 64,工程走
//     材料段默认 64 零特判(数据集 = 每会话单份全幅,同栈多张读同一视图,Beta 1.6-1.7.3「全图
//     共享一份数据」期口径一致)。合成 = 罗盘+8 纸环(multiset {Paper:8, Compass:1} 唯一;熔炉 8
//     圆石环 / 画作 8 棒环异料逐格比对不冲突)。kMc 面不扩(近期材料段物品同模式,mcMaterialId 越表界 -1 →
//     自绘回退)。
//   ② 探索填充面:数据集 = **每会话单份全幅**(MapStore,Game 层纯像素存储零 World 依赖)。范围
//     裁定:MC 1.0 是 128×128 激活锚点制,工程取全幅(width×depth,1 格 = 1 像素单缩放)=「有限
//     世界全幅渲染」先例的地图面延伸;无锚点即无「存档丢锚重锚」面。探索机制 = 持填充地图者
//     周界 kMapExploreRadius(16) 半径盒扫,0.5s 相位(vanilla 每 ~5 game tick 周界列扫描的工程
//     节拍);激活首绘 kMapInitialRadius(24)「即刻绘制中心区域」;未到列保持未探索底色。色调 =
//     地形顶块族色 + 七群系 tint + 高度明暗(mapColumnColor,§9 原创色板零 MC 资产)。
//   ③ 显示面:手持地图 overlay(Main.qml 纯 QtQuick 面板,牌子编辑面板 / paintingHost 同门;1.0
//     Beta 1.8→1.0 持图显大幅地图视图的工程 QML 面板选型)。纹理 = image://mapstore/<revision>
//     (main.cpp 胶水 MapAtlasProvider,t414 rp provider 同门——QtQuick 依赖留 app 胶水层,Game 层
//     MapStore 只出 QImage 不沾 QtQuick);玩家位点小点按世界坐标比例投影(全幅图无锚点偏移)。
//   ④ 存档面:**会话口径如实登记**(t1013 箱车 / t1112 鞍面 / t1113 牌面先例族)——地图数据是物品
//     附挂数据,工程背包物品栈无 id 外附加面(Hotbar::normalizeDurability 材料段恒归 0 的实读
//     留痕)+ 地图非方块无坐标键控面 → 零存档门,worldstore 表族零加表;进世界 mapStore.clearAll
//     (signStore.loadAll 同门位置,跨世界泄漏收口)+ 持图惰性建库 = 重探索再填充面。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;摘调用点非摘守卫体 t1051 教训应用)──
//   NEG-1 = 摘 playercontroller.cpp tickImpl 的 tickMapExploration(dt); 调用行(单语句行删除,
//     编译仍绿——成员函数声明/定义不在摘面)→ 恰红 = {r2084b}(探索填充柱真链盒扫:摘行后持图
//     tick 恒不推进探索相位 → 探索列恒未探索底色 = 腿级 FAIL;激活首绘走 placeBlock 分支的
//     refreshMapAroundPlayer 直调不在摘面 = a 幸存)。**豁免设计:r2084d 不含该调用行任何形态
//     的源钉**(t1113 摘面行豁免不钉同门;d 腿钉 tickMapExploration 的 decl 行与 impl 头行——
//     均非调用行,NEG-1 摘行后原值幸存)。
//   NEG-2 = 摘 playercontroller.cpp placeBlock 空地图分支的激活转换行(hb setStack FilledMapId
//     单语句行删除,编译仍绿——分支体余行完整)→ 恰红 = {r2084a}(激活真链:摘行后槽 id 恒
//     EmptyMapId ≠ FilledMapId = 腿级 FAIL;建库/首绘行不在摘面,b/c/d 不触达)。**豁免设计:
//     r2084c/r2084d 均不含该语句行任何形态的源钉**(摘面行豁免不钉——t1111/t1112/t1113 同门)。
//     [t1128 沿革] 本 NEG-2 已随 t1114 关单归档(行已还原);t1128 件一修把旧整栈替换行撤换为
//     消耗+入包新面,激活断言面随 r2084a lawful 修订(新面 NEG 见 section91 r2098 头注——消耗行
//     摘面由 t1128 自己的 NEG-2 承接)。
namespace {

// fixed 宿主小世界 incantation(section69..76 同款四 setter)。
inline void initRosterMapWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(84);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section76 同款)。
inline void layRosterMapPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section72..76 同款)。
inline QString srcRootForRosterMapPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚——pinSet 剥注释会失配,section72..76 同款)。
inline bool rawContainsRosterMap(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 真链 pc rig 的 placeBlock 前置泵 / 探索节拍驱动器(r2067c/section76 同门:m_evtClock 放置 CD
//   200ms → 事件泵 320ms 越窗;探索相位 0.5s + 单 tick dt 钳 0.05 → 逐 tick 墙钟泵 70ms 跑 16 拍)。
inline void pumpRosterMap(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
inline void driveRosterMapExplore(PlayerController &pc, int beats)
{
    for (int i = 0; i < beats; ++i) {
        pc.tick();
        pumpRosterMap(70);
    }
}

} // namespace

void MatrixRun::section77_roster_map_t1114()
{
    // ── r2084a:激活链柱([t1128 lawful 修订]——外部审查翻案 era jar 定谳罗盘芯在册 + 件一激活
    //   新面;沿革注见本段头 ② 与 NEG-2 注)──────────────────────────────────────────────────
    //   合成命中(罗盘+8 纸环 → 1 空地图;era 样板 "###/#X#/###" + #=paper + X=compass,期望网格
    //   独立抄自 jar 工件 build/t1128_jar_crafting_map_sign.txt 非旧实现) + 环孪生零污染(熔炉
    //   8 圆石环 / 画作 8 棒环各行其答) + 双 id 段位钉(0x290/0x291 追加序 + 0x28F 牛奶桶原位)
    //   + maxStack 64(翻案面:不可堆叠是 1.4.2+) + 名面双行 + 调色板双 id 在册 + kMc -1(近期
    //   材料段不扩表) + 激活真链新面(创造持 1 张:激活不耗,手持空地图原样 + 产物填充地图入
    //   背包空槽 + 建库全幅 + 首绘中心区 + 世界零写入)。
    runLeg("r2084a activation chain column (the compass centered eight paper ring answers one"
        " empty map per the one point zero jar read while the furnace ring and the painting"
        " ring keep their own answers, the two map ids sit at the material segment tail with"
        " milk bucket at 0x28F, both ids stack to sixty four per the one point zero read,"
        " the name and palette rows carry both forms, the mc mapping stays absent for both,"
        " and a real right click in creative keeps the held empty map unconsumed while one"
        " filled map lands in the inventory, builds the whole world extent dataset and draws"
        " the central area around the player with zero world writes)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 合成命中 + 环孪生零污染(熔炉 / 画作 / 闪烁西瓜环各行其答——异料逐格比对)。
        const int PA = int(RecipeRegistry::PaperId);
        const int CP = int(RecipeRegistry::CompassId);               // [t1128] era 罗盘芯(独立建证自 jar 工件)
        const int CO = int(BR::Cobble);
        const int ST = int(RecipeRegistry::StickId);                 // 木棒是物品段 id（recipe 段）
        const int WO = int(BR::Wool);
        const int gMap[9]   = { PA, PA, PA, PA, CP, PA, PA, PA, PA }; // 罗盘居中 8 纸环(t1128 勘误形)
        const int gFurn[9]  = { CO, CO, CO, CO, 0, CO, CO, CO, CO }; // 熔炉环(同形异料)
        const int gPaint[9] = { ST, ST, ST, ST, WO, ST, ST, ST, ST }; // 画作环
        const auto *rMap   = RecipeRegistry::match(gMap, 3);
        const auto *rFurn  = RecipeRegistry::match(gFurn, 3);
        const auto *rPaint = RecipeRegistry::match(gPaint, 3);
        const bool craftOk = rMap && rMap->outputId == int(RecipeRegistry::EmptyMapId)
            && rMap->outputCount == 1 && rMap->consumeCount == 1     // 每原料格耗 1(CRAFT-01 消耗断言)
            && rFurn && rFurn->outputId == int(BR::Furnace)
            && rPaint && rPaint->outputId == int(RecipeRegistry::PaintingId)
            && RecipeRegistry::recipeCount() > 0;                    // 全表自匹配审计归 t802 在册
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craft m=%1 f=%2 p=%3]")
            .arg(rMap ? rMap->outputId : -1).arg(rFurn ? rFurn->outputId : -1)
            .arg(rPaint ? rPaint->outputId : -1);
        // (2) 双 id 段位钉 + maxStack 64(1.0 翻案面) + 名面 + 调色板 + kMc -1。
        Hotbar hbPal;
        const QVariantList pal = hbPal.creativeMaterials();
        const bool itemOk = int(RecipeRegistry::EmptyMapId) == 0x290
            && int(RecipeRegistry::FilledMapId) == 0x291
            && int(RecipeRegistry::MilkBucketId) == 0x28F             // 段尾追加序(牛奶桶原位)
            && hbPal.maxStackSize(int(RecipeRegistry::EmptyMapId)) == 64
            && hbPal.maxStackSize(int(RecipeRegistry::FilledMapId)) == 64 // 1.0 翻案面(不可堆叠 = 1.4.2+)
            && hbPal.nameForBlock(int(RecipeRegistry::EmptyMapId)) == QStringLiteral("空地图")
            && hbPal.nameForBlock(int(RecipeRegistry::FilledMapId)) == QStringLiteral("填充地图")
            && pal.contains(QVariant(int(RecipeRegistry::EmptyMapId)))
            && pal.contains(QVariant(int(RecipeRegistry::FilledMapId)))
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::EmptyMapId)) == -1
            && RecipeRegistry::mcMaterialId(int(RecipeRegistry::FilledMapId)) == -1;
        ok = ok && itemOk;
        if (!itemOk) diag += QStringLiteral("[item]");
        // (3) 激活真链新面([t1128 件一修]:创造不耗 + 产物入背包;建库 + 首绘 + 零世界写入)。
        {
            World w;
            initRosterMapWorld(w);
            layRosterMapPlatform(w, 4, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            MapStore ms;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.setMapStore(&ms);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            hb.setStack(0, int(RecipeRegistry::EmptyMapId), 1, 0); // 空地图上手
            pc.setSelectedBlock(int(BR::Air));                     // 非方块物品 → selectedBlock 归 Air
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 1 /* Creative */);
            pc.tick();
            pumpRosterMap(320); // 越过放置 CD
            pc.placeBlock();
            const bool actOk = hb.blockIdAt(0) == int(RecipeRegistry::EmptyMapId) // 创造不耗:手持空地图原样
                && hb.countAt(0) == 1
                && hb.blockIdAt(1) == int(RecipeRegistry::FilledMapId)            // 产物入背包空槽
                && hb.countAt(1) == 1
                && ms.hasMap()
                && ms.mapWidth() == 48 && ms.mapDepth() == 48                     // 全幅定版
                && ms.revision() > 0
                && ms.columnColor(24, 24) != MapStore::kUnexploredColor            // 首绘中心区(脚下列已绘)
                && w.blockAt(25, 81, 24) == quint8(BR::Air);                       // 零世界写入(非方块放置)
            ok = ok && actOk;
            if (!actOk) diag += QStringLiteral("[act id0=%1 n0=%2 id1=%3 n1=%4 map=%5 col=%6]")
                .arg(hb.blockIdAt(0)).arg(hb.countAt(0))
                .arg(hb.blockIdAt(1)).arg(hb.countAt(1)).arg(ms.hasMap())
                .arg(ms.columnColor(24, 24) != MapStore::kUnexploredColor ? 1 : 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2084a activation chain column (the compass centered eight paper ring answers one"
               " empty map per the one point zero jar read while the furnace ring and the painting"
               " ring keep their own answers, the two map ids sit at the material segment tail with"
               " milk bucket at 0x28F, both ids stack to sixty four per the one point zero read,"
               " the name and palette rows carry both forms, the mc mapping stays absent for both,"
               " and a real right click in creative keeps the held empty map unconsumed while one"
               " filled map lands in the inventory, builds the whole world extent dataset and draws"
               " the central area around the player with zero world writes)"
            << (ok ? QString() : diag);
    });

    // ── r2084b:探索填充柱(NEG-1 敏感面 = tickMapExploration 调用行)────────────────────────
    //   MapStore 行为面(建库全幅未探索 + 写列读回 + 越界兜底 + 出图尺寸 + revision 单调 + 清库
    //   重建[会话口径重探索面]) + 持图探索真链双 rig 盒扫(rig1 建库 + 盒一绘 / rig2 同库盒二绘 +
    //   两盒外交格恒未探索) + 不持图不扫描负例(空手 pump 后零建库)。
    runLeg("r2084b exploration fill column (the map store initializes the whole extent"
        " unexplored, answers written columns back with out of bounds fallback, exports the"
        " image at the dataset size with a monotonic revision, clears and rebuilds lazily"
        " per the session caliber, a filled map holder drives the periodic box scan across"
        " two player rigs sharing one dataset with the far cell staying unexplored, and an"
        " empty hand never initializes the dataset)", [&]() {
        bool ok = true;
        QString diag;
        // (1) MapStore 行为面(建库 / 写列 / 读回 / 越界 / 出图 / revision / 清库重建)。
        {
            MapStore ms;
            const bool freshOk = !ms.hasMap()
                && ms.columnColor(1, 1) == MapStore::kUnexploredColor; // 未建库兜底
            ok = ok && freshOk;
            if (!freshOk) diag += QStringLiteral("[fresh]");
            ms.initialize(8, 6);
            const bool initOk = ms.hasMap() && ms.mapWidth() == 8 && ms.mapDepth() == 6
                && ms.columnColor(7, 5) == MapStore::kUnexploredColor  // 全图未探索底色
                && ms.revision() > 0;
            ok = ok && initOk;
            if (!initOk) diag += QStringLiteral("[init]");
            const quint32 c1 = 0xFF4e8f3c, c2 = 0xFF3b6cb0;
            ms.writeColumn(2, 3, c1);
            ms.writeColumn(7, 5, c2);
            ms.writeColumn(99, 99, c1);                                 // 越界写 no-op
            ms.commitColumns();
            const bool writeOk = ms.columnColor(2, 3) == c1
                && ms.columnColor(7, 5) == c2
                && ms.columnColor(0, 0) == MapStore::kUnexploredColor   // 未写列保持底色
                && ms.columnColor(-1, 3) == MapStore::kUnexploredColor  // 越界读兜底
                && ms.revision() >= 2;                                  // 单调自增
            ok = ok && writeOk;
            if (!writeOk) diag += QStringLiteral("[write]");
            const QImage img = ms.renderImage();
            const bool imgOk = img.width() == 8 && img.height() == 6
                && img.pixelColor(2, 3).rgba() == c1;                   // 出图像素同源
            ok = ok && imgOk;
            if (!imgOk) diag += QStringLiteral("[img %1x%2]").arg(img.width()).arg(img.height());
            ms.clearAll();
            const bool clearOk = !ms.hasMap()
                && ms.columnColor(2, 3) == MapStore::kUnexploredColor;  // 清库兜底(会话口径)
            ok = ok && clearOk;
            if (!clearOk) diag += QStringLiteral("[clear]");
            ms.initialize(4, 4);
            ms.writeColumn(1, 1, c1);
            ms.commitColumns();
            const bool rebuildOk = ms.hasMap() && ms.mapWidth() == 4
                && ms.columnColor(1, 1) == c1;                          // 惰性重建面
            ok = ok && rebuildOk;
            if (!rebuildOk) diag += QStringLiteral("[rebuild]");
        }
        // (2) 持图探索真链双 rig 盒扫(NEG-1 敏感面:摘调用行 → 两盒恒零绘)。
        quint32 revProbe = 0;
        {
            World w;
            initRosterMapWorld(w);
            layRosterMapPlatform(w, 4, 44, 4, 44);
            w.setBlock(12, 81, 12, BR::Sand, 0);                        // 盒一内异色列(色板面)
            EntityManager ents;
            Hotbar hb;
            MapStore ms;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.setMapStore(&ms);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0);     // 填充地图上手(无激活——惰性建库面)
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(10.5, 81.0, 10.5, 0.0, 0.0, 1 /* Creative */);
            driveRosterMapExplore(pc, 16);                              // 0.8s 相位 ≥ 0.5s → 盒一
            const bool box1Ok = ms.hasMap() && ms.mapWidth() == 48
                && ms.columnColor(10, 10) != MapStore::kUnexploredColor // 盒一内脚下列已绘
                && ms.columnColor(12, 12) != MapStore::kUnexploredColor
                && ms.columnColor(40, 40) == MapStore::kUnexploredColor // 盒二区未至未绘
                && ms.columnColor(20, 40) == MapStore::kUnexploredColor; // 两盒外交格未绘
            ok = ok && box1Ok;
            if (!box1Ok) diag += QStringLiteral("[box1 %1 %2]")
                .arg(ms.columnColor(10, 10) != MapStore::kUnexploredColor ? 1 : 0)
                .arg(ms.columnColor(40, 40) != MapStore::kUnexploredColor ? 1 : 0);
            revProbe = quint32(ms.revision());
            // rig2 同库异位(数据集跨 rig 持久 = 会话单份口径面):盒二绘 + 交格仍未绘。
            hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0);
            pc.loadSavedState(40.5, 81.0, 40.5, 0.0, 0.0, 1 /* Creative */);
            driveRosterMapExplore(pc, 16);                              // 再 0.8s → 盒二
            const bool box2Ok = ms.columnColor(40, 40) != MapStore::kUnexploredColor
                && ms.columnColor(20, 40) == MapStore::kUnexploredColor  // 两盒外交格仍未绘
                && ms.revision() > int(revProbe);                         // 批次 revision 单调
            ok = ok && box2Ok;
            if (!box2Ok) diag += QStringLiteral("[box2 %1 %2]")
                .arg(ms.columnColor(40, 40) != MapStore::kUnexploredColor ? 1 : 0)
                .arg(ms.revision() - int(revProbe));
        }
        // (3) 不持图不扫描负例(空手 pump 后零建库——探索面仅持图者推进)。
        {
            World w;
            initRosterMapWorld(w);
            layRosterMapPlatform(w, 4, 44, 4, 44);
            EntityManager ents;
            Hotbar hb;
            MapStore ms;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.setMapStore(&ms);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            pc.setSelectedBlock(int(BR::Air));                          // 空手
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, 0.0, 1 /* Creative */);
            driveRosterMapExplore(pc, 16);                              // 同拍数零建库
            const bool idleOk = !ms.hasMap() && ms.revision() == 0;
            ok = ok && idleOk;
            if (!idleOk) diag += QStringLiteral("[idle]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2084b exploration fill column (the map store initializes the whole extent"
               " unexplored, answers written columns back with out of bounds fallback, exports"
               " the image at the dataset size with a monotonic revision, clears and rebuilds"
               " lazily per the session caliber, a filled map holder drives the periodic box"
               " scan across two player rigs sharing one dataset with the far cell staying"
               " unexplored, and an empty hand never initializes the dataset)"
            << (ok ? QString() : diag);
    });

    // ── r2084c:显示面柱(NEG 双摘面不触达 = 对照腿)──────────────────────────────────────────
    //   Main.qml 接线 raw 钉(MapStore 实例行 / 进世界清库行 / player 注入行 / overlay 显隐行 /
    //   provider 源行) + main.cpp provider 钉(provider 类行 + 注册行) + 注入面 pc.h 钉(Q_PROPERTY
    //   行 + getter 行——NEG 双摘面在 pc.cpp 调用/语句行,本腿零重叠)。
    runLeg("r2084c display face column (the qml wiring rows instantiate the map store, clear"
        " it on world entry per the session caliber, inject it into the player, gate the"
        " hand held overlay on the filled map id with the provider source and project the"
        " player dot, the app glue registers the mapstore image provider, and the header"
        " carries the map store property row)", [&]() {
        bool ok = true;
        QString diag;
        const QString qml = srcRootForRosterMapPins() + QStringLiteral("/ui/Main.qml");
        const QString mainCpp = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("main.cpp"));
        const QString pcH = srcRootForRosterMapPins() + QStringLiteral("/Game/playercontroller.h");
        const bool qmlOk = rawContainsRosterMap(qml, QStringLiteral("MapStore { id: mapStore }"))
            && rawContainsRosterMap(qml, QStringLiteral("mapStore.clearAll()"))     // 进世界清库(会话口径)
            && rawContainsRosterMap(qml, QStringLiteral("mapStore: mapStore"))      // player 注入行
            && rawContainsRosterMap(qml, QStringLiteral("hotbarVM.selectedItemId === 0x291")) // overlay 显隐门
            && rawContainsRosterMap(qml, QStringLiteral("image://mapstore/"))       // provider 源行
            && rawContainsRosterMap(qml, QStringLiteral("id: mapOverlay"));
        ok = ok && qmlOk;
        if (!qmlOk) diag += QStringLiteral("[qml]");
        const bool glueOk = rawContainsRosterMap(mainCpp,
                QStringLiteral("class MapAtlasProvider : public QQuickImageProvider"))
            && rawContainsRosterMap(mainCpp,
                QStringLiteral("addImageProvider(QStringLiteral(\"mapstore\"), new MapAtlasProvider)"))
            && rawContainsRosterMap(mainCpp, QStringLiteral("#include \"mapstore.h\""));
        ok = ok && glueOk;
        if (!glueOk) diag += QStringLiteral("[glue]");
        const bool propOk = rawContainsRosterMap(pcH,
                QStringLiteral("Q_PROPERTY(MapStore *mapStore READ mapStore WRITE setMapStore NOTIFY mapStoreChanged)"))
            && rawContainsRosterMap(pcH, QStringLiteral("void setMapStore(MapStore *s);"));
        ok = ok && propOk;
        if (!propOk) diag += QStringLiteral("[prop]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2084c display face column (the qml wiring rows instantiate the map store,"
               " clear it on world entry per the session caliber, inject it into the player,"
               " gate the hand held overlay on the filled map id with the provider source and"
               " project the player dot, the app glue registers the mapstore image provider,"
               " and the header carries the map store property row)"
            << (ok ? QString() : diag);
    });

    // ── r2084d:结构钉族(NEG 双摘面全豁免 = 值面 + 源钉对照腿)────────────────────────────────
    //   值面(双 id / 牛奶桶原位 / 图集零变更 / 相邻族零污染) + 源钉族(recipe.h/.cpp + hotbar +
    //   playercontroller .h/.cpp[NEG-1 调用行与 NEG-2 转换行豁免不钉] + mapstore.h/.cpp +
    //   MaterialIcon + CMake) + 四负面钉(罗盘芯不取 / 缩放克隆不取 / 不可堆叠不取 / 存档表零加)。
    runLeg("r2084d structure pin family (the two map ids ride the material segment tail with"
        " milk bucket in place and the atlas unchanged, the recipe and hotbar and controller"
        " and map store and icon and cmake rows are pinned on file with the two negative"
        " lesion faces exempt, the map recipe row carries the compass core per the one point"
        " zero jar read, no zoom or clone"
        " symbol exists, no unstackable special case exists, and no map table rides the"
        " world store per the session caliber)", [&]() {
        bool ok = true;
        QString diag;
        // (D1) 值面:双 id 段位 / 牛奶桶原位 / 图集零变更(物品零新瓦) / 相邻族零污染。
        {
            const bool enumOk = int(RecipeRegistry::EmptyMapId) == 0x290
                && int(RecipeRegistry::FilledMapId) == 0x291
                && int(RecipeRegistry::MilkBucketId) == 0x28F
                && int(BR::AtlasTileCount) == 210                        // 物品面零新瓦(t1113 尾钉原值)
                && int(BR::StandingSign) == 160 && int(BR::WallSign) == 161
                && int(BR::Count) == 162
                && BR::mcBlockId(int(BR::StandingSign)) == 63
                && BR::mcBlockId(int(BR::FenceGate)) == 107
                && BR::mcBlockId(int(BR::Cake)) == 92
                && RecipeRegistry::mcMaterialId(int(RecipeRegistry::MilkBucketId)) == -1;
            ok = ok && enumOk;
            if (!enumOk) diag += QStringLiteral("[enum]");
        }
        // (D2) 源钉族(NEG-1 调用行 / NEG-2 转换行均豁免不钉——恰红归因全权在 b/a)。
        {
            const QString srcDir = srcRootForRosterMapPins();
            const QStringList missRh = pinSet(srcDir + QStringLiteral("/Game/recipe.h"), {
                SrcPin("empty id decl", "static constexpr int EmptyMapId  = 0x290;", 1),
                SrcPin("filled id decl", "static constexpr int FilledMapId = 0x291;", 1)});
            ok = ok && missRh.isEmpty();
            if (!missRh.isEmpty())
                diag += QStringLiteral("[rh %1]").arg(missRh.join(QLatin1Char(',')));
            // 裁定锚注（t1114 勘误表在 recipe.h 行注——注释体锚走 raw 含，pinSet 剥注释会失配）。
            const bool rulingAnchor = rawContainsRosterMap(srcDir + QStringLiteral("/Game/recipe.h"),
                                                           QStringLiteral("t1114"));
            ok = ok && rulingAnchor;
            if (!rulingAnchor) diag += QStringLiteral("[rhAnchor]");
            const QStringList missRc = pinSet(srcDir + QStringLiteral("/Game/recipe.cpp"), {
                SrcPin("map recipe row", "RecipeRegistry::EmptyMapId, 1, 1, \"empty_map\" },", 1),
                SrcPin("empty id assert", "RecipeRegistry::EmptyMapId  == 0x290", 1),
                SrcPin("filled id assert", "RecipeRegistry::FilledMapId == 0x291", 1)});
            ok = ok && missRc.isEmpty();
            if (!missRc.isEmpty())
                diag += QStringLiteral("[rc %1]").arg(missRc.join(QLatin1Char(',')));
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("empty name row", "RecipeRegistry::EmptyMapId)         return QStringLiteral(\"空地图\")", 1),
                SrcPin("filled name row", "RecipeRegistry::FilledMapId)        return QStringLiteral(\"填充地图\")", 1),
                SrcPin("palette empty row", "int(RecipeRegistry::EmptyMapId),", 1)});
            ok = ok && missHb.isEmpty();
            if (!missHb.isEmpty())
                diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
            // 调色板填充地图行（行尾注释体锚——pinSet 剥注释失配，raw 含同门）。
            //   [lawful 修订 t1115/r2085] 行尾自「无逗号形」改「逗号行」——苹果/金苹果（0x292/0x293）
            //   t1115 段尾追加，填充地图行让出数组尾位（r2073d/r2075d 砂岩楼梯尾行同门先例）。
            const bool palFilledRow = rawContainsRosterMap(srcDir + QStringLiteral("/Game/hotbar.cpp"),
                                                           QStringLiteral("int(RecipeRegistry::FilledMapId),        // 填充地图：激活产物；手持显地图 overlay（会话数据集）"));
            ok = ok && palFilledRow;
            if (!palFilledRow) diag += QStringLiteral("[hbPalFilled]");
            const QStringList missPcH = pinSet(srcDir + QStringLiteral("/Game/playercontroller.h"), {
                SrcPin("property row", "Q_PROPERTY(MapStore *mapStore READ mapStore WRITE setMapStore NOTIFY mapStoreChanged)", 1),
                SrcPin("tick decl", "void tickMapExploration(float dt);", 1),
                SrcPin("explore interval", "kMapExploreIntervalSec = 0.5f;", 1),
                SrcPin("explore radius", "kMapExploreRadius      = 16;", 1),
                SrcPin("initial radius", "kMapInitialRadius      = 24;", 1)});
            ok = ok && missPcH.isEmpty();
            if (!missPcH.isEmpty())
                diag += QStringLiteral("[pcH %1]").arg(missPcH.join(QLatin1Char(',')));
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("activation branch head", "if (m_hotbar && m_world && heldItemId == RecipeRegistry::EmptyMapId) {", 1),
                SrcPin("tick impl head", "void PlayerController::tickMapExploration(float dt)", 1),
                SrcPin("refresh impl head", "void PlayerController::refreshMapAroundPlayer(int radius)", 1),
                SrcPin("color impl head", "quint32 PlayerController::mapColumnColor(int x, int z)", 1),
                SrcPin("lazy init row", "m_mapStore->initialize(m_world->width(), m_world->depth());", 2), // 激活 + 惰性双处
                SrcPin("first draw row", "refreshMapAroundPlayer(kMapInitialRadius);", 1),
                SrcPin("commit row", "m_mapStore->commitColumns();", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
            const QStringList missMsH = pinSet(srcDir + QStringLiteral("/Game/mapstore.h"), {
                SrcPin("class row", "class MapStore : public QObject", 1),
                SrcPin("unexplored row", "static constexpr quint32 kUnexploredColor = 0xFF15151a;", 1),
                SrcPin("clearAll decl", "Q_INVOKABLE void clearAll();", 1),
                SrcPin("renderImage decl", "QImage renderImage() const;", 1)});
            ok = ok && missMsH.isEmpty();
            if (!missMsH.isEmpty())
                diag += QStringLiteral("[msH %1]").arg(missMsH.join(QLatin1Char(',')));
            const QStringList missMsC = pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
                SrcPin("initialize impl", "void MapStore::initialize(int width, int depth)", 1),
                SrcPin("writeColumn impl", "void MapStore::writeColumn(int x, int z, quint32 argb)", 1)});
            ok = ok && missMsC.isEmpty();
            if (!missMsC.isEmpty())
                diag += QStringLiteral("[msC %1]").arg(missMsC.join(QLatin1Char(',')));
            const QStringList missMi = pinSet(srcDir + QStringLiteral("/ui/MaterialIcon.qml"), {
                SrcPin("blank case row", "case 0x290: drawMapBlank(); break", 1),
                SrcPin("filled case row", "case 0x291: drawMapFilled(); break", 1)});
            ok = ok && missMi.isEmpty();
            if (!missMi.isEmpty())
                diag += QStringLiteral("[mi %1]").arg(missMi.join(QLatin1Char(',')));
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake mapstore src", "src/Game/mapstore.cpp", 2), // 主目标 + 矩阵目标两列
                SrcPin("cmake section77", "tools/matrix/section77_roster_map_t1114.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (D3) 四钉([t1128 lawful 修订一处]——罗盘芯钉自「负面钉:行内零 CompassId」翻转为
        //     「era 定谳钉:行内含 CompassId」,沿革=外部审查翻案 jar 定谳,工件
        //     build/t1128_jar_crafting_map_sign.txt;其余三负面钉原样):缩放/克隆不取
        //     (无缩放/克隆符号)+ 不可堆叠不取(maxStackSize 无地图特例)+ 存档表零加(会话口径,
        //     worldstore 无 map 表)。
        {
            const QString srcDir = srcRootForRosterMapPins();
            // 罗盘芯([t1128 翻案钉]):定位地图配方行(pattern 三行 + 产物行四行窗口),断言窗口内含
            //   罗盘(era 1.0.0 罗盘芯在册——旧负面钉「行内零 CompassId = 12w34a+ 面不取」随 jar
            //   定谳撤销翻转)。
            QFile rc(srcDir + QStringLiteral("/Game/recipe.cpp"));
            bool compassCore = false;
            if (rc.open(QIODevice::ReadOnly)) {
                const QStringList lines = QString::fromUtf8(rc.readAll()).split(QLatin1Char('\n'));
                for (int i = 0; i < lines.size(); ++i) {
                    if (!lines.at(i).contains(QStringLiteral("\"empty_map\""))) continue;
                    for (int j = qMax(0, i - 3); j <= i && !compassCore; ++j)
                        compassCore = lines.at(j).contains(QStringLiteral("CompassId"));
                    break;
                }
            }
            ok = ok && compassCore; // [t1128] era 罗盘芯(旧钉翻转,沿革注见上)
            if (!compassCore) diag += QStringLiteral("[compassCore]");
            // 缩放 / 克隆面不取(12w34a/1.4.2+ 面留痕不取——无缩放/克隆符号)。
            const bool noZoom = !rawContainsRosterMap(srcDir + QStringLiteral("/Game/recipe.h"),
                                                      QStringLiteral("MapZoom"))
                && !rawContainsRosterMap(srcDir + QStringLiteral("/Game/playercontroller.h"),
                                         QStringLiteral("mapZoomLevel"))
                && !rawContainsRosterMap(srcDir + QStringLiteral("/Game/mapstore.h"),
                                         QStringLiteral("cloneMap"));
            ok = ok && noZoom;
            if (!noZoom) diag += QStringLiteral("[noZoom]");
            // 不可堆叠不取(1.0 翻案面:maxStackSize 无地图特例行)。
            const bool noUnstack = !rawContainsRosterMap(srcDir + QStringLiteral("/Game/hotbar.cpp"),
                                                         QStringLiteral("RecipeRegistry::FilledMapId) return 1"));
            ok = ok && noUnstack;
            if (!noUnstack) diag += QStringLiteral("[noUnstack]");
            // 存档表零加(会话口径:worldstore 无 map 表门)。
            const bool noTable = !rawContainsRosterMap(srcDir + QStringLiteral("/World/worldstore.h"),
                                                       QStringLiteral("map_data"))
                && !rawContainsRosterMap(srcDir + QStringLiteral("/World/worldstore.h"),
                                         QStringLiteral("loadMaps"));
            ok = ok && noTable;
            if (!noTable) diag += QStringLiteral("[noTable]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2084d structure pin family (the two map ids ride the material segment tail"
               " with milk bucket in place and the atlas unchanged, the recipe and hotbar and"
               " controller and map store and icon and cmake rows are pinned on file with the"
               " two negative lesion faces exempt, the map recipe row carries the compass core"
               " per the one point zero jar read, no zoom or clone symbol exists, no"
               " unstackable special case exists, and no"
               " map table rides the world store per the session caliber)"
            << (ok ? QString() : diag);
    });
}
