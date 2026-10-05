#include "matrix_helpers.h"

// t1134 活塞族评估单探针段（4 腿；filter 词 r2104；矩阵 958→962）。置尾先例沿用（接 section96，
//   runAll 末执行，rig 世界零接触——行为腿自建 fresh 小世界，余纯源钉/工件钉腿）。
//   **t1135 lawful 翻案注（r2104b）**：切片一交付活塞本体（引擎 id 162 段尾追加 + kMc 行 33）后，
//   原「缺口确认柱」前提（kMc 零家族映射 + 源树零活塞词元）失真 → 翻案改钉交付面（恰一引擎 id
//   映射 33 且 = Piston、家族另三 id 仍零映射、era 词元零命中），沿革全文见该腿注释。
//
// ── 单性裁定：**零代码评估单**（交接单 AUTO-01 两案取 a 案；评估先行先例 = t1086/t1087 纪元裁定单
//    + t1125 件二零代码裁定收口 + t1120 件三件四裁定锚柱同门）。选型理由：①era 全景已由本单 jar
//    反汇编定谳（工件三件在盘），但完整行为面横跨「方块搬运 + 双拍动画 + 头块渲染 + 实体位移 +
//    附着断裂 + 红石激励接入 + 推动中持久化」六域，首片诚实验收锚（era 考据产物直出）在缺渲染/
//    动画面时立不住——「加模型和配方 ≠ 完整活塞实现」交接单原话即此；②t1130 刚收口的红石 pass
//    钉族（r2100a-i）需要一个独立切片承接新接收器族，不宜与本单混装；③零代码面 = 本段三锚柱
//    （era 证据 / 缺口确认 / 零行为面，t1120 件三件四同门）如实承载现状，后续片按切分序派工。
//
// ── era 全景定谳（jar 反汇编工件 build/t1134_jar_piston_{base,push,tile}.txt；来源
//    build/era_100_unpacked_t1122 官方 1.0.0 client jar sha 锚 b679fea2 = t1121/t1122/t1125 同锚）──
//  · 家族四员：29 粘性活塞 yy.V / 33 活塞 yy.Z（构造 abr(id,tex,sticky)：yy.txt:2112-2116/2153-2160
//    ——**粘性 = 构造布尔**，非独立类）/ 34 头块 yy.aa(acu) / 36 移动占位 yy.ac(qz extends ba)。
//  · 状态位：meta bit[2:0] = 朝向 0..5（e(meta)=meta&7；meta==7 非法即弃），bit3(0x8) = 伸出位
//    （f(meta)=meta&8）；头块 meta 同编码 + bit3 复用携带粘性标记（放置时 dir|sticky?8:0）。
//  · 拒推集合（canPushBlock=abr.a(int,ry,IIII,Z)）：黑曜石 id49 显式拒（yy.ap，yy.txt:2360-2371 构造
//    序 name"obsidian"）∪ 硬度 -1（基岩族）∪ 材料迁移位 2（p.n() 设位器在 1.0 材料静态初始化零调用
//    = 死常量防御分支）∪ **BlockContainer 派生（带 TileEntity 方块族——箱/炉/发射器/酿造台/附魔台/
//    音符盒/刷怪笼/牌子/唱片机/移动占位，jar 内 ba 派生恰 11 类）∪ 已伸出活塞本体**。
//    **交接单问②定谳：箱子被推 = 拒推零掉落**（piston 不伸出，非破坏掉落——jar 无箱子破坏分支）。
//  · 推动上限 **12**：tryExtend(abr.g static) 正扫 count<13 循环、count==12 即败；执行相(abr.h)同界。
//  · 附着断裂：执行相正扫迁移位 1（材料 p.m() 设 J=1）格 -> dropBlockAsItemWithChance 恒掉落 +
//    setBlockWithNotify 清空 -> 继续回走搬移（不入推列）；正扫失败（13 格界内遇拒推/满界）->
//    **整次放弃零部分推动**（world 零改写）。迁移位 1 材料成员（sn 类材料 jar 恰两实例 p.g/p.h）的
//    完整方块清单 = 首片交付时的 era 复核件（本单登记不猜）。
//  · 时序（交接单「0.5s 推程=10 game tick」前提**被字节码否证**）：TilePiston(agb).b() 每 game tick
//    progress += 0.5F（agb.txt:383 ldc 0.5f）-> **伸程 = 2 game tick（0.1s）推进 + 第 3 tick 实体化**
//    （n>=1 终相 world.d(pos,storedId,storedMeta)）；缩回即时（非粘性/粘性无可拉块 = 头格当拍清空，
//    无缩回动程）；粘性拉回块 = 拉回格走 2-tick 伸程动画入头格，前格清空。
//  · 实体位移面（交接单问项定谳——1.0 **实有**）：agb.a(float,float) = swept AABB（qz.b 按存储块
//    包围盒沿朝向偏移进度）-> world.getEntitiesWithinAABBExcludingEntity(null,aabb) -> 逐实体
//    moveEntity(增量×方向向量)，每 tick 增量推移 +0.0625 ε、终相 0.25；静态暂存表 o 用后即清。
//  · 红石激励：活塞**零自排程**（abr 无 Random-updateTick 方法）——状态机纯事件驱动（放置/邻变/
//    写入事件），受电查询 = 本体邻域 + 头格邻域逐向 World.l(IIII)Z（ry.txt:8720-8753：实心格聚合
//    u() / 非实心格面供电 yy.b(kq,IIII)Z）。**BUD 敏感性 = era 结构真**（供电变化不发邻变即不可见，
//    任意邻变补触发；是否复刻留主控裁定，报告分析利弊在案）。事件入口 abr.a(world,IIIII) 6 参 =
//    phase0 伸 / phase1 缩，静态 cb 守卫环绕全事件抑制级联再入（重复脉冲面 = 中途 j() 立即实体化
//    + meta 重查 + cb 防再入）。
//  · 持久化：TilePiston NBT = blockId/blockData/facing/progress(float)/extending(bool)（agb.txt:
//    410-475）——**推动中两拍状态随 chunk TileEntity 持久化**（era 真值）；工程承载评估 = 静态面
//    全落 state 位（facing 3b + extended 1b，chunk m_states 既有存档 round-trip 契约，**零新存档表**
//    成立），两拍动画若按 era 实做需 tile 形存储面（工程无 TileEntity 层，Qt 侧 store 族为先例）或
//    state 位扩宽裁定，属后续片设计件。
//  · 世界边界：tryExtend/执行相唯一域守卫 = y<=0 || y>=世界高-1 弃推（era 无 chunk 概念；工程侧
//    跨 chunk 推动走 ChunkManager 跨 chunk 路由 + 物化门，population 窗门形[t1131 定理]**不适用**
//    ——活塞是运行期玩家激励非 population 重放；同域先例 = TNT detonateTntSphere 批量静默写 +
//    单次 worldChanged/clearAllDirty/refloodBox 收口）。
//  · 声音：伸 "tile.piston.out"（音量 0.5 音高 0.6+rand×0.25）/ 缩 "tile.piston.in"（0.5 /
//    0.6+rand×0.15）——era 真值留痕，后续片实现须原创名（零 MC 专有名词纪律）。
//
// ── 切分序登记（候选池；逐片派工，每片验收锚全数出自本单工件 + 复核件）─────────────────
//  · 切片一（首片候选）：普通活塞本体（新引擎 id 追加 + kMc 行 33）+ 朝向放置 + 推 ≤12 实心 +
//    拒推集合四员（黑曜石/硬度-1/带 store 方块族[TileEntity 对应面]/已伸活塞）+ 附着断裂掉落 +
//    红石接收器接入（tickRedstone 消费族沿 + 受电查询）+ **瞬时推动简化**（无头块/无动画/无移动
//    占位 id，era 两拍面如实登记为后续片）。验收锚草案：12 上限柱 / 拒推四柱 / 断裂掉落柱 /
//    激励伸-缩柱 / NEG 摘激励调用点。
//  · 切片二：粘性活塞（id 追加 + kMc 29）+ 头块（kMc 34）+ 拉回 + 头块渲染（acu 台面盒）。
//  · 切片三：推动两拍动画（移动占位 id 36 + 进度存储面裁定 + swept 渲染/碰撞）+ 推动中持久化。
//  · 切片四：实体位移面（EntityManager AABB 扫增量推移）+ BUD 面裁定执行 + 重复脉冲/失败恢复
//    硬化（j() 立即实体化语义）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；双 NEG 互不重叠；无 src 行为面 = 钉面/工件面为靶）──
//  NEG-1 = 双手工 Edit 摘 CMakeLists 本段注册行（编译无关，钉面消失）→ 恰红 = {r2104d}（结构钉
//    族 cmake 行）；r2104a/b/c 豁免（零触达）。
//  NEG-2 = mv build/era_100_unpacked_t1122/abr.class 出位面（工件面缺失；mv 保原字节，restore 后
//    md5 复归核）→ 恰红 = {r2104a}（era 证据钉缺类文件）；r2104b/c/d 豁免（零触达）。
namespace {

// 源钉根路径（section96 同门：applicationDirPath/../src）。
inline QString srcRootForPistonEvalPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体/二进制类文件锚——pinSet 剥注释会失配，section72..96 同款）。
inline bool rawContainsPistonEval(const QString &path, const QByteArray &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().contains(needle);
}

inline bool fileExistsNonEmptyPistonEval(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.size() > 0;
}

// 造一块石坪 + 清上空（section83/88 同款四 setter）。
inline void initPistonEvalWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

} // namespace

void MatrixRun::section97_piston_eval_t1134()
{
    // ── r2104a:era 证据行钉柱（fixture 类串锚 + 家族四类在盘 + 工件三件在盘非空携锚）─────────
    //   取证基座 = build/era_100_unpacked_t1122（t1121/t1122/t1125 同锚 sha b679fea2）。
    //   NEG-2 敏感腿（abr.class 出位面 → 本腿恰红）。
    runLeg("r2104a era evidence on file column (the one point zero jar fixture carries the"
        " piston family with the base class registering pistonBase and pistonStickyBase and"
        " the piston base class carrying the extend and retract sound names, the language"
        " table carries the piston tile keys, all four family class files exist on disk non"
        " empty, and the three adjudication artifacts exist non empty carrying the push"
        " limit bytecode anchor and the retract sound anchor and the persisted nbt key"
        " anchor)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString fx = QCoreApplication::applicationDirPath()
            + QStringLiteral("/era_100_unpacked_t1122");
        // (1) 注册串锚：yy.class（Block 基类，static{} 全方块注册）含两活塞注册名。
        const bool yyPins = rawContainsPistonEval(fx + QStringLiteral("/yy.class"),
                                                  QByteArray("pistonBase"))
            && rawContainsPistonEval(fx + QStringLiteral("/yy.class"),
                                     QByteArray("pistonStickyBase"));
        // (2) 事件声锚：abr.class（BlockPistonBase）含伸缩两声名。
        const bool abrPins = rawContainsPistonEval(fx + QStringLiteral("/abr.class"),
                                                   QByteArray("tile.piston.out"))
            && rawContainsPistonEval(fx + QStringLiteral("/abr.class"),
                                     QByteArray("tile.piston.in"));
        // (3) 语言表锚：en_US.lang 含活塞 tile 键（1.0 键形 = tile.pistonBase.name）。
        const bool langPins = rawContainsPistonEval(fx + QStringLiteral("/lang/en_US.lang"),
                                                    QByteArray("tile.pistonBase.name"))
            && rawContainsPistonEval(fx + QStringLiteral("/lang/en_US.lang"),
                                     QByteArray("tile.pistonStickyBase.name"));
        ok = ok && yyPins && abrPins && langPins;
        if (!(yyPins && abrPins && langPins))
            diag += QStringLiteral("[fixture yy=%1 abr=%2 lang=%3]")
                        .arg(yyPins).arg(abrPins).arg(langPins);
        // (4) 家族四类在盘非空（abr 基座 / acu 头块 / qz 移动占位 / agb TilePiston）。
        const QStringList family = { QStringLiteral("abr.class"), QStringLiteral("acu.class"),
            QStringLiteral("qz.class"), QStringLiteral("agb.class") };
        bool familyOk = true;
        for (const QString &c : family)
            if (!fileExistsNonEmptyPistonEval(fx + QLatin1Char('/') + c))
                familyOk = false;
        ok = ok && familyOk;
        if (!familyOk) diag += QStringLiteral("[family]");
        // (5) 本单三工件在盘非空携锚（12 上限字节码 / 缩回声名 / NBT 键名）。
        const QString bdir = QCoreApplication::applicationDirPath();
        const bool artBase = fileExistsNonEmptyPistonEval(
                                 bdir + QStringLiteral("/t1134_jar_piston_base.txt"))
            && rawContainsPistonEval(bdir + QStringLiteral("/t1134_jar_piston_base.txt"),
                                     QByteArray("bipush        13"));
        const bool artPush = fileExistsNonEmptyPistonEval(
                                 bdir + QStringLiteral("/t1134_jar_piston_push.txt"))
            && rawContainsPistonEval(bdir + QStringLiteral("/t1134_jar_piston_push.txt"),
                                     QByteArray("tile.piston.in"));
        const bool artTile = fileExistsNonEmptyPistonEval(
                                 bdir + QStringLiteral("/t1134_jar_piston_tile.txt"))
            && rawContainsPistonEval(bdir + QStringLiteral("/t1134_jar_piston_tile.txt"),
                                     QByteArray("blockId"));
        ok = ok && artBase && artPush && artTile;
        if (!(artBase && artPush && artTile))
            diag += QStringLiteral("[artifacts base=%1 push=%2 tile=%3]")
                        .arg(artBase).arg(artPush).arg(artTile);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2104a era evidence on file column (the one point zero jar fixture carries"
               " the piston family with the base class registering pistonBase and"
               " pistonStickyBase and the piston base class carrying the extend and retract"
               " sound names, the language table carries the piston tile keys, all four"
               " family class files exist on disk non empty, and the three adjudication"
               " artifacts exist non empty carrying the push limit bytecode anchor and the"
               " retract sound anchor and the persisted nbt key anchor)"
            << (ok ? QString() : diag);
    });

    // ── r2104b:交付面行钉柱（t1136 lawful 翻案——切片二交付收口缺口，本柱再翻钉全家族映射面）──────
    //   沿革：原「缺口确认柱」在 t1135 交付活塞本体后第一次翻案改钉「恰一映射 33」（t1088/t1113
    //   前移先例同门）；t1136 切片二交付粘性/头块/移动占位后 29/34/36 三 id 入映射 → 二次 lawful
    //   翻案改钉全家族映射面（翻案注立此存照）：注册表层 = kMc 恰一引擎 id 映射 33（= Piston 162）、
    //   恰一映射 29（= StickyPiston 165）、恰一映射 34（= PistonHead 163）、恰一映射 36（=
    //   PistonMoving 164）；词法层 = src 全树零 era 家族标识词元（混淆类名 abr/acu/qz/agb、era
    //   注册名 pistonBase/pistonStickyBase、era 声名 tile.piston）——机制交付但零 era 资产/专名词形
    //   （PLAN §9）。
    runLeg("r2104b delivery face column (the t1135 slice one delivery lawfully closed the"
        " registry gap and the t1136 slice two delivery closes the family so the engine to"
        " one point zero block mapping answers exactly one engine id for each of the family"
        " numeric ids thirty three twenty nine thirty four and thirty six and those engine"
        " ids are the piston and the sticky piston and the head block and the moving"
        " placeholder respectively, and the whole"
        " source tree carries zero era family identifier tokens from the jar fixture"
        " obfuscation set and zero era registration and sound name tokens, so the ledger"
        " row is anchored at the registry layer with the full delivered family and the"
        " lexical layer stays era free)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) kMc 映射幂扫（交付面）：29/33/34/36 各恰一引擎 id 且 = 活塞族对应 id（t1136 全家族钉）。
        {
            int hits33 = 0, hits29 = 0, hits34 = 0, hits36 = 0;
            int pistonMapped = 0, stickyMapped = 0, headMapped = 0, movingMapped = 0;
            for (int id = 0; id < int(BR::Count); ++id) {
                const int mc = BR::mcBlockId(quint8(id));
                if (mc == 33) {
                    ++hits33;
                    if (id == int(BR::Piston)) ++pistonMapped;
                } else if (mc == 29) {
                    ++hits29;
                    if (id == int(BR::StickyPiston)) ++stickyMapped;
                } else if (mc == 34) {
                    ++hits34;
                    if (id == int(BR::PistonHead)) ++headMapped;
                } else if (mc == 36) {
                    ++hits36;
                    if (id == int(BR::PistonMoving)) ++movingMapped;
                }
            }
            const bool sweep = hits33 == 1 && pistonMapped == 1
                && hits29 == 1 && stickyMapped == 1
                && hits34 == 1 && headMapped == 1
                && hits36 == 1 && movingMapped == 1;
            ok = ok && sweep;
            if (!sweep)
                diag += QStringLiteral("[kMc h33=%1 m33=%2 h29=%3 m29=%4 h34=%5 m34=%6 h36=%7 m36=%8]")
                            .arg(hits33).arg(pistonMapped).arg(hits29).arg(stickyMapped)
                            .arg(hits34).arg(headMapped).arg(hits36).arg(movingMapped);
        }
        // (2) 源树 era 词元零命中（混淆类名 + era 注册名 + era 声名；工程自名 Piston/piston* 不在列）。
        {
            static const char *kEraTokens[] = { "pistonBase", "pistonStickyBase",
                                                "tile.piston", "pistonMoving" };
            bool leaked = false;
            int leakFiles = 0;
            QDirIterator it(srcRootForPistonEvalPins(),
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString p = it.next();
                for (const char *tk : kEraTokens) {
                    if (rawContainsPistonEval(p, QByteArray(tk))) {
                        leaked = true;
                        ++leakFiles;
                        break;
                    }
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token files=%1]").arg(leakFiles);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2104b delivery face column (the t1135 slice one delivery lawfully closed"
               " the registry gap and the t1136 slice two delivery closes the family so the engine to"
               " one point zero block mapping answers exactly one engine id for each of the family"
               " numeric ids thirty three twenty nine thirty four and thirty six and those engine"
               " ids are the piston and the sticky piston and the head block and the moving"
               " placeholder respectively, and the whole source tree carries zero era family identifier"
               " tokens from the jar fixture obfuscation set and zero era registration and"
               " sound name tokens, so the ledger row is anchored at the registry layer with"
               " the full delivered family and the lexical layer stays era free)"
            << (ok ? QString() : diag);
    });

    // ── r2104c:零行为面钉柱（真 rig 供电正控 + 域快照逐位恒等）────────────────────────────
    //   正控 = 拉杆点亮贴邻红石灯且断电复灭（供电通路真实两面——t1130 r2100h 同域）；零面 =
    //   30 次红石 tick 前后 9×7×9 域 id 快照逐位恒等（供电不产生/消失/移动任何方块 = 活塞零承载
    //   的行为面证明——t1120 件四「权威在场而锅零消费」同门形）。
    runLeg("r2104c zero behavior face rig column (a lit lever lights the adjacent redstone"
        " lamp and an unlit lever extinguishes it proving the power path is real in both"
        " directions, and thirty redstone ticks over the powered rig leave the nine by"
        " seven by nine region block identity snapshot bit identical so no block is created"
        " destroyed or moved by any power state in the current tree)",
        [&]() {
        bool ok = true;
        QString diag;
        World w;
        initPistonEvalWorld(w, 1134);
        const int y0 = 40;
        for (int x = 18; x <= 30; ++x)
            for (int z = 18; z <= 30; ++z)
                w.setBlock(x, y0, z, BR::Stone, 0);
        // 拉杆（state bit0 点燃）+ 东邻红石灯（侧邻直供 = r2100h 同域）。
        const int lx = 24, ly = y0 + 1, lz = 24;
        const bool leverOk = w.setBlock(lx, ly, lz, BR::Lever, quint8(0x01));
        const bool lampOk = w.setBlock(lx + 1, ly, lz, BR::RedstoneLamp, 0);
        ok = ok && leverOk && lampOk;
        if (!(leverOk && lampOk)) diag += QStringLiteral("[rig]");
        auto regionSnapshot = [&w, ly]() {
            QVector<int> ids;
            for (int x = 20; x <= 28; ++x)
                for (int y = ly - 3; y <= ly + 3; ++y)
                    for (int z = 20; z <= 28; ++z)
                        ids.append(int(w.blockAt(x, y, z)));
            return ids;
        };
        // 基线快照（供电前）→ 供电 5 pass → 灯亮正控。
        const QVector<int> before = regionSnapshot();
        for (int i = 0; i < 5; ++i)
            w.tickRedstone();
        const quint8 lampState = w.stateAt(lx + 1, ly, lz);
        const bool litOn = (lampState & BR::RedstoneLampStateOnFlag) != 0;
        ok = ok && litOn;
        if (!litOn)
            diag += QStringLiteral("[lampOn state=%1]").arg(lampState);
        // 零面：断电复灭 + 30 pass 后域 id 快照与基线逐位恒等（含拉杆格——bit0 是 state 非 id）。
        w.setBlock(lx, ly, lz, BR::Lever, quint8(0x00));
        for (int i = 0; i < 3; ++i)
            w.tickRedstone();
        const bool litOff = (w.stateAt(lx + 1, ly, lz) & BR::RedstoneLampStateOnFlag) == 0;
        ok = ok && litOff;
        if (!litOff) diag += QStringLiteral("[lampOff]");
        w.setBlock(lx, ly, lz, BR::Lever, quint8(0x01)); // 复燃（最富供电态下证零面）
        for (int i = 0; i < 30; ++i)
            w.tickRedstone();
        const QVector<int> after = regionSnapshot();
        const bool frozen = before == after;
        ok = ok && frozen;
        if (!frozen) diag += QStringLiteral("[region moved]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2104c zero behavior face rig column (a lit lever lights the adjacent"
               " redstone lamp and an unlit lever extinguishes it proving the power path is"
               " real in both directions, and thirty redstone ticks over the powered rig"
               " leave the nine by seven by nine region block identity snapshot bit"
               " identical so no block is created destroyed or moved by any power state in"
               " the current tree)"
            << (ok ? QString() : diag);
    });

    // ── r2104d:结构钉族柱（NEG-1 敏感腿 = CMake 注册行）──────────────────────────────────
    //   CMake 段行 + runAll 调用行与声明行 + 源树 filter 词元零命中 + QML 玩法路径零迁移负面门
    //   复钉（r2090d/r2102e/r2103e 同门）。
    runLeg("r2104d structure pins column (the cmake section row and the harness registration"
        " and declaration rows for this section are present exactly once, the source tree"
        " stays free of the cross task filter token, and the gameplay path literals stay"
        " out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        // (D1) CMake 注册行（NEG-1 摘行 → 本腿恰红）。
        const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"),
                                          {
                                              SrcPin("cmake section row",
                                                     "tools/matrix/section97_piston_eval_t1134.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // (D2) harness 两行（runAll 调用 + 头声明）。
        const QStringList missRun = pinSet(srcRootForPistonEvalPins()
                                               + QStringLiteral("/../tools/matrix/matrix_helpers.cpp"),
                                           {
                                               SrcPin("runAll call row",
                                                      "section97_piston_eval_t1134();", 1)});
        ok = ok && missRun.isEmpty();
        if (!missRun.isEmpty())
            diag += QStringLiteral("[run %1]").arg(missRun.join(QLatin1Char(',')));
        const QStringList missDecl = pinSet(srcRootForPistonEvalPins()
                                                + QStringLiteral("/../tools/matrix/matrix_helpers.h"),
                                            {
                                                SrcPin("declaration row",
                                                       "void section97_piston_eval_t1134();", 1)});
        ok = ok && missDecl.isEmpty();
        if (!missDecl.isEmpty())
            diag += QStringLiteral("[decl %1]").arg(missDecl.join(QLatin1Char(',')));
        // (D3) 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(srcRootForPistonEvalPins(),
                            {QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                             QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsPistonEval(it.next(), QByteArray("r2104"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (D4) QML 玩法路径零迁移负面门（r2090d/r2102e/r2103e 同门复钉）。
        {
            const QString qml = srcRootForPistonEvalPins() + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsPistonEval(qml, QByteArray("GameSession"))
                && !rawContainsPistonEval(qml, QByteArray("MeshWorker"))
                && !rawContainsPistonEval(qml, QByteArray("setChunkLifecycle"))
                && !rawContainsPistonEval(qml, QByteArray("ChunkEvictor"))
                && !rawContainsPistonEval(qml, QByteArray("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2104d structure pins column (the cmake section row and the harness"
               " registration and declaration rows for this section are present exactly"
               " once, the source tree stays free of the cross task filter token, and the"
               " gameplay path literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
