#include "matrix_helpers.h"

#include <QImage>

// t1116 名册后小缺口批探针段(4 腿;filter 词 r2086;矩阵 881→885)。置尾先例沿用(接 section78,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 真链 pc rig / 纯表腿 / 源码钉)。
//
// ── 现状核实裁定表(派工三件逐一全仓 grep 实读)──
//   ① IronBars 图标:全仓 grep icon_iron_bars 零命中(t1112 登记先例存量缺口——铁栏杆自 t998 入册起
//     iconFileForBlock 无 case 无 qrc 稿)→ 真缺。交付 = icon_iron_bars.png 补齐(build_cube_icons.py
//     PARTIALS_3D_T1116 程序生成——iron_bars shape 中心细柱 2/16 见方满格高 + 四向横板 y[7/16,9/16]
//     与 PartialBlockGeometry IronBars case / atlasIconSpecForBlock IronBars 满连盒集同构,十字轮廓
//     最可辨;fill = default_iron_bars 瓦与放置态 tile 183 同源,铁灰调 §9a 原创)+ CMakeLists 资源行
//     + iconFileForBlock case 补行。**零行为变更面**:铁栏杆在 isPackDerivedIconFamily 族(正常路径走
//     程序图集重渲),本 case 是回退链 ①② 皆空的降级兜底层。
//   ② cake 失撑:蛋糕放置预检 = solidSupportBlock 门(t1112 先例,playercontroller.cpp 蛋糕分支实读)
//     而失撑自动破坏面未接线(行注「候选池登记」既录)→ 真缺。交付 = World::checkCakeSupportOnEdit
//     (checkSignSupportOnEdit/checkPaintingSupportOnEdit 同位同序挂点家族:4/5 参 setBlock +
//     setWaterSilent 三写入口收口;popSign 同款写入族 m_chunks 直写无重入 + blockBroken + 批量
//     worldChanged;**零掉落**——不发 blockDroppedAsItem,1.0 蛋糕破坏零掉落口径同门,蛋糕 def 行
//     dropId=0 玩家破块同门零掉落)。与放置预检同谓词零漂移(solidSupportBlock 单一权威)。
//   ③ 地牢箱金苹果行:mineshaftChestPool 现存实读 = **9 条**(派工稿「8 条」系 t1103 瓜种行前置的
//     旧口径——loottable.cpp 池逐行点数为准),权重和 117。交付 = 金苹果行段尾追加(权重 1 / 恒
//     1 件——1.0.0 地牢箱金苹果口径原值:11 条目之 1 行 / 权重 1 / 1 件,追加不插中间);t484
//     「矿物族替代」面解除(loottable.h 沿革注勘正,上段「不取」裁定关闭);**生苹果仍不入池**
//     (1.0 地牢箱池无生苹果行,「RecipeRegistry::AppleId」字面零命中面锁死)。既有 9 行逐字幸存
//     (melon 行在案 r2073a 钉不动),权重和 117→118(roll 分母动态求和无硬编码)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;摘调用点非摘守卫体 t1051 教训应用)──
//   NEG-1 = 摘 hotbar.cpp iconFileForBlock 的 iron bars case 单行(case 行整行删除,编译仍绿——
//     纯 return 语句无被引用符号;同函数 isPackDerivedIconFamily 的 IronBars 入族行不在摘面)→
//     恰红 = {r2086a}(case 行源钉归零 = 腿级 FAIL;CMake 资源行钉 / 图标文件非空钉 /
//     iconSourceForBlock 非空钉[家族图集路径恒答]不在摘面 = 幸存)。**豁免设计:r2086b/c/d 均不含
//     该 case 行任何形态的源钉**(t1115 摘面行豁免不钉同门)。
//   NEG-2 = 摘 world.cpp 4 参数 setBlock 的 checkCakeSupportOnEdit 挂点行(整行删除,编译仍绿——
//     5 参版与 setWaterSilent 挂点行仍在,函数无未引用告警)→ 恰红 = {r2086b}(4 参入口破支撑子柱:
//     摘行后蛋糕恒幸存 = 腿级 FAIL;5 参/静默写子柱幸存但腿以合取归红;放置预检同谓词面不在摘面)。
//     **豁免设计:r2086d 不钉 4 参挂点行**——pinSet 剥注释后 4/5 参挂点行同文「checkCakeSupportOnEdit
//     (x, y, z, oldId, id);」,minCount=2 形态在 NEG-2 下必红(归因污染),故 d 只钉 setWaterSilent
//     挂点行(lightOldId 变元名唯一)+ decl 行 + impl 头行,4 参挂点行为 NEG-2 专权摘面(t1115
//     NEG 摘面行豁免不钉同门)。
namespace {

// fixed 宿主小世界 incantation(section69..78 同款四 setter)。
inline void initSmallGapsWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(86);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section78 同款)。
inline void laySmallGapsPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 真链 pc rig 的 placeBlock 前置泵(r2067c/section75 同门:m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗)。
inline void pumpSmallGaps(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section75..78 同款)。
inline QString srcRootForSmallGapsPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚/负面面——pinSet 剥注释会失配,section75..78 同款)。
inline bool rawContainsSmallGaps(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

} // namespace

void MatrixRun::section79_small_gaps_t1116()
{
    // ── r2086a:铁栏杆图标柱(NEG-1 敏感面 = iconFileForBlock case 行)─────────────────────────
    //   case 行源钉(NEG-1 摘行 → 归零 = FAIL)+ CMake 资源行钉 + 图标文件在盘非空钉(QImage 逐像素
    //   alpha 扫描——调色板行图标「空缺观感」缺口的本体面)+ iconSourceForBlock 运行期非空钉(回退链
    //   末层=本 case 的 URL 面)+ 调色板行在册钉。
    runLeg("r2086a iron bars icon column (the icon file for block switch answers the iron"
        " bars png row, the cmake resource row carries the icon on file, the baked icon"
        " answers a real non blank image, the runtime icon source for the iron bars"
        " palette row stays resolvable, and the palette carries the iron bars entry)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) case 行源钉(NEG-1 摘行 → 零命中 = 恰红)。
        const QString srcDir = srcRootForSmallGapsPins();
        const QStringList missHb = pinSet(srcDir + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("icon case row", "return \"icon_iron_bars.png\";", 1)});
        ok = ok && missHb.isEmpty();
        if (!missHb.isEmpty())
            diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
        // (2) CMake 资源行钉(qrc 注册面)。
        const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                          + QStringLiteral("/../CMakeLists.txt"), {
            SrcPin("cmake icon row", "textures/icon_iron_bars.png", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // (3) 图标文件在盘非空钉(零 alpha = t1112 三图标「空缺观感」缺口本体面;铁栏杆稿必真图)。
        const QString iconPath = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("textures/icon_iron_bars.png"));
        const QImage icon(iconPath);
        bool iconOk = !icon.isNull() && icon.width() > 0;
        int opaque = 0;
        for (int yy = 0; iconOk && yy < icon.height(); ++yy)
            for (int xx = 0; xx < icon.width(); ++xx)
                if (qAlpha(icon.pixel(xx, yy)) > 0) { ++opaque; break; }
        iconOk = iconOk && opaque > 0;
        ok = ok && iconOk;
        if (!iconOk) diag += QStringLiteral("[icon null=%1 opaque=%2]")
            .arg(icon.isNull()).arg(opaque);
        // (4) 运行期非空钉:iconSourceForBlock 回退链对铁栏杆恒答非空(pack 关态家族图集路径②;
        //     链路末层 = 本 case 的 qrc URL 面③——两态任一答非空即调色板行可解析)。
        // (5) 调色板行在册钉。
        {
            Hotbar hb;
            const bool runtimeOk = !hb.iconSourceForBlock(int(BR::IronBars)).isEmpty();
            ok = ok && runtimeOk;
            if (!runtimeOk) diag += QStringLiteral("[src]");
            const bool palOk = hb.creativeBlocks().contains(QVariant(int(BR::IronBars)));
            ok = ok && palOk;
            if (!palOk) diag += QStringLiteral("[pal]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2086a iron bars icon column (the icon file for block switch answers the"
               " iron bars png row, the cmake resource row carries the icon on file, the"
               " baked icon answers a real non blank image, the runtime icon source for"
               " the iron bars palette row stays resolvable, and the palette carries the"
               " iron bars entry)"
            << (ok ? QString() : diag);
    });

    // ── r2086b:蛋糕失撑柱(NEG-2 敏感面 = 4 参 setBlock 挂点行)───────────────────────────────
    //   三写入口收口行为柱:4 参 setBlock(支撑置换非完整立方 → 蛋糕当场破块零掉落 + 完整立方置换
    //   幸存面)/ 5 参 setBlock(破支撑 → 破块)/ setWaterSilent(非空内容置换 → 破块)+ 同谓词零漂移
    //   (真链 pc rig:蛋糕放置预检半砖顶恒拒 + 石坪顶可放)+ blockBroken 带 Cake id + state 清零。
    runLeg("r2086b cake support column (replacing the support below a cake with a partial"
        " block through the four argument write pops the cake in place with zero drops"
        " and the block broken signal carrying the cake id while a full cube replacement"
        " keeps the cake, the five argument write and the fluid silent write pop the same"
        " way, and the real controller placement gate refuses a cake over a slab top"
        " while answering a cake over the stone floor with the same support predicate)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 4 参 setBlock 入口(NEG-2 敏感面:摘挂点行 → 半砖置换后蛋糕恒幸存 = FAIL)。
        {
            World w;
            initSmallGapsWorld(w);
            laySmallGapsPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Cake, 0);
            int cakeBroken = 0, drops = 0;
            QObject::connect(&w, &World::blockBroken, &w,
                             [&](int, int, int, int id) { if (id == int(BR::Cake)) ++cakeBroken; });
            QObject::connect(&w, &World::blockDroppedAsItem, &w,
                             [&](int, int, int, int) { ++drops; });
            // 幸存面:支撑置换为另一完整立方(Dirt) → 蛋糕保留零信号。
            w.setBlock(24, 80, 24, BR::Dirt);
            const bool surviveOk = w.blockAt(24, 81, 24) == quint8(BR::Cake) && cakeBroken == 0;
            // 破块面:支撑置换为半砖(非 solidSupportBlock) → 当场破块 + blockBroken 带 Cake id
            //     + **零掉落**(无 blockDroppedAsItem)+ state 清零。
            w.setBlock(24, 80, 24, BR::StoneSlab);
            const bool popOk = w.blockAt(24, 81, 24) == quint8(BR::Air)
                && w.stateAt(24, 81, 24) == quint8(0)
                && cakeBroken == 1 && drops == 0;
            const bool entry4Ok = surviveOk && popOk;
            ok = ok && entry4Ok;
            if (!entry4Ok) diag += QStringLiteral("[e4 sv=%1 pop=%2 brk=%3 dr=%4]")
                .arg(surviveOk).arg(popOk).arg(cakeBroken).arg(drops);
        }
        // (2) 5 参 setBlock 入口(破支撑 → 破块;挂点行同族收口——NEG-2 摘 4 参行后本柱幸存)。
        {
            World w;
            initSmallGapsWorld(w);
            laySmallGapsPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Cake, 0);
            int drops = 0;
            QObject::connect(&w, &World::blockDroppedAsItem, &w,
                             [&](int, int, int, int) { ++drops; });
            w.setBlock(24, 80, 24, BR::Air, 0);
            const bool entry5Ok = w.blockAt(24, 81, 24) == quint8(BR::Air) && drops == 0;
            ok = ok && entry5Ok;
            if (!entry5Ok) diag += QStringLiteral("[e5 b=%1 dr=%2]")
                .arg(w.blockAt(24, 81, 24)).arg(drops);
        }
        // (3) setWaterSilent 入口(非空内容置换 → 破块;焚毁/蒸发/流体静默写路径同收口)。
        {
            World w;
            initSmallGapsWorld(w);
            laySmallGapsPlatform(w, 14, 44, 4, 44);
            w.setBlock(24, 81, 24, BR::Cake, 0);
            int drops = 0;
            QObject::connect(&w, &World::blockDroppedAsItem, &w,
                             [&](int, int, int, int) { ++drops; });
            w.setWaterSilent(24, 80, 24, BR::Water, 0);
            const bool entrySilentOk = w.blockAt(24, 81, 24) == quint8(BR::Air) && drops == 0;
            ok = ok && entrySilentOk;
            if (!entrySilentOk) diag += QStringLiteral("[es b=%1 dr=%2]")
                .arg(w.blockAt(24, 81, 24)).arg(drops);
        }
        // (4) 同谓词零漂移(真链 pc rig):蛋糕放置预检 solidSupportBlock 门——半砖顶恒拒(不挥
        //     不放)/ 石坪顶可放(同一谓词正例)。
        {
            World w;
            initSmallGapsWorld(w);
            laySmallGapsPlatform(w, 14, 44, 4, 44);
            w.setBlock(30, 80, 30, BR::StoneSlab, 0); // 半砖对照点(顶面 y 80.5)
            EntityManager ents;
            Hotbar hb;
            PlayerController pc;
            pc.setWorld(&w);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            QQuickWindow probeWin;
            pc.setParentItem(probeWin.contentItem());
            pc.grab();
            pc.setSelectedBlock(int(BR::Cake));
            // 拒放面:半砖顶正上方悬停俯视 → 目标格 (30,81,30) 下方 = 半砖(非支撑) → 拒。
            pc.loadSavedState(30.5, 83.0, 30.5, 0.0, -90.0, 1 /* Creative */);
            pc.tick();
            pumpSmallGaps(320);
            pc.placeBlock();
            const bool refuseOk = w.blockAt(30, 81, 30) == quint8(BR::Air);
            // 可放面:石坪顶正上方俯视 → 目标格 (24,81,24) 下方 = Stone(支撑) → 放置成功。
            pumpSmallGaps(320);
            pc.loadSavedState(24.5, 83.0, 24.5, 0.0, -90.0, 1 /* Creative */);
            pc.tick();
            pumpSmallGaps(320);
            pc.placeBlock();
            const bool placeOk = w.blockAt(24, 81, 24) == quint8(BR::Cake);
            const bool gateOk = refuseOk && placeOk;
            ok = ok && gateOk;
            if (!gateOk) diag += QStringLiteral("[gate rf=%1 pl=%2]")
                .arg(refuseOk).arg(placeOk);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2086b cake support column (replacing the support below a cake with a"
               " partial block through the four argument write pops the cake in place with"
               " zero drops and the block broken signal carrying the cake id while a full"
               " cube replacement keeps the cake, the five argument write and the fluid"
               " silent write pop the same way, and the real controller placement gate"
               " refuses a cake over a slab top while answering a cake over the stone"
               " floor with the same support predicate)"
            << (ok ? QString() : diag);
    });

    // ── r2086c:地牢箱柱(金苹果行 + 既有行权重面回归 + roll 行为面)────────────────────────────
    //   金苹果行逐字段(权重 1 / 恒 1 件)+ 尾位钉(段尾追加不插中间)+ 既有 9 行逐条幸存(权重/
    //   数量区间逐位)+ 总权重面 118(roll 分母动态求和)+ 千 seed roll 行为面(命中 + 同 seed 复现
    //   + 恒 1 件 + 宽频带 [20,120] λ=8000/118≈67.8)+ 瓜种族面带回归([450,950] 在 118 分母下
    //   仍容纳 λ≈678)+ 矿物族幸存面 + kMineshaftRolls 原值。
    runLeg("r2086c dungeon chest column (the mineshaft chest pool answers the golden apple"
        " row at weight one with a single item riding the pool tail, the nine prior rows"
        " keep their exact weights and count ranges with the mineral family surviving and"
        " the total weight face answering one hundred eighteen, and the deterministic"
        " roll sweep hits the golden apple at exactly one item per hit inside the wide"
        " band while the melon seed band keeps its answer under the new denominator)",
        [&]() {
        bool ok = true;
        QString diag;
        const auto &pool = LootTable::mineshaftChestPool();
        // (1) 金苹果行逐字段 + 尾位钉(段尾追加)。
        const LootTable::Entry *ga = nullptr;
        for (const auto &e : pool)
            if (e.itemId == int(RecipeRegistry::GoldenAppleId)) ga = &e;
        const bool gaOk = ga && ga->weight == 1 && ga->minCount == 1 && ga->maxCount == 1
            && !pool.empty()
            && pool.back().itemId == int(RecipeRegistry::GoldenAppleId); // 追加不插中间
        ok = ok && gaOk;
        if (!gaOk) diag += QStringLiteral("[ga w=%1]")
            .arg(ga ? ga->weight : -1);
        // (2) 既有 9 行逐条幸存(权重/数量区间逐位)+ 总权重面 118 + 矿物族幸存面。
        struct Booked { int id, weight, lo, hi; };
        const Booked booked[9] = {
            { int(RecipeRegistry::CoalId),         30, 1, 6 },
            { int(RecipeRegistry::RedstoneId),     25, 1, 5 },
            { int(RecipeRegistry::IronIngotId),    20, 1, 4 },
            { int(RecipeRegistry::GoldIngotId),    12, 1, 3 },
            { int(RecipeRegistry::LapisId),        10, 1, 3 },
            { int(RecipeRegistry::DiamondId),       5, 1, 1 },
            { int(RecipeRegistry::EnchantedBookId), 3, 1, 1 },
            { int(RecipeRegistry::MusicDiscEchoId), 2, 1, 1 },
            { int(RecipeRegistry::MelonSeedsId),   10, 2, 4 },
        };
        bool rowsOk = pool.size() == 10;
        for (const auto &b : booked) {
            const LootTable::Entry *hit = nullptr;
            for (const auto &e : pool)
                if (e.itemId == b.id) hit = &e;
            if (!hit || hit->weight != b.weight || hit->minCount != b.lo || hit->maxCount != b.hi)
                rowsOk = false;
        }
        // 总权重面 = 全池求和(含金苹果行 1) == 118——roll 分母动态求和面(无硬编码分母)。
        int totalWeight = 0;
        for (const auto &e : pool)
            if (e.weight > 0) totalWeight += e.weight; // roll 同式(仅正权重入分母)
        rowsOk = rowsOk && totalWeight == 118;
        ok = ok && rowsOk;
        if (!rowsOk) diag += QStringLiteral("[rows n=%1 w=%2]")
            .arg(pool.size()).arg(totalWeight);
        // (3) 千 seed roll 行为面:金苹果命中 + 同 seed 复现 + 恒 1 件 + 宽频带(λ=8000×1/118≈67.8,
        //     [20,120] 双向拒错);瓜种面带回归([450,950];118 分母下 λ≈678 仍带内)+ 数量区间 2..4。
        bool goldenHit = false, reproOk = false;
        int goldenStacks = 0, melonStacks = 0;
        bool melonRangeOk = true, goldenCountOk = true;
        for (quint32 s = 0; s < 1000; ++s) {
            const auto stacks = LootTable::roll(LootTable::mineshaftChestPool(), 8, s);
            bool hitGa = false;
            for (const auto &st : stacks) {
                if (st.itemId == int(RecipeRegistry::GoldenAppleId)) {
                    hitGa = true;
                    ++goldenStacks;
                    if (st.count != 1) goldenCountOk = false; // 恒 1 件
                }
                if (st.itemId == int(RecipeRegistry::MelonSeedsId)) {
                    ++melonStacks;
                    if (st.count < 2 || st.count > 4) melonRangeOk = false;
                }
            }
            if (hitGa && !goldenHit) {
                goldenHit = true;
                const auto again = LootTable::roll(LootTable::mineshaftChestPool(), 8, s);
                for (const auto &st : again)
                    if (st.itemId == int(RecipeRegistry::GoldenAppleId)) { reproOk = true; break; }
            }
        }
        const bool rollOk = goldenHit && reproOk && goldenCountOk
            && goldenStacks >= 20 && goldenStacks <= 120
            && melonStacks >= 450 && melonStacks <= 950
            && melonRangeOk;
        ok = ok && rollOk;
        if (!rollOk) diag += QStringLiteral("[roll hit=%1 rep=%2 g=%3 m=%4 rng=%5 cnt=%6]")
            .arg(goldenHit).arg(reproOk).arg(goldenStacks).arg(melonStacks)
            .arg(melonRangeOk).arg(goldenCountOk);
        // (4) 抽取次数原值 + 生苹果零命中面(1.0 地牢箱池无生苹果行——值面双证)。
        bool plainAppleRow = false;
        for (const auto &e : pool)
            if (e.itemId == int(RecipeRegistry::AppleId)) plainAppleRow = true;
        const bool tailOk = LootTable::kMineshaftRolls == 6 && !plainAppleRow;
        ok = ok && tailOk;
        if (!tailOk) diag += QStringLiteral("[tail rolls=%1 apple=%2]")
            .arg(LootTable::kMineshaftRolls).arg(plainAppleRow);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2086c dungeon chest column (the mineshaft chest pool answers the golden"
               " apple row at weight one with a single item riding the pool tail, the nine"
               " prior rows keep their exact weights and count ranges with the mineral"
               " family surviving and the total weight face answering one hundred"
               " eighteen, and the deterministic roll sweep hits the golden apple at"
               " exactly one item per hit inside the wide band while the melon seed band"
               " keeps its answer under the new denominator)"
            << (ok ? QString() : diag);
    });

    // ── r2086d:结构钉族(NEG 双摘面全豁免 = 值面 + 源钉对照腿 + 负面钉)────────────────────────
    //   值面(方块段零新 id/图集零变更/苹果双 id 原值/roll 次数原值)+ 源钉族(world.h decl + world.cpp
    //   impl 头 + setWaterSilent 挂点行[lightOldId 变元名唯一,NEG-2 摘 4 参行不触达] + loottable
    //   金苹果行 + loottable.h 沿革锚 + CMake 段行)+ 双负面钉(苹果消亡掉落仍零[world.cpp 零
    //   AppleId,t305 既录面幸存]/ 无 r2086 新方块 id[值面 Count/图集/尾 id 原值即锁])。
    runLeg("r2086d structure pin family (the block segment and atlas and apple ids and"
        " the roll count keep their original values, the cake hook declaration and"
        " implementation and the fluid silent attach row and the golden apple loot row"
        " and the lineage anchor and the cmake section row are pinned on file, and the"
        " natural decay path stays apple free on file)", [&]() {
        bool ok = true;
        QString diag;
        // (D1) 值面:方块段/图集/苹果双 id/roll 次数原值(无 r2086 新方块 id 即此锁)。
        const bool enumOk = int(BR::Count) == 166 && int(BR::AtlasTileCount) == 213 // t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）；t1136 前移：Atlas 212→213（活塞伸出态瓦追加——钉值随追加前移）
            && int(BR::IronBars) == 142 && int(BR::Cake) == 159
            && int(BR::StandingSign) == 160 && int(BR::WallSign) == 161
            && int(BR::ShapeCake) == 16
            && int(RecipeRegistry::AppleId) == 0x292
            && int(RecipeRegistry::GoldenAppleId) == 0x293
            && LootTable::kMineshaftRolls == 6;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");
        // (D2) 源钉族(NEG-1 摘面 case 行与 NEG-2 摘面 4 参挂点行均豁免不钉——恰红归因全权在 a/b)。
        {
            const QString srcDir = srcRootForSmallGapsPins();
            const QStringList missWh = pinSet(srcDir + QStringLiteral("/World/world.h"), {
                SrcPin("cake hook decl", "void checkCakeSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id);", 1)});
            ok = ok && missWh.isEmpty();
            if (!missWh.isEmpty())
                diag += QStringLiteral("[wh %1]").arg(missWh.join(QLatin1Char(',')));
            const QStringList missWc = pinSet(srcDir + QStringLiteral("/World/world.cpp"), {
                SrcPin("cake hook impl head", "void World::checkCakeSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id)", 1),
                SrcPin("silent attach row", "checkCakeSupportOnEdit(x, y, z, lightOldId, id);", 1)});
            ok = ok && missWc.isEmpty();
            if (!missWc.isEmpty())
                diag += QStringLiteral("[wc %1]").arg(missWc.join(QLatin1Char(',')));
            const QStringList missLt = pinSet(srcDir + QStringLiteral("/Game/loottable.cpp"), {
                SrcPin("golden apple pool row", "{ RecipeRegistry::GoldenAppleId,  1, 1, 1 },", 1)});
            ok = ok && missLt.isEmpty();
            if (!missLt.isEmpty())
                diag += QStringLiteral("[lt %1]").arg(missLt.join(QLatin1Char(',')));
            // loottable.h 沿革锚(注释体锚走 raw 含——pinSet 剥注释失配,section78 同门)。
            const bool lineageOk = rawContainsSmallGaps(srcDir + QStringLiteral("/Game/loottable.h"),
                                                        QStringLiteral("t1116"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
            const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"), {
                SrcPin("cmake section79", "tools/matrix/section79_small_gaps_t1116.cpp", 1)});
            ok = ok && missCm.isEmpty();
            if (!missCm.isEmpty())
                diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        }
        // (D3) 负面钉:苹果消亡路径掉落仍零(world.cpp 零 AppleId——t1116 零触碰面,t305 既录)。
        {
            const QString srcDir = srcRootForSmallGapsPins();
            const bool decayClean = !rawContainsSmallGaps(srcDir + QStringLiteral("/World/world.cpp"),
                                                          QStringLiteral("AppleId"));
            ok = ok && decayClean;
            if (!decayClean) diag += QStringLiteral("[decay]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2086d structure pin family (the block segment and atlas and apple ids"
               " and the roll count keep their original values, the cake hook declaration"
               " and implementation and the fluid silent attach row and the golden apple"
               " loot row and the lineage anchor and the cmake section row are pinned on"
               " file, and the natural decay path stays apple free on file)"
            << (ok ? QString() : diag);
    });
}
