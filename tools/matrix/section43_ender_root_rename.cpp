#include "matrix_helpers.h"

#include <QDirIterator>
#include <QRegularExpression>

// t1068 §9 Ender 族改名收口探针段（4 腿 r2042a-d；filter 词 "r2042"；矩阵 699→699+N）。
// 置尾先例沿用（接 section42，runAll 末执行）。任务契约（dev-plan t1068；t1067 登记后续批）：
// src/ 树全量盘点的 Ender 词根族（EnderEye/EnderPearl 驼峰族 + endereye/enderpearl 扁平族 +
// entity_endereye 资产名）按 t1068 映射表改名，行为语义逐位不变——只改名不改行为；数值 id 零
// 变化 = 存档安全铁律（物品 EnderPearlId→AbyssPearlId 0x243 / EndEyeId 0x23A 沿 t1067 不动 /
// 实体 Kind AbyssEye=7 AbyssPearl=8 / 死因 AbyssPearlTp=16——saves 存数值 id，改名零迁移）。
// 四腿分工：
//   「旧行为等价墙」→ r2042a（改名后暗渊之眼/暗渊珠全链行为回归：眼投射物 kind 数值 + 远段巡航
//     高度恰掷出眼位 +8 + 飞行态非碎裂；珠投射物 kind 数值 + 非整格接触落点 = 自身格经改名后
//     landed 信号上报；Game 层改名钩子传送立位 + Survival 传送自伤恰一次 (5, AbyssPearlTp) +
//     Creative 零伤；shapeless 珠+燃烬粉配方仍答不变眼物品 id 且内部分类字面新名在场；
//     真 WorldStore 临时库存读往返双物品 id/count/名逐位保真——「只改名」的最硬行为级证明）；
//   「旧词根清零钉」→ r2042b（src/ 全树递归扫八词根字面：EnderEye/EnderPearl/enderEye/enderPearl/
//     endereye/enderpearl/ender_pearl/ender_eye，命中行必携 §9 记载标记 + 每文件命中数 ==
//     盘点表口径 + 总数 24——新代码引入旧词根字面即响亮红）；豁免面 = 资源包读取面（用户包内
//     MC 物品图标布局名 ender_eye.png / ender_pearl.png）+ t1068 映射头注对照记载（本表 +
//     item 域 recipe.h 锚）。Enderman→Nightwalker 已于 t727 §9 改名，本批不涉（不入扫面）。
//   「用户可见面钉」→ r2042c（nameForBlock 新名双物品断言 + 死因文案在场 + 资源包图标路径
//     读取面字面在场 + MaterialIcon 自绘入口 + 创造调色板物品 id 面——用户可见面全数新名，
//     displayName 在 t1067 已改过「末影之眼→暗渊之眼」本批核对零重复改动）；
//   「结构钉」→ r2042d（数值 id 逐位不变钉：AbyssPearlId==0x243 / EndEyeId==0x23A /
//     Kind AbyssEye==7 / Kind AbyssPearl==8 / DeathCause AbyssPearlTp==16 + 编译期 static_assert
//     互钉 + 改名后方法/信号/字段/常量族声明面源钉 + QML delegate/连接处理器/kind 面 + 两处
//     t1068 映射头注锚串裸文本在场钉）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘旧词根清零钉（blockregistry.cpp 追加一行含旧词根字面、无 §9 标记的注释）→ 声明红面 =
//     {r2042b}（清零钉命中计数 +1 且未标记 → 单腿红；行为腿/c 钉/d 钉零误伤——纯注释行不触任何
//     断言面）。
//   NEG-2 摘用户可见面钉（hotbar.cpp nameForBlock(AbyssPearlId) 字面「暗渊珠」回改「珍珠」）→
//     声明红面 = {r2042c}（nameForBlock 断言不等红；r2042b 扫英文词根零交集、r2042a/d 零误伤）。
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section43_ender_root_rename()
{
    // ── 段内共享帮手（各腿自建 fresh 世界/临时库，零共享 rig 状态；section39/41/42 同门）─────────
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
    constexpr int kWF = 48, kDF = 48, kHF = 96; // fresh 小世界（3×3 chunk）
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2042_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };

    // ── r2042a：旧行为等价墙（改名后眼/珠全链行为回归 + 内部配方字面 + 真库存读往返逐位保真）────
    runLeg(QStringLiteral("t1068 r2042a abyss-eye and abyss-pearl behavior equivalence wall after the"
        " root rename (the eye projectile keeps entity kind seven with its far-leg cruise altitude"
        " exactly the thrown-eye height plus eight and a non-shattering flying state, the pearl"
        " projectile keeps entity kind eight and any contact on a non-full cell reports the contact"
        " cell itself through the renamed landed signal, the game-layer teleport through the renamed"
        " hook stands the player in that cell and fires the renamed death cause with exactly five"
        " hit points once per survival teleport while creative teleports stay free, the shapeless"
        " pearl plus ember-powder craft still answers the unchanged eye item id through the renamed"
        " internal recipe key with a half-recipe control answering null, and a real WorldStore"
        " save-load round trip of a gathered inventory carrying both renamed items lands every id"
        " and count bit-identical - behavior semantics bit-identical, only names moved)"),
        [&]() {
        bool ok = true;
        QString diag;

        // ① 眼投射物：kind==AbyssEye(7) + 巡航高度 == 掷出眼位 Y + 8（t757 回归面语义）+ 飞行态非碎裂。
        World wE;
        wE.setWidth(kWF);
        wE.setDepth(kDF);
        wE.setHeight(kHF);
        wE.setSeed(91);
        EntityManager entsE;
        const int eye = entsE.spawnAbyssEye(QVector3D(10.5f, 30.0f, 10.5f), QVector3D(2.0f, 0.5f, 0.0f));
        const bool eyeOk = eye >= 0 && entsE.aliveAt(eye)
            && entsE.kindAt(eye) == int(EntityManager::AbyssEye)
            && qAbs(entsE.abyssEyeCruiseYAt(eye) - 38.0f) < 1e-3f // 30 + kAbyssEyeClimbHeight 8（spawn 定死）
            && !entsE.shatteringAt(eye);
        ok = ok && eyeOk;
        if (!eyeOk)
            diag += QStringLiteral("[eye idx=%1 alive=%2 kind=%3 cruise=%4 shatter=%5] ")
                        .arg(eye).arg(entsE.aliveAt(eye)).arg(entsE.kindAt(eye))
                        .arg(entsE.abyssEyeCruiseYAt(eye)).arg(entsE.shatteringAt(eye));

        // ② 珠投射物：kind==AbyssPearl(8) + 非整格接触（火把本体格）落点 = 自身格经改名后信号上报。
        World wP;
        wP.setWidth(kWF);
        wP.setDepth(kDF);
        wP.setHeight(kHF);
        wP.setSeed(92);
        EntityManager entsP;
        int landedCount = 0, lastLx = -1, lastLy = -1, lastLz = -1;
        QObject::connect(&entsP, &EntityManager::abyssPearlLanded, &entsP,
                         [&](int x, int y, int z) { ++landedCount; lastLx = x; lastLy = y; lastLz = z; });
        const int floorY = 20;
        for (int dx = 11; dx <= 13; ++dx)
            for (int dz = 11; dz <= 13; ++dz)
                for (int y = floorY + 1; y < kHF; ++y)
                    wP.setBlock(dx, y, dz, BR::Air, 0); // 清场盒（fresh 世界自带地形/树冠——r2041a 先例：不轻信柱空）
        wP.setBlock(12, floorY, 12, BR::Stone, 0);
        wP.setBlock(12, floorY + 1, 12, BR::Torch, 0); // 非整格本体（t835① 语义：落点 = 自身格）
        const int pearl = entsP.spawnAbyssPearl(QVector3D(12.5f, 30.0f, 12.5f), QVector3D(0, 0, 0));
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        for (int t = 0; t < 120 && entsP.aliveAt(pearl); ++t)
            entsP.tick(0.016f, &wP, farListener, 0.3f, 1.8f, false);
        const bool pearlOk = pearl >= 0
            && landedCount == 1 && lastLx == 12 && lastLy == floorY + 1 && lastLz == 12;
        ok = ok && pearlOk;
        if (!pearlOk)
            diag += QStringLiteral("[pearl idx=%1 landed=%2 at %3,%4,%5] ")
                        .arg(pearl).arg(landedCount).arg(lastLx).arg(lastLy).arg(lastLz);

        // ③ Game 层传送（改名钩子直调）：Survival 立触点格 + 自伤恰一次 (5, AbyssPearlTp)；Creative 零伤。
        PlayerController pc;
        pc.setWorld(&wP);
        pc.setMode(PlayerController::Survival);
        int dmgHits = 0, dmgHp = -1, dmgCause = -1;
        QObject::connect(&pc, &PlayerController::fallDamageTaken, &pc,
                         [&](int hp, int cause) { ++dmgHits; dmgHp = hp; dmgCause = cause; });
        pc.applyAbyssPearlTeleport(12, floorY + 1, 12);
        const bool tpSurvivalOk = qAbs(pc.feetPosition().y() - float(floorY + 1)) < 0.01f
            && dmgHits == 1 && dmgHp == 5 && dmgCause == int(PlayerState::AbyssPearlTp);
        pc.setMode(PlayerController::Creative);
        pc.applyAbyssPearlTeleport(12, floorY + 1, 12); // 同触点格重传（列有支撑；创造零伤口径）
        const bool tpCreativeOk = qAbs(pc.feetPosition().y() - float(floorY + 1)) < 0.01f
            && dmgHits == 1; // 创造传送零伤：计数不再增长（t758 口径）
        ok = ok && tpSurvivalOk && tpCreativeOk;
        if (!tpSurvivalOk || !tpCreativeOk)
            diag += QStringLiteral("[tp footY=%1 hits=%2 hp=%3 cause=%4] ")
                        .arg(pc.feetPosition().y()).arg(dmgHits).arg(dmgHp).arg(dmgCause);

        // ④ 内部配方字面 + 数值产物：shapeless 珠+燃烬粉 → 眼物品 id 不变；半配方对照 → null。
        int grid[9] = { RecipeRegistry::AbyssPearlId, RecipeRegistry::BlazePowderId, 0,
                        0, 0, 0, 0, 0, 0 };
        const auto *r = RecipeRegistry::match(grid, 2);
        int gridHalf[9] = { RecipeRegistry::AbyssPearlId, 0, 0, 0, 0, 0, 0, 0, 0 };
        const auto *rHalf = RecipeRegistry::match(gridHalf, 2);
        const bool craftOk = r != nullptr && rHalf == nullptr
            && r->outputId == RecipeRegistry::EndEyeId && r->outputCount == 1
            && r->gridSize == 2 && r->shapeless
            && QLatin1String(r->name) == QLatin1String("abyss_eye");
        ok = ok && craftOk;
        if (!craftOk)
            diag += QStringLiteral("[craft r=%1 half=%2 out=%3 name=%4] ")
                        .arg(r != nullptr).arg(rHalf != nullptr)
                        .arg(r ? r->outputId : -1).arg(r ? r->name : "null");

        // ⑤ 存档安全铁律行为级：真 WorldStore 真临时库存读往返 → 双改名物品 id/count/名逐位保真。
        Hotbar vm;
        vm.setStack(0, RecipeRegistry::AbyssPearlId, 16, 0, QVariantList { 0, 0, 0, 0 }, QString());
        vm.setStack(1, RecipeRegistry::EndEyeId, 3, 0, QVariantList { 0, 0, 0, 0 },
                    QStringLiteral("t1068 eye"));
        QVariantMap data;
        QVariantList hotbarArr;
        for (int i = 0; i < vm.slotCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm.blockIdAt(i));
            s.insert(QStringLiteral("count"), vm.countAt(i));
            s.insert(QStringLiteral("durability"), vm.durabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm.enchantsAt(i));
            s.insert(QStringLiteral("name"), vm.customNameAt(i));
            hotbarArr.append(s);
        }
        data.insert(QStringLiteral("version"), 3);
        data.insert(QStringLiteral("hotbar"), hotbarArr);

        WorldStore store;
        const QString db = tempDb("a");
        QFile::remove(db);
        const bool save1 = store.openWorld(db) && store.savePlayerData(data);
        store.closeWorld();
        QVariantMap back;
        bool reopen1 = false;
        if (save1) {
            reopen1 = store.openWorld(db);
            if (reopen1) back = store.loadPlayerData();
            store.closeWorld();
        }
        QFile::remove(db);
        const QVariantList backHotbar = back.value(QStringLiteral("hotbar")).toList();
        const auto slotEq = [&](const QVariant &v, int id, int count, const QString &name) {
            const QVariantMap s = v.toMap();
            return s.value(QStringLiteral("id")).toInt() == id
                && s.value(QStringLiteral("count")).toInt() == count
                && s.value(QStringLiteral("name")).toString() == name;
        };
        const bool rtOk = save1 && reopen1 && backHotbar.size() >= 2
            && slotEq(backHotbar.at(0), RecipeRegistry::AbyssPearlId, 16, QString())
            && slotEq(backHotbar.at(1), RecipeRegistry::EndEyeId, 3, QStringLiteral("t1068 eye"));
        ok = ok && rtOk;
        if (!rtOk)
            diag += QStringLiteral("[rt save=%1 open=%2 n=%3] ")
                        .arg(save1).arg(reopen1).arg(backHotbar.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1068 r2042a abyss-eye and abyss-pearl behavior equivalence wall"
                             " after the root rename: eye kind seven with cruise altitude"
                             " throw-height plus eight, pearl kind eight landing on its own"
                             " contact cell through the renamed signal, the renamed teleport hook"
                             " standing the player in-cell with a single five-point renamed death"
                             " cause in survival and free teleports in creative, the shapeless"
                             " craft still answering the unchanged eye item id under the renamed"
                             " internal key, and a real save-load round trip keeping both renamed"
                             " item stacks bit-identical"
                          << (ok ? QString() : diag);
    });

    // ── r2042b：旧词根清零钉（src/ 全树八词根命中行必携 §9 + 每文件计数 == 盘点口径 11/6/3/4）───
    runLeg(QStringLiteral("t1068 r2042b old-root zero pin over the whole src tree (every line"
        " carrying the legacy EnderEye/EnderPearl camel roots, the endereye/enderpearl flat roots"
        " or the ender_pearl/ender_eye underscored roots must carry the section-nine record"
        " marker, the per-file hit counts must equal the audited inventory of eleven in the block"
        " registry header mapping note, six in the resource pack reader faces, three in the item"
        " domain header and four in the main QML pack-face notes for a total of twenty-four, and"
        " any other file must carry zero hits - new legacy literals without the marker ring the"
        " bell immediately)"), [&]() {
        bool ok = true;
        QString diag;
        const QRegularExpression re(QStringLiteral(
            "EnderEye|EnderPearl|enderEye|enderPearl|endereye|enderpearl|ender_pearl|ender_eye"));
        const QMap<QString, int> expected = {
            { QStringLiteral("Core/blockregistry.h"), 11 },
            { QStringLiteral("Core/resourcepackmanager.cpp"), 6 },
            { QStringLiteral("Game/recipe.h"), 3 },
            { QStringLiteral("ui/Main.qml"), 4 },
        };
        QMap<QString, int> hits;
        QStringList unmarked;
        QDirIterator it(srcRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (!QStringList { QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("qml") }
                    .contains(QFileInfo(p).suffix()))
                continue;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) {
                ok = false;
                diag += QStringLiteral("[unreadable %1] ").arg(p);
                continue;
            }
            const QString rel = QDir(srcRoot).relativeFilePath(p);
            const QStringList lines = QString::fromUtf8(f.readAll())
                                          .split(QLatin1Char('\n'));
            for (const QString &ln : lines)
                if (re.match(ln).hasMatch()) {
                    hits[rel] = hits.value(rel) + 1;
                    if (!ln.contains(QStringLiteral("§9")))
                        unmarked << QStringLiteral("%1: %2").arg(rel, ln.trimmed().left(60));
                }
        }
        if (!unmarked.isEmpty()) {
            ok = false;
            diag += QStringLiteral("[unmarked n=%1 %2] ").arg(unmarked.size()).arg(unmarked.first());
        }
        QStringList countBad;
        for (auto pit = expected.begin(); pit != expected.end(); ++pit)
            if (hits.value(pit.key()) != pit.value())
                countBad << QStringLiteral("%1=%2!=%3")
                                .arg(pit.key()).arg(hits.value(pit.key())).arg(pit.value());
        for (auto hit = hits.begin(); hit != hits.end(); ++hit)
            if (!expected.contains(hit.key()))
                countBad << QStringLiteral("unexpected %1=%2").arg(hit.key()).arg(hit.value());
        int total = 0;
        for (auto hit = hits.begin(); hit != hits.end(); ++hit) total += hit.value();
        if (!countBad.isEmpty() || total != 24) {
            ok = false;
            diag += QStringLiteral("[counts total=%1 %2] ").arg(total).arg(countBad.join(";"));
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1068 r2042b old-root zero pin: every legacy Ender word-root line in"
                             " the whole src tree carries the section-nine record marker and the"
                             " per-file hit counts equal the audited inventory of 24 lines across"
                             " four whitelisted files, with every other file at zero"
                          << (ok ? QString() : diag);
    });

    // ── r2042c：用户可见面钉（nameForBlock 双物品新名 + 死因文案 + 读取面图标路径 + 自绘入口）────
    runLeg(QStringLiteral("t1068 r2042c user-visible face pin (the hotbar name for the renamed"
        " pearl item answers the abyss pearl name and the eye item keeps its t1067 dark-abyss eye"
        " name with both item ids untouched, the abyss-pearl teleport death text stays present in"
        " the player state, the resource pack icon paths still answer the user pack layout file"
        " names on section-nine marked reader faces, the material icon self-draw entry keeps its"
        " renamed function face, and the creative palette lists the renamed pearl item id - the"
        " user-visible surface is fully on original naming with zero display-name churn in this"
        " batch)"), [&]() {
        bool ok = true;
        QString diag;

        // ① nameForBlock 新名双物品（AbyssPearlId 0x243 / EndEyeId 0x23A 数值 id 不变 → 存档/配方零迁移）。
        Hotbar hb;
        const QString pearlName = hb.nameForBlock(RecipeRegistry::AbyssPearlId);
        const QString eyeName = hb.nameForBlock(RecipeRegistry::EndEyeId);
        const bool dispOk = pearlName == QStringLiteral("暗渊珠")
            && eyeName == QStringLiteral("暗渊之眼");
        ok = ok && dispOk;
        if (!dispOk) diag += QStringLiteral("[names %1/%2] ").arg(pearlName, eyeName);

        // ② 死因文案在场（playerstate 单一权威读面字面；t758 口径）。
        // ③ 资源包图标路径读取面字面在场（用户包内 MC 布局文件名——§9 豁免，行携标记由 r2042b 口径）。
        // ④ MaterialIcon 自绘入口改名后函数面在场。
        // ⑤ 创造调色板物品 id 面在场。
        const QStringList missPs = pinSet(srcRoot + QStringLiteral("/Game/playerstate.cpp"), {
            SrcPin("t1068 death text", "被暗渊珠传送撕碎", 1),
        });
        const QStringList missRp = pinSet(srcRoot + QStringLiteral("/Core/resourcepackmanager.cpp"), {
            SrcPin("t1068 eye pack icon", "ender_eye.png", 1),
            SrcPin("t1068 pearl pack icon", "ender_pearl.png", 1),
        });
        const QStringList missMi = pinSet(srcRoot + QStringLiteral("/ui/MaterialIcon.qml"), {
            SrcPin("t1068 pearl self-draw", "drawAbyssPearl", 2),
        });
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("t1068 pearl item name", "暗渊珠", 1),
            SrcPin("t1068 palette pearl id", "int(RecipeRegistry::AbyssPearlId)", 1),
        });
        for (const QString &m : missPs + missRp + missMi + missHb) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1068 r2042c user-visible face pin: hotbar names answer the abyss"
                             " pearl and dark-abyss eye names on unchanged item ids, the teleport"
                             " death text stays present, the pack icon reader faces keep the user"
                             " pack layout names, the self-draw entry and palette id face moved in"
                             " lockstep, with zero display-name churn in this batch"
                          << (ok ? QString() : diag);
    });

    // ── r2042d：结构钉（数值 id 逐位不变 + 编译期互钉 + 声明面族 + QML 面 + 映射头注锚串）──────────
    runLeg(QStringLiteral("t1068 r2042d structure pin (the numeric item ids stay 0x243 and 0x23A,"
        " the entity kinds stay seven and eight and the death cause stays sixteen, compile-time"
        " assertions freeze those five values, the renamed spawn, signal, field, constant and"
        " teleport-hook family keeps its declaration face in the entity manager, player controller"
        " and item domain headers, the main QML answers the renamed kind faces, delegate ids and"
        " connection handlers, and the t1068 mapping-table header notes are present verbatim in"
        " both the block registry header and the item domain header - save files keep working"
        " across the rename with zero migration)"),
        [&]() {
        bool ok = true;
        QString diag;

        // ① 数值 id 逐位不变钉（存档安全铁律——saves 存数值 id，改名零迁移）。
        const bool idsOk = RecipeRegistry::AbyssPearlId == 0x243
            && RecipeRegistry::EndEyeId == 0x23A
            && int(EntityManager::AbyssEye) == 7
            && int(EntityManager::AbyssPearl) == 8
            && int(PlayerState::AbyssPearlTp) == 16;
        ok = ok && idsOk;
        if (!idsOk)
            diag += QStringLiteral("[ids %1/%2/%3/%4/%5] ")
                        .arg(RecipeRegistry::AbyssPearlId, 4, 16, QLatin1Char('0'))
                        .arg(RecipeRegistry::EndEyeId, 4, 16, QLatin1Char('0'))
                        .arg(int(EntityManager::AbyssEye)).arg(int(EntityManager::AbyssPearl))
                        .arg(int(PlayerState::AbyssPearlTp));

        // ② 编译期互钉（枚举/常量零漂移；本段 TU 编译即验证）。
        static_assert(RecipeRegistry::AbyssPearlId == 0x243, "t1068 numeric id pin: AbyssPearlId == 0x243");
        static_assert(RecipeRegistry::EndEyeId == 0x23A, "t1068 numeric id pin: EndEyeId == 0x23A");
        static_assert(int(EntityManager::AbyssEye) == 7, "t1068 kind pin: AbyssEye == 7");
        static_assert(int(EntityManager::AbyssPearl) == 8, "t1068 kind pin: AbyssPearl == 8");
        static_assert(int(PlayerState::AbyssPearlTp) == 16, "t1068 death cause pin: AbyssPearlTp == 16");

        // ③ 改名后方法/信号/字段/常量族声明面源钉（剥注释；缺声明即红）。
        const QStringList missEm = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("t1068 spawn eye", "Q_INVOKABLE int spawnAbyssEye(", 1),
            SrcPin("t1068 spawn pearl", "Q_INVOKABLE int spawnAbyssPearl(", 1),
            SrcPin("t1068 eye became item signal", "void abyssEyeBecameItem(", 1),
            SrcPin("t1068 pearl landed signal", "void abyssPearlLanded(", 1),
            SrcPin("t1068 cruise accessor", "Q_INVOKABLE float abyssEyeCruiseYAt(", 1),
            SrcPin("t1068 climb constant", "static constexpr float kAbyssEyeClimbHeight", 1),
            SrcPin("t1068 pearl gravity constant", "static constexpr float kAbyssPearlGravity", 1),
            SrcPin("t1068 dist-left field", "float abyssEyeDistLeft = 0.0f;", 1),
            SrcPin("t1068 cruise field", "float abyssEyeCruiseY = 0.0f;", 1),
        });
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("t1068 teleport hook", "Q_INVOKABLE void applyAbyssPearlTeleport(int x, int y, int z);", 1),
            SrcPin("t1068 teleport damage constant", "static constexpr int kAbyssPearlTpDamage", 1),
        });
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("t1068 pearl item id", "static constexpr int AbyssPearlId    = 0x243;", 1),
        });
        const QStringList missPst = pinSet(srcRoot + QStringLiteral("/Game/playerstate.h"), {
            SrcPin("t1068 death cause member", "AbyssPearlTp, Anvil", 1),
        });
        for (const QString &m : missEm + missPc + missRh + missPst) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ QML 面（kind 分流 / delegate id / 连接处理器——呈现层随改名同步）。
        const QStringList missQml = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("t1068 qml eye kind face", "EntityManager.AbyssEye", 1),
            SrcPin("t1068 qml pearl kind face", "EntityManager.AbyssPearl", 1),
            SrcPin("t1068 qml eye node", "id: abyssEyeNode", 1),
            SrcPin("t1068 qml pearl node", "id: abyssPearlNode", 1),
            SrcPin("t1068 qml eye handler", "function onAbyssEyeBecameItem(", 1),
            SrcPin("t1068 qml pearl handler", "function onAbyssPearlLanded(", 1),
        });
        for (const QString &m : missQml) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 映射表头注在场钉（t1068 头注锚串；头注是 §9 记载 → 裸文本 contains，不走剥注释 pinSet）。
        const auto rawContains = [](const QString &path, const char *needle) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return false;
            return QString::fromUtf8(f.readAll()).contains(QString::fromUtf8(needle));
        };
        const bool map1 = rawContains(srcRoot + QStringLiteral("/Core/blockregistry.h"),
                                      "t1068 §9 Ender 族改名映射表");
        const bool map2 = rawContains(srcRoot + QStringLiteral("/Game/recipe.h"),
                                      "t1068 §9 Ender 族改名映射表");
        const bool mapOk = map1 && map2;
        ok = ok && mapOk;
        if (!mapOk) diag += QStringLiteral("[map %1/%2] ").arg(map1).arg(map2);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1068 r2042d structure pin: numeric ids 0x243/0x23A, entity kinds"
                             " seven and eight and the death cause sixteen all unchanged, compile-"
                             "time assertions freeze the five values, the renamed family keeps its"
                             " declaration face across entity manager, player controller, item"
                             " domain and main QML, and the t1068 mapping-table header notes sit"
                             " verbatim in both headers"
                          << (ok ? QString() : diag);
    });
}
