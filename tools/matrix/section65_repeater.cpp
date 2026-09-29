#include "matrix_helpers.h"

// t1095 红石中继器探针段（5 腿；filter 词 r2065；矩阵 797→802）。置尾先例沿用（接 section64，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：§14-④ 池底重盘首位（t1087 裁定单确证中继器全仓零存在 = 真缺口；Beta 1.3 机制、1.0.0
//   基线内）。四语义：①延迟四档（1..4 redstone tick = tickRedstone 一 pass；右键循环调档，音符盒调音
//   同门）；②二极管整流（输入只读后端格 + 后端格为中继器「面朝本格」才计）；③输出强充能 15（亮态自身
//   即电源，前端粉从 15 重新起步 = 续距）；④锁定面 1.0 无（中继器锁存是 1.5+ 机制，如实登记不做）。
//   交付面：Repeater=147（id 尾追加）+ ShapeRepeater=12（2/16 薄板，t639 四消费者同源）+ state 编码
//   （bit[1:0] 朝向 / bit[3:2] 延迟档 / bit4 输出位 / bit[7:5] 挂起计数——零运行期侧表）+ 程序贴图两张
//   (191/192 尾部追加）+ 红石 tab 调色板 + 失撑掉落钩子（checkPressurePlateOnEdit 族扩展）。
//
// 机制面（world.cpp t1095）：中继器 = 受控电源——powerSourceLevel 亮态 15；Phase A 粉播种水平 4 向改走
//   sourceFeedsCell 定向（中继器亮态只喂朝向格的粉；非中继器源全向逐字保留 → 对既有电路零变化）；
//   addReceiver 扫描纳中继器 → 专用分支：定向读后端（repeaterInputOn）+ 挂起计数达「档+1」翻转输出位 +
//   翻转时前端格入脏集（前端粉定向播种 15 / 前端接收器复查）+ 挂起期自回插（定点迭代，火把重亮同门）。
//
// 腿面与阴性面设计（恰红归因）：
//   r2065a 延迟档位语义（同构双电路 delay=1 / delay=4：输入粉恰在首 pass 写 15；挂起计数逐 pass 推进的
//     state 直读；前端粉首亮 pass 精确 = 2+档 [3 vs 6，差恰 3]；终态双亮 + 计数清零）。
//     NEG-1（达档判定 cnt > delay 降为 >= → 零延迟）→ 恰红 r2065a。
//   r2065b 整流单向（链上正控制 + 侧粉重唤醒探针 + 分相摆放的背靠背反灌拒绝探针族）。
//     NEG-2（sourceFeedsCell 定向判定改「亮态即馈电」四向灌电）→ 恰红 r2065b。
//   r2065c 续距 15 + 端到端（负坐标 + 核心域双跑 17 格电路——无中继器灯必暗，有中继器前端粉重启 15、
//     远端恰 5、灯亮，输入侧 15/14/13 梯度原样）。
//   r2065d 结构钉（def 行逐字段 + kMcBlockId 93 + Count 148 + 图集 193 + state 编解码可逆 + 调档循环
//     1-2-3-4-1 + 全链源钉族）。
//   r2065e 域门收口复钉 + 相邻族零污染（r2059-r2064 计数钉与锚注族 + 既有红石族行为回归柱 + 负坐标墙
//     复钉 + 惰性编辑零重算）。
//
// rig 纪律：fixed 宿主小世界 incantation（四 setter，section61 同款）/ sparse 构造缝 `World w{ {82,
//   48, 48, 96, 0} }` + loadChunkAt 按需物化（t799/t814 rig 域教训：未物化 chunk 的 setBlock 静默拒写
//   = 探针环境性假红——本段电路横跨原点四邻 chunk，全数 load）；rig 坪取 y=80..81 高空（s82 地形/
//   树冠带之上）；rig 基建全经 setBlock 主门；坐标不作哨兵。
//
// 「评估须重唤醒」机制口径（r2065b 探针设计承重）：电力重算只扫脏锚点邻域——器件翻亮后未被任何
//   锚点邻域覆盖的探针格永不再评。凡「A 翻亮后 B 是否被灌」类探针必须让 B 的评估严格后置于 A 稳态
//   （重放 B 或分相摆放 B），否则探针在 A 翻亮前评估一次即沉寂 = 整流面不可观测（NEG-2 验红依赖）。

namespace {

// fixed 宿主小世界 incantation（section64 同款四 setter；默认构造 World 原地配置）。
inline void initFixedRepeaterWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

} // namespace

void MatrixRun::section65_repeater()
{
    // ── r2065a：延迟档位语义（同构双电路 delay=1 / delay=4 精确时序差）────────────────────────
    runLeg("r2065a delay-tier semantics (two identical circuits with a lever feeding dust feeding a"
        " repeater feeding output dust, delay tier 1 vs tier 4: the input dust turns on exactly on"
        " pass 1, the pending counter reads 1 right after pass 1 on both, the output dust first"
        " lights on pass 3 for tier 1 and pass 6 for tier 4 [exact 2+tier ticks, difference"
        " exactly 3], and both repeaters end lit with the pending counter cleared)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRepeaterWorld(w);
        // 双电路同构布局（y=81 行，z=20 / z=24 错行防交叉；全程核心域）：
        //   lever(20) → dust(21) → R(22, facing +X, 档位位段 [A=档1=0x00 / B=档4=0x0C]) → dust(23)。
        for (int x = 19; x <= 24; ++x)
            for (int z = 19; z <= 25; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        const quint8 stA = 0x00;           // facing 0=+X | delay-1=0（档 1）
        const quint8 stB = quint8(3 << 2); // facing 0=+X | delay-1=3（档 4）
        w.setBlock(20, 81, 20, BR::Lever, 1);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::Repeater, stA);
        w.setBlock(23, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(20, 81, 24, BR::Lever, 1);
        w.setBlock(21, 81, 24, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 24, BR::Repeater, stB);
        w.setBlock(23, 81, 24, BR::RedstoneDust, 0);
        // pass 1：输入粉恰在本 pass 写 15（拉杆直供邻粉）；挂起计数首评置 1（双电路同）。
        w.tickRedstone();
        const bool inOn = (w.stateAt(21, 81, 20) & BR::RedstoneDustPowerMask) == 15
            && (w.stateAt(21, 81, 24) & BR::RedstoneDustPowerMask) == 15;
        const bool cnt1 = BR::repeaterPendingCount(w.stateAt(22, 81, 20)) == 1
            && BR::repeaterPendingCount(w.stateAt(22, 81, 24)) == 1;
        ok = ok && inOn && cnt1;
        if (!(inOn && cnt1))
            diag += QStringLiteral("[p1 in=%1 cnt=%2 dA=%3 dB=%4]").arg(inOn).arg(cnt1)
                        .arg(BR::repeaterPendingCount(w.stateAt(22, 81, 20)))
                        .arg(BR::repeaterPendingCount(w.stateAt(22, 81, 24)));
        // 前端粉首亮 pass：档 1 → 3 / 档 4 → 6（精确 2+档；差恰 3）。逐 pass 采样（每 pass 后查两路）。
        int pA = 0, pB = 0;
        for (int p = 2; p <= 12 && (pA == 0 || pB == 0); ++p) {
            w.tickRedstone();
            if (pA == 0 && (w.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask) > 0) pA = p;
            if (pB == 0 && (w.stateAt(23, 81, 24) & BR::RedstoneDustPowerMask) > 0) pB = p;
        }
        const bool timing = pA == 3 && pB == 6 && (pB - pA) == 3;
        ok = ok && timing;
        if (!timing) diag += QStringLiteral("[timing pA=%1 pB=%2]").arg(pA).arg(pB);
        // 终态：双输出位置位 + 挂起计数清零（达档翻转收口）。
        const bool fin = (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0
            && (w.stateAt(22, 81, 24) & BR::RepeaterStatePoweredFlag) != 0
            && BR::repeaterPendingCount(w.stateAt(22, 81, 20)) == 0
            && BR::repeaterPendingCount(w.stateAt(22, 81, 24)) == 0;
        ok = ok && fin;
        if (!fin) diag += QStringLiteral("[fin sA=%1 sB=%2]").arg(w.stateAt(22, 81, 20))
                              .arg(w.stateAt(22, 81, 24));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2065a delay-tier semantics (two identical circuits with a lever feeding dust"
               " feeding a repeater feeding output dust, delay tier 1 vs tier 4: the input dust"
               " turns on exactly on pass 1, the pending counter reads 1 right after pass 1 on"
               " both, the output dust first lights on pass 3 for tier 1 and pass 6 for tier 4"
               " [exact 2+tier ticks, difference exactly 3], and both repeaters end lit with the"
               " pending counter cleared)"
            << (ok ? QString() : diag);
    });

    // ── r2065b：整流单向（链上正控制 + 侧粉重唤醒探针 + 分相摆放的背靠背反灌拒绝探针族）──────────
    runLeg("r2065b diode rectification (a lit repeater feeds its front dust at 15 and chains into"
        " a second repeater whose back reads that dust, a side dust re-placed after the repeater"
        " settles stays dark, a lever-fed probe repeater lights through the directional back read,"
        " and a probe repeater placed facing a lit repeater that points AWAY from it stays"
        " unpowered with its own front dust dark [output never feeds back sideways or backwards"
        " under the re-evaluation-requires-rewake probe discipline])", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        // rig 物化域：电路横跨原点四邻 chunk（x/z 正负两侧）→ 四块全 load（t799/t814 rig 域教训：
        //   未物化 chunk 的 setBlock 静默拒写 = 探针环境性假红，先补齐物化再摆件）。
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, 0)
            && w.loadChunkAt(-1, 0) && w.loadChunkAt(0, -1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 石坪 y=80（x -3..7, z -1..7，含负坐标列）。
        for (int x = -3; x <= 7; ++x)
            for (int z = -1; z <= 7; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        // 电路 A（z=0 行）：lever(0) → dust(1) → R(2, facing +X) → frontDust(3) → Rc(4, facing +X，
        //   后端 = (3,0) 通电粉 = 链上正控制）。先稳态化（12 pass：档 1 两 pass 翻亮 + 前端粉第三 pass 亮）。
        w.setBlock(0, 81, 0, BR::Lever, 1);
        w.setBlock(1, 81, 0, BR::RedstoneDust, 0);
        w.setBlock(2, 81, 0, BR::Repeater, 0);
        w.setBlock(3, 81, 0, BR::RedstoneDust, 0);
        w.setBlock(4, 81, 0, BR::Repeater, 0);
        tickN(w, 12);
        const bool rOn = (w.stateAt(2, 81, 0) & BR::RepeaterStatePoweredFlag) != 0;
        const bool rcOn = (w.stateAt(4, 81, 0) & BR::RepeaterStatePoweredFlag) != 0;
        const int frontP = int(w.stateAt(3, 81, 0) & BR::RedstoneDustPowerMask);
        const bool chainOk = rOn && rcOn && frontP == 15;
        ok = ok && chainOk;
        if (!chainOk)
            diag += QStringLiteral("[chain r=%1 rc=%2 fp=%3]").arg(rOn).arg(rcOn).arg(frontP);
        // 侧向探针粉（评估须重唤醒）：R 翻亮后重放侧粉 = 放置自挂脏集 → 下一 pass 带亮态 R 重评；
        //   摘整流面变异下即被灌亮 = NEG-2 可观测；固定实现（仅朝向格馈电）下恒暗。
        w.setBlock(2, 81, 1, BR::RedstoneDust, 0);
        tickN(w, 3);
        const int sideP = int(w.stateAt(2, 81, 1) & BR::RedstoneDustPowerMask);
        ok = ok && sideP == 0;
        if (sideP != 0)
            diag += QStringLiteral("[side %1]").arg(sideP);
        // 整流负探针（放后必评：先稳态化再放探针件，探针件放置编辑自挂脏集 → 放后必评）。
        //   Rb 分相放：lever(-1,81,4) → Rb(0,81,4, facing +X) 直供型（后端格 = 拉杆格）→ 两 pass 内翻亮
        //   （正控制：定向读后端电源路径正常）。Rd 后放：后端格 = (0,81,4) = Rb 本体且 Rb 输出朝 +X
        //   背向 Rd → 整流拒绝恒灭；Rd 前端粉 (0,81,6) 同批后放（摘整流面下 Rd 被反灌亮）。
        w.setBlock(-1, 81, 4, BR::Lever, 1);
        w.setBlock(0, 81, 4, BR::Repeater, 0);
        tickN(w, 6);
        const bool rbOn = (w.stateAt(0, 81, 4) & BR::RepeaterStatePoweredFlag) != 0;
        ok = ok && rbOn;
        if (!rbOn) diag += QStringLiteral("[rb]");
        w.setBlock(0, 81, 5, BR::Repeater, quint8(2)); // facing 2=+Z（bit[1:0]=2）
        w.setBlock(0, 81, 6, BR::RedstoneDust, 0);
        tickN(w, 6);
        const bool rdOff = (w.stateAt(0, 81, 5) & BR::RepeaterStatePoweredFlag) == 0
            && BR::repeaterPendingCount(w.stateAt(0, 81, 5)) == 0;
        const int rdFrontP = int(w.stateAt(0, 81, 6) & BR::RedstoneDustPowerMask);
        const bool rectOk = rdOff && rdFrontP == 0;
        ok = ok && rectOk;
        if (!rectOk)
            diag += QStringLiteral("[rect rdOff=%1 rdF=%2]").arg(rdOff).arg(rdFrontP);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2065b diode rectification (a lit repeater feeds its front dust at 15 and chains"
               " into a second repeater whose back reads that dust, a side dust re-placed after"
               " the repeater settles stays dark, a lever-fed probe repeater lights through the"
               " directional back read, and a probe repeater placed facing a lit repeater that"
               " points AWAY from it stays unpowered with its own front dust dark [output never"
               " feeds back sideways or backwards under the"
               " re-evaluation-requires-rewake probe discipline])"
            << (ok ? QString() : diag);
    });

    // ── r2065c：续距 15 + 端到端供电链（负坐标 + 核心域双域）────────────────────────────────────
    runLeg("r2065c signal-extending and end-to-end chain (in a sparse world the same 17-cell"
        " circuit lever, three dust, repeater, eleven dust, lamp is built once on negative"
        " coordinates and once in the core domain: without the repeater the lamp sits beyond the"
        " 15-cell dust decay limit and could never light, with it the first output dust restarts"
        " at 15, the far end holds exactly 5, the lamp lights, and the input-side dust keeps its"
        " 15/14/13 decay gradient in both domains)", [&]() {
        bool ok = true;
        QString diag;
        World w{ { 82, 48, 48, 96, 0 } };
        // rig 物化域：负域电路跨 chunk (-1,-1)/(0,-1)，核心电路在 (1,1)/(2,1)——四块全 load。
        bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, -1)
            && w.loadChunkAt(1, 1) && w.loadChunkAt(2, 1);
        ok = ok && rig;
        if (!rig) diag += QStringLiteral("[rig] ");
        // 负坐标电路（y=81 行 z=-10；石坪 y=80）：lever(-11) → dust(-10..-8) → R(-7) → dust(-6..4,
        //   11 枚) → lamp(5)。全程 17 格 > 15 格粉衰减上限。
        for (int x = -13; x <= 8; ++x)
            for (int z = -12; z <= -8; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(-11, 81, -10, BR::Lever, 1);
        for (int x = -10; x <= -8; ++x) w.setBlock(x, 81, -10, BR::RedstoneDust, 0);
        w.setBlock(-7, 81, -10, BR::Repeater, 0);
        for (int x = -6; x <= 4; ++x) w.setBlock(x, 81, -10, BR::RedstoneDust, 0);
        w.setBlock(5, 81, -10, BR::RedstoneLamp, 0);
        // 核心域对照电路（z=20 行）：同构平移（lever(19) → … → R(23) → dust(24..34) → lamp(35)）。
        for (int x = 17; x <= 38; ++x)
            for (int z = 18; z <= 22; ++z)
                w.setBlock(x, 80, z, BR::Stone, 0);
        w.setBlock(19, 81, 20, BR::Lever, 1);
        for (int x = 20; x <= 22; ++x) w.setBlock(x, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(23, 81, 20, BR::Repeater, 0);
        for (int x = 24; x <= 34; ++x) w.setBlock(x, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(35, 81, 20, BR::RedstoneLamp, 0);
        tickN(w, 12);
        // 负域：中继器亮 / 前端粉重启 15 / 远端粉恰 5（16-11）/ 灯亮 / 输入侧梯度 15/14/13。
        const bool negOk = (w.stateAt(-7, 81, -10) & BR::RepeaterStatePoweredFlag) != 0
            && int(w.stateAt(-6, 81, -10) & BR::RedstoneDustPowerMask) == 15
            && int(w.stateAt(4, 81, -10) & BR::RedstoneDustPowerMask) == 5
            && (w.stateAt(5, 81, -10) & BR::RedstoneLampStateOnFlag) != 0
            && int(w.stateAt(-10, 81, -10) & BR::RedstoneDustPowerMask) == 15
            && int(w.stateAt(-9, 81, -10) & BR::RedstoneDustPowerMask) == 14
            && int(w.stateAt(-8, 81, -10) & BR::RedstoneDustPowerMask) == 13;
        ok = ok && negOk;
        if (!negOk)
            diag += QStringLiteral("[neg r=%1 f=%2 far=%3 lamp=%4 g=%5/%6/%7]")
                        .arg((w.stateAt(-7, 81, -10) & BR::RepeaterStatePoweredFlag) != 0)
                        .arg(int(w.stateAt(-6, 81, -10) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(4, 81, -10) & BR::RedstoneDustPowerMask))
                        .arg((w.stateAt(5, 81, -10) & BR::RedstoneLampStateOnFlag) != 0)
                        .arg(int(w.stateAt(-10, 81, -10) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(-9, 81, -10) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(-8, 81, -10) & BR::RedstoneDustPowerMask));
        // 核心域：同构断言全绿。
        const bool coreOk = (w.stateAt(23, 81, 20) & BR::RepeaterStatePoweredFlag) != 0
            && int(w.stateAt(24, 81, 20) & BR::RedstoneDustPowerMask) == 15
            && int(w.stateAt(34, 81, 20) & BR::RedstoneDustPowerMask) == 5
            && (w.stateAt(35, 81, 20) & BR::RedstoneLampStateOnFlag) != 0
            && int(w.stateAt(20, 81, 20) & BR::RedstoneDustPowerMask) == 15
            && int(w.stateAt(21, 81, 20) & BR::RedstoneDustPowerMask) == 14
            && int(w.stateAt(22, 81, 20) & BR::RedstoneDustPowerMask) == 13;
        ok = ok && coreOk;
        if (!coreOk)
            diag += QStringLiteral("[core r=%1 f=%2 far=%3 lamp=%4 g=%5/%6/%7]")
                        .arg((w.stateAt(23, 81, 20) & BR::RepeaterStatePoweredFlag) != 0)
                        .arg(int(w.stateAt(24, 81, 20) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(34, 81, 20) & BR::RedstoneDustPowerMask))
                        .arg((w.stateAt(35, 81, 20) & BR::RedstoneLampStateOnFlag) != 0)
                        .arg(int(w.stateAt(20, 81, 20) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(21, 81, 20) & BR::RedstoneDustPowerMask))
                        .arg(int(w.stateAt(22, 81, 20) & BR::RedstoneDustPowerMask));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2065c signal-extending and end-to-end chain (in a sparse world the same 17-cell"
               " circuit lever, three dust, repeater, eleven dust, lamp is built once on negative"
               " coordinates and once in the core domain: without the repeater the lamp sits"
               " beyond the 15-cell dust decay limit and could never light, with it the first"
               " output dust restarts at 15, the far end holds exactly 5, the lamp lights, and"
               " the input-side dust keeps its 15/14/13 decay gradient in both domains)"
            << (ok ? QString() : diag);
    });

    // ── r2065d：结构钉（def 行 / id 行 / 图集 / state 编解码 / 调档循环 / 全链源钉族）────────────
    runLeg("r2065d structure pins (def row field-exact with the 2/16 thin-plate shape, kMcBlockId"
        " 93 tail row, Count 148, atlas 193 with tiles 191/192, the state codec round-trips facing"
        " delay powered and pending-count fields with out-of-domain clamps, the right-click tune"
        " cycles delay 1-2-3-4-1 while preserving facing powered and count bits, and the full"
        " wiring source-pin family holds across world mesher playercontroller hotbar palette and"
        " icon spec)", [&]() {
        bool ok = true;
        QString diag;
        // (1) def 行逐字段（kDefs 单一权威——破坏 / 音色 / 贴图 / 掉落 / 形态面全部读表）。
        const auto &d = BR::def(BR::Repeater);
        const bool d1 = d.solid == false && d.shape == BR::ShapeRepeater
            && d.hardness == 0.0f && d.toolType == int(BR::NoTool) && !d.requiresTool
            && d.dropId == int(BR::Repeater) && d.dropCount == 1 && d.maxStack == 64
            && d.topTile == 191 && d.bottomTile == 191 && d.sideTile == 191
            && d.frontTile == 192 // frontTile 字段复用承载亮态瓦片（Farmland 湿态同门）
            && BR::materialGroup(BR::Repeater) == BR::GroupStone
            && BR::isRepeater(BR::Repeater) && !BR::isRepeater(BR::NoteBlock);
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1]");
        // (2) id / kMcBlockId 行 / 图集容量（t691 一行一条目纪律的行为级对齐）。
        const bool d2 = int(BR::Repeater) == 147 && int(BR::Count) == 162 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1105 曾 151→154（PumpkinStem/JackOLantern/Cauldron 尾部追加；t1103 曾 149→151、t1097 曾 148→149）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && BR::mcBlockId(quint8(BR::Repeater)) == 93
            && BR::AtlasTileCount == 210; // t1112 lawful 前移：207→209（蛋糕族 tile 207..208 追加）；t1105 lawful 前移：201→207（南瓜族 tile 201..205 + 炼药锅 206 追加；t1103 曾 195→201、t1097 曾 193→195）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2 id=%1 mc=%2 atlas=%3]")
                             .arg(int(BR::Repeater)).arg(BR::mcBlockId(quint8(BR::Repeater)))
                             .arg(BR::AtlasTileCount);
        // (3) state 编解码可逆 + 越界 clamp（朝向 4 向 / 延迟档 1..4 / 输出位 / 挂起计数 0..7）。
        bool d3 = true;
        for (int f = 0; f < 4; ++f) {
            for (int dd = 0; dd < 4; ++dd) {
                const quint8 st = quint8(f | (dd << BR::RepeaterStateDelayShift));
                int dx = 0, dz = 0;
                BR::repeaterOutDelta(st, dx, dz);
                static constexpr int kDx[4] = {1, -1, 0, 0};
                static constexpr int kDz[4] = {0, 0, 1, -1};
                d3 = d3 && dx == kDx[f] && dz == kDz[f]
                    && BR::repeaterDelayTicks(st) == dd + 1
                    && BR::repeaterPendingCount(st) == 0;
            }
        }
        d3 = d3 && BR::repeaterDelayTicks(0xFF) == 4 // 越界位段 clamp（延迟档段取 3 → 档 4）
            && BR::repeaterPendingCount(0xFF) == 7
            && BR::repeaterPendingCountState(0x00, 5) == quint8(5 << BR::RepeaterStateCountShift)
            && BR::repeaterPendingCountState(0xFF, 9) == 0x1F // 越界 clamp = 段清零（jukeboxInsertState 域外值按 0 兜底同门）
            && BR::repeaterPendingCount(BR::repeaterPendingCountState(0x1F, 3)) == 3;
        ok = ok && d3;
        if (!d3) diag += QStringLiteral("[d3]");
        // (4) 右键调档循环：1→2→3→4→1 回绕（repeaterTunedState 单一权威），朝向 / 输出位 / 计数原样。
        bool d4 = true;
        {
            const quint8 base = quint8(2 | BR::RepeaterStatePoweredFlag
                                       | quint8(5 << BR::RepeaterStateCountShift)); // facing=2, lit, cnt=5
            quint8 st = base;
            for (int expect = 2; expect <= 4; ++expect) { // 档 1→2→3→4（expect = 调后档位）
                st = BR::repeaterTunedState(st);
                d4 = d4 && BR::repeaterDelayTicks(st) == expect;
            }
            st = BR::repeaterTunedState(st); // 档 4→1 回绕
            d4 = d4 && BR::repeaterDelayTicks(st) == 1;
            d4 = d4 && (st & 0x03) == 2 // 朝向位原样
                && (st & BR::RepeaterStatePoweredFlag) != 0 // 输出位原样
                && BR::repeaterPendingCount(st) == 5;   // 计数位原样
        }
        ok = ok && d4;
        if (!d4) diag += QStringLiteral("[d4]");
        // (5) 全链源钉族（剥注释 pinSet 锚真实语句 + 裸读文件锚注释体——t989 帮手纪律：pinSet 剥注释
        //     会吞掉注释体锚文本，注释锚走 raw contains，section51 「*/ 84,\n};」同门）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const auto readRaw = [&srcRoot](const char *rel) {
            QFile f(srcRoot + QString::fromLatin1(rel));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QStringList missWorld = pinSet(srcRoot + QStringLiteral("/World/world.cpp"), {
            SrcPin("sourceFeedsCell def unique", "bool World::sourceFeedsCell(int x, int y, int z, int dx, int dz) const", 1),
            SrcPin("repeaterInputOn def unique", "bool World::repeaterInputOn(int x, int y, int z, quint8 state) const", 1),
            SrcPin("seeding directional call", "sourceFeedsCell(x, y, z, d[0], d[2])", 1),
            SrcPin("front-cell reinsert on flip", "m_powerDirty.insert(packGrowthCell(x + fx, y, z + fz));", 1),
            SrcPin("family predicate member", "id == BR::Repeater", 1)});
        const QStringList missWorldH = pinSet(srcRoot + QStringLiteral("/World/world.h"), {
            SrcPin("world.h decl pair", "bool repeaterInputOn(int x, int y, int z, quint8 state) const;", 1)});
        const QStringList missMb = pinSet(srcRoot + QStringLiteral("/World/meshbuilder.cpp"), {
            SrcPin("mesher routing", "b == BlockRegistry::Repeater", 1)});
        const QStringList missPbg = pinSet(srcRoot + QStringLiteral("/World/partialblockgeometry.cpp"), {
            SrcPin("mesher case", "case BlockRegistry::Repeater: {", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("tune call", "BlockRegistry::repeaterTunedState(st)", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("palette entry", "int(BlockRegistry::Repeater),", 1),
            SrcPin("icon family", "case BlockRegistry::Repeater:", 1)});
        // 锚注族（裸读文件 raw contains）。
        const QString wTxt = readRaw("/World/world.cpp");
        const QString whTxt = readRaw("/World/world.h");
        const QString pcTxt = readRaw("/Game/playercontroller.cpp");
        const QString rpTxt = readRaw("/Core/resourcepackmanager.cpp");
        const QString invTxt = readRaw("/../src/ui/Inventory.qml");
        const QString brTxt = readRaw("/Core/blockregistry.cpp");
        const bool anchors = wTxt.contains(QStringLiteral("t1095 中继器：定向输入 + 延迟档翻转输出"))
            && whTxt.contains(QStringLiteral("t1095 中继器（Repeater，受控电源）"))
            && pcTxt.contains(QStringLiteral("t1095 中继器朝向：输出端朝玩家所视方向"))
            && pcTxt.contains(QStringLiteral("t1095 中继器放置支撑预检"))
            && rpTxt.contains(QStringLiteral("t1095 中继器贴地薄板"))
            && invTxt.contains(QStringLiteral("147,  // 红石中继器（Repeater，t1095"));
        // kMcBlockId 行唯一（行注释后缀形态 → 裸读文件口径）。
        const bool rowUnique = brTxt.count(QStringLiteral("/* repeater               */ 93,")) == 1;
        const bool d5 = missWorld.isEmpty() && missWorldH.isEmpty() && missMb.isEmpty()
            && missPbg.isEmpty() && missPc.isEmpty() && missHb.isEmpty()
            && anchors && rowUnique;
        ok = ok && d5;
        if (!d5)
            diag += QStringLiteral("[d5 w=%1 wh=%2 mb=%3 pbg=%4 pc=%5 hb=%6 anc=%7 row=%8]")
                        .arg(missWorld.join(QLatin1Char(',')), missWorldH.join(QLatin1Char(',')),
                             missMb.join(QLatin1Char(',')), missPbg.join(QLatin1Char(',')),
                             missPc.join(QLatin1Char(',')), missHb.join(QLatin1Char(',')))
                        .arg(anchors).arg(rowUnique);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2065d structure pins (def row field-exact with the 2/16 thin-plate shape,"
               " kMcBlockId 93 tail row, Count 148, atlas 193 with tiles 191/192, the state codec"
               " round-trips facing delay powered and pending-count fields with out-of-domain"
               " clamps, the right-click tune cycles delay 1-2-3-4-1 while preserving facing"
               " powered and count bits, and the full wiring source-pin family holds across world"
               " mesher playercontroller hotbar palette and icon spec)"
            << (ok ? QString() : diag);
    });

    // ── r2065e：域门收口复钉 + 相邻族零污染（r2059-r2064 计数钉 + 既有红石族行为回归柱）──────────
    runLeg("r2065e domain-gate re-pin and neighbour-family zero-pollution (the r2059-r2064 gate"
        " line counts and anchor families are untouched, existing redstone circuits keep their"
        " exact former behavior with a redstone block feeding dust feeding a lamp, a lever feeding"
        " dust feeding a powered rail, and the t1094 negative-coordinate circuit wall re-pinned,"
        " and an inert stone edit next to no redstone still triggers zero power recomputes as the"
        " t937 narrow face demands)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 计数钉复钉（world.cpp 直读——本批新增代码不得扰动 r2059-r2064 收口面）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile wcf(srcRoot + QStringLiteral("/World/world.cpp"));
        const QString wSrc = wcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wcf.readAll()) : QString();
        const int matGate = wSrc.count(QStringLiteral(
            "if (!m_chunks.chunkContentPresent(floorDiv(x, Chunk::kSize), floorDiv(z, Chunk::kSize)))")); // =7
        const int c2gate = wSrc.count(QStringLiteral("} else if (y < 0 || y >= m_height) {")); // =3
        const int c2scan = wSrc.count(QStringLiteral("} else if (by < 0 || by >= m_height) {")); // =1
        const int gateTmH = wSrc.count(QStringLiteral("} else if (ty < 0 || ty >= m_height) {")); // =1
        const int aliasPack = wSrc.count(QStringLiteral("return packLeafCell(x, y, z);")); // =1
        const int mask = wSrc.count(QStringLiteral("0x3FFFFFFu"));          // =4
        const int wGate = wSrc.count(QStringLiteral("t1094 域门"));        // =14
        const int wScan = wSrc.count(QStringLiteral("t1094 邻扫门"));      // =5
        const bool g1 = matGate == 7 && c2gate == 3 && c2scan == 1 && gateTmH == 1
            && aliasPack == 1 && mask == 4 && wGate == 14 && wScan == 5
            && wSrc.count(QStringLiteral("t1094 域门（checkPressurePlateOnEdit）两模式分流")) == 1
            && wSrc.contains(QStringLiteral("t1094 写门家族「核心域假设」同族清偿·残面批"));
        ok = ok && g1;
        if (!g1)
            diag += QStringLiteral("[g1 mat=%1 c2=%2 cs=%3 tm=%4 pk=%5 msk=%6 wg=%7 ws=%8]")
                        .arg(matGate).arg(c2gate).arg(c2scan).arg(gateTmH).arg(aliasPack)
                        .arg(mask).arg(wGate).arg(wScan);
        // (2) 既有红石族行为回归柱（fixed 核心域）：红石块→粉→灯 + 拉杆→粉→动力轨（中继器零涉入）。
        World w;
        initFixedRepeaterWorld(w);
        w.setBlock(20, 80, 20, BR::Stone, 0);
        w.setBlock(21, 80, 20, BR::Stone, 0);
        w.setBlock(22, 80, 20, BR::Stone, 0);
        w.setBlock(20, 81, 20, BR::RedstoneBlock, 0);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::RedstoneLamp, 0);
        w.setBlock(20, 81, 24, BR::Lever, 1);
        w.setBlock(21, 81, 24, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 24, BR::GoldenRail, 0);
        tickN(w, 4);
        const bool lampOn = (w.stateAt(22, 81, 20) & BR::RedstoneLampStateOnFlag) != 0;
        const bool railOn = (w.stateAt(22, 81, 24) & BR::GoldenRailStateOnFlag) != 0;
        ok = ok && lampOn && railOn;
        if (!(lampOn && railOn))
            diag += QStringLiteral("[core lamp=%1 rail=%2]").arg(lampOn).arg(railOn);
        // (3) 负坐标电路墙复钉（t1094a 同构——本批代码域门零回归）：红石块→粉→灯（负坐标）。
        World w2{ { 82, 48, 48, 96, 0 } };
        const bool rig2 = w2.isSparse() && w2.loadChunkAt(-1, -1);
        for (int x = -9; x <= -6; ++x)
            for (int z = -9; z <= -6; ++z)
                w2.setBlock(x, 80, z, BR::Stone, 0);
        w2.setBlock(-8, 81, -8, BR::RedstoneBlock, 0);
        w2.setBlock(-7, 81, -8, BR::RedstoneDust, 0);
        w2.setBlock(-6, 81, -8, BR::RedstoneLamp, 0);
        tickN(w2, 4);
        const bool negLamp = rig2
            && (w2.stateAt(-6, 81, -8) & BR::RedstoneLampStateOnFlag) != 0;
        ok = ok && negLamp;
        if (!negLamp) diag += QStringLiteral("[negLamp rig=%1]").arg(rig2);
        // (4) 惰性编辑零重算复钉（t937② 收窄面——中继器入 emitter 族后普通编辑仍零触发）。
        World w3;
        initFixedRepeaterWorld(w3);
        const int passes0 = w3.powerRecomputePasses();
        w3.setBlock(40, 90, 40, BR::Stone, 0); // 高空惰性格（6 邻全空气，无粉 / 无源）
        tickN(w3, 2);
        const bool inert = w3.powerRecomputePasses() == passes0;
        ok = ok && inert;
        if (!inert)
            diag += QStringLiteral("[inert p0=%1 p1=%2]").arg(passes0)
                        .arg(w3.powerRecomputePasses());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2065e domain-gate re-pin and neighbour-family zero-pollution (the r2059-r2064"
               " gate line counts and anchor families are untouched, existing redstone circuits"
               " keep their exact former behavior with a redstone block feeding dust feeding a"
               " lamp, a lever feeding dust feeding a powered rail, and the t1094"
               " negative-coordinate circuit wall re-pinned, and an inert stone edit next to no"
               " redstone still triggers zero power recomputes as the t937 narrow face demands)"
            << (ok ? QString() : diag);
    });

    // ── r2068a：中继器生存配方链（t1098 件一；矩阵 810→811）────────────────────────────────
    //   交付面（fix(t1098) 行注立证）：recipe.cpp repeater 行（底行 3 石 + 中排 火把-红石粉-火把
    //   → 1，工作台 shaped，多重集 {Stone:3, RedstoneTorch:2, Redstone:1} 唯一）+ smelting.cpp
    //   圆石→石头行（兑现石钮 / 石压力板 / 石砖族行注「石头经熔炉烧圆石产出」——此前 kSmelt 漏行，
    //   注释与表不符）。腿面：三合成命中（工作台 canonical + 上移平移 + 2×2 背包栏拒绝 + 中料缺失
    //   拒绝）+ 材料链源钉族（三 id 源钉 + repeater 行钉 + smelt 行行为钉）+ 唯一性钉（同多重集
    //   全表行计数恰 1）。零世界（纯表面，rig 零接触）。
    //   NEG-1 面 = 配方判定面（中排红石粉格变异）→ 本腿恰红；NEG-2 面 = r2068b（连接卫生摘除）。
    runLeg(QStringLiteral("r2068a repeater survival crafting chain (t1098: the table-only shaped"
        " recipe torch-dust-torch over three stone yields one repeater at both grid"
        " translations, is rejected in the 2x2 inventory grid and when the center dust is"
        " missing, the ingredient ids are pinned at source [stone 3, redstone torch 129,"
        " redstone dust item 0x224] with the furnace link cobble-to-stone now present, and"
        " the ingredient multiset is unique across the whole recipe table)"), [&]() {
        bool ok = true;
        QString diag;
        const int kS = int(BR::Stone);
        const int kT = int(BR::RedstoneTorch);
        const int kD = RecipeRegistry::RedstoneId;
        // (1) 三合成命中面（工作台 shaped 精确 + 平移 + 负面两枚；t802 expectCraft 同门）。
        const auto expectCraft = [&](const int *grid, int n, int wantOut, int wantCnt,
                                     const char *tag) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(grid, n);
            if (!r || r->outputId != wantOut || r->outputCount != wantCnt) {
                diag += QStringLiteral("[%1 got-%2] ").arg(QLatin1String(tag))
                            .arg(r ? QStringLiteral("out=%1 cnt=%2")
                                        .arg(r->outputId).arg(r->outputCount)
                                   : QStringLiteral("null"));
                ok = false;
            }
        };
        const auto expectNoMatch = [&](const int *grid, int n, const char *tag) {
            if (RecipeRegistry::match(grid, n)) {
                diag += QStringLiteral("[%1 unexpectedly-matched] ").arg(QLatin1String(tag));
                ok = false;
            }
        };
        const int gCanon[9] = { 0, 0, 0, kT, kD, kT, kS, kS, kS };
        expectCraft(gCanon, 3, int(BR::Repeater), 1, "canon");
        const int gShift[9] = { kT, kD, kT, kS, kS, kS, 0, 0, 0 };
        expectCraft(gShift, 3, int(BR::Repeater), 1, "shift-up");
        const int g2x2[9] = { kT, kD, kS, kS };
        expectNoMatch(g2x2, 2, "2x2-inventory-reject");
        const int gNoDust[9] = { 0, 0, 0, kT, 0, kT, kS, kS, kS };
        expectNoMatch(gNoDust, 3, "missing-dust-reject");
        // (2) 材料链源钉族（剥注释 pinSet 锚真实语句 + smelt 行行为钉——比源钉更强：直接查表）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missBrH = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("stone id source", "Stone         = 3,", 1),
            SrcPin("redstone torch id source", "RedstoneTorch = 129,", 1)});
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("redstone dust item id source", "RedstoneId      = 0x224;", 1)});
        const QStringList missRc = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("repeater recipe row tail", "int(BlockRegistry::Repeater), 1, 1, \"repeater\" }", 1),
            SrcPin("repeater mid-row judgment face",
                   "int(BlockRegistry::RedstoneTorch), RecipeRegistry::RedstoneId,"
                   " int(BlockRegistry::RedstoneTorch),", 1)});
        const bool smeltLink = SmeltingRegistry::smeltResult(int(BR::Cobble)) == int(BR::Stone);
        if (!smeltLink) {
            diag += QStringLiteral("[smelt-link got=%1]")
                        .arg(SmeltingRegistry::smeltResult(int(BR::Cobble)));
            ok = false;
        }
        // (3) 唯一性钉：同原料多重集全表行计数恰 1（多重集比较 = 无序计数对，r2068a 申报面）。
        int multisetRows = 0;
        const int total = RecipeRegistry::recipeCount();
        for (int i = 0; i < total; ++i) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::recipeAt(i);
            if (!r)
                continue;
            int stoneN = 0, torchN = 0, dustN = 0;
            for (int c = 0; c < 9; ++c) {
                if (r->pattern[c] == kS) ++stoneN;
                else if (r->pattern[c] == kT) ++torchN;
                else if (r->pattern[c] == kD) ++dustN;
            }
            if (stoneN == 3 && torchN == 2 && dustN == 1)
                ++multisetRows;
        }
        if (multisetRows != 1) {
            diag += QStringLiteral("[multiset-rows=%1]").arg(multisetRows);
            ok = false;
        }
        const bool pinsOk = missBrH.isEmpty() && missRh.isEmpty() && missRc.isEmpty();
        if (!pinsOk)
            diag += QStringLiteral("[pins br=%1 rh=%2 rc=%3]")
                        .arg(missBrH.join(QLatin1Char(',')), missRh.join(QLatin1Char(',')),
                             missRc.join(QLatin1Char(',')));
        ok = ok && pinsOk;

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2068a repeater survival crafting chain (the table-only shaped recipe"
               " torch-dust-torch over three stone yields one repeater at both grid"
               " translations, is rejected in the 2x2 inventory grid and when the center"
               " dust is missing, the ingredient ids are pinned at source with the furnace"
               " link cobble-to-stone now present, and the ingredient multiset is unique"
               " across the whole recipe table)"
            << (ok ? QString() : diag);
    });
}
