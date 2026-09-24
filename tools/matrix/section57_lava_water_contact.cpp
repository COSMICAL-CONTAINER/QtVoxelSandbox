#include "matrix_helpers.h"

// t1085 岩浆×水接触规则探针段（4 腿；filter 词 r2056；矩阵 754→758）。置尾先例（接
// section56，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 48×48×96 s82，section11/53/55/56
// 同门；流体窗口驱动 = 直调 Q_INVOKABLE tickWaterFlow/tickLavaFlow（section03 r24#3 同款：35 调 ≥
// 节流 30 恰 1 岩浆窗；水节流 3 → 100 调 ≈ 33 窗足够 7 级波前 + re-level 收敛））。
//
// 现场核实（核实先行，候选池七度失真降级谱系 t1070/t1077/t1078/t1080/t1083/t1084 之后）：岩浆×水
//   接触四支中**三支早已在位**——① 流水+岩浆源→黑曜石（t411 pass A，tickWaterFlow obsidianTargets，
//   改岩浆格）；② 水源+岩浆源→黑曜石（t472 pass B，tickLavaFlow obsidianTargets，改岩浆格）；
//   ③ 增量流体格位索引（c282bc0：m_waterCells/m_lavaCells 经 noteFluidWrite 增量维护，tick 快照
//   O(流体格数) 全量扫反模式零复活）+ 熔岩收敛状态机（lava-never-settles 修复：bk==1 守卫 + t563
//   盒过滤零写入兜底）+ 写入门 setWaterSilent（noteFluidWrite/活动盒/dirty 全套）**全部在位**。
//   **唯一 1.0 语义缺口（本段随附 fix）= 流岩浆×水支（t438 口径）**：t438 把转化落在**水格**（静水
//   源→Stone / 流水→Cobble、岩浆格存活）。MC 1.0 口径裁定（pre-flattening BlockFluid::checkForMixing）：
//   接触水时转化恒落在**岩浆格**——岩浆源（level 0）→ Obsidian、流岩浆（level>0）→ Cobblestone，
//   **水格永不转化**（水面存活 = 圆石机永续的前提；1.0 Java 无「水+岩浆产石头」结算——石头产物系
//   现代流体重写产物）。项目台账内证同向：t411 行「流岩浆 + 静水 → 圆石（非石头）」；t438 行与其注释
//   引「spec 要求 Stone」与 t411 行矛盾（t438 主目标本为修「流+流相遇两 pass 互不反应=水火共融」，
//   石头产物系重写顺带）。fix 将该支改回岩浆格自身凝固为圆石 + 「不问水方 state」（t438 要修的水火
//   共融 bug 由本支独立完整覆盖）+ 格域交叠守卫（solidifyKeys：蒸发跳过 + 扩散落点/re-leveling 拒入，
//   防 adds/evap 在应用序覆写凝固产物）。
//   **如实降级登记（零实现）**：黑曜石两支 / 索引 / 收敛机 / 写入门零改动只钉现状；岩浆源触**流水**
//   触发面（pass B 只认静水源、流水触发归 pass A 水侧）为双 pass 分工缝——净行为与 1.0 等价（双支
//   并存覆盖全水态），登记不改。
//
// 腿面：r2056a 岩浆源×水黑曜石承重墙（水源+岩浆源 [t472 pass B 单窗] / 流水+岩浆源 [t411 pass A] →
//   岩浆格 Obsidian=57 + 水面存活逐位 + 黑曜石幂等稳态 + 无水对照源不转化）；r2056b 流岩浆×水圆石
//   承重墙（流岩浆格自身 → Cobble=4 [静水源/流水两态同结算] + 水面存活 + 无石头产物反探 + 同水源
//   复投可再生 [圆石机行为柱] + 源/流 level 双漏斗同场景互斥）；r2056c 转化后账面收敛 + 确定性墙
//   （水 7 级波前全展开精确 states [NEG-2 靶面：索引失维护即停滞] + 接触转化不动点 + 双 seed 孪生
//   逐位恒等 [接触规则局部确定性、seed 无关] + 水岩双 tick 共驱平衡）；r2056d 结构钉（接触漏斗单点
//   收口源钉 [pass A/B obsidian 各恰 1 + solidify 收集恰 1] + solidifyKeys 四守卫在场 + 增量索引零
//   触碰反探 [rebuildFluidCells 调用点恰 2 + tick 快照 m_*Cells 消费钉] + tick 接线钉 [Main.qml +
//   Q_INVOKABLE] + 转化 id 正确性 [57/4/3 逐位] + t1085 头注锚在场）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2056_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘接触转化行（world.cpp tickLavaFlow 固化应用环 for (const SolidifyTarget &s : ...) 注释
//     摘除；收集/solidifyKeys 守卫保留）→ 声明红面 {r2056b}（流岩浆格永不凝固：固化应用环是 cobble
//     唯一写点；b 腿 cobble 断言全红）。r2056a 不误伤（obsidian 双漏斗是另一对应用环）；r2056c 不
//     误伤（c 腿三场景全走水波前 / obsidian 支 / 孪生相对恒等——c3 孪生面在变异下双世界同变仍恒等
//     [相对恒等纪律，R20.11]）；r2056d 不误伤（NEG 靶行[固化应用环两行]不入 d 腿钉面——收集行/
//     守卫行是另一文本，t1083/t1084「NEG 靶行不入钉」先例）。
//   NEG-2 摘账面收敛行（world.cpp setWaterSilent 内 noteFluidWrite 调用行注释摘除 → c282bc0 增量
//     索引失维护）→ **实测红面 = {r2056a, r2056b, r2056c}（恰红宣告据实修订）**：a1/b4 的「无残余
//     岩浆」面（转化后源 spread 残流的跨窗蒸发收敛）与 c1 波前展开 / c2 不动点同属**多窗索引依赖面**
//     ——残流格入索引后下窗才能进快照被蒸发；摘维护行即永留 3 残流（账面收敛=正是索引承重面）。
//     子面互补性：NEG-1 下 b 腿红在 cobble 四面（b4 的 cobble 面也红）、b4 的 lav 面绿；NEG-2 下 b 腿
//     红恰在 b4 的 lav 面、b1/b2/b3 绿 —— 双 NEG 红面分域不互吞。r2056d 不误伤（d 腿零这些依赖；
//     NEG-2 靶行 noteFluidWrite 不入 d 腿钉面；d 腿索引反探钉的是消费面 rebuildFluidCells 调用点与
//     m_*Cells 快照循环，非该维护行）。
//
// rig 纪律：fresh 小世界四 setter incantation（section11 同款——**每个 setter 触发一次全量
//   generate**，终态 = 48×48×96 s82/83 真地形世界；故 rig 取 y=80 高空坪 [kWaterLevel=58 上 +
//   地形/树冠带之上]，手铺石坪即 grounding 面，不与地形交互）；流体全经 w.setBlock 直放（id+state
//   同步入栅格 + 增量索引，桶倒等价面）；坐标不作哨兵（t1081 教训——区域扫描用有界框循环非 -1 哨兵）；
//   seed 无关面不查 hashVoxel（rig 无木质可燃物 → ignite pass 零掷点 → 双 seed 恒等是结构保证非概率）。

namespace {

// fresh 小世界 incantation（section11 同款四 setter + 恒晴零 RNG）。
inline void initFluidWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// 石坪：[x0, x0+sx) × [z0, z0+sz) @ yFloor 铺 Stone（流体 grounding 面——bk==1 水平蔓延 / bk==0 下落判据）。
inline void placeStoneFloor(World &w, int x0, int z0, int sx, int sz, int yFloor)
{
    for (int x = x0; x < x0 + sx; ++x)
        for (int z = z0; z < z0 + sz; ++z)
            w.setBlock(x, yFloor, z, BR::Stone, 0);
}

// 有界框内 id 计数（区域反探用：无残余 cells / 无石头产物——坐标不作哨兵，框循环有界）。
inline int countIdInBox(const World &w, int x0, int y0, int z0, int x1, int y1, int z1, quint8 id)
{
    int n = 0;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (w.blockAt(x, y, z) == id) ++n;
    return n;
}

// 有界框内容指纹（id+state 逐格线性化 → 字符串；孪生恒等 / 不动点比对用）。
inline QString boxFingerprint(const World &w, int x0, int y0, int z0, int x1, int y1, int z1)
{
    QString s;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                s += QString::number(w.blockAt(x, y, z)) + u':' + QString::number(w.stateAt(x, y, z)) + u';';
    return s;
}

} // namespace

void MatrixRun::section57_lava_water_contact()
{
    // ── r2056a：岩浆源×水 → 黑曜石承重墙（pass B 水源触发 / pass A 流水触发 / 幂等 / 无水对照）────
    runLeg("r2056a lava-source x water obsidian wall (water source + lava source -> obsidian at the lava cell via lava-tick funnel with water surface surviving bit-exact, flowing water + lava source -> obsidian via water-tick funnel, obsidian idempotent-stable under further windows, no-water control source unconverted, zero cobble/stone side-products)", [&]() {
        bool ok = true;
        QString diag;
        const int yF = 80; // kRigY 同理取高空（kWaterLevel=58 上 + 地形/树冠 ~33 —— 40 以上必空，section56 先例；80 更留余量）
        // (a1) 水源 + 岩浆源（正交邻接）→ 岩浆源格 Obsidian=57（t472 pass B，岩浆 tick 单窗结算；
        //      改的是岩浆格——1.0 口径「水触岩浆源 → 岩浆源变黑曜石」的目标格面）；水面逐位存活
        //      （Water state=0 不动——水面存活面）；区域零 Cobble/Stone 副产物；续窗幂等稳态。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0); // 水源
            w.setBlock(25, yF, 24, BR::Lava, 0);  // 岩浆源（正交邻接）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow(); // 70 调 ≥ 2×节流 30 → 转化窗 + 残流蒸发窗齐
            const bool a1 = w.blockAt(25, yF, 24) == BR::Obsidian
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0
                && countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Cobble) == 0
                && countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Stone) == 8 * 8 // 仅手铺石坪自身
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava) == 0; // 无残余岩浆（账面收敛）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow(); // 幂等续窗
            const bool a1b = w.blockAt(25, yF, 24) == BR::Obsidian
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0;
            ok = ok && a1 && a1b;
            if (!(a1 && a1b))
                diag += QStringLiteral("[a1 id25=%1/%2 w=%3/%4 cob=%5 lav=%6 idem=%7]")
                            .arg(w.blockAt(25, yF, 24)).arg(w.blockAt(25, yF, 24) == BR::Obsidian)
                            .arg(w.blockAt(24, yF, 24)).arg(w.stateAt(24, yF, 24))
                            .arg(countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Cobble))
                            .arg(countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava)).arg(a1b);
        }
        // (a2) 流水 + 岩浆源 → 岩浆源格 Obsidian（t411 pass A，水 tick 漏斗；流水由 setBlock 直放
        //      level=1 + 同排上游水源托撑——单窗结算无多窗索引依赖）；流水/水源双存活；零副产物。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(22, yF, 24, BR::Water, 0); // 水源（托撑流水不蒸发）
            w.setBlock(23, yF, 24, BR::Water, 1); // 流水（pass A 触发方，level>0）
            w.setBlock(24, yF, 24, BR::Lava, 0);  // 岩浆源（正交邻接）
            for (int t = 0; t < 100; ++t) w.tickWaterFlow(); // 100 调 ≈ 33 窗 ≥ 转化 + 波前收敛
            const bool a2 = w.blockAt(24, yF, 24) == BR::Obsidian
                && w.blockAt(23, yF, 24) == BR::Water && w.stateAt(23, yF, 24) == 1
                && w.blockAt(22, yF, 24) == BR::Water && w.stateAt(22, yF, 24) == 0
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Cobble) == 0;
            ok = ok && a2;
            if (!a2)
                diag += QStringLiteral("[a2 id24=%1 w23=%2/%3 w22=%4/%5 cob=%6]")
                            .arg(w.blockAt(24, yF, 24)).arg(w.blockAt(23, yF, 24))
                            .arg(w.stateAt(23, yF, 24)).arg(w.blockAt(22, yF, 24))
                            .arg(w.stateAt(22, yF, 24))
                            .arg(countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Cobble));
        }
        // (a3) 无水对照：孤岩浆源无任何水 → 永不转化（源格保持 Lava state=0；无 Obsidian/Cobble 出现
        //      ——接触漏斗触发面收口：无水不反应）。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Lava, 0);
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const bool a3 = w.blockAt(24, yF, 24) == BR::Lava && w.stateAt(24, yF, 24) == 0
                && countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Obsidian) == 0
                && countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Cobble) == 0;
            ok = ok && a3;
            if (!a3)
                diag += QStringLiteral("[a3 id=%1/%2 obs=%3 cob=%4]")
                            .arg(w.blockAt(24, yF, 24)).arg(w.stateAt(24, yF, 24))
                            .arg(countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Obsidian))
                            .arg(countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Cobble));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2056a lava-source x water obsidian wall (water source + lava source -> obsidian at the lava cell via lava-tick funnel with water surface surviving bit-exact, flowing water + lava source -> obsidian via water-tick funnel, obsidian idempotent-stable under further windows, no-water control source unconverted, zero cobble/stone side-products)"
            << (ok ? QString() : diag);
    });

    // ── r2056b：流岩浆×水 → 圆石承重墙（岩浆格自身凝固 / 水面存活 / 无石头产物 / 可再生 / 双漏斗互斥）──
    runLeg("r2056b flowing-lava x water cobble wall (flowing lava cell itself solidifies to cobble at either water state [source or flowing, t1085 ruling: conversion lands on the lava cell, water surface never converts], water surface survives bit-exact, zero stone product anti-probe, same-source re-cast renewability [cobble-generator behavior column], lava source + flowing lava dual-funnel level-disjoint in one scene)", [&]() {
        bool ok = true;
        QString diag;
        const int yF = 80; // kRigY 同理取高空（kWaterLevel=58 上 + 地形/树冠 ~33 —— 40 以上必空，section56 先例；80 更留余量）
        // (b1) 流岩浆（level=1 setBlock 直放）+ 静水源 → 流岩浆格自身 Cobble=4（t1085：转化落岩浆格，
        //      不问水方 state）；水面逐位存活；无 Stone 副产物（1.0 无水+岩浆产石结算）；无残余岩浆。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0); // 水源（存活面）
            w.setBlock(25, yF, 24, BR::Lava, 1);  // 流岩浆（触发方，level>0）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow(); // ≥ 2×节流 → 凝固窗 + 收敛窗齐
            const bool b1 = w.blockAt(25, yF, 24) == BR::Cobble
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0
                && countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Stone) == 8 * 8
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava) == 0;
            ok = ok && b1;
            if (!b1)
                diag += QStringLiteral("[b1 id25=%1 w24=%2/%3 stone=%4 lav=%5]")
                            .arg(w.blockAt(25, yF, 24)).arg(w.blockAt(24, yF, 24))
                            .arg(w.stateAt(24, yF, 24))
                            .arg(countIdInBox(w, 20, yF - 1, 20, 30, yF + 2, 30, BR::Stone))
                            .arg(countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava));
        }
        // (b2) 流岩浆 + **流水**（state=1，t438 当初水火共融病灶面）→ 流岩浆格自身 Cobble；流水格
        //      逐位存活（state=1 不动——1.0 水面存活面；只驱岩浆 tick 隔离岩浆侧规则）。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 1); // 流水（state=1 不问）
            w.setBlock(25, yF, 24, BR::Lava, 1);  // 流岩浆
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const bool b2 = w.blockAt(25, yF, 24) == BR::Cobble
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 1
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Stone) == 0;
            ok = ok && b2;
            if (!b2)
                diag += QStringLiteral("[b2 id25=%1 w24=%2/%3 stone=%4]")
                            .arg(w.blockAt(25, yF, 24)).arg(w.blockAt(24, yF, 24))
                            .arg(w.stateAt(24, yF, 24))
                            .arg(countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Stone));
        }
        // (b3) 可再生（圆石机行为柱）：同一水源两侧先后各投一注流岩浆 → 两注各自凝固为 Cobble，
        //      水源三面全程存活（水面被消耗则本面必红——t438 旧口径水面变 Stone/Cobble 即不可再生）。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0);
            w.setBlock(25, yF, 24, BR::Lava, 1); // 第一注（+X 侧）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const bool b3a = w.blockAt(25, yF, 24) == BR::Cobble
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0;
            w.setBlock(23, yF, 24, BR::Lava, 1); // 第二注（-X 侧，同水源）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const bool b3b = w.blockAt(25, yF, 24) == BR::Cobble
                && w.blockAt(23, yF, 24) == BR::Cobble
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0;
            ok = ok && b3a && b3b;
            if (!(b3a && b3b))
                diag += QStringLiteral("[b3 a=%1 b=%2 id23=%3 id25=%4]")
                            .arg(b3a).arg(b3b).arg(w.blockAt(23, yF, 24)).arg(w.blockAt(25, yF, 24));
        }
        // (b4) 双漏斗互斥同场景：岩浆源（-X 侧）与流岩浆（+X 侧）同触一水源 → 源格 Obsidian=57
        //      （level 0 漏斗）/ 流格 Cobble=4（level>0 漏斗）level 互斥零串扰；水面存活；无残余岩浆。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0);
            w.setBlock(23, yF, 24, BR::Lava, 0); // 岩浆源（→ Obsidian）
            w.setBlock(25, yF, 24, BR::Lava, 1); // 流岩浆（→ Cobble）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const bool b4 = w.blockAt(23, yF, 24) == BR::Obsidian
                && w.blockAt(25, yF, 24) == BR::Cobble
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava) == 0;
            ok = ok && b4;
            if (!b4)
                diag += QStringLiteral("[b4 id23=%1 id25=%2 w24=%3 lav=%4]")
                            .arg(w.blockAt(23, yF, 24)).arg(w.blockAt(25, yF, 24))
                            .arg(w.blockAt(24, yF, 24))
                            .arg(countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2056b flowing-lava x water cobble wall (flowing lava cell itself solidifies to cobble at either water state [source or flowing, t1085 ruling: conversion lands on the lava cell, water surface never converts], water surface survives bit-exact, zero stone product anti-probe, same-source re-cast renewability [cobble-generator behavior column], lava source + flowing lava dual-funnel level-disjoint in one scene)"
            << (ok ? QString() : diag);
    });

    // ── r2056c：转化后账面收敛 + 确定性墙（水波前全展开精确 states / 接触转化不动点 / 双 seed 孪生 /
    //    水岩双 tick 共驱平衡）────────────────────────────────────────────────────────────────────
    runLeg("r2056c post-conversion accounting convergence + determinism wall (water wavefront full 7-ring expansion with exact per-distance states [index-stall sensitive], contact conversion fixed point with zero residual lava cells, twin-seed byte-identical regions [contact rules local-deterministic seed-independent], co-driven water+lava ticks equilibrium)", [&]() {
        bool ok = true;
        QString diag;
        const int yF = 80; // kRigY 同理取高空（kWaterLevel=58 上 + 地形/树冠 ~33 —— 40 以上必空，section56 先例；80 更留余量）
        // (c1) 水波前全展开（NEG-2 靶面）：平地水源 → 7 级波前全展开，逐距 state 1..7 精确钉 + 距 8
        //      为 Air（kMaxFlowLevel=7 收口）。多窗波前每窗新格经 noteFluidWrite 入 m_waterCells ——
        //      索引失维护（NEG-2）→ 快照停滞环 1 → 本面红。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 14, 18, 22, 14, yF - 1); // 22×14 大坪（≥7 级波前 + 收口带）
            w.setBlock(24, yF, 24, BR::Water, 0);
            for (int t = 0; t < 100; ++t) w.tickWaterFlow(); // ≈33 窗 ≥ 7 级波前 + re-level 收敛
            bool c1 = true;
            for (int d = 1; d <= 7 && c1; ++d) {
                if (w.blockAt(24 + d, yF, 24) != BR::Water || w.stateAt(24 + d, yF, 24) != d) c1 = false;
                if (w.blockAt(24 - d, yF, 24) != BR::Water || w.stateAt(24 - d, yF, 24) != d) c1 = false;
            }
            if (w.blockAt(24 + 8, yF, 24) != BR::Air || w.blockAt(24 - 8, yF, 24) != BR::Air) c1 = false;
            ok = ok && c1;
            if (!c1)
                diag += QStringLiteral("[c1 e+%1/%2 e-%3/%4 w+%5/%6]")
                            .arg(w.blockAt(31, yF, 24)).arg(w.stateAt(31, yF, 24))
                            .arg(w.blockAt(17, yF, 24)).arg(w.stateAt(17, yF, 24))
                            .arg(w.blockAt(32, yF, 24)).arg(w.stateAt(32, yF, 24));
        }
        // (c2) 接触转化不动点（水源+岩浆源 obsidian 场）：转化收敛后框指纹逐位冻结 + 区域零残余岩浆
        //      （「无残余 cells/无再流动」行为柱——lava-never-settles 教训面的行为级反探）。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0);
            w.setBlock(25, yF, 24, BR::Lava, 0);
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const QString fp1 = boxFingerprint(w, 20, yF - 1, 20, 30, yF + 2, 30);
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();
            const QString fp2 = boxFingerprint(w, 20, yF - 1, 20, 30, yF + 2, 30);
            const bool c2 = fp1 == fp2
                && w.blockAt(25, yF, 24) == BR::Obsidian
                && countIdInBox(w, 20, yF, 20, 30, yF + 2, 30, BR::Lava) == 0;
            ok = ok && c2;
            if (!c2) diag += QStringLiteral("[c2 fpEq=%1]").arg(fp1 == fp2);
        }
        // (c3) 双 seed 孪生恒等（接触规则局部确定性、seed 无关——PLAN §2-K 面；rig 无木质 → ignite
        //      零掷点 → 恒等是结构保证）。仅钉**相对恒等**（R20.11 纪律：辅助面不跨变异承载语义断言
        //      ——cobble id 断言在 r2056b，本面在 NEG-1 下双世界同变仍须恒等不添红）+ 水源存活共钉。
        {
            World wa, wb;
            initFluidWorld(wa, 82);
            initFluidWorld(wb, 83); // 异 seed
            for (World *pw : { &wa, &wb }) {
                placeStoneFloor(*pw, 20, 20, 8, 8, yF - 1);
                pw->setBlock(24, yF, 24, BR::Water, 0);
                pw->setBlock(25, yF, 24, BR::Lava, 1); // 流岩浆×水（b1 同布局）
                pw->setBlock(24, yF, 26, BR::Lava, 0); // 岩浆源×水（a1 同布局）
                for (int t = 0; t < 70; ++t) pw->tickLavaFlow();
            }
            const bool c3 = boxFingerprint(wa, 20, yF - 1, 20, 30, yF + 2, 30)
                == boxFingerprint(wb, 20, yF - 1, 20, 30, yF + 2, 30)
                && wa.blockAt(24, yF, 24) == BR::Water && wa.stateAt(24, yF, 24) == 0;
            ok = ok && c3;
            if (!c3) diag += QStringLiteral("[c3 twinEq=%1]").arg(c3);
        }
        // (c4) 水岩双 tick 共驱平衡（实机双系统同跑序）：水先铺满 → 岩浆源转化（源周无 air 邻 → 无
        //      残流）→ 双 tick 各 100 调共驱 → 框指纹冻结（平衡序鲁棒面）。
        {
            World w;
            initFluidWorld(w, 82);
            placeStoneFloor(w, 20, 20, 8, 8, yF - 1);
            w.setBlock(24, yF, 24, BR::Water, 0);
            w.setBlock(25, yF, 24, BR::Lava, 0);
            for (int t = 0; t < 100; ++t) w.tickWaterFlow(); // 水先铺满（岩浆源格非 Air → 水不入格）
            for (int t = 0; t < 70; ++t) w.tickLavaFlow();   // 岩浆源转化（周身皆水 → 无 spread 落点）
            const QString fp1 = boxFingerprint(w, 20, yF - 1, 20, 30, yF + 2, 30);
            for (int t = 0; t < 100; ++t) { w.tickWaterFlow(); w.tickLavaFlow(); }
            const QString fp2 = boxFingerprint(w, 20, yF - 1, 20, 30, yF + 2, 30);
            const bool c4 = fp1 == fp2
                && w.blockAt(25, yF, 24) == BR::Obsidian
                && w.blockAt(24, yF, 24) == BR::Water && w.stateAt(24, yF, 24) == 0;
            ok = ok && c4;
            if (!c4) diag += QStringLiteral("[c4 fpEq=%1 obs=%2]").arg(fp1 == fp2).arg(w.blockAt(25, yF, 24));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2056c post-conversion accounting convergence + determinism wall (water wavefront full 7-ring expansion with exact per-distance states [index-stall sensitive], contact conversion fixed point with zero residual lava cells, twin-seed byte-identical regions [contact rules local-deterministic seed-independent], co-driven water+lava ticks equilibrium)"
            << (ok ? QString() : diag);
    });

    // ── r2056d：结构钉（接触漏斗单点收口 + solidifyKeys 四守卫 + 增量索引零触碰反探 + tick 接线 +
    //    转化 id 逐位 + t1085 头注锚）。NEG 靶行（固化应用环 / noteFluidWrite 维护行）不入本腿
    //    （t1083/t1084 先例——防恰红面双红）────────────────────────────────────────────────────
    runLeg("r2056d structure pins (contact funnels single-write-site source pins, solidifyKeys four interlock guards present, incremental-index zero-touch anti-probe [rebuildFluidCells call sites == 2 + tick snapshots consume m_waterCells/m_lavaCells], tick wiring pins Main.qml + Q_INVOKABLE, conversion ids 57/4/3 bit-exact, t1085 header anchor)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile wf(root + QStringLiteral("/src/World/world.cpp"));
        const QString wSrc = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
        // (1) 接触漏斗单点收口源钉：obsidian 双漏斗（水 tick / 岩浆 tick 各恰 1 写点，配对恒等）+
        //     固化收集恰 1（岩浆格自身凝固的单一权威）；solidify 应用环**不钉**（NEG-1 靶行）。
        const bool d1 = wSrc.count(QStringLiteral("setWaterSilent(o.x, o.y, o.z, BlockRegistry::Obsidian, 0)")) == 2
            && wSrc.count(QStringLiteral("solidifyTargets.push_back({c.x, c.y, c.z, quint8(BlockRegistry::Cobble)});")) == 1
            && wSrc.count(QStringLiteral("obsidianTargets.push_back(c);")) == 1;
        ok = ok && d1;
        if (!d1)
            diag += QStringLiteral("[d1 obs=%1 sol=%2 obsB=%3]")
                        .arg(wSrc.count(QStringLiteral("setWaterSilent(o.x, o.y, o.z, BlockRegistry::Obsidian, 0)")))
                        .arg(wSrc.count(QStringLiteral("solidifyTargets.push_back({c.x, c.y, c.z, quint8(BlockRegistry::Cobble)});")))
                        .arg(wSrc.count(QStringLiteral("obsidianTargets.push_back(c);")));
        // (2) solidifyKeys 四守卫在场（蒸发跳过 / 扩散源跳过 / 下落落点拒入 / re-leveling 拒入——
        //     应用序覆写防线；删任一守卫计数即红）。
        const bool d2 = wSrc.count(QStringLiteral("solidifyKeys.count(")) == 4;
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2 guards=%1]").arg(wSrc.count(QStringLiteral("solidifyKeys.count(")));
        // (3) 增量索引零触碰反探（禁全扫，c282bc0/perf-fluid-scan 面）：rebuildFluidCells 全图重建
        //     恰 2 调用点（generate / finishLoad 一次性，非每 tick）+ tick 快照消费 m_lavaCells 恰 1
        //     （岩浆 tick）/ m_waterCells 恰 2（水 tick + tickIceFreeze 同 O(cells) 快照族）。
        //     noteFluidWrite 维护行**不钉**（NEG-2 靶行）。
        const bool d3 = wSrc.count(QStringLiteral("rebuildFluidCells();")) == 2
            && wSrc.count(QStringLiteral("for (const quint64 k : m_lavaCells)")) == 1
            && wSrc.count(QStringLiteral("for (const quint64 k : m_waterCells)")) == 2;
        ok = ok && d3;
        if (!d3)
            diag += QStringLiteral("[d3 rb=%1 lav=%2 wat=%3]")
                        .arg(wSrc.count(QStringLiteral("rebuildFluidCells();")))
                        .arg(wSrc.count(QStringLiteral("for (const quint64 k : m_lavaCells)")))
                        .arg(wSrc.count(QStringLiteral("for (const quint64 k : m_waterCells)")));
        // (4) tick 接线钉：Main.qml WorldClock 100ms 桥接两行恰各 1 + world.h Q_INVOKABLE 声明恰各 1
        //     （呈现层驱动面断线即红）。
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString mSrc = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        QFile whf(root + QStringLiteral("/src/World/world.h"));
        const QString whSrc = whf.open(QIODevice::ReadOnly) ? QString::fromUtf8(whf.readAll()) : QString();
        const bool d4 = mSrc.count(QStringLiteral("theWorld.tickWaterFlow()")) == 1
            && mSrc.count(QStringLiteral("theWorld.tickLavaFlow()")) == 1
            && whSrc.count(QStringLiteral("Q_INVOKABLE void tickWaterFlow();")) == 1
            && whSrc.count(QStringLiteral("Q_INVOKABLE void tickLavaFlow();")) == 1;
        ok = ok && d4;
        if (!d4)
            diag += QStringLiteral("[d4 qmlW=%1 qmlL=%2 hdrW=%3 hdrL=%4]")
                        .arg(mSrc.count(QStringLiteral("theWorld.tickWaterFlow()")))
                        .arg(mSrc.count(QStringLiteral("theWorld.tickLavaFlow()")))
                        .arg(whSrc.count(QStringLiteral("Q_INVOKABLE void tickWaterFlow();")))
                        .arg(whSrc.count(QStringLiteral("Q_INVOKABLE void tickLavaFlow();")));
        // (5) 转化 id 正确性逐位（黑曜石 57 / 圆石 4 / 石头 3——行为腿读 id 的单一权威面）+ t1085
        //     裁定头注锚在场（裸文本锚，注释语义锚先例）。
        const bool d5 = int(BR::Obsidian) == 57 && int(BR::Cobble) == 4 && int(BR::Stone) == 3
            && wSrc.contains(QStringLiteral("t1085 裁定"));
        ok = ok && d5;
        if (!d5)
            diag += QStringLiteral("[d5 obs=%1 cob=%2 stone=%3 anchor=%4]")
                        .arg(int(BR::Obsidian)).arg(int(BR::Cobble)).arg(int(BR::Stone))
                        .arg(wSrc.contains(QStringLiteral("t1085 裁定")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2056d structure pins (contact funnels single-write-site source pins, solidifyKeys four interlock guards present, incremental-index zero-touch anti-probe [rebuildFluidCells call sites == 2 + tick snapshots consume m_waterCells/m_lavaCells], tick wiring pins Main.qml + Q_INVOKABLE, conversion ids 57/4/3 bit-exact, t1085 header anchor)"
            << (ok ? QString() : diag);
    });
}
