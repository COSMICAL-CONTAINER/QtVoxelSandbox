#include "matrix_helpers.h"

// t1140 活塞推动响应完整化探针段（5 腿；filter 词 r2110；矩阵 985→990）。置尾先例沿用（接 section102，
//   runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界直驱推动机 / 缩回机 / 动画 tick，余纯源钉/工件钉腿）。
//
// ── 单性裁定：第三轮外审 A05=F03/F04 era 定谳翻案承接（活塞族唯一登记未清偿面）——交付五面：
//    ① 全材料→方块映射重建（全枚举工件 build/t1140_jar_material_map.txt = 唯一权威：era p.txt static{}
//    全块 30 链逐链字节定谳，J=1 恰 14 材料实例 g,h,i,j,k,n,p,s,u,w,y,z,B,C / J=2 恰 A,D——audit #29
//    F-1 勘正 12 例口径 + 交接单 F03/F04 翻案 t1135 前置件三处失实）；② canPush/拉回 J 三响应语义
//    （0=搬移原样 / 1=Destroy 终止推进+真实掉落路径恰一次 / 2=拒推全单放弃零部分推动；拉回镜像
//    J≠0 不可拉、缩回态活塞本体族可拉）；③ 活塞头拒推拒拉（era acu p.D=J=2——双活塞相向/正交/六向）；
//    ④ r2105/r2106 腿 lawful 修订（section98/99 沿革注 + r2105d R6b 扩员钉，前序 NEG 八面保全）；
//    ⑤ 验收柱 = 六方向 / Destroy 终止格 / 双活塞相向+正交 / 附着物逐类掉落恰一次 + 状态索引同步
//    + 普通块与容器零回归。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；摘行均单行完整语句编译绿）──
//  NEG-1 = 双手工 Edit 摘 world.cpp Destroy 真实掉落行（if (termDestroy && BlockRegistry::dropId(
//    termOldId) > 0) emit blockDroppedAsItem(...); 单行完整语句；termDestroy 仍被终止格写面读用、
//    dropId 在头占位行等处另有读者 → 零未用告警编译绿行为废）→ 恰红 = {r2110a 掉落子断言族,
//    r2110d 掉落期望面, r2110e 真实掉落行钉}（实测量为准）。r2110b/c 豁免（J=2 拒推面 / 拉回面
//    零掉落路径依赖）。
//  NEG-2 = 双手工 Edit 摘 world.cpp J=2 拒推行（if (pushJ == 2) return false; 单行完整语句——pushJ
//    仍被 J=1 行读用 → 零未用告警编译绿行为废）→ 恰红 = {r2110b, r2110e J=2 行钉}（头/门体/暗渊门
//    面拒推翻绿向）。r2110a/c/d 豁免（Destroy 面 / 拉回面 / 映射面零触达 J=2 行）。
//  双还原 = NEG 后双手工 Edit 逐字还原原行（手工 Edit 还原 + md5 复核；还原后各带实跑）。
namespace {

// 源钉根路径（section96..102 同门：applicationDirPath/../src）。
inline QString srcRootForPistonMaterialPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体/工件锚——pinSet 剥注释会失配，section98..102 同款）。
inline bool rawContainsPistonMaterial(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonMaterial(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 造一块石坪 + 清上空（section98..101 同款四 setter）。
inline void initPistonMaterialWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

inline void clearPistonMaterialBox(World &w, int y0)
{
    for (int x = 16; x <= 40; ++x)
        for (int y = y0; y <= y0 + 6; ++y)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y, z, BR::Air, 0);
}

// 朝向增量镜像（era ot.b/c/d 方向表同值——腿内独立表，expected 独立建证源 = 全枚举工件第四节
//   era 判定序，非抄实现）。
inline void pistonMaterialFacingDelta(int facing, int &dx, int &dy, int &dz)
{
    switch (facing & 0x07) {
    case 0: dx = 0; dy = -1; dz = 0; break;
    case 1: dx = 0; dy = 1; dz = 0; break;
    case 2: dx = 0; dy = 0; dz = -1; break;
    case 3: dx = 0; dy = 0; dz = 1; break;
    case 4: dx = -1; dy = 0; dz = 0; break;
    case 5: dx = 1; dy = 0; dz = 0; break;
    default: dx = 0; dy = 1; dz = 0; break;
    }
}

inline int pistonMaterialOpposite(int facing)
{
    const int table[6] = { 1, 0, 3, 2, 5, 4 };
    return table[size_t(facing & 0x07)];
}

} // namespace

void MatrixRun::section103_piston_material_t1140()
{
    // ── r2110a:Destroy 推动柱（A05 逐类掉落恰一次 + J 先于上限 + 首员终止 + 状态索引同步）────
    //   NEG-1 敏感腿（真实掉落行摘除 → 逐类掉落子断言翻红）。
    runLeg("r2110a destroy push column (a piston facing plus x with a two stone line and a"
        " torch terminator pushes successfully destroying the torch through the real drop"
        " path exactly once while the line loads into the freed cell and the head block"
        " settles in the first cell, the dust and lever and repeater terminators each"
        " answer the same destroy once drop face, a torch directly at the piston face is"
        " destroyed with one drop and the head takes the freed cell, twelve solids with a"
        " thirteenth torch succeed because the destroy verdict precedes the push limit, a"
        " second torch behind the first stays in place with exactly one drop, and the"
        " destroyed dust schedules exactly one power recompute pass that converges by the"
        " next tick so the state index sync face holds)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonMaterialWorld(w, 1140);
        const int y0 = 40;
        clearPistonMaterialBox(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int dropCount = 0, lastDropId = -1;
        QObject::connect(&w, &World::blockDroppedAsItem,
                         [&dropCount, &lastDropId](int, int, int, int id) {
                             ++dropCount;
                             lastDropId = id;
                         });
        // (1) 火把终止格：活塞 (20,41,24) 朝 +X + 石 21..22 + 火把 23（Destroy 终止格）→ 推动成功、
        //     火把走真实掉落路径恰一次、线收进其格（两拍 settle 后 21 头 / 22..23 石）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 24, BR::Torch, 1);
        const bool torchOk = w.tryPistonExtend(20, y0 + 1, 24);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool torchFace = torchOk && w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone;
        ok = ok && torchFace;
        if (!torchFace) diag += QStringLiteral("[torch ok=%1]").arg(torchOk);
        ok = ok && dropCount == 1 && lastDropId > 0;
        if (dropCount != 1 || lastDropId <= 0)
            diag += QStringLiteral("[torch drops=%1 id=%2]").arg(dropCount).arg(lastDropId);
        // (2) A05 逐类：粉 / 拉杆 / 中继器终止格各掉落恰一次（每员独立槽位独立基线）。
        struct DestroyCase { int z; BR::Id id; const char *tag; };
        const DestroyCase cases[] = {
            { 26, BR::RedstoneDust, "dust" },
            { 28, BR::Lever, "lever" },
            { 30, BR::Repeater, "repeater" },
        };
        for (const DestroyCase &c : cases) {
            w.setBlock(20, y0 + 1, c.z, BR::Piston, 5);
            w.setBlock(21, y0 + 1, c.z, BR::Stone, 0);
            w.setBlock(22, y0 + 1, c.z, BR::Stone, 0);
            w.setBlock(23, y0 + 1, c.z, c.id, 0);
            const int base = dropCount;
            const bool pushed = w.tryPistonExtend(20, y0 + 1, c.z);
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const bool face = pushed && w.blockAt(21, y0 + 1, c.z) == BR::PistonHead
                && w.blockAt(23, y0 + 1, c.z) == BR::Stone;
            ok = ok && face;
            if (!face) diag += QStringLiteral("[%1 face=%2]").arg(QLatin1String(c.tag)).arg(pushed);
            ok = ok && dropCount == base + 1;
            if (dropCount != base + 1)
                diag += QStringLiteral("[%1 drops=%2]").arg(QLatin1String(c.tag)).arg(dropCount - base);
        }
        // (3) 贴脸 Destroy（零实心线）：火把直接在活塞面前 → 毁格 + 恰一次掉落 + 头占位 settle。
        w.setBlock(20, y0 + 1, 32, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 32, BR::Torch, 1);
        const int base3 = dropCount;
        const bool face3 = w.tryPistonExtend(20, y0 + 1, 32);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool faceDestroy = face3 && w.blockAt(21, y0 + 1, 32) == BR::PistonHead;
        ok = ok && faceDestroy;
        if (!faceDestroy) diag += QStringLiteral("[face destroy=%1]").arg(face3);
        ok = ok && dropCount == base3 + 1;
        if (dropCount != base3 + 1)
            diag += QStringLiteral("[face drops=%1]").arg(dropCount - base3);
        // (4) 12 实心 + 第 13 格火把 = Destroy 终止成功（era J 判在 count 判之前——非上限败）。
        w.setBlock(20, y0 + 1, 34, BR::Piston, 5);
        for (int x = 21; x <= 32; ++x)
            w.setBlock(x, y0 + 1, 34, BR::Stone, 0);
        w.setBlock(33, y0 + 1, 34, BR::Torch, 1);
        const int base4 = dropCount;
        const bool pushed4 = w.tryPistonExtend(20, y0 + 1, 34);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool limitFace = pushed4 && w.blockAt(21, y0 + 1, 34) == BR::PistonHead
            && w.blockAt(33, y0 + 1, 34) == BR::Stone;
        ok = ok && limitFace;
        if (!limitFace) diag += QStringLiteral("[limit pushed=%1]").arg(pushed4);
        ok = ok && dropCount == base4 + 1;
        if (dropCount != base4 + 1)
            diag += QStringLiteral("[limit drops=%1]").arg(dropCount - base4);
        // (5) 首员终止：石 + 火把 + 火把 → 恰第一格被毁一次掉落，第二格原样在位。
        w.setBlock(20, y0 + 1, 36, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 36, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 36, BR::Torch, 1);
        w.setBlock(23, y0 + 1, 36, BR::Torch, 1);
        const int base5 = dropCount;
        const bool pushed5 = w.tryPistonExtend(20, y0 + 1, 36);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool firstFace = pushed5 && w.blockAt(21, y0 + 1, 36) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 36) == BR::Stone
            && w.blockAt(23, y0 + 1, 36) == BR::Torch;
        ok = ok && firstFace;
        if (!firstFace) diag += QStringLiteral("[first face]");
        ok = ok && dropCount == base5 + 1;
        if (dropCount != base5 + 1)
            diag += QStringLiteral("[first drops=%1]").arg(dropCount - base5);
        // (6) 状态索引同步：被毁粉格经推动机 note 钩子入电力脏集 → 恰一次重算 pass 后收敛
        //     （下一 tick 零再算——索引无陈项）。
        w.setBlock(20, y0 + 1, 38, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 38, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 38, BR::RedstoneDust, 0);
        tickN(w, 1); // 预清稳态（前五案 Destroy 族的在途脏面一并消化）
        const int passBase = w.powerRecomputePasses();
        const bool pushed6 = w.tryPistonExtend(20, y0 + 1, 38);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        tickN(w, 1);
        const int passOne = w.powerRecomputePasses();
        tickN(w, 1);
        const int passTwo = w.powerRecomputePasses();
        const bool syncFace = pushed6 && passOne == passBase + 1 && passTwo == passOne;
        ok = ok && syncFace;
        if (!syncFace)
            diag += QStringLiteral("[sync base=%1 one=%2 two=%3]").arg(passBase).arg(passOne).arg(passTwo);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2110a destroy push column (a piston facing plus x with a two stone line"
               " and a torch terminator pushes successfully destroying the torch through the"
               " real drop path exactly once while the line loads into the freed cell and the"
               " head block settles in the first cell, the dust and lever and repeater"
               " terminators each answer the same destroy once drop face, a torch directly at"
               " the piston face is destroyed with one drop and the head takes the freed"
               " cell, twelve solids with a thirteenth torch succeed because the destroy"
               " verdict precedes the push limit, a second torch behind the first stays in"
               " place with exactly one drop, and the destroyed dust schedules exactly one"
               " power recompute pass that converges by the next tick so the state index"
               " sync face holds)"
            << (ok ? QString() : diag);
    });

    // ── r2110b:J=2 拒推柱（头块/门体/暗渊门面 + 双活塞相向/正交 + 六方向 + 零部分推动）────────
    //   NEG-2 敏感腿（J=2 拒推行摘除 → 拒推翻绿向 + 零部分推动子断言翻红）。
    runLeg("r2110b block response column (an ember gate terminator refuses the whole push"
        " with the region bit identical, an extended piston head refuses the push of a"
        " second piston in the facing and in the orthogonal direction across all six"
        " facings with zero partial writes, an orphan abyss gate surface is cleared on"
        " placement by the integrity machine with the surface block response answered at"
        " the table face, and a retracted piston inside a line stays pushable so the piston"
        " family bypass survives)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonMaterialWorld(w, 1141);
        const int y0 = 40;
        clearPistonMaterialBox(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        auto regionIds = [&w, y0](int x0, int y1, int z0) {
            QVector<int> ids;
            for (int x = x0; x <= x0 + 10; ++x)
                for (int y = y1 - 1; y <= y1 + 3; ++y)
                    for (int z = z0; z <= z0 + 10; ++z)
                        ids.append(int(w.blockAt(x, y, z)) * 256 + int(w.stateAt(x, y, z)));
            return ids;
        };
        // (1) 门体（EmberGate，era 90 sc p.A=J=2）终止格：活塞 + 石 + 门体 → 全单败 + 域逐位恒等。
        w.setBlock(20, y0 + 1, 20, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 20, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 20, BR::EmberGate, 0);
        const QVector<int> beforeGate = regionIds(18, y0 + 1, 18);
        const bool gateRefused = !w.tryPistonExtend(20, y0 + 1, 20);
        const QVector<int> afterGate = regionIds(18, y0 + 1, 18);
        ok = ok && gateRefused && beforeGate == afterGate;
        if (!gateRefused || !(beforeGate == afterGate))
            diag += QStringLiteral("[gate rej=%1 frozen=%2]").arg(gateRefused).arg(beforeGate == afterGate);
        // (2) 暗渊门面（AbyssGateSurface，era 119 aid 容器派生面 → J=2 表面）：孤儿门面被完整性
        //     机器当拍清除（t664 checkAbyssGateIntegrity——任何编辑 ±3 反查环，孤儿静默清 Air），
        //     独立放置面不存在 → 本子面改钉「孤儿清除机器先占」面 + J=2 表值直读面；拒推行为面由
        //     门体（EmberGate）与头块（PistonHead）两员承载（J=2 族同响应）。
        w.setBlock(20, y0 + 1, 22, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 22, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 22, BR::AbyssGateSurface, 0);
        const bool surfCleared = w.blockAt(22, y0 + 1, 22) == BR::Air; // 孤儿门面当拍清除（机器先占面）
        const bool surfTableBlock = BR::materialPushResponse(BR::AbyssGateSurface) == 2; // J=2 表值直读面
        const QVector<int> beforeSurf = regionIds(18, y0 + 1, 20);
        const bool surfPushed = w.tryPistonExtend(20, y0 + 1, 22); // 门面已被机器清空 → 推动入空界成功
        const QVector<int> afterSurf = regionIds(18, y0 + 1, 20);
        ok = ok && surfCleared && surfTableBlock && surfPushed;
        if (!surfCleared || !surfTableBlock || !surfPushed)
            diag += QStringLiteral("[surf clear=%1 tbl=%2 push=%3]").arg(surfCleared).arg(surfTableBlock).arg(surfPushed);
        ok = ok && beforeSurf != afterSurf; // 机器清空 + 推动写入 = 域变化如实（孤儿面零驻留）
        if (!(beforeSurf != afterSurf))
            diag += QStringLiteral("[surf frozen]");
        // (3) 六方向头块拒推（双活塞相向 + 正交）：活塞 A 沿朝向 d 伸程落头格（零线伸程），活塞 B1
        //     在 A+2d 反向朝 A（首个扫描格 = 头格）→ J=2 全单败零写；B2 在头格侧向（正交）反向朝头格
        //     → 同败。覆盖 0..5 全朝向。
        const int rows[6] = { 22, 25, 28, 31, 34, 37 };
        for (int f = 0; f < 6; ++f) {
            int ddx = 0, ddy = 0, ddz = 0;
            pistonMaterialFacingDelta(f, ddx, ddy, ddz);
            const int opp = pistonMaterialOpposite(f);
            const int perp = (f == 4 || f == 5) ? 2 : (f == 2 || f == 3) ? 4 : 2;
            int pdx = 0, pdy = 0, pdz = 0;
            pistonMaterialFacingDelta(perp, pdx, pdy, pdz);
            const int b2Facing = pistonMaterialOpposite(perp);
            // A 本体位：-Y 朝向抬到 y0+3（头格落 y0+2 避开石坪），其余在工作行 y0+1；头格 = A+d。
            const int ax = 24, az = rows[size_t(f)];
            const int ay = (f == 0) ? y0 + 3 : y0 + 1;
            w.setBlock(ax, ay, az, BR::Piston, quint8(f));
            const bool extOk = w.tryPistonExtend(ax, ay, az);
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const int hx2 = ax + ddx, hy2 = ay + ddy, hz2 = az + ddz;
            const bool headPlaced = extOk && w.blockAt(hx2, hy2, hz2) == BR::PistonHead;
            ok = ok && headPlaced;
            if (!headPlaced)
                diag += QStringLiteral("[f%1 head=%2]").arg(f).arg(extOk);
            // B1 相向：A+2d 反向朝头格。
            const int b1x = ax + 2 * ddx, b1y = ay + 2 * ddy, b1z = az + 2 * ddz;
            w.setBlock(b1x, b1y, b1z, BR::Piston, quint8(opp));
            const QVector<int> beforeB1 = regionIds(std::min(ax, b1x) - 2, std::min(ay, b1y), std::min(az, b1z) - 2);
            const bool b1Refused = !w.tryPistonExtend(b1x, b1y, b1z);
            const QVector<int> afterB1 = regionIds(std::min(ax, b1x) - 2, std::min(ay, b1y), std::min(az, b1z) - 2);
            ok = ok && b1Refused && beforeB1 == afterB1;
            if (!b1Refused || !(beforeB1 == afterB1))
                diag += QStringLiteral("[f%1 b1 rej=%2 frozen=%3]").arg(f).arg(b1Refused).arg(beforeB1 == afterB1);
            // B2 正交：头格 + 垂向，反向朝头格。
            const int b2x = hx2 + pdx, b2y = hy2 + pdy, b2z = hz2 + pdz;
            w.setBlock(b2x, b2y, b2z, BR::Piston, quint8(b2Facing));
            const QVector<int> beforeB2 = regionIds(std::min(hx2, b2x) - 2, std::min(hy2, b2y), std::min(hz2, b2z) - 2);
            const bool b2Refused = !w.tryPistonExtend(b2x, b2y, b2z);
            const QVector<int> afterB2 = regionIds(std::min(hx2, b2x) - 2, std::min(hy2, b2y), std::min(hz2, b2z) - 2);
            ok = ok && b2Refused && beforeB2 == afterB2;
            if (!b2Refused || !(beforeB2 == afterB2))
                diag += QStringLiteral("[f%1 b2 rej=%2 frozen=%3]").arg(f).arg(b2Refused).arg(beforeB2 == afterB2);
        }
        // (4) 缩回态活塞在线内可推（era 活塞分支旁路面回归——J 表缺省 0 编码）。
        w.setBlock(20, y0 + 1, 40 > 38 ? 38 : 38, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 38, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 38, BR::StickyPiston, 5); // 缩回态粘性活塞（facing 5 未伸）
        const bool retractedPushed = w.tryPistonExtend(20, y0 + 1, 38);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool retractedOk = retractedPushed
            && w.blockAt(21, y0 + 1, 38) == BR::PistonHead
            && w.blockAt(23, y0 + 1, 38) == BR::StickyPiston;
        ok = ok && retractedOk;
        if (!retractedOk) diag += QStringLiteral("[retracted]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2110b block response column (an ember gate terminator refuses the whole"
               " push with the region bit identical, an extended piston head refuses the push"
               " of a second piston in the facing and in the orthogonal direction across all"
               " six facings with zero partial writes, an orphan abyss gate surface is"
               " cleared on placement by the integrity machine with the surface block"
               " response answered at the table face, and a retracted piston inside a line"
               " stays pushable so the piston family bypass survives)"
            << (ok ? QString() : diag);
    });

    // ── r2110c:拉回镜像柱（J≠0 全族不可拉 + 缩回态活塞本体族可拉 + 拒拉零掉落）────────────────
    runLeg("r2110c pull mirror column (a sticky piston retract refuses to pull a torch at"
        " the source cell so the head clears alone with the torch untouched and zero item"
        " drops, a piston head at the source refuses the pull through the block response, a"
        " water source cell refuses the pull through the destroy response, an ember gate"
        " source cell refuses the pull, a retracted piston at the source is pulled back"
        " with its facing state preserved through the piston family clause, and a retracted"
        " sticky piston at the source is pulled back the same way)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonMaterialWorld(w, 1142);
        const int y0 = 40;
        clearPistonMaterialBox(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int dropCount = 0;
        QObject::connect(&w, &World::blockDroppedAsItem, [&dropCount](int, int, int, int) {
            ++dropCount;
        });
        // 通用 rig：粘性活塞 20 朝 +X + 石 21..22 + 拉杆 19 点燃 → 伸程 settle → 源格 22 置换为
        //     探针员 → 失电缩回 → 断言拉回判定。槽位族 z = 20/23/26/29/32/35。
        struct PullCase { int z; BR::Id id; quint8 st; bool pullable; BR::Id pulledId; quint8 pulledSt; const char *tag; };
        const PullCase cases[] = {
            { 20, BR::Torch, 0, false, BR::Air, 0, "torch" }, // 落地火把（state 0 = TorchFloor 贴地支撑
                                                              //   ——墙面火把 state 1 的支撑 = 头格，
                                                              //   缩回头格清除会连带失撑掉落，污染拉回面）
            { 23, BR::PistonHead, 5, false, BR::Air, 0, "head" },
            { 26, BR::Water, 0, false, BR::Air, 0, "water" },
            { 29, BR::EmberGate, 0, false, BR::Air, 0, "gate" },
            { 32, BR::Piston, 3, true, BR::Piston, 3, "piston" },
            { 35, BR::StickyPiston, 2, true, BR::StickyPiston, 2, "sticky" },
        };
        for (const PullCase &c : cases) {
            w.setBlock(20, y0 + 1, c.z, BR::StickyPiston, 5);
            w.setBlock(21, y0 + 1, c.z, BR::Stone, 0);
            w.setBlock(22, y0 + 1, c.z, BR::Stone, 0);
            w.setBlock(19, y0 + 1, c.z, BR::Lever, 0x01);
            w.tickRedstone();
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const bool extFace = w.blockAt(21, y0 + 1, c.z) == BR::PistonHead
                && w.blockAt(22, y0 + 1, c.z) == BR::Stone;
            if (!extFace) { // 伸程面失败 = rig 缺陷，计红快退本员
                ok = false;
                diag += QStringLiteral("[%1 extrig]").arg(QLatin1String(c.tag));
                w.setBlock(19, y0 + 1, c.z, BR::Air, 0);
                continue;
            }
            w.setBlock(22, y0 + 1, c.z, c.id, c.st); // 源格置换（r2106a 黑曜石置换先例同门）
            w.setBlock(19, y0 + 1, c.z, BR::Lever, 0x00);
            w.tickRedstone();
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const int base = dropCount;
            if (c.pullable) {
                const bool pulled = w.blockAt(21, y0 + 1, c.z) == c.pulledId
                    && w.blockAt(22, y0 + 1, c.z) == BR::Air
                    && w.stateAt(21, y0 + 1, c.z) == c.pulledSt;
                ok = ok && pulled;
                if (!pulled)
                    diag += QStringLiteral("[%1 pulled id=%2 st=%3]").arg(QLatin1String(c.tag))
                                .arg(w.blockAt(21, y0 + 1, c.z)).arg(w.stateAt(21, y0 + 1, c.z));
            } else {
                const bool notPulled = w.blockAt(21, y0 + 1, c.z) == BR::Air
                    && w.blockAt(22, y0 + 1, c.z) == c.id;
                ok = ok && notPulled;
                if (!notPulled)
                    diag += QStringLiteral("[%1 notpulled id=%2 src=%3]").arg(QLatin1String(c.tag))
                                .arg(w.blockAt(21, y0 + 1, c.z)).arg(w.blockAt(22, y0 + 1, c.z));
            }
            ok = ok && dropCount == base; // 拒拉零掉落面（Destroy 只在推动相）
            if (dropCount != base)
                diag += QStringLiteral("[%1 drops=%2]").arg(QLatin1String(c.tag)).arg(dropCount - base);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2110c pull mirror column (a sticky piston retract refuses to pull a torch"
               " at the source cell so the head clears alone with the torch untouched and"
               " zero item drops, a piston head at the source refuses the pull through the"
               " block response, a water source cell refuses the pull through the destroy"
               " response, an ember gate source cell refuses the pull, a retracted piston at"
               " the source is pulled back with its facing state preserved through the"
               " piston family clause, and a retracted sticky piston at the source is pulled"
               " back the same way)"
            << (ok ? QString() : diag);
    });

    // ── r2110d:全材料映射行为柱（全枚举工件第五节引擎成员面逐员考定——Destroy 掉落 / 零掉落 /
    //     J=0 可推回归；NEG-1 敏感腿二[掉落期望面]）───────────────────────────────────────────
    runLeg("r2110d material map column (every destroy family member answers the destroy"
        " once face when terminating a push and the zero drop members fluids leaves fire"
        " and cake answer the destroy face with zero drops, and the movable members snow"
        " glass pane stone pressure plate glass and wool are pushed intact as the era byte"
        " verdicts hold for the significant non members)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonMaterialWorld(w, 1143);
        const int y0 = 40;
        clearPistonMaterialBox(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int dropCount = 0;
        QObject::connect(&w, &World::blockDroppedAsItem, [&dropCount](int, int, int, int) {
            ++dropCount;
        });
        // Destroy 族逐员（贴脸终止格：活塞 (x-1) 朝 +X + 成员格 x → 毁格 + 头占位 settle）。
        //   掉落期望 = era 掉落路径面：携带物品形态成员恰一次；零物品形态成员（流体四 id/树叶/
        //   火/蛋糕）零信号（era 掉落量恒 0 面）。
        struct Member { BR::Id id; int x, z; int drops; const char *tag; };
        const Member members[] = {
            { BR::Torch, 20, 18, 1, "torch" },
            { BR::RedstoneDust, 20, 19, 1, "dust" },
            { BR::RedstoneTorch, 20, 20, 1, "rtorch" },
            { BR::Lever, 20, 21, 1, "lever" },
            { BR::Repeater, 20, 22, 1, "repeater" },
            { BR::Rail, 20, 23, 1, "rail" },
            { BR::GoldenRail, 20, 24, 1, "grail" },
            { BR::DetectorRail, 20, 25, 1, "drail" },
            { BR::Ladder, 20, 26, 1, "ladder" },
            { BR::StoneButton, 20, 27, 1, "sbutton" },
            { BR::WoodButton, 20, 28, 1, "wbutton" },
            { BR::SnowLayer, 20, 29, 1, "snowlayer" },
            { BR::Cactus, 34, 18, 1, "cactus" },
            { BR::Cobweb, 34, 19, 1, "cobweb" },
            { BR::Sapling, 34, 20, 1, "sapling" },
            { BR::FlowerRed, 34, 21, 1, "flower" },
            { BR::Sugarcane, 34, 22, 1, "cane" },
            { BR::WheatCrop, 34, 23, 1, "wheat" },
            { BR::TallGrass, 34, 24, 1, "tallgrass" },
            { BR::Pumpkin, 34, 25, 1, "pumpkin" },
            { BR::Melon, 34, 26, 1, "melon" },
            { BR::Water, 34, 27, 0, "water" },
            { BR::Lava, 34, 28, 0, "lava" },
            { BR::Leaves, 34, 29, 0, "leaves" },
            { BR::Fire, 34, 30, 0, "fire" },
            { BR::Cake, 34, 31, 0, "cake" },
        };
        for (const Member &m : members) {
            w.setBlock(m.x - 1, y0 + 1, m.z, BR::Piston, 5);
            w.setBlock(m.x, y0 + 1, m.z, m.id, 0);
            const int base = dropCount;
            const bool pushed = w.tryPistonExtend(m.x - 1, y0 + 1, m.z);
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const bool destroyed = pushed && w.blockAt(m.x, y0 + 1, m.z) == BR::PistonHead;
            ok = ok && destroyed;
            if (!destroyed)
                diag += QStringLiteral("[%1 destroyed=%2]").arg(QLatin1String(m.tag)).arg(pushed);
            ok = ok && dropCount == base + m.drops;
            if (dropCount != base + m.drops)
                diag += QStringLiteral("[%1 drops=%2 want=%3]").arg(QLatin1String(m.tag))
                            .arg(dropCount - base).arg(m.drops);
        }
        // J=2 表值直读补面（孤儿清除机器先占的暗渊门面 + 头块——独立放置面不存在的成员在此验表值）。
        ok = ok && BR::materialPushResponse(BR::AbyssGateSurface) == 2
            && BR::materialPushResponse(BR::PistonHead) == 2
            && BR::materialPushResponse(BR::EmberGate) == 2
            && BR::materialPushResponse(BR::PistonMoving) == 2;
        // J=0 缺省面直读（活塞本体族 era 活塞分支旁路 = 表缺省编码 + 显著非成员三员）。
        ok = ok && BR::materialPushResponse(BR::Piston) == 0
            && BR::materialPushResponse(BR::StickyPiston) == 0
            && BR::materialPushResponse(BR::StonePressurePlate) == 0
            && BR::materialPushResponse(BR::Snow) == 0;

        // J=0 显著非成员回归（era 字节定谳可推面——「薄块一概 Destroy」过修禁手，A05 逐类考定：
        //   压力板 = era wx 注册点实参 p.e/p.d = J=0 可推）。
        struct Movable { BR::Id id; int x, z; const char *tag; };
        const Movable movables[] = {
            { BR::Snow, 20, 33, "snow" },
            { BR::GlassPane, 20, 34, "pane" },
            { BR::StonePressurePlate, 20, 35, "plate" },
            { BR::Glass, 20, 36, "glass" },
            { BR::Wool, 20, 37, "wool" },
        };
        for (const Movable &mv : movables) {
            w.setBlock(mv.x - 2, y0 + 1, mv.z, BR::Piston, 5);
            w.setBlock(mv.x - 1, y0 + 1, mv.z, BR::Stone, 0);
            w.setBlock(mv.x, y0 + 1, mv.z, mv.id, 0);
            const int base = dropCount;
            const bool pushed = w.tryPistonExtend(mv.x - 2, y0 + 1, mv.z);
            w.tickPistonAnimations();
            w.tickPistonAnimations();
            const bool movedIntact = pushed
                && w.blockAt(mv.x - 1, y0 + 1, mv.z) == BR::PistonHead
                && w.blockAt(mv.x, y0 + 1, mv.z) == BR::Stone
                && w.blockAt(mv.x + 1, y0 + 1, mv.z) == mv.id;
            ok = ok && movedIntact;
            if (!movedIntact)
                diag += QStringLiteral("[%1 moved=%2]").arg(QLatin1String(mv.tag)).arg(pushed);
            ok = ok && dropCount == base;
            if (dropCount != base)
                diag += QStringLiteral("[%1 drops=%2]").arg(QLatin1String(mv.tag)).arg(dropCount - base);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2110d material map column (every destroy family member answers the destroy"
               " once face when terminating a push and the zero drop members fluids leaves"
               " fire and cake answer the destroy face with zero drops, and the movable"
               " members snow glass pane stone pressure plate glass and wool are pushed"
               " intact as the era byte verdicts hold for the significant non members)"
            << (ok ? QString() : diag);
    });

    // ── r2110e:结构钉族柱（J 表三面行 + 推动机 J 行族 + 拉回 J 行 + 前序 NEG 八面保全复钉 +
    //     工件在盘 + 哨兵钉 + 词元零命中 + QML 门）────────────────────────────────────────────
    //   NEG-1/NEG-2 双敏感腿（真实掉落行钉 / J=2 行钉各由本腿持有）。
    runLeg("r2110e structure pins column (the block registry carries the material push"
        " response declaration and the destroy family case rows and the block response case"
        " rows, the world layer carries the push scan j row and the destroy terminate face"
        " and the real drop row and the pull mirror j row, the eight prior negative faces"
        " stay pinned across the excitation call and the push limit and the pull source"
        " clear and the sticky recipe and the settle row and the save piston part and the"
        " rod placeholder and the beat emission, the full enumeration artifact exists non"
        " empty carrying the fourteen instance list and both reversal chains, the id and"
        " atlas sentinels stay put, the source tree stays free of the cross task filter"
        " token, and the gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonMaterialPins();
        // (R1) blockregistry 声明/表行族（J 表声明 + Destroy/Block 两组 case 行）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("push response decl",
                                                      "static int materialPushResponse(quint8 blockId);", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        const QStringList missBrC = pinSet(root + QStringLiteral("/Core/blockregistry.cpp"),
                                           {
                                               SrcPin("push response def",
                                                      "int BlockRegistry::materialPushResponse(quint8 blockId)", 1),
                                               SrcPin("destroy water case", "case Water:", 1),
                                               SrcPin("destroy cobweb case", "case Cobweb:", 1),
                                               SrcPin("destroy snowlayer case", "case SnowLayer:", 1),
                                               SrcPin("destroy cake case", "case Cake:", 1),
                                               SrcPin("block head case", "case PistonHead:", 1),
                                               SrcPin("block gate case", "case EmberGate:", 1),
                                               SrcPin("block surface case", "case AbyssGateSurface:", 1),
                                               SrcPin("store predicate body",
                                                      "bool BlockRegistry::isStoreBlock(quint8 blockId)", 1),
                                           });
        ok = ok && missBrC.isEmpty();
        if (!missBrC.isEmpty())
            diag += QStringLiteral("[brc %1]").arg(missBrC.join(QLatin1Char(',')));
        // (R2) world 层行族（推动机 J 行 + Destroy 终止面 + 真实掉落行[NEG-1 靶] + J=2 行[NEG-2 靶]
        //     + 拉回 J 行 + 前序 NEG 八面保全复钉[r2105×2 / r2106 拉回腾空 / r2107×2 / r2108×2]）。
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("push j row",
                                                        "const int pushJ = BlockRegistry::materialPushResponse(id);", 1),
                                                 SrcPin("destroy terminate face",
                                                        "termDestroy = true;", 1),
                                                 SrcPin("real drop row",
                                                        "if (termDestroy && BlockRegistry::dropId(termOldId) > 0) emit blockDroppedAsItem(cx, cy, cz, BlockRegistry::dropId(termOldId));", 1),
                                                 SrcPin("block response row",
                                                        "if (pushJ == 2)", 1),
                                                 SrcPin("pull mirror j row",
                                                        "&& BlockRegistry::materialPushResponse(pid) == 0;", 1),
                                                 SrcPin("p2105 excitation call row",
                                                        "any = pistonReceiverAt(x, y, z, b, st, powered) || any;", 1),
                                                 SrcPin("p2105 push limit row",
                                                        "if (i == BlockRegistry::PistonPushLimit) { return false; }", 1),
                                                 SrcPin("p2106 pull source clear row",
                                                        "push(px, py, pz, pulledId, BlockRegistry::Air, 0);", 1),
                                                 SrcPin("p2107 settle row",
                                                        "m_chunks.setBlock(e.x, e.y, e.z, e.storedId, e.storedState);", 1),
                                                 SrcPin("p2108 rod placeholder row",
                                                        "push(x, y, z, bodyId, BlockRegistry::PistonMoving,", 1),
                                                 SrcPin("p2108 beat emission row",
                                                        "emitSweep(e.x, e.y, e.z, e.facing, kBeatPush, false);", 1),
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        // (R2b) savecoordinator 步⑥c 行载体（p2107 NEG-2 靶行在 savecoordinator 层）。
        const QStringList missSave = pinSet(root + QStringLiteral("/World/savecoordinator.cpp"),
                                            {
                                                SrcPin("p2107 save piston part row",
                                                       "m_store->writePistonAnimsPart(req.pistonAnims)", 1),
                                            });
        ok = ok && missSave.isEmpty();
        if (!missSave.isEmpty())
            diag += QStringLiteral("[save %1]").arg(missSave.join(QLatin1Char(',')));
        // (R3) recipe.cpp 粘性配方行族载体（p2106 NEG-2 靶行在 recipe 层——本腿载体面复钉）。
        const QStringList missRec = pinSet(root + QStringLiteral("/Game/recipe.cpp"),
                                           {
                                               SrcPin("sticky recipe row",
                                                      "{ RecipeRegistry::SlimeBallId, 0, 0,", 1),
                                           });
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty())
            diag += QStringLiteral("[rec %1]").arg(missRec.join(QLatin1Char(',')));
        // (R4) t1140 全枚举工件钉（唯一权威在盘非空：14 实例清单 + 三翻案主面链锚 + J 计数节锚）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const QString fullArt = bdir + QStringLiteral("/t1140_jar_material_map.txt");
        const bool artOk = fileExistsNonEmptyPistonMaterial(fullArt)
            && rawContainsPistonMaterial(fullArt, QByteArray("g,h,i,j,k,n,p,s,u,w,y,z,B,C"))
            && rawContainsPistonMaterial(fullArt, QByteArray("new mw(aav.b).m()"))
            && rawContainsPistonMaterial(fullArt, QByteArray("new p(aav.m).n()"))
            && rawContainsPistonMaterial(fullArt, QByteArray("new bk(aav.b).n()"))
            && rawContainsPistonMaterial(fullArt, QByteArray("J=1（Destroy）恰 14"));
        ok = ok && artOk;
        if (!artOk) diag += QStringLiteral("[artifact]");
        // (R5) 哨兵钉（零新方块零新贴图——Count 166 / AtlasTileCount 213 不动）。
        const QStringList missSent = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                            {
                                                SrcPin("count sentinel", "Count           = 166,", 1),
                                                SrcPin("atlas sentinel", "AtlasTileCount = 213;", 1),
                                            });
        ok = ok && missSent.isEmpty();
        if (!missSent.isEmpty())
            diag += QStringLiteral("[sent %1]").arg(missSent.join(QLatin1Char(',')));
        // (R6) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(root,
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonMaterial(it.next(), QByteArray("r2110"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (R7) QML 玩法路径零迁移负面门（r2090d/r2105d/r2106e 同门复钉）。
        {
            const QString qml = root + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsPistonMaterial(qml, QByteArray("GameSession"))
                && !rawContainsPistonMaterial(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonMaterial(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonMaterial(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonMaterial(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2110e structure pins column (the block registry carries the material push"
               " response declaration and the destroy family case rows and the block response"
               " case rows, the world layer carries the push scan j row and the destroy"
               " terminate face and the real drop row and the pull mirror j row, the eight"
               " prior negative faces stay pinned across the excitation call and the push"
               " limit and the pull source clear and the sticky recipe and the settle row and"
               " the save piston part and the rod placeholder and the beat emission, the full"
               " enumeration artifact exists non empty carrying the fourteen instance list"
               " and both reversal chains, the id and atlas sentinels stay put, the source"
               " tree stays free of the cross task filter token, and the gameplay path"
               " literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
