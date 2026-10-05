#include "matrix_helpers.h"

// t1123 用户实测 P0 三件批探针段(3 腿;filter 词 r2093;矩阵 905→908)。置尾先例沿用(接 section85,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc rig,余纯源钉腿)。
//
// ── 三件裁定表(每件 MC 真值 / 缺席成因实读留痕;2026-10-01 用户实测运行 build 戳 bd2c3612 = 本 HEAD)──
//   件三(先做) 漏斗贴面放置排料口朝向反:**用户实测翻案交付**——用户原话「漏斗贴方块左右放置时排料口
//     方向反」。MC 真值(wiki Hopper/Placement:the output faces the block it was placed against;嘴恒
//     不朝上):贴**侧面**放置 → 排料口指向**被点方块**(命中面外法线的**反方向**)——正是「对着容器侧壁
//     放漏斗即接入该容器」的经典用法;t1080 原裁定「沿命中面外法线(指向所贴面外向)」四向同错(外向 ↔
//     内向整体取反)。修复 = playercontroller.cpp 漏斗放置侧面行取反法线映射(顶/底面 → 朝下 flag 不变
//     ——底面 MC 亦朝下);解码端 hopperOutDelta / mesher 嘴盒 / 探针三方同源零改动(只翻写入向,消费
//     面语义不变)。r2052 族钉的是 scanHoppers 手工 state 机制面,放置写入行无既钉;牌子挂墙同句式钉
//     (r2083d「wall facing row」count 2)lawful 修订 2→1 携沿革注(漏斗行改写后仅牌子分支幸存)。
//   件二(次做) 创造背包图标缺席:**全量差集扫描清偿**——创造清单 147 条目 × 图标路由(hotbar.cpp
//     iconFileForBlock case / isPackDerivedIconFamily 程序图集重渲族 / CMake qrc 资源)差集恰五件:
//     Hopper / BrewingStand / Melon / JackOLantern / Cauldron 自各入册起 iconFileForBlock 无 case 且
//     不在重渲族 → iconSourceForBlock 回退链四层全空 = 调色板条目透明(用户报漏斗 / 酿造台「看不到」;
//     形态 = 图标文件整缺,≠ t1117 已扫的「在盘空白件」形态——该扫面 r2087a 已固化下界,本批五件
//     非空白入库后下界仍绿)。清偿 = 五 PNG 生成(build_cube_icons.py:三新 shape 分支与世界内
//     hopperShapeBoxes / brewingStandShapeBoxes / PartialBlockGeometry Cauldron 盒集同构编码镜像 +
//     BLOCKS melon 行 + BLOCKS_FRONT jack_o_lantern 行——t1117 三分支先例同门:表行无分支 → 空画布
//     空白件,表行与分支同批落)+ 五 iconFileForBlock case + 五 CMake 资源行。铁栏杆 / 玻璃板 / 蛋糕 /
//     牌子 / 石砖台阶族等其余 142 条目路由面原样幸存(运行期全调色板 147/147 非空钉承载)。
//   件一(末做) 走路卡顿:**调研件**——用户实测「走几步整画面卡一下约 1 秒后恢复,疑无限世界流式相关;
//     实机 F3 稳态 55fps/18.2ms 帧/0.58ms sim/mesh 69/window 81/100/streams 行全零 ev——尖刺非常态」。
//     疑面清单(a 新环批量 mesh 构建单帧突发 / b SQLite chunk 写阻塞 / c populationWindow 同步窗 /
//     d 驻留集可见性重算风暴 / e 天光重算)逐一定界留待 offscreen rig 数据;本段先钉**裁定锚面**(调研
//     产物行钉):玩家跨 chunk 边界检测触发面(Main.qml _updatePlayerChunk → _refreshChunkVisibility
//     全扫行)+ 流式收割拍面(pumpStreamingTick ④ 数据面 while 排干行 / ⑤ 网格收割 while 排干行——
//     「收割拍单点收口」两排干点)+ 快照采集主线程面(captureChunkMeshSnapshot 调用行 ×2——提交相
//     采集恒主线程,meshworker.h 头注释在案)。尖刺主线程阻塞源若落在机械面(一处批量上限 / 一次同步
//     调用错位)→ 另单修复;若架构面(跨子系统重设计)→ 候选池登记——两结局凭帧曲线数据定,数据入
//     提交消息与本段头注,不入腿(计时断言禁绝对时间钉,机器相关=脆弱;本段腿零计时断言)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;r2011 教训)──────────────────────────────────────────
//   NEG-1 = 摘漏斗放置写入行(playercontroller.cpp 漏斗分支体内 if/else 两写行整枝删除——侧面三元行 +
//     朝下 flag 行;分支头与注释幸存,placeState 落链前初值 0,编译仍绿)→ 恰红 = {r2093a}(六放置
//     state/指向断言失 + 摘面行本腿钉失;r2093b 图标面 / r2093c 钉面豁免——b 不涉放置,c 不钉该两行,
//     翻案锚注行归 c 持有但锚注在注释体 NEG 摘面不触)。
//   NEG-2 = 摘 iconFileForBlock 漏斗 case 单行(hotbar.cpp「case BlockRegistry::Hopper: … return
//     "icon_hopper.png";」整行删除;switch 落 default nullptr = 空串,编译仍绿)→ 恰红 = {r2093b}
//     (漏斗 case 钉失 + 运行期 iconSourceForBlock(Hopper) 空串 + 全调色板 147 非空扫面在漏斗位破;
//     r2093a 放置面 / r2093c 钉面豁免——c 钉其余四 case 行不钉漏斗行,五 CMake 资源行归 b 持有)。
//   (双摘面互不重叠:playercontroller 漏斗分支两行 vs hotbar 漏斗 case 一行;a/b 各由专权腿持有。)

namespace {

// fixed 宿主小世界 incantation(section85 同款四 setter)。
inline void initP0BatchWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section85 同款)。
inline void layP0BatchPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section85 同款:applicationDirPath/../src)。
inline QString srcRootForP0BatchPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 仓库根(applicationDirPath/..——icon PNG 在盘钉用,section80 同款)。
inline QString repoRootForP0BatchPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absolutePath();
}

// 原始读含(翻案锚注 / 调研锚注 / 负面门——pinSet 剥注释会失配,section85 同款)。
inline bool rawContainsP0Batch(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 事件泵(section85 同款:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpP0Batch(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// PNG 非空白计数(alpha>0 像素数;section80 countNonTransparentPixelsIconFix 同款)。
inline int countOpaqueP0Batch(const QImage &icon)
{
    int n = 0;
    for (int yy = 0; yy < icon.height(); ++yy)
        for (int xx = 0; xx < icon.width(); ++xx)
            if (qAlpha(icon.pixel(xx, yy)) > 0)
                ++n;
    return n;
}

} // namespace

void MatrixRun::section86_p0_batch_t1123()
{
    // ── r2093a:漏斗贴面放置朝向柱(NEG-1 敏感面 = 漏斗分支两写行)──────────────────────────────
    //   真链 pc rig 六放置(四水平贴面 + 顶面点 + 底面点):每放置真链 loadSavedState(眼位/yaw/pitch)
    //     → tick(准星射线定 m_hit*) → 事件泵越放置 CD → placeBlock → 落格读回三断言(id/state/
    //     hopperOutDelta 指向)。四水平覆盖编码 {3,2,0,1} 全四向:贴 +Z 面(法线 +Z)→ 嘴 -Z(3)/
    //     贴 -Z 面 → 嘴 +Z(2)/ 贴 -X 面 → 嘴 +X(0)/ 贴 +X 面 → 嘴 -X(1)——嘴指向被点方块(MC 真值,
    //     t1080 外向裁定翻案面);顶面点 / 底面点 → 嘴朝下(bit2,解码 (0,-1,0);底面 MC 嘴恒不朝上
    //     同朝下)。NEG-1 摘面行(两写行)本腿钉。
    runLeg("r2093a hopper placement facing column (a real creative player placing a hopper against"
        " the four side faces of walls answers the spout pointing back into the clicked block in"
        " all four horizontal codes, placing on a top face and under a bottom face both answer the"
        " down flag with the downward spout delta, every placed cell reads back the hopper id with"
        " the spout delta landing exactly on the clicked wall cell for the side placements, and"
        " both placement write rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initP0BatchWorld(w);
        layP0BatchPlatform(w, 4, 28, 4, 28);
        // 贴面墙四堵(双高,y 81..82 落坪上;射线走廊互不 crossing——六放置射线 / 落格两两不相交)。
        w.setBlock(8, 81, 8, BR::Stone, 0);  w.setBlock(8, 82, 8, BR::Stone, 0);   // A 贴 +Z 面
        w.setBlock(16, 81, 12, BR::Stone, 0); w.setBlock(16, 82, 12, BR::Stone, 0); // B 贴 -Z 面
        w.setBlock(20, 81, 16, BR::Stone, 0); w.setBlock(20, 82, 16, BR::Stone, 0); // C 贴 -X 面
        w.setBlock(12, 81, 20, BR::Stone, 0); w.setBlock(12, 82, 20, BR::Stone, 0); // D 贴 +X 面
        w.setBlock(16, 84, 21, BR::Stone, 0);                                       // F 悬空块(点底面)
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Hopper));
        struct FaceChain { const char *tag; float fx, fy, fz, yaw, pitch;
                           int tx, ty, tz, wantState; int dx, dy, dz; };
        // 眼位 = feet+1.62(t1112 首轮教训同门);四水平 pitch 0 / 顶面点 pitch -45 / 底面点 pitch +45。
        const FaceChain chains[6] = {
            { "side+Z",  8.5f,  81.0f, 11.5f,   0.0f,   0.0f,  8, 82,  9, 3,  0,  0, -1 }, // 嘴 -Z → 墙 (8,82,8)
            { "side-Z", 16.5f,  81.0f,  9.5f, 180.0f,   0.0f, 16, 82, 11, 2,  0,  0,  1 }, // 嘴 +Z → 墙 (16,82,12)
            { "side-X", 16.5f,  81.0f, 16.5f, 270.0f,   0.0f, 19, 82, 16, 0,  1,  0,  0 }, // 嘴 +X → 墙 (20,82,16)
            { "side+X", 16.5f,  81.0f, 20.5f,  90.0f,   0.0f, 13, 82, 20, 1, -1,  0,  0 }, // 嘴 -X → 墙 (12,82,20)
            { "top",     8.5f,  81.0f, 22.5f,   0.0f, -45.0f,  8, 81, 20, 4,  0, -1,  0 }, // 顶面点 → 嘴朝下 → 坪 (8,80,20)
            { "bottom", 16.5f,  81.0f, 22.5f,   0.0f,  45.0f, 16, 83, 21, 4,  0, -1,  0 }, // 底面点 → 嘴恒不朝上亦朝下
        };
        for (const FaceChain &c : chains) {
            pc.loadSavedState(double(c.fx), double(c.fy), double(c.fz), double(c.yaw), double(c.pitch),
                              1 /* Creative */);
            pc.tick();
            pumpP0Batch(320); // 越放置 CD(r2083a 同门)
            pc.placeBlock();
            const quint8 st = w.stateAt(c.tx, c.ty, c.tz);
            const bool placed = w.blockAt(c.tx, c.ty, c.tz) == quint8(BR::Hopper)
                && st == quint8(c.wantState);
            int dx = 0, dy = 0, dz = 0;
            BR::hopperOutDelta(st, dx, dy, dz); // 指向断言走解码单一权威(禁自写位运算)
            // 侧面:嘴 delta 恰落被点墙格;顶面点:delta 落被点坪格;底面点:MC 嘴不朝上 → 恒 (0,-1,0)
            //   (被点块在上方,不指向——wantState=4 已锁朝下面)。
            const bool aimed = dx == c.dx && dy == c.dy && dz == c.dz;
            ok = ok && placed && aimed;
            if (!(placed && aimed))
                diag += QStringLiteral("[%1 id=%2 st=%3 d=%4,%5,%6]")
                    .arg(QLatin1String(c.tag)).arg(w.blockAt(c.tx, c.ty, c.tz)).arg(st)
                    .arg(dx).arg(dy).arg(dz);
        }
        // NEG-1 摘面行本腿钉(侧面取反三元行 + 朝下 flag 行——漏斗分支体内两写行,playercontroller.cpp)。
        {
            const QStringList missNeg = pinSet(srcRootForP0BatchPins()
                                               + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("side facing row", "placeState = quint8(m_hitNx > 0 ? 1 : m_hitNx < 0 ? 0 : m_hitNz > 0 ? 3 : 2);", 1),
                SrcPin("down flag row", "placeState = BlockRegistry::HopperFacingDownFlag;", 1)});
            ok = ok && missNeg.isEmpty();
            if (!missNeg.isEmpty())
                diag += QStringLiteral("[neg1 %1]").arg(missNeg.join(QLatin1Char(',')));
        }

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2093a hopper placement facing column (a real creative player placing a hopper"
               " against the four side faces of walls answers the spout pointing back into the"
               " clicked block in all four horizontal codes, placing on a top face and under a"
               " bottom face both answer the down flag with the downward spout delta, every placed"
               " cell reads back the hopper id with the spout delta landing exactly on the clicked"
               " wall cell for the side placements, and both placement write rows are pinned on"
               " file)"
            << (ok ? QString() : diag);
    });

    // ── r2093b:创造背包图标差集清偿柱(NEG-2 敏感面 = iconFileForBlock 漏斗 case 行)──────────────
    //   五 PNG 在盘非空白(alpha 计数>0 ×5)+ 全目录零空白扫面(r2087a 同门下界随五件入库抬升)+
    //   运行期全调色板扫面(creativeBlocks 147 条目 iconSourceForBlock 全非空——差集清偿的完备面:
    //   漏斗 / 酿造台 / 西瓜 / 南瓜灯 / 炼药锅五缺席件自此可解析)+ 五 case 行源钉(NEG-2 摘漏斗行
    //   本腿钉)+ 五 CMake 资源行钉。
    runLeg("r2093b creative palette icon closure column (the five repaired icons answer real non"
        " blank images on disk with per pixel alpha scans, the icon directory sweeps clean of zero"
        " opaque images at a raised lower bound, every entry of the creative palette resolves a"
        " non empty runtime icon source including the five previously invisible hopper brewing"
        " stand melon jack o lantern and cauldron rows, the five icon case rows are pinned on"
        " file, and the five cmake resource rows are pinned)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = repoRootForP0BatchPins();
        // (1) 五 PNG 在盘非空白(逐像素 alpha 计数 >0;清偿资产本体面)。
        const QStringList names = {
            QStringLiteral("icon_hopper.png"),
            QStringLiteral("icon_brewing_stand.png"),
            QStringLiteral("icon_melon.png"),
            QStringLiteral("icon_jack_o_lantern.png"),
            QStringLiteral("icon_cauldron.png"),
        };
        for (const QString &name : names) {
            const QImage icon(root + QStringLiteral("/textures/") + name);
            const int opaque = icon.isNull() ? 0 : countOpaqueP0Batch(icon);
            const bool iconOk = !icon.isNull() && opaque > 0;
            ok = ok && iconOk;
            if (!iconOk)
                diag += QStringLiteral("[%1 null=%2 op=%3]")
                    .arg(name).arg(icon.isNull() ? 1 : 0).arg(opaque);
        }
        // (2) 全目录零空白扫面(section80 同门;下界随五件入库 147→152,取 150 在册下界)。
        {
            const QDir iconDir(root + QStringLiteral("/textures"));
            int swept = 0;
            bool sweepOk = true;
            const QFileInfoList files = iconDir.entryInfoList(
                QStringList { QStringLiteral("icon_*.png") }, QDir::Files);
            for (const QFileInfo &fi : files) {
                const QImage icon(fi.absoluteFilePath());
                const int opaque = icon.isNull() ? 0 : countOpaqueP0Batch(icon);
                ++swept;
                if (opaque == 0) {
                    sweepOk = false;
                    diag += QStringLiteral("[blank %1]").arg(fi.fileName());
                }
            }
            sweepOk = sweepOk && swept >= 150; // 目录面在册下界(152 张实测口径——防目录移位假绿)
            ok = ok && sweepOk;
            if (!sweepOk) diag += QStringLiteral("[sweep n=%1]").arg(swept);
        }
        // (3) 运行期全调色板扫面(差集清偿完备面:creativeBlocks 全条目 × iconSourceForBlock 非空;
        //     五缺席件在 r2092 时代全答空串,清偿后 147/147 可解析——pack 关默认态,section80 同门)。
        {
            Hotbar hb;
            const QVariantList pal = hb.creativeBlocks();
            if (pal.size() != 148) { // t1135 lawful 前移：147→148（活塞调色板行追加——creativeBlocks 段尾 +1，钉值随追加前移）
                ok = false;
                diag += QStringLiteral("[pal size=%1]").arg(pal.size());
            }
            for (int i = 0; i < pal.size(); ++i) {
                const int id = pal.at(i).toInt();
                if (!hb.iconSourceForBlock(id).isEmpty()) continue;
                ok = false;
                diag += QStringLiteral("[empty %1]").arg(id);
            }
            // 五缺席件在册显式钉(差集本体五元——调色板行在册是缺席成因的反面:行一直在,缺的是图标)。
            const QList<int> five = { int(BR::Hopper), int(BR::BrewingStand), int(BR::Melon),
                                      int(BR::JackOLantern), int(BR::Cauldron) };
            for (const int id : five) {
                if (pal.contains(QVariant(id))) continue;
                ok = false;
                diag += QStringLiteral("[absent %1]").arg(id);
            }
        }
        // (4) 五 case 行源钉(NEG-2 摘漏斗行本腿钉;其余四行同行式——清偿面五元全钉在本腿)。
        {
            const QStringList missHb = pinSet(srcRootForP0BatchPins()
                                              + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("hopper case", "return \"icon_hopper.png\";", 1),
                SrcPin("brewing case", "return \"icon_brewing_stand.png\";", 1),
                SrcPin("melon case", "return \"icon_melon.png\";", 1),
                SrcPin("jack case", "return \"icon_jack_o_lantern.png\";", 1),
                SrcPin("cauldron case", "return \"icon_cauldron.png\";", 1)});
            ok = ok && missHb.isEmpty();
            if (!missHb.isEmpty())
                diag += QStringLiteral("[cases %1]").arg(missHb.join(QLatin1Char(',')));
        }
        // (5) 五 CMake 资源行钉(qrc 注册面——行缺则 PNG 在盘不进包,运行期回退链仍空)。
        {
            const QStringList missCm = pinSet(repoRootForP0BatchPins()
                                              + QStringLiteral("/CMakeLists.txt"), {
                SrcPin("cmake hopper", "textures/icon_hopper.png", 1),
                SrcPin("cmake brewing", "textures/icon_brewing_stand.png", 1),
                SrcPin("cmake melon", "textures/icon_melon.png", 1),
                SrcPin("cmake jack", "textures/icon_jack_o_lantern.png", 1),
                SrcPin("cmake cauldron", "textures/icon_cauldron.png", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cmake %1]").arg(missCm.join(QLatin1Char(',')));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2093b creative palette icon closure column (the five repaired icons answer real"
               " non blank images on disk with per pixel alpha scans, the icon directory sweeps"
               " clean of zero opaque images at a raised lower bound, every entry of the creative"
               " palette resolves a non empty runtime icon source including the five previously"
               " invisible hopper brewing stand melon jack o lantern and cauldron rows, the five"
               " icon case rows are pinned on file, and the five cmake resource rows are pinned)"
            << (ok ? QString() : diag);
    });

    // ── r2093c:结构钉族 + 裁定锚柱(NEG 双摘面豁免不钉——漏斗两写行归 a / 漏斗 case 行归 b 专权)────
    //   生成器面(三新 shape 分支行 + T1123 表行/循环行 + BLOCKS melon 行 + BLOCKS_FRONT jack 行 +
    //     hopper 盒 fill 特判行)+ 解码权威幸存钉(hopperOutDelta 朝下行逐字)+ 调色板五行幸存钉 +
    //     翻案锚注自钉(取反裁定注 raw 在场)+ lawful 修订钉(r2083d 同句式钉计数 2→1 行 + 沿革注
    //     raw 在场)+ 牌子分支头幸存钉 + CMake 段行 + 调研锚柱(跨界检测 / 收割拍排干 / 快照采集
    //     三站点锚注)+ 负面门(spec 图标面零 case / 玩法主 QML 零触)。
    runLeg("r2093c structure pin family and ruling anchor column (the icon generator carries the"
        " three new shape branches with their table loop the melon cube row and the jack o lantern"
        " front row and the hopper fill split, the spout delta decoder keeps its down branch"
        " verbatim, the palette rows and the sign placement branch survive, the reversal and the"
        " lawful pin revision anchors hold with the revised single count row, the cmake section row"
        " holds, the investigation anchors pin the boundary detection and the two harvest drain"
        " loops and the main thread snapshot capture sites, and the atlas spec door plus the"
        " gameplay qml door both stay shut)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForP0BatchPins();
        const QString root = repoRootForP0BatchPins();
        // (1) 生成器面(.py hash 注剥离口径;表行/循环行/三分支/melon 行/jack 行/fill 特判行)。
        {
            const QStringList missPy = pinSet(root + QStringLiteral("/tools/build_cube_icons.py"), {
                SrcPin("hopper branch", "elif shape == \"hopper\":", 1),
                SrcPin("brewing branch", "elif shape == \"brewing_stand\":", 1),
                SrcPin("cauldron branch", "elif shape == \"cauldron\":", 1),
                SrcPin("t1123 table", "PARTIALS_3D_T1123 = [", 1),
                SrcPin("t1123 loop", "for out_name, shape, fill_top, fill_side in PARTIALS_3D_T1123:", 1),
                SrcPin("melon block row", "(\"melon\",           \"default_melon_top\", \"default_melon_side\"),", 1),
                SrcPin("jack front row", "(\"jack_o_lantern\", \"default_jackolantern_face\", \"default_pumpkin_top\", \"default_pumpkin_side\"),", 1),
                SrcPin("hopper fill split", "if shape == \"hopper\" and bi > 0:", 1)});
            ok = ok && missPy.isEmpty();
            if (!missPy.isEmpty())
                diag += QStringLiteral("[py %1]").arg(missPy.join(QLatin1Char(',')));
        }
        // (2) 解码权威幸存(朝下分支逐字——只翻写入向,消费面零改动的反面锚)+ 调色板五行幸存 +
        //     牌子放置分支头幸存(同句式钉修订后的牌子侧锚)。
        {
            const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
                SrcPin("down branch verbatim", "if (state & HopperFacingDownFlag) { dx = 0; dy = -1; dz = 0; return; }", 1)});
            const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
                SrcPin("palette hopper", "int(BlockRegistry::Hopper),", 1),
                SrcPin("palette brewing", "int(BlockRegistry::BrewingStand),", 1),
                SrcPin("palette cauldron", "int(BlockRegistry::Cauldron),", 1),
                SrcPin("palette melon", "int(BlockRegistry::Melon),", 1),
                SrcPin("palette jack", "int(BlockRegistry::JackOLantern),", 1)});
            const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
                SrcPin("sign branch head", "} else if (BlockRegistry::isSign(quint8(m_selectedBlock))) {", 1)});
            const bool survOk = missBr.isEmpty() && missHb.isEmpty() && missPc.isEmpty();
            ok = ok && survOk;
            if (!survOk)
                diag += QStringLiteral("[surv %1|%2|%3]").arg(missBr.join(QLatin1Char(',')))
                            .arg(missHb.join(QLatin1Char(','))).arg(missPc.join(QLatin1Char(',')));
        }
        // (3) 翻案锚注自钉(playercontroller 取反裁定注 raw 在场)+ lawful 修订钉(r2083d 同句式钉
        //     计数 2→1 行代码面 + 沿革注 raw 在场)+ era 真值串自钉(本段文件裸文本在案)。
        {
            const bool anchorOk = rawContainsP0Batch(srcDir + QStringLiteral("/Game/playercontroller.cpp"),
                                                     QStringLiteral("t1123 用户实测翻案"))
                && rawContainsP0Batch(root + QStringLiteral("/tools/matrix/section76_roster_sign_t1113.cpp"),
                                      QStringLiteral("t1123 lawful 修订"))
                && pinSet(root + QStringLiteral("/tools/matrix/section76_roster_sign_t1113.cpp"), {
                    SrcPin("revised count row", "? 2 : 3);\", 1),", 1)}).isEmpty()
                && rawContainsP0Batch(root + QStringLiteral("/tools/matrix/section86_p0_batch_t1123.cpp"),
                                      QStringLiteral("the output faces the block it was placed against"));
            ok = ok && anchorOk;
            if (!anchorOk) diag += QStringLiteral("[anchor]");
        }
        // (4) CMake 段行 + 调研锚柱(件一定界三站点:跨 chunk 边界检测行 / 流式收割拍两排干 while 行 /
        //     快照采集主线程调用行——注释体锚,raw 口径)。
        {
            const bool invOk = pinSet(root + QStringLiteral("/CMakeLists.txt"), {
                SrcPin("cmake section86", "tools/matrix/section86_p0_batch_t1123.cpp", 1)}).isEmpty()
                && rawContainsP0Batch(root + QStringLiteral("/src/ui/Main.qml"),
                                      QStringLiteral("_refreshChunkVisibility()"))
                && rawContainsP0Batch(srcDir + QStringLiteral("/Game/gamesession.h"),
                                      QStringLiteral("while (m_streamWorker->takeResultData(data))"))
                && rawContainsP0Batch(srcDir + QStringLiteral("/Game/gamesession.h"),
                                      QStringLiteral("while (m_meshWorker->takeBuilt(built))"))
                && rawContainsP0Batch(srcDir + QStringLiteral("/World/chunkgeometry.cpp"),
                                      QStringLiteral("snap = captureChunkMeshSnapshot(WorldFacade(*m_world), m_cx, m_cz, bake);"));
            ok = ok && invOk;
            if (!invOk) diag += QStringLiteral("[inv]");
        }
        // (5) 负面门:spec 图标面零 case(漏斗 shape 无图集 spec case——qrc PNG 是唯一图标源,t1117
        //     三 shape 负面钉同门核实口径)+ 玩法主 QML 零触(本单零 QML 迁移,新标识符不入 Main.qml)。
        {
            const bool doorOk = !rawContainsP0Batch(srcDir + QStringLiteral("/Core/resourcepackmanager.cpp"),
                                                    QStringLiteral("case BlockRegistry::Hopper"))
                && !rawContainsP0Batch(srcDir + QStringLiteral("/ui/Main.qml"),
                                       QStringLiteral("icon_hopper"))
                && !rawContainsP0Batch(srcDir + QStringLiteral("/ui/Main.qml"),
                                       QStringLiteral("HopperFacingDownFlag"));
            ok = ok && doorOk;
            if (!doorOk) diag += QStringLiteral("[door]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2093c structure pin family and ruling anchor column (the icon generator carries"
               " the three new shape branches with their table loop the melon cube row and the jack"
               " o lantern front row and the hopper fill split, the spout delta decoder keeps its"
               " down branch verbatim, the palette rows and the sign placement branch survive, the"
               " reversal and the lawful pin revision anchors hold with the revised single count"
               " row, the cmake section row holds, the investigation anchors pin the boundary"
               " detection and the two harvest drain loops and the main thread snapshot capture"
               " sites, and the atlas spec door plus the gameplay qml door both stay shut)"
            << (ok ? QString() : diag);
    });
}
