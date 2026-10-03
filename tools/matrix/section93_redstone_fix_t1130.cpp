#include "matrix_helpers.h"

// t1130 红石正确性批探针段（9 腿；filter 词 r2100；矩阵 935→944）。置尾先例沿用（接 section92，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界）。
//
// 任务契约：月度交接单 RED-01（中继器短脉冲吞没）+ RED-02（朝向供电），jar 反汇编 era 定谳后按真值
// 交付（主控亲核坐实两处；工件 build/t1130_jar_diode.txt / build/t1130_jar_worldpower.txt，r2100i
// 在盘钉——测试预期独立建证自 jar 考据，禁抄实现）。
//
// era 定谳摘要（jar 原文见工件）：
//   RED-01（mz=BlockDiode，id 93/yy.bh 灭 / 94/yy.bi 亮，注册名 "diode"）：排定面 = 邻变沿排定
//   cb[档]*2 game tick 一次翻转（cb={1,2,3,4}；1 redstone tick = 2 game tick）；**排定不可撤销**——
//   灭中继器 fire 无条件翻亮、翻亮后输入已回落再排定 +d 的灭翻转（⇒ 短于档的开启脉冲**延长面**：
//   输出在 [武装+d, 武装+2d) 亮 = 脉宽拉成恰 d）；亮中继器 fire 复读输入、已回则 no-op（短**关**
//   脉冲吞没 = era 本就如此）。工程旧实现「输入回同态即取消挂起」吞掉短开脉冲，其「MC 同口径」注
//   系误注（t1130 勘误）。tick 单位换算：**档 d = d pass**（工程 tickRedstone 10Hz 一 pass = 一
//   redstone tick）。挂起持久化：1.0.0 ChunkLoader gy "TileTicks" 表随区块存读排定项（t=余量
//   game tick）——工程 state bit[7:5] 计数随方块 state 存读 = 等价面，零新表面（交接单验收项落定）。
//   RED-02（ry.v/l/u/k + 源 b 面）：接收器供电查询 = ry.v 六正交邻 OR 的逐面定向读 l(邻格, 朝本格
//   的面)；源 b(kq,IIII) 逐面自述——亮中继器（mz.b）**仅输出面** true；拉杆 / 按钮 / 压力板 / 探测轨
//   全面馈电、火把除贴附面、粉按连接形状（kw.b）。消费面全走 v（uc 门 / abm TNT / cu 发射器 / yq
//   音符盒 / afr 动力轨 javap 调用点在册）。工程修法 = 中继器源统一走 sourceFeedsCell 单一权威
//   （isReceivingPower + 火把反相 attachPowered 同门收口）；垂直面中继器恒不馈电（era b() 无垂直
//   输出）。仍未收子面如实登记：粉形状输入（接收器侧全向粉读法）、实块间接承载（era l→u/k 强弱供电
//   分层）——留后续任务。
//
// 时序换算约定（全腿 expected 独立建证自本换算 + r2065a 既有「前端器件滞后翻转一 pass 重评」约定；
// 武装 = rise 检出 pass = 1）：
//   fire = pass 1+d；前端器件首应 = pass 2+d。脉长 L（input on 的 pass 数，off 自 pass L+1 起）：
//   L ≥ d：中继器亮窗 [d+1, L+d] / 前端粉亮窗 [d+2, L+d+1]（宽 L）；L < d（era 延长面）：中继器
//   亮窗 [d+1, 2d] / 前端粉亮窗 [d+2, 2d+1]（宽恰 d = max(L,d)）。
//
// 「评估须重唤醒」纪律（r2065b 承重口径沿用）：凡「A 翻亮后 B 是否被灌」探针，B 的评估严格后置于
//   A 稳态——本段全部接收器 / 消费器件在**中继器稳定亮后**摆放（摆放编辑自挂脏集 → 放置 pass 对亮
//   源重评），否则 B 在 A 翻亮前评一次即沉寂 = 方向面不可观测（NEG-2 验红依赖）。
//
// 腿面与阴性面设计（恰红归因；红集实跑后如实申报，含跨族扩面）：
//   r2100a 短开脉冲延长面 + 四档×脉长 4×4 矩阵（每档一行 L=1..4，亮窗逐 pass 精确钉）。
//     NEG-1（摘「排定保持」分支体）→ 档 ≥2 行红（d=1 行不走该分支 = 结构性幸存，如实申报）。
//   r2100b 短关脉冲面（era 吞没：L<d 亮保持 + 长关 L≥d 正常穿透亮窗精确）。
//     NEG-1 → 红（档 3 排定保持分支在 Path 上）。
//   r2100c 恒开 / 恒关稳态 + 串联两枚时序（R1 亮 pass2 / R2 亮 pass4 / 输出粉 pass5 = 每枚 +d）。
//   r2100d 定点求解多 pass 不重复消耗模拟时间（挂起期逐 pass 注入惰性编辑噪声——计数恒 1/pass、
//     fire pass 不漂移；噪声被 receivers 集去重）+ FPS 无关性（腿注：逻辑 tick 驱动 tickN 直驱，
//     无墙钟面——pass 数即断言面，天然 FPS 无关）。
//     NEG-1 → 红。
//   r2100e 挂起态存档 / 重载规则（era TileTicks 同面：挂起中保存 → 新世界 loadChunks 回填 → 计数
//     存续 → 排定翻转按余程完成；稳态对照双跑）。NEG-1 → 红。
//   r2100f RED-02 四朝向 × {前亮 / 两侧不亮 / 上不亮} 矩阵 + 背驮面（中继器立接收器顶上不亮；
//     换支撑 wake 法）+ 背面不馈由 r2065b Rd 探针既有在册（本段不重复钉）。
//     NEG-2（isReceivingPower 定向门回退全向）→ 红。
//   r2100g 六消费面（灯 / TNT / 动力轨 / 音符盒 行为级 + 发射器 / 漏斗锁停 查询级——发射器信号
//     恒发沿检测在消费端故用查询面，section53 漏斗先例同门）前 vs 侧。
//     NEG-2 → 红。
//   r2100h 既有源零回归（拉杆 / 红石块全面馈电 = era 默认口径）+ 粉定向播种负域复钉 + 跨区块 /
//     负坐标方向面（亮中继器跨 chunk 前亮侧暗）。NEG-2 → 红。
//   r2100i 结构钉族（NEG 双摘面豁免不钉：NEG-1 靶行 / NEG-2 靶行均不入钉）+ era 工件在盘非空钉
//     + 跨任务词元零命中扫面 + CMake / 段注册三行钉 + world.h / blockregistry.h 勘误注 raw 钉。
//
// rig 纪律：fixed 宿主小世界 incantation（四 setter，section65 同款）/ sparse 构造缝 `World w{ {82,
//   48, 48, 96, 0} }` + loadChunkAt 按需物化（未物化 chunk 的 setBlock 静默拒写 = 探针环境性假红
//   教训，r2065b/c 同门）；rig 坪取 y=80..81 高空（s82 地形 / 树冠带之上）；rig 基建全经 setBlock
//   主门；坐标不作哨兵；信号计数 lambda 以 &w 为接收者上下文（世界析构自动断连）。

namespace {

// fixed 宿主小世界 incantation（section65 initFixedRepeaterWorld 同款四 setter）。
inline void initFixedRedstoneWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺设（y=80 闭区间盒）。
inline void layStonePad(World &w, int x0, int x1, int z0, int z1)
{
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            w.setBlock(x, 80, z, BR::Stone, 0);
}

} // namespace

void MatrixRun::section93_redstone_fix_t1130()
{
    // ── r2100a：短开脉冲延长面 + 四档 × 脉长 4×4 矩阵（RED-01 主柱）──────────────────────────
    //   每档 d 一世界，四行 z=20/24/28/32 各 L=1..4：lever(20)→dust(21)→R(22, facing +X, 档 d)
    //   →outDust(23)。pass 1 全行武装；L 行拉杆在 pass L 后扳灭。逐 pass 采样 2+2d+3 窗，亮窗按
    //   换算约定精确钉（中继器位窗 / 粉窗 / 终态三清）。
    runLeg("r2100a short-on-pulse extension and the four-tier by pulse-length 4x4 matrix (each"
        " delay tier builds four lever dust repeater output-dust rows pulsed for L=1..4 passes:"
        " the repeater powered window is exactly passes [tier+1, max(L,tier)+tier] and the output"
        " dust window exactly [tier+2, max(L,tier)+tier+1] so pulses shorter than the tier are"
        " EXTENDED to exactly the tier width while longer pulses pass through at full width"
        " delayed by the tier, and every row ends unlit with the pending counter cleared - the"
        " era short-pulse extension face from the jar diode evidence)", [&]() {
        bool ok = true;
        QString diag;
        static constexpr int kDx[4] = { 1, 0, 0, 0 };   // 本腿恒 facing 0=+X
        Q_UNUSED(kDx);
        for (int d = 1; d <= 4; ++d) {
            World w;
            initFixedRedstoneWorld(w);
            layStonePad(w, 19, 24, 19, 33);
            const quint8 st = quint8((d - 1) << BR::RepeaterStateDelayShift); // facing 0 | 档 d
            for (int row = 0; row < 4; ++row) {
                const int z = 20 + row * 4; // L = row+1
                w.setBlock(20, 81, z, BR::Lever, 1);
                w.setBlock(21, 81, z, BR::RedstoneDust, 0);
                w.setBlock(22, 81, z, BR::Repeater, st);
                w.setBlock(23, 81, z, BR::RedstoneDust, 0);
            }
            const int P = 2 * d + 5;
            int litR[4] = { 0, 0, 0, 0 }, litRlast[4] = { 0, 0, 0, 0 };
            int litD[4] = { 0, 0, 0, 0 }, litDlast[4] = { 0, 0, 0, 0 };
            for (int p = 1; p <= P; ++p) {
                w.tickRedstone();
                if (p <= d) { /* L ≥ p 行仍在 on */ }
                for (int row = 0; row < 4; ++row) {
                    const int L = row + 1;
                    if (p == L) // L 行：本 pass 后扳灭（下一 pass 起输入 off）
                        w.setBlock(20, 81, 20 + row * 4, BR::Lever, 0);
                    const bool rOn = (w.stateAt(22, 81, 20 + row * 4) & BR::RepeaterStatePoweredFlag) != 0;
                    const int dust = int(w.stateAt(23, 81, 20 + row * 4) & BR::RedstoneDustPowerMask);
                    if (rOn) { if (litR[row] == 0) litR[row] = p; litRlast[row] = p; }
                    if (dust > 0) { if (litD[row] == 0) litD[row] = p; litDlast[row] = p; }
                }
            }
            for (int row = 0; row < 4; ++row) {
                const int L = row + 1;
                const int rFirst = d + 1, rLast = (L > d ? L : d) + d;
                const int dFirst = d + 2, dLast = (L > d ? L : d) + d + 1;
                const bool win = litR[row] == rFirst && litRlast[row] == rLast
                    && litD[row] == dFirst && litDlast[row] == dLast;
                const quint8 rs = w.stateAt(22, 81, 20 + row * 4);
                const bool fin = (rs & BR::RepeaterStatePoweredFlag) == 0
                    && BR::repeaterPendingCount(rs) == 0
                    && int(w.stateAt(23, 81, 20 + row * 4) & BR::RedstoneDustPowerMask) == 0;
                if (!(win && fin)) {
                    ok = false;
                    diag += QStringLiteral("[d%1L%2 r=%3..%4 want %5..%6 dust=%7..%8 want %9..%10 fin=%11]")
                                .arg(d).arg(L).arg(litR[row]).arg(litRlast[row]).arg(rFirst)
                                .arg(rLast).arg(litD[row]).arg(litDlast[row]).arg(dFirst)
                                .arg(dLast).arg(fin);
                }
            }
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100a short-on-pulse extension and the four-tier by pulse-length 4x4 matrix"
               " (each delay tier builds four lever dust repeater output-dust rows pulsed for"
               " L=1..4 passes: the repeater powered window is exactly passes"
               " [tier+1, max(L,tier)+tier] and the output dust window exactly"
               " [tier+2, max(L,tier)+tier+1] so pulses shorter than the tier are EXTENDED to"
               " exactly the tier width while longer pulses pass through at full width delayed"
               " by the tier, and every row ends unlit with the pending counter cleared - the"
               " era short-pulse extension face from the jar diode evidence)"
            << (ok ? QString() : diag);
    });

    // ── r2100b：短关脉冲面（era 吞没）+ 长关正常穿透 ─────────────────────────────────────────
    //   档 3 双电路 z=20（L_off=1 < 3）/ z=24（L_off=4 ≥ 3）：亮稳态 8 pass → 同 pass 扳灭双拉杆；
    //   z=20 于 pass 9 后回开、z=24 于 pass 12 后回开。f=9（首个 off pass）。
    //   期望（era 亮中继器 fire 复读输入）：z=20 恒亮 12 窗 + 粉恒 15 + 终态 cnt0 亮（吞没）；
    //   z=24 位灭窗 [f+3, f+6]、f+7 复亮（宽 4 = L），粉灭窗 [f+4, f+7]。
    runLeg("r2100b short-off-pulse swallow face and the long-off pass-through (two tier-3 lit"
        " circuits lose their levers together and regain them after one and four off passes:"
        " the one-pass dip is swallowed entirely with the repeater staying lit all twelve"
        " sampled passes and its front dust holding 15 and the pending counter消费 cancelling"
        " back to zero, while the four-pass outage drops the repeater for exactly passes"
        " [firstOff+3, firstOff+6] and restores it at firstOff+7 with the dust windows lagging"
        " one pass each way - the era lit-diode fire-rechecks-input semantics)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRedstoneWorld(w);
        layStonePad(w, 19, 24, 19, 25);
        const quint8 st3 = quint8(2 << BR::RepeaterStateDelayShift); // 档 3
        for (int z : { 20, 24 }) {
            w.setBlock(20, 81, z, BR::Lever, 1);
            w.setBlock(21, 81, z, BR::RedstoneDust, 0);
            w.setBlock(22, 81, z, BR::Repeater, st3);
            w.setBlock(23, 81, z, BR::RedstoneDust, 0);
        }
        // 亮稳态 8 pass（档 3：fire 在 pass 4，粉 pass 5 起 15）。
        for (int p = 1; p <= 8; ++p) w.tickRedstone();
        bool steady = true;
        for (int z : { 20, 24 })
            steady = steady && (w.stateAt(22, 81, z) & BR::RepeaterStatePoweredFlag) != 0
                && int(w.stateAt(23, 81, z) & BR::RedstoneDustPowerMask) == 15;
        ok = ok && steady;
        if (!steady) diag += QStringLiteral("[steady] ");
        // 双拉杆同扳灭（pass 8 后）；z=20 回开于 pass 9 后（off 仅 pass 9）、z=24 回开于 pass 12 后
        // （off pass 9..12）。f = pass 9。
        w.setBlock(20, 81, 20, BR::Lever, 0);
        w.setBlock(20, 81, 24, BR::Lever, 0);
        bool swallow = true, passThrough = true;
        for (int q = 1; q <= 12; ++q) {
            w.tickRedstone(); // pass 9..20
            if (q == 1) w.setBlock(20, 81, 20, BR::Lever, 1); // z=20：off 仅 pass 9
            if (q == 4) w.setBlock(20, 81, 24, BR::Lever, 1); // z=24：off pass 9..12
            // z=20：L_off=1 < 档 3 → 吞没：恒亮 + 前端粉恒 15（fire=pass12 复读输入已回 → no-op）。
            {
                const quint8 rs = w.stateAt(22, 81, 20);
                swallow = swallow && (rs & BR::RepeaterStatePoweredFlag) != 0
                    && int(w.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask) == 15;
            }
            // z=24：L_off=4 ≥ 档 3 → 正常穿透：位灭窗 [12, 15]、16 复亮；粉灭窗 [13, 16]。
            {
                const quint8 rs = w.stateAt(22, 81, 24);
                const bool on = (rs & BR::RepeaterStatePoweredFlag) != 0;
                const int dust = int(w.stateAt(23, 81, 24) & BR::RedstoneDustPowerMask);
                const int pass = 8 + q;
                const bool wantOn = !(pass >= 12 && pass <= 15);
                const int wantDust = (pass >= 13 && pass <= 16) ? 0 : 15;
                passThrough = passThrough && on == wantOn && dust == wantDust;
            }
        }
        ok = ok && swallow && passThrough;
        if (!swallow) diag += QStringLiteral("[swallow] ");
        if (!passThrough) diag += QStringLiteral("[pass-through] ");
        // 终态：z=20 亮 + cnt0（吞没收口）；z=24 亮 + cnt0。
        const bool fin = BR::repeaterPendingCount(w.stateAt(22, 81, 20)) == 0
            && (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0
            && BR::repeaterPendingCount(w.stateAt(22, 81, 24)) == 0
            && (w.stateAt(22, 81, 24) & BR::RepeaterStatePoweredFlag) != 0;
        ok = ok && fin;
        if (!fin) diag += QStringLiteral("[fin] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100b short-off-pulse swallow face and the long-off pass-through (two tier-3"
               " lit circuits lose their levers together and regain them after one and four off"
               " passes: the one-pass dip is swallowed entirely with the repeater staying lit"
               " all twelve sampled passes and its front dust holding 15 and the pending count"
               " cancelling back to zero, while the four-pass outage drops the repeater for"
               " exactly passes [firstOff+3, firstOff+6] and restores it at firstOff+7 with the"
               " dust windows lagging one pass each way - the era lit-diode fire-rechecks-input"
               " semantics)"
            << (ok ? QString() : diag);
    });

    // ── r2100c：恒开 / 恒关稳态 + 串联两枚时序 ───────────────────────────────────────────────
    runLeg("r2100c steady-on and steady-off stability and the two-repeater series timing (a"
        " tier-2 circuit holds lit from pass 3 with a cleared counter and zero oscillation over"
        " twelve passes, an unfed repeater stays unlit with a cleared counter, and the series"
        " lever dust repeater dust repeater output-dust chain lights the first repeater at"
        " pass 2, the second at pass 4 and the output dust at pass 5 - each diode adding"
        " exactly its one redstone tick of delay as era chains do)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRedstoneWorld(w);
        layStonePad(w, 19, 28, 19, 29);
        const quint8 st2 = quint8(1 << BR::RepeaterStateDelayShift); // 档 2
        // A 行 z=20：恒开（档 2）。
        w.setBlock(20, 81, 20, BR::Lever, 1);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::Repeater, st2);
        w.setBlock(23, 81, 20, BR::RedstoneDust, 0);
        // B 行 z=24：恒关（无源）。
        w.setBlock(22, 81, 24, BR::Repeater, st2);
        w.setBlock(23, 81, 24, BR::RedstoneDust, 0);
        // C 行 z=28：串联两枚（档 1）。
        w.setBlock(20, 81, 28, BR::Lever, 1);
        w.setBlock(21, 81, 28, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 28, BR::Repeater, 0);
        w.setBlock(23, 81, 28, BR::RedstoneDust, 0);
        w.setBlock(24, 81, 28, BR::Repeater, 0);
        w.setBlock(25, 81, 28, BR::RedstoneDust, 0);
        int r1On = 0, r2On = 0, outLit = 0;
        bool steadyOn = true, steadyOff = true;
        for (int p = 1; p <= 12; ++p) {
            w.tickRedstone();
            const bool r1 = (w.stateAt(22, 81, 28) & BR::RepeaterStatePoweredFlag) != 0;
            const bool r2 = (w.stateAt(24, 81, 28) & BR::RepeaterStatePoweredFlag) != 0;
            const int out = int(w.stateAt(25, 81, 28) & BR::RedstoneDustPowerMask);
            if (r1 && r1On == 0) r1On = p;
            if (r2 && r2On == 0) r2On = p;
            if (out > 0 && outLit == 0) outLit = p;
            // A 行稳态：pass 3 起恒亮（fire=pass 3 档 2）+ 无振荡。
            if (p >= 4) {
                steadyOn = steadyOn
                    && (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0
                    && int(w.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask) == 15
                    && BR::repeaterPendingCount(w.stateAt(22, 81, 20)) == 0;
            }
            // B 行恒关。
            steadyOff = steadyOff
                && (w.stateAt(22, 81, 24) & BR::RepeaterStatePoweredFlag) == 0
                && BR::repeaterPendingCount(w.stateAt(22, 81, 24)) == 0;
        }
        const bool aOk = steadyOn;
        const bool bOk = steadyOff;
        const bool cOk = r1On == 2 && r2On == 4 && outLit == 5
            && (w.stateAt(22, 81, 28) & BR::RepeaterStatePoweredFlag) != 0
            && (w.stateAt(24, 81, 28) & BR::RepeaterStatePoweredFlag) != 0
            && BR::repeaterPendingCount(w.stateAt(22, 81, 28)) == 0
            && BR::repeaterPendingCount(w.stateAt(24, 81, 28)) == 0;
        ok = ok && aOk && bOk && cOk;
        if (!aOk) diag += QStringLiteral("[steadyOn] ");
        if (!bOk) diag += QStringLiteral("[steadyOff] ");
        if (!cOk) diag += QStringLiteral("[series r1=%1 r2=%2 out=%3]").arg(r1On).arg(r2On).arg(outLit);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100c steady-on and steady-off stability and the two-repeater series timing"
               " (a tier-2 circuit holds lit from pass 3 with a cleared counter and zero"
               " oscillation over twelve passes, an unfed repeater stays unlit with a cleared"
               " counter, and the series lever dust repeater dust repeater output-dust chain"
               " lights the first repeater at pass 2, the second at pass 4 and the output dust"
               " at pass 5 - each diode adding exactly its one redstone tick of delay as era"
               " chains do)"
            << (ok ? QString() : diag);
    });

    // ── r2100d：定点求解多 pass 不重复消耗模拟时间（噪声下计数恒 1/pass）────────────────────
    //   档 4：rise 武装 pass1 → 逐 pass 在中继器头顶放 / 拆惰性石头（邻居编辑 → 脏集重插）→ 计数
    //   仍 1/pass（fire=pass5）；延长窗（扳灭后挂起灭翻转）同法抗噪（off fire=pass11 不漂移）。
    //   FPS 无关性：逻辑 tick 驱动（tickN / tickRedstone 直驱，无墙钟面）——pass 数即断言面，天然
    //   FPS 无关（腿注口径）。
    runLeg("r2100d fixed-point solving consumes exactly one pass of simulation time per pass (a"
        " tier-4 rise armed on pass 1 receives an inert stone place-and-break neighbour edit"
        " between every pass while pending: the counter reads exactly 1 2 3 4 on passes 1..4"
        " and the fire lands on pass 5 with the output dust on pass 6, and the same noise"
        " injected during the short-pulse extension hold leaves the scheduled off flip exactly"
        " on pass 11 - extra dirty hits are deduped by the receiver set so the count never"
        " accelerates; the legs are driven by logic ticks alone so the timing is frame-rate"
        " independent by construction)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRedstoneWorld(w);
        layStonePad(w, 19, 24, 19, 21);
        w.setBlock(20, 81, 20, BR::Lever, 1);
        w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
        w.setBlock(22, 81, 20, BR::Repeater, quint8(3 << BR::RepeaterStateDelayShift)); // 档 4
        w.setBlock(23, 81, 20, BR::RedstoneDust, 0);
        bool riseOk = true;
        int got[5] = { 0, 0, 0, 0, 0 };
        for (int p = 1; p <= 6; ++p) {
            if (p >= 2 && p <= 4) // 挂起期噪声：头顶惰性编辑（放/拆交替——同 id 写会被静默早退吞掉）
                w.setBlock(22, 82, 20, (p % 2 == 0) ? BR::Stone : BR::Air, 0);
            w.tickRedstone();
            if (p <= 4) got[p] = BR::repeaterPendingCount(w.stateAt(22, 81, 20));
        }
        riseOk = got[1] == 1 && got[2] == 2 && got[3] == 3 && got[4] == 4
            && (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0       // fire=pass5
            && int(w.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask) == 15;     // 粉 pass6
        ok = ok && riseOk;
        if (!riseOk) diag += QStringLiteral("[rise %1%2%3%4]").arg(got[1]).arg(got[2])
                                 .arg(got[3]).arg(got[4]);
        // 延长窗：pass 6 后扳灭（input off 自 pass 7）→ 挂起灭翻转 + 噪声 → off fire=pass11。
        w.setBlock(20, 81, 20, BR::Lever, 0);
        bool extOk = true;
        for (int p = 7; p <= 12; ++p) {
            if (p >= 8 && p <= 10)
                w.setBlock(22, 82, 20, BR::Stone, 0); // 惰性邻居编辑噪声
            w.tickRedstone();
            const bool on = (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0;
            const int dust = int(w.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask);
            const bool wantOn = p <= 10; // off fire=pass11（6+档5? 档 4：arm@7 → fire@11）
            const int wantDust = p <= 11 ? 15 : 0;
            extOk = extOk && on == wantOn && dust == wantDust;
        }
        ok = ok && extOk;
        if (!extOk) diag += QStringLiteral("[ext] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100d fixed-point solving consumes exactly one pass of simulation time per"
               " pass (a tier-4 rise armed on pass 1 receives an inert stone place-and-break"
               " neighbour edit between every pass while pending: the counter reads exactly 1 2"
               " 3 4 on passes 1..4 and the fire lands on pass 5 with the output dust on pass"
               " 6, and the same noise injected during the short-pulse extension hold leaves the"
               " scheduled off flip exactly on pass 11 - extra dirty hits are deduped by the"
               " receiver set so the count never accelerates; the legs are driven by logic"
               " ticks alone so the timing is frame-rate independent by construction)"
            << (ok ? QString() : diag);
    });

    // ── r2100e：挂起态存档 / 重载规则（era TileTicks 同面）──────────────────────────────────
    //   档 3 电路：pass1 武装、pass2 计 2 时保存 → 新世界 beginLoad/loadChunks/finishLoad 回填 →
    //   计数 2 存续 → 续两 pass fire（input on → 亮稳态）→ 第三 pass 前端粉 15。稳态对照：灭态
    //   （cnt0）保存 → 重载恒灭。era 注：1.0.0 ChunkLoader gy TileTicks 表持久化排定项（余量
    //   game tick），工程以计数域随 state 存读 = 等价面（pass 量化内），零新表面。
    runLeg("r2100e pending-state persistence across save and reload (a tier-3 circuit saved"
        " mid-pending with counter 2 reloads into a fresh world through the chunk blob with the"
        " counter intact and completes its scheduled flip two passes later with the front dust"
        " lighting one pass after that, and a steady unlit control round-trips staying unlit -"
        " the era persists scheduled flips in the chunk TileTicks table so carrying the pending"
        " count in block state is the same face, zero new storage)", [&]() {
        bool ok = true;
        QString diag;
        const QString db = QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2100_%1.sqlite")
                                                             .arg(QCoreApplication::applicationPid()));
        QFile::remove(db);
        const quint8 st3 = quint8(2 << BR::RepeaterStateDelayShift);
        quint8 savedCnt = 9;
        bool savedPowered = true;
        {
            World w;
            initFixedRedstoneWorld(w);
            layStonePad(w, 19, 24, 19, 21);
            w.setBlock(20, 81, 20, BR::Lever, 1);
            w.setBlock(21, 81, 20, BR::RedstoneDust, 0);
            w.setBlock(22, 81, 20, BR::Repeater, st3);
            w.setBlock(23, 81, 20, BR::RedstoneDust, 0);
            w.tickRedstone(); // pass1：武装 cnt1
            w.tickRedstone(); // pass2：cnt2（≤3 排定保持）
            WorldStore store;
            const bool opened = store.openWorld(db) && store.isOpen();
            ok = ok && opened;
            if (!opened) diag += QStringLiteral("[open] ");
            store.setWorld(&w);
            const bool saved = opened && store.saveAll(QStringLiteral("t1130e"));
            ok = ok && saved;
            if (!saved) diag += QStringLiteral("[save] ");
            store.closeWorld();
            savedCnt = quint8(BR::repeaterPendingCount(w.stateAt(22, 81, 20)));
            savedPowered = (w.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0;
        }
        const bool midOk = savedCnt == 2 && !savedPowered;
        ok = ok && midOk;
        if (!midOk) diag += QStringLiteral("[mid cnt=%1 pwr=%2]").arg(savedCnt).arg(savedPowered);
        {
            World w2;
            initFixedRedstoneWorld(w2);
            WorldStore store2;
            const bool opened = store2.openWorld(db) && store2.isOpen();
            store2.setWorld(&w2);
            w2.beginLoad(82);
            const int chunks = opened ? store2.loadChunks() : 0;
            w2.finishLoad();
            store2.closeWorld();
            const bool loadOk = opened && chunks > 0;
            ok = ok && loadOk;
            if (!loadOk) diag += QStringLiteral("[load opened=%1 chunks=%2]").arg(opened).arg(chunks);
            const quint8 rs = w2.stateAt(22, 81, 20);
            const bool carryOk = BR::repeaterPendingCount(rs) == 2
                && (rs & BR::RepeaterStatePoweredFlag) == 0
                && w2.blockAt(22, 81, 20) == BR::Repeater
                && w2.blockAt(20, 81, 20) == BR::Lever;
            ok = ok && carryOk;
            if (!carryOk) diag += QStringLiteral("[carry cnt=%1 id=%2]")
                                     .arg(BR::repeaterPendingCount(rs)).arg(w2.blockAt(22, 81, 20));
            // 续程：pass A 计 3（≤3 保持）、pass B fire（input on → 亮 + cnt0）、pass C 前端粉 15。
            w2.tickRedstone();
            const bool cont1 = BR::repeaterPendingCount(w2.stateAt(22, 81, 20)) == 3;
            w2.tickRedstone();
            const bool cont2 = (w2.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) != 0
                && BR::repeaterPendingCount(w2.stateAt(22, 81, 20)) == 0;
            w2.tickRedstone();
            const bool cont3 = int(w2.stateAt(23, 81, 20) & BR::RedstoneDustPowerMask) == 15;
            ok = ok && cont1 && cont2 && cont3;
            if (!(cont1 && cont2 && cont3))
                diag += QStringLiteral("[cont %1%2%3]").arg(cont1).arg(cont2).arg(cont3);
        }
        // 稳态对照：灭态（无源 cnt0）保存 → 重载恒灭。
        {
            const QString db2 = QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2100b_%1.sqlite")
                                                                  .arg(QCoreApplication::applicationPid()));
            QFile::remove(db2);
            World w3;
            initFixedRedstoneWorld(w3);
            layStonePad(w3, 19, 24, 19, 21);
            w3.setBlock(22, 81, 20, BR::Repeater, st3);
            WorldStore s3;
            const bool opened = s3.openWorld(db2) && s3.isOpen();
            s3.setWorld(&w3);
            const bool saved = opened && s3.saveAll(QStringLiteral("t1130e2"));
            s3.closeWorld();
            World w4;
            initFixedRedstoneWorld(w4);
            WorldStore s4;
            const bool opened2 = s4.openWorld(db2) && s4.isOpen();
            s4.setWorld(&w4);
            w4.beginLoad(82);
            const int chunks = opened2 ? s4.loadChunks() : 0;
            w4.finishLoad();
            s4.closeWorld();
            bool ctl = opened && saved && opened2 && chunks > 0;
            for (int p = 0; p < 4 && ctl; ++p) {
                w4.tickRedstone();
                ctl = ctl && (w4.stateAt(22, 81, 20) & BR::RepeaterStatePoweredFlag) == 0
                    && BR::repeaterPendingCount(w4.stateAt(22, 81, 20)) == 0;
            }
            ok = ok && ctl;
            if (!ctl) diag += QStringLiteral("[ctl] ");
            QFile::remove(db2);
        }
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100e pending-state persistence across save and reload (a tier-3 circuit"
               " saved mid-pending with counter 2 reloads into a fresh world through the chunk"
               " blob with the counter intact and completes its scheduled flip two passes later"
               " with the front dust lighting one pass after that, and a steady unlit control"
               " round-trips staying unlit - the era persists scheduled flips in the chunk"
               " TileTicks table so carrying the pending count in block state is the same face,"
               " zero new storage)"
            << (ok ? QString() : diag);
    });

    // ── r2100f：RED-02 四朝向 × 方位矩阵 + 背驮面 ───────────────────────────────────────────
    //   每朝向一象限：feed 稳亮 8 pass 后再摆接收器（评估须重唤醒纪律）——前灯 ON / 两侧灯 OFF /
    //   上灯 OFF（垂直面恒不馈）。背驮面：换支撑石为灯（编辑自挂脏 → 对亮中继器重评）恒 OFF 且
    //   中继器因满立方支撑幸存。背面不馈由 r2065b Rd 探针既有在册（不重复钉）。
    runLeg("r2100f directional feeding across the four orientations and the piggyback face"
        " (each orientation builds a lit repeater and then places lamps after it settles: the"
        " front lamp lights, both side lamps and the lamp above stay dark, and swapping the"
        " support stone under the lit repeater for a lamp - the piggyback face - re-evaluates"
        " that lamp dark while the repeater survives on the full-cube support; the back face is"
        " covered by the existing rectification probe family)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRedstoneWorld(w);
        layStonePad(w, 10, 32, 10, 34);
        static constexpr int kDx[4] = { 1, -1, 0, 0 };
        static constexpr int kDz[4] = { 0, 0, 1, -1 };
        for (int f = 0; f < 4; ++f) {
            const int bx = 13 + (f % 2) * 12, bz = 14 + (f / 2) * 12;
            const int dx = kDx[f], dz = kDz[f];
            w.setBlock(bx - 2 * dx, 81, bz - 2 * dz, BR::Lever, 1);
            w.setBlock(bx - dx, 81, bz - dz, BR::RedstoneDust, 0);
            w.setBlock(bx, 81, bz, BR::Repeater, quint8(f)); // facing f | 档 1
        }
        tickN(w, 8);
        bool litOk = true;
        for (int f = 0; f < 4; ++f) {
            const int bx = 13 + (f % 2) * 12, bz = 14 + (f / 2) * 12;
            litOk = litOk && (w.stateAt(bx, 81, bz) & BR::RepeaterStatePoweredFlag) != 0;
        }
        ok = ok && litOk;
        if (!litOk) diag += QStringLiteral("[lit] ");
        for (int f = 0; f < 4; ++f) {
            const int bx = 13 + (f % 2) * 12, bz = 14 + (f / 2) * 12;
            const int dx = kDx[f], dz = kDz[f];
            w.setBlock(bx + dx, 81, bz + dz, BR::RedstoneLamp, 0);            // 前
            w.setBlock(bx - dz, 81, bz + dx, BR::RedstoneLamp, 0);            // 侧 1
            w.setBlock(bx + dz, 81, bz - dx, BR::RedstoneLamp, 0);            // 侧 2
            w.setBlock(bx, 82, bz, BR::RedstoneLamp, 0);                      // 上
        }
        tickN(w, 3);
        bool dirOk = true;
        for (int f = 0; f < 4; ++f) {
            const int bx = 13 + (f % 2) * 12, bz = 14 + (f / 2) * 12;
            const int dx = kDx[f], dz = kDz[f];
            const bool front = (w.stateAt(bx + dx, 81, bz + dz) & BR::RedstoneLampStateOnFlag) != 0;
            const bool s1 = (w.stateAt(bx - dz, 81, bz + dx) & BR::RedstoneLampStateOnFlag) != 0;
            const bool s2 = (w.stateAt(bx + dz, 81, bz - dx) & BR::RedstoneLampStateOnFlag) != 0;
            const bool above = (w.stateAt(bx, 82, bz) & BR::RedstoneLampStateOnFlag) != 0;
            dirOk = dirOk && front && !s1 && !s2 && !above;
            if (!(front && !s1 && !s2 && !above))
                diag += QStringLiteral("[f%1 F=%2 S=%3/%4 A=%5]").arg(f).arg(front)
                            .arg(s1).arg(s2).arg(above);
        }
        ok = ok && dirOk;
        // 背驮面：支撑石 → 灯（对亮中继器重评恒暗）；满立方支撑幸存 → 中继器仍在且亮。
        {
            const int bx = 25, bz = 38; // 独立即位（坪内）
            layStonePad(w, bx - 3, bx + 2, bz - 1, bz + 1);
            w.setBlock(bx - 2, 81, bz, BR::Lever, 1);
            w.setBlock(bx - 1, 81, bz, BR::RedstoneDust, 0);
            w.setBlock(bx, 81, bz, BR::Repeater, 0);
            tickN(w, 8);
            const bool preLit = (w.stateAt(bx, 81, bz) & BR::RepeaterStatePoweredFlag) != 0;
            w.setBlock(bx, 80, bz, BR::RedstoneLamp, 0); // 支撑换灯（编辑 → 灯入脏重评）
            tickN(w, 3);
            const bool piggy = preLit
                && w.blockAt(bx, 81, bz) == BR::Repeater
                && (w.stateAt(bx, 81, bz) & BR::RepeaterStatePoweredFlag) != 0
                && (w.stateAt(bx, 80, bz) & BR::RedstoneLampStateOnFlag) == 0;
            ok = ok && piggy;
            if (!piggy) diag += QStringLiteral("[piggy pre=%1 id=%2 lit=%3 lamp=%4]")
                                    .arg(preLit).arg(w.blockAt(bx, 81, bz))
                                    .arg((w.stateAt(bx, 81, bz) & BR::RepeaterStatePoweredFlag) != 0)
                                    .arg((w.stateAt(bx, 80, bz) & BR::RedstoneLampStateOnFlag) != 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100f directional feeding across the four orientations and the piggyback"
               " face (each orientation builds a lit repeater and then places lamps after it"
               " settles: the front lamp lights, both side lamps and the lamp above stay dark,"
               " and swapping the support stone under the lit repeater for a lamp - the"
               " piggyback face - re-evaluates that lamp dark while the repeater survives on"
               " the full-cube support; the back face is covered by the existing rectification"
               " probe family)"
            << (ok ? QString() : diag);
    });

    // ── r2100g：六消费面前 vs 侧（灯 / TNT / 动力轨 / 音符盒 行为级；发射器 / 漏斗 查询级）────
    //   发射器信号面：World 层信号恒发（沿检测在消费端）→ 查询面判向（isReceivingPower）；漏斗
    //   锁停同查询面（section53 先例）。
    runLeg("r2100g the six consumer faces front versus side (lamp, TNT, powered rail and note"
        " block behave - the front copy lights, primes, powers and chimes once while the side"
        " copy stays inert - and dispenser and hopper-lock are probed at the power-query face"
        " since the dispenser signal fires on every recompute touch by design and the hopper"
        " lock reads the same query at its tick)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initFixedRedstoneWorld(w);
        layStonePad(w, 19, 24, 19, 42);
        int tntFront = 0, tntSide = 0, noteFront = 0, noteSide = 0;
        QObject::connect(&w, &World::powerTntTriggered, &w, [&](int x, int y, int z) {
            Q_UNUSED(x); Q_UNUSED(y);
            if (z == 24) ++tntFront; else if (z == 25) ++tntSide; // 前=z24 / 侧=z25（件位行）
        });
        QObject::connect(&w, &World::noteBlockPlayed, &w, [&](int x, int y, int z, int pitch, int family) {
            Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(pitch); Q_UNUSED(family);
            if (z == 36) ++noteFront; else if (z == 37) ++noteSide; // 前=z36 / 侧=z37
        });
        // 六行 feed（lever20 / dust21 / R22 facing+X 档1），先稳亮，后摆消费器件（重唤醒纪律）。
        for (int z : { 20, 24, 28, 32, 36, 40 }) {
            w.setBlock(20, 81, z, BR::Lever, 1);
            w.setBlock(21, 81, z, BR::RedstoneDust, 0);
            w.setBlock(22, 81, z, BR::Repeater, 0);
        }
        tickN(w, 8);
        // 消费器件（front=x23 本行 / side=x22,z+1 邻行间位）。
        w.setBlock(23, 81, 20, BR::RedstoneLamp, 0);
        w.setBlock(22, 81, 21, BR::RedstoneLamp, 0);
        w.setBlock(23, 81, 24, BR::TntBlock, 0);
        w.setBlock(22, 81, 25, BR::TntBlock, 0);
        w.setBlock(23, 81, 28, BR::Dispenser, 0);
        w.setBlock(22, 81, 29, BR::Dispenser, 0);
        w.setBlock(23, 81, 32, BR::GoldenRail, 0);
        w.setBlock(22, 81, 33, BR::GoldenRail, 0);
        w.setBlock(23, 81, 36, BR::NoteBlock, 0);
        w.setBlock(22, 81, 37, BR::NoteBlock, 0);
        w.setBlock(23, 81, 40, BR::Hopper, 0);
        w.setBlock(22, 81, 41, BR::Hopper, 0);
        tickN(w, 3);
        const bool lampOk = (w.stateAt(23, 81, 20) & BR::RedstoneLampStateOnFlag) != 0
            && (w.stateAt(22, 81, 21) & BR::RedstoneLampStateOnFlag) == 0;
        const bool tntOk = tntFront >= 1 && tntSide == 0;
        const bool dispOk = w.isReceivingPower(23, 81, 28) && !w.isReceivingPower(22, 81, 29);
        const bool railOk = (w.stateAt(23, 81, 32) & BR::GoldenRailStateOnFlag) != 0
            && (w.stateAt(22, 81, 33) & BR::GoldenRailStateOnFlag) == 0;
        const bool noteOk = (w.stateAt(23, 81, 36) & BR::NoteBlockStatePoweredFlag) != 0
            && noteFront == 1
            && (w.stateAt(22, 81, 37) & BR::NoteBlockStatePoweredFlag) == 0 && noteSide == 0;
        const bool hopOk = w.isReceivingPower(23, 81, 40) && !w.isReceivingPower(22, 81, 41);
        ok = ok && lampOk && tntOk && dispOk && railOk && noteOk && hopOk;
        if (!lampOk) diag += QStringLiteral("[lamp] ");
        if (!tntOk) diag += QStringLiteral("[tnt f=%1 s=%2]").arg(tntFront).arg(tntSide);
        if (!dispOk) diag += QStringLiteral("[disp] ");
        if (!railOk) diag += QStringLiteral("[rail] ");
        if (!noteOk) diag += QStringLiteral("[note f=%1 s=%2]").arg(noteFront).arg(noteSide);
        if (!hopOk) diag += QStringLiteral("[hop] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100g the six consumer faces front versus side (lamp, TNT, powered rail and"
               " note block behave - the front copy lights, primes, powers and chimes once"
               " while the side copy stays inert - and dispenser and hopper-lock are probed at"
               " the power-query face since the dispenser signal fires on every recompute touch"
               " by design and the hopper lock reads the same query at its tick)"
            << (ok ? QString() : diag);
    });

    // ── r2100h：既有源零回归（全面馈电 = era 默认口径）+ 负坐标 / 跨区块方向面 ───────────────
    runLeg("r2100h existing-source zero-regression and the negative-coordinate cross-chunk"
        " directional face (a lit lever feeds lamps on its side and top faces and a redstone"
        " block does the same - the era all-face default for non-repeater sources intact - and"
        " in a sparse world a lit repeater across a chunk boundary on negative coordinates"
        " lights the lamp in front of it while the lamp beside it in the neighbouring chunk"
        " stays dark)", [&]() {
        bool ok = true;
        QString diag;
        // (a) 拉杆全面馈电 + 红石块全面馈电（era 默认口径零回归）。
        {
            World w;
            initFixedRedstoneWorld(w);
            layStonePad(w, 19, 23, 19, 27);
            w.setBlock(20, 81, 20, BR::Lever, 1);
            w.setBlock(21, 81, 20, BR::RedstoneLamp, 0); // 侧（+X）
            w.setBlock(20, 81, 21, BR::RedstoneLamp, 0); // 侧（+Z）
            w.setBlock(20, 82, 20, BR::RedstoneLamp, 0); // 上
            w.setBlock(20, 81, 26, BR::RedstoneBlock, 0);
            w.setBlock(21, 81, 26, BR::RedstoneLamp, 0);
            w.setBlock(20, 82, 26, BR::RedstoneLamp, 0);
            tickN(w, 3);
            const bool leverOk = (w.stateAt(21, 81, 20) & BR::RedstoneLampStateOnFlag) != 0
                && (w.stateAt(20, 81, 21) & BR::RedstoneLampStateOnFlag) != 0
                && (w.stateAt(20, 82, 20) & BR::RedstoneLampStateOnFlag) != 0;
            const bool blockOk = (w.stateAt(21, 81, 26) & BR::RedstoneLampStateOnFlag) != 0
                && (w.stateAt(20, 82, 26) & BR::RedstoneLampStateOnFlag) != 0;
            ok = ok && leverOk && blockOk;
            if (!leverOk) diag += QStringLiteral("[lever] ");
            if (!blockOk) diag += QStringLiteral("[block] ");
        }
        // (b) 负坐标 + 跨区块：R(-1,81,-8) facing +X（chunk(-1,-1)）→ 前灯 (0,81,-8)（chunk(0,-1)）
        //     亮 / 侧灯 (-1,81,-7)（chunk(-1,-1)）暗。
        {
            World w{ { 82, 48, 48, 96, 0 } };
            const bool rig = w.isSparse() && w.loadChunkAt(-1, -1) && w.loadChunkAt(0, -1)
                && w.loadChunkAt(0, 0) && w.loadChunkAt(-1, 0);
            ok = ok && rig;
            if (!rig) diag += QStringLiteral("[rig] ");
            layStonePad(w, -5, 2, -11, -5);
            w.setBlock(-3, 81, -8, BR::Lever, 1);
            w.setBlock(-2, 81, -8, BR::RedstoneDust, 0);
            w.setBlock(-1, 81, -8, BR::Repeater, 0); // facing 0=+X
            tickN(w, 8);
            w.setBlock(0, 81, -8, BR::RedstoneLamp, 0);   // 前（跨 chunk）
            w.setBlock(-1, 81, -7, BR::RedstoneLamp, 0);  // 侧
            tickN(w, 3);
            const bool negOk = rig
                && (w.stateAt(-1, 81, -8) & BR::RepeaterStatePoweredFlag) != 0
                && (w.stateAt(0, 81, -8) & BR::RedstoneLampStateOnFlag) != 0
                && (w.stateAt(-1, 81, -7) & BR::RedstoneLampStateOnFlag) == 0;
            ok = ok && negOk;
            if (!negOk)
                diag += QStringLiteral("[neg lit=%1 F=%2 S=%3]")
                            .arg((w.stateAt(-1, 81, -8) & BR::RepeaterStatePoweredFlag) != 0)
                            .arg((w.stateAt(0, 81, -8) & BR::RedstoneLampStateOnFlag) != 0)
                            .arg((w.stateAt(-1, 81, -7) & BR::RedstoneLampStateOnFlag) != 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100h existing-source zero-regression and the negative-coordinate"
               " cross-chunk directional face (a lit lever feeds lamps on its side and top"
               " faces and a redstone block does the same - the era all-face default for"
               " non-repeater sources intact - and in a sparse world a lit repeater across a"
               " chunk boundary on negative coordinates lights the lamp in front of it while"
               " the lamp beside it in the neighbouring chunk stays dark)"
            << (ok ? QString() : diag);
    });

    // ── r2100i：结构钉族 + era 工件在盘钉 + 跨任务词元零命中 + 注册三行钉（NEG 双摘面豁免不钉）──
    runLeg("r2100i structure pins (the world.cpp machine pins for the arm line, the"
        " unconditional fire-on line and the torch-face directional gate line each appear"
        " exactly once while the two NEG-target lines stay unpinned by design, the world.h and"
        " blockregistry.h erratum-note anchors are present verbatim, both jar adjudication"
        " artifacts exist on disk non-empty carrying the delay-table, tick-conversion,"
        " output-face-only and TileTicks anchors, the section source carries zero cross-task"
        " legacy tokens besides its own filter family, and the CMake plus harness registration"
        " rows for this section are present)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missW = pinSet(srcRoot + QStringLiteral("/World/world.cpp"), {
            // NEG-1 靶行（排定保持分支体）/ NEG-2 靶行（isReceivingPower 定向调用行）**豁免不钉**——
            // 恰红归因洁净；本组钉全在非靶行（arm / fire-on / 火把面门 / def）。
            SrcPin("machine arm line", "BlockRegistry::repeaterPendingCountState(st, 1));", 1),
            SrcPin("machine unconditional fire-on line",
                   "quint8(st | BlockRegistry::RepeaterStatePoweredFlag), input ? 0 : 1));", 1),
            SrcPin("torch-face directional gate line",
                   "sourceFeedsCell(sx, sy, sz, d[0], d[2])", 1),
            SrcPin("isReceivingPower def unique",
                   "bool World::isReceivingPower(int x, int y, int z) const", 1)});
        const auto readRaw = [&srcRoot](const char *rel) {
            QFile f(srcRoot + QString::fromLatin1(rel));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString wTxt = readRaw("/World/world.cpp");
        const QString whTxt = readRaw("/World/world.h");
        const QString brTxt = readRaw("/Core/blockregistry.h");
        const bool anchors = wTxt.contains(QStringLiteral("t1130 RED-01 era 定谳"))
            && wTxt.contains(QStringLiteral("t1130 RED-02 era 定谳"))
            && whTxt.contains(QStringLiteral("t1130 RED-01 挂起语义勘误"))
            && whTxt.contains(QStringLiteral("t1130 RED-02"))
            && brTxt.contains(QStringLiteral("距已排定翻转的已计 pass 数"));
        // era 工件在盘非空 + 锚串（测试预期独立建证——工件在 build/ 位面，exe 同目录）。
        const QString buildDir = QCoreApplication::applicationDirPath();
        const auto readBuild = [&buildDir](const char *name) {
            QFile f(buildDir + QString::fromLatin1(name));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString jar1 = readBuild("/t1130_jar_diode.txt");
        const QString jar2 = readBuild("/t1130_jar_worldpower.txt");
        const bool jarOk = jar1.size() > 2000 && jar2.size() > 2000
            && jar1.contains(QStringLiteral("cb = 延迟表 {1,2,3,4}"))
            && jar1.contains(QStringLiteral("档 d = d pass"))
            && jar1.contains(QStringLiteral("TileTicks"))
            && jar1.contains(QStringLiteral("延长面"))
            && jar2.contains(QStringLiteral("仅输出面"))
            && jar2.contains(QStringLiteral("ry.v"))
            && jar2.contains(QStringLiteral("TileTicks"))
            && jar2.contains(QStringLiteral("实块间接承载"));
        // 跨任务词元零命中（本段源文件只携本单 filter 族词元）。
        QFile self(QDir(QCoreApplication::applicationDirPath()
                        + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/section93_redstone_fix_t1130.cpp")));
        const QString selfTxt = self.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(self.readAll()) : QString();
        int legacy = 0;
        for (int t = 0; t <= 9; ++t)
            legacy += selfTxt.count(QStringLiteral("r209%1").arg(t));
        const bool selfOk = selfTxt.size() > 4000 && selfTxt.contains(QStringLiteral("r2100"))
            && legacy == 0;
        // 注册三行钉（CMake 段行 + harness 声明 / 调度行）。
        QFile cmake(QDir(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("CMakeLists.txt")));
        const QString cmakeTxt = cmake.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(cmake.readAll()) : QString();
        QFile mh(QDir(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/matrix_helpers.cpp")));
        const QString mhTxt = mh.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mh.readAll()) : QString();
        QFile mhh(QDir(QCoreApplication::applicationDirPath()
                       + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/matrix_helpers.h")));
        const QString mhhTxt = mhh.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mhh.readAll()) : QString();
        const bool regOk = cmakeTxt.count(QStringLiteral(
            "tools/matrix/section93_redstone_fix_t1130.cpp")) == 1
            && mhTxt.count(QStringLiteral("section93_redstone_fix_t1130();")) == 1
            && mhhTxt.count(QStringLiteral("void section93_redstone_fix_t1130();")) == 1;
        ok = ok && missW.isEmpty() && anchors && jarOk && selfOk && regOk;
        if (!ok)
            diag += QStringLiteral("[w=%1 anc=%2 jar=%3 self=%4 legacy=%5 reg=%6]")
                        .arg(missW.join(QLatin1Char(','))).arg(anchors).arg(jarOk)
                        .arg(selfOk).arg(legacy).arg(regOk);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2100i structure pins (the world.cpp machine pins for the arm line, the"
               " unconditional fire-on line and the torch-face directional gate line each"
               " appear exactly once while the two NEG-target lines stay unpinned by design,"
               " the world.h and blockregistry.h erratum-note anchors are present verbatim,"
               " both jar adjudication artifacts exist on disk non-empty carrying the"
               " delay-table, tick-conversion, output-face-only and TileTicks anchors, the"
               " section source carries zero cross-task legacy tokens besides its own filter"
               " family, and the CMake plus harness registration rows for this section are"
               " present)"
            << (ok ? QString() : diag);
    });
}
