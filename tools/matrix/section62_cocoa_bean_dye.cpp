#include "matrix_helpers.h"

#include <QFile> // 结构钉 rawRead / pinSet（helpers 已含枢纽；显式列出示信钉面）

// t1092 棕色染料生存源——可可豆探针段（4 腿；filter 词 r2062；矩阵 782→786）。
// 置尾先例沿用（接 section61，runAll 末执行，rig 世界零接触——纯表腿 + 源码钉，零世界）。
//
// 任务契约：棕染料（DyeBrownId 0x257，t788 染料段）是 16 色染料生存可达的「最后一环」（t1078 关单
//   登记「棕色机制源 = 可可豆本工程无 → 15/16」）。本单交付可可豆（CocoaBeanId 0x262，材料段尾追加）
//   的生存源 = 地牢箱战利品 + 1:1 转换棕染料行 → 16/16 全色生存可达。**零方块 / 零图集 / 零数值 id
//   改动**（物品非方块；MaterialIcon 自绘走 QML 位面非图集瓦片；Core 层零触碰）。
//
// 纪元裁定（核实留痕，t1086/t1087 降级先例同门）：①可可豆**地牢战利品面** = Beta 1.2（2011-01，随
//   16 色染料同批）起地牢箱既有行，早于 Java 1.0.0 正式版（2011-11-18）→ **1.0 基准内**（wiki Chest
//   loot 历史：1.0.0 地牢池 cocoa beans 权重 10 / 数量 1..2——loottable.cpp 行按原值落表，权重和
//   143→153）；②丛林可可荚（种植方块）= 12w19a / Java 1.3.1（2012-08）丛林机制 → **非 1.0 机制如实
//   降级留池**（陶瓦 / 染色玻璃 / 丛林混入面同款先例）——荚方块不做。合成面 = 1:1 转换行（机制等价
//   MC 1.0「可可豆即棕染料」dye 351 damage 3 同物异名；本工程两 id 分立以配方桥接，t1078 墨囊同门）。
//
// 腿面：r2062a 地牢池 roll 行为柱（t1083 战利品腿同门：确定性 seed 扫描命中可可豆 + 同 seed 复现 +
//   命中数量区间 [1,2] 逐次 + 千 seed 权重面宽频带防权重塌零）；r2062b 转换行为柱（2×2 单放 / 3×3
//   中心单放命中 1:1 产棕 + 行名身份钉 + 错误原料负例[可可+染料 / 双可可 / 可可+羊毛全 nullptr 无效应
//   不消耗] + 两跳链端到端可可→棕染料→棕羊毛）；r2062c 16 色生存可达闭包钉（基源 = 四花 + 仙人掌绿 +
//   墨囊 + **可可豆[以池内在为门]**，shapeless 不动点扫全表 → 16/16 全可达逐位——t1078 登记的「最后一
//   环」兑现验证）；r2062d 结构钉（CocoaBeanId 段位源钉[0x262 + 段尾追加序] + dungeonChestPool 行逐
//   字段 + 池行数/权重和注释同步钉 + 合成行源钉与名去重 + maxStack 64 双层面[Hotbar/Core] + 名/调色
//   板/图标三呈现面 + 荚面降级登记注 + 零方块/零图集反探[blockregistry/resourcepackmanager 全无 cocoa]）。
//
// 相邻族零污染（分层论证，d 腿不再复钉 r2051 本体）：r2051a 闭包的基源集冻结在 t1078 交付时点（四花 +
//   仙人掌绿 + 墨囊，**不含可可**，亦不扫战利品池）→ 其「15/16 + 棕唯一不可达」断言在自身模型内恒真、
//   本单零改动零扰动（NEG-2 也不误伤）；t1092 的 16/16 闭包由本段 r2062c 以「含战利品基源」的上位模型
//   交付——两腿语义分层（t1050/t1052 源钉同变更修订先例的反向面：本单无需修订任何旧钉）。t1083（r2054）
//   战利品族：盘行未动、盘区连续同列未动（可可豆排盘区**尾**）→ 全绿。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2062_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘池行（loottable.cpp 摘可可豆行 + 池头注 10 条/153 还原为 9 条/143）→ **恰红 = {r2062a,
//     r2062c, r2062d}**（a 千 seed 零命中；c 基源失可可 → 闭包 15/16；d 池行字段/行数/权重和子面）。
//     b 不误伤（转换行在）；r2051 族不误伤（基源冻结）；r2054 族不误伤（盘行未动）。
//   NEG-2 摘合成行（recipe.cpp 摘可可豆→棕染料行）→ **恰红 = {r2062b, r2062c, r2062d}**（b 转换
//     未命中 nullptr 面；c 棕无生产者 → 15/16；d 合成行源钉子面）。a 不误伤（池行在）；
//     r2051a 不误伤（同上分层论证）；r2054 族不误伤。
void MatrixRun::section62_cocoa_bean_dye()
{
    // ── r2062a：地牢池 roll 行为柱（确定性 seed 命中 + 复现 + 数量区间 + 千 seed 宽频带）────────
    runLeg("r2062a dungeon loot roll behavior column (a deterministic seed sweep of the dungeon"
        " chest pool yields cocoa beans with reproducible same-seed results, every cocoa stack"
        " count stays within [1,2], and the thousand-seed hit total stays inside the weight"
        " band [old-gate red payload: removing the pool row leaves zero cocoa hits])", [&]() {
        bool ok = true;
        QString diag;
        // (1) 确定性 seed 扫描 0..999：恰有一 seed 命中可可豆（t1083 b7 同门）；同 seed 复现
        //     （PLAN §2-K 坐标确定性战利品面的表级抽查）。统计千 seed 命中 stack 总数。
        bool seedHit = false, reproOk = true, rangeOk = true;
        int cocoaStacks = 0;
        for (quint32 s = 0; s < 1000; ++s) {
            const auto stacks = LootTable::roll(LootTable::dungeonChestPool(), 8, s);
            bool hit = false;
            for (const auto &st : stacks) {
                if (st.itemId == RecipeRegistry::CocoaBeanId) {
                    hit = true;
                    ++cocoaStacks;
                    if (st.count < 1 || st.count > 2) rangeOk = false; // MC 1.0 原行数量区间 [1,2]
                }
            }
            if (hit && !seedHit) {
                const auto again = LootTable::roll(LootTable::dungeonChestPool(), 8, s);
                for (const auto &st : again)
                    if (st.itemId == RecipeRegistry::CocoaBeanId) { seedHit = true; break; }
            }
        }
        // (2) 千 seed 宽频带（确定性复算，非统计带）：权重 10/153 ≈ 6.5%/roll × 8000 roll ≈ 523
        //     期望；带 [300,800] 防权重塌零（NEG-1 → 0）与权重爆涨两向漂移。
        const bool bandOk = cocoaStacks >= 300 && cocoaStacks <= 800;
        ok = ok && seedHit && reproOk && rangeOk && bandOk;
        if (!ok)
            diag += QStringLiteral("[hit=%1 range=%2 stacks=%3]").arg(seedHit).arg(rangeOk)
                        .arg(cocoaStacks);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2062a dungeon loot roll behavior column (a deterministic seed sweep of the"
               " dungeon chest pool yields cocoa beans with reproducible same-seed results,"
               " every cocoa stack count stays within [1,2], and the thousand-seed hit total"
               " stays inside the weight band [old-gate red payload: removing the pool row"
               " leaves zero cocoa hits])"
            << (ok ? QString() : diag);
    });

    // ── r2062b：转换行为柱（1:1 产棕 + 行名身份 + 错误原料负例 + 两跳链端到端）────────────────
    runLeg("r2062b cocoa-to-brown-dye conversion behavior column (single-ingredient shapeless"
        " hit in both 2x2 and 3x3 grids yields exactly one brown dye under the unique row"
        " identity, wrong-ingredient combinations all return null with no consumption, and the"
        " two-hop chain cocoa bean -> brown dye -> brown wool lands end to end)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 2×2 单放 / 3×3 中心单放：恰 DyeBrownId ×1、shapeless、行名唯一身份（墨囊转黑同构）。
        const int g2[9] = { RecipeRegistry::CocoaBeanId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r2 = RecipeRegistry::match(g2, 2);
        const bool hit2 = r2 && r2->outputId == RecipeRegistry::DyeBrownId
            && r2->outputCount == 1 && r2->shapeless
            && QLatin1String(r2->name) == QLatin1String("dye_brown_from_cocoa");
        const int g3[9] = { 0, 0, 0, 0, RecipeRegistry::CocoaBeanId, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *r3 = RecipeRegistry::match(g3, 3);
        const bool hit3 = r3 && r3->outputId == RecipeRegistry::DyeBrownId
            && r3->outputCount == 1;
        ok = ok && hit2 && hit3;
        if (!(hit2 && hit3))
            diag += QStringLiteral("[hit 2x2=%1 3x3=%2]").arg(hit2).arg(hit3);
        // (2) 错误原料负例（合成 UI 以 match()==nullptr 为「无效应不消耗」权威面）：可可豆不是染料、
        //     不参与混色 / 直染 / 双放——转换行只吃单放。
        const auto expectNoMatch2 = [&](int a, int b, const char *why) {
            const int g[9] = { a, b, 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 2);
            if (r) diag += QStringLiteral("[%1 matched %2]").arg(QLatin1String(why)).arg(r->outputId);
            return r == nullptr;
        };
        const bool neg1 = expectNoMatch2(RecipeRegistry::CocoaBeanId, RecipeRegistry::DyeWhiteId, "cocoa+white");
        const bool neg2 = expectNoMatch2(RecipeRegistry::CocoaBeanId, int(BR::Wool), "cocoa+wool");
        const bool neg3 = expectNoMatch2(RecipeRegistry::CocoaBeanId, RecipeRegistry::CocoaBeanId, "cocoa+cocoa");
        const bool neg4 = expectNoMatch2(RecipeRegistry::CocoaBeanId, RecipeRegistry::InkSacId, "cocoa+ink");
        ok = ok && neg1 && neg2 && neg3 && neg4;
        if (!(neg1 && neg2 && neg3 && neg4)) diag += QStringLiteral("[neg]");
        // (3) 两跳链端到端：可可豆 → 棕染料 →（t788 直染行）+ 白羊毛 → 棕羊毛（生存链终点逐位）。
        const int gc[9] = { RecipeRegistry::DyeBrownId, int(BR::Wool), 0, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gc, 2);
        const bool chainOk = rc && rc->outputId == int(BR::WoolBrown) && rc->outputCount == 1;
        ok = ok && chainOk;
        if (!chainOk)
            diag += QStringLiteral("[chain out=%1 want=%2]").arg(rc ? rc->outputId : -1)
                        .arg(int(BR::WoolBrown));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2062b cocoa-to-brown-dye conversion behavior column (single-ingredient"
               " shapeless hit in both 2x2 and 3x3 grids yields exactly one brown dye under"
               " the unique row identity, wrong-ingredient combinations all return null with"
               " no consumption, and the two-hop chain cocoa bean -> brown dye -> brown wool"
               " lands end to end)"
            << (ok ? QString() : diag);
    });

    // ── r2062c：16 色生存可达闭包钉（基源含战利品门控可可豆 → shapeless 不动点 16/16）──────────
    runLeg("r2062c sixteen-color survival closure pin (seeding the fixed-point scan with the"
        " survival base sources [four flower dyes, cactus green, ink sac] plus the cocoa bean"
        " gated on its dungeon pool presence reaches all sixteen dye colors through the"
        " shapeless recipe table [old-gate red payloads: no pool row or no conversion row"
        " strands brown at fifteen of sixteen, closing the gap t1078 registered])", [&]() {
        bool ok = true;
        QString diag;
        // 染料常量表（行序 = 羊毛 16 色标准序，r2051a 同源；棕色 idx=12）。
        const int dyeIds[16] = {
            RecipeRegistry::DyeWhiteId, RecipeRegistry::DyeOrangeId, RecipeRegistry::DyeMagentaId,
            RecipeRegistry::DyeLightBlueId, RecipeRegistry::DyeYellowId, RecipeRegistry::DyeLimeId,
            RecipeRegistry::DyePinkId, RecipeRegistry::DyeGrayId, RecipeRegistry::DyeLightGrayId,
            RecipeRegistry::DyeCyanId, RecipeRegistry::DyePurpleId, RecipeRegistry::DyeBlueId,
            RecipeRegistry::DyeBrownId, RecipeRegistry::DyeGreenId, RecipeRegistry::DyeRedId,
            RecipeRegistry::DyeBlackId,
        };
        // 基源 = 生存「无配方即可得」：四花 + 仙人掌绿 + 墨囊（杀鱿鱼 / 钓鱼垃圾）+ 可可豆
        //     （**以地牢池内在为门**——战利品面即其生存可达性本体；NEG-1 摘池行 → 门闭 → 15/16）。
        QSet<int> reach;
        reach << RecipeRegistry::DyeWhiteId << RecipeRegistry::DyeYellowId
              << RecipeRegistry::DyeRedId << RecipeRegistry::DyeBlueId
              << RecipeRegistry::DyeGreenId << RecipeRegistry::InkSacId;
        bool lootSource = false;
        for (const auto &e : LootTable::dungeonChestPool())
            if (e.itemId == RecipeRegistry::CocoaBeanId) lootSource = true;
        if (lootSource) reach.insert(RecipeRegistry::CocoaBeanId);
        // shapeless 不动点：凡配方全部非空原料 ∈ 可达集 → 产物入可达集（r2051a 同算法，基源上位）。
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
        int reachableCount = 0;
        for (int i = 0; i < 16; ++i)
            if (reach.contains(dyeIds[i])) ++reachableCount;
        const bool closureOk = reachableCount == 16 && reach.contains(RecipeRegistry::DyeBrownId);
        ok = ok && closureOk;
        if (!closureOk)
            diag += QStringLiteral("[closure %1/16 loot=%2]").arg(reachableCount).arg(lootSource);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2062c sixteen-color survival closure pin (seeding the fixed-point scan with"
               " the survival base sources [four flower dyes, cactus green, ink sac] plus the"
               " cocoa bean gated on its dungeon pool presence reaches all sixteen dye colors"
               " through the shapeless recipe table [old-gate red payloads: no pool row or no"
               " conversion row strands brown at fifteen of sixteen, closing the gap t1078"
               " registered])"
            << (ok ? QString() : diag);
    });

    // ── r2062d：结构钉（段位源钉 + 池行逐字段 + 合成行源钉 + maxStack 双层面 + 三呈现面 + 降级注 + 反探）──
    runLeg("r2062d structure pins (the cocoa bean id sits at the material-section tail 0x262"
        " after the music discs, the dungeon pool row keeps its exact weight-10 count-1..2"
        " fields as the last row with the ten-row 153-weight-sum comment in sync, the"
        " conversion row stays uniquely registered, max stack reads 64 on both the Hotbar and"
        " Core faces, name palette tail and icon case all resolve, the pod-face downgrade"
        " registration note remains, and the zero-block zero-atlas counter-probe holds"
        " [no cocoa anywhere in blockregistry or resourcepackmanager])", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString recH = srcRoot + QStringLiteral("/Game/recipe.h");
        const QString recCpp = srcRoot + QStringLiteral("/Game/recipe.cpp");
        const QString iconQml = srcRoot + QStringLiteral("/ui/MaterialIcon.qml");
        const QString brCpp = srcRoot + QStringLiteral("/Core/blockregistry.cpp");
        const QString brH = srcRoot + QStringLiteral("/Core/blockregistry.h");
        const QString rpmCpp = srcRoot + QStringLiteral("/Core/resourcepackmanager.cpp");
        QString recHTxt, recCppTxt, iconTxt, brCppTxt, brHTxt, rpmTxt;
        const auto rawRead = [&](const QString &path, QString &out) {
            QFile f(path);
            out = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            return !out.isEmpty();
        };
        const bool reads = rawRead(recH, recHTxt) && rawRead(recCpp, recCppTxt)
            && rawRead(iconQml, iconTxt) && rawRead(brCpp, brCppTxt) && rawRead(brH, brHTxt)
            && rawRead(rpmCpp, rpmTxt);
        ok = ok && reads;
        if (!reads) diag += QStringLiteral("[reads] ");
        // (1) 数值段位钉：CocoaBeanId == 0x262（运行期）+ 源序段尾追加（音乐盘行之后、恰一次定义）。
        const bool idPin = RecipeRegistry::CocoaBeanId == 0x262;
        const int discIdx = recHTxt.indexOf(QLatin1String("static constexpr int MusicDiscNightId"));
        const int cocoaIdx = recHTxt.indexOf(QLatin1String("static constexpr int CocoaBeanId"));
        const bool tailPin = discIdx >= 0 && cocoaIdx > discIdx
            && recHTxt.count(QLatin1String("static constexpr int CocoaBeanId")) == 1;
        ok = ok && idPin && tailPin;
        if (!(idPin && tailPin))
            diag += QStringLiteral("[id id=%1 tail=%2]").arg(idPin).arg(tailPin);
        // (2) dungeonChestPool 行逐字段（运行期遍历 = t1083 b6 同门）+ 行数 10 + 权重和 153 + 段尾行序
        //     （注释登记面同步钉——注释宣称 10 条/153 与表本体互钉防漂移）。
        const auto &pool = LootTable::dungeonChestPool();
        bool fieldOk = false;
        int cocoaRow = -1;
        int weightSum = 0;
        for (int i = 0; i < int(pool.size()); ++i) {
            weightSum += pool[size_t(i)].weight;
            if (pool[size_t(i)].itemId == RecipeRegistry::CocoaBeanId) {
                cocoaRow = i;
                fieldOk = pool[size_t(i)].weight == 10 && pool[size_t(i)].minCount == 1
                    && pool[size_t(i)].maxCount == 2;
            }
        }
        const bool poolOk = fieldOk && cocoaRow == int(pool.size()) - 1
            && pool.size() == size_t(10) && weightSum == 153;
        ok = ok && poolOk;
        if (!poolOk)
            diag += QStringLiteral("[pool row=%1 field=%2 n=%3 sum=%4]").arg(cocoaRow)
                        .arg(fieldOk).arg(pool.size()).arg(weightSum);
        // (3) 转换行源钉（pinSet 剥注释口径；行名 / 单原料 pattern 行裸字串恰一次）。
        const QStringList missRec = pinSet(recCpp, {
            SrcPin("cocoa row", "\"dye_brown_from_cocoa\" }", 1),
            SrcPin("cocoa row pattern", "RecipeRegistry::CocoaBeanId, 0, 0, 0, 0, 0, 0, 0, 0 },", 1)});
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty()) diag += QStringLiteral("[recipe %1] ").arg(missRec.join(QLatin1Char(',')));
        const int nameCnt = recCppTxt.count(QLatin1String("\"dye_brown_from_cocoa\""));
        ok = ok && nameCnt == 1; // 名去重恰一次（裸读计数；头注不带引号形态不计入）
        if (nameCnt != 1) diag += QStringLiteral("[dup name=x%1]").arg(nameCnt);
        // (4) maxStack 64 双层面：Hotbar（背包权威）+ Core BlockRegistry::maxStackSize（掉落物合并用，
        //     材料段默认 64 零特判——双默认即零 Core 改动的核实面）。
        Hotbar hb;
        const int hbCap = hb.maxStackSize(RecipeRegistry::CocoaBeanId);
        const int coreCap = BR::maxStackSize(RecipeRegistry::CocoaBeanId);
        const bool capOk = hbCap == 64 && coreCap == 64;
        ok = ok && capOk;
        if (!capOk) diag += QStringLiteral("[cap hb=%1 core=%2]").arg(hbCap).arg(coreCap);
        // (5) 三呈现面：名（nameForBlock）+ 创造调色板（盘区尾四连——盘三连先例 r2054b 不扰动）+
        //     MaterialIcon 自绘（case 0x262 恰一次 + drawCocoaBean 定义恰一次）。
        const bool nameOk = hb.nameForBlock(RecipeRegistry::CocoaBeanId) == QStringLiteral("可可豆");
        const QList<QVariant> mats = hb.creativeMaterials();
        int pos0 = -1;
        for (int i = 0; i < mats.size(); ++i)
            if (mats[i].toInt() == RecipeRegistry::MusicDiscAmberId) { pos0 = i; break; }
        const bool paletteOk = pos0 >= 0 && pos0 + 3 < mats.size()
            && mats[pos0 + 1].toInt() == RecipeRegistry::MusicDiscEchoId
            && mats[pos0 + 2].toInt() == RecipeRegistry::MusicDiscNightId
            && mats[pos0 + 3].toInt() == RecipeRegistry::CocoaBeanId;
        const QStringList missQml = pinSet(iconQml, {
            SrcPin("icon case", "case 0x262: drawCocoaBean(); break", 1),
            SrcPin("icon draw fn", "const drawCocoaBean = () => {", 1)});
        const bool iconOk = missQml.isEmpty();
        ok = ok && nameOk && paletteOk && iconOk;
        if (!(nameOk && paletteOk && iconOk))
            diag += QStringLiteral("[face name=%1 pal=%2 icon=%3]").arg(nameOk).arg(paletteOk).arg(iconOk);
        // (6) 荚面降级登记注（注释语义 → 裸读口径，r2051d(4) 同门）：纪元裁定留痕在 recipe.h 头注。
        //     中文 needle 禁用 QLatin1String（按 Latin-1 逐字节解释 → 与 fromUtf8 解码文本恒不等）。
        const bool eraNote = recHTxt.contains(QStringLiteral("非 1.0 机制如实降级留池"))
            && recHTxt.contains(QStringLiteral("t1092 可可豆"));
        ok = ok && eraNote;
        if (!eraNote) diag += QStringLiteral("[era] ");
        // (7) 零方块 / 零图集反探：blockregistry（Core 方块注册表）与 resourcepackmanager（物品 pack
        //     映射）全树无 cocoa 词元 / 无 0x262 字面量 → 物品走 MaterialIcon 自绘回退（mcMaterialId
        //     越表界 -1），零方块注册 / 零 pack 映射 / 零图集瓦片变更。
        const bool zeroProbe = !brCppTxt.contains(QLatin1String("Cocoa"))
            && !brCppTxt.contains(QStringLiteral("可可"))
            && !brHTxt.contains(QLatin1String("Cocoa"))
            && !brHTxt.contains(QStringLiteral("可可"))
            && !rpmTxt.contains(QLatin1String("0x262"))
            && !rpmTxt.contains(QLatin1String("cocoa"), Qt::CaseInsensitive);
        ok = ok && zeroProbe;
        if (!zeroProbe) diag += QStringLiteral("[zero-probe] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2062d structure pins (the cocoa bean id sits at the material-section tail"
               " 0x262 after the music discs, the dungeon pool row keeps its exact weight-10"
               " count-1..2 fields as the last row with the ten-row 153-weight-sum comment in"
               " sync, the conversion row stays uniquely registered, max stack reads 64 on"
               " both the Hotbar and Core faces, name palette tail and icon case all resolve,"
               " the pod-face downgrade registration note remains, and the zero-block"
               " zero-atlas counter-probe holds [no cocoa anywhere in blockregistry or"
               " resourcepackmanager])"
            << (ok ? QString() : diag);
    });
}
