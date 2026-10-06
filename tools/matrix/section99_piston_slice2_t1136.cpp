#include "matrix_helpers.h"

// t1136 活塞切片二探针段（5 腿；filter 词 r2106；矩阵 966→971）。置尾先例沿用（接 section98，
//   runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界红石 rig 直驱接收器 / 缩回机 / 配方
//   匹配，余纯源钉 / 工件钉 / 资产钉腿）。
//
// ── 单性裁定：切片二交付单（t1134 切分序第二片；交付面 = 粘性活塞 id 165 + 头块 163 + 移动占位
//    164 三 id 段尾追加（era 29/34/36 与引擎床段冲突 → 尾部追加存档契约 + static_assert 四钉）+
//    粘性拉回瞬时段（era 拉回块两拍动画 = 切片三）+ 伸/缩态贴图差（t1135 简化③收口，瓦 212）+
//    头块渲染（era 台面盒/专用渲染 = 整立方近似登记）+ 两行合成（era sl.txt 定谳工件在案）+
//    伸缩两声（era 两声名同位，引擎原创声名三面注册）+ 拒推族第 12 员（移动占位 = isStoreBlock
//    引擎容器等价面））。两拍动画 / 移动占位动画承载 / 实体位移 / 孤儿头恢复（era acu.d 面）=
//    切片三四候选池，本单零触达。
//
// ── era 定谳锚（本单工件 build/t1136_jar_crafting_piston.txt + t1136_jar_head_moving.txt 在盘；
//    t1134 工件三件复用）──
//  · 拉回（t1134 工件 2/3 phase1 解码注）：era 缩回 = 头格当拍清空（无动程）+ 粘性拉回源格
//    （本体+2Δ）可拉判定（canPush destroyMode=false：流体/黑曜石/容器族/硬度-1/已伸活塞拒）→
//    拉回块两拍动画（切片三）——本切片拉回瞬时段 = 当拍搬回。
//  · 合成（sl.txt offset 3450/3546）：活塞 "TTT"/"#X#"/"#R#"（T=木板 yy.x / #=圆石 yy.w[era 旧名
//    stonebrick] / X=ingotIron / R=redstone → 1）；粘性 "S"/"P" 1×2 竖列（S=acy.aL slimeball 上 /
//    P=yy.Z 活塞下 → 1）。
//  · 头块/占位（acu.txt/qz.txt ctor）：acu(34,107) 硬度 0.5f 材料 p.D 破坏零掉落 a(Random)=0；
//    qz(36) extends ba（容器派生族第 12 员自动生效面）硬度 -1.0f 零掉落。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；摘行均单行完整语句编译绿）──
//  NEG-1 = 双手工 Edit 摘 world.cpp 粘性拉回源格腾空行（push(px, py, pz, pulledId,
//    BlockRegistry::Air, 0); 单行完整语句；pulledId/state 仍被上行头格收口读用 → 零未用告警）
//    → 恰红 = {r2106a, r2106e}（拉回源格腾空子断言翻红 [拉回退化为复制：头格承接但源格不腾空] +
//    结构钉族源格腾空行钉失配）。r2105c/r2106b/c/d 豁免（头格收口行在位——清头/缩回位/非粘性
//    头消失两面不受摘行影响）。
//  NEG-2 = 双手工 Edit 摘 recipe.cpp 粘性活塞配方行（kRecipes 表五行结构体条目整体摘除——单条目
//    完整语句编译绿）→ 恰红 = {r2106c, r2106e}（粘性配方命中子断言翻 nullptr + 结构钉族配方行钉
//    失配）。r2106a/b/d 豁免（拉回 / 头块 / 贴图音名零触达配方表）。
//  双还原 = NEG 后双手工 Edit 逐字还原原行（代码行手工 Edit 还原 + md5 复核；还原后各带实跑）。
namespace {

// 源钉根路径（section96..98 同门：applicationDirPath/../src）。
inline QString srcRootForPistonSlice2Pins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体 / kMc 注释行 / .py / CMake 锚——pinSet 剥注释会失配，section72..98 同款）。
inline bool rawContainsPistonSlice2(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonSlice2(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 造一块石坪 + 清上空（section98 同款四 setter）。
inline void initPistonSlice2World(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 清空 rig 盒（四 setter 触发 worldgen——先清后铺石坪；盒域 = 行为腿布局域 + 界格余量）。
inline void clearPistonSlice2Box(World &w, int y0)
{
    for (int x = 16; x <= 40; ++x)
        for (int y = y0; y <= y0 + 6; ++y)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y, z, BR::Air, 0);
}

} // namespace

void MatrixRun::section99_piston_slice2_t1136()
{
    // ── r2106a:粘性拉回柱（真 rig 伸→失电→拉回两拍动画 + 零线清头 + 不可拉清头 + 信号沿计数）──
    //   NEG-1 敏感腿（源格腾空行摘除 → 拉回退化为复制，源格腾空子断言翻红）。t1137 lawful 行为
    //   修订：伸程与拉回均改两拍排定动画（窗口面 + settle 面两级实核——t1136 曾当拍搬回钉）。
    //   [t1140 lawful 修订·沿革注] 拉回可拉谓词 era 镜像重建（交接单 F03/F04 + audit #29 F-1——
    //   build/t1140_jar_material_map.txt 第四节：era 可拉 = canPush(id,false) && (i()==0 || 活塞本体
    //   族)，era 拉回拒绝 J=1、头 J=2 拒拉）：本柱黑曜石不可拉子面幸存不变；J≠0 全族不可拉新面 +
    //   缩回态活塞可拉面 = r2110c 行为柱新钉（本柱零触碰 J 族成员源格）。
    runLeg("r2106a sticky pull column (a sticky piston with a lit lever extends shifting the"
        " stone line through the two beat placeholder window and loading the head block"
        " into the first cell, unpowering schedules the two beat pull animation of the"
        " first pushed stone back into the head cell clearing the source cell at once so"
        " the block moves rather than copies, the extend retract cycle answers identically on the"
        " second pass, a sticky extension into pure air retracts by clearing the head"
        " block alone with zero phantom writes, an obsidian source placed after the push"
        " is not pullable so the retract only clears the head cell and leaves the"
        " obsidian untouched, and the actuated signal answers exactly once per extend"
        " and once per retract across the whole column)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice2World(w, 1138);
        const int y0 = 40;
        clearPistonSlice2Box(w, y0); // 四 setter 触发 worldgen——先清 rig 盒再铺坪（已知空气域）
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        int extSig = 0, retSig = 0;
        QObject::connect(&w, &World::pistonActuated,
                         [&extSig, &retSig](int, int, int, bool extending) {
                             if (extending) ++extSig; else ++retSig;
                         });
        // (1) 受电伸：粘性活塞 (20,41,24) 朝 +X（facing 5）+ 石 21..22 + 拉杆 19 点燃。占位窗实核
        //     （线格+头格 = 164 + 侧表 storedId）→ 两拍 settle：21 头块 / 22..23 石。
        w.setBlock(20, y0 + 1, 24, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        const quint8 extState = w.stateAt(20, y0 + 1, 24);
        const bool extended = (extState & BR::PistonStateExtendedFlag) != 0;
        World::PistonAnimEntry probe1;
        const bool extWin = extended && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(22, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(23, y0 + 1, 24) == BR::PistonMoving
            && w.pistonAnimProbeAt(21, y0 + 1, 24, probe1)
            && probe1.storedId == BR::PistonHead && probe1.extending && probe1.beats == 2;
        ok = ok && extWin;
        if (!extWin) diag += QStringLiteral("[ext win state=%1]").arg(extState);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool extFace = w.blockAt(21, y0 + 1, 24) == BR::PistonHead
            && w.blockAt(22, y0 + 1, 24) == BR::Stone
            && w.blockAt(23, y0 + 1, 24) == BR::Stone;
        ok = ok && extFace;
        if (!extFace) diag += QStringLiteral("[ext settle]");
        // (2) 失电拉回：头格 21 落拉回动画占位（164 + storedId=石 extending=false）+ 源格 22 当拍
        //     腾空——「搬回」非「复制」（era 拉回分支字节同构；腾空当拍面 = t1136 NEG 承重面维持），
        //     两拍 settle 后 21 实体化石块。
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        const quint8 retState = w.stateAt(20, y0 + 1, 24);
        World::PistonAnimEntry probe2;
        const bool pullWin = (retState & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(22, y0 + 1, 24) == BR::Air
            && w.pistonAnimProbeAt(21, y0 + 1, 24, probe2)
            && probe2.storedId == BR::Stone && !probe2.extending && probe2.beats == 2;
        ok = ok && pullWin;
        if (!pullWin) diag += QStringLiteral("[pull win state=%1]").arg(retState);
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool pulled = w.blockAt(21, y0 + 1, 24) == BR::Stone
            && w.blockAt(22, y0 + 1, 24) == BR::Air
            && w.blockAt(23, y0 + 1, 24) == BR::Stone
            && w.pistonAnimCount() == 0;
        ok = ok && pulled;
        if (!pulled) diag += QStringLiteral("[pull settle]");
        // (3) 伸缩循环第二遍恒等（拉回幂等面：settled 线上再伸 → 头占位复落 → settle → 失电再拉回）。
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        const bool reext = (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) != 0
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving;
        ok = ok && reext;
        if (!reext) diag += QStringLiteral("[reext]");
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool repull = w.blockAt(21, y0 + 1, 24) == BR::Stone
            && w.blockAt(22, y0 + 1, 24) == BR::Air;
        ok = ok && repull;
        if (!repull) diag += QStringLiteral("[repull]");
        // (4) 零线粘性：伸入纯空气 → 头占位落贴脸格 → settle 实体化；失电 → 仅清头块零幻写
        //     （无源格拉回零登记）。[t1138 lawful 修订：失电缩当拍本体格落杆占位（era 定谳三①
        //     无条件收口——本体 164 杆占位两拍 + 位清隐式），窗实核 + 两拍排干后零在册清账。]
        w.setBlock(20, y0 + 1, 26, BR::StickyPiston, 5);
        w.setBlock(19, y0 + 1, 26, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool zeroExt = w.blockAt(21, y0 + 1, 26) == BR::PistonHead;
        ok = ok && zeroExt;
        if (!zeroExt) diag += QStringLiteral("[zeroext]");
        w.setBlock(19, y0 + 1, 26, BR::Lever, 0x00);
        w.tickRedstone();
        World::PistonAnimEntry zeroRod;
        const bool zeroRodWin = w.blockAt(20, y0 + 1, 26) == BR::PistonMoving
            && w.pistonAnimProbeAt(20, y0 + 1, 26, zeroRod)
            && zeroRod.storedId == BR::StickyPiston && !zeroRod.extending && zeroRod.beats == 2;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool zeroRet = zeroRodWin
            && w.blockAt(20, y0 + 1, 26) == BR::StickyPiston // 杆占位 settle 回写本体复位
            && w.blockAt(21, y0 + 1, 26) == BR::Air
            && w.blockAt(22, y0 + 1, 26) == BR::Air
            && w.pistonAnimCount() == 0;
        ok = ok && zeroRet;
        if (!zeroRet) diag += QStringLiteral("[zeroret]");
        // (5) 不可拉源：伸后把源格置换为黑曜石（模拟推后放置）→ 失电拉回判定拒 → 仅清头格，
        //     黑曜石原样零登记（era canPush destroyMode=false 黑曜石显式拒同构）。
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
        // (6) 信号沿计数：伸程沿 / 缩程沿各恰一次每动作（四次伸 + 四次缩：腿内 (1)(3)×2(4)(5)）。
        ok = ok && extSig == 4 && retSig == 4;
        if (extSig != 4 || retSig != 4)
            diag += QStringLiteral("[sig e=%1 r=%2]").arg(extSig).arg(retSig);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2106a sticky pull column (a sticky piston with a lit lever extends shifting the"
               " stone line through the two beat placeholder window and loading the head block"
               " into the first cell, unpowering schedules the two beat pull animation of the"
               " first pushed stone back into the head cell clearing the source cell at once so"
               " the block moves rather than copies, the extend retract cycle answers identically on the"
               " second pass, a sticky extension into pure air retracts by clearing the head"
               " block alone with zero phantom writes, an obsidian source placed after the push"
               " is not pullable so the retract only clears the head cell and leaves the"
               " obsidian untouched, and the actuated signal answers exactly once per extend"
               " and once per retract across the whole column)"
            << (ok ? QString() : diag);
    });

    // ── r2106b:头块柱（伸出态头块位+朝向镜像 / 缩回消失 / 占位拒推成员 / 注册面）────────────
    runLeg("r2106b head block column (an extended piston carries the head block one"
        " cell ahead with the facing mirrored into the head state and the extended bit"
        " clear, unpowering the piston destroys the head block while the pushed line stays"
        " settled, a moving placeholder block placed in a push line is rejected as a store"
        " family member with the region snapshot bit identical, and the registry rows"
        " answer the head and moving placeholder zero drop faces with the unbreakable"
        " moving hardness and the sticky piston dropping itself)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice2World(w, 1139);
        const int y0 = 40;
        clearPistonSlice2Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 伸出态头块位 + 朝向镜像：**非粘性**活塞 (20,41,32) 朝 +Z（facing 3）+ 石 33 + 拉杆 31
        //     点燃（非粘性承载 = 头块消失面与本腿 (2) 同门——粘性拉回搬回头格归 r2106a）。占位窗
        //     实核（t1137：头格 = 164 + storedId=163）→ 两拍 settle：头块落 33 且 state = 朝向位（3）
        //     + bit3 恒 0（era settled 头 meta 携粘性位——引擎粘性=本体 id 属性，头位冗余不取，
        //     登记简化行为钉）。
        w.setBlock(20, y0 + 1, 32, BR::Piston, 3);
        w.setBlock(20, y0 + 1, 33, BR::Stone, 0);
        w.setBlock(20, y0 + 1, 31, BR::Lever, 0x01);
        w.tickRedstone();
        World::PistonAnimEntry probeH;
        const bool headWin = w.blockAt(20, y0 + 1, 33) == BR::PistonMoving
            && w.pistonAnimProbeAt(20, y0 + 1, 33, probeH)
            && probeH.storedId == BR::PistonHead
            && (probeH.storedState & BR::PistonStateFacingMask) == 3;
        ok = ok && headWin;
        if (!headWin) diag += QStringLiteral("[headwin]");
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const quint8 headSt = w.stateAt(20, y0 + 1, 33);
        const bool headMir = w.blockAt(20, y0 + 1, 33) == BR::PistonHead
            && (headSt & BR::PistonStateFacingMask) == 3
            && (headSt & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(20, y0 + 1, 34) == BR::Stone;
        ok = ok && headMir;
        if (!headMir) diag += QStringLiteral("[headmir st=%1]").arg(headSt);
        // (2) 缩回消失：失电 → 头格清空（era 缩回无动程——头块当拍清空零掉落），线保持。
        w.setBlock(20, y0 + 1, 31, BR::Lever, 0x00);
        w.tickRedstone();
        const bool headGone = w.blockAt(20, y0 + 1, 33) == BR::Air
            && w.blockAt(20, y0 + 1, 34) == BR::Stone
            && (w.stateAt(20, y0 + 1, 32) & BR::PistonStateExtendedFlag) == 0;
        ok = ok && headGone;
        if (!headGone) diag += QStringLiteral("[headgone]");
        // (3) 移动占位拒推成员：活塞（朝 +Z = facing 3，线沿 z 铺）+ 石 + PistonMoving(164) 挡线 →
        //     拒推 + 域快照恒等（era qz extends ba 容器派生族 = 引擎 isStoreBlock 族成员——拒推集合
        //     成员自动生效面）。
        w.setBlock(20, y0 + 1, 36, BR::Piston, 3);
        w.setBlock(20, y0 + 1, 37, BR::Stone, 0);
        w.setBlock(20, y0 + 1, 38, BR::PistonMoving, 0);
        QVector<int> before;
        for (int z = 35; z <= 39; ++z)
            before.append(int(w.blockAt(20, y0 + 1, z)));
        const bool rejected = !w.tryPistonExtend(20, y0 + 1, 36);
        QVector<int> after;
        for (int z = 35; z <= 39; ++z)
            after.append(int(w.blockAt(20, y0 + 1, z)));
        const bool frozen = before == after;
        ok = ok && rejected && frozen;
        if (!rejected || !frozen)
            diag += QStringLiteral("[movingrej rej=%1 frozen=%2]").arg(rejected).arg(frozen);
        // (4) 注册面：头块/占位零掉落 + 占位不可破 + 粘性掉自身 + 头块整立方实心（era acu 零掉落
        //     a(Random)=0 / qz c(-1.0f) 定谳的注册承载面）。
        const bool regFace = BlockRegistry::def(BR::PistonHead).dropId == 0
            && BlockRegistry::def(BR::PistonMoving).dropId == 0
            && BlockRegistry::def(BR::PistonMoving).hardness < 0.0f
            && BlockRegistry::def(BR::StickyPiston).dropId == int(BR::StickyPiston)
            && BlockRegistry::def(BR::PistonHead).solid
            && BlockRegistry::def(BR::PistonHead).shape == BlockRegistry::ShapeFull;
        ok = ok && regFace;
        if (!regFace) diag += QStringLiteral("[regface]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2106b head block column (an extended piston carries the head block one"
               " cell ahead with the facing mirrored into the head state and the extended bit"
               " clear, unpowering the piston destroys the head block while the pushed line stays"
               " settled, a moving placeholder block placed in a push line is rejected as a store"
               " family member with the region snapshot bit identical, and the registry rows"
               " answer the head and moving placeholder zero drop faces with the unbreakable"
               " moving hardness and the sticky piston dropping itself)"
            << (ok ? QString() : diag);
    });

    // ── r2106c:合成柱（活塞四料行 + 粘性 1×2 竖列行 + 负例 + 板材回退 + 2×2 门）──────────────
    //   NEG-2 敏感腿（粘性配方行摘除 → 粘性命中面翻 nullptr）。
    runLeg("r2106c crafting column (three planks over cobble iron cobble over cobble redstone"
        " cobble answers one piston on the workbench grid, a slime ball over a piston in a"
        " one wide two tall column answers one sticky piston in the inventory grid and in"
        " every workbench column, the piston recipe accepts spruce planks through the"
        " plank family fallback, a gold ingot heart and a reversed sticky column and a"
        " slime less sticky column each answer no recipe, and the three by three only"
        " piston recipe stays out of the inventory grid)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 活塞行：3 板顶行 + 圆石/铁锭/圆石 + 圆石/红石/圆石 → 1 活塞（era "TTT"/"#X#"/"#R#"）。
        {
            const int g[9] = { int(BR::Planks), int(BR::Planks), int(BR::Planks),
                               int(BR::Cobble), RecipeRegistry::IronIngotId, int(BR::Cobble),
                               int(BR::Cobble), RecipeRegistry::RedstoneId,  int(BR::Cobble) };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 3);
            const bool hit = r && r->outputId == int(BR::Piston) && r->outputCount == 1
                && r->gridSize == 3 && QLatin1String(r->name) == QLatin1String("piston");
            ok = ok && hit;
            if (!hit) diag += QStringLiteral("[piston r=%1]").arg(r ? QLatin1String(r->name) : QStringLiteral("null"));
        }
        // (2) 粘性行 2×2：粘液球上 + 活塞下 1×2 竖列（era "S"/"P"；行优先网格 [0]=上 [2]=下）。
        {
            const int g[4] = { RecipeRegistry::SlimeBallId, 0, int(BR::Piston), 0 };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 2);
            const bool hit = r && r->outputId == int(BR::StickyPiston) && r->outputCount == 1
                && QLatin1String(r->name) == QLatin1String("sticky_piston");
            ok = ok && hit;
            if (!hit) diag += QStringLiteral("[sticky22 r=%1]").arg(r ? QLatin1String(r->name) : QStringLiteral("null"));
        }
        // (3) 粘性行 3×3 任意列（包围盒 1×2 平移面）：中列 / 右列同命中。
        {
            const int gm[9] = { 0, RecipeRegistry::SlimeBallId, 0,
                                0, int(BR::Piston),             0,
                                0, 0,                           0 };
            const RecipeRegistry::Recipe *rm = RecipeRegistry::match(gm, 3);
            const int gr[9] = { 0, 0, RecipeRegistry::SlimeBallId,
                                0, 0, int(BR::Piston),
                                0, 0, 0 };
            const RecipeRegistry::Recipe *rr = RecipeRegistry::match(gr, 3);
            const bool hit = rm && rm->outputId == int(BR::StickyPiston)
                && rr && rr->outputId == int(BR::StickyPiston);
            ok = ok && hit;
            if (!hit) diag += QStringLiteral("[sticky33]");
        }
        // (4) 板材族回退：顶行换云杉木板 → 匹配器第二轮规范化命中（板材族既定通配口径）。
        {
            const int g[9] = { int(BR::SprucePlanks), int(BR::SprucePlanks), int(BR::SprucePlanks),
                               int(BR::Cobble),       RecipeRegistry::IronIngotId, int(BR::Cobble),
                               int(BR::Cobble),       RecipeRegistry::RedstoneId,  int(BR::Cobble) };
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, 3);
            const bool hit = r && r->outputId == int(BR::Piston);
            ok = ok && hit;
            if (!hit) diag += QStringLiteral("[spruce]");
        }
        // (5) 负例三面：铁芯换金锭 / 粘性竖列倒置 / 缺粘液球 → 恒 nullptr。
        {
            const int g1[9] = { int(BR::Planks), int(BR::Planks), int(BR::Planks),
                                int(BR::Cobble), RecipeRegistry::GoldIngotId, int(BR::Cobble),
                                int(BR::Cobble), RecipeRegistry::RedstoneId,  int(BR::Cobble) };
            const int g2[4] = { int(BR::Piston), 0, RecipeRegistry::SlimeBallId, 0 };
            const int g3[4] = { int(BR::Piston), 0, 0, 0 };
            const bool neg = RecipeRegistry::match(g1, 3) == nullptr
                && RecipeRegistry::match(g2, 2) == nullptr
                && RecipeRegistry::match(g3, 2) == nullptr;
            ok = ok && neg;
            if (!neg) diag += QStringLiteral("[neg]");
        }
        // (6) 3×3 门：活塞行不在 2×2 背包网格命中（工作台口径）。
        {
            const int g[4] = { int(BR::Planks), int(BR::Planks),
                               int(BR::Cobble), RecipeRegistry::IronIngotId };
            const bool neg = RecipeRegistry::match(g, 2) == nullptr;
            ok = ok && neg;
            if (!neg) diag += QStringLiteral("[gate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2106c crafting column (three planks over cobble iron cobble over cobble redstone"
               " cobble answers one piston on the workbench grid, a slime ball over a piston in a"
               " one wide two tall column answers one sticky piston in the inventory grid and in"
               " every workbench column, the piston recipe accepts spruce planks through the"
               " plank family fallback, a gold ingot heart and a reversed sticky column and a"
               " slime less sticky column each answer no recipe, and the three by three only"
               " piston recipe stays out of the inventory grid)"
            << (ok ? QString() : diag);
    });

    // ── r2106d:贴图音名钉柱（伸/缩瓦分支 + 头块瓦分支 + 两声三面注册 + 工件/资产在盘）────────
    runLeg("r2106d texture and sound pin column (the mesher answers the extended inner tile"
        " on the facing face of an extended piston body and the head tile branch carries"
        " the head block facing face, the atlas count sentinel moved with the new tile,"
        " the two piston sound clips answer the three face registration with the manager"
        " methods and the load rows and the qml routing and the world signal emits and the"
        " generator anchors and the cmake resource rows, the generated wav and tile assets"
        " exist on disk non empty, and both t1136 era adjudication artifacts exist non"
        " empty carrying the recipe pattern anchors and the head and moving constructor"
        " anchors)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonSlice2Pins();
        // (T1) mesher 伸/缩瓦分支（t1135 简化③收口行）+ 头块瓦分支行。
        const QStringList missMesh = pinSet(root + QStringLiteral("/World/meshbuilder.cpp"),
                                            {
                                                SrcPin("extended tile row",
                                                       "? 212 : d.frontTile", 1),
                                                SrcPin("head tile branch",
                                                       "if (block == BlockRegistry::PistonHead) {", 1),
                                            });
        ok = ok && missMesh.isEmpty();
        if (!missMesh.isEmpty())
            diag += QStringLiteral("[mesh %1]").arg(missMesh.join(QLatin1Char(',')));
        // (T2) 图集哨兵（213 = 212 + 活塞伸出态瓦）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("atlas sentinel",
                                                      "static constexpr int AtlasTileCount = 213;", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        // (S1) 音频三面注册：声明面 / 加载行 / World 信号发射行 / QML 路由（raw 含）。
        const QStringList missAudH = pinSet(root + QStringLiteral("/Audio/audiomanager.h"),
                                            {
                                                SrcPin("play extend decl",
                                                       "Q_INVOKABLE void playPistonExtend();", 1),
                                                SrcPin("play retract decl",
                                                       "Q_INVOKABLE void playPistonRetract();", 1),
                                            });
        ok = ok && missAudH.isEmpty();
        if (!missAudH.isEmpty())
            diag += QStringLiteral("[audh %1]").arg(missAudH.join(QLatin1Char(',')));
        const QStringList missAudC = pinSet(root + QStringLiteral("/Audio/audiomanager.cpp"),
                                            {
                                                SrcPin("extend clip rows",
                                                       "pistonExtendClip", 3), // 声明 + 加载 + 播放三行（minCount 实核）
                                                SrcPin("retract clip rows",
                                                       "pistonRetractClip", 3),
                                            });
        ok = ok && missAudC.isEmpty();
        if (!missAudC.isEmpty())
            diag += QStringLiteral("[audc %1]").arg(missAudC.join(QLatin1Char(',')));
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("extend emit row",
                                                        "emit pistonActuated(x, y, z, true);", 1),
                                                 SrcPin("retract emit row",
                                                        "emit pistonActuated(x, y, z, false);", 1),
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        const QString mainQml = root + QStringLiteral("/ui/Main.qml");
        const bool qmlRoute = rawContainsPistonSlice2(mainQml, QByteArray("onPistonActuated"))
            && rawContainsPistonSlice2(mainQml, QByteArray("playPistonExtend"));
        ok = ok && qmlRoute;
        if (!qmlRoute) diag += QStringLiteral("[qmlRoute]");
        // (S2) 生成器锚（tools 域）+ CMake 资源行。
        const QString soundsPy = QCoreApplication::applicationDirPath()
            + QStringLiteral("/../tools/build_sounds.py");
        const bool genAnchors = rawContainsPistonSlice2(soundsPy, QByteArray("def gen_piston_extend"))
            && rawContainsPistonSlice2(soundsPy, QByteArray("def gen_piston_retract"));
        ok = ok && genAnchors;
        if (!genAnchors) diag += QStringLiteral("[genAnchors]");
        const QString cmake = QCoreApplication::applicationDirPath()
            + QStringLiteral("/../CMakeLists.txt");
        const bool cmakeRows = rawContainsPistonSlice2(cmake, QByteArray("sounds/piston_extend.wav"))
            && rawContainsPistonSlice2(cmake, QByteArray("sounds/piston_retract.wav"));
        ok = ok && cmakeRows;
        if (!cmakeRows) diag += QStringLiteral("[cmakeRows]");
        // (S3) 生成资产在盘非空（两 wav + 伸态瓦 PNG）。
        const QString base = QCoreApplication::applicationDirPath() + QStringLiteral("/..");
        const bool wavOk = fileExistsNonEmptyPistonSlice2(base + QStringLiteral("/sounds/piston_extend.wav"))
            && fileExistsNonEmptyPistonSlice2(base + QStringLiteral("/sounds/piston_retract.wav"))
            && fileExistsNonEmptyPistonSlice2(base + QStringLiteral("/textures/default_piston_extended.png"));
        ok = ok && wavOk;
        if (!wavOk) diag += QStringLiteral("[assets]");
        // (A1) era 工件两件在盘非空携锚（配方样板 + 构造器）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const QString craftArt = bdir + QStringLiteral("/t1136_jar_crafting_piston.txt");
        const bool craftOk = fileExistsNonEmptyPistonSlice2(craftArt)
            && rawContainsPistonSlice2(craftArt, QByteArray("acy.aL"))
            && rawContainsPistonSlice2(craftArt, QByteArray("3597"));
        ok = ok && craftOk;
        if (!craftOk) diag += QStringLiteral("[craftArt]");
        const QString headArt = bdir + QStringLiteral("/t1136_jar_head_moving.txt");
        const bool headOk = fileExistsNonEmptyPistonSlice2(headArt)
            && rawContainsPistonSlice2(headArt, QByteArray("acu extends yy"))
            && rawContainsPistonSlice2(headArt, QByteArray("qz extends ba"))
            && rawContainsPistonSlice2(headArt, QByteArray("c(0.5f)"))
            && rawContainsPistonSlice2(headArt, QByteArray("c(-1.0f)"));
        ok = ok && headOk;
        if (!headOk) diag += QStringLiteral("[headArt]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2106d texture and sound pin column (the mesher answers the extended inner tile"
               " on the facing face of an extended piston body and the head tile branch carries"
               " the head block facing face, the atlas count sentinel moved with the new tile,"
               " the two piston sound clips answer the three face registration with the manager"
               " methods and the load rows and the qml routing and the world signal emits and the"
               " generator anchors and the cmake resource rows, the generated wav and tile assets"
               " exist on disk non empty, and both t1136 era adjudication artifacts exist non"
               " empty carrying the recipe pattern anchors and the head and moving constructor"
               " anchors)"
            << (ok ? QString() : diag);
    });

    // ── r2106e:结构钉族柱（三 id 段位 + kMc 三行 + 谓词/拒推成员 + 缩回机行 + 合成行——
    //   NEG-1/NEG-2 摘面行本腿持有）。
    runLeg("r2106e structure pins column (the block registry carries the three tail append"
        " id selection pins and the enum rows and the moved count sentinel and the family"
        " predicate body and the store family placeholder member and the material group"
        " family row and the three definition rows and the three mapping rows, the world"
        " layer carries the retract machine and the receiver call row and the head write"
        " rows and the pullable predicate and the pull source clear row, the recipe layer"
        " carries the sticky row and its product anchor assert, the palette and icon and"
        " qml tab rows are wired, the source tree stays free of the cross task filter"
        " token, and the gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonSlice2Pins();
        // (R1) blockregistry.h 注册行族（三 static_assert + 三枚举行 + Count 哨兵 + 族谓词体）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("head id assert",
                                                      "static_assert(int(PistonHead) == 163", 1),
                                               SrcPin("moving id assert",
                                                      "static_assert(int(PistonMoving) == 164", 1),
                                               SrcPin("sticky id assert",
                                                      "static_assert(int(StickyPiston) == 165", 1),
                                               SrcPin("head enum row", "PistonHead       = 163,", 1),
                                               SrcPin("moving enum row", "PistonMoving     = 164,", 1),
                                               SrcPin("sticky enum row", "StickyPiston     = 165,", 1),
                                               SrcPin("count sentinel", "Count           = 166,", 1),
                                               SrcPin("family predicate body",
                                                      "return blockId == Piston || blockId == StickyPiston;", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        // (R2) blockregistry.cpp 表行族（kDefs 三行 + 谓词体两员 minCount + kMc 三行原始读）。
        const QStringList missBrC = pinSet(root + QStringLiteral("/Core/blockregistry.cpp"),
                                           {
                                               SrcPin("head defs row",
                                                      "{int(BlockRegistry::PistonHead),         211", 1),
                                               SrcPin("moving defs row",
                                                      "{int(BlockRegistry::PistonMoving),       211", 1),
                                               SrcPin("sticky defs row",
                                                      "{int(BlockRegistry::StickyPiston),       211", 1),
                                               SrcPin("family case rows",
                                                      "case PistonMoving:", 2), // isStoreBlock 成员 + materialGroup 族行（minCount 实核）
                                           });
        ok = ok && missBrC.isEmpty();
        if (!missBrC.isEmpty())
            diag += QStringLiteral("[brc %1]").arg(missBrC.join(QLatin1Char(',')));
        const bool kmcHead = rawContainsPistonSlice2(
            root + QStringLiteral("/Core/blockregistry.cpp"), QByteArray("/* piston_head             */ 34,"));
        const bool kmcMoving = rawContainsPistonSlice2(
            root + QStringLiteral("/Core/blockregistry.cpp"), QByteArray("/* piston_moving           */ 36,"));
        const bool kmcSticky = rawContainsPistonSlice2(
            root + QStringLiteral("/Core/blockregistry.cpp"), QByteArray("/* sticky_piston           */ 29,"));
        ok = ok && kmcHead && kmcMoving && kmcSticky;
        if (!(kmcHead && kmcMoving && kmcSticky)) diag += QStringLiteral("[kmc]");
        // (R3) world 层行族（缩回机 + 接收器调用行 + 头块写入双点 minCount + 可拉谓词 + NEG-1 靶行）。
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("retract machine",
                                                        "bool World::tryPistonRetract(int x, int y, int z, bool sticky)", 1),
                                                 SrcPin("receiver call row",
                                                        "tryPistonRetract(x, y, z, b == BlockRegistry::StickyPiston);", 1),
                                                 SrcPin("head write rows",
                                                        "BlockRegistry::PistonHead, quint8(pst & BlockRegistry::PistonStateFacingMask)", 2), // 零线 + 有线双点（minCount 实核）
                                                 SrcPin("pullable predicate",
                                                        "&& !BlockRegistry::isStoreBlock(pid)", 1),
                                                 SrcPin("pull source clear row",
                                                        "push(px, py, pz, pulledId, BlockRegistry::Air, 0);", 1),
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        // (R4) 合成层行族（NEG-2 靶行 + 产物行 + 产物锚 assert）。
        const QStringList missRec = pinSet(root + QStringLiteral("/Game/recipe.cpp"),
                                           {
                                               SrcPin("sticky recipe row",
                                                      "{ RecipeRegistry::SlimeBallId, 0, 0,", 1),
                                               SrcPin("sticky product row",
                                                      "int(BlockRegistry::StickyPiston), 1, 1, \"sticky_piston\" }", 1),
                                               SrcPin("sticky product assert",
                                                      "static_assert(int(BlockRegistry::StickyPiston)  == 165", 1),
                                           });
        ok = ok && missRec.isEmpty();
        if (!missRec.isEmpty())
            diag += QStringLiteral("[rec %1]").arg(missRec.join(QLatin1Char(',')));
        // (R5) 调色板 / 图标 / QML tab 行族。
        const QStringList missHotbar = pinSet(root + QStringLiteral("/Game/hotbar.cpp"),
                                              {
                                                  SrcPin("palette row",
                                                         "int(BlockRegistry::StickyPiston),", 1),
                                                  SrcPin("icon atlas row",
                                                         "case BlockRegistry::StickyPiston:", 1),
                                              });
        ok = ok && missHotbar.isEmpty();
        if (!missHotbar.isEmpty())
            diag += QStringLiteral("[hotbar %1]").arg(missHotbar.join(QLatin1Char(',')));
        const QString qmlInv = root + QStringLiteral("/ui/Inventory.qml");
        const bool qmlPalette = rawContainsPistonSlice2(qmlInv, QByteArray("粘性活塞"));
        ok = ok && qmlPalette;
        if (!qmlPalette) diag += QStringLiteral("[qmlPalette]");
        // (R6) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(root,
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonSlice2(it.next(), QByteArray("r2106"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (R7) QML 玩法路径零迁移负面门（r2090d/r2104d/r2105d 同门复钉）。
        {
            const QString qml = root + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsPistonSlice2(qml, QByteArray("GameSession"))
                && !rawContainsPistonSlice2(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonSlice2(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonSlice2(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonSlice2(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2106e structure pins column (the block registry carries the three tail append"
               " id selection pins and the enum rows and the moved count sentinel and the family"
               " predicate body and the store family placeholder member and the material group"
               " family row and the three definition rows and the three mapping rows, the world"
               " layer carries the retract machine and the receiver call row and the head write"
               " rows and the pullable predicate and the pull source clear row, the recipe layer"
               " carries the sticky row and its product anchor assert, the palette and icon and"
               " qml tab rows are wired, the source tree stays free of the cross task filter"
               " token, and the gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
