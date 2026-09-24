#include "matrix_helpers.h"

// t1088 甘蔗生长口径归一探针段（4 腿；filter 词 r2058；矩阵 758→762）。置尾先例（接 section57，
// runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 48×48×96 s82，section11/53/55/56/57 同门；
// 生长窗口驱动 = 直调 Q_INVOKABLE tickSugarcaneGrowth，kSugarcaneTickInterval=50 → 50 调 = 1 窗，
// 每窗每柱顶 20% 散布掷骰 hashVoxel(seed ⊕ 窗序) → 80 窗预算对 20%/窗命中率是 0.8^80 ≈ 2e-8 级
// 不命中概率，且同 seed 同 rig 位**逐窗确定**（无 RNG 流，跨跑零抖动））。
//
// 现场核实（核实先行）：t1087 裁定单例外发现的两处真实 1.0 口径偏差**均属实且均已修**（本段随附
//   fix(t1088)）——① 生长基材门内战：放置门草/泥土/沙基（playercontroller.cpp）vs 生长门仅沙基
//   （world.cpp t446 修法）→ 草/土基可种但永不长高；修 = 两面共读 BlockRegistry::sugarcaneBaseBlock
//   单一权威（t847 isGroundPlant 两面单一权威同门），基材集 = {Grass, Dirt, Sand, Sugarcane}（MC 1.0
//   wiki Sugar Cane「can be planted on … grass block, dirt … and sand that is directly adjacent to
//   water」三元组，2026 实读）。② 生长上限分裂：world.h kSugarcaneMaxHeight=5（t406 spec「max5」）
//   vs 放置面 kSugarcanePlaceMaxHeight=3 vs MC 1.0 random-tick 生长上限 3 → lawful 翻案归一 3
//   （t1081 点亮暗门翻案先例；同方块两面口径分裂必须收敛、方向按 1.0 基准）。worldgen placeSugarcane
//   列高 1..3（world.cpp「1 + int((r >> 16) % 3u)」）≤ 新上限 → worldgen 零改动同批核对一致。
//   t418 拔高潜力门（height≥3 时 kSugarcaneTallPct 哈希门「长到 4..5」）随上限归一成死码 → 同批
//   lawful 修订摘除（断言改强不削：新钉面 = 上限 3 精确「第 4 次拔高恒拒」，强于旧 4..5 潜力面）。
//
// 腿面：r2058a 草/土基生长命中承重墙（放宽基材集行为级：草基/土基邻水各长一格 + 石基对照恒不长
//   [基材集封闭面]）；r2058b 沙基回归 + 邻水门保持墙（沙基邻水仍长 [t446 时代行为保留] + 无水沙基/
//   无水草基恒不长 [1.0「无水不长」口径在放宽基材集下原样]）；r2058c 上限 3 精确墙（2 高柱恰好到 3
//   + 3 高柱第 4 次拔高恒拒 [延窗恒 3]，t406「max5」翻案行为级）；r2058d 结构钉（sugarcaneBaseBlock
//   真值表运行期 + 单一权威三面源钉 [谓词定义/生长门调用/放置门调用] + 手写基材集残留反探禁出 +
//   上限常量 =3 源钉 & =5 反探 [NEG-2 靶行——恰红面见下] + t406 翻案锚注 + t418 退役锚注 +
//   worldgen 1..3 列高公式钉 + kSugarcaneTallPct 残留禁出）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2058_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘草土基分支（blockregistry.cpp sugarcaneBaseBlock 谓词体摘除 Grass/Dirt 两个比较项 →
//     单一权威退回 {Sand, Sugarcane}，放置/生长两面同步收窄）→ **恰红 = {r2058a, r2058d 基材子面}**：
//     a 腿草/土基掷骰门在前即拒（行为红）；d 腿红恰在真值表 d1 + 谓词定义 d2 子面（谓词体即靶行）；
//     b 不误伤（沙基在收窄集内、无水对照面照旧）；c 不误伤（c 全用沙基——上限/邻水面与基材集正交）；
//     d 其余子面（d3-d8）不涉谓词体 → 绿。NEG-1 靶行（谓词体 Grass/Dirt 项）**不是** d3/d4 调用行
//     钉的文本 → 调用行钉在 NEG-1 下绿（调用行未动，塌的是谓词语义——由 a 腿行为面 + d1 真值表面
//     双层捕获）。r2058b 的无水草基对照面在 NEG-1 下仍绿（基材拒与无水拒同为「不长」断言，且该
//     断言面本就不区分拒绝原因）。
//   NEG-2 改回上限 5（world.h kSugarcaneMaxHeight = 3 → 5）→ **恰红 = {r2058c, r2058d 上限子面}**：
//     c 腿 3 高柱第 4 次拔高面红（cap 5 + 潜力门已退役 → 3 高柱可长第 4 格，延窗必现——靶位逐窗
//     确定，NEG-2 红跑实测复核）；d 腿红恰在 d5 常量源钉（=3 钉失配）子面；a/b 不误伤（升格面
//     1→2→3 在 cap 5 下照常）；d 其余子面（d1-d4/d6-d8）不涉常量 → 绿。双 NEG 红面分域：a+d 基材
//     子面 = NEG-1 / c+d 上限子面 = NEG-2，d 的 diag 按子面 id 归因，红模式可判别不互吞。
//
// rig 纪律：fresh 小世界四 setter incantation（section11 同款——**每个 setter 触发一次全量
//   generate**，终态 = 48×48×96 s82 真地形世界；故 rig 取 y=80 高空坪 [kWaterLevel=58 上 +
//   地形/树冠带之上]，手铺石坪即 grounding 面，不与地形交互）；甘蔗/基材/水全经 w.setBlock 直放
//   （id+state 同步入栅格 + noteGrowthWrite 生长索引，生长快照可见）；水面 source（state 0）放
//   基材同层水平邻位——wateredAt(by-1) 命中面（与 worldgen surfaceY-1 查水同语义）；坐标不作哨兵；
//   甘蔗写入经 setBlock 全 setter → 失撑钩子不触发（非破坏路径）。seed 无关面不查 hashVoxel。

namespace {

// fresh 小世界 incantation（section11/57 同款四 setter + 恒晴零 RNG）。
inline void initCaneWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// 甘蔗 rig 列：ground@(x,yF) + 水平邻水可选@(x+1,yF) + 甘蔗柱 height 格自 yF+1 起。
inline void plantCaneColumn(World &w, int x, int z, int yF, quint8 groundId, bool watered, int height)
{
    w.setBlock(x, yF, z, groundId, 0);
    if (watered)
        w.setBlock(x + 1, yF, z, BR::Water, 0);
    for (int i = 1; i <= height; ++i)
        w.setBlock(x, yF + i, z, BR::Sugarcane, 0);
}

// 推进 n 个生长窗（50 调 = 1 窗，kSugarcaneTickInterval 同源）。
inline void tickCaneWindows(World &w, int windows)
{
    for (int i = 0; i < windows * 50; ++i)
        w.tickSugarcaneGrowth();
}

// 柱高读取：自 yF+1 向上数连续甘蔗格。
inline int caneHeight(const World &w, int x, int z, int yF)
{
    int h = 0;
    while (w.blockAt(x, yF + 1 + h, z) == BR::Sugarcane)
        ++h;
    return h;
}

} // namespace

void MatrixRun::section58_sugarcane_growth()
{
    const int yF = 80; // 高空坪（kWaterLevel=58 上 + 地形/树冠 ~33 —— 40 以上必空，section56/57 先例）

    // ── r2058a：草/土基生长命中承重墙（放宽基材集行为级 + 石基对照）─────────────────────────
    runLeg("r2058a grass/dirt base growth hit wall (widened growth-base gate: grass block and dirt bases beside a water source each grow a cane cell within the window budget, stone-base control never grows [base set closed], MC 1.0 caliber: cane on grass/dirt/sand adjacent to water grows, t1087 ruling single-authority sugarcaneBaseBlock)", [&]() {
        bool ok = true;
        QString diag;
        // 三列同场（互距 6 列无交互）：草基+水 / 土基+水 / 石基+水（对照——石不在基材集）。
        World w;
        initCaneWorld(w, 82);
        for (int x = 18; x <= 36; ++x)
            for (int z = 18; z <= 24; ++z)
                w.setBlock(x, yF - 1, z, BR::Stone, 0); // grounding 坪
        plantCaneColumn(w, 20, 21, yF, BR::Grass, true, 1);
        plantCaneColumn(w, 26, 21, yF, BR::Dirt, true, 1);
        plantCaneColumn(w, 32, 21, yF, BR::Stone, true, 1);
        tickCaneWindows(w, 80); // 80 窗预算（0.8^80 ≈ 2e-8 级不命中；同 seed 同位逐窗确定）
        const bool a1 = w.blockAt(20, yF + 2, 21) == BR::Sugarcane;   // 草基长高
        const bool a2 = w.blockAt(26, yF + 2, 21) == BR::Sugarcane;   // 土基长高
        const bool a3 = w.blockAt(32, yF + 2, 21) == BR::Air          // 石基对照恒不长
            && caneHeight(w, 32, 21, yF) == 1;
        ok = ok && a1 && a2 && a3;
        if (!(a1 && a2 && a3))
            diag += QStringLiteral("[a1 grass=%1 a2 dirt=%2 a3 stone=%3 h=%4]")
                        .arg(a1).arg(a2).arg(a3).arg(caneHeight(w, 32, 21, yF));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2058a grass/dirt base growth hit wall (widened growth-base gate: grass block and dirt bases beside a water source each grow a cane cell within the window budget, stone-base control never grows [base set closed], MC 1.0 caliber: cane on grass/dirt/sand adjacent to water grows, t1087 ruling single-authority sugarcaneBaseBlock)"
            << (ok ? QString() : diag);
    });

    // ── r2058b：沙基回归 + 邻水门保持墙（t446 行为保留 + 1.0 无水不长回归柱）──────────────────
    runLeg("r2058b sand base regression + water-gate wall (sand base beside a water source still grows [t446-era behavior preserved], waterless sand base never grows and waterless grass base never grows [adjacent-to-water gate intact under the widened base set], zero growth without water is the 1.0 regression column)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initCaneWorld(w, 82);
        for (int x = 18; x <= 36; ++x)
            for (int z = 18; z <= 24; ++z)
                w.setBlock(x, yF - 1, z, BR::Stone, 0);
        plantCaneColumn(w, 20, 21, yF, BR::Sand, true, 1);   // 沙基 + 水 → 长（t446 行为回归柱）
        plantCaneColumn(w, 26, 21, yF, BR::Sand, false, 1);  // 无水沙基 → 恒不长
        plantCaneColumn(w, 32, 21, yF, BR::Grass, false, 1); // 无水草基 → 恒不长（放宽基材 ≠ 放宽水门）
        tickCaneWindows(w, 80);
        const bool b1 = w.blockAt(20, yF + 2, 21) == BR::Sugarcane;  // 沙基回归
        const bool b2 = w.blockAt(26, yF + 2, 21) == BR::Air
            && caneHeight(w, 26, 21, yF) == 1;                        // 无水沙基不长
        const bool b3 = w.blockAt(32, yF + 2, 21) == BR::Air
            && caneHeight(w, 32, 21, yF) == 1;                        // 无水草基不长（水门保持）
        ok = ok && b1 && b2 && b3;
        if (!(b1 && b2 && b3))
            diag += QStringLiteral("[b1 sand=%1 b2 nosand=%2 h=%3 b3 nograss=%4 h=%5]")
                        .arg(b1).arg(b2).arg(caneHeight(w, 26, 21, yF))
                        .arg(b3).arg(caneHeight(w, 32, 21, yF));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2058b sand base regression + water-gate wall (sand base beside a water source still grows [t446-era behavior preserved], waterless sand base never grows and waterless grass base never grows [adjacent-to-water gate intact under the widened base set], zero growth without water is the 1.0 regression column)"
            << (ok ? QString() : diag);
    });

    // ── r2058c：上限 3 精确墙（2 高柱恰好到 3 + 3 高柱第 4 次拔高恒拒；t406「max5」翻案行为级）──
    runLeg("r2058c cap-3 exactness wall (a height-2 column reaches exactly 3 within the window budget and a height-3 column never takes the 4th increment through the extended window budget [stays exactly 3], unified growth cap 3 = MC 1.0 random-tick cap + placement-face max 3, t406 max5 overturned, t418 potential pillar retired lawfully)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        initCaneWorld(w, 82);
        for (int x = 18; x <= 36; ++x)
            for (int z = 18; z <= 24; ++z)
                w.setBlock(x, yF - 1, z, BR::Stone, 0);
        plantCaneColumn(w, 20, 21, yF, BR::Sand, true, 2); // 2 高柱 → 应恰长到 3
        plantCaneColumn(w, 26, 21, yF, BR::Sand, true, 3); // 3 高柱 → 第 4 次拔高恒拒
        tickCaneWindows(w, 80);
        const bool c1 = caneHeight(w, 20, 21, yF) == 3;    // 恰到 3（NEG-2 下此面绿：cap5 也 ≥3）
        const bool c2 = caneHeight(w, 26, 21, yF) == 3     // 第 4 次拔高恒拒（NEG-2 靶面）
            && w.blockAt(26, yF + 4, 21) == BR::Air;
        tickCaneWindows(w, 80);                            // 延窗复验（恒拒是稳态非时机）
        const bool c3 = caneHeight(w, 26, 21, yF) == 3
            && w.blockAt(26, yF + 4, 21) == BR::Air;
        ok = ok && c1 && c2 && c3;
        if (!(c1 && c2 && c3))
            diag += QStringLiteral("[c1 h=%1 c2 h=%2 c3 h=%3 top=%4]")
                        .arg(caneHeight(w, 20, 21, yF)).arg(c2)
                        .arg(caneHeight(w, 26, 21, yF)).arg(w.blockAt(26, yF + 4, 21));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2058c cap-3 exactness wall (a height-2 column reaches exactly 3 within the window budget and a height-3 column never takes the 4th increment through the extended window budget [stays exactly 3], unified growth cap 3 = MC 1.0 random-tick cap + placement-face max 3, t406 max5 overturned, t418 potential pillar retired lawfully)"
            << (ok ? QString() : diag);
    });

    // ── r2058d：结构钉（单一权威三面源钉 + 真值表 + 常量/锚注/worldgen 口径 + 残留禁出）────────
    //   NEG 靶行入钉面按恰红设计收账：d2 谓词体（NEG-1 靶）/ d5 常量行（NEG-2 靶）——其余子面
    //   与双 NEG 靶行零交集（t1085「NEG 靶行钉面收账」反例设计：本段两靶行**有意**入钉，红面
    //   按「行为腿 + 子面 id」双维归因）。
    runLeg("r2058d structure pins (sugarcaneBaseBlock single-authority truth table + three-face call-site source pins [predicate definition / growth gate / placement gate] + hand-written base-set residual anti-pins, cap constant =3 source pin & =5 anti-pin [NEG-2 target row], t406 lawful-overturn anchor + t418 retirement anchor, worldgen 1..3 column-height formula pin, kSugarcaneTallPct residual zero)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile brf(root + QStringLiteral("/src/Core/blockregistry.cpp"));
        const QString brSrc = brf.open(QIODevice::ReadOnly) ? QString::fromUtf8(brf.readAll()) : QString();
        QFile bhf(root + QStringLiteral("/src/Core/blockregistry.h"));
        const QString bhSrc = bhf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bhf.readAll()) : QString();
        QFile wf(root + QStringLiteral("/src/World/world.cpp"));
        const QString wSrc = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
        QFile whf(root + QStringLiteral("/src/World/world.h"));
        const QString whSrc = whf.open(QIODevice::ReadOnly) ? QString::fromUtf8(whf.readAll()) : QString();
        QFile pcf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
        const QString pcSrc = pcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pcf.readAll()) : QString();
        // (d1) 谓词真值表（Core 运行期单一权威面）：{Grass, Dirt, Sand, Sugarcane} 真 / 其余假。
        const bool d1 = BR::sugarcaneBaseBlock(BR::Grass)
            && BR::sugarcaneBaseBlock(BR::Dirt)
            && BR::sugarcaneBaseBlock(BR::Sand)
            && BR::sugarcaneBaseBlock(BR::Sugarcane)
            && !BR::sugarcaneBaseBlock(BR::Stone)
            && !BR::sugarcaneBaseBlock(BR::Cobble)
            && !BR::sugarcaneBaseBlock(BR::Air)
            && !BR::sugarcaneBaseBlock(BR::Water)
            && !BR::sugarcaneBaseBlock(BR::Farmland)
            && !BR::sugarcaneBaseBlock(BR::Gravel)
            && !BR::sugarcaneBaseBlock(BR::WheatCrop);
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1 truth=0]");
        // (d2) 谓词定义源钉（NEG-1 靶行——谓词体即基材集单一权威，摘 Grass/Dirt 项即红）。
        const bool d2 = brSrc.count(QStringLiteral("bool BlockRegistry::sugarcaneBaseBlock(quint8 groundId)")) == 1
            && brSrc.count(QStringLiteral("groundId == Grass || groundId == Dirt || groundId == Sand")) == 1
            && bhSrc.count(QStringLiteral("static bool sugarcaneBaseBlock(quint8 groundId);")) == 1;
        ok = ok && d2;
        if (!d2)
            diag += QStringLiteral("[d2 def=%1 set=%2 decl=%3]")
                        .arg(brSrc.count(QStringLiteral("bool BlockRegistry::sugarcaneBaseBlock(quint8 groundId)")))
                        .arg(brSrc.count(QStringLiteral("groundId == Grass || groundId == Dirt || groundId == Sand")))
                        .arg(bhSrc.count(QStringLiteral("static bool sugarcaneBaseBlock(quint8 groundId);")));
        // (d3) 生长门调用行源钉（world.cpp）+ 上限消费行在场（消费行不含常量值 → NEG-2 不误伤）。
        const bool d3 = wSrc.count(QStringLiteral("!BlockRegistry::sugarcaneBaseBlock(m_chunks.blockAt(t.x, by - 1, t.z))")) == 1
            && wSrc.count(QStringLiteral("if (height >= kSugarcaneMaxHeight) continue;")) == 1;
        ok = ok && d3;
        if (!d3)
            diag += QStringLiteral("[d3 grow=%1 capuse=%2]")
                        .arg(wSrc.count(QStringLiteral("!BlockRegistry::sugarcaneBaseBlock(m_chunks.blockAt(t.x, by - 1, t.z))")))
                        .arg(wSrc.count(QStringLiteral("if (height >= kSugarcaneMaxHeight) continue;")));
        // (d4) 放置面调用行源钉（playercontroller.cpp）+ 手写基材集残留反探禁出（残留针取旧甘蔗
        //     分支专属形态——5539 仙人掌分支 `below != Sand && below != Cactus` 系另一族合法口径，
        //     不入反探）。
        const bool d4 = pcSrc.count(QStringLiteral("!BlockRegistry::sugarcaneBaseBlock(below)")) == 1
            && pcSrc.count(QStringLiteral("below != BlockRegistry::Sand && below != BlockRegistry::Sugarcane")) == 0;
        ok = ok && d4;
        if (!d4)
            diag += QStringLiteral("[d4 place=%1 old=%2]")
                        .arg(pcSrc.count(QStringLiteral("!BlockRegistry::sugarcaneBaseBlock(below)")))
                        .arg(pcSrc.count(QStringLiteral("below != BlockRegistry::Sand && below != BlockRegistry::Sugarcane")));
        // (d5) 上限常量源钉 =3 + 旧值反探（NEG-2 靶行——改回 5 即红）。
        const bool d5 = whSrc.count(QStringLiteral("static constexpr int kSugarcaneMaxHeight    = 3;")) == 1
            && whSrc.count(QStringLiteral("kSugarcaneMaxHeight    = 5")) == 0;
        ok = ok && d5;
        if (!d5)
            diag += QStringLiteral("[d5 pin3=%1 anti5=%2]")
                        .arg(whSrc.count(QStringLiteral("static constexpr int kSugarcaneMaxHeight    = 3;")))
                        .arg(whSrc.count(QStringLiteral("kSugarcaneMaxHeight    = 5")));
        // (d6) t406 lawful 翻案锚注（world.h）+ t418 退役锚注（world.cpp）在场。
        const bool d6 = whSrc.contains(QStringLiteral("t1088 lawful 翻案"))
            && wSrc.contains(QStringLiteral("t418 拔高潜力门退役"));
        ok = ok && d6;
        if (!d6)
            diag += QStringLiteral("[d6 overturn=%1 retire=%2]")
                        .arg(whSrc.contains(QStringLiteral("t1088 lawful 翻案")))
                        .arg(wSrc.contains(QStringLiteral("t418 拔高潜力门退役")));
        // (d7) worldgen 列高公式钉（1..3 ≤ 上限 3 同批核对一致面——worldgen 与生长上限同口径）。
        //     公式文本与仙人掌散布同形（两族共享 1..3 口径，count==2 逐字同账）；甘蔗实例由其
        //     口径注锚行单独锚定。
        const bool d7 = wSrc.count(QStringLiteral("const int height = 1 + int((r >> 16) % 3u);")) == 2
            && wSrc.contains(QStringLiteral("机制等价 MC 甘蔗 1..3 格柱"));
        ok = ok && d7;
        if (!d7)
            diag += QStringLiteral("[d7 wg=%1 cane=%2]")
                        .arg(wSrc.count(QStringLiteral("const int height = 1 + int((r >> 16) % 3u);")))
                        .arg(wSrc.contains(QStringLiteral("机制等价 MC 甘蔗 1..3 格柱")));
        // (d8) 潜力常量/门残留禁出（退役完备面：定义与调用形态零残留）。
        const bool d8 = whSrc.count(QStringLiteral("static constexpr int kSugarcaneTallPct")) == 0
            && wSrc.count(QStringLiteral("kSugarcaneTallPct) continue;")) == 0;
        ok = ok && d8;
        if (!d8)
            diag += QStringLiteral("[d8 defres=%1 callres=%2]")
                        .arg(whSrc.count(QStringLiteral("static constexpr int kSugarcaneTallPct")))
                        .arg(wSrc.count(QStringLiteral("kSugarcaneTallPct) continue;")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2058d structure pins (sugarcaneBaseBlock single-authority truth table + three-face call-site source pins [predicate definition / growth gate / placement gate] + hand-written base-set residual anti-pins, cap constant =3 source pin & =5 anti-pin [NEG-2 target row], t406 lawful-overturn anchor + t418 retirement anchor, worldgen 1..3 column-height formula pin, kSugarcaneTallPct residual zero)"
            << (ok ? QString() : diag);
    });
}
