#include "matrix_helpers.h"

#include "worldfacade.h" // t1137 mesher 快照替换柱：captureChunkMeshSnapshot 的收窄查询入参（section32 同门）
#include "savecoordinator.h" // t1137 持久化柱：真协调层统一保存链（t1133 实体持久化柱同门）

// t1137 活塞切片三探针段（5 腿；filter 词 r2107；矩阵 971→976）。置尾先例沿用（接 section99，
//   runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界红石 rig 直驱接收器 / 推动机 / 动画
//   tick（**逻辑时间驱动**——显式调 tickPistonAnimations 推进 pending，零 wall-clock sleep，
//   r2097c/r2024b 时序 flake 裁定史同门），余纯源钉 / 工件钉 / 资产钉腿）。
//
// ── 单性裁定：切片三交付单（t1134 切分序第三片；交付面 = ①伸程两拍排定动画（t1135 简化①「瞬时
//    推动」收口）②粘性拉回块两拍动画（t1136 拉回瞬时段收口）③移动占位 164 动画写入（t1136「零
//    放置」登记简化收口）④TilePiston storedId/state 侧表（piston_anims 表纯追加——era TileEntity
//    NBT 随 chunk 档真值）⑤推动中持久化面（存档窗落动画中途 → 重载续完）⑥BUD 复刻裁定报告
//    （裁定=建议不复刻，工件 2/2 在案；实现留切片四候选池）。era 定谳翻案一项如实申报：era 缩回
//    **本体格**另有杆占位两拍（t1136「缩回无动程」边界裁定当时系中段省略号外的不完整读——t1137
//    全字节翻案 build/t1137_jar_piston_anim.txt 定谳三，引擎切片三不实现本体杆占位 = 接收器
//    「位清随写」结构承重，切片四候选池）。swept AABB 实体位移 / 重复脉冲与失败恢复硬化 /
//    推动入玩家格侵入面 = 切片四（本单零触达）。
//
// ── era 定谳锚（本单工件 build/t1137_jar_piston_anim.txt + t1137_jar_piston_persist_bud.txt 在盘；
//    t1134 工件三件复用）──
//  · 两拍时序（agb b() 全字节）：占位落位 progress=0 → 两拍各 +0.5 → 第 3 tick n>=1 settle =
//    实体化（占位格仍 36 才写）；引擎映射 = beats 2→1→settle 两 pass（Main.qml/gamesession 桥接序
//    = tickRedstone 之前一格 → 事件拍新项首拍不被消费 → 两拍满窗）。
//  · extended 位 = 动画起点即置位（abr.g 字节 51-86：tryExtend 成功先 ry.c(facing|8) 后调 phase0）；
//    头格事件拍 = 36 占位 tile(storedId=34)，settled 头块 = 实体化面。
//  · 缩回拉回（abr.txt:407-492 全字节）：头格置拉回占位 tile(storedId=pulledId, extending=false) +
//    cb 抑制窗内源格腾空；头格清空（不可拉/非粘性）当拍零动程确证维持。
//  · 持久化（agb NBT 五键 + gy chunk TileEntities 表）：动画中途随 chunk 档持久化，重载续完。
//  · BUD（abr 方法表无 updateTick + 事件入口恰邻变沿/放置两路）：纯事件驱动 = BUD 结构根。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；摘行均单行完整语句编译绿）──
//  NEG-1 = 双手工 Edit 摘 world.cpp tickPistonAnimations settle 实体化行（m_chunks.setBlock(e.x,
//    e.y, e.z, e.storedId, e.storedState); 单行完整语句；oldId 仍被 settles 收口读用 → 零未用告警；
//    settles 非空 → 侧表销账+空收口路径仍在 → 编译绿行为废）→ **恰红九腿实测量**（neg1_red 轮
//    972/9）：{r2107a, r2107b, r2107d, r2107e}（本单四面：伸程/拉回 settle 实体化面 + 持久化柱
//    重载续完面 + 结构钉族 settle 行钉）∪ {r2105a, r2105b, r2105c, r2106a, r2106b}（切片一二
//    五条既有腿的 **t1137 lawful 修订面**——修订引入的 settle 承重断言[占位窗→两拍 settle 实核]
//    同源红；未修订的 r2105d/r2106c/d/e 全绿 = 豁免面对照成立）。红面同源单因 = settle 行，
//    零第二病灶（r2107c 零 settle 依赖实绿对照）。
//  NEG-2 = 双手工 Edit 摘 savecoordinator.cpp 步⑥c 活塞动画段 if 块（writePistonAnimsPart 调用
//    条目整体摘除——单条目完整语句编译绿；t1133 步⑥b 摘行先例同门）→ 恰红 = {r2107d, r2107e}
//    （存档活塞动画段不再落盘 → 重载恢复零行 → 续完面翻红 + 结构钉族 ⑥c 行钉失配）。
//    r2107a/b/c 豁免（存档链零触达——行为柱不落盘）。
//  双还原 = NEG 后双手工 Edit 逐字还原原行（代码行手工 Edit 还原 + md5 复核；还原后各带实跑）。
namespace {

// 源钉根路径（section96..99 同门：applicationDirPath/../src）。
inline QString srcRootForPistonSlice3Pins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体 / QML / 工件锚——pinSet 剥注释会失配，section72..99 同款）。
inline bool rawContainsPistonSlice3(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonSlice3(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 造一块石坪 + 清上空（section98/99 同款四 setter）。
inline void initPistonSlice3World(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 清空 rig 盒（四 setter 触发 worldgen——先清后铺石坪；盒域 = 行为腿布局域 + 界格余量）。
inline void clearPistonSlice3Box(World &w, int y0)
{
    for (int x = 16; x <= 40; ++x)
        for (int y = y0; y <= y0 + 6; ++y)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y, z, BR::Air, 0);
}

} // namespace

void MatrixRun::section100_piston_slice3_t1137()
{
    // ── r2107a:伸程动画柱（占位窗字段实核 → 两拍 settle 实体化 → 二循环恒等）──────────────
    //   NEG-1 敏感腿（settle 行摘除 → 占位永不实体化，settle 子断言翻红）。
    runLeg("r2107a extension animation column (a piston facing plus x extends a three stone"
        " line writing the moving placeholder window with the side table carrying the head"
        " stored id and the pushed stones at two beats, the first animation tick advances"
        " the beats without settling, the second animation tick settles every placeholder"
        " into its stored block and drains the side table, and a second rig answers the"
        " same window and settle faces identically)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice3World(w, 1140);
        const int y0 = 40;
        clearPistonSlice3Box(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 推动机直驱 → 占位窗：线格+头格 = 164；侧表四项（头 storedId=163 / 线 storedId=石，
        //     全 extending=true beats=2 facing=5）；extended 位零触达（归接收器，t1135 口径维持）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(23, y0 + 1, 24, BR::Stone, 0);
        const bool pushOk = w.tryPistonExtend(20, y0 + 1, 24);
        World::PistonAnimEntry ph, pl;
        const bool win = pushOk
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(22, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(23, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(24, y0 + 1, 24) == BR::PistonMoving
            && w.pistonAnimProbeAt(21, y0 + 1, 24, ph)
            && ph.storedId == BR::PistonHead && ph.extending && ph.beats == 2
            && (ph.facing & BR::PistonStateFacingMask) == 5
            && w.pistonAnimProbeAt(22, y0 + 1, 24, pl)
            && pl.storedId == BR::Stone && pl.extending && pl.beats == 2
            && w.pistonAnimCount() == 4;
        ok = ok && win;
        if (!win) diag += QStringLiteral("[win n=%1]").arg(w.pistonAnimCount());
        // (2) 首拍：递减不 settle（beats 2→1，占位维持，侧表满额）。
        w.tickPistonAnimations();
        World::PistonAnimEntry ph1;
        const bool beat1 = w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.pistonAnimProbeAt(21, y0 + 1, 24, ph1) && ph1.beats == 1
            && w.pistonAnimCount() == 4;
        ok = ok && beat1;
        if (!beat1) diag += QStringLiteral("[beat1]");
        // (3) 次拍 settle：逐占位实体化 storedId/state 原样 + 侧表清空（era agb b() 终相同构——
        //     **settle 实体化面 = NEG-1 摘行靶行为面**）。
        w.tickPistonAnimations();
        const quint8 headSt = w.stateAt(21, y0 + 1, 24);
        const bool settled = w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && (headSt & BR::PistonStateFacingMask) == 5
            && (headSt & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone
            && w.blockAt(24, y0 + 1, 24) == BR::Stone
            && w.pistonAnimCount() == 0;
        ok = ok && settled;
        if (!settled) diag += QStringLiteral("[settle]");
        // (4) 二循环恒等（第二台 fresh rig 同窗同 settle 面——动画机幂等面）。
        w.setBlock(20, y0 + 1, 26, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 26, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 26, BR::Stone, 0);
        const bool push2 = w.tryPistonExtend(20, y0 + 1, 26);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool cycle2 = push2
            && w.blockAt(21, y0 + 1, 26) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 26) == BR::Stone
            && w.blockAt(23, y0 + 1, 26) == BR::Stone
            && w.pistonAnimCount() == 0;
        ok = ok && cycle2;
        if (!cycle2) diag += QStringLiteral("[cycle2]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2107a extension animation column (a piston facing plus x extends a three stone"
               " line writing the moving placeholder window with the side table carrying the head"
               " stored id and the pushed stones at two beats, the first animation tick advances"
               " the beats without settling, the second animation tick settles every placeholder"
               " into its stored block and drains the side table, and a second rig answers the"
               " same window and settle faces identically)"
            << (ok ? QString() : diag);
    });

    // ── r2107b:拉回动画柱（真 rig 伸→失电→拉回占位窗 + 源格当拍腾空 + settle 实体化 +
    //   零线/不可拉零登记）────────────────────────────────────────────────────────────────
    //   NEG-1 敏感腿（settle 行摘除 → 拉回块永不实体化，pull settle 子断言翻红）。
    runLeg("r2107b pull animation column (a sticky piston with a lit lever extends through"
        " the two beat window and settles, unpowering schedules the pull animation with the"
        " head cell holding the pulled stone placeholder while the source cell empties at"
        " once, two animation ticks settle the pulled stone into the head cell and drain the"
        " side table, a non sticky retract clears the head cell at once with zero animation"
        " entries, and an obsidian source is not pullable so the retract registers nothing)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice3World(w, 1141);
        const int y0 = 40;
        clearPistonSlice3Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int extSig = 0, retSig = 0;
        QObject::connect(&w, &World::pistonActuated,
                         [&extSig, &retSig](int, int, int, bool extending) {
                             if (extending) ++extSig; else ++retSig;
                         });
        // (1) 伸程（真 rig 拉杆）：占位窗 → 两拍 settle（21 头块 / 22..23 石）。
        w.setBlock(20, y0 + 1, 24, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool extFace = (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) != 0
            && w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone;
        ok = ok && extFace;
        if (!extFace) diag += QStringLiteral("[ext]");
        // (2) 失电拉回：头格 = 拉回占位（164 + storedId=石 extending=false beats=2）+ 源格 22
        //     **当拍腾空**（era cb 抑制窗内 world.g 同构——腾空当拍面 = t1136 NEG 承重面维持）。
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        World::PistonAnimEntry pp;
        const bool pullWin = (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(22, y0 + 1, 24) == BR::Air
            && w.pistonAnimProbeAt(21, y0 + 1, 24, pp)
            && pp.storedId == BR::Stone && !pp.extending && pp.beats == 2;
        ok = ok && pullWin;
        if (!pullWin) diag += QStringLiteral("[pullwin]");
        // (3) 两拍 settle：拉回块实体化进头格 + 侧表清空（**NEG-1 摘行靶行为面之二**）。
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool pulled = w.blockAt(21, y0 + 1, 24) == BR::Stone
            && w.blockAt(22, y0 + 1, 24) == BR::Air
            && w.pistonAnimCount() == 0;
        ok = ok && pulled;
        if (!pulled) diag += QStringLiteral("[pullsettle]");
        // (4) 零线粘性失电：头块当拍清空（era 缩回头格零动程确证面——非拉回路径零拉回动画项）。
        //     [t1138 lawful 修订：失电缩当拍本体格落杆占位（era 定谳三①无条件收口——本体 164 杆
        //     占位两拍 + 位清隐式），窗实核 + 两拍排干后零在册清账。]
        w.setBlock(20, y0 + 1, 26, BR::StickyPiston, 5);
        w.setBlock(19, y0 + 1, 26, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool zeroExt = w.blockAt(21, y0 + 1, 26) == BR::PistonHead;
        w.setBlock(19, y0 + 1, 26, BR::Lever, 0x00);
        w.tickRedstone();
        World::PistonAnimEntry zeroRod;
        const bool zeroRodWin = w.blockAt(20, y0 + 1, 26) == BR::PistonMoving
            && w.pistonAnimProbeAt(20, y0 + 1, 26, zeroRod)
            && zeroRod.storedId == BR::StickyPiston && !zeroRod.extending && zeroRod.beats == 2;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool zeroRet = zeroExt && zeroRodWin
            && w.blockAt(20, y0 + 1, 26) == BR::StickyPiston // 杆占位 settle 回写本体复位
            && w.blockAt(21, y0 + 1, 26) == BR::Air
            && w.pistonAnimCount() == 0;
        ok = ok && zeroRet;
        if (!zeroRet) diag += QStringLiteral("[zeroret]");
        // (5) 不可拉源：失电仅清头格，黑曜石原样零登记（era canPush destroyMode=false 黑曜石显式拒）。
        //     [t1138 lawful 修订：同 (4) 杆占位窗实核 + 排干——不可拉面登记仍零（杆占位项除外）。]
        w.setBlock(20, y0 + 1, 28, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 28, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 28, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 28, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(22, y0 + 1, 28, BR::Obsidian, 0);
        w.setBlock(19, y0 + 1, 28, BR::Lever, 0x00);
        w.tickRedstone();
        World::PistonAnimEntry nopullRod;
        const bool nopullRodWin = w.blockAt(20, y0 + 1, 28) == BR::PistonMoving
            && w.pistonAnimProbeAt(20, y0 + 1, 28, nopullRod)
            && nopullRod.storedId == BR::StickyPiston && !nopullRod.extending;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool nopull = nopullRodWin
            && w.blockAt(20, y0 + 1, 28) == BR::StickyPiston
            && w.blockAt(21, y0 + 1, 28) == BR::Air
            && w.blockAt(22, y0 + 1, 28) == BR::Obsidian
            && w.pistonAnimCount() == 0;
        ok = ok && nopull;
        if (!nopull) diag += QStringLiteral("[nopull]");
        // (6) 信号沿：伸程/缩程各恰一次每动作（三次伸 + 三次缩——era phase0 动画起点发声 /
        //     phase1 缩程发声相位真值维持）。
        ok = ok && extSig == 3 && retSig == 3;
        if (extSig != 3 || retSig != 3)
            diag += QStringLiteral("[sig e=%1 r=%2]").arg(extSig).arg(retSig);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2107b pull animation column (a sticky piston with a lit lever extends through"
               " the two beat window and settles, unpowering schedules the pull animation with the"
               " head cell holding the pulled stone placeholder while the source cell empties at"
               " once, two animation ticks settle the pulled stone into the head cell and drain the"
               " side table, a non sticky retract clears the head cell at once with zero animation"
               " entries, and an obsidian source is not pullable so the retract registers nothing)"
            << (ok ? QString() : diag);
    });

    // ── r2107c:中窗语义柱（短 OFF 被吞 + 占位自愈 + mesher 快照替换面）────────────────────
    //   双 NEG 豁免腿（**零 settle 依赖**——(1) 断言排定存活与侧表清账 / (2) 断言自愈无幻写 /
    //   (3) 断言快照层替换，全部 settle 面留归 a/b/d——恰红归因的对照半边）。
    runLeg("r2107c mid window semantics column (a short off pulse during the extension"
        " window is swallowed era natively so the retract clears the head placeholder at"
        " once while the line schedule survives the pulse and drains without cancelling, a"
        " placeholder cell overwritten mid window self heals by dropping its entry without"
        " materializing over the foreign block, and the mesher snapshot answers the stored"
        " block in place of the moving placeholder id)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice3World(w, 1142);
        const int y0 = 40;
        clearPistonSlice3Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 短 OFF 被吞（era 排定不可撤销 native 面）：受电起窗 → 失电中窗缩回（头格占位当拍
        //     清 + 位置清）→ **线排定项存活不撤销**（era 无撤销逻辑——t1130 排定不可撤销同门；
        //     settle 面归 a/b，本柱断言排定存活 + 两拍清账 + 头格保持空）。
        w.setBlock(20, y0 + 1, 24, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        World::PistonAnimEntry s1, s2;
        const bool winUp = w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.pistonAnimCount() == 3;
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        const bool scheduleSurvives = w.pistonAnimProbeAt(22, y0 + 1, 24, s1)
            && w.pistonAnimProbeAt(23, y0 + 1, 24, s2)
            && s1.storedId == BR::Stone && s2.storedId == BR::Stone;
        const bool midRet = (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(21, y0 + 1, 24) == BR::Air;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool swallowed = winUp && scheduleSurvives && midRet
            && w.blockAt(21, y0 + 1, 24) == BR::Air
            && w.pistonAnimCount() == 0;
        ok = ok && swallowed;
        if (!swallowed)
            diag += QStringLiteral("[swallow win=%1 surv=%2 ret=%3]")
                        .arg(winUp).arg(scheduleSurvives).arg(midRet);
        // (2) 占位自愈（外力覆写面）：起窗后手写覆写一格占位 → 两拍后**覆写格不被陈旧项实体化**
        //     （era j() 即刻实体化同构的防御半边——覆写 = tile 已灭，项销账零幻写）+ 侧表清账。
        w.setBlock(20, y0 + 1, 30, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 30, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 30, BR::Stone, 0);
        const bool push2 = w.tryPistonExtend(20, y0 + 1, 30);
        w.setBlock(22, y0 + 1, 30, BR::Air, 0); // 外力覆写占位格（动画窗内编辑模拟）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool healed = push2
            && w.blockAt(22, y0 + 1, 30) == BR::Air // 覆写格保持外力结果，零幻写
            && w.pistonAnimCount() == 0;
        ok = ok && healed;
        if (!healed) diag += QStringLiteral("[heal]");
        // (3) mesher 快照替换面：占位窗内采集 → 快照格 = 侧表存储块原样（era 渲染=存储块随动的
        //     静态近似——世界栅格仍是 164，快照层替换；采集在持世界线程同门）。
        w.setBlock(20, y0 + 1, 32, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 32, BR::Stone, 0);
        const bool push3 = w.tryPistonExtend(20, y0 + 1, 32);
        ChunkMeshBakeParams bake; // terrain 默认（section17 同门）
        const ChunkMeshSnapshot snap = captureChunkMeshSnapshot(WorldFacade(w), 1, 1, bake);
        const bool snapFace = push3
            && w.blockAt(21, y0 + 1, 32) == BR::PistonMoving
            && snap.blockAtWorld(21, y0 + 1, 32) == BR::PistonHead
            && snap.blockAtWorld(22, y0 + 1, 32) == BR::Stone;
        ok = ok && snapFace;
        if (!snapFace) diag += QStringLiteral("[snap]");
        w.tickPistonAnimations();
        w.tickPistonAnimations();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2107c mid window semantics column (a short off pulse during the extension"
               " window is swallowed era natively so the retract clears the head placeholder at"
               " once while the line schedule survives the pulse and drains without cancelling, a"
               " placeholder cell overwritten mid window self heals by dropping its entry without"
               " materializing over the foreign block, and the mesher snapshot answers the stored"
               " block in place of the moving placeholder id)"
            << (ok ? QString() : diag);
    });

    // ── r2107d:持久化柱（真协调层 + 真临时库：存档窗落动画中途 → 重载续完；NEG-1/NEG-2 敏感）──
    //   NEG-1 敏感（settle 行摘除 → 重载续完面翻红——恢复后动画永不实体化）；NEG-2 敏感（⑥c 摘除
    //   → 活塞动画段零落盘 → 恢复零行 → 续完面翻红）。t1133 实体持久化柱同门（真链先例）。
    runLeg("r2107d persistence column (an extension window saved mid animation through the"
        " real coordinated save chain round trips every side table field, a fresh world"
        " replaying the same rig and restoring the saved rows answers the settled line one"
        " animation tick later exactly like the live world would have, a fresh generate"
        " drains the side table so no stale entry crosses worlds, and the exported rows"
        " match the restored rows field for field)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString db = QDir(QCoreApplication::applicationDirPath())
                               .absoluteFilePath(QStringLiteral("t1137_piston_anim_leg.sqlite"));
        QFile::remove(db);
        const int y0 = 40;
        // (1) 源世界：伸程起窗 → 首拍递减（beats=1 动画中途）→ 导出三行（头 + 线二）。
        World w;
        initPistonSlice3World(w, 1143);
        clearPistonSlice3Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        w.setBlock(20, y0 + 1, 24, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations(); // 首拍：beats 2→1（存档窗落动画中途）
        const QVariantList exported = w.exportPistonAnims();
        bool rigOk = w.pistonAnimCount() == 3 && exported.size() == 3;
        for (const QVariant &v : exported) {
            const QVariantMap m = v.toMap();
            rigOk = rigOk && m.contains(QStringLiteral("x")) && m.contains(QStringLiteral("id"))
                && m.contains(QStringLiteral("beats"))
                && m.value(QStringLiteral("beats")).toInt() == 1;
        }
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig n=%1 rows=%2]").arg(w.pistonAnimCount()).arg(exported.size());
        // (2) 统一保存链（真协调层 + 真临时库；SaveRequest.pistonAnims 载荷 = 生产 runExitSave 同形）。
        WorldStore store;
        const bool open1 = store.openWorld(db);
        store.setWorld(&w);
        SaveCoordinator coord;
        SaveRequest req;
        req.name = QStringLiteral("pisanimworld");
        req.pistonAnims = exported;
        coord.bind(&store, db);
        const SaveReceipt r1 = coord.saveAll(req);
        store.closeWorld();
        // (3) 关库重开 → 读面 → fresh 世界同 rig 重放 + 恢复行注入 → **一拍续完**（era TileEntity
        //     NBT 随 chunk 档真值：重载动画续完；beats=1 存档 → 一拍 settle = 活世界同面）。
        const bool open2 = store.openWorld(db);
        store.setWorld(&w);
        const QVariantList back = store.loadPistonAnims();
        store.closeWorld();
        World w2;
        initPistonSlice3World(w2, 1143);
        clearPistonSlice3Box(w2, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w2.setBlock(x, y0, z, BR::Stone, 0);
        w2.setBlock(20, y0 + 1, 24, BR::StickyPiston, 5);
        w2.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w2.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w2.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w2.tickRedstone(); // w2 自身起窗 beats=2 → 恢复行整体替换为存档 beats=1
        const int restored = w2.restorePistonAnims(back);
        bool fieldsOk = open1 && open2 && r1.ok() && restored == 3 && back.size() == 3;
        if (fieldsOk) {
            for (int i = 0; i < back.size() && fieldsOk; ++i) {
                const QVariantMap a = exported.at(i).toMap();
                const QVariantMap b = back.at(i).toMap();
                fieldsOk = a.value(QStringLiteral("x")) == b.value(QStringLiteral("x"))
                    && a.value(QStringLiteral("y")) == b.value(QStringLiteral("y"))
                    && a.value(QStringLiteral("z")) == b.value(QStringLiteral("z"))
                    && a.value(QStringLiteral("id")) == b.value(QStringLiteral("id"))
                    && a.value(QStringLiteral("st")) == b.value(QStringLiteral("st"))
                    && a.value(QStringLiteral("fc")) == b.value(QStringLiteral("fc"))
                    && a.value(QStringLiteral("ex")) == b.value(QStringLiteral("ex"))
                    && a.value(QStringLiteral("beats")) == b.value(QStringLiteral("beats"));
            }
        }
        w2.tickPistonAnimations(); // 续完：仅此一拍即 settle（beats=1 存档）
        const bool resumed = fieldsOk
            && w2.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w2.blockAt(22, y0 + 1, 24) == BR::Stone
            && w2.blockAt(23, y0 + 1, 24) == BR::Stone
            && w2.pistonAnimCount() == 0;
        ok = ok && resumed;
        if (!resumed) diag += QStringLiteral("[resume open=%1%2 save=%3]")
                                   .arg(open1).arg(open2).arg(r1.ok());
        // (4) 跨世界清账：世界重生成清侧表（stale 项零跨世界——cross-world 泄漏教训同门；
        //     setSeed 换值触发 generate = 公共面路径，generate 本身私有）。
        w2.setSeed(2143);
        const bool drained = w2.pistonAnimCount() == 0;
        ok = ok && drained;
        if (!drained) diag += QStringLiteral("[drain]");
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2107d persistence column (an extension window saved mid animation through the"
               " real coordinated save chain round trips every side table field, a fresh world"
               " replaying the same rig and restoring the saved rows answers the settled line one"
               " animation tick later exactly like the live world would have, a fresh generate"
               " drains the side table so no stale entry crosses worlds, and the exported rows"
               " match the restored rows field for field)"
            << (ok ? QString() : diag);
    });

    // ── r2107e:结构钉族柱（动画机行 / settle 行[NEG-1 靶] / ⑥c 行[NEG-2 靶] / 存档链四面 /
    //   桥接双驱动行 / 快照替换行 / 清账双点 / 哨兵不动钉 / 双工件锚——NEG-1/NEG-2 摘面行本腿持有）。
    runLeg("r2107e structure pins column (the world layer carries the animation tick and the"
        " settle materialize row and the moving placeholder write rows and the pull entry"
        " row and the two reset drains, the coordinator carries the piston anims part call"
        " and the bridge passthrough and the store schema write and load rows, the qml"
        " carries the export restore and tick driver rows and the session mirrors the tick,"
        " the facade and the snapshot capture carry the stored block query, the count and"
        " atlas sentinels stay unmoved, both t1137 era adjudication artifacts exist non"
        " empty carrying the animation timing and the tile entities and the update tick"
        " absence anchors, the source tree stays free of the cross task filter token, and"
        " the gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonSlice3Pins();
        // (R1) world 层行族（动画机 + settle 靶行 + 占位写行 + 拉回登记行 + 清账双点）。
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("animation tick def",
                                                        "void World::tickPistonAnimations()", 1),
                                                 SrcPin("settle materialize row",
                                                        "m_chunks.setBlock(e.x, e.y, e.z, e.storedId, e.storedState);", 1),
                                                 SrcPin("moving write rows",
                                                        "BlockRegistry::PistonMoving, animFacing", 3), // 线回写 + 零线/有线头占位双点（minCount 实核）
                                                 SrcPin("pull entry row",
                                                        "m_pistonAnims.push_back({hx, hy, hz, pulledId, pulledState,", 1),
                                                 SrcPin("reset drains",
                                                        "m_pistonAnims.clear();", 2), // generate + beginLoad 双点
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        // (R2) world.h 声明族（动画 tick Q_INVOKABLE + 侧表成员 + 探针读）。
        const QStringList missWorldH = pinSet(root + QStringLiteral("/World/world.h"),
                                              {
                                                  SrcPin("tick decl",
                                                         "Q_INVOKABLE void tickPistonAnimations();", 1),
                                                  SrcPin("side table member",
                                                         "std::vector<PistonAnimEntry> m_pistonAnims;", 1),
                                                  SrcPin("stored block query decl",
                                                         "void pistonStoredBlockAt(int x, int y, int z, quint8 &id, quint8 &st) const;", 1),
                                              });
        ok = ok && missWorldH.isEmpty();
        if (!missWorldH.isEmpty())
            diag += QStringLiteral("[worldH %1]").arg(missWorldH.join(QLatin1Char(',')));
        // (R3) 存档链四面（NEG-2 靶行 = ⑥c 调用行 + 桥透传 + 表三面）。
        const QStringList missCoord = pinSet(root + QStringLiteral("/World/savecoordinator.cpp"),
                                             {
                                                 SrcPin("piston anims part call",
                                                        "m_store->writePistonAnimsPart(req.pistonAnims)", 1),
                                             });
        ok = ok && missCoord.isEmpty();
        if (!missCoord.isEmpty())
            diag += QStringLiteral("[coord %1]").arg(missCoord.join(QLatin1Char(',')));
        const QStringList missBridge = pinSet(root + QStringLiteral("/Game/savebridge.cpp"),
                                              {
                                                  SrcPin("bridge passthrough",
                                                         "req.pistonAnims = pistonAnims;", 1),
                                              });
        ok = ok && missBridge.isEmpty();
        if (!missBridge.isEmpty())
            diag += QStringLiteral("[bridge %1]").arg(missBridge.join(QLatin1Char(',')));
        const QStringList missStore = pinSet(root + QStringLiteral("/World/worldstore.cpp"),
                                             {
                                                 SrcPin("schema row",
                                                        "CREATE TABLE IF NOT EXISTS piston_anims (", 1),
                                                 SrcPin("write part",
                                                        "INSERT INTO piston_anims (x, y, z, id, st, fc, ex, beats)", 1),
                                                 SrcPin("load row",
                                                        "SELECT x, y, z, id, st, fc, ex, beats FROM piston_anims", 1),
                                             });
        ok = ok && missStore.isEmpty();
        if (!missStore.isEmpty())
            diag += QStringLiteral("[store %1]").arg(missStore.join(QLatin1Char(',')));
        // (R4) QML 三行（导出/恢复/tick 驱动）+ 会话镜像行 + 门面/快照替换行。
        const QString mainQml = root + QStringLiteral("/ui/Main.qml");
        const bool qmlRows = rawContainsPistonSlice3(mainQml, QByteArray("theWorld.exportPistonAnims()"))
            && rawContainsPistonSlice3(mainQml, QByteArray("theWorld.restorePistonAnims"))
            && rawContainsPistonSlice3(mainQml, QByteArray("theWorld.tickPistonAnimations()"));
        ok = ok && qmlRows;
        if (!qmlRows) diag += QStringLiteral("[qmlRows]");
        const QStringList missSession = pinSet(root + QStringLiteral("/Game/gamesession.h"),
                                               {
                                                   SrcPin("session mirror",
                                                          "m_world.tickPistonAnimations();", 1),
                                               });
        ok = ok && missSession.isEmpty();
        if (!missSession.isEmpty())
            diag += QStringLiteral("[session %1]").arg(missSession.join(QLatin1Char(',')));
        const QStringList missFacade = pinSet(root + QStringLiteral("/World/worldfacade.h"),
                                              {
                                                  SrcPin("facade query",
                                                         "void pistonStoredAt(int x, int y, int z, quint8 &id, quint8 &st) const", 1),
                                              });
        ok = ok && missFacade.isEmpty();
        if (!missFacade.isEmpty())
            diag += QStringLiteral("[facade %1]").arg(missFacade.join(QLatin1Char(',')));
        const QStringList missMesh = pinSet(root + QStringLiteral("/World/meshbuilder.cpp"),
                                            {
                                                SrcPin("snapshot substitution",
                                                       "world.pistonStoredAt(wx, ly, wz, sid, sst);", 1),
                                            });
        ok = ok && missMesh.isEmpty();
        if (!missMesh.isEmpty())
            diag += QStringLiteral("[mesh %1]").arg(missMesh.join(QLatin1Char(',')));
        // (R5) 哨兵不动钉（本单零新方块：Count 166 / Atlas 213 不动——r2106d lawful 前移后现值复钉）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("count sentinel", "Count           = 166,", 1),
                                               SrcPin("atlas sentinel",
                                                      "static constexpr int AtlasTileCount = 213;", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        // (A1) era 工件双件在盘非空携锚（两拍时序 + 头旗标 / 持久化 + BUD 结构根）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const QString animArt = bdir + QStringLiteral("/t1137_jar_piston_anim.txt");
        const bool animOk = fileExistsNonEmptyPistonSlice3(animArt)
            && rawContainsPistonSlice3(animArt, QByteArray("qz.a:(IIIZZ)"))
            && rawContainsPistonSlice3(animArt, QByteArray("agb.j:()V"))
            && rawContainsPistonSlice3(animArt, QByteArray("tile.piston.in"))
            && rawContainsPistonSlice3(animArt, QByteArray("headFlag"))
            && rawContainsPistonSlice3(animArt, QByteArray("pendingTileEntities"));
        ok = ok && animOk;
        if (!animOk) diag += QStringLiteral("[animArt]");
        const QString budArt = bdir + QStringLiteral("/t1137_jar_piston_persist_bud.txt");
        const bool budOk = fileExistsNonEmptyPistonSlice3(budArt)
            && rawContainsPistonSlice3(budArt, QByteArray("String TileEntities"))
            && rawContainsPistonSlice3(budArt, QByteArray("updateTick"));
        ok = ok && budOk;
        if (!budOk) diag += QStringLiteral("[budArt]");
        // (R6) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(root,
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonSlice3(it.next(), QByteArray("r2107"))) {
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
            const bool qmlGate = !rawContainsPistonSlice3(qml, QByteArray("GameSession"))
                && !rawContainsPistonSlice3(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonSlice3(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonSlice3(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonSlice3(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2107e structure pins column (the world layer carries the animation tick and the"
               " settle materialize row and the moving placeholder write rows and the pull entry"
               " row and the two reset drains, the coordinator carries the piston anims part call"
               " and the bridge passthrough and the store schema write and load rows, the qml"
               " carries the export restore and tick driver rows and the session mirrors the tick,"
               " the facade and the snapshot capture carry the stored block query, the count and"
               " atlas sentinels stay unmoved, both t1137 era adjudication artifacts exist non"
               " empty carrying the animation timing and the tile entities and the update tick"
               " absence anchors, the source tree stays free of the cross task filter token, and"
               " the gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
