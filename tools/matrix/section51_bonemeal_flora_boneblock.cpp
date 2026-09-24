#include "matrix_helpers.h"

#include <QFile> // pinSet 文件读取（helpers 已含；显式列出示信源码钉面）

// t1077 骨粉系统缺口两件（草方块催生草丛/花 + 骨块方块）（4 腿；filter 词 r2050；矩阵 730→734）。
// 置尾先例（接 section50，runAll 末执行，rig 世界零接触）。
//
// 现场核实：骨来源（loottable t299）/ 骨粉物品与 1 骨→3 骨粉合成（t447）/ World::applyBonemeal 三类既有
// 目标（t791）/ 真玩家交互链（P-t1030a）均已交付不重做；本单缺口 = ④ 草方块催生（applyBonemeal 第四类
// 目标，5×5 邻域确定性哈希散布草丛/花：列基 Grass + 地上空气才可生、中心强制 TallGrass、邻列 1/2 生成、
// 其中 1/8 为花 4 色、上方被占/世界顶无效应不消耗）+ 骨块（BoneBlock 144：9 骨粉 3×3 满铺 ↔ 1 块双向
// 配方；tile 185；硬 2.0 镐采 tier1 requiresTool；kMcBlockId 行取 0 =「MC 1.10+ 方块，1.0 无」）。
//
// 腿面：r2050a 草方块催生承重墙（有效目标/邻域带/零覆盖/孪生恒等/错峰差分/围死中心长/负例）；
//   r2050b 骨块承重墙（配方双向/负例/往返恒等/def 逐字段/采掘三查询/世界写读回）；
//   r2050c 催生分布/边界墙（大田账目闭合/边缘截断/2×2 组合负例）；r2050d 结构钉（单一权威/入口单点/
//   注册族/派生链工具表/kMcBlockId 对齐补行裸读钉/QML 零触碰反探）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_t1077_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘催生推进（world.cpp ④ 守卫恒假化 `if (false && id == BlockRegistry::Grass)`）→ 声明红面
//     {r2050a, r2050c}（草方块目标恒 false：中心/邻域/账目全红）。r2050b 不误伤（纯表面）；r2050d 不误伤
//     （钉面不含 ④ 守卫行字面——flora 盐/写入行在恒假分支体内仍原样在场）。实测恰红 a+c。
//   NEG-2 摘骨块拆解产出（recipe.cpp 反向配方 outputCount 9→8）→ 声明红面 {r2050b}（拆解 9 断言红 +
//     往返恒等红）。r2050a 不误伤（零配方面）；r2050c 不误伤（2×2 四骨粉恒无配方，与 count 值无关）；
//     r2050d 不误伤（配方钉只核行在场不核 count 字面）。实测恰红 b。
//   rig 纪律（lessons 902/915 净空坑）：默认地形非空——所有工作带在搭台后显式清 y 上方至世界顶，
//   否则「上方须空气」类守卫被默认泥土击穿（本段首跑 a/c 双红的根因，已修）。
void MatrixRun::section51_bonemeal_flora_boneblock()
{
    // ── r2050a：草方块催生承重墙（World 直调 applyBonemeal，t791 探针同门）─────────────────
    runLeg("r2050a grass-block bonemeal flora wall (world-direct)", [&]() {
        bool ok = true;
        World wA;
        const int cx = 16, cy = 15, cz = 16;
        const auto buildField = [&](World &w, int seed) {
            w.setWidth(40); w.setDepth(40); w.setHeight(32); w.setSeed(seed);
            for (int x = 8; x <= 24; ++x)
                for (int z = 10; z <= 28; ++z) {
                    w.setBlock(x, cy, z, BR::Grass, 0);
                    for (int y = cy + 1; y < 32; ++y) // 净空纪律：工作带上方全空气（默认地形非空）
                        w.setBlock(x, y, z, BR::Air, 0);
                }
        };
        buildField(wA, 20501);
        // (1) 有效目标：草方块+上方空气 → true；中心上方强制 TallGrass。
        const bool retA = wA.applyBonemeal(cx, cy, cz);
        bool patch = retA && wA.blockAt(cx, cy + 1, cz) == BR::TallGrass;
        ok = ok && patch;
        // (2) 5x5 邻域带 + 带外四线抽scan。
        bool bandOk = true;
        for (int nz = cz - 2; nz <= cz + 2 && bandOk; ++nz) {
            for (int nx = cx - 2; nx <= cx + 2 && bandOk; ++nx) {
                const quint8 idv = wA.blockAt(nx, cy + 1, nz);
                const bool isFlora = idv == BR::TallGrass
                    || idv == BR::FlowerRed || idv == BR::FlowerYellow
                    || idv == BR::FlowerBlue || idv == BR::FlowerWhite;
                if (!((idv == BR::Air) || (isFlora && wA.blockAt(nx, cy, nz) == BR::Grass)))
                    bandOk = false;
            }
        }
        const bool outOk = wA.blockAt(cx - 4, cy + 1, cz) == BR::Air
            && wA.blockAt(cx + 4, cy + 1, cz) == BR::Air
            && wA.blockAt(cx, cy + 1, cz - 4) == BR::Air
            && wA.blockAt(cx, cy + 1, cz + 4) == BR::Air;
        ok = ok && bandOk && outOk;
        // (3) 零覆盖：预置 Stone / 既有 TallGrass 列原样。
        World wB;
        buildField(wB, 20501);
        wB.setBlock(cx + 1, cy + 1, cz, BR::Stone, 0);
        wB.setBlock(cx, cy + 1, cz - 1, BR::TallGrass, 0);
        const bool retB = wB.applyBonemeal(cx, cy, cz);
        const bool preserveOk = retB
            && wB.blockAt(cx + 1, cy + 1, cz) == BR::Stone
            && wB.blockAt(cx, cy + 1, cz - 1) == BR::TallGrass;
        // (4) 孪生恒等：wC 同 seed 同布置同单用 → 5x5 id 逐位 == wA。
        World wC;
        buildField(wC, 20501);
        const bool retC = wC.applyBonemeal(cx, cy, cz);
        bool twinOk = retC;
        for (int nz = cz - 2; nz <= cz + 2 && twinOk; ++nz)
            for (int nx = cx - 2; nx <= cx + 2 && twinOk; ++nx)
                if (wC.blockAt(nx, cy + 1, nz) != wA.blockAt(nx, cy + 1, nz)) twinOk = false;
        ok = ok && preserveOk && twinOk;
        // (5) 错峰差分：wD 两连用（序号 0→1）落 P1、P2；wE 首用（序号 0）落 P2 → 布局必异。
        World wD;
        buildField(wD, 20501);
        for (int x = 8; x <= 24; ++x)
            for (int z = 24; z <= 28; ++z)
                wD.setBlock(x, cy, z, BR::Grass, 0);
        wD.applyBonemeal(cx, cy, cz);          // 第 1 用（序号 0）落 P1
        const bool retD2 = wD.applyBonemeal(16, cy, 26); // 第 2 用（序号 1）落 P2
        World wE;
        buildField(wE, 20501);
        for (int x = 8; x <= 24; ++x)
            for (int z = 24; z <= 28; ++z)
                wE.setBlock(x, cy, z, BR::Grass, 0);
        const bool retE1 = wE.applyBonemeal(16, cy, 26); // 首用（序号 0）落 P2
        bool seqOk = retD2 && retE1;
        bool differ = false;
        for (int nz = 24; nz <= 28; ++nz)
            for (int nx = 14; nx <= 18; ++nx)
                if (wD.blockAt(nx, cy + 1, nz) != wE.blockAt(nx, cy + 1, nz)) differ = true;
        seqOk = seqOk && differ;
        ok = ok && seqOk;
        // (6) 围死仍中心长：8 邻列上方预置 Stone → 仍 true 且仅中心生。
        World wF;
        buildField(wF, 20501);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                wF.setBlock(cx + dx, cy + 1, cz + dz, BR::Stone, 0);
            }
        const bool retF = wF.applyBonemeal(cx, cy, cz);
        bool walledOk = retF && wF.blockAt(cx, cy + 1, cz) == BR::TallGrass;
        for (int dz = -1; dz <= 1 && walledOk; ++dz)
            for (int dx = -1; dx <= 1 && walledOk; ++dx) {
                if (dx == 0 && dz == 0) continue;
                if (wF.blockAt(cx + dx, cy + 1, cz + dz) != BR::Stone) walledOk = false;
            }
        // (7) 负例：上方被占 → false；泥土目标 → false。
        const bool negOk = !wB.applyBonemeal(cx + 1, cy, cz)
            && wB.blockAt(cx + 1, cy + 1, cz) == BR::Stone;
        wB.setBlock(22, cy, 22, BR::Dirt, 0);
        const bool negOk2 = !wB.applyBonemeal(22, cy, 22);
        ok = ok && walledOk && negOk && negOk2;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2050a grass-block bonemeal flora wall (world-direct)"
            << (ok ? QString()
                    : QString("diag retA=%1 center=%2 patch=%3 preserve=%4 twin=%5 seq=%6 walled=%7 neg=%8")
                        .arg(retA).arg(wA.blockAt(cx, cy + 1, cz)).arg(patch)
                        .arg(preserveOk).arg(twinOk).arg(seqOk).arg(walledOk).arg(negOk && negOk2));
    });
    // ── r2050b：骨块承重墙（纯表直查 + World 写读回）──────────────────────────────────
    runLeg("r2050b bone-block wall (pure table + world round trip)", [&]() {
        bool ok = true;
        const int M = RecipeRegistry::BonemealId;
        const int B = BR::BoneBlock;
        // (1) 正向：9 骨粉 3x3 满铺 -> 1 骨块（shaped）。
        const int g9[9] = { M, M, M, M, M, M, M, M, M };
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(g9, 3);
        const bool composeOk = rc && rc->outputId == B && rc->outputCount == 1 && !rc->shapeless;
        // (2) 负例：8 骨粉 / 8 骨粉+1 骨头 / 双块 → 无配方。
        const int g8[9] = { M, M, M, M, M, M, M, M, 0 };
        const int g8b[9] = { M, M, M, M, M, M, M, M, RecipeRegistry::BoneId };
        const int g2blk[9] = { B, B, 0, 0, 0, 0, 0, 0, 0 };
        const bool negOk = RecipeRegistry::match(g8, 3) == nullptr
            && RecipeRegistry::match(g8b, 3) == nullptr
            && RecipeRegistry::match(g2blk, 3) == nullptr;
        // (3) 反向：1 骨块 2x2 单放 / 3x3 中心单放 → 9 骨粉（shapeless 位置无关）。
        const int g2x2[4] = { B, 0, 0, 0 };
        const RecipeRegistry::Recipe *rd2 = RecipeRegistry::match(g2x2, 2);
        const bool decompose2 = rd2 && rd2->outputId == M && rd2->outputCount == 9 && rd2->shapeless;
        const int g3c[9] = { 0, 0, 0, 0, B, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rd3 = RecipeRegistry::match(g3c, 3);
        const bool decompose3 = rd3 && rd3->outputId == M && rd3->outputCount == 9;
        // (4) 往返恒等：正向产块 x1 + 反向产粉 x9 双向证毕即 9<->1<->9 无损。
        const bool roundTrip = composeOk && decompose2 && rc->outputCount == 1 && rd2->outputCount == 9;
        // (5) def 行逐字段（tile 185 六面同 + 石质族属性 + 掉自身）。
        const auto &d = BR::def(B);
        const bool defOk = d.id == B && d.topTile == 185 && d.bottomTile == 185
            && d.sideTile == 185 && d.frontTile == 185
            && d.solid && d.shape == BR::ShapeFull
            && d.hardness == 2.0f && d.toolType == int(BR::Pickaxe)
            && d.minToolTier == 1 && d.requiresTool
            && d.dropId == B && d.dropCount == 1 && d.maxStack == 64
            && QByteArray(d.name) == "bone_block";
        // (6) 采掘面：空手不可采 / 木镐可采 / 速度两向 / 耗时镐<手。
        const bool mineOk = !ToolRegistry::canHarvest(B, 0)
            && ToolRegistry::canHarvest(B, ToolRegistry::PickaxeWood)
            && ToolRegistry::miningSpeedMul(B, 0) == 1.0f
            && ToolRegistry::miningSpeedMul(B, ToolRegistry::PickaxeWood) > 1.0f
            && ToolRegistry::miningTime(B, ToolRegistry::PickaxeWood) < ToolRegistry::miningTime(B, 0);
        // (7) 世界写读回。
        World wG;
        wG.setWidth(24); wG.setDepth(24); wG.setHeight(32); wG.setSeed(20502);
        const bool worldOk = wG.setBlock(12, 16, 12, BR::BoneBlock, 0)
            && wG.blockAt(12, 16, 12) == BR::BoneBlock
            && wG.stateAt(12, 16, 12) == 0;
        ok = ok && composeOk && negOk && decompose2 && decompose3 && roundTrip && defOk && mineOk && worldOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2050b bone-block wall (pure table + world round trip)";
    });
    // ── r2050c：催生分布/边界墙（大田账目闭合 + 边缘截断 + 2x2 组合负例）───────────────
    runLeg("r2050c flora spread bookkeeping and edge truncation", [&]() {
        bool ok = true;
        World wA;
        const int cx = 30, cy = 20, cz = 30;
        const auto bigField = [&](World &w) {
            w.setWidth(56); w.setDepth(56); w.setHeight(40); w.setSeed(20503);
            for (int x = 14; x <= 42; ++x)
                for (int z = 14; z <= 42; ++z) {
                    w.setBlock(x, cy, z, BR::Grass, 0);
                    for (int y = cy + 1; y < 40; ++y) // 净空纪律：全田上方清空
                        w.setBlock(x, y, z, BR::Air, 0);
                }
        };
        bigField(wA);
        const bool retA = wA.applyBonemeal(cx, cy, cz);
        // 全田扫 y=cy+1：id 合法 + 基座 Grass + 账目闭合。
        int grown = 0, invalid = 0;
        for (int z = 14; z <= 42; ++z)
            for (int x = 14; x <= 42; ++x) {
                const quint8 idv = wA.blockAt(x, cy + 1, z);
                if (idv == BR::Air) continue;
                const bool isFlora = idv == BR::TallGrass
                    || idv == BR::FlowerRed || idv == BR::FlowerYellow
                    || idv == BR::FlowerBlue || idv == BR::FlowerWhite;
                const bool baseOk = wA.blockAt(x, cy, z) == BR::Grass;
                if (!isFlora || !baseOk) ++invalid;
                else ++grown;
            }
        const bool retOk = retA && grown >= 1 && grown <= 25 && invalid == 0
            && wA.blockAt(cx, cy + 1, cz) == BR::TallGrass;
        ok = ok && retOk;
        // 边缘截断：中心置田角 (2,16,2) → 邻域越界列被拒 → 不崩 + 带外方向零生成。
        World wB;
        wB.setWidth(24); wB.setDepth(24); wB.setHeight(32); wB.setSeed(20504);
        for (int x = 0; x < 24; ++x)
            for (int z = 0; z < 24; ++z) {
                if (x >= 2 && x <= 22 && z >= 2 && z <= 22)
                    wB.setBlock(x, 16, z, BR::Grass, 0);
                for (int y = 17; y < 32; ++y) // 净空纪律：全场上方清空（含田外检查点）
                    wB.setBlock(x, y, z, BR::Air, 0);
            }
        const bool retB = wB.applyBonemeal(2, 16, 2);
        const bool edgeOk = retB
            && wB.blockAt(2, 17, 2) == BR::TallGrass
            && wB.blockAt(0, 17, 2) == BR::Air
            && wB.blockAt(2, 17, 0) == BR::Air
            && wB.blockAt(1, 17, 1) == BR::Air; // x-1/z-1 两向越界 → 仅在世界内列生成
        ok = ok && edgeOk;
        // 2x2 四骨粉 → 无配方（骨块正向仅 3x3 满铺）。
        const int M2 = RecipeRegistry::BonemealId;
        const int g4[4] = { M2, M2, M2, M2 };
        const bool two2neg = RecipeRegistry::match(g4, 2) == nullptr;
        ok = ok && two2neg;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2050c flora spread bookkeeping and edge truncation"
            << (ok ? QString()
                    : QString("diag retA=%1 grown=%2 invalid=%3 centerId=%4 retB=%5 two2neg=%6")
                        .arg(retA).arg(grown).arg(invalid).arg(wA.blockAt(cx, cy + 1, cz)).arg(retB).arg(two2neg));
    });
    // ── r2050d：结构钉（源码钉，剥注释口径）──────────────────────────────────────────
    runLeg("r2050d structure pins (single authority, single routing, registration family)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString toolsRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("tools"));
        const QString worldCpp = srcRoot + QStringLiteral("/World/world.cpp");
        const QString worldH = srcRoot + QStringLiteral("/World/world.h");
        const QString pcCpp = srcRoot + QStringLiteral("/Game/playercontroller.cpp");
        const QString brH = srcRoot + QStringLiteral("/Core/blockregistry.h");
        const QString brCpp = srcRoot + QStringLiteral("/Core/blockregistry.cpp");
        const QString recCpp = srcRoot + QStringLiteral("/Game/recipe.cpp");
        const QString hbCpp = srcRoot + QStringLiteral("/Game/hotbar.cpp");
        const QString rpmCpp = srcRoot + QStringLiteral("/Core/resourcepackmanager.cpp");
        const QString mainQml = srcRoot + QStringLiteral("/ui/Main.qml");
        const QString atlasPy = toolsRoot + QStringLiteral("/build_atlas.py");
        const QString iconsPy = toolsRoot + QStringLiteral("/build_cube_icons.py");
        QString worldCppTxt, worldHTxt, pcTxt, brHTxt, brCppTxt, recTxt, hbTxt, rpmTxt, qmlTxt, atlasTxt, iconsTxt;
        const auto rawRead = [&](const QString &path, QString &out) {
            QFile f(path);
            out = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            return !out.isEmpty();
        };
        const bool reads = rawRead(worldCpp, worldCppTxt) && rawRead(worldH, worldHTxt)
            && rawRead(pcCpp, pcTxt) && rawRead(brH, brHTxt) && rawRead(brCpp, brCppTxt)
            && rawRead(recCpp, recTxt) && rawRead(hbCpp, hbTxt) && rawRead(rpmCpp, rpmTxt)
            && rawRead(mainQml, qmlTxt) && rawRead(atlasPy, atlasTxt)
            && rawRead(iconsPy, iconsTxt);
        ok = ok && reads;
        if (!reads) diag += QStringLiteral("[reads] ");
        // 单一权威钉（world.h 声明唯一 / world.cpp 定义唯一 + 催生机制面）。
        const QStringList missWorldH = pinSet(worldH, {
            SrcPin("applyBonemeal decl", "bool applyBonemeal(int x, int y, int z);", 1)});
        const QStringList missWorld = pinSet(worldCpp, {
            SrcPin("applyBonemeal def unique", "bool World::applyBonemeal(int x, int y, int z)", 1),
            SrcPin("flora salt consume", "hashVoxel(mixedSeed ^ int(kBonemealFloraSalt), nx, y, nz)", 1),
            SrcPin("flora write", "m_chunks.setBlock(nx, y + 1, nz, flora)", 1)});
        ok = ok && missWorldH.isEmpty() && missWorld.isEmpty();
        if (!(missWorldH.isEmpty() && missWorld.isEmpty()))
            diag += QStringLiteral("[world %1/%2] ").arg(missWorldH.join(QLatin1Char(',')))
                .arg(missWorld.join(QLatin1Char(',')));
        // 交互入口单点钉（playercontroller 唯一分流判据 + 唯一调用点）。
        const QStringList missPc = pinSet(pcCpp, {
            SrcPin("bonemeal routing predicate", "heldItemId == RecipeRegistry::BonemealId", 1),
            SrcPin("applyBonemeal call site", "m_world->applyBonemeal(", 1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty()) diag += QStringLiteral("[pc %1] ").arg(missPc.join(QLatin1Char(',')));
        // 注册族钉（枚举 / Count / 图集总数）。
        const QStringList missBrH = pinSet(brH, {
            SrcPin("BoneBlock enum entry", "BoneBlock         = 144", 1),
            // t1080 钉面同变更修订（追加不插中间先例——现值随新方块/新瓦片尾部追加 lawful 前移：
            //   145→146 / 186→189；钉语义 = 「哨兵存在」而非冻结数值，历史值见 git 与 blockregistry 注）：
            //   t1083 二次同门修订：Jukebox=146 尾部追加 → 146→147 / 189→191。
            SrcPin("Count sentinel", "Count           = 147", 1),
            SrcPin("atlas tile count", "AtlasTileCount = 191", 1)});
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty()) diag += QStringLiteral("[br.h %1] ").arg(missBrH.join(QLatin1Char(',')));
        // kMcBlockId 双行（音符盒对齐补行 25 + 骨块行 0）——行注释后缀形态 → 裸读文件口径（raw contains）。
        //   t1083 三次同门修订：骨块后尾部追加唱片机行（MC 1.0 jukebox id 84）→ 骨块行不再是表尾
        //   「*/ 0,\n};」，表尾现为「*/ 84,\n};」；骨块行本体（*/ 0,）钉存在性不钉「最末」位。
        const bool brCppRows = brCppTxt.contains(QLatin1String("*/ 25,")) // note_block 行（音符盒 MC 1.0 id 25）
            && brCppTxt.contains(QLatin1String("*/ 0,"))
            && brCppTxt.contains(QLatin1String("*/ 84,\n};")); // 唱片机行（表尾最后一条目，t1083）
        ok = ok && brCppRows;
        if (!brCppRows) diag += QStringLiteral("[br.cpp rows] ");
        // 配方两行 / 调色板 / 图标 / pack 映射 / 派生链工具表。
        const QStringList missRec = pinSet(recCpp, {
            SrcPin("compose row", "\"bone_block\" }", 1),
            SrcPin("decompose row", "\"bone_meal_x9\" }", 1)});
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty()) diag += QStringLiteral("[recipe %1] ").arg(missRec.join(QLatin1Char(',')));
        const QStringList missHb = pinSet(hbCpp, {
            SrcPin("icon case", "case BlockRegistry::BoneBlock:    return \"icon_bone_block.png\"", 1),
            SrcPin("palette entry", "int(BlockRegistry::BoneBlock)", 1)});
        ok = ok && missHb.isEmpty();
        if (!missHb.isEmpty()) diag += QStringLiteral("[hotbar %1] ").arg(missHb.join(QLatin1Char(',')));
        const QStringList missRpm = pinSet(rpmCpp, {
            SrcPin("pack tile mapping", "{185, QStringLiteral(\"bone_block.png\")}", 1)});
        ok = ok && missRpm.isEmpty();
        if (!missRpm.isEmpty()) diag += QStringLiteral("[rpm %1] ").arg(missRpm.join(QLatin1Char(',')));
        const QStringList missAtlas = pinSet(atlasPy, {
            SrcPin("atlas tile entry", "\"default_bone_block\",", 1)});
        const QStringList missIcons = pinSet(iconsPy, {
            SrcPin("icon source entry", "(\"bone_block\",      \"default_bone_block\", \"default_bone_block\")", 1)});
        ok = ok && missAtlas.isEmpty() && missIcons.isEmpty();
        if (!(missAtlas.isEmpty() && missIcons.isEmpty()))
            diag += QStringLiteral("[tools %1/%2] ").arg(missAtlas.join(QLatin1Char(',')))
                .arg(missIcons.join(QLatin1Char(',')));
        // 反探：QML 零骨粉分发面（交互走 Game 层权威链，呈现层零第二入口）。
        const bool qmlClean = !qmlTxt.contains(QLatin1String("applyBonemeal"))
            && !qmlTxt.contains(QLatin1String("BonemealId"));
        ok = ok && qmlClean;
        if (!qmlClean) diag += QStringLiteral("[qml touched] ");
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2050d structure pins (single authority, single routing, registration family)"
            << (ok ? QString() : diag);
    });
}

