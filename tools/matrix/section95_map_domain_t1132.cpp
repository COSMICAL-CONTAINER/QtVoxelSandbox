#include "matrix_helpers.h"

// t1132 地图域批探针段（5 腿；filter 词 r2102；矩阵 948→953）。置尾先例沿用（接 section94，
//   runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / sparse 构造缝 / 真临时库 / 纯源钉腿）。
//
// ── era 定谳与选案（交接单 MAP-02/03；留痕三面 = 源码注 + 本段腿文 + 提交注）────────────────
//   MAP-02 era 真值：1.0 地图 = 128×128 固定画布、首用定锚玩家位置、1:1 单缩放、画布绑定地图物品
//   自身原点。工程 t1114 交付 = 单份全幅数据集绑定核心域。选案 = **b 案（最小修复）**：保持单份
//   数据集，绘制/存储域从「核心域裁剪」扩为「核心域 + 四侧 kDomainMargin 前沿带」的有界扩展域——
//   era 偏离（全幅共享 vs per-map 128 画布）已在 t1114 登记，本单如实延伸；「无地形不绘」门
//   （heightmapAt = -1：sparse 未物化 / fixed 域外）保住未探索底色 = 不绘虚空。era 128 画布封顶的
//   工程对应面 = 前沿带有界（64 列），禁无限位图。选案理由：与件二裁定联动——per-map 画布（a 案）
//   需要物品侧 per-map 身份（era damage 面工程材料段纪律冲突），改动面过大（见下裁定），b 案与
//   件二共享口径持久化自洽。
//   MAP-03 era 真值：per-map 独立持久化（map_xx.dat：颜色数组 + x/z 中心 + 尺寸 + 缩放）。**部分
//   裁定收口**：per-map 身份牵动 Hotbar damage 段纪律 / 栈合并拆分 / 物品实体元数据链 / overlay
//   序号路由 / 创造调色板全动 = 改动面过大 → 交付**首片 = 共享口径 per-world 单份数据集持久化**
//   （map_dataset 单行表 initSchema 纯追加 + 统一保存链第 10 参同事务落盘 + 进世界 loadVariant
//   整体替换 + 旧档无行空集降级），per-map 族全模型设计入本段文末裁定锚 + 候选池（设计全文随任务
//   报告交付）。禁硬造半吊子持久化 = 本片对「共享口径」自洽完备（往返 / 隔离 / 兼容三面全验）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；摘调用点形编译绿）──────────────────────────────────
//   NEG-1 = 摘 playercontroller.cpp refreshMapAroundPlayer 的「无地形不绘」判定行
//     （if (m_world->heightmapAt(x, z) < 0) continue; 单语句行删除，编译仍绿）→ 恰红 = {r2102b}
//     （无地形列 [in-box 未物化] 被绘成石族灰 → 未探索保底色断言 FAIL；r2084b 盒一/盒二列全部
//     有地形 = 幸存；r2102a 纯数据面不触扫描 = 幸存）。
//   NEG-2 = 摘 mapstore.cpp exportVariant 的 pixels 载荷行（v.insert(QStringLiteral("pixels"),
//     m_pixels); 单语句行删除，编译仍绿）→ 恰红 = {r2102c}（导出缺 pixels → 装载走账不平降级
//     clearAll → 往返断言 FAIL；QML 落盘行运行期才消费 = 幸存；r2102d 钉的是 QML 行与 decl 行，
//     载荷行豁免不钉 = 幸存）。
//   【t1133 lawful 修订留痕】r2102d「exit save row」尾针自 t1133 起由 "mapStore.exportVariant())"
//     迁至 "mapStore.exportVariant(), entityManager.exportPersistedEntities())"——统一保存链追加
//     第 15 参（生物持久化）后地图第 14 参不再是调用尾，原尾针 "))" 闭合被接续实参顶替必失配；
//     新针同时承载地图载荷行与新尾实参，第 10/14 参事实面逐位不弱化（worldstore / coordinator /
//     桥三面第 10 参钉原样幸存）。
namespace {

// 源钉根路径（section77 同门：applicationDirPath/../src）。
inline QString srcRootForMapDomainPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体锚——pinSet 剥注释会失配，section72..77 同款）。
inline bool rawContainsMapDomain(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 真链 pc rig 的探索节拍驱动器（section77 driveRosterMapExplore 同门：放置 CD 泵 + 0.5s 相位
//   越窗——单 tick dt 钳 0.05 → 逐 tick 墙钟泵 70ms 跑 16 拍 ≥ 0.8s）。
inline void pumpMapDomain(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
inline void driveMapDomainExplore(PlayerController &pc, int beats)
{
    for (int i = 0; i < beats; ++i) {
        pc.tick();
        pumpMapDomain(70);
    }
}

// sparse rig 三列确定性草面（loadChunkAt 物化后覆盖自然地形——地形 <40 的 s82 带之上铺 y=79 草 +
//   上空 80..92 清 Air = heightmap 恒 79 草顶，地图色确定）。
inline void layMapDomainGrass(World &w, int x, int z)
{
    for (int y = 80; y <= 92; ++y)
        w.setBlock(x, y, z, BR::Air, 0);
    w.setBlock(x, 79, z, BR::Grass, 0);
}

// 临时库路径（section92 tempDb 同门：QDir::temp() + pid + 腿标，测试自清理）。
inline QString mapDomainTempDb(const char *tag)
{
    return QDir::temp().absoluteFilePath(QStringLiteral("vo_t1132_map_%1_%2.sqlite")
                                             .arg(QLatin1String(tag))
                                             .arg(QCoreApplication::applicationPid()));
}

} // namespace

void MatrixRun::section95_map_domain_t1132()
{
    // ── r2102a:扩展域数据面柱（件一 MAP-02 数据面直驱）──────────────────────────────────────
    //   建库扩展域定版（核心域 + 2×带）+ 四象限/零轴写读（负象限 / 超核心域 / 带内零轴）+ 带外
    //   双向拒写 + 出图随域 + revision 单调 + 清库 + 未建库兜底 + 空装载降级。
    runLeg("r2102a extended domain data column (the map store builds the dataset at the world"
        " core plus a bounded frontier band on all four sides, answers writes and reads in the"
        " negative quadrant and beyond the core and on the zero axes inside the band, refuses"
        " writes outside the band on both sides, exports the image at the dataset size with a"
        " monotonic revision, clears on demand, keeps the unexplored fallback for out of band"
        " and uninitialized reads, and an empty variant load degrades to the cleared state)",
        [&]() {
        bool ok = true;
        QString diag;
        const quint32 c1 = 0xFF4e8f3c, c2 = 0xFF3b6cb0, c3 = 0xFFd9cf9c, c4 = 0xFF7a5c38;
        const int B = MapStore::kDomainMargin;
        // (1) 未建库兜底 + 建库扩展域定版。
        MapStore ms;
        const bool freshOk = !ms.hasMap()
            && ms.columnColor(-5, -5) == MapStore::kUnexploredColor
            && ms.columnColor(5, 5) == MapStore::kUnexploredColor;
        ok = ok && freshOk;
        if (!freshOk) diag += QStringLiteral("[fresh]");
        ms.initialize(48, 48);
        const bool initOk = ms.hasMap()
            && ms.mapWidth() == 48 + 2 * B && ms.mapDepth() == 48 + 2 * B
            && ms.mapMargin() == B
            && ms.columnColor(0, 0) == MapStore::kUnexploredColor;
        ok = ok && initOk;
        if (!initOk) diag += QStringLiteral("[init]");
        // (2) 四象限 + 零轴写读（负象限 / 超核心域 / 带内零轴——世界坐标直进直出）。
        ms.writeColumn(-10, -20, c1);
        ms.writeColumn(60, 5, c2);   // x 超核心域（48）入带
        ms.writeColumn(5, 60, c3);   // z 超核心域入带
        ms.writeColumn(0, -1, c4);   // 零轴（x=0 负 z 侧）
        ms.writeColumn(-1, 0, c4);   // 零轴（z=0 负 x 侧）
        ms.commitColumns();
        const bool quadOk = ms.columnColor(-10, -20) == c1
            && ms.columnColor(60, 5) == c2
            && ms.columnColor(5, 60) == c3
            && ms.columnColor(0, -1) == c4
            && ms.columnColor(-1, 0) == c4;
        ok = ok && quadOk;
        if (!quadOk) diag += QStringLiteral("[quad]");
        // (3) 带外双向拒写（era 画布有界封顶——两侧越带恒底色）。
        ms.writeColumn(-B - 1, 0, c1);
        ms.writeColumn(48 + B, 0, c1);
        ms.writeColumn(0, -B - 1, c1);
        ms.writeColumn(0, 48 + B, c1);
        const bool boundOk = ms.columnColor(-B - 1, 0) == MapStore::kUnexploredColor
            && ms.columnColor(48 + B, 0) == MapStore::kUnexploredColor
            && ms.columnColor(0, -B - 1) == MapStore::kUnexploredColor
            && ms.columnColor(0, 48 + B) == MapStore::kUnexploredColor;
        ok = ok && boundOk;
        if (!boundOk) diag += QStringLiteral("[bound]");
        // (4) 出图随域（下标 = 世界坐标 + 带平移）+ revision 单调 + 清库 + 空装载降级。
        const QImage img = ms.renderImage();
        const bool imgOk = img.width() == 48 + 2 * B && img.height() == 48 + 2 * B
            && img.pixelColor(-10 + B, -20 + B).rgba() == c1
            && img.pixelColor(60 + B, 5 + B).rgba() == c2;
        ok = ok && imgOk;
        if (!imgOk) diag += QStringLiteral("[img %1x%2]").arg(img.width()).arg(img.height());
        const int revBefore = ms.revision();
        ms.commitColumns();
        const bool revOk = ms.revision() > revBefore;
        ok = ok && revOk;
        if (!revOk) diag += QStringLiteral("[rev]");
        ms.clearAll();
        const bool clearOk = !ms.hasMap();
        ok = ok && clearOk;
        if (!clearOk) diag += QStringLiteral("[clear]");
        ms.loadVariant(QVariantMap()); // 空装载（旧档无行降级形态）——清态保持不崩
        const bool degradeOk = !ms.hasMap();
        ok = ok && degradeOk;
        if (!degradeOk) diag += QStringLiteral("[degrade]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2102a extended domain data column (the map store builds the dataset at the"
               " world core plus a bounded frontier band on all four sides, answers writes and"
               " reads in the negative quadrant and beyond the core and on the zero axes inside"
               " the band, refuses writes outside the band on both sides, exports the image at"
               " the dataset size with a monotonic revision, clears on demand, keeps the"
               " unexplored fallback for out of band and uninitialized reads, and an empty"
               " variant load degrades to the cleared state)"
            << (ok ? QString() : diag);
    });

    // ── r2102b:探索扩展真链柱（NEG-1 敏感腿——「无地形不绘」判定行）────────────────────────
    //   sparse 构造缝 + loadChunkAt 三 chunk 物化 + 确定性草面三列 + 持图探索三站走查：核心域站
    //   （零轴列已绘）→ 负象限站（负列已绘 + 盒内未物化列保底色 = NEG-1 红面）→ 超核心域站（带内
    //   列已绘）+ 地形变化刷新（草顶 → 木顶换色）+ 带外列恒底色。
    runLeg("r2102b exploration walk column (a held filled map drives the periodic box scan"
        " across three stations in a sparse streaming core, the core station draws the zero"
        " axis columns, the negative station draws the negative quadrant while an in box"
        " unmaterialized column keeps the unexplored base color, the beyond core station draws"
        " a column past the world width inside the band, replacing the grass top with a log"
        " block refreshes the drawn color on the next pass, and a band edge column never"
        " visited stays unexplored)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } }; // sparse 构造缝：核心 48×48 h96 s82 零预生成（r2053c 同门）
        const bool rig = w.isSparse()
            && w.loadChunkAt(0, 0) && w.loadChunkAt(-1, -1) && w.loadChunkAt(3, 3);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig]");
        layMapDomainGrass(w, 10, 10);
        layMapDomainGrass(w, -10, -10);
        layMapDomainGrass(w, 50, 50);
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
        hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0); // 填充地图上手（惰性建库面）
        pc.setSelectedBlock(int(BR::Air));
        // 站一：核心域 (10.5, 10.5)——盒 [-6,26]² 含零轴列；chunk (-1,-1) 已物化但盒未及 = (-10,-10) 未绘。
        pc.loadSavedState(10.5, 81.0, 10.5, 0.0, 0.0, 1 /* Creative */);
        driveMapDomainExplore(pc, 16);
        const int B = MapStore::kDomainMargin;
        const bool st1Ok = ms.hasMap()
            && ms.mapWidth() == 48 + 2 * B
            && ms.columnColor(10, 10) != MapStore::kUnexploredColor
            && ms.columnColor(0, 10) != MapStore::kUnexploredColor   // 零轴（x=0）
            && ms.columnColor(10, 0) != MapStore::kUnexploredColor   // 零轴（z=0）
            && ms.columnColor(-10, -10) == MapStore::kUnexploredColor; // 负象限未至未绘
        ok = ok && st1Ok;
        if (!st1Ok)
            diag += QStringLiteral("[st1 map=%1 c10=%2 c0=%3 cneg=%4]")
                        .arg(ms.hasMap())
                        .arg(ms.columnColor(10, 10) != MapStore::kUnexploredColor ? 1 : 0)
                        .arg(ms.columnColor(0, 10) != MapStore::kUnexploredColor ? 1 : 0)
                        .arg(ms.columnColor(-10, -10) != MapStore::kUnexploredColor ? 1 : 0);
        const quint32 grassColor = ms.columnColor(10, 10);
        // 站二：负象限 (-10.5, -10.5)——盒 [-26,4]² 覆负列；盒内 (-20,-20) 落未物化 chunk (-2,-2)
        //   = 「无地形不绘」门红面（NEG-1 摘行后被绘成石族灰 → 保底色断言 FAIL）。
        hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0);
        pc.loadSavedState(-10.5, 81.0, -10.5, 0.0, 0.0, 1 /* Creative */);
        driveMapDomainExplore(pc, 16);
        const bool st2Ok = ms.columnColor(-10, -10) != MapStore::kUnexploredColor
            && ms.columnColor(-20, -20) == MapStore::kUnexploredColor;
        ok = ok && st2Ok;
        if (!st2Ok)
            diag += QStringLiteral("[st2 neg=%1 unmat=%2]")
                        .arg(ms.columnColor(-10, -10) != MapStore::kUnexploredColor ? 1 : 0)
                        .arg(ms.columnColor(-20, -20) == MapStore::kUnexploredColor ? 1 : 0);
        // 站三：超核心域 (50.5, 50.5)——盒 [34,66]²；x/z 50 超核心域（48）入带内已绘。
        hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0);
        pc.loadSavedState(50.5, 81.0, 50.5, 0.0, 0.0, 1 /* Creative */);
        driveMapDomainExplore(pc, 16);
        const bool st3Ok = ms.columnColor(50, 50) != MapStore::kUnexploredColor
            && ms.columnColor(60, 60) != MapStore::kUnexploredColor   // 盒内物化自然地形列已绘
            && ms.columnColor(100, 100) == MapStore::kUnexploredColor; // 全站盒外带内列未至未绘
        ok = ok && st3Ok;
        if (!st3Ok)
            diag += QStringLiteral("[st3 beyond=%1 nat=%2 far=%3]")
                        .arg(ms.columnColor(50, 50) != MapStore::kUnexploredColor ? 1 : 0)
                        .arg(ms.columnColor(60, 60) != MapStore::kUnexploredColor ? 1 : 0)
                        .arg(ms.columnColor(100, 100) != MapStore::kUnexploredColor ? 1 : 0);
        // 地形变化刷新：草顶 → 木顶（y=91 置 Log = 列顶换族色）→ 回站一再绘换色。
        hb.setStack(0, int(RecipeRegistry::FilledMapId), 1, 0);
        w.setBlock(10, 91, 10, BR::Log, 0);
        pc.loadSavedState(10.5, 81.0, 10.5, 0.0, 0.0, 1 /* Creative */);
        driveMapDomainExplore(pc, 16);
        const bool refreshOk = ms.columnColor(10, 10) != grassColor
            && ms.columnColor(10, 10) != MapStore::kUnexploredColor;
        ok = ok && refreshOk;
        if (!refreshOk) diag += QStringLiteral("[refresh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2102b exploration walk column (a held filled map drives the periodic box scan"
               " across three stations in a sparse streaming core, the core station draws the"
               " zero axis columns, the negative station draws the negative quadrant while an in"
               " box unmaterialized column keeps the unexplored base color, the beyond core"
               " station draws a column past the world width inside the band, replacing the"
               " grass top with a log block refreshes the drawn color on the next pass, and a"
               " band edge column never visited stays unexplored)"
            << (ok ? QString() : diag);
    });

    // ── r2102c:持久化柱（件二 MAP-03 首片，NEG-2 敏感腿 = exportVariant pixels 载荷行）────────
    //   真临时库三面：存取往返（存 → 重开 → 装载 → 像素/尺寸/revision 复原 + 幂等二次存）+
    //   跨世界隔离（第二库无行 → 空装载降级）+ 旧档兼容（手工最小库无 map 表 → 幂等补建 → 空集
    //   读入不丢不崩 + 新码收敛存取）。
    runLeg("r2102c persistence column (the map dataset rides the unified save chain onto a"
        " single row map table per world, a fresh store round trips the pixels and the world"
        " dims and the revision with a second save staying idempotent, a second world database"
        " answers no row and the empty variant load degrades to cleared, and a hand built"
        " legacy library without the table opens through the additive schema, reads an empty"
        " set without loss or crash, and converges when the new code saves onto it)", [&]() {
        bool ok = true;
        QString diag;
        const quint32 c1 = 0xFF4e8f3c, c2 = 0xFF3b6cb0, c3 = 0xFFd9cf9c;
        const int B = MapStore::kDomainMargin;
        // (1) 存取往返 + 幂等二次存。
        const QString db = mapDomainTempDb("a");
        QFile::remove(db);
        {
            World w;
            w.setWidth(48);
            w.setDepth(48);
            w.setHeight(96);
            w.setSeed(82);
            WorldStore store;
            const bool open1 = store.openWorld(db);
            store.setWorld(&w);
            MapStore ms;
            ms.initialize(48, 48);
            ms.writeColumn(-10, -10, c1);
            ms.writeColumn(55, 5, c2);  // 带内超核心域列（持久化面同保）
            ms.writeColumn(10, 10, c3);
            ms.commitColumns();
            const int savedRev = ms.revision();
            const bool save1 = store.saveAll(QStringLiteral("mapworld"), {}, {}, {}, {}, {}, {},
                                             {}, {}, ms.exportVariant());
            store.closeWorld();
            // 重开装载（fresh store + fresh MapStore——enterWorld 形态镜像）。
            const bool open2 = store.openWorld(db);
            store.setWorld(&w);
            const QVariantMap row = store.loadMapDataset();
            MapStore ms2;
            ms2.loadVariant(row);
            const bool rtOk = open1 && save1 && open2
                && !row.isEmpty() && row.value(QStringLiteral("present")).toBool()
                && row.value(QStringLiteral("width")).toInt() == 48
                && ms2.hasMap()
                && ms2.mapWidth() == 48 + 2 * B && ms2.mapDepth() == 48 + 2 * B
                && ms2.columnColor(-10, -10) == c1
                && ms2.columnColor(55, 5) == c2
                && ms2.columnColor(10, 10) == c3
                && ms2.columnColor(0, 0) == MapStore::kUnexploredColor
                && ms2.revision() == savedRev;
            ok = ok && rtOk;
            if (!rtOk)
                diag += QStringLiteral("[rt open1=%1 save=%2 open2=%3 row=%4 map=%5 rev=%6]")
                            .arg(open1).arg(save1).arg(open2).arg(!row.isEmpty())
                            .arg(ms2.hasMap())
                            .arg(ms2.revision()).arg(savedRev);
            // 幂等二次存（同数据集再存 → 再开读回逐位同）。
            const bool save2 = store.saveAll(QStringLiteral("mapworld"), {}, {}, {}, {}, {}, {},
                                             {}, {}, ms2.exportVariant());
            store.closeWorld();
            const bool open3 = store.openWorld(db);
            store.setWorld(&w);
            MapStore ms3;
            ms3.loadVariant(store.loadMapDataset());
            const bool idemOk = save2 && open3
                && ms3.hasMap()
                && ms3.columnColor(-10, -10) == c1
                && ms3.revision() == savedRev;
            ok = ok && idemOk;
            if (!idemOk) diag += QStringLiteral("[idem]");
            store.closeWorld();
        }
        // (2) 跨世界隔离（第二库 = 独立文件无行 → 空 map → 装载降级清态）。
        {
            const QString db2 = mapDomainTempDb("b");
            QFile::remove(db2);
            World w;
            w.setWidth(48);
            w.setDepth(48);
            w.setHeight(96);
            w.setSeed(83);
            WorldStore store2;
            const bool openB = store2.openWorld(db2);
            store2.setWorld(&w);
            const QVariantMap rowB = store2.loadMapDataset();
            MapStore msB;
            msB.loadVariant(rowB);
            const bool isoOk = openB && rowB.isEmpty() && !msB.hasMap();
            ok = ok && isoOk;
            if (!isoOk)
                diag += QStringLiteral("[iso open=%1 row=%2 map=%3]")
                            .arg(openB).arg(!rowB.isEmpty()).arg(msB.hasMap());
            store2.closeWorld();
            QFile::remove(db2);
        }
        // (3) 旧档兼容（手工最小库无 map_dataset 表——r2099d(2) 同门：user_version 0 + 旧表族 +
        //   手插行；新码开库幂等补建 → 空集读入不丢不崩 → 新码存取收敛）。
        {
            const QString db3 = mapDomainTempDb("c");
            QFile::remove(db3);
            const QString conn = QStringLiteral("r2102c_%1").arg(QCoreApplication::applicationPid());
            bool built = false;
            {
                if (QSqlDatabase::contains(conn))
                    QSqlDatabase::removeDatabase(conn);
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                p.setDatabaseName(db3);
                if (p.open()) {
                    QSqlQuery q(p);
                    built = q.exec(QStringLiteral(
                        "CREATE TABLE world_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))
                        && q.exec(QStringLiteral(
                            "INSERT INTO world_meta (key, value) VALUES ('name', 'legacy-map')"));
                }
            }
            QSqlDatabase::removeDatabase(conn);
            World w;
            w.setWidth(48);
            w.setDepth(48);
            w.setHeight(96);
            w.setSeed(84);
            WorldStore store3;
            const bool openC = store3.openWorld(db3);
            store3.setWorld(&w);
            MapStore msC0;
            const QVariantMap rowC = store3.loadMapDataset();
            msC0.loadVariant(rowC); // 空集装载（旧档降级——不丢不崩）
            // 新码收敛：建库绘图 → 存 → 再开 → 复原。
            MapStore msC1;
            msC1.initialize(48, 48);
            msC1.writeColumn(-30, -30, c1);
            msC1.commitColumns();
            const bool saveC = store3.saveAll(QStringLiteral("legacy-map"), {}, {}, {}, {}, {},
                                              {}, {}, {}, msC1.exportVariant());
            store3.closeWorld();
            const bool openD = store3.openWorld(db3);
            store3.setWorld(&w);
            MapStore msC2;
            msC2.loadVariant(store3.loadMapDataset());
            const bool legacyOk = built && openC && rowC.isEmpty() && !msC0.hasMap()
                && saveC && openD
                && msC2.hasMap()
                && msC2.columnColor(-30, -30) == c1;
            ok = ok && legacyOk;
            if (!legacyOk)
                diag += QStringLiteral("[legacy built=%1 open=%2 row=%3 save=%4 reopen=%5 map=%6]")
                            .arg(built).arg(openC).arg(rowC.isEmpty()).arg(saveC).arg(openD)
                            .arg(msC2.hasMap());
            store3.closeWorld();
            QFile::remove(db3);
        }
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2102c persistence column (the map dataset rides the unified save chain onto a"
               " single row map table per world, a fresh store round trips the pixels and the"
               " world dims and the revision with a second save staying idempotent, a second"
               " world database answers no row and the empty variant load degrades to cleared,"
               " and a hand built legacy library without the table opens through the additive"
               " schema, reads an empty set without loss or crash, and converges when the new"
               " code saves onto it)"
            << (ok ? QString() : diag);
    });

    // ── r2102d:接线柱（件二保存链 raw 钉——NEG-2 载荷行豁免不钉）────────────────────────────
    //   Main.qml 装载行 + 统一保存链第 10 参行 + 位点投影带平移行 + worldstore 表/读面/写体 +
    //   SaveRequest 载荷字段 + coordinator 透传 + 桥透传 + CMake 段行。
    runLeg("r2102d wiring column (the qml entry loads the map dataset from the world store and"
        " the exit save passes the exported variant as the tenth save argument, the player dot"
        " projection offsets by the frontier band through the margin property, the world store"
        " carries the table row the loader face and the writer body, the request payload and"
        " the coordinator and the bridge pass the dataset through, and the cmake section row is"
        " on file)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForMapDomainPins();
        const QStringList missQml = pinSet(srcDir + QStringLiteral("/ui/Main.qml"), {
            SrcPin("entry load row", "mapStore.loadVariant(worldStore.loadMapDataset())", 1),
            SrcPin("exit save row", // lawful 修订 t1137：第 16 参（活塞动画）追加后调用尾随迁——本行
                                    //   仍承第 10 参导出变体面；t1133 曾迁第 15 参追加后的调用尾（头注留痕）
                   "mapStore.exportVariant(), entityManager.exportPersistedEntities(),", 1),
            SrcPin("dot offset x row",
                   "mapImage.width * ((player.feetPosition.x + mapStore.mapMargin) / Math.max(1, mapStore.mapWidth)) - 3.5", 1),
            SrcPin("dot offset z row",
                   "mapImage.height * ((player.feetPosition.z + mapStore.mapMargin) / Math.max(1, mapStore.mapDepth)) - 3.5", 1)});
        ok = ok && missQml.isEmpty();
        if (!missQml.isEmpty())
            diag += QStringLiteral("[qml %1]").arg(missQml.join(QLatin1Char(',')));
        const QStringList missWsH = pinSet(srcDir + QStringLiteral("/World/worldstore.h"), {
            SrcPin("loader decl", "Q_INVOKABLE QVariantMap loadMapDataset() const;", 1),
            SrcPin("saveAll tenth arg", "const QVariantMap &mapDataset = {});", 1)});
        ok = ok && missWsH.isEmpty();
        if (!missWsH.isEmpty())
            diag += QStringLiteral("[wsH %1]").arg(missWsH.join(QLatin1Char(',')));
        const QStringList missWsC = pinSet(srcDir + QStringLiteral("/World/worldstore.cpp"), {
            SrcPin("table row", "CREATE TABLE IF NOT EXISTS map_dataset (", 1),
            SrcPin("writer head", "bool WorldStore::writeMapDataset(const QVariantMap &dataset)", 1),
            SrcPin("loader head", "QVariantMap WorldStore::loadMapDataset() const", 1)});
        ok = ok && missWsC.isEmpty();
        if (!missWsC.isEmpty())
            diag += QStringLiteral("[wsC %1]").arg(missWsC.join(QLatin1Char(',')));
        const QStringList missScH = pinSet(srcDir + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("payload field", "QVariantMap mapDataset;", 1)});
        ok = ok && missScH.isEmpty();
        if (!missScH.isEmpty())
            diag += QStringLiteral("[scH %1]").arg(missScH.join(QLatin1Char(',')));
        const QStringList missScC = pinSet(srcDir + QStringLiteral("/World/savecoordinator.cpp"), {
            SrcPin("world part call", "req.brewingStands, req.signs, req.mapDataset))", 1)});
        ok = ok && missScC.isEmpty();
        if (!missScC.isEmpty())
            diag += QStringLiteral("[scC %1]").arg(missScC.join(QLatin1Char(',')));
        const QStringList missSb = pinSet(srcDir + QStringLiteral("/Game/savebridge.cpp"), {
            SrcPin("bridge pass row", "req.mapDataset = mapDataset;", 1)});
        ok = ok && missSb.isEmpty();
        if (!missSb.isEmpty())
            diag += QStringLiteral("[sb %1]").arg(missSb.join(QLatin1Char(',')));
        const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"),
                                          {
                                              SrcPin("cmake section row",
                                                     "tools/matrix/section95_map_domain_t1132.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2102d wiring column (the qml entry loads the map dataset from the world store"
               " and the exit save passes the exported variant as the tenth save argument, the"
               " player dot projection offsets by the frontier band through the margin property,"
               " the world store carries the table row the loader face and the writer body, the"
               " request payload and the coordinator and the bridge pass the dataset through,"
               " and the cmake section row is on file)"
            << (ok ? QString() : diag);
    });

    // ── r2102e:结构钉族 + 裁定锚柱（NEG 双摘面全豁免 = 值面 + 源钉对照腿）────────────────────
    //   值面（带宽常量 64 单一权威 + 双 id 段位原位 + 物品面零变更）+ 源钉族（mapstore 声明/实现
    //   行 + playercontroller 扫描域面行——「无地形不绘」判定行豁免不钉 + 裁定锚注）+ 跨任务词元
    //   零命中 + QML 玩法路径零迁移负面门复钉。
    runLeg("r2102e structure pin family and ruling anchor column (the frontier band constant"
        " rides the map store as the single authority with the value sixty four, the two map"
        " ids and the material segment stay in place, the store and controller faces are pinned"
        " on file with the terrain gate row exempt, the per map candidate ruling and the shared"
        " caliber persistence deviation are anchored in the store header, the filter token"
        " stays out of the source tree, and the gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        // (E1) 值面：带宽单一权威 + 双 id 段位原位（相邻族零污染）。
        {
            Hotbar hbPal;
            const bool enumOk = MapStore::kDomainMargin == 64
                && int(RecipeRegistry::EmptyMapId) == 0x290
                && int(RecipeRegistry::FilledMapId) == 0x291
                && int(RecipeRegistry::MilkBucketId) == 0x28F
                && hbPal.maxStackSize(int(RecipeRegistry::FilledMapId)) == 64;
            ok = ok && enumOk;
            if (!enumOk) diag += QStringLiteral("[enum]");
        }
        // (E2) 源钉族（NEG-1 判定行 / NEG-2 载荷行豁免不钉）。
        {
            const QString srcDir = srcRootForMapDomainPins();
            const QStringList missMsH = pinSet(srcDir + QStringLiteral("/Game/mapstore.h"), {
                SrcPin("band constant", "static constexpr int kDomainMargin = 64;", 1),
                SrcPin("margin property", "Q_PROPERTY(int mapMargin READ mapMargin CONSTANT)", 1),
                SrcPin("export decl", "Q_INVOKABLE QVariantMap exportVariant() const;", 1),
                SrcPin("load decl", "Q_INVOKABLE void loadVariant(const QVariantMap &data);", 1)});
            ok = ok && missMsH.isEmpty();
            if (!missMsH.isEmpty())
                diag += QStringLiteral("[msH %1]").arg(missMsH.join(QLatin1Char(',')));
            const QStringList missMsC = pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
                SrcPin("export head", "QVariantMap MapStore::exportVariant() const", 1),
                SrcPin("load head", "void MapStore::loadVariant(const QVariantMap &data)", 1),
                SrcPin("domain guard", "if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_depth)", 1)});
            ok = ok && missMsC.isEmpty();
            if (!missMsC.isEmpty())
                diag += QStringLiteral("[msC %1]").arg(missMsC.join(QLatin1Char(',')));
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("domain bounds row",
                       "const int xLo = -MapStore::kDomainMargin, zLo = -MapStore::kDomainMargin;", 1),
                SrcPin("scan head", "void PlayerController::refreshMapAroundPlayer(int radius)", 1)});
            ok = ok && missPc.isEmpty();
            if (!missPc.isEmpty())
                diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
            // 裁定锚注（注释体锚——raw 含）：候选池登记 + 共享口径偏离登记 + era 定谳留痕。
            const bool anchorOk = rawContainsMapDomain(srcDir + QStringLiteral("/Game/mapstore.h"),
                                                       QStringLiteral("候选池"))
                && rawContainsMapDomain(srcDir + QStringLiteral("/Game/mapstore.h"),
                                        QStringLiteral("存档面裁定"))
                && rawContainsMapDomain(srcDir + QStringLiteral("/Game/mapstore.h"),
                                        QStringLiteral("t1132"));
            ok = ok && anchorOk;
            if (!anchorOk) diag += QStringLiteral("[anchor]");
        }
        // (E3) 跨任务词元零命中（filter 词只落矩阵域——src/ 全树零 r2102）。
        {
            bool leaked = false;
            QDirIterator it(srcRootForMapDomainPins(), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsMapDomain(it.next(), QStringLiteral("r2102"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (E4) QML 玩法路径零迁移负面门（r2090d 同门复钉——Main.qml 零新桥字面）。
        {
            const QString qml = srcRootForMapDomainPins() + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsMapDomain(qml, QStringLiteral("GameSession"))
                && !rawContainsMapDomain(qml, QStringLiteral("MeshWorker"))
                && !rawContainsMapDomain(qml, QStringLiteral("setChunkLifecycle"))
                && !rawContainsMapDomain(qml, QStringLiteral("ChunkEvictor"))
                && !rawContainsMapDomain(qml, QStringLiteral("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2102e structure pin family and ruling anchor column (the frontier band"
               " constant rides the map store as the single authority with the value sixty"
               " four, the two map ids and the material segment stay in place, the store and"
               " controller faces are pinned on file with the terrain gate row exempt, the per"
               " map candidate ruling and the shared caliber persistence deviation are anchored"
               " in the store header, the filter token stays out of the source tree, and the"
               " gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
