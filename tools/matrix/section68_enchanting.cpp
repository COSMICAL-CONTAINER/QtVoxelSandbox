#include "matrix_helpers.h"

// t1104 附魔台 + 附魔机制探针段（4 腿；filter 词 r2074；矩阵 832→836）。置尾先例沿用（接 section67，
//   runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 纯对象直驱；固定 48×48×96 s82 宿主 +
//   坪 y=80 + 上空清空 = section66/67 同款四 setter，t769「先清工作体积」教训在场）。
//
// ── 现状核实裁定表（派工四项逐一 1.0 口径实读，全部在场交付 = 闭环任务；核实结论钉进腿面）──
//   ① 方块 id：工程 EnchantingTable=94（t474），kMcBlockId[94]=116 唯一行（blockregistry.cpp:885）——
//     MC 1.0 真值 116（Beta 1.9 pre3 引入、1.0.0 沿用）核实一致；异形几何 = 0.75 矮盒（collisionTopY
//     特例 + PartialBlockGeometry 顶书盒，忠实简化裁定 t620/t644）+ 程序贴图 §9a（tools/
//     build_enchanting_table.py，top 109 / bottom 77 / side 110）+ 右键开 UI（enchantingTableOpened →
//     Main.qml openEnchantingTable）全在场。
//   ② 配方：1 书 + 2 钻石 + 4 黑曜石（recipe.cpp t474 行，book 顶中 / 钻石中两侧 / 黑曜石其余 = 真 1.0）。
//     **派工期望修正留痕**：派工文「黑曜石 1」与 1.0 实读不符——真 1.0 enchanting table 配方为
//     4 黑曜石，工程在场行即 4 黑曜石，按实读裁定不动（t1103 矿井池无瓜种同型修正先例）。
//   ③ 书架增幅（1.0 口径实读）：World::countBookshelvesAround = 水平切比雪夫距离 2 的 16 格环带 × 两层
//     （y / y+1）+ 半步格空气走道（书架位向台位方向 1 格须 Air）+ 15 封顶（t649 对齐；「检测半径 1
//     空气走道」派工口径 = 半步格空气，在场一致）。忠实简化留痕：MC 还查台位中列空气，本实现只查
//     书架半步格（t474 注释在案，绝大多数摆法同果）。档位门 1/5/10 书架（t649 用户口径钉死）+
//     offered = floor(bs×20×t/33)+t 钳 [1,30]，满 15 书架顶格 30（t795 单一权威）——对 1.0 的
//     逐项随机等级表做确定性简化裁定（锚点在案），非逐字节复刻，简化裁定自 t795 起留痕。
//   ④ 等级消耗：工程 XP 面 = **球拾取累计经验点 + 等级制派生**（PlayerState.addXp 累积 m_xp；level 由
//     MC 1.0 三段曲线 2L+7 / 5L−38 / 9L−158 派生；spendLevels 把总量截到「恰升 (level−N) 所需」= 消耗
//     N 级 + 级内进度清零）——派工问「球拾取还是等级制」实读答案：两者皆有（点数累计为底、等级为派生
//     显值），附魔消耗走等级面。档位消耗映射 = 1+floor(书架/7) 钳 [1,3] 三档（t590）+ 青金石 1/2/3 恒须
//     （**用户定稿口径**：1.0 本无青金石门槛、1.8 加入；工程按用户 8-28 定稿保留恒须，注释钉死于
//     EnchantingTableUI.qml——本任务不改用户定稿，偏差登记留痕）。1.0 严史口径（消耗=档位显示等级整级）
//     与工程的简化三档并录，以用户定稿为准绳。
//   ⑤ 随机附魔表 + compatibility：EnchantRegistry 自有 id 段 1..20（锐锋/亡灵杀手/节肢克星/击退/燃焰 +
//     效率/精准采集/时运 + 耐久 + 保护四族 + 水上亲和 + t960 弓四件/竿两件），**存量族完全够用、零新 id**
//     （派工「禁发明新 id 若存量族够用」核实成立）；pack=(id<<8)|level 落 ItemStack.enchants[4] 四槽；
//     selectEnchantsForItem = isApplicableForItem 逐物品精判池 + offered 阶梯（≥10 双抽 / ≥20 三抽）+
//     等级 = round(maxLevel×offered/30)±1 扰动（t824/t959 单一权威）。
//   ⑥ 应用面核实：锋利 → attackMob 起点 weaponAttackDamage 单一权威（playercontroller.cpp:2541，
//     t825）；效率 → miningTime 第三参（playercontroller.cpp:2851 selectedItemEnchantLevel 读取，
//     t798 分档加法 + 匹配门）；保护/摔落/耐久/水上亲和 EPF 面（t763 全在场）；**全部已生效，无后续登记**。
//   ⑦ 铁砧边界核实（派工「AnvilUI 修复职能单一收口——不构成附魔获得机制」**部分过期**留痕）：铁砧另有
//     anvilDoMergeEnchantsSelected 同 id 合并 / 附魔书合并面（t550 起）= 附魔**转移/合并**面（只取既有
//     max 等级，不产生新随机附魔）；随机获得面唯附魔台一家，边界成立。铁砧本体为 1.4.2 物（1.0 无），
//     工程已在场登记为超时代面（既有裁定，不扩大）。
//
// ── 覆盖缺口（本段四腿只补缺口，既有钉零重抄）──
//   t795（section01）已钉 tier/offered 公式 + 书架计数三层；t824/t959 已钉逐物品池 + 书收窄 + 同源桥；
//   t875 已钉真链门禁 + 不清洗；t917 已钉 tierSeed 源钉；t763 已钉 EPF/耐久/锐锋公式。**零覆盖面**：
//   PlayerState 等级阶梯 + spendLevels 结算（本段 a）；台位锚定的「书架环 → 档位 → offered → 抽签阶梯 →
//   字段落值」整链 + 抽签数量/等级阶梯（本段 b，t824 未钉数量阶梯）；表产物 → 消费面回归柱（本段 c）；
//   UI 路由 + 等级消耗接线源钉族（本段 d，r2063a 漏斗 UI 路由钉同门）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；候选面排查记录在案）──
//   NEG-1 = 摘附魔池抽签数量阶梯（enchantregistry.cpp selectEnchantsForItem 的 `if (lvl >= 10) count = 2;`
//     + `if (lvl >= 20) count = 3;` 两行摘除 → count 恒 1，编译仍绿）→ 恰红 = {r2074b}（数量阶梯断言
//     全失：offered 15→2 / 30→3 不复现；等级阶梯 / 确定性 / 字段落值面不在摘面 = 部分幸存，腿级判 FAIL）。
//     候选面排查：书架计数上层扫描摘除被 t823 叠放=2 钉死（必连带红）→ 弃；tier/offered 公式被 t795
//     逐值钉死（必连带红）→ 弃；同 seed 恒等被 t959⑤ 钉死 → 弃。数量阶梯为 t824/t959 均未钉的唯一
//     附魔池面（t824 只钉池成员并集、t959 只钉成簇/可达/恒等，统计上对 count 恒 1 稳绿——1200 次抽样
//     并集覆盖按最稀权重 1/31 仍 >4σ），恰红可行。b 腿的台位世界面 / offered 锚点面不在摘面。
//   NEG-2 = 摘等级消耗结算（playerstate.cpp spendLevels 本体换 `return false;` 恒拒，编译仍绿）→
//     恰红 = {r2074a}（真付 / 余额不足 / 零消耗防御 / 扣到零四个结算断言全失；曲线阶梯 / 信号纪律 /
//     addLevels 面不在摘面 = 部分幸存）。零碰撞核实：全矩阵无 spendLevels 消费腿（grep 零命中），
//     QML 消费端（EnchantingTableUI.doEnchant）不经矩阵驱动到成功路径（t875 只钉门禁拒入）；d 腿钉
//     playerstate.h **声明行** + EnchantingTableUI.qml **调用行**，两行均不在摘面 → 幸存（豁免设计）。
//   a / c 双 NEG 均不触达（a 走 PlayerState 直驱不碰抽签阶梯；c 走表产物 → 消费权威直调，数量阶梯
//     摘除只改每 seed 产物条数、c 的「扫 seed 找锐锋/效率」在恒 1 抽下依旧可达——锐锋权重 10/32，
//     400 seed 扫描必中）→ 对照腿。
//   d 腿钉面全在双 NEG 摘面之外（函数声明 / 调用行 / def 行 / 配方行 / 路由行），= 结构钉对照腿。

namespace {

// fixed 宿主小世界 incantation（section66/67 同款四 setter）。
inline void initFixedEnchantWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空（坪 y=80 闭区间，上空 y 81..92 清 Air；台位环带 y 81/82 全落净空带内）。
inline void layEnchantPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径（t1102 r2072 置尾腿共用式：applicationDirPath/../src；本段匿名空间同式自持）。
inline QString srcRootForPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 裸读源文件原文（块注释锚面——pinSet 剥注释不可锚时用；r2067d readRaw 同门收拢）。
inline QString rawSource(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// 抽签产物 → 4 槽 packed 数组（enchants 字段落值单一形态：EnchantRegistry::pack）。
inline void fillPackedArr(const QVariantList &picks, int *arr)
{
    for (int i = 0; i < 4; ++i) arr[i] = 0;
    for (int i = 0; i < picks.size() && i < 4; ++i) {
        const QVariantMap m = picks.at(i).toMap();
        arr[i] = EnchantRegistry::pack(m.value(QStringLiteral("id")).toInt(),
                                       m.value(QStringLiteral("level")).toInt());
    }
}

} // namespace

void MatrixRun::section68_enchanting()
{
    // ── r2074a：XP 等级阶梯 + 附魔等级消耗结算承重墙（PlayerState 直驱；NEG-2 敏感面）──────────────
    //   曲线钉（MC 1.0 三段 2L+7 / 5L−38 / 9L−158 经公开面读回：need 以 xpToNextLevel 显形、总量以
    //   升级边界 xp() 显形——xpNeedForLevel/xpTotalForLevel 为私有静态，矩阵只走公开观测面）：0/1/2/14/15/30
    //   级锚点 7/9/11/35/37/112 + 总量边界 7/16/280/315/1395 + 级内条填充比。结算钉（NEG-2 面）：真付
    //   2 级 → 恰截到 level3 总量 27（级内清零）；余额不足 4 级 → false + 零副作用零信号；0 级防御
    //   true no-op；扣到 0 级 → 干净基线；addLevels 整级抬升（/xp L 面）+ 单次 addXp 跨 5 级。
    runLeg("r2074a xp level ladder and enchantment level settlement load-bearing wall (the mc-style"
        " curve pins per-level costs 7/9/11 at the floor rungs 35/37 across the mid gate and 112 at"
        " level thirty with exact totals 7/16/280/315/1395 at the upgrade boundaries and a bar"
        " fraction of seven fifteenths mid rung, spending two levels at level five truncates the"
        " total to exactly the level-three sum of twenty-seven with the bar denominator following,"
        " a spend larger than the level answers false with zero side effects and zero signals, a"
        " zero spend is a defensive true no-op, spending down to level zero leaves a clean seven-xp"
        " baseline, add-levels raises whole levels with in-level progress cleared, and one orb feed"
        " of fifty-five crosses five rungs in a single call with the level signal fired)", [&]() {
        PlayerState ps;
        bool ok = true;
        QString diag;
        int lvlChanges = 0, xpChanges = 0;
        const QMetaObject::Connection connL = QObject::connect(
            &ps, &PlayerState::levelChanged, [&]() { ++lvlChanges; });
        const QMetaObject::Connection connX = QObject::connect(
            &ps, &PlayerState::xpChanged, [&]() { ++xpChanges; });
        // ── 曲线阶梯（公开面读回）──
        ok = ok && ps.level() == 0 && ps.xp() == 0 && ps.xpToNextLevel() == 7;
        ps.addXp(6);
        ok = ok && ps.level() == 0 && ps.xp() == 6 && ps.xpToNextLevel() == 7;   // 级内不升
        ps.addXp(1);
        ok = ok && ps.level() == 1 && ps.xp() == 7 && ps.xpToNextLevel() == 9;   // 过界升 1
        ps.addXp(9);
        ok = ok && ps.level() == 2 && ps.xp() == 16 && ps.xpToNextLevel() == 11;
        ps.setXp(280);
        ok = ok && ps.level() == 14 && ps.xp() == 280 && ps.xpToNextLevel() == 35; // 低段顶
        ps.setXp(315);
        ok = ok && ps.level() == 15 && ps.xp() == 315 && ps.xpToNextLevel() == 37; // 中段起
        ps.setXp(1395);
        ok = ok && ps.level() == 30 && ps.xp() == 1395 && ps.xpToNextLevel() == 112; // 高段锚
        ps.setXp(47); // level 4（total 40）+ 级内 7/15
        ok = ok && ps.level() == 4 && ps.xp() == 47
                  && std::fabs(ps.xpBarFraction() - 7.0 / 15.0) < 1e-9;
        if (!ok) diag += QStringLiteral("[curve]");
        // ── 结算面（NEG-2 摘 spendLevels 本体 → 本段起四断言红）──
        const int lvlBeforeSpend = lvlChanges, xpBeforeSpend = xpChanges;
        ps.setXp(55);
        ok = ok && ps.level() == 5 && ps.xp() == 55 && ps.xpToNextLevel() == 17;
        const bool spent2 = ps.spendLevels(2);
        ok = ok && spent2 && ps.level() == 3 && ps.xp() == 27
                  && ps.xpToNextLevel() == 13;                                   // 级内清零面
        const int lvlBeforeNeg = lvlChanges, xpBeforeNeg = xpChanges;
        const bool overspend = ps.spendLevels(4);
        ok = ok && !overspend && ps.level() == 3 && ps.xp() == 27;               // 余额不足拒
        ok = ok && lvlChanges == lvlBeforeNeg && xpChanges == xpBeforeNeg;       // 零副作用零信号
        const bool zeroSpend = ps.spendLevels(0);
        ok = ok && zeroSpend && ps.level() == 3 && ps.xp() == 27;                // 防御 no-op
        const bool drain = ps.spendLevels(3);
        ok = ok && drain && ps.level() == 0 && ps.xp() == 0 && ps.xpToNextLevel() == 7;
        if (!(spent2 && !overspend && zeroSpend && drain)) diag += QStringLiteral("[settle]");
        // ── 整级抬升 + 单呼跨 5 级（信号纪律：真升才发）──
        ps.addLevels(2);
        ok = ok && ps.level() == 2 && ps.xp() == 16 && ps.xpToNextLevel() == 11;
        ps.setXp(0);
        const int lvlBeforeJump = lvlChanges;
        ps.addXp(55);
        ok = ok && ps.level() == 5 && lvlChanges > lvlBeforeJump;                // 跨级发信号
        ok = ok && bool(connL) && bool(connX);                                   // 连接活性（恒真合流）
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2074a xp level ladder and enchantment level settlement load-bearing wall (the"
               " mc-style curve pins per-level costs 7/9/11 at the floor rungs 35/37 across the mid"
               " gate and 112 at level thirty with exact totals 7/16/280/315/1395 at the upgrade"
               " boundaries and a bar fraction of seven fifteenths mid rung, spending two levels at"
               " level five truncates the total to exactly the level-three sum of twenty-seven with"
               " the bar denominator following, a spend larger than the level answers false with"
               " zero side effects and zero signals, a zero spend is a defensive true no-op,"
               " spending down to level zero leaves a clean seven-xp baseline, add-levels raises"
               " whole levels with in-level progress cleared, and one orb feed of fifty-five"
               " crosses five rungs in a single call with the level signal fired)"
            << (ok ? QString() : diag);
    });

    // ── r2074b：书架增幅 → 档位 → offered → 抽签阶梯 + 字段落值 承重墙（台位锚定世界 rig；NEG-1 敏感面）──
    //   世界面（既有 t795 计数公式在台位锚定场景的部分复钉 = 链条胶水，非新权威）：台位格入世 → 净空 0 →
    //   **仅上层环带 6 书架**（y+1 层）计 6 → 档 2 → offered(6,1)=9；满环 15 → 档 3 → offered [10,20,30]；
    //   堵角半步 -1 → 14（t795 角位半步教训复用）。抽签阶梯（NEG-1 摘面；t824/t959 均未钉的唯一池面）：
    //   钻石剑池 6 候选 → offered 5 恒 1 抽 / 15 恒 2 抽 / 30 恒 3 抽（seed 0..39 全扫；三抽不受互斥组
    //   耗尽影响——组 1 三选一后 KB/FA/U 仍足）；等级阶梯：锐锋在 offered 30 恒 5 级（(5×30+15)/30=5 + 扰动
    //   钳顶）/ offered 2 恒 1 级（25/30=0 + 扰动钳底）；确定性：同 (物品, offered, seed) 两次逐条相等；
    //   字段落值：产物逐条 pack → 4 槽数组 → findLevel 读回恒等 + packEnchantId/packLevel 往返。
    runLeg("r2074b bookshelf boost through tier unlock and offered level into the deterministic draw"
        " ladder with field landing (a table-anchored world rig answers zero on a cleared ring, an"
        " upper-layer ring of six bookshelves counts six and unlocks tier two at offered nine, the"
        " full fifteen-shelf ring caps at fifteen and unlocks tier three at offered ten twenty"
        " thirty, and one blocked corner half-step drops the count by one; the draw ladder answers"
        " exactly one pick below ten offered exactly two picks from ten and exactly three from"
        " twenty across the forty-seed sweep on the six-candidate sword pool, sharpness lands at"
        " level five whenever picked at offered thirty and level one at offered two, the same item"
        " level and seed reproduces the identical pick list, and every pick lands in the four-slot"
        " packed field readable back through find level with the pack unpack round trip"
        " identity)", [&]() {
        bool ok = true;
        QString diag;
        // ── 世界面：台位锚定书架环（section66/67 同款 fixed 宿主）──
        World wB;
        initFixedEnchantWorld(wB);
        layEnchantPlatform(wB, 12, 38, 20, 28);
        const int tx = 24, ty = 81, tz = 24;
        wB.setBlock(tx, ty, tz, BR::EnchantingTable, 0);
        ok = ok && BR::isEnchantingTable(wB.blockAt(tx, ty, tz));
        const int cntEmpty = wB.countBookshelvesAround(tx, ty, tz);
        ok = ok && cntEmpty == 0
                  && EnchantRegistry::tierForBookshelves(cntEmpty) == 1
                  && EnchantRegistry::offeredLevelFor(cntEmpty, 0) == 1;
        // 仅上层环带（y+1 层）6 书架（北边中三格 + 东侧角/中两格 + 西中一格；半步全净空）→ 计 6。
        const int upCells[6][2] = { { -2, -2 }, { -2, 0 }, { -2, 2 }, { 0, 2 }, { 2, 2 }, { 2, 0 } };
        for (const auto &c : upCells)
            wB.setBlock(tx + c[0], ty + 1, tz + c[1], BR::Bookshelf, 0);
        const int cntUpper = wB.countBookshelvesAround(tx, ty, tz);
        ok = ok && cntUpper == 6
                  && EnchantRegistry::tierForBookshelves(cntUpper) == 2
                  && EnchantRegistry::offeredLevelFor(cntUpper, 1) == 9;
        // 补下层 9 格（不与上层同格重复）→ 满环带 15 → 封顶；offered 顶格 [10,20,30]。
        int placedLower = 0;
        for (int dx = -2; dx <= 2 && placedLower < 9; ++dx)
            for (int dz = -2; dz <= 2 && placedLower < 9; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                bool alreadyUp = false;
                for (const auto &c : upCells)
                    alreadyUp |= c[0] == dx && c[1] == dz;
                if (alreadyUp) continue;
                wB.setBlock(tx + dx, ty, tz + dz, BR::Bookshelf, 0);
                ++placedLower;
            }
        const int cntFull = wB.countBookshelvesAround(tx, ty, tz);
        ok = ok && placedLower == 9 && cntFull == 15
                  && EnchantRegistry::tierForBookshelves(cntFull) == 3
                  && EnchantRegistry::offeredLevelFor(cntFull, 0) == 10
                  && EnchantRegistry::offeredLevelFor(cntFull, 1) == 20
                  && EnchantRegistry::offeredLevelFor(cntFull, 2) == 30;
        // 堵角位书架 (2,0,−2) 的专属半步 (1,0,−1) → 恰 −1（角位半步只服务自己，t795 踩坑注复用；
        //   四角中仅此角在**下层**实存书架，其余三角或在上层 / 或为空——半步按层匹配，堵错层零效果）。
        wB.setBlock(tx + 1, ty, tz - 1, BR::Cobble, 0);
        const int cntBlocked = wB.countBookshelvesAround(tx, ty, tz);
        wB.setBlock(tx + 1, ty, tz - 1, BR::Air, 0);
        ok = ok && cntBlocked == 14;
        if (!(cntEmpty == 0 && cntUpper == 6 && cntFull == 15 && cntBlocked == 14))
            diag += QStringLiteral("[ring %1/%2/%3/%4]")
                        .arg(cntEmpty).arg(cntUpper).arg(cntFull).arg(cntBlocked);
        // ── 抽签阶梯（NEG-1 摘面：count 两行摘除 → 下列尺寸断言全红）──
        Hotbar hb;
        const int diaSword = int(ToolRegistry::DiamondSword);
        int minP = 99, maxP = 0;
        bool sizeLadder = true, sharpLv = true;
        for (int seed = 0; seed < 40; ++seed) {
            const int n5 = EnchantRegistry::selectEnchantsForItem(diaSword, 5, seed).size();
            const int n15 = EnchantRegistry::selectEnchantsForItem(diaSword, 15, seed).size();
            const QVariantList p30 = EnchantRegistry::selectEnchantsForItem(diaSword, 30, seed);
            sizeLadder = sizeLadder && n5 == 1 && n15 == 2 && p30.size() == 3;
            minP = std::min(minP, n5); maxP = std::max(maxP, n5);
            for (const QVariant &v : p30) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == int(EnchantRegistry::Sharpness))
                    sharpLv = sharpLv && m.value(QStringLiteral("level")).toInt() == 5;
            }
            const QVariantList p2 = EnchantRegistry::selectEnchantsForItem(diaSword, 2, seed);
            for (const QVariant &v : p2) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == int(EnchantRegistry::Sharpness))
                    sharpLv = sharpLv && m.value(QStringLiteral("level")).toInt() == 1;
            }
        }
        ok = ok && sizeLadder && sharpLv && minP == 1 && maxP == 1;
        if (!(sizeLadder && sharpLv)) diag += QStringLiteral("[ladder size=%1 lv=%2]")
                                                 .arg(sizeLadder).arg(sharpLv);
        // 确定性（同 item/offered/seed 逐条恒等）+ 桥接同源（链条胶水；t959 同式）。
        const QVariantList pA = EnchantRegistry::selectEnchantsForItem(diaSword, 30, 4242);
        const QVariantList pB = EnchantRegistry::selectEnchantsForItem(diaSword, 30, 4242);
        const QVariantList viaBridge = hb.selectEnchantsPreviewForItem(diaSword, 30, 4242);
        bool det = pA.size() == pB.size() && pA.size() == viaBridge.size() && !pA.isEmpty();
        for (int i = 0; det && i < pA.size(); ++i)
            det = pA.at(i).toMap().value(QStringLiteral("id")) == pB.at(i).toMap().value(QStringLiteral("id"))
               && pA.at(i).toMap().value(QStringLiteral("level")) == pB.at(i).toMap().value(QStringLiteral("level"))
               && pA.at(i).toMap().value(QStringLiteral("id")) == viaBridge.at(i).toMap().value(QStringLiteral("id"))
               && pA.at(i).toMap().value(QStringLiteral("level")) == viaBridge.at(i).toMap().value(QStringLiteral("level"));
        ok = ok && det;
        if (!det) diag += QStringLiteral("[determinism]");
        // 字段落值：产物逐条 pack → 4 槽 → findLevel 读回 + unpack 往返恒等。
        int arr[4];
        fillPackedArr(pA, arr);
        bool field = pA.size() <= 4;
        for (int i = 0; field && i < pA.size(); ++i) {
            const QVariantMap m = pA.at(i).toMap();
            const int id = m.value(QStringLiteral("id")).toInt();
            const int lv = m.value(QStringLiteral("level")).toInt();
            field = field && EnchantRegistry::findLevel(arr, id) == lv
                    && EnchantRegistry::packEnchantId(arr[i]) == id
                    && EnchantRegistry::packLevel(arr[i]) == lv;
        }
        field = field && pA.size() >= 1 && pA.size() <= 4;
        if (pA.size() < 4) field = field && arr[3] == 0;   // 未占槽恒 0（防误写）
        ok = ok && field;
        if (!field) diag += QStringLiteral("[field]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2074b bookshelf boost through tier unlock and offered level into the"
               " deterministic draw ladder with field landing (a table-anchored world rig answers"
               " zero on a cleared ring, an upper-layer ring of six bookshelves counts six and"
               " unlocks tier two at offered nine, the full fifteen-shelf ring caps at fifteen and"
               " unlocks tier three at offered ten twenty thirty, and one blocked corner half-step"
               " drops the count by one; the draw ladder answers exactly one pick below ten offered"
               " exactly two picks from ten and exactly three from twenty across the forty-seed"
               " sweep on the six-candidate sword pool, sharpness lands at level five whenever"
               " picked at offered thirty and level one at offered two, the same item level and"
               " seed reproduces the identical pick list, and every pick lands in the four-slot"
               " packed field readable back through find level with the pack unpack round trip"
               " identity)"
            << (ok ? QString() : diag);
    });

    // ── r2074c：表产物 → 既有消费面回归柱（双 NEG 均不触达 = 对照腿）──────────────────────────────
    //   附魔台随机产物（selectEnchantsPreviewForItem，与 UI 点档同入口）落 4 槽字段 → 三条既有消费面
    //   直调回归：① 锋利 → weaponAttackDamage（attackMob 起点 t825 单一权威）：扫 seed 找锐锋产物 →
    //   伤害 == 基础 + 0.5×级（与合成数组同值 = 非锐锋伴生附魔不串扰）> 基础 + tooltip 显示面 round 恒等；
    //   ② 效率 → Hotbar::selectedItemEnchantLevel（playercontroller miningTime 第三参的读取面）+
    //   ToolRegistry::miningTime 对匹配工具-方块真加速（t798 匹配门行为级）；③ 源钉族：四条消费接线行
    //   （silk/fortune/efficiency 读取行 + weaponAttackDamage 起点行 + miningTime 第三参行）。
    //   保护 / 摔落 / 耐久 / 水上亲和 EPF 面与 tooltip 公式已由 t763 钉死（本段零重抄，头注登记）。
    runLeg("r2074c enchant table product consumption face regression column (the sword sweep finds a"
        " sharpness-bearing cast whose four-slot field raises the attack authority by exactly half"
        " a heart per level over the bare weapon with the co-picked lines provably not bleeding"
        " into the damage and the tooltip display rounding the same authority, the pickaxe sweep"
        " finds an efficiency-bearing cast whose field reads back through the selected-slot"
        " accessor that the mining clock consumes and makes the stone mining time strictly faster"
        " than the unenchanted baseline, and the four consumption wiring lines stay pinned at"
        " their single call sites)", [&]() {
        bool ok = true;
        QString diag;
        Hotbar hb;
        const int diaSword = int(ToolRegistry::DiamondSword);
        const int diaPick = int(ToolRegistry::PickaxeDiamond);
        const int SH = int(EnchantRegistry::Sharpness);
        const int EF = int(EnchantRegistry::Efficiency);
        int zeroArr[4] = { 0, 0, 0, 0 };
        const float baseDmg = EnchantRegistry::weaponAttackDamage(diaSword, zeroArr);
        // ① 锋利回归（扫 seed 必中：锐锋权重 10 / 池权 32，40 步漏扫率 <1e-9）。
        bool sharpFound = false, sharpFace = false;
        for (int seed = 0; seed < 400 && !sharpFound; ++seed) {
            const QVariantList picks = hb.selectEnchantsPreviewForItem(diaSword, 30, seed);
            int shLv = 0;
            for (const QVariant &v : picks)
                if (v.toMap().value(QStringLiteral("id")).toInt() == SH)
                    shLv = v.toMap().value(QStringLiteral("level")).toInt();
            if (shLv <= 0) continue;
            sharpFound = true;
            int arr[4];
            fillPackedArr(picks, arr);
            int onlySharp[4] = { 0, 0, 0, 0 };
            onlySharp[0] = EnchantRegistry::pack(SH, shLv);
            const float dmg = EnchantRegistry::weaponAttackDamage(diaSword, arr);
            const float dmgIso = EnchantRegistry::weaponAttackDamage(diaSword, onlySharp);
            const int shown = hb.displayAttackDamage(diaSword, QVariantList{arr[0], arr[1], arr[2], arr[3]});
            sharpFace = std::fabs(dmg - (baseDmg + 0.5f * float(shLv))) < 1e-4f
                     && std::fabs(dmg - dmgIso) < 1e-4f      // 伴生附魔不串扰
                     && dmg > baseDmg
                     && std::fabs(float(shown) - dmg) <= 0.5f + 1e-4f;
        }
        ok = ok && sharpFound && sharpFace;
        if (!(sharpFound && sharpFace)) diag += QStringLiteral("[sharp found=%1 face=%2]")
                                                 .arg(sharpFound).arg(sharpFace);
        // ② 效率回归（镐池 {效率10,精准1,时运2,耐久5}，30 offered 双抽必含效率高概率；400 步兜底）。
        bool effFound = false, effFace = false;
        for (int seed = 0; seed < 400 && !effFound; ++seed) {
            const QVariantList picks = hb.selectEnchantsPreviewForItem(diaPick, 30, seed);
            int efLv = 0;
            for (const QVariant &v : picks)
                if (v.toMap().value(QStringLiteral("id")).toInt() == EF)
                    efLv = v.toMap().value(QStringLiteral("level")).toInt();
            if (efLv <= 0) continue;
            effFound = true;
            int arr[4];
            fillPackedArr(picks, arr);
            hb.setStack(0, diaPick, 1, -1, QVariantList{arr[0], arr[1], arr[2], arr[3]});
            hb.setSelectedSlot(0);
            const float tPlain = ToolRegistry::miningTime(int(BR::Stone), diaPick, 0);
            const float tEff = ToolRegistry::miningTime(int(BR::Stone), diaPick, efLv);
            effFace = hb.selectedItemEnchantLevel(EF) == efLv
                   && EnchantRegistry::findLevel(arr, EF) == efLv
                   && tEff < tPlain;                            // 匹配工具-方块真加速
        }
        ok = ok && effFound && effFace;
        if (!(effFound && effFace)) diag += QStringLiteral("[eff found=%1 face=%2]")
                                               .arg(effFound).arg(effFace);
        hb.setStack(0, 0, 0);
        // ③ 消费接线源钉族（单点调用行；NEG 双摘面零钉）。
        const QStringList missPc = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("sharpness entry", "EnchantRegistry::weaponAttackDamage(heldItemId, heldEnch)", 1),
            SrcPin("efficiency read", "m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Efficiency)", 1),
            SrcPin("silk read", "m_hotbar->selectedItemEnchantLevel(EnchantRegistry::SilkTouch)", 1),
            SrcPin("fortune read", "m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Fortune)", 1),
            SrcPin("mining clock", "ToolRegistry::miningTime(bid, heldItemId, effLvl)", 1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty()) diag += QStringLiteral("[pins %1]").arg(missPc.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2074c enchant table product consumption face regression column (the sword sweep"
               " finds a sharpness-bearing cast whose four-slot field raises the attack authority"
               " by exactly half a heart per level over the bare weapon with the co-picked lines"
               " provably not bleeding into the damage and the tooltip display rounding the same"
               " authority, the pickaxe sweep finds an efficiency-bearing cast whose field reads"
               " back through the selected-slot accessor that the mining clock consumes and makes"
               " the stone mining time strictly faster than the unenchanted baseline, and the four"
               " consumption wiring lines stay pinned at their single call sites)"
            << (ok ? QString() : diag);
    });

    // ── r2074d：结构钉族 + UI 路由钉 + 相邻族零污染（双 NEG 摘面全豁免 = 结构钉对照腿）────────────────
    //   方块 id 94 / kMc 116 唯一行（运行期全表扫 + 裸读锚）/ def 行逐字段 / 0.75 碰撞特例 / 石质音色 /
    //   图标 case / 配方行（1 书+2 钻+4 黑曜石，命中 + 错心负例 + 2×2 拒纳）/ maxStack 双面 / isEnchanting
    //   Table 谓词 / XP 面声明行 / UI 路由族（信号声明 + 发射行 + Main.qml 函数/属性/实例化/互斥级联 +
    //   EnchantingTableUI 等级门槛/等级消耗/青金石校验/书架查询/档位桥五行）/ 相邻族零污染（0x28A /
    //   0x287 / Count 151 / 图集 201 原值）。
    runLeg("r2074d structure pin family and ui route column (the enchanting table sits at engine id"
        " ninety-four with the mc mapping one sixteen as the unique table row, the def row pins"
        " every field from the tile triple through the pickaxe gate and the self drop to the"
        " sixty-four stack and both display names, the collision top answers zero point seven five,"
        " the stone sound case and the icon case stay pinned, the recipe answers the one-book"
        " two-diamond four-obsidian grid with a count of one and rejects the wrong-center grid and"
        " the two-by-two grid, the predicate singles out the table, the xp face declarations stay"
        " pinned, the ui route family holds from the controller signal through the main window"
        " open close and mutex cascade to the panel instantiation and the five level and lapis and"
        " bookshelf wiring lines inside the panel, and the neighbouring melon and mundane and"
        " brewing families keep their id atlas and count sentinels untouched)", [&]() {
        bool ok = true;
        QString diag;
        // (S1) id / kMc 映射唯一性（运行期全表扫）+ 相邻族零污染。
        Hotbar hb;
        int mc116Rows = 0;
        for (int i = 0; i < int(BR::Count); ++i)
            mc116Rows += BR::mcBlockId(quint8(i)) == 116 ? 1 : 0;
        const bool idsOk = int(BR::EnchantingTable) == 94
            && mc116Rows == 1
            && BR::mcBlockId(quint8(BR::EnchantingTable)) == 116
            && int(BR::Count) == 151                       // 相邻族原值（t1103 段尾）
            && int(BR::AtlasTileCount) == 201
            && RecipeRegistry::MelonSliceId == 0x28A       // 0x28A 段尾原值
            && RecipeRegistry::MundanePotionId == 0x287    // t1102 段尾原值
            && int(BR::BrewingStand) == 148                // t1097 邻族原值
            && hb.maxStackSize(int(BR::EnchantingTable)) == 64
            && BR::maxStackSize(int(BR::EnchantingTable)) == 64;
        ok = ok && idsOk;
        if (!idsOk) diag += QStringLiteral("[ids mc116=%1]").arg(mc116Rows);
        // (S2) def 行逐字段（t474 行原文对读）+ 0.75 碰撞特例 + 谓词。
        const auto &ed = BR::def(BR::EnchantingTable);
        const bool defOk = ed.topTile == 109 && ed.bottomTile == 77
            && ed.sideTile == 110 && ed.frontTile == 110
            && !ed.solid && ed.shape == int(BR::ShapeFull) && ed.hardness == 5.0f
            && ed.toolType == int(BR::Pickaxe) && ed.minToolTier == 1 && ed.requiresTool
            && ed.dropId == int(BR::EnchantingTable) && ed.dropCount == 1 && ed.maxStack == 64
            && QLatin1String(ed.name) == QLatin1String("enchanting_table")
            && QString::fromUtf8(ed.display) == QStringLiteral("附魔台")
            && BR::collisionTopY(quint8(BR::EnchantingTable), 0) == 0.75f
            && BR::isEnchantingTable(quint8(BR::EnchantingTable))
            && !BR::isEnchantingTable(0) && !BR::isEnchantingTable(quint8(BR::BrewingStand));
        // 图标面：iconFileForBlock 是 hotbar.cpp 文件级函数（非 Hotbar 公有方法，外部不可调）→
        //   裸名走源钉（iconSourceForBlock 运行期走 pack 链物化缓存 URL，不可等值钉裸名——首跑踩坑）。
        const QStringList missHbIcon = pinSet(srcRootForPins() + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("icon case", "icon_enchanting_table.png", 1)});
        const bool defOk2 = defOk && missHbIcon.isEmpty();
        ok = ok && defOk2;
        if (!defOk) {
            diag += QStringLiteral("[def] top=%1 bottom=%2 side=%3 front=%4")
                        .arg(ed.topTile).arg(ed.bottomTile).arg(ed.sideTile).arg(ed.frontTile);
            diag += QStringLiteral("[def] solid=%1 shape=%2 hard=%3")
                        .arg(ed.solid).arg(ed.shape).arg(ed.hardness);
            diag += QStringLiteral("[def] tool=%1 tier=%2 rt=%3 drop=%4/%5 ms=%6")
                        .arg(ed.toolType).arg(ed.minToolTier).arg(ed.requiresTool)
                        .arg(ed.dropId).arg(ed.dropCount).arg(ed.maxStack);
            diag += QStringLiteral("[def] name=%1 disp=%2 colTop=%3 isET=%4 iconPin=%5")
                        .arg(QString::fromUtf8(ed.name)).arg(QString::fromUtf8(ed.display))
                        .arg(BR::collisionTopY(quint8(BR::EnchantingTable), 0))
                        .arg(BR::isEnchantingTable(quint8(BR::EnchantingTable)))
                        .arg(missHbIcon.join(QLatin1Char(',')));
        }
        // (S3) 配方行：1 书 + 2 钻 + 4 黑曜石（真 1.0；派工「黑曜石 1」修正留痕见段头）命中 + 负例。
        const int BK = RecipeRegistry::BookId, DM = RecipeRegistry::DiamondId, OB = int(BR::Obsidian);
        const int gridOk3x3[9] = { 0, BK, 0, DM, OB, DM, OB, OB, OB };
        const int gridWrongCenter[9] = { 0, BK, 0, DM, DM, DM, OB, OB, OB };
        const RecipeRegistry::Recipe *hit = RecipeRegistry::match(gridOk3x3, 3);
        const bool recipeOk = hit != nullptr
            && hit->outputId == int(BR::EnchantingTable) && hit->outputCount == 1
            && QLatin1String(hit->name) == QLatin1String("enchanting_table")
            && RecipeRegistry::match(gridWrongCenter, 3) == nullptr
            && RecipeRegistry::match(gridOk3x3, 2) == nullptr;   // 3×3 包围盒拒入 2×2
        ok = ok && recipeOk;
        if (!recipeOk) diag += QStringLiteral("[recipe]");
        // (S4) 源钉族（剥注释 pinSet 代码行 + 裸读注释锚双口径）。
        const QStringList missPcH = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.h"), {
            SrcPin("signal decl", "void enchantingTableOpened(int x, int y, int z);", 1)});
        const QStringList missPcC = pinSet(srcRootForPins() + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("emit row", "emit enchantingTableOpened(m_hitBx, m_hitBy, m_hitBz);", 1)});
        const QStringList missPsH = pinSet(srcRootForPins() + QStringLiteral("/Game/playerstate.h"), {
            SrcPin("spend decl", "Q_INVOKABLE bool spendLevels(int amount);", 1),
            SrcPin("curve decl", "static int xpNeedForLevel(int level);", 1)});
        const QStringList missMq = pinSet(srcRootForPins() + QStringLiteral("/ui/Main.qml"), {
            SrcPin("open prop", "property bool enchantingTableOpen: false", 1),
            SrcPin("open fn", "function openEnchantingTable(x, y, z)", 1),
            SrcPin("close fn", "function closeEnchantingTable()", 1),
            SrcPin("route fn", "function onEnchantingTableOpened(x, y, z)", 1),
            SrcPin("panel type", "EnchantingTableUI {", 1),
            SrcPin("panel id", "id: enchantingPanel", 1),
            SrcPin("panel visible", "visible: window.appState === \"playing\" && window.enchantingTableOpen", 1),
            SrcPin("mutex cascade", "if (enchantingTableOpen) closeEnchantingTable()", 5)});
        const QStringList missEq = pinSet(srcRootForPins() + QStringLiteral("/ui/EnchantingTableUI.qml"), {
            SrcPin("level gate", "root.playerLevel < lvlCost", 1),
            SrcPin("spend call", "playerState.spendLevels(lvlCost)", 1),
            SrcPin("lapis gate", "root.lapisCount < lapCost", 1),
            SrcPin("bookshelf query", "countBookshelvesAround(enchantX, enchantY, enchantZ)", 1),
            SrcPin("tier bridge", "enchantTierForBookshelves(root.bookshelfPower)", 1),
            SrcPin("offered bridge", "enchantOfferedLevel(root.bookshelfPower, slotIdx)", 1)});
        const QStringList missBrH = pinSet(srcRootForPins() + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("enum row", "EnchantingTable = 94,", 1)});
        const QStringList missBrC = pinSet(srcRootForPins() + QStringLiteral("/Core/blockregistry.cpp"), {
            SrcPin("height row", "return 0.75f;", 1),
            SrcPin("def row", "{int(BlockRegistry::EnchantingTable),", 1)});
        // 块注释锚（pinSet 剥注释不可达 → 裸读）：kMc 116 行 + 石质音色 case。
        const QString brRaw = rawSource(srcRootForPins() + QStringLiteral("/Core/blockregistry.cpp"));
        const bool kMcAnchor = brRaw.contains(QStringLiteral("116, // t474"));
        const bool soundAnchor = brRaw.contains(QStringLiteral("case EnchantingTable: // t474"));
        const bool pinsOk = missPcH.isEmpty() && missPcC.isEmpty() && missPsH.isEmpty()
            && missMq.isEmpty() && missEq.isEmpty() && missBrH.isEmpty() && missBrC.isEmpty()
            && kMcAnchor && soundAnchor;
        ok = ok && pinsOk;
        if (!pinsOk) {
            const QStringList allMiss = QStringList()
                << missPcH << missPcC << missPsH << missMq << missEq << missBrH << missBrC;
            QString extra;
            if (!kMcAnchor) extra += QStringLiteral("[kMc anchor]");
            if (!soundAnchor) extra += QStringLiteral("[sound anchor]");
            diag += QStringLiteral("[pins %1%2]").arg(allMiss.join(QLatin1Char(','))).arg(extra);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2074d structure pin family and ui route column (the enchanting table sits at"
               " engine id ninety-four with the mc mapping one sixteen as the unique table row,"
               " the def row pins every field from the tile triple through the pickaxe gate and"
               " the self drop to the sixty-four stack and both display names, the collision top"
               " answers zero point seven five, the stone sound case and the icon case stay"
               " pinned, the recipe answers the one-book two-diamond four-obsidian grid with a"
               " count of one and rejects the wrong-center grid and the two-by-two grid, the"
               " predicate singles out the table, the xp face declarations stay pinned, the ui"
               " route family holds from the controller signal through the main window open close"
               " and mutex cascade to the panel instantiation and the five level and lapis and"
               " bookshelf wiring lines inside the panel, and the neighbouring melon and mundane"
               " and brewing families keep their id atlas and count sentinels untouched)"
            << (ok ? QString() : diag);
    });
}
