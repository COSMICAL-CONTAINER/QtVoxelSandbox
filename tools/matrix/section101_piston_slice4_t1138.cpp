#include "matrix_helpers.h"

#include "itementitymanager.h" // t1138 掉落物 swept 位移消费面（applyPistonSweeps 直调）
#include "entitymanager.h"     // t1138 mob swept 位移消费面（applyPistonSweeps 直调）

// t1138 活塞切片四（收官片）探针段（5 腿；filter 词 r2108；矩阵 976→981）。置尾先例沿用（接
//   section100，runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界红石 rig 直驱推动机 /
//   缩回机 / 动画 tick / swept 位移消费（**逻辑时间驱动**——显式 tickPistonAnimations +
//   applyPistonSweeps，零 wall-clock sleep，r2097c/r2024b 时序 flake 裁定史同门），余纯源钉 /
//   工件钉 / 资产钉腿）。交付后活塞族整族收官（t1134 切分序四片全闭）。
//
// ── 单性裁定：切片四交付面（t1137 切片四候选池七件收口六件 + BUD 裁定执行面零代码）：
//   ① swept AABB 实体位移（era agb a(float,float) 全字节——build/t1138_jar_entity_push_placement.txt
//     定谳一：拍推 0.5625×2 + settle 末推 0.25；盒 = qz.b 存储块盒偏移；实体集合 = world.b(null,盒)
//     全实体含玩家零拒推分支；ia.b(DDD) moveEntity 碰撞让位）②era 缩回本体杆占位两拍（t1137 翻案面
//     收口——本体格 36/164 杆占位两拍 + g() 位清先行全序，build/t1138_jar_retract_rod_acu.txt 定谳一；
//     引擎 = 位清随写归缩回机 + 接收器自身位清写摘除 + settle 回写本体 id+dir 隐式位清）③重复脉冲
//     与失败恢复硬化（接收器域门拒 → 位未清下拍重试；杆占位窗内受电零再伸 = era qz 格零接收同构）
//     ④推动入玩家格侵入面（era 零拒推 = 玩家同面被推——定谳一③ zx.a null 集合全实体直译）⑤era
//     acu.d 孤儿头恢复面（头破+本体已伸 → 本体掉落+清空——acu.d 全字节勘正 t1136「未伸孤儿态」
//     反读：恢复条件 = 已伸 bit8 置；acu.a 本体毁 → 头自清）⑥era 放置朝向规则定谳（abr.c(ry,IIILvi)
//     全字节——近距垂直分支 [眼位级双门 |dx|<2 且 |dz|<2 内：格下 2 格开外上推 / 头位上方下推 /
//     身体同层穿水平段] + yaw 象限水平段 = 推动朝玩家——t1135 简化④「俯仰 ±45°+推离玩家」v1 双翻
//     案，lawful 替换）。BUD 已裁定不复刻（t1137 主控落档）——本片零实现（禁触面）。
//   不复刻（t1137 主控落档）——本片零实现（禁触面）。
//
// ── era 定谳锚（本单工件 build/t1138_jar_entity_push_placement.txt + t1138_jar_retract_rod_acu.txt
//    在盘；t1134/t1136/t1137 工件族复用）──
//  · swept 位移三面：拍推盒 = era 拍一 [占位格-0.5dir, 占位格+0.5dir] ∪ 拍二 [占位格,占位格+1dir]
//    并集（引擎 [格-0.5s, 格+1] 沿朝向轴单点权威 World::pistonSweepBox）；delta 精确值 0.5625 =
//    0.5+0.0625 / 末推 0.25（二进制精确零累加误差）；缩程拉回零拍推（era b() 缩程分支零 a() 调用）
//    仅 settle 末推。
//  · 缩回全序三写：g() 位清（id 保持 meta=dir）→ 本体占位覆写（36,dir）→ 两拍 settle 回写（本体
//    id,dir 无 bit8）——settled 位清 = 隐式；接收器自身位清写摘除（era 位清在 g() 不在六参入口）。
//  · 孤儿恢复双面：acu.d = 头破+背本体已伸（bit8 置）→ 本体 dropBlockAsItem+清格；acu.a = 本体毁 →
//    头格自清零掉落。
//  · 放置朝向：era yaw0=+Z（nq 游泳推进字节）象限表 {q0:2,q1:5,q2:3,q3:4} = 所视反向（朝玩家）；
//    近距垂直分支双门 |posX-x|<2 && |posZ-z|<2 + 眼位级 d5=posY+1.82。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；摘行均完整语句编译绿）──
//  NEG-1 = 双手工 Edit 摘 world.cpp tryPistonRetract 本体杆占位写行（push(x, y, z, bodyId, ...
//    PistonMoving, ...) 完整语句——g() 位清行仍在 = 缩回态稳定，bodyId 仍被侧表登记行读用 →
//    零未用告警编译绿行为废：杆占位永不落位，extended 位清退化 g() 位清行独撑）→ **恰红五腿
//    实测量**（neg1_red 轮）：{r2108b}（本单杆占位窗实核翻红）∪ {r2108e}（结构钉族杆占位写行钉
//    失配）∪ {r2105c, r2106a, r2107b}（切片一二三三条既有腿的 **t1138 lawful 修订面**——修订引入
//    的杆占位承重断言 [窗内 blockAt==PistonMoving + 侧表探针] 同源红；r2105a/b、r2106b/c/d、
//    r2107c/d 全绿 = 豁免面对照成立——位清行幸存故缩回基态面零波及）。
//  NEG-2 = 双手工 Edit 摘 world.cpp tickPistonAnimations 拍推事件发射行（emitSweep(..., kBeatPush,
//    false); 单行完整语句——settle 拍完推行与末推行仍在 → 编译零告警行为废：拍推位移零产出）→
//    恰红 = {r2108a, r2108c, r2108e}（swept 位移柱拍推面翻红[末推 0.25 面幸存为豁免对照] + 玩家
//    侵入柱拍推面翻红 + 结构钉族拍推行钉 minCount 失配）。r2108b/d 豁免（杆占位/放置零 swept 依赖）。
//  双还原 = NEG 后双手工 Edit 逐字还原原行（代码行手工 Edit 还原 + md5 复核；还原后各带实跑）。
namespace {

// 源钉根路径（section96..100 同门：applicationDirPath/../src）。
inline QString srcRootForPistonSlice4Pins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体 / QML / 工件锚——pinSet 剥注释会失配，section72..100 同款）。
inline bool rawContainsPistonSlice4(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonSlice4(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 事件泵（section85/86 同款：m_evtClock 放置 CD 200ms → 事件泵 320ms 越窗——r2083a/r2093a 同门；
//   仅放置 CD 越窗用，动画/位移腿全逻辑时间驱动零涉）。
inline void pumpPistonSlice4(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 造一块石坪 + 清上空（section98/99/100 同款四 setter）。
inline void initPistonSlice4World(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 清空 rig 盒（四 setter 触发 worldgen——先清后铺石坪；盒域 = 行为腿布局域 + 界格余量）。
inline void clearPistonSlice4Box(World &w, int y0)
{
    for (int x = 16; x <= 40; ++x)
        for (int y = y0; y <= y0 + 6; ++y)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y, z, BR::Air, 0);
}

} // namespace

void MatrixRun::section101_piston_slice4_t1138()
{
    // ── r2108a:swept 位移柱（mob/item 嵌入提取 1.125 / 拉回 settle 末推 0.25 / 背面控制）──────
    //   NEG-2 敏感腿（拍推发射行摘除 → mob/item 拍推位移归零，末推 0.25 子面幸存为豁免对照）。
    runLeg("r2108a swept displacement column (a zero line extension extracts a mob and an item"
        " embedded in the moving placeholder cell by two pushes totalling exactly 1.125 along"
        " the facing while a control mob behind the piston never moves, a sticky pull settles"
        " with the final quarter push of 0.25 extracting an item from the head cell after a"
        " pull window that emits zero beat events, and the sweep generation advances only on"
        " producing ticks)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice4World(w, 1144);
        const int y0 = 40;
        clearPistonSlice4Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        EntityManager ents;
        ItemEntityManager items;
        const quint32 gen0 = w.pistonSweepGeneration();
        // (1) 零线伸程：活塞 (20,41,24) 朝 +X（facing 5）+ 贴脸空气 → 头占位落 21。mob 与 item
        //     嵌 21 占位格（era 被推块挤入实体同面——ia.b moveEntity 对已交叠实体不钉死的嵌入豁免
        //     承载）→ 首拍各恰 0.5625；背面 mob 26 零位移（盒背向零交叠 = era 集合零成员同面）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        const int mobFront = ents.spawnMobTyped(21, y0 + 1, 24, int(EntityManager::MobTest),
                                                QStringLiteral("#ffffff"), 10);
        const int mobBack = ents.spawnMobTyped(26, y0 + 1, 24, int(EntityManager::MobTest),
                                               QStringLiteral("#ffffff"), 10);
        items.spawnItemAt(QVector3D(21.5f, float(y0 + 1) + 0.5f, 24.5f), int(BR::Stone), 1, 0.0f, 0.0f, 0.0f);
        const QVector3D mobFront0 = ents.posAt(mobFront);
        const QVector3D mobBack0 = ents.posAt(mobBack);
        const QVector3D item0 = items.posAt(0);
        const bool pushOk = w.tryPistonExtend(20, y0 + 1, 24)
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving;
        w.tickPistonAnimations(); // 首拍：beats 2→1 + 拍推事件（era a(m, m-n+0.0625)）
        const quint32 genBeat = w.pistonSweepGeneration();
        ents.applyPistonSweeps(&w);
        items.applyPistonSweeps(&w);
        const float mobDx1 = ents.posAt(mobFront).x() - mobFront0.x();
        const float itemDx1 = items.posAt(0).x() - item0.x();
        const bool beatFace = pushOk && genBeat != gen0
            && qFuzzyCompare(mobDx1, 0.5625f) && qFuzzyCompare(itemDx1, 0.5625f)
            && ents.posAt(mobFront).y() == mobFront0.y() && ents.posAt(mobFront).z() == mobFront0.z()
            && items.posAt(0).y() == item0.y() && items.posAt(0).z() == item0.z()
            && ents.posAt(mobBack) == mobBack0;
        ok = ok && beatFace;
        if (!beatFace)
            diag += QStringLiteral("[beat mob=%1 item=%2 back=%3 gen=%4]")
                        .arg(mobDx1).arg(itemDx1)
                        .arg(QVector3D(ents.posAt(mobBack) - mobBack0).length())
                        .arg(genBeat);
        // (2) 次拍 settle：拍完推事件（0.5625——引擎 settle 拍压缩 era T+2 拍推）+ 末推事件
        //     （0.25）——mob/item 拍完推再 0.5625（盒仍交叠）→ 各累计恰 1.125，末推面零触（出盒）；
        //     占位 settle 实体化（头块 21）+ 代次再进。
        w.tickPistonAnimations(); // 次拍：settle 实体化 + 拍完推 + 末推事件
        const quint32 genSettle = w.pistonSweepGeneration();
        ents.applyPistonSweeps(&w);
        items.applyPistonSweeps(&w);
        const float mobDx2 = ents.posAt(mobFront).x() - mobFront0.x();
        const float itemDx2 = items.posAt(0).x() - item0.x();
        const bool settleFace = genSettle != genBeat
            && qFuzzyCompare(mobDx2, 1.125f) && qFuzzyCompare(itemDx2, 1.125f)
            && w.blockAt(21, y0 + 1, 24) == BR::PistonHead;
        ok = ok && settleFace;
        if (!settleFace)
            diag += QStringLiteral("[settle mob=%1 item=%2]").arg(mobDx2).arg(itemDx2);
        // (3) 零事件 tick 不 bump 代次（稳态缓冲清空零成本——消费者幂等门不误应用陈旧缓冲）。
        w.tickPistonAnimations();
        ents.applyPistonSweeps(&w);
        items.applyPistonSweeps(&w);
        const bool genStable = w.pistonSweepGeneration() == genSettle
            && qFuzzyCompare(ents.posAt(mobFront).x() - mobFront0.x(), 1.125f)
            && qFuzzyCompare(items.posAt(0).x() - item0.x(), 1.125f);
        ok = ok && genStable;
        if (!genStable) diag += QStringLiteral("[genstable]");
        // (4) 拉回 settle 末推 0.25 面（era 缩程零拍推直译）：粘性 rig 伸→settle→失电拉回，item
        //     嵌头格 → 拉回窗（首拍）零事件零位移；次拍 settle 末推恰 0.25（拉回块实体化石块于
        //     头格 + 本体杆占位同拍复位）。
        w.setBlock(20, y0 + 1, 34, BR::StickyPiston, 5);
        w.setBlock(21, y0 + 1, 34, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 34, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 34, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        items.spawnItemAt(QVector3D(21.5f, float(y0 + 1) + 0.5f, 34.5f), int(BR::Stone), 1, 0.0f, 0.0f, 0.0f);
        const QVector3D pullItem0 = items.posAt(1);
        const quint32 genPull0 = w.pistonSweepGeneration();
        w.setBlock(19, y0 + 1, 34, BR::Lever, 0x00);
        w.tickRedstone();
        w.tickPistonAnimations(); // 拉回窗首拍：缩程零拍推（era b() 缩程分支零 a() 调用直译）
        items.applyPistonSweeps(&w);
        const bool pullWindowQuiet = w.pistonSweepGeneration() == genPull0
            && items.posAt(1) == pullItem0;
        w.tickPistonAnimations(); // 次拍 settle：拉回块实体化 + 末推事件（0.25）
        items.applyPistonSweeps(&w);
        const float pullDx = items.posAt(1).x() - pullItem0.x();
        const bool pullFace = pullWindowQuiet
            && w.blockAt(21, y0 + 1, 34) == BR::Stone
            && qFuzzyCompare(pullDx, 0.25f);
        ok = ok && pullFace;
        if (!pullFace)
            diag += QStringLiteral("[pull dx=%1 quiet=%2]")
                        .arg(pullDx).arg(pullWindowQuiet);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2108a swept displacement column (a zero line extension pushes a mob standing in"
               " front of the moving placeholder exactly one beat push of 0.5625 along the facing"
               " while an item embedded in the placeholder cell is extracted by two pushes totalling"
               " 1.125 and a control mob behind the piston never moves, a sticky pull settles with"
               " the final quarter push of 0.25 extracting an item from the head cell after a pull"
               " window that emits zero beat events, and the sweep generation advances only on"
               " producing ticks)"
            << (ok ? QString() : diag);
    });

    // ── r2108b:杆占位 + 脉冲柱（era 定谳一收口：缩回本体杆占位两拍 + 隐式位清 + 占位窗零再伸 +
    //   重复 ON 零重触发 + 反复横跳 + 杆占位渲染查询面）────────────────────────────────────
    //   NEG-1 敏感腿（杆占位写行摘除 → 窗实核 blockAt==PistonMoving 翻红）。
    runLeg("r2108b rod placeholder and pulse column (unpowering a plain piston writes the body"
        " rod placeholder at the body cell for two beats with the stored id carrying the"
        " piston itself and the facing state clearing the extended bit implicitly, two"
        " animation ticks restore the body with the facing only, powering during the rod"
        " window is not received because the placeholder is outside the receiver family and"
        " the extension lands only after the settle restores the block, a repeated on pulse"
        " inside the extension window triggers nothing new, an off on oscillation inside the"
        " window retracts once and reextends only after the rod drains, and the render query"
        " answers the head plate for the rod window while the pull window answers the stored"
        " block)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice4World(w, 1145);
        const int y0 = 40;
        clearPistonSlice4Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 非粘性缩回杆占位（era 定谳一全序：g() 位清 id 保持 → 本体 36/164 杆占位两拍
        //     storedId=本体 extending=false storedMeta=dir 无 bit8）→ 两拍 settle 回写本体 id+dir
        //     = **隐式位清**（本体复位态恒无 extended 位）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        w.setBlock(21, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(22, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        World::PistonAnimEntry rod;
        const bool rodWin = w.blockAt(20, y0 + 1, 24) == BR::PistonMoving
            && w.blockAt(21, y0 + 1, 24) == BR::Air
            && (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) == 0
            && w.pistonAnimProbeAt(20, y0 + 1, 24, rod)
            && rod.storedId == BR::Piston && !rod.extending && rod.beats == 2
            && (rod.storedState & BR::PistonStateFacingMask) == 5;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const quint8 bodySt = w.stateAt(20, y0 + 1, 24);
        const bool rodSettled = rodWin
            && w.blockAt(20, y0 + 1, 24) == BR::Piston
            && (bodySt & BR::PistonStateFacingMask) == 5
            && (bodySt & BR::PistonStateExtendedFlag) == 0
            && w.pistonAnimCount() == 0;
        ok = ok && rodSettled;
        if (!rodSettled)
            diag += QStringLiteral("[rod win=%1 set=%2 st=%3]").arg(rodWin).arg(rodSettled).arg(bodySt);
        // (2) 杆占位窗受电零再伸（era qz 格零接收同构——占位 164 非接收器族）：再伸→排干→缩回起窗
        //     → 窗内受电 deaf（零伸零占位写入）→ 排干本体复位 → 下一红石 pass 受电伸落位。
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone(); // 伸（本体复位态受电）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone(); // 缩 → 杆占位窗开启
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone(); // 窗内受电 → deaf
        const bool rodWindowDeaf = w.blockAt(20, y0 + 1, 24) == BR::PistonMoving
            && (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) == 0
            && w.blockAt(21, y0 + 1, 24) == BR::Air; // 零伸零占位写入（头格仍空）
        w.tickPistonAnimations();
        w.tickPistonAnimations(); // 排干杆占位 → 本体复位（id+dir）
        w.tickRedstone();         // 复位本体 + 受电（settle notePowerWrite 脏）→ 伸
        const bool reextAfterDrain = rodWindowDeaf
            && (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) != 0
            && w.blockAt(21, y0 + 1, 24) == BR::PistonMoving;
        ok = ok && reextAfterDrain;
        if (!reextAfterDrain) diag += QStringLiteral("[deaf]");
        w.tickPistonAnimations();
        w.tickPistonAnimations(); // 排干新窗（伸程窗开着进 (3)）
        // (3) 重复 ON 脉冲（伸程窗内重复受电零重触发——era g() f(meta) 已伸短路同构）：先回缩态
        //     再受电起窗，窗内二次拉杆写同 state → 零新登记 + 恰一起始沿零重复沿。
        int extSig = 0;
        QObject::connect(&w, &World::pistonActuated,
                         [&extSig](int, int, int, bool extending) { if (extending) ++extSig; });
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone(); // 缩（本体复位态）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone(); // 受电起窗（零线单员窗）
        const int countAtWin = w.pistonAnimCount();
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01); // 重复 ON（state 同值重写）
        w.tickRedstone();
        const bool repeatQuiet = countAtWin == 1 // 零线伸程（21 空头占位单员窗）
            && w.pistonAnimCount() == countAtWin
            && extSig == 1; // 恰一起始沿 + 二次 ON 零新沿（era 成功段才发声同相位）
        ok = ok && repeatQuiet;
        if (!repeatQuiet)
            diag += QStringLiteral("[repeat n=%1->%2 sig=%3]")
                        .arg(countAtWin).arg(w.pistonAnimCount()).arg(extSig);
        // (4) 反复横跳（窗内 OFF→ON）：OFF 中窗缩回（杆占位起窗 + 线排定存活 r2107c 同面）→
        //     ON 窗内零再伸（杆占位 deaf）→ 排干后 ON 落伸 = era 原生乱象的收口形态。
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone();
        const bool oscRetracted = w.blockAt(20, y0 + 1, 24) == BR::PistonMoving;
        w.setBlock(19, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone();
        const bool oscDeaf = oscRetracted
            && w.blockAt(20, y0 + 1, 24) == BR::PistonMoving; // 杆占位窗内 ON 零伸
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.tickRedstone();
        const bool oscReext = oscDeaf
            && (w.stateAt(20, y0 + 1, 24) & BR::PistonStateExtendedFlag) != 0;
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        ok = ok && oscReext;
        if (!oscReext) diag += QStringLiteral("[osc]");
        // (5) 渲染查询面（era xd headFlag 消费直译）：杆占位窗查询答**头块台面板**（PistonHead +
        //     朝向 state）；拉回窗查询答存储块原样（石块 + storedState）——快照层替换不动栅格。
        w.setBlock(30, y0 + 1, 24, BR::StickyPiston, 5);
        w.setBlock(31, y0 + 1, 24, BR::Stone, 0);
        w.setBlock(29, y0 + 1, 24, BR::Lever, 0x01);
        w.tickRedstone(); // 伸（真 rig 单程——直驱+lever 叠加会让拉回源变头块致查询面失真）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        quint8 rid = 0, rst = 0;
        w.setBlock(29, y0 + 1, 24, BR::Lever, 0x00);
        w.tickRedstone(); // 缩：拉回窗（31 拉回占位 storedId=石）+ 杆占位窗（30 本体杆占位）
        w.pistonStoredBlockAt(31, y0 + 1, 24, rid, rst); // 拉回占位格
        const quint8 pullId = rid, pullSt = rst;
        w.pistonStoredBlockAt(30, y0 + 1, 24, rid, rst); // 杆占位格
        const bool renderFace = w.blockAt(31, y0 + 1, 24) == BR::PistonMoving
            && pullId == BR::Stone && pullSt == 0 // 拉回 tile storedMeta = 被拉块自身 meta（era meta13 直译——非朝向）
            && rid == BR::PistonHead && (rst & BR::PistonStateFacingMask) == 5
            && w.blockAt(30, y0 + 1, 24) == BR::PistonMoving; // 栅格仍 164（快照层替换面）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        ok = ok && renderFace;
        if (!renderFace)
            diag += QStringLiteral("[render pull=%1/%2 rod=%3/%4 cell31=%5]")
                        .arg(pullId).arg(pullSt).arg(rid).arg(rst)
                        .arg(w.blockAt(31, y0 + 1, 24));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2108b rod placeholder and pulse column (unpowering a plain piston writes the"
               " body rod placeholder at the body cell for two beats with the stored id carrying"
               " the piston itself and the facing state clearing the extended bit implicitly, two"
               " animation ticks restore the body with the facing only, powering during the rod"
               " window is not received because the placeholder is outside the receiver family and"
               " the extension lands only after the settle restores the block, a repeated on pulse"
               " inside the extension window triggers nothing new, an off on oscillation inside the"
               " window retracts once and reextends only after the rod drains, and the render query"
               " answers the head plate for the rod window while the pull window answers the stored"
               " block)"
            << (ok ? QString() : diag);
    });

    // ── r2108c:玩家侵入 + 孤儿头柱（era 定谳一③全实体含玩家 + acu.d/acu.a 孤儿恢复双面）────
    //   NEG-2 敏感腿（拍推发射行摘除 → 玩家拍推位移归零翻红）。
    runLeg("r2108c player invasion and orphan head column (a player standing in the sweep"
        " span is pushed exactly one beat push like any entity with zero refusal branch,"
        " breaking the head of an extended piston drops the whole body as an item and clears"
        " the body cell while the head itself drops nothing, destroying the body cell"
        " instead self clears the head with zero drops, and breaking the head of a body"
        " already retracted is plain head removal with zero recovery)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice4World(w, 1146);
        const int y0 = 40;
        clearPistonSlice4Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // (1) 玩家被推（era 零拒推分支直译）：真链 pc rig 脚位 (22,y0+1,24.5) 站零线伸程扫掠带 →
        //     首拍恰 0.5625 位移（AABB 盒交叠 + 逐轴试探 + 目标域净空）。
        w.setBlock(20, y0 + 1, 24, BR::Piston, 5);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.loadSavedState(22.0f, float(y0 + 1), 24.5f, 270.0f, 0.0f, 1 /* Creative */);
        pc.tick();
        const bool pushOk = w.tryPistonExtend(20, y0 + 1, 24);
        w.tickPistonAnimations(); // 首拍拍推事件
        pc.tick();                // 玩家面消费（tickImpl swept 段）
        const float pdx = float(pc.position().x()) - 22.0f;
        const bool playerPush = pushOk && qFuzzyCompare(pdx, 0.5625f)
            && float(pc.position().z()) == 24.5f;
        ok = ok && playerPush;
        if (!playerPush) diag += QStringLiteral("[player dx=%1]").arg(pdx);
        probeWin.deleteLater();
        pc.release();
        // (2) 孤儿头恢复（era acu.d 直译——恢复条件 = 背本体**已伸** bit8 置，t1136 反读勘正面）：
        //     伸程 settle 后破头 → 本体以物品形态掉落 + 本体格清空（头自身零掉落维持）。
        int dropCount = 0;
        int lastDropId = 0;
        QObject::connect(&w, &World::blockDroppedAsItem,
                         [&dropCount, &lastDropId](int, int, int, int id) {
                             ++dropCount;
                             lastDropId = id;
                         });
        w.setBlock(20, y0 + 1, 30, BR::Piston, 5);
        w.setBlock(19, y0 + 1, 30, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool headReady = w.blockAt(21, y0 + 1, 30) == BR::PistonHead
            && (w.stateAt(20, y0 + 1, 30) & BR::PistonStateExtendedFlag) != 0;
        w.setBlock(21, y0 + 1, 30, BR::Air, 0); // 破头（4 参主入口 → 钩子族）
        const bool orphanRecovered = headReady
            && w.blockAt(20, y0 + 1, 30) == BR::Air // 本体格清空
            && dropCount == 1 && lastDropId == int(BR::Piston); // 本体整只掉落
        ok = ok && orphanRecovered;
        if (!orphanRecovered)
            diag += QStringLiteral("[orphan ready=%1 body=%2 drops=%3/%4]")
                        .arg(headReady).arg(w.blockAt(20, y0 + 1, 30)).arg(dropCount).arg(lastDropId);
        // (3) 本体毁 → 头自清（era acu.a 直译——零掉落零信号）：伸程 settle 后清本体格 → 头格
        //     自清空气，掉落计数零增。
        w.setBlock(20, y0 + 1, 32, BR::Piston, 5);
        w.setBlock(19, y0 + 1, 32, BR::Lever, 0x01);
        w.tickRedstone();
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        const bool head2 = w.blockAt(21, y0 + 1, 32) == BR::PistonHead;
        w.setBlock(20, y0 + 1, 32, BR::Air, 0); // 毁本体
        const bool headSelfClear = head2
            && w.blockAt(21, y0 + 1, 32) == BR::Air
            && dropCount == 1; // 头自清零掉落（计数不增）
        ok = ok && headSelfClear;
        if (!headSelfClear) diag += QStringLiteral("[selfclear]");
        // (4) 缩回态（本体已复位位清）破头零恢复：伸→缩→再伸破头前先失电缩（本体位清）→
        //     手置头块再破 = 非伸态头移除零连带（era acu.d 位清 return 直译）。
        w.setBlock(20, y0 + 1, 36, BR::Piston, 5);
        w.setBlock(19, y0 + 1, 36, BR::Lever, 0x01);
        w.tickRedstone();
        w.setBlock(19, y0 + 1, 36, BR::Lever, 0x00);
        w.tickRedstone(); // 立即失电缩（本体位清——杆占位 settle 前）
        w.tickPistonAnimations();
        w.tickPistonAnimations();
        w.setBlock(21, y0 + 1, 36, BR::PistonHead, 5); // 手置孤儿头（背本体未伸）
        const int dropsBefore = dropCount;
        w.setBlock(21, y0 + 1, 36, BR::Air, 0); // 破孤儿头
        const bool plainRemoval = w.blockAt(20, y0 + 1, 36) == BR::Piston
            && dropCount == dropsBefore; // 零恢复零掉落
        ok = ok && plainRemoval;
        if (!plainRemoval) diag += QStringLiteral("[plain]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2108c player invasion and orphan head column (a player standing in the sweep"
               " span is pushed exactly one beat push like any entity with zero refusal branch,"
               " breaking the head of an extended piston drops the whole body as an item and clears"
               " the body cell while the head itself drops nothing, destroying the body cell"
               " instead self clears the head with zero drops, and breaking the head of a body"
               " already retracted is plain head removal with zero recovery)"
            << (ok ? QString() : diag);
    });

    // ── r2108d:放置朝向柱（era 定谳二全字节收口——t1135 简化④ v1 双翻案 lawful 替换面）──────
    runLeg("r2108d placement facing column (a real creative player placing a piston answers"
        " the push direction toward the player for all four horizontal look codes exactly"
        " mirroring the era yaw quadrant table, a placement two cells below the feet inside"
        " the two block near gate answers up and a placement above the head answers down, a"
        " placement at the body level inside the near gate falls through to the horizontal"
        " rule era natively, and a far horizontal placement outside the two block gate"
        " answers the yaw quadrant regardless of pitch)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonSlice4World(w, 1147);
        const int y0 = 40;
        clearPistonSlice4Box(w, y0);
        for (int x = 16; x <= 40; ++x)
            for (int z = 16; z <= 40; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Piston));
        // (1) 四水平面向 → 朝玩家反向（era yaw 象限表 {q0:2(-Z),q1:5(+X),q2:3(+Z),q3:4(-X)} 的引擎
        //     换算；r2093a 真链直驱同门）。几何 = 玩家面前两格立两高墙柱、平视命中其上层正面 →
        //     放置格 = 柱面前邻格（与玩家隔一格 = 近距双门内 [距离 1.5<2] 且不与玩家 AABB 重叠
        //     [隔格豁免]）；放置格与玩家**身体同层跨 y**（d5-cellY=0.82 零垂直命中）= era 身体
        //     同层 fall-through → 水平段面，四向共面。
        struct HWall { const char *tag; float fx, fz, yaw; int px, pz, wallDx, wallDz, want; };
        const HWall walls[4] = {
            { "look-Z", 24.5f, 21.5f,   0.0f, 24, 20,  0, -1, 3 }, // 看 -Z → 推 +Z（朝玩家）
            { "look+Z", 24.5f, 27.5f, 180.0f, 24, 28,  0,  1, 2 }, // 看 +Z → 推 -Z（朝玩家）
            { "look+X", 17.5f, 24.5f, 270.0f, 18, 24,  1,  0, 4 }, // 看 +X → 推 -X（朝玩家）
            { "look-X", 31.5f, 24.5f,  90.0f, 30, 24, -1,  0, 5 }, // 看 -X → 推 +X（朝玩家）
        };
        for (const HWall &c : walls) {
            const int wallX = c.px + c.wallDx, wallZ = c.pz + c.wallDz; // 墙柱 = 放置格外侧一格
            w.setBlock(wallX, y0 + 1, wallZ, BR::Stone, 0);
            w.setBlock(wallX, y0 + 2, wallZ, BR::Stone, 0);
            pc.loadSavedState(c.fx, float(y0 + 1), c.fz, c.yaw, 0.0f, 1 /* Creative */);
            pc.tick();
            pumpPistonSlice4(320); // 越放置 CD（r2083a/r2093a 同门）
            pc.placeBlock();
            const quint8 st = w.stateAt(c.px, y0 + 2, c.pz);
            const bool placed = w.blockAt(c.px, y0 + 2, c.pz) == quint8(BR::Piston)
                && (st & BR::PistonStateFacingMask) == quint8(c.want)
                && (st & BR::PistonStateExtendedFlag) == 0;
            ok = ok && placed;
            if (!placed)
                diag += QStringLiteral("[%1 id=%2 st=%3 want=%4]")
                            .arg(QLatin1String(c.tag)).arg(w.blockAt(c.px, y0 + 2, c.pz))
                            .arg(st).arg(c.want);
        }
        // (1b) 远距门外面（era 近距门只辖垂直分支——门外水平照走象限表 = 同答案无关俯仰）：玩家距
        //     放置格 2.5（双门外）平视贴墙 → 同朝玩家反向。
        {
            w.setBlock(24, y0 + 1, 22, BR::Stone, 0);
            w.setBlock(24, y0 + 2, 22, BR::Stone, 0);
            pc.loadSavedState(24.5f, float(y0 + 1), 25.5f, 0.0f, 0.0f, 1); // 距放置格 (24,42,23) 2.5
            pc.tick();
            pumpPistonSlice4(320); // 越放置 CD（r2083a/r2093a 同门）
            pc.placeBlock();
            const quint8 st = w.stateAt(24, y0 + 2, 23);
            const bool farFace = w.blockAt(24, y0 + 2, 23) == quint8(BR::Piston)
                && (st & BR::PistonStateFacingMask) == 3;
            ok = ok && farFace;
            if (!farFace)
                diag += QStringLiteral("[far id=%1 st=%2]").arg(w.blockAt(24, y0 + 2, 23)).arg(st);
        }
        // (2) 近距垂直上推（era d5-cellY>2 直译）：坪面开洞 + 洞下基座块 → 俯角射线穿洞命中基座
        //     顶面（= feet-2 格）→ 放置格 = 洞格（= feet-1，d5-y=2.82>2）→ 上推(1)。
        w.setBlock(20, y0, 35, BR::Air, 0);        // 坪面开洞（射线通道）
        w.setBlock(20, y0 - 1, 35, BR::Stone, 0);  // 洞下基座（顶面 y = y0-1 = 39）
        pc.loadSavedState(20.5f, float(y0 + 1), 36.5f, 0.0f, -65.0f, 1);
        pc.tick();
        pumpPistonSlice4(320); // 越放置 CD（r2083a/r2093a 同门）
        pc.placeBlock();
        quint8 vst = w.stateAt(20, y0, 35);
        const bool upFace = w.blockAt(20, y0, 35) == quint8(BR::Piston)
            && (vst & BR::PistonStateFacingMask) == 1;
        ok = ok && upFace;
        if (!upFace)
            diag += QStringLiteral("[up id=%1 st=%2]").arg(w.blockAt(20, y0, 35)).arg(vst);
        // (3) 近距垂直下推（era cellY-d5>0 直译）：目标悬空块底面 = feet+3 格（放置格 = feet+2，
        //     43-42.82=0.18>0）→ 下推(0)。仰角 +75° 命中真算（眼 42.62 → 底面 44 距 1.43）。
        w.setBlock(24, y0 + 4, 37, BR::Stone, 0); // 悬空块（底面 y = y0+4）
        pc.loadSavedState(24.5f, float(y0 + 1), 37.5f, 0.0f, 75.0f, 1);
        pc.tick();
        pumpPistonSlice4(320); // 越放置 CD（r2083a/r2093a 同门）
        pc.placeBlock();
        vst = w.stateAt(24, y0 + 3, 37);
        const bool downFace = w.blockAt(24, y0 + 3, 37) == quint8(BR::Piston)
            && (vst & BR::PistonStateFacingMask) == 0;
        ok = ok && downFace;
        if (!downFace)
            diag += QStringLiteral("[down id=%1 st=%2]").arg(w.blockAt(24, y0 + 3, 37)).arg(vst);

        probeWin.deleteLater();
        pc.release();

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2108d placement facing column (a real creative player placing a piston answers"
               " the push direction toward the player for all four horizontal look codes exactly"
               " mirroring the era yaw quadrant table, a placement two cells below the feet inside"
               " the two block near gate answers up and a placement above the head answers down, a"
               " placement at the body level inside the near gate falls through to the horizontal"
               " rule era natively, and a far horizontal placement outside the two block gate"
               " answers the yaw quadrant regardless of pitch)"
            << (ok ? QString() : diag);
    });

    // ── r2108e:结构钉族柱（六面交付行族 + 双工件锚 + 哨兵不动钉 + 双 NEG 摘面行本腿持有）──────
    runLeg("r2108e structure pins column (the world layer carries the rod placeholder write"
        " row and the implicit bit clear machine row and the sweep event emission rows and"
        " the sweep box helper and the orphan head hook chain and the head plate render"
        " query row, the entities and game layers carry the three consumer rows with the"
        " generation gates, the placement row carries the era near gate and the toward"
        " player quadrant table, the count and atlas sentinels stay unmoved, both t1138 era"
        " adjudication artifacts exist non empty carrying the entity push and placement and"
        " rod and acu anchors, the source tree stays free of the cross task filter token,"
        " and the gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString root = srcRootForPistonSlice4Pins();
        // (R1) world 层行族（杆占位写行[NEG-1 靶] + 拍推发射行[NEG-2 靶 minCount=2] + 末推行 +
        //      盒单点 + 孤儿钩子 + 渲染查询行 + 缩回机 g() 位清行）。
        const QStringList missWorld = pinSet(root + QStringLiteral("/World/world.cpp"),
                                             {
                                                 SrcPin("rod placeholder write row",
                                                        "push(x, y, z, bodyId, BlockRegistry::PistonMoving,", 1),
                                                 SrcPin("machine bit clear row",
                                                        "quint8(pst & quint8(~BlockRegistry::PistonStateExtendedFlag)));", 1),
                                                 SrcPin("rod side table row",
                                                        "m_pistonAnims.push_back({x, y, z, bodyId,", 1),
                                                 SrcPin("beat emission rows",
                                                        "emitSweep(e.x, e.y, e.z, e.facing, kBeatPush, false);", 2),
                                                 SrcPin("final emission row",
                                                        "emitSweep(e.x, e.y, e.z, e.facing, kFinalPush, true);", 1),
                                                 SrcPin("sweep box helper",
                                                        "bool World::pistonSweepBox(const PistonSweep &e, float &minx, float &miny, float &minz,", 1),
                                                 SrcPin("orphan hook def",
                                                        "void World::checkPistonHeadOnEdit(int x, int y, int z, quint8 oldId, quint8 id)", 1),
                                                 SrcPin("orphan body drop row",
                                                        "emit blockDroppedAsItem(nx, ny, nz, int(nid));", 1),
                                                 SrcPin("head plate query row",
                                                        "id = BlockRegistry::PistonHead;", 1),
                                             });
        ok = ok && missWorld.isEmpty();
        if (!missWorld.isEmpty())
            diag += QStringLiteral("[world %1]").arg(missWorld.join(QLatin1Char(',')));
        // (R2) world.h 声明族（PistonSweep 结构 + 事件面 + 孤儿钩子声明 + 代次）。
        const QStringList missWorldH = pinSet(root + QStringLiteral("/World/world.h"),
                                              {
                                                  SrcPin("sweep struct",
                                                         "struct PistonSweep", 1),
                                                  SrcPin("sweeps accessor",
                                                         "const std::vector<PistonSweep> &pistonSweeps() const { return m_pistonSweeps; }", 1),
                                                  SrcPin("generation accessor",
                                                         "quint32 pistonSweepGeneration() const { return m_pistonSweepGeneration; }", 1),
                                                  SrcPin("orphan hook decl",
                                                         "void checkPistonHeadOnEdit(int x, int y, int z, quint8 oldId, quint8 id);", 1),
                                              });
        ok = ok && missWorldH.isEmpty();
        if (!missWorldH.isEmpty())
            diag += QStringLiteral("[worldH %1]").arg(missWorldH.join(QLatin1Char(',')));
        // (R3) 三消费面行族（mob / 掉落物 / 玩家——代次幂等门 + 嵌入豁免 + 逐轴应用）。
        const QStringList missEnt = pinSet(root + QStringLiteral("/Entities/entitymanager.cpp"),
                                           {
                                               SrcPin("mob consume row",
                                                      "void EntityManager::applyPistonSweeps(World *world)", 1),
                                               SrcPin("mob gen gate",
                                                      "world->pistonSweepGeneration() == m_pistonSweepGenSeen", 1),
                                               SrcPin("mob embedded exempt",
                                                      "if (embedded || !mobAabbHitsSolid(world, nx2, p.y(), p.z(), halfW, halfH))", 1),
                                           });
        ok = ok && missEnt.isEmpty();
        if (!missEnt.isEmpty())
            diag += QStringLiteral("[ent %1]").arg(missEnt.join(QLatin1Char(',')));
        const QStringList missStore = pinSet(root + QStringLiteral("/Entities/entitystore.cpp"),
                                             {
                                                 SrcPin("item apply def",
                                                        "void EntityStore::applyPistonDisplacement(World *world, float dx, float dy, float dz,", 1),
                                             });
        ok = ok && missStore.isEmpty();
        if (!missStore.isEmpty())
            diag += QStringLiteral("[store %1]").arg(missStore.join(QLatin1Char(',')));
        const QStringList missItem = pinSet(root + QStringLiteral("/Game/itementitymanager.cpp"),
                                            {
                                                SrcPin("item consume def",
                                                       "void ItemEntityManager::applyPistonSweeps(World *world)", 1),
                                            });
        ok = ok && missItem.isEmpty();
        if (!missItem.isEmpty())
            diag += QStringLiteral("[item %1]").arg(missItem.join(QLatin1Char(',')));
        const QStringList missPc = pinSet(root + QStringLiteral("/Game/playercontroller.cpp"),
                                          {
                                              SrcPin("player gen gate",
                                                     "m_world->pistonSweepGeneration() != m_pistonSweepGenSeen", 1),
                                              SrcPin("item consume call",
                                                     "m_itemEntities->applyPistonSweeps(m_world);", 1),
                                              SrcPin("near gate row",
                                                     "std::fabs(m_pos.x() - cellFx) < 2.0f", 1),
                                              SrcPin("toward player row",
                                                     "case 0: placeState = 4; break;", 1),
                                          });
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty())
            diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
        // (R4) 哨兵不动钉（本单零新方块：Count 166 / Atlas 213 不动——t1136 lawful 前移后现值复钉）。
        const QStringList missBrH = pinSet(root + QStringLiteral("/Core/blockregistry.h"),
                                           {
                                               SrcPin("count sentinel", "Count           = 166,", 1),
                                               SrcPin("atlas sentinel",
                                                      "static constexpr int AtlasTileCount = 213;", 1),
                                           });
        ok = ok && missBrH.isEmpty();
        if (!missBrH.isEmpty())
            diag += QStringLiteral("[brh %1]").arg(missBrH.join(QLatin1Char(',')));
        // (A1) era 工件双件在盘非空携锚（swept + 放置 / 杆占位 + 孤儿恢复）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const QString pushArt = bdir + QStringLiteral("/t1138_jar_entity_push_placement.txt");
        const bool pushOk = fileExistsNonEmptyPistonSlice4(pushArt)
            && rawContainsPistonSlice4(pushArt, QByteArray("private void a(float, float);"))
            && rawContainsPistonSlice4(pushArt, QByteArray("ry.b(Lia;Lc;)"))
            && rawContainsPistonSlice4(pushArt, QByteArray("0.5625"))
            && rawContainsPistonSlice4(pushArt, QByteArray("abr.c(ry,IIILvi)"));
        ok = ok && pushOk;
        if (!pushOk) diag += QStringLiteral("[pushArt]");
        const QString rodArt = bdir + QStringLiteral("/t1138_jar_retract_rod_acu.txt");
        const bool rodOk = fileExistsNonEmptyPistonSlice4(rodArt)
            && rawContainsPistonSlice4(rodArt, QByteArray("Method ry.c:(IIII)Z"))
            && rawContainsPistonSlice4(rodArt, QByteArray("dropBlockAsItem"))
            && rawContainsPistonSlice4(rodArt, QByteArray("public static int f(int);"))
            && rawContainsPistonSlice4(rodArt, QByteArray("headFlag"));
        ok = ok && rodOk;
        if (!rodOk) diag += QStringLiteral("[rodArt]");
        // (R6) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(root,
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonSlice4(it.next(), QByteArray("r2108"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (R7) QML 玩法路径零迁移负面门（r2090d/r2105d/r2106e/r2107e 同门复钉）。
        {
            const QString qml = root + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsPistonSlice4(qml, QByteArray("GameSession"))
                && !rawContainsPistonSlice4(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonSlice4(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonSlice4(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonSlice4(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2108e structure pins column (the world layer carries the rod placeholder write"
               " row and the implicit bit clear machine row and the sweep event emission rows and"
               " the sweep box helper and the orphan head hook chain and the head plate render"
               " query row, the entities and game layers carry the three consumer rows with the"
               " generation gates, the placement row carries the era near gate and the toward"
               " player quadrant table, the count and atlas sentinels stay unmoved, both t1138 era"
               " adjudication artifacts exist non empty carrying the entity push and placement and"
               " rod and acu anchors, the source tree stays free of the cross task filter token,"
               " and the gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
