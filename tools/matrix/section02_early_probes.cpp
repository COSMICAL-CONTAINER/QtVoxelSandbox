// tools/matrix/section02_early_probes.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L6157-12287 逐字节搬移（段 md5: c9937470e0ffc10128d288c94d7e73db；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section02_early_probes()
{

    // ── t791 骨粉催熟平衡探针（spec R19.12 🅵「3-4 个骨粉应催熟一株」；World::applyBonemeal 直调锁数值分布）──
    // 背景：t447 原实现每骨粉 +1 阶段 → 0..7 共 8 阶段要 7 骨粉（用户实测「多个骨粉催不熟一株」）。t791 对齐
    //   MC 骨粉「+2~5 阶段」推进语义、压缩上界为 +2..3 → 从阶段 0 恰 3-4 骨粉催熟（数学保证与哈希质量无关：
    //   3 骨粉推进和 ∈ [6,9]，≥7 即 3 骨粉熟；2+2+2=6 时第 4 骨粉钳到 7 → uses ∈ {3,4} 恒成立）。树苗走 MC 1.0
    //   sapling bone meal 45% 概率即时成树（概率判定非阶段推进）+ 支撑 / 主干畅通守卫（光照豁免）；浆果丛
    //   +1 阶段封顶。rig 寻址：运行期扫描 y40..47 全净空 20×5 区（P20 先例——nextSlot 网格已被前序循环探针
    //   耗尽；树苗须 y≤41 才容得下 4 格主干 + 2 格树冠余量 → 净空须验到 y47）。
    runLegMulti({ "t791 bonemeal balance: crops advance 2-3 stages per use so 3-4 bone meals mature a plant from st"
        "age 0 (10 plants locked uses 3..4, advance in {2,3} clamped at 7, id preserved, mature -> no-op "
        "no-consume), sapling 45% per use instant tree (trunk base Log, 6 saplings within 10-24 rolls, fi"
        "rst-try and retry both observed) with support+clearance guards (cobble base / blocked trunk neve"
        "r grow yet still consumed), berry bush +1 stage to cap then no-op, non-targets false (survival c"
        "onsume + swing + stage texture swap = playercontroller/QML, manual check)" }, [&]() {
        bool ok = true;
        int bx = -1, bz = -1;
        for (int zz = 2; zz + 4 < 96 && bx < 0; zz += 3) {
            for (int xx = 2; xx + 19 < 96; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 19 && clear; ++dx)
                    for (int dz = 0; dz <= 4 && clear; ++dz)
                        for (int dy = 40; dy <= 47 && clear; ++dy)
                            if (w.blockAt(xx + dx, dy, zz + dz) != BR::Air) clear = false;
                if (clear) { bx = xx; bz = zz; }
            }
        }
        if (bx < 0) {
            qInfo().noquote() << "  [t791 diag] no clear 20x5x8 region at y=40..47";
            ok = false;
        } else {
            const int gY = 40; // 地台层（耕地 / 泥土 / 圆石）
            const int pY = 41; // 植物层（树苗 41 恰容 4 主干 + 2 树冠余量 ≤47）
            // ① 树苗守卫负例（先于成树正例——正例树冠会在 y43+ 写叶混入守卫区）：石支撑（非草/泥土）+
            //    主干列 y45 阻塞 → 30 骨粉仍不成树（返回 true = 使用即耗，树苗保留；MC 1.0 同）。
            w.setBlock(bx, gY, bz + 3, BR::Cobble, 0);
            w.setBlock(bx, pY, bz + 3, BR::Sapling, 0);
            w.setBlock(bx + 2, gY, bz + 3, BR::Dirt, 0);
            w.setBlock(bx + 2, pY, bz + 3, BR::Sapling, 0);
            w.setBlock(bx + 2, 45, bz + 3, BR::Dirt, 0);
            bool guardConsumed = true;
            for (int i = 0; i < 30; ++i) {
                guardConsumed = w.applyBonemeal(bx, pY, bz + 3) && guardConsumed;       // 石支撑
                guardConsumed = w.applyBonemeal(bx + 2, pY, bz + 3) && guardConsumed;   // 主干阻塞
            }
            if (!guardConsumed || w.blockAt(bx, pY, bz + 3) != BR::Sapling
                    || w.blockAt(bx + 2, pY, bz + 3) != BR::Sapling) {
                qInfo().noquote() << "  [t791 diag] guard saplings: consumed" << guardConsumed
                                  << "stillA" << (w.blockAt(bx, pY, bz + 3) == BR::Sapling)
                                  << "stillB" << (w.blockAt(bx + 2, pY, bz + 3) == BR::Sapling);
                ok = false;
            }
            // ② 树苗成树正例：6 株泥土支撑 + 净空 → 逐株骨粉到成树（≤60 次防死循环）；树基 Log 顶替树苗位。
            //    锁分布：总投掷数 10..24（6 株 / 45% → 期望 ~13.3）且首掷即中与 ≥2 掷两态都出现（45% 非 0/100）。
            int totalRolls = 0, firstTry = 0, slowGrow = 0;
            for (int s = 0; s < 6; ++s) {
                const int sx = bx + s * 3, sz = bz + 2;
                w.setBlock(sx, gY, sz, BR::Dirt, 0);
                w.setBlock(sx, pY, sz, BR::Sapling, 0);
                int n = 0;
                bool consumed = true;
                while (w.blockAt(sx, pY, sz) == BR::Sapling && n < 60) {
                    consumed = w.applyBonemeal(sx, pY, sz) && consumed; // 使用即耗（判定落空也 true）
                    ++n; ++totalRolls;
                }
                if (!consumed || w.blockAt(sx, pY, sz) != BR::Log) {
                    qInfo().noquote() << "  [t791 diag] sapling" << s << "rolls" << n
                                      << "base" << w.blockAt(sx, pY, sz);
                    ok = false;
                }
                if (n == 1) ++firstTry;
                if (n >= 2) ++slowGrow;
            }
            if (totalRolls < 10 || totalRolls > 24 || firstTry < 1 || slowGrow < 1) {
                qInfo().noquote() << "  [t791 diag] sapling distribution: rolls" << totalRolls
                                  << "firstTry" << firstTry << "slow" << slowGrow;
                ok = false;
            }
            // ③ 作物 +2..3 阶段 / 3-4 骨粉催熟（8 小麦 + 胡萝卜 + 马铃薯，耕地支撑同生产种植路径）：
            //    每骨粉推进 ∈ {2,3}（钳顶 7 时 delta 可 <2 但 after 必为 7）；uses ∈ [3,4]；id 不漂移；
            //    分布锁：推进 2 与 3 两值都出现 + uses 最小 3 / 最大 4（带内两端都达）。
            int usesMin = 99, usesMax = 0, adv2 = 0, adv3 = 0;
            const quint8 cropIds[10] = { BR::WheatCrop, BR::WheatCrop, BR::WheatCrop, BR::WheatCrop,
                                         BR::WheatCrop, BR::WheatCrop, BR::WheatCrop, BR::WheatCrop,
                                         BR::CarrotCrop, BR::PotatoCrop };
            for (int c = 0; c < 10; ++c) {
                const int cx = bx + c;
                w.setBlock(cx, gY, bz, BR::Farmland, 0);
                w.setBlock(cx, pY, bz, cropIds[c], 0);
                int uses = 0;
                bool consumed = true;
                while (w.stateAt(cx, pY, bz) < BR::WheatCropStageMax && uses < 8) {
                    const int before = w.stateAt(cx, pY, bz);
                    consumed = w.applyBonemeal(cx, pY, bz) && consumed;
                    const int after = w.stateAt(cx, pY, bz);
                    ++uses;
                    const int d = after - before;
                    if (d == 2) ++adv2;
                    else if (d == 3) ++adv3;
                    else if (after != int(BR::WheatCropStageMax)) { // 钳顶例外（after==7 合法）
                        qInfo().noquote() << "  [t791 diag] crop" << c << "advance" << d << "->" << after;
                        ok = false;
                    }
                }
                if (!consumed || w.stateAt(cx, pY, bz) != BR::WheatCropStageMax
                        || w.blockAt(cx, pY, bz) != cropIds[c] || uses < 3 || uses > 4) {
                    qInfo().noquote() << "  [t791 diag] crop" << c << "uses" << uses
                                      << "state" << w.stateAt(cx, pY, bz);
                    ok = false;
                }
                usesMin = std::min(usesMin, uses);
                usesMax = std::max(usesMax, uses);
            }
            if (adv2 < 1 || adv3 < 1 || usesMin != 3 || usesMax != 4) {
                qInfo().noquote() << "  [t791 diag] crop distribution: adv2" << adv2 << "adv3" << adv3
                                  << "uses" << usesMin << "-" << usesMax;
                ok = false;
            }
            // ③b 成熟作物负例：state==7 → 返 false（无效应不消耗）+ 阶段保持 7。
            if (w.applyBonemeal(bx, pY, bz) || w.stateAt(bx, pY, bz) != BR::WheatCropStageMax) {
                qInfo().noquote() << "  [t791 diag] mature crop bonemeal not a no-op";
                ok = false;
            }
            // ④ 浆果丛 +1 阶段：0→1→2 后封顶返 false（MC sweet berry bush bone meal 单阶段推进）。
            w.setBlock(bx + 12, pY, bz + 1, BR::SweetBerryBush, 0);
            if (!w.applyBonemeal(bx + 12, pY, bz + 1) || w.stateAt(bx + 12, pY, bz + 1) != 1
                    || !w.applyBonemeal(bx + 12, pY, bz + 1) || w.stateAt(bx + 12, pY, bz + 1) != 2
                    || w.applyBonemeal(bx + 12, pY, bz + 1)
                    || w.stateAt(bx + 12, pY, bz + 1) != 2) {
                qInfo().noquote() << "  [t791 diag] berry bush stages wrong:"
                                  << w.stateAt(bx + 12, pY, bz + 1);
                ok = false;
            }
            // ⑤ 非目标负例：泥土 / 空气 → 返 false（机制等价 MC 骨粉对非生长目标无效应）。
            w.setBlock(bx + 11, pY, bz + 1, BR::Dirt, 0);
            if (w.applyBonemeal(bx + 11, pY, bz + 1) || w.applyBonemeal(bx + 13, pY, bz + 1)) {
                qInfo().noquote() << "  [t791 diag] bonemeal applied to dirt/air";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t791 bonemeal balance: crops advance 2-3 stages per use so 3-4 bone meals "
                             "mature a plant from stage 0 (10 plants locked uses 3..4, advance in {2,3} "
                             "clamped at 7, id preserved, mature -> no-op no-consume), sapling 45% per use "
                             "instant tree (trunk base Log, 6 saplings within 10-24 rolls, first-try and "
                             "retry both observed) with support+clearance guards (cobble base / blocked "
                             "trunk never grow yet still consumed), berry bush +1 stage to cap then no-op, "
                             "non-targets false (survival consume + swing + stage texture swap = "
                             "playercontroller/QML, manual check)";
    });

    // ── t806 余烬门尺寸泛化探针（World 层直调：点燃检测 / 连通域熄灭 t806 已自 PlayerController 下沉 World
    //    单一权威——同末地门三件套模式，矩阵可直编；粒子改门色紫是 QML blockColor 表（呈现层单一权威）→
    //    需人工目视，此处不测）：
    //    ① 2×3 最小门（X 平面 / 带角）通过 + 门格恰 6 + state=0；② 4×5 门（Z 平面；t806 时代上限，t848
    //    放宽后仅常规尺寸——更大门全覆盖在 t848 探针）通过 + 门格恰 20（点燃填满整个开口）+ state=1；
    //    ③ 超限拒（t848 尺寸表）：内腔 22 宽 / 22 高均拒且零门格（t806 时代此处断言 5 宽 / 6 高拒——
    //    t848 放宽后那些已是合法门，本 rig 世界的 32 高也放不下 22 高门 → 升 64）；④ 缺角通过（MC 1.0 角块
    //    可选）+ 破角不碎门（角块不承结构且与门格对角不邻）；⑤ 缺承重框格拒：底梁 / 顶梁 / 边柱各破一格
    //    均拒；⑥ 非矩形（腔内异物）拒；⑦ 低于最小（1 宽 / 2 高）拒；⑧ 点燃位无关性：4×5 开口四角 + 中部
    //    任一格点燃同成门；⑨ 破框碎门：破任一承重框格（镜像 finishMiningAt 的 setBlock(Air)+
    //    breakNetherPortalsAround 序列）→ 整门 20 格全熄；直挖门格（setBlock(Air)+removeNetherPortalAt）
    //    同样整门熄（连通域尺寸无关）。
    runLegMulti({ "t806 portal frame generalization: ignite fills whole 2x3..4x5 inner opening (2x3 min X-plane 6 c"
        "ells state=0 / 4x5 Z-plane 20 cells state=1, ignition position-independent at 5 sample cells), o"
        "versized 22w/22h (t848-era cap; t806-era 5w/6h now legal) + below-min 1w/2h + non-rectangular + "
        "missing beam/pillar rejected with zero cells, corners optional (cornerless 3x4 lights + corner b"
        "reak keeps door), frame-member break collapses whole door via connected-domain clear (particle c"
        "olor = QML blockColor, manual check)" }, [&]() {
        World w806;
        w806.setWidth(48);
        w806.setDepth(48);
        w806.setHeight(64); // t848：③ 超限样本升 22 高（清场盒 y 至 pY+25）→ 原 32 高放不下
        w806.setSeed(13);
        bool ok = true;
        const int pY = 20; // 门框基线层（开口 y=pY..pY+h-1；不轻信「y 以上必空」——buildFrame 先显式清场兜底）
        // 建门框（泛化）：开口左下角 (x0,pY,z0) 沿 u=(ux,uz) 展开 w 列 × h 层全 Air；底梁 / 顶梁（开口正下 /
        //   正上各 w 格，不含角）与左右边柱（两翼各 h 格，不含角）全黑曜石；corners=true 补四角。清场盒 =
        //   框外沿 ±3 × 门法向 ±2（含 y ±(h+3)），防地形 / 上一场景残留干扰。
        const auto buildFrame = [&](int x0, int z0, int ux, int uz, int w, int h, bool corners) {
            const int vx = uz, vz = ux; // 门法线向（清深 ±2）
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        w806.setBlock(x0 + c * ux + d * vx, pY + r, z0 + c * uz + d * vz, BR::Air, 0);
            for (int c = 0; c < w; ++c) {
                w806.setBlock(x0 + c * ux, pY - 1, z0 + c * uz, BR::Obsidian, 0);
                w806.setBlock(x0 + c * ux, pY + h, z0 + c * uz, BR::Obsidian, 0);
            }
            for (int r = 0; r < h; ++r) {
                w806.setBlock(x0 - ux, pY + r, z0 - uz, BR::Obsidian, 0);
                w806.setBlock(x0 + w * ux, pY + r, z0 + w * uz, BR::Obsidian, 0);
            }
            if (corners) {
                const int cs[2] = {-1, w};
                for (const int ci : cs)
                    for (const int ry : {-1, h})
                        w806.setBlock(x0 + ci * ux, pY + ry, z0 + ci * uz, BR::Obsidian, 0);
            }
        };
        // 局部门格计数：只数本 rig 清场盒内的 NetherPortal（各场景共用一世界，隔壁 rig 的残留门不串数）。
        const auto cellsInBox = [&](int x0, int z0, int ux, int uz, int w, int h) -> int {
            int n = 0;
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (w806.blockAt(x0 + c * ux + d * uz, pY + r, z0 + c * uz + d * ux)
                            == BR::NetherPortal)
                            ++n;
            return n;
        };

        // ① 2×3 最小门（X 平面 / 带角）：开口中格点燃 → true + 本 rig 门格恰 6 + state=0（X 平面）。
        {
            buildFrame(8, 8, 1, 0, 2, 3, true);
            const bool lit = w806.tryIgniteNetherPortal(9, pY + 1, 8);
            const int n = cellsInBox(8, 8, 1, 0, 2, 3);
            const int st = int(w806.stateAt(8, pY, 8));
            if (!lit || n != 6 || st != 0) {
                qInfo().noquote() << "  [t806 diag] 2x3 min gate:" << lit << "cells" << n << "state" << st;
                ok = false;
            }
        }
        // ⑧ 点燃位无关性（兼 ② 4×5 最大门 Z 平面 + ⑨ 直挖门格熄灭链）：4×5 开口的四角 + 中部共 5 个点燃位
        //    逐轮（每轮重搭框）点燃 → 均成门恰 20 格 + state=1；随后直挖一门格 + removeNetherPortalAt 连通域
        //    熄灭（镜像 finishMiningAt 门格分支序列）→ 归零。
        {
            const int wx = 30, wz = 8;
            const int cells[5][2] = {{0, 0}, {3, 0}, {0, 4}, {3, 4}, {1, 2}}; // (列, 行) 开口内点燃位
            for (int i = 0; i < 5; ++i) {
                buildFrame(wx, wz, 0, 1, 4, 5, true);
                const bool lit = w806.tryIgniteNetherPortal(wx, pY + cells[i][1], wz + cells[i][0]);
                const int n = cellsInBox(wx, wz, 0, 1, 4, 5);
                const int st = int(w806.stateAt(wx, pY, wz));
                if (!lit || n != 20 || st != 1) {
                    qInfo().noquote() << "  [t806 diag] 4x5 ignite pos" << i << ":" << lit
                                      << "cells" << n << "state" << st;
                    ok = false;
                }
                w806.setBlock(wx, pY, wz, BR::Air, 0);        // 直挖门格（finishMiningAt 同款先清格）
                w806.removeNetherPortalAt(wx, pY, wz, 1);     // 连通域熄灭余格
                if (cellsInBox(wx, wz, 0, 1, 4, 5) != 0) {
                    qInfo().noquote() << "  [t806 diag] direct-mine teardown at pos" << i
                                      << "left" << cellsInBox(wx, wz, 0, 1, 4, 5);
                    ok = false;
                }
            }
        }
        // ③ 超限拒（t848 尺寸表）：内腔 22 宽 / 22 高（超 21×21 上限，框外沿 23×23 封顶）均拒且零门格。
        //    t806 时代断言 5 宽 / 6 高拒——恰是用户「再大点不着」的设计根源（用户期望 MC 语义的更大门），
        //    t848 放宽后移交本处只验新上限。
        {
            buildFrame(8, 14, 1, 0, 22, 3, true); // 22 宽（X 平面）
            bool bad = w806.tryIgniteNetherPortal(19, pY + 1, 14) || cellsInBox(8, 14, 1, 0, 22, 3) != 0;
            buildFrame(8, 22, 1, 0, 2, 22, true); // 22 高
            bad = bad || w806.tryIgniteNetherPortal(9, pY + 1, 22) || cellsInBox(8, 22, 1, 0, 2, 22) != 0;
            if (bad) {
                qInfo().noquote() << "  [t806 diag] oversize 22w/22h not rejected";
                ok = false;
            }
        }
        // ④ 缺角通过（MC 1.0 角块可选）：3×4 无角门 → 成门恰 12 格。
        {
            buildFrame(20, 8, 1, 0, 3, 4, false);
            const bool lit = w806.tryIgniteNetherPortal(21, pY + 1, 8);
            const int n = cellsInBox(20, 8, 1, 0, 3, 4);
            if (!lit || n != 12) {
                qInfo().noquote() << "  [t806 diag] cornerless 3x4:" << lit << "cells" << n;
                ok = false;
            }
        }
        // ⑤ 缺承重框格拒：完整 3×4 带角框分别拆 底梁中格 / 顶梁中格 / 左边柱中格 → 均拒且零门格。
        {
            const int holes[3][2] = {{1, -1}, {1, 4}, {-1, 1}}; // (开口列偏移, 行偏移) 的框格位
            for (int i = 0; i < 3; ++i) {
                buildFrame(20, 8, 1, 0, 3, 4, true);
                w806.setBlock(20 + holes[i][0], pY + holes[i][1], 8, BR::Air, 0);
                const bool lit = w806.tryIgniteNetherPortal(21, pY + 1, 8);
                if (lit || cellsInBox(20, 8, 1, 0, 3, 4) != 0) {
                    qInfo().noquote() << "  [t806 diag] missing frame member" << i << "still lit";
                    ok = false;
                }
            }
        }
        // ⑥ 非矩形（腔内异物）拒：4×3 框开口内塞一块石 → 拒且零门格（矩形校验生效）。
        {
            buildFrame(20, 14, 1, 0, 4, 3, true);
            w806.setBlock(22, pY + 2, 14, BR::Stone, 0);
            if (w806.tryIgniteNetherPortal(21, pY + 1, 14) || cellsInBox(20, 14, 1, 0, 4, 3) != 0) {
                qInfo().noquote() << "  [t806 diag] non-rectangular opening not rejected";
                ok = false;
            }
        }
        // ⑦ 低于最小拒：1×3（单列开口）/ 2×2（矮开口）→ 均拒且零门格。
        {
            buildFrame(30, 20, 0, 1, 1, 3, true); // 1 宽（Z 平面）
            bool bad = w806.tryIgniteNetherPortal(30, pY + 1, 21) || cellsInBox(30, 20, 0, 1, 1, 3) != 0;
            buildFrame(30, 28, 0, 1, 2, 2, true); // 2 高
            bad = bad || w806.tryIgniteNetherPortal(30, pY + 1, 29) || cellsInBox(30, 28, 0, 1, 2, 2) != 0;
            if (bad) {
                qInfo().noquote() << "  [t806 diag] below-min 1w/2h not rejected";
                ok = false;
            }
        }
        // ⑨ 破框碎门 + 破角不碎门（镜像 finishMiningAt 框格破坏链：setBlock(Air) + breakNetherPortalsAround）：
        //    4×5 门破 底梁中 / 顶梁中 / 左边柱中 / 右边柱中 任一承重格 → 整门 20 格全熄；破左下角块
        //    （不承结构且与门格对角不邻）→ 门健在 20 格。
        {
            const int wx = 8, wz = 8; // Z 平面 4×5（buildFrame 自带清场抹掉 ① 的残留门）
            const int breaks[4][2] = {{0, -1}, {1, 5}, {-1, 2}, {4, 2}}; // (列偏移, 行偏移) 的框格位
            for (int i = 0; i < 4; ++i) {
                buildFrame(wx, wz, 0, 1, 4, 5, true);
                if (!w806.tryIgniteNetherPortal(wx, pY + 1, wz + 1) || cellsInBox(wx, wz, 0, 1, 4, 5) != 20) {
                    qInfo().noquote() << "  [t806 diag] frame-break rig" << i << "ignite failed";
                    ok = false;
                    continue;
                }
                const int bx = wx, by = pY + breaks[i][1], bz = wz + breaks[i][0];
                w806.setBlock(bx, by, bz, BR::Air, 0);
                w806.breakNetherPortalsAround(bx, by, bz);
                if (cellsInBox(wx, wz, 0, 1, 4, 5) != 0) {
                    qInfo().noquote() << "  [t806 diag] frame member" << i << "broken, door survived";
                    ok = false;
                }
            }
            buildFrame(wx, wz, 0, 1, 4, 5, true);
            w806.tryIgniteNetherPortal(wx, pY + 1, wz + 1);
            w806.setBlock(wx, pY - 1, wz - 1, BR::Air, 0); // 左下角块
            w806.breakNetherPortalsAround(wx, pY - 1, wz - 1);
            if (cellsInBox(wx, wz, 0, 1, 4, 5) != 20) {
                qInfo().noquote() << "  [t806 diag] corner break collapsed door:"
                                  << cellsInBox(wx, wz, 0, 1, 4, 5);
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t806 portal frame generalization: ignite fills whole 2x3..4x5 inner opening "
                             "(2x3 min X-plane 6 cells state=0 / 4x5 Z-plane 20 cells state=1, ignition "
                             "position-independent at 5 sample cells), oversized 22w/22h (t848-era cap; "
                             "t806-era 5w/6h now legal) + below-min 1w/2h + "
                             "non-rectangular + missing beam/pillar rejected with zero cells, corners optional "
                             "(cornerless 3x4 lights + corner break keeps door), frame-member break collapses "
                             "whole door via connected-domain clear (particle color = QML blockColor, manual "
                             "check)";
    });

    // ── Review 2026-08-23 #1 slim 皮肤布局探测回归探针 ──
    // 背景：复审 #8 的 slim 修复整体无效——旧探测区 u[52,54) 落在 slim 臂背面 [51,54) 内（PIL 实测
    //   demo 包 alex/steve 该区同为 24 个不透明像素）→ 对真实 slim 皮肤恒判 classic；且 kPiecesSlim
    //   盒区数值错（臂深 3 应为 4、腿不应缩 3px——数值锁在 playerskinbox.cpp static_assert，编译期拦）。
    //   修正后探测区 u[54,56)：classic = 臂背面右半必不透明、slim = 布局外空白必透明（alex 0 / steve 24，
    //   区分度完美）。本探针锁两层：
    //   ① 合成布局判定（密闭，任意机器可跑）：按 MC box-UV 条带公式画「规范 classic/slim 右臂条带」
    //     ——条带右缘 = u0 + 2d + 2w（classic 40+8+8=56 / slim 40+8+6=54），条带列 [40, 右缘) 全不透明、
    //     其余全透明 → classic 必判 classic、slim 必判 slim。若有人把探测区改回条带内（如 [52,54)），
    //     slim 合成图的 52/53 列不透明 → 误判 classic → FAIL；若改过头越出 classic 条带（如 [56,58)），
    //     classic 也判 slim → FAIL。HD 2×（128×128，sc=2）与退化小图（32×16 → 保守 classic）同锁。
    //   ② demo 包实测（有 pack 才跑，缺则记 note 跳过）：alex.png → slim、steve.png → classic——
    //     真实皮肤布局假设的防线（①合成图按公式画，公式理解错则②用真图拦）。
    runLegMulti({ "review#1 slim-skin probe region: synthetic MC-layout arm strips classify classic(end u=56)/slim("
        "end u=54) at base 64x32 + HD 2x, degenerate 32x16 + 64x16 stub (probe rows off-canvas) conservat"
        "ive-classic, demo pack alex->slim / steve->classic (real PIL-verified layouts)" }, [&]() {
        bool ok = true;
        // ① 合成条带（64×32 base 与 128×64 HD 2× 两档）。
        const auto makeArmStrip = [](int w, int h, int stripEndPx) {
            QImage img(w, h, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xd0, 0xa0, 0x70));
            const qreal sx = qreal(w) / 64.0, sy = qreal(h) / 32.0;
            // 右臂条带（base 像素 u∈[40,stripEnd) × v∈[16,32) 全不透明——侧面/背面带必paint，
            //   含探测行 v∈[20,32)）。
            p.drawRect(QRectF(40 * sx, 16 * sy, (stripEndPx - 40) * sx, 16 * sy));
            return img;
        };
        const int classicEnd = 40 + 2 * 4 + 2 * 4; // = 56（Renderer kPiecesClassic[2] 同公式）
        const int slimEnd = 40 + 2 * 4 + 2 * 3;    // = 54（Renderer kPiecesSlim[2] 同公式）
        const QImage synClassic = makeArmStrip(64, 32, classicEnd);
        const QImage synSlim = makeArmStrip(64, 32, slimEnd);
        if (probeSlimSkinLayout(synClassic)) {
            qInfo().noquote() << "  [#1 diag] synthetic classic strip (end u=56) misjudged slim";
            ok = false;
        }
        if (!probeSlimSkinLayout(synSlim)) {
            qInfo().noquote() << "  [#1 diag] synthetic slim strip (end u=54) misjudged classic";
            ok = false;
        }
        // HD 2× 同布局（128×64；sc=2 坐标缩放路径）。
        const QImage synClassicHd = makeArmStrip(128, 64, classicEnd);
        const QImage synSlimHd = makeArmStrip(128, 64, slimEnd);
        if (probeSlimSkinLayout(synClassicHd) || !probeSlimSkinLayout(synSlimHd)) {
            qInfo().noquote() << "  [#1 diag] HD 2x (128x64) classification wrong";
            ok = false;
        }
        // 退化小图（w<64）→ 保守 classic。
        QImage tiny(32, 16, QImage::Format_ARGB32);
        tiny.fill(Qt::transparent);
        if (probeSlimSkinLayout(tiny)) {
            qInfo().noquote() << "  [#1 diag] degenerate 32x16 not conservative-classic";
            ok = false;
        }
        // Review 2026-08-24 低危③：高度下界守卫——64×16 残图（宽达标但探测行区间 v[20,32) 整段在画布外）
        //   旧实现循环零次执行 → 空真判 slim（与保守方向相反）；守卫后必保守 classic。
        QImage stubR24(64, 16, QImage::Format_ARGB32);
        stubR24.fill(Qt::transparent);
        if (probeSlimSkinLayout(stubR24)) {
            qInfo().noquote() << "  [#1 diag] 64x16 stub vacuously judged slim (height floor guard missing)";
            ok = false;
        }
        // ② demo 包真实皮肤：settings.json resourcePack 指向的包 → 该包 entity/alex.png + steve.png；
        //   缺配置则试工程内 demo 包相对路径（exe 在 build/ → ../docs）。两候选都 miss → 记 note 跳过
        //   （①仍守布局语义；不在无包机器上假 FAIL）。
        QString packRoot;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString settingsCandidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
                QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                        .absoluteFilePath(QStringLiteral("settings.json")),
            };
            for (const QString &c : settingsCandidates) {
                QFile f(c);
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
                const QString p = obj.value(QLatin1String("resourcePack")).toString();
                if (!p.isEmpty() && QDir(p).exists()) {
                    packRoot = p;
                    break;
                }
            }
        }
        QString alexPath, stevePath;
        const auto entitySkin = [&packRoot](const char *name) {
            return packRoot.isEmpty()
                    ? QString()
                    : QDir(packRoot).absoluteFilePath(
                            QStringLiteral("assets/minecraft/textures/entity/") + QLatin1String(name));
        };
        alexPath = entitySkin("alex.png");
        stevePath = entitySkin("steve.png");
        if (alexPath.isEmpty() || !QFile::exists(alexPath) || !QFile::exists(stevePath)) {
            const QString fallback = QDir(QStringLiteral("..")).absoluteFilePath(
                    QStringLiteral("docs/Default HD 128x Demo 1.8.2.2"));
            const QString a = QDir(fallback).absoluteFilePath(
                    QStringLiteral("assets/minecraft/textures/entity/alex.png"));
            const QString s = QDir(fallback).absoluteFilePath(
                    QStringLiteral("assets/minecraft/textures/entity/steve.png"));
            if (QFile::exists(a) && QFile::exists(s)) {
                alexPath = a;
                stevePath = s;
            }
        }
        bool realChecked = false;
        if (QFile::exists(alexPath) && QFile::exists(stevePath)) {
            const QImage alex(alexPath), steve(stevePath);
            if (alex.isNull() || steve.isNull()) {
                qInfo().noquote() << "  [#1 diag] demo pack skin PNG decode failed";
                ok = false;
            } else {
                realChecked = true;
                if (!probeSlimSkinLayout(alex)) {
                    qInfo().noquote() << "  [#1 diag] demo alex.png misjudged classic (slim regression)";
                    ok = false;
                }
                if (probeSlimSkinLayout(steve)) {
                    qInfo().noquote() << "  [#1 diag] demo steve.png misjudged slim (classic regression)";
                    ok = false;
                }
            }
        }
        if (!realChecked)
            qInfo().noquote() << "  [#1 note] demo pack skins not found - real-skin assertions skipped "
                                 "(synthetic layout assertions still ran)";
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review#1 slim-skin probe region: synthetic MC-layout arm strips classify "
                             "classic(end u=56)/slim(end u=54) at base 64x32 + HD 2x, degenerate 32x16 + "
                             "64x16 stub (probe rows off-canvas) conservative-classic"
                          << (realChecked
                                  ? ", demo pack alex->slim / steve->classic (real PIL-verified layouts)"
                                  : "");
    });

    // ── P21 复审 #2（2026-08-23）低顶净空坡道探针（MinecartManager 直编，同 P12b/P18 模式）──
    //   Review #2：scanRailColumn「实心即断」× 坡道车位居上 —— 坡段 rise>0.55 时 floor(pos.y) = 轨Y+1
    //   恰是贴坡天花板实心格 → 严格断扫返 -1 → 坡上死车 / 俯仰清零 / 采样失联三症状同根因（af9ec8e 的
    //   断扫在平轨天花板不触发 —— P18 平轨 pos.y=R+0.45 首扫格即轨；本探针补坡道变体）。修后骑乘族走
    //   宽容版（实心正下是轨 + 骑乘高一致 → 放行）。
    //   **t944 重定scope（2026-08-29 用户第五轮口径覆盖 review#2 穿越承诺，t931/t941 最新口径优先先例）**：
    //   坡格正上方的实心天花板 = 用户「上坡处上方放方块」的阻挡格 —— t944 起轨态推进对上坡向位移做车体
    //   AABB 碰撞探测，贴顶穿越自此**被阻挡**（不再是通路）。本探针改双相钉：
    //   相1（天花板在场）：爬坡车体格撞实心 → 被挡停在坡下侧 + 轨态保持（Y 恒钉轨面，yOk 全程）+
    //       持续 W 稳定不穿墙（速度清零、钳回位渐近稳定）——宽容列扫的「坡上车位居上不失联」承重面由
    //       停驻态继续钉住（被挡车停在坡上，floor(pos.y) 跨上层时列扫仍须解析到本轨）；
    //   相2（移除天花板）：原 review#2 全套断言（Y 钉定 / 俯仰连续且 ≤45 / 坡中段 ~+45 / 顶死端格心
    //       停驻 + 停稳守卫）在清顶后半程钉住 —— 兼作 t944「移除方块后车恢复通行」行为腿。
    //   断言：
    //   (a) 相1 阻挡 + 相2 恢复通行（上）；
    //   (b) af9ec8e 隔板回归防线：地面车（kCartGroundH=0.3875）站实心地板、地板下 1 格平轨 —— mount +
    //       持续 W + 玩家推全链后钉死不动（宽容版一致性校验拒：差 1.9375 >> kRideScanTol 0.5）。
    runLegMulti({ "review#2 low-headroom ramp re-scoped by t944 (ceiling flush above slope rail now BLOCKS the clim"
        "b - user's latest word): phase 1 the cart is stopped on the lower slope flank, stays rail-pinned"
        " (Y on surface) and holds under continued W (no wall-pass); phase 2 clearing the block resumes t"
        "he traverse to the top dead-end with the original review#2 assertions (Y pinned, pitch continuou"
        "s & clamped, ~+45 mid-slope, settle guard)",
               "review#2 af9ec8e floor-slab guard kept: ground cart above rail-under-floor stays dead (lenient s"
        "can consistency rejects, gap 1.9375 >> tol)" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例——nextSlot() 4×31 网格已耗尽）。需 7×3×6（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 6 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#2 low-headroom ramp: no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 镜像值（P11/P12b 同款）
            // 轨：x0 低平 + x0+1 坡（东邻高一格）+ x0+2..x0+3 高平死端；天花板：x0/x0+1 上 Y+1（低顶）、
            //   x0+2/x0+3 上 Y+2 —— 坡段 1 格净空（紧凑螺旋下层坡段等效布局）。
            w.setBlock(x0,     kRigY,     z0, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY,     z0, BR::Rail, 0);
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);
            w.setBlock(x0 + 3, kRigY + 1, z0, BR::Rail, 0);
            w.setBlock(x0,     kRigY + 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 2, kRigY + 2, z0, BR::Stone, 0);
            w.setBlock(x0 + 3, kRigY + 2, z0, BR::Stone, 0);
            const auto wantSurf = [&](float x) {
                if (x < float(x0 + 1)) return float(kRigY);
                if (x > float(x0 + 2)) return float(kRigY + 1);
                return float(kRigY) + (x - float(x0 + 1));
            };
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            // ── 相1（t944 阻挡：天花板在场 = 用户「上坡处上方放方块」）：爬坡 → 车体格撞实心 → 被挡停在
            //    坡下侧（接触面 rise≈0.1）；轨态保持（Y 恒钉轨面，yOk 贯穿）+ 持续 W 稳定不穿墙。
            bool yOk = true;
            QVector3D cp;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOk = false; break; }
            }
            const QVector3D stuck = carts.posAt(0);
            QVector3D stuck2 = stuck;
            for (int t = 0; t < 20; ++t) { // 持续 W 阻挡稳定（速度清零 + 钳回位渐近稳定，不穿墙不漂移）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                stuck2 = carts.posAt(0);
            }
            const bool blockedOk = yOk
                && stuck.x() > float(x0 + 1) && stuck.x() < float(x0 + 1) + 0.45f // 坡下侧（未过坡中点）
                && std::fabs(stuck.y() - (wantSurf(stuck.x()) + rideH)) < 0.02f   // 轨态保持（Y 钉坡面）
                && (stuck2 - stuck).length() < 0.05f                              // 持续 W 不穿墙
                && std::fabs(carts.pitchAt(0)) <= 45.5f;                          // 姿态正常（未出轨未翻）
            // ── 相2（移除方块 = t944「移除方块后车恢复通行」）：原 review#2 全套断言在清顶后半程钉住。
            w.setBlock(x0,     kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Air, 0);
            bool pitchCont = true, pitchClamped = true;
            float prevPitch = carts.pitchAt(0), slopeSum = 0.0f;
            int slopeN = 0;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOk = false; break; }
                const float p = carts.pitchAt(0);
                if (std::fabs(p - prevPitch) > 46.0f) pitchCont = false;
                if (std::fabs(p) > 45.5f) pitchClamped = false;
                if (cp.x() > float(x0 + 1) + 0.3f && cp.x() < float(x0 + 1) + 0.7f) { slopeSum += p; ++slopeN; }
                prevPitch = p;
            }
            const QVector3D fin = carts.posAt(0);
            ok = ok && blockedOk && yOk && pitchCont && pitchClamped
                && slopeN >= 3 && std::fabs(slopeSum / float(slopeN) - 45.0f) < 2.5f
                && std::fabs(fin.x() - float(x0 + 3) - 0.5f) < 0.01f
                && std::fabs(fin.y() - float(kRigY + 1) - rideH) < 0.02f
                && std::fabs(carts.pitchAt(0)) < 0.5f;
            if (ok) { // 停稳守卫（顶死端格心）
                for (int t = 0; t < 20 && ok; ++t) {
                    carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    if ((cp - fin).length() > 1e-4f) ok = false;
                }
            }
            if (!ok)
                qInfo().noquote() << "  low-headroom uphill: stuck" << stuck << "stuckDrift"
                                  << (stuck2 - stuck).length() << "blockedOk" << blockedOk
                                  << "final" << fin << "pitch" << carts.pitchAt(0)
                                  << "yOk" << yOk << "pitchCont" << pitchCont << "clamp" << pitchClamped
                                  << "slopeN" << slopeN
                                  << "slopeMean" << (slopeN ? slopeSum / float(slopeN) : 0.0f);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#2 low-headroom ramp re-scoped by t944 (ceiling flush above slope"
                                 " rail now BLOCKS the climb - user's latest word): phase 1 the cart is"
                                 " stopped on the lower slope flank, stays rail-pinned (Y on surface) and"
                                 " holds under continued W (no wall-pass); phase 2 clearing the block"
                                 " resumes the traverse to the top dead-end with the original review#2"
                                 " assertions (Y pinned, pitch continuous & clamped, ~+45 mid-slope,"
                                 " settle guard)";
            // (c) 隔板防线：清坡轨布局 → 地板（Y 实心）+ 地板下 1 格平轨（Y-1）+ 地面车（Y+1 空格）。
            carts.clearAll();
            for (int dx = 0; dx <= 3; ++dx) { // 清轨 / 天花板 / 高段（含越层残留）
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY + 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY + 2, z0, BR::Air, 0);
            }
            w.setBlock(x0, kRigY, z0, BR::Stone, 0);    // 地板（车格正下）
            w.setBlock(x0, kRigY - 1, z0, BR::Rail, 0); // 地板下平轨（隔板场景：实心在车与轨之间）
            carts.spawnCart(x0, kRigY + 1, z0, &w);     // 地面静止模式（车底贴 cell 底 kCartGroundH）
            const QVector3D gp0 = carts.posAt(0);
            bool guardOk = std::fabs(gp0.y() - (float(kRigY + 1) + 0.3875f)) < 1e-3f; // 地面车基准高
            guardOk = guardOk && carts.tryMount(QVector3D(gp0.x(), gp0.y() + 1.5f, gp0.z()),
                                                QVector3D(0, -1, 0), 4.0f);
            QVector3D gcp;
            for (int t = 0; t < 30; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, gcp); // 持续 W（宽容版若误放行 → 钉到下方轨）
                carts.pushEmptyCart(&w, gcp, 1.0f, 0.0f);         // 玩家推（pickTrackStep 同一宽容扫描）
                carts.tickPushedCarts(0.016, &w);
            }
            guardOk = guardOk && (gcp - gp0).length() < 1e-4f; // 全链后钉死不动（离轨静止守卫接管）
            if (!guardOk)
                qInfo().noquote() << "  floor-slab guard: cart moved/teleported, start" << gp0
                                  << "after" << gcp;
            if (!guardOk) ++totalFail;
            qInfo().noquote() << (guardOk ? "PASS" : "FAIL")
                              << "| review#2 af9ec8e floor-slab guard kept: ground cart above rail-under-"
                                 "floor stays dead (lenient scan consistency rejects, gap 1.9375 >> tol)";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P22 复审 #3（2026-08-23）拐角路由连续探针（MinecartManager 直编）【t982 改版】──
    //   原腿钉「坡臂拐角双线性 rise 连续」（拐角格东臂 +1 / 南臂同层 → 拐角 quad 沿坡臂整边抬升 45°）。
    //   t982 用户铁律「一格铁轨绝对不可同时转弯和上坡」落成 railConnections 规则①互斥后：坡臂拐角形态
    //   断绝（任一臂带坡 → 禁弯，落成坡臂轴向直坡段，垂直臂弃连）→ 拐角只属平地、rise 恒 0，双线性
    //   连续性由几何构造保证。本腿改钉**平地拐角**路由连续（零回归守卫）：
    //   (a) Y 连续：每 tick |Δy| ≤ 0.55（全轨同层 → 恒 0 阶跃）；
    //   (b) 俯仰连续：每 tick |Δpitch| ≤ 46° 且 |pitch| ≤ 45.5°（kCartPitchMaxDeg 护栏仍在）；
    //   (c) 过弯驶达南死端格心停驻 + 停稳守卫（连续性断言不放松「不出轨」底线）。
    runLegMulti({ "review#3 flat-corner route continuity (t982 reshaped: slope-arm corners are outlawed by the turn"
        "-x-ascend mutual exclusion, corners are flat-only now so the bilinear-rise geometry class is gon"
        "e - this leg guards the flat-corner round trip): Y step <=0.55/tick, pitch continuous |d|<=46 & "
        "clamped 45, parks at far dead-end" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例）。需 6×6×5（含隔离边；全轨同层 Y+1）。
        int cx = -1, cz = -1;
        for (int zz = 1; zz < 96 && cx < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 4 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { cx = xx; cz = zz; }
            }
        if (cx < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#3 flat-corner route: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            const int kCy = kRigY + 1; // t982 后拐角只属平地 → 全轨同层
            // 东平臂（死端 (cx+2)）+ 平拐角 (cx)（东臂 + 南臂同层）+ 南平臂 (cz+1..cz+3) 死端。
            w.setBlock(cx + 2, kCy, cz, BR::Rail, 0);
            w.setBlock(cx + 1, kCy, cz, BR::Rail, 0);
            w.setBlock(cx,     kCy, cz, BR::Rail, 0);
            w.setBlock(cx,     kCy, cz + 1, BR::Rail, 0);
            w.setBlock(cx,     kCy, cz + 2, BR::Rail, 0);
            w.setBlock(cx,     kCy, cz + 3, BR::Rail, 0);
            MinecartManager carts;
            carts.spawnCart(cx + 2, kCy, cz, &w); // 单端连接（西）→ spawn 定向 -X 西行
            bool mounted = carts.tryMount(QVector3D(float(cx + 2) + 0.5f, float(kCy) + 2.0f,
                                                    float(cz) + 0.5f),
                                          QVector3D(0, -1, 0), 4.0f);
            QVector3D prev = carts.posAt(0);
            bool yCont = true, pitchCont = true, pitchClamped = true, onFootprint = true;
            float prevPitch = 0.0f;
            QVector3D cp = prev;
            for (int t = 0; t < 900 && onFootprint; ++t) {
                // 骑乘驱动（追推跑法摩擦磨停点随机、退化 wish 在死端形成推-停振荡，终点不确定；骑手
                // 连续供速 + 死端「停在格心、零溢出」给出确定性终点）。wish 沿臂正向：西行段 (-1,0)，
                // 过拐角心（z 越过 cz+0.5）后切 (0,+1) —— 死端处 dot<0 被滤 → 停驻不动。
                const bool southArm = cp.z() > float(cz) + 0.5f;
                carts.tickRiddenCart(0.016, &w, southArm ? 0.0f : -1.0f,
                                     southArm ? 1.0f : 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                cp = carts.posAt(0);
                if (std::fabs(cp.y() - prev.y()) > 0.55f) yCont = false;   // (a) Y 连续
                const float p = carts.pitchAt(0);
                if (t > 0 && std::fabs(p - prevPitch) > 46.0f) pitchCont = false; // (b) 俯仰连续
                if (std::fabs(p) > 45.5f) pitchClamped = false;
                prevPitch = p;
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                const bool onTrack = (bx >= cx && bx <= cx + 2 && bz == cz) // 东臂（含拐角列）
                                     || (bx == cx && bz >= cz + 1 && bz <= cz + 3); // 南臂
                if (!onTrack) { onFootprint = false; break; }
                prev = cp;
            }
            const QVector3D fin = carts.posAt(0);
            // 终点：死端格心停驻（沿臂 wish 在死端无 dot≥0 连接 → 停驻重选向保持静止；「轨尽头 → 停在
            //   格心、零溢出」是 stepCartAlongRail 到心分支的既定语义）。
            bool ok = mounted && yCont && pitchCont && pitchClamped && onFootprint
                && std::fabs(fin.x() - (float(cx) + 0.5f)) < 0.01f
                && std::fabs(fin.z() - (float(cz + 3) + 0.5f)) < 0.01f
                && std::fabs(fin.y() - (float(kCy) + rideH)) < 0.02f;
            if (ok) {
                for (int t = 0; t < 20 && ok; ++t) { // 停稳守卫（停止追推后钉死）
                    carts.tickPushedCarts(0.016f, &w);
                    if ((carts.posAt(0) - fin).length() > 1e-4f) ok = false;
                }
            }
            if (!ok)
                qInfo().noquote() << "  flat-corner route: final" << fin << "rig cx" << cx << "cz" << cz
                                  << "yCont" << yCont
                                  << "pitchCont" << pitchCont << "clamp" << pitchClamped
                                  << "onFootprint" << onFootprint;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#3 flat-corner route continuity (t982 reshaped: slope-arm"
                                 " corners are outlawed by the turn-x-ascend mutual exclusion, corners"
                                 " are flat-only now so the bilinear-rise geometry class is gone -"
                                 " this leg guards the flat-corner round trip): Y step <=0.55/tick,"
                                 " pitch continuous |d|<=46 & clamped 45, parks at far dead-end";
            // 清场
            carts.clearAll();
            w.setBlock(cx + 2, kCy, cz, BR::Air, 0);
            w.setBlock(cx + 1, kCy, cz, BR::Air, 0);
            w.setBlock(cx,     kCy, cz, BR::Air, 0);
            for (int dz = 1; dz <= 3; ++dz) w.setBlock(cx, kCy, cz + dz, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P-t982 铁轨转弯×上坡互斥探针（World setBlock + MinecartManager 直编；spec「一格铁轨绝对不可
    //    同时转弯和上坡——坡上转弯场景转弯贴图被拉伸 45° 兼作上坡；坡上转弯按 MC 口径落成平转弯 /
    //    直上坡之一；直线坡 / 平转弯零回归」）──
    //   根因：railConnections 规则①（t709 臂高放宽后）允许带坡臂的垂直对成弯 → mesher 拐角 quad 沿坡臂
    //   整边抬 1.0（armLift）= 拐角贴图拉伸 45° 兼作上坡；矿车 railRiseAt 同面 45° 爬坡过弯。
    //   修：规则①两臂同层（三高探针层差均 0）才许弯；任一臂带坡 → 禁弯，落成坡臂轴向直坡段（垂直臂
    //   弃连，坡度优先于转弯）；规则⑤ T 分支弯道端恰一端带坡时选平端。腿：
    //   (a) 坡顶接转弯（用户场景）逐格 shape：拐角候选格（东臂 +1 + 南臂同层）→ state = 直坡段（持 Px、
    //       无 Pz —— turn 位与 ascend 位不再同置；railCornerArms false = 45° 拉伸形态断绝）；南臂 stub
    //       单向指回（Nz——端点相对才连，坡格侧不回连）；
    //   (b) 平转弯零回归：同层垂直对 → 拐角 2 位 + railCornerArms true；
    //   (c) 坡底同判：东臂 -1 + 南臂同层 → 直坡段（Px），南臂弃连；
    //   (d) 行为腿：骑乘东行爬坡到坡顶格（西臂 -1 + 南臂 stub，无东延续）→ 车直线停驻坡顶（z 恒轨心线、
    //       不被拽上南臂）；pre-fix 该格成弯（Nx|Pz）→ 车过心即被甩向南臂（z 变）= 红；
    //   (e) 源码钉：互斥判定两行 / T 分支平端优选。
    runLegMulti({ "t982 rail turn x ascend mutual exclusion: railConnections corner rule (t709 arm-height relaxatio"
        "n) let a perpendicular pair with a SLOPED arm form a corner - the mesher corner quad then lifts "
        "its full slope-arm edge by 1.0 (armLift), stretching the corner texture 45 degrees to also serve"
        " as the ascent (user report), and the cart's railRiseAt mirror made it climb the stretched face "
        "through the turn. Fix: the corner forms only when BOTH arms are same-layer (three-high probe del"
        "tas == 0); any sloped arm forbids the bend and the cell becomes a straight ascending/descending "
        "segment along the sloped arm (slope beats turn, the perpendicular arm is dropped and re-resolves"
        " as an independent stub facing the cell's side), so a slope-top turn is exactly one of flat-turn"
        " or straight-slope and the 45-degree stretched form is gone; the t812 switch T branch prefers th"
        "e flat through-end for its bend when exactly one end is sloped. Probe legs: (a) slope-top with a"
        " perpendicular same-level stub resolves to a straight east-ascending segment holding Px with NO "
        "Pz (turn and ascend bits never coexist, railCornerArms false) while the stub one-way faces it wi"
        "th Nz; (b) same-level perpendicular pair still corners (Px|Pz, arms +1/+1, flat-turn zero regres"
        "sion); (c) slope-bottom (east arm -1) likewise becomes a straight descending segment; (d) a moun"
        "ted cart creeping east climbs and stops ON the slope-top cell center with z pinned to the rail l"
        "ine (pre-fix corner Nx|Pz flung the cart onto the south stub = red); (e) source pins for the mut"
        "ual-exclusion lines and the flat-end preference" }, [&]() {
        // (a)(b)(c) shape 腿 rig 选址：footprint xt-2..xt+2 × zt-1..zt+2 × Y-2..Y+2。
        int xt = -1, zt = -1;
        for (int zz = 3; zz < 94 && xt < 0; zz += 4)
            for (int xx = 6; xx + 2 < 96 && xt < 0; ++xx) {
                bool clear = true;
                for (int dx = -2; dx <= 2 && clear; ++dx)
                    for (int dz = -1; dz <= 2 && clear; ++dz)
                        for (int dy = -2; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xt = xx; zt = zz; }
            }
        bool okA = false, okB = false, okC = false, okD = false;
        if (xt < 0) {
            qInfo().noquote() << "  [t982 diag] shape: no clear rig area";
        } else {
            const auto probe = [&](int dx, int dz, int dy) {
                return BlockRegistry::RailProbe{ w.blockAt(xt + dx, kRigY + dy, zt + dz),
                                                 w.blockAt(xt + dx, kRigY + dy + 1, zt + dz),
                                                 w.blockAt(xt + dx, kRigY + dy - 1, zt + dz) };
            };
            // (a) 坡顶接转弯：候选格 (xt,Y)（东臂 (xt+1,Y+1) +1；南臂 (xt,Y,zt+1) 同层）。
            w.setBlock(xt + 1, kRigY + 1, zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY,     zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY,     zt + 1, BR::Rail, 0);
            tickN(w, 2);
            const quint8 stA = w.stateAt(xt, kRigY, zt);
            const quint8 conA = quint8(stA & 0x0F);
            int aDummyXD = 0, aDummyZD = 0;
            const bool aTurn = (conA & BR::RailConnPx) != 0          // 直坡段持东向连接（ascend 在）
                            && (conA & BR::RailConnPz) == 0          // 转弯位断绝
                            && BlockRegistry::railProbeDelta(probe(1, 0, 0)) == 1
                            && !BlockRegistry::railCornerArms(conA, aDummyXD, aDummyZD);
            const quint8 stStub = w.stateAt(xt, kRigY, zt + 1);
            const bool aStub = (quint8(stStub & 0x0F) == BR::RailConnNz); // stub 单向指回
            okA = aTurn && aStub;
            if (!okA)
                qInfo().noquote() << "  [t982 diag] a turn" << aTurn << "stub" << aStub
                                  << "con" << conA << "stubCon" << (stStub & 0x0F);
            w.setBlock(xt + 1, kRigY + 1, zt, BR::Air, 0);
            w.setBlock(xt,     kRigY,     zt, BR::Air, 0);
            w.setBlock(xt,     kRigY,     zt + 1, BR::Air, 0);
            tickN(w, 2);
            // (b) 平转弯零回归：同层垂直对 (xt+1,Y) + (xt,Y,zt+1)。
            w.setBlock(xt + 1, kRigY, zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY, zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY, zt + 1, BR::Rail, 0);
            tickN(w, 2);
            const quint8 stB = w.stateAt(xt, kRigY, zt);
            const quint8 conB = quint8(stB & 0x0F);
            int bXD = 0, bZD = 0;
            okB = conB == quint8(BR::RailConnPx | BR::RailConnPz)
                && BlockRegistry::railCornerArms(conB, bXD, bZD)
                && bXD == 1 && bZD == 1;
            if (!okB)
                qInfo().noquote() << "  [t982 diag] b con" << conB;
            w.setBlock(xt + 1, kRigY, zt, BR::Air, 0);
            w.setBlock(xt,     kRigY, zt, BR::Air, 0);
            w.setBlock(xt,     kRigY, zt + 1, BR::Air, 0);
            tickN(w, 2);
            // (c) 坡底同判：候选格 (xt,Y)（东臂 (xt+1,Y-1) -1；南臂同层）→ 直坡段 Px。
            w.setBlock(xt + 1, kRigY - 1, zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY,     zt, BR::Rail, 0);
            w.setBlock(xt,     kRigY,     zt + 1, BR::Rail, 0);
            tickN(w, 2);
            const quint8 stC = w.stateAt(xt, kRigY, zt);
            const quint8 conC = quint8(stC & 0x0F);
            okC = (conC & BR::RailConnPx) != 0 && (conC & BR::RailConnPz) == 0
                && BlockRegistry::railProbeDelta(probe(1, 0, 0)) == -1;
            if (!okC)
                qInfo().noquote() << "  [t982 diag] c con" << conC;
            w.setBlock(xt + 1, kRigY - 1, zt, BR::Air, 0);
            w.setBlock(xt,     kRigY,     zt, BR::Air, 0);
            w.setBlock(xt,     kRigY,     zt + 1, BR::Air, 0);
            tickN(w, 2);
        }
        // (d) 行为腿 rig 选址：footprint xd-1..xd+2 × zd-1..zd+1 × Y-1..Y+2。
        int xd = -1, zd = -1;
        for (int zz = 3; zz < 94 && xd < 0; zz += 4)
            for (int xx = 6; xx + 2 < 96 && xd < 0; ++xx) {
                bool clear = true;
                for (int dx = -1; dx <= 2 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xd = xx; zd = zz; }
            }
        if (xd < 0) {
            qInfo().noquote() << "  [t982 diag] d: no clear rig area";
        } else {
            w.setBlock(xd,     kRigY,     zd, BR::Rail, 0); // 坡底平轨
            w.setBlock(xd + 1, kRigY,     zd, BR::Rail, 0); // 坡格（东邻 +1 → 面自西向东抬）
            w.setBlock(xd + 2, kRigY + 1, zd, BR::Rail, 0); // 坡顶格（t982 后 = 直坡段顶，南臂弃连）
            w.setBlock(xd + 2, kRigY + 1, zd + 1, BR::Rail, 0); // 南臂 stub（坡格侧面）
            tickN(w, 2);
            const quint8 stTop = w.stateAt(xd + 2, kRigY + 1, zd);
            MinecartManager carts;
            carts.spawnCart(xd, kRigY, zd, &w);
            const bool mounted = carts.tryMount(QVector3D(float(xd) + 0.5f, float(kRigY) + 2.0f,
                                                          float(zd) + 0.5f), QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            bool zPulled = false, reached = false;
            for (int t = 0; t < 1200; ++t) {
                // 蠕行东行（wish 大垂直分量 → proj≈0.0625 → targetV≈0.3 爬坡）——坡顶格心 deadEnd
                //   停驻（<3 不飞出）。死亡车（防御）不采样。
                carts.tickRiddenCart(0.016, &w, 0.0625f, 0.998f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!carts.aliveAt(0)) break;
                cp = carts.posAt(0);
                if (std::fabs(cp.z() - (float(zd) + 0.5f)) > 0.02f) zPulled = true; // 被拽上南臂
                if (std::fabs(cp.x() - (float(xd + 2) + 0.5f)) <= 0.3f
                    && std::fabs(cp.z() - (float(zd) + 0.5f)) <= 0.02f) reached = true;
            }
            okD = mounted
                && (quint8(stTop & 0x0F) == BR::RailConnNx)   // 坡顶 = 直坡段（西向连接，无弯位）
                && reached && !zPulled;
            if (!okD)
                qInfo().noquote() << "  [t982 diag] d mount" << mounted << "top" << (stTop & 0x0F)
                                  << "reached" << reached << "zPulled" << zPulled << "fin" << cp;
            carts.clearAll();
            w.setBlock(xd,     kRigY,     zd, BR::Air, 0);
            w.setBlock(xd + 1, kRigY,     zd, BR::Air, 0);
            w.setBlock(xd + 2, kRigY + 1, zd, BR::Air, 0);
            w.setBlock(xd + 2, kRigY + 1, zd + 1, BR::Air, 0);
            tickN(w, 2);
        }
        // (e) 源码钉（任一消失即红）。
        const QString exeDir982 = QCoreApplication::applicationDirPath();
        const QString root982 = QDir(exeDir982 + QStringLiteral("/..")).absolutePath();
        auto readSrc982 = [&root982](const QString &rel) -> QString {
            QFile f(root982 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString br982 = readSrc982(QStringLiteral("src/Core/blockregistry.cpp"));
        const bool okE1 = br982.contains(QStringLiteral(
            "const int dxArm = hasPX ? railProbeDelta(px) : railProbeDelta(nx);"));
        const bool okE2 = br982.contains(QStringLiteral(
            "if (dxArm != 0 || dzArm != 0) {"));
        const bool okE3 = br982.contains(QStringLiteral(
            "if (dEndPos == 0 && dEndNeg != 0) return quint8(stem | negEnd);"));
        const bool okT982 = okA && okB && okC && okD && okE1 && okE2 && okE3;
        if (!okT982) ++totalFail;
        if (!okT982)
            qInfo().noquote() << "  [t982 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE1 << okE2 << okE3;
        qInfo().noquote() << (okT982 ? "PASS" : "FAIL")
                          << "| t982 rail turn x ascend mutual exclusion: railConnections corner rule"
                             " (t709 arm-height relaxation) let a perpendicular pair with a SLOPED arm"
                             " form a corner - the mesher corner quad then lifts its full slope-arm"
                             " edge by 1.0 (armLift), stretching the corner texture 45 degrees to also"
                             " serve as the ascent (user report), and the cart's railRiseAt mirror"
                             " made it climb the stretched face through the turn. Fix: the corner"
                             " forms only when BOTH arms are same-layer (three-high probe deltas =="
                             " 0); any sloped arm forbids the bend and the cell becomes a straight"
                             " ascending/descending segment along the sloped arm (slope beats turn,"
                             " the perpendicular arm is dropped and re-resolves as an independent"
                             " stub facing the cell's side), so a slope-top turn is exactly one of"
                             " flat-turn or straight-slope and the 45-degree stretched form is gone;"
                             " the t812 switch T branch prefers the flat through-end for its bend"
                             " when exactly one end is sloped. Probe legs: (a) slope-top with a"
                             " perpendicular same-level stub resolves to a straight east-ascending"
                             " segment holding Px with NO Pz (turn and ascend bits never coexist,"
                             " railCornerArms false) while the stub one-way faces it with Nz;"
                             " (b) same-level perpendicular pair still corners (Px|Pz, arms +1/+1,"
                             " flat-turn zero regression); (c) slope-bottom (east arm -1) likewise"
                             " becomes a straight descending segment; (d) a mounted cart creeping"
                             " east climbs and stops ON the slope-top cell center with z pinned to"
                             " the rail line (pre-fix corner Nx|Pz flung the cart onto the south"
                             " stub = red); (e) source pins for the mutual-exclusion lines and the"
                             " flat-end preference"
                          ;
    });

    // ── P23 复审 #23（2026-08-23）段中重选向横向收敛限速探针（MinecartManager 直编，同 P12c 场景）──
    //   Review #23：停驻重选向（minecartmanager tickRiddenCart 停驻分支）在**段中非心位**改 dir → 下一步
    //   行贴轨约束把 ≤0.5 格横向偏移**一次钉回** → 一 tick ~0.5 格横向瞬移（被骑时玩家视点同步跳；旧
    //   P12c 只验收终态测不出）。修后收敛限速每 tick ≤kCartCenterSnapPerTick(0.1)。断言：
    //   (a) 蠕行进拐角格早段松键磨停 → 停驻点在格心之前、偏移 ∈[0.11,0.45]（>0.1 保证能分辨瞬移与限速）；
    //   (b) 停驻位 +X wish 一 tick 重选向出口臂（yaw 270）；
    //   (c) 重推期每 tick 横向（z，此时垂直于行进轴）位移 ≤0.101（修前首 tick = 全偏移 ≥0.11 → FAIL）
    //       且限窗内收敛到格心线（|z−(z0+0.5)| ≤1e-3）；
    //   (d) 沿东臂驶达死端格心停驻（限速不破坏到达性）。
    runLegMulti({ "review#23 mid-cell relaunch: lateral recenter capped at 0.1/tick (was one-shot ~0.5 teleport), c"
        "onverges to centerline, still reaches east dead-end" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例）。L 形：南腿 2 直 + 拐角 + 东臂 3 直，需 6×6×5（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -3; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#23 mid-cell relaunch snap rate: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            w.setBlock(x0, kRigY, z0 - 2, BR::Rail, 0); // 南死端（spawn 格）
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0,     BR::Rail, 0); // 拐角（南臂 + 东臂）
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Rail, 0); // 东死端
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0 - 2, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0 - 2) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            // (a) 蠕行（wish 大垂直分量 → proj≈0.0625 → targetV≈0.5 格/s，P12c(a) 同款）进拐角格后
            //     再爬 2 tick 即松键 → 磨停点落在格心之前（偏移 ~0.2-0.4）。
            bool entered = false;
            int enteredTicks = -1;
            for (int t = 0; t < 2500 && ok; ++t) {
                const bool release = entered && (t - enteredTicks) > 2;
                carts.tickRiddenCart(0.016, &w, release ? 0.0f : 0.998f,
                                     release ? 0.0f : 0.0625f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!entered && int(std::floor(cp.z())) == z0 && int(std::floor(cp.x())) == x0) {
                    entered = true;
                    enteredTicks = t;
                }
                if (release) break;
            }
            for (int t = 0; t < 250 && ok; ++t) { // 摩擦磨停
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
            }
            const float off0 = std::fabs(cp.z() - (float(z0) + 0.5f));
            ok = ok && entered && int(std::floor(cp.x())) == x0 && int(std::floor(cp.z())) == z0
                && cp.z() < float(z0) + 0.5f && off0 >= 0.11f && off0 <= 0.45f; // (a) 停驻偏移前置
            if (!ok)
                qInfo().noquote() << "  park position not mid-corner pre-center with usable offset:"
                                  << cp << "off0" << off0 << "entered" << entered;
            if (ok) {
                // (b) 停驻位 +X 重选向（一 tick）：dir 掰向出口臂，yaw → 270。
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                ok = int(std::lround(carts.yawAt(0))) % 360 == 270;
                if (!ok) qInfo().noquote() << "  reselect heading not +X (yaw 270), yaw" << carts.yawAt(0);
            }
            bool convOk = false;
            if (ok) {
                // (c) 重推期横向限速：z 是垂直轴（行进 +X）→ 每 tick |Δz| ≤0.101，直到钉回格心线。
                bool rateOk = true;
                float prevZ = cp.z();
                int convLeft = -1; // -1 = 未开始判定（首 tick 的 prevZ 基准在 (b) 末）
                for (int t = 0; t < 600; ++t) {
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    const float dz = std::fabs(cp.z() - prevZ);
                    if (convLeft < 0) { // 收敛窗内：横向位移须限速（修前首 tick ≈ off0 全额瞬移）
                        if (dz > 0.101f) { rateOk = false; break; }
                        if (std::fabs(cp.z() - (float(z0) + 0.5f)) <= 1e-3f) convLeft = 0; // 已收敛
                    } else if (++convLeft == 1) { break; } // 收敛后再验 1 tick（保持格心线）
                    prevZ = cp.z();
                }
                convOk = rateOk && std::fabs(cp.z() - (float(z0) + 0.5f)) <= 1e-3f;
                if (!convOk)
                    qInfo().noquote() << "  lateral convergence violated rate cap or never converged:"
                                      << "rateOk" << rateOk << "z" << cp.z() << "off0" << off0;
                ok = ok && convOk;
            }
            if (ok) { // (d) 沿东臂驶达死端格心停驻 + 停稳守卫。
                for (int t = 0; t < 600; ++t) {
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    if (cp.x() >= float(x0 + 3) + 0.5f) break;
                }
                const QVector3D fin = carts.posAt(0);
                ok = std::fabs(fin.x() - (float(x0 + 3) + 0.5f)) < 0.01f
                    && std::fabs(fin.z() - (float(z0) + 0.5f)) < 0.01f
                    && std::fabs(fin.y() - (float(kRigY) + rideH)) < 0.02f;
                if (ok) {
                    for (int t = 0; t < 20 && ok; ++t) {
                        carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                        carts.tickPushedCarts(0.016, &w);
                        if ((carts.posAt(0) - fin).length() > 1e-4f) ok = false;
                    }
                }
                if (!ok) qInfo().noquote() << "  did not park at east dead-end center, fin" << fin;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#23 mid-cell relaunch: lateral recenter capped at 0.1/tick (was "
                                 "one-shot ~0.5 teleport), converges to centerline, still reaches east dead-end";
            // 清场
            carts.clearAll();
            for (int dx = 0; dx <= 3; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            for (int dz = -2; dz <= -1; ++dz) w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P24 复审 #4（2026-08-23 中危）重力坍落柱附着物级联掉落探针 ──
    //   Review #4：dropGravityColumn 逐格 m_chunks.setBlock(Air) 直写绕过 check*OnEdit 编辑钩子族 +
    //   无邻格火把失撑扫 → 沙柱坍落后柱顶火把 / 红石火把（照常发光供电）/ 甘蔗 / 雪层 / 花 / 压力板 / 铁轨
    //   全部悬空残留（t794 铁砧并入重力族后受面扩大）。修后 dropGravityColumn 每清一格调
    //   recheckAttachmentsAfterClear（正上方族 check*OnEdit + 6 邻火把 / 红石火把扫，与 clearBlockSilent
    //   口径合一）。断言（8 柱 rig：石基座 + 2 高沙柱 + 各一附着物；拆基座触发整柱坍落）：
    //   (a) 柱顶附着物随坍落清空：火把 / 铁轨 / 木压力板 / 花 → Air + blockDroppedAsItem；甘蔗×2 →
    //       整柱级联（两格全清）；雪层 → Air + snowLayerFell；柱侧贴墙红石火把（state 编码附着本柱）
    //       → Air + 掉落（修前全残留 → FAIL）；
    //   (b) 沙柱本体全清（坍落完整性，非附着物断言的副作用核对）；
    //   (c) 对照柱（基座不拆）火把原样保留（拆别柱不误伤）+ 全程 ≥7 次掉落信号（含甘蔗 2）+ ≥1 雪层坍落。
    runLegMulti({ "review#4 gravity-column attachments: base removal collapses sand column and clears torch/rail/pl"
        "ate/flower (dropped), sugarcane cascade (2 cells), snow layer (fell entity), side redstone torch"
        " (dropped); control column untouched; >=7 drop signals + 1 snow-fell" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例——nextSlot() 网格已耗尽）。8 柱单排、列距 2（柱侧红石火把
        //   占邻列不受扰：邻柱坍落扫到它时 state 解码支撑在另一侧 → 跳过）→ 需 17×3×7（含隔离边）。
        //   首版 4×2 网格 14×11×7 实测扫不到（124 矩阵 rig 残块 + 生成石柱把大块净空切碎）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 15 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 15 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 5 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#4 gravity-column attachments: no clear rig area found";
        } else {
            const int by = kRigY;
            // 8 柱（单排列距 2）：0 火把 / 1 铁轨 / 2 木压力板 / 3 柱侧红石火把 / 4 甘蔗×2 / 5 花 / 6 雪层 /
            //   7 对照火把。列距 2：柱 3 的红石火把在 colX[3]+1（与柱 4 隔 1 格），两柱坍落互不误清。
            const int colX[8] = { x0, x0 + 2, x0 + 4, x0 + 6, x0 + 8, x0 + 10, x0 + 12, x0 + 14 };
            for (int i = 0; i < 8; ++i) {
                const int cz = z0;
                w.setBlock(colX[i], by,     cz, BR::Stone, 0); // 基座
                w.setBlock(colX[i], by + 1, cz, BR::Sand, 0);  // 沙柱 ×2（放置自检：下方满立方 → 稳）
                w.setBlock(colX[i], by + 2, cz, BR::Sand, 0);
            }
            w.setBlock(colX[0], by + 3, z0, BR::Torch, 0);             // 柱顶立火把（state 0 贴地）
            w.setBlock(colX[1], by + 3, z0, BR::Rail, 0);              // 柱顶铁轨
            w.setBlock(colX[2], by + 3, z0, BR::WoodPressurePlate, 0); // 柱顶压力板
            w.setBlock(colX[3] + 1, by + 2, z0, BR::RedstoneTorch, 1); // 柱侧贴墙红石火把（state 1=TorchOnNX 支撑 -X 本柱顶格）
            w.setBlock(colX[4], by + 3, z0, BR::Sugarcane, 0);         // 甘蔗 ×2（级联整柱）
            w.setBlock(colX[4], by + 4, z0, BR::Sugarcane, 0);
            w.setBlock(colX[5], by + 3, z0, BR::FlowerRed, 0);         // 花
            w.setBlock(colX[6], by + 3, z0, BR::SnowLayer, 0);         // 雪层（1 层）
            w.setBlock(colX[7], by + 3, z0, BR::Torch, 0);             // 对照柱顶火把（基座不拆）
            const int drops0 = dropItemCount, snow0 = snowFellCount;
            // 触发：拆柱 0..6 的基座（对照柱 7 不拆）→ checkGravityBlockOnEdit ② → dropGravityColumn
            //   → 每清一格 recheckAttachmentsAfterClear（柱顶格清完的复检带走全部附着物）。
            for (int i = 0; i < 7; ++i) w.setBlock(colX[i], by, z0, BR::Air, 0);
            tickN(w, 2);
            bool ok = true;
            // (a) 附着物清空（含甘蔗底格级联 + 柱侧红石火把）。
            const int attY[7] = { by + 3, by + 3, by + 3, by + 2, by + 4, by + 3, by + 3 };
            for (int i = 0; i < 7 && ok; ++i) {
                const int ax = (i == 3) ? colX[3] + 1 : colX[i];
                if (w.blockAt(ax, attY[i], z0) != BR::Air) {
                    qInfo().noquote() << "  column" << i << "attachment survived at"
                                      << ax << attY[i] << z0
                                      << "id" << int(w.blockAt(ax, attY[i], z0));
                    ok = false;
                }
            }
            if (ok && w.blockAt(colX[4], by + 3, z0) != BR::Air) { // 甘蔗整柱级联（底格）
                qInfo().noquote() << "  sugarcane column base survived";
                ok = false;
            }
            // (b) 沙柱本体全清。
            for (int i = 0; i < 7 && ok; ++i)
                for (int dy = 1; dy <= 2 && ok; ++dy)
                    if (w.blockAt(colX[i], by + dy, z0) != BR::Air) {
                        qInfo().noquote() << "  column" << i << "sand cell survived at dy" << dy;
                        ok = false;
                    }
            // (c) 对照柱原样 + 信号计数（火把1+轨1+板1+红石火把1+甘蔗2+花1 = 7 掉落 + 1 雪层坍落）。
            if (ok && w.blockAt(colX[7], by + 3, z0) != BR::Torch) {
                qInfo().noquote() << "  control torch disturbed";
                ok = false;
            }
            const int drops = dropItemCount - drops0, snows = snowFellCount - snow0;
            if (ok && (drops < 7 || snows < 1)) {
                qInfo().noquote() << "  drop/snow signal counts low: drops" << drops << "snow" << snows;
                ok = false;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#4 gravity-column attachments: base removal collapses sand "
                                 "column and clears torch/rail/plate/flower (dropped), sugarcane cascade "
                                 "(2 cells), snow layer (fell entity), side redstone torch (dropped); "
                                 "control column untouched; >=7 drop signals + 1 snow-fell";
            // 清场（坍落成功时柱 0..6 已全空；拆对照柱基座会再触发一次坍落 + 掉落 → 全格兜底清）。
            for (int i = 0; i < 8; ++i)
                for (int dy = 0; dy <= 4; ++dy)
                    w.setBlock(colX[i], by + dy, z0, BR::Air, 0);
            w.setBlock(colX[3] + 1, by + 2, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P25 复审 #15（2026-08-23 低危）「支撑格被换成非满顶支撑 → 轨坍落」探针 ──
    //   Review #15：checkRailOnEdit 失撑守卫旧要求 `id == Air`（仅挖掘 / 爆炸清格触发），本格被换成水
    //   （冰融化 setWaterSilent 写 Water）/ 火（可燃支撑焚毁写 Fire）时轨悬浮到水蒸发。修后失撑判定只读
    //   「本格现内容非 isTopFlushSupport 即坍落」。断言：
    //   (a) 冰融成水（setWaterSilent 写 Water 入支撑格，真实融化 tickIceMelt 同入口）→ 正上方铁轨立即
    //       坍落清 Air + blockDroppedAsItem(id=Rail)——修前 id==Water≠Air 守卫跳过 → 轨浮空 → FAIL；
    //   (b) 焚毁路径（setBlock 写 Fire 入可燃支撑格 Planks，火吞支撑的等价写）→ 轨同样立即坍落；
    //   (c) 对照：水写入非支撑邻格 → 轨保留（坍落只看唯一支撑位，不误伤邻写）。
    runLegMulti({ "review#15 rail support substitution: ice->water (setWaterSilent) and planks->fire (setBlock) und"
        "er rail both drop the rail immediately (support loss reads current cell content, not just Air ed"
        "its); neighbor water write leaves rail intact" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例）。3 组各 2 格宽（支撑+轨）+ 隔离边 → 9×4×5。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 7 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dy = -1; dy <= 2 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#15 rail support substitution: no clear rig area found";
        } else {
            const int by = kRigY;
            // (a) 冰支撑 + 轨：setWaterSilent 模拟融化结果（Ice→Water）。
            w.setBlock(x0, by, z0, BR::Ice, 0);
            w.setBlock(x0, by + 1, z0, BR::Rail, 0);
            const int drops0 = dropItemCount;
            w.setWaterSilent(x0, by, z0, BR::Water, 0);
            const bool aOk = w.blockAt(x0, by + 1, z0) == BR::Air
                             && w.blockAt(x0, by, z0) == BR::Water
                             && dropItemCount > drops0 && lastDropId == int(BR::Rail);
            // (b) 木板支撑 + 轨：setBlock 写 Fire（可燃支撑焚毁等价写路径；harness 不跑 tickFire → 火静止）。
            w.setBlock(x0 + 3, by, z0, BR::Planks, 0);
            w.setBlock(x0 + 3, by + 1, z0, BR::Rail, 0);
            const int drops1 = dropItemCount;
            w.setBlock(x0 + 3, by, z0, BR::Fire, 0);
            const bool bOk = w.blockAt(x0 + 3, by + 1, z0) == BR::Air
                             && w.blockAt(x0 + 3, by, z0) == BR::Fire
                             && dropItemCount > drops1 && lastDropId == int(BR::Rail);
            // (c) 对照：石支撑 + 轨；水写进邻格（非支撑位）→ 轨保留。
            w.setBlock(x0 + 6, by, z0, BR::Stone, 0);
            w.setBlock(x0 + 6, by + 1, z0, BR::Rail, 0);
            w.setWaterSilent(x0 + 7, by, z0, BR::Water, 0);
            tickN(w, 2);
            const bool cOk = w.blockAt(x0 + 6, by + 1, z0) == BR::Rail;
            const bool ok = aOk && bOk && cOk;
            if (!ok)
                qInfo().noquote() << "  rail-after-substitution: melt" << aOk << "burn" << bOk
                                  << "ctrl" << cOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#15 rail support substitution: ice->water (setWaterSilent) and "
                                 "planks->fire (setBlock) under rail both drop the rail immediately "
                                 "(support loss reads current cell content, not just Air edits); "
                                 "neighbor water write leaves rail intact";
            // 清场
            w.setBlock(x0, by, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by, z0, BR::Air, 0);
            w.setBlock(x0 + 6, by, z0, BR::Air, 0);
            w.setBlock(x0 + 6, by + 1, z0, BR::Air, 0);
            w.setWaterSilent(x0 + 7, by, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P26 复审 #16（2026-08-23 低危）火吞木门整门联动探针（t843 语义重做版）──
    //   Review #16：门在可燃表内，火蔓延只点燃半格 → 另半扇孤立残留无掉落。t843 重做后语义：点燃 = 进
    //   燃烧态（栅格 id 不变），联动迁移到 igniteFlammableAt 单一入口——目标是门 → 配对半扇（state bit3
    //   上/下互补 y∓1）同为门且非湿时一并点燃（同窗同计时 → 同窗烧毁）。断言：
    //   (a) 门下格被邻火点燃的**那次 tickFire 调用内**，上格一并进燃烧态（isBurningAt 真且两半 id 仍是
    //       WoodDoor）——修前上格非火的 6 邻（隔一格对角），只能等下格燃烧的后续窗同态蔓延 → 首次观测
    //       下格燃烧时上格未燃 → FAIL；
    //   (b) 两半同计时同窗烧毁（终态均非 WoodDoor）且 blockBroken(WoodDoor) 恒 0（烧毁无掉落——余烬火
    //       setBlock(Fire) 放置语义，Air 收尾在对称同烧下不触发）；
    //   (c) 对照石柱（不可燃）同布局永不被吞。
    //   确定性：火源 6 邻仅门下格可燃（无燃料不熄灭路径被 hasFuel 门挡）→ 点燃只是时间问题（2.5%/窗
    //   ——review-g #5 叠加补偿后；上限 3000 窗，P(未燃)≈0.975^3000≈e^-76）；harness 只驱动 tickFire
    //   （无雨 / 无风灭混淆源）。
    runLegMulti({ "review#16 door whole-burn (t843 semantics): fire lighting one door half enters it into burning s"
        "tate with id preserved and ignites the paired half in the same call (upper is not 6-adjacent to "
        "the fire - only reachable via linkage); both halves burn out same-window via ember flare (no blo"
        "ckBroken = no-drop burn), stone control never ignites" }, [&]() {
        // rig 寻址：运行期扫描空区（P20 先例）。火源 + 门 2 格 + 石柱 2 格 + 隔离边 → 7×6×6。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dy = -1; dy <= 4 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#16 door whole-burn linkage: no clear rig area found";
        } else {
            const int by = kRigY;
            w.setBlock(x0, by, z0, BR::Fire, 0);              // 火源（邻门可燃 → 恒 hasFuel 不自熄）
            w.setBlock(x0 + 1, by, z0, BR::WoodDoor, 0);      // 门下格（state bit3=0）
            w.setBlock(x0 + 1, by + 1, z0, BR::WoodDoor, 8);  // 门上格（state bit3=1）
            w.setBlock(x0 + 3, by, z0, BR::Stone, 0);         // 对照石柱（不可燃）
            w.setBlock(x0 + 3, by + 1, z0, BR::Stone, 0);
            bool lit = false;
            for (int t = 0; t < 15000 && !lit; ++t) { // 每 5 次 tickFire = 1 窗（kFireTickInterval=5 节流）
                w.tickFire();
                if (w.isBurningAt(x0 + 1, by, z0)) lit = true; // 首次观测下格进燃烧态即停（上格同调用已联动）
            }
            // t843 语义重做版断言：点燃 = 燃烧态（World 侧表，栅格 id 不变——门两半仍 WoodDoor），整扇
            //   联动 = 同一 igniteFlammableAt 调用内配对半扇一并进燃烧态（上格与火源隔一格对角，非 6 邻
            //   ——仅联动可达；同态蔓延是后续窗的事，首窗观测只可能来自联动，旧探针的隔离手法原样保留）。
            const bool aOk = lit && w.blockAt(x0 + 1, by, z0) == BR::WoodDoor
                             && w.isBurningAt(x0 + 1, by + 1, z0)
                             && w.blockAt(x0 + 1, by + 1, z0) == BR::WoodDoor;
            // 驱至烧毁收尾：两半同计时同窗归零 → 一半先烧成余烬火、另一半见对偶非门自走 setBlock(Fire)
            //   （无 Air 收尾路径 → blockBroken(WoodDoor) 恒 0；对偶「仍是未燃门」的 Air 收尾只在不对称
            //   场景触发，对称同烧不命中——见 world.cpp (d) pass 烧毁收尾注释）。
            int doorBreaks = 0;
            QObject::connect(&w, &World::blockBroken, &w,
                             [&](int, int, int, int oldId) { if (oldId == int(BR::WoodDoor)) ++doorBreaks; });
            bool consumed = false;
            for (int t = 0; t < 15000 && !consumed; ++t) {
                w.tickFire();
                if (w.blockAt(x0 + 1, by, z0) != BR::WoodDoor
                    && w.blockAt(x0 + 1, by + 1, z0) != BR::WoodDoor) consumed = true;
            }
            const bool bOk = w.blockAt(x0 + 3, by, z0) == BR::Stone
                             && w.blockAt(x0 + 3, by + 1, z0) == BR::Stone;
            const bool ok = aOk && consumed && doorBreaks == 0 && bOk;
            if (!ok)
                qInfo().noquote() << "  door whole-burn: lit" << lit
                                  << "lower" << int(w.blockAt(x0 + 1, by, z0))
                                  << "upper" << int(w.blockAt(x0 + 1, by + 1, z0))
                                  << "consumed" << consumed << "doorBreaks" << doorBreaks
                                  << "stoneCtrl" << bOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#16 door whole-burn (t843 semantics): fire lighting one door "
                                 "half enters it into burning state with id preserved and ignites the "
                                 "paired half in the same call (upper is not 6-adjacent to the fire - "
                                 "only reachable via linkage); both halves burn out same-window via "
                                 "ember flare (no blockBroken = no-drop burn), stone control never "
                                 "ignites";
            // 清场（火 / 门残格 / 石柱；部分火格可能已自熄 → setBlock(Air) 对 Air no-op 无害）。
            w.setBlock(x0, by, z0, BR::Air, 0);
            w.setBlock(x0 + 1, by, z0, BR::Air, 0);
            w.setBlock(x0 + 1, by + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by + 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── P27 复审 #19（2026-08-23 低危）栅栏向铁砧伸横档探针（mesher 同源直调，同 P11/P19 模式）──
    //   Review #19：t766 铁砧 solid=false 后栅栏连接谓词仍 isFence||isSolid → 栅栏不向铁砧伸横档（同类：
    //   画钉铁砧墙 / 雪傀儡立铁砧不铺雪，UI/实体侧改动矩阵不可锁，本探针锁 mesher 侧谓词）。修后连接
    //   谓词 = isCollidable ∨ isFullCube（R1 口径 a890bfa）。断言（顶点数差分：每连接向 +2 盒横档）：
    //   (a) +X 邻铁砧 → 顶点数 > 全空气基线（横档画出；修前 isSolid(Anvil)=false → 与基线同 → FAIL）；
    //   (b) +X 邻铁砧 == +X 邻栅栏（连接量与栅栏互连完全一致）；
    //   (c) +X 邻火把（ShapeNone）== 基线（非实体面仍不连——谓词放宽不至连空气族）。
    runLegMulti({ "review#19 fence-to-anvil connection: R1 predicate (isCollidable||isFullCube) draws rail arms tow"
        "ard anvil exactly like fence-fence; torch (ShapeNone) still not connected" }, [&]() {
        const float tileW = 1.0f / 16.0f;
        PartialLightCtx lctx; lctx.light = 1.0f;
        for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
        const auto fenceVerts = [&](quint8 nbPosX) -> int {
            PartialNeighborCtx nctx; // 其余三向缺省 0（Air）→ 只 +X 连接位在变
            nctx.posX = nbPosX;
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::WoodFence, 0,
                                         lctx, nctx, tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            return int(verts.size()); // qsizetype → int 显式收窄（顶点数远小于 2^31）
        };
        const int baseN = fenceVerts(BR::Air);
        const int anvilN = fenceVerts(BR::Anvil);
        const int fenceN = fenceVerts(BR::WoodFence);
        const int torchN = fenceVerts(BR::Torch);
        const bool ok = anvilN > baseN && anvilN == fenceN && torchN == baseN;
        if (!ok)
            qInfo().noquote() << "  fence-connect vertex counts: base" << baseN << "anvil" << anvilN
                              << "fence" << fenceN << "torch" << torchN;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review#19 fence-to-anvil connection: R1 predicate (isCollidable||"
                             "isFullCube) draws rail arms toward anvil exactly like fence-fence; torch "
                             "(ShapeNone) still not connected";
    });

    // ── review-e 修复批探针（Review 2026-08-23 #6/#7/#8/#32 + dev-plan t834 生存链闭环）──
    // (a) #7/#8 配方生存链：剪/杀白羊掉 Wool 方块（QML sheepWoolDropId 字面量 27——静态契约由 recipe.cpp 尾部
    //     static_assert 钉死，此处运行期以**QML 同款字面量**喂 matcher 复核）→ 32 条染色配方（t788 已全测 16
    //     dye+wool；此处锁「字面量 27 / 63+idx-1 与配方原料/产物逐位相等」的跨层契约）+ 红床简化配方（羊毛
    //     方块版可合、旧 0x20E 版不再合）+ 红石灯（Glass 方块版可合、旧 0x204 物品版不再合）+ 两方块 dropId=
    //     自身（放置-破坏回收闭环）+ 烧沙仍产 0x204（放置过境物品——生存玻璃唯一入口）。
    // (b) #32 mobDied 第 8 参 sheared：剪毛羊致死 → sheared=true（QML 据此压掉羊毛掉落）；未剪对照 → false。
    // (c) #6 派生缓存文件名带 revision + 逐版清理（mobhead / sheep_woolface 两族；同 t745 icon2 / Review #11
    //     skin 族模式）。#33（QML Math.min/max 钳）与 #34（腿罩随腿摆）是纯 QML 呈现层，矩阵不链 Quick3D，
    //     需人工目视（字面量界标 27/63 已由 (a) 锁住）。
    // ⚠️ (c) 写共享 AppLocalData 目录（缓存生成器落盘路径固定）——探针会清掉 mobhead_1 / woolface 当前真身，
    //     属自愈型副作用（下次构建期 ensureBuiltLocked / 懒生成重落盘），同 t777/t779/t780 先例。
    runLegMulti({ "review-e survival chains & death payload & rev-named caches: sheep wool drop literal 27/63+idx-1"
        " feeds all 16 dye recipes and planks+wool->bed_red (retired 0x20E no longer crafts anything), re"
        "dstone lamp crafts from glass BLOCK (54, self-drop) not glass item 0x204 (sand smelt still yield"
        "s placeable 0x204 transit), mobDied 8th param sheared=true suppresses wool drop on sheared sheep"
        " death (unsheared control false), mobhead/sheep-woolface derived cache filenames embed _r<revisi"
        "on> with per-revision stale cleanup incl. legacy unsuffixed names (QML clamp #33 / leg covers #3"
        "4 = manual visual check)" }, [&]() {
        bool ok = true;
        // (a1) 跨层字面量契约：QML sheepWoolDropId 白→27 / 有色→63+idx-1 ↔ BlockRegistry 同值 ↔ 染色配方
        //     原料/产物逐位相等（QML 掉什么，配方就吃什么、产什么色）。
        {
            const int qmlWhite = 27, qmlBase = 63; // Main.qml sheepWoolDropId 字面量镜像
            if (int(BR::Wool) != qmlWhite || int(BR::FirstWoolVariant) != qmlBase) {
                qInfo().noquote() << "  [review-e diag] literal drift: Wool" << int(BR::Wool)
                                  << "FirstWoolVariant" << int(BR::FirstWoolVariant);
                ok = false;
            }
            for (int i = 0; i < 16; ++i) {
                const int dye = RecipeRegistry::DyeIdBase + i;
                const int drop = (i > 0) ? (qmlBase + i - 1) : qmlWhite; // QML sheepWoolDropId 公式镜像
                // 染色配方原料 = **白羊毛 27**（白羊掉落直连）；断言配方产出 == QML 有色羊掉落 id
                //   （同 id 同方块：剪/杀有色羊得的羊毛 = 染料染出的羊毛，两路产物汇流同一方块段）。
                int g[9] = { dye, qmlWhite, 0, 0, 0, 0, 0, 0, 0 };
                const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, 2);
                if (!m || m->outputId != drop) {
                    qInfo().noquote() << "  [review-e diag] dye" << i << "+white-wool" << qmlWhite << "->"
                                      << (m ? m->outputId : -1) << "expected qmlDrop" << drop;
                    ok = false;
                }
            }
            // 红床：木板+Wool 方块（白羊毛掉落直连）→ BedRed；旧 0x20E 物品版不再合（退役不回头）。
            int gBed[9] = { int(BR::Planks), int(BR::Wool), 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *mBed = RecipeRegistry::match(gBed, 2);
            if (!mBed || mBed->outputId != int(BR::BedRed)) {
                qInfo().noquote() << "  [review-e diag] bed_red from planks+wool-block ->"
                                  << (mBed ? mBed->outputId : -1);
                ok = false;
            }
            int gBedOld[9] = { int(BR::Planks), RecipeRegistry::WoolId, 0, 0, 0, 0, 0, 0, 0 };
            if (RecipeRegistry::match(gBedOld, 2)) {
                qInfo().noquote() << "  [review-e diag] retired wool item 0x20E still crafts a bed";
                ok = false;
            }
            // 红石灯：4 红石十字 + 中心 Glass 方块 → RedstoneLamp；旧 0x204 物品中心版不再合。
            int gLamp[9] = { 0, RecipeRegistry::RedstoneId, 0,
                             RecipeRegistry::RedstoneId, int(BR::Glass), RecipeRegistry::RedstoneId,
                             0, RecipeRegistry::RedstoneId, 0 };
            const RecipeRegistry::Recipe *mLamp = RecipeRegistry::match(gLamp, 3);
            if (!mLamp || mLamp->outputId != int(BR::RedstoneLamp)) {
                qInfo().noquote() << "  [review-e diag] redstone_lamp from glass-block ->"
                                  << (mLamp ? mLamp->outputId : -1);
                ok = false;
            }
            int gLampOld[9] = { 0, RecipeRegistry::RedstoneId, 0,
                                RecipeRegistry::RedstoneId, RecipeRegistry::GlassId, RecipeRegistry::RedstoneId,
                                0, RecipeRegistry::RedstoneId, 0 };
            if (RecipeRegistry::match(gLampOld, 3)) {
                qInfo().noquote() << "  [review-e diag] retired glass item 0x204 center still crafts lamp";
                ok = false;
            }
            // 生存回收闭环：Glass / Wool 破坏 dropId=自身（放置→破坏→回手入配方，Wool/床族自掉先例）；
            //   熔炉烧沙仍产 0x204（放置过境物品 = 生存玻璃唯一入口，playercontroller t405 放置成 Glass）。
            if (BR::dropId(BR::Glass) != int(BR::Glass) || BR::dropId(BR::Wool) != int(BR::Wool)) {
                qInfo().noquote() << "  [review-e diag] self-drop broken: Glass->" << BR::dropId(BR::Glass)
                                  << "Wool->" << BR::dropId(BR::Wool);
                ok = false;
            }
            if (SmeltingRegistry::smeltResult(int(BR::Sand)) != RecipeRegistry::GlassId) {
                qInfo().noquote() << "  [review-e diag] sand smelt ->"
                                  << SmeltingRegistry::smeltResult(int(BR::Sand));
                ok = false;
            }
        }
        // (b) #32：两只成体羊（一剪一不剪）致死 → mobDied 各一发，sheared 载荷 true/false 对照；woolIdx 仍
        //     携带（QML 剪毛分支压掉羊毛掉落、烧死分支仍给熟羊肉——呈现层语义，此处只锁信号载荷）。
        {
            EntityManager emE;
            World wE;
            wE.setWidth(32);
            wE.setDepth(32);
            wE.setHeight(48);
            wE.setSeed(11);
            const int a = emE.spawnMobTyped(14, 12, 15, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
            const int b = emE.spawnMobTyped(16, 12, 15, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
            if (a < 0 || b < 0) {
                qInfo().noquote() << "  [review-e diag] failed to spawn probe sheep" << a << b;
                ok = false;
            } else {
                emE.shearSheep(a); // a 剪毛 / b 对照
                int died = 0, shearedSeen = -1, unshearedSeen = -1;
                QObject::connect(&emE, &EntityManager::mobDied, &emE,
                                 [&](int, int, int, int type, bool, bool, int, bool sheared) {
                                     if (type != EntityManager::MobSheep) return;
                                     ++died;
                                     if (sheared) shearedSeen = 1; else unshearedSeen = 0;
                                 });
                emE.damageEntity(a, emE.maxHealthAt(a));
                emE.damageEntity(b, emE.maxHealthAt(b));
                const QVector3D farListenerE(-1000.0f, 10.0f, -1000.0f);
                for (int t = 0; t < 40 && died < 2; ++t) // 0.64s > kDeathTime 0.5s
                    emE.tick(0.016f, &wE, farListenerE, 0.3f, 1.8f, false);
                if (died != 2 || shearedSeen != 1 || unshearedSeen != 0) {
                    qInfo().noquote() << "  [review-e diag] sheared-death payload: died" << died
                                      << "sheared" << shearedSeen << "unsheared" << unshearedSeen;
                    ok = false;
                }
            }
        }
        // (c) #6：两族派生缓存文件名 _r<rev> 嵌版 + 换版清旧（含 t749 期无后缀旧名）。
        {
            QDir dE(QDir::temp().absoluteFilePath("review_e_cache_probe"));
            dE.removeRecursively();
            dE.mkpath(".");
            const QString furPath = dE.absoluteFilePath("fur.png");
            const QString bodyPath = dE.absoluteFilePath("body.png");
            QImage fE(64, 32, QImage::Format_ARGB32), bE(64, 32, QImage::Format_ARGB32);
            fE.fill(QColor(0xf0, 0xec, 0xe4));
            bE.fill(QColor(0x7a, 0x5a, 0x48));
            if (!fE.save(furPath, "PNG") || !bE.save(bodyPath, "PNG")) {
                qInfo().noquote() << "  [review-e diag] failed to write temp rig PNGs";
                ok = false;
            }
            const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            // 预置无后缀旧名（t749 期格式）→ 调用后应被清。
            const QString legacy = QDir(cacheDir).absoluteFilePath(
                    QStringLiteral("voxelsandbox_rp_sheep_woolface.png"));
            QImage legacyPx(2, 2, QImage::Format_ARGB32);
            legacyPx.fill(Qt::black);
            legacyPx.save(legacy, "PNG");
            const QString p5 = generateSheepWoolFaceFile(furPath, bodyPath, 5);
            const QString p6 = generateSheepWoolFaceFile(furPath, bodyPath, 6);
            if (p5.isEmpty() || p6.isEmpty() || !p5.contains(QStringLiteral("_r5.png"))
                    || !p6.contains(QStringLiteral("_r6.png")) || QFile::exists(p5)
                    || !QFile::exists(p6) || QFile::exists(legacy)) {
                qInfo().noquote() << "  [review-e diag] woolface rev cache: p5" << p5 << "exists"
                                  << QFile::exists(p5) << "p6" << p6 << "exists" << QFile::exists(p6)
                                  << "legacy-gone" << !QFile::exists(legacy);
                ok = false;
            }
            // mobhead 族同模式（pig rig 子目录布局，同 t779）。
            QDir(dE.absoluteFilePath("pig")).mkpath(".");
            QImage pE(64, 32, QImage::Format_ARGB32);
            pE.fill(QColor(0xf0, 0xa0, 0xa8));
            pE.save(dE.absoluteFilePath("pig/pig.png"), "PNG");
            const QString h5 = generateMobHeadIconFor(1, dE.absolutePath(), 5);
            const QString h6 = generateMobHeadIconFor(1, dE.absolutePath(), 6);
            if (h5.isEmpty() || h6.isEmpty() || !h5.contains(QStringLiteral("_r5.png"))
                    || !h6.contains(QStringLiteral("_r6.png")) || QFile::exists(h5)
                    || !QFile::exists(h6)) {
                qInfo().noquote() << "  [review-e diag] mobhead rev cache: h5" << h5 << "exists"
                                  << QFile::exists(h5) << "h6" << h6 << "exists" << QFile::exists(h6);
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review-e survival chains & death payload & rev-named caches: sheep wool drop "
                             "literal 27/63+idx-1 feeds all 16 dye recipes and planks+wool->bed_red (retired "
                             "0x20E no longer crafts anything), redstone lamp crafts from glass BLOCK (54, "
                             "self-drop) not glass item 0x204 (sand smelt still yields placeable 0x204 "
                             "transit), mobDied 8th param sheared=true suppresses wool drop on sheared "
                             "sheep death (unsheared control false), mobhead/sheep-woolface derived cache "
                             "filenames embed _r<revision> with per-revision stale cleanup incl. legacy "
                             "unsuffixed names (QML clamp #33 / leg covers #34 = manual visual check)";
    });

    // ── Review 2026-08-24 #4/#5 apply() 重建序 + pack 直返 URL cache-bust 探针 ──
    // #4 背景：mobhead 头像是构建期预生成（ensureBuiltLocked 以 s.revision 落盘 _r<rev>.png）；启动后首次
    //   构建走懒查询路径（revision=0、不自增）。旧序 apply() = ensureBuiltLocked()（用旧 rev 生成）→
    //   ++s.revision：第一次切包重建仍写 _r0.png 同名覆盖 → mobHeadIconSource 缓存命中直返同一 URL →
    //   QML Image 不重载（图鉴旧包头像；第二次切包写 _r1.png 才自愈）。修法 = ++s.revision 移到重建之前。
    // #5 背景：五族 pack 原文件直返（playerSkinSource 64×32 族 / entitySource / mobTextureSource /
    //   effectIconSource / paintingSource）不带 revision——同路径原地换包内容 + apply() 重解析下 URL 不变
    //   → QML 按 URL 缓存继续用旧像素（Review 2026-08-23 #12 皮肤族同款病，当时只修了皮肤一族）。修法 =
    //   packFileUrl(path, revision) 统一挂 ?r= 查询串。
    // 锁法：apply()/五族查询都持进程全局 BuiltState（读宿主机 settings/pack，不可密闭实例化——t777/t779
    //   先例），三层代替：
    //   ① #4 密闭 rig 时序模拟：懒构建(rev0) → 首次切包重建(bump 后 rev1) → 两代 URL 必不同（同 URL =
    //     QML 按 URL 缓存直返旧像素 = #4 病征本体；任意机器可跑）。
    //   ② 源序钉：直读 resourcepackmanager.cpp（滤 // 注释行后按函数切片）——apply() 体内 "++s.revision"
    //     必须先于 "ensureBuiltLocked()"（把 ++ 挪回重建之后在文本序上即时 FAIL）；五个查询函数体内必经
    //     packFileUrl（防后续再加「裸 file:/// 直返 pack 原文件」的新路径漏 cache-bust）。源文件不可读
    //     （无源部署）→ 记 note 跳过②（①③仍跑，不在无源机器上假 FAIL）。
    //   ③ #5 纯函数契约：查询串存在 / 随 revision 变 / QUrl::toLocalFile 剥离查询串（mobTextureSource
    //     羊/夜行者分支拿命中 URL 取 localFile 喂合成器靠这条——查询串不得污染文件寻址）。
    runLegMulti({ "review24 #4/#5 pack cache-bust timing: sealed pig rig lazy(rev0) vs first-apply rebuild(rev1) mo"
        "bhead URLs differ (same URL = QML URL-cache stale), packFileUrl appends ?r=<rev> that changes wi"
        "th revision and strips from QUrl::toLocalFile, source pin: apply() bumps revision BEFORE ensureB"
        "uiltLocked() rebuild + all five direct-return families route through packFileUrl+ review25 #7: i"
        "con/atlas/strip families (6 small + 2 long) bust ?r=<rev> with bare file:/// direct-returns bann"
        "ed (source pin skipped - no source tree next to exe)" }, [&]() {
        bool ok = true;
        // ① #4 时序模拟（pig rig 子目录布局，同 review-e (c) 模式）。
        QDir dR24(QDir::temp().absoluteFilePath("review24_45_probe"));
        dR24.removeRecursively();
        dR24.mkpath(".");
        QDir(dR24.absoluteFilePath("pig")).mkpath(".");
        QImage pR24(64, 32, QImage::Format_ARGB32);
        pR24.fill(QColor(0xf0, 0xa0, 0xa8));
        if (!pR24.save(dR24.absoluteFilePath("pig/pig.png"), "PNG")) {
            qInfo().noquote() << "  [r24#4 diag] failed to write temp rig PNG";
            ok = false;
        } else {
            const QString lazyUrl = generateMobHeadIconFor(1, dR24.absolutePath(), 0);  // 启动懒构建（rev0）
            const QString applyUrl = generateMobHeadIconFor(1, dR24.absolutePath(), 1); // 首次切包重建（bump 后 rev1）
            if (lazyUrl.isEmpty() || applyUrl.isEmpty() || lazyUrl == applyUrl
                    || !lazyUrl.contains(QStringLiteral("_r0.png"))
                    || !applyUrl.contains(QStringLiteral("_r1.png"))) {
                qInfo().noquote() << "  [r24#4 diag] lazy/apply generation URL pair: lazy" << lazyUrl
                                  << "apply" << applyUrl;
                ok = false;
            }
        }
        // ③ #5 纯函数契约。
        const QString pu1 = packFileUrl(QStringLiteral("E:/probe/pack/pig.png"), 7);
        const QString pu2 = packFileUrl(QStringLiteral("E:/probe/pack/pig.png"), 8);
        if (!pu1.startsWith(QStringLiteral("file:///")) || !pu1.endsWith(QStringLiteral("?r=7"))
                || pu1 == pu2
                || QUrl(pu1).toLocalFile() != QStringLiteral("E:/probe/pack/pig.png")) {
            qInfo().noquote() << "  [r24#5 diag] packFileUrl contract: pu1" << pu1 << "pu2" << pu2
                              << "localFile" << QUrl(pu1).toLocalFile();
            ok = false;
        }
        // ② 源序钉：源文件定位（exe 在 build/ → ../src；嵌套一层再 ../../ 兜底）。
        QString rpmSrcPath;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                        QStringLiteral("src/Core/resourcepackmanager.cpp")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(
                        QStringLiteral("src/Core/resourcepackmanager.cpp")),
            };
            for (const QString &c : candidates) {
                if (QFile::exists(c)) { rpmSrcPath = c; break; }
            }
        }
        bool srcChecked = false;
        if (rpmSrcPath.isEmpty()) {
            qInfo().noquote() << "  [r24 note] resourcepackmanager.cpp not found near exe - source-order "
                                 "assertions (apply bump-before-rebuild / five-family packFileUrl) skipped "
                                 "(sealed-rig + pure-contract parts still ran)";
        } else {
            QFile srcF(rpmSrcPath);
            if (!srcF.open(QIODevice::ReadOnly)) {
                ok = false;
                qInfo().noquote() << "  [r24 diag] failed to open source" << rpmSrcPath;
            } else {
                srcChecked = true;
                // 滤 // 注释行（探测目标是语句文本序，注释里的标识符会干扰 indexOf；本文件注释全为行注释）。
                QString codeText;
                const QString rawText = QString::fromUtf8(srcF.readAll());
                for (const QString &line : rawText.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//")))
                        continue;
                    codeText += line;
                    codeText += QLatin1Char('\n');
                }
                // 函数体切片：从函数头到下一个列 0 闭括号（本文件成员函数体无列 0 嵌套闭括号）。
                const auto funcBody = [&codeText](const QString &header, QString *out) -> bool {
                    const int h = codeText.indexOf(header);
                    if (h < 0)
                        return false;
                    const int end = codeText.indexOf(QStringLiteral("\n}"), h);
                    *out = codeText.mid(h, end < 0 ? 6000 : int(end) - h);
                    return true;
                };
                // #4：apply() 体内 ++s.revision 先于 ensureBuiltLocked()。
                QString applyBody;
                if (!funcBody(QStringLiteral("ResourcePackManager::apply()"), &applyBody)) {
                    ok = false;
                    qInfo().noquote() << "  [r24#4 diag] apply() body not found in source";
                } else {
                    const int bump = applyBody.indexOf(QStringLiteral("++s.revision"));
                    const int build = applyBody.indexOf(QStringLiteral("ensureBuiltLocked"));
                    if (bump < 0 || build < 0 || bump > build) {
                        ok = false;
                        qInfo().noquote() << "  [r24#4 diag] apply() order: bumpIdx" << bump
                                          << "rebuildIdx" << build
                                          << "(++s.revision must precede ensureBuiltLocked)";
                    }
                }
                // #5：五族查询函数体内必经 packFileUrl。
                const char *families[5] = {
                    "ResourcePackManager::playerSkinSource(",
                    "ResourcePackManager::entitySource(",
                    "ResourcePackManager::mobTextureSource(",
                    "ResourcePackManager::effectIconSource(",
                    "ResourcePackManager::paintingSource(",
                };
                for (const char *fam : families) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(fam), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))) {
                        ok = false;
                        qInfo().noquote() << "  [r24#5 diag] family function missing packFileUrl direct-return"
                                          << fam;
                    }
                }
                // review25 #7 扩面：图标/atlas/strip 族（itemIconSource 主返回 + 蛋/铜派生缓存 /
                //   emptyArmorSlotSource / blockItemIconSource（主返回 + 床染色族）/ atlasSource 固定名合成
                //   图集 / 四条 strip 固定名合成条带）——d6051e6 的「直返族已闭环」叙事漏掉的高频族，本批
                //   补收口。钉法分两档：7 个小函数（atlas/4 strip/emptyArmor）体短且全返回路径都该 bust →
                //   必经 packFileUrl + **禁**裸 QStringLiteral("file:///") 构造（防「主路径改了回退漏改」的
                //   半改态）；itemIconSource/blockItemIconSource 体长且含 leather/_r 文件名族的合法裸直返 →
                //   钉主返回裸直返语句的**不存在**（"file:///") + path / + foundPath 的构造被禁）。
                const char *smallFamilies[6] = {
                    "ResourcePackManager::atlasSource(",
                    "ResourcePackManager::waterStripSource(",
                    "ResourcePackManager::lavaStripSource(",
                    "ResourcePackManager::fireStripSource(",
                    "ResourcePackManager::portalStripSource(",
                    "ResourcePackManager::emptyArmorSlotSource(",
                };
                for (const char *fam : smallFamilies) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(fam), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))
                            || body.contains(QStringLiteral("QStringLiteral(\"file:///\")"))) {
                        ok = false;
                        qInfo().noquote() << "  [r25#7 diag] icon/atlas/strip family missing packFileUrl or"
                                             " still has bare file:/// construction"
                                          << fam;
                    }
                }
                const char *longFamilies[2] = {
                    "ResourcePackManager::itemIconSource(",
                    "ResourcePackManager::blockItemIconSource(",
                };
                const char *longBare[2] = {
                    "QStringLiteral(\"file:///\") + path",
                    "QStringLiteral(\"file:///\") + foundPath",
                };
                for (int i = 0; i < 2; ++i) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(longFamilies[i]), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))
                            || body.contains(QString::fromLatin1(longBare[i]))) {
                        ok = false;
                        qInfo().noquote() << "  [r25#7 diag] long icon family missing packFileUrl main return"
                                          << longFamilies[i];
                    }
                }
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24 #4/#5 pack cache-bust timing: sealed pig rig lazy(rev0) vs first-apply "
                             "rebuild(rev1) mobhead URLs differ (same URL = QML URL-cache stale), packFileUrl "
                             "appends ?r=<rev> that changes with revision and strips from QUrl::toLocalFile"
                          << (srcChecked
                                  ? ", source pin: apply() bumps revision BEFORE ensureBuiltLocked() rebuild "
                                    "+ all five direct-return families route through packFileUrl"
                                    "+ review25 #7: icon/atlas/strip families (6 small + 2 long) bust"
                                    " ?r=<rev> with bare file:/// direct-returns banned"
                                  : " (source pin skipped - no source tree next to exe)");
    });

    // ── Review 2026-08-23 #27 余烬门熄灭钩子并入 World 写入族探针 ──
    // 背景：旧熄门钩子只挂 playercontroller 挖掘路径（finishMiningAt 显式调 breakNetherPortalsAround），
    //   爆炸 / TNT 点火 / 火焚 / 流体置换等系统静默写路径拆掉门框或门面后**门面残留**（玩家肉眼：框被炸掉
    //   一角、紫门面悬空不灭）。review #27 修法 = 钩子下沉 World 写入族（setBlock×2 / setBlockSilent /
    //   clearBlockSilent / setWaterSilent / setBlockFromEntity / destroySphereSilent 逐破坏格 /
    //   dropGravityColumn 逐清格，谓词=本格原有非空内容被置换；removeNetherPortalAt 清域带
    //   m_inRemoveNetherPortal 守卫防嵌套 BFS）。本探针逐入口驱动：
    //   (a) 爆炸（destroySphereSilent r=1.5 打掉 4×5 门一角 4 门格；黑曜石框爆炸免疫——残 16 格靠钩子熄）
    //   (b) setBlockSilent 置换门格（火吞系统写代表）；(c) clearBlockSilent 拆底梁（TNT 点火清格代表）；
    //   (d) setWaterSilent 置换门格（流体蒸发 / 改道代表）；(e) 纯放置对照（旧格 Air → 不熄，机制等价
    //   MC 放置不破门）；(f) 玩家挖掘路径回归（setBlock(Air) 拆柱 —— 旧 playercontroller 显式调用的等价
    //   序列，现由 World 层钩子自覆盖）。每场景重建 4×5 Z 平面无角门（t806 ④ 无角合法）+ 点燃 20 格。
    runLegMulti({ "review-f #27 portal extinguish joined World write family: displacement of non-air cell (explosio"
        "n sphere per-voxel / setBlockSilent / clearBlockSilent / setWaterSilent / player dig setBlock) c"
        "ollapses whole connected portal domain (4x5 rig: 16 out-of-sphere cells die via hook, obsidian f"
        "rame blast-immune), pure placement into air beside a lit door keeps all 20 cells (predicate guar"
        "d), removeNetherPortalAt clear guarded against nested BFS" }, [&]() {
        World wRf27;
        wRf27.setWidth(48);
        wRf27.setDepth(48);
        wRf27.setHeight(32);
        wRf27.setSeed(13);
        bool okRf27 = true;
        const int pY27 = 20;
        const int px27 = 8, pz27 = 20; // 4×5 Z 平面门：门面 x=px27 平面，z∈[pz27,pz27+3]，y∈[pY27,pY27+4]
        // 建无角 4×5 门框（t806 buildFrame 简化版；先清场盒防地形 / 上一场景残留）。
        const auto buildPortal45 = [&]() {
            for (int c = -3; c <= 7; ++c)
                for (int r = -3; r <= 8; ++r)
                    for (int d = -2; d <= 2; ++d)
                        wRf27.setBlock(px27 + d, pY27 + r, pz27 + c, BR::Air, 0);
            for (int c = 0; c < 4; ++c) { // 底梁 / 顶梁
                wRf27.setBlock(px27, pY27 - 1, pz27 + c, BR::Obsidian, 0);
                wRf27.setBlock(px27, pY27 + 5, pz27 + c, BR::Obsidian, 0);
            }
            for (int r = 0; r < 5; ++r) { // 左右边柱
                wRf27.setBlock(px27, pY27 + r, pz27 - 1, BR::Obsidian, 0);
                wRf27.setBlock(px27, pY27 + r, pz27 + 4, BR::Obsidian, 0);
            }
        };
        // 本 rig 清场盒内门格计数（隔壁区域串数免疫）。
        const auto countPortal27 = [&]() {
            int n = 0;
            for (int c = -3; c <= 7; ++c)
                for (int r = -3; r <= 8; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (wRf27.blockAt(px27 + d, pY27 + r, pz27 + c) == BR::NetherPortal) ++n;
            return n;
        };
        // 每场景起手式：重建 + 点燃 + 核对恰 20 门格。
        const auto ignite20 = [&]() -> bool {
            buildPortal45();
            const bool lit = wRf27.tryIgniteNetherPortal(px27, pY27 + 1, pz27 + 1);
            return lit && countPortal27() == 20;
        };
        // (a) 爆炸拆角：r=1.5 球心打门面角格 (px27,pY27,pz27) → 球内门格 4（dz²+dy²≤2.25），球外 16 格
        //     旧代码残留（无钩子）、新代码被球内破坏格的逐格钩子连坐熄灭；黑曜石框爆炸免疫（skip 列表）→
        //     拆门路径 = 门格自身被清。
        {
            const bool lit = ignite20();
            const auto dv = wRf27.destroySphereSilent(px27, pY27, pz27, 1.5f);
            const int left = countPortal27();
            if (!lit || int(dv.size()) < 4 || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] explosion teardown: lit" << lit
                                  << "destroyed" << int(dv.size()) << "portal left" << left;
                okRf27 = false;
            }
        }
        // (b) setBlockSilent 置换门格（火吞可燃物等系统静默写代表）：门格被 Stone 置换 → 其余 19 格全熄。
        {
            const bool lit = ignite20();
            wRf27.setBlockSilent(px27, pY27 + 2, pz27 + 3, BR::Stone, 0);
            const int left = countPortal27();
            if (!lit || wRf27.blockAt(px27, pY27 + 2, pz27 + 3) != BR::Stone || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] setBlockSilent displace: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (c) clearBlockSilent 拆底梁中格（TNT 点火清格代表路径）：梁去 → 门面失框全熄。
        {
            const bool lit = ignite20();
            wRf27.clearBlockSilent(px27, pY27 - 1, pz27 + 1);
            const int left = countPortal27();
            if (!lit || wRf27.blockAt(px27, pY27 - 1, pz27 + 1) != BR::Air || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] clearBlockSilent beam: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (d) setWaterSilent 置换门格（流体蒸发 / 改道批量写代表）：门格被清 → 全熄。
        {
            const bool lit = ignite20();
            wRf27.setWaterSilent(px27, pY27 + 3, pz27 + 2, BR::Air, 0);
            const int left = countPortal27();
            if (!lit || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] setWaterSilent displace: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (e) 纯放置对照：门旁空气格放 Stone（旧格 Air → 钩子谓词不触发）→ 门健在恰 20（放置不破门，
        //     机制等价 MC——只有拆 / 置换才破）。防钩子过度触发回归。
        {
            const bool lit = ignite20();
            wRf27.setBlock(px27 + 1, pY27 + 2, pz27 + 1, BR::Stone, 0);
            const int left = countPortal27();
            if (!lit || left != 20) {
                qInfo().noquote() << "  [review-f #27 diag] placement control: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (f) 玩家挖掘路径回归（拆右边柱中格 setBlock(Air)，旧 playercontroller 显式序列的 World 层等价）。
        {
            const bool lit = ignite20();
            wRf27.setBlock(px27, pY27 + 2, pz27 + 4, BR::Air, 0);
            const int left = countPortal27();
            if (!lit || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] dig pillar teardown: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        if (!okRf27) ++totalFail;
        qInfo().noquote() << (okRf27 ? "PASS" : "FAIL")
                          << "| review-f #27 portal extinguish joined World write family: displacement of "
                             "non-air cell (explosion sphere per-voxel / setBlockSilent / clearBlockSilent / "
                             "setWaterSilent / player dig setBlock) collapses whole connected portal domain "
                             "(4x5 rig: 16 out-of-sphere cells die via hook, obsidian frame blast-immune), "
                             "pure placement into air beside a lit door keeps all 20 cells (predicate "
                             "guard), removeNetherPortalAt clear guarded against nested BFS";
    });

    // ── Review 2026-08-23 #28 铁砧砸伤按目标结算探针 ──
    // 背景：旧 anvilDamaged 是**落体侧一次性拍**——下落铁砧首个命中帧置位后整次下落不再结算 → 铁砧先穿
    //   玩家后落到猪身上则猪免伤、台阶两猪只伤上面那只。review #28 修法 = **按目标记账**：落体
    //   spawnSerial 写进每个被结算目标（mob 侧 Entity.anvilCrushSerial / 玩家侧 m_playerAnvilCrushSerial），
    //   同 serial 再压同目标跳过 → 真语义「每实体每次下落只伤一次」。rig：圈养猪（100HP，四邻墙防走脱）
    //   列底 + 虚拟玩家 listener 悬列中段 → 一块铁砧从 y=15 落穿两者：(P) 猪恰扣 10HP（落差 5.x..6 格 →
    //   (floor−1)×2=10；旧代码先结算玩家 → 猪 100 不动）+ 玩家恰 1 次 2HP（落差 2.x 格）；(Q) 着地后 20 帧
    //   （< 窒息首扣 ~63 帧）两者读数不动（同落体不重复结算）；(R) 第二块铁砧（新 serial）→ 玩家再结算
    //   一次（「每次下落」独立）+ 落在第一块上（着地还原链不受记账影响）。
    runLegMulti({ "review-f #28 anvil crush settles per target: one fall through virtual player band then penned pi"
        "g damages BOTH exactly once each (pig 100->92 at first-overlap frame (floor(5.1x)-1)*2=8, player"
        " 1 hit 2HP — old one-shot falling-entity flag starved the later target), readings frozen for 20 "
        "post-land frames (no re-settlement within one fall, pre-suffocation window), second anvil with f"
        "resh serial re-settles player (per-fall independence) and stacks on first (restore chain intact)" }, [&]() {
        World wRf28;
        wRf28.setWidth(48);
        wRf28.setDepth(48);
        wRf28.setHeight(32);
        wRf28.setSeed(13);
        const int cx28 = 12, cz28 = 12, ty28 = 9;
        for (int x = 10; x <= 15; ++x)
            for (int z = 10; z <= 15; ++z) {
                for (int y = 9; y <= 17; ++y) wRf28.setBlock(x, y, z, BR::Air, 0);
                wRf28.setBlock(x, 8, z, BR::Stone, 0); // 平台（猪圈底 + 铁砧落点支撑）
            }
        // 圈栏（1 高即可——aiWander 无跳跃；猪恒留落点列，盒顶恒 ty+0.9）。
        wRf28.setBlock(cx28 - 1, ty28, cz28, BR::Stone, 0);
        wRf28.setBlock(cx28 + 1, ty28, cz28, BR::Stone, 0);
        wRf28.setBlock(cx28, ty28, cz28 - 1, BR::Stone, 0);
        wRf28.setBlock(cx28, ty28, cz28 + 1, BR::Stone, 0);
        EntityManager emRf28;
        int hits28 = 0, hit28a = 0;
        QObject::connect(&emRf28, &EntityManager::mobAttackedPlayer, &emRf28,
                         [&](int amount, int, float, float) {
                             if (hits28 == 0) hit28a = amount;
                             ++hits28;
                         });
        const int pig28 = emRf28.spawnMobTyped(cx28, ty28, cz28, EntityManager::MobPig,
                                               QStringLiteral("#ffd0d0"), 100);
        const QVector3D far28(-1000.0f, 10.0f, -1000.0f);
        for (int t = 0; t < 90; ++t) // 猪落定（resting 盒顶 ty+0.9）
            emRf28.tick(0.016f, &wRf28, far28, 0.3f, 1.8f, false);
        bool okRf28 = pig28 >= 0 && emRf28.healthAt(pig28) == 100;
        // (P) 一块铁砧 y=15（fallStart 15.5）：先穿玩家带 [12,13.8]（fallDist≥2 → 2HP），后压猪顶 9.9
        //     （首个重叠帧 fallDist 5.1x → (floor 5−1)×2 = 8HP）——两目标各恰一次。
        const QVector3D listener28(float(cx28) + 0.5f, 12.0f, float(cz28) + 0.5f);
        emRf28.spawnFallingBlock(cx28, 15, cz28, int(BR::Anvil));
        int pigAtCrush28 = -1;
        for (int t = 0; t < 160; ++t) {
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
            if (pigAtCrush28 < 0 && emRf28.healthAt(pig28) < 100)
                pigAtCrush28 = emRf28.healthAt(pig28);
            if (pigAtCrush28 >= 0 && hits28 >= 1 && wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil)
                break;
        }
        okRf28 = okRf28 && pigAtCrush28 == 92      // 猪被结算（旧一次性拍：先穿玩家 → 猪恒 100）；
                                                   //   首个重叠帧落差 5.1x → (floor 5−1)×2 = 8HP
                 && hits28 == 1 && hit28a == 2     // 玩家同落体恰一次 2HP
                 && wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil; // 先伤后落：着地还原于猪格
        // (Q) 着地后 20 帧（< 窒息 63 帧首扣）读数不动：同落体不重复结算。
        for (int t = 0; t < 20; ++t)
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
        okRf28 = okRf28 && emRf28.healthAt(pig28) == pigAtCrush28 && hits28 == 1;
        // (R) 第二块铁砧（新 spawnSerial）落第一块上方：玩家再结算一次（每「次下落」独立记账）。
        emRf28.spawnFallingBlock(cx28, 15, cz28, int(BR::Anvil));
        for (int t = 0; t < 160; ++t) {
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
            if (wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil) break;
        }
        okRf28 = okRf28 && hits28 == 2 && wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil;
        if (!okRf28) {
            qInfo().noquote() << "  [review-f #28 diag] pig" << pig28 << "hp"
                              << (pig28 >= 0 ? emRf28.healthAt(pig28) : -1)
                              << "atCrush" << pigAtCrush28 << "| hits" << hits28
                              << "amt" << hit28a << "| landed"
                              << (wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil)
                              << (wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil);
        }
        if (!okRf28) ++totalFail;
        qInfo().noquote() << (okRf28 ? "PASS" : "FAIL")
                          << "| review-f #28 anvil crush settles per target: one fall through virtual "
                             "player band then penned pig damages BOTH exactly once each (pig 100->92 "
                             "at first-overlap frame (floor(5.1x)-1)*2=8, player 1 hit 2HP — old "
                             "one-shot falling-entity flag "
                             "starved the later target), readings frozen for 20 post-land frames (no "
                             "re-settlement within one fall, pre-suffocation window), second anvil with "
                             "fresh serial re-settles player (per-fall independence) and stacks on first "
                             "(restore chain intact)";
    });

    // ── Review 2026-08-23 #29 铁砧砸伤窄盒探针 ──
    // 背景：旧砸伤 XZ 判定用落体 halfW(0.5)+目标 halfW 的满格宽 → 贴格边站（视觉上铁砧 12/16 宽没碰到）
    //   也被砸。review #29 修法 = 铁砧砸伤足印独立常量 kAnvilCrushHalfW=0.375（12/16 视觉宽一半；沙 / 砾
    //   等其余落体仍 0.5）。玩家 listener halfW=0.3 → 命中阈 0.375+0.3=0.675（旧 0.8）。断言：
    //   (a) 列心偏 0.7（>0.675 新界、<0.8 旧界）→ 0 命中（旧代码必中的判别位）；(b) 偏 0.6 对照 → 恰一次
    //   2HP（窄盒内仍正常结算，落差 2.x 格）。两列各自落定还原铁砧于平台顶。
    runLegMulti({ "review-f #29 anvil crush narrow footprint: crush XZ test uses anvil visual half-width 0.375 (12/"
        "16) + listener 0.3 = 0.675 threshold — listener 0.7 off column axis untouched (old full-cell 0.8"
        " box would hit), 0.6 control hit exactly once for 2HP at 2-block fall, both anvils restore on pl"
        "atform (sand-family falling blocks keep 0.5, manual check)" }, [&]() {
        World wRf29;
        wRf29.setWidth(48);
        wRf29.setDepth(48);
        wRf29.setHeight(32);
        wRf29.setSeed(13);
        const int ty29 = 9;
        for (int x = 26; x <= 36; ++x)
            for (int z = 26; z <= 33; ++z) {
                for (int y = 9; y <= 15; ++y) wRf29.setBlock(x, y, z, BR::Air, 0);
                wRf29.setBlock(x, 8, z, BR::Stone, 0);
            }
        EntityManager emRf29;
        int hits29 = 0, hit29a = 0;
        QObject::connect(&emRf29, &EntityManager::mobAttackedPlayer, &emRf29,
                         [&](int amount, int, float, float) {
                             if (hits29 == 0) hit29a = amount;
                             ++hits29;
                         });
        // (a) 列 (30,·,28) 心 30.5；listener X 偏 +0.7（31.2）→ 新窄盒外（≥0.675）→ 0 命中。
        emRf29.spawnFallingBlock(30, 13, 28, int(BR::Anvil));
        const QVector3D outside29(31.2f, float(ty29), 28.5f);
        for (int t = 0; t < 140; ++t)
            emRf29.tick(0.016f, &wRf29, outside29, 0.3f, 1.8f, true);
        const bool okA29 = hits29 == 0 && wRf29.blockAt(30, ty29, 28) == BR::Anvil;
        const int hitsAfterA29 = hits29; // (a) 阶段末命中数（判别用）
        // (b) 列 (30,·,31) 心 30.5；listener X 偏 +0.6（31.1）→ 窄盒内（<0.675）→ 恰一次 2HP。
        emRf29.spawnFallingBlock(30, 13, 31, int(BR::Anvil));
        const QVector3D inside29(31.1f, float(ty29), 31.5f);
        for (int t = 0; t < 140; ++t)
            emRf29.tick(0.016f, &wRf29, inside29, 0.3f, 1.8f, true);
        const bool okB29 = hits29 == 1 && hit29a == 2 && wRf29.blockAt(30, ty29, 31) == BR::Anvil;
        const bool okRf29 = okA29 && okB29;
        if (!okRf29) {
            qInfo().noquote() << "  [review-f #29 diag] offset0.7 phase hits" << hitsAfterA29
                              << "landedA" << (wRf29.blockAt(30, ty29, 28) == BR::Anvil)
                              << "| offset0.6 final hits" << hits29 << "amt" << hit29a
                              << "landedB" << (wRf29.blockAt(30, ty29, 31) == BR::Anvil);
        }
        if (!okRf29) ++totalFail;
        qInfo().noquote() << (okRf29 ? "PASS" : "FAIL")
                          << "| review-f #29 anvil crush narrow footprint: crush XZ test uses anvil "
                             "visual half-width 0.375 (12/16) + listener 0.3 = 0.675 threshold — "
                             "listener 0.7 off column axis untouched (old full-cell 0.8 box would hit), "
                             "0.6 control hit exactly once for 2HP at 2-block fall, both anvils restore "
                             "on platform (sand-family falling blocks keep 0.5, manual check)";
    });

    // ── Review 2026-08-23 #30 鱿鱼笼水格刷位探针 ──
    // 背景：tickSpawners 找位谓词硬编码「air+上 air+下 solid」陆生条件 → 鱿鱼笼（生物蛋改型）复用后
    //   只能刷在陆上（搁浅鱿鱼慢爬）。review #30 修法 = 鱿鱼走水格谓词（本格+上格均 Water，免固体底）。
    //   rig：48×48 种子 9 世界（y=8 worldgen 恒实心），笼刻 MobSquid 型，唯一合格邻位 = (sx+1) 水柱两格，
    //   其余 7 水平邻显式塞 Stone 双层（陆 / 水两谓词均不满足 → 无处可刷的旧代码 0 刷判别位）。一周期
    //   tickSpawners(6.0) → 恰 1 只鱿鱼且落水柱格。
    runLegMulti({ "review-f #30 squid cage spawns in water: squid-typed spawner uses water cell predicate (cell+abo"
        "ve both Water, no solid floor needed) — exactly 1 squid in the sole 2-deep water column neighbor"
        " while all 7 other neighbors stone-filled (old land predicate would spawn nowhere / beached squi"
        "d)" }, [&]() {
        World wRf30;
        wRf30.setWidth(48);
        wRf30.setDepth(48);
        wRf30.setHeight(32);
        wRf30.setSeed(9);
        const int sx30 = 24, sy30 = 8, sz30 = 24;
        wRf30.setBlock(sx30, sy30, sz30, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobSquid));
        // (sx+1,sz) 水柱两格（鱿鱼谓词：here+above 均 Water）；其余 7 水平邻 Stone 双层（worldgen 实心再
        //   显式保险——两谓词全灭 → 旧代码必 0 刷）。
        wRf30.setBlock(sx30 + 1, sy30, sz30, BR::Water, 0);
        wRf30.setBlock(sx30 + 1, sy30 + 1, sz30, BR::Water, 0);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue; // 笼格本身
                if (dx == 1 && dz == 0) continue; // 水柱位
                wRf30.setBlock(sx30 + dx, sy30, sz30 + dz, BR::Stone, 0);
                wRf30.setBlock(sx30 + dx, sy30 + 1, sz30 + dz, BR::Stone, 0);
            }
        EntityManager emRf30;
        const QVector3D player30(float(sx30) + 0.5f, float(sy30) + 0.5f, float(sz30) + 12.5f); // XZ 12.5 < 16 激活圈
        emRf30.tickSpawners(6.0, &wRf30, player30); // 单周期（kSpawnerInterval=6s）
        int squids30 = 0;
        bool atWater30 = false;
        for (int i = 0; i < emRf30.count(); ++i) {
            if (!emRf30.aliveAt(i) || emRf30.mobTypeAt(i) != int(EntityManager::MobSquid)) continue;
            ++squids30;
            const QVector3D p = emRf30.posAt(i);
            atWater30 = atWater30 || (int(p.x()) == sx30 + 1 && int(p.y()) == sy30
                                      && int(p.z()) == sz30);
        }
        const bool okRf30 = squids30 == 1 && atWater30;
        if (!okRf30) {
            qInfo().noquote() << "  [review-f #30 diag] squids" << squids30
                              << "atWater" << atWater30 << "| count" << emRf30.count()
                              << "| waterCell"
                              << (wRf30.blockAt(sx30 + 1, sy30, sz30) == BR::Water);
        }
        if (!okRf30) ++totalFail;
        qInfo().noquote() << (okRf30 ? "PASS" : "FAIL")
                          << "| review-f #30 squid cage spawns in water: squid-typed spawner uses water "
                             "cell predicate (cell+above both Water, no solid floor needed) — exactly 1 "
                             "squid in the sole 2-deep water column neighbor while all 7 other neighbors "
                             "stone-filled (old land predicate would spawn nowhere / beached squid)";
    });

    // ── Review 2026-08-23 #31 被动笼不受敌对预算压制探针 ──
    // 背景：旧 tickSpawners 入口全局敌对 cap（hostileCount()>=kHostileMobCap=30）+ 玩家周边区域 cap 早退
    //   对**被动笼**同样生效 → 夜里敌对满 30 时猪 / 羊笼全停。review #31 修法 = 闸门按笼型分流：被动笼仅留
    //   同型 local cap（kSpawnerLocalCap）+ 总 cap（kCap）；敌对笼保留三重闸门。机制等价 MC 1.0 spawner
    //   不受 ambient hostile cap 约束。rig：96 宽世界（48 区域半径外可容远场种子），远场 (4..9,·,4..8) 预置
    //   30 只蹒腚者（XZ 距玩家 ~57 > kHostileAreaRadius=48，全局敌对预算打满），玩家旁猪笼 + 蹒腚者笼各一
    //   （邻位手工挖空 + 石底）。一周期 → 猪笼照刷恰 1（旧入口早退判别位：0 刷），蹒腚者笼被全局 cap 压制
    //   0 刷，hostileCount 恒 30。
    runLegMulti({ "review-f #31 passive cage ignores hostile budget: with global hostile pool maxed (30 far seeded "
        "shamblers >48 away, outside area radius), nearby pig cage still spawns exactly 1 (old entry gate"
        " starved ALL cages incl. passive), shambler cage correctly suppressed by hostile global cap, hos"
        "tileCount stays 30 (MC 1.0 spawner not bound by ambient hostile cap; same-type local cap + total"
        " kCap remain, manual check)" }, [&]() {
        World wRf31;
        wRf31.setWidth(96);
        wRf31.setDepth(96);
        wRf31.setHeight(48);
        wRf31.setSeed(9);
        const int py31 = 8, pz31 = 48;
        const int pigX31 = 44, shamX31 = 52; // 两笼 XZ 距玩家 (48.5,48.5) 均 <16 激活圈
        const int cages31[2] = { pigX31, shamX31 };
        for (int cxi : cages31)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue; // 笼格本身
                    wRf31.setBlock(cxi + dx, py31, pz31 + dz, BR::Air, 0);    // 陆生刷位：air×2
                    wRf31.setBlock(cxi + dx, py31 + 1, pz31 + dz, BR::Air, 0);
                    wRf31.setBlock(cxi + dx, py31 - 1, pz31 + dz, BR::Stone, 0); // + 石底（显式，同 t786 口径）
                }
        wRf31.setBlock(pigX31, py31, pz31, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobPig));
        wRf31.setBlock(shamX31, py31, pz31, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobShambler));
        EntityManager emRf31;
        for (int i = 0; i < 30; ++i) // 远场敌对种子：打满全局 cap（kHostileMobCap=30）
            emRf31.spawnHostileMob(4 + (i % 6), py31, 4 + (i / 6), EntityManager::MobShambler);
        const QVector3D player31(48.5f, 8.5f, 48.5f);
        emRf31.tickSpawners(6.0, &wRf31, player31); // 单周期
        const QVector3D pigCage31(float(pigX31) + 0.5f, 8.5f, float(pz31) + 0.5f);
        const QVector3D shamCage31(float(shamX31) + 0.5f, 8.5f, float(pz31) + 0.5f);
        const int pigs31 = emRf31.mobTypeCountNear(pigCage31, 4.0f, EntityManager::MobPig);
        const int shamsNear31 = emRf31.mobTypeCountNear(shamCage31, 4.0f, EntityManager::MobShambler);
        const bool okRf31 = emRf31.hostileCount() == 30 && shamsNear31 == 0 && pigs31 == 1;
        if (!okRf31) {
            qInfo().noquote() << "  [review-f #31 diag] pigsNear" << pigs31
                              << "shamsNear" << shamsNear31
                              << "hostiles" << emRf31.hostileCount();
        }
        if (!okRf31) ++totalFail;
        qInfo().noquote() << (okRf31 ? "PASS" : "FAIL")
                          << "| review-f #31 passive cage ignores hostile budget: with global hostile "
                             "pool maxed (30 far seeded shamblers >48 away, outside area radius), "
                             "nearby pig cage still spawns exactly 1 (old entry gate starved ALL cages "
                             "incl. passive), shambler cage correctly suppressed by hostile global cap, "
                             "hostileCount stays 30 (MC 1.0 spawner not bound by ambient hostile cap; "
                             "same-type local cap + total kCap remain, manual check)";
    });

    // ── Review 2026-08-23 #5 火蔓延抑制层探针（① 新蔓延率统计 / ② 湿燃料防火带 / ③ 火自身邻水加速自熄 /
    //    ④ 降雨露天自熄 + 屋檐对照）──
    // 背景：t804 逐邻独立掷 5%/窗（提速 6×）且全程无雨 / 水抑制 → 多火源叠加（k 火格包围每窗 1-0.95^k，
    //   k=2~3 → 10~14%/窗 ≈ 3~6s/块）下木建筑几十秒烧穿、雨天不灭火、水邻不阻蔓延（玩家误点火无反制，
    //   且烧毁 setBlock(Fire) 无掉落不可逆）。review-g 修法 = 抑制层最小版（火语义重做留 dev-plan t843，
    //   抑制判定收口 World::fireRainExposedAt / fireWaterNeighborAt 两函数供整体搬走）：
    //   ① 叠加补偿：逐邻 5% → 2.5%（kFireSpreadPermille=25‰）；② 火格露天降雨 / 自身 6 邻含水 →
    //   kFireSuppressExtinguishPct=40%/窗 加速自熄（压过燃料）+ 蔓延掷骰减半；③ 蔓延目标格 6 邻含水 →
    //   湿燃料不点燃（水格周围 1 圈 = 防火带）。
    // 四段断言（专用局部世界 seed 9，P30/P31 先例；seed 9 丘陵地形可达 y44+ 无保证净空层 → (a)-(c)
    //   rig 带自凿清空（晴天态抑制判定不看 skyLight，埋地不混淆）；(d) 扫描真天空列 + skyLight 前置）：
    // (a) 新率统计（晴天默认态，无抑制混淆源）：24 条独立泳道（火 + 贴邻木板），每窗点燃即复原木板再继续
    //     → 每泳道每窗恰 1 个 Bernoulli 样本，800 窗 × 24 = 19200 样本 → 点燃率落 [2.0%, 3.0%]（期望 2.5%，
    //     σ≈21.6/480 ≈ ±4.4σ 裕量；5% 旧值 → ~960 远超上界、t724 旧 0.83% → ~160 远低下界，两代旧率均
    //     被排除）；末窗 24 火全存活（有燃料 + 晴天 → 无自熄路径，确定性——顺带锁「无抑制不误熄」）。
    // (b) 湿燃料防火带（确定性）：火-木板-水一线（水邻木板、距火 2 格不抑制火格自身）→ 300 窗木板原样
    //     + 火仍存活（火有燃料不熄、每次掷骰被湿燃料判定拦下——非概率断言）。
    // (c) 火自身邻水加速自熄：火 6 邻含 水 + 燃料板（水在火正上方）→ 200 窗内火灭（P(幸存)=0.6^200≈1e-44；
    //     燃料板可能被濒死火余烬（减半 1.2%）点燃，新火无燃料 5%/窗亦熄 → 终态两格均非 Fire）。
    // (d) 降雨露天自熄 + 屋檐对照：tickWeather 大步长驱动状态机强制降水（Clear→降水必翻；雷态翻转回避
    //     strikeLightning 随机落点——rig 木料此时尚未放置，雷击焚木走 setBlock(Air) 不产生 Fire 格，混不进
    //     tickFire）→ 露天火（skyLightAt==15 + 该列 isPrecipitatingAt 前置校验）带燃料 200 窗内熄灭；
    //     对照：同列隔 8 格加石板屋顶（火格 skyLight<15 前置校验）→ 若火熄则其燃料板必已被吞（火只可能
    //     烧完燃料自然熄，不可能被雨杀——淋不到；杀错 = 抑制判定漏了遮挡门）。结束恢复 Clear（卫生）。
    runLegMulti({ "review-g #5 fire suppression layer: per-neighbor spread compensated 5%->2.5% (19200-sample lane "
        "statistics inside 2.0-3.0% band, all fueled fires survive clear weather), water-adjacent target "
        "never ignites (moat firebreak, deterministic), water-adjacent fire self-extinguishes fast (~40%/"
        "window suppression beats fuel), open-sky rain kills fueled fire within 200 windows while roofed "
        "control fire only dies by consuming its own fuel (skyLight<15 not rained on); suppression predic"
        "ates factored into fireRainExposedAt/fireWaterNeighborAt for t843 fire-semantics redo to adopt",
               "review25 #9 rain-saved door half survives burn-out cleanup: with rain forced upfront (big-step t"
        "ickWeather), each column burns a lone lower door half under a stone roof (roof keeps skyLight<15"
        " so no douse rolls) for 9 of 10 windows, then roof removed + paired upper half placed + sky refl"
        "ooded to 15 right before the final window so burn-out cleanup sees an un-burnt rain-exposed pair"
        "; every burnout column keeps the upper half as WoodDoor, zero WoodDoor blockBroken during the fi"
        "nal window (stray pair-clear-to-Air is the only possible broken source), >=1 burnout scenario as"
        "serted across 20 spread columns (40%/window douse roll on the now-exposed lower half is the only"
        " skip path; hashVoxel-seeded so the outcome is deterministic)" }, [&]() {
        World wG5;
        wG5.setWidth(96);
        wG5.setDepth(96);
        wG5.setHeight(48);
        wG5.setSeed(9);
        const int gy = 41; // rig 层（seed 9 丘陵地形可达 y44+——rig 带自凿清空，露天 rig 才用真天空列）
        bool okA = false, okB = false, okC = false, okD = false;
        double rateA = -1.0;
        int firesA = -1;
        QString diagA, diagD;

        // ── (a) 新蔓延率统计（24 泳道 × 800 窗；晴天默认态）──
        // seed 9 丘陵地形可达 y44+（固定「高空层」不存在）→ rig 带整片自凿清空（P31「凿进石里」先例；
        //   (a)-(c) 不依赖露天——晴天态 fireRainExposedAt 全局早退不看 skyLight）。泳道网格 8 列(x 步 4)
        //   × 3 行(z 步 3)：火(x)+板(x+1)，净空带外扩 1 格 → 泳道间互不邻接、每火恰 1 个可燃邻。
        constexpr int kLanesG5 = 24;
        const auto laneX = [](int k) { return 8 + 4 * (k % 8); };
        const auto laneZ = [](int k) { return 4 + 3 * (k / 8); };
        for (int k = 0; k < kLanesG5; ++k) {
            const int fx = laneX(k), fz = laneZ(k);
            for (int dx = -1; dx <= 2; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        wG5.setBlock(fx + dx, gy + dy, fz + dz, BR::Air, 0); // 凿净空（火/板全部 6 邻 + 上窜位）
        }
        int hitsA = 0;
        for (int k = 0; k < kLanesG5; ++k) {
            wG5.setBlock(laneX(k), gy, laneZ(k), BR::Fire, 0);       // 火源（邻木板恒有燃料 → 晴天无自熄路径）
            wG5.setBlock(laneX(k) + 1, gy, laneZ(k), BR::Planks, 0); // 贴邻木板（唯一可燃邻；泳道间距 ≥3 隔离）
        }
        for (int win = 0; win < 800; ++win) {
            for (int t = 0; t < 5; ++t) wG5.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval）
            for (int k = 0; k < kLanesG5; ++k) { // 点燃即复原（下窗再掷——每泳道每窗恰 1 样本）
                // t843 语义重做：命中 = 木板进燃烧态（isBurningAt 侧表真值，栅格 id 不变）；复原 = 同 id
                //   setBlock no-op 写（新契约：任何显式写调用含同 id 写都清燃烧侧表——见 world.cpp setBlock）。
                if (wG5.isBurningAt(laneX(k) + 1, gy, laneZ(k))) {
                    ++hitsA;
                    wG5.setBlock(laneX(k) + 1, gy, laneZ(k), BR::Planks, 0);
                }
            }
        }
        firesA = 0;
        for (int k = 0; k < kLanesG5; ++k)
            if (wG5.blockAt(laneX(k), gy, laneZ(k)) == BR::Fire) ++firesA;
        rateA = double(hitsA) / (double(kLanesG5) * 800.0);
        okA = firesA == kLanesG5 && rateA >= 0.020 && rateA <= 0.030;
        if (!okA)
            diagA = QStringLiteral("hits %1 fires %2 rate %3%")
                        .arg(hitsA).arg(firesA).arg(rateA * 100.0, 0, 'f', 2);

        // ── (b) 湿燃料防火带（确定性：水邻木板永不被点燃）── rig 行 z=80（泳道网格 z≤11 之外）
        for (int dx = 11; dx <= 15; ++dx)
            for (int dz = 79; dz <= 81; ++dz)
                for (int dy = -1; dy <= 2; ++dy)
                    wG5.setBlock(dx, gy + dy, dz, BR::Air, 0); // 净空带（火/板/水全部 6 邻 + 上窜位）
        wG5.setBlock(12, gy, 80, BR::Fire, 0);   // 火（水距火 2 格不在其 6 邻——火自身不被抑制）
        wG5.setBlock(13, gy, 80, BR::Planks, 0); // 木板（右侧邻水 → 湿燃料）
        wG5.setBlock(14, gy, 80, BR::Water, 0);  // 水（harness 不驱动 tickWaterFlow → 静止不漫）
        for (int win = 0; win < 300; ++win)
            for (int t = 0; t < 5; ++t) wG5.tickFire();
        // t843：防火带语义不变（湿燃料 igniteFlammableAt 内统一拒绝——蔓延 / 直燃三入口同判），断言加
        //   isBurningAt 反证（id 不变语义下 blockAt==Planks 单独不再充分——燃烧态也保持 Planks）。
        okB = wG5.blockAt(13, gy, 80) == BR::Planks   // 木板原样（掷骰被湿燃料判定拦下，非概率）
             && !wG5.isBurningAt(13, gy, 80)          // 且从未进燃烧态（确定性防火带）
             && wG5.blockAt(12, gy, 80) == BR::Fire;  // 火有燃料仍存活（水不在火的 6 邻）

        // ── (c) 火自身邻水加速自熄（火正上方邻水 + 侧邻燃料板）── rig 行 z=82
        for (int dx = 11; dx <= 15; ++dx)
            for (int dz = 81; dz <= 83; ++dz)
                for (int dy = -1; dy <= 3; ++dy)
                    wG5.setBlock(dx, gy + dy, dz, BR::Air, 0); // 净空带（含水悬位 gy+1 一并清）
        wG5.setBlock(12, gy, 82, BR::Fire, 0);      // 火（6 邻：正上水 + 右燃料板 → 抑制态且有燃料）
        wG5.setBlock(13, gy, 82, BR::Planks, 0);    // 燃料板（与 (b) 的水不相邻——对角非 6 邻）
        wG5.setBlock(12, gy + 1, 82, BR::Water, 0); // 水悬在火正上方（harness 不跑流体 → 不落）
        for (int win = 0; win < 200; ++win)
            for (int t = 0; t < 5; ++t) wG5.tickFire();
        okC = wG5.blockAt(12, gy, 82) != BR::Fire    // 火灭（40%/窗 × 200 窗，P(幸存)≈1e-44）
             && wG5.blockAt(13, gy, 82) != BR::Fire; // 板若被余烬点燃，新火无燃料亦熄（终态非 Fire）

        // ── (d) 降雨露天自熄 + 屋檐对照 ──
        // 强制降水：大步长 tickWeather 驱动状态机（Clear→降水必翻；Thunder=3 翻转回避雷击）。rig 木料
        //   尚未放置 → 即使路过雷态，雷击焚木 setBlock(Air) 不产生 Fire 格，混不进后续 tickFire。
        bool precip = false;
        for (int i = 0; i < 60 && !precip; ++i) {
            wG5.tickWeather(1.0e6);
            const int st = wG5.weatherState(); // World::Weather int 编码：Clear=0/Rain=1/Snow=2/Thunder=3
            precip = (st == 1 || st == 2);
        }
        // rig 列：露天组 x=12..13 / 屋檐组 x=20..21 同 z —— 两列都须正降水（biomeAt 低频，两列同群系；
        //   沙漠列恒 Clear 被跳过重扫）+ 放置格全空（露天真天空，不凿——skyLight 前置即证头顶无遮挡）。
        //   z 从 12 起（泳道网格带 z≤11）且跳过 (b)/(c) rig 行 ±1（其火 / 水不混入本 rig 邻域）。
        int zR = -1;
        for (int z = 12; z < 92 && zR < 0; ++z) {
            if (z >= 79 && z <= 83) continue; // (b)/(c) rig 行及其邻行
            if (!wG5.isPrecipitatingAt(12, z) || !wG5.isPrecipitatingAt(20, z)) continue;
            if (wG5.blockAt(12, gy, z) != BR::Air || wG5.blockAt(13, gy, z) != BR::Air) continue;
            if (wG5.blockAt(20, gy, z) != BR::Air || wG5.blockAt(21, gy, z) != BR::Air) continue;
            if (wG5.blockAt(20, gy + 2, z) != BR::Air) continue; // 屋顶位
            zR = z;
        }
        if (precip && zR >= 0) {
            wG5.setBlock(12, gy, zR, BR::Fire, 0);      // 露天火
            wG5.setBlock(13, gy, zR, BR::Planks, 0);    // 露天燃料板
            wG5.setBlock(20, gy, zR, BR::Fire, 0);      // 屋檐火（正上隔 1 格空气 + 石板）
            wG5.setBlock(21, gy, zR, BR::Planks, 0);    // 屋檐燃料板
            wG5.setBlock(20, gy + 2, zR, BR::Stone, 0); // 屋顶（火格头顶遮挡 → skyLight<15 淋不到）
            const bool preSky = wG5.skyLightAt(12, gy, zR) >= 15 && wG5.skyLightAt(20, gy, zR) < 15;
            for (int win = 0; win < 200; ++win)
                for (int t = 0; t < 5; ++t) wG5.tickFire();
            // 露天火：40%/窗 × 200 窗 → 必熄（setBlock Air）；屋檐火：不被雨杀——若熄必因燃料板已被吞
            //   （自然烧完；板被吞后无燃料 5%/窗自熄），被雨误杀 = 板仍在却火没了 → FAIL。
            okD = preSky && wG5.blockAt(12, gy, zR) == BR::Air
                 && (wG5.blockAt(20, gy, zR) == BR::Fire || wG5.blockAt(21, gy, zR) != BR::Planks);
            if (!okD)
                diagD = QStringLiteral("precip %1 zR %2 preSky %3 openFire %4 roofFire %5 roofPlank %6")
                            .arg(wG5.weatherState()).arg(zR).arg(preSky)
                            .arg(int(wG5.blockAt(12, gy, zR)))
                            .arg(int(wG5.blockAt(20, gy, zR)))
                            .arg(int(wG5.blockAt(21, gy, zR)));
            wG5.tickWeather(1.0e6); // 降水→Clear 必翻（卫生还原；后续无探针依赖，防御性）
        } else {
            diagD = QStringLiteral("precip %1 zR %2").arg(wG5.weatherState()).arg(zR);
        }

        const bool okG5 = okA && okB && okC && okD;
        if (!okG5) {
            qInfo().noquote() << "  [review-g #5 diag] A:" << okA << diagA
                              << "| B:" << okB << "| C:" << okC
                              << "| D:" << okD << diagD;
        }
        if (!okG5) ++totalFail;
        qInfo().noquote() << (okG5 ? "PASS" : "FAIL")
                          << "| review-g #5 fire suppression layer: per-neighbor spread compensated 5%->"
                             "2.5% (19200-sample lane statistics inside 2.0-3.0% band, all fueled fires "
                             "survive clear weather), water-adjacent target never ignites (moat firebreak, "
                             "deterministic), water-adjacent fire self-extinguishes fast (~40%/window "
                             "suppression beats fuel), open-sky rain kills fueled fire within 200 windows "
                             "while roofed control fire only dies by consuming its own fuel (skyLight<15 "
                             "not rained on); suppression predicates factored into "
                             "fireRainExposedAt/fireWaterNeighborAt for t843 fire-semantics redo to adopt";

    // ── Review 2026-08-25 #9 雨浇门对偶半扇守卫探针（world.cpp 门烧尽收尾补 fireRainExposedAt）──
    //   场景（review 原文）：露天门整扇点燃后某半扇被雨浇熄掷中「火灭块存」，对偶随后烧尽时收尾守卫旧只判
    //   !fireWaterNeighborAt → 被雨救下的半扇仍被连带清 Air + 误发 blockBroken（水泼保得住、雨浇保不住——
    //   抑制源行为不对称）。确定性构造走「点燃时无对偶、后补放的半门」收尾面（world.cpp (d) 注释自列；避开
    //   双半扇同窗浇熄竞态掷骰）：① 转降雨（tickWeather 大步长，review-g #5 (d) 同款；rig 木料未放 → 雷态
    //   路过无焚木混淆）后布列——各列全列凿空（seed 9 地形可达 y44+，t769 教训不假设高空必空）+ 石板屋顶
    //   （gy+2，挡天光 → 下扇整程不被雨掷浇熄）+ 门下扇（上格 Air → 联动不触发，只点下扇，10 窗计时）；
    //   ② 烧 9/10 窗（45 tickFire；屋顶护体 → 降雨下零浇熄掷骰）；③ 撤顶（skyLight 重 flood 回 15）+ 后补
    //   门上扇（bit3=1，非燃烧态——守卫的判击对象）→ 第 10 窗下扇烧尽收尾查对偶：仍是门 / 未在燃 / 无水邻 /
    //   露天真（fireRainExposedAt）→ 修复后半扇 WoodDoor 原样保住，未修则连带 Air + blockBroken。
    //   断言：① burnout 列（下扇非 WoodDoor = 烧尽路径；第 10 窗 40% 浇熄掷中的存活列不算）上扇一律仍
    //   WoodDoor；② 第 10 窗 WoodDoor blockBroken 恒 0（烧毁走 setBlock(Fire) 放置语义无 broken → 对偶
    //   连带 Air 是唯一 broken 源，守卫的直证）；③ 场景 ≥1 列（多列铺开压浇熄掷骰；hashVoxel 固定 seed →
    //   结果确定性，本轮实测锁定）。非降水列 / 撤顶后仍不露天列被前置校验剔除（守卫本就不该触发）。
    {
        World wR9;
        wR9.setWidth(96); wR9.setDepth(96); wR9.setHeight(48); wR9.setSeed(9);
        const int gy9 = 41; // rig 层（review-g #5 同款 seed 9 布局带）
        constexpr int kColsR9 = 20;
        const auto colXR9 = [](int k) { return 8 + 3 * k; }; // 列距 3 → 门互不 6 邻（同态蔓延不串列）
        // 先转降雨（rig 木料未放，雷态路过只可能落自然火——列布局后再无 tickWeather 调用）。
        for (int i = 0; i < 60; ++i) {
            wR9.tickWeather(1.0e6);
            const int st = wR9.weatherState();
            if (st == 1 || st == 2) break; // Rain/Snow（Thunder=3 继续翻——rig 未放木料无雷击焚木面）
        }
        // z 带：20 列里降水覆盖 ≥8 才用（biomeAt 低频，成带存在；isPrecipitatingAt 全局 Clear 恒 false →
        //   必须在降雨态下扫，review-g #5 (d) 先例）。
        int zR9 = -1;
        for (int z = 12; z < 92 && zR9 < 0; z += 2) {
            int n = 0;
            for (int k = 0; k < kColsR9; ++k)
                if (wR9.isPrecipitatingAt(colXR9(k), z)) ++n;
            if (n >= 8) zR9 = z;
        }
        bool okKeep = false, okNoBreak = false, okScenario = false;
        int burnouts9 = 0, used9 = 0;
        QString diag9 = QStringLiteral("precip z not found");
        if (zR9 >= 0) {
            diag9.clear();
            // ① 布列：全列凿空（gy-1..47——屋顶以上到世界顶全清，保证撤顶后列内天光直达）→ 屋顶 → 门下扇 → 点燃。
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int y = gy9 - 1; y <= 47; ++y)
                            wR9.setBlock(x + dx, y, zR9 + dz, BR::Air, 0);
                wR9.setBlock(x, gy9 + 2, zR9, BR::Stone, 0); // 屋顶（skyLight<15 → 下扇不吃雨浇熄掷骰）
                wR9.setBlock(x, gy9, zR9, BR::WoodDoor, 0);  // 门下扇（bit3=0；上格 Air → 联动不触发）
                wR9.igniteFlammableAt(x, gy9, zR9);
            }
            // ② 烧 9/10 窗：计时 10→1（晴天 / 屋顶双保险下零浇熄路径）。
            for (int t = 0; t < 45; ++t) wR9.tickFire();
            // ③ 撤顶 + 后补上扇（此刻上下扇皆露天真——下扇仅剩 1 窗计时，第 10 窗才有浇熄掷骰 40%/列）。
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                wR9.setBlock(x, gy9 + 2, zR9, BR::Air, 0);
                wR9.setBlock(x, gy9 + 1, zR9, BR::WoodDoor, 8);
            }
            // 前置校验（剔除非降水 / 不露天 / 下扇已非燃 / 上扇已燃的列——守卫对它们本就不该触发）。
            bool part9[kColsR9] = {};
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                part9[k] = wR9.isPrecipitatingAt(x, zR9)
                           && wR9.skyLightAt(x, gy9 + 1, zR9) >= 15
                           && wR9.blockAt(x, gy9, zR9) == BR::WoodDoor
                           && wR9.isBurningAt(x, gy9, zR9)
                           && wR9.blockAt(x, gy9 + 1, zR9) == BR::WoodDoor
                           && !wR9.isBurningAt(x, gy9 + 1, zR9);
                if (part9[k]) ++used9;
            }
            int doorBreaks9 = 0; // 只计第 10 窗（布列 / 撤顶 / 后补的 setBlock 无 WoodDoor→Air；清理在计数窗外）
            QObject::connect(&wR9, &World::blockBroken, &wR9,
                             [&](int, int, int, int oldId) { if (oldId == int(BR::WoodDoor)) ++doorBreaks9; });
            for (int t = 0; t < 5; ++t) wR9.tickFire(); // 第 10 窗：浇熄掷骰（40%）或烧尽收尾
            okKeep = true;
            for (int k = 0; k < kColsR9; ++k) {
                if (!part9[k]) continue;
                const int x = colXR9(k);
                if (wR9.blockAt(x, gy9, zR9) != BR::WoodDoor) { // 烧尽（余烬 Fire / Air 均非门）
                    ++burnouts9;
                    if (wR9.blockAt(x, gy9 + 1, zR9) != BR::WoodDoor) okKeep = false; // 对偶被连带清 = 守卫缺
                }
            }
            okNoBreak = doorBreaks9 == 0;
            okScenario = burnouts9 >= 1;
            if (!okKeep || !okNoBreak || !okScenario)
                diag9 = QStringLiteral("zR9 %1 used %2 burnouts %3 breaks %4 keep %5")
                            .arg(zR9).arg(used9).arg(burnouts9).arg(doorBreaks9).arg(okKeep);
            // 清理（探针世界即弃；计数已收口）
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                wR9.setBlock(x, gy9, zR9, BR::Air, 0);
                wR9.setBlock(x, gy9 + 1, zR9, BR::Air, 0);
            }
        }
        const bool okR9 = okKeep && okNoBreak && okScenario;
        if (!okR9) ++totalFail;
        qInfo().noquote() << (okR9 ? "PASS" : "FAIL")
                          << "| review25 #9 rain-saved door half survives burn-out cleanup: with rain forced "
                             "upfront (big-step tickWeather), each column burns a lone lower door half under "
                             "a stone roof (roof keeps skyLight<15 so no douse rolls) for 9 of 10 windows, "
                             "then roof removed + paired upper half placed + sky reflooded to 15 right "
                             "before the final window so burn-out cleanup sees an un-burnt rain-exposed "
                             "pair; every burnout column keeps the upper half as WoodDoor, zero WoodDoor "
                             "blockBroken during the final window (stray pair-clear-to-Air is the only "
                             "possible broken source), >=1 burnout scenario asserted across 20 spread "
                             "columns (40%/window douse roll on the now-exposed lower half is the only "
                             "skip path; hashVoxel-seeded so the outcome is deterministic)";
    }
    });

    // ── P-t843 可燃物直燃语义重做探针（专用局部世界 wT，P30/P31/review-g#5 先例）──
    //   dev-plan t843：打火石右键可燃方块 = 方块本身点燃（燃烧态 = World 侧表 m_burningCells 瞬态，
    //   栅格 id 不变；面火 overlay 走 blockIgnited → QML burningHost）；燃烧计时归零 → 烧毁无掉落
    //   （t724 语义保持）；蔓延 = 相邻可燃进同态（火格掷骰 + 燃烧格同态掷骰）；批 G 三抑制谓词整体
    //   接入（湿燃料防火带收口在 igniteFlammableAt 三入口统一拒绝；燃烧中变湿 = 浇熄火灭块存）；
    //   接触燃烧方块 = 着火（mob 侧实证；玩家侧同款三格判定复制在 PlayerController::step——Game 层
    //   直编不可达（captured 物理闸门 + 私有 step，P20 注释先例），同构代码 + 人工目视收口）；3D 立
    //   地火失撑 = 编辑钩子同调用内立即熄灭。
    //   wT：48×48×96 seed 21，gy=88 高空净空层（heightAt ∈57..71 + 树冠 ~+10，88 全空免凿；review-g#5
    //   凿净空先例在此免除——Air→Air 早退不重算光照）。六段断言：
    //   (a) 直燃进态 + 精确计时 + 非可燃拒：ignite(Planks) 真 / isBurningAt 真 / id 保留 / 重复 ignite
    //       false（幂等不重置）；ignite(Stone) false（打火石回退立地火路径的 World 侧判据）；恰 9 窗
    //       仍在燃（计时 10→1）、第 10 窗烧毁 → cap 内燃起余烬火（blockAt==Fire）→ 余烬无燃料 5%/窗
    //       自熄（≤600 窗终态 Air，P(幸存)≈0.95^600≈4e-14）；
    //   (b) 湿燃料防火带（确定性）：水邻木板 ignite 拒（false）+ 300 窗从未燃烧 + 块存；燃烧中变湿
    //       浇熄（火灭块存）：先点燃**后**贴水（顺序即语义：贴水在先会被防火带拒）→ 40%/窗浇熄（双板
    //       60 窗均灭 P≈(0.6^60)²≈3e-27；烧穿尾部 P(0.6^10)≈0.6%/板，双板同穿≈3.6e-5 → 断言 ≥1 板
    //       块存）；
    //   (c) 同态蔓延存在性：10 泳道（燃板 A + 贴邻新板 B，全场无 Fire 格——纯燃烧格掷骰）每窗复原 B +
    //       重燃 A → 400 窗 ≥1 次 B 被燃板点燃（P(全未中)=0.975^4000≈3e-44；率值统计已由 review-g#5(a)
    //       锁定，此处只证燃烧格自身掷骰通路存在）；
    //   (d) mob 接触着火：石框围 1×1 燃烧木板上的猪（脚下一格 footY-1 命中）→ 40 tick（< 首次火伤结算
    //       1s → 无随机熄灭 / 伤害混淆）内 EntityManager::isBurningAt(猪) 真；
    //   (e) 立地火失撑即时灭 + 有撑对照：破火下石支撑 → **同一 setBlock 调用内**火格变 Air
    //       （checkFireOnEdit 编辑钩子，无 tickFire 参与——即时性本身是断言）；对照（石撑 + 邻燃料板）
    //       1 窗后仍 Fire（有燃料火无自熄掷骰路径——确定性非概率）；
    //   (f) 燃烧中替换 / 同 id 复原清态：燃板 setBlock(Stone) 替换 → isBurningAt 假；再点燃后同 id
    //       setBlock(Planks) no-op 写 → isBurningAt 假（review-g#5(a) 每窗复原所依赖契约的显式锁定）。
    runLegMulti({ "t843 direct-ignite fire semantics: flint right-click on flammable enters burning state with id p"
        "reserved (side-table transient, surface-fire overlay via blockIgnited), exact 10-window wood tim"
        "er (9 windows burning, 10th burns to ember flare, ember dies fuel-less), stone rejected (falls b"
        "ack to standing fire), wet-fuel firebreak rejected at ignite (unified 3-entry gate) while burnin"
        "g-turned-wet douses with block intact, burning-cell same-state spread proven without any fire ce"
        "ll, mob on burning plank ignites within 40 ticks (player leg = same predicate copy in step, manu"
        "al visual), standing fire loses support = extinguished inside the same setBlock call (checkFireO"
        "nEdit hook) while fueled+supported control survives deterministically, replace/same-id-noop writ"
        "e clears burning state" }, [&]() {
        World wT;
        wT.setWidth(48);
        wT.setDepth(48);
        wT.setHeight(96);
        wT.setSeed(21);
        const int gy = 88;
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        QString diagT843;
        const auto win843 = [&wT](int n) { for (int i = 0; i < n; ++i) for (int t = 0; t < 5; ++t) wT.tickFire(); };

        // ── (a) 直燃进态 + 精确计时（gy 层 z=6 行 x=7 板 / x=9 石对照）──
        {
            const int px = 7, pz = 6;
            wT.setBlock(px, gy - 1, pz, BR::Stone, 0);  // 石台（余烬火支撑 + 与后段隔离）
            wT.setBlock(px, gy, pz, BR::Planks, 0);
            wT.setBlock(9, gy, pz, BR::Stone, 0);       // 非可燃对照（距板 2 格非 6 邻）
            const bool igFirst = wT.igniteFlammableAt(px, gy, pz);
            const bool idKept = wT.blockAt(px, gy, pz) == BR::Planks;
            const bool igAgain = wT.igniteFlammableAt(px, gy, pz);   // 重复 → false（幂等）
            const bool igStone = wT.igniteFlammableAt(9, gy, pz);    // 石 → false（回退立地火路径）
            win843(9);                                                // 计时 10→1：恰 9 窗仍在燃
            const bool stillBurningAt9 = wT.isBurningAt(px, gy, pz)
                                         && wT.blockAt(px, gy, pz) == BR::Planks;
            win843(1);                                                // 第 10 窗：归零烧毁 → 余烬火
            const bool burntAt10 = !wT.isBurningAt(px, gy, pz)
                                   && wT.blockAt(px, gy, pz) == BR::Fire;
            win843(600);                                              // 余烬无燃料 5%/窗自熄
            const bool emberDied = wT.blockAt(px, gy, pz) == BR::Air;
            okA = igFirst && idKept && !igAgain && !igStone && stillBurningAt9 && burntAt10
                  && emberDied && wT.blockAt(9, gy, pz) == BR::Stone;
            wT.setBlock(px, gy - 1, pz, BR::Air, 0);
            wT.setBlock(9, gy, pz, BR::Air, 0);
        }

        // ── (f) 燃烧中替换 / 同 id 复原清态（z=10 行）──
        {
            const int px = 7, pz = 10;
            wT.setBlock(px, gy, pz, BR::Planks, 0);
            wT.igniteFlammableAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Stone, 0);      // 替换 → 燃烧作废
            const bool clearedByReplace = !wT.isBurningAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Planks, 0);     // 换回木 + 重新点燃
            const bool reignited = wT.igniteFlammableAt(px, gy, pz) && wT.isBurningAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Planks, 0);     // 同 id no-op 写 → 复原契约：燃烧清除
            const bool clearedByNoop = !wT.isBurningAt(px, gy, pz);
            okF = clearedByReplace && reignited && clearedByNoop;
            wT.setBlock(px, gy, pz, BR::Air, 0);
        }

        // ── (b) 湿燃料防火带 + 燃烧中变湿浇熄（z=6 行）──
        {
            // (b1) 防火带：水邻木板直燃被拒（确定性——三入口统一口径收口在 igniteFlammableAt）。
            wT.setBlock(16, gy, 6, BR::Planks, 0);
            wT.setBlock(17, gy, 6, BR::Water, 0);       // 木板 6 邻含水 → 湿燃料
            const bool wetRejected = !wT.igniteFlammableAt(16, gy, 6)
                                     && !wT.isBurningAt(16, gy, 6);
            win843(300);                                // 无燃格 → tickFire 早退，窗口空转（保序）
            const bool wetNeverBurnt = !wT.isBurningAt(16, gy, 6)
                                       && wT.blockAt(16, gy, 6) == BR::Planks;
            // (b2) 浇熄（火灭块存）：先点燃（此刻无水——顺序即语义）后贴水 → 40%/窗浇熄掷骰。
            wT.setBlock(22, gy, 6, BR::Planks, 0);
            wT.setBlock(26, gy, 6, BR::Planks, 0);
            const bool d1 = wT.igniteFlammableAt(22, gy, 6);
            const bool d2 = wT.igniteFlammableAt(26, gy, 6);
            wT.setBlock(23, gy, 6, BR::Water, 0);
            wT.setBlock(27, gy, 6, BR::Water, 0);
            bool bothOut = false;
            for (int win = 0; win < 60 && !bothOut; ++win) {
                win843(1);
                bothOut = !wT.isBurningAt(22, gy, 6) && !wT.isBurningAt(26, gy, 6);
            }
            const int intact = int(wT.blockAt(22, gy, 6) == BR::Planks)
                               + int(wT.blockAt(26, gy, 6) == BR::Planks);
            okB = wetRejected && wetNeverBurnt && d1 && d2 && bothOut && intact >= 1;
            wT.setBlock(16, gy, 6, BR::Air, 0);
            wT.setBlock(17, gy, 6, BR::Air, 0);
            wT.setBlock(22, gy, 6, BR::Air, 0);
            wT.setBlock(23, gy, 6, BR::Air, 0);
            wT.setBlock(26, gy, 6, BR::Air, 0);
            wT.setBlock(27, gy, 6, BR::Air, 0);
        }

        // ── (c) 同态蔓延存在性（z=14 行，10 泳道 A@4+3k / B@A+1，全场无 Fire 格）──
        {
            constexpr int kLanesT = 10;
            int hits = 0;
            for (int k = 0; k < kLanesT; ++k) {
                wT.setBlock(4 + 3 * k, gy, 14, BR::Planks, 0);
                wT.setBlock(5 + 3 * k, gy, 14, BR::Planks, 0);
                wT.igniteFlammableAt(4 + 3 * k, gy, 14); // A 初始入燃态
            }
            for (int win = 0; win < 400; ++win) {
                win843(1);
                for (int k = 0; k < kLanesT; ++k) {
                    if (wT.isBurningAt(5 + 3 * k, gy, 14)) { // B 被燃板 A 点燃 → 计数 + 复原
                        ++hits;
                        wT.setBlock(5 + 3 * k, gy, 14, BR::Planks, 0);
                    }
                    wT.setBlock(4 + 3 * k, gy, 14, BR::Planks, 0); // 复原 A + 重燃（计时恒 10，
                    wT.igniteFlammableAt(4 + 3 * k, gy, 14);       // 每窗恰 1 次掷骰样本）
                }
            }
            okC = hits >= 1;
            for (int k = 0; k < kLanesT; ++k) { // 清场（含清燃烧态）
                wT.setBlock(4 + 3 * k, gy, 14, BR::Air, 0);
                wT.setBlock(5 + 3 * k, gy, 14, BR::Air, 0);
            }
        }

        // ── (d) mob 接触着火（z=6 行 x=38：石框围 1×1 燃烧木板上的猪）──
        {
            EntityManager entsT;
            wT.setBlock(38, gy, 6, BR::Planks, 0);
            wT.igniteFlammableAt(38, gy, 6); // 燃烧板（ents.tick 不驱动 tickFire → 计时冻结，接触源稳定）
            for (int dx = 37; dx <= 39; ++dx)
                for (int dz = 5; dz <= 7; ++dz) { // 石框（gy+1 环 + gy+2 盖：猪定身在燃烧板正上）
                    if (dx == 38 && dz == 6) continue;
                    wT.setBlock(dx, gy + 1, dz, BR::Stone, 0);
                    wT.setBlock(dx, gy + 2, dz, BR::Stone, 0);
                }
            const int pig = entsT.spawnMobTyped(38, gy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#ee9999"), 20);
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            for (int t = 0; t < 40; ++t) // 0.64s：首个 aiTick ≤4 帧；首次火伤 1s 后 → 无伤害 / 随机熄灭混淆
                entsT.tick(0.016f, &wT, farListener, 0.3f, 1.8f, false);
            okD = pig >= 0 && entsT.isBurningAt(pig) && entsT.aliveAt(pig)
                  && wT.isBurningAt(38, gy, 6); // 板仍在燃（接触源未耗尽）
            for (int dx = 37; dx <= 39; ++dx)
                for (int dz = 5; dz <= 7; ++dz)
                    for (int dy = 0; dy <= 2; ++dy)
                        wT.setBlock(dx, gy + dy, dz, BR::Air, 0); // 清场（燃烧板 + 石框，dy=0 含燃烧态清除）
        }

        // ── (e) 立地火失撑即时灭（编辑钩子）+ 有撑有燃料对照（z=14 行 x=36/40）──
        {
            wT.setBlock(36, gy - 1, 14, BR::Stone, 0);
            wT.setBlock(36, gy, 14, BR::Fire, 0);       // 石撑立地火
            wT.setBlock(36, gy - 1, 14, BR::Air, 0);    // 破支撑 → 同调用内 checkFireOnEdit 即时灭
            const bool instantOut = wT.blockAt(36, gy, 14) == BR::Air;
            wT.setBlock(40, gy - 1, 14, BR::Stone, 0);
            wT.setBlock(40, gy, 14, BR::Fire, 0);       // 对照：有撑 + 邻燃料板
            wT.setBlock(41, gy, 14, BR::Planks, 0);
            win843(1);                                  // 1 窗：有燃料 → 无自熄掷骰（确定性存活）
            const bool ctrlAlive = wT.blockAt(40, gy, 14) == BR::Fire;
            okE = instantOut && ctrlAlive;
            wT.setBlock(40, gy - 1, 14, BR::Air, 0);
            wT.setBlock(40, gy, 14, BR::Air, 0);
            wT.setBlock(41, gy, 14, BR::Air, 0);
        }

        const bool okT843 = okA && okB && okC && okD && okE && okF;
        if (!okT843) {
            diagT843 = QStringLiteral("A:%1 B:%2 C:%3 D:%4 E:%5 F:%6")
                           .arg(okA).arg(okB).arg(okC).arg(okD).arg(okE).arg(okF);
            qInfo().noquote() << "  [t843 diag]" << diagT843;
        }
        if (!okT843) ++totalFail;
        qInfo().noquote() << (okT843 ? "PASS" : "FAIL")
                          << "| t843 direct-ignite fire semantics: flint right-click on flammable "
                             "enters burning state with id preserved (side-table transient, "
                             "surface-fire overlay via blockIgnited), exact 10-window wood timer "
                             "(9 windows burning, 10th burns to ember flare, ember dies fuel-less), "
                             "stone rejected (falls back to standing fire), wet-fuel firebreak "
                             "rejected at ignite (unified 3-entry gate) while burning-turned-wet "
                             "douses with block intact, burning-cell same-state spread proven "
                             "without any fire cell, mob on burning plank ignites within 40 ticks "
                             "(player leg = same predicate copy in step, manual visual), standing "
                             "fire loses support = extinguished inside the same setBlock call "
                             "(checkFireOnEdit hook) while fueled+supported control survives "
                             "deterministically, replace/same-id-noop write clears burning state";
    });

    // ── t813 构建版本戳探针（Core 叶子直编）：锁 BuildInfo 两值非空 + 格式 ──
    //   stamp = CMake 每次 build 生成的 "YYYY-MM-DD HH:MM"（16 字符）；gitHash = git
    //   rev-parse --short HEAD（7-10 hex）。git 缺失 / 超时（TIMEOUT 5）回退 "nogit" 判 **SKIP**
    //   （review24 低危：旧版硬套 hex 正则判 FAIL，与 WriteBuildStamp.cmake「无 git 诚实回退、构建不因此
    //   失败」自相矛盾——无 .git 源码导出包 / CI 缓存构建会矩阵恒红；环境缺失≠格式回归，SKIP 单列不算
    //   FAIL 不算 PASS）；stamp 格式恒断（真回归照 FAIL）——SKIP 行也带 stamp 值供人工核。探针跑在矩阵
    //   测试 exe 里 = 顺带验证「该 exe 的 stamp 随本次构建刷新」（构建-运行同刻，分钟差即重建链生效证据）。
    runLegMulti({ "t813 build stamps: stamp(YYYY-MM-DD HH:MM) git(7-10 hex, git rev-parse --short HEAD); header reg"
        "enerated every build via cmake/WriteBuildStamp.cmake with content-change-only rewrite so only bu"
        "ildinfo.cpp recompiles; git==\"nogit\" judged SKIP (CMake honest fallback = environment without "
        "git, not a format regression; stamp format still asserted, stamp break stays FAIL)" }, [&]() {
        const QString stamp = BuildInfo::instance()->stamp();
        const QString ghash = BuildInfo::instance()->gitHash();
        const QRegularExpression stampRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"));
        const QRegularExpression gitRe(QStringLiteral("^[0-9a-f]{7,10}$"));
        const bool okStamp = stampRe.match(stamp).hasMatch();
        const bool noGit = ghash == QStringLiteral("nogit"); // CMake 诚实回退值（无 .git / git 超时）
        const bool okGit = gitRe.match(ghash).hasMatch();
        const bool okT813 = okStamp && (noGit || okGit); // stamp 破 = 真回归仍 FAIL；仅 git 缺失 → SKIP
        if (!okT813) ++totalFail;
        qInfo().noquote() << ((noGit && okStamp) ? "SKIP" : (okT813 ? "PASS" : "FAIL"))
                          << "| t813 build stamps: stamp" << stamp
                          << "(YYYY-MM-DD HH:MM) git" << ghash
                          << "(7-10 hex, git rev-parse --short HEAD); header regenerated every "
                             "build via cmake/WriteBuildStamp.cmake with content-change-only "
                             "rewrite so only buildinfo.cpp recompiles; git==\"nogit\" judged SKIP "
                             "(CMake honest fallback = environment without git, not a format "
                             "regression; stamp format still asserted, stamp break stays FAIL)";
    });

    // ── P-t814 真消费端执行探针（Game 层 PlayerController 直编）──
    //   用户报告（R19.13）：「激活红石粉/红石块/红石火把都不能点燃 TNT、不能触发发射器/投掷器」，但
    //   t772（42 组合）/t773（3 探针）全 PASS 且用户同时确认铁门/铁活板门能开。分叉实证：铁门/铁活板门是
    //   recomputePowerLocal **静默写 state**（纯 World 内闭环），TNT/发射器/投掷器走 **信号→QML 转发→
    //   player.firePowerTnt/fireDispenserAtQml** —— 此前矩阵只对 World 原始信号计数（P15）/镜像 clearBlockSilent
    //   （t773），**真 PlayerController 消费方法从未被任何自动化测试执行过**（QML 侧仅静态审计）。本探针把
    //   Game 层 PlayerController 连同 EntityManager/DispenserStore/ItemEntityManager 装进矩阵，以 C++ 直接
    //   连接精确镜像 Main.qml 双转发 handler（Connections{target:theWorld} 同线程直接调用语义），驱动用户
    //   实测路径（器件先就位 → 后激活源）断言端到端效果：
    //   (a) 拉杆扳开 → 邻 TNT：方块被 clearBlockSilent 清 + EntityManager 生 PrimedTnt 实体（格心坐标）；
    //   (b) 红石块贴发射器（store 预填 3 箭）→ spawnArrowPlayer 生 1 箭 + 库存 3→2（右键 UI 装填后电力触发
    //       的完整用户路径）；
    //   (c) 拉杆贴投掷器（store 预填 5 粉）→ ItemEntityManager 生 1 掉落物 + 库存 5→4（dropper spawnItemAt
    //       分支——m_itemEntities 注入态）；
    //   (d) 空库存发射器通电 → 无任何实体生成（t607「空库存玩家机器无动作」设计语义，防误判为缺陷）；
    //   (e) 沿语义：稳定通电下再制造电力活动（另一侧拉杆扳开再扳回）→ 不重复发射（fireDispenserAtQml 基线
    //       集 unpowered→powered 沿检测，t689）；源拆除再快速复置 → 冷却窗内仍不发射（per-dispenser 2s 闸）。
    //   任一 FAIL = 用户症状在消费端的复现点（World 层已由 P15/t773 洗冤）；全 PASS = 链完整，用户复测走
    //   docs/test-reports/t814-redstone-repro-steps.md（版本戳核对 + 逐源×逐器件最简搭建）。
    runLegMulti({ "t814 real-consumer probes: Main.qml forwarding mirrored onto actual PlayerController.firePowerTn"
        "t/fireDispenserAtQml (lever->TNT clears block + spawns primed entity at cell center; redstone-bl"
        "ock->dispenser w/ 3 arrows fires 1 + decrements to 2; lever->dropper w/ 5 dust pops 1 item entit"
        "y + decrements to 4; empty tracked dispenser powered = design no-op; stable-power re-touch and s"
        "ub-0.5s-cooldown re-power both do not re-fire, each zero-fire leg gated by powerDispenserTrigger"
        "ed emission-count delta so cooldown-block vs signal-lost are distinguished; cooldown driven past"
        " 0.5s expiry via scanDispenserTraps equivalent-decrement then a true re-edge MUST re-fire +1 arr"
        "ow/stock 2->1, pinning the cooldown duration both directions) - consumer leg never executed by P"
        "15/t773 before, iron-door contrast explained (door = in-World state write, TNT/dispenser = signa"
        "l->QML->consumer)" }, [&]() {
        PlayerController pc;          // 真消费端（Game 层；C++ 直造不启 16ms 定时器——componentComplete 不触发）
        EntityManager ents;           // spawnPrimedTnt / spawnArrowPlayer 断言源（不 tick → 实体冻结可数）
        DispenserStore store;         // 库存分派断言源
        ItemEntityManager items;      // 投掷器 spawnItemAt 掉落物断言源
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        pc.setItemEntities(&items);
        // Main.qml 双转发 handler 的 C++ 等价镜像（src/ui/Main.qml onPowerTntTriggered → player.firePowerTnt /
        //   onPowerDispenserTriggered → player.fireDispenserAtQml；同线程直接连接 = QML handler 语义）。
        QObject::connect(&w, &World::powerTntTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.firePowerTnt(x, y, z); });
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });

        // (a) TNT：拉杆贴合（器件先就位稳态 → 后扳拉杆，用户实测路径）。器件放置走 placeRigBlock
        //     （回读钉落位——setBlock 越界 / 同 id 早退都静默吞放置，review24 低危）。
        bool okA = false;
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::TntBlock, 0);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::Lever, 1); // 扳开（state bit0=1）
            tickN(w, 4);
            int primedIdx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) { primedIdx = i; break; }
            okA = w.blockAt(x0, kRigY, z0) == BR::Air                    // firePowerTnt 清块
                  && primedIdx >= 0                                     // PrimedTnt 实体在场
                  && std::abs(ents.posAt(primedIdx).x() - (x0 + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(primedIdx).y() - (kRigY + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(primedIdx).z() - (z0 + 0.5f)) < 1e-3f;
            if (!okA)
                qInfo().noquote() << "  [t814 a diag] block=" << int(w.blockAt(x0, kRigY, z0))
                                  << " primedIdx=" << primedIdx << " ents.count=" << ents.count();
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            ents.clearAll(); // 清引燃实体（防污染后续探针的实体计数）
            tickN(w, 2);
        }

        // (b) 发射器：红石块贴合 + store 预填 3 箭（右键 UI 装填后电力触发的完整路径）。
        bool okB = false, okBInv = false;
        int arrowsAfterB = -1;
        int bx0 = 0, bz0 = 0;
        {
            const auto [x0, z0] = nextSlot();
            bx0 = x0; bz0 = z0;
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 3);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            int arrows = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows;
            arrowsAfterB = arrows;
            okB = arrows == 1;
            okBInv = store.slotIdAt(x0, kRigY, z0, 0) == RecipeRegistry::ArrowId
                     && store.slotCountAt(x0, kRigY, z0, 0) == 2;
            if (!okB || !okBInv)
                qInfo().noquote() << "  [t814 b diag] arrows=" << arrows
                                  << " slotId=" << store.slotIdAt(x0, kRigY, z0, 0)
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
        }

        // (e) 沿语义 + 冷却**时间**语义（review24 #9 重做——旧断言两层误 PASS 面：① 探针不启 16ms 定时器
        //     → 冷却递减（tick→scanDispenserTraps 开头）从不运行 → 「sub-2s-cooldown」实际只验 contains 即拦，
        //     kDispenserCooldown 回归改 0 探针照样 PASS；② arrows 持平在「信号根本没发」时也恒真）。三段：
        //     ① 稳定通电下再制造电力活动（对侧拉杆扳开→扳回）不重复发射（沿检测基线集 t689）；
        //     ② 拆源→快速复置 = 真上升沿但 <1s 冷却 → 不发射，**且断言 powerDispenserTriggered 信号确有发出**
        //        （头部全局 dispFired 计数差分——区分「冷却拦截」与「信号未发」两种零发射）；
        //     ③ 直调 pc.scanDispenserTraps(2.5f) 推进冷却过 0.5s 过期（tick 的等价递减驱动，探针态沿表恒空
        //        零副作用；t868② 冷却 2.0→1.0s 后本值仍 >1.5× 新冷却，两向断言语义不变）
        //        → 再造真上升沿 → **必须再发射**（箭 +1 / 库存 2→1 正向断言——冷却时长回归改 0
        //        ②不触发、改 ∞ ③不触发，两个方向都在此现形）。
        //     review25 #8：② 段前先 scanDispenserTraps(1e-3f) 走一步真实递减再复置——配合 fireDispenserAt
        //        门改 contains && value>0，0 值冷却在递减步即被 erase → 「常量回归改 0」时复置沿会发射、
        //        ② 断言 FAIL（纯 contains 门下 0 表项永驻恒拦，下界不可见）。
        bool okE = false, okE2 = false;
        {
            placeRigBlock(w, bx0 - 1, kRigY, bz0, BR::Lever, 1); // 对侧第二源扳开 → 复算触达（升沿到已通电机）
            tickN(w, 4);
            w.setBlock(bx0 - 1, kRigY, bz0, BR::Lever, 0);       // 扳回 → 降沿触达
            tickN(w, 4);
            int arrows2 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows2;
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);         // 拆源（降沿）
            tickN(w, 4);
            const int dispBeforeRepower = dispFired;             // 信号计数基线（复置段必须发出 ≥1 次）
            pc.scanDispenserTraps(1e-3f);                        // review25 #8：走一步真实递减再复置——0 值冷却即被 erase，常量回归改 0 时下复置沿必发射 → ② FAIL
            placeRigBlock(w, bx0 + 1, kRigY, bz0, BR::RedstoneBlock, 0); // 复置（真上升沿，但冷却 0.5s 未过）
            tickN(w, 4);
            int arrows3 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows3;
            okE = arrows2 == arrowsAfterB && arrows3 == arrowsAfterB
                  && store.slotCountAt(bx0, kRigY, bz0, 0) == 2  // 库存不再扣（无第二次发射）
                  && dispFired > dispBeforeRepower;              // 信号确发出（零发射 = 冷却拦，非信号没发）
            // ③ 冷却过期后再造上升沿 → 必须再发射（时间维度正向断言）。
            pc.scanDispenserTraps(2.5f); // 等价递减驱动：一次耗尽 0.5s 冷却（探针不启定时器，直调推进；沿表空）
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);         // 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            const int dispBeforeExpire = dispFired;
            placeRigBlock(w, bx0 + 1, kRigY, bz0, BR::RedstoneBlock, 0); // 复置 = 新上升沿 + 冷却已过
            tickN(w, 4);
            int arrows4 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows4;
            okE2 = arrows4 == arrows3 + 1                        // 再发射恰一次
                   && store.slotCountAt(bx0, kRigY, bz0, 0) == 1 // 库存 2→1（真扣一发）
                   && dispFired > dispBeforeExpire;              // 信号链全通（fire 非信号缺失）
            if (!okE || !okE2)
                qInfo().noquote() << "  [t814 e diag] arrowsAfterB=" << arrowsAfterB
                                  << " stable=" << arrows2 << " repower=" << arrows3
                                  << " afterCooldown=" << arrows4
                                  << " slotCount=" << store.slotCountAt(bx0, kRigY, bz0, 0)
                                  << " sigRepower=" << (dispFired > dispBeforeRepower)
                                  << " sigExpire=" << (dispFired > dispBeforeExpire);
            // 清场
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);
            w.setBlock(bx0, kRigY, bz0, BR::Air, 0);
            store.clearDispenser(bx0, kRigY, bz0);
            ents.clearAll();
            tickN(w, 2);
        }

        // (c) 投掷器：拉杆贴合 + store 预填 5 粉 → 掉落物弹出（dropper 只投不射，spawnItemAt 分支）。
        //   review24 低危（探针族）：items 断言改**相对基线**——(a) 的失撑补口未来可能生成掉落物而 (a) 只
        //   ents.clearAll() 不清 items，绝对值 ==1 依赖未言明的前序清场前提（拉杆附着语义一变即环境性假
        //   FAIL）；(d) 本就是相对基线还注释了为什么，现统一口径。
        bool okC = false, okCInv = false;
        {
            const auto [x0, z0] = nextSlot();
            const int itemsBeforeC = items.count(); // 相对基线（弹前快照）
            placeRigBlock(w, x0, kRigY, z0, BR::Dropper, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::RedstoneId, 5);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            okC = items.count() == itemsBeforeC + 1; // 相对本场景恰弹 1（新增量断言，不绑前序清场）
            okCInv = store.slotIdAt(x0, kRigY, z0, 0) == RecipeRegistry::RedstoneId
                     && store.slotCountAt(x0, kRigY, z0, 0) == 4;
            if (!okC || !okCInv)
                qInfo().noquote() << "  [t814 c diag] items=" << items.count()
                                  << "/" << itemsBeforeC
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        // (d) 空库存发射器通电 → 无动作（t607 设计语义：tracked 空库存 = 陷阱解除，无 fallback 箭）。
        //   断言相对基线（快照本场景前 ents/items 计数）：绝对值依赖 (c) 掉落物持久性（ItemEntityManager
        //   生命周期属呈现层语义，探针不该跨场景绑定）——「无**新**实体」才是本场景的语义内核。
        bool okD = false;
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0); // 有条目但库存全空
            tickN(w, 2);
            const int entsBefore = ents.count(), itemsBefore = items.count();
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            int arrows = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows;
            okD = arrows == 0 && ents.count() == entsBefore && items.count() == itemsBefore; // 无新实体
            if (!okD)
                qInfo().noquote() << "  [t814 d diag] arrows=" << arrows
                                  << " ents=" << ents.count() << "/" << entsBefore
                                  << " items=" << items.count() << "/" << itemsBefore;
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        const bool okT814 = okA && okB && okBInv && okC && okCInv && okD && okE && okE2;
        if (!okT814) ++totalFail;
        qInfo().noquote() << (okT814 ? "PASS" : "FAIL")
                          << "| t814 real-consumer probes: Main.qml forwarding mirrored onto actual "
                             "PlayerController.firePowerTnt/fireDispenserAtQml (lever->TNT clears block + spawns "
                             "primed entity at cell center; redstone-block->dispenser w/ 3 arrows fires 1 + "
                             "decrements to 2; lever->dropper w/ 5 dust pops 1 item entity + decrements to 4; "
                             "empty tracked dispenser powered = design no-op; stable-power re-touch and "
                             "sub-0.5s-cooldown re-power both do not re-fire, each zero-fire leg gated by "
                             "powerDispenserTriggered emission-count delta so cooldown-block vs signal-lost "
                             "are distinguished; cooldown driven past 0.5s expiry via scanDispenserTraps "
                             "equivalent-decrement then a true re-edge MUST re-fire +1 arrow/stock 2->1, "
                             "pinning the cooldown duration both directions) - consumer leg never executed by "
                             "P15/t773 before, iron-door contrast explained (door = in-World state write, "
                             "TNT/dispenser = signal->QML->consumer)";
    });

    // ── P-t856 发射器弹点燃 TNT 探针（Game 层真消费端，t814 模式）──
    //   MC 1.0 dispenser 语义：发射器内 TNT 经激活（拉杆，激活链 t772/t814 已锁、本任务零改动）→ 弹出
    //   **已点燃** TNT 实体（PrimedTnt），标准引信落地爆。断言四组：
    //   (a) 弹出位 = 发射面邻格格心（state 0 → +X，源贴背面保发射面净空）+ **标准引信**（fuseProgress==1.0
    //       钉满值 kPrimedTntFuseSec ~5s——链式短 fuse 1.2s 会给 0.24，缩短立现形）+ 引信在跑（tick 0.25s
    //       后 progress 递减）+ **定向初速**（tick 后 +X 位移 ≈ v·dt，钉弹射方向=发射面朝向）+ 库存 3→2；
    //   (b) 二次激活 <0.5s 冷却 → 无第二发（信号确发的零发射 = 冷却拦，t814 ② 口径）+ 库存不再扣；
    //       冷却过 0.5s 后再造沿 → 必再弹（+1 实体 / 库存 2→1，t814 ③ 口径）——冷却闸对 TNT 分支不回归；
    //   (c) 投掷器 + TNT → 普通掉落物弹出**不点燃**（dropper 只投不射口径——两路径边界的另一侧：dropper
    //       弹 TNT 是物品非引燃实体）+ 库存照扣；
    //   (d) review25 #11 排出口占用门：发射面邻格被实体方块堵住 → 不在墙格内 spawn（primed 水平积分不查
    //       碰撞 → 墙格 spawn = ~5s 后就地爆穿墙波及发射器自身）；review26 #24 起堵口**一律**退化普通掉落物
    //       弹出（不点燃）——旧版「再探一格」不看连通 → 1 格厚墙时 TNT 隔墙生成在墙后（穿墙 TNT），已收口；
    //   (e) 红石直接邻接 TNT 原地引爆（firePowerTnt 清方块 + 原格生成）不回归由 t814 (a) 既有探针复跑覆盖。
    runLegMulti({ "t856 dispenser fires primed TNT: lever behind a 3-TNT dispenser pops a PrimedTnt at the facing-a"
        "djacent cell center (state 0 -> +X, source kept off the firing face), full standard fuse (fusePr"
        "ogress==1.0 pins kPrimedTntFuseSec, chain-fuse 1.2s would read 0.24), fuse ticking + directional"
        " +X drift after one 0.25s entity tick pins pop-along-facing velocity (t871: pop speed 4.0->1.6),"
        " and after 1.05s of driven ticks the primed TNT settles INSIDE the facing-adjacent cell (floor=="
        "x0+1, the 'one cell past the muzzle' user contract; the old 4.0 speed landed two cells out), sto"
        "ck 3->2; sub-0.5s-cooldown re-edge fires nothing while powerDispenserTriggered still emits, cool"
        "down driven past 0.5s then re-edge MUST re-pop (+1 entity, stock 2->1); dropper w/ TNT pops a pl"
        "ain item drop with zero primed entities (dropper = item-only, the other side of the two-path bou"
        "ndary); blocked firing face (review25 #11 / review26 #24) always degrades to a plain item drop w"
        "ith zero primed entities -- whether only the adjacent cell is walled (the wall-behind cell stays"
        " primed-free: old code teleported TNT through a 1-thick wall) or the exit is fully walled (MC-ap"
        "proximate: recoverable item over silent swallow), stock decremented on every path; redstone-dire"
        "ct-adjacent in-place priming regression is covered by the t814(a) probe above" }, [&]() {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        ItemEntityManager items;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        pc.setItemEntities(&items);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto primedCount = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) ++n;
            return n;
        };

        // (a) 装填 3 TNT 的发射器 + 背面拉杆激活 → 弹出 PrimedTnt @ 发射面邻格 + 标准引信 + 定向初速。
        //   t871：弹出速度调小（kDispenserTntPopSpeed 4.0→1.6）→ 摩擦积分总位移 ≈0.43 格，TNT 落定在
        //   **发射面邻格内**（用户「出现在发射口前一格即可」）；方向断言阈值随之下调（+0.5 → +0.15，
        //   0.25s 位移 v(1-e^-1)/4≈0.354），并新增落点格断言（驱动 1.05s 后 floor(x) == x0+1）。
        bool okA = false, okAFuse = false, okAMove = false, okRest = false;
        int tx0 = 0, tz0 = 0;
        {
            const auto [x0, z0] = nextSlot();
            tx0 = x0; tz0 = z0;
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0); // state 0 → 朝 +X（chestFrontFace 解码）
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0); // 凿空发射面（review25 #11 起 spawn 前查占用；新 rig 行
                                                        //   z≥97 地形可达 y41（t814 深度扩展带）→ 不凿则探针
                                                        //   走「堵口退化」分支非本段口径）
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, BR::TntBlock, 3);
            tickN(w, 2);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1); // 源贴背面 → 发射面 x0+1 保持净空
            tickN(w, 4);
            int idx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) { idx = i; break; }
            okA = idx >= 0
                  && std::abs(ents.posAt(idx).x() - (x0 + 1.5f)) < 1e-3f // 发射面邻格（x0+1）格心
                  && std::abs(ents.posAt(idx).y() - (kRigY + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(idx).z() - (z0 + 0.5f)) < 1e-3f
                  && store.slotIdAt(x0, kRigY, z0, 0) == BR::TntBlock
                  && store.slotCountAt(x0, kRigY, z0, 0) == 2;          // 库存 3→2（激活一次消耗 1）
            if (idx >= 0) {
                // 标准引信：满值（progress==1.0）；细步驱动 0.25s（16×0.015625——生产帧率级步长；单步 0.25s
                //   的粗欧拉一步跳 0.4 格失真）→ 引信递减（<1.0）+ +X 定向位移（t871 初速 1.6 → ≈0.26 格）。
                const float progBefore = ents.fuseProgressAt(idx);
                const float xBefore = ents.posAt(idx).x();
                for (int i = 0; i < 16; ++i)
                    ents.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                okAFuse = progBefore >= 0.999f && ents.isPrimedAt(idx)
                          && ents.fuseProgressAt(idx) < progBefore;     // 引信计时在跑
                okAMove = ents.isPrimedAt(idx)
                          && ents.posAt(idx).x() > xBefore + 0.15f;     // 弹射方向 = 发射面朝向（+X；t871 阈值随新初速下调）
                // t871 落点：续细步驱动 0.8s（总 1.05s < 引信 5s）→ 摩擦耗尽水平动量 → 落定格必须在邻格内
                //   （连续极限总位移 = v/摩擦率 = 0.4 格 → 落定 ≈x0+1.91；旧 4.0 → 1.0 格 → 落到 x0+2 红此断言）。
                for (int i = 0; i < 51; ++i)
                    ents.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                okRest = ents.isPrimedAt(idx)
                         && int(std::floor(ents.posAt(idx).x())) == x0 + 1  // 仍在发射面邻格（t871 口径）
                         && ents.posAt(idx).x() < float(x0) + 2.0f;
            }
            if (!okA || !okAFuse || !okAMove || !okRest)
                qInfo().noquote() << "  [t856 a diag] primedIdx=" << idx
                                  << " pos=" << (idx >= 0 ? ents.posAt(idx) : QVector3D())
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0)
                                  << " fuse=" << (idx >= 0 ? ents.fuseProgressAt(idx) : -1.0f)
                                  << " okAFuse=" << okAFuse << " okAMove=" << okAMove
                                  << " okRest=" << okRest;
        }

        // (b) 冷却闸不回归（t814 (e) ②③ 压缩版，分派物换 TNT）：0.5s 内真上升沿 → 冷却拦（零发射且信号确发）；
        //     冷却驱动过 0.5s 再造沿 → 必再弹。
        bool okB = false;
        {
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Lever, 0); // 扳回 → 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            const int dispBeforeRepower = dispFired;
            placeRigBlock(w, tx0 - 1, kRigY, tz0, BR::Lever, 1); // 复置 = 真上升沿，但冷却 0.5s 未过
            tickN(w, 4);
            const bool noRefire = primedCount() == 1                      // 仍只有首发（无第二发）
                                  && store.slotCountAt(tx0, kRigY, tz0, 0) == 2 // 库存不再扣
                                  && dispFired > dispBeforeRepower;       // 信号确发（零发射 = 冷却拦非信号丢）
            pc.scanDispenserTraps(2.5f); // 等价递减驱动：一次耗尽 0.5s 冷却（探针态沿表空零副作用，t814 ③ 同款）
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Lever, 0);
            tickN(w, 4);
            placeRigBlock(w, tx0 - 1, kRigY, tz0, BR::Lever, 1); // 新上升沿 + 冷却已过 → 必再弹
            tickN(w, 4);
            okB = noRefire && primedCount() == 2
                  && store.slotCountAt(tx0, kRigY, tz0, 0) == 1;          // 库存 2→1（真扣一发）
            if (!okB)
                qInfo().noquote() << "  [t856 b diag] noRefire=" << noRefire
                                  << " primed=" << primedCount()
                                  << " slotCount=" << store.slotCountAt(tx0, kRigY, tz0, 0)
                                  << " sig=" << (dispFired > dispBeforeRepower);
            // 清场（实体留在探针私有 ents 内冻结，不外泄）
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Air, 0);
            w.setBlock(tx0, kRigY, tz0, BR::Air, 0);
            store.clearDispenser(tx0, kRigY, tz0);
            tickN(w, 2);
        }

        // (c) 投掷器 + TNT → 普通掉落物弹出不点燃（dropper 全物品分支先于 TNT 分派，两路径边界另一侧）。
        bool okC = false;
        {
            const auto [x0, z0] = nextSlot();
            const int itemsBefore = items.count(); // 相对基线（review24 低危口径）
            const int primedBefore = primedCount();
            placeRigBlock(w, x0, kRigY, z0, BR::Dropper, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, BR::TntBlock, 2);
            tickN(w, 2);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            okC = items.count() == itemsBefore + 1                     // 弹出 1 掉落物（TNT 物品形态）
                  && primedCount() == primedBefore                     // 零 PrimedTnt（不点燃）
                  && store.slotCountAt(x0, kRigY, z0, 0) == 1;         // 库存照扣
            if (!okC)
                qInfo().noquote() << "  [t856 c diag] items=" << items.count() << "/" << itemsBefore
                                  << " primed=" << primedCount() << "/" << primedBefore
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        // (d) review25 #11 排出口占用门（review26 #24 口径）：发射面邻格被实体方块堵住 → 不在墙格内 spawn
        //     TNT（primed 水平积分不查碰撞 → 墙格内 spawn = ~5s 后就地爆穿墙并波及发射器自身）。两段：
        //     d1 邻格墙、墙后格空（review26 #24 复现形态：旧版在此隔 1 格墙把 TNT 生成在墙后）→ 零
        //        PrimedTnt（墙格 + 墙后格都无）+ 掉落物 +1（堵口一律退化物品形态）+ 库存照扣；
        //     d2 邻格 + 墙后格都墙 → 同口径（零 PrimedTnt + 掉落物 +1 + 库存照扣）。
        bool okD1 = false, okD2 = false;
        {
            // d1：堵一格（墙后格净空）→ 堵口退化掉落物，墙后零 PrimedTnt（旧版穿墙生成位）。
            const auto [xa, za] = nextSlot();
            placeRigBlock(w, xa, kRigY, za, BR::Dispenser, 0); // state 0 → 朝 +X
            w.setBlock(xa + 2, kRigY, za, BR::Air, 0); // 凿空墙后格（z≥97 新行地形可达 y41；墙格由下方覆写）
            store.ensureDispenser(xa, kRigY, za);
            store.setSlot(xa, kRigY, za, 0, BR::TntBlock, 3);
            placeRigBlock(w, xa + 1, kRigY, za, BR::Stone, 0); // 堵口墙（发射面邻格）
            tickN(w, 2);
            // 基线在拉杆激活**前**取（发射发生在下方 tickN(4) 内——事后取会把退化掉落物算进基线 = 假 +0）
            const int itemsBeforeD1 = items.count();
            const int primedBeforeD1 = primedCount();
            placeRigBlock(w, xa - 1, kRigY, za, BR::Lever, 1); // 源贴背面（激活发射）
            tickN(w, 4);
            int idxD = -1;
            for (int i = 0; i < ents.count(); ++i) // 找「墙后格格心」的 PrimedTnt（旧版穿墙生成位；ents 内有 (a)/(b) 冻结残留）
                if (ents.isPrimedAt(i)
                    && std::abs(ents.posAt(i).x() - (xa + 2.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).y() - (kRigY + 0.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).z() - (za + 0.5f)) < 1e-3f) { idxD = i; break; }
            bool noneInWall = true; // 墙格（xa+1）内不得有任何新 PrimedTnt（旧版就地 spawn 的位置）
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i) && std::abs(ents.posAt(i).x() - (xa + 1.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).z() - (za + 0.5f)) < 1e-3f)
                    noneInWall = false;
            // review26 #24：堵口一律退化掉落物——旧版在墙后格（xa+2）生成 PrimedTnt（隔 1 格墙穿墙）。
            //   新口径断言：零 PrimedTnt（含墙后格，idxD 必 -1）+ 掉落物 +1 + 库存照扣。
            okD1 = idxD < 0 && noneInWall
                  && primedCount() == primedBeforeD1
                  && items.count() == itemsBeforeD1 + 1
                  && store.slotIdAt(xa, kRigY, za, 0) == BR::TntBlock
                  && store.slotCountAt(xa, kRigY, za, 0) == 2; // 库存 3→2
            if (!okD1)
                qInfo().noquote() << "  [t856 d1 diag] idxD=" << idxD
                                  << " noneInWall=" << noneInWall
                                  << " items=" << items.count() << "/" << itemsBeforeD1
                                  << " primed=" << primedCount() << "/" << primedBeforeD1
                                  << " slotCount=" << store.slotCountAt(xa, kRigY, za, 0)
                                  << " b1=" << int(w.blockAt(xa + 1, kRigY, za))
                                  << " cb1=" << (BR::collisionAABBs(w.blockAt(xa + 1, kRigY, za),
                                                                    w.stateAt(xa + 1, kRigY, za))).size()
                                  << " b2=" << int(w.blockAt(xa + 2, kRigY, za))
                                  << " cb2=" << (BR::collisionAABBs(w.blockAt(xa + 2, kRigY, za),
                                                                    w.stateAt(xa + 2, kRigY, za))).size();
            w.setBlock(xa - 1, kRigY, za, BR::Air, 0);
            w.setBlock(xa, kRigY, za, BR::Air, 0);
            w.setBlock(xa + 1, kRigY, za, BR::Air, 0);
            store.clearDispenser(xa, kRigY, za);
            tickN(w, 2);

            // d2：堵两格（邻格 + 墙后格）→ 退化普通掉落物弹出（不点燃，d1 同口径）。
            const auto [xb, zb] = nextSlot();
            const int itemsBefore = items.count();
            const int primedBefore = primedCount();
            placeRigBlock(w, xb, kRigY, zb, BR::Dispenser, 0);
            store.ensureDispenser(xb, kRigY, zb);
            store.setSlot(xb, kRigY, zb, 0, BR::TntBlock, 2);
            placeRigBlock(w, xb + 1, kRigY, zb, BR::Stone, 0);
            placeRigBlock(w, xb + 2, kRigY, zb, BR::Stone, 0);
            tickN(w, 2);
            placeRigBlock(w, xb - 1, kRigY, zb, BR::Lever, 1);
            tickN(w, 4);
            okD2 = primedCount() == primedBefore              // 零 PrimedTnt（不在墙格 / 再探格内引爆实体）
                  && items.count() == itemsBefore + 1          // TNT 以掉落物形态弹出（可回收，不静默吞）
                  && store.slotCountAt(xb, kRigY, zb, 0) == 1; // 库存照扣
            if (!okD2)
                qInfo().noquote() << "  [t856 d2 diag] items=" << items.count() << "/" << itemsBefore
                                  << " primed=" << primedCount() << "/" << primedBefore
                                  << " slotCount=" << store.slotCountAt(xb, kRigY, zb, 0);
            w.setBlock(xb - 1, kRigY, zb, BR::Air, 0);
            w.setBlock(xb, kRigY, zb, BR::Air, 0);
            w.setBlock(xb + 1, kRigY, zb, BR::Air, 0);
            w.setBlock(xb + 2, kRigY, zb, BR::Air, 0);
            store.clearDispenser(xb, kRigY, zb);
            tickN(w, 2);
        }

        const bool okT856 = okA && okAFuse && okAMove && okRest && okB && okC && okD1 && okD2;
        if (!okT856) ++totalFail;
        qInfo().noquote() << (okT856 ? "PASS" : "FAIL")
                          << "| t856 dispenser fires primed TNT: lever behind a 3-TNT dispenser pops a "
                             "PrimedTnt at the facing-adjacent cell center (state 0 -> +X, source kept off "
                             "the firing face), full standard fuse (fuseProgress==1.0 pins "
                             "kPrimedTntFuseSec, chain-fuse 1.2s would read 0.24), fuse ticking + "
                             "directional +X drift after one 0.25s entity tick pins pop-along-facing "
                             "velocity (t871: pop speed 4.0->1.6), and after 1.05s of driven ticks the "
                             "primed TNT settles INSIDE the facing-adjacent cell (floor==x0+1, the "
                             "'one cell past the muzzle' user contract; the old 4.0 speed landed two "
                             "cells out), stock 3->2; sub-0.5s-cooldown re-edge fires nothing while "
                             "powerDispenserTriggered still emits, cooldown driven past 0.5s then re-edge "
                             "MUST re-pop (+1 entity, stock 2->1); dropper w/ TNT pops a plain item drop "
                             "with zero primed entities (dropper = item-only, the other side of the "
                             "two-path boundary); blocked firing face (review25 #11 / review26 #24) "
                             "always degrades to a plain item drop with zero primed entities -- "
                             "whether only the adjacent cell is walled (the wall-behind cell stays "
                             "primed-free: old code teleported TNT through a 1-thick wall) or the "
                             "exit is fully walled (MC-approximate: recoverable item over silent "
                             "swallow), stock decremented on every path; "
                             "redstone-direct-adjacent in-place priming regression is "
                             "covered by the t814(a) probe above";
    });

    // ── P-t868 高频红石逐沿发射探针（Game 层真消费端，t814/t856 模式）──
    //   用户实测：「发射器高频红石只能激活一次」（旧 2.0s 冷却把第二个上升沿整只吞掉）。修复 = 冷却缩短
    //   2.0→0.5s 且语义重钉「短防抖闸」（拦同 tick 双路径双发），非节流窗。断言三段：
    //   (a) 高频沿序列连发：拉杆快速循环（扳开→0.7s→扳回→扳开；0.7s = 真实帧驱动的冷却递减——
    //       scanDispenserTraps(0.016f)×44 ≈ 60Hz 帧，等价生产里 tick() 每帧推进冷却）。旧 2.0s 常量下
    //       0.7s < 2.0s → 第二沿被吞（fired 恒 1 = 用户症状）；新 0.5s 下 0.7s > 0.5s → 必再发（≥2）。
    //   (b) 防抖闸仍有效：同一冷却窗内（只 tickN 推进世界、无帧驱动递减）再造真上升沿 → 不多发
    //       （t814 (e)② 同口径；t869 火把环时钟落地前的独立驱动——本探针不依赖无稳态电路存在）。
    //   (c) 库存对账：发射次数 == 库存扣减量（无凭空箭 / 无吞库存）。
    runLegMulti({ "t868 high-frequency redstone re-fires per rising edge: rapid lever cycling (edge -> 0.7s frame-d"
        "riven cooldown decay -> re-edge) MUST re-fire (the old 2.0s cooldown swallowed every sub-2s edge"
        " = the reported fires-once symptom), stock decremented exactly once per shot; a fresh re-edge in"
        "side the cooldown window stays blocked (single-path double-fire guard intact)" }, [&]() {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });

        bool okClock = false, okDebounce = false, okStock = false;
        const auto arrowCount = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++n;
            return n;
        };
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);   // state 0 → 朝 +X
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 4);
            tickN(w, 2);

            // (a) 高频沿序列：沿#1（扳开）→ 帧驱动 0.7s → 沿#2（扳回+再扳开）→ 必再发。
            const int arrows0 = arrowCount();
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // 上升沿 #1 → 发射 1
            tickN(w, 4);
            const int afterFirst = arrowCount();
            for (int f = 0; f < 44; ++f) pc.scanDispenserTraps(0.016f); // 0.704s 帧驱动（>0.5s 新冷却 / <2.0s 旧冷却）
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);         // 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // 上升沿 #2 → 旧 2.0s 冷却吞 / 新 0.5s 过闸
            tickN(w, 4);
            const int fired = arrowCount() - arrows0;
            okClock = afterFirst == arrows0 + 1 && fired >= 2;    // 首沿恰 1 发 + 第二沿必再发（旧常量恒 1 → FAIL）
            okStock = store.slotCountAt(x0, kRigY, z0, 0) == 4 - fired; // 库存对账（发射数==扣减数）
            if (!okClock || !okStock)
                qInfo().noquote() << "  [t868 a diag] first=" << (afterFirst - arrows0)
                                  << " fired=" << fired
                                  << " stock=" << store.slotCountAt(x0, kRigY, z0, 0);

            // (b) 防抖闸：冷却窗内（仅 tickN，无帧驱动递减——冷却表项保持 ~0.5s）再造真上升沿 → 不多发。
            pc.scanDispenserTraps(1e-3f);                        // review25 #8：走一步真实递减（0 值冷却即被 erase）
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);         // 降沿
            tickN(w, 4);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // <0.5s 新升沿 → 防抖闸拦（不发射）
            tickN(w, 4);
            const int firedB = arrowCount() - arrows0;
            okDebounce = firedB == fired                          // 与 (a) 末尾持平（无新发射）
                         && store.slotCountAt(x0, kRigY, z0, 0) == 4 - fired;
            if (!okDebounce)
                qInfo().noquote() << "  [t868 b diag] fired=" << firedB
                                  << " expect=" << fired
                                  << " stock=" << store.slotCountAt(x0, kRigY, z0, 0);
            // 清场
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            ents.clearAll();
            tickN(w, 2);
        }
        const bool okT868 = okClock && okDebounce && okStock;
        if (!okT868) ++totalFail;
        qInfo().noquote() << (okT868 ? "PASS" : "FAIL")
                          << "| t868 high-frequency redstone re-fires per rising edge: rapid lever cycling "
                             "(edge -> 0.7s frame-driven cooldown decay -> re-edge) MUST re-fire (the old 2.0s "
                             "cooldown swallowed every sub-2s edge = the reported fires-once symptom), stock "
                             "decremented exactly once per shot; a fresh re-edge inside the cooldown "
                             "window stays blocked (single-path double-fire guard intact)";
    });

    // ── review26 #16 同柱垂直叠放发射器独立冷却探针（Game 层真消费端，t856/t868 模式）──
    //   用户症状（review26 低危）：冷却键 (x<<32|z) 不含 Y → 同柱垂直两台发射器共享冷却，0.5s 内上台
    //   发射后下台的合法沿被吞（t868「逐沿发射」语义在柱粒度上破裂）。修：键入 Y（21/21/10 三维布局，
    //   同 m_redstoneLitCells 既有键序）。断言：同 tick 两台各自被拉杆通电 → 两箭各发一支（箭 +2、
    //   两台库存各扣 1）——旧键下第二台被共享冷却拦（恰 1 箭、一台库存不扣），回退即红。
    runLegMulti({ "review26-16 per-dispenser cooldown keyed in 3D: two vertically stacked dispensers powered the sa"
        "me tick each fire their own arrow (old (x<<32|z) key shared the cooldown across the column and s"
        "wallowed the lower machine's legal edge within 0.5s), both stocks decremented" }, [&]() {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto arrowCount16 = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++n;
            return n;
        };
        const auto [x0, z0] = nextSlot();
        const int arrowsBefore = arrowCount16();
        // 同柱两台：下台 @kRigY、上台 @kRigY+1，各配独立背面拉杆（同 tick 双上升沿）、各装 2 支箭。
        placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);       // 下台（state 0 → 朝 +X）
        placeRigBlock(w, x0, kRigY + 1, z0, BR::Dispenser, 0);   // 上台
        store.ensureDispenser(x0, kRigY, z0);
        store.ensureDispenser(x0, kRigY + 1, z0);
        store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 2);
        store.setSlot(x0, kRigY + 1, z0, 0, RecipeRegistry::ArrowId, 2);
        tickN(w, 2);
        placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);       // 下台拉杆（on）
        placeRigBlock(w, x0 - 1, kRigY + 1, z0, BR::Lever, 1);   // 上台拉杆（on）
        tickN(w, 4);
        const bool ok16 = arrowCount16() == arrowsBefore + 2                // 两台各发一支（旧键恰 +1）
            && store.slotCountAt(x0, kRigY, z0, 0) == 1                     // 下台库存 2→1
            && store.slotCountAt(x0, kRigY + 1, z0, 0) == 1;                // 上台库存 2→1（旧键不扣 = 被吞沿）
        if (!ok16)
            qInfo().noquote() << "  [review26-16 diag] arrows +" << arrowCount16() - arrowsBefore
                          << " lowerStock" << store.slotCountAt(x0, kRigY, z0, 0)
                          << " upperStock" << store.slotCountAt(x0, kRigY + 1, z0, 0);
        if (!ok16) ++totalFail;
        qInfo().noquote() << (ok16 ? "PASS" : "FAIL")
                          << "| review26-16 per-dispenser cooldown keyed in 3D: two vertically stacked "
                             "dispensers powered the same tick each fire their own arrow (old (x<<32|z) key "
                             "shared the cooldown across the column and swallowed the lower machine's legal "
                             "edge within 0.5s), both stocks decremented";
        // 清场
        w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0 - 1, kRigY + 1, z0, BR::Air, 0);
        w.setBlock(x0, kRigY, z0, BR::Air, 0);
        w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
        store.clearDispenser(x0, kRigY, z0);
        store.clearDispenser(x0, kRigY + 1, z0);
        tickN(w, 2);
    });

    // ── P-t869 红石无稳态电路（时钟）复刻探针（World 层，t740 回归定位）──
    //   用户实测：「红石高频 / 无限电路上版本有、本版没了」。考古结论：v1 电网从未支持过*合法*无稳态——
    //   t740（3686e27，2026-08-21）为修「灯闪 / 时亮时不亮」把「火把斜下 4 格的粉」整体豁免出
    //   attachPowered 读，装饰环误触发**和**粉输入 NOT 门 / 时钟回路一并哑火（用户记忆中的「上版本有」
    //   即 t740 前的整体回灌振荡）。t812 转辙器 bit7 / t689 沿检测 / t707 BFS 均无涉（各自升 / 降沿对称）。
    //   修复 = MC **形状输出**语义（BlockRegistry::redstoneDustPowersNeighbor，连接位反推开放端）：
    //   粉终止于 / 拐入方块 → 供能（时钟 / NOT 门恢复）；贯穿直线贴块而过 → 不供能（t740 装饰环保持
    //   稳定——原修案的正确形态）。断言三段：
    //   (a) **火把时钟**：石块 + 立顶火把 + 单格粉 stub 贴基座侧（火把斜下自喂 → stub 形状指向基座 →
    //       火把熄 → stub 断电 → 重亮 …… 自持振荡）。24 tick 内火把态翻转 ≥4 次 + stub 电力出现 0↔非0
    //       交替（t740 豁免下恒 0 次翻转 = 用户症状）；
    //   (b) **粉线 NOT 门**（稳定反相）：拉杆(on) → 粉×3 线终止于基座侧 → 基座恒被供 → 火把持续熄灭
    //       （tick 6/12/20 采样 OffFlag 恒置位——非振荡，锁存反相）；
    //   (c) **贯穿直线稳定对照**（t740 反闪烁保持）：粉直线贴基座侧而过（中格对向双连 = 贯穿形）→
    //       20 tick 火把恒亮（装饰环不再误触发——豁免换成形状后原修案语义仍在）。
    runLegMulti({ "t869 redstone astable circuits restored via dust shape semantics: torch-on-block + 2-cell dust s"
        "tub at the base side self-oscillates (>=4 state flips + power alternation in 24 ticks; the t740 "
        "blanket base-ring exemption pinned it at 0 flips = the reported regression), lever-driven 3-dust"
        " line ENDING at the support holds the torch inverted (latched NOT gate), while a straight dust l"
        "ine PASSING the support side stays silent (through-line has no side output) and a lone dot has n"
        "o output at all - both t740 anti-flicker shapes remain stable; archaeology: t740 3686e27 killed "
        "both the accidental ring flicker AND every dust-fed NOT/clock input, t812 bit7 / t689 edges / t7"
        "07 BFS cleared of involvement" }, [&]() {
        bool okClock = false, okNot = false, okStable = false;
        // (a) 火把时钟：x0=Stone(基座) x0/y+1=Torch x0+1..x0+2=粉 stub 两格（近格连接朝远格 = 端点形 →
        //     开放端指向基座 → 供能；单格 dot 不输出（P4/P14 稳定语义），须两格才成回路）。
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneDust, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::RedstoneDust, 0);
            tickN(w, 2);
            placeRigBlock(w, x0, kRigY + 1, z0, BR::RedstoneTorch, 0); // 最后放火把 → 起振
            int flips = 0, powerFlips = 0;
            bool prevOff = false, prevPow = false;
            for (int t = 0; t < 24; ++t) {
                w.tickRedstone();
                const bool off = (w.stateAt(x0, kRigY + 1, z0) & BR::RedstoneTorchStateOffFlag) != 0;
                const bool pow = (w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask) > 0;
                if (t > 0) {
                    if (off != prevOff) ++flips;
                    if (pow != prevPow) ++powerFlips;
                }
                prevOff = off; prevPow = pow;
            }
            okClock = flips >= 4 && powerFlips >= 4; // 自持振荡（t740 豁免下恒 0 → FAIL = 用户症状）
            if (!okClock)
                qInfo().noquote() << "  [t869 a diag] flips=" << flips << " powerFlips=" << powerFlips;
            // 清场
            w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
        // (b) 粉线 NOT 门（稳定反相）：Stone x1 / Torch 其上 / 粉 x1+1..x1+3 / Lever(on) x1+4。
        {
            const auto [x1, z1] = nextSlot();
            placeRigBlock(w, x1, kRigY, z1, BR::Stone, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x1 + i, kRigY, z1, BR::RedstoneDust, 0);
            placeRigBlock(w, x1 + 4, kRigY, z1, BR::Lever, 1);
            tickN(w, 2);
            placeRigBlock(w, x1, kRigY + 1, z1, BR::RedstoneTorch, 0);
            bool off6 = false, off12 = false, off20 = false;
            for (int t = 1; t <= 20; ++t) {
                w.tickRedstone();
                const bool off = (w.stateAt(x1, kRigY + 1, z1) & BR::RedstoneTorchStateOffFlag) != 0;
                if (t == 6) off6 = off;
                if (t == 12) off12 = off;
                if (t == 20) off20 = off;
            }
            okNot = off6 && off12 && off20; // 持续熄灭（线保供，锁存反相非振荡）
            if (!okNot)
                qInfo().noquote() << "  [t869 b diag] off6=" << off6 << " off12=" << off12
                                  << " off20=" << off20;
            // 清场
            w.setBlock(x1, kRigY + 1, z1, BR::Air, 0);
            w.setBlock(x1 + 4, kRigY, z1, BR::Air, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x1 + i, kRigY, z1, BR::Air, 0);
            w.setBlock(x1, kRigY, z1, BR::Air, 0);
            tickN(w, 2);
        }
        // (c) 贯穿直线稳定对照：Stone x2 / Torch 其上 / 粉直线 (x2+1, z2-1..z2+1)（中格对向双连）/
        //     Lever(on) (x2+1, z2+2)。直线贴基座 +X 侧而过 → 形状侧向不供 → 火把恒亮。
        {
            const auto [x2, z2] = nextSlot();
            placeRigBlock(w, x2, kRigY, z2, BR::Stone, 0);
            for (int dz = -1; dz <= 1; ++dz) w.setBlock(x2 + 1, kRigY, z2 + dz, BR::RedstoneDust, 0);
            placeRigBlock(w, x2 + 1, kRigY, z2 + 2, BR::Lever, 1);
            tickN(w, 2);
            placeRigBlock(w, x2, kRigY + 1, z2, BR::RedstoneTorch, 0);
            bool anyOff = false;
            for (int t = 0; t < 20; ++t) {
                w.tickRedstone();
                if (w.stateAt(x2, kRigY + 1, z2) & BR::RedstoneTorchStateOffFlag) anyOff = true;
            }
            okStable = !anyOff; // 贯穿形侧向不供（装饰环稳定，t740 原修案语义保持）
            if (!okStable)
                qInfo().noquote() << "  [t869 c diag] anyOff=" << anyOff;
            // 清场
            w.setBlock(x2, kRigY + 1, z2, BR::Air, 0);
            w.setBlock(x2 + 1, kRigY, z2 + 2, BR::Air, 0);
            for (int dz = -1; dz <= 1; ++dz) w.setBlock(x2 + 1, kRigY, z2 + dz, BR::Air, 0);
            w.setBlock(x2, kRigY, z2, BR::Air, 0);
            tickN(w, 2);
        }
        const bool okT869 = okClock && okNot && okStable;
        if (!okT869) ++totalFail;
        qInfo().noquote() << (okT869 ? "PASS" : "FAIL")
                          << "| t869 redstone astable circuits restored via dust shape semantics: torch-on-block "
                             "+ 2-cell dust stub at the base side self-oscillates (>=4 state flips + power "
                             "alternation in 24 ticks; the t740 blanket base-ring exemption pinned it at 0 "
                             "flips = the reported regression), lever-driven 3-dust line ENDING at the support "
                             "holds the torch inverted (latched NOT gate), while a straight dust line PASSING "
                             "the support side stays silent (through-line has no side output) and a lone dot "
                             "has no output at all - both t740 anti-flicker shapes remain stable; "
                             "archaeology: t740 3686e27 killed both the accidental ring flicker AND every "
                             "dust-fed NOT/clock input, t812 bit7 / t689 edges / t707 BFS "
                             "cleared of involvement";
    });

    // ── P-review26-5 红石粉形状输出「臂轴延长端」语义探针（review26 #5：拐角/单臂垂直侧自相矛盾修口）──
    //   旧判定等价「目标向无连接位且非贯穿直线侧即供电」：单臂粉向三个非连接方向全 true，贯穿直线垂直侧
    //   false——同为垂直于粉臂的侧面，一格之差行为翻转（L 形拐角格垂直侧贴 TNT/灯/发射器/铁门意外通电）。
    //   新语义：目标 ±X 输出 iff X 轴有连接（px||nx）、±Z iff（pz||nz）、dot 仍 false。
    //   (a) 纯函数四形真值表（连接位直构）：端点垂直侧 false（修掉矛盾面）/ 端点延长端 true / 拐角开放侧
    //       true / 贯穿直线侧向 false / dot 全 false / 垂直对角 false；
    //   (b) World 行为级（专用局部世界 + 固定坐标，t829 免凿高台先例——**不占 nextSlot 主世界位**：本批
    //       探针前移会推移 t812 等下游探针的 rig 位，矿车跑法对槽位地形敏感 = 假 FAIL）：端点粉臂**垂直于**
    //       基座方向 → 火把恒亮（旧代码此形态供电 → 火把熄 = FAIL 面）；臂沿基座方向（延长端，拉杆驱动）→
    //       火把锁存熄灭（正对照，t869(b) NOT 门形态不变）；拐角开放侧 → 供电熄灭（正对照，与旧行为一致）。
    runLegMulti({ "review26-5 dust shape output unified to arm-axis extension semantics: target +-X powered iff the"
        " dust has an X-axis arm (px||nx), +-Z iff (pz||nz), dot stays dark - endpoint perpendicular side"
        "s no longer power (old rule powered all 3 non-connected sides of a single-arm dust while a throu"
        "gh-line's perpendicular side stayed dark - same geometry, opposite verdict one cell apart), endp"
        "oint extension end / corner open sides / through-line axial ends still power (NOT-gate and clock"
        " wiring intact), t740 anti-flicker shapes (dot + through-line side) unchanged; four-shape functi"
        "on truth table + world rigs: perpendicular-arm endpoint keeps the torch lit (old code powered it"
        "), lever-driven extension end and corner open side keep it latched off (positive controls)" }, [&]() {
        const auto dustSt = [](quint8 conn) { return quint8(conn << 4); };
        const quint8 armNx  = dustSt(BR::RedstoneDustConnNx);
        const quint8 armPx  = dustSt(BR::RedstoneDustConnPx);
        const quint8 corner = dustSt(BR::RedstoneDustConnPx | BR::RedstoneDustConnPz);
        const quint8 lineZ  = dustSt(BR::RedstoneDustConnPz | BR::RedstoneDustConnNz);
        const bool okTable =
            // 端点（单臂 -X）：延长端 ±X 输出；垂直侧 ±Z 不输出（review26 #5 修掉的矛盾面）
            BR::redstoneDustPowersNeighbor(armNx, -1, 0) && BR::redstoneDustPowersNeighbor(armNx, 1, 0)
            && !BR::redstoneDustPowersNeighbor(armNx, 0, 1) && !BR::redstoneDustPowersNeighbor(armNx, 0, -1)
            // 端点（单臂 +X）镜像
            && BR::redstoneDustPowersNeighbor(armPx, 1, 0) && BR::redstoneDustPowersNeighbor(armPx, -1, 0)
            && !BR::redstoneDustPowersNeighbor(armPx, 0, 1) && !BR::redstoneDustPowersNeighbor(armPx, 0, -1)
            // 拐角（px+pz）：两个几何可达开放侧（-X 延长端 / -Z 延长端）输出
            && BR::redstoneDustPowersNeighbor(corner, -1, 0) && BR::redstoneDustPowersNeighbor(corner, 0, -1)
            // 贯穿直线（Z 轴）：侧向 ±X 不输出（t740 反闪烁保持）；轴向 ±Z 输出
            && !BR::redstoneDustPowersNeighbor(lineZ, 1, 0) && !BR::redstoneDustPowersNeighbor(lineZ, -1, 0)
            && BR::redstoneDustPowersNeighbor(lineZ, 0, 1) && BR::redstoneDustPowersNeighbor(lineZ, 0, -1)
            // dot（无连接）：全向不输出（P4/P14 语义）
            && !BR::redstoneDustPowersNeighbor(dustSt(0), 1, 0) && !BR::redstoneDustPowersNeighbor(dustSt(0), 0, 1)
            // 垂直 / 对角 / 零偏移：无形状语义
            && !BR::redstoneDustPowersNeighbor(armNx, 0, 0) && !BR::redstoneDustPowersNeighbor(armNx, 1, 1);
        // 专用世界（seed 77：实测地形 ≤81 → 84+ 全空带，t835 同款）：y84 石平台，器件层 y85，火把 y86。
        World wR5;
        wR5.setWidth(48); wR5.setDepth(48); wR5.setHeight(96); wR5.setSeed(77);
        for (int x = 2; x <= 40; ++x)
            for (int z = 2; z <= 40; ++z) wR5.setBlock(x, 84, z, BR::Stone, 0);
        // (b1) 端点垂直侧（判别面）：基座 -X、粉臂 +Z（臂垂直于基座方向）→ 无 X 轴臂 → 不供电 → 火把恒亮。
        //      旧代码：!nx && !straightZ 全 true + 火把斜下喂粉 → attachPowered → 熄灭（= 矛盾行为）。
        placeRigBlock(wR5, 6, 85, 6, BR::Stone, 0);
        wR5.setBlock(7, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(7, 85, 7, BR::RedstoneDust, 0); // 臂 +Z（垂直侧形态）
        tickN(wR5, 2);
        placeRigBlock(wR5, 6, 86, 6, BR::RedstoneTorch, 0);
        bool anyOff1 = false;
        for (int t = 0; t < 16; ++t) {
            wR5.tickRedstone();
            if (wR5.stateAt(6, 86, 6) & BR::RedstoneTorchStateOffFlag) anyOff1 = true;
        }
        const bool okPerp = !anyOff1;
        if (!okPerp)
            qInfo().noquote() << "  [review26-5 b1 diag] anyOff1" << anyOff1;
        wR5.setBlock(6, 86, 6, BR::Air, 0);
        wR5.setBlock(7, 85, 6, BR::Air, 0);
        wR5.setBlock(7, 85, 7, BR::Air, 0);
        wR5.setBlock(6, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        // (b2) 端点延长端正对照（拉杆驱动，锁存非振荡）：臂 +X 沿基座方向 → 供电 → 火把持续熄灭。
        placeRigBlock(wR5, 12, 85, 6, BR::Stone, 0);
        wR5.setBlock(13, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(14, 85, 6, BR::RedstoneDust, 0);
        placeRigBlock(wR5, 15, 85, 6, BR::Lever, 1);
        tickN(wR5, 2);
        placeRigBlock(wR5, 12, 86, 6, BR::RedstoneTorch, 0);
        bool off2 = false;
        for (int t = 0; t < 12; ++t) {
            wR5.tickRedstone();
            if (t >= 6 && (wR5.stateAt(12, 86, 6) & BR::RedstoneTorchStateOffFlag)) off2 = true;
        }
        const bool okExt = off2;
        if (!okExt)
            qInfo().noquote() << "  [review26-5 b2 diag] off2" << off2;
        wR5.setBlock(12, 86, 6, BR::Air, 0);
        wR5.setBlock(15, 85, 6, BR::Air, 0);
        wR5.setBlock(13, 85, 6, BR::Air, 0);
        wR5.setBlock(14, 85, 6, BR::Air, 0);
        wR5.setBlock(12, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        // (b3) 拐角开放端正对照：粉 A 连 +X（远端）与 +Z（拐臂）成拐角，基座在 -X 开放侧 → 供电 → 熄灭
        //      （旧行为一致——拐角开放端 true 两版相同，钉住不回归）。
        placeRigBlock(wR5, 18, 85, 6, BR::Stone, 0);
        wR5.setBlock(19, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(20, 85, 6, BR::RedstoneDust, 0); // px 臂（远端）
        wR5.setBlock(19, 85, 7, BR::RedstoneDust, 0); // pz 拐臂 → A 成拐角形
        placeRigBlock(wR5, 21, 85, 6, BR::Lever, 1);
        tickN(wR5, 2);
        placeRigBlock(wR5, 18, 86, 6, BR::RedstoneTorch, 0);
        bool off3 = false;
        for (int t = 0; t < 12; ++t) {
            wR5.tickRedstone();
            if (t >= 6 && (wR5.stateAt(18, 86, 6) & BR::RedstoneTorchStateOffFlag)) off3 = true;
        }
        const bool okCorner = off3;
        if (!okCorner)
            qInfo().noquote() << "  [review26-5 b3 diag] off3" << off3;
        wR5.setBlock(18, 86, 6, BR::Air, 0);
        wR5.setBlock(21, 85, 6, BR::Air, 0);
        wR5.setBlock(19, 85, 6, BR::Air, 0);
        wR5.setBlock(20, 85, 6, BR::Air, 0);
        wR5.setBlock(19, 85, 7, BR::Air, 0);
        wR5.setBlock(18, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        const bool okR5 = okTable && okPerp && okExt && okCorner;
        if (!okR5) ++totalFail;
        qInfo().noquote() << (okR5 ? "PASS" : "FAIL")
                          << "| review26-5 dust shape output unified to arm-axis extension semantics: "
                             "target +-X powered iff the dust has an X-axis arm (px||nx), +-Z iff "
                             "(pz||nz), dot stays dark - endpoint perpendicular sides no longer power "
                             "(old rule powered all 3 non-connected sides of a single-arm dust while a "
                             "through-line's perpendicular side stayed dark - same geometry, opposite "
                             "verdict one cell apart), endpoint extension end / corner open sides / "
                             "through-line axial ends still power (NOT-gate and clock wiring intact), "
                             "t740 anti-flicker shapes (dot + through-line side) unchanged; four-shape "
                             "function truth table + world rigs: perpendicular-arm endpoint keeps the "
                             "torch lit (old code powered it), lever-driven extension end and corner "
                             "open side keep it latched off (positive controls)";
    });

    // ── P-review26-6 火把 burnout 熔断探针（review26 #6：端点粉贴基座永续振荡无兜底）──
    //   t869 形状语义恢复端点/拐角回灌后，「火把立方块上 + 基座旁 ≥2 格端点粉」= 5Hz 永续振荡（每 tick
    //   recomputeLightAround 双调 + chunk mesh 10Hz 重建直到玩家干预）；MC 有 torch burnout 熔断。本探针钉
    //   MC 近似参数（镜像常量 P18 模式）：**8 次翻转（60s 窗内）→ 锁定熄灭 80 红石 tick（8s）→ 冷却后可再
    //   振荡**（确定性整数计数，PLAN §2-K）。
    //   (a) 拉杆驱动精确翻转：NOT 门 rig 4 个 on/off 半周期 = 恰 8 翻 → 第 8 翻熔断——拉杆 OFF（attach 失电，
    //       自然应重亮）而火把保持熄灭 = 锁定面；+70 tick 仍熄；75..100 tick 窗内重亮（冷却 80 ± 传播余量）；
    //       重亮后再拨拉杆立即再熄（冷却后电路照常工作）。
    //   (b) 自由时钟（t869(a) 同 rig）长跑 320 tick：早期 ≥4 翻（振荡未被熔断误杀）→ 出现 60..100 tick
    //       连续熄灭段（锁定段长钉冷却量级）→ 段后 100 tick 内再翻转（冷却后恢复振荡 = 「可振荡」tradeoff
    //       保持，burnout 只封「永续」）。
    runLegMulti({ "review26-6 torch burnout fuse: a torch flipping  times inside the 60s window locks OFF for  reds"
        "tone ticks (8s, MC 160gt parity) then re-evaluates - lever-driven NOT rig: 8th flip (would-be re"
        "light under an OFF lever) stays dark = locked, no relight within 70 ticks, relight lands in the "
        "73..100 window, circuit toggles normally after cooldown; free-running endpoint-dust clock: >=4 f"
        "lips before the fuse (oscillation not over-killed), a 60..100-tick continuous dark stretch (lock"
        " magnitude), and post-lock flips within 100 ticks (cooldown expiry restores oscillation - astabl"
        "e circuits remain buildable, only PERPETUAL 5Hz hammering is fused; deterministic integer counte"
        "rs, PLAN 2-K)" }, [&]() {
        constexpr int kMirrorBurnoutFlips = 8;      // World::kTorchBurnoutFlipLimit 镜像（改值须两处同步）
        constexpr int kMirrorBurnoutCooldown = 80;  // World::kTorchBurnoutCooldownTicks 镜像
        Q_UNUSED(kMirrorBurnoutFlips);
        // 专用世界（同 review26-5：seed 77 全空带 y84+ 平台——不占 nextSlot 主世界位，防下游槽位漂移）。
        World wR6;
        wR6.setWidth(48); wR6.setDepth(48); wR6.setHeight(96); wR6.setSeed(77);
        for (int x = 2; x <= 40; ++x)
            for (int z = 2; z <= 40; ++z) wR6.setBlock(x, 84, z, BR::Stone, 0);
        // (a) 拉杆驱动（P14 几何：拉杆贴**支撑块**侧面——直供 attach，翻转链每拨恰一翻可精确计数；t869(b)
        //     的「拉杆贴粉线远端」形态锚点 2-hop 展开够不到火把，lever 翻转永不复评 = rig 假死）。
        placeRigBlock(wR6, 6, 85, 14, BR::Stone, 0);
        placeRigBlock(wR6, 6, 85, 15, BR::Lever, 1); // ON（贴支撑侧面）
        tickN(wR6, 2);
        placeRigBlock(wR6, 6, 86, 14, BR::RedstoneTorch, 0);
        tickN(wR6, 6);
        const auto torchOffA = [&]() {
            return (wR6.stateAt(6, 86, 14) & BR::RedstoneTorchStateOffFlag) != 0;
        };
        bool okA = torchOffA(); // 翻 1：lever ON → 锁存熄灭
        for (int half = 0; half < 7 && okA; ++half) { // 翻 2..8：交替 off/on 半周期
            wR6.setBlock(6, 85, 15, BR::Lever, quint8(half % 2 == 0 ? 0 : 1));
            tickN(wR6, 6);
            const bool off = torchOffA();
            if (half < 6) {
                okA = okA && (off == (half % 2 == 1)); // 前 6 半周期正常翻转（lever on 段熄 / off 段亮）
            } else {
                okA = okA && off; // 第 8 翻（lever OFF 后本应重亮）熔断 → 保持熄灭 = 锁定
            }
            if (!okA)
                qInfo().noquote() << "  [review26-6 a half diag] half" << half << "off" << off;
        }
        int relightTick = -1;
        for (int t = 1; t <= 110 && okA; ++t) {
            wR6.tickRedstone();
            if (t <= 70 && !torchOffA()) { okA = false; break; } // 冷却期内不得重亮
            if (t > 70 && !torchOffA() && relightTick < 0) relightTick = t;
        }
        okA = okA && relightTick > 72 && relightTick <= 100; // 冷却 80 ± 锁定点/传播余量
        if (okA) { // 冷却后电路照常：再拨 lever → 立即再熄（新计数窗开跑）
            wR6.setBlock(6, 85, 15, BR::Lever, 1);
            tickN(wR6, 6);
            okA = torchOffA();
        }
        if (!okA)
            qInfo().noquote() << "  [review26-6 a diag] relightTick" << relightTick
                              << "endOff" << torchOffA();
        wR6.setBlock(6, 86, 14, BR::Air, 0);
        wR6.setBlock(6, 85, 15, BR::Air, 0);
        wR6.setBlock(6, 85, 14, BR::Air, 0);
        tickN(wR6, 2);
        // (b) 自由时钟长跑（t869(a) 同 rig：火把立方块上 + 端点粉 stub 贴基座侧自持振荡）。
        placeRigBlock(wR6, 12, 85, 14, BR::Stone, 0);
        wR6.setBlock(13, 85, 14, BR::RedstoneDust, 0);
        wR6.setBlock(14, 85, 14, BR::RedstoneDust, 0);
        tickN(wR6, 2);
        placeRigBlock(wR6, 12, 86, 14, BR::RedstoneTorch, 0);
        std::vector<bool> offSeq;
        offSeq.reserve(320);
        for (int t = 0; t < 320; ++t) {
            wR6.tickRedstone();
            offSeq.push_back((wR6.stateAt(12, 86, 14) & BR::RedstoneTorchStateOffFlag) != 0);
        }
        int flipsEarly = 0;
        for (size_t t = 1; t < offSeq.size(); ++t)
            if (t <= 60 && offSeq[t] != offSeq[t - 1]) ++flipsEarly;
        int best = 0, cur = 0, bestEnd = 0;
        for (size_t t = 0; t < offSeq.size(); ++t) {
            if (offSeq[t]) {
                ++cur;
                if (cur > best) { best = cur; bestEnd = int(t); }
            } else {
                cur = 0;
            }
        }
        bool recovered = false; // 锁定段结束后 100 tick 内有翻转（冷却后恢复振荡）
        for (int t = bestEnd + 1; t < std::min(int(offSeq.size()), bestEnd + 101); ++t)
            if (offSeq[t] != offSeq[t - 1]) { recovered = true; break; }
        const bool okB = flipsEarly >= 4 && best >= 60 && best <= 100 && recovered;
        if (!okB)
            qInfo().noquote() << "  [review26-6 b diag] flipsEarly" << flipsEarly
                              << "bestStreak" << best << "recovered" << recovered;
        wR6.setBlock(12, 86, 14, BR::Air, 0);
        wR6.setBlock(13, 85, 14, BR::Air, 0);
        wR6.setBlock(14, 85, 14, BR::Air, 0);
        wR6.setBlock(12, 85, 14, BR::Air, 0);
        tickN(wR6, 2);
        const bool okR6 = okA && okB;
        if (!okR6) ++totalFail;
        qInfo().noquote() << (okR6 ? "PASS" : "FAIL")
                          << "| review26-6 torch burnout fuse: a torch flipping "
                             + QString::number(kMirrorBurnoutFlips) + " times inside the 60s window "
                             "locks OFF for " + QString::number(kMirrorBurnoutCooldown) + " redstone "
                             "ticks (8s, MC 160gt parity) then re-evaluates - lever-driven NOT rig: "
                             "8th flip (would-be relight under an OFF lever) stays dark = locked, no "
                             "relight within 70 ticks, relight lands in the 73..100 window, circuit "
                             "toggles normally after cooldown; free-running endpoint-dust clock: >=4 "
                             "flips before the fuse (oscillation not over-killed), a 60..100-tick "
                             "continuous dark stretch (lock magnitude), and post-lock flips within "
                             "100 ticks (cooldown expiry restores oscillation - astable circuits "
                             "remain buildable, only PERPETUAL 5Hz hammering is fused; deterministic "
                             "integer counters, PLAN 2-K)";
    });

    // ── P-t870 红石粉中键复制给物品 id 探针（t815 返修：根因不在图标源在 id）──
    //   用户二报「复制红石粉仍非红石粉图标」。根因：pickBlock 把**方块形态** 130 写进 hotbar →
    //   ① 图标走方块段路径（图集瓦片重渲，连接形随电力态变）；② 与红石 tab / 材料段的粉条目（0x224）
    //   id 失配（切槽判定 / 双显面）。t815 只修了 130 的图标渲染源，没修「该给什么 id」。
    //   修 = pickItemIdForBlock 单一权威（130 → 0x224，与 dropId 同源；其余恒自身）。
    //   断言：(a) 行为级——映射函数直调（dust→0x224；石头/发射器/红石矿石恒自身——矿石 pick 给
    //   矿石本体非掉落物，MC 语义）；(b) 源码钉——pickBlock 经 pickItemIdForBlock 路由（captured /
    //   射线门内不可行为直驱，t889/a3 先例）。
    runLegMulti({ "t870 pick-block on redstone dust yields the dust ITEM id: mapping authority returns RedstoneId(0"
        "x224) for the wire block (130) and identity for stone/dispenser/redstone-ore (ore picks its bloc"
        "k, not the drop), and pickBlock routes through pickItemIdForBlock before the hotbar write (sourc"
        "e pin - the t815 icon-source-only fix never touched the id, hotbar held the block-form id so the"
        " icon came from the block-segment atlas and never matched the redstone-tab/material entries)" }, [&]() {
        // (a) 行为级：映射单一权威直调。
        const bool okMap = PlayerController::pickItemIdForBlock(quint8(BR::RedstoneDust))
                               == RecipeRegistry::RedstoneId
                           && PlayerController::pickItemIdForBlock(quint8(BR::Stone)) == int(BR::Stone)
                           && PlayerController::pickItemIdForBlock(quint8(BR::Dispenser)) == int(BR::Dispenser)
                           && PlayerController::pickItemIdForBlock(quint8(BR::RedstoneOre)) == int(BR::RedstoneOre);
        // (b) 源码钉：pickBlock 函数体（滤注释）内 pickItemIdForBlock 路由存在且先于 pickIdToHotbar 落位。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
            const int b0 = t.indexOf(QStringLiteral("void PlayerController::pickBlock()"));
            const int b1 = t.indexOf(QStringLiteral("void PlayerController::pickIdToHotbar(int id)"));
            if (b0 < 0 || b1 <= b0) {
                qInfo().noquote() << "  t870 pickBlock slice miss";
            } else {
                QString body;
                for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                    if (!line.trimmed().startsWith(QLatin1String("//"))) {
                        body += line; body += QLatin1Char('\n');
                    }
                const int iRoute = body.indexOf(QStringLiteral("pickIdToHotbar(pickItemIdForBlock(id));"));
                okPin = iRoute >= 0;
            }
        }
        const bool okT870 = okMap && okPin;
        if (!okT870) ++totalFail;
        qInfo().noquote() << (okT870 ? "PASS" : "FAIL")
                          << "| t870 pick-block on redstone dust yields the dust ITEM id: mapping authority "
                             "returns RedstoneId(0x224) for the wire block (130) and identity for stone/"
                             "dispenser/redstone-ore (ore picks its block, not the drop), and pickBlock "
                             "routes through pickItemIdForBlock before the hotbar write (source pin - the "
                             "t815 icon-source-only fix never touched the id, hotbar held the block-form id "
                             "so the icon came from the block-segment atlas and never matched the redstone-"
                             "tab/material entries)";
    });

    // ── P-t879 活板门双修（行为级 + 源码钉；专用断言不建 rig）──
    //    (a) 木活板门 def 贴图契约：大面（top/bottom）= 180 四镂空板、薄侧边（side/front）= planks(8)
    //        —— 旧全 8（planks 整面实心）= 用户「像木压力板」根因；
    //    (b) 图集契约：AtlasTileCount==185（t1028 起追加到 185；t998 曾追加到 184）且 qrc atlas.png 宽 ==
    //        185×64（瓦片已随 180 重生——陈旧图集 180×64 即红）+ tile 180 / 178 含 alpha 孔（四镂空真透明，
    //        cutout 语义的贴图前提）；
    //    (c) 源码钉：chunkgeometry isCutoutTrapX 同时含 IronTrapdoor 与 WoodTrapdoor（cutout 段
    //        路由——木活板门孔须 alphaCutoff 透视；驱动 ChunkGeometry 需渲染后端，行为级不可密闭，
    //        t870/t889 源码钉先例）+ mesher trapdoor case 木/铁 sideTile 分流（planks/iron_block）。
    runLegMulti({ "t879 trapdoor pair fix: wood trapdoor def swaps large faces to tile 180 (four-hole plank board, "
        "alpha cutout - the old all-planks solid plate read as a wooden pressure plate) with plank thin e"
        "dges, atlas regenerated (185 tiles since t1028 appended 184; t998 appended 181..183 before; stal"
        "e pre-t879 atlas width still fails) with real alpha holes in tiles 178/180, and both trapdoors r"
        "oute to the cutout pass (source pin - holes need alphaCutoff to see through); iron side tiles us"
        "e iron_block / wood planks per family (mesher + runtime icon spec + offline icon)" }, [&]() {
        const BR::BlockDef &wtd = BR::def(BR::WoodTrapdoor);
        const bool okDef = wtd.topTile == 180 && wtd.bottomTile == 180
                           && wtd.sideTile == 8 && wtd.frontTile == 8;
        bool okAtlas = BR::AtlasTileCount == 185; // t1028 起图集随音符盒 tile 184 追加到 185（追加不插中间——存档契约）
        // 测试二进制无 qrc（t815/t838 探针同因：图集资源不在测试 target）→ 直读源树 textures/atlas.png
        //   （构建机源树布局，与源码钉同根路径解析）。
        const QString exeDirA = QCoreApplication::applicationDirPath();
        const QString rootA = QDir(exeDirA + QStringLiteral("/..")).absolutePath();
        QImage atlas(QDir(rootA).absoluteFilePath(QStringLiteral("textures/atlas.png")));
        if (atlas.isNull() || atlas.width() != 185 * 64) {
            okAtlas = false;
            qInfo().noquote() << "  t879 diag: atlas w =" << (atlas.isNull() ? -1 : atlas.width());
        } else {
            int holes180 = 0, holes178 = 0;
            for (int y = 0; y < 64; ++y) {
                if (qAlpha(atlas.pixel(180 * 64 + 20, y)) < 255) ++holes180;   // 孔列 x=4（16 尺度 [3,5] → 64 尺度 x 12..23 中点）
                if (qAlpha(atlas.pixel(178 * 64 + 20, y)) < 255) ++holes178;
            }
            okAtlas = okAtlas && holes180 >= 8 && holes178 >= 8; // 每列两段 3×4px 孔 → ≥8 半透明行
        }
        // (c) 源码钉（滤注释后断言路由谓词文本）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile cf(root + QStringLiteral("/src/World/chunkgeometry.cpp"));
            const QString t = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("isCutoutTrapX ="));
            if (i0 < 0) {
                qInfo().noquote() << "  t879 pin slice miss (chunkgeometry)";
            } else {
                const QString seg = t.mid(i0, 240);
                okPin = seg.contains(QStringLiteral("IronTrapdoor")) && seg.contains(QStringLiteral("WoodTrapdoor"));
            }
            QFile pf(root + QStringLiteral("/src/World/partialblockgeometry.cpp"));
            const QString t2 = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const int j0 = t2.indexOf(QStringLiteral("const int ironSideTile ="));
            if (j0 < 0) {
                qInfo().noquote() << "  t879 pin slice miss (partialblockgeometry)";
                okPin = false;
            } else {
                const QString seg2 = t2.mid(j0, 400);
                okPin = okPin && seg2.contains(QStringLiteral("tileIndex(BlockRegistry::Planks"))
                                              && seg2.contains(QStringLiteral("IronBlock"));
            }
        }
        const bool okT879 = okDef && okAtlas && okPin;
        if (!okT879) {
            ++totalFail;
            qInfo().noquote() << "  t879 diag: def" << wtd.topTile << wtd.bottomTile << wtd.sideTile
                              << wtd.frontTile << "atlas" << okAtlas << "pin" << okPin;
        }
        qInfo().noquote() << (okT879 ? "PASS" : "FAIL")
                          << "| t879 trapdoor pair fix: wood trapdoor def swaps large faces to tile 180 "
                             "(four-hole plank board, alpha cutout - the old all-planks solid plate read as "
                             "a wooden pressure plate) with plank thin edges, atlas regenerated (185 tiles "
                             "since t1028 appended 184; t998 appended 181..183 before; stale pre-t879 atlas "
                             "width still fails) with "
                             "real alpha holes in tiles 178/180, and both trapdoors route to the cutout "
                             "pass (source pin - holes need alphaCutoff to see through); iron side tiles use "
                             "iron_block / wood planks per family (mesher + runtime icon spec + offline icon)";
    });

    // ── P-t880 异形物品 3D 模型族（ItemShapeGeometry 行为级 + 红石粉粉堆源码钉）──
    //    (a) 几何契约：每族 blockId 建形状后断言**顶点数 + 形心居中 bounds**——活板门 24 顶点（单薄板，
    //        yMax=3/32）/ 台阶 24（半高盒 yMax=0.25）/ 楼梯 48（两盒）/ 雪层 24（yMax=1/16）/ 火把 24
    //        （细柱 xMax=1/16 —— cell [7/16,9/16] 居中即 ±1/16）/ 草丛 16 顶点（2 对角片 × 双面 2 pass，
    //        双面 = 4 quad → 16）+ 每片索引 12×2（cross 双面发）。这些断言钉「形状真建了 + 形心居中口径」，
    //        防退化成满立方（24 顶点但 bounds ±0.5 —— bounds 断言抓它）。
    //    (b) 红石粉 item 图标改粉堆瓦片 167（dust_dot_off；旧 166 线向 = 「一条线」观感根因）——运行期
    //        spec 内部函数不可直调 → 源码钉 flatSpec(167) 落位（滤注释）。
    runLegMulti({ "t880 item 3D family: ItemShapeGeometry builds real partial shapes (trapdoor plate / slab half-bo"
        "x / stairs two-box / snow 1/8 / torch 2/16-column / grass double-sided cross / enchant 0.75 box)"
        " centered on each shape's mid-height with vertex-count + bounds contracts pinned per family, and"
        " the redstone dust item icon renders the dust-dot pile tile 167 instead of the wire line 166 (so"
        "urce pin)" }, [&]() {
        bool ok = true;
        {
            ItemShapeGeometry g;
            struct Expect { int blockId; int vCount; float yMax; float xMax; };
            const Expect exp[] = {
                { int(BR::WoodTrapdoor), 24, 3.0f / 32.0f + 0.001f, 0.501f },
                { int(BR::WoodSlab),     24, 0.25f + 0.001f,        0.501f },
                { int(BR::WoodStairs),   48, 0.501f,                0.501f },
                { int(BR::SnowLayer),    24, 1.0f / 16.0f + 0.001f, 0.501f },
                { int(BR::Torch),        24, 0.313f,                1.0f / 16.0f + 0.001f },
                { int(BR::TallGrass),    16, 0.501f,                0.501f },
                { int(BR::EnchantingTable), 24, 0.376f,             0.501f },
            };
            for (const Expect &e : exp) {
                g.setBlockId(e.blockId);
                const QByteArray vd = g.vertexData();
                const int vCount = int(vd.size()) / 20; // stride = pos3+uv2 = 5 float = 20B（类注释契约）
                const QVector3D bMax = g.boundsMax();
                if (vCount != e.vCount || bMax.y() > e.yMax || bMax.x() > e.xMax) {
                    ok = false;
                    qInfo().noquote() << "  t880 diag: id" << e.blockId << "v" << vCount
                                      << "expect" << e.vCount << "yMax" << bMax.y() << "xMax" << bMax.x();
                }
            }
        }
        // (b) 源码钉：红石粉 flatSpec 用粉堆瓦片 167（非 def.topTile 166 线向）。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile rf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
            const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("case BlockRegistry::RedstoneDust:"));
            const int i1 = t.indexOf(QStringLiteral("case BlockRegistry::WheatCrop:"), i0);
            if (i0 < 0 || i1 <= i0) {
                ok = false;
                qInfo().noquote() << "  t880 pin slice miss (dust flatSpec)";
            } else {
                const QString seg = t.mid(i0, i1 - i0);
                ok = ok && seg.contains(QStringLiteral("flatSpec(167)"))
                     && !seg.contains(QStringLiteral("flatSpec(d.topTile)"));
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t880 item 3D family: ItemShapeGeometry builds real partial shapes (trapdoor "
                             "plate / slab half-box / stairs two-box / snow 1/8 / torch 2/16-column / grass "
                             "double-sided cross / enchant 0.75 box) centered on each shape's mid-height with "
                             "vertex-count + bounds contracts pinned per family, and the redstone dust item icon "
                             "renders the dust-dot pile tile 167 instead of the wire line 166 (source pin)";
    });

    // ── P-t925 3D 模型扩面第二批（t880 口径：每新形状 顶点数 + 包络 断言；栅栏 / 门 / 机关 / cross 扩面）──
    //    几何契约（ItemShapeGeometry 直调）：栅栏族 = 木/云杉 柱 + 四向双档 9 盒（216 顶点；t991 圆石墙
    //    形制分家 = 柱+凸缘+四向拱 6 盒 144）；门族 = 合态 +X 边
    //    3/16 薄板（24 顶点，xMin ≥ 0.31 = 薄板非对称防退化满格）；cross 族扩面（枯灌木 / 小麦 / 红白蘑菇 /
    //    蛛网 / 红石火把）= 2 对角片 × 双面 16 顶点；拉杆 = mechBoxes 底座 + 两段摆棍 3 盒（72）且形心系
    //    yMax ≈ 0（棍顶 8/16）；按钮 = 单盒 24 且 yMax = -0.375（盒 y[0,2/16] 贴地小凸块，负 yMax 防退化
    //    满格）。另有两侧家族表同步源码钉（ResourceBrowser selectedIsItem3D ∪ Main.qml isItem3DFamily
    //    都含 t925 全部 15 个族员——加族员须同步两处的契约钉死）。
    runLegMulti({ "t925 item 3D family batch 2: fences (wood/spruce post + four double arms, 9 boxes; t991 cobble w"
        "all split to its own 6-box post+flange+arch shape), doors (3/16 plate at +X edge with family-pla"
        "nks thin sides), lever (mechBoxes base + stick, same source as world mesher) and buttons (single"
        " 2/16 nub) build real multi-box shapes, the cross batch (dead bush / mature wheat / red+white mu"
        "shroom / cobweb / redstone torch) builds double-sided X-quads, all with per-shape vertex+bounds "
        "contracts, and both family tables (viewer + drop) are source-pinned to contain all 15 new ids" }, [&]() {
        bool ok = true;
        {
            ItemShapeGeometry g;
            struct Expect { int blockId; int vCount; float yMax; float xMax; float xMin; };
            const Expect exp[] = {
                // 栅栏族（木 17 / 云杉 88）：柱 + 四向双档 = 9 盒 × 24。t991：柱 1.5 格高 → 形心居中
                //   yShift -0.75，包络 yMax = 1.5-0.75 = 0.75。
                { int(BR::WoodFence),   216, 0.751f, 0.501f, -0.501f },
                { int(BR::SpruceFence), 216, 0.751f, 0.501f, -0.501f },
                // t991 圆石墙（60）形制分家：柱 + 凸缘 + 四向拱 = 6 盒 × 24，柱高 1.0 → yMax = 0.5。
                { int(BR::CobbleFence), 144, 0.501f, 0.501f, -0.501f },
                // 门族（木 19 / 云杉 89 / 铁 135）：单薄板；x ∈ [0.3125, 0.5]（+X 边厚 3/16 居中后非对称）。
                //   t965 订正：铁门 135（家族表旧字面量 71 系错 id=青色羊毛；几何类本就用 BR::IronDoor 不受影响）。
                { int(BR::WoodDoor),   24, 0.501f, 0.501f, 0.31f },
                { int(BR::SpruceDoor), 24, 0.501f, 0.501f, 0.31f },
                { int(BR::IronDoor),   24, 0.501f, 0.501f, 0.31f },
                // cross 族扩面：16 顶点（2 片 × 双面）。
                { int(BR::WheatCrop),     16, 0.501f, 0.501f, -0.501f },
                { int(BR::DeadBush),      16, 0.501f, 0.501f, -0.501f },
                { int(BR::Mushroom),      16, 0.501f, 0.501f, -0.501f },
                { int(BR::BrownMushroom), 16, 0.501f, 0.501f, -0.501f },
                { int(BR::Cobweb),        16, 0.501f, 0.501f, -0.501f },
                { int(BR::RedstoneTorch), 16, 0.501f, 0.501f, -0.501f },
                // 拉杆（112）：底座 + 两段摆棍 = 3 盒；棍顶 8/16 → 形心系 yMax ≈ 0.0（上界 0.001 + 容差）。
                { int(BR::Lever),       72, 0.002f, 0.501f, -0.501f },
                // 按钮（113/114）：单盒 y[0,2/16] → 形心系 yMax = -0.375（负值 = 贴地小凸块）。
                { int(BR::WoodButton),  24, -0.374f, 0.501f, -0.501f },
                { int(BR::StoneButton), 24, -0.374f, 0.501f, -0.501f },
            };
            for (const Expect &e : exp) {
                g.setBlockId(e.blockId);
                const QByteArray vd = g.vertexData();
                const int vCount = int(vd.size()) / 20; // stride 5 float = 20B（t880 同契约）
                const QVector3D bMax = g.boundsMax();
                const QVector3D bMin = g.boundsMin();
                if (vCount != e.vCount || bMax.y() > e.yMax || bMax.x() > e.xMax || bMin.x() < e.xMin) {
                    ok = false;
                    qInfo().noquote() << "  t925 diag: id" << e.blockId << "v" << vCount
                                      << "expect" << e.vCount << "yMax" << bMax.y()
                                      << "xMax" << bMax.x() << "xMin" << bMin.x();
                }
            }
        }
        // 家族表同步源码钉：查看器侧 QML 谓词 + 掉落侧家族权威都含 t925 全部 15 个族员（字面量逐一）。
        //   t965 订正：铁门 135（旧 71 系错 id=青色羊毛，两侧 QML 同步订正；查看器侧另新增 127
        //   动力轨——掉落物侧排除清单不变，故 127 不进本同步表）。
        //   t1027 合法演化：掉落侧家族表自 Main.qml isItem3DFamily 字面量收编 C++ 单一权威
        //   （ItemEntityManager::isItem3DFamily switch 表，25 id 原样；Main.qml 函数退化薄委托）——
        //   掉落侧锚串同步迁移到 itementitymanager.cpp 表体（case <id> 逐一），t965 锚串演化先例。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile bf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
            QFile mf(root + QStringLiteral("/src/Game/itementitymanager.cpp"));
            const QString browser = bf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bf.readAll()) : QString();
            const QString mainQml = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const int iSel = browser.indexOf(QStringLiteral("selectedIsItem3D:"));
            const int iFn = mainQml.indexOf(QStringLiteral("bool ItemEntityManager::isItem3DFamily"));
            const int iFnEnd = mainQml.indexOf(QLatin1Char('}'), iFn);
            if (iSel < 0 || iFn < 0 || iFnEnd <= iFn || browser.isEmpty()) {
                ok = false;
                qInfo().noquote() << "  t925 pin slice miss";
            } else {
                const QString selBlock = browser.mid(iSel, 1600);  // 谓词绑定块窗口（现长 ~750，余量防漂移误红）
                const QString fnBlock = mainQml.mid(iFn, iFnEnd - iFn);
                const QList<int> ids = { 43, 25, 17, 60, 88, 19, 135, 89, 115, 48, 102, 129, 112, 113, 114 };
                for (int id : ids) {
                    const QString lit = QStringLiteral("=== ") + QString::number(id);
                    const QString cas = QStringLiteral("case ") + QString::number(id);
                    if (!selBlock.contains(lit) || !fnBlock.contains(cas)) {
                        ok = false;
                        qInfo().noquote() << "  t925 pin miss id" << id
                                          << "browser" << selBlock.contains(lit)
                                          << "drop" << fnBlock.contains(cas);
                    }
                }
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t925 item 3D family batch 2: fences (wood/spruce post + four double arms, "
                             "9 boxes; t991 cobble wall split to its own 6-box post+flange+arch shape), "
                             "doors (3/16 plate at +X edge with family-planks thin sides), lever (mechBoxes "
                             "base + stick, same source as world mesher) and buttons (single 2/16 nub) build "
                             "real multi-box shapes, the cross batch (dead bush / mature wheat / red+white "
                             "mushroom / cobweb / redstone torch) builds double-sided X-quads, all with "
                             "per-shape vertex+bounds contracts, and both family tables (viewer + drop) are "
                             "source-pinned to contain all 15 new ids";
    });

    // ── P-t924 附魔台查看器 = 世界放置形态 对齐契约（R19.16 t924；源码钉——QML 渲染分支 headless 不可
    //    行为级断言，t880 (b) 先例）──
    //    用户报「查看器重建整格黑曜石且抖动」根因 = review27 #4：附魔台 94 不在 isPartialBlock →
    //    selectedIsCube 对 94 仍 true，满格 BlockCube（六面黑曜石深色瓦片 = 「整格黑曜石」）与
    //    ItemShapeGeometry 0.75 矮盒叠渲 z-fight（= 「抖动」）——修法为家族互斥钉死。本探针锁五面契约：
    //    (a) 查看器 selectedIsItem3D 家族覆盖 94（94 走 ItemShapeGeometry 分支）；
    //    (b) 立方分支 visible 排除 item3D 家族（互斥防叠渲回归 = 症状本体防复发）；
    //    (c) ItemShapeGeometry 附魔台 case 真用 0.75 矮盒（partialblockgeometry 同高；防「修回整格」）；
    //    (d) 悬浮书坐标三处同源：查看器 etBookNode 与掉落物 dropBookNode 同为 y=0.46、页 scale
    //        0.38×0.03×0.46 ×2（review27 #9 钉的「同款局部坐标」契约）。
    runLegMulti({ "t924 enchant-table viewer/world parity: browser routes 94 to the item3D family, cube branch excl"
        "udes the family (z-fight/full-cube regression pin - the reported 'full obsidian cube + jitter' w"
        "as the review27 #4 double render), ItemShapeGeometry keeps the 0.75 half-height box (same as par"
        "tialblockgeometry), and the hovering book coords (y=0.46, pages 0.38x0.03x0.46) stay identical b"
        "etween viewer and drop (review27 #9 contract)" }, [&]() {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile bf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        QFile gf(root + QStringLiteral("/src/Renderer/itemshapegeometry.cpp"));
        const QString browser = bf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bf.readAll()) : QString();
        const QString mainQml = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        const QString geo = gf.open(QIODevice::ReadOnly) ? QString::fromUtf8(gf.readAll()) : QString();
        if (browser.isEmpty() || mainQml.isEmpty() || geo.isEmpty()) {
            ok = false;
            qInfo().noquote() << "  t924 diag: source slice miss";
        } else {
            // (a) 查看器家族含 94（selectedIsItem3D 绑定块内字面量；600 字符窗口防在文件它处命中漂移）。
            const int iSel = browser.indexOf(QStringLiteral("selectedIsItem3D:"));
            ok = ok && iSel >= 0
                 && browser.indexOf(QStringLiteral("=== 94"),
                                    iSel) > iSel
                 && browser.indexOf(QStringLiteral("=== 94"), iSel) < iSel + 600;
            // (b) 立方分支 + 大图标分支均排除 item3D（互斥 ≥2 处）。
            ok = ok && browser.count(QStringLiteral("!root.selectedIsItem3D")) >= 2;
            // (c) 附魔台 case 0.75 矮盒（case 切片内含 0.75f 且不含「1.0f, 1.0f, 1.0f,」满格兜底主路径）。
            const int iCase = geo.indexOf(QStringLiteral("EnchantingTable"));
            const int iCaseEnd = geo.indexOf(QStringLiteral("} else {"), iCase);
            ok = ok && iCase >= 0 && iCaseEnd > iCase
                 && geo.mid(iCase, iCaseEnd - iCase).contains(QStringLiteral("0.75f"));
            // (d) 书坐标两文件同源（review27 #9 契约：书心 y=0.46 + 页 scale 0.38×0.03×0.46 各 ×2）。
            //     t1032 锚串合法演化（t965 先例）：dropBookNode 上移 entRoot 直属（94 入形状桶后书
            //     保留 delegate 渲染），0.3 父级缩小并入本节点——Main.qml 侧锚串换为
            //     `entRoot.bobY + 0.46 * 0.3`（同一 0.46 书心常量 × 0.3 补偿，世界位逐位不变）；
            //     查看器侧 etBookNode 字面量不动。
            ok = ok && browser.contains(QStringLiteral("Qt.vector3d(0, 0.46, 0)"))
                 && mainQml.contains(QStringLiteral("entRoot.bobY + 0.46 * 0.3"))
                 && browser.count(QStringLiteral("Qt.vector3d(0.38, 0.03, 0.46)")) >= 2
                 && mainQml.count(QStringLiteral("Qt.vector3d(0.38, 0.03, 0.46)")) >= 2;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t924 enchant-table viewer/world parity: browser routes 94 to the "
                             "item3D family, cube branch excludes the family (z-fight/full-cube "
                             "regression pin - the reported 'full obsidian cube + jitter' was the "
                             "review27 #4 double render), ItemShapeGeometry keeps the 0.75 "
                             "half-height box (same as partialblockgeometry), and the hovering "
                             "book coords (y=0.46, pages 0.38x0.03x0.46) stay identical between "
                             "viewer and drop (review27 #9 contract)";
    });

    // ── t822 铁砧附魔丢失实机复现二探针（R19.13）：t792 桩外两段真链补测 ──
    //   用户再报「附魔物品放入铁砧 UI 即消失附魔、取出变普通」；t792 实机探针（qml.exe 驱动真实
    //   AnvilUI.qml + InventoryOps.js，11 放入路径）47/47 全过，但其 Hotbar 是 **QML 桩**
    //   （build/_anvil_probe/VoxelSandbox/Hotbar.qml 语义复刻，非 C++ hotbar.cpp 本体），且
    //   **存档序列化 round-trip（t622 gatherPlayerState/applyPlayerState 落盘 enchants/name）从未
    //   有任何自动化探针**。本探针把两段桩外真链补进矩阵：
    //   (A) 真 Hotbar VM 光标序列镜像（AnvilUI.slotLeft :253-261 与 takeProduct 落定段 :894-900 的
    //       C++ 逐行等价——同一调用序：writeSlot 清源 → heldBlock → heldCount → heldDurability →
    //       setHeldEnchants → heldCustomName；顺序敏感：setHeldBlock 切新 id 清附魔+清名，附魔回填必须
    //       在其后）。含「同 id 早退」边角（光标已持同 id 素品、槽内附魔品的 pickup 序列——早退不清
    //       字段、后续 setter 逐个覆写，探针断言实 VM 语义与桩一致）。类别覆盖工具 / 护甲 / 附魔书 +
    //       四条满配多附魔（4 ench 全占用）。
    //   ⚠ 同步义务（review24 #10）：A 段是 AnvilUI.qml 调用序的**手工镜像**——改 slotLeft / takeProduct /
    //   returnAnvilToHotbar 的 setter 调用序（增删 / 换序，典型丢失形态如漏 heldDurability）时**必须同步
    //   本探针 A1-A4**，否则探针按旧序继续 PASS 掩盖真回归；本段也刻意不镜像 InventoryOps.resolveClick
    //   的 JS 分派（那层由 t792 qml.exe 实机探针覆盖）。中期方案（暂不做）：qml.exe 挂真 Hotbar C++ 对象
    //   替换 t792 桩，消掉拼接缝。
    //   (B) 真 WorldStore SQLite round-trip（Main.qml gatherPlayerState :665-688 的精确 map 形状
    //       version 3 / applyPlayerState :634-655 的精确回灌调用）：hotbar 9 + main 27 + armor 4
    //       每槽 id/count/durability/enchants[4]/name 落盘 → 关库重开 → 读回 → 灌入新 Hotbar VM
    //       逐字段比对（含空槽）。存档库用临时目录绝对路径（dbPath 对绝对入参直通，不污染 saves/）。
    //   全 PASS = 桩外真链同判完好，用户症状按 t814 版本戳方法论走复现文档分流。
    bool okA_pickup = true, okA_sameId = true, okA_place = true, okA_return = true;
    runLegMulti({ "t822a real-Hotbar anvil cursor chain (mock-VM residual divergence closed): AnvilUI.slotLeft/take"
        "Product/returnAnvilToHotbar call sequences mirrored onto the actual C++ Hotbar VM - pickup clear"
        "s-then-refills cursor in order (setHeldBlock new-id wipes ench/name, setters after), same-id pic"
        "kup early-return keeps stale fields then overwrites one-by-one (ench MUST land), full-stack plac"
        "e into main, addToAny close-panel return for armor piece + enchanted book (new-slot branch write"
        "s ench/name, cap-1 never merges); covered categories tool/armor/book + multi-ench (2-slot pick, "
        "4-field arrays)" }, [&]() {
        Hotbar vm;
        const int pick = ToolRegistry::PickaxeIron;                  // 0x102
        const int chest = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
        const int book = RecipeRegistry::EnchantedBookId;            // 0x227
        const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
        const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
        const int pickDur = ToolRegistry::maxDurability(pick) - 5;   // 磨损实例（非满耐久）
        vm.setStack(2, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "我的神镐");

        // (A1) 普通拾取：光标空 → 槽 2 附魔镐上光标（AnvilUI.slotLeft 拾取臂逐行镜像）。
        vm.setStack(2, 0, 0, 0);                                     // writeSlot 清源槽
        vm.setHeldBlock(pick);                                       // 切新 id：清附魔/清名/满耐久
        vm.setHeldCount(1);
        vm.setHeldDurability(pickDur);                               // 覆写为槽内实例耐久
        vm.setHeldEnchants(QVariantList{eff3, unb2, 0, 0});          // 覆写为槽内实例附魔
        vm.setHeldCustomName(QStringLiteral("我的神镐"));
        {
            const QVariantList he = vm.heldEnchants();
            okA_pickup = vm.heldBlock() == pick && vm.heldDurability() == pickDur
                          && vm.heldCustomName() == QStringLiteral("我的神镐")
                          && int(he.size()) == 4 && he.at(0).toInt() == eff3 && he.at(1).toInt() == unb2
                          && he.at(2).toInt() == 0 && he.at(3).toInt() == 0;
            if (!okA_pickup)
                qInfo().noquote() << "  [t822 A1 diag] held=" << vm.heldBlock() << "dur=" << vm.heldDurability()
                                  << "ench=" << he;
        }
        // (A2) 放入（resolveClick B 整栈放置臂镜像）：光标 → main 槽 5（铁砧 A 槽是 QML 本地数组，
        //   入槽写的就是这些 VM 读值——readSlot/writeSlot 经同一组访问器）。
        vm.mainSetStack(5, vm.heldBlock(), vm.heldCount(), vm.heldDurability(), vm.heldEnchants(), vm.heldCustomName());
        vm.setHeldBlock(0);                                          // 光标清（AnvilUI r.heldId=0 分支）
        {
            const QVariantList me = vm.mainEnchantsAt(5);
            okA_place = vm.mainBlockIdAt(5) == pick && vm.mainCountAt(5) == 1
                         && vm.mainDurabilityAt(5) == pickDur
                         && vm.mainCustomNameAt(5) == QStringLiteral("我的神镐")
                         && int(me.size()) == 4 && me.at(0).toInt() == eff3 && me.at(1).toInt() == unb2
                         && me.at(2).toInt() == 0 && me.at(3).toInt() == 0;
            if (!okA_place)
                qInfo().noquote() << "  [t822 A2 diag] main5=" << vm.mainBlockIdAt(5) << "dur=" << vm.mainDurabilityAt(5)
                                  << "ench=" << me << "name=" << vm.mainCustomNameAt(5);
        }
        // (A3) 同 id 早退边角：光标持同 id **素品**（无附魔满耐久），拾取槽内**附魔品**——
        //   setHeldBlock 早退不清字段，后续四个 setter 逐个覆写 → 附魔必须落上（桩同语义实链验证）。
        vm.setHeldBlock(pick);
        vm.setHeldCount(1);
        vm.setHeldDurability(ToolRegistry::maxDurability(pick));
        vm.setHeldEnchants(QVariantList{0, 0, 0, 0});
        vm.setHeldCustomName(QString());
        vm.setStack(6, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "第二把");
        vm.setStack(6, 0, 0, 0);                                     // 清源
        vm.setHeldBlock(pick);                                       // 同 id → 早退（旧字段保留）
        vm.setHeldCount(1);
        vm.setHeldDurability(pickDur);
        vm.setHeldEnchants(QVariantList{eff3, unb2, 0, 0});
        vm.setHeldCustomName(QStringLiteral("第二把"));
        {
            const QVariantList he2 = vm.heldEnchants();
            okA_sameId = he2.at(0).toInt() == eff3 && he2.at(1).toInt() == unb2
                          && vm.heldDurability() == pickDur && vm.heldCustomName() == QStringLiteral("第二把");
            if (!okA_sameId)
                qInfo().noquote() << "  [t822 A3 diag] held dur=" << vm.heldDurability()
                                  << "ench=" << he2 << "name=" << vm.heldCustomName();
        }
        // (A4) 关包归还（AnvilUI.returnAnvilToHotbar → addToAny 全参镜像）：护甲 + 附魔书整件归还，
        //   空槽开新分支须写附魔 + 名（合并不搬实例元数据；cap=1 物品永不走合并）。
        vm.mainSetStack(6, chest, 1, 40, QVariantList{(EnchantRegistry::Protection << 8) | 4, unb2, 0, 0}, "守护者");
        vm.mainSetStack(7, book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        const int leftChest = vm.addToAny(chest, 1, 40, QVariantList{(EnchantRegistry::Protection << 8) | 4, unb2, 0, 0}, "守护者");
        const int leftBook = vm.addToAny(book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        bool foundChest = false, foundBook = false;
        for (int i = 0; i < vm.slotCount(); ++i) {
            if (vm.blockIdAt(i) == chest) {
                const QVariantList ce = vm.enchantsAt(i);
                foundChest = vm.customNameAt(i) == QStringLiteral("守护者") && vm.durabilityAt(i) == 40
                              && ce.at(0).toInt() == ((EnchantRegistry::Protection << 8) | 4) && ce.at(1).toInt() == unb2;
            }
            if (vm.blockIdAt(i) == book) {
                const QVariantList be = vm.enchantsAt(i);
                foundBook = be.at(0).toInt() == ((EnchantRegistry::Sharpness << 8) | 5)
                             && be.at(1).toInt() == ((EnchantRegistry::FireAspect << 8) | 1);
            }
        }
        okA_return = leftChest == 0 && leftBook == 0 && foundChest && foundBook;
        if (!okA_return)
            qInfo().noquote() << "  [t822 A4 diag] leftChest=" << leftChest << "leftBook=" << leftBook
                              << "foundChest=" << foundChest << "foundBook=" << foundBook;
        const bool okT822a = okA_pickup && okA_place && okA_sameId && okA_return;
        if (!okT822a) ++totalFail;
        qInfo().noquote() << (okT822a ? "PASS" : "FAIL")
                          << "| t822a real-Hotbar anvil cursor chain (mock-VM residual divergence closed): "
                             "AnvilUI.slotLeft/takeProduct/returnAnvilToHotbar call sequences mirrored onto the "
                             "actual C++ Hotbar VM - pickup clears-then-refills cursor in order (setHeldBlock "
                             "new-id wipes ench/name, setters after), same-id pickup early-return keeps stale "
                             "fields then overwrites one-by-one (ench MUST land), full-stack place into main, "
                             "addToAny close-panel return for armor piece + enchanted book (new-slot branch "
                             "writes ench/name, cap-1 never merges); covered categories tool/armor/book + "
                             "multi-ench (2-slot pick, 4-field arrays)";
    });

    // (B) 真 WorldStore SQLite round-trip：gatherPlayerState 精确形状落盘 → 关库重开 → applyPlayerState
    //   精确调用回灌 → 全字段比对。四条满配剑（4 ench 全占用）覆盖多附魔上界。
    bool okB_build = true, okB_round = true, okB_apply = true;
    runLegMulti({ "t822b real-WorldStore SQLite enchant round-trip (serialization leg never probed before): exact g"
        "atherPlayerState v3 map shape (hotbar9+main27+armor4, per-slot id/count/durability/enchants[4]/n"
        "ame) saved via savePlayerData -> close -> reopen -> loadPlayerData -> JSON field compare -> exac"
        "t applyPlayerState calls into a fresh Hotbar -> all-slot field equality vs source VM; covered mu"
        "lti-ench 4/4-full sword, enchanted book, armor piece with partial durability + custom names + em"
        "pty slots (db on temp-dir absolute path, saves/ untouched)" }, [&]() {
        Hotbar vm2;
        const int pick = ToolRegistry::PickaxeIron;
        const int sword = ToolRegistry::SwordIron;
        const int chest = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
        const int book = RecipeRegistry::EnchantedBookId;
        const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
        const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
        const int sharp3 = (EnchantRegistry::Sharpness << 8) | 3;
        const int kb2 = (EnchantRegistry::Knockback << 8) | 2;
        const int fire2 = (EnchantRegistry::FireAspect << 8) | 2;
        const int unb3 = (EnchantRegistry::Unbreaking << 8) | 3;
        const int prot4 = (EnchantRegistry::Protection << 8) | 4;
        const int pickDur = ToolRegistry::maxDurability(pick) - 5;
        const int swordDur = ToolRegistry::maxDurability(sword) - 9;
        const int chestDur = ArmorRegistry::maxDurability(chest) - 17;
        vm2.setStack(2, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "我的神镐");
        vm2.setStack(3, book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        vm2.mainSetStack(5, sword, 1, swordDur, QVariantList{sharp3, kb2, fire2, unb3}, "勇者之剑");
        vm2.mainSetStack(6, chest, 1, chestDur, QVariantList{prot4, unb2, 0, 0}, "守护者");
        vm2.armorSetStack(1, chest, 1, chestDur - 3, QVariantList{(EnchantRegistry::Protection << 8) | 3, unb2, 0, 0}, "");

        // gather（Main.qml gatherPlayerState :665-688 精确镜像：每槽五字段 + version 3）。
        QVariantMap data;
        QVariantList hotbarArr, mainArr, armorArr;
        for (int i = 0; i < vm2.slotCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.blockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.countAt(i));
            s.insert(QStringLiteral("durability"), vm2.durabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.enchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.customNameAt(i));
            hotbarArr.append(s);
        }
        for (int i = 0; i < vm2.mainCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.mainBlockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.mainCountAt(i));
            s.insert(QStringLiteral("durability"), vm2.mainDurabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.mainEnchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.mainCustomNameAt(i));
            mainArr.append(s);
        }
        for (int i = 0; i < vm2.armorCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.armorBlockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.armorCountAt(i));
            s.insert(QStringLiteral("durability"), vm2.armorDurabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.armorEnchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.armorCustomNameAt(i));
            armorArr.append(s);
        }
        data.insert(QStringLiteral("version"), 3);
        data.insert(QStringLiteral("px"), 40); data.insert(QStringLiteral("py"), 44); data.insert(QStringLiteral("pz"), 40);
        data.insert(QStringLiteral("yaw"), 0); data.insert(QStringLiteral("pitch"), -42);
        data.insert(QStringLiteral("mode"), 0);
        data.insert(QStringLiteral("health"), 20); data.insert(QStringLiteral("hunger"), 20);
        data.insert(QStringLiteral("xp"), 7);
        data.insert(QStringLiteral("selectedSlot"), 2);
        data.insert(QStringLiteral("hotbar"), hotbarArr);
        data.insert(QStringLiteral("main"), mainArr);
        data.insert(QStringLiteral("armor"), armorArr);

        // 落盘 → 关库 → 重开 → 读回（临时目录绝对路径；dbPath 对绝对入参直通，不污染 saves/）。
        //   review24 #10：文件名拼 PID——固定名两实例并发时 Windows 对被占用文件的 QFile::remove 静默
        //   失败 → 两进程共库互相 INSERT OR REPLACE 覆盖 / 环境性假 FAIL（数据形状相同还可能巧合双 PASS
        //   掩盖竞态）；PID 后缀按进程隔离（本进程退出后的遗留文件仍由两次 remove + 下轮覆盖清）。
        WorldStore store;
        const QString dbAbs = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t822_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbAbs);
        okB_build = store.openWorld(dbAbs) && store.savePlayerData(data);
        store.closeWorld();
        QVariantMap back;
        if (okB_build) {
            okB_build = store.openWorld(dbAbs);
            if (okB_build) back = store.loadPlayerData();
            store.closeWorld();
        }
        QFile::remove(dbAbs);
        if (!okB_build) {
            qInfo().noquote() << "  [t822 B diag] open/save/reopen failed";
        } else {
            // JSON 读回逐字段（toInt 显式转换：JSON 数字经 toVariant 可能成 double，勿依赖 QVariant==）。
            const auto slotEq = [](const QVariantMap &s, int id, int count, int dur, int e0, int e1, int e2, int e3, const QString &name) {
                const QVariantList e = s.value(QStringLiteral("enchants")).toList();
                return s.value(QStringLiteral("id")).toInt() == id
                        && s.value(QStringLiteral("count")).toInt() == count
                        && s.value(QStringLiteral("durability")).toInt() == dur
                        && s.value(QStringLiteral("name")).toString() == name
                        && int(e.size()) == 4 && e.at(0).toInt() == e0 && e.at(1).toInt() == e1
                        && e.at(2).toInt() == e2 && e.at(3).toInt() == e3;
            };
            const QVariantList hb = back.value(QStringLiteral("hotbar")).toList();
            const QVariantList mn = back.value(QStringLiteral("main")).toList();
            const QVariantList ar = back.value(QStringLiteral("armor")).toList();
            okB_round = int(hb.size()) == vm2.slotCount() && int(mn.size()) == vm2.mainCount() && int(ar.size()) == vm2.armorCount()
                          && slotEq(hb.at(0).toMap(), 0, 0, 0, 0, 0, 0, 0, QString())
                          && slotEq(hb.at(2).toMap(), pick, 1, pickDur, eff3, unb2, 0, 0, QStringLiteral("我的神镐"))
                          && slotEq(hb.at(3).toMap(), book, 1, 0, (EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0, QString())
                          && slotEq(mn.at(5).toMap(), sword, 1, swordDur, sharp3, kb2, fire2, unb3, QStringLiteral("勇者之剑"))
                          && slotEq(mn.at(6).toMap(), chest, 1, chestDur, prot4, unb2, 0, 0, QStringLiteral("守护者"))
                          && slotEq(mn.at(26).toMap(), 0, 0, 0, 0, 0, 0, 0, QString())
                          && slotEq(ar.at(1).toMap(), chest, 1, chestDur - 3, (EnchantRegistry::Protection << 8) | 3, unb2, 0, 0, QString());
            if (!okB_round) {
                qInfo().noquote() << "  [t822 B diag] hbN=" << hb.size() << "mnN=" << mn.size() << "arN=" << ar.size()
                                  << " hb2=" << hb.at(2).toMap() << " mn5=" << mn.at(5).toMap() << " ar1=" << ar.at(1).toMap();
            }
            // apply（Main.qml applyPlayerState :634-655 精确镜像）→ 新 VM 逐字段与 vm2 比对。
            Hotbar vm3;
            for (int i = 0; i < 9; ++i) {
                const QVariantMap s = hb.at(i).toMap();
                vm3.setStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                             s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                             s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                             s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            for (int i = 0; i < 27; ++i) {
                const QVariantMap s = mn.at(i).toMap();
                vm3.mainSetStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                                 s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                                 s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                 s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            for (int i = 0; i < 4; ++i) {
                const QVariantMap s = ar.at(i).toMap();
                vm3.armorSetStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                                  s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                                  s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                  s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            const auto vmEq = [&vm2, &vm3](bool armor, int i) {
                const int idA = armor ? vm2.armorBlockIdAt(i) : (i < 9 ? vm2.blockIdAt(i) : vm2.mainBlockIdAt(i - 9));
                const int idB = armor ? vm3.armorBlockIdAt(i) : (i < 9 ? vm3.blockIdAt(i) : vm3.mainBlockIdAt(i - 9));
                // review24 #10 顺带：补比 count 字段——「JSON→VM 灌入丢 count」回归此前在 VM 级漏检
                //   （JSON 级 slotEq 可兜存档层，VM 级补齐后两段都有防线）。
                const int cntA = armor ? vm2.armorCountAt(i) : (i < 9 ? vm2.countAt(i) : vm2.mainCountAt(i - 9));
                const int cntB = armor ? vm3.armorCountAt(i) : (i < 9 ? vm3.countAt(i) : vm3.mainCountAt(i - 9));
                const int durA = armor ? vm2.armorDurabilityAt(i) : (i < 9 ? vm2.durabilityAt(i) : vm2.mainDurabilityAt(i - 9));
                const int durB = armor ? vm3.armorDurabilityAt(i) : (i < 9 ? vm3.durabilityAt(i) : vm3.mainDurabilityAt(i - 9));
                const QString nmA = armor ? vm2.armorCustomNameAt(i) : (i < 9 ? vm2.customNameAt(i) : vm2.mainCustomNameAt(i - 9));
                const QString nmB = armor ? vm3.armorCustomNameAt(i) : (i < 9 ? vm3.customNameAt(i) : vm3.mainCustomNameAt(i - 9));
                const QVariantList eA = armor ? vm2.armorEnchantsAt(i) : (i < 9 ? vm2.enchantsAt(i) : vm2.mainEnchantsAt(i - 9));
                const QVariantList eB = armor ? vm3.armorEnchantsAt(i) : (i < 9 ? vm3.enchantsAt(i) : vm3.mainEnchantsAt(i - 9));
                if (idA != idB || cntA != cntB || durA != durB || nmA != nmB || eA.size() != eB.size()) return false;
                for (int k = 0; k < int(eA.size()); ++k) if (eA.at(k).toInt() != eB.at(k).toInt()) return false;
                return true;
            };
            bool eq = true;
            for (int i = 0; i < 9 && eq; ++i) eq = vmEq(false, i);
            for (int i = 0; i < 27 && eq; ++i) eq = vmEq(false, i + 9);
            for (int i = 0; i < 4 && eq; ++i) eq = vmEq(true, i);
            okB_apply = eq;
            if (!okB_apply)
                qInfo().noquote() << "  [t822 B apply diag] applied VM differs from source VM (field mismatch above)";
        }
        const bool okT822b = okB_build && okB_round && okB_apply;
        if (!okT822b) ++totalFail;
        qInfo().noquote() << (okT822b ? "PASS" : "FAIL")
                          << "| t822b real-WorldStore SQLite enchant round-trip (serialization leg never probed "
                             "before): exact gatherPlayerState v3 map shape (hotbar9+main27+armor4, per-slot "
                             "id/count/durability/enchants[4]/name) saved via savePlayerData -> close -> reopen -> "
                             "loadPlayerData -> JSON field compare -> exact applyPlayerState calls into a fresh "
                             "Hotbar -> all-slot field equality vs source VM; covered multi-ench 4/4-full sword, "
                             "enchanted book, armor piece with partial durability + custom names + empty slots "
                             "(db on temp-dir absolute path, saves/ untouched)";
    });

    // ── t809 空车身体推送过拐角探针（pushEmptyCart 选向，MinecartManager 直编，P12c 同款 L 形场景）──
    //   用户报告（R19.13）：空车沿直段长按 W 连推（视点 / 输入不随拐角转向），推到拐弯处来回振荡「推不动」。
    //   根因：pushEmptyCart 旧版把 wish 直接当选向向量 → 车过拐角后停在与 wish 垂直的臂上，两臂点积同为 0
    //   平局 → kDirs 枚举序破平局（Px 先于 Nx、Pz 先于 Nz）→ 拐角出口朝枚举序败者（-X / -Z）时选中**指回
    //   拐角**的臂 → 车滑回拐角、到心重选（运动向）又把车送回来路 → 推一下退一格的往返振荡，永不抵达死端。
    //   修后选向 = away（车−玩家，权重 1.0）+ wish（0.5）+ dir（0.25）合成向量（身体推开语义，机制等价
    //   MC 玩家撞静止矿车 → 车沿轨被推离玩家）。断言（修前 FAIL / 修后 PASS）：
    //   rig：L 形（南腿死端 1 + 直段 1 + 拐角[出口 -X = 枚举序败者] + 西臂 2 = 西死端）；
    //   玩家模型 = 贴身追随（每帧玩家位 = 车上一帧位，P11(d) 先例）+ wish 恒北（0,-1) —— 长按 W 视点不转；
    //   (a) 车抵达西死端格心 ±0.05 且贴中心线（|z−(z0-2+0.5)| ≤0.05，t770 ② 钉轨）；
    //   (b) 抵达后 100 tick 停驻不动（死端无沿合成向的可走连接 → 不再被推走）；
    //   (c) 全程 5 格 L 形 footprint 内 + Y 钉轨面；
    //   (d) 振荡诊断计数：从西臂滑回拐角的次数（修前每循环 +1 不收敛；修后 0）。
    runLegMulti({ "t809 empty-cart body-push through corner (exit toward enum-order loser): reaches west dead-end c"
        "enter + parks, no oscillation; arrivedTickcornerBacks" }, [&]() {
        // rig 寻址：运行期扫描空区（P20/P23 先例——nextSlot() 网格已被上方探针耗尽）。需 6×5×5 净空
        //   （含隔离边；x 从 x0-2 到 x0、z 从 z0-2 到 z0）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 1 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -3; dx <= 1 && clear; ++dx)
                    for (int dz = -3; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t809 L-push corner: no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 文档值（P11 同款镜像注释）
            // 摆轨：臂与腿先放、拐角最后放（邻齐一次成形，P16 先例）；拐角出口 = -X（枚举序败者）。
            w.setBlock(x0,     kRigY, z0,     BR::Rail, 0); // 南死端（spawn 格）
            w.setBlock(x0,     kRigY, z0 - 1, BR::Rail, 0); // 直段
            w.setBlock(x0 - 1, kRigY, z0 - 2, BR::Rail, 0); // 西臂 1
            w.setBlock(x0 - 2, kRigY, z0 - 2, BR::Rail, 0); // 西死端
            w.setBlock(x0,     kRigY, z0 - 2, BR::Rail, 0); // 拐角（南臂 + 西臂）
            const quint8 cCon = quint8(w.stateAt(x0, kRigY, z0 - 2) & 0x0F);
            bool ok = cCon == quint8(BR::RailConnPz | BR::RailConnNx); // 拐角 = 两垂直臂位（rig 自检）
            if (!ok) qInfo().noquote() << "  t809 corner con" << int(cCon) << "expect Pz|Nx";
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            QVector3D player = carts.posAt(0);
            int lastBx = int(std::floor(player.x())), lastBz = int(std::floor(player.z()));
            const auto onL = [&](int bx, int bz) {
                return (bx == x0 && bz >= z0 - 2 && bz <= z0) || (bz == z0 - 2 && bx >= x0 - 2 && bx <= x0 - 1);
            };
            int arrivedTick = -1, cornerBacks = 0;
            QVector3D arrivePos;
            bool yOk = true;
            for (int t = 0; t < 1500 && ok; ++t) {
                // t863④ 适配：抵达西死端格后停推（追推会把死端车推离轨道——本探针验拐角选向非推离；
                //   停推后余速滑到格心停驻，观察窗看稳态）。
                if (!(int(std::floor(player.x())) == x0 - 2 && int(std::floor(player.z())) == z0 - 2))
                    carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北：wish 恒定不随拐角转（用户场景）
                carts.tickPushedCarts(0.016, &w);
                const QVector3D cp = carts.posAt(0);
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                if (!onL(bx, bz)) {
                    qInfo().noquote() << "  t809 cart left L at tick" << t << "pos" << cp;
                    ok = false;
                    break;
                }
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.01f) yOk = false;
                if (lastBz == z0 - 2 && lastBx == x0 - 1 && bx == x0) ++cornerBacks; // 西臂滑回拐角（振荡签名）
                if (arrivedTick < 0 && bx == x0 - 2 && bz == z0 - 2 && cp.x() <= float(x0 - 2) + 0.55f) {
                    arrivedTick = t;
                    arrivePos = cp;
                }
                lastBx = bx; lastBz = bz;
                player = cp; // 贴身追随（P11(d) 先例：玩家追着车、静止即续推）
                if (arrivedTick >= 0 && t - arrivedTick >= 100) break; // 停驻观察窗已满
            }
            // (a) 抵达 + 中心线；(b) 停驻 100 tick 位移 <0.05（观察窗内不被推走）。
            if (ok && arrivedTick >= 0) {
                ok = std::fabs(arrivePos.z() - (float(z0 - 2) + 0.5f)) <= 0.05f
                    && std::fabs(carts.posAt(0).x() - arrivePos.x()) < 0.05f
                    && std::fabs(carts.posAt(0).z() - arrivePos.z()) < 0.05f;
            } else if (ok) {
                qInfo().noquote() << "  t809 cart never reached west dead-end (oscillation?), final"
                                  << carts.posAt(0) << "cornerBacks" << cornerBacks;
            }
            ok = ok && arrivedTick >= 0 && yOk && cornerBacks == 0;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t809 empty-cart body-push through corner (exit toward enum-order loser):"
                                 " reaches west dead-end center + parks, no oscillation; arrivedTick"
                              << arrivedTick << "cornerBacks" << cornerBacks;
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            for (int dx = 0; dx <= 2; ++dx) w.setBlock(x0 - dx, kRigY, z0 - 2, BR::Air);
            tickN(w, 2);
        }
    });

    // ── t810 满动力轨环线骑乘速度曲线探针（tickRiddenCart 动力段 boost，MinecartManager 直编，P11 环场景）──
    //   用户报告（R19.13）：满动力轨环线骑乘过弯速度骤减（两条动力轨喂入也救不回）；同轨空车匀速圈跑。
    //   根因：tickRiddenCart 动力段旧版按 **proj 幅度**（wish·dir 投影）改写目标速 → 过弯后玩家视点未跟上
    //   新行进向的窗口 proj≈0 → 弹射档 2.8 接管 → boost 12.8 以 ~3 格/s² 拉垮到爬行速；空车路径
    //   （tickPushedCarts t735 ④）按运动符号全额 boost 无此症（对照即定位）。修后无输入且车在动 → 沿 speed
    //   符号全额 boost；前进输入 → 全 boost 不按 proj 打折；反踩刹车 → 玩家意图优先。
    //   rig：5×5 环 = 四角普通轨 + 每边 3 格动力轨直段（动力轨不拐弯 t771 → 拐角必普通轨；3×3 环拐角占比
    //   50% 是病态几何 —— 拐角摩擦本身就把均衡速压到 ~9，非用户场景）+ 环内 3×3 除心外 8 格红石块直供
    //   全部 12 条动力轨（tickRedstone 置 bit4，直写 state 会被电力重算清掉 → 必须真源供）；
    //   wish 模型 = 相位制：A 段无输入（proj≡0 —— 纯动力轨维持力断言，无视点模型、修前修后分离度最大：
    //   修前弹射档 2.7 每 tick 接管全部动力格 → 均衡速崩到 ~3）；B 段采样保持（每 16 tick 重采为当前车头向
    //   yaw 反推 dir —— 玩家过弯后 ~0.26s 转回镜头的滞后模型；从 yaw 采样而非位移：位移采样在小环上会采到
    //   跨拐角对角向、再下一拐角后成反向刹车，是探针伪影非玩家行为。旧 P11(c) 每 tick 动态随行进向 → proj
    //   恒 1，把本缺陷完全掩蔽，故须新探针）；
    //   (a) 环 footprint + Y 钉轨面 + 跨格单位轴对齐（P11 同款）；
    //   (b) A 段速度曲线（|Δpos|/dt，预热 60 tick 后统计）：minA ≥5.0 且 meanA ≥8.0（修后均衡 ~7.2/10.4
    //       —— boost 12.8 − 拐角普通轨摩擦小谷；修前崩到 0.6/2.0 → 双断言 FAIL）；
    //   (c) B 段滞后输入曲线：minB ≥6.0 且 meanB ≥10.0（修后 ~7.9/11.0；修前滞窗动力格弹射档拉垮到
    //       2.5/8.9）；A+B 共 1600 tick（25.6s）内 ≥13 圈（修后 ~17 圈 / 修前 8 圈，16 格/圈）；
    //   (d) 刹车守卫：C 段 wish 反车头向（每 tick 跟随）60 tick 内 |v| 一度 <7（proj<0 不被 boost 角力）；
    //   (e) 恢复守卫：D 段 wish 车头向 300 tick 内 v 回 ≥11（全 boost 档恢复力）。
    runLegMulti({ "t810 powered-ring ridden speed curve (no-input + lagged input, was corner collapse to catapult g"
        "ear): minAmeanAminBmeanBlaps; brake honored + recovers to boost" }, [&]() {
        // rig 寻址：运行期扫描空区（P20/P23 先例）。5×5 环（环心 ±2）+ 1 格隔离边 → 需 7×7×5 净空
        //   （隔离边防邻 rig 红石元件误供本环动力轨）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 92 && x0 < 0; zz += 2)
            for (int xx = 3; xx + 6 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -3; dx <= 3 && clear; ++dx)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t810 powered-ring speed curve: no clear rig area found";
        } else {
            const int cx = x0 + 3, cz = z0 + 3; // 环心（环 = max(|dx|,|dz|)==2 的 16 格）
            const float rideH = 0.45f;          // kCartRideH 文档值（P11 同款镜像注释）
            // 摆环：四边直段动力轨（每边 3 格）+ 四角普通轨（拐角必普通轨 t771）；
            //   环内 3×3 除心外 8 格红石块（每条动力轨至少一格 4 邻直供，t740 语义），最后放。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    const int ax = std::abs(dx), az = std::abs(dz);
                    const int m = std::max(ax, az);
                    if (m == 2)
                        w.setBlock(cx + dx, kRigY, cz + dz, (ax == 2 && az == 2) ? BR::Rail : BR::GoldenRail, 0);
                }
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx != 0 || dz != 0)
                        w.setBlock(cx + dx, kRigY, cz + dz, BR::RedstoneBlock, 0);
            tickN(w, 4);
            // rig 自检：12 格动力轨全通电 + 四拐角连接位 = 各自两邻臂（P11 corner 表）。
            bool ok = true;
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    const int ax = std::abs(dx), az = std::abs(dz);
                    if (std::max(ax, az) == 2 && !(ax == 2 && az == 2)
                        && (w.stateAt(cx + dx, kRigY, cz + dz) & BR::GoldenRailStateOnFlag) == 0)
                        ok = false;
                }
            const struct { int x, z; quint8 wantCon; } wantC[4] = {
                { cx - 2, cz - 2, quint8(BR::RailConnPx | BR::RailConnPz) },
                { cx + 2, cz - 2, quint8(BR::RailConnNx | BR::RailConnPz) },
                { cx - 2, cz + 2, quint8(BR::RailConnPx | BR::RailConnNz) },
                { cx + 2, cz + 2, quint8(BR::RailConnNx | BR::RailConnNz) },
            };
            for (const auto &c : wantC)
                if (quint8(w.stateAt(c.x, kRigY, c.z) & 0x0F) != c.wantCon) ok = false;
            if (!ok) qInfo().noquote() << "  t810 rig self-check failed (power/corner con)";
            const auto isRing = [&](int x, int z) {
                const int ax = std::abs(x - cx), az = std::abs(z - cz);
                return std::max(ax, az) == 2;
            };
            MinecartManager carts;
            carts.spawnCart(cx, kRigY, cz - 2, &w); // 北边中点（动力轨 EW 直位 → spawn 定向 ±X）
            const QVector3D mountOrigin(float(cx) + 0.5f, float(kRigY) + 2.0f, float(cz - 2) + 0.5f);
            ok = ok && carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            // 相位：A 无输入巡航 [0,900)（proj≡0：修前弹射档接管 → 均衡速崩到 ~3；修后沿运动向全 boost
            //   → 均衡 ~11）| B 滞后输入巡航 [900,1600)（wish 每 16 tick 重采车头向 —— 真实玩家过弯滞后）
            //   | C 倒踩刹车 [1600,1660)（wish = 反车头向每 tick 跟随）| D 恢复 [1660,2000)（wish = 车头向）。
            const int kLag = 16, kWarmup = 60, kNoInput = 900, kCruise = 1600, kBrake = 60, kRecover = 340;
            float wishX = 0.0f, wishZ = 0.0f;
            QVector3D prev = carts.posAt(0);
            int lastBx = int(std::floor(prev.x())), lastBz = int(std::floor(prev.z()));
            double sumA = 0.0, sumB = 0.0;
            int nA = 0, nB = 0, laps = 0;
            float minA = 1e9f, minB = 1e9f, maxRecoverV = 0.0f;
            bool yOk = true, adjOk = true, brakeDipped = false;
            // wish 采样保持：每 kLag tick 把 wish 重采为车头向（yaw 反推 dir —— 见头注释「从 yaw 采样」段）。
            const auto headingWish = [&carts](float &wx, float &wz) {
                const float yr = carts.yawAt(0) * 3.14159265358979f / 180.0f;
                wx = -std::sin(yr);
                wz = -std::cos(yr);
            };
            for (int t = 0; t < kCruise + kBrake + kRecover && ok; ++t) {
                if (t < kNoInput) {
                    wishX = 0.0f;
                    wishZ = 0.0f; // A：无输入（proj≡0，纯动力轨维持力断言）
                } else if (t < kCruise) {
                    if (t % kLag == 0) headingWish(wishX, wishZ); // B：采样保持重采（滞后 ~0.26s）
                } else if (t < kCruise + kBrake) {
                    headingWish(wishX, wishZ); // C：反车头向每 tick 跟随（不受起步时刻过拐角巧合干扰）
                    wishX = -wishX;
                    wishZ = -wishZ;
                } else {
                    headingWish(wishX, wishZ); // D：车头向每 tick 跟随（proj≈1）
                }
                QVector3D cp;
                carts.tickRiddenCart(0.016, &w, wishX, wishZ, cp);
                carts.tickPushedCarts(0.016, &w);
                const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                const float v = dl / 0.016f;
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                if (!isRing(bx, bz)) {
                    qInfo().noquote() << "  t810 cart left ring at tick" << t << "pos" << cp;
                    ok = false;
                    break;
                }
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.01f) yOk = false;
                if (bx != lastBx || bz != lastBz) {
                    const int ndx = bx - lastBx, ndz = bz - lastBz;
                    if (std::abs(ndx) + std::abs(ndz) != 1) adjOk = false; // 跨格必单位轴对齐
                    if (bx == cx && bz == cz - 2 && t < kCruise) ++laps;   // 每入北边中点格 = 1 圈
                    lastBx = bx; lastBz = bz;
                }
                if (t < kWarmup) {
                    // 预热（起步加速不计曲线）
                } else if (t < kNoInput) {
                    sumA += double(v);
                    ++nA;
                    if (v < minA) minA = v;
                } else if (t < kCruise) {
                    sumB += double(v);
                    ++nB;
                    if (v < minB) minB = v;
                } else if (t < kCruise + kBrake) {
                    if (v < 7.0f) brakeDipped = true; // (d) 刹车压速（proj<0 不被 boost 角力）
                } else if (t >= kCruise + kBrake + 60) {
                    if (v > maxRecoverV) maxRecoverV = v; // (e) 恢复段峰值（留 60 tick 起步余量）
                }
                prev = cp;
            }
            const double meanA = nA > 0 ? sumA / double(nA) : 0.0;
            const double meanB = nB > 0 ? sumB / double(nB) : 0.0;
            // A 段：无输入均衡速 —— 修后动力段沿运动向全额 boost（拐角普通轨摩擦小谷）；修前弹射档 2.8
            //   把均衡速崩到 ~2（实测 0.6/2.0 → 双断言锁）。B 段：滞后输入下均值仍近 boost 档（输入不打折
            //   语义；修前滞窗动力格弹射档拉垮实测 minB 2.5/meanB 8.9）。阈值取修前修后实测值中位。
            ok = ok && yOk && adjOk && nA > 0 && nB > 0
                && minA >= 5.0f && meanA >= 8.0
                && minB >= 6.0f && meanB >= 10.0
                && laps >= 13 && brakeDipped && maxRecoverV >= 11.0f;
            if (!ok)
                qInfo().noquote() << "  t810 speed curve: minA" << minA << "meanA" << meanA
                                  << "minB" << minB << "meanB" << meanB << "laps" << laps
                                  << "yOk" << yOk << "adjOk" << adjOk << "brakeDipped" << brakeDipped
                                  << "maxRecoverV" << maxRecoverV;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t810 powered-ring ridden speed curve (no-input + lagged input, was corner"
                                 " collapse to catapult gear): minA" << minA << "meanA" << float(meanA)
                              << "minB" << minB << "meanB" << float(meanB) << "laps" << laps
                              << "; brake honored + recovers to boost";
            // 清场（环 16 格 + 环内 8 红石块）
            carts.clearAll();
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) > 0)
                        w.setBlock(cx + dx, kRigY, cz + dz, BR::Air);
            tickN(w, 2);
        }
    });

    // ── t811 生物自动乘坐矿车探针（EntityManager + MinecartManager 直编；登乘 / 满员拒载 / AI 冻结钉位
    //   随车 / 挖车释放 / 玩家占用不接客，全链一次过）──
    //   机制（R19.13 spec）：非骑乘 mob 走进矿车 ≤0.8 格自动登乘（乘员总数限 1：玩家 XOR 生物）；骑乘期
    //   AI / 物理全冻结、位置钉车座位（车动它动）；下车唯一路径 = 车被挖（对账自释放原地，恢复 AI）。
    //   rig：直轨 6 格（北向 z0-5..z0）+ 3 宽石板地板 kRigY-1（mob 落脚 / 释放后重力落点）；驱动序镜像
    //   PlayerController 真序（mob 桶 tick → 钉位① → step 内车物理 pushEmptyCart+tickPushedCarts → 钉位②）。
    //   断言：(a) 生于车格 → 首 pass 即登乘 + moveSpeed 归 0（姿态锁定）；(b) 推动期每 tick 钉位误差 <0.01
    //   （含 Y 座位公式 车心−0.3125+halfH）且车总位移 ≥3 格（确在动）；(c) 停驻后 100 tick 零漂移；
    //   (d) hitCartFromRay 挖车 → 下一 pass rideCart==-1 + 存活 + 重力落定地板顶（kRigY+halfH）；
    //   (e) 第二 mob 同格不登（生物占座满员）+ 玩家 tryMount 满员车被拒；(f) 玩家骑乘的车不接 mob。
    runLegMulti({ "t811 mob auto-rides minecart: board+freeze-pin follows cart (seatY = cartY-0.3125+halfH), 100-ti"
        "ck park zero-drift, destroy releases + resettles, full cart refuses 2nd mob & player, player-rid"
        "den cart takes no mob; travel" }, [&]() {
        // rig 选址：运行期扫描空区（t809 先例——nextSlot 网格已被上方探针耗尽）。需 7×4×6 净空。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 1 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 1 && clear; ++dx)
                    for (int dz = -6; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t811 mob ride cart: no clear rig area found";
        } else {
            const float seatDrop = 0.3125f; // kCartSeatDrop 同值（playercontroller.cpp 骑车分支同款镜像常量）
            // 3 宽石板地板（kRigY-1）+ 中列直轨 6 格（kRigY；北向 z0-5..z0）。
            for (int dz = -5; dz <= 0; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                    if (dx == 0) w.setBlock(x0, kRigY, z0 + dz, BR::Rail, 0);
                }
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mobA = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ff5555"), 10);
            QVector3D player = carts.posAt(0);
            bool boarded = false, pinOk = true, speedLocked = true;
            float travel = 0.0f;
            int parkedTicks = 0;
            QVector3D lastCart = carts.posAt(0);
            for (int t = 0; t < 1500 && parkedTicks < 100; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();                        // 钉位①（mob 桶内，游戏同序）
                // t863④ 适配：车进北死端格后停推（追推会把死端车推离轨道——本探针验登乘 / 钉位 / 释放，
                //   停推后余速滑到死端格心停驻，停驻窗看钉位零漂）。
                if (int(std::floor(player.z())) > z0 - 5)
                    carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北推（t809 玩家模型）
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();                        // 钉位②（step 后，同帧随车）
                const QVector3D cp = carts.posAt(0);
                const QVector3D mp = ents.posAt(mobA);
                if (ents.rideCartAt(mobA) >= 0) {
                    boarded = true;
                    if (std::fabs(mp.x() - cp.x()) > 0.01f
                        || std::fabs(mp.y() - (cp.y() - seatDrop + 0.5f)) > 0.01f
                        || std::fabs(mp.z() - cp.z()) > 0.01f) pinOk = false;
                    if (ents.moveSpeedAt(mobA) != 0.0f) speedLocked = false;
                    const float d = QVector3D(cp - lastCart).length();
                    if (d > 1e-4f) travel += d; else ++parkedTicks; // 停驻窗（车停后钉位零漂计数）
                }
                lastCart = cp;
                player = cp; // 贴身追随（t809 先例：玩家追着车、静止即续推）
            }
            // (d) 挖车释放（从车正上方垂直下挖；instantBreak 免耐久轮）→ 对账自释放 + 重力 / 落定重接手。
            //   t865 支撑收口后轨格（ShapeNone 无碰撞）不承载 → 释放 mob（resting 已清）穿透轨格、落定
            //   石板地板顶（kRigY−1 石块真顶 = kRigY）+ halfH —— 旧断言钉的 kRigY+1.5（轨当满格悬上一格）
            //   正是 t865「支撑判定把非整格当满格」修掉的行为。释放自座位高（≈kRigY+0.64）落 ~0.64 格
            //   需数 tick → 驱动至多 40 tick（0.64s，落定 + 余量）再量。
            const QVector3D cp = carts.posAt(0);
            const bool hit = carts.hitCartFromRay(QVector3D(cp.x(), cp.y() + 3.0f, cp.z()),
                                                  QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            const bool released = hit && !carts.aliveAt(0) && ents.rideCartAt(mobA) == -1 && ents.aliveAt(mobA);
            for (int t = 0; t < 40; ++t) { // 释放后物理 tick：重力穿透轨格落定地板 + AI 复活（listener 远，纯游荡）
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const float settleY = ents.posAt(mobA).y();
            const bool settleOk = std::fabs(settleY - (float(kRigY) + 0.5f)) <= 0.02f;
            // (e) 满员拒载：新车（槽复用 0）+ mobB 占座 → mobC 同格不登 + 玩家 tryMount 被拒。
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mobB = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#55ff55"), 10);
            const int mobC = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#5555ff"), 10);
            for (int t = 0; t < 8; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const bool fullOk = ents.rideCartAt(mobB) == 0 && ents.rideCartAt(mobC) == -1
                                && !carts.tryMount(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
            // (f) 玩家占用不接客：挖掉满员车（mobB 释放）→ 重放车 → 玩家骑 → mobD 同格不登。
            carts.hitCartFromRay(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                 QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            carts.spawnCart(x0, kRigY, z0, &w);
            const bool playerRode = carts.tryMount(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f)
                                    && carts.ridingIndex() == 0;
            const int mobD = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ffff55"), 10);
            for (int t = 0; t < 8; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const bool playerOccupyOk = playerRode && ents.rideCartAt(mobD) == -1;
            const bool ok = boarded && pinOk && speedLocked && travel >= 3.0f && parkedTicks >= 100
                            && released && settleOk && fullOk && playerOccupyOk;
            if (!ok)
                qInfo().noquote() << "  t811 cart: boarded" << boarded << "pinOk" << pinOk
                                  << "speedLocked" << speedLocked << "travel" << travel
                                  << "parked" << parkedTicks << "released" << released
                                  << "settleY" << settleY << "fullOk" << fullOk
                                  << "playerOccupyOk" << playerOccupyOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t811 mob auto-rides minecart: board+freeze-pin follows cart"
                                 " (seatY = cartY-0.3125+halfH), 100-tick park zero-drift, destroy"
                                 " releases + resettles, full cart refuses 2nd mob & player,"
                                 " player-ridden cart takes no mob; travel" << travel;
            // 清场
            carts.clearAll();
            ents.clearAll();
            for (int dz = -5; dz <= 0; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
                w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── t811 生物自动乘坐船探针（EntityManager + BoatManager 直编；双座登乘 / 满员 / 玩家拒载 / 挖船释放）──
    //   rig：凿进天然石 3 宽水道（t805 模式：fy=44 石板 + 45..47 凿空 + 45 层铺水 → 水面顶 46；船浮 46）。
    //   断言：(a) mob A/B 生于船格 → 双双登乘（A 先扫 → 座 0 / B 座 1），双钉位横向分离 ~0.6（右舷 +0.3 /
    //   左舷 −0.3，yaw=0 时 right=+X）且 Y = 船位+halfH；(b) mob C 同格不登（2 生物 = 满员 2）；
    //   (c) 满员船玩家 tryMount 被拒（返 false 且 ridingIndex 不变）；(d) hitBoatFromRay 挖船 → A/B
    //   双双 rideBoat==-1 且存活（原地释放恢复 AI）。
    runLegMulti({ "t811 mob auto-rides boat: dual seats (+/-0.3 sides, pinY = boatY+halfH), 3rd mob refused (cap 2)"
        ", player tryMount refused when full, break releases both in place; seatGap" }, [&]() {
        const int bx = 40, bz = 10; // t805 rig（bx=6,bz=6,x≤39）之外的开阔石区
        const int fy = 44;          // 石板层；水面 = 45；水面顶 = 46
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                w.setBlock(bx + dx, fy, bz + dz, BR::Stone, 0);
                w.setBlock(bx + dx, fy + 1, bz + dz, BR::Air, 0);
                w.setBlock(bx + dx, fy + 2, bz + dz, BR::Air, 0);
                w.setBlock(bx + dx, fy + 3, bz + dz, BR::Air, 0);
            }
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(bx + dx, fy + 1, bz + dz, BR::Water, 0);
        BoatManager boats;
        EntityManager ents;
        ents.setVehicleManagers(nullptr, &boats);
        const bool spawned = boats.spawnBoat(bx + 2, fy + 1, bz, BoatManager::Oak);
        const QVector3D bp0 = boats.posAt(0);
        const int mA = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#ff5555"), 10);
        const int mB = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#55ff55"), 10);
        const int mC = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#5555ff"), 10);
        QVector3D pA, pB;
        for (int t = 0; t < 12; ++t) {
            boats.tick(0.016, &w); // 船浮水常开（游戏序：boat 桶在 mob 桶前）
            ents.tick(0.016, &w, bp0, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            pA = ents.posAt(mA);
            pB = ents.posAt(mB);
        }
        const QVector3D bpNow = boats.posAt(0);
        const float sepAB = QVector3D(pA - pB).length();
        const bool dualSeat = spawned && ents.rideBoatAt(mA) == 0 && ents.rideBoatAt(mB) == 0
                              && sepAB >= 0.45f && sepAB <= 0.75f          // 双座横向分离 ~0.6
                              && std::fabs(pA.y() - (bpNow.y() + 0.5f)) <= 0.02f
                              && std::fabs(pB.y() - (bpNow.y() + 0.5f)) <= 0.02f;
        const bool fullOk = ents.rideBoatAt(mC) == -1;
        const bool playerRefused = !boats.tryMount(bpNow + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f)
                                   && boats.ridingIndex() == -1;
        const bool broke = boats.hitBoatFromRay(bpNow + QVector3D(0.0f, 3.0f, 0.0f),
                                                QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
        ents.tick(0.016, &w, bp0, 0.3f, 1.8f, false);
        ents.tickVehicleRiding();
        const bool released = broke && !boats.aliveAt(0)
                              && ents.rideBoatAt(mA) == -1 && ents.rideBoatAt(mB) == -1
                              && ents.aliveAt(mA) && ents.aliveAt(mB);
        const bool ok = dualSeat && fullOk && playerRefused && released;
        if (!ok)
            qInfo().noquote() << "  t811 boat: dualSeat" << dualSeat << "sepAB" << sepAB
                              << "fullOk" << fullOk << "playerRefused" << playerRefused
                              << "released" << released
                              << "pA" << pA << "pB" << pB << "boatY" << bpNow.y();
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t811 mob auto-rides boat: dual seats (+/-0.3 sides, pinY = boatY+halfH),"
                             " 3rd mob refused (cap 2), player tryMount refused when full, break"
                             " releases both in place; seatGap" << sepAB;
        // 清场
        ents.clearAll();
        boats.clearAll();
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 3; ++dy)
                    w.setBlock(bx + dx, fy + dy, bz + dz, BR::Air, 0);
        tickN(w, 2);
    });

    // ── t865 生物贴轨行走 + 草丛误跳探针（EntityManager 直编，t803 追击走廊模式）──
    //   用户报告（R19.15）：「生物走铁轨悬浮上方一格」（支撑判定把非整格当满格抬高）+「僵尸遇草丛跳过去」
    //   （草丛/花等无碰撞植物被 isJumpObstacle 当墙）。根因（t865）：mob 三谓词（mobAabbHitsSolid /
    //   mobFootprintHasSupport / mobSupportTopY / isJumpObstacle）消费 isSolid（非 air 实存）而非碰撞语义 →
    //   轨 / 火把 / 草丛等 ShapeNone 无碰撞格被当满格墙 + 满格支撑。修 = 收口 World::isCollidable /
    //   World::supportTopYAt（碰撞 sub-AABB 真顶单一权威）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) 贴轨行走：僵尸（Shambler，追击确定性 +X）沿 10 格直轨走廊追玩家 —— 全程脚底 Y 恒 ≈ 地面顶
    //       （kRigY，穿透轨格踩地面——轨板厚 1/16 视觉贴合）且到达走廊远端（旧象：轨=墙 → 越障跳翻上轨 →
    //       悬浮轨上一格 feet=kRigY+1）；
    //   (b) 草丛直走：同走廊铺 4 格草丛（无轨）—— 全程无起跳（feet 恒 ≈ 地面，越障跳从未触发）且到达
    //       远端（旧象：草丛=墙 → 起跳翻过 = 用户「僵尸遇草丛跳过去」）。
    runLegMulti({ "t865 mobs walk rails at true surface (feet on floor through 1/16 rail plate, no full-block lift)"
        " and stride through tall grass without jumping (no-collision blocks are neither wall nor support"
        ")" }, [&]() {
        // rig 选址：运行期扫描空区（t809/t811 先例）。需 13×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 12 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 12 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t865 mob walks rails/grass at true surface: no clear rig area found";
        } else {
            EntityManager ents;
            // (a) 贴轨走廊：石地板 x0..x0+10 @kRigY-1 + 直轨中列 x0..x0+10 @kRigY；僵尸 x0 追 +X（虚拟玩家
            //     脚位走廊远端，kDetectRange=16 内）。断言全程 feet == kRigY（±0.02）且 maxX ≥ x0+8。
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Rail, 0);
            }
            const int zA = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D railTarget(float(x0) + 10.5f, float(kRigY), float(z0) + 0.5f);
            // 预热 40 tick：spawn 高度（cell+0.5 中心）对 1.8 高 mob 脚位偏低 → 首拍嵌入地板下沉 ~0.85 再
            //   snap 回真支撑顶（引擎既定落定链，非本任务对象）；预热后才开始记录 feet 偏差（否则把该
            //   出生暂态误计为「跳」）。
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, railTarget, 0.3f, 1.8f, true);
            float railMaxFeetOff = 0.0f, railMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) { // 4.8s：10 格追击 ~3.6s（kChaseSpeed 2.8）+ 余量
                ents.tick(0.016f, &w, railTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zA);
                railMaxFeetOff = std::max(railMaxFeetOff, std::fabs(p.y() - 0.9f - float(kRigY)));
                railMaxX = std::max(railMaxX, p.x());
            }
            const bool okA = railMaxFeetOff <= 0.02f && railMaxX >= float(x0) + 8.0f;
            // 清 (a) 场（轨全拆；地板留作 (b) 走廊，僵尸留在 ents 内随后续段自然游荡，不参与断言）。
            for (int dx = 0; dx <= 10; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            // (b) 草丛走廊：地板中段铺 4 格草丛（x0+3..x0+6）；新僵尸自 x0 追 +X。断言全程 feet == kRigY
            //     （越障跳从未把脚抬离地面 = 草丛直走过）且到达远端。
            for (int dx = 3; dx <= 6; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::TallGrass, 0);
            const int zB = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D grassTarget(float(x0) + 10.5f, float(kRigY), float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, grassTarget, 0.3f, 1.8f, true); // 预热（同上）
            float grassMaxFeetOff = 0.0f, grassMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, grassTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zB);
                grassMaxFeetOff = std::max(grassMaxFeetOff, std::fabs(p.y() - 0.9f - float(kRigY)));
                grassMaxX = std::max(grassMaxX, p.x());
            }
            const bool okB = grassMaxFeetOff <= 0.02f && grassMaxX >= float(x0) + 8.0f;
            const bool ok = okA && okB;
            if (!ok)
                qInfo().noquote() << "  t865 rail: feetOff" << railMaxFeetOff << "maxX" << railMaxX
                                  << "| grass: feetOff" << grassMaxFeetOff << "maxX" << grassMaxX;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t865 mobs walk rails at true surface (feet on floor through 1/16 rail"
                                 " plate, no full-block lift) and stride through tall grass without"
                                 " jumping (no-collision blocks are neither wall nor support)";
            // 清场
            ents.clearAll();
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── t867 压力板掉落物贴板探针（ItemEntityManager 直编，t804 掉落物探针模式）──
    //   用户报告（R19.15）：「掉落物落在压力板上悬上方一格」。根因（t867）：ItemEntityManager 落地列扫
    //   把任何非空气格当**整格高**支撑（World::isSolid 语义）→ 板上掉落物 restY = 板格+1+0.3 悬空。修 =
    //   列扫 / resting 复探 / 冰面摩擦面判定收口 World::supportTopYAt（碰撞 sub-AABB 真顶；t865 同族单一
    //   权威）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) 板上掉落物紧贴板面：resting 且中心 Y = 板格 + 1/16（ShapePlate 盒真顶）+ kRestOffset(0.3)
    //       （旧象 = 板格+1+0.3 悬一格）；
    //   (b) 满格支撑回归对照：同 rig 相邻列石块顶的掉落物仍停 块格+1+0.3（收口不改变整格落定高度）；
    //   (c) 挖板后失支撑穿透：拆板 → 掉落物解除 resting 续落到石块顶（块格+1+0.3）——薄支撑消失的
    //       重力跟随（板不承载的另一半语义）。
    runLegMulti({ "t867 item rests glued to pressure plate top (restY = plate 1/16 + 0.3, was floating a full block"
        " above), full-block rest height unchanged, plate removal drops item onto pedestal (thin support "
        "vanishes -> gravity re-settles)" }, [&]() {
        // rig 选址：运行期扫描空区（t865 先例）。需 5×1×4 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 4 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t867 items rest on pressure plate top: no clear rig area found";
        } else {
            const float restOff = 0.3f; // kRestOffset（itementitymanager.h 私有常量文档值）
            const float plateTop = 1.0f / 16.0f; // ShapePlate 盒真顶（blockregistry shapeBoxes）
            // 石基座两列 + 左列压力板：左 = 板路径 (a)/(c)，右 = 满格对照 (b)。
            w.setBlock(x0,     kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 2, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0,     kRigY,     z0, BR::WoodPressurePlate, 0);
            ItemEntityManager items;
            // (a) 板上：零初速直落（spawnItemAt 免 spawnItem 弹出方向哈希；t804 确定性同款）。
            items.spawnItemAt(QVector3D(float(x0) + 0.5f, float(kRigY) + 1.5f, float(z0) + 0.5f),
                              BR::Torch, 1, 0.0f, 0.0f, 0.0f);
            // (b) 满格对照（不同 itemId：t490fix 就近合并半径 2.0 恰等于两列间距，同 id 会被并成一实体）。
            items.spawnItemAt(QVector3D(float(x0 + 2) + 0.5f, float(kRigY) + 1.5f, float(z0) + 0.5f),
                              BR::Stone, 1, 0.0f, 0.0f, 0.0f);
            for (int t = 0; t < 60; ++t) items.tick(0.016f, &w); // 1s 落定 + 余量
            const bool okA = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY) + plateTop + restOff)) <= 0.02f;
            const bool okB = items.restingAt(1)
                && std::fabs(items.posAt(1).y() - (float(kRigY - 1) + 1.0f + restOff)) <= 0.02f;
            // (c) 拆板 → 板上物品失支撑穿透轨…落到石基座顶（复探两格窗见不到碰撞支撑 → 解除 resting 续落）。
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int t = 0; t < 60; ++t) items.tick(0.016f, &w);
            const bool okC = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY - 1) + 1.0f + restOff)) <= 0.02f;
            const bool ok = okA && okB && okC;
            if (!ok)
                qInfo().noquote() << "  t867 plateY" << items.posAt(0).y() << "restingA"
                                  << items.restingAt(0) << "| fullY" << items.posAt(1).y()
                                  << "restingB" << items.restingAt(1);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t867 item rests glued to pressure plate top (restY = plate 1/16 +"
                                 " 0.3, was floating a full block above), full-block rest height"
                                 " unchanged, plate removal drops item onto pedestal (thin support"
                                 " vanishes -> gravity re-settles)";
            // 清场
            items.clearAll();
            w.setBlock(x0,     kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── review26 #1 mob 矮碰撞支撑行走探针（EntityManager 直编，t865 追击走廊模式）──
    //   Review 2026-08-26 #1：t865 把支撑/落定收口到碰撞真顶后，mob 脚位落进矮支撑格内部（下半砖 +0.5 /
    //   耕地 +0.9375）→ 水平碰撞 mobAabbHitsSolid 的 y0=脚位格=支撑格自身 → isCollidable 恒 true → 逐轴
    //   撤回 → 原地冻结（农田生物全员站桩）。修 = 脚位格薄支撑豁免（碰撞真顶 ≤ 脚位+1e-3 → 视穿透）+
    //   越障跳同口径（前方格真顶 ≤ 脚位 → 非墙不跳，防全程兔跳）。矩阵断言（任一 FAIL = 症状复现）：
    //   (a) 下半砖地面：僵尸沿 10 格下半砖走廊追击 —— 全程脚底 Y ≈ 砖真顶（kRigY+0.5，贴面行走非冻结
    //       非兔跳）且到达走廊远端（旧象 = 起步即冻结 maxX≈x0）；
    //   (b) 耕地地面：同走廊铺耕地 —— 脚底 ≈ kRigY+0.9375（耕地矮盒真顶）且到达远端。
    runLegMulti({ "review26-1 mobs stride across bottom-slab and farmland floors at true support tops (feet snapped"
        " inside the support cell are exempt from the foot-cell horizontal scan; no freeze, no bunny-hop)" }, [&]() {
        // rig 选址：运行期扫描空区（t865 先例）。需 13×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 12 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 12 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-1 mob walks short-support floors: no clear rig area found";
        } else {
            EntityManager ents;
            // (a) 下半砖走廊：石地板 x0..x0+10 @kRigY-1 + 下半砖（CobbleSlab state0 真顶 +0.5）同列 @kRigY；
            //     僵尸自 x0 上方一格半落下（免嵌入出生）追 +X。断言全程 feet ≈ kRigY+0.5（±0.02）且 maxX ≥ x0+8。
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::CobbleSlab, 0);
            }
            const int zA = ents.spawnMobTyped(x0, kRigY + 2, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D slabTarget(float(x0) + 10.5f, float(kRigY) + 0.5f, float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, slabTarget, 0.3f, 1.8f, true); // 预热落定
            float slabMaxFeetOff = 0.0f, slabMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) { // 4.8s：10 格追击 ~3.6s（kChaseSpeed 2.8）+ 余量
                ents.tick(0.016f, &w, slabTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zA);
                slabMaxFeetOff = std::max(slabMaxFeetOff, std::fabs(p.y() - 0.9f - (float(kRigY) + 0.5f)));
                slabMaxX = std::max(slabMaxX, p.x());
            }
            const bool okA = slabMaxFeetOff <= 0.02f && slabMaxX >= float(x0) + 8.0f;
            // t1049（GOV-20260914-1）根因修复：拆 (a) 场前先遣散 zA——此前 zA 跨段泄漏进 (b)，slab→farmland
            //   换地后其脚位（砖顶 kRigY+0.5）留在耕地格内部（新支撑真顶 +0.9375 高于脚位）→ aiHostile 玩家
            //   路径越障跳探（:4156，无攻击距离门）把自身所在耕地格判墙（isJumpObstacle 真顶 > 脚位+1e-3）
            //   → 虚假起跳 + t670 滑流落回走廊中段 → 落地 fallDist≈0.75 触发 t1045 踩踏掷骰（全局 RNG）→
            //   掷中则耕地变 Dirt 满格台阶（+0.0625）→ (b) zB 走到台阶合规越障跳 → feetOff = 离散弧顶采样
            //   （kJumpSpeed 8.4 / kGravity 28 / 半隐式欧拉 19 tick ≈ 1.19347）> 0.02 = 本腿环境敏感翻红根因
            //   （同 binary 红绿翻转 = 踩踏 RNG；跨 binary 失败轨迹逐位一致 = 跳跃物理常数，非 UB/布局敏感
            //   FP）。遣散后 (b) 从净实体态起：zB 出生落差 0.0625 → 踩踏掷骰 p≤0 概率地板提前返回，全腿
            //   零 RNG 消费确定化。断言本体 (a)/(b) 未放宽。
            ents.removeEntityAt(zA);
            // 清 (a) 场（拆砖铺耕地；地板留作 (b)）。
            for (int dx = 0; dx <= 10; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Farmland, 0);
            // (b) 耕地走廊：耕地矮盒真顶 +0.9375；新僵尸同款。断言全程 feet ≈ kRigY+0.9375 且 maxX ≥ x0+8。
            //     t1045 注记：出生高由 kRigY+2 降为 kRigY+1——MC onFallenUpon 踩踏概率化（裁-3）后，
            //     自 2 格高落的僵尸着地落差 ~1.06 → P≈0.56 概率踩坏脚下耕地（feetOff 断言被行为性打破）。
            //     本探针验证面是「行走贴真顶」非「坠落」，出生降到落差 0.0625（概率地板 0.5 内恒不踩，
            //     World::farmlandTrampleRoll 单一权威）→ 行走语义面恢复确定；断言本体未放宽。
            const int zB = ents.spawnMobTyped(x0, kRigY + 1, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D farmTarget(float(x0) + 10.5f, float(kRigY) + 0.9375f, float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, farmTarget, 0.3f, 1.8f, true); // 预热落定
            float farmMaxFeetOff = 0.0f, farmMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, farmTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zB);
                farmMaxFeetOff = std::max(farmMaxFeetOff, std::fabs(p.y() - 0.9f - (float(kRigY) + 0.9375f)));
                farmMaxX = std::max(farmMaxX, p.x());
            }
            const bool okB = farmMaxFeetOff <= 0.02f && farmMaxX >= float(x0) + 8.0f;
            // t1049 结构钉：锚定腿 (b) 起跑前的 zA 遣散语句（剥注释后仍在的真实语句）——未来腿重构若回退
            //   跨段泄漏（踩踏 RNG 翻红源）即矩阵可辨。pinSet 自文件自钉（t1027 源钉先例）；
            //   R20.03 拆分随迁：本腿自 redstone_matrix_test.cpp 迁入本段 TU，自钉读路径同步改指
            //   tools/matrix/section02_early_probes.cpp（路径解析同 exeDir/../ 约定）。理由：本修复是
            //   行为腿内部的确定性结构面，行为断言 (a)/(b) 在「泄漏恰好未掷中」的 run 下无法区分
            //   已修/未修——只有源钉能把回归变红。
            const bool okPin = pinSet(
                QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absoluteFilePath(
                    QStringLiteral("tools/matrix/section02_early_probes.cpp")), {
                {"review26-1-legb-despawn", "ents.removeEntityAt(zA);"},
            }).isEmpty();
            const bool ok = okA && okB && okPin;
            if (!ok)
                qInfo().noquote() << "  review26-1 slab: feetOff" << slabMaxFeetOff << "maxX" << slabMaxX
                                  << "| farmland: feetOff" << farmMaxFeetOff << "maxX" << farmMaxX
                                  << "| pin" << okPin;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-1 mobs stride across bottom-slab and farmland floors at"
                                 " true support tops (feet snapped inside the support cell are exempt"
                                 " from the foot-cell horizontal scan; no freeze, no bunny-hop)";
            // 清场
            ents.clearAll();
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── review26 #2 掉落物贴薄支撑水平滑动探针（ItemEntityManager 直编，t867 探针模式）──
    //   Review 2026-08-26 #2：物品静息中心 = 真顶 + kRestOffset(0.3)，下半砖（真顶 +0.5）中心落在砖格
    //   内部 → 摩擦段水平碰撞探测 hcy=qFloor(pos.y())=砖格自身 → isCollidable 恒 true → 带初始弹出速度
    //   也滑不动（物品被钉死在落点）。修 = 水平碰撞探测与 resting 复探同源：目标格真顶 ≤ 当前底+容差
    //   = 正站其顶不挡。矩阵断言：(a) 砖面带 +X 初速掉落物滑行 ≥1.5 格（旧象 = 位移 0）；(b) 满格墙
    //   仍挡（滑到墙前停，不进墙格——豁免不过界）。
    runLegMulti({ "review26-2 item with horizontal pop velocity slides across a bottom-slab floor (resting cell exe"
        "mpt when support top is at the item's feet) and full blocks ahead still stop it" }, [&]() {
        // rig 选址：运行期扫描空区（t867 先例）。需 6×1×4 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 5 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-2 item slides on slab floor: no clear rig area found";
        } else {
            const float restOff = 0.3f;  // kRestOffset（itementitymanager.h 私有常量文档值）
            const float slabTop = 0.5f;  // 下半砖（state0）碰撞盒真顶
            // 石基座 x0..x0+3 @kRigY-1 + 下半砖面 x0..x0+2 @kRigY + 满格石墙 x0+3 @kRigY（墙顶 +1 > 物品
            //   底 +0.5 → 不豁免，仍挡）。物品自砖面上方带 +X 初速 20（总滑程 ≈ 20/6 ≈ 3.3 格 → 必抵墙前）。
            for (int dx = 0; dx <= 3; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
            for (int dx = 0; dx <= 2; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::CobbleSlab, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Stone, 0);
            ItemEntityManager items;
            // spawnItemAt 末三参 = (dirX, dirZ, speed)（方向 × 速率，t608 发射器排出口口径）：+X 弹出 20。
            //   生成点取砖面静息高（slabTop+kRestOffset）——首拍即落定贴面滑行（免高落差 +X 弹出飞越
            //   1 格墙控制位形：本探针钉的是贴面滑动碰撞，不是抛物线）。
            items.spawnItemAt(QVector3D(float(x0) + 0.5f, float(kRigY) + slabTop + restOff,
                                        float(z0) + 0.5f),
                              BR::Torch, 1, 1.0f, 0.0f, 20.0f);
            for (int t = 0; t < 150; ++t) items.tick(0.016f, &w); // 2.4s：落定 + 滑行 + 摩擦停
            const float finalX = items.posAt(0).x();
            const bool ok = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY) + slabTop + restOff)) <= 0.02f
                && (finalX - (float(x0) + 0.5f)) >= 1.5f   // (a) 滑起来了（旧象位移 = 0）
                && finalX < float(x0 + 3);                 // (b) 满格墙仍挡（不进墙格）
            if (!ok)
                qInfo().noquote() << "  review26-2 finalX" << finalX << "resting" << items.restingAt(0)
                                  << "y" << items.posAt(0).y();
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-2 item with horizontal pop velocity slides across a"
                                 " bottom-slab floor (resting cell exempt when support top is at the"
                                 " item's feet) and full blocks ahead still stop it";
            // 清场
            items.clearAll();
            for (int dx = 0; dx <= 3; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── review26 #3 岩浆沟壑越障跳探针（EntityManager 直编，t865 追击走廊模式）──
    //   Review 2026-08-26 #3：t865 收口后 isJumpObstacle 只对 Water 保留 ditch-jump，岩浆（ShapeNone →
    //   isCollidable=false）落到 false = 不当沟壑 → mob 径直走进脚位岩浆格（点燃殉死）。修 = Water/Lava
    //   同列沟壑跳。矩阵断言：僵尸沿石走廊追击，脚位层中段一格岩浆源 —— 途径时脚底离地（跳跃触发，
    //   maxFeetY ≥ 地面+0.4；旧象 = 不跳恒贴地走进岩浆）且到达远端。
    runLegMulti({ "review26-3 mob jumps over a foot-level lava cell like water ditches (lava joins the ditch-jump l"
        "ist; was walked straight into)" }, [&]() {
        // rig 选址：运行期扫描空区（t865 先例）。需 10×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 9 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 9 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-3 mob jumps lava ditch: no clear rig area found";
        } else {
            EntityManager ents;
            // 石地板 x0..x0+8 @kRigY-1（顶 = kRigY）+ 脚位层中段一格岩浆源 (x0+4, kRigY)（坐在地板上，
            //   与 mob 脚位同层 —— isJumpObstacle 前方探针的检出位形；探针不 tick World，岩浆不外溢）。
            for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 4, kRigY, z0, BR::Lava, 0);
            const int zC = ents.spawnMobTyped(x0, kRigY + 1, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D lavaTarget(float(x0) + 8.5f, float(kRigY), float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, lavaTarget, 0.3f, 1.8f, true); // 预热落定
            float maxFeetY = -1e9f, lavaMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, lavaTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zC);
                maxFeetY = std::max(maxFeetY, p.y() - 0.9f);
                lavaMaxX = std::max(lavaMaxX, p.x());
            }
            const bool ok = maxFeetY >= float(kRigY) + 0.4f   // 越障跳触发（跳跃顶点 ≈ 地面+1.26）
                && lavaMaxX >= float(x0) + 6.0f;              // 越过岩浆格到达远端
            if (!ok)
                qInfo().noquote() << "  review26-3 maxFeetY" << maxFeetY << "maxX" << lavaMaxX;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-3 mob jumps over a foot-level lava cell like water"
                                 " ditches (lava joins the ditch-jump list; was walked straight into)";
            // 清场（岩浆先拆再 tickN，免流体外溢）
            ents.clearAll();
            for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 4, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── t863 矿车坡道物理四修探针（MinecartManager 直编，P12b 同款坡 rig 族）──
    //   用户报告（R19.15 玩法阻塞）：① 上坡失速悬停半空（应反向滑落）；② 悬停 / 停驻态挖掉下方轨 /
    //   支撑不受重力（应坠落）；③ 坡顶前端无轨 + 速度够自动暂停（应飞出平抛）；④ 轨末端静止车推不动
    //   （应可被玩家推离轨道进入自由物理）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) ① 被骑车爬坡中途松键 → 摩擦死区 → 反溜起步滑回坡脚停驻（不悬停坡面）；
    //   (b) ② 高架轨（支撑 + 轨被拆）→ 停驻车失支撑转坠落，落到下方接住地板贴面停驻（不冻结半空）；
    //   (c) ③ 坡顶死端（后邻轨低一格 = 爬升到顶 + 前端无轨）+ 速度足 → 飞出平抛落到轨端外接地板
    //       （机制等价 MC 1.0 轨端飞行，速度不足才停驻——平死端停靠面由 t769/t811 既有探针钉）；
    //   (d) ④ 平死端静止车被玩家朝端外推 → 推离轨道出轨，贴地滑行落到轨端外地板（进入自由物理）。
    runLegMulti({ "t863 ramp physics: uphill stall slides back to foot, mined support drops parked cart onto catch "
        "floor, crest dead-end launches at speed onto beyond-end floor, dead-end cart pushable off the ra"
        "il into free physics" }, [&]() {
        // rig 选址：运行期扫描空区（t867 先例）。需 10×1×4 净空（含隔离边 + 落地板区）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 9 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 9 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t863 ramp physics four fixes: no clear rig area found";
        } else {
            const float rideH = 0.45f;    // kCartRideH 镜像（P12b 同款）
            const float groundH = 0.3875f; // kCartGroundH 镜像（出轨贴地落定中心偏移）
            // ── (a) 上坡失速反溜：x0 低平 + x0+1..x0+4 四格连坡（东邻逐格 +1）+ x0+5 高平死端。
            //    松键滑行余量 = 初速 4.8 / 摩擦 2 ≈ 2.4 格 < 坡长 4 格 → 停驻点必落坡面（非坡顶）。──
            for (int i = 0; i <= 5; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 5) ? 4 : (i >= 1) ? (i - 1) : 0), z0, BR::Rail, 0);
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool okA = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            bool reachedSlope = false;
            for (int t = 0; t < 300 && !reachedSlope; ++t) { // W 爬坡到坡中（x∈[x0+1.2, x0+1.6]）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (cp.x() > float(x0 + 1) + 0.2f) reachedSlope = true;
            }
            for (int t = 0; t < 600; ++t) { // 松键：摩擦死区 → 反溜起步 → 倒行滑回坡脚
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
            }
            const QVector3D fa = carts.posAt(0);
            okA = okA && reachedSlope
                && fa.x() < float(x0) + 1.0f                      // 滑回低平段（坡脚前；不悬停坡面）
                && std::fabs(fa.y() - (float(kRigY) + rideH)) < 0.02f; // 贴低平轨面停驻
            if (!okA) qInfo().noquote() << "  t863(a) slideback final" << fa << "reached" << reachedSlope;
            // 清 (a)：销毁被骑车（instantBreak 免掉落 + 清骑乘态）+ 拆坡 / 高平轨（低平 x0 留作 (d)）。
            carts.hitCartFromRay(QVector3D(fa.x(), fa.y() + 3.0f, fa.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            for (int i = 1; i <= 5; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 5) ? 4 : (i - 1)), z0, BR::Air, 0);

            // ── (b) 支撑被挖坠落：高架轨（支撑浮空 @kRigY+2 顶 / 轨 @kRigY+3）+ 接住地板 @kRigY-1。──
            w.setBlock(x0 + 4, kRigY + 2, z0, BR::Stone, 0);  // 浮空支撑（石块不落）
            w.setBlock(x0 + 4, kRigY + 3, z0, BR::Rail, 0);   // 高架轨
            w.setBlock(x0 + 4, kRigY - 1, z0, BR::Stone, 0);  // 接住地板（顶 = kRigY）
            carts.spawnCart(x0 + 4, kRigY + 3, z0, &w);       // 停驻高架轨（(a) 车已毁 → 槽 0 复用）
            const float y0b = carts.posAt(0).y();
            w.setBlock(x0 + 4, kRigY + 3, z0, BR::Air, 0);     // 挖轨
            w.setBlock(x0 + 4, kRigY + 2, z0, BR::Air, 0);     // 挖支撑
            for (int t = 0; t < 120; ++t) carts.tickPushedCarts(0.016, &w);
            const QVector3D fb = carts.posAt(0);
            const bool okB = carts.aliveAt(0)
                && y0b > float(kRigY + 3)                       // 起始确实在高架轨面
                && std::fabs(fb.y() - (float(kRigY) + groundH)) < 0.02f // 坠落到接住地板贴面停驻
                && std::fabs(fb.x() - float(x0 + 4) - 0.5f) < 0.05f;    // 原列坠落（无水平速度）
            if (!okB) qInfo().noquote() << "  t863(b) fall final" << fb << "y0" << y0b;
            // 清 (b)：销毁坠落车 + 拆接住地板。
            carts.hitCartFromRay(QVector3D(fb.x(), fb.y() + 3.0f, fb.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            w.setBlock(x0 + 4, kRigY - 1, z0, BR::Air, 0);

            // ── (c) 坡顶死端飞出：x0 低平 + x0+1 坡（东邻 x0+2@Y+1 **最后一格** —— 后邻低一格 = 坡顶）+
            //        轨端外接地板（x0+3..x0+5 @kRigY，顶 = Y+1 同轨面高）。被骑 W 冲顶 → 飞出平抛落板。──
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);       // 坡（东邻高格轨 → 抬升）
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);   // 坡顶死端（东端无轨）
            for (int i = 3; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Stone, 0); // 落地板
            carts.spawnCart(x0, kRigY, z0, &w);               // （(b) 车已毁 → 槽 0 复用）
            const QVector3D mountC(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            const bool rodeC = carts.tryMount(mountC, QVector3D(0, -1, 0), 4.0f);
            bool flewC = false;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (cp.x() > float(x0 + 2) + 0.6f && cp.y() < float(kRigY + 1) + rideH - 0.03f)
                    flewC = true; // 过坡顶格心后低于轨面 = 平抛下坠（不自动暂停）
            }
            const QVector3D fc = carts.posAt(0);
            const bool okC = rodeC && flewC
                && fc.x() > float(x0 + 3)                       // 落到轨端外地板（滑行渐停位）
                && fc.x() < float(x0 + 6)
                && std::fabs(fc.y() - (float(kRigY + 1) + groundH)) < 0.02f;
            if (!okC) qInfo().noquote() << "  t863(c) launch final" << fc << "flew" << flewC
                                          << "x0" << x0 << "z0" << z0
                                          << "blocks" << int(w.blockAt(x0 + 1, kRigY, z0))
                                          << int(w.blockAt(x0 + 2, kRigY + 1, z0))
                                          << int(w.blockAt(x0 + 3, kRigY, z0));
            // 清 (c)：拆坡 / 坡顶轨 / 落地板（低平 x0 轨留作 (d)）。
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Air, 0);
            for (int i = 3; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);

            // ── (d) 轨末端推离：x0 单轨死端（孤轨）+ 端外接地板（x0+1..x0+3 @kRigY-1，顶 = kRigY）。
            //        静止车被玩家朝 +X 端外推 → 出轨推离 → 贴地滑行落板停驻。──
            for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
            carts.spawnCart(x0, kRigY, z0, &w);               // （(c) 车留板上；槽 1 —— (c) 车滑停在 x0+5 附近不挡本段）
            QVector3D pusher = carts.posAt(1) + QVector3D(-0.4f, 0.0f, 0.0f); // 玩家在西侧贴住
            for (int t = 0; t < 300; ++t) {
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);  // 朝 +X（端外向）推
                carts.tickPushedCarts(0.016, &w);
                if (carts.posAt(1).x() > float(x0) + 0.6f) break; // 已离轨格
                pusher.setX(carts.posAt(1).x() - 0.4f);        // 追着推
            }
            for (int t = 0; t < 200; ++t) carts.tickPushedCarts(0.016, &w); // 滑行渐停
            const QVector3D fd = carts.posAt(1);
            const bool okD = fd.x() > float(x0) + 0.6f                          // 推离轨道
                && std::fabs(fd.y() - (float(kRigY) + groundH)) < 0.02f         // 贴地（地板顶 + groundH）
                && std::fabs(fd.z() - float(z0) - 0.5f) < 0.05f;                // 不侧漂
            if (!okD) qInfo().noquote() << "  t863(d) push-off final" << fd;
            const bool ok = okA && okB && okC && okD;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t863 ramp physics: uphill stall slides back to foot, mined support"
                                 " drops parked cart onto catch floor, crest dead-end launches at speed"
                                 " onto beyond-end floor, dead-end cart pushable off the rail into free"
                                 " physics";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── t864 矿车互卡悬浮探针（MinecartManager 直编；PlayerController 骑乘帧同序驱动）──
    //   用户报告（R19.15 玩法阻塞）：「上坡被前方矿车卡住时悬浮原地一直向上——碰撞卡阻时应停驻 / 滑回，
    //   不向上漂」。驱动序镜像 PlayerController 骑乘分支（tickRiddenCart → tickPushedCarts →
    //   resolveCartCollisions）。断言：
    //   (a) 无上漂：被卡车全程 Y 恒贴轨面（±0.1——旧症状「一直向上」= Y 持续抬升脱离轨面）；
    //   (b) 卡阻终态 = 停驻 / 滑回：被卡车不越过前车（A.x < B.x 恒成立），且全程存在「首次接触后回落」
    //       （反溜 / 被顶回——碰撞冲量 + 死区 + t863① 反溜链把卡阻车送回坡下，不在坡面悬停）。
    runLegMulti({ "t864 uphill cart-cart jam: blocked cart stays glued to rail surface (no upward drift), never pen"
        "etrates the blocker, and falls back after first contact (stall/slide-back, no mid-air hover)" }, [&]() {
        // rig 选址：运行期扫描空区。需 8×1×4 净空。坡：x0..x0+1 低平 + x0+2 坡（东邻高 1）+
        //   x0+3..x0+4 高平（B 停驻死端）；A 被骑从坡脚冲。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 7 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t864 cart-cart uphill jam: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            const auto wantSurf = [&](float x) {
                if (x < float(x0 + 1)) return float(kRigY);
                if (x > float(x0 + 2)) return float(kRigY + 1);
                return float(kRigY) + (x - float(x0 + 1));
            };
            // 3 格轨：x0 低平 + x0+1 坡（东邻高 1）+ x0+2 坡顶死端（B 停驻格）。A 被卡在坡面格 →
            //   松手 / 下车后有梯度可反溜（t863①）；高平延长段会让被卡点落在无梯度平段（平段停驻
            //   语义，非本症状）—— 首版 5 格 rig 实测踩坑。
            for (int i = 0; i <= 2; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 2) ? 1 : 0), z0, BR::Rail, 0);
            MinecartManager carts;
            // B 停驻**坡顶死端格**（x0+2@Y+1：后邻低一格的爬升顶、东端无轨）→ A 被卡在坡面格
            //   （x0+1）上有梯度 → 松手 / 下车后摩擦死区 + t863① 反溜可触发放回（高平面被卡无梯度
            //   不反溜——那是平段停驻语义，非本症状）。
            carts.spawnCart(x0 + 2, kRigY + 1, z0, &w);       // B：坡顶死端静止车（挡路；槽 0）
            carts.spawnCart(x0, kRigY, z0, &w);               // A：坡脚车（槽 1）
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            float maxLift = 0.0f;      // Y 超出轨面的最大量（上漂签名；容 0.1 内的接触抖动）
            bool neverPassed = true;
            for (int t = 0; t < 900; ++t) { // (a) W 持续冲坡 - 卡阻：供能不断的极限压测（旧症状「一直向上」）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp); // W 持续（卡阻供能不断）
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
                const QVector3D a = carts.posAt(1), b = carts.posAt(0);
                maxLift = std::max(maxLift, float(a.y() - (wantSurf(a.x()) + rideH)));
                if (a.x() >= b.x() - 0.05f) neverPassed = false;   // A 越过 / 并入 B = 穿透
            }
            const bool okA = maxLift <= 0.1f && neverPassed;    // 无上漂（贴轨面）+ 不穿透
            // (b) 无动力被卡（下车后空车留坡面）：A 已停在接触位附近（W 段终点），无持续供能 → 坡面
            //     摩擦死区 + t863① 反溜链把卡阻车送回坡脚（「停驻 / 滑回」，不悬停坡面）。
            carts.dismount(nullptr, cp);                        // 下车（A 变空车；玩家位丢弃）
            for (int t = 0; t < 600; ++t) { // 9.6s：摩擦 - 死区 - 反溜 - 滑回
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
            }
            const QVector3D fa = carts.posAt(1);
            const bool okB = fa.x() < float(x0) + 1.0f                           // 滑回低平段（坡脚）
                && std::fabs(fa.y() - (float(kRigY) + rideH)) < 0.02f            // 贴低平轨面停驻
                // t909② 语义适配：B（坡顶死端格停驻的挡路车）在新坡道物理下会**自然滚回坡下**
                //（静置空车坡道自溜 —— 机制等价 MC 坡上的车不停驻；旧「B 仍在坡顶格」钉的是 t708
                // 保守闸门时代的行为）。改钉：两车都活着、仍在 rig 轨段内、且分离 ≥0.85（A 的滑回
                // 不是把 B 顶穿 / 顶飞下山——B 的位移是自重滚落，非 A 推挤穿透）。
                && carts.aliveAt(0)
                && carts.posAt(0).x() > float(x0) - 0.5f && carts.posAt(0).x() < float(x0) + 2.6f
                && std::fabs(carts.posAt(0).x() - fa.x()) >= 0.85f;
            ok = ok && okA && okB;
            if (!ok)
                qInfo().noquote() << "  t864 maxLift" << maxLift << "neverPassed" << neverPassed
                                  << "slidebackFinal" << fa;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t864 uphill cart-cart jam: blocked cart stays glued to rail surface"
                                 " (no upward drift), never penetrates the blocker, and falls back after"
                                 " first contact (stall/slide-back, no mid-air hover)";
            // 清场
            carts.clearAll();
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY + ((i >= 2) ? 1 : 0), z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── review26 #14 derailed 矿车可被撞滑探针（MinecartManager 直编，t863(d)/t864 推离 rig 族）──
    //   用户症状（review26 低危）：出轨落地的矿车在车-车碰撞中是不可推动的幽灵障碍 —— 行进车撞上
    //   出轨车被每帧顶回、出轨车纹丝不动，动力轨也推不过去（clampShift 无轨列恒 0 + impulseDirOk
    //   需轨连接）。修：derailed 态加碰撞分支（冲量放行 + 自由体墙检去穿插）。断言：
    //   (a) 停驻出轨车被行进车撞后位移 ≥0.5 格（旧代码恒 0 = 幽灵障碍签名，回退即红）；
    //   (b) 终态两车分离 ≥0.85（kCartCollideSep−ε：无穿透互锁 / 无永久贴脸抖动）；
    //   (c) 行进车推进 ≥1 格（撞滑不吞行进侧动量到「原地锁死」）。
    runLegMulti({ "review26-14 derailed cart is knockable: a sliding cart striking a derailed-parked cart displaces"
        " it >=0.5 cells (old code: immovable ghost obstacle), both settle apart >=0.85 with no interpene"
        "tration lock, and the striker keeps >=1 cell of progress" }, [&]() {
        // rig 选址：运行期扫描空区（t863 同款）。需 11×1×4 净空（地板走廊 x0..x0+8 + 隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 10 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 10 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-14 derailed cart knockable: no clear rig area found";
        } else {
            const float groundH = 0.3875f; // kCartGroundH 镜像（出轨贴地落定中心偏移）
            // 走廊地板 x0+1..x0+8 @kRigY-1（顶 = kRigY）；A 起动轨 x0、B 出轨用临时轨 x0+5（轨下地板承接）。
            for (int i = 1; i <= 8; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 5, kRigY, z0, BR::Rail, 0);
            MinecartManager carts;
            // B：临时轨上静止车被向西推离 → derailed + 沿 -X 贴地滑行 ≤2 格（4.0/摩擦 2）→ 停驻走廊中段。
            carts.spawnCart(x0 + 5, kRigY, z0, &w); // B（槽 0）
            QVector3D pusherB(carts.posAt(0).x() + 0.4f, carts.posAt(0).y(), carts.posAt(0).z());
            for (int t = 0; t < 400; ++t) {
                carts.pushEmptyCart(&w, pusherB, -1.0f, 0.0f); // 朝 -X（走廊内侧）推
                carts.tickPushedCarts(0.016, &w);
                if (carts.posAt(0).x() < float(x0 + 5) - 0.4f) break; // 已离临时轨格 → 出轨成立
                pusherB.setX(carts.posAt(0).x() + 0.4f);              // 追着推
            }
            for (int t = 0; t < 300; ++t) carts.tickPushedCarts(0.016, &w); // 滑行摩擦停驻
            const float bPark = carts.posAt(0).x();
            const float bParkY = carts.posAt(0).y();
            const bool bRigOk = bPark < float(x0 + 5) - 0.3f            // 确已推离临时轨
                && bPark > float(x0 + 2) + 0.2f                          // 停驻走廊中段（A 进攻走廊可达）
                && std::fabs(bParkY - (float(kRigY) + groundH)) < 0.02f; // 贴地落定（derailed 落地形态）
            w.setBlock(x0 + 5, kRigY, z0, BR::Air, 0); // 拆临时轨（B 列确认无轨 = 自由体分支前置）
            // A：西端轨上静止车被向东反复推（停驻即再推，单次滑 ≤2 格）→ 撞上 B。B 在 A 东侧 →
            //   被撞离向 = +X（冲量沿 n=B−A 推离；B.dir=-X × 负速 = +X 位移，与「顶退-推离」注释一致）。
            //   **推手距离门**（阴性验证教训）：只在 A.x < bPark−1.35 时跟推 —— 推手贴 A 后 0.4 → 距 B
            //   恒 ≥1.75 > kCartPushReach(0.8)，玩家永不直接推到 B（旧代码下 A 穿进 B 时跟推会把 B 直接
            //   推走 = 假绿污染通道）；A 的最后一推从 ≤bPark−1.35 处滑 ≤2 格必触 B（触点差 <1.4 格）。
            carts.spawnCart(x0, kRigY, z0, &w); // A（槽 1）
            QVector3D pusherA(carts.posAt(1).x() - 0.4f, carts.posAt(1).y(), carts.posAt(1).z());
            float bMaxX = bPark;
            for (int t = 0; t < 1200; ++t) {
                const float axNow = carts.posAt(1).x();
                if (bRigOk && axNow < bPark - 1.35f) {
                    pusherA.setX(axNow - 0.4f);
                    carts.pushEmptyCart(&w, pusherA, 1.0f, 0.0f); // 距离门内才跟推
                }
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
                bMaxX = std::max(bMaxX, float(carts.posAt(0).x()));
            }
            for (int t = 0; t < 200; ++t) { // 收尾：撞滑余动量摩擦停驻
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
            }
            const float fa = carts.posAt(1).x(), fb = carts.posAt(0).x();
            const bool okMove = bRigOk && (bMaxX - bPark) >= 0.5f;              // (a) 出轨车被撞滑 ≥0.5 格（撞离向）
            const bool okSep = carts.aliveAt(0) && carts.aliveAt(1)
                && std::fabs(fa - fb) >= 0.85f;                                 // (b) 终态无穿透互锁
            const bool okProg = fa >= float(x0) + 1.0f;                         // (c) 行进车推进 ≥1 格
            const bool ok14 = okMove && okSep && okProg;
            if (!ok14)
                qInfo().noquote() << "  review26-14 bPark" << bPark << "bMaxX" << bMaxX
                              << "finalA" << fa << "finalB" << fb << "bRigOk" << bRigOk;
            if (!ok14) ++totalFail;
            qInfo().noquote() << (ok14 ? "PASS" : "FAIL")
                              << "| review26-14 derailed cart is knockable: a sliding cart striking a"
                                 " derailed-parked cart displaces it >=0.5 cells (old code: immovable"
                                 " ghost obstacle), both settle apart >=0.85 with no interpenetration"
                                 " lock, and the striker keeps >=1 cell of progress";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 8; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── t907 密闭单格 cart-cart 挤压飞穿探针（MinecartManager 直编；t864/review26-14 rig 族）──
    //   用户报告（R19.16 玩法阻塞）：全封闭单格空间内矿车互挤，碰撞解析把车飞出牢笼（穿实体方块）。
    //   硬不变量（spec 原文）：解析结果不得写车入实体格；任何弹射不得越实体墙。断言三段（驱动序镜像
    //   PlayerController 非骑乘分支 tickPushedCarts → pushEmptyCart → resolveCartCollisions）：
    //   (a) 密闭双格轨笼对挤：两轨车被玩家交替两侧持续挤 600 tick —— 全程两车车心永不入实体格、
    //       永不出笼（x∈[x0,x0+2)），终态两车存活且分离 ≥0.85（去穿插仍工作 = 非冻结假绿）；
    //   (b) 密闭单格地面笼：两地面车（t734 静止）互挤 + 推离弹射（t863④ push-off derailed）——
    //       车心永不入实体格、不出笼；
    //   (c) dt 尖峰弹射穿墙（「任何弹射不得越实体墙」的弹射半边）：地面车推离获 4 blocks/s 后
    //       tickPushedCarts(0.3f)（tick 饿死窗口，t904 实测 16ms 定时器可被饿到 1/3 以下）—— 单步
    //       1.2 格 > 1 格厚墙：旧版一步欧拉只验落点格（墙后开格）→ 整车穿墙；子步化（≤0.45 格逐格
    //       墙检）后贴墙停驻。
    runLegMulti({ "t907 sealed-cage cart squeeze: collision resolution keeps both cart centers out of solid cells a"
        "nd inside a fully sealed 2-cell rail cage (separation still resolves >=0.85, not frozen), ground"
        " carts squeezed in a 1-cell stone box stay boxed, a derailed ejection at 4 blocks/s through a 0."
        "5s starved tick cannot tunnel the 1-thick wall (substepped wall checks; old single-step Euler la"
        "nded past the wall in the open cell), and two carts alternately squeezed across a 1:1 slope step"
        " (rail layers differ by 1, dead-end launches caught by end walls) still end >=0.85 apart - revie"
        "w28 #7 lets the depenetration gate admit |rail layer delta| <= 1 slope continuations, pinned by "
        "P-review28c" }, [&]() {
        // rig 选址：运行期扫描空区（t863 先例）。需 12×1×4 净空（(a)(b) 笼 + (c) 穿墙走廊 + 隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 11 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 11 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t907 sealed-cage squeeze: no clear rig area found";
        } else {
            // 车心「在实体格内」判定（硬不变量口径：中心腰位格实体 = 入墙；腰位 = floor(y-0.1)，
            //   同 tickDerailedCart / clampShift 墙检口径）。
            const auto centerInSolid = [&](const QVector3D &p) {
                return w.isCollidable(int(std::floor(p.x())), int(std::floor(p.y() - 0.1f)),
                                      int(std::floor(p.z())));
            };
            // ── (a) 密闭双格轨笼：内部 (x0,Y)/(x0+1,Y) 两格；地板 Y-1、墙 (x0-1,Y)/(x0+2,Y)、
            //        侧墙 z0±1（Y..Y+1）、顶 Y+1。轨两根（互连 2 格孤段——墙列 ±1 高度皆无轨）。──
            for (int i = 0; i <= 1; ++i) {
                w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);          // 地板
                w.setBlock(x0 + i, kRigY, z0, BR::Rail, 0);               // 轨
                w.setBlock(x0 + i, kRigY + 1, z0, BR::Stone, 0);          // 顶
                w.setBlock(x0 + i, kRigY, z0 - 1, BR::Stone, 0);          // 侧墙
                w.setBlock(x0 + i, kRigY + 1, z0 - 1, BR::Stone, 0);
                w.setBlock(x0 + i, kRigY, z0 + 1, BR::Stone, 0);
                w.setBlock(x0 + i, kRigY + 1, z0 + 1, BR::Stone, 0);
            }
            w.setBlock(x0 - 1, kRigY, z0, BR::Stone, 0);                   // 端墙
            w.setBlock(x0 + 2, kRigY, z0, BR::Stone, 0);
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);       // A（槽 0，西格）
            carts.spawnCart(x0 + 1, kRigY, z0, &w);   // B（槽 1，东格）
            bool okA = true;
            float minSep = 99.0f;
            for (int t = 0; t < 600; ++t) {
                // 玩家交替两侧挤（每 100 tick 换向）：推手贴目标车后 0.4（kCartPushReach 0.8 内）。
                const int side = (t / 100) % 2; // 0 = 东侧推西向（推 B），1 = 西侧推东向（推 A）
                const int tgt = side == 0 ? 1 : 0;
                const QVector3D tp = carts.posAt(tgt);
                const QVector3D pusher(tp.x() + (side == 0 ? 0.4f : -0.4f), tp.y(), tp.z());
                carts.pushEmptyCart(&w, pusher, side == 0 ? -1.0f : 1.0f, 0.0f);
                carts.tickPushedCarts(0.016f, &w);
                carts.resolveCartCollisions(&w);
                for (int ci = 0; ci <= 1; ++ci) {
                    const QVector3D p = carts.posAt(ci);
                    if (!carts.aliveAt(ci) || centerInSolid(p)
                        || p.x() < float(x0) || p.x() >= float(x0 + 2))
                        okA = false;
                }
                const QVector3D pa = carts.posAt(0), pb = carts.posAt(1);
                minSep = std::min(minSep, float(pa.x() - pb.x()));
            }
            const QVector3D fa = carts.posAt(0), fb = carts.posAt(1);
            okA = okA && carts.aliveAt(0) && carts.aliveAt(1)
                && std::fabs(fa.x() - fb.x()) >= 0.85f;   // 终态分离（去穿插工作，非冻结）
            if (!okA)
                qInfo().noquote() << "  t907(a) cage final" << fa << fb
                                  << "alive" << carts.aliveAt(0) << carts.aliveAt(1);
            // 清 (a)：毁车 + 拆笼（地板 / 轨 / 顶 / 侧墙 / 端墙）。
            carts.hitCartFromRay(QVector3D(fa.x(), fa.y() + 3.0f, fa.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            carts.hitCartFromRay(QVector3D(fb.x(), fb.y() + 3.0f, fb.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            for (int i = -1; i <= 2; ++i)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (w.blockAt(x0 + i, kRigY + dy, z0 + dz) != BR::Air)
                            w.setBlock(x0 + i, kRigY + dy, z0 + dz, BR::Air, 0);

            // ── (b) 密闭单格地面笼：内部单格 (x0,Y)；地板 / 四墙 / 顶全石。两地面车互挤 + 反复推离。──
            w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx != 0 || dz != 0) w.setBlock(x0 + dx, kRigY, z0 + dz, BR::Stone, 0);
            w.setBlock(x0, kRigY + 1, z0, BR::Stone, 0);
            bool okB = true;
            {
                MinecartManager gc;                       // 独立管理器（槽位语义干净：0/1）
                gc.spawnCart(x0, kRigY, z0, &w);          // 地面静止模式（槽 0）
                gc.spawnCart(x0, kRigY, z0, &w);          // 同格第二车（槽 1；同格互挤形态）
                for (int t = 0; t < 400; ++t) {
                    const QVector3D tp = gc.posAt(1);
                    const QVector3D pusher(tp.x() + ((t / 80) % 2 ? 0.4f : -0.4f), tp.y(), tp.z());
                    gc.pushEmptyCart(&w, pusher, (t / 80) % 2 ? -1.0f : 1.0f, 0.0f);
                    gc.tickPushedCarts(0.016f, &w);
                    gc.resolveCartCollisions(&w);
                    for (int ci = 0; ci < gc.count(); ++ci) {
                        if (!gc.aliveAt(ci)) continue;
                        const QVector3D p = gc.posAt(ci);
                        if (centerInSolid(p) || std::floor(p.x()) != x0 || std::floor(p.z()) != z0)
                            okB = false;
                    }
                }
                okB = okB && gc.aliveAt(0) && gc.aliveAt(1);
                if (!okB) qInfo().noquote() << "  t907(b) ground-cage pos" << gc.posAt(0) << gc.posAt(1);
                gc.clearAll();
            }
            // 清 (b)：拆笼。
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (w.blockAt(x0 + dx, kRigY + dy, z0 + dz) != BR::Air)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);

            // ── (c) dt 尖峰弹射穿墙：走廊地板 (x0+2..x0+5,Y-1) + 1 格厚墙 (x0+6,Y) + 墙后开格。
            //        地面车推离（push-off 4 blocks/s derailed）滑向墙磨停 → 再推一次（speed 重置 4.0）
            //        → 立即 tickPushedCarts(0.5f)（tick 饿死病理帧：拖窗 / 断点 / 饿死，t904 实测定时器
            //        可被饿）—— 单步 2.0 格挑战 1 格厚墙：旧版一步欧拉只验**落点格**（墙后开格 → 放行）
            //        整车穿墙；子步化（≤0.45 格逐格墙检）贴墙停驻。──
            for (int i = 2; i <= 5; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0); // 地板
            w.setBlock(x0 + 6, kRigY, z0, BR::Stone, 0);                                  // 1 格厚墙
            bool okC = true;
            QVector3D fc;
            {
                MinecartManager sc;                        // 独立管理器（槽 0）
                sc.spawnCart(x0 + 3, kRigY, z0, &w);       // 地面静止车（走廊地板上）
                {
                    const QVector3D tp = sc.posAt(0);
                    sc.pushEmptyCart(&w, QVector3D(tp.x() - 0.4f, tp.y(), tp.z()), 1.0f, 0.0f); // 推离
                    for (int t = 0; t < 400; ++t) sc.tickPushedCarts(0.016f, &w);               // 滑向墙磨停
                }
                {
                    const QVector3D tp = sc.posAt(0);
                    sc.pushEmptyCart(&w, QVector3D(tp.x() - 0.4f, tp.y(), tp.z()), 1.0f, 0.0f); // 再推（speed 4.0）
                    sc.tickPushedCarts(0.5f, &w);          // dt 尖峰：单步 2.0 格挑战 1 格厚墙
                }
                for (int t = 0; t < 20; ++t) sc.tickPushedCarts(0.016f, &w); // 尖峰后余速收尾
                fc = sc.posAt(0);
                // 车心留在墙格前（贴墙停驻位 ~墙边界−0.05 内属正常；穿墙 = 落墙格 / 墙后开格）
                okC = sc.aliveAt(0) && fc.x() < float(x0 + 6) - 0.02f;
                okC = okC && !centerInSolid(fc);
                if (!okC) qInfo().noquote() << "  t907(c) dt-spike final" << fc << "alive" << sc.aliveAt(0);
                sc.clearAll();
            }
            // ── (d) review28 #7：坡段双车挤压分离 ≥0.85（近层闸 |Δ层|≤1 后的坡段不变量行为级钉死）。
            //        rig：平段 (x0,Y)(x0+1,Y) + 1:1 上坡 (x0+2,Y+1) + 平顶 (x0+3..x0+4,Y+1)；两端
            //        接轨墙 + 分层地板（下坡加速的轨端弹射（t863③ ≥3.0）被墙接住、落在 rig 地板上，
            //        不坠出 rig）。两车交替对挤 600 tick（镜像 (a) 驱动序）——断言全程两车不入实体格、
            //        不出 rig、不坠层，终态水平分离 ≥0.85（坡格轨层差 ±1 区的跨格去穿插在近层闸下
            //        照常收口）。闸形本身由 P-review28c 源码钉单独钉（本腿钉行为不变量）。──
            bool okD = true;
            {
                for (int i = -1; i <= 1; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);   // 低段地板
                for (int i = 2; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Stone, 0);        // 高段地板
                w.setBlock(x0 - 1, kRigY, z0, BR::Stone, 0);                                     // 西接轨墙
                w.setBlock(x0 - 1, kRigY + 1, z0, BR::Stone, 0);
                w.setBlock(x0 + 5, kRigY + 1, z0, BR::Stone, 0);                                 // 东接轨墙
                w.setBlock(x0 + 5, kRigY + 2, z0, BR::Stone, 0);
                w.setBlock(x0, kRigY, z0, BR::Rail, 0);
                w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
                for (int i = 2; i <= 4; ++i) w.setBlock(x0 + i, kRigY + 1, z0, BR::Rail, 0);
                MinecartManager dc;
                dc.spawnCart(x0 + 1, kRigY, z0, &w);       // A（低段坡脚格）
                dc.spawnCart(x0 + 3, kRigY + 1, z0, &w);   // B（坡上平顶）
                for (int t = 0; t < 600; ++t) {
                    // 交替对挤（每 100 tick 换向，镜像 (a)）：0 = 东端推西向（压 B 下坡冲 A）、
                    //   1 = 西端推东向（压 A 上坡冲 B）——挤压发生在坡格边界两侧（轨层差 ±1 区）。
                    const int side = (t / 100) % 2;
                    const int tgt = side == 0 ? 1 : 0;
                    const QVector3D tp = dc.posAt(tgt);
                    dc.pushEmptyCart(&w, QVector3D(tp.x() + (side == 0 ? 0.4f : -0.4f), tp.y(), tp.z()),
                                     side == 0 ? -1.0f : 1.0f, 0.0f);
                    dc.tickPushedCarts(0.016f, &w);
                    dc.resolveCartCollisions(&w);
                    for (int ci = 0; ci < 2; ++ci) {
                        const QVector3D p = dc.posAt(ci);
                        if (!dc.aliveAt(ci) || centerInSolid(p)
                            || p.x() <= float(x0 - 1) || p.x() >= float(x0 + 6)
                            || p.y() < float(kRigY) - 0.6f || p.y() > float(kRigY) + 2.6f)
                            okD = false;
                    }
                }
                const QVector3D da = dc.posAt(0), db = dc.posAt(1);
                okD = okD && dc.aliveAt(0) && dc.aliveAt(1)
                    && std::fabs(da.x() - db.x()) >= 0.85f; // 终态分离（坡段挤压不冻结、不永久重叠）
                if (!okD)
                    qInfo().noquote() << "  t907(d) slope final" << da << db
                                      << "alive" << dc.aliveAt(0) << dc.aliveAt(1);
                // 清 (d)：毁车 + 拆 rig。
                dc.hitCartFromRay(QVector3D(da.x(), da.y() + 3.0f, da.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
                dc.hitCartFromRay(QVector3D(db.x(), db.y() + 3.0f, db.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
                dc.clearAll();
                for (int i = -1; i <= 1; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                for (int i = 2; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY + 1, z0, BR::Air, 0);
                w.setBlock(x0 + 5, kRigY + 1, z0, BR::Air, 0);
                w.setBlock(x0 + 5, kRigY + 2, z0, BR::Air, 0);
                w.setBlock(x0, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
                for (int i = 2; i <= 4; ++i) w.setBlock(x0 + i, kRigY + 1, z0, BR::Air, 0);
            }
            const bool ok = okA && okB && okC && okD;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t907 sealed-cage cart squeeze: collision resolution keeps both cart "
                                 "centers out of solid cells and inside a fully sealed 2-cell rail cage "
                                 "(separation still resolves >=0.85, not frozen), ground carts squeezed in "
                                 "a 1-cell stone box stay boxed, a derailed ejection at 4 blocks/s "
                                 "through a 0.5s starved tick cannot tunnel the 1-thick wall (substepped "
                                 "wall checks; old single-step Euler landed past the wall in the open cell), "
                                 "and two carts alternately squeezed across a 1:1 slope step (rail layers "
                                 "differ by 1, dead-end launches caught by end walls) still end >=0.85 "
                                 "apart - review28 #7 lets the depenetration gate admit |rail layer "
                                 "delta| <= 1 slope continuations, pinned by P-review28c";
            // 清场
            carts.clearAll();
            for (int i = 2; i <= 6; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 6, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── t908 玩家推车向量分解探针（MinecartManager 直编；spec「轨上矿车被玩家身体/推动时只接受沿轨
    //   前后分量，横向推无效（不脱轨）；pushEmptyCart 与玩家碰撞挤推两路都按轨向投影」）──
    //   EW 三格直线轨 + 全线地板；车在中格 / 西端死端格。断言四段：
    //   (a) 中格横推（wish + away 皆 ⊥ 轨轴）：零位移（旧版 dot=0.25 兜底臂仍被选中 → 车被推走）；
    //   (b) 中格纵推（沿轨 -X）：沿轨位移 ≥0.8 格且恒贴轨面（轨约束行驶）；
    //   (c) 西端死端格横推：零位移 + 不脱轨（旧版选中内陆臂 dot=0.25 推走；再旧路径 = 合成主轴落
    //       垂直向时朝侧向弹出脱轨）；
    //   (d) 西端死端格**沿轴外向**推：t863④ 推离保留（出轨滑离轨端 —— 分解不吞死端合法推离）。
    runLegMulti({ "t908 push decomposition: lateral player push on a railed cart is a no-op (zero displacement, no "
        "derail - body-squeeze away and walking wish both project onto the rail axis), longitudinal push "
        "still rolls the cart along the rail glued to the surface, dead-end lateral push stays put, and t"
        "he along-axis outward push at the dead end still knocks the cart off the rail end (t863 push-off"
        " preserved, direction = rail-axis sign); review28 #6 adds orphan-rail symmetry - a 0-connection "
        "rail carrying only the EW axis-preference bit is pushed off by a lateral push exactly like a sta"
        "te-0 NS orphan (the axis bit is texture/rise metadata, not a push decomposition axis - old code "
        "swallowed lateral pushes on EW orphans only, an orientation-dependent asymmetry)" }, [&]() {
        // rig 选址：运行期扫描空区。需 10×1×4 净空（地板 x0-3..x0+2 + 轨 3 格 + 隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 9 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 9 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t908 push vector decomposition: no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 镜像
            for (int i = -3; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0); // 地板
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Rail, 0);       // EW 三格轨
            MinecartManager carts;
            // (a)+(b) 中格车。
            carts.spawnCart(x0 + 1, kRigY, z0, &w);
            const QVector3D p0 = carts.posAt(0);
            for (int t = 0; t < 60; ++t) { // (a) 横推：玩家在车南侧贴住、wish 朝北（皆 ⊥ EW 轨轴）
                carts.pushEmptyCart(&w, QVector3D(p0.x(), p0.y(), p0.z() + 0.4f), 0.0f, -1.0f);
                carts.tickPushedCarts(0.016f, &w);
                carts.resolveCartCollisions(&w);
            }
            const QVector3D pa = carts.posAt(0);
            const bool okA = (pa - p0).length() < 1e-3f; // 零位移（横推无效）
            for (int t = 0; t < 300; ++t) { // (b) 纵推：玩家在车东侧贴住、wish 朝 -X（沿轨）。
                const QVector3D tp = carts.posAt(0);
                if (tp.x() > float(x0) + 1.0f)                    // t863④ 适配：车进西死端格后停推
                    carts.pushEmptyCart(&w, QVector3D(tp.x() + 0.4f, tp.y(), tp.z()), -1.0f, 0.0f);
                carts.tickPushedCarts(0.016f, &w);
                carts.resolveCartCollisions(&w);
            }
            const QVector3D pb = carts.posAt(0);
            const bool okB = pb.x() < p0.x() - 0.8f                             // 沿轨西移 ≥0.8
                && std::fabs(pb.y() - (float(kRigY) + rideH)) < 0.02f          // 恒贴轨面（未脱轨）
                && std::fabs(pb.z() - p0.z()) < 0.05f;                          // 不侧漂
            carts.hitCartFromRay(QVector3D(pb.x(), pb.y() + 3.0f, pb.z()),
                                 QVector3D(0, -1, 0), 4.0f, &w, true);          // 清 (a)(b) 车
            // (c)+(d) 西端死端格车（x0：仅 +X 内陆连接）。
            carts.spawnCart(x0, kRigY, z0, &w);
            const QVector3D q0 = carts.posAt(0);
            for (int t = 0; t < 60; ++t) { // (c) 死端横推：零位移 + 不脱轨
                carts.pushEmptyCart(&w, QVector3D(q0.x(), q0.y(), q0.z() + 0.4f), 0.0f, -1.0f);
                carts.tickPushedCarts(0.016f, &w);
                carts.resolveCartCollisions(&w);
            }
            const QVector3D pc = carts.posAt(0);
            const bool okC = (pc - q0).length() < 1e-3f;
            for (int t = 0; t < 300; ++t) { // (d) 死端沿轴外向推：t863④ 推离保留（出轨滑离轨端西行）
                const QVector3D tp = carts.posAt(0);
                if (tp.x() < float(x0) - 0.6f) break; // 已滑离轨端（免追推干扰）
                carts.pushEmptyCart(&w, QVector3D(tp.x() + 0.4f, tp.y(), tp.z()), -1.0f, 0.0f);
                carts.tickPushedCarts(0.016f, &w);
                carts.resolveCartCollisions(&w);
            }
            for (int t = 0; t < 200; ++t) carts.tickPushedCarts(0.016f, &w); // 滑行渐停
            const QVector3D pd = carts.posAt(0);
            const bool okD = pd.x() < float(x0) - 0.3f                          // 推离出轨（西行离轨格）
                && std::fabs(pd.z() - q0.z()) < 0.05f                           // 不侧漂（轨轴符号向）
                && carts.aliveAt(0);
            // 清场（先于 (e)/(f)：孤轨须与主线轨 / 旧车完全隔离 —— 轴位孤轨的连接重算会读到邻轨）。
            carts.clearAll();
            for (int i = -3; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
            // (e)/(f) review28 #6：孤轨推动语义对称（EW 轴偏好位孤轨 vs NS state=0 孤轨）。孤轨 = 0 连接
            //   （轴偏好位不构成定向）→ 全向可推；旧版对轴位孤轨的向量分解吞掉 ⊥ 轴偏好向的侧推（投影
            //   < kCartPushProjMin → no-op）而 state=0 孤轨任意可推 —— 同一布局仅放置朝向不同、推动行为
            //   恰好相反。两腿同构：孤轨上车、北向推（EW 轴偏好向的侧向）→ 必须推离出轨（位移 ≥0.3 即
            //   语义达成，不追满滑程 —— 车恒在地板带内）。
            bool okE = true, okF = true;
            QVector3D e0, e1, f0, f1;
            {
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0); // 地板（出轨贴地滑支撑）
                // (e) EW 孤轨（state = RailAxisEWFlag，t666 放置同款位）：北推 = ⊥ 轴偏好向。
                w.setBlock(x0, kRigY, z0, BR::Rail, BR::RailAxisEWFlag);
                MinecartManager ec;
                ec.spawnCart(x0, kRigY, z0, &w);
                e0 = ec.posAt(0);
                e1 = e0;
                for (int t = 0; t < 200; ++t) {
                    const QVector3D tp = ec.posAt(0);
                    ec.pushEmptyCart(&w, QVector3D(tp.x(), tp.y(), tp.z() + 0.4f), 0.0f, -1.0f);
                    ec.tickPushedCarts(0.016f, &w);
                    e1 = ec.posAt(0);
                    if (e1.z() < e0.z() - 0.3f) break; // 已推离 0.3（语义达成即停）
                }
                okE = e1.z() < e0.z() - 0.3f && std::fabs(e1.x() - e0.x()) < 0.05f && ec.aliveAt(0);
                ec.clearAll();
                w.setBlock(x0, kRigY, z0, BR::Air, 0);
                // (f) NS 孤轨（state = 0，无轴位）：同构北推（对称对照组 —— 两朝向语义一致）。
                w.setBlock(x0, kRigY, z0, BR::Rail, 0);
                MinecartManager fc;
                fc.spawnCart(x0, kRigY, z0, &w);
                f0 = fc.posAt(0);
                f1 = f0;
                for (int t = 0; t < 200; ++t) {
                    const QVector3D tp = fc.posAt(0);
                    fc.pushEmptyCart(&w, QVector3D(tp.x(), tp.y(), tp.z() + 0.4f), 0.0f, -1.0f);
                    fc.tickPushedCarts(0.016f, &w);
                    f1 = fc.posAt(0);
                    if (f1.z() < f0.z() - 0.3f) break;
                }
                okF = f1.z() < f0.z() - 0.3f && std::fabs(f1.x() - f0.x()) < 0.05f && fc.aliveAt(0);
                fc.clearAll();
                w.setBlock(x0, kRigY, z0, BR::Air, 0);
                // 清地板。
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
                tickN(w, 2);
            }
            const bool ok = okA && okB && okC && okD && okE && okF;
            if (!ok)
                qInfo().noquote() << "  t908 lateralA" << pa << "longB" << pb
                                  << "deadLatC" << pc << "deadAxD" << pd
                                  << "orphanEwE" << (e1 - e0) << "orphanNsF" << (f1 - f0);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t908 push decomposition: lateral player push on a railed cart is a "
                                 "no-op (zero displacement, no derail - body-squeeze away and walking wish "
                                 "both project onto the rail axis), longitudinal push still rolls the cart "
                                 "along the rail glued to the surface, dead-end lateral push stays put, and "
                                 "the along-axis outward push at the dead end still knocks the cart off the "
                                 "rail end (t863 push-off preserved, direction = rail-axis sign); "
                                 "review28 #6 adds orphan-rail symmetry - a 0-connection rail carrying only "
                                 "the EW axis-preference bit is pushed off by a lateral push exactly like a "
                                 "state-0 NS orphan (the axis bit is texture/rise metadata, not a push "
                                 "decomposition axis - old code swallowed lateral pushes on EW orphans "
                                 "only, an orientation-dependent asymmetry)";
        }
    });

    // ── P-review28a 睡眠分支探测轨占用重扫源码钉（review28 #5；行为级 headless 不可达——PlayerController
    //    睡眠窗口分支需 Game 层 tick 驱动，退路 = 源码钉两调齐全 + 次序，P-t887b 先例）──
    //    review26 #13 给睡眠分支补 checkCartEnvironment 却漏了 updateDetectorRailOccupancy——环境检查
    //    销毁压探测轨的车后，该轨带电滞留整个睡眠窗口（骑船分支两调齐全）。断言：review28 #5 标记之后
    //    的窗口内先 checkCartEnvironment 后 updateDetectorRailOccupancy（tickPushedCarts 同序：环境
    //    检查在前、占用收口在后——毁车当帧收离开沿断电）。
    runLegMulti({ "review28a sleep-branch detector-rail rescan pin: the sleeping-window cart environment check is f"
        "ollowed by updateDetectorRailOccupancy within the same block (tickPushedCarts order - env check "
        "first, occupancy close after: a cart destroyed on a detector rail during sleep drops the power e"
        "dge the same frame instead of staying powered until wake; review26 #13 added the env check witho"
        "ut the rescan)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
        const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("review28 #5"));
        const QString seg = i0 >= 0 ? t.mid(i0, 800) : QString();
        const int iEnv = seg.indexOf(QStringLiteral("checkCartEnvironment(m_world)"));
        const int iOcc = seg.indexOf(QStringLiteral("updateDetectorRailOccupancy(m_world)"));
        const bool ok = i0 >= 0 && iEnv >= 0 && iOcc >= 0 && iEnv < iOcc;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review28a sleep-branch detector-rail rescan pin: the sleeping-window "
                             "cart environment check is followed by updateDetectorRailOccupancy within "
                             "the same block (tickPushedCarts order - env check first, occupancy close "
                             "after: a cart destroyed on a detector rail during sleep drops the power "
                             "edge the same frame instead of staying powered until wake; review26 #13 "
                             "added the env check without the rescan)";
    });

    // ── P-review28b 翻书大摆 worldRunning 硬档门源码钉（review28 #9；行为级 headless 不可达——ESC 硬暂停
    //    + 2.65s 大摆动画时序需真窗口，退路 = 源码钉变更 handler 语句面）──
    //    pageFlipAnim 本体 running:false 字面 + restart() 命令式驱动（声明式 worldRunning 门会与 restart
    //    抢 running 绑定）→ 硬档门补在 window 级 onWorldRunningChanged：硬档时 stop() + 复位 flipAngle
    //    （恢复侧由 pageFlutterAnim 的声明式 running 条件自动接管续摆）。断言三语句面存在。
    runLegMulti({ "review28b page-flip hard-pause gate pin: the big page-flip animation (imperatively driven - runn"
        "ing:false literal + restart(), a declarative worldRunning gate would fight restart over the runn"
        "ing binding) gets its hard-pause gate at the window-level onWorldRunningChanged handler: ESC dur"
        "ing the 2.65s swing stops the animation and resets flipAngle to the rest pose (resume is automat"
        "ic - the idle flutter animation's declarative running condition takes back over)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile qf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
        const bool ok = t.contains(QStringLiteral("onWorldRunningChanged: {"))
                     && t.contains(QStringLiteral("pageFlipAnim.stop()"))
                     && t.contains(QStringLiteral("flipPivot.flipAngle = 0.0"));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review28b page-flip hard-pause gate pin: the big page-flip animation "
                             "(imperatively driven - running:false literal + restart(), a declarative "
                             "worldRunning gate would fight restart over the running binding) gets its "
                             "hard-pause gate at the window-level onWorldRunningChanged handler: ESC "
                             "during the 2.65s swing stops the animation and resets flipAngle to the "
                             "rest pose (resume is automatic - the idle flutter animation's declarative "
                             "running condition takes back over)";
    });

    // ── P-review28c 去穿插近层闸源码钉（review28 #7；多车坡谷挤压的行为级构造需受控中间态（三车
    //    非重合定位无 headless 手段），闸形以源码钉钉死；坡段双车分离 ≥0.85 不变量由 t907(d) 行为级
    //    覆盖）──
    //    t907 原同层闸严格相等（== rySelf）把坡上跨格去穿插全钳回当前格——格宽 1.0 只容一车分离
    //    （kCartCollideSep 0.98），坡谷格 / 坡段格 ±1 邻接时多车或死端夹逼的对永久 <0.85 重叠。
    //    review28 #7 放行 |Δ轨层| ≤ 1（坡面延续）。**t983 闸形升级（review0830-B #12 翻案连带）**：
    //    旧的「ryTgt 宽容列扫首轨层 + |ryTgt−rySelf| ≤ 1」判据被「链延续层精确解」取代——rySelf +
    //    chainDelta（railProbeDelta 三高探针值域 {-1,0,+1}，±1 近层约束隐式保持）直接验该层轨本体
    //    （+ 下一帧列扫窗顶约束 ryChain ≤ floor(pos.y)），头顶并行线轨不再劫持列扫首层。断言（闸标记
    //    后窗口内）：链延续层提取 + 该层 isRail 精确验 + 腰位闸 isCollidable 配套（拆任一 → FAIL）。
    runLegMulti({ "review28c depenetration near-layer gate pin: clampShift admits cross-cell depenetration shifts o"
        "nly when the chain-continuation layer (rySelf + chainDelta, the +-1 probe domain keeping review2"
        "8 #7's slope-continuation admission) is a rail in the target column AND the landing waist cell i"
        "s non-solid - the old strict-equality gate clamped every slope-boundary shift back into the curr"
        "ent cell, and a 1-wide cell cannot fit two 0.98 separations, so carts wedged between +1/-1 slope"
        " neighbors stayed permanently overlapped (t983 swapped the old colRailY first-rail-layer compare"
        " for the exact chain layer so a parallel overhead line can no longer hijack the verdict)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile sf(root + QStringLiteral("/src/Entities/minecartmanager.cpp"));
        const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("review28 #7 近层闸"));
        const QString seg = i0 >= 0 ? t.mid(i0, 2900) : QString(); // t983 闸体加长（精确解两行）→ 窗口 1500→2900
        const bool ok = i0 >= 0
                     && seg.contains(QStringLiteral("const int ryChain = rySelf + chainDelta;"))
                     && seg.contains(QStringLiteral("BlockRegistry::isRail(world->blockAt(tx, ryChain, tz))"))
                     && seg.contains(QStringLiteral("isCollidable"));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review28c depenetration near-layer gate pin: clampShift admits "
                             "cross-cell depenetration shifts only when the chain-continuation layer "
                             "(rySelf + chainDelta, the +-1 probe domain keeping review28 #7's "
                             "slope-continuation admission) is a rail in the target column AND the "
                             "landing waist cell is non-solid - the old strict-equality gate clamped "
                             "every slope-boundary shift back into the current cell, and a 1-wide cell "
                             "cannot fit two 0.98 separations, so carts wedged between +1/-1 slope "
                             "neighbors stayed permanently overlapped (t983 swapped the old "
                             "colRailY first-rail-layer compare for the exact chain layer so a "
                             "parallel overhead line can no longer hijack the verdict)";
    });

    // ── t909 V 形动力永动探针（MinecartManager 直编；spec「V 底两格激活动力轨应无限往复 / 半山腰放置
    //   应下坡运动 / 下坡初速与加速度加大」）──
    //   rig：V 形 —— 底部两格激活动力轨（GoldenRail ×2 + 下方 RedstoneBlock 直供，t704 链传第二格）+
    //   两侧各 4 格普通轨爬升（端格平顶死端）。断言四段：
    //   (a) ② 半山腰放置（东坡中段）→ 初始即往**坡底**方向运动（旧版静止不动）；
    //   (b) ③ 下坡 5 格内达最大速度：首段下滑的视速度（5 tick 窗平滑）≥9.0 且达速点累计水平位移 ≤4.5
    //       格（v²=2ad → kCartSlopeKick 1.0 起步 ~2.5 格到 10；旧版恒速不加速恒到不了）；
    //   (c) ① 底部往复不停驻：1500 tick（24s）内方向反转 ≥6 次（≥3 完整周期；停驻 / 飞出顶 = 反转不足）；
    //   (d) 全程含留在 rig 内（x∈[x0-4.6, x0+5.6]，未飞出顶）+ Y 平滑（|Δy|/tick < 0.35，未坠落）。
    runLegMulti({ "t909 V perpetual rig: cart placed mid-slope rolls downhill immediately (static-start gate yields"
        " on gradients), first descent reaches ~max slope speed (9.0+) within 4.5 blocks (slope gravity a"
        "ccel g*sin45, old code coasted at constant speed), and the two powered rails at the V bottom sus"
        "tain endless oscillation (>=6 direction reversals in 24s, no mid-slope park, no crest launch, st"
        "ays inside the rig)" }, [&]() {
        // rig 选址：运行期扫描空区。需 12×1×4 净空（V 底 2 + 两坡 4+4 + 隔离边；高度 +5）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 6; xx + 5 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -5; dx <= 6 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 6 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t909 V perpetual rig: no clear rig area found";
        } else {
            // V 形：底 (x0,Y) (x0+1,Y) GoldenRail；东坡 (x0+2..x0+5) 逐格 +1（顶格平顶）；西坡镜像。
            w.setBlock(x0, kRigY - 1, z0, BR::RedstoneBlock, 0); // 直供源（兼支撑）
            w.setBlock(x0, kRigY, z0, BR::GoldenRail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::GoldenRail, 0);
            for (int i = 1; i <= 4; ++i) {
                w.setBlock(x0 + 1 + i, kRigY + i, z0, BR::Rail, 0); // 东坡（顶格 x0+5@Y+4 平顶）
                w.setBlock(x0 - i, kRigY + i, z0, BR::Rail, 0);     // 西坡镜像
            }
            tickN(w, 8); // 电力重算：直供第一格 + t704 链传第二格
            const bool poweredOk = (w.stateAt(x0, kRigY, z0) & BR::GoldenRailStateOnFlag) != 0
                                && (w.stateAt(x0 + 1, kRigY, z0) & BR::GoldenRailStateOnFlag) != 0;
            MinecartManager carts;
            carts.spawnCart(x0 + 3, kRigY + 2, z0, &w); // ② 半山腰（东坡中段）
            // 驱动 + 采样：5 tick 窗视速度 / 反向计数 / 含留 / Y 平滑。
            bool downOk = false, inRig = true, ySmooth = true;
            float vFirstFast = -1.0f, distAtFirstFast = -1.0f, vPeak = 0.0f;
            float dist = 0.0f;
            int reversals = 0, state = 0;
            float accum = 0.0f, prevX = carts.posAt(0).x(), prevY = carts.posAt(0).y();
            float xHist[6] = { prevX, prevX, prevX, prevX, prevX, prevX };
            for (int t = 0; t < 1500; ++t) {
                carts.tickPushedCarts(0.016f, &w);
                const QVector3D p = carts.posAt(0);
                const float dX = p.x() - prevX;
                dist += std::fabs(dX);
                // (a) ② 首段下行：起步 60 tick 内朝坡底（-X）累计 ≥0.3 格。
                if (t < 60 && p.x() < float(x0 + 3) + 0.5f - 0.3f) downOk = true;
                // (b) ③ 视速度（5 tick 窗）：**首次**达 9.0 的累计水平位移（全局峰在后续往复的
                //     boost 入坡段出现，不钉首段）。
                xHist[t % 6] = p.x();
                if (t >= 6) {
                    const float vApp = std::fabs(p.x() - xHist[(t + 1) % 6]) / 0.08f;
                    if (vApp > vPeak) vPeak = vApp;
                    if (vFirstFast < 0.0f && vApp >= 9.0f) {
                        vFirstFast = vApp;
                        distAtFirstFast = dist;
                    }
                }
                // (c) ① 反向计数（0.3 格阈值去微抖）。
                accum += dX;
                if (state == 0) {
                    if (accum > 0.3f) { state = 1; accum = 0.0f; }
                    else if (accum < -0.3f) { state = -1; accum = 0.0f; }
                } else if (state > 0 && accum < -0.3f) { ++reversals; state = -1; accum = 0.0f; }
                else if (state < 0 && accum > 0.3f) { ++reversals; state = 1; accum = 0.0f; }
                // (d) 含留 + Y 平滑。
                if (p.x() < float(x0) - 4.6f || p.x() > float(x0) + 5.6f) inRig = false;
                if (std::fabs(p.y() - prevY) > 0.35f) ySmooth = false;
                prevX = p.x(); prevY = p.y();
            }
            const bool okA = downOk;                                   // ② 半山腰下坡起步
            const bool okB = vFirstFast >= 9.0f && distAtFirstFast <= 4.5f; // ③ 5 格内达最大速度
            const bool okC = reversals >= 6;                           // ① ≥3 完整往复周期
            const bool ok = poweredOk && okA && okB && okC && inRig && ySmooth && carts.aliveAt(0);
            if (!ok)
                qInfo().noquote() << "  t909 powered" << poweredOk << "downhill" << okA
                                  << "vFirstFast" << vFirstFast << "distAtFirstFast" << distAtFirstFast
                                  << "vPeak" << vPeak << "reversals" << reversals << "inRig" << inRig
                                  << "ySmooth" << ySmooth << "final" << carts.posAt(0);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t909 V perpetual rig: cart placed mid-slope rolls downhill immediately "
                                 "(static-start gate yields on gradients), first descent reaches ~max slope "
                                 "speed (9.0+) within 4.5 blocks (slope gravity accel g*sin45, old code "
                                 "coasted at constant speed), and the two powered rails at the V bottom "
                                 "sustain endless oscillation (>=6 direction reversals in 24s, no mid-slope "
                                 "park, no crest launch, stays inside the rig)";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 4; ++i) {
                w.setBlock(x0 + 1 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 - i, kRigY + i, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── t910 动力铁轨充能沿坡传播探针（World 直编；spec「上坡的动力铁轨被红石激活应传播到上下坡固定
    //    距离的动力铁轨（现只有平地传远）—— 充能扩散沿轨走向含升降」）──
    //   rig：坡链（种子直供 + 向东逐格 +1 共 6 根）+ 同长度平链对照（同种子位直供）。
    //   断言：(a) 坡链 6 根全部置 GoldenRailStateOnFlag —— 传播距离与平地一致（钉「固定距离沿轨走向含
    //            升降」；旧版链 BFS 钉同 y → 坡链除种子外一根不亮）；
    //         (b) 平链 6 根全亮（t704 平链语义回归）；
    //         (c) 拆源 → 两链全灭（降沿对称沿坡收缩 —— 波前 ±1 层入脏集，熄灭链不被卡在首格）。
    runLegMulti({ "t910 golden-rail power chain follows rail geometry up/down slopes: a redstone-fed powered rail l"
        "ights all 6 rails of a +1-per-cell climbing chain exactly like the same-length flat chain (old B"
        "FS was same-Y only - slope chains stayed dark past the seed), and removing the source extinguish"
        "es both chains symmetrically (falling-edge wavefront also walks the slope layers)" }, [&]() {
        // rig 选址：运行期扫描空区。dx -1..6、dz -1..3、dy -2..+6。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 92 && x0 < 0; zz += 4)
            for (int xx = 4; xx + 5 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 6 && clear; ++dx)
                    for (int dz = -1; dz <= 3 && clear; ++dz)
                        for (int dy = -2; dy <= 6 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t910 golden-rail slope power chain: no clear rig area found";
        } else {
            // 坡链（z0）：种子 (x0,Y) 直供 + 向东逐格 +1 共 6 根。**源放种子侧邻**（非脚下）——
            //   脚下源被拆时种子轨同步失撑掉落（t733 支撑规则），测的是「轨消失」非「降沿熄灭」。
            w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
            for (int i = 0; i <= 5; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0);
            // 平链（z0+2）：镜像同长、种子位同侧邻直供。
            w.setBlock(x0 - 1, kRigY, z0 + 2, BR::RedstoneBlock, 0);
            for (int i = 0; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0 + 2, BR::GoldenRail, 0);
            tickN(w, 10); // 电力重算收敛（链波前逐 tick 外扩）
            const auto onCount = [&](int z, bool slope) {
                int n = 0;
                for (int i = 0; i <= 5; ++i)
                    if ((w.stateAt(x0 + i, kRigY + (slope ? i : 0), z) & BR::GoldenRailStateOnFlag) != 0) ++n;
                return n;
            };
            const int onSlope = onCount(z0, true);
            const int onFlat = onCount(z0 + 2, false);
            // (c) 降沿：拆两源 → 全灭（源在侧邻，拆源只断供不掉轨）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 - 1, kRigY, z0 + 2, BR::Air, 0);
            tickN(w, 10);
            const int offSlope = onCount(z0, true);
            const int offFlat = onCount(z0 + 2, false);
            const bool ok = onSlope == 6 && onFlat == 6 && offSlope == 0 && offFlat == 0;
            if (!ok)
                qInfo().noquote() << "  t910 onSlope" << onSlope << "onFlat" << onFlat
                                  << "offSlope" << offSlope << "offFlat" << offFlat;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t910 golden-rail power chain follows rail geometry up/down slopes: a "
                                 "redstone-fed powered rail lights all 6 rails of a +1-per-cell climbing "
                                 "chain exactly like the same-length flat chain (old BFS was same-Y only - "
                                 "slope chains stayed dark past the seed), and removing the source "
                                 "extinguishes both chains symmetrically (falling-edge wavefront also "
                                 "walks the slope layers)";
            // 清场
            for (int i = 0; i <= 5; ++i) {
                w.setBlock(x0 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 + i, kRigY, z0 + 2, BR::Air, 0);
            }
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 - 1, kRigY, z0 + 2, BR::Air, 0);
            tickN(w, 2);
        }
    });

    // ── t911 铁轨贴仙人掌探针（World 直编）──
    //   **t984 口径翻案**（用户 9-01 原话「我的口径是能放下来，而不是仙人掌会掉落，你之前一直都做错了」）：
    //   旧钉「轨贴仙人掌 → 整柱坍落（铁轨非法邻面）」作废；现钉「轨贴仙人掌 → 放置成功 + 仙人掌不动」
    //   （仙人掌破坏校验只被整立方方块 isFullCube 触发，见 checkCactusOnEdit ④；自动下矿车 t866② 走
    //   Entities 层接触判定，与方块邻接口径解耦不受影响）。
    //   断言三段：
    //   (a) 铁轨贴 2 高仙人掌**上层**格放置 → 放置成功（轨留存）+ 上下两格仙人掌原样 + 零掉落；
    //   (b) 铁轨贴 1 高仙人掌（基座层）→ 同（基线场景钉语义）；
    //   (c) 阴性对照：铁轨距仙人掌 2 格（不放置）→ 仙人掌无恙（保留旧对照腿）。
    runLegMulti({ "t911 rail placement beside a cactus leaves the cactus standing (t984 reversed caliber: a rail ne"
        "xt to a cactus, mid-column or base level, places successfully and the whole column stays intact "
        "with zero drops - only full-cube blocks break cacti; the cactus cart-dropper chain t866 uses ent"
        "ity contact damage and is unaffected), and a rail two cells away leaves the cactus untouched" }, [&]() {
        // rig 选址：运行期扫描空区。dx -1..6、dz -1..1、dy -2..+3。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 6 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 6 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t911 rail-adjacent cactus: no clear rig area found";
        } else {
            // 掉落计数（blockDroppedAsItem 局部连接——句柄断开，不误伤其它探针的连接；只数本 rig 柱格）。
            int dropsAtCol = 0;
            const QMetaObject::Connection dropConn = QObject::connect(
                &w, &World::blockDroppedAsItem, &w,
                [&dropsAtCol, x0, z0](int bx, int, int bz, int bid) {
                    if (bid == int(BR::Cactus) && bz == z0 && bx >= x0 && bx <= x0 + 5)
                        ++dropsAtCol;
                });
            // (a) 2 高仙人掌 @（x0, Y/Y+1）+ 铁轨贴上层格 (x0+1, Y+1)。
            w.setBlock(x0, kRigY - 1, z0, BR::Sand, 0);        // 沙基座（仙人掌合法支撑）
            w.setBlock(x0, kRigY, z0, BR::Cactus, 0);
            w.setBlock(x0, kRigY + 1, z0, BR::Cactus, 0);
            dropsAtCol = 0;
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Rail, 0);    // 铁轨贴仙人掌上层 → 放置成功、仙人掌不动
            const bool okA = w.blockAt(x0 + 1, kRigY + 1, z0) == BR::Rail
                          && w.blockAt(x0, kRigY, z0) == BR::Cactus
                          && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                          && dropsAtCol == 0;                  // 零掉落
            // (b) 1 高仙人掌 @（x0+2, Y）+ 铁轨贴基座层 (x0+3, Y)。
            w.setBlock(x0 + 2, kRigY - 1, z0, BR::Sand, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Cactus, 0);
            dropsAtCol = 0;
            w.setBlock(x0 + 3, kRigY, z0, BR::Rail, 0);
            const bool okB = w.blockAt(x0 + 3, kRigY, z0) == BR::Rail
                          && w.blockAt(x0 + 2, kRigY, z0) == BR::Cactus
                          && dropsAtCol == 0;
            // (c) 阴性对照：仙人掌 @（x0+5, Y），与 (b) 留下的铁轨 (x0+3) 相距 2 格（x0+4 空）→ 仙人掌留存。
            w.setBlock(x0 + 5, kRigY - 1, z0, BR::Sand, 0);
            w.setBlock(x0 + 5, kRigY, z0, BR::Cactus, 0);
            dropsAtCol = 0;
            const bool okC = w.blockAt(x0 + 5, kRigY, z0) == BR::Cactus
                          && dropsAtCol == 0;
            const bool ok = okA && okB && okC;
            if (!ok)
                qInfo().noquote() << "  t911 upperRail" << okA << "baseRail" << okB
                                  << "farControl" << okC
                                  << "a-cell" << int(w.blockAt(x0, kRigY, z0))
                                  << int(w.blockAt(x0, kRigY + 1, z0));
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t911 rail placement beside a cactus leaves the cactus standing"
                                 " (t984 reversed caliber: a rail next to a cactus, mid-column or"
                                 " base level, places successfully and the whole column stays intact"
                                 " with zero drops - only full-cube blocks break cacti; the cactus"
                                 " cart-dropper chain t866 uses entity contact damage and is"
                                 " unaffected), and a rail two cells away leaves the cactus untouched";
            // 清场（t984 口径仙人掌不再坍落 → 各腿仙人掌 / 铁轨 / 沙基座全部显式清空）
            QObject::disconnect(dropConn);
            for (int dx = 0; dx <= 5; ++dx) {
                for (int dy = 0; dy <= 1; ++dy)
                    w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    });

    // ── t912 发射器发矿车探针（Game 层 PlayerController + DispenserStore + MinecartManager +
    //    ItemEntityManager 直编，t856/t868 模式；spec「发射矿车物品 → 在发射口前邻格放置矿车实体；
    //    邻格是铁轨则对齐轨向」）──
    //   断言三段（每段间 pc.scanDispenserTraps(2.5f) 耗冷却 + 拉杆降/升沿造新触发）：
    //   (a) 轨上模式：发射面邻格是铁轨（双格线 → +X 连接定向）→ 邻格生成矿车实体（轨面高 kCartRideH
    //       镜像 0.45）+ yaw 轴向取轨向（+X 行进 → yaw 270，t708② 连接位定向链）+ 库存 2→1；
    //   (b) 地面模式：拆轨后邻格净空 → 矿车实体贴 cell 底（kCartGroundH 镜像 0.3875，t734 放宽放置）；
    //   (c) 堵口降级：邻格实体方块堵住 → 不放实体（车数不增）+ 掉落物实体 +1（MinecartId 物品形态，
    //       review26 #24 堵口门复用）+ 库存照扣。
    runLegMulti({ "t912 dispenser minecart: dispensing a minecart places a cart entity in the spout-adjacent cell ("
        "rail cell -> on-rail mode with yaw aligned to the rail axis via the connection-direction chain, "
        "open ground -> ground-parked mode at kCartGroundH), a blocked spout degrades to a dropped mineca"
        "rt item (review26-24 gate reused) and stock decrements in all cases" }, [&]() {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        ItemEntityManager items;
        MinecartManager carts;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        pc.setItemEntities(&items);
        pc.setMinecartManager(&carts);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto cartItemCount = [&items]() {
            int n = 0;
            for (int i = 0; i < items.count(); ++i)
                if (items.aliveAt(i) && items.itemIdAt(i) == RecipeRegistry::MinecartId) ++n;
            return n;
        };
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0); // state 0 → 朝 +X
        w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);          // 凿空发射面（新 rig 行地形可达 y41）
        w.setBlock(x0 + 2, kRigY, z0, BR::Air, 0);
        store.ensureDispenser(x0, kRigY, z0);
        store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::MinecartId, 3);
        tickN(w, 2);
        const auto reEdge = [&]() { // 造一个真上升沿：先拆源（清 fireDispenserAtQml 沿基线）再置源
            pc.scanDispenserTraps(2.5f); // 耗尽冷却（等价递减驱动，t856(b) 同款）
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            tickN(w, 4);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
        };
        // (a) 轨上模式：发射面邻格 (x0+1) 铺双格线（+X 连接 → spawn 定向 +X）。
        w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
        w.setBlock(x0 + 2, kRigY, z0, BR::Rail, 0);
        reEdge();
        bool okA = carts.liveCount() == 1
            && std::fabs(carts.posAt(0).x() - (x0 + 1.5f)) < 1e-3f
            && std::fabs(carts.posAt(0).y() - (kRigY + 0.45f)) < 1e-3f   // 轨面高（kCartRideH 镜像）
            && std::fabs(carts.posAt(0).z() - (z0 + 0.5f)) < 1e-3f
            && std::fabs(carts.yawAt(0) - 270.0f) < 1.0f                 // 轴向取轨向（+X → yaw 270）
            && store.slotCountAt(x0, kRigY, z0, 0) == 2;                 // 库存 3→2
        if (!okA)
            qInfo().noquote() << "  t912(a) rail cart" << carts.liveCount() << carts.posAt(0)
                              << "yaw" << carts.yawAt(0)
                              << "stock" << store.slotCountAt(x0, kRigY, z0, 0);
        // 清 (a)：毁车（创造瞬破免掉落）+ 拆轨。
        carts.hitCartFromRay(carts.posAt(0) + QVector3D(0, 3.0f, 0), QVector3D(0, -1, 0), 4.0f, &w, true);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0 + 2, kRigY, z0, BR::Air, 0);
        tickN(w, 2);
        // (b) 地面模式：邻格净空（非轨）→ 贴 cell 底静止车（t734 放宽放置）。
        reEdge();
        const bool okB = carts.liveCount() == 1
            && std::fabs(carts.posAt(0).x() - (x0 + 1.5f)) < 1e-3f
            && std::fabs(carts.posAt(0).y() - (kRigY + 0.3875f)) < 1e-3f // 地面高（kCartGroundH 镜像）
            && store.slotCountAt(x0, kRigY, z0, 0) == 1;                 // 库存 2→1
        if (!okB)
            qInfo().noquote() << "  t912(b) ground cart" << carts.liveCount() << carts.posAt(0)
                              << "stock" << store.slotCountAt(x0, kRigY, z0, 0);
        // 清 (b)：毁车。
        carts.hitCartFromRay(carts.posAt(0) + QVector3D(0, 3.0f, 0), QVector3D(0, -1, 0), 4.0f, &w, true);
        tickN(w, 2);
        // (c) 堵口降级：发射面邻格实体方块 → 不放实体 + 掉落物 +1（MinecartId 物品形态）+ 库存照扣。
        placeRigBlock(w, x0 + 1, kRigY, z0, BR::Stone, 0);
        const int itemsBefore = cartItemCount();
        reEdge();
        const bool okC = carts.liveCount() == 0
            && cartItemCount() == itemsBefore + 1
            && store.slotCountAt(x0, kRigY, z0, 0) == 0;                 // 库存 1→0（最后一发）
        if (!okC)
            qInfo().noquote() << "  t912(c) blocked carts" << carts.liveCount()
                              << "items" << cartItemCount() << "was" << itemsBefore
                              << "stock" << store.slotCountAt(x0, kRigY, z0, 0);
        const bool ok = okA && okB && okC;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t912 dispenser minecart: dispensing a minecart places a cart entity in "
                             "the spout-adjacent cell (rail cell -> on-rail mode with yaw aligned to the "
                             "rail axis via the connection-direction chain, open ground -> ground-parked "
                             "mode at kCartGroundH), a blocked spout degrades to a dropped minecart item "
                             "(review26-24 gate reused) and stock decrements in all cases";
        // 清场
        carts.clearAll();
        w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0, kRigY, z0, BR::Air, 0);
        store.clearDispenser(x0, kRigY, z0);
        items.clearAll();
        tickN(w, 2);
    });
}
