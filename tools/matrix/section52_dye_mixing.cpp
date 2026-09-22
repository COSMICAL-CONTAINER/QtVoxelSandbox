#include "matrix_helpers.h"

#include <QDirIterator> // QML 零触碰反探：src/ui/*.qml 全扫（helpers 已含枢纽；显式列出示信反探面）
#include <QFile>        // pinSet / rawRead 源码钉（helpers 已含枢纽；显式列出示信钉面）

// t1078 染料系统缺口（二级混色染料 9 条 + 墨囊→黑染料 1:1 转换，共 10 条 shapeless 2×2 配方）
//   （4 腿；filter 词 r2051；矩阵 734→738）。置尾先例（接 section51，runAll 末执行，rig 世界零接触）。
//
// 现场核实（候选失真即降级，t1070/t1077 先例第三现）：候选「花→染料→羊毛/陶瓦/玻璃染色」三段中——
//   ① 花→染料已交付（t788：四花 dropId→红/黄/蓝/白 + 仙人掌烧绿 smelting.cpp）；
//   ② 染料→羊毛已交付（t788：染料+白羊毛→色羊毛 16 条 + 染料+白床→色床 16 条，全 shapeless 2×2；
//   染羊右键链 t832 dyeSheep 单点，playercontroller 染料分流唯一）；③ **陶瓦/玻璃染色如实降级**：
//   陶瓦族本体不存在（src/ 全树 terracotta 零命中）且机制面非 MC 1.0（硬化粘土 1.6 / 染色玻璃 1.7.2）
//   → 不为本单新开方块族，候选池登记（勿为单开大块）。**有效缺口 = 染料获取链生存可达性**：t788 起
//   16 色染料仅 5 色有生存源（四花 + 仙人掌绿），9 个二级色只可创造取用；墨囊（t399 杀鱿鱼 / 钓鱼垃圾）
//   对染料链是死端（recipe.h 注「染料原料预留」从未接线）。本单交付 = 9 条二级混色（2 染料→2 染料，
//   机制等价 MC 1.0 二级染料合成；三料配方落选——只收「2→2」一种形态少一类匹配歧义）+ 墨囊→黑染料
//   1:1 转换（MC 1.0 墨囊即黑染料，本工程两 id 分立以配方桥接）→ 生存链闭合 15/16 色（棕色机制源 =
//   可可豆，本工程无可可豆 → 维持仅创造，候选池登记）。染色权威链收口 = 既有合成路（禁第二份右键
//   分发——羊毛方块右键染色非 MC 1.0 机制，不做）。
//
// 腿面：r2051a 染料获取承重墙（四花 dropId / 仙人掌烧绿自含复核 + 墨囊转换 2×2 / 3×3 双格命中 +
//   全表不动点闭包[6 生存源 → 15/16 色可达逐位、棕色永不可达、黑必经墨囊转换]）；
//   r2051b 混色行为柱（9 条 2→2 产率/序无关摆位逐位 + 混色→羊毛两跳链 9 色端到端[新可达色经 t788
//   直染配方落羊毛变体]）；r2051c 边界与覆盖墙（未知对/同色对/墨囊不替代混料/骨粉非白染料替身/
//   三料落选形态/棕色终末无消费 → 全 nullptr 无效应不消耗口径）；r2051d 结构钉（10 配方行注册族
//   在旁 + 名去重恰一次 + 染料右键分流单点反探 + QML 零触碰反探 + t1078 头注锚点在场）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_t1078_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘混色语义本体（recipe.cpp 染橙行产物 DyeOrangeId→DyePinkId 变异）→ 声明红面 {r2051a, r2051b}
//     （闭包橙不可达 reach=14 + 行为柱橙产出断言红）。r2051c 不误伤（负例面与橙产物 id 无关——橙+白
//     本就非配方，变异不改任何 nullptr 断言）；r2051d 不误伤（行名/行数/分流/QML 反探钉不动）。
//   NEG-2 摘墨囊转换语义本体（recipe.cpp 墨囊行产物 DyeBlackId→DyeGrayId 变异）→ 声明红面 {r2051a}
//     （闭包黑不可达[基源无黑，转换行变异后黑永无生产者 → reach=14] + 转换产物断言红）。r2051b 不误伤
//     （9 条混色行未动，{黑,白}→灰 产出的是灰配方本体非转换行）；r2051c 不误伤（{InkSac, White} 本就
//     nullptr，变异不改负例面）；r2051d 不误伤（行名/行数/分流/QML 反探钉不动）。
//   rig 纪律：纯表腿（RecipeRegistry::match / recipeAt 静态查询，t788 探针同门）+ 源码钉，零世界接触。
void MatrixRun::section52_dye_mixing()
{
    // ── r2051a：染料获取承重墙（源自含复核 + 墨囊转换 + 全表不动点闭包）─────────────────────
    runLeg("r2051a dye acquisition wall (ink conversion + recipe-table reachability closure)", [&]() {
        bool ok = true;
        // (0) 染料常量表（行序 = 羊毛 16 色标准序，t788 探针同源；棕色 idx=12）。
        const int dyeIds[16] = {
            RecipeRegistry::DyeWhiteId, RecipeRegistry::DyeOrangeId, RecipeRegistry::DyeMagentaId,
            RecipeRegistry::DyeLightBlueId, RecipeRegistry::DyeYellowId, RecipeRegistry::DyeLimeId,
            RecipeRegistry::DyePinkId, RecipeRegistry::DyeGrayId, RecipeRegistry::DyeLightGrayId,
            RecipeRegistry::DyeCyanId, RecipeRegistry::DyePurpleId, RecipeRegistry::DyeBlueId,
            RecipeRegistry::DyeBrownId, RecipeRegistry::DyeGreenId, RecipeRegistry::DyeRedId,
            RecipeRegistry::DyeBlackId,
        };
        // (1) 原色源自含复核（跨层契约 recipe.cpp static_assert 钉死，此处运行期直读双保险）。
        const bool flowerOk = BR::dropId(BR::FlowerRed) == RecipeRegistry::DyeRedId
            && BR::dropId(BR::FlowerYellow) == RecipeRegistry::DyeYellowId
            && BR::dropId(BR::FlowerBlue) == RecipeRegistry::DyeBlueId
            && BR::dropId(BR::FlowerWhite) == RecipeRegistry::DyeWhiteId;
        const bool cactusOk = SmeltingRegistry::smeltResult(int(BR::Cactus)) == RecipeRegistry::DyeGreenId;
        ok = ok && flowerOk && cactusOk;
        // (2) 墨囊 → 黑染料：2×2 单放 / 3×3 中心单放均命中（单原料 shapeless 位置无关，t1077 骨块拆解
        //     同构），产出恰 DyeBlackId ×1（1:1 桥接非 1:多——MC 语义两者同物，防物品通胀）。
        const int gInk2[4] = { RecipeRegistry::InkSacId, 0, 0, 0 };
        const RecipeRegistry::Recipe *ink2 = RecipeRegistry::match(gInk2, 2);
        const bool ink2Ok = ink2 && ink2->outputId == RecipeRegistry::DyeBlackId
            && ink2->outputCount == 1 && ink2->shapeless;
        const int gInk3[9] = { 0, 0, 0, 0, RecipeRegistry::InkSacId, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *ink3 = RecipeRegistry::match(gInk3, 3);
        const bool ink3Ok = ink3 && ink3->outputId == RecipeRegistry::DyeBlackId
            && ink3->outputCount == 1;
        ok = ok && ink2Ok && ink3Ok;
        // (3) 全表不动点闭包：生存「无配方即可得」基源 = 四花染料 + 仙人掌绿 + 墨囊（杀鱿鱼 / 钓鱼垃圾）；
        //     反复扫全表，凡 shapeless 配方全部非空原料 ∈ 可达集 → 产物入可达集，直至不动点。
        //     断言：15/16 色可达 + 唯一不可达 = 棕（idx 12）+ 黑必经墨囊转换（基源无黑 → 转换配方是
        //     闭包的必经半边，NEG-2 的敏感面）。
        QSet<int> reach;
        reach << RecipeRegistry::DyeWhiteId << RecipeRegistry::DyeYellowId
              << RecipeRegistry::DyeRedId << RecipeRegistry::DyeBlueId
              << RecipeRegistry::DyeGreenId << RecipeRegistry::InkSacId;
        for (bool grew = true; grew;) {
            grew = false;
            const int total = RecipeRegistry::recipeCount();
            for (int i = 0; i < total; ++i) {
                const RecipeRegistry::Recipe *r = RecipeRegistry::recipeAt(i);
                if (!r || !r->shapeless || reach.contains(r->outputId)) continue;
                bool allIn = true;
                for (int c = 0; c < 9 && allIn; ++c)
                    if (r->pattern[c] != 0 && !reach.contains(r->pattern[c])) allIn = false;
                if (allIn) { reach.insert(r->outputId); grew = true; }
            }
        }
        int reachableCount = 0, unreachableIdx = -1;
        for (int i = 0; i < 16; ++i) {
            if (reach.contains(dyeIds[i])) ++reachableCount;
            else unreachableIdx = i;
        }
        const bool closureOk = reachableCount == 15 && unreachableIdx == 12
            && reach.contains(RecipeRegistry::DyeBlackId)
            && !reach.contains(RecipeRegistry::DyeBrownId);
        ok = ok && closureOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2051a dye acquisition wall (ink conversion + recipe-table reachability closure)"
            << (ok ? QString()
                    : QString("diag flowerOk=%1 cactusOk=%2 ink2=%3 ink3=%4 reach=%5 badIdx=%6")
                        .arg(flowerOk).arg(cactusOk).arg(ink2Ok).arg(ink3Ok)
                        .arg(reachableCount).arg(unreachableIdx));
    });
    // ── r2051b：混色行为柱（9 条 2→2 产率 / 序无关 + 混色→羊毛两跳链端到端逐位）────────────
    runLeg("r2051b dye mixing behavior column (9 mixes 2->2 yield, order-free, two-hop wool chain)", [&]() {
        bool ok = true;
        // 混色表：{原料 a, 原料 b} → 产物染料 → 该染料经 t788 直染配方落的羊毛变体（两跳链端点）。
        //   映射 = MC 1.0 二级染料配方的机制等价重排（rose red+dandelion yellow→orange 等，原创 id）。
        struct MixDef { int a, b, out, woolOut; };
        const MixDef mixes[9] = {
            { RecipeRegistry::DyeRedId,    RecipeRegistry::DyeYellowId, RecipeRegistry::DyeOrangeId,    int(BR::WoolOrange) },
            { RecipeRegistry::DyeBlueId,   RecipeRegistry::DyeWhiteId,  RecipeRegistry::DyeLightBlueId, int(BR::WoolLightBlue) },
            { RecipeRegistry::DyePurpleId, RecipeRegistry::DyePinkId,   RecipeRegistry::DyeMagentaId,   int(BR::WoolMagenta) },
            { RecipeRegistry::DyeGreenId,  RecipeRegistry::DyeWhiteId,  RecipeRegistry::DyeLimeId,      int(BR::WoolLime) },
            { RecipeRegistry::DyeRedId,    RecipeRegistry::DyeWhiteId,  RecipeRegistry::DyePinkId,      int(BR::WoolPink) },
            { RecipeRegistry::DyeBlackId,  RecipeRegistry::DyeWhiteId,  RecipeRegistry::DyeGrayId,      int(BR::WoolGray) },
            { RecipeRegistry::DyeGrayId,   RecipeRegistry::DyeWhiteId,  RecipeRegistry::DyeLightGrayId, int(BR::WoolLightGray) },
            { RecipeRegistry::DyeGreenId,  RecipeRegistry::DyeBlueId,   RecipeRegistry::DyeCyanId,      int(BR::WoolCyan) },
            { RecipeRegistry::DyeRedId,    RecipeRegistry::DyeBlueId,   RecipeRegistry::DyePurpleId,    int(BR::WoolPurple) },
        };
        int mixOk = 0, swapOk = 0, chainOk = 0;
        QString diag;
        for (const MixDef &m : mixes) {
            // (1) 正摆：2×2 顶行 {a,b} → 恰产物 ×2（2 染料入 2 染料出，MC 二级染料产率同构）。
            const int g[9] = { m.a, m.b, 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 2);
            const bool hit = r && r->outputId == m.out && r->outputCount == 2 && r->shapeless
                && r->gridSize == RecipeRegistry::Inventory2x2;
            if (hit) ++mixOk;
            else diag += QStringLiteral("[mix %1->%2 got %3]").arg(m.a).arg(m.out).arg(r ? r->outputId : -1);
            // (2) 换位摆：{b,a} 斜对角（2×2 输入格 stride 2 → 合法下标 0..3：1=右上 / 3=右下，
            //     t788 床换位同门）→ 同配方（无序位置无关）。
            const int gs[9] = { 0, m.b, 0, m.a, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *rs = RecipeRegistry::match(gs, 2);
            const bool hitSwap = rs && rs->outputId == m.out && rs->outputCount == 2;
            if (hitSwap) ++swapOk;
            else diag += QStringLiteral("[swap %1 got %2]").arg(m.out).arg(rs ? rs->outputId : -1);
            // (3) 两跳链端到端：混出的染料 + 白羊毛（Wool=27）→ 对应色羊毛变体（生存链终点逐位）。
            const int gc[9] = { m.out, int(BR::Wool), 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gc, 2);
            const bool hitChain = rc && rc->outputId == m.woolOut && rc->outputCount == 1;
            if (hitChain) ++chainOk;
            else diag += QStringLiteral("[chain %1 got %2 want %3]").arg(m.out)
                             .arg(rc ? rc->outputId : -1).arg(m.woolOut);
        }
        // (4) 品红二级二级双跳原料自证：紫 / 粉两原料本身都是混色产物（闭包深度 ≥2 的唯一色，
        //     r2051a 闭包的明细面复钉——链不是平的）。
        const int gP[9] = { RecipeRegistry::DyeRedId, RecipeRegistry::DyeBlueId, 0, 0, 0, 0, 0, 0, 0 };
        const int gK[9] = { RecipeRegistry::DyeRedId, RecipeRegistry::DyeWhiteId, 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rp = RecipeRegistry::match(gP, 2);
        const RecipeRegistry::Recipe *rk = RecipeRegistry::match(gK, 2);
        const bool magentaDeep = rp && rk
            && rp->outputId == RecipeRegistry::DyePurpleId
            && rk->outputId == RecipeRegistry::DyePinkId;
        ok = ok && mixOk == 9 && swapOk == 9 && chainOk == 9 && magentaDeep;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2051b dye mixing behavior column (9 mixes 2->2 yield, order-free, two-hop wool chain)"
            << (ok ? QString() : QString("diag mix=%1/9 swap=%2/9 chain=%3/9 deep=%4 %5")
                        .arg(mixOk).arg(swapOk).arg(chainOk).arg(magentaDeep).arg(diag));
    });
    // ── r2051c：边界与覆盖墙（非配方组合全 nullptr = 无效应不消耗权威面）──────────────────
    runLeg("r2051c dye boundary wall (unknown pairs, substitutes, declined shapes -> no match)", [&]() {
        bool ok = true;
        QString diag;
        // 合成 UI 以 match()==nullptr 为「不产出不消耗」权威面 → 每个负例即一条「无效应不消耗」断言。
        const auto expectNoMatch2 = [&](int a, int b, const char *why) {
            const int g[9] = { a, b, 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 2);
            if (r) diag += QStringLiteral("[%1 matched %2]").arg(QLatin1String(why)).arg(r->outputId);
            return r == nullptr;
        };
        // (1) 未知对：非配方染料组合不产任何产物（防等价表误扩 / 防未来误并配方吃进）。
        ok = ok && expectNoMatch2(RecipeRegistry::DyeRedId,   RecipeRegistry::DyeOrangeId, "red+orange");
        ok = ok && expectNoMatch2(RecipeRegistry::DyeLimeId,  RecipeRegistry::DyePinkId,   "lime+pink");
        ok = ok && expectNoMatch2(RecipeRegistry::DyeWhiteId, RecipeRegistry::DyeOrangeId, "white+orange");
        // (2) 同色对：双份同染料无配方。
        ok = ok && expectNoMatch2(RecipeRegistry::DyeRedId, RecipeRegistry::DyeRedId, "red+red");
        // (3) 棕色终末：棕不作为混色原料（无可可豆源 → 也不允许棕参与合成扩链）。
        ok = ok && expectNoMatch2(RecipeRegistry::DyeBrownId, RecipeRegistry::DyeWhiteId, "brown+white");
        // (4) 墨囊不替代混料：混色原料只认黑染料（DyeBlackId），墨囊只走 1:1 转换（两 id 分工钉死）。
        ok = ok && expectNoMatch2(RecipeRegistry::InkSacId, RecipeRegistry::DyeWhiteId, "ink+white");
        ok = ok && expectNoMatch2(RecipeRegistry::InkSacId, RecipeRegistry::DyeGreenId,  "ink+green");
        // (5) 骨粉非白染料替身：混色 whitening 只认白染料（BonemealId 是催熟道具，非染料段）。
        ok = ok && expectNoMatch2(RecipeRegistry::BonemealId, RecipeRegistry::DyeGreenId, "bonemeal+green");
        ok = ok && expectNoMatch2(RecipeRegistry::BonemealId, RecipeRegistry::DyeRedId,   "bonemeal+red");
        ok = ok && expectNoMatch2(RecipeRegistry::DyeWhiteId, RecipeRegistry::BonemealId, "white+bonemeal");
        // (6) 三料落选形态：MC 三料混色（墨囊+2 白→浅灰）落选 → 满格 2×2 三料 / 3×2 布置无匹配。
        {
            const int g3[9] = { RecipeRegistry::InkSacId, RecipeRegistry::DyeWhiteId,
                                RecipeRegistry::DyeWhiteId, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *r3 = RecipeRegistry::match(g3, 2);
            if (r3) diag += QStringLiteral("[ink+2white matched %1]").arg(r3->outputId);
            ok = ok && r3 == nullptr;
        }
        // (7) 转换配方负例：墨囊 + 任意伴料不命中转换（单原料配方只吃单放——防满格误扩）。
        ok = ok && expectNoMatch2(RecipeRegistry::InkSacId, RecipeRegistry::InkSacId, "ink+ink");
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2051c dye boundary wall (unknown pairs, substitutes, declined shapes -> no match)"
            << (ok ? QString() : diag);
    });
    // ── r2051d：结构钉（注册族 + 名去重恰一次 + 分流单点反探 + QML 零触碰反探）────────────
    runLeg("r2051d structure pins (registration family, name dedup, single dispatch, QML zero-touch)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString recCpp = srcRoot + QStringLiteral("/Game/recipe.cpp");
        const QString recH = srcRoot + QStringLiteral("/Game/recipe.h");
        const QString pcCpp = srcRoot + QStringLiteral("/Game/playercontroller.cpp");
        QString recCppTxt, recHTxt, pcTxt;
        const auto rawRead = [&](const QString &path, QString &out) {
            QFile f(path);
            out = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            return !out.isEmpty();
        };
        const bool reads = rawRead(recCpp, recCppTxt) && rawRead(recH, recHTxt) && rawRead(pcCpp, pcTxt);
        ok = ok && reads;
        if (!reads) diag += QStringLiteral("[reads] ");
        // (1) 注册族钉：10 条新配方行在旁（pinSet 剥注释口径；行名裸字串只在代码出现）。
        const QStringList missRec = pinSet(recCpp, {
            SrcPin("mix orange",  "\"dye_mix_orange\" }", 1),
            SrcPin("mix l.blue",  "\"dye_mix_light_blue\" }", 1),
            SrcPin("mix magenta", "\"dye_mix_magenta\" }", 1),
            SrcPin("mix lime",    "\"dye_mix_lime\" }", 1),
            SrcPin("mix pink",    "\"dye_mix_pink\" }", 1),
            SrcPin("mix gray",    "\"dye_mix_gray\" }", 1),
            SrcPin("mix l.gray",  "\"dye_mix_light_gray\" }", 1),
            SrcPin("mix cyan",    "\"dye_mix_cyan\" }", 1),
            SrcPin("mix purple",  "\"dye_mix_purple\" }", 1),
            SrcPin("ink->black",  "\"dye_black_from_ink\" }", 1),
            SrcPin("ink row pattern", "RecipeRegistry::InkSacId, 0, 0, 0, 0, 0, 0, 0, 0 },", 1)});
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty()) diag += QStringLiteral("[recipe %1] ").arg(missRec.join(QLatin1Char(',')));
        // (2) 名去重恰一次（本表名无唯一性强制 → 裸读计数钉；头注/行注释不带引号形态不计入）。
        const char *names[10] = {
            "\"dye_mix_orange\"", "\"dye_mix_light_blue\"",
            "\"dye_mix_magenta\"", "\"dye_mix_lime\"",
            "\"dye_mix_pink\"", "\"dye_mix_gray\"",
            "\"dye_mix_light_gray\"", "\"dye_mix_cyan\"",
            "\"dye_mix_purple\"", "\"dye_black_from_ink\"" };
        for (const char *nm : names) {
            const int cnt = recCppTxt.count(QString::fromUtf8(nm));
            if (cnt != 1) { ok = false; diag += QStringLiteral("[dup %1=x%2] ").arg(QLatin1String(nm)).arg(cnt); }
        }
        // (3) 染料右键分流单点反探（禁第二份分发——羊毛方块右键染色非 MC 1.0 机制）：playercontroller
        //     染料段谓词恰一处（染羊分支）；羊染后 takeStack 消耗点仍唯一。
        const QStringList missPc = pinSet(pcCpp, {
            SrcPin("dye dispatch predicate",
                   "heldItemId >= RecipeRegistry::DyeIdBase && heldItemId <= RecipeRegistry::DyeBlackId", 1),
            SrcPin("dyeSheep call site", "m_entityManager->dyeSheep(", 1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty()) diag += QStringLiteral("[pc %1] ").arg(missPc.join(QLatin1Char(',')));
        // (4) t1078 头注锚点在场（recipe.h / recipe.cpp 两处范围注——注释语义 → 裸读口径）。
        const bool anchorOk = recHTxt.contains(QLatin1String("t1078"))
            && recCppTxt.contains(QLatin1String("t1078"));
        ok = ok && anchorOk;
        if (!anchorOk) diag += QStringLiteral("[anchor] ");
        // (5) QML 零触碰反探：src/ui/*.qml 全树无混色配方名 / 染料段常量引用（交互与获取链收口 Game 层；
        //     MaterialIcon drawDye 是 t788 既有图标路径、Main.qml 0x22D 注释是 t399 既有鱿鱼链——均不含
        //     本批词元，反探不误伤）。
        int qmlHit = 0;
        QDirIterator it(srcRoot + QStringLiteral("/ui"), QStringList() << QStringLiteral("*.qml"),
            QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            const QString txt = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            if (txt.contains(QLatin1String("dye_mix_")) || txt.contains(QLatin1String("DyeIdBase")))
                ++qmlHit;
        }
        ok = ok && qmlHit == 0;
        if (qmlHit != 0) diag += QStringLiteral("[qml touched x%1] ").arg(qmlHit);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2051d structure pins (registration family, name dedup, single dispatch, QML zero-touch)"
            << (ok ? QString() : diag);
    });
}
