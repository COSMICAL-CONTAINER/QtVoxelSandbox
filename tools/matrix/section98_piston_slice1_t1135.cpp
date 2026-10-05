#include "matrix_helpers.h"

// t1135 活塞切片一探针段（4 腿；filter 词 r2105；矩阵 962→966）。置尾先例沿用（接 section97，
//   runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界直驱推动机 / 红石 tick，余纯源钉/工件钉腿）。
//
// ── 单性裁定：切片一交付单（t1134 切分序首片；交付面 = 普通活塞本体 id 162 段尾追加 + kMc 33 +
//    state = facing 3b + extended 1b（era meta 同构）+ 推动机（扫 ≤12 上限 + 拒推四员 + 附着断裂
//    掉落 + 位移搬运，EditBuffer 批量静默写 + TNT detonateTntSphere 先例单次刷新）+ tickRedstone
//    接收器接入（受电伸 / 失电缩，瞬时简化）+ 创造调色板。合成 / 头块 / 粘性 / 两拍动画 / 移动
//    占位 / 实体位移 = 切片二三四（t1134 切分序），本单零触达）。
//
// ── era 定谳锚（前置件工件 build/t1135_jar_material.txt + t1134 工件三件在盘复用）──
//  · 迁移位 1 材料成员清单（t1134 登记不猜 → 本单 jar 复核定谳）：sn 类材料恰二实例 p.g/p.h，
//    成员 = 恰四方块 id 8/9/10/11（水/岩浆流动+静止）→ 引擎 Water=21/Lava=31 全族；火把/红石粉/
//    拉杆/中继器（p.p）/压力板（p.e）负发现在册。附着断裂执行面 = 毁格 + 零掉落（era 流体无物品
//    形态掉落量恒 0）。
//  · 上限 12（count==12 败；扫描失败 = 整次放弃零部分推动）；拒推四员（黑曜石 / 硬度 -1 /
//    BlockContainer 派生族 [isStoreBlock] / 已伸活塞）；迁移位 2 = era 死分支不实现（t1134 定谳）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；摘行均单行完整语句编译绿）──
//  NEG-1 = 双手工 Edit 摘 world.cpp 激励调用行（any = pistonReceiverAt(...) || any; 单行完整语句）
//    → 恰红 = {r2105c, r2105d}（激励 rig 柱活塞恒不伸 + 结构钉族激励调用行钉失配；t1133 双腿
//    恰红先例同门）。r2105a/b 豁免（推动机直驱零触达该行）。
//  NEG-2 = 双手工 Edit 摘 world.cpp 推动上限判定行（if (i == BlockRegistry::PistonPushLimit)
//    { return false; } 单行完整语句）→ 恰红 = {r2105a, r2105d}（13 实心恰败子断言翻绿向 +
//    结构钉族上限判定行钉失配）。r2105b/c 豁免（流体界 / 小线推动零触达第 13 实心判定）。
//  双还原 = NEG 后双手工 Edit 逐字还原原行（mv 保字节不适用代码行——手工 Edit 还原 + md5 复核）。
namespace {

// 源钉根路径（section96/97 同门：applicationDirPath/../src）。
inline QString srcRootForPistonSlice1Pins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体/工件锚——pinSet 剥注释会失配，section72..97 同款）。
inline bool rawContainsPistonSlice1(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonSlice1(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 造一块石坪 + 清上空（section83/88/97 同款四 setter）。
inline void initPistonSlice1World(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 清空 rig 盒（四 setter 会触发 worldgen——rig 域内岩体 / 洞穴随 seed 漂移，推动机 / 断裂柱
//   的界格判定须已知空气域；先清后铺石坪）。盒域 = 行为腿布局域 + 界格余量（y0..y0+6）。
inline void clearPistonSlice1Box(World &w, int y0)
{
    for (int x = 16; x <= 40; ++x)
        for (int y = y0; y <= y0 + 6; ++y)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y, z, BR::Air, 0);
}

} // namespace

void MatrixRun::section98_piston_slice1_t1135()
{
    // ── r2105a:推动柱（基本推动 + 12 上限 + 拒推四员 + 扫描失败零部分推动）────────────────
    //   NEG-2 敏感腿（上限判定行摘除 → 13 实心恰败子断言翻红）。
    runLeg("r2105a push machine column (a piston facing plus x shifts a three stone line"
        " one cell forward writing the two beat moving placeholder window and then settling"
        " the head block in the first cell and the terminator cell loaded while"
        " the machine leaves the extended bit to the receiver, a twelve stone line with air"
        " beyond succeeds and a thirteen stone line fails with the region bit identical so"
        " the push limit twelve abandons the whole attempt with zero partial push, and the"
        " four reject members obsidian and negative hardness bedrock and store family chest"
        " and already extended piston each fail with zero world change while a retracted"
        " piston stays pushable)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice1World(w, 1135);
        const int y0 = 40;
        clearPistonSlice1Box(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        auto regionIds = [&w, y0](int x0, int z0) {
            QVector<int> ids;
            for (int x = x0; x <= x0 + 15; ++x)
                for (int y = y0 + 1; y <= y0 + 2; ++y)
                    for (int z = z0; z <= z0 + 15; ++z)
                        ids.append(int(w.blockAt(x, y, z)));
            return ids;
        };
        // (1) 基本推动：活塞 (20,41,24) 朝 +X（facing 5）；线 21..23 石。推动当拍 = 两拍占位窗
        //     （t1137 lawful 行为修订：切片三排定动画——机器当拍落 PistonMoving(164) 占位 + 侧表，
        //     两拍后 settle 实体化；t1136 曾当拍头块，钉随交付面前移）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 24, BR::Stone, 0);
        const bool pushOk = w.tryPistonExtend(20, y0 + 1, 24);
        ok = ok && pushOk;
        if (!pushOk) diag += QStringLiteral("[basic ret]");
        World::PistonAnimEntry probe;
        const bool winHead = w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.pistonAnimProbeAt(21, y0 + 1, 24, probe)
            && probe.storedId == BR::PistonHead && probe.extending && probe.beats == 2;
        ok = ok && winHead;
        if (!winHead) diag += QStringLiteral("[win head]");
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool basicMoved = w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone
            && w.blockAt(24, y0 + 1, 24) == BR::Stone
            && w.pistonAnimCount() == 0;
        ok = ok && basicMoved;
        if (!basicMoved) diag += QStringLiteral("[basic moved]");
        const quint8 headState = w.stateAt(21, y0 + 1, 24);
        const bool headFacing = (headState & BR::PistonStateFacingMask) == 5
            && (headState & BR::PistonStateExtendedFlag) == 0;
        ok = ok && headFacing; // 头块 state = 本体朝向镜像 + bit3 恒 0（t1136 头块承载位，settle 面实核）
        if (!headFacing) diag += QStringLiteral("[head state=%1]").arg(headState);
        const quint8 bitState = w.stateAt(20, y0 + 1, 24);
        const bool bitUntouched = (bitState & BR::PistonStateExtendedFlag) == 0;
        ok = ok && bitUntouched; // 机器只搬块，extended 位归接收器（红石灯同门口径）
        if (!bitUntouched) diag += QStringLiteral("[bit set=%1]").arg(bitState);
        // (2) 12 上限：线 21..32 恰 12 石 + 33 空气 → 成功（21 落头占位 → 两拍后 settle 实体化）；
        //     线 21..33 恰 13 石 → 败 + 域快照恒等（占位零写——整次放弃零部分推动不变量含占位承载位）。
        w.setBlock(20, y0 + 1, 26, BR::Piston, 5);
        for (int x = 21; x <= 32; ++x)
            w.setBlock(x, y0 + 1, 26, BR::Stone, 0);
        const bool capOk = w.tryPistonExtend(20, y0 + 1, 26);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool capMoved = capOk && w.blockAt(21, y0 + 1, 26) == BR::PistonHead
            && w.blockAt(32, y0 + 1, 26) == BR::Stone && w.blockAt(33, y0 + 1, 26) == BR::Stone;
        ok = ok && capMoved;
        if (!capMoved) diag += QStringLiteral("[cap12 ok=%1]").arg(capOk);
        w.setBlock(20, y0 + 1, 28, BR::Piston, 5);
        for (int x = 21; x <= 33; ++x)
            w.setBlock(x, y0 + 1, 28, BR::Stone, 0);
        const QVector<int> before13 = regionIds(18, 26);
        const bool cap13 = w.tryPistonExtend(20, y0 + 1, 28);
        ok = ok && !cap13;
        if (cap13) diag += QStringLiteral("[cap13 pushed]");
        const QVector<int> after13 = regionIds(18, 26);
        ok = ok && before13 == after13;
        if (!(before13 == after13)) diag += QStringLiteral("[cap13 partial]");
        // (3) 拒推四员：黑曜石 / 硬度 -1（基岩）/ 侧存储族（箱子）/ 已伸活塞——各败 + 域恒等；
        //     缩回态活塞可推（era 同——拒推只钉伸出态）。
        struct RejectCase { int z; BR::Id blocker; quint8 blockerState; const char *tag; };
        const RejectCase cases[] = {
            { 30, BR::Obsidian, 0, "obsidian" },
            { 32, BR::Bedrock, 0, "bedrock" },
            { 34, BR::Chest, 0, "chest" },
            { 36, BR::Piston, quint8(5 | BR::PistonStateExtendedFlag), "extpiston" },
        };
        for (const RejectCase &c : cases) {
            w.setBlock(20, y0 + 1, c.z, BR::Piston, 5);
            w.setBlock(21, y0 + 1, c.z, BR::Stone, 0);
            w.setBlock(22, y0 + 1, c.z, c.blocker, c.blockerState);
            const QVector<int> before = regionIds(18, c.z - 1);
            const bool rejected = !w.tryPistonExtend(20, y0 + 1, c.z);
            const QVector<int> after = regionIds(18, c.z - 1);
            const bool frozen = before == after;
            ok = ok && rejected && frozen;
            if (!rejected || !frozen)
                diag += QStringLiteral("[%1 rej=%2 frozen=%3]").arg(QLatin1String(c.tag))
                            .arg(rejected).arg(frozen);
        }
        // (4) 缩回态活塞在线内可推（era 同；t1137：占位窗 + 两拍 settle 后实核）。
        w.setBlock(20, y0 + 1, 38, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 38, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 38, BR::Piston, 5); // 缩回态活塞（facing 5 未伸）
        const bool pushed4 = w.tryPistonExtend(20, y0 + 1, 38);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool retractedPushable = pushed4
            && w.blockAt(21, y0 + 1, 38) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 38) == BR::Stone
            && w.blockAt(23, y0 + 1, 38) == BR::Piston;
        ok = ok && retractedPushable;
        if (!retractedPushable) diag += QStringLiteral("[retracted]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2105a push machine column (a piston facing plus x shifts a three stone"
               " line one cell forward writing the two beat moving placeholder window and then"
               " settling the head block in the first cell and the terminator"
               " cell loaded while the machine leaves the extended bit to the receiver, a twelve"
               " stone line with air beyond succeeds and a thirteen stone line fails with"
               " the region bit identical so the push limit twelve abandons the whole"
               " attempt with zero partial push, and the four reject members obsidian and"
               " negative hardness bedrock and store family chest and already extended"
               " piston each fail with zero world change while a retracted piston stays"
               " pushable)"
            << (ok ? QString() : diag);
    });

    // ── r2105b:附着断裂柱（前置件清单逐成员：水/岩浆界格毁格零掉落 + 界格收线尾块）──────────
    //   era 定谳（build/t1135_jar_material.txt）：迁移位 1 成员 = 水/岩浆全族——界格毁格清空 +
    //   零掉落（era 流体无物品形态）；12 实心 + 流体第 13 格 = 流体是界非第 13 实心（成功）。
    runLeg("r2105b attachment break column (a water terminator is destroyed with zero item"
        " drops while the stone line loads into the freed cell behind the head block and the"
        " drop signal counter"
        " stays zero, a lava terminator answers the same destroy and zero drop face, a"
        " fluid directly at the piston face is destroyed and the head block takes the freed"
        " cell with an empty line and zero drops,"
        " and twelve solids with a thirteenth fluid cell succeed because the fluid is the"
        " boundary rather than a thirteenth solid)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice1World(w, 1136);
        const int y0 = 40;
        clearPistonSlice1Box(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int dropCount = 0;
        QObject::connect(&w, &World::blockDroppedAsItem, [&dropCount](int, int, int, int) {
            ++dropCount;
        });
        // (1) 水界：活塞 + 2 石 + 水（state 0）→ 推动成功、水毁零掉落、线收进界格 + 头占位落首格
        //     （t1137 lawful 行为修订：占位窗 + 两拍 settle 实核；t1136 曾当拍头块钉）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 24, BR::Water, 0);
        const bool waterOk = w.tryPistonExtend(20, y0 + 1, 24);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool waterFace = waterOk && w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone;
        ok = ok && waterFace;
        if (!waterFace) diag += QStringLiteral("[water ok=%1]").arg(waterOk);
        // (2) 岩浆界：同面（Lava=31）。
        w.setBlock(20, y0 + 1, 26, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 26, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 26, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 26, BR::Lava, 0);
        const bool lavaOk = w.tryPistonExtend(20, y0 + 1, 26);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool lavaFace = lavaOk && w.blockAt(21, y0 + 1, 26) == BR::PistonHead
            && w.blockAt(23, y0 + 1, 26) == BR::Stone;
        ok = ok && lavaFace;
        if (!lavaFace) diag += QStringLiteral("[lava ok=%1]").arg(lavaOk);
        // (3) 贴脸流体（零实心线）：水直接在活塞面前 → 毁格落头占位、零掉落（t1137：占位窗 +
        //     settle 实核；t1136 曾当拍头块）。
        w.setBlock(20, y0 + 1, 28, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 28, BR::Water, 0);
        const bool pushed3 = w.tryPistonExtend(20, y0 + 1, 28);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool faceFluid = pushed3 && w.blockAt(21, y0 + 1, 28) == BR::PistonHead;
        ok = ok && faceFluid;
        if (!faceFluid) diag += QStringLiteral("[facefluid]");
        // (4) 12 实心 + 流体第 13 格：流体是界非第 13 实心（上限不误拒）→ 成功 + 界格收线尾块 +
        //     首格落头占位 → 两拍 settle。
        w.setBlock(20, y0 + 1, 30, BR::Piston, 5);
        for (int x = 21; x <= 32; ++x)
            w.setBlock(x, y0 + 1, 30, BR::Stone, 0);
        w.setBlock(33, y0 + 1, 30, BR::Water, 0);
        const bool pushed4 = w.tryPistonExtend(20, y0 + 1, 30);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool fluidBound = pushed4
            && w.blockAt(21, y0 + 1, 30) == BR::PistonHead
            && w.blockAt(33, y0 + 1, 30) == BR::Stone;
        ok = ok && fluidBound;
        if (!fluidBound) diag += QStringLiteral("[fluidbound]");
        // (5) 附着断裂恒零掉落（era dropBlockAsItemWithChance 流体掉落量恒 0 → 引擎零信号）。
        ok = ok && dropCount == 0;
        if (dropCount != 0) diag += QStringLiteral("[drops=%1]").arg(dropCount);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2105b attachment break column (a water terminator is destroyed with zero"
               " item drops while the stone line loads into the freed cell behind the head block and the drop"
               " signal counter stays zero, a lava terminator answers the same destroy and"
               " zero drop face, a fluid directly at the piston face is destroyed and the head block takes the freed"
               " cell with an empty line and zero drops, and twelve solids with a thirteenth fluid cell"
               " succeed because the fluid is the boundary rather than a thirteenth solid)"
            << (ok ? QString() : diag);
    });

    // ── r2105c:激励柱（受电伸 / 失电缩 + 方向化查询同门 + 两拍排定动画行为钉）────────────────
    //   NEG-1 敏感腿（激励调用行摘除 → 活塞恒不伸）。t1137 lawful 行为修订：瞬时简化收口 = era
    //   两拍排定动画交付面（一个红石 tick 写占位 + 置位，两拍动画 tick 后 settle 实体化——
    //   build/t1137_jar_piston_anim.txt 定谳二时序台账）。
    runLeg("r2105c excitation column (one redstone tick with a lit lever extends the piston"
        " sets the extended bit and writes the moving placeholder window in that single pass"
        " as the era scheduled animation that settles the head block into the first cell"
        " after two animation ticks,"
        " unpowering the lever clears the bit destroys the head block and keeps the line"
        " settled in place, repowering extends again over the settled line, and a"
        " powered repeater feeds the piston only through the directional gate so the output"
        " facing away leaves the piston retracted while the output facing the piston"
        " extends it)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice1World(w, 1137);
        const int y0 = 40;
        clearPistonSlice1Box(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 受电伸（t1137 era 排定动画行为钉）：拉杆（state bit0=1）贴活塞西侧。一个红石 tick =
        //     占位窗写入 + 位置位；两拍动画 tick 后 settle 实体化头块于 25。
        w.setBlock(24, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(25, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(26, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        const quint8 extState = w.stateAt(24, y0 + 1, 24);
        const bool extended = (extState & BR::PistonStateExtendedFlag) != 0;
        const bool winWritten = extended
            && w.blockAt(25, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(26, y0 + 1, 24) == BR::PistonMoving;
        ok = ok && winWritten;
        if (!winWritten)
            diag += QStringLiteral("[ext state=%1]").arg(extState);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool onePassMoved = w.blockAt(25, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(26, y0 + 1, 24) == BR::Stone
            && w.blockAt(27, y0 + 1, 24) == BR::Stone;
        ok = ok && onePassMoved;
        if (!onePassMoved)
            diag += QStringLiteral("[settle]");
        // (2) 失电缩：位清 + 头块消失（25 头格清空）+ 线不回搬（era 非粘性缩回头格当拍清空——
        //     era 缩回无动程；粘性拉回 = 切片二同单交付）。
        w.setBlock(23, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        w.tickRedstone();
        const quint8 retState = w.stateAt(24, y0 + 1, 24);
        const bool retracted = (retState & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(25, y0 + 1, 24) == BR::Air
            && w.blockAt(26, y0 + 1, 24) == BR::Stone
            && w.blockAt(27, y0 + 1, 24) == BR::Stone;
        ok = ok && retracted;
        if (!retracted) diag += QStringLiteral("[ret state=%1]").arg(retState);
        // (3) 复伸：线上 settled 界（25 空）→ 再伸置位零搬移。
        w.setBlock(23, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        const bool reext = (w.stateAt(24, y0 + 1, 24) & BR::PistonStateExtendedFlag) != 0;
        ok = ok && reext;
        if (!reext) diag += QStringLiteral("[reext]");
        // (4) 方向化查询（t1130 sourceFeedsCell 同门）：红石块恒源喂中继器输入端（repeaterInputOn
        //     后端直读），中继器经自身状态机点燃后仅输出面馈电——输出背离活塞 → 活塞缩回保持；
        //     输出朝活塞 → 活塞伸。（不手写 RepeaterStatePoweredFlag——该位归中继器状态机所有，
        //     手写位在输入失供的下一 pass 被自身机器合法熄灭，rig 会读到自熄假阴性。）
        w.setBlock(24, y0 + 1, 30, BR::Piston, 5);
        w.setBlock(22, y0 + 1, 30, BR::RedstoneBlock, 0); // 中继器输入端（后端）恒源
        w.setBlock(23, y0 + 1, 30, BR::Repeater, 1);      // 输出面 -X（背向活塞）
        for (int i = 0; i < 3; ++i)
            w.tickRedstone();
        const bool awayDark = (w.stateAt(24, y0 + 1, 30) & BR::PistonStateExtendedFlag) == 0;
        ok = ok && awayDark;
        if (!awayDark) diag += QStringLiteral("[away]");
        w.setBlock(23, y0 + 1, 30, BR::Repeater, 0);      // 输出面 +X（朝活塞；输入端恒源不变）
        for (int i = 0; i < 3; ++i)
            w.tickRedstone();
        const bool towardLit = (w.stateAt(24, y0 + 1, 30) & BR::PistonStateExtendedFlag) != 0;
        ok = ok && towardLit;
        if (!towardLit) diag += QStringLiteral("[toward]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2105c excitation column (one redstone tick with a lit lever extends the"
               " piston sets the extended bit and writes the moving placeholder window in that"
               " single pass as the era scheduled animation that settles the head block into"
               " the first cell after two animation ticks,"
               " unpowering the lever clears the bit destroys the head block and keeps the line"
               " settled in place, repowering extends again over"
               " the settled line, and a powered repeater feeds the piston only through the"
               " directional gate so the output facing away leaves the piston retracted"
               " while the output facing the piston extends it)"
            << (ok ? QString() : diag);
    });

    // ── r2105d:结构钉族柱（注册行 / 激励调用行 / 推动机行——NEG-1/NEG-2 摘面行本腿持有）──────
    //   NEG-1 敏感（激励调用行）+ NEG-2 敏感（上限判定行）。含 era 前置件工件钉 / filter 词元
    //   零命中 / QML 玩法路径零迁移负面门复钉（r2090d 同门）。
    runLeg("r2105d structure pins column (the block registry carries the piston id selection"
        " pin and the state bit constants and the facing decoder and the definition row and"
        " the mapping row and the store family predicate, the world layer carries the push"
        " machine and the receiver family row and the receiver scan row and the excitation"
        " call row and the push limit judgment row, the presentation and palette rows are"
        " wired, the era material adjudication artifact exists non empty carrying the"
        " mobility flag field anchor and both material instances, the source tree stays"
        " free of the cross task filter token, and the gameplay path literals stay out of"
        " the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonSlice1Pins();
        // (R1) blockregistry.h 注册行族（id 选型 static_assert 钉 + state 位 + 朝向解码 + 谓词）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("id selection assert",
                                                      "static_assert(int(Piston) == 162", 1),
                                               SrcPin("enum row", "Piston           = 162,", 1),
                                               SrcPin("count sentinel", "Count           = 166,", 1), // t1136 lawful 前移：163→166（头块 163/移动占位 164/粘性 165 段尾追加——钉文随源行前移；t1135 曾 162→163、t1113 曾 160→162）
                                               SrcPin("extended flag",
                                                      "PistonStateExtendedFlag = 0x08", 1),
                                               SrcPin("facing mask",
                                                      "PistonStateFacingMask   = 0x07", 1),
                                               SrcPin("facing decoder decl",
                                                      "static void pistonFacingDelta(quint8 state", 1),
                                               SrcPin("push limit",
                                                      "PistonPushLimit         = 12", 1),
                                               SrcPin("store predicate",
                                                      "static bool isStoreBlock(quint8 blockId);", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        // (R2) blockregistry.cpp 表行族（kDefs 行 + kMc 行 + 侧存储族谓词体）。
        const QStringList missBrC = pinSet(root + QStringLiteral("/Core/blockregistry.cpp"),
                                           {
                                               SrcPin("defs row",
                                                      "{int(BlockRegistry::Piston),             211", 1),
                                               SrcPin("store family body",
                                                      "bool BlockRegistry::isStoreBlock(quint8 blockId)", 1),
                                               SrcPin("facing decoder def",
                                                      "void BlockRegistry::pistonFacingDelta(quint8 state, int &dx, int &dy, int &dz)", 1),
                                           });
        ok = ok && missBrC.isEmpty();
        if (!missBrC.isEmpty())
            diag += QStringLiteral("[brc %1]").arg(missBrC.join(QLatin1Char(',')));
        // kMc 行 = 块注释承载行（pinSet 剥块注释会失配 → 原始读同门）。
        const bool kmcRow = rawContainsPistonSlice1(
            root + QStringLiteral("/Core/blockregistry.cpp"), QByteArray("piston                  */ 33,"));
        ok = ok && kmcRow;
        if (!kmcRow) diag += QStringLiteral("[kmcRow]");
        // (R3) world 层行族（推动机 + 接收器族入族行 + 接收器扫行 + 激励调用行 + 上限判定行）。
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("push machine",
                                                        "bool World::tryPistonExtend(int x, int y, int z)", 1),
                                                 SrcPin("receiver action",
                                                        "bool World::pistonReceiverAt(", 1),
                                                 SrcPin("excitation call row",
                                                        "any = pistonReceiverAt(x, y, z, b, st, powered) || any;", 1),
                                                 SrcPin("push limit judgment row",
                                                        "if (i == BlockRegistry::PistonPushLimit) { return false; }", 1),
                                                 SrcPin("family row", "|| BR::isPiston(id)", 1),
                                                 SrcPin("scan row", "|| BlockRegistry::isPiston(b))", 1),
                                                 SrcPin("writable gate",
                                                        "bool World::pistonCellWritable(int x, int y, int z) const", 1),
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        // (R4) 呈现/调色板行族（mesher 朝向面分支 + 放置朝向分支 + 调色板行 + 图标入族行）。
        //   t1136 lawful 修订：两分支钉随源行前移——mesher 走族谓词 isPiston(block)、放置走
        //   isPiston(m_selectedBlock)（单一权威族谓词承载粘性活塞同分支，t1135 曾钉 == Piston 单 id 面）。
        const QStringList missMesh = pinSet(root + QStringLiteral("/World/meshbuilder.cpp"),
                                            {
                                                SrcPin("tile facing branch",
                                                       "if (BlockRegistry::isPiston(block)) {", 1),
                                            });
        ok = ok && missMesh.isEmpty();
        if (!missMesh.isEmpty())
            diag += QStringLiteral("[mesh %1]").arg(missMesh.join(QLatin1Char(',')));
        const QStringList missPc = pinSet(root + QStringLiteral("/Game/playercontroller.cpp"),
                                          {
                                              SrcPin("placement facing branch",
                                                     "BlockRegistry::isPiston(m_selectedBlock)", 1),
                                          });
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty())
            diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
        const QStringList missHotbar = pinSet(root + QStringLiteral("/Game/hotbar.cpp"),
                                              {
                                                  SrcPin("palette row",
                                                         "int(BlockRegistry::Piston),", 1),
                                                  SrcPin("icon atlas row",
                                                         "case BlockRegistry::Piston:", 1),
                                              });
        ok = ok && missHotbar.isEmpty();
        if (!missHotbar.isEmpty())
            diag += QStringLiteral("[hotbar %1]").arg(missHotbar.join(QLatin1Char(',')));
        // (R5) QML 调色板行（注释体原始读——pinSet 剥注释会失配，raw contains 同门）。
        const QString qmlInv = root + QStringLiteral("/ui/Inventory.qml");
        const bool qmlPalette = rawContainsPistonSlice1(qmlInv, QByteArray("Piston"));
        ok = ok && qmlPalette;
        if (!qmlPalette) diag += QStringLiteral("[qmlPalette]");
        // (R6) era 前置件工件钉（本单 jar 复核件在盘非空携锚：J 旗标字段 + 二材料实例构造位）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const QString matArt = bdir + QStringLiteral("/t1135_jar_material.txt");
        const bool artOk = fileExistsNonEmptyPistonSlice1(matArt)
            && rawContainsPistonSlice1(matArt, QByteArray("Field J:I"))
            && rawContainsPistonSlice1(matArt, QByteArray("Field g:Lp"))
            && rawContainsPistonSlice1(matArt, QByteArray("Field h:Lp"));
        ok = ok && artOk;
        if (!artOk) diag += QStringLiteral("[artifact]");
        // (R7) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(root,
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonSlice1(it.next(), QByteArray("r2105"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (R8) QML 玩法路径零迁移负面门（r2090d/r2102e/r2103e/r2104d 同门复钉）。
        {
            const QString qml = root + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsPistonSlice1(qml, QByteArray("GameSession"))
                && !rawContainsPistonSlice1(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonSlice1(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonSlice1(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonSlice1(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2105d structure pins column (the block registry carries the piston id"
               " selection pin and the state bit constants and the facing decoder and the"
               " definition row and the mapping row and the store family predicate, the"
               " world layer carries the push machine and the receiver family row and the"
               " receiver scan row and the excitation call row and the push limit judgment"
               " row, the presentation and palette rows are wired, the era material"
               " adjudication artifact exists non empty carrying the mobility flag field"
               " anchor and both material instances, the source tree stays free of the cross"
               " task filter token, and the gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
