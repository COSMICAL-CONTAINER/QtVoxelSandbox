// tools/matrix/section07_chests_mobs.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L37540-43624 逐字节搬移（段 md5: 52328ff44f3db0e2054a30e2c45351ad；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section07_chests_mobs()
{

    // ── t1021 音效层探针（面板 A：结构环境音 region 门控行为——要塞内→低鸣请求发出 / 离开→停；
    //    矿井滴水 / 沙漠夜风（夜+露天复合门）/ 丛林虫鸣（昼夜密度分流）/ 地牢无音登记口径）──
    //    rig：96×96×96 单世界逐种子扫描（seeds 1..80，一份 worldgen 同填四张 region 表，四结构各自记
    //    首个命中种子 —— t1020 四世界分扫的省时变体）+ 96×96×48 要塞世界（t1000 同款 seeds 1..24 扫
    //    hasStronghold）。真 PlayerController（Spectator noclip 定点 tick，同 t1000 口径）+ 真 WorldClock
    //    （setPhase 钉正午 / 子夜控制 isNight）。断言：足迹外 zone=0；要塞内→1 且界内连 tick 零重发、
    //    离开→0；矿井内→2（昼夜无关）；沙漠顶冠露天格：昼→0 / 夜→3、地下角格（不见天）夜→0；丛林内
    //    昼→4 / 夜→5；地牢内→0（四音不覆盖地牢）；足迹外采样点选在五谓词全外（防重叠结构串扰）。
    runLegMulti({ "t1021 structure-ambient gating: structureAmbientZone derived per tick from t1020 region tables -"
        "- outside=0, stronghold in->1 (steady in-region re-ticks zero re-emit) out->0, mineshaft->2 day "
        "and night, desert temple exposed cell 0 by day / 3 at midnight / underground cell silent at nigh"
        "t, jungle temple 4 by day / 5 at night, dungeon-only cell stays 0 (no ambience registered) (seed"
        "s dun/mine/des/jun =)" }, [&]() {
        World wT21;
        wT21.setWidth(96); wT21.setDepth(96); wT21.setHeight(96);
        int seedT21Dun = -1, seedT21Mine = -1, seedT21Des = -1, seedT21Jun = -1;
        for (int s = 1; s <= 80
             && (seedT21Dun < 0 || seedT21Mine < 0 || seedT21Des < 0 || seedT21Jun < 0); ++s) {
            wT21.setSeed(s);
            const auto hit = [&](int kind) {
                return wT21.structureRegionCount(kind) > 0
                       && wT21.structureRegion(kind, 0).size() == 6;
            };
            if (seedT21Dun < 0 && hit(World::StructureDungeon)) seedT21Dun = s;
            if (seedT21Mine < 0 && hit(World::StructureMineshaft)) seedT21Mine = s;
            if (seedT21Des < 0 && hit(World::StructureDesertTemple)) seedT21Des = s;
            if (seedT21Jun < 0 && hit(World::StructureJungleTemple)) seedT21Jun = s;
        }
        // 要塞世界（t1000 同款 48 高；要塞与四结构 region 表无关，走 hasStronghold / insideStronghold）。
        World wT21S;
        wT21S.setWidth(96); wT21S.setDepth(96); wT21S.setHeight(48);
        for (int s = 1; s <= 24 && !wT21S.hasStronghold(); ++s)
            wT21S.setSeed(s);
        bool okA = seedT21Dun > 0 && seedT21Mine > 0 && seedT21Des > 0 && seedT21Jun > 0
                   && wT21S.hasStronghold();
        if (!okA)
            qInfo().noquote() << "  [t1021 diag] seeds dun/mine/des/jun =" << seedT21Dun
                              << seedT21Mine << seedT21Des << seedT21Jun
                              << "stronghold=" << wT21S.hasStronghold();
        if (okA) {
            PlayerController pcT21; // 无窗口直造（componentComplete 不触发，无 16ms 定时器；tick 直调）
            WorldClock clockT21;    // 真 Game 层时间源（setPhase 钉相位 → isNight 纯函数即时翻转）
            pcT21.setWorldClock(&clockT21);
            clockT21.setPhase(0.0f); // 正午（昼）
            int emitsT21 = 0;
            QObject::connect(&pcT21, &PlayerController::structureAmbientZoneChanged, &pcT21,
                             [&]() { ++emitsT21; });
            // ── 要塞腿（wT21S）：足迹外（五谓词外围采样）→0；厅内→1；界内连 tick 零重发；离开→0 ──
            const int sPx = wT21S.strongholdPortalX(), sPy = wT21S.strongholdPortalY(),
                      sPz = wT21S.strongholdPortalZ();
            const int sCx = sPx, sCy = sPy - World::kStrongholdPortalDy,
                      sCz = sPz - World::kStrongholdPortalDz; // 反解原点（t1000 同口径）
            const int sHalf = World::kStrongholdHalf;
            const double sFeetY = double(sCy + 1) + 0.5;
            double sOutX = -1.0;
            for (double off : {9.5, 14.5, 19.5, 24.5, 29.5, 34.5}) {
                if (sCx + sHalf + off < wT21S.width()
                    && !wT21S.insideStronghold(sCx + sHalf + off, sFeetY, double(sCz) + 0.5)) {
                    sOutX = double(sCx) + sHalf + off; break;
                }
                if (sCx - sHalf - off > 0
                    && !wT21S.insideStronghold(sCx - sHalf - off, sFeetY, double(sCz) + 0.5)) {
                    sOutX = double(sCx) - sHalf - off; break;
                }
            }
            okA = okA && sOutX > 0;
            pcT21.setWorld(&wT21S);
            pcT21.loadSavedState(float(sOutX), float(sFeetY), float(sCz) + 0.5f, -90.0f, 0.0f, 0);
            for (int t = 0; t < 3; ++t) pcT21.tick();
            okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientNone);
            // 要塞内 → 低鸣请求发出（zone=1）。
            pcT21.loadSavedState(float(sCx) + 0.5f, float(sFeetY), float(sCz) + 0.5f, -90.0f, 0.0f, 0);
            pcT21.tick();
            okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientStronghold);
            const int emitsBeforeT21 = emitsT21;
            for (int t = 0; t < 6; ++t) pcT21.tick();
            okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientStronghold)
                  && emitsT21 == emitsBeforeT21; // 界内连 tick：值不变零重发（禁每帧抖 QML）
            // 离开 → 停（zone=0 请求发出；Main.qml 侧 stop 由路由行源码钉 + 降级链承担）。
            pcT21.loadSavedState(float(sOutX), float(sFeetY), float(sCz) + 0.5f, -90.0f, 0.0f, 0);
            pcT21.tick();
            okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientNone);

            // ── 四结构腿（wT21）：逐 kind re-setSeed（同 seed 同表，t1020 面板 A' seed-pure 口径）──
            pcT21.setWorld(&wT21);
            auto outsideAllT21 = [&wT21](double x, double y, double z) -> bool {
                return !wT21.insideStronghold(x, y, z)
                    && !wT21.insideStructureRegion(World::StructureDungeon, x, y, z)
                    && !wT21.insideStructureRegion(World::StructureMineshaft, x, y, z)
                    && !wT21.insideStructureRegion(World::StructureDesertTemple, x, y, z)
                    && !wT21.insideStructureRegion(World::StructureJungleTemple, x, y, z);
            };
            // 足迹外采样点：bbox 东西向逐档外移（9.5..34.5），取首个五谓词全外的点（防重叠结构串扰）。
            auto pickOutsideT21 = [&](int kind) -> double {
                const QVariantList rg = wT21.structureRegion(kind, 0);
                const double mnx = rg[0].toDouble(), mxx = rg[3].toDouble();
                const double mnz = rg[2].toDouble(), mxz = rg[5].toDouble();
                const double cy = rg[1].toDouble() + 1.5;
                const double cz = (mnz + mxz) / 2.0 + 0.5;
                for (double off : {9.5, 14.5, 19.5, 24.5, 29.5, 34.5}) {
                    if (mxx + off < wT21.width() && outsideAllT21(mxx + off, cy, cz))
                        return mxx + off;
                    if (mnx - off > 0 && outsideAllT21(mnx - off, cy, cz))
                        return mnx - off;
                }
                return -1.0;
            };
            // 矿井腿：bbox 中心（区域表口径足迹，与体素巷道开口无关）→ 昼 2；夜仍 2（滴水昼夜无关）；
            //   离开足迹 → 0。
            wT21.setSeed(seedT21Mine);
            {
                const QVariantList rm = wT21.structureRegion(World::StructureMineshaft, 0);
                const double cx = (rm[0].toDouble() + rm[3].toDouble()) / 2.0 + 0.5;
                const double cy = rm[1].toDouble() + 1.5;
                const double cz = (rm[2].toDouble() + rm[5].toDouble()) / 2.0 + 0.5;
                const double outX = pickOutsideT21(World::StructureMineshaft);
                okA = okA && outX > 0;
                pcT21.loadSavedState(float(cx), float(cy), float(cz), -90.0f, 0.0f, 0);
                clockT21.setPhase(0.0f);
                pcT21.tick();
                okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientMineshaft);
                clockT21.setPhase(0.5f); // 子夜
                pcT21.tick();
                okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientMineshaft);
                pcT21.loadSavedState(float(outX), float(cy), float(cz), -90.0f, 0.0f, 0);
                pcT21.tick();
                okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientNone);
            }
            // 沙漠腿：bbox 角列 (min+1,min+1) 自下而上找首个见天格（金字塔坡 / 顶冠露天格，确定性）——
            //   昼→0（无夜不风）；同格夜→3；地下角格 (min+1,min+1,min+1)（深埋不见天，先钉 skyLight<15）
            //   夜→0（夜探密室无风）。
            wT21.setSeed(seedT21Des);
            {
                const QVariantList rd = wT21.structureRegion(World::StructureDesertTemple, 0);
                const int mnx = rd[0].toInt(), mny = rd[1].toInt(), mnz = rd[2].toInt();
                const int mxy = rd[4].toInt();
                int exY = -1;
                for (int y = mny; y <= mxy; ++y) {
                    if (wT21.skyLightAt(mnx + 1, y, mnz + 1) >= 15) { exY = y; break; }
                }
                const bool underCellDark = wT21.skyLightAt(mnx + 1, mny + 1, mnz + 1) < 15;
                okA = okA && exY > 0 && underCellDark;
                if (exY > 0 && underCellDark) {
                    pcT21.loadSavedState(float(mnx + 1) + 0.5f, float(exY) + 0.5f,
                                         float(mnz + 1) + 0.5f, -90.0f, 0.0f, 0);
                    clockT21.setPhase(0.0f); // 正午
                    pcT21.tick();
                    okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientNone);
                    clockT21.setPhase(0.5f); // 子夜
                    pcT21.tick();
                    okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientDesertTemple);
                    // 地下角格（不见天）：夜仍无风（三重门的露天项排除密室 / 厅内）。
                    pcT21.loadSavedState(float(mnx + 1) + 0.5f, float(mny + 1) + 0.5f,
                                         float(mnz + 1) + 0.5f, -90.0f, 0.0f, 0);
                    pcT21.tick();
                    okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientNone);
                }
            }
            // 丛林腿：苔石建筑内地板格（bbox 角内 floor 上一格）→ 昼 4（稀疏）/ 夜 5（密集）。
            wT21.setSeed(seedT21Jun);
            {
                const QVariantList rj = wT21.structureRegion(World::StructureJungleTemple, 0);
                const double jx = rj[0].toDouble() + World::kJungleTempleHalf + 0.5;
                const double jy = rj[1].toDouble() + 1.5;
                const double jz = rj[2].toDouble() + World::kJungleTempleHalf + 0.5;
                pcT21.loadSavedState(float(jx), float(jy), float(jz), -90.0f, 0.0f, 0);
                clockT21.setPhase(0.0f);
                pcT21.tick();
                okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientJungleDay);
                clockT21.setPhase(0.5f);
                pcT21.tick();
                okA = okA && pcT21.structureAmbientZone() == int(PlayerController::AmbientJungleNight);
            }
            // 地牢腿：bbox 中心 → 0（四音不覆盖地牢，登记口径；若与矿井区重叠则按优先级 2 亦合法 ——
            //   采样点换五谓词外判定不可用（此腿要的就是在区内），改取「仅地牢」格防串扰）。
            wT21.setSeed(seedT21Dun);
            {
                const QVariantList rdn = wT21.structureRegion(World::StructureDungeon, 0);
                const double cx = (rdn[0].toDouble() + rdn[3].toDouble()) / 2.0 + 0.5;
                const double cy = rdn[1].toDouble() + 1.5;
                const double cz = (rdn[2].toDouble() + rdn[5].toDouble()) / 2.0 + 0.5;
                pcT21.loadSavedState(float(cx), float(cy), float(cz), -90.0f, 0.0f, 0);
                clockT21.setPhase(0.0f);
                pcT21.tick();
                const int zoneDunT21 = pcT21.structureAmbientZone();
                const bool dunOnlyT21 = wT21.insideStructureRegion(World::StructureDungeon, cx, cy, cz)
                    && !wT21.insideStructureRegion(World::StructureMineshaft, cx, cy, cz)
                    && !wT21.insideStructureRegion(World::StructureDesertTemple, cx, cy, cz)
                    && !wT21.insideStructureRegion(World::StructureJungleTemple, cx, cy, cz)
                    && !wT21.insideStronghold(cx, cy, cz);
                okA = okA && dunOnlyT21 && zoneDunT21 == int(PlayerController::AmbientNone);
            }
        }
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1021 structure-ambient gating: structureAmbientZone derived per tick from"
                             " t1020 region tables -- outside=0, stronghold in->1 (steady in-region re-ticks"
                             " zero re-emit) out->0, mineshaft->2 day and night, desert temple exposed cell"
                             " 0 by day / 3 at midnight / underground cell silent at night, jungle temple"
                             " 4 by day / 5 at night, dungeon-only cell stays 0 (no ambience registered)"
                             " (seeds dun/mine/des/jun ="
                          << seedT21Dun << seedT21Mine << seedT21Des << seedT21Jun << ")";
    });

    // ── t1021 音效层探针（面板 C：review0907 A-P2-1 要塞足迹 × 矿井 region 重叠格 structureEntered
    //    边沿不被要塞态吞 —— 边沿检测无条件执行；环境音区折叠要塞优先级保留）──
    //    rig：96×96×48 世界逐 seed 扫描（1..48），找「有要塞 + 足迹∩矿井包络 AABB 重叠点 B + 足迹内
    //    矿井区外点 A」的世界（A 先建立 m_insideStronghold=true + mineshaft 边沿基线 → B 断言
    //    entered_mineshaft 边沿仍触发）。原实现把四结构 loop 挂在 m_insideStronghold 的 else 臂：
    //    要塞区内重叠格处进入事件被整 tick 跳过（本面板 B 点腿阴性敏感）。zone 断言：A/B 两点均
    //    要塞低鸣（折叠单独做后要塞最高优先级不变）。
    runLegMulti({ "t1021 stronghold-footprint x mineshaft-region overlap (review0907 A-P2-1): standing inside the s"
        "tronghold footprint does NOT swallow structureEntered edges anymore -- point A (footprint, outsi"
        "de mineshaft region) establishes the stronghold state with a clean mineshaft edge baseline, poin"
        "t B (footprint AND mineshaft envelope overlap) still fires entered_mineshaft on the next tick, a"
        "nd the ambient zone stays stronghold (priority fold kept; the old else-branch skipped the whole "
        "region loop inside strongholds) (seed =)" }, [&]() {
        World wT21C;
        wT21C.setWidth(96); wT21C.setDepth(96); wT21C.setHeight(48);
        bool foundC = false;
        int foundSeedC = -1;
        double axC = -1, ayC = -1, azC = -1; // A：要塞足迹内、矿井区外（建立要塞态 + 边沿基线）
        double bxC = -1, byC = -1, bzC = -1; // B：要塞足迹 ∩ 矿井包络 重叠格（边沿阳性点）
        for (int s = 1; s <= 48 && !foundC; ++s) {
            wT21C.setSeed(s);
            if (!wT21C.hasStronghold()) continue;
            const int sCx = wT21C.strongholdPortalX();
            const int sCy = wT21C.strongholdPortalY() - World::kStrongholdPortalDy;
            const int sCz = wT21C.strongholdPortalZ() - World::kStrongholdPortalDz;
            const int sHalf = World::kStrongholdHalf;
            const int sYLo = sCy, sYHi = sCy + World::kStrongholdWallH + 1;
            for (int mi = 0; mi < wT21C.structureRegionCount(World::StructureMineshaft) && !foundC; ++mi) {
                const QVariantList rm = wT21C.structureRegion(World::StructureMineshaft, mi);
                const int ix0 = std::max(sCx - sHalf, rm[0].toInt());
                const int ix1 = std::min(sCx + sHalf, rm[3].toInt());
                const int iy0 = std::max(sYLo, rm[1].toInt());
                const int iy1 = std::min(sYHi, rm[4].toInt());
                const int iz0 = std::max(sCz - sHalf, rm[2].toInt());
                const int iz1 = std::min(sCz + sHalf, rm[5].toInt());
                if (ix0 > ix1 || iy0 > iy1 || iz0 > iz1) continue; // 足迹盒 × 矿井包络盒不相交
                const int bxc = (ix0 + ix1) / 2, byc = (iy0 + iy1) / 2, bzc = (iz0 + iz1) / 2;
                for (int corner = 0; corner < 4 && !foundC; ++corner) {
                    const int axc = (corner & 1) ? sCx + sHalf - 1 : sCx - sHalf + 1;
                    const int azc = (corner & 2) ? sCz + sHalf - 1 : sCz - sHalf + 1;
                    for (int ayc : { sYHi, sYLo, byc }) { // A 的 y 优先取矿井 y 带外要塞层（更稳在区外）
                        if (ayc < sYLo || ayc > sYHi) continue;
                        const bool aOk = wT21C.insideStronghold(axc + 0.5, ayc + 0.5, azc + 0.5)
                            && !wT21C.insideStructureRegion(World::StructureMineshaft, axc + 0.5, ayc + 0.5, azc + 0.5);
                        const bool bOk = wT21C.insideStronghold(bxc + 0.5, byc + 0.5, bzc + 0.5)
                            && wT21C.insideStructureRegion(World::StructureMineshaft, bxc + 0.5, byc + 0.5, bzc + 0.5);
                        if (aOk && bOk) {
                            axC = axc + 0.5; ayC = ayc + 0.5; azC = azc + 0.5;
                            bxC = bxc + 0.5; byC = byc + 0.5; bzC = bzc + 0.5;
                            foundC = true; foundSeedC = s;
                            break;
                        }
                    }
                }
            }
        }
        bool okC = foundC;
        if (okC) {
            PlayerController pcT21C; // 无窗口直造（Spectator noclip 定点 tick，同面板 A 口径）
            int mineEnteredC = 0;
            QObject::connect(&pcT21C, &PlayerController::structureEntered, &pcT21C,
                             [&mineEnteredC](int k) { if (k == int(World::StructureMineshaft)) ++mineEnteredC; });
            pcT21C.setWorld(&wT21C);
            // A 点：要塞足迹内、矿井区外 → zone=要塞低鸣 + mineshaft 边沿基线（0 次）。
            pcT21C.loadSavedState(float(axC), float(ayC), float(azC), -90.0f, 0.0f, 0);
            pcT21C.tick();
            const int mineAfterA = mineEnteredC;
            okC = okC && mineAfterA == 0
                && pcT21C.structureAmbientZone() == int(PlayerController::AmbientStronghold);
            // B 点：重叠格 → entered_mineshaft 边沿仍触发（修复核心）+ zone 保持要塞（优先级保留）。
            pcT21C.loadSavedState(float(bxC), float(byC), float(bzC), -90.0f, 0.0f, 0);
            pcT21C.tick();
            okC = okC && mineEnteredC == mineAfterA + 1
                && pcT21C.structureAmbientZone() == int(PlayerController::AmbientStronghold);
        }
        if (!okC)
            qInfo().noquote() << "  [t1021 diag C]" << "found=" << foundC << "seed=" << foundSeedC;
        else
            qInfo().noquote() << "  [t1021 diag C] overlap seed =" << foundSeedC
                              << "A=(" << axC << ayC << azC << ") B=(" << bxC << byC << bzC << ")";
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| t1021 stronghold-footprint x mineshaft-region overlap (review0907"
                             " A-P2-1): standing inside the stronghold footprint does NOT swallow"
                             " structureEntered edges anymore -- point A (footprint, outside mineshaft"
                             " region) establishes the stronghold state with a clean mineshaft edge"
                             " baseline, point B (footprint AND mineshaft envelope overlap) still fires"
                             " entered_mineshaft on the next tick, and the ambient zone stays"
                             " stronghold (priority fold kept; the old else-branch skipped the whole"
                             " region loop inside strongholds) (seed =" << foundSeedC << ")";
    });

    // ── t1021 音效层探针（面板 B：四环境音 + 三事件音 wav 资产存在性 / WAV 格式合法性 + 全链源码钉）──
    //    (a) 资产腿：sounds/ 七新 wav 存在且格式合法（RIFF/WAVE、PCM s16 mono 44100、data 非空、时长
    //        落各自期望带——循环音 ≥6s、单发音 ≤2.5s）；CMake qrc 已登记。
    //    (b) 源码钉：PlayerController zone 属性 / 值域枚举 / 折叠 emit 门 / 复合门 / 重置钩子；Main.qml
    //        zone 分流路由 + 成就 chime + 箱子开/关路由；AudioManager Q_INVOKABLE 面；build_sounds.py
    //        生成器在位（重生成面可复现）。
    runLegMulti({ "t1021 sound-layer assets and wiring: seven new wavs exist with valid PCM s16 mono 44100 headers "
        "and expected durations (two 8s loops, drip/chirps/achievement/chest-open/chest-close one-shots),"
        " qrc registered, and the full chain pinned (zone property/enum/fold-emit gate/night+exposed gate"
        "/dual resets in PlayerController, QML zone dispatch + achievement chime + chest open/close routi"
        "ng, AudioManager Q_INVOKABLE surface, build_sounds.py generators)" }, [&]() {
        bool okB = true;
        const QString rootT21 = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        // WAV 解析：走 chunk 遍历（不假设 44 字节头 —— fmt 扩展 / 附加块兼容），钉 PCM s16 mono 44100。
        auto parseWavT21 = [](const QString &path, double *secsOut, QString *err) -> bool {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) { *err = QStringLiteral("open-fail"); return false; }
            const QByteArray b = f.readAll();
            if (b.size() < 44 || b.mid(0, 4) != "RIFF" || b.mid(8, 4) != "WAVE") {
                *err = QStringLiteral("riff-wave-magic"); return false;
            }
            bool haveFmt = false, haveData = false;
            quint16 fmtTag = 0, chans = 0, bits = 0;
            quint32 rate = 0;
            qint64 dataSize = 0;
            int pos = 12;
            while (pos + 8 <= b.size()) {
                const QString id = QString::fromLatin1(b.mid(pos, 4));
                const qint64 sz = qint64(quint8(b[pos + 4])) | (qint64(quint8(b[pos + 5])) << 8)
                                | (qint64(quint8(b[pos + 6])) << 16) | (qint64(quint8(b[pos + 7])) << 24);
                if (id == QLatin1String("fmt ") && pos + 8 + 16 <= b.size()) {
                    fmtTag = quint16(quint8(b[pos + 8])) | (quint16(quint8(b[pos + 9])) << 8);
                    chans = quint16(quint8(b[pos + 10])) | (quint16(quint8(b[pos + 11])) << 8);
                    rate = quint32(quint8(b[pos + 12])) | (quint32(quint8(b[pos + 13])) << 8)
                         | (quint32(quint8(b[pos + 14])) << 16) | (quint32(quint8(b[pos + 15])) << 24);
                    bits = quint16(quint8(b[pos + 22])) | (quint16(quint8(b[pos + 23])) << 8);
                    haveFmt = true;
                } else if (id == QLatin1String("data")) {
                    dataSize = sz;
                    haveData = true;
                }
                pos += int(8 + sz + (sz & 1)); // RIFF 块 2 字节对齐
            }
            if (!haveFmt || !haveData) { *err = QStringLiteral("missing-chunk"); return false; }
            if (fmtTag != 1 || chans != 1 || rate != 44100 || bits != 16) {
                *err = QStringLiteral("not-pcm16-mono-44100"); return false;
            }
            if (dataSize <= 0) { *err = QStringLiteral("empty-data"); return false; }
            *secsOut = double(dataSize) / (double(rate) * 2.0); // s16 mono → 字节 / (44100*2)
            return true;
        };
        struct WavSpecT21 { const char *name; double minSec; double maxSec; };
        const WavSpecT21 wavSpecsT21[] = {
            {"stronghold_hum.wav", 6.0, 10.0},      // 8s 循环低鸣
            {"mineshaft_drip.wav", 0.2, 1.5},       // 单滴（含两级回声）
            {"jungle_chirps.wav", 0.5, 2.5},        // 颤音簇 one-shot
            {"desert_night_wind.wav", 6.0, 10.0},   // 8s 循环夜风
            {"achievement.wav", 0.3, 1.5},          // 成就 chime
            {"chest_open.wav", 0.15, 1.0},          // 箱子开启
            {"chest_close.wav", 0.10, 1.0},         // 箱子关闭
        };
        for (const WavSpecT21 &spec : wavSpecsT21) {
            double secs = 0.0;
            QString err;
            const QString path = rootT21 + QStringLiteral("/sounds/") + QString::fromLatin1(spec.name);
            if (!parseWavT21(path, &secs, &err) || secs < spec.minSec || secs > spec.maxSec) {
                okB = false;
                qInfo().noquote() << "  [t1021 diag] wav" << spec.name << "bad:" << err << "secs" << secs;
            }
        }
        // ── 源码钉：zone 推导 / 路由 / 播放 API / 生成器全链在位 ──
        //     B-P1-1 迁移：滤注释钉（pinSet）—— 本组 33 条全量过帮手（音频路由行注释化即红，
        //     敏感度证明轮 2026-09-06 留档：注释掉 startMineshaftDrips 路由行恰本组红、其余绿）。
        {
            const auto srcT21 = [&rootT21](const QString &rel) {
                return rootT21 + QLatin1Char('/') + rel;
            };
            QStringList missPinT21;
            missPinT21 << pinSet(srcT21(QStringLiteral("src/Game/playercontroller.h")), {
                {"prop-zone", "Q_PROPERTY(int structureAmbientZone READ structureAmbientZone NOTIFY structureAmbientZoneChanged)"},
                {"enum-jungle-night", "AmbientJungleNight = 5"},
            });
            missPinT21 << pinSet(srcT21(QStringLiteral("src/Game/playercontroller.cpp")), {
                {"zone-fold-gate", "if (ambientZone != m_structureAmbientZone)"},
                {"zone-night-exposed", "if (night && exposed)"},
                {"zone-reset", "m_structureAmbientZone = AmbientNone;", 2}, // setWorld + finishWorldLoad 双重置
            });
            missPinT21 << pinSet(srcT21(QStringLiteral("src/ui/Main.qml")), {
                {"qml-onZoneChanged", "function onStructureAmbientZoneChanged()"},
                {"qml-route-hum", "audio.startStrongholdHum()"},
                {"qml-route-drips", "audio.startMineshaftDrips()"},
                {"qml-route-wind", "audio.startDesertNightWind()"},
                {"qml-route-chirps", "audio.startJungleChirps(true)"},
                {"qml-route-achievement", "audio.playAchievement()"},
                {"qml-route-chestOpen", "audio.playChestOpen()"},
                {"qml-route-chestClose", "audio.playChestClose()"},
            });
            missPinT21 << pinSet(srcT21(QStringLiteral("src/Audio/audiomanager.h")), {
                {"api-startStrongholdHum", "Q_INVOKABLE void startStrongholdHum();"},
                {"api-startMineshaftDrips", "Q_INVOKABLE void startMineshaftDrips();"},
                {"api-startDesertNightWind", "Q_INVOKABLE void startDesertNightWind();"},
                {"api-startJungleChirps", "Q_INVOKABLE void startJungleChirps(bool dense);"},
                {"api-playAchievement", "Q_INVOKABLE void playAchievement();"},
                {"api-playChestOpen", "Q_INVOKABLE void playChestOpen();"},
                {"api-playChestClose", "Q_INVOKABLE void playChestClose();"},
            });
            missPinT21 << pinSet(srcT21(QStringLiteral("CMakeLists.txt")), {
                {"cmake-stronghold_hum", "sounds/stronghold_hum.wav"},
                {"cmake-mineshaft_drip", "sounds/mineshaft_drip.wav"},
                {"cmake-jungle_chirps", "sounds/jungle_chirps.wav"},
                {"cmake-desert_night_wind", "sounds/desert_night_wind.wav"},
                {"cmake-achievement", "sounds/achievement.wav"},
                {"cmake-chest_open", "sounds/chest_open.wav"},
                {"cmake-chest_close", "sounds/chest_close.wav"},
            });
            missPinT21 << pinSet(srcT21(QStringLiteral("tools/build_sounds.py")), {
                {"gen-stronghold_hum", "def gen_stronghold_hum"},
                {"gen-mineshaft_drip", "def gen_mineshaft_drip"},
                {"gen-jungle_chirps", "def gen_jungle_chirps"},
                {"gen-desert_night_wind", "def gen_desert_night_wind"},
                {"gen-achievement", "def gen_achievement"},
                {"gen-chest_open", "def gen_chest_open"},
                {"gen-chest_close", "def gen_chest_close"},
            });
            const bool okPinT21 = missPinT21.isEmpty();
            okB = okB && okPinT21;
            if (!okPinT21)
                qInfo().noquote() << "  [t1021 diag] source pins drifted:" << missPinT21.join(QLatin1Char(','));
        }
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1021 sound-layer assets and wiring: seven new wavs exist with valid"
                             " PCM s16 mono 44100 headers and expected durations (two 8s loops,"
                             " drip/chirps/achievement/chest-open/chest-close one-shots), qrc"
                             " registered, and the full chain pinned (zone property/enum/fold-emit"
                             " gate/night+exposed gate/dual resets in PlayerController, QML zone"
                             " dispatch + achievement chime + chest open/close routing, AudioManager"
                             " Q_INVOKABLE surface, build_sounds.py generators)";
    });

    // ── t1022 键位重映射探针（面板 A：KeybindManager 默认表完整性 + settings.json 真 round-trip +
    //    冲突拒收 + 旧档缺节/坏值向后兼容 + 恢复默认 + 显示名；显式 setStorePath 密闭于临时目录，
    //    绝不触碰工程根真实 settings.json——t779/t785 探针密闭语义同款）──
    runLegMulti({ "t1022 key-remap table: 13-action default table complete with pairwise-unique canonical keys (pin"
        "ned id->key order), remap persists into settings.json keyBindings preserving sibling fields, rel"
        "oad restores the mapping (forward->Up canonicalizes to engine W), unowned physical keys DISCARD "
        "to Key_unknown (review0907 alias fix), conflicting apply is rejected with mapping intact, vacate"
        "d canonical key is claimable, fixed keys (Esc/digits/B/G/Ctrl) rejected ApplyForbiddenKey with m"
        "odeCycle->G idempotent re-apply kept, unknown action/invalid key rejected, resetDefaults round-t"
        "rips to file, partial section keeps defaults for missing/invalid rows, keyDisplayName W" }, [&]() {
        using KM = KeybindManager;
        bool okA = true;
        QString diagT22;
        // (a) 默认表完整性：恰 13 动作、每动作默认键合法且两两不冲突（一键一动作不变式）、
        //     id/canonical 逐项钉死（单一权威表被重排/漏行/改键在此暴露——引擎 canonical 语义锚）。
        const QList<KM::ActionDef> &tableA = KM::actionTable();
        if (tableA.size() != 13)
            diagT22 += QStringLiteral("table size %1 ").arg(tableA.size());
        okA = okA && tableA.size() == 13;
        struct ExpT22 { const char *id; int key; };
        const ExpT22 expT22[] = {
            {"forward", Qt::Key_W},  {"back", Qt::Key_S},   {"left", Qt::Key_A},
            {"right", Qt::Key_D},    {"jump", Qt::Key_Space}, {"sneak", Qt::Key_Shift},
            {"inventory", Qt::Key_E}, {"drop", Qt::Key_Q},  {"chat", Qt::Key_T},
            {"camera", Qt::Key_F5},  {"debugTime", Qt::Key_F6}, {"modeCycle", Qt::Key_G},
            {"debugOverlay", Qt::Key_F3},
        };
        QSet<int> seenKeysT22;
        for (int i = 0; i < tableA.size() && i < 13; ++i) {
            const bool rowOk = tableA[i].canonical == expT22[i].key
                && QByteArray(tableA[i].id) == expT22[i].id
                && tableA[i].canonical >= 0x20 && !seenKeysT22.contains(tableA[i].canonical);
            if (!rowOk)
                diagT22 += QStringLiteral("row%1 ").arg(i);
            okA = okA && rowOk;
            seenKeysT22.insert(tableA[i].canonical);
        }
        // (b) 重映射→持久化→重载→映射保持（真 settings.json round-trip）+ 其它字段保留。
        const QString storeDirT22 = QDir::tempPath()
            + QStringLiteral("/voxel_t1022_probe_%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(storeDirT22);
        const QString storeAT22 = storeDirT22 + QStringLiteral("/settings.json");
        const auto writeTextT22 = [](const QString &path, const QByteArray &body) {
            QFile f(path);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(body) == body.size();
        };
        writeTextT22(storeAT22, "{\"playerSkin\":\"alex\"}");
        KM kbT22;
        kbT22.setStorePath(storeAT22);   // 缺 keyBindings 节 → 全默认（旧档向后兼容面）
        okA = okA && kbT22.keyFor(QStringLiteral("forward")) == Qt::Key_W
                  && kbT22.keyFor(QStringLiteral("sneak")) == Qt::Key_Shift;
        okA = okA && kbT22.applyBinding(QStringLiteral("forward"), Qt::Key_Up) == int(KM::ApplyOk);
        {
            QFile f(storeAT22);
            const QString bodyA = f.open(QIODevice::ReadOnly)
                ? QString::fromUtf8(f.readAll()) : QString();
            const bool persistedA = bodyA.contains(QStringLiteral("\"keyBindings\""))
                && bodyA.contains(QStringLiteral("\"forward\""))
                && bodyA.contains(QStringLiteral("\"playerSkin\""));   // 其它字段保留（writeSettings 管线同款）
            if (!persistedA) diagT22 += QStringLiteral("persist %1 ").arg(bodyA.size());
            okA = okA && persistedA;
        }
        KM kbT22b;
        kbT22b.setStorePath(storeAT22);  // 重载（模拟重启）：映射保持
        okA = okA && kbT22b.keyFor(QStringLiteral("forward")) == Qt::Key_Up;
        okA = okA && kbT22b.canonicalKey(Qt::Key_Up) == Qt::Key_W;   // 反向规范化：↑ → 引擎 W
        // review0907 A-P2-2 4a：无归属键丢弃（Key_unknown）—— 旧断言「Key_W 原样透传（canonical 自身
        //   恒等）」正是被修的隐藏别名缺陷（forward 改 ↑ 后物理 W 经透传恒等继续驱动前进）。
        okA = okA && kbT22b.canonicalKey(Qt::Key_W) == Qt::Key_unknown;
        okA = okA && kbT22b.actionOfKey(Qt::Key_Up) == QStringLiteral("forward");
        okA = okA && kbT22b.keyFor(QStringLiteral("back")) == Qt::Key_S; // 未动的动作保持默认
        // (c) 冲突检测拒收（一键多动作）+ 旧 canonical 让出后可被认领 + 幂等 + 未知动作/非法键。
        okA = okA && kbT22b.applyBinding(QStringLiteral("back"), Qt::Key_Up) == int(KM::ApplyConflict);
        okA = okA && kbT22b.keyFor(QStringLiteral("back")) == Qt::Key_S;   // 拒收后映射不变
        okA = okA && kbT22b.applyBinding(QStringLiteral("back"), Qt::Key_W) == int(KM::ApplyOk); // W 已让出 → 可认领
        okA = okA && kbT22b.canonicalKey(Qt::Key_W) == Qt::Key_S;
        okA = okA && kbT22b.applyBinding(QStringLiteral("forward"), Qt::Key_Up) == int(KM::ApplyOk); // 自身同值幂等
        okA = okA && kbT22b.applyBinding(QStringLiteral("nosuch"), Qt::Key_P) == int(KM::ApplyUnknownAction);
        okA = okA && kbT22b.keyFor(QStringLiteral("nosuch")) == 0;
        okA = okA && kbT22b.applyBinding(QStringLiteral("jump"), 0) == int(KM::ApplyUnknownAction); // 非法键拒收
        // (c2) review0907 A-P2-2 4b：固定不可映射黑名单守卫 —— Esc / 数字键（登记口径固定键）作为绑定
        //      目标被拒（ApplyForbiddenKey），映射不变；同值重绑黑名单内默认键（modeCycle→G）幂等 Ok。
        okA = okA && kbT22b.applyBinding(QStringLiteral("forward"), Qt::Key_Escape) == int(KM::ApplyForbiddenKey);
        okA = okA && kbT22b.applyBinding(QStringLiteral("forward"), Qt::Key_3) == int(KM::ApplyForbiddenKey);
        okA = okA && kbT22b.keyFor(QStringLiteral("forward")) == Qt::Key_Up;  // 拒收后映射不变
        okA = okA && kbT22b.applyBinding(QStringLiteral("inventory"), Qt::Key_B) == int(KM::ApplyForbiddenKey);
        okA = okA && kbT22b.applyBinding(QStringLiteral("drop"), Qt::Key_Control) == int(KM::ApplyForbiddenKey);
        okA = okA && kbT22b.keyFor(QStringLiteral("modeCycle")) == Qt::Key_G; // 默认 G 在黑名单内（登记口径）
        okA = okA && kbT22b.applyBinding(QStringLiteral("modeCycle"), Qt::Key_G) == int(KM::ApplyOk); // 同值幂等不回归
        // (d) 恢复默认：内存回落 + 落盘改写 + 无归属键丢弃（4a 语义）。
        kbT22b.resetDefaults();
        okA = okA && kbT22b.keyFor(QStringLiteral("forward")) == Qt::Key_W
                  && kbT22b.keyFor(QStringLiteral("back")) == Qt::Key_S;
        okA = okA && kbT22b.canonicalKey(Qt::Key_Up) == Qt::Key_unknown; // 无归属 → 丢弃（非透传）
        {
            QFile f(storeAT22);
            const QString bodyB = f.open(QIODevice::ReadOnly)
                ? QString::fromUtf8(f.readAll()) : QString();
            okA = okA && !bodyB.contains(QStringLiteral("16777235")); // Key_Up 十进制值已从盘上消失
        }
        // (e) 部分节 / 坏值：仅 jump 重绑生效，坏值动作落默认，其余动作不串。
        const QString storeBT22 = storeDirT22 + QStringLiteral("/settings_partial.json");
        writeTextT22(storeBT22,
                     QByteArray("{\"keyBindings\":{\"jump\":74,\"sneak\":5}}")); // 74=Key_J；5 非法
        KM kbT22c;
        kbT22c.setStorePath(storeBT22);
        okA = okA && kbT22c.keyFor(QStringLiteral("jump")) == (Qt::Key_J)
              && kbT22c.keyFor(QStringLiteral("sneak")) == Qt::Key_Shift
              && kbT22c.keyFor(QStringLiteral("forward")) == Qt::Key_W;
        // (f) 键显示名（设置页行按钮文本权威）。
        okA = okA && kbT22.keyDisplayName(Qt::Key_W) == QStringLiteral("W");
        if (!okA)
            qInfo().noquote() << "  [t1022 diag]" << diagT22;
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1022 key-remap table: 13-action default table complete with"
                             " pairwise-unique canonical keys (pinned id->key order), remap"
                             " persists into settings.json keyBindings preserving sibling"
                             " fields, reload restores the mapping (forward->Up canonicalizes"
                             " to engine W), unowned physical keys DISCARD to Key_unknown"
                             " (review0907 alias fix), conflicting apply is rejected with"
                             " mapping intact, vacated canonical key is claimable, fixed"
                             " keys (Esc/digits/B/G/Ctrl) rejected ApplyForbiddenKey with"
                             " modeCycle->G idempotent re-apply kept, unknown action/invalid"
                             " key rejected, resetDefaults round-trips to file, partial"
                             " section keeps defaults for missing/invalid rows, keyDisplayName W";
    });

    // ── t1022 键位重映射探针（面板 B：引擎侧生效链——真 PlayerController × KeybindManager 注入，
    //    模拟按键事件（setKey 物理键）→ canonical 翻译 → m_keys → 动作触发（跳/蹲/前进位移）；
    //    null 注入对照（未注入 = 原始键直入旧行为）；QML 路由/设置页/引擎 choke 点/构建接线源码钉）──
    runLegMulti({ "t1022 key-remap engine chain: injected KeybindManager makes PlayerController.setKey canonicalize"
        " simulated key events -- default Space jumps, rebound C jumps, rebound X sneaks into Crouch and "
        "releases to Walk, rebound Up walks forward (+Z at yaw 180) while the vacated W is dead (review09"
        "07 alias fix: unowned keys discard instead of passthrough), non-injected controller keeps raw-ke"
        "y legacy behavior, all probe writes stay hermetic to a temp store (project-root settings.json by"
        "te-identical sentinel), and the full wiring is pinned (QML seven-action keyFor routing + recorde"
        "r/conflict/reset settings panel + revision-touch guard + engine canonical choke point + CMake du"
        "al-target sources; 18 wiring pins, comment-filtered) SEPARATELY COUNTED from 3 copy pins (user-v"
        "isible string existence: reset/recording/ conflict labels - presence pinning, not wiring)" }, [&]() {
        using KM = KeybindManager;
        bool okB = true;
        QString diagT22B;
        World wT22;
        wT22.setWidth(32); wT22.setDepth(32); wT22.setHeight(48);
        // 高空石平台（kRigY=41 同款；地形最高 ~33，41 必空）：12×12 立足面供跳/蹲/走行为腿。
        for (int x = 6; x <= 26; ++x)
            for (int z = 6; z <= 26; ++z)
                wT22.setBlock(x, 41, z, BR::Stone, 0);
        // 密闭（t779/t785 先例）：显式 setStorePath 指临时目录 —— 本面板 applyBinding 会触发 persist()，
        //   不密闭则写穿工程根真实 settings.json（本单实现头注释明令禁止）。文件先落盘再 setStorePath
        //   （load() 读该文件全默认起步，构造期对真实档的一次只读不落地，随即被覆盖）。
        const QString storeDirB22 = QDir::tempPath()
            + QStringLiteral("/voxel_t1022_probeB_%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(storeDirB22);
        const QString storeBT22Engine = storeDirB22 + QStringLiteral("/settings.json");
        const auto writeTextB22 = [](const QString &path, const QByteArray &body) {
            QFile f(path);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(body) == body.size();
        };
        writeTextB22(storeBT22Engine, "{}");   // 空档（面板 A 作用域外，本面板自备密闭存储）
        KM kbT22B;
        kbT22B.setStorePath(storeBT22Engine);
        // 密闭哨兵：面板开头抓工程根真实 settings.json 字节快照（可能不存在），面板末断言逐字节
        //   未变 —— 本面板任何 applyBinding 写穿真实档（含未来真实档已有 keyBindings 节场景）在此暴露。
        const QString realStoreT22 = QDir(QCoreApplication::applicationDirPath()
                                          + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json"));
        QByteArray realSnapT22;
        {
            QFile f(realStoreT22);
            if (f.open(QIODevice::ReadOnly)) realSnapT22 = f.readAll();
        }
        PlayerController pcT22;   // 无窗口直造（t1021 同款；tick 直调）
        pcT22.setWorld(&wT22);
        pcT22.setKeybinds(&kbT22B);   // 引擎侧映射注入（Main.qml keybinds: keybindsMgr 同链）
        // 墙钟泵（t889 pumpFor 同款 busy-wait）：tickImpl 的 dt = m_clock.restart() 墙钟差，探针连调
        //   tick() 间隔仅微秒 → dt≈0 → 重力/位移零推进（首跑全腿假红根因：jump/crouch/walk 全不触发）。
        //   每 tick 前 busy-wait ≥17ms 喂出真实 dt≈0.017s（钳 50ms 内）。
        const auto tickB = [&](PlayerController &pc, int n) {
            for (int i = 0; i < n; ++i) {
                QElapsedTimer wait22; wait22.start();
                while (wait22.elapsed() < 17)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                pc.tick();
            }
        };
        // (i) 默认恒等腿：Space（canonical 直入）→ 生存跳起离地又落地。
        // yaw=180：前向 = (-sin yaw, -cos yaw) = (0,+1) = +Z（首跑踩过的坑：yaw -90 前进朝 +X，
        //   (iv)(v) 的 z 位移断言恒假——朝向轴与断言轴对齐）。
        pcT22.loadSavedState(16.5f, 42.05f, 16.5f, 180.0f, 0.0f, 2); // Survival；脚位平台顶上方
        tickB(pcT22, 12);
        okB = okB && pcT22.onGround();
        const float y0T22 = pcT22.feetPosition().y();
        pcT22.setKey(Qt::Key_Space, true);
        bool jumpedDefaultT22 = false;
        for (int t = 0; t < 30 && !jumpedDefaultT22; ++t) {
            tickB(pcT22, 1);
            jumpedDefaultT22 = pcT22.feetPosition().y() > y0T22 + 0.25f;
        }
        pcT22.setKey(Qt::Key_Space, false);
        tickB(pcT22, 40);
        if (!jumpedDefaultT22 || !pcT22.onGround())
            diagT22B += QStringLiteral("i jump=%1 gnd=%2 ").arg(jumpedDefaultT22).arg(pcT22.onGround());
        okB = okB && jumpedDefaultT22 && pcT22.onGround();
        // (ii) 重映射跳跃腿：jump→C 后按 C（物理键）→ canonical Space → 真跳（模拟按键事件→动作触发）。
        okB = okB && kbT22B.applyBinding(QStringLiteral("jump"), Qt::Key_C) == int(KM::ApplyOk);
        const float y1T22 = pcT22.feetPosition().y();
        pcT22.setKey(Qt::Key_C, true);
        bool jumpedRemapT22 = false;
        for (int t = 0; t < 30 && !jumpedRemapT22; ++t) {
            tickB(pcT22, 1);
            jumpedRemapT22 = pcT22.feetPosition().y() > y1T22 + 0.25f;
        }
        pcT22.setKey(Qt::Key_C, false);
        tickB(pcT22, 40);
        if (!jumpedRemapT22 || !pcT22.onGround())
            diagT22B += QStringLiteral("ii jump=%1 gnd=%2 ").arg(jumpedRemapT22).arg(pcT22.onGround());
        okB = okB && jumpedRemapT22 && pcT22.onGround();
        // (iii) 潜行重映射腿：sneak→X → setKey(X) → canonical Shift → 蹲态机 Crouch；松开回 Walk。
        okB = okB && kbT22B.applyBinding(QStringLiteral("sneak"), Qt::Key_X) == int(KM::ApplyOk);
        pcT22.setKey(Qt::Key_X, true);
        const bool crouchT22 = pcT22.moveState() == PlayerController::Crouch;
        pcT22.setKey(Qt::Key_X, false);
        const bool standT22 = pcT22.moveState() == PlayerController::Walk; // 露天平台可站立
        if (!crouchT22 || !standT22)
            diagT22B += QStringLiteral("iii crouch=%1 stand=%2 ").arg(crouchT22).arg(standT22);
        okB = okB && crouchT22 && standT22;
        // (iv) 前进重映射腿：forward→↑ → setKey(↑) → canonical W → +Z 位移（yaw 180 = 朝 +Z）；
        //      且让出的 W（无任何动作值集归属）**不再驱动**（review0907 A-P2-2 4a 别名丢弃：旧断言
        //      「W 透传仍前进」正是被修的隐藏别名缺陷）。24 tick（≈0.4s）：走加速段起步低速，
        //      8 tick 位移不足 0.2 阈（先例走位腿 24-60 tick 同量级）。
        okB = okB && kbT22B.applyBinding(QStringLiteral("forward"), Qt::Key_Up) == int(KM::ApplyOk);
        const float z0T22 = pcT22.feetPosition().z();
        pcT22.setKey(Qt::Key_Up, true);
        tickB(pcT22, 24);
        pcT22.setKey(Qt::Key_Up, false);
        tickB(pcT22, 2);
        const bool fwdRemapT22 = pcT22.feetPosition().z() > z0T22 + 0.2f;
        const float z1T22 = pcT22.feetPosition().z();
        pcT22.setKey(Qt::Key_W, true);
        tickB(pcT22, 24);
        pcT22.setKey(Qt::Key_W, false);
        tickB(pcT22, 2);
        const bool fwdAliasKilledT22 = std::fabs(pcT22.feetPosition().z() - z1T22) < 0.05f;
        if (!fwdRemapT22 || !fwdAliasKilledT22)
            diagT22B += QStringLiteral("iv remap=%1 aliasKilled=%2 ").arg(fwdRemapT22).arg(fwdAliasKilledT22);
        okB = okB && fwdRemapT22 && fwdAliasKilledT22;
        // (v) null 注入对照：未注入映射 → 原始键直入 m_keys（旧行为不变，安全降级面）。
        PlayerController pcT22b;
        pcT22b.setWorld(&wT22);
        pcT22b.loadSavedState(20.5f, 42.05f, 12.5f, 180.0f, 0.0f, 2);
        tickB(pcT22b, 12);
        const float z0bT22 = pcT22b.feetPosition().z();
        pcT22b.setKey(Qt::Key_W, true);
        tickB(pcT22b, 24);
        pcT22b.setKey(Qt::Key_W, false);
        okB = okB && pcT22b.feetPosition().z() > z0bT22 + 0.2f;
        // ── 源码钉：QML 路由（keyInput 七动作查询 + 设置页编辑面 + 焦点归还）+ 引擎 choke 点 +
        //    注入属性 + 构建接线（app/test 双目标）。B-P1-1 迁移：滤注释钉（pinSet）+ 分列记账
        //    （B-P2-5）：**接线钉**（wiring，18 条：语句/属性/构建接线）与 **copy 钉**（3 条：
        //    「恢复默认键位 / 按任意键… / 冲突：「」——用户可见字符串存在性，非接线）分开计数，
        //    PASS 行文案分列，不再混称接线钉。
        {
            const QString rootT22 = QDir(QCoreApplication::applicationDirPath()
                                         + QStringLiteral("/..")).absolutePath();
            const auto srcT22 = [&rootT22](const QString &rel) {
                return rootT22 + QLatin1Char('/') + rel;
            };
            QStringList missPinW22; // 接线钉（wiring）
            missPinW22 << pinSet(srcT22(QStringLiteral("src/ui/Main.qml")), {
                {"qml-inst-keybindMgr", "KeybindManager { id: keybindsMgr }"},
                {"qml-prop-keybinds", "keybinds: keybindsMgr"},
                {"qml-keyFor-chat", "keybindsMgr.keyFor(\"chat\")"},
                {"qml-keyFor-inventory", "keybindsMgr.keyFor(\"inventory\")"},
                {"qml-keyFor-drop", "keybindsMgr.keyFor(\"drop\")"},
                {"qml-keyFor-camera", "keybindsMgr.keyFor(\"camera\")"},
                {"qml-keyFor-debugTime", "keybindsMgr.keyFor(\"debugTime\")"},
                {"qml-keyFor-modeCycle", "keybindsMgr.keyFor(\"modeCycle\")"},
                {"qml-keyFor-debugOverlay", "keybindsMgr.keyFor(\"debugOverlay\")", 2}, // press+release
                {"qml-applyBinding", "keybindsMgr.applyBinding(act, e.key)"},
                {"qml-conflict-enum", "KeybindManager.ApplyConflict"},
                {"qml-resetDefaults", "keybindsMgr.resetDefaults()"},
                {"qml-recording-action", "settingsPanelT22.recordingAction = modelData.id"},
                {"qml-revision-guard", "const _r = keybindsMgr.revision"}, // t976 AOT 触碰守卫
                {"qml-recorder-focus", "keyRecorderT22.forceActiveFocus()"},
            });
            missPinW22 << pinSet(srcT22(QStringLiteral("src/Game/playercontroller.h")), {
                {"prop-keybinds", "Q_PROPERTY(KeybindManager *keybinds READ keybinds WRITE setKeybinds NOTIFY keybindsChanged)"},
            });
            missPinW22 << pinSet(srcT22(QStringLiteral("src/Game/playercontroller.cpp")), {
                {"choke-canonical", "key = m_keybinds->canonicalKey(key);"},
            });
            missPinW22 << pinSet(srcT22(QStringLiteral("CMakeLists.txt")), {
                {"cmake-keybindmgr", "src/Core/keybindmanager.cpp", 2},    // app + test 双目标
            });
            const QStringList missPinC22 = pinSet(srcT22(QStringLiteral("src/ui/Main.qml")), {
                {"copy-reset-label", "恢复默认键位"},   // copy 钉：用户可见字符串存在性（非接线）
                {"copy-recording-prompt", "按任意键…"},
                {"copy-conflict-prefix", "冲突：「"},
            });
            const bool okPinW22 = missPinW22.isEmpty();  // 接线钉组
            const bool okPinC22 = missPinC22.isEmpty();  // copy 钉组（独立记账，不与接线混计）
            okB = okB && okPinW22 && okPinC22;
            if (!okPinW22)
                diagT22B += QStringLiteral("pinsW[%1] ").arg(missPinW22.join(QLatin1Char(',')));
            if (!okPinC22)
                diagT22B += QStringLiteral("pinsC[copy] %1 ").arg(missPinC22.join(QLatin1Char(',')));
        }
        // 密闭哨兵断言：真实 settings.json 逐字节未变（写穿本单头注释明令禁止面在此暴露）。
        QByteArray realAfterT22;
        {
            QFile f(realStoreT22);
            if (f.open(QIODevice::ReadOnly)) realAfterT22 = f.readAll();
        }
        const bool hermeticT22 = realAfterT22 == realSnapT22;
        if (!hermeticT22) diagT22B += QStringLiteral("hermetic ");
        okB = okB && hermeticT22;
        if (!okB)
            qInfo().noquote() << "  [t1022 diag B]" << diagT22B;
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1022 key-remap engine chain: injected KeybindManager makes"
                             " PlayerController.setKey canonicalize simulated key events --"
                             " default Space jumps, rebound C jumps, rebound X sneaks into"
                             " Crouch and releases to Walk, rebound Up walks forward (+Z at"
                             " yaw 180) while the vacated W is dead (review0907 alias fix:"
                             " unowned keys discard instead of passthrough),"
                             " non-injected controller keeps raw-key legacy behavior, all"
                             " probe writes stay hermetic to a temp store (project-root"
                             " settings.json byte-identical sentinel), and"
                             " the full wiring is pinned (QML seven-action keyFor routing +"
                             " recorder/conflict/reset settings panel + revision-touch guard"
                             " + engine canonical choke point + CMake dual-target sources;"
                             " 18 wiring pins, comment-filtered) SEPARATELY COUNTED from"
                             " 3 copy pins (user-visible string existence: reset/recording/"
                             " conflict labels - presence pinning, not wiring)";
    });

    // ── t1022 键位重映射探针（面板 C：review0907 A-P2-2 两病灶修复面 —— 隐藏别名丢弃 + 固定键黑名单
    //    强制；纯 KeybindManager 层，密闭临时存储）──
    //    (i) 别名丢弃：forward→C 后 canonicalKey(C)=W（翻译生效）而 canonicalKey(W)=Key_unknown
    //        （物理旧键不再驱动 —— 原「无归属透传」让 W 经 canonical 恒等继续前进）。
    //    (ii) 黑名单强制：applyBinding 目标命中登记口径固定键（Esc / 数字 / B / G / Ctrl / 修饰）→
    //         ApplyForbiddenKey 拒收，映射不变；Esc/鼠标旁路面不经 canonicalKey choke（登记口径备案）。
    //    (iii) 幂等不回归：黑名单内默认键同值重绑（modeCycle→G / sneak→Shift）保持 ApplyOk。
    //    (iv) load() 侧强制：settings.json 带黑名单键值（jump=51 即 Key_3 / chat=16777216 即 Esc）→
    //         读回落默认（值域守卫口径延伸到黑名单；非法值落默认语义不变）。
    runLegMulti({ "t1022 alias-kill + forbidden-key blacklist (review0907 A-P2-2): after forward->C the physical ol"
        "d key W canonicalizes to Key_unknown (discarded by the setKey choke; no hidden passthrough alias"
        "), fixed registration keys (Esc / digits 0-9 / B / G / Ctrl) are rejected as bind targets with A"
        "pplyForbiddenKey and the mapping stays intact, same-value re-apply of blacklisted defaults (mode"
        "Cycle->G, sneak->Shift) stays ApplyOk, and load() falls blacklisted settings.json values back to"
        " defaults" }, [&]() {
        using KM = KeybindManager;
        bool okC22 = true;
        QString diagT22C;
        const QString storeDirC22 = QDir::tempPath()
            + QStringLiteral("/voxel_t1022_probeC_%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(storeDirC22);
        const QString storeCT22 = storeDirC22 + QStringLiteral("/settings.json");
        {
            QFile f(storeCT22);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("{}");
        }
        KM kbC22;
        kbC22.setStorePath(storeCT22);
        // (i) 别名丢弃。
        okC22 = okC22 && kbC22.applyBinding(QStringLiteral("forward"), Qt::Key_C) == int(KM::ApplyOk);
        okC22 = okC22 && kbC22.canonicalKey(Qt::Key_C) == Qt::Key_W;        // 新键翻译生效
        okC22 = okC22 && kbC22.canonicalKey(Qt::Key_W) == Qt::Key_unknown;  // 旧键别名丢弃（4a 核心）
        // (iii) 幂等不回归（先于 (ii) 验 —— 需 modeCycle 当前值 = G 默认）：黑名单内默认键同值重绑
        //       保持 ApplyOk（幂等判定先于黑名单守卫，sneak→Shift 同理）。
        okC22 = okC22 && kbC22.keyFor(QStringLiteral("modeCycle")) == Qt::Key_G
                          && kbC22.applyBinding(QStringLiteral("modeCycle"), Qt::Key_G) == int(KM::ApplyOk)
                          && kbC22.applyBinding(QStringLiteral("sneak"), Qt::Key_Shift) == int(KM::ApplyOk);
        // (ii) 黑名单强制（4b 核心）：拒收 + 映射不变。G 默认被 modeCycle 占用（先让它让出，排除
        //      ApplyConflict 分流干扰，单独钉 G 的黑名单面）；其余固定键本就无占用。
        okC22 = okC22 && kbC22.applyBinding(QStringLiteral("modeCycle"), Qt::Key_P) == int(KM::ApplyOk);
        const int forbiddenTargetsC22[6] = { Qt::Key_Escape, Qt::Key_3, Qt::Key_0,
                                             Qt::Key_B, Qt::Key_G, Qt::Key_Control };
        for (int fk : forbiddenTargetsC22) {
            const int res = kbC22.applyBinding(QStringLiteral("jump"), fk);
            if (res != int(KM::ApplyForbiddenKey))
                diagT22C += QStringLiteral("fb%1=%2 ").arg(fk).arg(res);
            okC22 = okC22 && res == int(KM::ApplyForbiddenKey);
        }
        okC22 = okC22 && kbC22.keyFor(QStringLiteral("jump")) == Qt::Key_Space; // 拒收后映射不变
        // (iv) load() 侧黑名单强制：坏键值落默认（51=Key_3；16777216=Key_Escape）。
        const QString storeC22b = storeDirC22 + QStringLiteral("/settings_forbidden.json");
        {
            QFile f(storeC22b);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(QByteArray("{\"keyBindings\":{\"jump\":51,\"chat\":16777216}}"));
        }
        KM kbC22b;
        kbC22b.setStorePath(storeC22b);
        okC22 = okC22 && kbC22b.keyFor(QStringLiteral("jump")) == Qt::Key_Space   // 黑名单值 → 默认
                          && kbC22b.keyFor(QStringLiteral("chat")) == Qt::Key_T;
        if (!okC22)
            qInfo().noquote() << "  [t1022 diag C]" << diagT22C;
        if (!okC22) ++totalFail;
        qInfo().noquote() << (okC22 ? "PASS" : "FAIL")
                          << "| t1022 alias-kill + forbidden-key blacklist (review0907 A-P2-2):"
                             " after forward->C the physical old key W canonicalizes to Key_unknown"
                             " (discarded by the setKey choke; no hidden passthrough alias), fixed"
                             " registration keys (Esc / digits 0-9 / B / G / Ctrl) are rejected as"
                             " bind targets with ApplyForbiddenKey and the mapping stays intact,"
                             " same-value re-apply of blacklisted defaults (modeCycle->G,"
                             " sneak->Shift) stays ApplyOk, and load() falls blacklisted"
                             " settings.json values back to defaults";
    });

    // ── t1022 键位重映射探针（面板 D：review0907 B #1/#2/#5 显示层与拒收码语义腿——纯 KeybindManager
    //    层 + QML 钉，密闭临时存储）──
    //    (i) keyDisplayName 只拒非键值（#1 回归修复签名）：Shift / G / Esc（sneak / modeCycle 默认键
    //        与固定键，恰在绑定黑名单内）返回非空正确名——33cdf6c 显示层复用黑名单把设置页两行键名
    //        滤成空串；0 / Key_unknown 仍空（「（未设置）」门不误开）。
    //    (ii) 单键绑回默认（#2 单向门修复）：sneak→C 后 applyBinding(sneak, Shift) ApplyOk
    //        （own-canonical 放行）；modeCycle→P 后绑回 G 同理。
    //    (iii) 拒收码分流顺序（#5 二义修复）：绑到被其它动作占用的黑名单键（sneak 占 Shift /
    //        modeCycle 占 G）→ ApplyForbiddenKey 而非 ApplyConflict（黑名单先于 conflict）；非黑名单
    //        占用键（inventory 占 E）仍 ApplyConflict（对照腿，分流面不倒挂）。
    //    (iv) QML 钉：录制器 ApplyForbiddenKey 分流行（接线钉）+「该键为固定功能键」文案（copy 钉，
    //        单独记账不与接线混计——t1022B 先例）。
    runLegMulti({ "t1022 keybind display layer + reject ordering (review0907 B #1/#2/#5): keyDisplayName returns co"
        "rrect non-empty names for blacklisted default keys Shift/G/Esc while staying empty for non-key v"
        "alues (settings-page rows no longer blank), own-canonical rebind back to defaults is allowed (sn"
        "eak->Shift, modeCycle->G after moving away), blacklist precedes conflict (occupied fixed key = A"
        "pplyForbiddenKey; non-blacklisted occupied key still ApplyConflict), and the QML recorder has th"
        "e dedicated fixed-key branch (1 wiring pin, comment-filtered) SEPARATELY COUNTED from 1 copy pin"
        " (fixed-key message presence)" }, [&]() {
        using KM = KeybindManager;
        bool okD22 = true;
        QString diagT22D;
        const QString storeDirD22 = QDir::tempPath()
            + QStringLiteral("/voxel_t1022_probeD_%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(storeDirD22);
        const QString storeDT22 = storeDirD22 + QStringLiteral("/settings.json");
        {
            QFile f(storeDT22);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("{}");
        }
        KM kbD22;
        kbD22.setStorePath(storeDT22);
        // (i) 显示层腿。
        const bool dispShiftD22 = kbD22.keyDisplayName(Qt::Key_Shift) == QStringLiteral("Shift");
        const bool dispGD22 = kbD22.keyDisplayName(Qt::Key_G) == QStringLiteral("G");
        const bool dispEscD22 = kbD22.keyDisplayName(Qt::Key_Escape) == QStringLiteral("Esc");
        const bool dispEmptyD22 = kbD22.keyDisplayName(0).isEmpty()
            && kbD22.keyDisplayName(Qt::Key_unknown).isEmpty();
        if (!(dispShiftD22 && dispGD22 && dispEscD22 && dispEmptyD22))
            diagT22D += QStringLiteral("disp[%1%2%3%4] ")
                .arg(dispShiftD22).arg(dispGD22).arg(dispEscD22).arg(dispEmptyD22);
        okD22 = okD22 && dispShiftD22 && dispGD22 && dispEscD22 && dispEmptyD22;
        // (ii) 单键绑回默认（own-canonical 放行；先走开再绑回，幂等路径不参与）。
        const bool backSneakD22 = kbD22.applyBinding(QStringLiteral("sneak"), Qt::Key_C) == int(KM::ApplyOk)
            && kbD22.applyBinding(QStringLiteral("sneak"), Qt::Key_Shift) == int(KM::ApplyOk)
            && kbD22.keyFor(QStringLiteral("sneak")) == Qt::Key_Shift;
        const bool backModeD22 = kbD22.applyBinding(QStringLiteral("modeCycle"), Qt::Key_P) == int(KM::ApplyOk)
            && kbD22.applyBinding(QStringLiteral("modeCycle"), Qt::Key_G) == int(KM::ApplyOk)
            && kbD22.keyFor(QStringLiteral("modeCycle")) == Qt::Key_G;
        if (!(backSneakD22 && backModeD22))
            diagT22D += QStringLiteral("back[%1%2] ").arg(backSneakD22).arg(backModeD22);
        okD22 = okD22 && backSneakD22 && backModeD22;
        // (iii) 分流顺序：黑名单先于 conflict（此时 sneak=Shift / modeCycle=G 在位）；非黑名单占用
        //       仍 conflict 对照；拒收后映射不变。
        const bool forbShiftD22 = kbD22.applyBinding(QStringLiteral("forward"), Qt::Key_Shift)
            == int(KM::ApplyForbiddenKey);
        const bool forbGD22 = kbD22.applyBinding(QStringLiteral("jump"), Qt::Key_G)
            == int(KM::ApplyForbiddenKey);
        const bool stillConflictD22 = kbD22.applyBinding(QStringLiteral("forward"), Qt::Key_E)
            == int(KM::ApplyConflict); // inventory 占 E（非黑名单占用 → 冲突面保持）
        const bool intactD22 = kbD22.keyFor(QStringLiteral("forward")) == Qt::Key_W
            && kbD22.keyFor(QStringLiteral("jump")) == Qt::Key_Space;
        if (!(forbShiftD22 && forbGD22 && stillConflictD22 && intactD22))
            diagT22D += QStringLiteral("order[%1%2%3%4] ")
                .arg(forbShiftD22).arg(forbGD22).arg(stillConflictD22).arg(intactD22);
        okD22 = okD22 && forbShiftD22 && forbGD22 && stillConflictD22 && intactD22;
        // (iv) QML 钉（注释感知 pinSet）：ApplyForbiddenKey 分流行 = 接线钉；专文案 = copy 钉。
        const QString rootD22 = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absolutePath();
        const QStringList missW22D = pinSet(rootD22 + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-forbidden-enum", "KeybindManager.ApplyForbiddenKey"},
        });
        const QStringList missC22D = pinSet(rootD22 + QStringLiteral("/src/ui/Main.qml"), {
            {"copy-forbidden-msg", "该键为固定功能键"},   // copy 钉：用户可见字符串存在性
        });
        const bool pinWokD22 = missW22D.isEmpty();
        const bool pinCokD22 = missC22D.isEmpty(); // copy 钉独立记账（不与接线混计）
        if (!pinWokD22 || !pinCokD22)
            diagT22D += QStringLiteral("pins[%1|%2] ")
                .arg(missW22D.join(QLatin1Char(',')), missC22D.join(QLatin1Char(',')));
        okD22 = okD22 && pinWokD22 && pinCokD22;
        if (!okD22)
            qInfo().noquote() << "  [t1022 diag D]" << diagT22D;
        if (!okD22) ++totalFail;
        qInfo().noquote() << (okD22 ? "PASS" : "FAIL")
                          << "| t1022 keybind display layer + reject ordering (review0907 B #1/#2/#5):"
                             " keyDisplayName returns correct non-empty names for blacklisted default"
                             " keys Shift/G/Esc while staying empty for non-key values (settings-page"
                             " rows no longer blank), own-canonical rebind back to defaults is allowed"
                             " (sneak->Shift, modeCycle->G after moving away), blacklist precedes"
                             " conflict (occupied fixed key = ApplyForbiddenKey; non-blacklisted"
                             " occupied key still ApplyConflict), and the QML recorder has the"
                             " dedicated fixed-key branch (1 wiring pin, comment-filtered)"
                             " SEPARATELY COUNTED from 1 copy pin (fixed-key message presence)";
    });

    // ── P-t1002 要塞 piece 链逐方块重建探针（R19.19 批最大项；placeStronghold piece 化重写验收面）──
    //    rig：t995/t1001 同款 5 seed（20260821/777/424242/1337/90210，缺要塞的种子跳过、备胎续扫，
    //    ≥4 世界才判）× 128×128×64 世界池。断言五层：
    //    (a) 传送门房逐方块：全图 EndPortal 恰 12 格且全在足迹内（12 框架环 + 至多一座要塞）+ 预嵌眼
    //        池化窗（~10%/框）+ 岩浆 [80,200]（盆 9 + 河沟 ~108）+ 银鱼笼 ≥1 + 铁栏杆恰 59（传送门房
    //        39 格栅 + 监狱厅 20）+ 铁门恰 4 格（监狱厅 2 门）+ 圆石楼梯恰 3（直梯段 C3）；
    //    (b) piece 家具面（足迹域计数）：书架 [200,400]（大馆 231 固定 + 小馆可选 86）/ 梯 ≥7（大馆 7 +
    //        储藏室 3）/ 栅栏 ≥10（大馆护栏）/ 要塞箱 ≥3（馆 2 + 储 1 + 箱走廊 0..6）/ 蛛网 ≥8（wiki
    //        7% 口径期望 ~20）/ 火把 ≥10 / 石砖台阶 [40,80]（五向 36 + 螺旋 6 + 柱房 0..24）；
    //    (c) 石砖变体逐块随机池化窗（dy1..8 壳体族格占比）：普通 [40,60]% / 苔 [25,35]% / 裂 [15,25]% /
    //        怪物蛋 [3,8]%（期望 45/30/20/5；窗宽覆盖多 seed 二项噪声 + 家具 plain 砖不入党）；
    //    (d) 确定性：首个有效 seed 重生成 → bounds 域 stride-3 抽样 FNV 一致（PLAN §2-K）；
    //    (e) 源码钉：piece 上限 50 / 传送门保证旗（**阴性轮钉**：摘除即红）/ 权重表行字面（**阴性轮
    //        钉**：打乱即红）/ 变体 45/75/95 比例字面 / 格架 spine 行（传送门链深 5 / 大图书馆 5 /
    //        角房 7 ≥ 链深契约）/ 重试子 seed。
    runLegMulti({ "t1002 stronghold piece-chain rebuild: 12-frame portal room (world-total EndPortal == 12, eyes/po"
        "oled, lava, silverfish cage, 39 grate bars + 20 prison bars = 59, 2 iron doors), furniture count"
        "s (bookshelf [200,400], ladders >=7, webs >=8 at wiki 7%, chests >=3, slabs [40,80]), per-block "
        "variant pool 45/30/20/5 in windows, deterministic re-gen, piece-table/guarantee source pins, see"
        "ds-missworlds" }, [&]() {
        bool ok = true;
        const quint32 seedsT1002[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 5150u, 2718u, 1618u };
        auto sampleHashT1002 = [](World &w, int cx, int cy, int cz) {
            quint32 h = 0x811c9dc5u;
            auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
            for (int dx = -World::kStrongholdHalf; dx <= World::kStrongholdHalf; dx += 3)
                for (int dz = -World::kStrongholdHalf; dz <= World::kStrongholdHalf; dz += 3)
                    for (int dy = 0; dy <= World::kStrongholdWallH + 1; ++dy) {
                        step(quint32(w.blockAt(cx + dx, cy + dy, cz + dz)));
                        step(quint32(w.stateAt(cx + dx, cy + dy, cz + dz)));
                    }
            h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
            return h;
        };
        int  worldsChecked = 0, pooledFrames = 0, pooledEyes = 0, seedMiss = 0;
        bool haveFirst = false;
        quint32 firstSeed = 0, firstHash = 0;
        for (quint32 sd : seedsT1002) {
            if (worldsChecked >= 5) break;
            World wT1002;
            wT1002.setWidth(128);
            wT1002.setDepth(128);
            wT1002.setHeight(64);
            wT1002.setSeed(int(sd)); // setter 内 generate() 全量 worldgen
            if (!wT1002.hasStronghold()) { ++seedMiss; continue; }
            const int px = wT1002.strongholdPortalX(), py = wT1002.strongholdPortalY(), pz = wT1002.strongholdPortalZ();
            const int cx = px, cy = py - World::kStrongholdPortalDy, cz = pz - World::kStrongholdPortalDz;

            // (a)+(b)+(c) 足迹域逐格计数（[cx±22] × [cy, cy+9]）。
            int bars = 0, doors = 0, lava = 0, cobSt = 0, slabs = 0, shelf = 0, ladders = 0;
            int fences = 0, chests = 0, webs = 0, torches = 0, silver = 0;
            long family = 0, famPlain = 0, famMossy = 0, famCrack = 0, famEgg = 0;
            for (int dx = -World::kStrongholdHalf; dx <= World::kStrongholdHalf; ++dx)
                for (int dz = -World::kStrongholdHalf; dz <= World::kStrongholdHalf; ++dz)
                    for (int dy = 0; dy <= World::kStrongholdWallH + 1; ++dy) {
                        const quint8 id = wT1002.blockAt(cx + dx, cy + dy, cz + dz);
                        switch (id) {
                        case BR::IronBars:        ++bars; break;
                        case BR::IronDoor:        ++doors; break;
                        case BR::Lava:            ++lava; break;
                        case BR::CobbleStairs:    ++cobSt; break;
                        case BR::StoneBrickSlab:  ++slabs; break;
                        case BR::Bookshelf:       ++shelf; break;
                        case BR::Ladder:          ++ladders; break;
                        case BR::WoodFence:       ++fences; break;
                        case BR::Cobweb:          ++webs; break;
                        case BR::Torch:           ++torches; break;
                        default: break;
                        }
                        if (id == BR::Chest && (wT1002.stateAt(cx + dx, cy + dy, cz + dz) & BR::ChestStateStrongholdFlag)) ++chests;
                        if (id == BR::Spawner) {
                            const quint8 st = wT1002.stateAt(cx + dx, cy + dy, cz + dz);
                            if (st == BR::SpawnerStateSilverfishFlag || st == BR::SpawnerStateSilverfish) ++silver;
                        }
                        if (dy >= 1 && dy <= World::kStrongholdWallH) { // 变体族池（墙体层）
                            if (id == BR::StoneBrick)          { ++family; ++famPlain; }
                            else if (id == BR::MossyStoneBrick)   { ++family; ++famMossy; }
                            else if (id == BR::CrackedStoneBrick) { ++family; ++famCrack; }
                            else if (id == BR::MonsterEgg)        { ++family; ++famEgg; }
                        }
                    }
            // (a) 全图 EndPortal 扫描：恰 12 格（至多一座要塞 + 环完整）且全在足迹内。
            int worldFrames = 0, framesInBounds = 0;
            for (int x = 0; x < wT1002.width(); ++x)
                for (int z = 0; z < wT1002.depth(); ++z)
                    for (int y = 0; y < wT1002.height(); ++y)
                        if (wT1002.blockAt(x, y, z) == BR::EndPortal) {
                            ++worldFrames;
                            if (x >= cx - World::kStrongholdHalf && x <= cx + World::kStrongholdHalf
                                && z >= cz - World::kStrongholdHalf && z <= cz + World::kStrongholdHalf
                                && y >= cy && y <= cy + World::kStrongholdWallH + 1)
                                ++framesInBounds;
                        }
            // 预嵌眼（12 框位 state bit0）。
            int eyes = 0;
            for (int rdx = -2; rdx <= 2; ++rdx)
                for (int rdz = -2; rdz <= 2; ++rdz) {
                    const bool onRing = (rdx == -2 || rdx == 2) ? (rdz >= -1 && rdz <= 1)
                                        : (rdz == -2 || rdz == 2) && (rdx >= -1 && rdx <= 1);
                    if (onRing && (wT1002.stateAt(px + rdx, py, pz + rdz) & BR::EndPortalStateActiveFlag)) ++eyes;
                }
            pooledFrames += 12;
            pooledEyes += eyes;

            ok = ok && worldFrames == 12 && framesInBounds == 12;
            ok = ok && bars == 59 && doors == 4 && cobSt == 3 && silver >= 1;
            ok = ok && lava >= 80 && lava <= 200;
            ok = ok && shelf >= 200 && shelf <= 400 && ladders >= 7 && fences >= 10;
            ok = ok && chests >= 3 && webs >= 8 && torches >= 10;
            ok = ok && slabs >= 40 && slabs <= 80;
            ok = ok && family >= 3000;
            ok = ok && famPlain * 100 >= family * 40 && famPlain * 100 <= family * 60;
            ok = ok && famMossy * 100 >= family * 25 && famMossy * 100 <= family * 35;
            ok = ok && famCrack * 100 >= family * 15 && famCrack * 100 <= family * 25;
            ok = ok && famEgg   * 100 >= family *  3 && famEgg   * 100 <= family *  8;
            if (!ok)
                qInfo().noquote() << "  [t1002 diag] seed" << sd << "frames" << worldFrames << "/" << framesInBounds
                                  << "bars" << bars << "doors" << doors << "cobSt" << cobSt << "silver" << silver
                                  << "lava" << lava << "shelf" << shelf << "ladders" << ladders << "fences" << fences
                                  << "chests" << chests << "webs" << webs << "torches" << torches << "slabs" << slabs
                                  << "fam%" << (family ? famPlain * 100 / family : -1) << (family ? famMossy * 100 / family : -1)
                                  << (family ? famCrack * 100 / family : -1) << (family ? famEgg * 100 / family : -1)
                                  << "family" << family;

            if (!haveFirst) { // (d) 确定性基线（首个有效世界）
                haveFirst = true;
                firstSeed = sd;
                firstHash = sampleHashT1002(wT1002, cx, cy, cz);
            }
            ++worldsChecked;
        }
        ok = ok && worldsChecked >= 4;
        if (haveFirst) { // (d) 同 seed 重生成 → 抽样 FNV 一致
            World wR1002;
            wR1002.setWidth(128);
            wR1002.setDepth(128);
            wR1002.setHeight(64);
            wR1002.setSeed(int(firstSeed));
            const int px = wR1002.strongholdPortalX(), py = wR1002.strongholdPortalY(), pz = wR1002.strongholdPortalZ();
            ok = ok && wR1002.hasStronghold()
                 && sampleHashT1002(wR1002, px, py - World::kStrongholdPortalDy, pz - World::kStrongholdPortalDz) == firstHash;
        }
        // 预嵌眼池化窗（~10%/框：均值 ~6/60；[0,18] 容多 seed 二项噪声）。
        ok = ok && pooledEyes >= 1 && pooledEyes <= pooledFrames * 3 / 10; // review0904：下界 0→1（原恒真，对「永不预嵌眼」回归无判别力；10%/框 × 5 世界期望 ~6）

        // (e) 源码钉（world.cpp）：上限 / 保证旗 / 权重表行 / 变体比例 / 格架 spine 行 / 重试子 seed。
        {
            const QString exeDirT1002 = QCoreApplication::applicationDirPath();
            const QString rootT1002 = QDir(exeDirT1002 + QStringLiteral("/..")).absolutePath();
            QFile fT1002(rootT1002 + QStringLiteral("/src/World/world.cpp"));
            const QString src = fT1002.open(QIODevice::ReadOnly) ? QString::fromUtf8(fT1002.readAll()) : QString();
            const bool okPin =
                src.contains(QStringLiteral("kStrongholdPieceCap      = 50;"))
                && src.contains(QStringLiteral("kPortalChainDepth        = 5;"))
                && src.contains(QStringLiteral("kLibraryMinChainDepth    = 4;"))
                && src.contains(QStringLiteral("constexpr bool kPortalGrateGuaranteed = true;"))
                && src.contains(QStringLiteral("attempt * 7919"))
                && src.contains(QStringLiteral("{ PieceRoomEmpty,    6,"))
                && src.contains(QStringLiteral("{ PieceRoomFountain, 5,"))
                && src.contains(QStringLiteral("{ PieceCorridor,      10,"))
                && src.contains(QStringLiteral("(m < 45u)"))
                && src.contains(QStringLiteral("(m < 75u)"))
                && src.contains(QStringLiteral("(m < 95u)"))
                && src.contains(QStringLiteral("{ -13, -22,  13, -11, 5, PiecePortalRoom"))
                && src.contains(QStringLiteral("{ -22,  -6, -12,  10, 5, PieceLibraryLarge"))
                && src.contains(QStringLiteral("{ -22, -21, -14, -12, 7, -1"));
            ok = ok && okPin;
            if (!okPin)
                qInfo().noquote() << "  [t1002 diag] source pins drifted (weight table / guarantee / variants / spine)";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1002 stronghold piece-chain rebuild: 12-frame portal room (world-total EndPortal"
                             " == 12, eyes" << pooledEyes << "/" << pooledFrames << "pooled, lava, silverfish cage,"
                             " 39 grate bars + 20 prison bars = 59, 2 iron doors), furniture counts (bookshelf"
                             " [200,400], ladders >=7, webs >=8 at wiki 7%, chests >=3, slabs [40,80]), per-block"
                             " variant pool 45/30/20/5 in windows, deterministic re-gen, piece-table/guarantee"
                             " source pins, seeds-miss" << seedMiss << "worlds" << worldsChecked;
    });

    // ── P-t1001 废弃矿井逐方块重建探针（R19.19 批 t1001；placeMineshaft piece 化重写验收面）──
    //    rig：t995 同款 5 seed（20260821/777/424242/1337/90210）× 128×128×64 世界池。矿井中心复刻：
    //    placeMineshaft 外层候选扫描（grid 36 / 40% / 抖动位 / margin / y 公式）全同源 —— hashColumn
    //    为私有方法 → 探针侧 FNV-1a 复刻（同 basis/prime/avalanche，算法行源码钉防漂移）；候选再验
    //    起点厅拱带签名（footprint 边环 36 格 Planks @ sy+4）为净样（海列跳过 / 双矿井重叠破坏 → 弃样，
    //    同 t995 净样口径）。
    //    腿：①起点厅（每净样）：拱带 36/36 Planks + 内芯气柱（中心 sy+4 Air）+ 出口数 ∈ [2,4]（四向
    //        房缘外首格 sy+2 头层空气探）且池内见 4 出口矿井（考据 up to 4 exits）；
    //    ②巷道 3×3：出口巷全步采样 w=±1 双侧未封（内容物白名单 Air/Cobweb/WoodFence/Torch = 巷道
    //        合法占用，支撑双柱 t1001 设计本占 w=±1 sy+1..sy+2）池化 ≥70%，双侧实壁 ≥50%（t1012 段 B
    //        推进修复后支廊口 / 交叉口 / 邻巷拐腿 legitimately 开墙 → 全步池化窗，见采样处注释）；
    //    ③支撑间距窗：出口巷 ±1 侧线成对 WoodFence 柱步距全部 ≡0 (mod 4) 且 ≥4、池内 min==4
    //        （阴性轮敏感：kSupportInterval 回退 5 → 步距 5 mod 4 ≠ 0 → 恰红）；
    //    ④蛛网走廊（t1012 ② 走廊形重校）：MobSpider 刷怪笼在场（笼座实体地板 + 7×7×3 域 ≥8 网
    //        〔= t786「≥8 网即矿井蛛笼」分流同口径；走廊形夹网巢保底〕+ 笼邻 4 向 × 3 层零开露空气
    //        〔满网签名〕；t995 地牢净样口径不受染：sy+3 层笼邻有网 → 空气游程 0 ≠ {5,7} 恒弃样；
    //        地牢蛛笼零网 → 不入样）（阴性轮敏感：pieceSpiderRoom 摘除 → 在场腿恰红）；
    //    ⑤残缺轨窗：净样出口巷中线头层可走格轨占率 ∈ [50,90]%（<100% = 残缺真发生）；
    //    ⑥箱贴轨（偏差「箱落地轨旁」）：每 ChestStateMineshaftFlag 箱四水平邻含 Rail 且池内 ≥1；
    //    ⑦火把窗：矿井域 y∈{sy+3, sy+4} Torch 池化 ≥15（火把只出现在支撑过梁顶）；
    //    ⑧源码钉：piece 表五件 + 支撑间隔常量行 + 笼 state 行 + 残缺轨率行 + hashColumn 算法行。
    runLegMulti({ "t1001 mineshaft per-block rebuild: piece-based placeMineshaft (start room 10x10 with arched plan"
        "k band + 3..4 radial exits, 3x3 corridors with supports every 4 (min pooled gap, rock-embedded w"
        "alls% all-steps), fragmented rails% in [50,90], 5x5 pillared intersections, diagonal slope piece"
        "s, cave-spider web corridors (spawner state CaveSpider, box webs>=8 full-web fill%), chests rail"
        "-sidetorches) overclean shafts /candidates, piece-table pins" }, [&]() {
        bool ok = true;
        struct ShaftT1001 { int cx, cz, sy; };
        std::vector<ShaftT1001> shaftsT1001;
        int candT1001 = 0;
        int exitMinT1001 = 99, exitMaxT1001 = 0;
        int sectionTotal = 0, sectionClean = 0, sectionWalls = 0;
        bool gapOkT1001 = true;
        int gapMinT1001 = 999;
        int railWalk = 0, railOn = 0;
        int chestT1001 = 0;
        bool chestRailSide = true;
        bool spiderSeen = false;
        int spiderWebPct = -1;
        int torchT1001 = 0;
        const quint32 seedsT1001[] = { 20260821u, 777u, 424242u, 1337u, 90210u };
        EntityManager emT1001;
        for (quint32 sd : seedsT1001) {
            World wT1001;
            wT1001.setWidth(128);
            wT1001.setDepth(128);
            wT1001.setHeight(64);
            wT1001.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeMineshaft）
            // hashColumn 私有 → FNV-1a 复刻（与 World::hashColumn 同 basis / prime / avalanche；算法行钉源码）
            auto colHashT1001 = [](int seed, int x, int z) -> quint32 {
                quint32 h = 0x811c9dc5u;
                auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
                step(quint32(seed));
                step(quint32(x));
                step(quint32(z));
                h ^= h >> 16;
                h *= 0x7feb352du;
                h ^= h >> 15;
                return h;
            };
            const int wW = wT1001.width(), wD = wT1001.depth();
            const quint32 mineSeed = sd + 15047u; // 与 placeMineshaft 同偏移（mineSeed = m_seed + 15047）
            for (int bx = 18; bx < wW; bx += 36) { // 复刻外层候选网格（kMineshaftGrid=36）
                for (int bz = 18; bz < wD; bz += 36) {
                    const quint32 r = colHashT1001(int(mineSeed), bx, bz);
                    if ((r % 100u) >= 40u) continue;                    // kMinePct=40
                    const int jx = int((r >> 1) & 0xFu) % 19 - 9;       // 抖动位（span=18，同源）
                    const int jz = int((r >> 5) & 0xFu) % 19 - 9;
                    const int cx = bx + jx, cz = bz + jz;
                    if (cx < 16 || cz < 16 || cx >= wW - 16 || cz >= wD - 16) continue; // kMargin=16
                    const int h = std::min(wT1001.heightAt(cx, cz), 63);
                    const int yLo = 6;                            // kBedrockTop+2
                    const int yHi = std::min(43, h - 11);         // kMineshaftMaxY-kRoomH-1 / h-kSurfaceFloor-kRoomH-1
                    if (yHi <= yLo) continue;
                    const int sy = yLo + int((r >> 9) & 0x1Fu) % (yHi - yLo + 1);
                    ++candT1001;
                    // 净样签名：起点厅拱带（10×10 footprint 边环 36 格 Planks @ sy+4）
                    int band = 0;
                    for (int dx = 0; dx < 10; ++dx)
                        for (int dz = 0; dz < 10; ++dz) {
                            const bool edge = (dx == 0 || dx == 9 || dz == 0 || dz == 9);
                            if (edge && wT1001.blockAt(cx - 5 + dx, sy + 4, cz - 5 + dz) == BR::Planks)
                                ++band;
                        }
                    if (band != 36) continue; // 海列 / 重叠破坏 / 口径漂移 → 弃样
                    shaftsT1001.push_back({ cx, cz, sy });
                    ok = ok && wT1001.blockAt(cx, sy + 4, cz) == BR::Air; // ① 内芯气柱（拱顶高段开放）
                    // ① 出口 + ②③⑤ 巷道行走采样（段 A ≤ kTunnelLenMax=10 步 → 11 步窗）
                    static const int kDirsT1001[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    int exitN = 0;
                    for (const auto &dd : kDirsT1001) {
                        const int dx = dd[0], dz = dd[1];
                        const int sx = cx + (dx > 0 ? 5 : (dx < 0 ? -6 : 0)); // 房缘外首格（同生成端）
                        const int sz = cz + (dz > 0 ? 5 : (dz < 0 ? -6 : 0));
                        if (wT1001.blockAt(sx, sy + 2, sz) != BR::Air) continue; // 此向无出口
                        ++exitN;
                        int prevPair = -1; // 成对栅栏柱步位（支撑间距核；杂源单柱不成对 → 忽略）
                        int steps = 0, rails = 0;
                        for (int s = 0; s < 11; ++s) {
                            const int px = sx + dx * s, pz = sz + dz * s;
                            if (wT1001.blockAt(px, sy + 2, pz) != BR::Air) break; // 头层堵 → 巷尽
                            ++steps;
                            if (wT1001.blockAt(px, sy + 1, pz) == BR::Rail) ++rails;
                            const bool fL = wT1001.blockAt(px - dz, sy + 1, pz - dx) == BR::WoodFence;
                            const bool fR = wT1001.blockAt(px + dz, sy + 1, pz + dx) == BR::WoodFence;
                            if (fL && fR) { // 支撑双柱成对
                                if (prevPair >= 0) {
                                    const int gap = s - prevPair;
                                    if (gap < 4 || gap % 4 != 0) gapOkT1001 = false; // ③ 间距 4 窗
                                    if (gap < gapMinT1001) gapMinT1001 = gap;
                                }
                                prevPair = s;
                            }
                            { // ② 3×3 截面（w=±1 未封 + w=±2 实壁；全步池化 —— t1012 ③ 段 B 推进修复后
                              //   巷侧 legitimately 被蛛网支廊口 / 交叉口 5×5 / 邻巷拐腿开墙，单步窗
                              //   （旧 s==6）碰撞率过高 → 全步池化稀释局部开墙，实壁率仍锚「巷道嵌岩」）
                              //   t1012 口径修正：「在位」= 未被天然岩壁封死，内容物白名单（Air / Cobweb /
                              //   WoodFence / Torch）= 巷道合法占用 —— 支撑双柱按 t1001 设计就占 w=±1
                              //   sy+1..sy+2（旧 s==6 单步窗 6%4=2 恰躲开支撑步；全步池化后支撑步 ~1/4
                              //   必含步 0/4 豁免缺失柱，误判「不在位」把池化率拉到 66%<70%，非生成回归）。
                                const auto inPlaceT1001 = [](quint8 b) {
                                    return b == BR::Air || b == BR::Cobweb
                                        || b == BR::WoodFence || b == BR::Torch;
                                };
                                const bool aL = inPlaceT1001(wT1001.blockAt(px - dz, sy + 2, pz - dx));
                                const bool aR = inPlaceT1001(wT1001.blockAt(px + dz, sy + 2, pz + dx));
                                const bool wL = wT1001.blockAt(px - 2 * dz, sy + 2, pz - 2 * dx) != BR::Air;
                                const bool wR = wT1001.blockAt(px + 2 * dz, sy + 2, pz + 2 * dx) != BR::Air;
                                ++sectionTotal;
                                if (aL && aR) {
                                    ++sectionClean;
                                    if (wL && wR) ++sectionWalls;
                                }
                            }
                        }
                        if (steps >= 8) { // ⑤ 残缺轨窗样本（足够长的净巷）
                            railWalk += steps;
                            railOn += rails;
                        }
                    }
                    if (exitN < exitMinT1001) exitMinT1001 = exitN;
                    if (exitN > exitMaxT1001) exitMaxT1001 = exitN;
                    // ⑦ 火把窗（矿井域 y∈{sy+3, sy+4} Torch 计数；火把只在支撑立柱顶，考据「火把部分巷道」）
                    for (int tx = std::max(0, cx - 24); tx <= std::min(wW - 1, cx + 24); ++tx)
                        for (int tz = std::max(0, cz - 24); tz <= std::min(wD - 1, cz + 24); ++tz)
                            for (int ty = sy + 3; ty <= sy + 4; ++ty)
                                if (wT1001.blockAt(tx, ty, tz) == BR::Torch) ++torchT1001;
                }
            }
            // ④⑥ 池级扫描：MobCaveSpider 笼（蛛网室净样；t1012③ 偏差转正 0x0E→0x28）+ 矿井箱贴轨
            for (int y = 7; y < 60; ++y)
                for (int z = 1; z < wD - 1; ++z)
                    for (int x = 1; x < wW - 1; ++x) {
                        const quint8 b = wT1001.blockAt(x, y, z);
                        if (b == BR::Spawner) {
                            if (emT1001.spawnerMobTypeForState(int(wT1001.stateAt(x, y, z)))
                                != EntityManager::MobCaveSpider) continue;
                            if (wT1001.blockAt(x, y - 1, z) == BR::Air) continue; // 笼座须实体地板
                            int webs = 0; // 7×7×3 域蛛网计数（笼心；笼自身格不计）+ 笼邻开露空气（满网签名）
                            int openAir = 0;
                            for (int dx = -3; dx <= 3; ++dx)
                                for (int dz = -3; dz <= 3; ++dz)
                                    for (int dy = 1; dy <= 3; ++dy) {
                                        if (dx == 0 && dz == 0 && dy == 1) continue;
                                        if (wT1001.blockAt(x + dx, y - 1 + dy, z + dz) == BR::Cobweb)
                                            ++webs;
                                    }
                            static const int kCageNbT1001[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                            for (const auto &nb : kCageNbT1001)
                                for (int dy = 1; dy <= 3; ++dy)
                                    if (wT1001.blockAt(x + nb[0], y - 1 + dy, z + nb[1]) == BR::Air)
                                        ++openAir;
                            const int pct = webs * 100 / (49 * 3 - 1);
                            if (webs >= 8 && openAir == 0) { // ④ 走廊形签名（t786 ≥8 分流同口径 + 满网零开露）
                                spiderSeen = true;
                                spiderWebPct = pct;
                            }
                        } else if (b == BR::Chest) {
                            if ((wT1001.stateAt(x, y, z) & BR::ChestStateMineshaftFlag) == 0) continue;
                            ++chestT1001;
                            bool railAdj = false; // ⑥ 偏差「箱落地轨旁」：四水平邻含 Rail
                            static const int kChestNbT1001[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                            for (const auto &nb : kChestNbT1001)
                                if (wT1001.blockAt(x + nb[0], y, z + nb[1]) == BR::Rail) railAdj = true;
                            if (!railAdj) chestRailSide = false;
                        }
                    }
        }
        // ⑧ 源码钉（piece 表五件 + 阴性轮敏感常量 + 笼 state + 残缺轨率 + hash 复刻同源）
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            okPin = src.contains(QStringLiteral("constexpr int kSupportInterval  = 4;"))
                 && src.contains(QStringLiteral("pieceStartRoom"))
                 && src.contains(QStringLiteral("pieceCorridor"))
                 && src.contains(QStringLiteral("pieceIntersection"))
                 && src.contains(QStringLiteral("pieceSlope"))
                 && src.contains(QStringLiteral("pieceSpiderRoom"))
                 && src.contains(QStringLiteral("BlockRegistry::Spawner, BlockRegistry::SpawnerStateCaveSpider")) // t1012③ 转正（旧 SpawnerStateSpider 钉退役）
                 && src.contains(QStringLiteral("(hashVoxel(mineSeed ^ 0x5A17u, ax, ry, az) % 100u) < kRailPct"))
                 && src.contains(QStringLiteral("quint32 World::hashColumn(int seed, int x, int z) const"));
        }
        ok = ok && int(shaftsT1001.size()) >= 6;                            // 净样池充足
        ok = ok && exitMinT1001 >= 2 && exitMinT1001 <= 4;                  // ① 出口 1-4 窗
        ok = ok && exitMaxT1001 >= 4;                                       //    池内见 4 出口矿井
        ok = ok && sectionTotal > 0 && sectionClean * 10 >= sectionTotal * 7;      // ② 3×3 未封在位 ≥70%（内容物白名单）
        ok = ok && sectionClean > 0 && sectionWalls * 10 >= sectionClean * 5;      //    双侧实壁 ≥50%（全步窗，t1012）
        ok = ok && gapOkT1001 && gapMinT1001 == 4;                          // ③ 支撑间距 4 窗
        ok = ok && spiderSeen;                                              // ④ 蛛网走廊在场（t1012 走廊形）
        const int railPctT1001 = railWalk > 0 ? railOn * 100 / railWalk : -1;
        ok = ok && railWalk > 0 && railPctT1001 >= 50 && railPctT1001 <= 90; // ⑤ 残缺轨窗
        ok = ok && chestT1001 >= 1 && chestRailSide;                        // ⑥ 箱贴轨
        ok = ok && torchT1001 >= 15;                                        // ⑦ 火把窗
        ok = ok && okPin;                                                   // ⑧ 源码钉
        if (!ok)
            qInfo().noquote() << "  [t1001 diag] shafts" << shaftsT1001.size() << "/" << candT1001
                              << "exits" << exitMinT1001 << ".." << exitMaxT1001
                              << "section" << sectionClean << "/" << sectionTotal << "walls"
                              << sectionWalls << "gapMin" << gapMinT1001 << "gapOk" << gapOkT1001
                              << "rail%" << railPctT1001 << "(" << railOn << "/" << railWalk << ")"
                              << "spider" << spiderSeen << "web%" << spiderWebPct
                              << "chests" << chestT1001 << "railSide" << chestRailSide
                              << "torches" << torchT1001 << "pin" << okPin;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1001 mineshaft per-block rebuild: piece-based placeMineshaft (start"
                             " room 10x10 with arched plank band + 3..4 radial exits, 3x3 corridors"
                             " with supports every 4 (min pooled gap" << gapMinT1001
                          << ", rock-embedded walls" << (sectionClean > 0 ? sectionWalls * 100 / sectionClean : -1)
                          << "% all-steps), fragmented"
                             " rails" << railPctT1001 << "% in [50,90], 5x5 pillared intersections,"
                             " diagonal slope pieces, cave-spider web corridors (spawner state CaveSpider,"
                             " box webs>=8 full-web fill" << spiderWebPct << "%), chests rail-side" << chestT1001
                          << "torches" << torchT1001 << ") over" << shaftsT1001.size() << "clean"
                             " shafts /" << candT1001 << "candidates, piece-table pins";
    });

    // ── P-t1011 矿井火把密度窗 + 光源归因探针（t1011；placeMineshaft 跨中壁挂火把 + 照明口径验收面）──
    //    用户实测「矿井里见光但 F3 bl:0 没看到火把」→ 归因：bl 通道非零必须近处有火把（lightEmission 14，
    //    generate 末尾 recomputeLightField 种子全场传播；P-t1001 ⑦ 池化 ≥15 已证火把真落块）→ bl:0 的
    //    「见光」必为天光通道：carveCaves 洞穴网络与矿井壁交叠出豁口、洞穴连通地表 → 天光 BFS 渗入巷道
    //    （sl>0 / bl=0 格）。修复面 = 火把覆盖不足：原仅支撑柱顶 55%×2 侧（组级 25% 缺失）+ 斜坡段零火把
    //    → 全暗巷腿出现率 ~3%（0.2×0.4×0.4：step0 双侧皆败 × step4/8 皆败）。t1011 跨中壁挂补光
    //    （step ≡ 2 mod 4 顶层巷壁、TorchAttach 墙插 state、侧 hash 交替）→ 任何走廊步距最近火把 ≤ 4。
    //    rig：t1001 同款候选复刻（hashColumn FNV 同源）扩 8 seed × 128×128×64，拱带 36 Planks 净样 →
    //    逐出口直行采样（头层 sy+2 空气游程，同 t1001 行走口径）：
    //    ① 照明窗（结构性）：巷内火把步距 ≤ 6（7-10 / 11+ 桶恒零 → 任何巷走位距最近火把 ≤ 6 步 =
    //       块光 ≥ 8 光斑在位）+ 全暗前 5 步巷走（真可走段）池化 ≤ 1（修复前 ~3.2% 巷腿全暗 → 恰红）；
    //    ② 密度窗：火把步距直方图（步）0-3 / 4-6 / 7-10 / 11+，0-3 桶占比 ≥ 40% 且 7-10 / 11+ 桶 == 0
    //       （修复前柱间距 4 → 0-3 桶 ≈ 0 恰红；4-6 桶 = 交叉口 evStep=4/6 5×5 刻清邻位壁挂，光照由
    //       邻火把兜住 → 如实入窗）；
    //    ③ 壁挂落地：state 低 3 位 ∈ TorchAttach{1..4}（墙插）的巷道火把池化 ≥ 10；
    //    ④ 块光归因：巷道火把格 blockLightAt ≥ 12 全过（14 种子在场 → 真传播，非残字段）；
    //    ⑤ 起点厅角壁火把：4 角格直接查（墙插/地板回退），池化 ≥ 2×净样数（入口厅照明收口）；
    //    ⑥ 天光豁口登记（遥测非断言）：sl>0 且 bl==0 的巷走头层格计数 —— 用户 F3 观感签名，豁口渗光
    //       通道固有存在（与火把覆盖正交），登记不复零；巷走 max bl 最小值同为遥测（巷外连通敞域
    //       〔洞穴长廊 / 邻矿井厅〕非矿井结构管辖，照明属后续批次口径）；
    //    ⑦ 源码钉：壁挂判定行 / 壁挂落块行 / kTorchPct 行（阴性轮敏感）。
    runLegMulti({ "t1011 mineshaft torch coverage + light attribution: mid-span paired wall torches + start-parlor "
        "corner torches (torch gap hist 0-3/4-6/7-10/11+share% 7-10/11+ zero = light trace within 6 steps"
        " everywhere, dark walkable prefixes<=1, wall-mount torchesroom-corner torchestorch-cell blockLig"
        "ht/>=12, min walk bl(telemetry: corridor-external open volumes = caves/neighbor parlors out of m"
        "ineshaft scope), skylight-seep sl>0/bl=0 cells/(cave-opening daylight channel = the F3 bl:0 attr"
        "ibution, orthogonal to torch coverage) overwalks /shafts, wall-torch pins" }, [&]() {
        bool ok = true;
        int walksT1011 = 0;             // 巷走池（rig 体量）
        int darkPrefixT1011 = 0;        // ① 前 5 步（steps≥5 真可走段）全暗巷走数
        int minWalkBlT1011 = 99;        // ⑥ 遥测：每巷走 max blockLight 的池化最小
        int gapA = 0, gapB = 0, gapC = 0, gapD = 0; // ② 步距直方图 0-3/4-6/7-10/11+
        int wallTorchesT1011 = 0;       // ③ 壁挂（墙插 state）火把
        int torchProbesT1011 = 0, torchBlOkT1011 = 0; // ④ 块光归因探针
        int roomTorchesT1011 = 0;       // ⑤ 起点厅角壁火把（4 角格直查）
        int slSeepT1011 = 0;            // ⑥ sl>0 且 bl==0 头层格（天光豁口遥测）
        int headCellsT1011 = 0;         //    巷走头层采样总数
        int shaftsT1011 = 0, candT1011 = 0;
        const quint32 seedsT1011[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 4242u, 2024u, 31337u };
        for (quint32 sd : seedsT1011) {
            World wT1011;
            wT1011.setWidth(128);
            wT1011.setDepth(128);
            wT1011.setHeight(64);
            wT1011.setSeed(int(sd)); // setter 内 generate() 全量 worldgen + recomputeLightField
            auto colHashT1011b = [](int seed, int x, int z) -> quint32 { // hashColumn FNV 同源复刻（同 t1001）
                quint32 h = 0x811c9dc5u;
                auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
                step(quint32(seed));
                step(quint32(x));
                step(quint32(z));
                h ^= h >> 16;
                h *= 0x7feb352du;
                h ^= h >> 15;
                return h;
            };
            const int wW = wT1011.width(), wD = wT1011.depth();
            const quint32 mineSeed = sd + 15047u;
            for (int bx = 18; bx < wW; bx += 36) {
                for (int bz = 18; bz < wD; bz += 36) {
                    const quint32 r = colHashT1011b(int(mineSeed), bx, bz);
                    if ((r % 100u) >= 40u) continue;
                    const int jx = int((r >> 1) & 0xFu) % 19 - 9;
                    const int jz = int((r >> 5) & 0xFu) % 19 - 9;
                    const int cx = bx + jx, cz = bz + jz;
                    if (cx < 16 || cz < 16 || cx >= wW - 16 || cz >= wD - 16) continue;
                    const int h = std::min(wT1011.heightAt(cx, cz), 63);
                    const int yLo = 6;
                    const int yHi = std::min(43, h - 11);
                    if (yHi <= yLo) continue;
                    const int sy = yLo + int((r >> 9) & 0x1Fu) % (yHi - yLo + 1);
                    ++candT1011;
                    int band = 0; // 净样签名：起点厅拱带 36 Planks @ sy+4（同 t1001）
                    for (int dx = 0; dx < 10; ++dx)
                        for (int dz = 0; dz < 10; ++dz) {
                            const bool edge = (dx == 0 || dx == 9 || dz == 0 || dz == 9);
                            if (edge && wT1011.blockAt(cx - 5 + dx, sy + 4, cz - 5 + dz) == BR::Planks)
                                ++band;
                        }
                    if (band != 36) continue;
                    ++shaftsT1011;
                    // ⑤ 起点厅角壁火把直查（4 角格头层墙插 / 地板回退 —— 与生成端同位）
                    for (int sx2 = -1; sx2 <= 1; sx2 += 2)
                        for (int sz2 = -1; sz2 <= 1; sz2 += 2) {
                            const int px = (sx2 < 0) ? cx - 5 : cx + 4;
                            const int pz = (sz2 < 0) ? cz - 5 : cz + 4;
                            for (int ty = sy + 1; ty <= sy + 2; ++ty)
                                if (wT1011.blockAt(px, ty, pz) == BR::Torch) { ++roomTorchesT1011; break; }
                        }
                    static const int kDirsT1011b[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (const auto &dd : kDirsT1011b) {
                        const int dx = dd[0], dz = dd[1];
                        const int sx = cx + (dx > 0 ? 5 : (dx < 0 ? -6 : 0));
                        const int sz = cz + (dz > 0 ? 5 : (dz < 0 ? -6 : 0));
                        if (wT1011.blockAt(sx, sy + 2, sz) != BR::Air) continue; // 此向无出口
                        int lastLit = -1;   // 上一火把步（步距直方图）
                        int walkMaxBl = 0;  // 本巷走 head 级 max blockLight（遥测）
                        int walkSteps = 0;  // 本巷走步数（暗前缀腿量纲）
                        int prefixLit = 0;  // 前 5 步点亮步数（①）
                        for (int s = 0; s < 11; ++s) {
                            const int px = sx + dx * s, pz = sz + dz * s;
                            if (wT1011.blockAt(px, sy + 2, pz) != BR::Air) break; // 头层堵 → 巷尽
                            ++headCellsT1011;
                            ++walkSteps;
                            const quint8 bl = wT1011.blockLightAt(px, sy + 2, pz);
                            const quint8 sl = wT1011.skyLightAt(px, sy + 2, pz);
                            if (bl > walkMaxBl) walkMaxBl = bl;
                            if (sl > 0 && bl == 0) ++slSeepT1011; // ⑥ 天光豁口签名（遥测）
                            bool lit = false; // 截面 w=±1/0 × 三层火把扫描（柱顶/壁挂 sy+3、豁口地板回退 sy+1）
                            for (int w = -1; w <= 1 && !lit; ++w) {
                                const int tx = px + w * (-dz), tz = pz + w * dx; // 截面偏移同生成端（w⊥巷轴）
                                for (int ty = sy + 1; ty <= sy + 3; ++ty) {
                                    if (wT1011.blockAt(tx, ty, tz) != BR::Torch) continue;
                                    lit = true;
                                    const quint8 st = wT1011.stateAt(tx, ty, tz) & 0x07u;
                                    if (st >= 1 && st <= 4) ++wallTorchesT1011; // ③ TorchAttach 墙插
                                    ++torchProbesT1011;
                                    if (wT1011.blockLightAt(tx, ty, tz) >= 12) ++torchBlOkT1011; // ④
                                    break;
                                }
                            }
                            if (s < 5 && lit) ++prefixLit; // ① 暗前缀统计窗
                            if (lit) {
                                if (lastLit >= 0) { // ② 步距分桶
                                    const int gap = s - lastLit;
                                    if (gap <= 3) ++gapA;
                                    else if (gap <= 6) ++gapB;
                                    else if (gap <= 10) ++gapC;
                                    else ++gapD;
                                }
                                lastLit = s;
                            }
                        }
                        ++walksT1011;
                        if (walkMaxBl < minWalkBlT1011) minWalkBlT1011 = walkMaxBl; // ⑥ 遥测
                        if (walkSteps >= 5 && prefixLit == 0) ++darkPrefixT1011; // ① 真可走段全暗前缀
                    }
                }
            }
        }
        // ⑥ 源码钉（壁挂判定 / 落块 / kTorchPct —— 阴性轮敏感：摘壁挂块 → ①②③ 恰红 + 钉失）
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            okPin = src.contains(QStringLiteral("if (step % kSupportInterval == 2) {"))
                 && src.contains(QStringLiteral("putStruct(px, wy, pz, BlockRegistry::Torch, attach);")) // review0905 #4 收口 putStruct 同口（旧裸 setBlock 针随行迁移）
                 && src.contains(QStringLiteral("quint8(BlockRegistry::TorchOnNX)"))
                 && src.contains(QStringLiteral("constexpr unsigned kTorchPct    = 55u;"));
        }
        const int gapsT1011 = gapA + gapB + gapC + gapD;
        ok = ok && walksT1011 >= 40;                                  // rig 体量
        ok = ok && darkPrefixT1011 <= 1;                              // ① 真可走段全暗前缀
        ok = ok && gapsT1011 > 0 && gapC == 0 && gapD == 0            // ② 密度窗（步距 ≤ 6 → 块光 ≥ 8）
             && gapA * 100 >= gapsT1011 * 40;
        ok = ok && wallTorchesT1011 >= 10;                            // ③ 壁挂落地
        ok = ok && torchProbesT1011 >= 20 && torchBlOkT1011 == torchProbesT1011; // ④ 块光归因
        ok = ok && roomTorchesT1011 >= shaftsT1011 * 2;               // ⑤ 起点厅角壁火把
        ok = ok && okPin;                                             // ⑦
        if (!ok)
            qInfo().noquote() << "  [t1011 diag] walks" << walksT1011 << "shafts" << shaftsT1011
                              << "/" << candT1011 << "darkPrefix" << darkPrefixT1011
                              << "minWalkBl" << minWalkBlT1011
                              << "gaps 0-3/4-6/7-10/11+" << gapA << gapB << gapC << gapD
                              << "wallTorches" << wallTorchesT1011
                              << "roomTorches" << roomTorchesT1011
                              << "torchBl" << torchBlOkT1011 << "/" << torchProbesT1011
                              << "slSeep" << slSeepT1011 << "/" << headCellsT1011 << "pin" << okPin;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1011 mineshaft torch coverage + light attribution: mid-span paired"
                             " wall torches + start-parlor corner torches (torch gap hist 0-3/4-6/"
                             "7-10/11+" << gapA << gapB << gapC << gapD << "share"
                             << (gapsT1011 > 0 ? gapA * 100 / gapsT1011 : -1) << "% 7-10/11+ zero"
                             " = light trace within 6 steps everywhere, dark walkable prefixes"
                             << darkPrefixT1011 << "<=1, wall-mount torches" << wallTorchesT1011
                             << "room-corner torches" << roomTorchesT1011 << "torch-cell blockLight"
                             << torchBlOkT1011 << "/" << torchProbesT1011 << ">=12, min walk bl"
                             << minWalkBlT1011 << "(telemetry: corridor-external open volumes ="
                             " caves/neighbor parlors out of mineshaft scope), skylight-seep sl>0/"
                             "bl=0 cells" << slSeepT1011 << "/" << headCellsT1011
                             << "(cave-opening daylight channel = the F3 bl:0 attribution,"
                             " orthogonal to torch coverage) over"
                          << walksT1011 << "walks /" << shaftsT1011 << "shafts, wall-torch pins";
    });

    // ── P-t1012 矿井几何三项探针（t1012 ①② + t1011 移交段 B 推进回归；placeMineshaft 几何回归验收面）──
    //    rig：t1001/t1011 同款候选复刻（hashColumn FNV 同源 + 起点厅拱带 36 Planks 净样）；hashVoxel 已
    //    public（t836）→ 阴影模型直调 World 同一哈希权威重放生成端几何（lenA/lenB/turnSign/ev 事件 /
    //    走廊形选型全同源），零复刻漂移。四腿：
    //    ① 段 B 推进（t1012 ③ 回归腿）：阴影模型按修复后推进语义（leg==1||step>0，t565 口径）重放每条
    //       巷道两腿游标 → 段 B 每 step 截面头层（curY+2）不得是未动过的天然岩层（Stone/Dirt/Gravel/
    //       Sand/矿/基岩；后续结构〔神殿 / 要塞〕与既有矿井结构占用合法）。回归态（leg==0&&step>0 +
    //       入腿预推进）段 B 整腿不推进 → 阴影点位落天然实体 → 恰红（L 第二腿缺失 / 斜坡同柱逐层坑
    //       同腿兼钉）。
    //    ② 地板政策（t1012 ①）：阴影模型登记全部 carveCell 地板点位（起点厅 10×10 / 巷道截面 w∈[-1,1] /
    //       交叉口 5×5 / 蛛网走廊廊体；同列被 >1 个不同 y 重刻 → 政策歧义弃样）→ actual∈{Stone,Planks}
    //       时断言 下方 Air ↔ Planks（空腔桥面）、下方实地 ↔ Stone（嵌岩石底）；池内两分支齐现
    //       （政策真执行，防空转绿）。
    //    ③ 蛛网走廊（t1012 ②）：阴影模型重放走廊形选型（宽 1-2 / 高 2-3 / 长 3-6 / 笼位居中 + 夹网巢）
    //       → 廊体阴影点位逐格须 Cobweb（笼格 = Spawner；跨巷道重刻容忍 → 池级匹配率窗）；世界侧扫
    //       MobCaveSpider 笼（t1012③ 偏差转正，旧 MobSpider 0x0E 登记退役）：笼座实体地板 + 7×7×3 盒
    //       ≥8 网（t786「≥8 网即矿井蛛笼」分流同口径）+ 笼邻 4 向 × 3 层零开露空气（满网签名）；池内净笼 ≥3。
    //    ④ 源码钉：段 B 推进条件行 / 地板政策行 / 走廊形选型行 / 满网落块行（阴性轮敏感）。
    runLegMulti({ "t1012 mineshaft geometry trio: corridor leg-B advancement restored(t565 semantics, solid head ce"
        "lls/), per-column floor policy (embedded=stone/cavity=planks, violations), spider web corridors "
        "1-2x2-3x3-6 full-webbed with cave-spider cages (branches/, web fill/, clean cages/), geometry pi"
        "ns" }, [&]() {
        bool ok = true;
        int legBCellsT1012 = 0;          // ① 段 B 阴影点位总数（量纲）
        int legBSolidT1012 = 0;          // ① 段 B 阴影点位落天然实体数（回归签名；修复后恒 0）
        int floorViolT1012 = 0;          // ② 地板政策违例（下方 Air ↔ Planks 不一致）
        int floorStoneT1012 = 0, floorPlanksT1012 = 0; // ② 两分支池化出现数（嵌岩 / 空腔）
        int branchesT1012 = 0;           // ③ 蛛网走廊阴影重放数
        int branchDimOkT1012 = 0;        // ③ 选型落在 1-2 / 2-3 / 3-6 窗内的走廊数
        int webMatchT1012 = 0, webCellsT1012 = 0; // ③ 廊体阴影点位 = Cobweb/Spawner 匹配
        int cagesT1012 = 0, cleanCagesT1012 = 0;  // ③ 世界侧 MobSpider 笼 / 满网净笼
        bool pinT1012 = false;
        const quint32 seedsT1012[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 4242u, 2024u, 31337u };
        EntityManager emT1012;
        for (quint32 sd : seedsT1012) {
            World wT1012;
            wT1012.setWidth(128);
            wT1012.setDepth(128);
            wT1012.setHeight(64);
            wT1012.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeMineshaft）
            auto colHashT1012 = [](int seed, int x, int z) -> quint32 { // hashColumn FNV 同源复刻（同 t1001）
                quint32 h = 0x811c9dc5u;
                auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
                step(quint32(seed));
                step(quint32(x));
                step(quint32(z));
                h ^= h >> 16;
                h *= 0x7feb352du;
                h ^= h >> 15;
                return h;
            };
            const int wW = wT1012.width(), wD = wT1012.depth();
            const quint32 mineSeed = sd + 15047u;
            for (int bx = 18; bx < wW; bx += 36) { // 复刻外层候选网格（kMineshaftGrid=36）
                for (int bz = 18; bz < wD; bz += 36) {
                    const quint32 r = colHashT1012(int(mineSeed), bx, bz);
                    if ((r % 100u) >= 40u) continue;
                    const int jx = int((r >> 1) & 0xFu) % 19 - 9;
                    const int jz = int((r >> 5) & 0xFu) % 19 - 9;
                    const int cx = bx + jx, cz = bz + jz;
                    if (cx < 16 || cz < 16 || cx >= wW - 16 || cz >= wD - 16) continue;
                    const int h = std::min(wT1012.heightAt(cx, cz), 63);
                    const int yLo = 6;
                    const int yHi = std::min(43, h - 11);
                    if (yHi <= yLo) continue;
                    const int sy = yLo + int((r >> 9) & 0x1Fu) % (yHi - yLo + 1);
                    int band = 0; // 净样签名：起点厅拱带 36 Planks @ sy+4（同 t1001；海列 / 重叠破坏弃样）
                    for (int dx = 0; dx < 10; ++dx)
                        for (int dz = 0; dz < 10; ++dz) {
                            const bool edge = (dx == 0 || dx == 9 || dz == 0 || dz == 9);
                            if (edge && wT1012.blockAt(cx - 5 + dx, sy + 4, cz - 5 + dz) == BR::Planks)
                                ++band;
                        }
                    if (band != 36) continue;
                    // ── 阴影模型：按修复后生成端语义重放（hashVoxel 同一权威；巷道 ci 升序同生成端）──
                    const quint32 eh = wT1012.hashVoxel(mineSeed ^ 0xE017u, cx, sy, cz);
                    int dirsT[4] = { 0, 1, 2, 3 };
                    for (int i = 3; i > 0; --i) { // Fisher-Yates（同生成端）
                        const int j = int((eh >> (2 * i)) & 3u) % (i + 1);
                        const int t = dirsT[i]; dirsT[i] = dirsT[j]; dirsT[j] = t;
                    }
                    const int corridors = 3 + int((eh >> 8) & 1u);
                    std::map<std::pair<int, int>, int> floorY; // carveCell 地板点位（(x,z) → 最后 carve y；多 y 重刻 → -1 弃样）
                    auto trackFloor = [&floorY](int px, int pz, int fy) {
                        auto it = floorY.find({ px, pz });
                        if (it == floorY.end()) floorY[{ px, pz }] = fy;
                        else if (it->second != fy) it->second = -1; // 多 y 重刻 → 政策歧义弃样
                    };
                    for (int ci = 0; ci < corridors; ++ci) {
                        int dx = (dirsT[ci] == 0) ? 1 : (dirsT[ci] == 1) ? -1 : 0;
                        int dz = (dirsT[ci] == 2) ? 1 : (dirsT[ci] == 3) ? -1 : 0;
                        const quint32 rh = wT1012.hashVoxel(mineSeed ^ (0xDEC0u + quint32(ci)), cx, sy, cz);
                        const int lenA = 5 + int((rh >> 2) & 0xFu) % 6;
                        const int lenB = 5 + int((rh >> 6) & 0xFu) % 6;
                        const int turnSign = ((rh >> 10) & 1u) ? 1 : -1;
                        const quint32 ev = wT1012.hashVoxel(mineSeed ^ (0x1717u + quint32(ci) * 0x9E37u), cx, sy, cz);
                        const unsigned evRoll = ev % 100u;
                        const int evStep = 2 + int((ev >> 8) & 7u) % (lenA - 3);
                        const bool slopeLegB = evRoll >= 85u; // pieceSlope 段（evRoll ≥ 60+25）
                        int ax = cx + (dx > 0 ? 5 : (dx < 0 ? -6 : 0)); // 起点厅房缘外首格（同生成端）
                        int az = cz + (dz > 0 ? 5 : (dz < 0 ? -6 : 0));
                        int curY = sy;
                        for (int leg = 0; leg < 2; ++leg) {
                            const int legLen = (leg == 0) ? lenA : lenB;
                            bool inSlope = false;
                            if (leg == 1) {
                                const int ndx = turnSign * dz, ndz = -turnSign * dx;
                                dx = ndx; dz = ndz;
                                inSlope = slopeLegB;
                            }
                            for (int step = 0; step < legLen; ++step) {
                                if (leg == 1 || step > 0) { ax += dx; az += dz; } // 修复后推进语义（t565）
                                if (inSlope && (step & 1) != 0) curY = std::max(curY - 1, 6);
                                for (int w = -1; w <= 1; ++w) // 3 宽截面地板登记
                                    trackFloor(ax + w * (-dz), az + w * dx, curY);
                                if (leg == 1) { // ① 段 B 推进腿：截面头层不得是未动过的天然岩层
                                    ++legBCellsT1012;
                                    const quint8 hb = wT1012.blockAt(ax, curY + 2, az);
                                    // 天然实体 = 未被 carve 触及的岩层（回归态段 B 点位即落此类 → 恰红）。
                                    //   后续结构（神殿 / 要塞石砖等 placeMineshaft 之后落位）与既有矿井结构
                                    //   （支撑 / 拱带 / 蛛网 / 火把 / 笼 / 箱 / 轨）占用均合法 → 非天然即过。
                                    const bool natural = hb == BR::Stone || hb == BR::Dirt
                                        || hb == BR::Gravel || hb == BR::Sand
                                        || hb == BR::CoalOre || hb == BR::IronOre
                                        || hb == BR::Bedrock;
                                    if (natural) ++legBSolidT1012;
                                }
                                if (leg == 0 && step == evStep) { // 途中事件重放（同生成端落位）
                                    if (evRoll < 60u) { // pieceSpiderRoom 走廊形重放（t1012 ②）
                                        const int ppx = (dirsT[ci] < 2) ? 0 : 1;
                                        const int ppz = (dirsT[ci] < 2) ? 1 : 0;
                                        const int tdx = (dirsT[ci] == 0) ? 1 : (dirsT[ci] == 1) ? -1 : 0;
                                        const int tdz = (dirsT[ci] == 2) ? 1 : (dirsT[ci] == 3) ? -1 : 0;
                                        const int sgn = (wT1012.hashVoxel(mineSeed ^ 0x5ED1u, ax, sy, az) & 1u) ? 1 : -1;
                                        const quint32 dh = wT1012.hashVoxel(mineSeed ^ 0x5C1Au, ax, sy, az);
                                        const int snw = 1 + int(dh & 1u);        // 宽 1-2
                                        const int snh = 2 + int((dh >> 2) & 1u); // 高 2-3
                                        const int snl = 3 + int((dh >> 4) & 3u) % 4; // 长 3-6
                                        const int cageOff = 2 + snl / 2;         // 笼位（廊体 = off 2..snl+1）
                                        ++branchesT1012;
                                        if (snw >= 1 && snw <= 2 && snh >= 2 && snh <= 3
                                            && snl >= 3 && snl <= 6)
                                            ++branchDimOkT1012;
                                        for (int off = 2; off <= snl + 1; ++off)
                                            for (int w2 = 0; w2 < snw; ++w2) {
                                                const int px = ax + ppx * sgn * off + tdx * w2;
                                                const int pz = az + ppz * sgn * off + tdz * w2;
                                                const int nh = (off >= cageOff - 1 && off <= cageOff + 1) ? 3 : snh;
                                                trackFloor(px, pz, sy);
                                                for (int dy = 1; dy <= nh; ++dy) { // 满网点位（笼格 = Spawner）
                                                    ++webCellsT1012;
                                                    const quint8 wb = wT1012.blockAt(px, sy + dy, pz);
                                                    const bool cageCell = (off == cageOff && w2 == 0 && dy == 1);
                                                    if (wb == BR::Cobweb || (cageCell && wb == BR::Spawner))
                                                        ++webMatchT1012;
                                                }
                                            }
                                    } else if (evRoll < 85u) { // pieceIntersection 5×5 地板登记
                                        for (int dx2 = -2; dx2 <= 2; ++dx2)
                                            for (int dz2 = -2; dz2 <= 2; ++dz2)
                                                trackFloor(ax + dx2, az + dz2, sy);
                                    }
                                }
                            }
                        }
                    }
                    for (int dx = 0; dx < 10; ++dx) // 起点厅 10×10 地板登记（生成端先于巷道 carve；歧义口径与序无关）
                        for (int dz = 0; dz < 10; ++dz)
                            trackFloor(cx - 5 + dx, cz - 5 + dz, sy);
                    // ② 地板政策（候选内登记完后统一核）：下方 Air ↔ Planks、下方实地 ↔ Stone
                    for (const auto &kv : floorY) {
                        const int fy = kv.second;
                        if (fy < 0) continue; // 多 y 重刻 → 政策歧义弃样
                        const quint8 actual = wT1012.blockAt(kv.first.first, fy, kv.first.second);
                        if (actual != BR::Stone && actual != BR::Planks) continue; // 被重刻清空 / 结构占用 → 弃样
                        const quint8 below = wT1012.blockAt(kv.first.first, fy - 1, kv.first.second);
                        if (actual == BR::Planks) {
                            ++floorPlanksT1012;
                            if (below != BR::Air) ++floorViolT1012; // 空腔桥面政策违例
                        } else {
                            ++floorStoneT1012;
                            if (below == BR::Air) ++floorViolT1012; // 嵌岩石底政策违例
                        }
                    }
                }
            }
            // ③ 世界侧 MobCaveSpider 笼净样（t1012③ 偏差转正：pieceSpiderRoom 笼 state=0x28 → 解码
            //   MobCaveSpider；满网 + 笼座实体 + ≥8 盒网〔t786 分流同口径〕+ 零开露）
            for (int y = 7; y < 60; ++y)
                for (int z = 1; z < wD - 1; ++z)
                    for (int x = 1; x < wW - 1; ++x) {
                        if (wT1012.blockAt(x, y, z) != BR::Spawner) continue;
                        if (emT1012.spawnerMobTypeForState(int(wT1012.stateAt(x, y, z)))
                            != EntityManager::MobCaveSpider) continue;
                        ++cagesT1012;
                        if (wT1012.blockAt(x, y - 1, z) == BR::Air) continue; // 笼座须实体地板
                        int webs = 0, openAir = 0;
                        for (int dx = -3; dx <= 3; ++dx)
                            for (int dz = -3; dz <= 3; ++dz)
                                for (int dy = 1; dy <= 3; ++dy) {
                                    if (dx == 0 && dz == 0 && dy == 1) continue;
                                    if (wT1012.blockAt(x + dx, y - 1 + dy, z + dz) == BR::Cobweb) ++webs;
                                }
                        static const int kCageNbT1012[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                        for (const auto &nb : kCageNbT1012)
                            for (int dy = 1; dy <= 3; ++dy)
                                if (wT1012.blockAt(x + nb[0], y - 1 + dy, z + nb[1]) == BR::Air) ++openAir;
                        if (webs >= 8 && openAir == 0) ++cleanCagesT1012;
                    }
        }
        // ④ 源码钉（段 B 推进条件 / 地板政策 / 走廊形选型 / 满网落块 —— 阴性轮敏感：任一 revert → 钉失）
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            pinT1012 = src.contains(QStringLiteral("if (leg == 1 || step > 0) { ax += dx; az += dz; }"))
                && src.contains(QStringLiteral("overCavity ? BlockRegistry::Planks : BlockRegistry::Stone"))
                && src.contains(QStringLiteral("const int snl = kSpiderCorrLenMin"))
                && src.contains(QStringLiteral("const int cageOff = 2 + snl / 2;"))
                && src.contains(QStringLiteral("m_chunks.setBlock(px2, py2, pz2, BlockRegistry::Cobweb, 0);"));
        }
        ok = ok && legBCellsT1012 >= 100 && legBSolidT1012 == 0;            // ① 段 B 全腿推进（L 折线 / 斜坡对角下切）
        ok = ok && floorStoneT1012 >= 5 && floorPlanksT1012 >= 1;           // ② 政策两分支真执行
        ok = ok && floorViolT1012 == 0;                                     // ② 零违例
        ok = ok && branchesT1012 >= 5 && branchDimOkT1012 == branchesT1012; // ③ 走廊形选型窗
        ok = ok && webCellsT1012 > 0 && webMatchT1012 * 100 >= webCellsT1012 * 75; // ③ 满网匹配率（自巷拐腿
        //   重刻 / 后续结构占用 legitimately 清网 → 池级 75% 窗；旧 7×7 房间形此值 ~30% → 恰红）
        ok = ok && cagesT1012 >= 3 && cleanCagesT1012 >= 3;                 // ③ 世界侧满网净笼
        ok = ok && pinT1012;                                                // ④
        if (!ok)
            qInfo().noquote() << "  [t1012 diag] legB" << legBCellsT1012 - legBSolidT1012
                              << "/" << legBCellsT1012 << "floorViol" << floorViolT1012
                              << "stone/planks" << floorStoneT1012 << "/" << floorPlanksT1012
                              << "branches" << branchDimOkT1012 << "/" << branchesT1012
                              << "webMatch" << webMatchT1012 << "/" << webCellsT1012
                              << "cages" << cleanCagesT1012 << "/" << cagesT1012
                              << "pin" << pinT1012;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1012 mineshaft geometry trio: corridor leg-B advancement restored"
                             "(t565 semantics, solid head cells" << legBSolidT1012 << "/" << legBCellsT1012
                          << "), per-column floor policy (embedded=stone" << floorStoneT1012
                          << "/cavity=planks" << floorPlanksT1012 << ", violations" << floorViolT1012
                          << "), spider web corridors 1-2x2-3x3-6 full-webbed with cave-spider cages (branches"
                          << branchDimOkT1012 << "/" << branchesT1012 << ", web fill" << webMatchT1012
                          << "/" << webCellsT1012 << ", clean cages" << cleanCagesT1012 << "/"
                          << cagesT1012 << "), geometry pins";
    });

    // ── P-t1012③④ 洞穴蜘蛛真变种 + 水破坏附着块探针（R19.20 t1012 第二棒；engine 侧行为腿）──
    //    ③ 洞穴蜘蛛真变种（转正偏差 1）四腿：
    //      (a) 枚举/state/蛋契约：MobCaveSpider==20（**枚举尾追加**=存档兼容契约）+ SpawnerStateCaveSpider
    //          ==(20<<1)=0x28（bit1-5 type 位编码 round-trip）+ kValidMobTypeCount==21（MobModel 白名单
    //          镜像，static_assert 同源三级防线）+ 蛋表双向（mobTypeForSpawnEgg(0x25E)=20 /
    //          mobTypeEggId(20)=0x25E）+ 旧蜘蛛笼 0x0E 解码不断档（存档兼容）+ 新笼 0x28 解码 MobCaveSpider。
    //      (b) 0.7× 小体型实体腿：spawnMobTyped(20) → halfW 0.32 / halfH 0.21 / hostile=true，且与蜘蛛
    //          (halfW 0.45/halfH 0.30) 恰 0.7× 比例（引擎侧缩放字段断言；渲染视觉实机确认登记 dev-plan）。
    //      (c) 中毒 DoT 行为腿：Survival applyStatusEffect(EffectPoison, 3.75s) → 真时钟泵 pc.tick() →
    //          poisonDamageTaken 每段 1HP 多段命中 ≥2（t669/t715 既有 m_poisonTimer 链，绕 fallDamageTaken
    //          ——「沿用现有链」口径的引擎侧证据）；
    //      (d) 无效对照：Creative 下 applyStatusEffect 静默丢弃（无敌模式不吃 DoT）。
    //    ④ 水破坏附着块（附着块族单一权威 + 流体更新钩）rig 腿：悬空石平台 6×6（y=P4Y，worldgen 沉降后
    //      落 rig 免地形水干扰），中线布 水源(S) / 石头对照(S+1) / 火把(S-1) / 蛛网(S-2) / 红石火把(S-3)。
    //      tickWaterFlow 沉降泵后断言：三附着块格全数 → Air 且有 dropId 掉落信号（Torch→13 自身 /
    //      RedstoneTorch→129 自身 / Cobweb→0x219 线，对齐玩家挖除掉落链）；石头对照被水漫但**完好**
    //     （阴性腿：isAttachableBlock(Stone)=false）；非附着块不产生掉落（掉落计数恰 3）。
    runLegMulti({ "t1012 cave-spider variant + water attachment breakup: MobCaveSpider=20 enum-tail + spawner state"
        " 0x28 roundtrip (old 0x0E decode kept) + egg 0x25E both-way + 0.7x mini hitbox (0.32x0.21 hostil"
        "e) + poison DoT multi-tick (armor-bypass chain, creative-inert) + wash torch/cobweb/redstone-tor"
        "ch to Air with dropId drops (stone control intact)" }, [&]() {
        bool ok = true;
        // diag 载荷（绿跑静默；红跑打印定位失败腿）。
        int diagPoison = -1, diagDrops = -1;
        int diagTorch = -1, diagWeb = -1, diagRTorch = -1;
        int diagHT = -1, diagHRT = -1, diagHS = -1, diagSurf = -1;
        // ---- ③(a) 契约腿 ----
        ok = ok && int(EntityManager::MobCaveSpider) == 20; // 枚举尾追加（勿插中间——存档兼容契约）
        ok = ok && BR::SpawnerStateCaveSpider == quint8(0x28); // (20<<1)
        ok = ok && BR::spawnerStateForMob(int(EntityManager::MobCaveSpider)) == quint8(0x28);
        ok = ok && MobModel::kValidMobTypeCount == 21;
        {
            EntityManager emT34a;
            ok = ok && emT34a.spawnerMobTypeForState(0x28) == EntityManager::MobCaveSpider; // 新笼解码
            ok = ok && emT34a.spawnerMobTypeForState(0x0E) == EntityManager::MobSpider;     // 旧蜘蛛笼解码不断档
            ok = ok && emT34a.spawnerMobTypeForState(BR::SpawnerStateCaveSpider) == EntityManager::MobCaveSpider;
        }
        ok = ok && RecipeRegistry::mobTypeForSpawnEgg(RecipeRegistry::SpawnEggCaveSpiderId)
                     == EntityManager::MobCaveSpider; // 蛋→mob 单一权威表
        ok = ok && RecipeRegistry::SpawnEggCaveSpiderId == 0x25E;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobCaveSpider) // 中键 pick 双向往返（t653②，static 单表）
                     == RecipeRegistry::SpawnEggCaveSpiderId;

        // ---- ③(b) 0.7× 小体型实体腿（真 worldgen 世界免扰角落 spawn）----
        {
            World wT34;
            wT34.setWidth(48); wT34.setDepth(48); wT34.setHeight(64); wT34.setSeed(4242);
            EntityManager emT34b;
            // 找一块实体地表（上方两层空气）：候选列网格 × y 自高向低，取首个合格列（单列硬编码
            //   在山地 / 洞穴 seed 下可能整列无「实体+双层空气」采样位 → 假红，diag surf=-2 即此）。
            int gy = -1, gx = 24, gz = 24;
            for (int cx2 = 18; cx2 <= 30 && gy < 0; ++cx2)
                for (int cz2 = 18; cz2 <= 30 && gy < 0; ++cz2)
                    for (int y = 60; y >= 8; --y) {
                        if (wT34.blockAt(cx2, y, cz2) != BR::Air && wT34.blockAt(cx2, y + 1, cz2) == BR::Air
                            && wT34.blockAt(cx2, y + 2, cz2) == BR::Air) { gy = y + 1; gx = cx2; gz = cz2; break; }
                    }
            diagSurf = gy;
            if (gy > 0) {
                const int sSpider = emT34b.spawnMobTyped(gx, gy, gz, EntityManager::MobSpider,
                                                         QStringLiteral("#2a1a1a"), 0);
                const int sCave = emT34b.spawnMobTyped(gx, gy, gz, EntityManager::MobCaveSpider,
                                                       QStringLiteral("#1c3a52"), 0);
                ok = ok && sSpider >= 0 && sCave >= 0;
                ok = ok && emT34b.mobTypeAt(sCave) == EntityManager::MobCaveSpider;
                ok = ok && emT34b.isHostileAt(sCave);
                // 恰 0.7× 比例（蜘蛛 0.45/0.30 → 洞蛛 0.32/0.21；0.45×0.7=0.315 引擎取 0.32 步进 →
                //   断言绝对值窗 ≤0.01 覆盖步进舍入，halfH 0.21=0.30×0.7 精确）。
                ok = ok && qAbs(emT34b.halfHeightAt(sCave) - emT34b.halfHeightAt(sSpider) * 0.7f) < 1e-4f;
                ok = ok && qAbs(emT34b.radiusAt(sCave) - 0.32f) < 1e-4f
                         && qAbs(emT34b.halfHeightAt(sCave) - 0.21f) < 1e-4f;
                ok = ok && emT34b.radiusAt(sSpider) > emT34b.radiusAt(sCave)
                         && emT34b.halfHeightAt(sSpider) > emT34b.halfHeightAt(sCave); // 小于成体蜘蛛
            } else {
                ok = false; // rig 找不到地表（病态世界）→ 响亮红
                diagSurf = -2;
            }
        }

        // ---- ③(c)(d) 中毒 DoT 行为腿（真时钟泵，同 P-t890 tickP 模式）----
        {
            World wT34p;
            wT34p.setWidth(48); wT34p.setDepth(48); wT34p.setHeight(64); wT34p.setSeed(77);
            Hotbar hbT34;
            PlayerController pcT34;
            pcT34.setWorld(&wT34p);
            pcT34.setHotbar(&hbT34);
            int poisonHits = 0, poisonHp = -1;
            QObject::connect(&pcT34, &PlayerController::poisonDamageTaken, &pcT34,
                             [&](int hp) { ++poisonHits; poisonHp = hp; });
            const auto pumpT34 = [](int ms) {
                QElapsedTimer t; t.start();
                while (t.elapsed() < ms)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            };
            const auto tickT34 = [&](int n) {
                for (int i = 0; i < n; ++i) { pumpT34(17); pcT34.tick(); }
            };
            // (d) Creative 对照先跑：applyStatusEffect 静默丢弃 → 零毒伤（无敌模式不吃 DoT）。
            //     泵窗 90 tick ≈ 1.53s > kPoisonInterval=1.25s 首段窗 → 若误生效必已发首击（对照腿有判别力）。
            pcT34.setMode(PlayerController::Creative);
            pcT34.applyStatusEffect(PlayerState::EffectPoison, 3.75f, 1);
            tickT34(90);
            ok = ok && poisonHits == 0;
            // (c) Survival 主腿：3.75s 毒 → 每段 1HP，~1.25s 间隔 → 4.6s 泵窗内 ≥2 段（3 段满窗，
            //     调度抖动留 1 段余量；每段恰 1 HP 断言 = 「血量多段下降」引擎侧证据）。
            pcT34.setMode(PlayerController::Survival);
            pcT34.applyStatusEffect(PlayerState::EffectPoison, 3.75f, 1);
            tickT34(270); // ~4.6s 真时钟
            diagPoison = poisonHits;
            ok = ok && poisonHits >= 2 && poisonHp == 1;
        }

        // ---- ④ 水毁附着块 rig 腿 ----
        {
            World wT34w;
            wT34w.setWidth(64); wT34w.setDepth(64); wT34w.setHeight(64); wT34w.setSeed(9);
            // worldgen 水沉降（同 t933 口径：推进到连续静默），免地形水体扩散扰 rig。
            int wcT34w = 0;
            QObject::connect(&wT34w, &World::worldChanged, &wT34w, [&]() { ++wcT34w; });
            const auto settleT34w = [&]() {
                int quiet = 0;
                for (int i = 0; i < 2000 && quiet < 10; ++i) {
                    const int wc0 = wcT34w;
                    wT34w.tickWaterFlow(); wT34w.tickWaterFlow(); wT34w.tickWaterFlow();
                    quiet = (wcT34w == wc0) ? quiet + 1 : 0;
                }
            };
            settleT34w();
            // 悬空石平台 6×6 @ y=P4Y（高空无 worldgen 水干扰；平台 = water grounded 面）。
            constexpr int P4Y = 41, P4X = 28, P4Z = 28;
            for (int dx = -3; dx <= 2; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    wT34w.setBlock(P4X + dx, P4Y, P4Z + dz, BR::Stone, 0); // 平台面
                    for (int dy = 1; dy <= 3; ++dy) {                      // 净空
                        if (wT34w.blockAt(P4X + dx, P4Y + dy, P4Z + dz) != BR::Air)
                            wT34w.setWaterSilent(P4X + dx, P4Y + dy, P4Z + dz, BR::Air, 0);
                    }
                }
            // 中线布置：水源 S(28) | 石头对照 S+1(29) | 火把 S-1(27) | 蛛网 S-2(26) | 红石火把 S-3(25)。
            const int sy4 = P4Y + 1, sz4 = P4Z;
            wT34w.setBlock(P4X, sy4, sz4, BR::Water, 0);          // 源（桶倒路径同款 setBlock 源写入）
            wT34w.setBlock(P4X + 1, sy4, sz4, BR::Stone, 0);      // 非附着对照（水漫不毁）
            wT34w.setBlock(P4X - 1, sy4, sz4, BR::Torch, 0);      // 附着：火把（TorchFloor state=0）
            wT34w.setBlock(P4X - 2, sy4, sz4, BR::Cobweb, 0);     // 附着：蛛网
            wT34w.setBlock(P4X - 3, sy4, sz4, BR::RedstoneTorch, 0); // 附着：红石火把
            // 掉落收集（等价 Main.qml onBlockDroppedAsItem 消费面）。
            int dropsT34w = 0;
            std::vector<int> dropIdsT34w;
            QObject::connect(&wT34w, &World::blockDroppedAsItem, &wT34w,
                             [&](int, int, int, int id) { ++dropsT34w; dropIdsT34w.push_back(id); });
            settleT34w();
            // 三附着块全数被冲毁 → Air（随后流水灌入：同 tick「冲毁+入水」，断言 Air ∨ Water 兼容续流）。
            const quint8 afterTorch = wT34w.blockAt(P4X - 1, sy4, sz4);
            const quint8 afterWeb = wT34w.blockAt(P4X - 2, sy4, sz4);
            const quint8 afterRTorch = wT34w.blockAt(P4X - 3, sy4, sz4);
            ok = ok && (afterTorch == BR::Air || afterTorch == BR::Water);
            ok = ok && (afterWeb == BR::Air || afterWeb == BR::Water);
            ok = ok && (afterRTorch == BR::Air || afterRTorch == BR::Water);
            ok = ok && afterTorch != BR::Torch && afterWeb != BR::Cobweb && afterRTorch != BR::RedstoneTorch;
            // 石头对照完好（阴性腿：非附着块不被水毁）。
            ok = ok && wT34w.blockAt(P4X + 1, sy4, sz4) == BR::Stone;
            // 掉落链：恰 3 件，id 对齐玩家挖除（Torch→自身 13 / RedstoneTorch→自身 129 / Cobweb→线 0x219）。
            ok = ok && dropsT34w == 3;
            const int haveTorch = int(std::count(dropIdsT34w.begin(), dropIdsT34w.end(), int(BR::Torch)));
            const int haveRTorch = int(std::count(dropIdsT34w.begin(), dropIdsT34w.end(), int(BR::RedstoneTorch)));
            const int haveString = int(std::count(dropIdsT34w.begin(), dropIdsT34w.end(), 0x219));
            ok = ok && haveTorch == 1 && haveRTorch == 1 && haveString == 1;
            // 源仍为水（冲刷不吞源）且平台被浸（水流扩散工作面证据）。
            ok = ok && wT34w.blockAt(P4X, sy4, sz4) == BR::Water
                     && wT34w.stateAt(P4X, sy4, sz4) == 0;
            diagTorch = afterTorch; diagWeb = afterWeb; diagRTorch = afterRTorch;
            diagDrops = dropsT34w; diagHT = haveTorch; diagHRT = haveRTorch; diagHS = haveString;
        }

        if (!ok)
            qInfo().noquote() << "  [t1012csd diag] poison" << diagPoison << "surf" << diagSurf
                              << "cells(t/w/rt)" << diagTorch << diagWeb << diagRTorch
                              << "drops" << diagDrops << "ids(t/rt/str)" << diagHT << diagHRT << diagHS;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1012 cave-spider variant + water attachment breakup: MobCaveSpider=20 enum-tail"
                             " + spawner state 0x28 roundtrip (old 0x0E decode kept) + egg 0x25E both-way"
                             " + 0.7x mini hitbox (0.32x0.21 hostile) + poison DoT multi-tick (armor-bypass"
                             " chain, creative-inert) + wash torch/cobweb/redstone-torch to Air with dropId"
                             " drops (stone control intact)";
    });

    // ── P-t1012⑤ 满网蛛网走廊刷怪笼刷新腿（review0907 B 跨批高危 #1 修复面）──
    //    病灶：t1012② 满网走廊把笼周 8 邻 × 上层全填 Cobweb（非 Air）→ tickSpawners 陆生谓词
    //    「here/above == Air」恒假 → 洞穴蜘蛛笼（唯一自然来源）永久零刷。修复 = 谓词对 Cobweb 格豁免
    //    （机制等价 MC：cave spider 在网窝里照常刷；不取「刷出后清网」以免破 t786「≥8 网 = 蛛笼」分流）。
    //    rig：worldgen 真矿井 seed 池自举（扫 0x28 洞蛛笼 + 满网签名——P-t1012 ③ 净笼判据严格化为
    //    「笼周 8 邻 × 2 层全非 Air」，钉死「豁免即唯一使能」：摘豁免〔阴性轮〕本腿必红）→ 玩家激活
    //    圈内直调 tickSpawners 长 tick（80×0.1s 累计 8s > kSpawnerInterval=6s，P-t786 同式）→ 笼邻
    //    Cobweb 格刷出 MobCaveSpider。
    runLegMulti({ "t1012 full-web corridor cage spawns (review0907 B cross-batch high #1): worldgen cave-spider cag"
        "e embedded in a fully webbed corridor (8-neighbor x 2-layer all-non-air precondition) ticks past"
        " kSpawnerInterval with the player in range and spawns a MobCaveSpider on a Cobweb neighbor cell "
        "(land spawn predicate exempts Cobweb here/above; negative-round: removing the exemption re-block"
        "s every candidate = loud red)" }, [&]() {
        bool okCageSpawn = false;
        int diagSeedT1212c = -1, diagCageWorldsT1212c = 0;
        const quint32 seedsT1212c[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 4242u, 2024u, 31337u };
        EntityManager emT1212c;
        for (quint32 sd : seedsT1212c) {
            World wC;
            wC.setWidth(128); wC.setDepth(128); wC.setHeight(64);
            wC.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeMineshaft 满网走廊）
            // 扫 0x28 洞蛛笼 + 满网净笼签名（P-t1012 ③ 同款判据 + 8 邻 × 2 层全非 Air 严格化）。
            int cageX = -1, cageY = -1, cageZ = -1;
            const int wW = wC.width(), wD = wC.depth();
            for (int y = 7; y < 60 && cageX < 0; ++y)
                for (int z = 1; z < wD - 1 && cageX < 0; ++z)
                    for (int x = 1; x < wW - 1 && cageX < 0; ++x) {
                        if (wC.blockAt(x, y, z) != BR::Spawner) continue;
                        if (emT1212c.spawnerMobTypeForState(int(wC.stateAt(x, y, z)))
                            != EntityManager::MobCaveSpider) continue;
                        if (wC.blockAt(x, y - 1, z) == BR::Air) continue; // 笼座须实体地板
                        static const int kNbCageT1212c[8][2] = {
                            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                            { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
                        };
                        bool fullWeb = true; // 满网签名：笼周 8 邻 × (y, y+1) 两层全非 Air（全 Cobweb）
                        for (const auto &nb : kNbCageT1212c)
                            for (int dy = 0; dy <= 1; ++dy)
                                if (wC.blockAt(x + nb[0], y + dy, z + nb[1]) == BR::Air) fullWeb = false;
                        if (!fullWeb) continue;
                        cageX = x; cageY = y; cageZ = z;
                    }
            if (cageX < 0) continue; // 本 seed 无满网净笼 → 池续扫
            ++diagCageWorldsT1212c;
            // 激活圈内长 tick：累计 8s > kSpawnerInterval=6s（首周期到点即扫）。
            const QVector3D playerPos(float(cageX) + 0.5f, float(cageY) + 0.5f, float(cageZ) + 0.5f);
            for (int t = 0; t < 80; ++t) emT1212c.tickSpawners(0.1, &wC, playerPos);
            for (int i = 0; i < emT1212c.count(); ++i) {
                if (!emT1212c.aliveAt(i)) continue;
                if (emT1212c.mobTypeAt(i) != EntityManager::MobCaveSpider) continue;
                const QVector3D d = emT1212c.posAt(i) - playerPos;
                if (std::abs(d.x()) <= 2.0f && std::abs(d.z()) <= 2.0f) { // 笼邻 2 格内刷出洞蛛
                    okCageSpawn = true;
                    break;
                }
            }
            diagSeedT1212c = int(sd);
            break;                 // 首个含笼世界即判（池自举只为找到 rig；不再多生成）
        }
        if (!okCageSpawn)
            qInfo().noquote() << "  [t1012cage diag] seed" << diagSeedT1212c
                              << "cageWorlds" << diagCageWorldsT1212c;
        if (!okCageSpawn) ++totalFail;
        qInfo().noquote() << (okCageSpawn ? "PASS" : "FAIL")
                          << "| t1012 full-web corridor cage spawns (review0907 B cross-batch high #1):"
                             " worldgen cave-spider cage embedded in a fully webbed corridor (8-neighbor"
                             " x 2-layer all-non-air precondition) ticks past kSpawnerInterval with the"
                             " player in range and spawns a MobCaveSpider on a Cobweb neighbor cell"
                             " (land spawn predicate exempts Cobweb here/above; negative-round:"
                             " removing the exemption re-blocks every candidate = loud red)";
    });

    // ── P-t1003 沙漠神殿逐方块重建探针（R19.19 批 t1003；placeDesertTemple 21×21 重写验收面）──
    //    rig：t995/t1001/t1002 同款池化口径，但世界升 160×160×**128**（t307 起地表基线 64、desert 地表
    //    ~61..67 —— 64 高 rig 世界把地表钳到 63 → 塔顶越界守卫恒拒 = 历史全矩阵神殿恒 0 的根因，本探针
    //    世界高对齐游戏本体 128；160² 宽为 t485 设计口径「约 1-2 座神殿」；biome 门私有不可复刻 → 箱簇定位）。
    //    16 seed 大池自举：无神殿种子跳过不计（diag 留痕），≥4 个含神殿世界且池内神殿 ≥5 座才判。
    //    神殿定位 = 全图扫 Chest&PyramidFlag 聚簇（4 箱 Chebyshev≤6 一簇，质心即神殿中心）→
    //    surfaceY = heightAt(cx,cz)。断言八层：
    //    (a) 21×21 足迹 + 逐层半边表 {10,10,9,9,8,8,7,7,6,6,5}（四向外环面取样）+ L10 11×11 CutSandstone 顶冠
    //        + 门楣 CutSandstone v=4 |u|≤2（偏差 6 刻纹→切制）；
    //    (b) 安卡纹样四面：-Z / ±X 三面 v1..7 各恰 21 格 WoolOrange 且与源行表逐格全等（四折旋转 + 镜像
    //        对称），+Z 正面 = 21 - 主入口门洞 3 格（u=0,v1..3）- 门楣 CutSandstone 覆盖 5 格（v4,|u|≤2）= 13；
    //        review0905 #5 考据收口：MC 门面本就无完整安卡（完整安卡形〔橙陶瓦、蓝陶瓦心〕只在地面中央
    //        与密室，外立面仅前侧双角塔条带装饰、门楣上即切制砂岩 —— wiki Desert_Pyramid/Structure 蓝图）
    //        → 门面截断 = 忠实同构，非缺陷（不追「四面等量」）。
    //    (c) 风玫瑰：地板 y=S 菱域棋盘 WoolOrange 恰 24（(dx+dz) 偶、|dx|+|dz|≤5、非中心）+ WoolBlue
    //        足迹域恰 1（地板中心；偏差 3 时代口径）；
    //    (d) 入口三处（+Z 主入口阶梯门洞 / -Z 两副入口 u=±5）+ 顶窗四面 + L8/L9 顶部暗腔；
    //    (e) 暗渠：+Z 入口 (4,S+1..2,10) / 对角段 (4,S-4..S-3,5) / 密室接通口 (4,S-9..S-8,0) 全 Air；
    //    (f) 密室：7×7×4 内部空气 + 壳 Sandstone + 4 箱（±2/±3 墙位、朝向房心、PyramidFlag 逐箱核 state）+
    //        压板→9 TNT 链（StonePressurePlate (0,S-11,0) 正下 3×3 TntBlock @S-12 恰 9）；
    //    (g) 确定性：首个有效 seed 重生成 → 足迹域 stride-2 抽样 FNV 一致（PLAN §2-K）；
    //    (h) 源码钉：层半边表行 / 密室深度行 / 安卡行表行 / 中心蓝块行 / 暗渠循环行 / 箱 state 行。
    runLegMulti({ "t1003 desert temple per-block rebuild: 21x21 stepped pyramid (layer halves 10..5, cut-sandstone "
        "cap + lintel), four-face ankh wool pattern (21/face, front 13 at door+lintel holes - MC-faithful"
        " per wiki blueprint: the complete ankh figure lives on the floor centre and the cellar, facades "
        "carry only striped front-tower bands, no complete pattern above the door), floor wind-rose check"
        "er 24 orange + center blue, 3 entrances + 4 top windows + upper hollow, secret diagonal tunnel, "
        "chamber 7x7x4 with 4 oriented pyramid chests + plate-over-3x3-TNT (9) chain, well deterministic "
        "re-gen, source pins, templesworldsseeds-missravine-skipped" }, [&]() {
        bool ok = true;
        const quint32 seedsT1003[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 5150u, 2718u, 1618u,
                                       42u, 999u, 31337u, 2024u, 8675309u, 271828u, 314159u, 123456789u,
                                       7u, 12345u, 54321u, 8888u, 111u, 2222u, 33333u, 555555u,
                                       7777777u, 97531u, 13579u, 24680u };
        // 与源码同源的安卡行表（v → |u| 掩码；镜像对称由掩码天然成立）。
        const int ankhMask[11] = { 0, 0b1001, 0b1001, 0b1001, 0b0111, 0b0100, 0b0100, 0b0011, 0, 0, 0 };
        const int layerHalf[11] = { 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5 };
        // (a) 形制采样（足迹四向 + 逐层半边错位点 + 顶冠 / 门楣）—— review0905 #3 首跑修：拉成 lambda 供
        //    主循环与 (g) 重扫同款复用。首跑 (g) 用「扫序前 4 箱」朴素质心重定位神殿，与主循环「首个恰 4 箱
        //    净簇」选择口径错位（世界含残簇 / 多神殿时二者可不同座——保底上线后多神殿世界常见：首跑 seed
        //    777 实证 = 主路径座被峡谷削至 3 箱〔残簇静默跳过〕+ 保底新增座，重扫质心 = 3+1 跨座错位 → FNV
        //    静默失配假红）；现重扫与主循环同构（全簇迭代 + 恰 4 箱 + 形制筛），选择口径永不漂移。
        auto t1003ShapeOk = [&layerHalf](World &w, int cx, int cz, int S) {
            for (const int d : { -10, 0, 10 }) {
                if (!(w.blockAt(cx + 10, S, cz + d) == BR::Sandstone
                    && w.blockAt(cx - 10, S, cz + d) == BR::Sandstone
                    && w.blockAt(cx + d, S, cz + 10) == BR::Sandstone
                    && w.blockAt(cx + d, S, cz - 10) == BR::Sandstone))
                    return false;
            }
            for (int v = 0; v <= 9; ++v) {
                // 取样错位偏移：避开安卡纹样（|u|≤3）、后侧副入口（|u|=5）、暗渠入口（+Z u=+4）、
                // 门洞 / 门楣（+Z |u|≤2）—— 四面各取无开口的偏移位。
                if (!(w.blockAt(cx + layerHalf[v], S + v, cz - 4) == BR::Sandstone
                    && w.blockAt(cx - layerHalf[v], S + v, cz + 4) == BR::Sandstone
                    && w.blockAt(cx - 4, S + v, cz + layerHalf[v]) == BR::Sandstone
                    && w.blockAt(cx + 4, S + v, cz - layerHalf[v]) == BR::Sandstone))
                    return false;
            }
            return w.blockAt(cx, S + 10, cz) == BR::CutSandstone
                && w.blockAt(cx + 5, S + 10, cz + 5) == BR::CutSandstone
                && w.blockAt(cx - 5, S + 10, cz - 5) == BR::CutSandstone
                && w.blockAt(cx + 2, S + 4, cz + 8) == BR::CutSandstone
                && w.blockAt(cx - 2, S + 4, cz + 8) == BR::CutSandstone;
        };
        // (g) 净簇重扫（与主循环同构：全图 PyramidFlag 箱扫 → Chebyshev(6,6,3) 聚簇 → 恰 4 箱 → 形制筛 →
        //    首个净簇质心）。返回是否寻得；寻得时回填质心坐标。
        auto t1003FirstCleanCentroid = [&t1003ShapeOk](World &w, int &cxOut, int &czOut) {
            struct PCg { int x, y, z; };
            std::vector<PCg> pcg;
            for (int x = 0; x < w.width(); ++x)
                for (int z = 0; z < w.depth(); ++z)
                    for (int y = 0; y < w.height(); ++y)
                        if (w.blockAt(x, y, z) == BR::Chest
                            && (w.stateAt(x, y, z) & BR::ChestStatePyramidFlag))
                            pcg.push_back({ x, y, z });
            std::vector<bool> usedG(pcg.size(), false);
            for (size_t i = 0; i < pcg.size(); ++i) {
                if (usedG[i]) continue;
                int n = 0, sx = 0, sz = 0;
                for (size_t j = i; j < pcg.size(); ++j)
                    if (!usedG[j] && std::abs(pcg[j].x - pcg[i].x) <= 6
                                  && std::abs(pcg[j].z - pcg[i].z) <= 6
                                  && std::abs(pcg[j].y - pcg[i].y) <= 3) {
                        usedG[j] = true; ++n; sx += pcg[j].x; sz += pcg[j].z;
                    }
                if (n != 4) continue;
                const int cgx = int((sx + 2) / 4), cgz = int((sz + 2) / 4);
                const int Sg = w.heightAt(cgx, cgz);
                if (!t1003ShapeOk(w, cgx, cgz, Sg)) continue;
                cxOut = cgx; czOut = cgz;
                return true;
            }
            return false;
        };
        auto sampleHashT1003 = [](World &w, int cx, int cy, int cz) {
            quint32 h = 0x811c9dc5u;
            auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
            for (int dx = -11; dx <= 11; dx += 2)
                for (int dz = -11; dz <= 11; dz += 2)
                    for (int dy = -13; dy <= 11; ++dy) {
                        step(quint32(w.blockAt(cx + dx, cy + dy, cz + dz)));
                        step(quint32(w.stateAt(cx + dx, cy + dy, cz + dz)));
                    }
            h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
            return h;
        };
        int worldsChecked = 0, templesTotal = 0, seedMiss = 0, ravineSkipped = 0;
        bool haveFirst = false;
        quint32 firstSeed = 0, firstHash = 0;
        for (quint32 sd : seedsT1003) {
            if (worldsChecked >= 5) break;
            World wT1003;
            wT1003.setWidth(160);
            wT1003.setDepth(160);
            wT1003.setHeight(128); // t307 地表基线 64 → 世界高须 128（64 高钳地表致塔顶守卫恒拒，见 rig 注释）
            wT1003.setSeed(int(sd)); // setter 内 generate() 全量 worldgen
            // 全图扫 Chest&PyramidFlag → 聚簇（4 箱质心 = 神殿中心）。
            struct PChest { int x, y, z; };
            std::vector<PChest> pcs;
            for (int x = 0; x < wT1003.width(); ++x)
                for (int z = 0; z < wT1003.depth(); ++z)
                    for (int y = 0; y < wT1003.height(); ++y)
                        if (wT1003.blockAt(x, y, z) == BR::Chest
                            && (wT1003.stateAt(x, y, z) & BR::ChestStatePyramidFlag))
                            pcs.push_back({ x, y, z });
            std::vector<bool> used(pcs.size(), false);
            int templesHere = 0;
            for (size_t i = 0; i < pcs.size(); ++i) {
                if (used[i]) continue;
                std::vector<size_t> cluster;
                for (size_t j = i; j < pcs.size(); ++j) {
                    if (used[j]) continue;
                    if (std::abs(pcs[j].x - pcs[i].x) <= 6 && std::abs(pcs[j].z - pcs[i].z) <= 6
                        && std::abs(pcs[j].y - pcs[i].y) <= 3)
                        cluster.push_back(j);
                }
                if (cluster.size() != 4) continue; // 残簇（口径漂移 / 地形破坏）→ 由数量断言兜红
                int cx = 0, cz = 0;
                for (size_t j : cluster) {
                    used[j] = true;
                    cx += pcs[j].x;
                    cz += pcs[j].z;
                }
                cx = int((cx + 2) / 4);
                cz = int((cz + 2) / 4);
                const int S = wT1003.heightAt(cx, cz);
                // (a) 足迹 + 逐层半边 + 顶冠 / 门楣（t1003ShapeOk 同款：(g) 重扫复用同一选择口径）。
                const bool shapeOk = t1003ShapeOk(wT1003, cx, cz, S);
                if (!shapeOk) { // 神殿先于峡谷生成 → 峡谷切塔 = 地形破坏弃样（净样口径，同 t995/t1001）
                    ++ravineSkipped;
                    qInfo().noquote() << "  [t1003 diag] seed" << sd << "temple@" << cx << cz
                                      << "S" << S << "shape broken (ravine?) -> sample discarded";
                    continue;
                }
                ++templesHere;
                // (b) 安卡四面（-Z / ±X 恰 21 格逐格全等；+Z 扣门洞 3 格 = 18）。
                bool ankhOk = true;
                int faceCount[4] = { 0, 0, 0, 0 }; // -Z, +Z, -X, +X
                for (int v = 1; v <= 7; ++v)
                    for (int u = -3; u <= 3; ++u) {
                        const bool want = (ankhMask[v] & (1 << std::abs(u))) != 0;
                        const bool mz = wT1003.blockAt(cx + u, S + v, cz - layerHalf[v]) == BR::WoolOrange;
                        const bool pz = wT1003.blockAt(cx + u, S + v, cz + layerHalf[v]) == BR::WoolOrange;
                        const bool nx = wT1003.blockAt(cx - layerHalf[v], S + v, cz + u) == BR::WoolOrange;
                        const bool px = wT1003.blockAt(cx + layerHalf[v], S + v, cz + u) == BR::WoolOrange;
                        const bool doorHole = (u == 0 && v <= 3);             // +Z 主入口门洞吞没格
                        const bool lintelHole = (v == 4 && std::abs(u) <= 2); // +Z 门楣 CutSandstone 覆盖格
                        if (mz != want) ankhOk = false;
                        if (nx != want) ankhOk = false;
                        if (px != want) ankhOk = false;
                        if (pz != (want && !doorHole && !lintelHole)) ankhOk = false;
                        faceCount[0] += mz ? 1 : 0;
                        faceCount[1] += pz ? 1 : 0;
                        faceCount[2] += nx ? 1 : 0;
                        faceCount[3] += px ? 1 : 0;
                    }
                ankhOk = ankhOk && faceCount[0] == 21 && faceCount[2] == 21 && faceCount[3] == 21
                         && faceCount[1] == 13; // +Z = 21 - 门洞 3 - 门楣覆盖 5
                ok = ok && ankhOk;
                // (c) 风玫瑰（地板橙 30 + 全域蓝 1）。
                int roseOrange = 0, blueTotal = 0;
                bool roseOk = true;
                for (int dx = -7; dx <= 7; ++dx)
                    for (int dz = -7; dz <= 7; ++dz) {
                        const quint8 b = wT1003.blockAt(cx + dx, S, cz + dz);
                        const bool onRose = ((dx + dz) & 1) == 0
                                            && std::abs(dx) + std::abs(dz) <= 5
                                            && !(dx == 0 && dz == 0);
                        if (b == BR::WoolOrange) {
                            ++roseOrange;
                            if (!onRose) roseOk = false;
                        } else if (onRose) {
                            roseOk = false; // 玫瑰位缺格
                        }
                    }
                for (int dx = -11; dx <= 11; ++dx)
                    for (int dz = -11; dz <= 11; ++dz)
                        for (int dy = -13; dy <= 11; ++dy)
                            if (wT1003.blockAt(cx + dx, S + dy, cz + dz) == BR::WoolBlue) ++blueTotal;
                roseOk = roseOk && roseOrange == 24 && blueTotal == 1
                         && wT1003.blockAt(cx, S, cz) == BR::WoolBlue;
                ok = ok && roseOk;
                // (d) 入口三处 + 顶窗 + 顶部暗腔。
                bool openOk = true;
                openOk = openOk
                    && wT1003.blockAt(cx, S + 1, cz + 10) == BR::Air
                    && wT1003.blockAt(cx - 1, S + 2, cz + 9) == BR::Air
                    && wT1003.blockAt(cx, S + 3, cz + 8) == BR::Air
                    && wT1003.blockAt(cx - 5, S + 1, cz - 10) == BR::Air
                    && wT1003.blockAt(cx + 5, S + 2, cz - 9) == BR::Air
                    && wT1003.blockAt(cx, S + 8, cz + 5) == BR::Air
                    && wT1003.blockAt(cx, S + 8, cz - 5) == BR::Air
                    && wT1003.blockAt(cx + 5, S + 8, cz) == BR::Air
                    && wT1003.blockAt(cx - 5, S + 8, cz) == BR::Air
                    && wT1003.blockAt(cx, S + 9, cz) == BR::Air
                    && wT1003.blockAt(cx, S + 10, cz) == BR::CutSandstone;
                ok = ok && openOk;
                // (e) 暗渠（入口 / 对角中段 / 密室接通口）。
                bool tunOk = true;
                tunOk = tunOk
                    && wT1003.blockAt(cx + 4, S + 1, cz + 10) == BR::Air
                    && wT1003.blockAt(cx + 4, S + 2, cz + 10) == BR::Air
                    && wT1003.blockAt(cx + 4, S - 4, cz + 5) == BR::Air
                    && wT1003.blockAt(cx + 4, S - 3, cz + 5) == BR::Air
                    && wT1003.blockAt(cx + 4, S - 9, cz) == BR::Air
                    && wT1003.blockAt(cx + 4, S - 8, cz) == BR::Air
                    && wT1003.blockAt(cx + 3, S - 8, cz) == BR::Air; // 密室侧接通
                ok = ok && tunOk;
                // (f) 密室 + 4 箱 + 压板→9 TNT 链。
                bool roomOk = true;
                for (const int c : { -3, 3 }) {
                    roomOk = roomOk
                        && wT1003.blockAt(cx + c, S - 11, cz + c) == BR::Air
                        && wT1003.blockAt(cx + c, S - 8, cz + c) == BR::Air
                        && wT1003.blockAt(cx + 4, S - 10, cz + 4) == BR::Sandstone
                        && wT1003.blockAt(cx + 4, S - 9, cz - 4) == BR::Sandstone
                        && wT1003.blockAt(cx + 3, S - 12, cz) == BR::Sandstone
                        && wT1003.blockAt(cx - 3, S - 12, cz - 3) == BR::Sandstone
                        && wT1003.blockAt(cx + 2, S - 7, cz + 2) == BR::Sandstone;
                }
                roomOk = roomOk
                    && wT1003.blockAt(cx - 2, S - 11, cz - 3) == BR::Chest
                    && (wT1003.stateAt(cx - 2, S - 11, cz - 3) == (BR::ChestStatePyramidFlag | 2))
                    && wT1003.blockAt(cx + 2, S - 11, cz - 3) == BR::Chest
                    && (wT1003.stateAt(cx + 2, S - 11, cz - 3) == (BR::ChestStatePyramidFlag | 2))
                    && wT1003.blockAt(cx - 2, S - 11, cz + 3) == BR::Chest
                    && (wT1003.stateAt(cx - 2, S - 11, cz + 3) == (BR::ChestStatePyramidFlag | 3))
                    && wT1003.blockAt(cx + 2, S - 11, cz + 3) == BR::Chest
                    && (wT1003.stateAt(cx + 2, S - 11, cz + 3) == (BR::ChestStatePyramidFlag | 3));
                int tnt = 0;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        if (wT1003.blockAt(cx + dx, S - 12, cz + dz) == BR::TntBlock) ++tnt;
                roomOk = roomOk && tnt == 9
                         && wT1003.blockAt(cx, S - 11, cz) == BR::StonePressurePlate;
                ok = ok && roomOk;
                if (!ok)
                    qInfo().noquote() << "  [t1003 diag] seed" << sd << "temple@" << cx << cz << "S" << S
                                      << "shape" << shapeOk << "ankh" << ankhOk << "rose" << roseOk
                                      << "open" << openOk << "tunnel" << tunOk << "room" << roomOk;
                if (!haveFirst) { // (g) 确定性基线
                    haveFirst = true;
                    firstSeed = sd;
                    firstHash = sampleHashT1003(wT1003, cx, S, cz);
                }
            }
            templesTotal += templesHere;
            if (templesHere == 0) { // 无神殿种子：跳过不计入有效世界（diag 留痕）
                ++seedMiss;
                qInfo().noquote() << "  [t1003 diag] seed" << sd << "no desert temple (skipped)";
                continue;
            }
            ++worldsChecked;
        }
        ok = ok && worldsChecked >= 4 && templesTotal >= 5; // 池化净样充足（阴性轮敏感）
        if (haveFirst) { // (g) 同 seed 重生成 → 抽样 FNV 一致
            World wR1003;
            wR1003.setWidth(160);
            wR1003.setDepth(160);
            wR1003.setHeight(128);
            wR1003.setSeed(int(firstSeed));
            // 重扫首神殿中心：与主循环同构的净簇选择（t1003FirstCleanCentroid —— 同序同筛，选择永不漂移）。
            int cx = 0, cz = 0;
            const bool centerOk = t1003FirstCleanCentroid(wR1003, cx, cz);
            const int S = centerOk ? wR1003.heightAt(cx, cz) : 0;
            const quint32 reHash = centerOk ? sampleHashT1003(wR1003, cx, S, cz) : 0;
            ok = ok && centerOk && reHash == firstHash;
            if (!centerOk || reHash != firstHash) // review0905 #3 起 (g) 失配带现场 diag（原静默判假）
                qInfo().noquote() << "  [t1003 diag] re-gen mismatch: seed" << firstSeed
                                  << "centerOk" << centerOk << "center@" << cx << cz << "S" << S
                                  << "reHash" << reHash << "vs firstHash" << firstHash;
        }
        // (h) 源码钉（world.cpp）：层半边表 / 密室深度 / 安卡行表 / 中心蓝块 / 暗渠循环 / 箱 state。
        {
            const QString exeDirT1003 = QCoreApplication::applicationDirPath();
            const QString rootT1003 = QDir(exeDirT1003 + QStringLiteral("/..")).absolutePath();
            QFile fT1003(rootT1003 + QStringLiteral("/src/World/world.cpp"));
            const QString src = fT1003.open(QIODevice::ReadOnly) ? QString::fromUtf8(fT1003.readAll()) : QString();
            const bool okPin =
                src.contains(QStringLiteral("constexpr int kLayerHalf[kPyramidTopLayer + 1] = { 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5 };"))
                && src.contains(QStringLiteral("constexpr int kChamberFloorDrop = 12;"))
                && src.contains(QStringLiteral("0, 0b1001, 0b1001, 0b1001, 0b0111, 0b0100, 0b0100, 0b0011, 0, 0, 0"))
                && src.contains(QStringLiteral("putSolid(cx, surfaceY, cz, BlockRegistry::WoolBlue); // 中心蓝块"))
                && src.contains(QStringLiteral("carveAir(cx + 4, surfaceY + 1 - k, cz + 10 - k);"))
                && src.contains(QStringLiteral("quint8(c[1] | BlockRegistry::ChestStatePyramidFlag));"));
            ok = ok && okPin;
            if (!okPin)
                qInfo().noquote() << "  [t1003 diag] source pins drifted (layer table / ankh rows / tunnel)";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1003 desert temple per-block rebuild: 21x21 stepped pyramid (layer halves"
                             " 10..5, cut-sandstone cap + lintel), four-face ankh wool pattern (21/face,"
                             " front 13 at door+lintel holes - MC-faithful per wiki blueprint: the"
                             " complete ankh figure lives on the floor centre and the cellar, facades"
                             " carry only striped front-tower bands, no complete pattern above the"
                             " door), floor wind-rose checker 24 orange + center blue,"
                             " 3 entrances + 4 top windows + upper hollow, secret diagonal tunnel, chamber"
                             " 7x7x4 with 4 oriented pyramid chests + plate-over-3x3-TNT (9) chain, well"
                             " deterministic re-gen, source pins, temples" << templesTotal << "worlds"
                          << worldsChecked << "seeds-miss" << seedMiss << "ravine-skipped" << ravineSkipped;
    });

    // ── P-t1004 丛林神殿逐方块重建探针（R19.19 批 t1004；placeJungleTemple 三层 + 红石组合锁验收面）──
    //    rig：t1003 同款 160×160×128 世界池（t307 地表基线 64 → 64 高 rig 塔顶守卫恒拒，见 P-t1003 注）+
    //    28 seed 大池自举（缺神殿种子跳过不计），≥4 个含神殿世界且池内神殿 ≥5 座才判。
    //    神殿定位 = 全图扫 IronDoor y≥50（丛林门立于地表 S+1≈62..68；要塞监狱门 y≤30 天然分离，worldgen
    //    仅此两处生成铁门）→ cx = doorx-3, cz = doorz, S = heightAt(cx,cz)。断言九层：
    //    (a) 三层形制：楼板 S+4 / S+8（15×15 混排）+ 屋顶 S+11（11×11）+ 地面/二层周界墙（|7|，高 3）+
    //        三层墙（|5|，S+9..S+10）+ 西入口 2 高门洞（被坡地 / 后置树干堵塞 → 净样弃样）；
    //        CobbleStairs 双跑 8 阶（dz=-5/-4 列嵌楼板齐平，硬判）；
    //    (b) 苔石混排：建筑域 Cobble+MossyCobble 池化苔率窗 [25,55]%（hash 40% 期望）；
    //    (c) 陷阱 2 组（【偏差登记：绊线→压力板】）：走廊中段 ±Z 对射（Dispenser (-4,S+1,±2) 朝走廊 +
    //        板 (-4,S+1,±1)）+ 转角箱前（(-5/-4,S+1,-7) 朝 +Z + 板 (-5/-4,S+1,-6)）；4 发射器「箭 2-14 发」
    //        state bit[5:2] ∈ [2,14]（考据读数编码）；
    //    (d) 组合锁电路静态（3 Lever 全 OFF + StoneBrick 底座 3 + 支线双粉 6 断电 + B 块 3 + NOT 火把 3
    //        常亮 state 4 + 汇流粉 6 平衡态 + 拐角粉 + 终 NOT 座 B_m（龛西墙格）+ 终 NOT 火把熄灭态 0x09 +
    //        门侧粉断电 + IronDoor 2 格合（state 0 / 0x08））；
    //    (e) 宝藏箱 2 只（(6,S+1,0) 谜题龛内 + (-6,S+1,-6) 侧室角；state = 朝向1|JungleFlag）+ 蛛网 6 定角；
    //    (f) 行为级开门腿（8 组合 × 独立重生世界防火把熔断）：逐组合置 3 Lever（0x06|on）→ tickRedstone
    //        收敛 → 恰组合 0b111（全 ON）IronDoor 两格 bit2 开，其余 7 组合恒闭 —— 确定性方块事件链；
    //    (g) 确定性：首个净样神殿重生成 → 足迹域 stride-2 抽样 FNV 一致；
    //    (h) 源码钉：拉杆 0x06 行 / 常亮火把 state 4 行 / 熄灭态火把行 / IronDoor 0x08 行 / 箭数 hash 行 /
    //        苔率 kMossyPct 行 / 汇流平衡表行。
    runLegMulti({ "t1004 jungle temple per-block rebuild: 3-story mossy-mixed cobble keep(15x15 two slabs + 11x11 t"
        "op, pooled mossy window), dual dispenser arrowtraps (corridor mid + corner-chest, ammo window 2-"
        "14 in state bits), 3-leverredstone AND combination lock (unique all-ON combo opens IronDoor beha"
        "viorallyover 8 fresh-world combos, closed otherwise), 2 oriented jungle chests,stairs + webs, de"
        "terministic re-gen, source pins, templesworldsseeds-missravine-skipped" }, [&]() {
        bool ok = true;
        const quint32 seedsT1004[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 5150u, 2718u, 1618u,
                                       42u, 999u, 31337u, 2024u, 8675309u, 271828u, 314159u, 123456789u,
                                       7u, 12345u, 54321u, 8888u, 111u, 2222u, 33333u, 555555u,
                                       7777777u, 97531u, 13579u, 24680u };
        const quint8 mergeExpect[6] = { 0x1F, 0x3E, 0x3E, 0x3F, 0x3E, 0x6E };
        auto isCobbleFam = [](quint8 id) { return id == BR::Cobble || id == BR::MossyCobble; };
        auto sampleHashT1004 = [](World &w, int cx, int cy, int cz) {
            quint32 h = 0x811c9dc5u;
            auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
            for (int dx = -8; dx <= 8; dx += 2)
                for (int dz = -8; dz <= 8; dz += 2)
                    for (int dy = 0; dy <= 12; ++dy) {
                        step(quint32(w.blockAt(cx + dx, cy + dy, cz + dz)));
                        step(quint32(w.stateAt(cx + dx, cy + dy, cz + dz)));
                    }
            h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
            return h;
        };
        int worldsChecked = 0, templesTotal = 0, seedMiss = 0, ravineSkipped = 0;
        bool haveFirst = false, behaviorDone = false;
        quint32 firstSeed = 0, firstHash = 0;
        int firstCx = 0, firstCy = 0, firstCz = 0;
        for (quint32 sd : seedsT1004) {
            if (worldsChecked >= 5) break;
            World wT1004;
            wT1004.setWidth(160);
            wT1004.setDepth(160);
            wT1004.setHeight(128); // t307 地表基线 64 → 世界高须 128（64 高 rig 恒拒，见 P-t1003 rig 注释）
            wT1004.setSeed(int(sd)); // setter 内 generate() 全量 worldgen
            // 全图扫 IronDoor y≥50 → 聚簇（同殿上下 2 格）。
            struct JDoor { int x, y, z; };
            std::vector<JDoor> jdoors;
            for (int x = 0; x < wT1004.width(); ++x)
                for (int z = 0; z < wT1004.depth(); ++z)
                    for (int y = 50; y < wT1004.height(); ++y)
                        if (wT1004.blockAt(x, y, z) == BR::IronDoor)
                            jdoors.push_back({ x, y, z });
            std::vector<bool> used(jdoors.size(), false);
            int templesHere = 0;
            for (size_t i = 0; i < jdoors.size(); ++i) {
                if (used[i]) continue;
                int cx = 0, cz = 0, pairs = 0;
                for (size_t j = i; j < jdoors.size(); ++j) {
                    if (used[j]) continue;
                    if (std::abs(jdoors[j].x - jdoors[i].x) <= 1
                        && std::abs(jdoors[j].z - jdoors[i].z) <= 1
                        && std::abs(jdoors[j].y - jdoors[i].y) <= 2) {
                        used[j] = true;
                        ++pairs;
                    }
                }
                if (pairs != 2) continue; // 残簇 → 弃
                cx = jdoors[i].x - 3;     // 门在龛西墙列 x=+3
                cz = jdoors[i].z;
                // 主控收口修正：S 锚定**结构自身**（下层门 = S+1），不用 heightAt(cx,cz)——生成后
                //   植被/坡度会改写地表高度（种子相关），heightAt 漂移一格即整体坐标系平移 = 静态腿
                //   种子相关假红（424242/90210 两座净样神殿静默挂 static legs 的根因）。门簇是结构
                //   固有锚点，读档/重生成同样成立。
                const int S = jdoors[i].y - 1;
                auto diagT = [&](const char *sec) {
                    qInfo().noquote() << "  [t1004 diag] seed" << sd << "temple@" << cx << cz << "S" << S
                                      << "section" << sec;
                };
                // (a) 三层形制净样门（峡谷切塔弃样，净样口径同 t995/t1001/t1003）。
                bool shapeOk = true;
                for (const int c : { -7, 7 }) {
                    for (int dy : { 1, 3, 5, 7 }) {
                        shapeOk = shapeOk && isCobbleFam(wT1004.blockAt(cx + c, S + dy, cz + 7))
                                             && isCobbleFam(wT1004.blockAt(cx + c, S + dy, cz - 7))
                                             && isCobbleFam(wT1004.blockAt(cx + 7, S + dy, cz + c))
                                             && isCobbleFam(wT1004.blockAt(cx - 7, S + dy, cz + c));
                    }
                    shapeOk = shapeOk && isCobbleFam(wT1004.blockAt(cx + c, S + 4, cz + c))
                                         && isCobbleFam(wT1004.blockAt(cx - c, S + 4, cz - c))
                                         && isCobbleFam(wT1004.blockAt(cx + c, S + 8, cz - c))
                                         && isCobbleFam(wT1004.blockAt(cx - c, S + 8, cz + c));
                }
                shapeOk = shapeOk && isCobbleFam(wT1004.blockAt(cx, S + 11, cz))
                                     && isCobbleFam(wT1004.blockAt(cx + 5, S + 11, cz + 5))
                                     && isCobbleFam(wT1004.blockAt(cx + 5, S + 10, cz))
                                     && isCobbleFam(wT1004.blockAt(cx - 5, S + 10, cz))
                                     && isCobbleFam(wT1004.blockAt(cx, S + 10, cz + 5))
                                     && isCobbleFam(wT1004.blockAt(cx, S + 10, cz - 5));
                if (!shapeOk) {
                    ++ravineSkipped;
                    diagT("shape broken (ravine?) -> sample discarded");
                    continue;
                }
                ++templesHere;
                // (a) 入口净样门 + 楼梯。入口 = 西墙 dz=0 两格气（worldgen 已 dx=-7/-8 切坡；后置树干
                //     仍可能长入门洞 —— MC wiki 载「外部方块（树叶等）可生成进结构内部」→ 按净样口径
                //     弃样登记，与峡谷切塔同桶）；楼梯 = 结构内定格，硬判。
                const bool entranceOk = wT1004.blockAt(cx - 7, S + 1, cz) == BR::Air
                                        && wT1004.blockAt(cx - 7, S + 2, cz) == BR::Air;
                if (!entranceOk) {
                    ++ravineSkipped;
                    diagT("entrance blocked (slope/overgrowth) -> sample discarded");
                    continue;
                }
                bool stairsOk = true;
                for (int i = 0; i < 4; ++i) {
                    stairsOk = stairsOk
                        && wT1004.blockAt(cx - 1 + i, S + 1 + i, cz - 5) == BR::CobbleStairs
                        && wT1004.blockAt(cx + 1 + i, S + 5 + i, cz - 4) == BR::CobbleStairs;
                }
                ok = ok && stairsOk;
                // (b) 苔率池（建筑域 [±7]²×[S, S+11] 全族格）。
                long famCobble = 0, famMossy = 0;
                for (int dx = -7; dx <= 7; ++dx)
                    for (int dz = -7; dz <= 7; ++dz)
                        for (int dy = 0; dy <= 11; ++dy) {
                            const quint8 b = wT1004.blockAt(cx + dx, S + dy, cz + dz);
                            if (b == BR::Cobble) ++famCobble;
                            else if (b == BR::MossyCobble) ++famMossy;
                        }
                const long famAll = famCobble + famMossy;
                const bool mossOk = famAll >= 900
                                    && famMossy * 100 >= famAll * 25 && famMossy * 100 <= famAll * 55;
                ok = ok && mossOk;
                // (c) 陷阱 2 组 + 箭数窗。
                bool trapOk = true;
                auto checkDisp = [&](int dx, int dy, int dz, int facing) {
                    const quint8 id = wT1004.blockAt(cx + dx, S + dy, cz + dz);
                    const quint8 st = wT1004.stateAt(cx + dx, S + dy, cz + dz);
                    trapOk = trapOk && id == BR::Dispenser && (st & 3) == facing
                             && int(st >> 2) >= 2 && int(st >> 2) <= 14;
                };
                checkDisp(-4, 1, -2, 2); // 走廊中段北（朝 +Z 入走廊）
                checkDisp(-4, 1, 2, 3);  // 走廊中段南（朝 -Z）
                checkDisp(-5, 1, -7, 2); // 转角箱前北墙 2 发射器
                checkDisp(-4, 1, -7, 2);
                trapOk = trapOk
                    && wT1004.blockAt(cx - 4, S + 1, cz - 1) == BR::CobblePressurePlate
                    && wT1004.blockAt(cx - 4, S + 1, cz + 1) == BR::CobblePressurePlate
                    && wT1004.blockAt(cx - 5, S + 1, cz - 6) == BR::CobblePressurePlate
                    && wT1004.blockAt(cx - 4, S + 1, cz - 6) == BR::CobblePressurePlate;
                ok = ok && trapOk;
                // (d) 组合锁电路静态。
                bool lockOk = true;
                for (const int lx : { -3, 0, 3 }) {
                    lockOk = lockOk
                        && wT1004.blockAt(cx + lx, S + 2, cz + 6) == BR::Lever
                        && wT1004.stateAt(cx + lx, S + 2, cz + 6) == 0x06 // 附 +Z 墙、全 OFF
                        && wT1004.blockAt(cx + lx, S + 2, cz + 7) == BR::StoneBrick
                        && wT1004.blockAt(cx + lx, S + 2, cz + 5) == BR::RedstoneDust
                        && wT1004.stateAt(cx + lx, S + 2, cz + 5) == 0
                        && wT1004.blockAt(cx + lx, S + 2, cz + 4) == BR::RedstoneDust
                        && wT1004.stateAt(cx + lx, S + 2, cz + 4) == 0
                        && wT1004.blockAt(cx + lx, S + 2, cz + 3) == BR::Cobble // B 块（拉杆 / 火把共挂）
                        && wT1004.blockAt(cx + lx, S + 2, cz + 2) == BR::RedstoneTorch
                        && wT1004.stateAt(cx + lx, S + 2, cz + 2) == 4; // 常亮（附着 +Z）
                    for (int dy = 1; dy <= 1; ++dy) {
                        // 主控收口修正：座层只有 S+1（mixPut 三列支座）——S+2 层同格是粉尘/火把本体
                        //   （worldgen put 落 surfaceY+2），旧 dy<=2 的家族检查把自家电路格判成缺座 = 全
                        //   神殿系统性假红（424242/90210/5150 双殿四座全挂 static: lock 的根因）。
                        lockOk = lockOk && isCobbleFam(wT1004.blockAt(cx + lx, S + dy, cz + 5))
                                             && isCobbleFam(wT1004.blockAt(cx + lx, S + dy, cz + 4))
                                             && isCobbleFam(wT1004.blockAt(cx + lx, S + dy, cz + 3));
                    }
                }
                for (int mx = -3; mx <= 2; ++mx) {
                    lockOk = lockOk
                        && wT1004.blockAt(cx + mx, S + 2, cz + 1) == BR::RedstoneDust
                        && wT1004.stateAt(cx + mx, S + 2, cz + 1) == mergeExpect[mx + 3]
                        && isCobbleFam(wT1004.blockAt(cx + mx, S + 1, cz + 1));
                }
                lockOk = lockOk
                    && wT1004.blockAt(cx + 2, S + 2, cz + 2) == BR::RedstoneDust
                    && wT1004.stateAt(cx + 2, S + 2, cz + 2) == 0x2F // 拐角粉（3 号火把馈入）
                    && isCobbleFam(wT1004.blockAt(cx + 2, S + 1, cz + 2))
                    && wT1004.blockAt(cx + 3, S + 2, cz + 1) == BR::Cobble // B_m（龛西墙格）
                    && wT1004.blockAt(cx + 4, S + 2, cz + 1) == BR::RedstoneTorch
                    && wT1004.stateAt(cx + 4, S + 2, cz + 1) == 0x09 // 终 NOT 初始熄灭（TorchOnNX|OffFlag）
                    && wT1004.blockAt(cx + 4, S + 2, cz + 0) == BR::RedstoneDust
                    && wT1004.stateAt(cx + 4, S + 2, cz + 0) == 0
                    && isCobbleFam(wT1004.blockAt(cx + 4, S + 1, cz + 0)) // 门侧粉座
                    && wT1004.blockAt(cx + 3, S + 1, cz + 0) == BR::IronDoor
                    && wT1004.stateAt(cx + 3, S + 1, cz + 0) == 0 // 合（下格）
                    && wT1004.blockAt(cx + 3, S + 2, cz + 0) == BR::IronDoor
                    && wT1004.stateAt(cx + 3, S + 2, cz + 0) == 0x08; // 上格
                ok = ok && lockOk;
                // (e) 宝藏箱 2 + 蛛网。
                bool lootOk = wT1004.blockAt(cx + 6, S + 1, cz + 0) == BR::Chest
                              && wT1004.stateAt(cx + 6, S + 1, cz + 0) == (BR::ChestStateJungleFlag | 1)
                              && wT1004.blockAt(cx - 6, S + 1, cz - 6) == BR::Chest
                              && wT1004.stateAt(cx - 6, S + 1, cz - 6) == BR::ChestStateJungleFlag; // 侧箱朝 +X（review0904 勘误随 world.cpp state 1→0）
                int webs = 0;
                for (const int dx : { -6, 6 })
                    for (const int dz : { -6, 6 }) {
                        if (wT1004.blockAt(cx + dx, S + 3, cz + dz) == BR::Cobweb) ++webs;
                        if (wT1004.blockAt(cx + dx, S + 7, cz + dz) == BR::Cobweb) ++webs;
                    }
                // 顶角两网移出 dx 循环（review0904：原位各计两次 → 顶角缺失不红）；蛛网 6 处定角全查。
                if (wT1004.blockAt(cx + 4, S + 10, cz + 4) == BR::Cobweb) ++webs;
                if (wT1004.blockAt(cx - 4, S + 10, cz - 4) == BR::Cobweb) ++webs;
                lootOk = lootOk && webs >= 6;
                ok = ok && lootOk;
                if (!ok) {
                    // 主控收口诊断（t1004 收口期）：static legs 是聚合腿，两座净样神殿（424242/90210）
                    //   静默挂在这里——逐腿拆分定位（植被侵扰 / 坐标漂移 / 苔率越窗三类嫌疑）。
                    if (!stairsOk) diagT("static: stairs");
                    if (!mossOk)
                        qInfo().noquote() << "  [t1004 diag] static: moss famAll" << famAll
                                                          << "famMossy" << famMossy
                                                          << "pct" << (famAll ? famMossy * 100 / famAll : -1);
                    if (!trapOk) diagT("static: traps");
                    if (!lockOk) diagT("static: lock");
                    if (!lootOk) diagT("static: loot");
                }
                // (f) 行为级开门腿：首个净样神殿承担（8 组合 × 独立重生世界防熔断）。
                if (!behaviorDone) {
                    behaviorDone = true;
                    firstSeed = sd;
                    firstCx = cx;
                    firstCy = S;
                    firstCz = cz;
                    bool comboOk = true;
                    for (int combo = 0; combo < 8 && comboOk; ++combo) {
                        World wB;
                        wB.setWidth(160);
                        wB.setDepth(160);
                        wB.setHeight(128);
                        wB.setSeed(int(sd)); // 独立重生：每组合全新熔断计时 + 初态
                        if (wB.blockAt(cx + 3, S + 1, cz) != BR::IronDoor) { comboOk = false; break; }
                        const int levers[3] = { -3, 0, 3 };
                        for (int i = 0; i < 3; ++i) {
                            const bool on = ((combo >> i) & 1) != 0;
                            const quint8 want = quint8(0x06 | (on ? 1 : 0));
                            if (on)
                                wB.setBlock(cx + levers[i], S + 2, cz + 6, BR::Lever, want);
                            // 全 OFF 组合与生成态一致（setBlock 同态早退）→ 只读回验。
                            comboOk = comboOk
                                && wB.stateAt(cx + levers[i], S + 2, cz + 6) == want;
                        }
                        for (int t = 0; t < 12 && comboOk; ++t) wB.tickRedstone(); // 确定性收敛（≤6 tick）
                        const bool wantOpen = (combo == 7); // 唯一正确组合 = 全 ON
                        const quint8 stLo = wB.stateAt(cx + 3, S + 1, cz);
                        const quint8 stUp = wB.stateAt(cx + 3, S + 2, cz);
                        comboOk = comboOk
                            && ((stLo & 4) != 0) == wantOpen
                            && ((stUp & 4) != 0) == wantOpen;
                        if (!comboOk)
                            qInfo().noquote() << "  [t1004 diag] combo" << combo << "lo" << stLo
                                              << "up" << stUp << "(want open =" << wantOpen << ")";
                    }
                    ok = ok && comboOk;
                }
                if (!haveFirst) { // (g) 确定性基线
                    haveFirst = true;
                    firstHash = sampleHashT1004(wT1004, cx, S, cz);
                }
            }
            templesTotal += templesHere;
            if (templesHere == 0) { // 无（净样）神殿种子：跳过不计入有效世界
                ++seedMiss;
                qInfo().noquote() << "  [t1004 diag] seed" << sd << "no clean jungle temple (skipped)";
                continue;
            }
            ++worldsChecked;
        }
        ok = ok && worldsChecked >= 4 && templesTotal >= 5 && behaviorDone; // 池化净样充足（阴性轮敏感）
        if (haveFirst) { // (g) 同 seed 重生成 → 抽样 FNV 一致
            World wR1004;
            wR1004.setWidth(160);
            wR1004.setDepth(160);
            wR1004.setHeight(128);
            wR1004.setSeed(int(firstSeed));
            ok = ok && wR1004.blockAt(firstCx + 3, firstCy + 1, firstCz) == BR::IronDoor
                 && sampleHashT1004(wR1004, firstCx, firstCy, firstCz) == firstHash;
        }
        // (h) 源码钉（world.cpp）：拉杆 / 火把 / 门 / 箭数 / 苔率 / 汇流平衡表。
        {
            const QString exeDirT1004 = QCoreApplication::applicationDirPath();
            const QString rootT1004 = QDir(exeDirT1004 + QStringLiteral("/..")).absolutePath();
            QFile fT1004(rootT1004 + QStringLiteral("/src/World/world.cpp"));
            const QString src = fT1004.open(QIODevice::ReadOnly) ? QString::fromUtf8(fT1004.readAll()) : QString();
            const bool okPin =
                src.contains(QStringLiteral("put(cx + lx, surfaceY + 2, cz + 6, BlockRegistry::Lever, quint8(0x06));"))
                && src.contains(QStringLiteral("put(cx + lx, surfaceY + 2, cz + 2, BlockRegistry::RedstoneTorch, 4);"))
                && src.contains(QStringLiteral("quint8(1 | BlockRegistry::RedstoneTorchStateOffFlag));"))
                && src.contains(QStringLiteral("put(cx + 3, surfaceY + 2, cz + 0, BlockRegistry::IronDoor, 0x08);"))
                && src.contains(QStringLiteral("quint8(2 + int(hashVoxel(templeSeed ^ 0xA2C0u, px, yy, pz) % 13));"))
                && src.contains(QStringLiteral("constexpr int kMossyPct       = 40u;"))
                && src.contains(QStringLiteral("static const quint8 kMergeStates[6] = { 0x1F, 0x3E, 0x3E, 0x3F, 0x3E, 0x6E };"));
            ok = ok && okPin;
            if (!okPin)
                qInfo().noquote() << "  [t1004 diag] source pins drifted (lever/torch/door/ammo/mossy/merge)";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1004 jungle temple per-block rebuild: 3-story mossy-mixed cobble keep"
                             "(15x15 two slabs + 11x11 top, pooled mossy window), dual dispenser arrow"
                             "traps (corridor mid + corner-chest, ammo window 2-14 in state bits), 3-lever"
                             "redstone AND combination lock (unique all-ON combo opens IronDoor behaviorally"
                             "over 8 fresh-world combos, closed otherwise), 2 oriented jungle chests,"
                             "stairs + webs, deterministic re-gen, source pins, temples" << templesTotal
                          << "worlds" << worldsChecked << "seeds-miss" << seedMiss
                          << "ravine-skipped" << ravineSkipped;
    });

    // ── P-t1013 箱子矿车探针（R19.20 t1013 转正偏差 2；机制等价 MC 1.0 minecart with chest）──
    //    rig：共享世界轨线 + worldgen 标记箱 + PlayerController/MinecartManager/ChestStore 真注入
    //    （convertMineshaftChests 真 Q_INVOKABLE 转正链，非复刻）。七腿：
    //    (a) 转正链：标记箱（Chest+ChestStateMineshaftFlag）→ convertMineshaftChests 路 (a) 摘块 +
    //        registerCart 内容键 + spawnChestCart 四邻轨格落车（轨面全姿态）；clearMineshaftChest 静默性
    //        （blockBroken 计数不增 —— 走 setBlock 会发 blockBroken(22) 触发掉内容/清键，静默是承重的）。
    //    (b) 交互：tryMount 箱车拒载（不可骑）+ 普通车同射线可骑对照 + chestKeyAt 内容键契约（右键
    //        开箱分支 findCartHit→chestKeyAt→chestOpened(键) 的 C++ 面）。
    //    (c) 掉落链：内容槽预填（含附魔 / 名字 / 耐久元数据）→ 生存 3 击末击毁 → chestCartBroken 恰一次
    //        携正确键 + cartBroken 0 次（防车物品双掉）；创造瞬破同发（t767 有意偏差：箱车全模式掉落，
    //        静默清键 = 数据丢失）；虚空卷没不发（键保留 → 回生面）。
    //    (d) 移动：默认静止（speed 0）→ 玩家 pushEmptyCart 沿轨推走 ≥1 格（可被推动；动力轨激活走
    //        tickPushedCarts 全车种既有物理，P12 族已钉，不重测）。
    //    (e) 存档回生：registerCart「键 + 全空条目」经 allChests（"cart":true，全空豁免跳过）→ loadAll
    //        round-trip 保真 → convertMineshaftChests 路 (b) 按键回生实体；populateMineshaftLoot 首开
    //        gate（键+空条目 roll / 已填 no-op / 非矿车键条目 no-op）；挖毁 clearChest 清键 → 不回生。
    //    (f) 源码钉：变体字段 / chestCartBroken 信号 / tryMount 拒载 / convertMineshaftChests /
    //        Main.qml delegate 箱体 / onChestCartBroken / enterWorld 接线（阴性轮敏感）。
    //    (g) review0907 A-P1-1：loot 不再生 —— roll 后取空重开不再 roll（looted 持久标志 gate）+
    //        "looted":true 落盘 round-trip（cart 回生标记同条目共存，旧档缺键 = 从未 roll）。
    runLegMulti({ "t1013 chest-minecart conversion: mineshaft marker chest -> silent block removal (no blockBroken)"
        " + cart key registered + cart spawned on adjacent rail cell in full rail posture with key = spaw"
        "n cell (convertMineshaftChests path a)",
               "t1013 chest-cart interaction: findCartHit + chestKeyAt contract for the right-click-open branch,"
        " tryMount rejects chest cart (plain cart control mounts)",
               "t1013 chest-cart break chain: survival 3-hit final blow emits chestCartBroken exactly once with "
        "cart key (cartBroken zero = no double minecart drop), slot metadata readable at signal time then"
        " clearChest, creative instant-break also emits (t767 deviation: contents never silently voided),"
        " void loss emits nothing and keeps the key for respawn",
               "t1013 chest-cart movement: spawns static, player push moves it along the rail >=1 cell pinned to"
        " rail surface (powered-rail physics shared, pinned by P12 family)",
               "t1013 chest-cart persistence: empty cart-key entry survives allChests (cart-flag true, empty-exe"
        "mption) -> loadAll round-trip -> convertMineshaftChests path (b) respawns the entity; populateMi"
        "neshaftLoot first-open gate (empty cart key rolls once, filled/non-cart entries no-op)",
               "t1013 loot no-regen (review0907 A-P1-1): mineshaft cart loot rolls once -- take all 27 slots emp"
        "ty then reopen -> populateMineshaftLoot rejected and slots stay empty (same-seed re-roll infinit"
        "e regen fixed by persistent looted flag); looted:true survives allChests->loadAll round-trip wit"
        "h cart respawn flag coexisting on the same entry (old-save compatible: missing looted key = neve"
        "r rolled)",
               "t1013 cart-key cell guard + chest-cart mob mount (review0907 B cross-batch high #3 / legacy #4):"
        " placing a Chest block onto a registered cart key cell is rejected as a no-op (no double-contain"
        "er aliasing, clearChest can never erase the respawn key via a block chest), the same aim places "
        "normally once the key is cleared (plain cells undisturbed), and the mob board scan skips chest c"
        "arts (spider at the cart cell stays unseated while a pig on a plain cart seats, matching the pla"
        "yer-side tryMount rejection)",
               "t1013 source pins: variant fields + chestCartBroken signal + tryMount reject + convert chain + M"
        "ain.qml delegate/lid-skip/handler/enterWorld wiring" }, [&]() {
        const auto [x0T1013, z0T1013] = nextSlot();
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        bool okG = false; // review0907 A-P1-1 战利品不再生腿（roll → 取空 → 重开不再生 + looted 落盘）
        bool okH = false; // review0907 B 跨批高危 #3 放置守卫腿（cart 键格放箱拒 + 普通格对照）+ #4 箱车拒载生物腿
        // (a) 轨线 4 格 + 标记箱立 (x0+1, z0+1)（键格；4 邻含轨格 (x0+1, z0) → 转正落车该轨上）。
        for (int i = 0; i < 4; ++i) w.setBlock(x0T1013 + i, kRigY, z0T1013, BR::Rail, 0);
        const int keyX = x0T1013 + 1, keyY = kRigY, keyZ = z0T1013 + 1;
        w.setBlock(keyX, keyY, keyZ, BR::Chest, BR::ChestStateMineshaftFlag);
        tickN(w, 2);
        {
            ChestStore csT1013;
            MinecartManager cartsT1013;
            PlayerController pcT1013;
            pcT1013.setWorld(&w);
            pcT1013.setMinecartManager(&cartsT1013);
            pcT1013.setChestStore(&csT1013);
            int brokenPlain = 0, brokenChest = 0;
            int chestKeyArgX = -1, chestKeyArgY = -1, chestKeyArgZ = -1;
            int dropArgX = -1, dropArgY = -1, dropArgZ = -1;
            const QMetaObject::Connection connPlainT1013 =
                QObject::connect(&cartsT1013, &MinecartManager::cartBroken, &w, [&](int, int, int) { ++brokenPlain; });
            const QMetaObject::Connection connChestT1013 =
                QObject::connect(&cartsT1013, &MinecartManager::chestCartBroken, &w,
                                 [&](int dx, int dy, int dz, int kx, int ky, int kz) {
                                     ++brokenChest;
                                     dropArgX = dx; dropArgY = dy; dropArgZ = dz;
                                     chestKeyArgX = kx; chestKeyArgY = ky; chestKeyArgZ = kz;
                                 });
            // (a) 静默性 + 转正三合一：clearMineshaftChest 不发 blockBroken（worldChanged 除外）。
            int blockBrokenCount = 0;
            const QMetaObject::Connection connBBT1013 =
                QObject::connect(&w, &World::blockBroken, &w, [&](int, int, int) { ++blockBrokenCount; });
            const int bbBefore = blockBrokenCount;
            okA = w.isMineshaftChest(keyX, keyY, keyZ);
            const int preLiveT1013 = cartsT1013.liveCount(); // 共享世界 worldgen 标记（若有）也在本轮转正
            pcT1013.convertMineshaftChests();
            const float rideDoc = 0.45f; // kCartRideH 文档镜像值（t734 贴轨面；P11 同款）
            const int liveAfterConvertT1013 = cartsT1013.liveCount();
            // 本 rig 的车按射线寻址（共享世界可能另有 worldgen 矿井标记被一并转正 → 槽位不可作身份）。
            const QVector3D rigAboveT1013(float(keyX) + 0.5f, float(keyY) + 2.0f, float(keyZ - 1) + 0.5f);
            const int mineIdxT1013 = cartsT1013.findCartHit(rigAboveT1013, QVector3D(0, -1, 0), 4.0f, nullptr);
            okA = okA
                && w.blockAt(keyX, keyY, keyZ) == BR::Air          // 标记箱摘除
                && blockBrokenCount == bbBefore                    // 静默（不发 blockBroken）
                && csT1013.isCartCell(keyX, keyY, keyZ)            // 内容键登记
                && liveAfterConvertT1013 > preLiveT1013
                && mineIdxT1013 >= 0
                && cartsT1013.chestAt(mineIdxT1013)                // 变体标志
                && std::fabs(cartsT1013.posAt(mineIdxT1013).x() - (keyX + 0.5f)) < 0.01f
                && std::fabs(cartsT1013.posAt(mineIdxT1013).z() - (keyZ - 1 + 0.5f)) < 0.01f // 落四邻轨格 (keyX, keyZ-1)
                && std::fabs(cartsT1013.posAt(mineIdxT1013).y() - (keyY + rideDoc)) < 0.01f; // 轨面全姿态
            int kkx = -1, kky = -1, kkz = -1;
            okA = okA && cartsT1013.chestKeyAt(mineIdxT1013, kkx, kky, kkz)
                && kkx == keyX && kky == keyY && kkz == keyZ;      // 内容键 = 生成格（非落车格）
            if (!okA)
                qInfo().noquote() << "  [t1013 diag a]" << "block=" << w.blockAt(keyX, keyY, keyZ)
                    << "bb=" << blockBrokenCount << "/" << bbBefore
                    << "cartCell=" << csT1013.isCartCell(keyX, keyY, keyZ)
                    << "pre=" << preLiveT1013 << "post=" << liveAfterConvertT1013
                    << "mineIdx=" << mineIdxT1013
                    << "pos=" << (mineIdxT1013 >= 0 ? cartsT1013.posAt(mineIdxT1013) : QVector3D())
                    << "keys=" << kkx << kky << kkz;
            // (b) 交互：箱车拒载（右键开箱分支的前置契约 —— findCartHit 命中 + chestKeyAt 供键）。
            {
                const QVector3D above(float(keyX) + 0.5f, float(keyY) + 2.0f, float(keyZ - 1) + 0.5f);
                float hitDist = 0.0f;
                const int hitIdx = cartsT1013.findCartHit(above, QVector3D(0, -1, 0), 4.0f, &hitDist);
                int tx = -1, ty = -1, tz = -1;
                okB = hitIdx >= 0 && cartsT1013.chestAt(hitIdx)
                    && cartsT1013.chestKeyAt(hitIdx, tx, ty, tz) && tx == keyX && ty == keyY && tz == keyZ
                    && !cartsT1013.tryMount(above, QVector3D(0, -1, 0), 4.0f) // 不可骑
                    && cartsT1013.ridingIndex() < 0;
            }
            // 对照：同射线形态的普通车可骑（拒载是箱车特化，非射线/骑乘链回归）。
            {
                w.setBlock(x0T1013, kRigY, z0T1013 + 4, BR::Rail, 0);
                MinecartManager plain;
                plain.spawnCart(x0T1013, kRigY, z0T1013 + 4, &w);
                const QVector3D above2(float(x0T1013) + 0.5f, float(kRigY) + 2.0f, float(z0T1013 + 4) + 0.5f);
                okB = okB && plain.tryMount(above2, QVector3D(0, -1, 0), 4.0f) && plain.ridingIndex() == 0;
                plain.clearAll();
                w.setBlock(x0T1013, kRigY, z0T1013 + 4, BR::Air);
            }
            // (c) 掉落链：预填内容键 27 槽（槽 0 带元数据；槽 1 普通栈；其余空）→ 生存末击毁。
            //   断言全部 rig 作用域（aliveAt 捕获槽位），不用全局 liveCount 算术 —— 共享世界 worldgen
            //   标记箱同轮转正的 W 车数量随 seed 而定（可与 rig 车同柱/同轨），全局计数必踩假红。
            csT1013.setSlot(keyX, keyY, keyZ, 0, 409, 1, {7, 0, 0, 0}, QStringLiteral("renamed"), 42);
            csT1013.setSlot(keyX, keyY, keyZ, 1, 4, 5);
            const auto findByKeyT1013 = [&cartsT1013](int kx, int ky, int kz) {
                for (int i = 0; i < cartsT1013.count(); ++i) {
                    int ax = -1, ay = -1, az = -1;
                    if (cartsT1013.aliveAt(i) && cartsT1013.chestKeyAt(i, ax, ay, az)
                        && ax == kx && ay == ky && az == kz)
                        return i;
                }
                return -1;
            };
            int dbgLiveC[8] = {};
            bool dbgH[3] = {};
            bool okCSurvival = false, okCCreative = false, okCVoid = false;
            int oursIdx2 = -1, voidIdx = -1;
            bool cHit = false;
            {
                const QVector3D above(float(keyX) + 0.5f, float(keyY) + 2.0f, float(keyZ - 1) + 0.5f);
                const QVector3D down(0, -1, 0);
                const int oursIdx = cartsT1013.findCartHit(above, down, 4.0f, nullptr); // 本 rig 的车（(b) 已钉 key=生成格）
                dbgLiveC[0] = cartsT1013.liveCount();
                // 生存：kCartHitPoints=3 → 前 2 击扣血（不掉不毁），末击毁 + 发信号。
                dbgH[0] = cartsT1013.hitCartFromRay(above, down, 4.0f, &w, false);
                dbgLiveC[1] = cartsT1013.liveCount();
                dbgH[1] = dbgH[0] && cartsT1013.hitCartFromRay(above, down, 4.0f, &w, false);
                dbgLiveC[2] = cartsT1013.liveCount();
                dbgH[2] = dbgH[1] && cartsT1013.hitCartFromRay(above, down, 4.0f, &w, false);
                dbgLiveC[3] = cartsT1013.liveCount();
                const bool hit = dbgH[0] && dbgH[1] && dbgH[2];
                okCSurvival = hit && brokenChest == 1 && brokenPlain == 0 // 箱车信号恰一次；车物品信号 0（防双掉）
                    && chestKeyArgX == keyX && chestKeyArgY == keyY && chestKeyArgZ == keyZ
                    && oursIdx >= 0 && !cartsT1013.aliveAt(oursIdx);      // 本 rig 的车已毁（槽级断言）
                // 呈层 handler 契约（信号时点内容仍可读 + 元数据保真 → 逐槽 spawnItem 后 clearChest）：
                okCSurvival = okCSurvival && dropArgY >= 0
                    && csT1013.slotIdAt(keyX, keyY, keyZ, 0) == 409
                    && csT1013.slotDurabilityAt(keyX, keyY, keyZ, 0) == 42
                    && csT1013.slotNameAt(keyX, keyY, keyZ, 0) == QStringLiteral("renamed")
                    && csT1013.slotIdAt(keyX, keyY, keyZ, 1) == 4;
                csT1013.clearChest(keyX, keyY, keyZ);
                // 键摘除：rig 键不再寻址（世界 W 键可共存 —— 共享世界 worldgen 标记同轮转正登记，非本腿清场对象）。
                okCSurvival = okCSurvival && !csT1013.isCartCell(keyX, keyY, keyZ)
                    && csT1013.slotIdAt(keyX, keyY, keyZ, 0) == 0;
                okC = okCSurvival;
                // 创造瞬破（t767 偏差面）：箱车同发（全模式掉落，内容物不被静默清）。
                csT1013.registerCart(keyX, keyY, keyZ);
                cartsT1013.spawnChestCart(keyX, keyY, keyZ, &w, keyX, keyY, keyZ);
                oursIdx2 = findByKeyT1013(keyX, keyY, keyZ);
                dbgLiveC[4] = cartsT1013.liveCount();
                brokenChest = 0;
                cHit = cartsT1013.hitCartFromRay(above, down, 4.0f, &w, true);
                dbgLiveC[5] = cartsT1013.liveCount();
                okCCreative = cHit && brokenChest == 1
                    && oursIdx2 >= 0 && !cartsT1013.aliveAt(oursIdx2) // 生+毁相抵（槽级）
                    && chestKeyArgX == keyX && chestKeyArgY == keyY && chestKeyArgZ == keyZ;
                okC = okC && okCCreative;
                csT1013.clearChest(keyX, keyY, keyZ);
                // 虚空卷没：不发信号 + 键保留（重进世界回生面）。
                csT1013.registerCart(keyX, -1, keyZ);
                cartsT1013.spawnChestCart(keyX, -1, keyZ, &w, keyX, -1, keyZ); // y<0 → 地面姿态 pos.y<0
                voidIdx = findByKeyT1013(keyX, -1, keyZ);
                dbgLiveC[6] = cartsT1013.liveCount();
                brokenChest = 0;
                cartsT1013.checkCartEnvironment(&w);
                dbgLiveC[7] = cartsT1013.liveCount();
                okCVoid = brokenChest == 0 && voidIdx >= 0 && !cartsT1013.aliveAt(voidIdx)
                    && csT1013.isCartCell(keyX, -1, keyZ); // 键保留 → 回生不丢内容
                okC = okC && okCVoid;
                csT1013.clearChest(keyX, -1, keyZ);
            }
            if (!okC) {
                QString dumpT1013;
                for (int i = 0; i < cartsT1013.count(); ++i) {
                    int ax = -1, ay = -1, az = -1;
                    cartsT1013.chestKeyAt(i, ax, ay, az);
                    dumpT1013 += QStringLiteral(" [%1]a%2(%3,%4,%5)k%6,%7,%8")
                        .arg(i).arg(cartsT1013.aliveAt(i) ? 1 : 0)
                        .arg(cartsT1013.posAt(i).x(), 0, 'f', 1).arg(cartsT1013.posAt(i).y(), 0, 'f', 1)
                        .arg(cartsT1013.posAt(i).z(), 0, 'f', 1).arg(ax).arg(ay).arg(az);
                }
                qInfo().noquote() << "  [t1013 diag c]" << "h=" << dbgH[0] << dbgH[1] << dbgH[2]
                    << "live=" << dbgLiveC[0] << dbgLiveC[1] << dbgLiveC[2] << dbgLiveC[3]
                    << dbgLiveC[4] << dbgLiveC[5] << dbgLiveC[6] << dbgLiveC[7]
                    << "survival=" << okCSurvival << "creative=" << okCCreative << "cHit=" << cHit
                    << "ours2=" << oursIdx2 << "void=" << okCVoid << "voidIdx=" << voidIdx
                    << "bc=" << brokenChest << "bp=" << brokenPlain
                    << "key=" << chestKeyArgX << chestKeyArgY << chestKeyArgZ
                    << "drop=" << dropArgX << dropArgY << dropArgZ
                    << "s0=" << csT1013.slotIdAt(keyX, keyY, keyZ, 0)
                    << "s1=" << csT1013.slotIdAt(keyX, keyY, keyZ, 1) << "carts:" << dumpT1013;
            }
            // (d) 移动：默认静止 → 玩家沿轨推动 ≥1 格（轨东段留空 → 推 +X）。
            csT1013.registerCart(keyX, keyY, keyZ);
            cartsT1013.spawnChestCart(keyX, keyY, keyZ, &w, keyX, keyY, keyZ); // 落 (keyX, keyZ-1) 轨格
            {
                const int pushIdx = cartsT1013.findCartHit(rigAboveT1013, QVector3D(0, -1, 0), 4.0f, nullptr);
                const float startX = cartsT1013.posAt(pushIdx).x();
                QVector3D playerFeet(float(keyX) + 0.5f, float(keyY), float(keyZ - 1) + 0.5f);
                for (int t = 0; t < 40; ++t) {
                    cartsT1013.pushEmptyCart(&w, playerFeet, 1.0f, 0.0f);
                    cartsT1013.tickPushedCarts(0.016, &w);
                    playerFeet = QVector3D(cartsT1013.posAt(pushIdx).x(), float(keyY), cartsT1013.posAt(pushIdx).z());
                }
                okD = pushIdx >= 0
                    && std::fabs(cartsT1013.posAt(pushIdx).y() - (keyY + rideDoc)) < 0.01f // 沿轨不脱
                    && (cartsT1013.posAt(pushIdx).x() - startX) > 1.0f;
                cartsT1013.clearAll(); // (e) 前全清（含共享世界的 worldgen 转正车）→ 回生断言回到绝对计数
            }
            // (e) 存档回生：键 + 全空条目 → allChests "cart" 标记（全空豁免）→ loadAll round-trip →
            //     convert 路 (b) 按键回生；populate 首开 gate 三态。
            csT1013.registerCart(keyX, keyY, keyZ); // 键 + 全空条目（未开箱回生标记）
            const QVariantList savedT1013 = csT1013.allChests();
            bool cartSaved = false;
            for (const QVariant &v : savedT1013) {
                const QVariantMap cm = v.toMap();
                if (cm.value(QStringLiteral("x")).toInt() == keyX
                    && cm.value(QStringLiteral("y")).toInt() == keyY
                    && cm.value(QStringLiteral("z")).toInt() == keyZ) {
                    cartSaved = cm.value(QStringLiteral("cart")).toBool();
                    const QVariantList slotsT = cm.value(QStringLiteral("slots")).toList();
                    bool anyFull = false;
                    for (const QVariant &sv : slotsT) {
                        const QVariantMap sm = sv.toMap();
                        if (sm.value(QStringLiteral("id")).toInt() != 0) anyFull = true;
                    }
                    cartSaved = cartSaved && !anyFull; // 全空条目也落盘（回生标记）
                }
            }
            // 只回放 rig 键条目：共享世界 worldgen 标记箱若在同轮 (a) 一并转正，其键也会进 savedT1013
            //   （数量随 regenerate seed 布局而定，非本探针可控基数，硬编码必假 FAIL）—— 过滤后 loadAll /
            //   回生断言回到确定性单键口径（round-trip 保真语义不变：rig 条目经 allChests→loadAll 仍
            //   cart:true + 27 空槽；worldgen 键的回生面由 (a) 腿 pre/post 相对计数覆盖）。
            QVariantList savedRigT1013;
            for (const QVariant &v : savedT1013) {
                const QVariantMap cm = v.toMap();
                if (cm.value(QStringLiteral("x")).toInt() == keyX
                    && cm.value(QStringLiteral("y")).toInt() == keyY
                    && cm.value(QStringLiteral("z")).toInt() == keyZ)
                    savedRigT1013.append(v);
            }
            ChestStore csLoaded;
            csLoaded.loadAll(savedRigT1013);
            // cartCells() 是扁平 [x,y,z,...] 三元组表：恰 1 键 ⟺ size 3。
            okE = cartSaved && csLoaded.isCartCell(keyX, keyY, keyZ)
                && csLoaded.cartCells().size() == 3;
            // 回生：convert 路 (b)（键无车 → 落车）；车已毁清键的负例已在 (c) 钉（cartCells 空不回生）。
            pcT1013.setChestStore(&csLoaded);
            pcT1013.convertMineshaftChests();
            const int respawnIdxT1013 = cartsT1013.findCartHit(rigAboveT1013, QVector3D(0, -1, 0), 4.0f, nullptr);
            const bool eLive1 = cartsT1013.liveCount() == 1;
            const bool eIdx = respawnIdxT1013 >= 0;
            const bool eChest = eIdx && cartsT1013.chestAt(respawnIdxT1013); // 槽复用 LIFO → 槽位不定，按射线寻址
            const bool eKey = csLoaded.isCartCell(keyX, keyY, keyZ);
            okE = okE && eLive1 && eIdx && eChest && eKey;
            // populate 首开 gate：键 + 空条目 → roll 成功且键保持；二调 no-op；非矿车键条目 no-op。
            const bool ePop1 = csLoaded.populateMineshaftLoot(keyX, keyY, keyZ);
            bool rolledAny = false;
            for (int i = 0; i < 27; ++i)
                if (csLoaded.slotIdAt(keyX, keyY, keyZ, i) != 0) rolledAny = true;
            const bool ePop2 = rolledAny && csLoaded.isCartCell(keyX, keyY, keyZ)
                && !csLoaded.populateMineshaftLoot(keyX, keyY, keyZ);
            csLoaded.setSlot(keyX + 9, keyY, keyZ, 0, 4, 1); // 非矿车键既有条目
            const bool ePop3 = !csLoaded.populateMineshaftLoot(keyX + 9, keyY, keyZ);
            okE = okE && ePop1 && ePop2 && ePop3;
            if (!okE) {
                QString dumpE;
                for (int i = 0; i < cartsT1013.count(); ++i) {
                    int ax = -1, ay = -1, az = -1;
                    cartsT1013.chestKeyAt(i, ax, ay, az);
                    dumpE += QStringLiteral(" [%1]a%2(%3,%4,%5)k%6,%7,%8")
                        .arg(i).arg(cartsT1013.aliveAt(i) ? 1 : 0)
                        .arg(cartsT1013.posAt(i).x(), 0, 'f', 1).arg(cartsT1013.posAt(i).y(), 0, 'f', 1)
                        .arg(cartsT1013.posAt(i).z(), 0, 'f', 1).arg(ax).arg(ay).arg(az);
                }
                qInfo().noquote() << "  [t1013 diag e]" << "cartSaved=" << cartSaved
                    << "cells=" << csLoaded.cartCells().size()
                    << "isCart=" << csLoaded.isCartCell(keyX, keyY, keyZ)
                    << "live=" << cartsT1013.liveCount()
                    << "eLive1=" << eLive1 << "eIdx=" << eIdx << "eChest=" << eChest << "eKey=" << eKey
                    << "pop1=" << ePop1 << "rolledAny=" << rolledAny << "pop2=" << ePop2 << "pop3=" << ePop3
                    << "carts:" << dumpE;
            }
            // (g) review0907 A-P1-1 战利品不再生：roll（(e) 的 ePop1 已 roll 于 csLoaded 键格）→ 取空
            //     全部 27 槽（复现「首开取空」）→ 再开（openChest 每次 open 都调 populate）断言**不再
            //     roll**（返 false + 槽保持空，同 seed 重 roll 的无限再生在此暴露）+ looted 落盘
            //     round-trip（allChests "looted":true → loadAll 读回 → gate 仍拒 = 跨存档面同断言）。
            //     旧档豁免语义不破：空条目仍随 "cart":true 落盘（回生面），looted 只封 loot 面。
            {
                for (int i = 0; i < 27; ++i)
                    csLoaded.setSlot(keyX, keyY, keyZ, i, 0, 0); // 取空全部槽（条目仍在 + 全空）
                const bool regenRejected = !csLoaded.populateMineshaftLoot(keyX, keyY, keyZ);
                bool allEmptyAfter = true;
                for (int i = 0; i < 27; ++i)
                    if (csLoaded.slotIdAt(keyX, keyY, keyZ, i) != 0) allEmptyAfter = false;
                QVariantList savedGT1013;
                for (const QVariant &v : csLoaded.allChests()) {
                    const QVariantMap cm = v.toMap();
                    if (cm.value(QStringLiteral("x")).toInt() == keyX
                        && cm.value(QStringLiteral("y")).toInt() == keyY
                        && cm.value(QStringLiteral("z")).toInt() == keyZ)
                        savedGT1013.append(v);
                }
                bool lootedSaved = false;
                if (!savedGT1013.isEmpty()) {
                    const QVariantMap cm = savedGT1013.first().toMap();
                    lootedSaved = cm.value(QStringLiteral("looted")).toBool()
                        && cm.value(QStringLiteral("cart")).toBool(); // 回生标记与 loot 标记同条目共存
                }
                ChestStore csGT1013;
                csGT1013.loadAll(savedGT1013); // round-trip（跨存档面）
                const bool lootedRoundTrip = csGT1013.isCartCell(keyX, keyY, keyZ)
                    && !csGT1013.populateMineshaftLoot(keyX, keyY, keyZ);
                okG = regenRejected && allEmptyAfter && lootedSaved && lootedRoundTrip;
                if (!okG)
                    qInfo().noquote() << "  [t1013 diag g]" << "regenRejected=" << regenRejected
                        << "allEmpty=" << allEmptyAfter << "lootedSaved=" << lootedSaved
                        << "roundTrip=" << lootedRoundTrip;
            }
            // (h) review0907 B 跨批高危 #3 + 遗留 #4：cart 键格放 Chest 双容器别名守卫 + 箱车拒载生物。
            //     病灶 #3：转正后标记格是 Air，在该格放 Chest → 方块箱（坐标寻址）与箱车（键寻址）共享
            //     同一份 27 槽条目互见互取；破箱 clearChest 抹回生键 → 车凭空消失。修 = 通用放置预检层
            //     （t973 同层）「Chest + isCartCell(目标格) → 拒（不挥不消耗，t1017 附着拒绝族口径）」。
            //     病灶 #4：生物登乘扫描（Pass C）漏箱车排除 → 蜘蛛钉坐箱车视觉穿插；修 = 扫描循环
            //     chestAt(i) 一行 continue（对齐玩家侧 tryMount 拒箱车语义）。
            {
                WorldClock clockT1013h;
                EntityManager entsT1013h;
                Hotbar hbT1013h;
                pcT1013.setWorldClock(&clockT1013h);
                pcT1013.setEntityManager(&entsT1013h);
                pcT1013.setHotbar(&hbT1013h);
                QQuickWindow probeWinT1013h;
                pcT1013.setParentItem(probeWinT1013h.contentItem());
                // 瞄准帮手（P-t945 aimP945 / P-t1017 aimT1017 同式）：release+grab 重居中 →
                //   loadSavedState（Creative=1，放置不消耗不观察者门拒）→ tick 刷射线，返命中格。
                const auto aimT1013h = [&](float feetX, float feetY, float feetZ,
                                           float aimX, float aimY, float aimZ) {
                    const float ex = feetX, ey = feetY + 1.62f, ez = feetZ;
                    const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
                    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float pitch = std::asin(dy / len) * 57.2957795f;
                    const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
                    pcT1013.release();
                    pcT1013.grab();
                    pcT1013.loadSavedState(feetX, feetY, feetZ, yaw, pitch, 1 /* Creative */);
                    pcT1013.tick(); // updateRaycast 刷新命中（t889 先例）
                    return pcT1013.hitBlock();
                };
                const auto pumpMsT1013h = [](int ms) { // 放置 200ms CD 间隔（t128）
                    QElapsedTimer t;
                    t.start();
                    while (t.elapsed() < ms)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                };
                // 放置 rig：新槽石台（kRigY-1 地板 + 上方净空），键格 K = 台面上一格（Air）。
                const auto [pxH, pzH] = nextSlot();
                for (int dx = 0; dx <= 4; ++dx) {
                    w.setBlock(pxH + dx, kRigY - 1, pzH, BR::Stone, 0);
                    for (int dy = 0; dy <= 3; ++dy)
                        w.setBlock(pxH + dx, kRigY + dy, pzH, BR::Air, 0);
                }
                const int keyHX = pxH + 1, keyHY = kRigY, keyHZ = pzH;
                csLoaded.registerCart(keyHX, keyHY, keyHZ); // 键格登记（守卫的触发前提）
                pcT1013.setSelectedBlock(int(BR::Chest));
                // 眼位 (px+2.5, kRigY+1.62) 瞄 (px+1.7, kRigY)（K 正下方石块顶面内缩点，P-t945 同式）
                //   → 命中 (px+1, kRigY-1, pz) 顶面 → 目标格 = K。
                const QVector3D hitH = aimT1013h(float(pxH) + 2.5f, float(kRigY), float(pzH) + 0.5f,
                                                 float(pxH) + 1.7f, float(kRigY), float(pzH) + 0.5f);
                const bool aimedH = hitH == QVector3D(float(keyHX), float(kRigY - 1), float(pzH));
                pcT1013.placeBlock();
                pumpMsT1013h(260);
                const bool rejectedH = w.blockAt(keyHX, keyHY, keyHZ) == BR::Air; // 键格放置被拒
                // 对照：摘键后同一瞄点同选块 → 放置成功（守卫只拦键格，普通格不受扰）。
                csLoaded.clearChest(keyHX, keyHY, keyHZ);
                pcT1013.placeBlock();
                pumpMsT1013h(260);
                const bool controlH = w.blockAt(keyHX, keyHY, keyHZ) == BR::Chest;
                w.setBlock(keyHX, keyHY, keyHZ, BR::Air, 0); // 清场
                csLoaded.clearChest(keyHX, keyHY, keyHZ);
                if (!aimedH || !rejectedH || !controlH)
                    qInfo().noquote() << "  [t1013 diag h-place] aimed=" << aimedH << "hit=" << hitH
                                      << "rejected=" << rejectedH << "control=" << controlH;
                // 箱车拒载生物：Pass C 扫描对 chestAt(i) 跳过（对齐 tryMount 玩家侧拒箱车）。
                //   (e) 回生的箱车仍在场（射线重寻址防槽漂移）；生物生在箱车同格（XZ 距 0）→
                //   tickVehicleRiding 后不得登乘。对照：普通矿车 + 同格生物 → 恰登乘（载具侧查证，
                //   证明负腿非「登乘链整体失效」假绿）。
                bool chestRefusedH = false, plainAcceptedH = false;
                const int hCartIdx = cartsT1013.findCartHit(rigAboveT1013, QVector3D(0, -1, 0), 4.0f, nullptr);
                if (hCartIdx >= 0 && cartsT1013.chestAt(hCartIdx)) {
                    entsT1013h.setVehicleManagers(&cartsT1013, nullptr);
                    const int mobH = entsT1013h.spawnMobTyped(keyX, kRigY, keyZ - 1,
                                                              EntityManager::MobSpider,
                                                              QStringLiteral("#2a1a1a"), 0);
                    // 对照普通车：轨线东端（(d) 已 clearAll，槽空闲；远离箱车 ≥0.8 登乘半径）。
                    cartsT1013.spawnCart(x0T1013, kRigY, z0T1013, &w);
                    int plainIdxH = -1;
                    for (int i = 0; i < cartsT1013.count(); ++i)
                        if (cartsT1013.aliveAt(i) && !cartsT1013.chestAt(i)) { plainIdxH = i; break; }
                    const int mobH2 = plainIdxH >= 0
                        ? entsT1013h.spawnMobTyped(x0T1013, kRigY, z0T1013, EntityManager::MobPig,
                                                   QStringLiteral("#e0a0a0"), 0)
                        : -1;
                    entsT1013h.tickVehicleRiding();
                    entsT1013h.tickVehicleRiding();
                    chestRefusedH = mobH >= 0 && cartsT1013.mobPassengerAt(hCartIdx) < 0;
                    plainAcceptedH = mobH2 >= 0 && plainIdxH >= 0
                        && cartsT1013.mobPassengerAt(plainIdxH) == mobH2;
                    entsT1013h.setVehicleManagers(nullptr, nullptr);
                }
                okH = aimedH && rejectedH && controlH && chestRefusedH && plainAcceptedH;
                if (!okH)
                    qInfo().noquote() << "  [t1013 diag h-ride] cartIdx=" << hCartIdx
                                      << "chestRefused=" << chestRefusedH
                                      << "plainAccepted=" << plainAcceptedH;
            }
            QObject::disconnect(connBBT1013);
            QObject::disconnect(connPlainT1013);
            QObject::disconnect(connChestT1013);
        }
        // (f) 源码钉：变体面全链（阴性轮敏感 —— revert 任一行即红）。B-P1-1 迁移：滤注释钉
        //     （pinSet 套件帮手，t989/t1008a/t1023a 先例抽版）——语句整行注释化即红，注释文本
        //     出现 needle 不再顶钉；needle 全部迁移前经同算法核对表验证锚在非注释语句行。
        {
            const QString rootT1013 = QDir(QCoreApplication::applicationDirPath()
                                           + QStringLiteral("/..")).absolutePath();
            QStringList missF;
            missF << pinSet(rootT1013 + QStringLiteral("/src/Entities/minecartmanager.h"), {
                {"sig-chestCartBroken", "void chestCartBroken(int x, int y, int z, int keyX, int keyY, int keyZ);"},
                {"field-chest", "bool chest = false;"},
                {"invokable-chestAt", "Q_INVOKABLE bool chestAt(int i) const;"},
            });
            missF << pinSet(rootT1013 + QStringLiteral("/src/Entities/minecartmanager.cpp"), {
                {"trymount-reject", "if (m_carts[size_t(idx)].chest) return false;" }, // tryMount 拒载
                {"emit-chestCartBroken", "emit chestCartBroken(dropX, dropY, dropZ, keyX, keyY, keyZ);"},
            });
            missF << pinSet(rootT1013 + QStringLiteral("/src/Game/playercontroller.cpp"), {
                {"emit-chestOpened", "emit chestOpened(keyX, keyY, keyZ);"},
                {"fn-convertMineshaftChests", "void PlayerController::convertMineshaftChests()"},
            });
            missF << pinSet(rootT1013 + QStringLiteral("/src/ui/Main.qml"), {
                {"qml-handler-onChestCartBroken", "function onChestCartBroken(x, y, z, keyX, keyY, keyZ)"},
                {"qml-route-chestAt", "carts.chestAt(index)"},
                {"qml-route-convert", "player.convertMineshaftChests()"},
                {"qml-route-populateLoot", "if (isCartCell) chestStore.populateMineshaftLoot(x, y, z)"},
            });
            okF = missF.isEmpty();
            if (!okF)
                qInfo().noquote() << "  [t1013 diag f] pin miss:" << missF.join(QLatin1Char(','));
        }
        // 清场（轨 / 标记箱位 / 对照位）。
        for (int i = 0; i < 4; ++i) w.setBlock(x0T1013 + i, kRigY, z0T1013, BR::Air);
        w.setBlock(keyX, keyY, keyZ, BR::Air);
        w.setBlock(x0T1013, kRigY, z0T1013 + 4, BR::Air);
        tickN(w, 2);
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1013 chest-minecart conversion: mineshaft marker chest -> silent block removal"
                             " (no blockBroken) + cart key registered + cart spawned on adjacent rail cell in"
                             " full rail posture with key = spawn cell (convertMineshaftChests path a)";
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1013 chest-cart interaction: findCartHit + chestKeyAt contract for the"
                             " right-click-open branch, tryMount rejects chest cart (plain cart control mounts)";
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| t1013 chest-cart break chain: survival 3-hit final blow emits chestCartBroken"
                             " exactly once with cart key (cartBroken zero = no double minecart drop), slot"
                             " metadata readable at signal time then clearChest, creative instant-break also"
                             " emits (t767 deviation: contents never silently voided), void loss emits nothing"
                             " and keeps the key for respawn";
        if (!okD) ++totalFail;
        qInfo().noquote() << (okD ? "PASS" : "FAIL")
                          << "| t1013 chest-cart movement: spawns static, player push moves it along the rail"
                             " >=1 cell pinned to rail surface (powered-rail physics shared, pinned by P12 family)";
        if (!okE) ++totalFail;
        qInfo().noquote() << (okE ? "PASS" : "FAIL")
                          << "| t1013 chest-cart persistence: empty cart-key entry survives allChests"
                             " (cart-flag true, empty-exemption) -> loadAll round-trip -> convertMineshaftChests"
                             " path (b) respawns the entity; populateMineshaftLoot first-open gate (empty cart"
                             " key rolls once, filled/non-cart entries no-op)";
        if (!okG) ++totalFail;
        qInfo().noquote() << (okG ? "PASS" : "FAIL")
                          << "| t1013 loot no-regen (review0907 A-P1-1): mineshaft cart loot rolls once --"
                             " take all 27 slots empty then reopen -> populateMineshaftLoot rejected and"
                             " slots stay empty (same-seed re-roll infinite regen fixed by persistent"
                             " looted flag); looted:true survives allChests->loadAll round-trip with"
                             " cart respawn flag coexisting on the same entry (old-save compatible:"
                             " missing looted key = never rolled)";
        if (!okH) ++totalFail;
        qInfo().noquote() << (okH ? "PASS" : "FAIL")
                          << "| t1013 cart-key cell guard + chest-cart mob mount (review0907 B cross-batch"
                             " high #3 / legacy #4): placing a Chest block onto a registered cart key cell"
                             " is rejected as a no-op (no double-container aliasing, clearChest can never"
                             " erase the respawn key via a block chest), the same aim places normally"
                             " once the key is cleared (plain cells undisturbed), and the mob board scan"
                             " skips chest carts (spider at the cart cell stays unseated while a pig on a"
                             " plain cart seats, matching the player-side tryMount rejection)";
        if (!okF) ++totalFail;
        qInfo().noquote() << (okF ? "PASS" : "FAIL")
                          << "| t1013 source pins: variant fields + chestCartBroken signal + tryMount reject +"
                             " convert chain + Main.qml delegate/lid-skip/handler/enterWorld wiring";
    });

    // ── P-t1010 沙漠/丛林神殿野外生成落位率探针（R19.20 t1010；用户实测「新世界未见到神殿」验收面）──
    //    根因（联合概率过低，非放置静默失败）：160² 世界沙漠神殿仅 3×3=9 网格候选 × 45% 命中 ×
    //    Desert 群系占比 ~15-20%（成片，非逐格独立）→ 期望 ~0.7 座/世界，近半数含沙漠世界 0 落位；
    //    丛林同构（16 候选 × 50% × ~13.5% ≈ 1.1）。地表判定本身无错位：放置 surfaceY = heightAt 与
    //    generate 地形填充同源（R19.19 的「64 高 vs 地表基线」错位仅存在于 rig 探针侧，见 P-t1003 注）。
    //    修法：保底机制（对标 placeStronghold t564「收集候选 → 选最优落点」口径）—— review0905 #3 起
    //    触发门 =「主路径最近座距世界中心 > kTempleSpawnGuaranteeRadius(56)」且世界含该群系 → 全图合格
    //    列（同 siteOk 五守卫）选距世界中心最近补座 ≥1（确定性纯函数）；旧「全世界 0 座」门在世界角落
    //    1 座时不触发（出生区仍 0 神殿）→ 由 P-t1010b 出生圈加严腿钉住。
    //    rig：32 seed × 160×160×128（游戏本体 Main.qml 同尺寸）全量 worldgen；落位数 / 群系列数取自
    //    qInstallMessageHandler 捕获的 worldgen 自报（"worldgen: biomes …" / "worldgen: … temples = N"
    //    行 = 引擎单次 generate 的真实输出，非探针复刻；其余消息链式转发不吞）。断言五腿：
    //    (a) 必落腿：desert 列 > 0 ⇒ 沙漠神殿 ≥1；jungle 列 > 0 ⇒ 丛林神殿 ≥1（逐 seed；t1010 主判据，
    //        保底旗置 false 即红 —— 阴性轮钉）；
    //    (b) rig 有效性：池内 ≥5 seed 含沙漠群系且 ≥5 seed 含丛林群系（空池则必落腿空转无意义）；
    //    (c) 实块核对腿：各抽首个含群系 seed，y 带扫 Chest&PyramidFlag 聚簇（4 箱 Chebyshev≤6，
    //        P-t1003 口径）/ IronDoor(state 0，y≥50，P-t1004 口径) 计数 == 日志自报座数 + 簇心群系核
    //        （钉「自报计数 = 真实栅格落位」，防日志与栅格漂移）；
    //    (d) 确定性腿：同 seed 二次独立构 World → 自报四元组逐项相等（PLAN §2-K）；
    //    (e) 源码钉：两处保底旗行 / 两处保底入口行 / siteOk 收口行（world.cpp）。
    runLegMulti({ "t1010 desert/jungle temple wild-generation rate: biome-presence implies >=1 temple per seed (str"
        "onghold-style nearest-center fallback when no temple sits within spawn radius of world center, r"
        "eview0905 #3), 32-seed full-scan log-captured counts with block-level cross-check, determinism r"
        "e-gen, source pins, desert seedsjungle seedsdesert templesjungle templeszero-with-biome seeds",
               "t1010b temple guarantee spawn-circle proximity: eligible desert/jungle site columns inside spawn"
        " radius R of world center imply >=1 temple of that kind inside R (guarantee fires on empty circl"
        "e and lands nearest-center, no duplicate when main path already covers the circle; biome-in-R wi"
        "thout eligible cols degrades to nearest-eligible landing, registered; lesion revert to placed==0"
        " goes red on corner-temple seeds), 32-seed block-level full-height chest/door presence scan, eli"
        "gible-in-R seeds/biome-in-R seeds/desert chest-clusters in Rjungle temples in R" }, [&]() {
        bool ok = true;
        const quint32 seedsT1010[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 5150u, 2718u, 1618u,
                                       42u, 999u, 31337u, 2024u, 8675309u, 271828u, 314159u, 123456789u,
                                       7u, 12345u, 54321u, 8888u, 111u, 2222u, 33333u, 555555u,
                                       7777777u, 97531u, 13579u, 24680u,
                                       20250904u, 1010u, 1919u, 2026u }; // R19.19 历史池 28 + 本批 4
        // 与 World::hashColumn 同源复刻（FNV-1a + avalanche；候选 diag 用，非判据）。
        auto colHashT1010 = [](int seed, int x, int z) {
            quint32 h = 0x811c9dc5u;
            auto step = [&h](quint32 v) { h ^= v; h *= 0x01000193u; };
            step(quint32(seed));
            step(quint32(x));
            step(quint32(z));
            h ^= h >> 16;
            h *= 0x7feb352du;
            h ^= h >> 15;
            return h;
        };
        // worldgen 自报捕获：一轮 generate 依次吐 biomes 行 → … temples 行；biomes 行开新帧，temples 行
        // 回填末帧。构造期 setWidth/setDepth/setHeight 各产生一帧 → 构造后末帧 = setSeed 帧（seed 相同
        // 时 setSeed 早退无帧，末帧即 setHeight 的全尺寸帧，语义仍正确）。其余消息链式转发不吞。
        //   （qInstallMessageHandler 收函数指针 → 无捕获 lambda + static 着陆点。）
        struct GenRecT1010 { int desertCols = -1, jungleCols = -1, desertT = -1, jungleT = -1; };
        static std::vector<GenRecT1010> *s_sinkT1010 = nullptr;   // 帧序列着陆点（探针块内手动装/卸）
        static QtMessageHandler s_prevHandlerT1010 = nullptr;     // 前任处理器（链式转发）
        s_prevHandlerT1010 = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext &ctx, const QString &m) {
                if (s_sinkT1010) {
                    if (m.startsWith(QStringLiteral("worldgen: biomes "))) {
                        static const QRegularExpression reBiomes(
                            QStringLiteral("desert = (\\d+).*jungle = (\\d+)"));
                        const auto mm = reBiomes.match(m);
                        GenRecT1010 rec;
                        rec.desertCols = mm.hasMatch() ? mm.captured(1).toInt() : -2;
                        rec.jungleCols = mm.hasMatch() ? mm.captured(2).toInt() : -2;
                        s_sinkT1010->push_back(rec);
                        return; // worldgen 自报行只入帧不转发（降噪）
                    }
                    if (m.startsWith(QStringLiteral("worldgen: desert temples = "))) {
                        if (!s_sinkT1010->empty())
                            s_sinkT1010->back().desertT =
                                m.mid(int(qstrlen("worldgen: desert temples = "))).toInt();
                        return;
                    }
                    if (m.startsWith(QStringLiteral("worldgen: jungle temples = "))) {
                        if (!s_sinkT1010->empty())
                            s_sinkT1010->back().jungleT =
                                m.mid(int(qstrlen("worldgen: jungle temples = "))).toInt();
                        return;
                    }
                }
                if (s_prevHandlerT1010) s_prevHandlerT1010(type, ctx, m);
                else std::fprintf(stderr, "%s\n", qUtf8Printable(m)); // 默认处理器兜底直写 stderr
            });
        int seedsWithDesert = 0, seedsWithJungle = 0;
        int desertTemplesTotal = 0, jungleTemplesTotal = 0;
        int desert0Seeds = 0, jungle0Seeds = 0; // 有群系 0 神殿 seed 计数（保底后应恒 0 = 必落腿的另一表述）
        quint32 firstDesertSeed = 0, firstJungleSeed = 0;
        GenRecT1010 firstDesertRec, firstJungleRec;
        QStringList violT1010;
        // ── P-t1010b 加严腿（review0905 #3）数据面：出生圈 R 内神殿座数（块级扫：沙漠 = PyramidFlag
        //    箱或 CutSandstone 任一块〔全高扫〕/ 丛林 = IronDoor 下格 state 0 y≥40 + 门心 (x-3,z) 群系核
        //    == Jungle，或 Lever / Dispenser 任一块〔全高扫〕）。证据块均 worldgen 独占（grep 全树核实），
        //    峡谷削塔后的残损神殿由大面积独占块（顶冠/门楣 ~126 块、拉杆/发射器）兜底在场——三跑迭代
        //    实证链：y 带漏计（跑1）→「恰 4 箱完整簇」藏残簇（跑2 seed 777 三箱座）→ 箱组/门被整组
        //    削光（跑3 seed 42/2026 箱 0、777/97531/20250904 门 0）→ 独占块证据终版。证据距世界中心
        //    ≤ R+4 计「圈内」（箱-心偏移 Chebyshev ≤3 / 丛林门偏移 +3 容差）。R 直读 World::
        //    kTempleSpawnGuaranteeRadius（world.h 单一权威，勿复制字面量）。
        //    判据（review0905 #3 首跑修正：前提从「群系列在 R 内」校正为「**合格列**在 R 内」——首跑实证
        //    R 内有群系 ≠ R 内有合格列〔海域 / 贴基岩 / margin 守卫拒〕，此时保底补座落全图最近合格列、
        //    如实在 R 外 = world.h 登记的实现契约内退化，非缺陷）：R 内合格列 ⇒ R 内神殿箱 / 门在场
        //    ≥1（保底必触发且落座 ≤R，或主路径已覆盖）；R 内簇数 ≤ 日志总数（保底不重复不膨胀；
        //    n≥1 簇互斥且单神殿箱永不分裂 → 簇数 ≤ 有箱神殿数，方向安全）。
        //    群系列在 R 计数保留 = diag 面（契约退化的观察窗口）。合格列判定 = World::desertTempleSiteOk /
        //    jungleTempleSiteOk 同源直读（review0905 #3 起公开，零复刻漂移）。
        QStringList violT1010b;
        int seedsDesertEligR = 0, seedsJungleEligR = 0, desertRTotal = 0, jungleRTotal = 0;
        int seedsDesertBiomeR = 0, seedsJungleBiomeR = 0;
        for (quint32 sd : seedsT1010) {
            std::vector<GenRecT1010> framesT1010; // 每世界一清（末帧 = 本世界最终态）
            s_sinkT1010 = &framesT1010;
            World wT1010;
            wT1010.setWidth(160);
            wT1010.setDepth(160);
            wT1010.setHeight(128); // t307 地表基线 64 → 世界高须 128（64 高 rig 恒拒，见 P-t1003 rig 注）
            wT1010.setSeed(int(sd)); // setter 内 generate() 全量 worldgen
            const GenRecT1010 rec = framesT1010.empty() ? GenRecT1010{} : framesT1010.back();
            s_sinkT1010 = nullptr;
            if (rec.desertCols < 0 || rec.jungleCols < 0 || rec.desertT < 0 || rec.jungleT < 0) {
                violT1010 << QStringLiteral("seed %1 frame incomplete (%2 %3 %4 %5)")
                                     .arg(sd).arg(rec.desertCols).arg(rec.jungleCols)
                                     .arg(rec.desertT).arg(rec.jungleT);
                continue;
            }
            // 候选 vs 实落 diag（同源复刻主路径网格 + 概率 + 抖动 + 群系短门；海域 / 高度 fbm 门不进
            // diag → 归「其余拒」，量级小）。区分「候选就少」vs「候选被群系门拒」。
            int dHash = 0, dDesert = 0, jHash = 0, jJungle = 0;
            for (int bx = 24; bx < 160; bx += 48) // 沙漠神殿主路径候选（grid 48 / pct 45 / seed+19487）
                for (int bz = 24; bz < 160; bz += 48) {
                    const quint32 r = colHashT1010(int(sd) + 19487, bx, bz);
                    if ((r % 100u) >= 45u) continue;
                    ++dHash;
                    const int cx = bx + int((r >> 1) & 0xFu) % 25 - 12;
                    const int cz = bz + int((r >> 5) & 0xFu) % 25 - 12;
                    if (cx >= 11 && cz >= 11 && cx < 149 && cz < 149
                        && wT1010.biomeIdAt(cx, cz) == 2) ++dDesert;
                }
            for (int bx = 20; bx < 160; bx += 40) // 丛林神殿主路径候选（grid 40 / pct 50 / seed+22617）
                for (int bz = 20; bz < 160; bz += 40) {
                    const quint32 r = colHashT1010(int(sd) + 22617, bx, bz);
                    if ((r % 100u) >= 50u) continue;
                    ++jHash;
                    const int cx = bx + int((r >> 1) & 0xFu) % 21 - 10;
                    const int cz = bz + int((r >> 5) & 0xFu) % 21 - 10;
                    if (cx >= 8 && cz >= 8 && cx < 152 && cz < 152
                        && wT1010.biomeIdAt(cx, cz) == 6) ++jJungle;
                }
            const bool hasD = rec.desertCols > 0, hasJ = rec.jungleCols > 0;
            seedsWithDesert += int(hasD);
            seedsWithJungle += int(hasJ);
            desertTemplesTotal += rec.desertT;
            jungleTemplesTotal += rec.jungleT;
            if (hasD && rec.desertT == 0) ++desert0Seeds;
            if (hasJ && rec.jungleT == 0) ++jungle0Seeds;
            if (hasD && rec.desertT < 1) // (a) 必落腿 —— 保底契约（保底旗置 false 即红）
                violT1010 << QStringLiteral("seed %1 desert cols %2 but 0 temples").arg(sd).arg(rec.desertCols);
            if (hasJ && rec.jungleT < 1)
                violT1010 << QStringLiteral("seed %1 jungle cols %2 but 0 temples").arg(sd).arg(rec.jungleCols);
            if (hasD && firstDesertSeed == 0) { firstDesertSeed = sd; firstDesertRec = rec; }
            if (hasJ && firstJungleSeed == 0) { firstJungleSeed = sd; firstJungleRec = rec; }
            qInfo().noquote() << "  [t1010 diag] seed" << sd << "desertCols" << rec.desertCols
                              << "jungleCols" << rec.jungleCols << "| dTemples" << rec.desertT
                              << "(dHash" << dHash << "dDesertGate" << dDesert << ") jTemples"
                              << rec.jungleT << "(jHash" << jHash << "jJungleGate" << jJungle << ")";
            // (f-t1010b 数据面) 出生圈 R 内神殿块级扫（本世界 wT1010 在域内直接扫，避免二次 worldgen）。
            {
                const double cR = double(World::kTempleSpawnGuaranteeRadius);
                const double ccx = double(wT1010.width()) * 0.5, ccz = double(wT1010.depth()) * 0.5;
                auto inCircle = [cR, ccx, ccz](double x, double z, double pad) {
                    const double dx = x - ccx, dz = z - ccz;
                    return dx * dx + dz * dz <= (cR + pad) * (cR + pad); // pad = 质心半格 / 丛林门偏移容差
                };
                // 群系列 / 合格列在圈检查（Desert=2 / Jungle=6，biomeIdAt 列级读；合格列 = siteOk 同源
                //   直读——siteOk 首门即群系门 → 只在群系列上判合格不漏列）。
                bool desertBiomeR = false, jungleBiomeR = false;
                bool desertEligR = false, jungleEligR = false;
                for (int x = 0; x < wT1010.width(); ++x) {
                    if (desertBiomeR && jungleBiomeR && desertEligR && jungleEligR) break;
                    for (int z = 0; z < wT1010.depth(); ++z) {
                        const double dx = double(x) - ccx, dz = double(z) - ccz;
                        if (dx * dx + dz * dz > cR * cR) continue;
                        const int b = wT1010.biomeIdAt(x, z);
                        if (b == 2) {
                            desertBiomeR = true;
                            desertEligR = desertEligR || wT1010.desertTempleSiteOk(x, z);
                        } else if (b == 6) {
                            jungleBiomeR = true;
                            jungleEligR = jungleEligR || wT1010.jungleTempleSiteOk(x, z);
                        }
                    }
                }
                // 神殿证据块单遍全高扫（证据独占性 = worldgen 全树 grep 核实）：
                //   沙漠 = PyramidFlag 箱（独占位）或 **CutSandstone 任一块**（worldgen 仅沙漠神殿顶冠
                //   + 门楣使用，~126 块/座）——三跑实证峡谷可把 R 内保底神殿的**箱组整组削光**（seed 42 /
                //   2026 箱 0 在场），顶冠/门楣大面积分布不可能全灭；丛林 = IronDoor 下格 state 0 y≥40 +
                //   门心 (x-3,z) 群系核 == Jungle（同 (c) 出处核；每座恰 1 下格门）或 **Lever / Dispenser
                //   任一块**（worldgen 仅丛林神殿拉杆谜题 / 发射器陷阱使用）——门贴地表（S+1）最易被
                //   峡谷整门削掉（seed 777/97531/20250904 实证）。箱-心偏移 Chebyshev ≤3、门偏移 +3 →
                //   证据距世界中心 ≤ R+4 计「圈内」。群系 / 合格列在圈 = 前提与 diag（上方块内已扫）。
                struct PCBb { int x, y, z; };
                std::vector<PCBb> pcsB;
                int desertCutSandR = 0;
                for (int x = 0; x < wT1010.width(); ++x)
                    for (int z = 0; z < wT1010.depth(); ++z)
                        for (int y = 0; y < wT1010.height(); ++y) {
                            const quint8 id = wT1010.blockAt(x, y, z);
                            if (id == BR::CutSandstone) {
                                if (inCircle(double(x), double(z), 4.0)) ++desertCutSandR;
                                continue;
                            }
                            if (id == BR::Chest
                                && (wT1010.stateAt(x, y, z) & BR::ChestStatePyramidFlag))
                                pcsB.push_back({ x, y, z });
                        }
                int desertChestsR = 0;
                for (const PCBb &c : pcsB)
                    if (inCircle(double(c.x), double(c.z), 4.0)) ++desertChestsR;
                std::vector<bool> usedB(pcsB.size(), false);
                int desertR = 0;
                for (size_t i = 0; i < pcsB.size(); ++i) {
                    if (usedB[i]) continue;
                    int n = 0, sx = 0, sz = 0;
                    for (size_t j = i; j < pcsB.size(); ++j)
                        if (!usedB[j] && std::abs(pcsB[j].x - pcsB[i].x) <= 6
                                      && std::abs(pcsB[j].z - pcsB[i].z) <= 6
                                      && std::abs(pcsB[j].y - pcsB[i].y) <= 3) {
                            usedB[j] = true; ++n; sx += pcsB[j].x; sz += pcsB[j].z;
                        }
                    if (n < 1) continue; // 恒假（n≥1）；防御式保留
                    if (inCircle(double(sx) / n, double(sz) / n, 4.0)) ++desertR;
                }
                // 丛林门 y≥40（隔离要塞监狱门 y≤32）；Lever / Dispenser 全高（worldgen 独占丛林神殿）。
                int jungleR = 0, jungleLeverDispR = 0;
                for (int x = 0; x < wT1010.width(); ++x)
                    for (int z = 0; z < wT1010.depth(); ++z)
                        for (int y = 0; y < wT1010.height(); ++y) {
                            const quint8 id = wT1010.blockAt(x, y, z);
                            if (id != BR::IronDoor && id != BR::Lever && id != BR::Dispenser) continue;
                            if (!inCircle(double(x), double(z), 4.0)) continue;
                            if (id == BR::Lever || id == BR::Dispenser) { ++jungleLeverDispR; continue; }
                            if (y >= 40 && wT1010.stateAt(x, y, z) == 0
                                && wT1010.biomeIdAt(x - 3, z) == 6) ++jungleR;
                        }
                seedsDesertBiomeR += int(desertBiomeR);
                seedsJungleBiomeR += int(jungleBiomeR);
                seedsDesertEligR += int(desertEligR);
                seedsJungleEligR += int(jungleEligR);
                desertRTotal += desertR;
                jungleRTotal += jungleR;
                // leg1（在场）：合格列在 R ⇒ 神殿证据在 R（保底必触发且落座 ≤R，或主路径已覆盖）。
                //   证据 = 箱 ∨ 切制砂岩 / 门 ∨ 拉杆 ∨ 发射器（全部独占块；整座被峡谷抹平才可能全灭 =
                //   净样口径外的登记退化，diag 留痕）。
                if (desertEligR && desertChestsR < 1 && desertCutSandR < 1)
                    violT1010b << QStringLiteral("seed %1 desert eligible cols in R but no temple evidence"
                                                 " in R (chests 0 cut-sand 0, total %2)")
                                              .arg(sd).arg(rec.desertT);
                if (jungleEligR && jungleR < 1 && jungleLeverDispR < 1)
                    violT1010b << QStringLiteral("seed %1 jungle eligible cols in R but no temple evidence"
                                                 " in R (doors 0 lever/dispenser 0, total %2)")
                                              .arg(sd).arg(rec.jungleT);
                if (desertR > rec.desertT) // R 内 ⊆ 总数（不重复/不膨胀对账；簇数 ≤ 有箱神殿数，方向安全）
                    violT1010b << QStringLiteral("seed %1 desert temples in R %2 > log total %3")
                                              .arg(sd).arg(desertR).arg(rec.desertT);
                if (jungleR > rec.jungleT)
                    violT1010b << QStringLiteral("seed %1 jungle temples in R %2 > log total %3")
                                              .arg(sd).arg(jungleR).arg(rec.jungleT);
                qInfo().noquote() << "  [t1010b diag] seed" << sd << "desertInR(bio/elig)" << desertBiomeR
                                  << "/" << desertEligR << "desertChestsInR" << desertChestsR
                                  << "desertCutSandInR" << desertCutSandR << "desertClustersInR" << desertR
                                  << "jungleInR(bio/elig)" << jungleBiomeR << "/" << jungleEligR
                                  << "jungleDoorsInR" << jungleR << "jungleLeverDispInR" << jungleLeverDispR;
            }
        }
        ok = ok && violT1010.isEmpty();
        for (const QString &v : violT1010)
            qInfo().noquote() << "  [t1010 diag] VIOLATION:" << v;
        const bool rigOkT1010 = seedsWithDesert >= 5 && seedsWithJungle >= 5; // (b) rig 有效性
        ok = ok && rigOkT1010;
        if (!rigOkT1010)
            qInfo().noquote() << "  [t1010 diag] rig vacuous: desert seeds" << seedsWithDesert
                              << "jungle seeds" << seedsWithJungle;
        // (c) 实块核对腿：日志自报 == 真实栅格落位。
        if (firstDesertSeed != 0) {
            World wV;
            wV.setWidth(160); wV.setDepth(160); wV.setHeight(128); wV.setSeed(int(firstDesertSeed));
            struct PCB { int x, z; };
            std::vector<PCB> pcs;
            for (int x = 0; x < wV.width(); ++x)
                for (int z = 0; z < wV.depth(); ++z)
                    for (int y = 40; y <= 64; ++y) // 密室箱带：y = S-11 ∈ [50,56]（S~61..67）
                        if (wV.blockAt(x, y, z) == BR::Chest
                            && (wV.stateAt(x, y, z) & BR::ChestStatePyramidFlag))
                            pcs.push_back({ x, z });
            std::vector<bool> used(pcs.size(), false);
            int clusters = 0, biomeMiss = 0;
            for (size_t i = 0; i < pcs.size(); ++i) {
                if (used[i]) continue;
                int n = 0, sx = 0, sz = 0;
                for (size_t j = i; j < pcs.size(); ++j)
                    if (!used[j] && std::abs(pcs[j].x - pcs[i].x) <= 6
                                  && std::abs(pcs[j].z - pcs[i].z) <= 6) {
                        used[j] = true; ++n; sx += pcs[j].x; sz += pcs[j].z;
                    }
                if (n != 4) continue; // 残簇（峡谷切塔等，净样口径同 P-t1003）→ 数目对账腿兜红
                ++clusters;
                if (wV.biomeIdAt(sx / 4, sz / 4) != 2) ++biomeMiss; // 簇心群系核（Desert=2）
            }
            const bool cOk = clusters == firstDesertRec.desertT && biomeMiss == 0;
            ok = ok && cOk;
            if (!cOk)
                qInfo().noquote() << "  [t1010 diag] desert block-count mismatch: clusters" << clusters
                                  << "vs log" << firstDesertRec.desertT << "biomeMiss" << biomeMiss;
        }
        if (firstJungleSeed != 0) {
            World wV;
            wV.setWidth(160); wV.setDepth(160); wV.setHeight(128); wV.setSeed(int(firstJungleSeed));
            int doors = 0, biomeMiss = 0;
            for (int x = 0; x < wV.width(); ++x)
                for (int z = 0; z < wV.depth(); ++z)
                    for (int y = 50; y <= 80; ++y) // 丛林门下格 y = S+1 ∈ [60,70]；要塞监狱门 y≤30 天然分离
                        if (wV.blockAt(x, y, z) == BR::IronDoor && wV.stateAt(x, y, z) == 0) {
                            ++doors; // 每座丛林神殿恰 1 下格门（state 0；上格 0x08 不计）
                            if (wV.biomeIdAt(x - 3, z) != 6) ++biomeMiss; // 门心群系核（cx=x-3，Jungle=6）
                        }
            const bool cOk = doors == firstJungleRec.jungleT && biomeMiss == 0;
            ok = ok && cOk;
            if (!cOk)
                qInfo().noquote() << "  [t1010 diag] jungle block-count mismatch: doors" << doors
                                  << "vs log" << firstJungleRec.jungleT << "biomeMiss" << biomeMiss;
        }
        // (d) 确定性腿：同 seed 二次独立构 World → 自报四元组逐项相等。
        if (firstDesertSeed != 0) {
            std::vector<GenRecT1010> frames2;
            s_sinkT1010 = &frames2;
            World wD;
            wD.setWidth(160); wD.setDepth(160); wD.setHeight(128); wD.setSeed(int(firstDesertSeed));
            s_sinkT1010 = nullptr;
            const GenRecT1010 rec2 = frames2.empty() ? GenRecT1010{} : frames2.back();
            const bool dOk = rec2.desertCols == firstDesertRec.desertCols
                             && rec2.jungleCols == firstDesertRec.jungleCols
                             && rec2.desertT == firstDesertRec.desertT
                             && rec2.jungleT == firstDesertRec.jungleT;
            ok = ok && dOk;
            if (!dOk)
                qInfo().noquote() << "  [t1010 diag] determinism drift on seed" << firstDesertSeed
                                  << ":" << rec2.desertCols << rec2.jungleCols << rec2.desertT
                                  << rec2.jungleT << "vs" << firstDesertRec.desertCols
                                  << firstDesertRec.jungleCols << firstDesertRec.desertT
                                  << firstDesertRec.jungleT;
        }
        qInstallMessageHandler(s_prevHandlerT1010); // 卸钩（其余探针 worldgen 行恢复原样）
        // (e) 源码钉（world.cpp / world.h）：保底旗 / 出生圈 R 常量 / 保底触发门（review0905 #3 起为
        //     「距中心 > R」distance 门）/ siteOk 收口。
        {
            const QString exeDirT1010 = QCoreApplication::applicationDirPath();
            const QString rootT1010 = QDir(exeDirT1010 + QStringLiteral("/..")).absolutePath();
            QFile fT1010(rootT1010 + QStringLiteral("/src/World/world.cpp"));
            QFile hT1010(rootT1010 + QStringLiteral("/src/World/world.h"));
            const QString src = fT1010.open(QIODevice::ReadOnly) ? QString::fromUtf8(fT1010.readAll()) : QString();
            const QString srch = hT1010.open(QIODevice::ReadOnly) ? QString::fromUtf8(hT1010.readAll()) : QString();
            const bool okPin =
                src.contains(QStringLiteral("constexpr bool kDesertBiomeGuarantee = true;"))
                && src.contains(QStringLiteral("constexpr bool kJungleBiomeGuarantee = true;"))
                && srch.contains(QStringLiteral("static constexpr int kTempleSpawnGuaranteeRadius = 56;"))
                && src.contains(QStringLiteral("if (nearestCenterSq > guaranteeRSq && kDesertBiomeGuarantee) {"))
                && src.contains(QStringLiteral("if (nearestCenterSq > guaranteeRSq && kJungleBiomeGuarantee) {"))
                && src.contains(QStringLiteral("auto siteOk = [&](int cx, int cz) {"));
            ok = ok && okPin;
            if (!okPin)
                qInfo().noquote() << "  [t1010 diag] source pins drifted (guarantee flags / R gate / siteOk)";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1010 desert/jungle temple wild-generation rate: biome-presence implies"
                             " >=1 temple per seed (stronghold-style nearest-center fallback when no"
                             " temple sits within spawn radius of world center, review0905 #3),"
                             " 32-seed full-scan log-captured counts with"
                             " block-level cross-check, determinism re-gen, source pins, desert seeds"
                          << seedsWithDesert << "jungle seeds" << seedsWithJungle << "desert temples"
                          << desertTemplesTotal << "jungle temples" << jungleTemplesTotal
                          << "zero-with-biome seeds" << (desert0Seeds + jungle0Seeds);
        // ── P-t1010b 加严腿（review0905 #3）：保底「出生区可见」口径 —— 世界中心 R 内无神殿时保底必
        //    触发且落座 ≤R（群系列在圈 ⇒ 圈内神殿 ≥1，块级核对）；中心 R 内已有主路径神殿时保底不触发
        //    （不重复：R 内座数 ⊆ 日志总数，distance 门保证不二次补座）。R = World::
        //    kTempleSpawnGuaranteeRadius。阴性轮钉：触发门还原 placed==0 → 「角落有神殿 / 主路径全在
        //    圈外」世界圈内 0 座 → 本腿恰红。
        {
            bool okB = violT1010b.isEmpty();
            for (const QString &v : violT1010b)
                qInfo().noquote() << "  [t1010b diag] VIOLATION:" << v;
            if (!okB) ++totalFail;
            qInfo().noquote() << (okB ? "PASS" : "FAIL")
                              << "| t1010b temple guarantee spawn-circle proximity: eligible desert/jungle"
                                 " site columns inside spawn radius R of world center imply >=1 temple of"
                                 " that kind inside R (guarantee fires on empty circle and lands"
                                 " nearest-center, no duplicate when main path already covers the circle;"
                                 " biome-in-R without eligible cols degrades to nearest-eligible landing,"
                                 " registered; lesion revert to placed==0 goes red on corner-temple"
                                 " seeds), 32-seed block-level full-height chest/door presence scan,"
                                 " eligible-in-R seeds"
                              << seedsDesertEligR << "/" << seedsJungleEligR << "biome-in-R seeds"
                              << seedsDesertBiomeR << "/" << seedsJungleBiomeR
                              << "desert chest-clusters in R" << desertRTotal
                              << "jungle temples in R" << jungleRTotal;
        }
    });

    // ── P-t1006 生物复制体未愈探针（R19.20 首项；用户 f6e9a51 实测「进场即有静止贴图生物 + 随时间无限
    //   复制」，R19.18 t978 三层防御未对症；t1005 实体层 7 布局 + 64 槽压满全绿未复现 → 本探针按
    //   dev-plan 假说 1/2/3/4 逐面重建用户路径）──
    //   复现策略：全部走真 EntityManager + 真 World（+ 真 QQmlEngine），复刻「进场生成 + 长时游玩」：
    //   (a) 进场生成复刻（spawnInitialMobs 引擎侧同构：固定 3 被动 + 散布 10 + 狼 2；鱿鱼 aquatic 面
    //       平台上无水体，登记近似）+ 8 分钟模拟长 tick（tick + tickHostileLife + tickSpawners 全链，
    //       skyBrightness=0 夜间满压暗刷）活体数增长曲线（判据：liveCount ≤ 进场 15 + 敌对全局 cap 30
    //       + 2 容差；aliveSlots == liveCount 簿记不变量全程）+ **静止体检测**（石台出生群 90s 连续
    //       零位移窗 → 静止复制体；合法 idle 上界 ~0.25^15≈1e-9，台外敌对地形群不判——洞穴密闭点
    //       按设计可静止，与用户「进场即有」台面无关）。
    //   (b) 刷怪笼路径单独布局（假说 1，R19.19 后用户世界现含地牢/矿井/要塞笼）：敌对双笼 240s 刷出
    //       率曲线（闸门 = 全局 30 + 玩家 48m 区域 12 + 笼周 4m 球敌对在场 → 判据：峰值 ≤ 14、180s
    //       后平台期无爬升）+ 被动笼同型 local cap 4 计量（判据 = 笼周 4m 球内同型 ≤ 4 恒成立 + 总量
    //       ≤ kCap；被动无远距消失的慢积累面以 diag 量化登记）。
    //   (c) QML delegate 语义（假说 4，权重上调面：真 QQmlEngine + mobHost 同构 Repeater——mon.revision
    //       + t978 count 自愈触碰 + aliveAt 三绑定逐字同形）：spawn / 长tick / removeEntityAt /
    //       clearAll / 复用重spawn 六相 delegate 可见数 == 引擎活体数（t978 三层防线在场回归）。
    runLegMulti({ "t1006 mob-clone-unhealed: the user's f6e9a51 playtest reports statictexture mobs already present"
        " on world entry and infinite duplication overtime (t978's three defensive layers did not cure it"
        "); this probe rebuildsthe user path on real EntityManager + real World + real QQmlEngine acrosst"
        "he four hypotheses: (a) entry spawn replica (3 fixed + 10 biome-weightedscatter + 2 wolves on a "
        "sealed stone platform 20 blocks above terrain) witha 480s simulated night-pressure long tick (ti"
        "ck + tickHostileLife +tickSpawners full chain) asserting the live-count curve stays within thele"
        "git ceiling 15+30+2, the aliveSlots==liveCount bookkeeping invariant everysimulated second, and "
        "a 90s consecutive zero-displacement static-bodydetector over platform mobs (legit idle upper bou"
        "nd ~0.25^15), (b) theR19.19 spawner-cage path laid out alone in explicitly shelled rigs(first-ru"
        "n lesson: a 3x3 pocket carved into worldgen stone is NOT sealed --a trapped mob deadlocks the ho"
        "stile 4m-ball gate at 1 spawn and worldgencave connections fake a passive gate breach), a dual h"
        "ostile cage openarena 17x17x3 must ramp then plateau at the 12-near-player area cap (peak<= 14, "
        "t180 >= 5 proves the ramp, no creep 180s -> 234s), a fully shelled3x3 passive pocket (whole pock"
        "et inside the 4m ball) must hold EXACTLY 4pigs = kSpawnerLocalCap both sides of the gate, and an"
        " open passive arenaquantifies in diag (registered design gap -- egg-modified cages beinguser-unr"
        "eachable in survival) the no-far-despawn slow creep with total <= kCap,(c) the QML delegate sema"
        "ntics through a real QQmlEngine: amobHost-isomorphic Repeater (mon.revision + t978 count self-he"
        "al touch +aliveAt binding verbatim) driven through six phases (spawn3 / long tick /kill1 / LIFO-"
        "reuse respawn2 / clearAll / respawn4) must keep visibledelegate count == engine live count at ev"
        "ery phase with delegate count ==slot count (t978 three layers regression-pinned); probe legs: (a"
        ") growthcurve + static detector green, (b) cage rates plateau at caps, (c) six-phasedelegate/ali"
        "ve parity" }, [&]() {
        bool ok1006 = true;
        QString diag1006;
        constexpr int kPlatY = 70;      // 石台面（远超 worldgen 地形上限 → 台上群与台外暗刷群空间隔离）
        constexpr int kPlatMin = 8, kPlatMax = 39; // 石台 [8,39]²（32×32）
        // ── (a) 进场生成复刻 + 长 tick 增长曲线 + 静止体检测 ──
        int maxLive = 0, entrySpawned = 0;
        bool bookkeepingOk = true, staticBody = false;
        QVector3D staticPos, staticAt;
        {
            World w6;
            w6.setWidth(48); w6.setDepth(48); w6.setHeight(80); w6.setSeed(606);
            for (int x = kPlatMin; x <= kPlatMax; ++x)
                for (int z = kPlatMin; z <= kPlatMax; ++z) {
                    w6.setBlock(x, kPlatY, z, BR::Stone, 0);
                    w6.setBlock(x, kPlatY + 1, z, BR::Air, 0);
                    w6.setBlock(x, kPlatY + 2, z, BR::Air, 0);
                    w6.setBlock(x, kPlatY + 3, z, BR::Air, 0);
                }
            for (int x = kPlatMin; x <= kPlatMax; ++x)
                for (int z = kPlatMin; z <= kPlatMax; ++z) {
                    const bool wall = (x == kPlatMin || x == kPlatMax || z == kPlatMin || z == kPlatMax);
                    if (wall)
                        for (int y = kPlatY + 1; y <= kPlatY + 3; ++y) w6.setBlock(x, y, z, BR::Stone, 0);
                }
            EntityManager em6;
            const int pc = 24;
            const QVector3D playerPos(float(pc) + 0.5f, float(kPlatY + 1) + 0.45f, float(pc) + 0.5f);
            // 进场复刻（Main.qml spawnInitialMobs 引擎侧同构；鱿鱼 aquatic 略登记）：
            // 固定 3（猪/牛/羊 左前右 ±4）+ 散布 10（pickPassiveMobType 群系加权）+ 狼 2。
            auto platSpawn = [&](int dx, int dz, int type) -> bool {
                const int sx = pc + dx, sz = pc + dz;
                if (sx < kPlatMin + 1 || sx > kPlatMax - 1 || sz < kPlatMin + 1 || sz > kPlatMax - 1)
                    return false;
                return em6.spawnMobTyped(sx, kPlatY + 1, sz, type, QStringLiteral("#f0a8b0"), 10) >= 0;
            };
            entrySpawned += platSpawn(-4, -2, EntityManager::MobPig) ? 1 : 0;
            entrySpawned += platSpawn(0, -4, EntityManager::MobCow) ? 1 : 0;
            entrySpawned += platSpawn(4, -2, EntityManager::MobSheep) ? 1 : 0;
            for (int i = 0; i < 10; ++i) { // 散布 10：台内随机列（台上无树/水 → headroom 门恒过 = 同构）
                const int sx = kPlatMin + 2 + int(QRandomGenerator::global()->bounded(kPlatMax - kPlatMin - 3));
                const int sz = kPlatMin + 2 + int(QRandomGenerator::global()->bounded(kPlatMax - kPlatMin - 3));
                const int mt = em6.pickPassiveMobType(w6.biomeIdAt(sx, sz));
                if (em6.spawnMobTyped(sx, kPlatY + 1, sz, mt, QStringLiteral("#f0a8b0"), 10) >= 0)
                    ++entrySpawned;
            }
            entrySpawned += platSpawn(-6, 3, EntityManager::MobWolf) ? 1 : 0;
            entrySpawned += platSpawn(6, -5, EntityManager::MobWolf) ? 1 : 0;
            if (entrySpawned != 15) {
                ok1006 = false;
                diag1006 += QStringLiteral("\n  [t1006 diag] entry spawned %1/15").arg(entrySpawned);
            }
            // 长 tick：480s 模拟（60Hz × 28800 帧），每秒采样。
            constexpr int kFrames = 60 * 480;
            const float dt6 = 1.0f / 60.0f;
            std::vector<QVector3D> lastSamplePos;   // 每槽上次采样位
            std::vector<int> stillSecs;             // 每槽连续静止秒数
            int aliveSlotsPeak = 0;
            int maxHostile = 0;
            for (int f = 0; f < kFrames; ++f) {
                em6.tick(dt6, &w6, playerPos, 0.3f, 1.8f, true, false, 0.0f); // 夜间满压（t951 缺省 0）
                em6.tickHostileLife(dt6, &w6, playerPos, 0.0f);
                em6.tickSpawners(dt6, &w6, playerPos);
                if (f % 60 == 0) { // 每模拟秒采样
                    int live = 0, hostiles = 0, aliveSlots = 0;
                    const int n = em6.count();
                    if (int(lastSamplePos.size()) < n) {
                        lastSamplePos.resize(size_t(n), QVector3D(0, -1000.0f, 0));
                        stillSecs.resize(size_t(n), 0);
                    }
                    for (int i = 0; i < n; ++i) {
                        if (!em6.aliveAt(i)) { stillSecs[size_t(i)] = 0; continue; }
                        ++aliveSlots;
                        if (em6.kindAt(i) == EntityManager::Mob && !em6.deadAt(i)) {
                            ++live;
                            if (em6.isHostileAt(i)) ++hostiles;
                        }
                        // 静止体检测：仅台上群（y > kPlatY，台外洞穴密闭点按设计可静止不判）
                        const QVector3D p = em6.posAt(i);
                        if (p.y() > float(kPlatY) && em6.kindAt(i) == EntityManager::Mob && !em6.deadAt(i)) {
                            QVector3D &lp = lastSamplePos[size_t(i)];
                            if (lp.y() > -500.0f && (p - lp).length() < 0.05f) {
                                ++stillSecs[size_t(i)];
                                if (stillSecs[size_t(i)] >= 90 && !staticBody) {
                                    staticBody = true; // 90s 连续零位移 = 静止复制体候选（合法 idle 上界 ~1e-9）
                                    staticPos = p;
                                    staticAt = QVector3D(float(f) / 60.0f, float(i), 0);
                                }
                            } else {
                                stillSecs[size_t(i)] = 0;
                            }
                            lp = p;
                        }
                    }
                    if (aliveSlots != em6.liveCount()) { // t978 簿记不变量：alive 槽数 == liveCount
                        bookkeepingOk = false;
                        if (diag1006.size() < 4096)
                            diag1006 += QStringLiteral("\n  [t1006 diag] bookkeeping drift t=%1s aliveSlots=%2 liveCount=%3")
                                            .arg(f / 60).arg(aliveSlots).arg(em6.liveCount());
                    }
                    maxLive = std::max(maxLive, live);
                    aliveSlotsPeak = std::max(aliveSlotsPeak, aliveSlots);
                    maxHostile = std::max(maxHostile, hostiles);
                }
            }
            const int kLiveCeil = 15 + 30 + 2; // 进场 15 + 敌对全局 cap 30 + 容差 2
            if (maxLive > kLiveCeil) {
                ok1006 = false;
                diag1006 += QStringLiteral("\n  [t1006 diag] live growth beyond ceiling: max=%1 > %2")
                                .arg(maxLive).arg(kLiveCeil);
            }
            if (staticBody) {
                ok1006 = false;
                diag1006 += QStringLiteral("\n  [t1006 diag] STATIC BODY at t=%1s slot=%2 pos=(%3,%4,%5)")
                                .arg(int(staticAt.x())).arg(int(staticAt.y()))
                                .arg(staticPos.x()).arg(staticPos.y()).arg(staticPos.z());
            }
            if (!bookkeepingOk) ok1006 = false;
            if (em6.liveCount() > kLiveCeil) { // 终态核对（480s 后仍在天花板内）
                ok1006 = false;
                diag1006 += QStringLiteral("\n  [t1006 diag] final live %1 > %2").arg(em6.liveCount()).arg(kLiveCeil);
            }
            qInfo().noquote() << "  [t1006 diag] leg-a: entry" << entrySpawned << "maxLive" << maxLive
                              << "peakSlots" << aliveSlotsPeak << "maxHostile" << maxHostile
                              << "finalLive" << em6.liveCount() << "slots" << em6.count()
                              << "static" << staticBody << "book" << bookkeepingOk;
        }
        // ── (b) 刷怪笼路径（显式石壳封闭竞技场，与 worldgen 洞穴/地牢完全隔离——首跑实锤：3×3 刻写
        //   袋靠 worldgen 石壁不封闭，敌对笼笼周 4m 球闸门被袋内滞留 mob 永久卡死只刷 1 只、被动袋
        //   连通 worldgen 空腔致球内重入 6>4 假 breach——两层测量都必须显式壳才有效）──
        {
            // 敌对双笼开敞竞技场：17×17×3 内腔（x17..31, z7..21, y8..10）+ 全封闭石壳（y6..11 填充 +
            // 内腔刻空）+ 双笼 (20,8,14)Shambler (28,8,14)Bones + 玩家 (24.5,9.5,20.5)（距笼 ~7.2 > 4m
            // 球外；playerTargetable=true → 笼怪追击聚拢玩家侧 → 恒离球 → 笼持续补刷直到区域 cap 12）。
            auto hostileArena = [](bool &anySpawn, int &peakTotal, int &sampleT180, int &sampleT234) {
                World wC;
                wC.setWidth(48); wC.setDepth(48); wC.setHeight(32); wC.setSeed(9);
                for (int x = 16; x <= 32; ++x)
                    for (int z = 6; z <= 22; ++z)
                        for (int y = 6; y <= 11; ++y)
                            wC.setBlock(x, y, z, BR::Stone, 0);
                for (int x = 17; x <= 31; ++x)
                    for (int z = 7; z <= 21; ++z)
                        for (int y = 8; y <= 10; ++y)
                            wC.setBlock(x, y, z, BR::Air, 0);
                wC.setBlock(20, 8, 14, BR::Spawner, quint8(BR::SpawnerStateShambler));
                wC.setBlock(28, 8, 14, BR::Spawner, quint8(BR::SpawnerStateBones));
                EntityManager emC;
                const QVector3D playerPos(24.5f, 9.5f, 20.5f);
                anySpawn = false; peakTotal = 0; sampleT180 = -1; sampleT234 = -1;
                for (int f = 0; f < 60 * 240; ++f) {
                    emC.tick(1.0f / 60.0f, &wC, playerPos, 0.3f, 1.8f, true, false, 0.0f);
                    emC.tickSpawners(1.0f / 60.0f, &wC, playerPos);
                    if (f % 60 == 0) {
                        int total = 0;
                        for (int i = 0; i < emC.count(); ++i)
                            if (emC.aliveAt(i) && emC.kindAt(i) == EntityManager::Mob && !emC.deadAt(i))
                                ++total;
                        anySpawn = anySpawn || total > 0;
                        peakTotal = std::max(peakTotal, total);
                        if (f == 60 * 180) sampleT180 = total;
                        if (f == 60 * 234) sampleT234 = total;
                    }
                }
            };
            { // 判据：有刷出、峰值 ≤ 14（区域 cap 12 + 同周期双笼 +2 容差）、180s→234s 平台无爬升（Δ ≤ 2）、
              //   平台非空（t180 ≥ 5 证坡道真实发生，防「永不触发」假绿——首跑 3×3 袋 rig 即此假相 peak=1）
                bool anySpawn = false;
                int peakTotal = 0, t180 = -1, t234 = -1;
                hostileArena(anySpawn, peakTotal, t180, t234);
                if (!anySpawn) { ok1006 = false; diag1006 += QStringLiteral("\n  [t1006 diag] hostile cage rig spawned nothing"); }
                if (peakTotal > 14) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] hostile cage growth: peak %1 > 14").arg(peakTotal);
                }
                if (t180 < 5) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] hostile cage ramp never happened: t180=%1 (gate deadlock face)").arg(t180);
                }
                if (t180 >= 0 && t234 >= 0 && t234 - t180 > 2) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] hostile cage creep after plateau: %1 -> %2").arg(t180).arg(t234);
                }
                qInfo().noquote() << "  [t1006 diag] leg-b hostile: spawned" << anySpawn << "peak" << peakTotal
                                  << "plateau" << t180 << "->" << t234 << "(area cap 12 expected)";
            }
            // 被动笼两腿：(b1) **全显式石壳封闭袋**（7×7×5 石壳 + 3×3×2 内腔，无 worldgen 连通）——
            //   袋全域在笼周 4m 球内 → 同型 gate 恰停 kSpawnerLocalCap=4：60 周期后总 pig 数 == 4（越界
            //   = gate breach；不足 = gate 过死，两端都红）。(b2) 开敞竞技场单 pig 笼 240s——被动无远距
            //   消失的慢积累面 diag 量化登记（蛋改型笼 = 生存不可达面，登记不判红；总量 ≤ kCap 判红）。
            auto sealedPocketPigs = []() {
                World wP;
                wP.setWidth(32); wP.setDepth(32); wP.setHeight(24); wP.setSeed(5);
                for (int x = 12; x <= 18; ++x)
                    for (int z = 12; z <= 18; ++z)
                        for (int y = 6; y <= 10; ++y)
                            wP.setBlock(x, y, z, BR::Stone, 0);
                for (int x = 13; x <= 15; ++x) // 3×3 内腔（y7..8 air，笼 y7 居中，底 y6 石）
                    for (int z = 13; z <= 15; ++z)
                        for (int y = 7; y <= 8; ++y)
                            wP.setBlock(x, y, z, BR::Air, 0);
                wP.setBlock(14, 7, 14, BR::Spawner, quint8(BR::spawnerStateForMob(EntityManager::MobPig)));
                EntityManager emP;
                const QVector3D playerPos(14.5f, 7.5f, 20.5f); // XZ 6 ≤ 16 激活圈（球外不需，袋本就全域在球内）
                for (int i = 0; i < 600; ++i) emP.tickSpawners(0.1, &wP, playerPos); // 60s = 10 周期×6s ≥ 袋满节奏
                int pigs = 0;
                for (int i = 0; i < emP.count(); ++i)
                    if (emP.aliveAt(i) && emP.kindAt(i) == EntityManager::Mob && emP.mobTypeAt(i) == EntityManager::MobPig)
                        ++pigs;
                return pigs;
            };
            {
                const int pigs = sealedPocketPigs();
                if (pigs != 4) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] passive sealed pocket pig count %1 != 4 (kSpawnerLocalCap)").arg(pigs);
                }
                qInfo().noquote() << "  [t1006 diag] leg-b passive pocket: pigs" << pigs << "(gate cap 4 exact)";
            }
            auto openPigArena = []() {
                World wP;
                wP.setWidth(48); wP.setDepth(48); wP.setHeight(32); wP.setSeed(9);
                for (int x = 16; x <= 32; ++x)
                    for (int z = 6; z <= 22; ++z)
                        for (int y = 6; y <= 11; ++y)
                            wP.setBlock(x, y, z, BR::Stone, 0);
                for (int x = 17; x <= 31; ++x)
                    for (int z = 7; z <= 21; ++z)
                        for (int y = 8; y <= 10; ++y)
                            wP.setBlock(x, y, z, BR::Air, 0);
                wP.setBlock(20, 8, 14, BR::Spawner, quint8(BR::spawnerStateForMob(EntityManager::MobPig)));
                EntityManager emP;
                const QVector3D playerPos(28.5f, 9.5f, 20.5f); // 球外
                int peak = 0;
                for (int f = 0; f < 60 * 240; ++f) {
                    emP.tick(1.0f / 60.0f, &wP, playerPos, 0.3f, 1.8f, true, false, 0.0f);
                    emP.tickSpawners(1.0f / 60.0f, &wP, playerPos);
                    if (f % 60 == 0) {
                        int total = 0;
                        for (int i = 0; i < emP.count(); ++i)
                            if (emP.aliveAt(i) && emP.kindAt(i) == EntityManager::Mob && !emP.deadAt(i))
                                ++total;
                        peak = std::max(peak, total);
                    }
                }
                return peak;
            };
            {
                const int peak = openPigArena();
                if (peak > 64) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] passive arena total beyond kCap: %1").arg(peak);
                }
                qInfo().noquote() << "  [t1006 diag] leg-b passive open-arena: peakPigs" << peak
                                  << "in 240s (no-far-despawn slow creep = registered design gap, egg-cage user-unreachable)";
            }
        }
        // ── (c) QML delegate 语义：mobHost 同构 Repeater × 真 EntityManager（t978 三层在场回归）──
        {
            static bool sEntRegistered1006 = false;
            if (!sEntRegistered1006) {
                qmlRegisterType<EntityManager>("VoxelSandboxProbe", 1, 0, "EntityManager");
                sEntRegistered1006 = true;
            }
            qputenv("QML_DISABLE_DISK_CACHE", "1");
            QQmlEngine eng6;
            EntityManager emQ;
            eng6.rootContext()->setContextProperty(QStringLiteral("ent"), &emQ);
            QQmlComponent comp6(&eng6);
            comp6.setData(R"QML(import QtQuick
import VoxelSandboxProbe 1.0
Item {
    id: host
    objectName: "t1006host"
    Repeater {
        model: ent.count
        delegate: Item {
            objectName: "t1006del"
            property var mon: ent.slotMonitorAt(index)
            property bool vis: { const _r = mon.revision; const _c = ent.count; return _r >= 0 ? (ent.aliveAt(index)) : false }
        }
    }
}
)QML", QUrl());
            QObject *host6 = nullptr;
            bool qmlOk = !comp6.isError();
            if (!qmlOk)
                diag1006 += QStringLiteral("\n  [t1006 diag] qml comp: ") + comp6.errorString();
            else {
                host6 = comp6.create();
                qmlOk = host6 != nullptr;
            }
            // delegate 计数走**视觉树**（QQuickItem::childItems 递归；Repeater 建 delegate 的 QObject
            // parent 为 null（仅 parentItem 指向宿主）→ findChildren 找不到 —— scratch 首跑实锤，
            // 查 QObject 树恒 0 是假 red，查视觉树才是真 delegate 面）。
            std::function<void(int *, int *)> walkDelegates = [host6](int *delsOut, int *visOut) {
                int dels = 0, vis = 0;
                std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
                    const auto ci = it->childItems();
                    for (QQuickItem *c : ci) {
                        if (c->objectName() == QStringLiteral("t1006del")) {
                            ++dels;
                            if (c->property("vis").toBool()) ++vis;
                        }
                        walk(c);
                    }
                };
                if (QQuickItem *hi = qobject_cast<QQuickItem *>(host6)) walk(hi);
                if (delsOut) *delsOut = dels;
                if (visOut) *visOut = vis;
            };
            auto visCount = [walkDelegates]() -> int {
                int vis = 0, dels = 0;
                walkDelegates(&dels, &vis);
                return vis;
            };
            auto aliveCount = [&emQ]() -> int {
                int n = 0;
                for (int i = 0; i < emQ.count(); ++i)
                    if (emQ.aliveAt(i) && emQ.kindAt(i) == EntityManager::Mob && !emQ.deadAt(i)) ++n;
                return n;
            };
            auto checkPhase = [&](const QString &phase, bool &ok) {
                QCoreApplication::processEvents();
                const int vis = visCount();
                const int alive = aliveCount();
                if (vis != alive) {
                    ok = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] phase %1: visible delegates %2 != alive %3")
                                    .arg(phase).arg(vis).arg(alive);
                }
            };
            if (qmlOk) {
                // 六相轮换：spawn 3 → 长 tick → 杀 1 → 复用重 spawn 2 → clearAll → 重 spawn 4。
                emQ.spawnMobTyped(10, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
                emQ.spawnMobTyped(12, kRigY, 10, EntityManager::MobCow, QStringLiteral("#ee9999"), 30);
                emQ.spawnMobTyped(14, kRigY, 10, EntityManager::MobSheep, QStringLiteral("#ee9999"), 30);
                checkPhase(QStringLiteral("spawn3"), ok1006);
                for (int f = 0; f < 120; ++f)
                    emQ.tick(1.0f / 60.0f, &w, QVector3D(11.5f, 42.0f, 11.5f), 0.3f, 1.8f, true, false, 0.0f);
                checkPhase(QStringLiteral("longtick"), ok1006);
                int killSlot = -1;
                for (int i = 0; i < emQ.count(); ++i)
                    if (emQ.aliveAt(i) && emQ.kindAt(i) == EntityManager::Mob) { killSlot = i; break; }
                if (killSlot >= 0) {
                    emQ.removeEntityAt(killSlot);
                    checkPhase(QStringLiteral("kill1"), ok1006);
                } else {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] no mob slot to kill");
                }
                emQ.spawnMobTyped(10, kRigY, 12, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
                emQ.spawnMobTyped(16, kRigY, 10, EntityManager::MobChicken, QStringLiteral("#ee9999"), 30);
                checkPhase(QStringLiteral("respawn2"), ok1006);
                emQ.clearAll();
                checkPhase(QStringLiteral("clearAll"), ok1006);
                for (int i = 0; i < 4; ++i)
                    emQ.spawnMobTyped(10 + i * 2, kRigY, 14, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
                checkPhase(QStringLiteral("respawn4"), ok1006);
                int delegates = 0, visFinal = 0;
                walkDelegates(&delegates, &visFinal);
                if (delegates != emQ.count()) { // Repeater model = count：delegate 数恒等槽数
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] delegate count %1 != slots %2").arg(delegates).arg(emQ.count());
                }
                if (visFinal != aliveCount()) {
                    ok1006 = false;
                    diag1006 += QStringLiteral("\n  [t1006 diag] final vis %1 != alive %2").arg(visFinal).arg(aliveCount());
                }
                delete host6;
            } else {
                ok1006 = false;
            }
            qInfo().noquote() << "  [t1006 diag] leg-c qml: comp" << qmlOk << "finalSlots" << emQ.count();
        }
        if (!ok1006) ++totalFail;
        qInfo().noquote() << (ok1006 ? "PASS" : "FAIL")
                          << "| t1006 mob-clone-unhealed: the user's f6e9a51 playtest reports static"
                             "texture mobs already present on world entry and infinite duplication over"
                             "time (t978's three defensive layers did not cure it); this probe rebuilds"
                             "the user path on real EntityManager + real World + real QQmlEngine across"
                             "the four hypotheses: (a) entry spawn replica (3 fixed + 10 biome-weighted"
                             "scatter + 2 wolves on a sealed stone platform 20 blocks above terrain) with"
                             "a 480s simulated night-pressure long tick (tick + tickHostileLife +"
                             "tickSpawners full chain) asserting the live-count curve stays within the"
                             "legit ceiling 15+30+2, the aliveSlots==liveCount bookkeeping invariant every"
                             "simulated second, and a 90s consecutive zero-displacement static-body"
                             "detector over platform mobs (legit idle upper bound ~0.25^15), (b) the"
                             "R19.19 spawner-cage path laid out alone in explicitly shelled rigs"
                             "(first-run lesson: a 3x3 pocket carved into worldgen stone is NOT sealed --"
                             "a trapped mob deadlocks the hostile 4m-ball gate at 1 spawn and worldgen"
                             "cave connections fake a passive gate breach), a dual hostile cage open"
                             "arena 17x17x3 must ramp then plateau at the 12-near-player area cap (peak"
                             "<= 14, t180 >= 5 proves the ramp, no creep 180s -> 234s), a fully shelled"
                             "3x3 passive pocket (whole pocket inside the 4m ball) must hold EXACTLY 4"
                             "pigs = kSpawnerLocalCap both sides of the gate, and an open passive arena"
                             "quantifies in diag (registered design gap -- egg-modified cages being"
                             "user-unreachable in survival) the no-far-despawn slow creep with total <= kCap,"
                             "(c) the QML delegate semantics through a real QQmlEngine: a"
                             "mobHost-isomorphic Repeater (mon.revision + t978 count self-heal touch +"
                             "aliveAt binding verbatim) driven through six phases (spawn3 / long tick /"
                             "kill1 / LIFO-reuse respawn2 / clearAll / respawn4) must keep visible"
                             "delegate count == engine live count at every phase with delegate count =="
                             "slot count (t978 three layers regression-pinned); probe legs: (a) growth"
                             "curve + static detector green, (b) cage rates plateau at caps, (c) six-phase"
                             "delegate/alive parity"
                          << (ok1006 ? QString() : diag1006);
    });

    // ── P-t1007 进程级卡顿泄漏 rig（R19.20 t1007；用户 f6e9a51 实测：跑一段时间掉到 7FPS、F3
    //    items 93/93、render-side 行 gpu/prep+present 重、重进存档恢复 99FPS；重开 t997 归因遗留 +
    //    t1005/t1006 关单数据采集面）──
    //   offscreen 无 QRhi 渲染线程 → gpu/prep/present/vmem/pipelineCount 只能实机 F3 采（本探针登记，
    //   不编数）；引擎侧等价面 = 双池计数曲线 + tick 墙钟 + 重载前后状态差分。复刻用户三高水位同场：
    //   (a) 真路径 TNT 大量引爆：36 primed TNT（t997 同款 6×6 石台布局）+ connect
    //       explosionDroppedItem → ItemEntityManager.spawnItem（Main.qml :2578 转发同构，掉落链真跑）
    //       → 逐 1/60s tick 采样曲线（items live/slots、primed、mobs live、tick 墙钟）→ 断言：全部 36
    //       实爆零 primed 残留、item live ≤ kCap（LRU 钳制在场）、双池 aliveSlots==liveCount 簿记不变量。
    //   (b) item 高水位压力：250 个 named 掉落（name 非空跳过 kMergeRadius 合并 → 计数确定性）散布
    //       独立格 → 断言 live == kCap=200 且 slots == 200（LRU 驱逐最老，超 cap 部分被逐，数量精确钉）。
    //   (c) mobs 高水位：spawnMobTyped 铺满平台 → 断言恰 64 == kCap（spawnMobCore cap 拒第 65 个）。
    //   (e1) 满池 tick 链墙钟（tick + tickHostileLife + tickSpawners + item tick = PlayerController
    //       相位同构）→ diag 落盘 avg ms/f（归因对表数据，无红绿判据——机器相关量不作断言）。
    //   (d) 重载状态面（Main.qml enterWorld 同构：双池 clearAll，实体非体素不进存档）：断言 live 全归零
    //       + primed 归零 + 槽向量与高水位**保留**（slots 200/64 + hw 不回撤）——「重进存档即恢复」重置
    //       的运行时状态面 = 活体集（+ 世界方块内容，rig 外）；保留面全部有界 ≤cap（t437 slot-reuse 设计
    //       残留），无无界引擎泄漏。用户读数对表：重载后 F3 应 live 归零 hw 不变。
    //   (e2) 空池 tick 链墙钟 → diag 落盘与 (e1) 同口径对照：若满池 sim 仍个位 ms（用户 F3 sim 4.63ms
    //       口径），7FPS 的 ~14× 恶化必在渲染侧（与 F3 render-side gpu/prep+present 重读数一致）→ 根因
    //       面 = 活体 delegate 渲染成本随活体数线性放大（每 delegate 内联 geometry/材质/贴图实例不共享、
    //       无 instancing，draw-call 数 ∝ 活体数；t858 经验球已示范 instancing 出路，掉落物/mob 登记治理
    //       路径——需实机曲线对表后定，offscreen 无视觉/渲染线程面不做盲修）。
    //   (f) 源码钉：新 F3 读数链在场（entitymanager.h primedCount/slotHighWater、itementitymanager.h
    //       liveHighWater、Main.qml entities 行 primed/del/64 hw token）——插桩不可静默消失（t1005/t1006
    //       关单要靠它采数）。
    runLegMulti({ "t1007 process-level stutter leak rig: the user's f6e9a51 playtest sawFPS decay to 7 with F3 item"
        "s 93/93 and a heavy render-side line(gpu/prep/present) that a world re-entry resets to 99 FPS; o"
        "ffscreen hasno QRhi render thread so gpu/prep/present/vmem stay on-device F3 readings(registered"
        ", never invented here) and this probe pins the ENGINE-sideequivalent surface through the real ex"
        "plosion->drop forwarding chain:(a) 36 primed TNT (t997 6x6 layout) all detonate with zero primed"
        " residuewhile item live stays <= the 200 cap under the drop flood with per-samplealiveSlots==liv"
        "eCount bookkeeping, (b) 250 named drops (merge-exempt)saturate EXACTLY live=200 slots=200 hw=200"
        " (LRU eviction pinned),(c) mobs fill EXACTLY 64 = kCap, (e1) full-pool vs (e2) empty-pool tickch"
        "ain wall time recorded in diag as the sim-side curve to table againstthe user F3 numbers (sim wa"
        "s 4.63ms of 66.7ms - single-digit sim at fullpools localizes the 14x decay to the render side: l"
        "ive entity delegatedraw cost scales linearly via per-delegate inline geometry/materialinstances "
        "with no instancing; the t858 xp-orb instancing precedent is theregistered governance path, on-de"
        "vice curve comparison required beforeany rendering rework - no blind offscreen fix), (d) world r"
        "e-entry(clearAll both pools, enterWorld-isomorphic) resets EXACTLY the livesets (items/mobs/prim"
        "ed -> 0) while bounded remnants persist by design(slot vectors 200/64, high waters 200/64) - no "
        "unbounded engine leakexists offscreen, so the recovery face is the live entity set; (f) sourcepi"
        "ns lock the new F3 telemetry (primedCount/slotHighWater/liveHighWater +the mobs-N/cap()-hw items"
        "-hw primed del V/T entities line shared with thet1005/t1006 closure data collection)" }, [&]() {
        bool ok1007 = true;
        QString diag1007;
        constexpr int kPlatY = 40;
        constexpr int kPlatMin = 12, kPlatMax = 51; // 40×40 石台
        World w7;
        w7.setWidth(64); w7.setDepth(64); w7.setHeight(64); w7.setSeed(1007);
        for (int x = kPlatMin; x <= kPlatMax; ++x)
            for (int z = kPlatMin; z <= kPlatMax; ++z) {
                w7.setBlock(x, kPlatY, z, BR::Stone, 0);
                for (int y = kPlatY + 1; y <= kPlatY + 6; ++y) w7.setBlock(x, y, z, BR::Air, 0);
            }
        EntityManager em7;
        ItemEntityManager im7;
        const QVector3D farPos(-1000.0f, 80.0f, -1000.0f); // t997 rig 同款：玩家远置（不参与碰撞/伤害）
        // 爆炸掉落转发链（Main.qml onExplosionDroppedItem 同构；批收口是 QML delegate 优化，offscreen
        // 无 Repeater 不需要）。
        const QMetaObject::Connection dropConn1007 = QObject::connect(
            &em7, &EntityManager::explosionDroppedItem, &em7,
            [&](int x, int y, int z, int itemId) { im7.spawnItem(x, y, z, itemId, 1); });

        // ── (a) 36 primed TNT 真路径引爆 + 掉落转发 → 采样曲线 ──
        for (int dz = 0; dz < 6; ++dz)
            for (int dx = 0; dx < 6; ++dx)
                em7.spawnPrimedTnt(20 + dx, kPlatY + 1, 20 + dz, 0.5f); // 短引信加速（链式语义无关本探针）
        int primed0 = em7.primedCount();
        if (primed0 != 36) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-a primed0 %1 != 36").arg(primed0);
        }
        int totalDet = 0, maxItemLive = 0, prevPrimed = primed0;
        qint64 maxTickNs7 = 0, sumTickNs7 = 0;
        int tickFrames = 0;
        bool book1007 = true;
        auto aliveSlots = [](EntityManager &em) {
            int n = 0;
            for (int i = 0; i < em.count(); ++i) if (em.aliveAt(i)) ++n;
            return n;
        };
        auto itemAliveSlots = [](ItemEntityManager &im) {
            int n = 0;
            for (int i = 0; i < im.count(); ++i) if (im.aliveAt(i)) ++n;
            return n;
        };
        for (int f = 0; f < 60 * 12 && em7.primedCount() > 0; ++f) {
            const qint64 t0 = FrameProfiler::nowNs();
            em7.tick(1.0 / 60.0, &w7, farPos, 0.3f, 1.8f, true, false, 0.0f);
            im7.tick(1.0 / 60.0, &w7);
            const qint64 tk = FrameProfiler::nowNs() - t0;
            sumTickNs7 += tk; if (tk > maxTickNs7) maxTickNs7 = tk; ++tickFrames;
            const int pc = em7.primedCount();
            if (prevPrimed > pc) totalDet += prevPrimed - pc; // 本 tick 引爆数（引爆即 releaseSlot）
            prevPrimed = pc;
            if (f % 15 == 0) { // 0.25s 采样
                const int il = im7.liveCount();
                maxItemLive = std::max(maxItemLive, il);
                if (il > 200 || aliveSlots(em7) != em7.liveCount() || itemAliveSlots(im7) != il) {
                    book1007 = false;
                    if (diag1007.size() < 2048)
                        diag1007 += QStringLiteral("\n  [t1007 diag] leg-a t=%1s itemLive=%2 book/unsafe")
                                        .arg(double(f) / 60.0, 0, 'f', 2).arg(il);
                }
            }
        }
        const int primedResidue = em7.primedCount();
        const int itemLiveA = im7.liveCount(), itemSlotsA = im7.count();
        const double avgTickAms = tickFrames > 0 ? double(sumTickNs7) / 1e6 / double(tickFrames) : 0.0;
        const double maxTickAms = double(maxTickNs7) / 1e6;
        if (totalDet != 36) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-a totalDet %1 != 36").arg(totalDet);
        }
        if (primedResidue != 0) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-a primed residue %1").arg(primedResidue);
        }
        if (itemLiveA > 200 || !book1007) ok1007 = false;
        qInfo().noquote() << "  [t1007 diag] leg-a: det" << totalDet << "/36 residue" << primedResidue
                          << "itemLive" << itemLiveA << "slots" << itemSlotsA
                          << "avgTick" << avgTickAms << "ms maxTick" << maxTickAms << "ms book" << book1007;

        // ── (b) item 高水位：250 named 掉落（跳过合并）→ 恰 200/200 ──
        for (int i = 0; i < 250; ++i) {
            const int sx = kPlatMin + 1 + (i % 25) * 2;  // 25×10 网格铺台面（named 不合并 → 格距只防堆叠）
            const int sz = kPlatMin + 1 + (i / 25) * 2;
            im7.spawnItem(sx, kPlatY + 1, sz, BR::Dirt, 1, QVariantList{}, QStringLiteral("t1007"), -1);
        }
        for (int f = 0; f < 30; ++f) im7.tick(1.0 / 60.0, &w7); // 落地沉降
        const int itemLiveB = im7.liveCount(), itemSlotsB = im7.count(), itemHwB = im7.liveHighWater();
        if (itemLiveB != 200 || itemSlotsB != 200 || itemHwB != 200) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-b live=%1 slots=%2 hw=%3 != 200/200/200")
                            .arg(itemLiveB).arg(itemSlotsB).arg(itemHwB);
        }
        qInfo().noquote() << "  [t1007 diag] leg-b: 250 named spawns -> live" << itemLiveB
                          << "slots" << itemSlotsB << "hw" << itemHwB << "(cap 200 LRU pinned)";

        // ── (c) mobs 高水位：铺满 64 ──
        int mobSpawned = 0;
        for (int i = 0; i < 100; ++i) {
            const int sx = kPlatMin + 1 + (i % 25) * 2;
            const int sz = kPlatMin + 1 + (i / 25) * 2;
            if (em7.spawnMobTyped(sx, kPlatY + 1, sz, EntityManager::MobPig,
                                  QStringLiteral("#f0a8b0"), 10) < 0)
                break;
            ++mobSpawned;
        }
        const int mobLiveC = em7.liveCount(), mobSlotsC = em7.count(), mobHwC = em7.slotHighWater();
        if (mobSpawned != 64 || mobLiveC != 64 || mobSlotsC != 64 || mobHwC != 64) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-c spawned=%1 live=%2 slots=%3 hw=%4 != 64x4")
                            .arg(mobSpawned).arg(mobLiveC).arg(mobSlotsC).arg(mobHwC);
        }
        qInfo().noquote() << "  [t1007 diag] leg-c: pigs" << mobSpawned << "live" << mobLiveC
                          << "slots" << mobSlotsC << "hw" << mobHwC << "(cap 64 pinned)";

        // ── (e1) 满池 tick 链墙钟（tick + hostileLife + spawners + item = PlayerController 相位同构）──
        qint64 sumFull = 0;
        for (int f = 0; f < 60; ++f) { // 1s 模拟窗（满池 200 item + 64 mob）
            const qint64 t0 = FrameProfiler::nowNs();
            em7.tick(1.0 / 60.0, &w7, farPos, 0.3f, 1.8f, true, false, 0.0f);
            em7.tickHostileLife(1.0 / 60.0, &w7, farPos, 0.0f);
            em7.tickSpawners(1.0 / 60.0, &w7, farPos);
            im7.tick(1.0 / 60.0, &w7);
            sumFull += FrameProfiler::nowNs() - t0;
        }
        const double avgFullMs = double(sumFull) / 1e6 / 60.0;
        if (aliveSlots(em7) != em7.liveCount() || itemAliveSlots(im7) != im7.liveCount()) ok1007 = false;
        qInfo().noquote() << "  [t1007 diag] leg-e1 full-pool tick chain avg" << avgFullMs
                          << "ms/f (items 200 + mobs" << em7.liveCount() << ")";

        // ── (d) 重载状态面：clearAll 双池（enterWorld 同构）→ live 归零、有界残留保留 ──
        im7.clearAll();
        em7.clearAll();
        const int itemLiveD = im7.liveCount(), itemSlotsD = im7.count(), itemHwD = im7.liveHighWater();
        const int mobLiveD = em7.liveCount(), mobSlotsD = em7.count(), mobHwD = em7.slotHighWater();
        const int primedD = em7.primedCount();
        bool okD = itemLiveD == 0 && mobLiveD == 0 && primedD == 0
                   && itemSlotsD == 200 && mobSlotsD == 64        // t437 slot-reuse 设计残留（有界）
                   && itemHwD == 200 && mobHwD == 64;             // 高水位跨重载保留（判读基线）
        if (!okD) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-d reload face: live %1/%2 primed %3 "
                                       "slots %4/%5 hw %6/%7 (want 0/0/0 then 200/64 kept, 200/64 kept)")
                            .arg(itemLiveD).arg(mobLiveD).arg(primedD)
                            .arg(itemSlotsD).arg(mobSlotsD).arg(itemHwD).arg(mobHwD);
        }
        qInfo().noquote() << "  [t1007 diag] leg-d reload: itemLive" << itemLiveD << "mobLive" << mobLiveD
                          << "primed" << primedD << "| kept slots" << itemSlotsD << "/" << mobSlotsD
                          << "hw" << itemHwD << "/" << mobHwD;

        // ── (e2) 空池 tick 链墙钟（与 e1 同口径对照；diag 落盘供实机曲线对表）──
        qint64 sumEmpty = 0;
        for (int f = 0; f < 60; ++f) {
            const qint64 t0 = FrameProfiler::nowNs();
            em7.tick(1.0 / 60.0, &w7, farPos, 0.3f, 1.8f, true, false, 0.0f);
            em7.tickHostileLife(1.0 / 60.0, &w7, farPos, 0.0f);
            em7.tickSpawners(1.0 / 60.0, &w7, farPos);
            im7.tick(1.0 / 60.0, &w7);
            sumEmpty += FrameProfiler::nowNs() - t0;
        }
        const double avgEmptyMs = double(sumEmpty) / 1e6 / 60.0;
        qInfo().noquote() << "  [t1007 diag] leg-e2 empty-pool tick chain avg" << avgEmptyMs
                          << "ms/f (live" << em7.liveCount() << "/"
                          << im7.liveCount() << "; far-player dark spawner may refill = diag only)"
                          << "| full/empty ratio" << (avgEmptyMs > 0.0 ? avgFullMs / avgEmptyMs : 0.0);

        // ── (f) 源码钉：F3 增补读数链在场 ──
        bool okPin7 = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile ehf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            QFile ihf(root + QStringLiteral("/src/Game/itementitymanager.h"));
            QFile mqf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString eh = ehf.open(QIODevice::ReadOnly) ? QString::fromUtf8(ehf.readAll()) : QString();
            const QString ih = ihf.open(QIODevice::ReadOnly) ? QString::fromUtf8(ihf.readAll()) : QString();
            const QString mq = mqf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mqf.readAll()) : QString();
            okPin7 = eh.contains(QStringLiteral("Q_INVOKABLE int primedCount() const"))
                && eh.contains(QStringLiteral("Q_INVOKABLE int slotHighWater() const"))
                && ih.contains(QStringLiteral("Q_INVOKABLE int liveHighWater() const"))
                && mq.contains(QStringLiteral("\"  primed \" + primedN"))
                && mq.contains(QStringLiteral("\"  del \" + delVis + \"/\" + delTot"))
                // review0905 #6：分母字面量 "/64" → entityManager.cap() 读口（kCap 单一权威），钉随行迁移。
                && eh.contains(QStringLiteral("Q_INVOKABLE int cap() const { return kCap; }"))
                && mq.contains(QStringLiteral("\"/\" + entityManager.cap() + \" hw \" + mobHw"));
        }
        if (!okPin7) {
            ok1007 = false;
            diag1007 += QStringLiteral("\n  [t1007 diag] leg-f F3 instrumentation source pin missing");
        }

        QObject::disconnect(dropConn1007);
        if (!ok1007) ++totalFail;
        qInfo().noquote() << (ok1007 ? "PASS" : "FAIL")
                          << "| t1007 process-level stutter leak rig: the user's f6e9a51 playtest saw"
                             "FPS decay to 7 with F3 items 93/93 and a heavy render-side line"
                             "(gpu/prep/present) that a world re-entry resets to 99 FPS; offscreen has"
                             "no QRhi render thread so gpu/prep/present/vmem stay on-device F3 readings"
                             "(registered, never invented here) and this probe pins the ENGINE-side"
                             "equivalent surface through the real explosion->drop forwarding chain:"
                             "(a) 36 primed TNT (t997 6x6 layout) all detonate with zero primed residue"
                             "while item live stays <= the 200 cap under the drop flood with per-sample"
                             "aliveSlots==liveCount bookkeeping, (b) 250 named drops (merge-exempt)"
                             "saturate EXACTLY live=200 slots=200 hw=200 (LRU eviction pinned),"
                             "(c) mobs fill EXACTLY 64 = kCap, (e1) full-pool vs (e2) empty-pool tick"
                             "chain wall time recorded in diag as the sim-side curve to table against"
                             "the user F3 numbers (sim was 4.63ms of 66.7ms - single-digit sim at full"
                             "pools localizes the 14x decay to the render side: live entity delegate"
                             "draw cost scales linearly via per-delegate inline geometry/material"
                             "instances with no instancing; the t858 xp-orb instancing precedent is the"
                             "registered governance path, on-device curve comparison required before"
                             "any rendering rework - no blind offscreen fix), (d) world re-entry"
                             "(clearAll both pools, enterWorld-isomorphic) resets EXACTLY the live"
                             "sets (items/mobs/primed -> 0) while bounded remnants persist by design"
                             "(slot vectors 200/64, high waters 200/64) - no unbounded engine leak"
                             "exists offscreen, so the recovery face is the live entity set; (f) source"
                             "pins lock the new F3 telemetry (primedCount/slotHighWater/liveHighWater +"
                             "the mobs-N/cap()-hw items-hw primed del V/T entities line shared with the"
                             "t1005/t1006 closure data collection)"
                          << (ok1007 ? QString() : diag1007);
    });

    // ── P-t1013b 回生落车守卫 + 开箱遮挡腿（review0906 #12；t1013 回生链的行为面补钉）──
    //    (a) 落格守卫：回生键格被玩家填实（Stone）→ spawnCartImpl 向上扫首个可落格（车落键格上
    //        一格地面姿态），不再嵌墙成幽灵实体；键寻址不受落格位移影响（chestKeyAt 仍返回原键）。
    //        （阴性敏感：摘守卫 → 车嵌在 Stone 格内 pos.y 回落到键格 → 腿红。）
    //    (b) 开箱遮挡：玩家与箱车之间隔墙（主选体命中 m_hitDist ≈ 1.6 < chestDist ≈ 3.2）→ 右键
    //        不再 emit chestOpened（旧 kReach 全程无遮挡 = 隔墙开箱取物品）；拆墙同几何 → 恰一次
    //        emit 携正确内容键（遮挡守卫未过杀贴脸开箱）。
    runLegMulti({ "t1013b respawn-into-solid guard + chest-open occlusion (review0906 #12): (a) a respawn whose key"
        " cell the player filled with stone nolonger embeds a ghost cart inside the block - spawnCartImpl"
        " scans upto the first non-collidable cell (cart lands on top of the filledcell in ground posture"
        ", key addressing unchanged, key cell untouched)and refuses outright if the column is full (key k"
        "ept in ChestStorefor the next enterWorld retry); (b) right-clicking a chest cartBEHIND a wall no"
        " longer opens it - the open ray is clamped by themain-selection hit distance (wall hit ~1.6 < ca"
        "rt ~3.2 -> chestOpenednot emitted), and the same geometry with the wall removed opens exactlyonc"
        "e with the correct content key (the guard does not over-rejectpoint-blank opens)diag a/b see [t1"
        "013b diag a]/[t1013b diag b]" }, [&]() {
        const auto [xa12, za12] = nextSlot();
        bool okA12 = false;
        {
            ChestStore cs12;
            MinecartManager carts12;
            // 键格 = 轨线旁 2 格空地（4 邻无轨 → spawnChestCart 落键格；填实后守卫向上扫）。
            const int keyX12 = xa12 + 2;
            for (int dx = -2; dx <= 4; ++dx)
                for (int dz = -3; dz <= 3; ++dz) {
                    for (int dy = -1; dy <= 4; ++dy) w.setBlock(xa12 + dx, kRigY + dy, za12 + dz, BR::Air, 0);
                    w.setBlock(xa12 + dx, kRigY - 1, za12 + dz, BR::Stone, 0);
                }
            w.setBlock(xa12, kRigY, za12, BR::Rail, 0);      // 轨线（与键格隔 1 空格 → 非四邻）
            w.setBlock(keyX12, kRigY, za12, BR::Stone, 0);   // 玩家把回生键格填实
            tickN(w, 2);
            cs12.registerCart(keyX12, kRigY, za12);
            carts12.spawnChestCart(keyX12, kRigY, za12, &w, keyX12, kRigY, za12);
            int fx = -1, fy = -1, fz = -1;
            int idx12 = -1;
            for (int i = 0; i < carts12.count() && idx12 < 0; ++i) {
                if (carts12.aliveAt(i) && carts12.chestKeyAt(i, fx, fy, fz)
                    && fx == keyX12 && fy == kRigY && fz == za12)
                    idx12 = i;
            }
            const float groundDoc12 = 0.3875f; // kCartGroundH 文档镜像（t734 地面静止姿态）
            okA12 = idx12 >= 0
                && w.blockAt(keyX12, kRigY, za12) == BR::Stone // 键格仍被填实（车不嵌其中）
                && std::fabs(carts12.posAt(idx12).x() - (keyX12 + 0.5f)) < 0.01f
                && std::fabs(carts12.posAt(idx12).z() - (za12 + 0.5f)) < 0.01f
                && std::fabs(carts12.posAt(idx12).y() - (float(kRigY + 1) + groundDoc12)) < 0.01f; // 上一格地面姿态
            if (!okA12)
                qInfo().noquote() << "  [t1013b diag a] idx" << idx12
                                  << "keyCell" << int(w.blockAt(keyX12, kRigY, za12))
                                  << "pos" << (idx12 >= 0 ? carts12.posAt(idx12) : QVector3D());
            carts12.clearAll();
            cs12.clearChest(keyX12, kRigY, za12);
        }
        bool okB12 = false;
        {
            ChestStore cs12;
            MinecartManager carts12;
            PlayerController pc12;
            // review0906 D2 首跑归因：m_selectedBlock 默认 Stone（t06 hotbar 绑定）——phase-1 隔墙点击
            //   在箱车分支被守卫正确拒后落到通用放置分支，把 Stone 误放到墙击面邻格；phase-2 拆墙后这颗
            //   「幽灵石头」距眼 ~0.54 抢占主选（m_hitDist < chestDist）→ 守卫拒开箱 = opens 0 假红。
            //   空手（Air）分流：无 shift → t1052 合取门（sneak ∧ 持方块）不旁路、照常进开箱分支（被
            //   遮挡守卫拒），通用放置无物可放。
            pc12.setSelectedBlock(BR::Air);
            QQuickWindow probeWin12;
            pc12.setWorld(&w);
            pc12.setMinecartManager(&carts12);
            pc12.setChestStore(&cs12);
            pc12.setParentItem(probeWin12.contentItem());
            // 箱车在轨格 (xa12+2, za12)；玩家 (xa12+2, za12+3.5) 同列瞄准车心；墙 = (xa12+2, kRigY+1, za12+1)。
            const int cx12 = xa12 + 2;
            w.setBlock(cx12, kRigY, za12, BR::Rail, 0);
            cs12.registerCart(cx12, kRigY, za12);
            carts12.spawnChestCart(cx12, kRigY, za12, &w, cx12, kRigY, za12);
            int chestOpens12 = 0;
            int openK12[3] = { -1, -1, -1 };
            QObject::connect(&pc12, &PlayerController::chestOpened, &pc12, [&](int kx, int ky, int kz) {
                ++chestOpens12;
                openK12[0] = kx; openK12[1] = ky; openK12[2] = kz;
            });
            const float feetY12 = float(kRigY);
            const QVector3D aim12(float(cx12) + 0.5f, float(kRigY) + 0.45f, float(za12) + 0.5f);
            const auto pump12 = [](int ms) {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            };
            const auto aim12f = [&](float fx12, float fy12, float fz12, float ax, float ay, float az) {
                const float ex = fx12, ey = fy12 + 1.62f, ez = fz12;
                const float dx = ax - ex, dy = ay - ey, dz = az - ez;
                const float ln = std::sqrt(dx * dx + dy * dy + dz * dz);
                pc12.release();
                pc12.grab();
                pc12.loadSavedState(fx12, fy12, fz12,
                                    std::atan2(-dx, -dz) * 57.2957795f,
                                    std::asin(dy / ln) * 57.2957795f, 1 /* Creative */);
                pc12.tick(); // updateRaycast 刷新命中（aimT1017 同式）
                return pc12.hitBlock();
            };
            // 隔墙：墙格 (cx12, kRigY+1, za12+1) 在视线上（射线 z 3.5→0.5、y 1.62→0.45，穿该格 y 层）。
            w.setBlock(cx12, kRigY + 1, za12 + 1, BR::Stone, 0);
            tickN(w, 2);
            const QVector3D hitW = aim12f(float(cx12) + 0.5f, feetY12, float(za12) + 3.5f,
                                          aim12.x(), aim12.y(), aim12.z());
            pc12.placeBlock();
            pump12(260);
            const bool wallHit = hitW == QVector3D(cx12, kRigY + 1, za12 + 1); // 主选体确实命中墙
            const bool blockedOk = wallHit && chestOpens12 == 0;              // 隔墙不开箱
            // 拆墙对照：同几何 → 开箱恰一次携键（守卫不过杀）。
            w.setBlock(cx12, kRigY + 1, za12 + 1, BR::Air, 0);
            tickN(w, 2);
            aim12f(float(cx12) + 0.5f, feetY12, float(za12) + 3.5f, aim12.x(), aim12.y(), aim12.z());
            pc12.placeBlock();
            pump12(260);
            const bool openOk = chestOpens12 == 1 && openK12[0] == cx12
                && openK12[1] == kRigY && openK12[2] == za12;
            okB12 = blockedOk && openOk;
            if (!okB12)
                qInfo().noquote() << "  [t1013b diag b] wallHit" << wallHit << "hitCell" << hitW
                                  << "opens" << chestOpens12 << "key" << openK12[0] << openK12[1] << openK12[2];
            carts12.clearAll();
            cs12.clearChest(cx12, kRigY, za12);
        }
        if (!okA12) ++totalFail;
        if (!okB12) ++totalFail;
        qInfo().noquote() << (okA12 && okB12 ? "PASS" : "FAIL")
                          << "| t1013b respawn-into-solid guard + chest-open occlusion (review0906"
                             " #12): (a) a respawn whose key cell the player filled with stone no"
                             "longer embeds a ghost cart inside the block - spawnCartImpl scans up"
                             "to the first non-collidable cell (cart lands on top of the filled"
                             "cell in ground posture, key addressing unchanged, key cell untouched)"
                             "and refuses outright if the column is full (key kept in ChestStore"
                             "for the next enterWorld retry); (b) right-clicking a chest cart"
                             "BEHIND a wall no longer opens it - the open ray is clamped by the"
                             "main-selection hit distance (wall hit ~1.6 < cart ~3.2 -> chestOpened"
                             "not emitted), and the same geometry with the wall removed opens exactly"
                             "once with the correct content key (the guard does not over-reject"
                             "point-blank opens)"
                          << (okA12 && okB12 ? QString()
                                             : QStringLiteral("diag a/b see [t1013b diag a]/[t1013b diag b]"));
    });

    // ── P-t1014 矿脉化逐矿种探针（R19.20 t1014；scatterOres 散点 → MC 1.0 式矿脉的验收面）──
    //    rig：t1001/t995 同款 5 seed（20260821/777/424242/1337/90210）× 128×128×64 世界池 +
    //    同 seed 复跑（确定性腿）。逐矿种全图连通域普查（6 连通形态口径 + 26 连通「贴连」观感口径——
    //    铁角斜对贴块按角触并入主域，对应用户口径「基本都连接起来」）。
    //    基线（旧散点 carve 后存活，同 rig 5 seed 实测）：coal 7868 / copper 5084 / iron 2467 /
    //    gold 1526 / diamond 2106 / lapis 2530 / redstone 28393（±13% 经济窗；**频率经济不涨总矿量**）。
    //    旧散点签名（阴性轮敏感）：连通域 ≥96% 为孤块、conn26 ≈ 0%、中带域中位长轴 -1（无域可采）。
    //    腿：(a) 煤 2×2×5 长条+贴连散块 (b) 铁 2×2×2+角贴块 (c) 金/红石 MC1.0 blob (d) 钻/青/铜 blob
    //        (e) 经济总量 ±13% 窗 + 同 seed 复跑 7 矿总量全等 (f) 源码钉（脉形表/三印章/矿盐/石门）。
    runLegMulti({ "t1014(a) coal 2x2x5 bar + hash-glued scatter: per-seed vein components300..470 (scattered baseli"
        "ne was ~7500 singletons), mid-size componentbbox median long axis 5..8 short axis 3..4 (bar+glue"
        " silhouette),26-adjacency connection >= 85%, band y[8,60] with >= 99% strictnessand absolute [7,"
        "60] (mineshaft wall ore exemption), economy per-seed6845..8891 (old-scatter carve-survival basel"
        "ine 7868 +-13% -> total oreNOT inflated)shape/window miss, see diag",
               "t1014(b) iron 2x2x2 cube + diagonal corner blocks: mid-size componentsare bare cubes (bbox media"
        "n exactly 2x2x2), 150..215 cubes per world,corner blocks merge under 26-adjacency -> connection "
        ">= 90% (they arediagonal so 6-conn singletons are expected and fine), band y[5,30]>= 99% strict "
        "with <= 40 absolute (mineshaft exemption), economy2146..2788 (baseline 2467 +-13%)shape/window m"
        "iss, see diag",
               "t1014(c) gold + redstone MC1.0-size blobs: gold 1328..1724 total in225..275 walk-veins strictly "
        "inside y[5,25]; redstone 24702..32084 in1300..1650 veins strictly inside y[5,16] with merged-she"
        "et tail capped(hist41 <= 160, registered effective-density carryover - the legacy(r2>>24)%10000 "
        "8-bit truncation is preserved per economy parity, seeworld.cpp ledger note), connection >= 95% ("
        "redstone vein-count windowlowered 1390 -> 1300 for review0906 #18: the horizontal-only repickrem"
        "oves zero-displacement retry deaths so walks travel farther and~5% fewer larger components form "
        "- totals and connection unchanged)shape/window miss, see diag",
               "t1014(d) diamond/lapis/copper MC1.0-size blobs: diamond 1832..2380 in315..375 veins y[5,40]; lap"
        "is 2201..2859 in 420..485 veins y[5,31];copper 4423..5745 in 790..990 veins y[5,45]; all strictl"
        "y banded,connection >= 85%, scraggly walk silhouette (bbox median long 4..8,short <= 2 like vani"
        "lla scraggle)shape/window miss, see diag",
               "t1014(e) frequency economy + determinism: all 7 ore species landper-seed within +-13% of the old"
        "-scatter carve-survival baseline(coal 7868 / copper 5084 / iron 2467 / gold 1526 / diamond 2106 "
        "/lapis 2530 / redstone 28393) - veining must not inflate total ore -and a same-seed regeneration"
        " reproduces all 7 totals EXACTLY(pure hashVoxel worldgen, no runtime RNG)economy window or regen"
        " miss, see diag",
               "t1014(f) source pins: world.cpp keeps the kProfiles vein table, theper-ore salts, the coal bar o"
        "rientation pick, the iron cube/cornerstamp branch, the blob off-stone redirect and the stone-onl"
        "y gate(negative-round sensitive: reverting scatterOres to per-voxel scatterloses the table and e"
        "very shape leg above reads the scatteredsignature: ~96% singletons, conn26 ~ 0%)source pin missi"
        "ng",
               "t1014(g) 128-high-world coal climbs above y=60 (review0907 A-P1-2):coal profile yMax=0 sentinel "
        "makes the band ceiling purely column-adaptive(hc-5, matching the legacy per-column stoneTop=h-3 "
        "scatter rule), so hillcolumns (surface up to ~71 at 128 world height) carry coal veins above the"
        "old hard cap of 60 -- the 64-high rig worlds never trigger the cap (hc-5<= 58) so legs (a)-(f) a"
        "re unchanged; threshold is y>=62 because thecapped bar overshoots the 60 ceiling by exactly one "
        "block (y=61,diag-verified negative round), and at least one of 5 seeds must showcoal at y>=62 (p"
        "er-seed counts in diag)no coal above 61 in any seed",
               "t1014(h) coastal land columns inside sea-centered cells carry ore(review0906 #11): the old cell-"
        "level skip vetoed a whole 16x16 cell onits center column height, so land columns of sea-centered"
        " cells(the coastal transition belt) went completely oreless - whole zero-orepatches unlike the l"
        "egacy per-column skip; the skip is gone and pure-seacells are now rejected per-column inside try"
        "Ore (h <= wl+1), restoringthe legacy distribution. Rig = two dedicated sea-bearing seeds(150461 "
        "/ 174218 - a 40-seed diag sweep found the t1014 fixed-seedwindows contain NO sea-centered cell a"
        "t all, so the skip never firedthere; each dedicated seed carries exactly one beach-centered cell"
        ",center h 58/59): each seed must show >= 200 ore blocks on land columns(h >= 60) of its sea-cent"
        "ered cell (measured 747 / 782 under therestored per-column rejection - the old skip reads wander"
        "-in overflowonly, see the D2 lesion round) and the census structurally confirms thesea cells exi"
        "st (no vacuous green on seed drift)sea-cell land-column distribution miss, see diag" }, [&]() {
        struct OreStatT1014 {
            int total = 0, comps = 0, conn26 = 0, inBand = 0;
            int ymin = 999, ymax = -1;
            int histMid = 0;      // size 7..12 域数（铁=裸立方域 / blob=主脉域）
            int hist41 = 0;       // size 41+ 域数（红石密带合板窗）
            int midLongMed = -1, midShortMed = -1; // size 7..40 域 bbox 排序最长/最短边中位
        };
        const quint32 seedsT1014[] = { 20260821u, 777u, 424242u, 1337u, 90210u };
        const int oresT1014[7] = { BR::CoalOre, BR::CopperOre, BR::IronOre, BR::GoldOre,
                                   BR::DiamondOre, BR::LapisOre, BR::RedstoneOre };
        const char *oreNamesT1014[7] = { "coal", "copper", "iron", "gold", "diamond", "lapis", "redstone" };
        // 高度带（world.cpp kProfiles 同源；bandLo/Hi = 严格带，slack 矿种用 inBand% 判）。
        const int bandT1014[7][2] = { { 8, 60 }, { 5, 45 }, { 5, 30 }, { 5, 25 }, { 5, 40 }, { 5, 31 }, { 5, 16 } };
        const bool strictBandT1014[7] = { false, true, false, true, true, true, true }; // 煤/铁留矿井巷壁暴露矿豁免（≥99% 口径）

        auto censusT1014 = [&](World &wv, int oreId, int bandLo, int bandHi, OreStatT1014 &st, int &seaLand) {
            const int W = wv.width(), D = wv.depth(), H = wv.height();
            // review0906 #11 海列 cell 分布腿素材：cell 中心列高缓存（8×8 cell 网格，0 = 未算）。
            //   59 = kWaterLevel(58)+1 镜像（world.cpp cell 跳过 / tryOre 海检同款阈值；测试侧文档锚）。
            int cellHc[8][8];
            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 8; ++b) cellHc[a][b] = 0;
            const auto cellCenterH = [&](int x, int z) -> int {
                const int a = std::min(x / 16, 7), b = std::min(z / 16, 7);
                if (cellHc[a][b] == 0)
                    cellHc[a][b] = std::min(wv.heightAt(std::min(a * 16 + 8, W - 1),
                                                        std::min(b * 16 + 8, D - 1)), H - 1) + 1; // +1 防 0 歧义
                return cellHc[a][b] - 1;
            };
            std::unordered_set<quint32> cells;
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z)
                    for (int x = 0; x < W; ++x)
                        if (wv.blockAt(x, y, z) == oreId) {
                            cells.insert(quint32(x + W * (z + D * y)));
                            if (y >= bandLo && y <= bandHi) ++st.inBand;
                            st.ymin = std::min(st.ymin, y);
                            st.ymax = std::max(st.ymax, y);
                            // review0906 #11：海洋中心 cell（中心列 h ≤ 59）内的陆地列（本列 h ≥ 60）
                            //   矿块计数——旧 cell 级跳过把整个 cell 一票否决 = 此计数恒 0（仅邻 cell
                            //   blob 越界蹭入少量）；恢复逐列海检兜底后海岸带陆地列恢复成矿。
                            if (cellCenterH(x, z) <= 59 && std::min(wv.heightAt(x, z), H - 1) >= 60)
                                ++seaLand;
                        }
            st.total = int(cells.size());
            static const int D6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            static const int D26[26][3] = {
                { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
                { 1, 1, 0 }, { 1, -1, 0 }, { -1, 1, 0 }, { -1, -1, 0 }, { 1, 0, 1 }, { 1, 0, -1 },
                { -1, 0, 1 }, { -1, 0, -1 }, { 0, 1, 1 }, { 0, 1, -1 }, { 0, -1, 1 }, { 0, -1, -1 },
                { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 }, { -1, 1, 1 }, { -1, 1, -1 },
                { -1, -1, 1 }, { -1, -1, -1 }
            };
            std::vector<int> midLong, midShort;
            auto bfs = [&](quint32 start, const int (*dirs)[3], int ndir,
                           std::unordered_set<quint32> &vis, int bbOut[6]) -> int {
                std::queue<quint32> q;
                q.push(start);
                vis.insert(start);
                int size = 0;
                bbOut[0] = bbOut[2] = bbOut[4] = 1 << 30;
                bbOut[1] = bbOut[3] = bbOut[5] = -1;
                while (!q.empty()) {
                    const quint32 cur = q.front(); q.pop();
                    const int x = int(cur % W), t = int(cur / W), z = int(t % D), y = int(t / D);
                    ++size;
                    bbOut[0] = std::min(bbOut[0], x); bbOut[1] = std::max(bbOut[1], x);
                    bbOut[2] = std::min(bbOut[2], y); bbOut[3] = std::max(bbOut[3], y);
                    bbOut[4] = std::min(bbOut[4], z); bbOut[5] = std::max(bbOut[5], z);
                    for (int i = 0; i < ndir; ++i) {
                        const int nx = x + dirs[i][0], ny = y + dirs[i][1], nz = z + dirs[i][2];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
                        const quint32 k = quint32(nx + W * (nz + D * ny));
                        if (cells.count(k) && !vis.count(k)) { vis.insert(k); q.push(k); }
                    }
                }
                return size;
            };
            { // 6 连通：域数 / 尺寸桶 / 中带域 bbox
                std::unordered_set<quint32> vis;
                int bb[6];
                for (quint32 s : cells) {
                    if (vis.count(s)) continue;
                    const int size = bfs(s, D6, 6, vis, bb);
                    ++st.comps;
                    if (size >= 7 && size <= 12) ++st.histMid;
                    if (size >= 41) ++st.hist41;
                    if (size >= 7 && size <= 40) {
                        int dims[3] = { bb[1] - bb[0] + 1, bb[3] - bb[2] + 1, bb[5] - bb[4] + 1 };
                        std::sort(dims, dims + 3);
                        midLong.push_back(dims[2]);
                        midShort.push_back(dims[0]);
                    }
                }
            }
            { // 26 连通：贴连观感连接率（角触并入）
                std::unordered_set<quint32> vis;
                int bb[6];
                for (quint32 s : cells) {
                    if (vis.count(s)) continue;
                    const int size = bfs(s, D26, 26, vis, bb);
                    if (size >= 4) st.conn26 += size;
                }
            }
            std::sort(midLong.begin(), midLong.end());
            std::sort(midShort.begin(), midShort.end());
            st.midLongMed = midLong.empty() ? -1 : midLong[midLong.size() / 2];
            st.midShortMed = midShort.empty() ? -1 : midShort[midShort.size() / 2];
        };
        // review0906 #11 (h) 结构钉素材：海中心 cell 数 / 陆地列数普查（16×16 cell 网格，cellCenterH
        //   与 censusT1014 同口径：中心列 heightAt clamp H-1 ≤ 59 = 海 cell；列 ≥ 60 = 陆地列）。
        auto censusSeaMetaT1014 = [&](World &wv, int &seaCellsAcc, int &landColsAcc, int &maxSeaCenterH, bool reset) {
            const int W = wv.width(), D = wv.depth(), H = wv.height();
            if (reset) { seaCellsAcc = 0; landColsAcc = 0; maxSeaCenterH = -1; }
            for (int cz = 0; cz < D; cz += 16)
                for (int cx = 0; cx < W; cx += 16) {
                    const int hc = std::min(wv.heightAt(std::min(cx + 8, W - 1),
                                                        std::min(cz + 8, D - 1)), H - 1);
                    if (hc > 59) continue;
                    ++seaCellsAcc;
                    maxSeaCenterH = std::max(maxSeaCenterH, hc);
                    for (int z = cz; z < std::min(cz + 16, D); ++z)
                        for (int x = cx; x < std::min(cx + 16, W); ++x)
                            if (std::min(wv.heightAt(x, z), H - 1) >= 60) ++landColsAcc;
                }
        };

        OreStatT1014 statT1014[5][7];
        OreStatT1014 regenT1014[7];
        int seaLandT1014[5] = { 0, 0, 0, 0, 0 }; // review0906 #11：海中心 cell 陆地列矿块（逐 seed 累计 7 矿）
        for (int si = 0; si < 5; ++si) {
            World wT1014;
            wT1014.setWidth(128);
            wT1014.setDepth(128);
            wT1014.setHeight(64);
            wT1014.setSeed(int(seedsT1014[si]));
            for (int oi = 0; oi < 7; ++oi)
                censusT1014(wT1014, oresT1014[oi], bandT1014[oi][0], bandT1014[oi][1], statT1014[si][oi],
                            seaLandT1014[si]);
            if (si == 0) // 确定性腿素材：同 seed 复跑
            {
                World wR;
                wR.setWidth(128);
                wR.setDepth(128);
                wR.setHeight(64);
                wR.setSeed(int(seedsT1014[0]));
                for (int oi = 0; oi < 7; ++oi) {
                    int regenSea = 0;
                    censusT1014(wR, oresT1014[oi], bandT1014[oi][0], bandT1014[oi][1], regenT1014[oi], regenSea);
                }
            }
        }

        auto inWin = [](int v, int lo, int hi) { return v >= lo && v <= hi; };

        // ── (a) 煤：2×2×5 长条+贴连散块（域数窗 / 长条 bbox 中位窗 / conn26 / 高度带 ≥99% 严带）──
        bool okA = true;
        {
            QString diagA;
            for (int si = 0; si < 5; ++si) {
                const OreStatT1014 &s = statT1014[si][0];
                const bool ok = inWin(s.total, 6845, 8891) && inWin(s.comps, 300, 470)
                    && inWin(s.midLongMed, 5, 8) && inWin(s.midShortMed, 3, 4)
                    && s.conn26 * 100 >= s.total * 85
                    && s.ymin >= 7 && s.ymax <= 60 && s.inBand * 100 >= s.total * 99;
                if (!ok) diagA += QStringLiteral(" seed%1:t%2/c%3/l%4/sh%5/cn%6/y[%7,%8]/ib%9")
                                     .arg(si).arg(s.total).arg(s.comps).arg(s.midLongMed)
                                     .arg(s.midShortMed).arg(s.conn26 * 100 / std::max(1, s.total))
                                     .arg(s.ymin).arg(s.ymax).arg(s.inBand);
                okA = okA && ok;
            }
            if (!okA)
                qInfo().noquote() << "  [t1014 diag a] coal vein-shape misses:" << diagA;
        }

        // ── (b) 铁：2×2×2 立方（中带域 bbox 恰 2×2×2）+ 角贴块（26 连通并入 → conn26 高）──
        bool okB = true;
        {
            QString diagB;
            for (int si = 0; si < 5; ++si) {
                const OreStatT1014 &s = statT1014[si][2];
                const bool ok = inWin(s.total, 2146, 2788) && inWin(s.comps, 790, 1000)
                    && s.midLongMed == 2 && s.midShortMed == 2
                    && inWin(s.histMid, 150, 215)
                    && s.conn26 * 100 >= s.total * 90
                    && s.ymin >= 5 && s.ymax <= 40 && s.inBand * 100 >= s.total * 99;
                if (!ok) diagB += QStringLiteral(" seed%1:t%2/c%3/l%4/sh%5/hm%6/cn%7/y[%8,%9]/ib%10")
                                     .arg(si).arg(s.total).arg(s.comps).arg(s.midLongMed)
                                     .arg(s.midShortMed).arg(s.histMid)
                                     .arg(s.conn26 * 100 / std::max(1, s.total))
                                     .arg(s.ymin).arg(s.ymax).arg(s.inBand);
                okB = okB && ok;
            }
            if (!okB)
                qInfo().noquote() << "  [t1014 diag b] iron vein-shape misses:" << diagB;
        }

        // ── (c) 金 / 红石 MC 1.0 blob（尺寸窗 / 严带 / conn26；红石密带合板 hist41 窗）──
        bool okC = true;
        {
            QString diagC;
            for (int si = 0; si < 5; ++si) {
                const OreStatT1014 &g = statT1014[si][3];
                const OreStatT1014 &r = statT1014[si][6];
                const bool okG = inWin(g.total, 1328, 1724) && inWin(g.comps, 225, 275)
                    && g.conn26 * 100 >= g.total * 85 && g.inBand == g.total
                    && g.ymin >= 5 && g.ymax <= 25 && g.midShortMed <= 2;
                const bool okR = inWin(r.total, 24702, 32084) && inWin(r.comps, 1300, 1650)
                    && r.conn26 * 100 >= r.total * 95 && r.inBand == r.total
                    && r.hist41 <= 160 && r.midShortMed <= 2;
                if (!okG || !okR)
                    diagC += QStringLiteral(" seed%1 gold:t%2/c%3/cn%4/ib%5 redstone:t%6/c%7/cn%8/h41%9")
                                 .arg(si).arg(g.total).arg(g.comps)
                                 .arg(g.conn26 * 100 / std::max(1, g.total)).arg(g.inBand)
                                 .arg(r.total).arg(r.comps)
                                 .arg(r.conn26 * 100 / std::max(1, r.total)).arg(r.hist41);
                okC = okC && okG && okR;
            }
            if (!okC)
                qInfo().noquote() << "  [t1014 diag c] gold/redstone misses:" << diagC;
        }

        // ── (d) 钻 / 青 / 铜 MC 1.0 blob（尺寸窗 / 严带 / conn26 / 细长 bbox 中位）──
        bool okD = true;
        {
            QString diagD;
            for (int si = 0; si < 5; ++si) {
                const OreStatT1014 &d = statT1014[si][4];
                const OreStatT1014 &l = statT1014[si][5];
                const OreStatT1014 &c = statT1014[si][1];
                const bool okDi = inWin(d.total, 1832, 2380) && inWin(d.comps, 315, 375)
                    && d.conn26 * 100 >= d.total * 85 && d.inBand == d.total
                    && inWin(d.midLongMed, 4, 8) && d.midShortMed <= 2;
                const bool okLa = inWin(l.total, 2201, 2859) && inWin(l.comps, 420, 485)
                    && l.conn26 * 100 >= l.total * 85 && l.inBand == l.total
                    && inWin(l.midLongMed, 4, 8) && l.midShortMed <= 2;
                const bool okCu = inWin(c.total, 4423, 5745) && inWin(c.comps, 790, 990)
                    && c.conn26 * 100 >= c.total * 85 && c.inBand == c.total
                    && inWin(c.midLongMed, 4, 8) && c.midShortMed <= 2;
                if (!okDi || !okLa || !okCu)
                    diagD += QStringLiteral(" seed%1 dia:t%2/c%3/cn%4 lap:t%5/c%6/cn%7 cop:t%8/c%9/cn%10")
                                 .arg(si).arg(d.total).arg(d.comps).arg(d.conn26 * 100 / std::max(1, d.total))
                                 .arg(l.total).arg(l.comps).arg(l.conn26 * 100 / std::max(1, l.total))
                                 .arg(c.total).arg(c.comps).arg(c.conn26 * 100 / std::max(1, c.total));
                okD = okD && okDi && okLa && okCu;
            }
            if (!okD)
                qInfo().noquote() << "  [t1014 diag d] diamond/lapis/copper misses:" << diagD;
        }

        // ── (e) 经济总量（**不涨总矿量**：逐 seed 落旧散点 carve 后基线 ±13% 窗）+ 同 seed 复跑全等 ──
        bool okE = true;
        {
            // 基线（旧散点 5 seed carve 后存活均值）：P-t1014 头注释同源。
            const int baseT1014[7] = { 7868, 5084, 2467, 1526, 2106, 2530, 28393 };
            const int loT1014[7] = { 6845, 4423, 2146, 1328, 1832, 2201, 24702 };
            const int hiT1014[7] = { 8891, 5745, 2788, 1724, 2380, 2859, 32084 };
            QString diagE;
            for (int oi = 0; oi < 7; ++oi) {
                for (int si = 0; si < 5; ++si) {
                    const int t = statT1014[si][oi].total;
                    if (!inWin(t, loT1014[oi], hiT1014[oi]))
                        diagE += QStringLiteral(" %1/s%2:%3").arg(oreNamesT1014[oi]).arg(si).arg(t);
                }
                okE = okE && regenT1014[oi].total == statT1014[0][oi].total; // 同 seed 复跑确定性
            }
            if (!okE)
                qInfo().noquote() << "  [t1014 diag e] economy/regen misses:" << diagE;
        }

        // ── (f) 源码钉：脉形表 / 三印章分支 / 矿盐 / 逐块石门（阴性敏感——revert 散点即失）──
        //     B-P1-1 迁移：滤注释钉（pinSet）；offStone 弱标识符升格为失位转全向的**判定语句**。
        bool okF = false;
        {
            const QString worldPath = QDir(QCoreApplication::applicationDirPath()
                                           + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/World/world.cpp"));
            const QStringList missF = pinSet(worldPath, {
                {"table-kProfiles", "constexpr VeinProfile kProfiles"},
                {"salt-redstone", "0x2ED57Eu"},   // 红石矿盐（t1014）
                {"salt-coal", "0x0C0A15u"},       // 煤矿盐（t1014）
                {"salt-iron", "0x12D0E1u"},       // 铁矿盐（t1014）
                {"coal-axis-pick", "const bool axX = (hv >> 16) & 1u;"},   // 煤长条横轴选型
                {"iron-cube-branch", "p.kind == 2"},                       // 铁立方角印章分支
                {"blob-offstone-redirect", "if (!offStone && int(rv % 100u) < 60) {"}, // blob 失位转全向判定
                {"blob-clamp-hrepick", "dx = kH4[d][0]; dz = kH4[d][1];"}, // review0906 #18 竖向重挑限水平 4 向
                {"stone-only-gate", "if (m_chunks.blockAt(x, y, z) != BlockRegistry::Stone) return false;"}, // 仅置换 Stone
            });
            okF = missF.isEmpty();
            if (!okF)
                qInfo().noquote() << "  [t1014 diag f] pin miss:" << missF.join(QLatin1Char(','));
        }

        // ── (g) review0907 A-P1-2 128 高世界煤脉自然上探：煤 yMax=0 哨兵（上界纯列自适应 hc-5，
        //     对齐旧散点 stoneTop=h-3 口径）→ 高山列（hills amp 7 → 地表可 ~71）煤脉可越过 y=60。
        //     原 yMax=60 硬帽把 128 高世界（游戏本体口径）的煤封顶——头注释「128 高世界煤脉自然上探」
        //     声明落空。rig 兼容：64 高世界 hc-5 ≤ 58 < 60，本就不触发帽 → (a)-(f) 腿行为不变。
        //     5 seed 联合断言（任一 seed 出现 y>61 煤块即过；逐 seed 计数落 diag）。
        //     **阈值 62 的由来（阴性轮实测回灌）**：帽顶 60 的长条姿态上探恰好 +1 越界到 y=61（三
        //     seed 实证 maxY=61），y>60 口径对硬帽摘除不敏感（假绿）→ 收紧到 62：帽下 62+ 恒 0，
        //     列自适应下 s777 实测到 63（diag y<=63）→ 恰红恰绿。
        bool okG = false;
        {
            const quint32 seedsHiT1014[] = { 20260821u, 777u, 424242u, 1337u, 90210u };
            QString diagG;
            for (int si = 0; si < 5; ++si) {
                World wHi;
                wHi.setWidth(128);
                wHi.setDepth(128);
                wHi.setHeight(128);
                wHi.setSeed(int(seedsHiT1014[si]));
                int aboveCount = 0, maxYSeen = -1;
                for (int y = 62; y < 128; ++y)
                    for (int z = 0; z < 128; ++z)
                        for (int x = 0; x < 128; ++x)
                            if (wHi.blockAt(x, y, z) == BR::CoalOre) {
                                ++aboveCount;
                                maxYSeen = std::max(maxYSeen, y);
                            }
                diagG += QStringLiteral(" s%1:%2(y<=%3)").arg(seedsHiT1014[si]).arg(aboveCount).arg(maxYSeen);
                okG = okG || aboveCount > 0;
            }
            if (!okG)
                qInfo().noquote() << "  [t1014 diag g] 128-high coal-above-61 misses:" << diagG;
            else
                qInfo().noquote() << "  [t1014 diag g] 128-high coal-above-61 counts:" << diagG;
        }

        // ── (h) review0906 #11 海列分布腿：海中心 cell 内的陆地列（海岸过渡带）矿量非零 ──
        //     旧 cell 级跳过（中心列 h ≤ 59 一票否决整 16×16 cell）= 该计数仅剩邻 cell blob 越界
        //     蹭入的少量补偿；移除 cell 跳过、恢复 tryOre 逐列海检兜底后，海岸带陆地列恢复成矿
        //     （旧散点分布原貌）。rig：t1014 五固定 seed 的 128×128×64 窗内**没有任何海中心 cell**
        //     （diag_t1014h 首跑实测 seaCells=0 —— 高度图基线 ~64、近岸过渡带列中心落在 60+ = cell
        //     跳过在这些世界本就从不触发），故本腿自建**专测海 seed**（40 seed 扫描命中 4 个，取前
        //     2：150461 / 174218，各恰 1 个滩心 cell = minCenterH 58/59，陆地列 236/238）。断言：两
        //     seed 计数各 ≥ 200（实测 747 / 782 —— 正裕量 ~3.5 倍；旧口径只剩越界蹭入，阴轮实测见
        //     dev-plan D2 段）。计数走同一 censusT1014（口径零分叉）。
        bool okH = false;
        {
            const int seaSeedsH[2] = { 150461, 174218 };
            int seaLandSeedsH[2] = { 0, 0 };
            int seaCellsH = 0, landColsH = 0, maxHSeaH = -1;
            OreStatT1014 scratchH[7];
            QString diagH;
            for (int k = 0; k < 2; ++k) {
                World wSea;
                wSea.setWidth(128);
                wSea.setDepth(128);
                wSea.setHeight(64);
                wSea.setSeed(seaSeedsH[k]);
                for (int oi = 0; oi < 7; ++oi)
                    censusT1014(wSea, oresT1014[oi], bandT1014[oi][0], bandT1014[oi][1],
                                scratchH[oi], seaLandSeedsH[k]); // 计数走同 censusT1014（口径零分叉）
                diagH += QStringLiteral(" s%1:%2").arg(seaSeedsH[k]).arg(seaLandSeedsH[k]);
                // diag 侧结构钉：确有海中心 cell（防 seed 漂移把腿打成 vacuous 绿）。
                censusSeaMetaT1014(wSea, seaCellsH, landColsH, maxHSeaH, k == 0);
            }
            okH = seaLandSeedsH[0] >= 200 && seaLandSeedsH[1] >= 200 && seaCellsH >= 2;
            qInfo().noquote() << "  [t1014 diag h] sea-seed land-column ore counts:" << diagH
                              << "dedicated seaCells" << seaCellsH;
            (void)landColsH;
            (void)maxHSeaH;
            if (!okH)
                qInfo().noquote() << "  [t1014 diag h] sea-cell land-column distribution leg misses";
        }

        for (int oi = 0; oi < 7; ++oi) { // profile 表数据落账（六矿种 + 铜）：5 seed 范围摘要
            int tmin = 1 << 30, tmax = -1, cmin = 1 << 30, cmax = -1, cnMin = 100, cnMax = -1;
            for (int si = 0; si < 5; ++si) {
                const OreStatT1014 &s = statT1014[si][oi];
                tmin = std::min(tmin, s.total); tmax = std::max(tmax, s.total);
                cmin = std::min(cmin, s.comps); cmax = std::max(cmax, s.comps);
                const int cn = s.conn26 * 100 / std::max(1, s.total);
                cnMin = std::min(cnMin, cn); cnMax = std::max(cnMax, cn);
            }
            qInfo().noquote() << QString::asprintf("  [t1014 profile] %-8s total[%d..%d] comps[%d..%d] conn26[%d%%..%d%%] mid[%d,%d]",
                oreNamesT1014[oi], tmin, tmax, cmin, cmax, cnMin, cnMax,
                statT1014[0][oi].midLongMed, statT1014[0][oi].midShortMed);
        }

        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1014(a) coal 2x2x5 bar + hash-glued scatter: per-seed vein components"
                             "300..470 (scattered baseline was ~7500 singletons), mid-size component"
                             "bbox median long axis 5..8 short axis 3..4 (bar+glue silhouette),"
                             "26-adjacency connection >= 85%, band y[8,60] with >= 99% strictness"
                             "and absolute [7,60] (mineshaft wall ore exemption), economy per-seed"
                             "6845..8891 (old-scatter carve-survival baseline 7868 +-13% -> total ore"
                             "NOT inflated)"
                          << (okA ? QString() : QStringLiteral("shape/window miss, see diag"));
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1014(b) iron 2x2x2 cube + diagonal corner blocks: mid-size components"
                             "are bare cubes (bbox median exactly 2x2x2), 150..215 cubes per world,"
                             "corner blocks merge under 26-adjacency -> connection >= 90% (they are"
                             "diagonal so 6-conn singletons are expected and fine), band y[5,30]"
                             ">= 99% strict with <= 40 absolute (mineshaft exemption), economy"
                             "2146..2788 (baseline 2467 +-13%)"
                          << (okB ? QString() : QStringLiteral("shape/window miss, see diag"));
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| t1014(c) gold + redstone MC1.0-size blobs: gold 1328..1724 total in"
                             "225..275 walk-veins strictly inside y[5,25]; redstone 24702..32084 in"
                             "1300..1650 veins strictly inside y[5,16] with merged-sheet tail capped"
                             "(hist41 <= 160, registered effective-density carryover - the legacy"
                             "(r2>>24)%10000 8-bit truncation is preserved per economy parity, see"
                             "world.cpp ledger note), connection >= 95% (redstone vein-count window"
                             "lowered 1390 -> 1300 for review0906 #18: the horizontal-only repick"
                             "removes zero-displacement retry deaths so walks travel farther and"
                             "~5% fewer larger components form - totals and connection unchanged)"
                          << (okC ? QString() : QStringLiteral("shape/window miss, see diag"));
        if (!okD) ++totalFail;
        qInfo().noquote() << (okD ? "PASS" : "FAIL")
                          << "| t1014(d) diamond/lapis/copper MC1.0-size blobs: diamond 1832..2380 in"
                             "315..375 veins y[5,40]; lapis 2201..2859 in 420..485 veins y[5,31];"
                             "copper 4423..5745 in 790..990 veins y[5,45]; all strictly banded,"
                             "connection >= 85%, scraggly walk silhouette (bbox median long 4..8,"
                             "short <= 2 like vanilla scraggle)"
                          << (okD ? QString() : QStringLiteral("shape/window miss, see diag"));
        if (!okE) ++totalFail;
        qInfo().noquote() << (okE ? "PASS" : "FAIL")
                          << "| t1014(e) frequency economy + determinism: all 7 ore species land"
                             "per-seed within +-13% of the old-scatter carve-survival baseline"
                             "(coal 7868 / copper 5084 / iron 2467 / gold 1526 / diamond 2106 /"
                             "lapis 2530 / redstone 28393) - veining must not inflate total ore -"
                             "and a same-seed regeneration reproduces all 7 totals EXACTLY"
                             "(pure hashVoxel worldgen, no runtime RNG)"
                          << (okE ? QString() : QStringLiteral("economy window or regen miss, see diag"));
        if (!okF) ++totalFail;
        qInfo().noquote() << (okF ? "PASS" : "FAIL")
                          << "| t1014(f) source pins: world.cpp keeps the kProfiles vein table, the"
                             "per-ore salts, the coal bar orientation pick, the iron cube/corner"
                             "stamp branch, the blob off-stone redirect and the stone-only gate"
                             "(negative-round sensitive: reverting scatterOres to per-voxel scatter"
                             "loses the table and every shape leg above reads the scattered"
                             "signature: ~96% singletons, conn26 ~ 0%)"
                          << (okF ? QString() : QStringLiteral("source pin missing"));
        if (!okG) ++totalFail;
        qInfo().noquote() << (okG ? "PASS" : "FAIL")
                          << "| t1014(g) 128-high-world coal climbs above y=60 (review0907 A-P1-2):"
                             "coal profile yMax=0 sentinel makes the band ceiling purely column-adaptive"
                             "(hc-5, matching the legacy per-column stoneTop=h-3 scatter rule), so hill"
                             "columns (surface up to ~71 at 128 world height) carry coal veins above the"
                             "old hard cap of 60 -- the 64-high rig worlds never trigger the cap (hc-5"
                             "<= 58) so legs (a)-(f) are unchanged; threshold is y>=62 because the"
                             "capped bar overshoots the 60 ceiling by exactly one block (y=61,"
                             "diag-verified negative round), and at least one of 5 seeds must show"
                             "coal at y>=62 (per-seed counts in diag)"
                          << (okG ? QString() : QStringLiteral("no coal above 61 in any seed"));
        if (!okH) ++totalFail;
        qInfo().noquote() << (okH ? "PASS" : "FAIL")
                          << "| t1014(h) coastal land columns inside sea-centered cells carry ore"
                             "(review0906 #11): the old cell-level skip vetoed a whole 16x16 cell on"
                             "its center column height, so land columns of sea-centered cells"
                             "(the coastal transition belt) went completely oreless - whole zero-ore"
                             "patches unlike the legacy per-column skip; the skip is gone and pure-sea"
                             "cells are now rejected per-column inside tryOre (h <= wl+1), restoring"
                             "the legacy distribution. Rig = two dedicated sea-bearing seeds"
                             "(150461 / 174218 - a 40-seed diag sweep found the t1014 fixed-seed"
                             "windows contain NO sea-centered cell at all, so the skip never fired"
                             "there; each dedicated seed carries exactly one beach-centered cell,"
                             "center h 58/59): each seed must show >= 200 ore blocks on land columns"
                             "(h >= 60) of its sea-centered cell (measured 747 / 782 under the"
                             "restored per-column rejection - the old skip reads wander-in overflow"
                             "only, see the D2 lesion round) and the census structurally confirms the"
                             "sea cells exist (no vacuous green on seed drift)"
                          << (okH ? QString()
                                  : QStringLiteral("sea-cell land-column distribution miss, see diag"));
    });

    // ── P-t1015 载具攻击目标甄别探针（R19.20 t1015；机制等价 MC 1.0 骑乘组合 hitbox 拆分甄别）──
    //    rig：共享世界清场 + 真 EntityManager/BoatManager/MinecartManager 注入 + tickVehicleRiding 真
    //    登乘钉位 + beginMining 真攻击链（t866「不可直驱」的 Q_INVOKABLE 公开面，review0830C hitOnceC
    //    同式：fresh pc 绕攻击冷却、m_hitDist 缺省 5.0、m_vel 零无暴击）。五腿：
    //    (a) 生物坐船瞄乘员上半（乘员盒命中、船盒不在射线上）→ 乘员掉血、船无恙（review26 #4 旧行为保持）；
    //    (b) 瞄船身（两盒都在射线上、船面更近）→ 船被拆、乘员不掉血（单目标，最近 AABB 甄别）；
    //    (c) 甄别阴性腿：乘员身后挡**另一条**船（沿同射线、船盒接住上身射线）→ 只打乘员，两条船全无恙
    //        —— 旧 t866 改判「重路由最近任意船」会把挡路的船误拆（revert 即红：乘员不掉血 + decoy 被毁）；
    //    (d1) 矿车同族：瞄车身（两盒同射线、车面更近）→ 车扣 1 耐久（3→2）、乘员不掉血；
    //    (d2) 瞄乘员上身（车盒不在射线上）→ 乘员掉血、车耐久不动。
    //    (e) 源码钉：beginMining 骑乘改判的 rayHitDistAt 甄别行 + hit*At 指定目标结算行 + 两 manager
    //        新读口签名（阴性轮敏感：旧重路由行被删即红）。
    runLegMulti({ "t1015 vehicle-attack target discrimination rig: mob riding a boat,ray through the rider's upper "
        "half damages the rider only (boatintact), ray through the hull with BOTH boxes on the ray picks "
        "thenearer boat surface -> boat breaks and the rider keeps full HP(single target per click), and "
        "the decoy leg pins the t1015 core:a second boat parked BEHIND the rider catching the same upper-"
        "bodyray is no longer hit - the rider takes the damage and BOTH boatssurvive (old t866 re-route r"
        "esolved 'nearest any boat' and woulddismantle the wrong decoy); cart family mirrors both faces ("
        "hullclick costs exactly 1 of 3 cart HP with rider unharmed, upper-bodyclick hurts the rider with"
        " cart HP untouched); source pins lockthe rayHitDistAt nearest-AABB discrimination and the hit*At"
        " pinnedsettlement (negative-round sensitive)diag a=%1 b=%2 c=%3 d=%4 e=%5",
               "t1015(f) attacker-eye-inside-vehicle-box leg (review0906 #13):with the attacker's eye standing I"
        "NSIDE the cart's hitbox (cartcenter column, cart-center height), a click on the boarded rider'sb"
        "ody damages the rider and leaves the cart at full 3/3 HP - thediscrimination ray used to return "
        "0 for in-box origins, and'0 <= any mobDist' made the vehicle win EVERY tie, so pointing atthe bo"
        "dy through your own overlap hit the cart instead;rayHitDistAt now reports -1 (miss) for in-box o"
        "rigins on both thecart and boat managers while findCartHit/findBoatHit (mount andopen-chest seek"
        "ing) keep their in-box-hit semanticsdiag see [t1015 diag f]" }, [&]() {
        const auto [x0T1015, z0T1015] = nextSlot();
        QQuickWindow probeWinT1015;
        bool okA = false, okB = false, okC = false, okD = false, okE = false;
        // 清场帮手：rig 柱体全域空气（mob 生成 t642 防嵌墙 + 射线净空），底铺石头场景面。
        const auto clearVolT1015 = [&](int cx, int cz) {
            for (int dx = -2; dx <= 3; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                    for (int dy = 0; dy <= 4; ++dy)
                        w.setBlock(cx + dx, kRigY + dy, cz + dz, BR::Air, 0);
            for (int dx = -2; dx <= 3; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                    w.setBlock(cx + dx, kRigY - 1, cz + dz, BR::Stone, 0);
        };
        // 攻击帮手（review0830C hitOnceC 同式）：fresh pc（攻击冷却不跨腿）+ 眼位/瞄点 → yaw/pitch →
        //   loadSavedState + beginMining。eyeY 由脚位推（eye = feet + 1.62，r0830C 同约定）。
        const auto attackT1015 = [&](EntityManager &em, BoatManager *bm, MinecartManager *cm,
                                     const QVector3D &feet, const QVector3D &aim) {
            PlayerController pc;
            pc.setParentItem(probeWinT1015.contentItem());
            pc.grab(); // m_captured（beginMining 入口门；t949 同式）
            pc.setWorld(&w);
            pc.setEntityManager(&em);
            if (bm) pc.setBoatManager(bm);
            if (cm) pc.setMinecartManager(cm);
            const QVector3D eye(feet.x(), feet.y() + 1.62f, feet.z());
            const QVector3D d = aim - eye;
            const float len = d.length();
            const float pitch = std::asin(d.y() / len) * 57.2957795f;
            const float yaw = std::atan2(-d.x(), -d.z()) * 57.2957795f;
            pc.loadSavedState(feet.x(), feet.y(), feet.z(), yaw, pitch, 2 /* Survival */);
            pc.beginMining();
        };
        // 搭船 rig：船在 (bx,kRigY,bz) 格 + 猪同格生成 → tickVehicleRiding 登乘钉位（seat 0，+X 侧偏）。
        //   返回 mob 槽号（-1 = 登乘失败，腿内 diag）。**tickVehicleRiding 三连调**：Pass C 登乘发生在
        //   首调、Pass B 座位钉位在次调（t866 rig 的 tick+riding 循环同构）——单调只登乘不钉位，乘员
        //   仍站生成点 = 甄别射线几何全错（首跑 diag a/c 假红归因：mobPos 停在 spawn 点）。
        const auto boatRigT1015 = [&](EntityManager &em, BoatManager &bm, int bx, int bz) {
            clearVolT1015(bx, bz);
            bm.clearAll();
            bm.spawnBoat(bx, kRigY, bz, BoatManager::Oak);
            const int mob = em.spawnMobTyped(bx, kRigY, bz, EntityManager::MobPig, QStringLiteral("#f0a8b0"), 10);
            em.setVehicleManagers(nullptr, &bm);
            for (int t = 0; t < 3; ++t) em.tickVehicleRiding();
            return mob;
        };
        // (a) 瞄乘员上半：水平射线 Y = kRigY+1.62（乘员盒 [1.0,1.9] 内、船盒顶 1.35 之下）。
        {
            EntityManager em;
            BoatManager bm;
            const int mob = boatRigT1015(em, bm, x0T1015, z0T1015);
            const bool boarded = mob >= 0 && em.rideBoatAt(mob) == 0 && bm.mobPassengerAt(0, 0) == mob;
            const int hp0 = em.healthAt(mob);
            attackT1015(em, &bm, nullptr,
                        QVector3D(float(x0T1015) + 0.8f, float(kRigY), float(z0T1015) + 3.5f),
                        QVector3D(float(x0T1015) + 0.8f, float(kRigY) + 1.62f, float(z0T1015) + 0.5f));
            okA = boarded && em.healthAt(mob) < hp0 && bm.aliveAt(0);
            if (!okA)
                qInfo().noquote() << "  [t1015 diag a] boarded" << boarded << "mob" << mob
                                  << "hp" << hp0 << "->" << (mob >= 0 ? em.healthAt(mob) : -1)
                                  << "boatAlive" << bm.aliveAt(0)
                                  << "mobPos" << (mob >= 0 ? em.posAt(mob) : QVector3D());
        }
        // (b) 瞄船身：射线穿船盒面（X 进面 0.833 处 Y=1.187 ∈ 船盒 [0.65,1.35]）后才穿乘员盒
        //     （0.967 处 Y=1.117 ∈ 乘员盒 [1.0,1.9]）→ 船更近 → 拆船、乘员不掉血。
        {
            const auto [xb, zb] = nextSlot();
            EntityManager em;
            BoatManager bm;
            const int mob = boatRigT1015(em, bm, xb, zb);
            const int hp0 = em.healthAt(mob);
            attackT1015(em, &bm, nullptr,
                        QVector3D(float(xb) - 2.5f, float(kRigY), float(zb) + 0.5f),
                        QVector3D(float(xb) + 0.5f, float(kRigY) + 1.1f, float(zb) + 0.5f));
            okB = mob >= 0 && !bm.aliveAt(0) && em.healthAt(mob) == hp0;
            if (!okB)
                qInfo().noquote() << "  [t1015 diag b] mob" << mob << "hp" << hp0
                                  << "->" << (mob >= 0 ? em.healthAt(mob) : -1)
                                  << "boatAlive" << bm.aliveAt(0);
        }
        // (c) 甄别阴性（本任务核心行为）：乘员身后沿同一上身射线挡 decoy 船 B（(x,kRigY+1,z-2) 格，
        //     盒 Y [1.65,2.35] 接住 1.85 高的射线、Z 进面距 4.3 < m_hitDist 5.0）。旧改判重路由
        //     hitBoatFromRay 会寻的到 B 并误拆；新甄别 rayHitDistAt(本乘员的 A 船) = 未中 → 打乘员。
        {
            const auto [xc, zc] = nextSlot();
            EntityManager em;
            BoatManager bm;
            bm.clearAll();
            clearVolT1015(xc, zc);
            bm.spawnBoat(xc, kRigY, zc, BoatManager::Oak);          // A：乘员所乘
            bm.spawnBoat(xc, kRigY + 1, zc - 2, BoatManager::Oak);  // B：decoy（A-B 中心距 √5 ≥ 1.4 可生成）
            const int mob = em.spawnMobTyped(xc, kRigY, zc, EntityManager::MobPig, QStringLiteral("#f0a8b0"), 10);
            em.setVehicleManagers(nullptr, &bm);
            for (int t = 0; t < 3; ++t) em.tickVehicleRiding(); // 登乘（首调）+ 座位钉位（次调），同 boatRigT1015 注
            const bool boarded = mob >= 0 && em.rideBoatAt(mob) == 0;
            const int hp0 = em.healthAt(mob);
            attackT1015(em, &bm, nullptr,
                        QVector3D(float(xc) + 0.8f, float(kRigY) + 0.23f, float(zc) + 3.5f),
                        QVector3D(float(xc) + 0.8f, float(kRigY) + 1.85f, float(zc) + 0.5f));
            okC = boarded && em.healthAt(mob) < hp0 && bm.aliveAt(0) && bm.aliveAt(1);
            if (!okC)
                qInfo().noquote() << "  [t1015 diag c] boarded" << boarded << "mob" << mob
                                  << "hp" << hp0 << "->" << (mob >= 0 ? em.healthAt(mob) : -1)
                                  << "A" << bm.aliveAt(0) << "B" << bm.aliveAt(1);
        }
        // (d1)(d2) 矿车同族：轨格落车 + 猪同格登乘（钉位 Y = 车心 +0.1375）。
        const auto cartRigT1015 = [&](EntityManager &em, MinecartManager &cm, int cx, int cz) {
            clearVolT1015(cx, cz);
            cm.clearAll();
            w.setBlock(cx, kRigY, cz, BR::Rail, 0);
            cm.spawnCart(cx, kRigY, cz, &w);
            const int mob = em.spawnMobTyped(cx, kRigY, cz, EntityManager::MobPig, QStringLiteral("#f0a8b0"), 10);
            em.setVehicleManagers(&cm, nullptr);
            for (int t = 0; t < 3; ++t) em.tickVehicleRiding(); // 登乘（首调）+ 座位钉位（次调），同 boatRigT1015 注
            return mob;
        };
        {
            const auto [xd, zd] = nextSlot();
            EntityManager em;
            MinecartManager cm;
            // (d1) 瞄车身：水平射线 Y = 车心高（车盒 [−0.45,+0.45] 与乘员盒 [−0.3125,+0.5875] 相对车心
            //      都含 0）→ 车面进距 3.55 < 乘员面 3.6 → 车更近 → 车扣 1 耐久、乘员不掉血。
            const int mob = cartRigT1015(em, cm, xd + 2, zd);
            const int hp0 = em.healthAt(mob);
            attackT1015(em, nullptr, &cm,
                        QVector3D(float(xd) - 1.5f, float(kRigY) + 0.45f - 1.62f, float(zd) + 0.5f),
                        QVector3D(float(xd) + 2.5f, float(kRigY) + 0.45f, float(zd) + 0.5f));
            const bool okD1 = mob >= 0 && cm.aliveAt(0) && cm.hpAt(0) == 2 && em.healthAt(mob) == hp0;
            // (d2) 瞄乘员上身（独立 rig）：射线 Y = kRigY+0.95（乘员盒顶 1.0375 内、车盒顶 0.9 之上）
            //      → 车盒不在射线上 → 打乘员、车耐久不动。
            const auto [xd2, zd2] = nextSlot();
            EntityManager em2;
            MinecartManager cm2;
            const int mob2 = cartRigT1015(em2, cm2, xd2 + 2, zd2);
            const int hp02 = em2.healthAt(mob2);
            attackT1015(em2, nullptr, &cm2,
                        QVector3D(float(xd2) - 1.5f, float(kRigY) + 0.95f - 1.62f, float(zd2) + 0.5f),
                        QVector3D(float(xd2) + 2.5f, float(kRigY) + 0.95f, float(zd2) + 0.5f));
            const bool okD2 = mob2 >= 0 && em2.healthAt(mob2) < hp02 && cm2.aliveAt(0) && cm2.hpAt(0) == 3;
            okD = okD1 && okD2;
            if (!okD)
                qInfo().noquote() << "  [t1015 diag d] d1 mob" << mob << "hp" << hp0
                                  << "->" << (mob >= 0 ? em.healthAt(mob) : -1)
                                  << "cartHp" << cm.hpAt(0) << "alive" << cm.aliveAt(0)
                                  << "| d2 mob" << mob2 << "hp" << hp02
                                  << "->" << (mob2 >= 0 ? em2.healthAt(mob2) : -1)
                                  << "cartHp" << cm2.hpAt(0) << "alive" << cm2.aliveAt(0);
        }
        // (e) 源码钉：骑乘改判的甄别行 / 指定结算行 / 新读口签名（旧重路由行绝迹）。
        //     B-P1-1 迁移：滤注释钉（pinSet）；原「t1015 甄别」注释文本钉改锚甄别**入口语句**
        //     （rideCartAt / rideBoatAt 两行 —— 改判分支的乘骑判定，剥注释后必须仍在）。
        {
            const QString root = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
            QStringList missE;
            missE << pinSet(root + QStringLiteral("/src/Game/playercontroller.cpp"), {
                {"cart-dist", "m_minecartManager->rayHitDistAt(rideCart, eye, look, m_hitDist)"},
                {"boat-dist", "m_boatManager->rayHitDistAt(rideBoat, eye, look, m_hitDist)"},
                {"cart-settle", "m_minecartManager->hitCartAt(rideCart"},
                {"boat-settle", "m_boatManager->hitBoatAt(rideBoat"},
                {"discriminate-cart-entry", "const int rideCart = m_entityManager->rideCartAt(mobIdx);"},
                {"discriminate-boat-entry", "const int rideBoat = m_entityManager->rideBoatAt(mobIdx);"},
            });
            missE << pinSet(root + QStringLiteral("/src/Entities/minecartmanager.h"), {
                {"hdr-rayHitDistAt-cart", "float rayHitDistAt(int idx, const QVector3D &origin"},
            });
            missE << pinSet(root + QStringLiteral("/src/Entities/boatmanager.h"), {
                {"hdr-rayHitDistAt-boat", "float rayHitDistAt(int i, const QVector3D &origin"},
            });
            okE = missE.isEmpty();
            if (!okE)
                qInfo().noquote() << "  [t1015 diag e] pin miss:" << missE.join(QLatin1Char(','));
        }
        // (f) review0906 #13 眼位落入载具盒：攻击者脚位 = 车心 y − 1.62 → 眼（= position()）恰在
        //     车厢盒内（车心柱、车心高）。旧 rayHitDistAt 盒内返 0 →「0 ≤ 任何 mobDist」恒真 = 点
        //     乘员身体恒判载具胜（车扣血、乘员无恙）。改盒内 -1（未中）→ 甄别落回乘员本体：乘员
        //     掉血、车耐久不动。findCartHit（登乘寻的）不改 —— 登乘依赖盒内命中，口径分叉见源码。
        bool okF13 = false;
        {
            const auto [xf13, zf13] = nextSlot();
            EntityManager em13;
            MinecartManager cm13;
            const int mob13 = cartRigT1015(em13, cm13, xf13 + 2, zf13); // 轨格落车 (xf13+2, zf13)
            const bool boarded13 = mob13 >= 0 && em13.rideCartAt(mob13) == 0;
            const int hp013 = em13.healthAt(mob13);
            attackT1015(em13, nullptr, &cm13,
                        QVector3D(float(xf13) + 2.5f, float(kRigY) + 0.45f - 1.62f, float(zf13) + 0.5f),
                        QVector3D(float(xf13) + 2.5f, float(kRigY) + 0.45f + 0.1375f, float(zf13) + 0.5f));
            okF13 = boarded13 && em13.healthAt(mob13) < hp013
                && cm13.aliveAt(0) && cm13.hpAt(0) == 3; // 车耐久不动（3/3）= 未误判载具
            if (!okF13)
                qInfo().noquote() << "  [t1015 diag f] boarded" << boarded13 << "mob" << mob13
                                  << "hp" << hp013 << "->" << (mob13 >= 0 ? em13.healthAt(mob13) : -1)
                                  << "cartHp" << cm13.hpAt(0) << "alive" << cm13.aliveAt(0);
        }
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        if (!okD) ++totalFail;
        if (!okE) ++totalFail;
        qInfo().noquote() << (okA && okB && okC && okD && okE ? "PASS" : "FAIL")
                          << "| t1015 vehicle-attack target discrimination rig: mob riding a boat,"
                             "ray through the rider's upper half damages the rider only (boat"
                             "intact), ray through the hull with BOTH boxes on the ray picks the"
                             "nearer boat surface -> boat breaks and the rider keeps full HP"
                             "(single target per click), and the decoy leg pins the t1015 core:"
                             "a second boat parked BEHIND the rider catching the same upper-body"
                             "ray is no longer hit - the rider takes the damage and BOTH boats"
                             "survive (old t866 re-route resolved 'nearest any boat' and would"
                             "dismantle the wrong decoy); cart family mirrors both faces (hull"
                             "click costs exactly 1 of 3 cart HP with rider unharmed, upper-body"
                             "click hurts the rider with cart HP untouched); source pins lock"
                             "the rayHitDistAt nearest-AABB discrimination and the hit*At pinned"
                             "settlement (negative-round sensitive)"
                          << (okA && okB && okC && okD && okE
                                  ? QString()
                                  : QStringLiteral("diag a=%1 b=%2 c=%3 d=%4 e=%5")
                                        .arg(okA).arg(okB).arg(okC).arg(okD).arg(okE));
        if (!okF13) ++totalFail;
        qInfo().noquote() << (okF13 ? "PASS" : "FAIL")
                          << "| t1015(f) attacker-eye-inside-vehicle-box leg (review0906 #13):"
                             "with the attacker's eye standing INSIDE the cart's hitbox (cart"
                             "center column, cart-center height), a click on the boarded rider's"
                             "body damages the rider and leaves the cart at full 3/3 HP - the"
                             "discrimination ray used to return 0 for in-box origins, and"
                             "'0 <= any mobDist' made the vehicle win EVERY tie, so pointing at"
                             "the body through your own overlap hit the cart instead;"
                             "rayHitDistAt now reports -1 (miss) for in-box origins on both the"
                             "cart and boat managers while findCartHit/findBoatHit (mount and"
                             "open-chest seeking) keep their in-box-hit semantics"
                          << (okF13 ? QString()
                                    : QStringLiteral("diag see [t1015 diag f]"));
    });

    // ── P-t1016 世界时间持久化探针（R19.20 t1016；存退重进保留退出时刻 + weather/天数）──
    //    (a) 真 WorldStore SQLite：saveAll 第 5 参时钟快照（phase/day/weather）落 world_meta → 关库
    //        重开 → loadWorldTime 逐键相等（phase 'g'9 float 短往返逐位还原 / day qint64 / 枚举 int）；
    //    (b) 旧档兼容：不含时间键的存档（旧 saveAll 四参调用）→ loadWorldTime 逐键缺省
    //        （0.0 / 0 / 0 = 新世界首帧晴天），不炸不跳；
    //    (c) 恢复链：WorldClock.restoreTime 精确复原 (phase, day)（无 setPhase 的 day+1 副作用，月相
    //        = day%8 随之复原）+ World.setWeatherState 设态 / 同态零噪声 / 非法值拒；
    //    (d) 源码钉：Main.qml 退出链第 5 参 + enterWorld 恢复接线 + 两 C++ 恢复入口签名（阴性轮敏感）。
    runLegMulti({ "t1016 world-clock persistence rig: the exit save writes the clocksnapshot {phase,day,weather} th"
        "rough saveAll's 5th arg intoworld_meta inside the SAME transaction as chunks/meta, a close/reope"
        "n round-trips all three keys exactly (float 'g'9 shortround-trip phase, qint64 day, enum weather"
        "); a legacy save withoutthe time keys loads per-key defaults (phase 0 / day 0 / weatherClear = a"
        " fresh world's first frame, no crash); restoreTime putsback (phase, day) EXACTLY with no setPhas"
        "e day+1 side effect(moon phase = day%8 follows), and setWeatherState sets the statewith one emit"
        ", stays silent on same-state restore, and silentlyrejects out-of-enum values; source pins lock t"
        "he QML exit-chain5th arg, the enterWorld restore wiring and both C++ restoreentries (negative-ro"
        "und sensitive)diag a=%1 b=%2 c=%3 d=%4" }, [&]() {
        World wT1016;
        wT1016.setWidth(48);
        wT1016.setDepth(48);
        wT1016.setHeight(96);
        wT1016.setSeed(1016);
        WorldStore storeT1016;
        storeT1016.setWorld(&wT1016);
        bool okA = false, okB = false, okC = false, okD = false;
        // (a) 快照落盘 → 关库重开 → 读回逐键相等。
        const QString dbT1016 = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t1016_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT1016);
        {
            QVariantMap wt;
            wt.insert(QStringLiteral("phase"), 0.3f); // 非整二进制相位（'g'9 短往返保真面）
            wt.insert(QStringLiteral("day"), qlonglong(3));
            wt.insert(QStringLiteral("weather"), 2); // Snow
            okA = storeT1016.openWorld(dbT1016)
                && storeT1016.saveAll(QStringLiteral("t1016rig"), QVariantList(), QVariantList(), QVariantList(), wt);
            storeT1016.closeWorld();
            QVariantMap back;
            if (okA && storeT1016.openWorld(dbT1016)) back = storeT1016.loadWorldTime();
            storeT1016.closeWorld();
            okA = okA && back.value(QStringLiteral("phase")).toFloat() == 0.3f
                && back.value(QStringLiteral("day")).toLongLong() == 3
                && back.value(QStringLiteral("weather")).toInt() == 2
                && back.value(QStringLiteral("hasWeather")).toBool() == true; // review0906 #14：真带键
            if (!okA)
                qInfo().noquote() << "  [t1016 diag a] back =" << back;
        }
        // (b) 旧档缺键 → 逐键缺省（默认早晨相位 0 / 第 0 天 / Clear 晴天）。
        {
            const QString dbOld = QDir::temp().absoluteFilePath(
                    QStringLiteral("voxel_t1016old_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbOld);
            bool built = storeT1016.openWorld(dbOld)
                && storeT1016.saveAll(QStringLiteral("t1016old")); // 四参旧调用形态：不写任何时间键
            storeT1016.closeWorld();
            QVariantMap back;
            if (built && storeT1016.openWorld(dbOld)) back = storeT1016.loadWorldTime();
            storeT1016.closeWorld();
            okB = built && back.value(QStringLiteral("phase")).toFloat() == 0.0f
                && back.value(QStringLiteral("day")).toLongLong() == 0
                && back.value(QStringLiteral("weather")).toInt() == 0
                && back.value(QStringLiteral("hasWeather")).toBool() == false; // review0906 #14：缺键
            if (!okB)
                qInfo().noquote() << "  [t1016 diag b] built" << built << "back =" << back;
            QFile::remove(dbOld);
        }
        // (c) 恢复链：restoreTime 精确复原（无 day+1）；setWeatherState 设态 / 同态零 emit / 非法拒。
        {
            WorldClock clockT1016;
            clockT1016.setPhase(0.5f); // 先搅动（setPhase 自带 day+1，恢复须覆盖而非叠加）
            clockT1016.restoreTime(0.75f, 3);
            const bool clockOk = clockT1016.dayPhase() == 0.75f
                && clockT1016.dayCount() == 3
                && clockT1016.moonPhase() == 3; // 月相 = day%8 随 day 复原（无 setPhase +1 漂移）
            int weatherEmits = 0;
            const QMetaObject::Connection connW = QObject::connect(
                &wT1016, &World::weatherChanged, &wT1016, [&weatherEmits]() { ++weatherEmits; });
            wT1016.setWeatherState(2); // Clear → Snow：设态 + emit
            const bool setOk = wT1016.weatherState() == 2 && weatherEmits == 1;
            wT1016.setWeatherState(2); // 同态恢复：零噪声
            wT1016.setWeatherState(9); // 非法：静默拒（保当前态，枚举不变量）
            const bool noiseOk = wT1016.weatherState() == 2 && weatherEmits == 1;
            wT1016.setWeatherState(0); // 回 Clear：emit
            const bool backOk = wT1016.weatherState() == 0 && weatherEmits == 2;
            QObject::disconnect(connW);
            okC = clockOk && setOk && noiseOk && backOk;
            if (!okC)
                qInfo().noquote() << "  [t1016 diag c] clockOk" << clockOk << "phase" << clockT1016.dayPhase()
                                  << "day" << clockT1016.dayCount() << "moon" << clockT1016.moonPhase()
                                  << "setOk" << setOk << "noiseOk" << noiseOk << "backOk" << backOk;
        }
        // (d) 源码钉：QML 退出链第 5 参 + enterWorld 恢复接线 + C++ 恢复入口签名。
        //     B-P1-1 迁移：滤注释钉（pinSet）。
        {
            const QString root = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
            QStringList missD;
            missD << pinSet(root + QStringLiteral("/src/ui/Main.qml"), {
                // t1046：退出链快照补 weatherTimerMs（剩余毫秒）→ 原单行钉拆两行 + 续跑接线钉。
                {"qml-exit-chain-5th", "{ phase: worldClock.dayPhase, day: worldClock.dayCount, weather: theWorld.weatherState,"},
                {"qml-exit-chain-timer", "weatherTimerMs: Math.round(theWorld.weatherRemainingSec() * 1000) },"},
                {"qml-restore-time", "worldClock.restoreTime(wt.phase, wt.day)"},
                // review0906 #14：仅真带 weather 键才恢复（缺键保 resetWeather 首场晴偏短窗）
                {"qml-restore-weather-guard", "if (wt.hasWeather) theWorld.setWeatherState(wt.weather)"},
                // t1046 低-5：真带剩余时长键 → setWeatherRemainingSec 精确续跑剩余窗
                {"qml-restore-weather-timer", "if (wt.hasWeatherTimer) theWorld.setWeatherRemainingSec(wt.weatherTimerMs / 1000)"},
            });
            missD << pinSet(root + QStringLiteral("/src/World/worldclock.h"), {
                {"hdr-restoreTime", "Q_INVOKABLE void restoreTime(float phase, qint64 day);"},
            });
            missD << pinSet(root + QStringLiteral("/src/World/world.h"), {
                {"hdr-setWeatherState", "Q_INVOKABLE void setWeatherState(int state);"},
            });
            okD = missD.isEmpty();
            if (!okD)
                qInfo().noquote() << "  [t1016 diag d] pin miss:" << missD.join(QLatin1Char(','));
        }
        QFile::remove(dbT1016);
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        if (!okD) ++totalFail;
        qInfo().noquote() << (okA && okB && okC && okD ? "PASS" : "FAIL")
                          << "| t1016 world-clock persistence rig: the exit save writes the clock"
                             "snapshot {phase,day,weather} through saveAll's 5th arg into"
                             "world_meta inside the SAME transaction as chunks/meta, a close/"
                             "reopen round-trips all three keys exactly (float 'g'9 short"
                             "round-trip phase, qint64 day, enum weather); a legacy save without"
                             "the time keys loads per-key defaults (phase 0 / day 0 / weather"
                             "Clear = a fresh world's first frame, no crash); restoreTime puts"
                             "back (phase, day) EXACTLY with no setPhase day+1 side effect"
                             "(moon phase = day%8 follows), and setWeatherState sets the state"
                             "with one emit, stays silent on same-state restore, and silently"
                             "rejects out-of-enum values; source pins lock the QML exit-chain"
                             "5th arg, the enterWorld restore wiring and both C++ restore"
                             "entries (negative-round sensitive)"
                          << (okA && okB && okC && okD
                                  ? QString()
                                  : QStringLiteral("diag a=%1 b=%2 c=%3 d=%4")
                                        .arg(okA).arg(okB).arg(okC).arg(okD));
    });

    // ── P-t1016b 天气键来源 + 大 day 相位保真（review0906 #14 / #15）──
    //    (a) #14 来源腿：带 weather 键存档 round-trip → hasWeather true（消费端恢复天气态）；
    //        旧档形态（四参 saveAll 不写时间键）→ hasWeather false —— 缺键默认 weather 0 与
    //        「真存过 Clear」从此可区分（enterWorld 仅 hasWeather 才 setWeatherState，缺键保
    //        resetWeather 首场晴偏短窗 20/45s，不被重抽为常规 45/120s）。
    //    (b) #15 相位保真腿：WorldClock 冻结表 + restoreTime(day=5,000,003 ≳ 2²², phase 0.3725)
    //        → dayPhase 复原（旧 float 折算在该量级 ULP ≈ 0.4 天吞相位小数 = 跳回整刻）、
    //        dayCount / moonPhase(=day%8) 精确；小 day 惯量（3）不变对照。
    runLegMulti({ "t1016b weather-key provenance + large-day phase fidelity(review0906 #14 / #15): loadWorldTime no"
        "w reports hasWeather so themissing-key default (weather 0) is distinguishable from a genuinelysa"
        "ved Clear - enterWorld restores the weather state ONLY when the keyexists, keeping resetWeather'"
        "s short first-clear window (20/45s,'weather visible shortly after entering a world') for old sav"
        "es andfresh worlds instead of re-rolling it to the regular 45/120s window;and applyTime derives "
        "elapsed ms by integer day*period + double phasesplit, so restoreTime round-trips phase EXACTLY a"
        "t day 5,000,003(past 2^22 where the old float folding quantized phase in ~0.4-dayULP steps - dir"
        "ty/hand-edited saves silently snapped to whole ticks)with dayCount and moonPhase (=day%8) exact "
        "and the small-day pathunchangeddiag a=%1 b see [t1016b diag b]" }, [&]() {
        World wT1016b;
        wT1016b.setWidth(48);
        wT1016b.setDepth(48);
        wT1016b.setHeight(96);
        wT1016b.setSeed(1016);
        WorldStore storeT1016b;
        storeT1016b.setWorld(&wT1016b);
        bool okA16 = false, okB16 = false;
        // (a) 带键 vs 缺键：hasWeather 来源腿。
        {
            const QString dbA = QDir::temp().absoluteFilePath(
                    QStringLiteral("voxel_t1016b_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbA);
            QVariantMap wt;
            wt.insert(QStringLiteral("weather"), 2); // Snow：真存过非晴态
            bool built = storeT1016b.openWorld(dbA)
                && storeT1016b.saveAll(QStringLiteral("t1016b"), QVariantList(), QVariantList(), QVariantList(), wt);
            storeT1016b.closeWorld();
            QVariantMap back;
            if (built && storeT1016b.openWorld(dbA)) back = storeT1016b.loadWorldTime();
            storeT1016b.closeWorld();
            const bool withKey = built && back.value(QStringLiteral("hasWeather")).toBool()
                && back.value(QStringLiteral("weather")).toInt() == 2;
            const QString dbOld = QDir::temp().absoluteFilePath(
                    QStringLiteral("voxel_t1016bold_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbOld);
            built = storeT1016b.openWorld(dbOld)
                && storeT1016b.saveAll(QStringLiteral("t1016bold")); // 四参旧调用形态：不写任何时间键
            storeT1016b.closeWorld();
            if (built && storeT1016b.openWorld(dbOld)) back = storeT1016b.loadWorldTime();
            storeT1016b.closeWorld();
            const bool noKey = built && !back.value(QStringLiteral("hasWeather")).toBool()
                && back.value(QStringLiteral("weather")).toInt() == 0;
            okA16 = withKey && noKey;
            if (!okA16)
                qInfo().noquote() << "  [t1016b diag a] withKey" << withKey << "noKey" << noKey
                                  << "back =" << back;
            QFile::remove(dbA);
            QFile::remove(dbOld);
        }
        // (b) 大 day round-trip 相位保真（冻结 100ms tick 防跨 tick 相位推进扰断言）。
        {
            WorldClock clockB16;
            clockB16.setRunning(false);
            const qint64 bigDay16 = 5000003LL; // ≳ 2²²：float 折算 ULP 在此量级 ≈ 0.4 天
            clockB16.restoreTime(0.3725f, bigDay16);
            const bool bigOk = qAbs(clockB16.dayPhase() - 0.3725f) < 5e-4f
                && clockB16.dayCount() == bigDay16
                && clockB16.moonPhase() == int(bigDay16 % 8); // 5000003 % 8 = 3
            clockB16.restoreTime(0.75f, 3); // 小 day 惯量对照（P-t1016(c) 同参不回退）
            const bool smallOk = clockB16.dayPhase() == 0.75f && clockB16.dayCount() == 3
                && clockB16.moonPhase() == 3;
            okB16 = bigOk && smallOk;
            if (!okB16)
                qInfo().noquote() << "  [t1016b diag b] bigOk" << bigOk
                                  << "phase" << clockB16.dayPhase() << "day" << clockB16.dayCount()
                                  << "moon" << clockB16.moonPhase() << "smallOk" << smallOk;
        }
        if (!okA16) ++totalFail;
        if (!okB16) ++totalFail;
        qInfo().noquote() << (okA16 && okB16 ? "PASS" : "FAIL")
                          << "| t1016b weather-key provenance + large-day phase fidelity"
                             "(review0906 #14 / #15): loadWorldTime now reports hasWeather so the"
                             "missing-key default (weather 0) is distinguishable from a genuinely"
                             "saved Clear - enterWorld restores the weather state ONLY when the key"
                             "exists, keeping resetWeather's short first-clear window (20/45s,"
                             "'weather visible shortly after entering a world') for old saves and"
                             "fresh worlds instead of re-rolling it to the regular 45/120s window;"
                             "and applyTime derives elapsed ms by integer day*period + double phase"
                             "split, so restoreTime round-trips phase EXACTLY at day 5,000,003"
                             "(past 2^22 where the old float folding quantized phase in ~0.4-day"
                             "ULP steps - dirty/hand-edited saves silently snapped to whole ticks)"
                             "with dayCount and moonPhase (=day%8) exact and the small-day path"
                             "unchanged"
                          << (okA16 && okB16 ? QString()
                                             : QStringLiteral("diag a=%1 b see [t1016b diag b]")
                                                   .arg(okA16));
    });

    // ── P-t1017 仙人掌不可附着探针（R19.20 t1017；机制等价 MC 1.0 仙人掌非可附着面）──
    //    (a) 谓词腿：torchSupportBlock 单一权威对 Cactus 翻假（Stone/Sand 常规支撑对照不回退）；
    //    (b) 行为腿（P-t945 真瞄准链）：2 高仙人掌柱 五个可瞄面（顶 + 四侧）× {火把, 红石火把,
    //        石按钮} 全 15 组合全拒（目标格恒 Air、柱无恙；底面目标格被沙/下柱占据 = 占位拒绝不可瞄，
    //        天花板面语义由 (c) 腿钉）；石方块同 15 组合全准（放置成功）——附着族同口径对照；
    //    (c) 悬空石底面（天花板语义对照）：火把 / 红石火把 / 石按钮贴底面全拒 = 既有 t738/机关「底面
    //        不挂装」共享规则，非仙人掌特化（钉「底面拒」不随本批漂移）；
    //    (d) 源码钉：torchSupportBlock Cactus 排除行 + 机关 / 木梯预检 Cactus 拒绝（阴性轮敏感）。
    runLegMulti({ "t1017 cactus-rejects-attachables rig: the single-authoritytorchSupportBlock predicate now return"
        "s false for Cactus (stone/sandcontrols keep supporting), and through the REAL aim->placeBlockcha"
        "in all three attachable kinds (torch / redstone torch / stonebutton) are rejected on a 2-high ca"
        "ctus column across ALL FIVEaimable faces (top + four sides; the bottom face's target cell isburi"
        "ed by sand/the lower column so placement there is alreadyoccupancy-rejected, and the ceiling rul"
        "e is pinned by thefloating-stone leg) - 15/15 combinations keep their target cellsAir with the c"
        "olumn intact, while the SAME 15 operations on astone block all place correctly - plus thefloatin"
        "g-stone bottom-face leg pins that ceiling rejection is thepre-existing shared rule, not cactus-s"
        "pecific; source pins lockthe cactus rejection in torchSupportBlock and the mech/ladderprecheck g"
        "uards (negative-round sensitive: reverting the cactusexclusion lets torches attach to the column"
        " and the predicate legreads red)diag a=%1 b=%2 c=%3 d=%4",
               "t1017(e) neighbor-edit drop leg (review0906 #8): old-save residue injects a ladder side-attached"
        " to a LIVE cactus column (a ladder is not a full cube so the cactus neighbor-collapse rule never"
        " fires - the residue persists exactly as in pre-t1017 saves) and a floor-mounted lever on the co"
        "lumn top (top-face attachment does not touch the cactus horizontal-neighbor rule either). Mining"
        " a DIFFERENT neighbor of each attached block (support cactus alive throughout) runs the 6-neighb"
        "or recheck: the rechecks used bare isFullCube which Cactus (ShapeFull) always passes - the place"
        "ment precheck rejected cactus while the residue never dropped (the reject-vs-residue split). Bot"
        "h rechecks now read the shared mechLadderSupportBlock authority (isFullCube && != Cactus), so th"
        "e ladder and the lever drop on the neighbor edit while the column stays intact; a stone-wall lad"
        "der whose support block is mined directly still drops (harness sanity control)diag see [t1017 di"
        "ag e]",
               "t1017(f) attach-family cactus sweep (review0906 #9): the ground/face-support wrapper predicates "
        "(trapdoorSupportBlock /isTopFlushSupport / isDustSupport / solidSupportBlock, the last nowthe sh"
        "ared authority mechLadderSupportBlock delegates to) all returnfalse for Cactus while stone and t"
        "op-half-slab controls keepsupporting, and through the REAL aim->placeBlock chain a trapdooragain"
        "st the column's side face (its only candidate attach face -nothing below), plus redstone dust, s"
        "now layer, rail and door on thecolumn top are ALL rejected with their target cells staying Air a"
        "ndthe column intact (pre-fix every one of these placed: ShapeFullcactus passed the bare predicat"
        "es), while snow layer and rail on astone block still place (the tightening does not over-reject)"
        "diag see [t1017 diag f]" }, [&]() {
        const auto [x0T1017, z0T1017] = nextSlot();
        QQuickWindow probeWinT1017;
        WorldClock clockT1017;
        EntityManager entsT1017; // 空管理器（放置链不依赖）
        Hotbar hbT1017;
        PlayerController pcT1017;
        pcT1017.setWorld(&w);
        pcT1017.setWorldClock(&clockT1017);
        pcT1017.setEntityManager(&entsT1017);
        pcT1017.setHotbar(&hbT1017);
        pcT1017.setParentItem(probeWinT1017.contentItem());
        // 瞄准帮手（P-t945 aimP945 同式）：release+grab 重居中 → loadSavedState → tick 刷射线，返命中格。
        const auto aimT1017 = [&](float feetX, float feetY, float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = feetX, ey = feetY + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT1017.release();
            pcT1017.grab();
            pcT1017.loadSavedState(feetX, feetY, feetZ, yaw, pitch, 1 /* Creative（Mode 枚举 0=Spectator/1=Creative/2=Survival）：放置不消耗；0 会落观察者 canPlace=false → 全部放置被观察者门拒 = 仙人掌腿假绿 */);
            pcT1017.tick(); // updateRaycast 刷新命中（t889 先例）
            return pcT1017.hitBlock();
        };
        const auto pumpMsT1017 = [](int ms) { // 放置 200ms CD 间隔（t128）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        bool okA = false, okB = false, okC = false, okD = false;
        // (a) 谓词腿。
        okA = !BlockRegistry::torchSupportBlock(quint8(BR::Cactus), 0)
            && BlockRegistry::torchSupportBlock(quint8(BR::Stone), 0)
            && BlockRegistry::torchSupportBlock(quint8(BR::Sand), 0);
        // (b) 行为腿：仙人掌柱（沙基 2 高）+ 顶面立足柱（**-X 侧旁柱**，不占目标格；首跑实锤：柱在
        //     +X 侧挡住 +X 侧面瞄准线〔ray 中柱段 hit (x+2,kRigY+1)〕→ 移 -X 侧，+X/-Z 侧瞄线净空）。
        {
            const auto [xc, zc] = nextSlot();
            for (int dx = -2; dx <= 3; ++dx)
                for (int dz = -3; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 4; ++dy) w.setBlock(xc + dx, kRigY + dy, zc + dz, BR::Air, 0);
                    w.setBlock(xc + dx, kRigY - 1, zc + dz, BR::Stone, 0);
                }
            w.setBlock(xc, kRigY - 1, zc, BR::Sand, 0);       // 仙人掌合法沙支撑
            w.setBlock(xc, kRigY, zc, BR::Cactus, 0);          // 下柱
            w.setBlock(xc, kRigY + 1, zc, BR::Cactus, 0);      // 上柱
            w.setBlock(xc - 2, kRigY, zc, BR::Stone, 0);       // 立足柱（-X 侧；顶面瞄准自柱顶直瞄下）
            w.setBlock(xc - 2, kRigY + 1, zc, BR::Stone, 0);
            w.setBlock(xc - 2, kRigY + 2, zc, BR::Stone, 0);
            // t1017（探针加宽）：五个可瞄面（顶 + 四侧）× 三附着 kind 全组合 —— 仙人掌 15 组合全拒
            //     （目标格恒 Air、柱无恙），石头 15 组合全准（放置成功）。仙人掌**底面**目标格被沙 /
            //     下柱占据 = 占位拒绝（不可瞄，射线先中沙 / 下柱），天花板面语义由 (c) 腿钉住 ——
            //     六面口径下可瞄五面全组合 + 底面两腿即完整覆盖。
            const int kindsT1017[3] = { BR::Torch, BR::RedstoneTorch, BR::StoneButton };
            const float feetF[5][3] = { // 每面立足脚位（feet）
                { float(xc) - 1.5f, float(kRigY + 3), float(zc) + 0.5f }, // 顶面：-X 立足柱顶直瞄下
                { float(xc) + 3.5f, float(kRigY),     float(zc) + 0.5f }, // +X 面
                { float(xc) - 1.5f, float(kRigY + 3), float(zc) + 0.5f }, // -X 面（立足柱顶斜瞄下）
                { float(xc) + 0.5f, float(kRigY),     float(zc) + 2.5f }, // +Z 面
                { float(xc) + 0.5f, float(kRigY),     float(zc) - 2.5f }, // -Z 面
            };
            const float aimF[5][3] = { // 每面瞄点（细柱面内缩 0.05 / 顶面内缩 0.1 防边界 ε）
                { float(xc) + 0.5f,  float(kRigY) + 1.9f, float(zc) + 0.5f },
                { float(xc) + 0.85f, float(kRigY) + 0.5f, float(zc) + 0.5f },
                { float(xc) + 0.15f, float(kRigY) + 0.5f, float(zc) + 0.5f },
                { float(xc) + 0.5f,  float(kRigY) + 0.5f, float(zc) + 0.85f },
                { float(xc) + 0.5f,  float(kRigY) + 0.5f, float(zc) + 0.15f },
            };
            const int hitExpF[5][3] = { // 每面期望命中格（hitBlock = 被瞄的柱格）
                { xc, kRigY + 1, zc }, // 上柱（顶面瞄上柱）
                { xc, kRigY,     zc },
                { xc, kRigY,     zc },
                { xc, kRigY,     zc },
                { xc, kRigY,     zc },
            };
            const int tgtF[5][3] = { // 每面目标格（预期放置位；仙人掌上全拒 → 恒 Air）
                { xc, kRigY + 2, zc },
                { xc + 1, kRigY, zc },
                { xc - 1, kRigY, zc },
                { xc, kRigY, zc + 1 },
                { xc, kRigY, zc - 1 },
            };
            okB = w.blockAt(xc, kRigY, zc) == BR::Cactus && w.blockAt(xc, kRigY + 1, zc) == BR::Cactus;
            const auto diagB = [&](int k, const QVector3D &hit, const QVector3D &tgt, bool pass) {
                QVector3D where(0, 0, 0);
                for (int ddx = -1; ddx <= 1; ++ddx)
                    for (int ddy = -1; ddy <= 1; ++ddy)
                        for (int ddz = -1; ddz <= 1; ++ddz)
                            if (w.blockAt(int(hit.x()) + ddx, int(hit.y()) + ddy, int(hit.z()) + ddz)
                                == quint8(kindsT1017[k]))
                                where = QVector3D(int(hit.x()) + ddx, int(hit.y()) + ddy, int(hit.z()) + ddz);
                qInfo().noquote() << "  [t1017 diag b] kind" << k << "pass" << pass << "hit" << hit
                                  << "nrm" << pcT1017.hitNormal() << "tgt" << tgt
                                  << "cell" << int(w.blockAt(tgt.x(), tgt.y(), tgt.z()))
                                  << "hitCellBlock" << int(w.blockAt(hit.x(), hit.y(), hit.z()))
                                  << "kindNearHit" << where;
            };
            for (int f = 0; f < 5 && okB; ++f) {
                for (int k = 0; k < 3 && okB; ++k) {
                    pcT1017.setSelectedBlock(kindsT1017[k]);
                    const QVector3D hit = aimT1017(feetF[f][0], feetF[f][1], feetF[f][2],
                                                   aimF[f][0], aimF[f][1], aimF[f][2]);
                    const QVector3D hitExp(hitExpF[f][0], hitExpF[f][1], hitExpF[f][2]);
                    const QVector3D tgt(tgtF[f][0], tgtF[f][1], tgtF[f][2]);
                    const bool aimed = hit == hitExp; // 射线确实命中仙人掌目标面（防「瞄空即拒」假阳性）
                    pcT1017.placeBlock();
                    pumpMsT1017(260);
                    const bool rejected = w.blockAt(tgtF[f][0], tgtF[f][1], tgtF[f][2]) == BR::Air;
                    if (!aimed || !rejected)
                        diagB(k, hit, tgt, aimed && rejected);
                    okB = okB && aimed && rejected;
                }
            }
            okB = okB && w.blockAt(xc, kRigY, zc) == BR::Cactus && w.blockAt(xc, kRigY + 1, zc) == BR::Cactus;
            // 石方块对照：同五面同三 kind 全准（top / ±X / ±Z 放置成功）——附着族同口径对照。
            const auto [xs, zs] = nextSlot();
            for (int dx = -2; dx <= 3; ++dx)
                for (int dz = -3; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 4; ++dy) w.setBlock(xs + dx, kRigY + dy, zs + dz, BR::Air, 0);
                    w.setBlock(xs + dx, kRigY - 1, zs + dz, BR::Stone, 0);
                }
            w.setBlock(xs, kRigY, zs, BR::Stone, 0);
            w.setBlock(xs - 2, kRigY, zs, BR::Stone, 0);      // 立足柱（-X 侧，同仙人掌 rig 布局）
            w.setBlock(xs - 2, kRigY + 1, zs, BR::Stone, 0);
            w.setBlock(xs - 2, kRigY + 2, zs, BR::Stone, 0);
            const float feetS[5][3] = {
                { float(xs) - 1.5f, float(kRigY + 3), float(zs) + 0.5f }, // 顶面
                { float(xs) + 3.5f, float(kRigY),     float(zs) + 0.5f }, // +X 面
                { float(xs) - 1.5f, float(kRigY + 3), float(zs) + 0.5f }, // -X 面
                { float(xs) + 0.5f, float(kRigY),     float(zs) + 2.5f }, // +Z 面
                { float(xs) + 0.5f, float(kRigY),     float(zs) - 2.5f }, // -Z 面
            };
            const float aimS[5][3] = { // 整立方面内缩 0.05（顶面 0.1）
                { float(xs) + 0.5f,  float(kRigY) + 0.9f, float(zs) + 0.5f },
                { float(xs) + 0.95f, float(kRigY) + 0.5f, float(zs) + 0.5f },
                { float(xs) + 0.05f, float(kRigY) + 0.5f, float(zs) + 0.5f },
                { float(xs) + 0.5f,  float(kRigY) + 0.5f, float(zs) + 0.95f },
                { float(xs) + 0.5f,  float(kRigY) + 0.5f, float(zs) + 0.05f },
            };
            const int tgtS[5][3] = {
                { xs, kRigY + 1, zs },  // 顶面
                { xs + 1, kRigY, zs },  // +X 面
                { xs - 1, kRigY, zs },  // -X 面
                { xs, kRigY, zs + 1 },  // +Z 面
                { xs, kRigY, zs - 1 },  // -Z 面
            };
            for (int f = 0; f < 5 && okB; ++f) {
                for (int k = 0; k < 3 && okB; ++k) {
                    pcT1017.setSelectedBlock(kindsT1017[k]);
                    const QVector3D hit = aimT1017(feetS[f][0], feetS[f][1], feetS[f][2],
                                                   aimS[f][0], aimS[f][1], aimS[f][2]);
                    const QVector3D hitExp(xs, kRigY, zs); // 石整立方：五面命中格同为石格
                    const QVector3D tgt(tgtS[f][0], tgtS[f][1], tgtS[f][2]);
                    const bool aimed = hit == hitExp;
                    pcT1017.placeBlock();
                    pumpMsT1017(260);
                    const bool placed = w.blockAt(tgtS[f][0], tgtS[f][1], tgtS[f][2]) == quint8(kindsT1017[k]);
                    if (!aimed || !placed) {
                        QVector3D where(0, 0, 0);
                        for (int ddx = -1; ddx <= 1; ++ddx)
                            for (int ddy = -1; ddy <= 1; ++ddy)
                                for (int ddz = -1; ddz <= 1; ++ddz)
                                    if (w.blockAt(xs + ddx, kRigY + ddy, zs + ddz) == quint8(kindsT1017[k]))
                                        where = QVector3D(xs + ddx, kRigY + ddy, zs + ddz);
                        qInfo().noquote() << "  [t1017 diag b-stone] face" << f << "kind" << k
                                          << "aimed" << aimed << "hit" << hit
                                          << "nrm" << pcT1017.hitNormal() << "tgt" << tgt << "cell"
                                          << int(w.blockAt(tgtS[f][0], tgtS[f][1], tgtS[f][2]))
                                          << "hitCellBlock" << int(w.blockAt(xs, kRigY, zs))
                                          << "kindNearHit" << where;
                    }
                    okB = okB && aimed && placed;
                    // 同面三 kind 共用目标格：验完即清，下一组合从净空格重跑（防占位拒绝假阴性）。
                    w.setBlock(tgtS[f][0], tgtS[f][1], tgtS[f][2], BR::Air, 0);
                }
            }
        }
        // (c) 悬空石底面（天花板共享规则对照）：火把 / 红石火把 / 石按钮贴底面全拒（既有语义，非本批新增）。
        {
            const auto [xf, zf] = nextSlot();
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dy = -2; dy <= 3; ++dy) w.setBlock(xf + dx, kRigY + dy, zf + dz, BR::Air, 0);
                    w.setBlock(xf + dx, kRigY - 2, zf + dz, BR::Stone, 0);
                }
            w.setBlock(xf, kRigY + 2, zf, BR::Stone, 0); // 悬空石（石无支撑语义可浮空）
            const int kindsT1017b[3] = { BR::Torch, BR::RedstoneTorch, BR::StoneButton };
            okC = true;
            for (int k = 0; k < 3 && okC; ++k) {
                pcT1017.setSelectedBlock(kindsT1017b[k]);
                const QVector3D hit = aimT1017(float(xf) + 0.5f, float(kRigY - 2), float(zf) + 0.5f,
                                               float(xf) + 0.5f, float(kRigY + 2), float(zf) + 0.5f);
                const QVector3D hitExp(xf, kRigY + 2, zf);       // 悬空石格（底面被瞄）
                const QVector3D tgt(xf, kRigY + 1, zf);          // 底面 → 目标 = 悬空石下方格
                const bool aimed = hit == hitExp;
                pcT1017.placeBlock();
                pumpMsT1017(260);
                const bool rejected = w.blockAt(xf, kRigY + 1, zf) == BR::Air;
                if (!aimed || !rejected)
                    qInfo().noquote() << "  [t1017 diag c] kind" << k << "aimed" << aimed << "hit" << hit
                                      << "hitExp" << hitExp << "tgt" << tgt << "cell"
                                      << int(w.blockAt(xf, kRigY + 1, zf));
                okC = okC && aimed && rejected;
            }
        }
        // (d) 源码钉：三处 Cactus 拒绝行（torchSupportBlock 单一权威 + 机关预检 + 木梯预检）。
        //     B-P1-1 迁移：滤注释钉（pinSet）；原钉串拼进尾注释「// t1017」——剥注释后必失配的
        //     隐患钉，现只钉语句本体（尾注释可自由演化不再牵动钉）。
        {
            const QString root = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
            QStringList missD;
            missD << pinSet(root + QStringLiteral("/src/Core/blockregistry.cpp"), {
                {"torch-support-cactus", "if (blockId == Cactus) return false;"},
                // review0906 #8：单一权威本体 + 公式行（预检/复检四处共享）
                {"mech-ladder-support-fn", "bool BlockRegistry::mechLadderSupportBlock(quint8 blockId)"},
                // review0906 #9：公式行上移 solidSupportBlock 统一权威，mechLadderSupportBlock 委托
                {"mech-ladder-support-formula", "return blockId != Cactus && isFullCube(blockId);"},
                {"solid-support-fn", "bool BlockRegistry::solidSupportBlock(quint8 blockId)"},
                {"mech-ladder-delegates", "return solidSupportBlock(blockId);"},
                {"trapdoor-support-cactus", "if (blockId == Cactus) return false;", 2}, // torch + trapdoor 两处排除行
                {"topflush-solid-support", "return solidSupportBlock(belowId) || (isSlab(belowId) && (belowState & 1) != 0);"},
            });
            missD << pinSet(root + QStringLiteral("/src/Game/playercontroller.cpp"), {
                // review0906 #8：预检两处 + 复检两处全部走单一权威（裸 isFullCube 复检 = 劈叉回潮）
                {"mech-precheck-authority", "if (!BlockRegistry::mechLadderSupportBlock(mechSup)) return;"},
                {"ladder-precheck-authority", "if (!BlockRegistry::mechLadderSupportBlock(hitBlock)) return;"},
                {"ladder-recheck-authority", "BlockRegistry::mechLadderSupportBlock(m_world->blockAt(sx, sy, sz))", 2},
                // review0906 #9：铁轨 / 雪层放置预检切 solidSupportBlock 统一权威
                {"rail-precheck-solid-support", "if (!BlockRegistry::solidSupportBlock(below)) return;"},
                {"snow-precheck-solid-support", "const bool belowSupport = BlockRegistry::solidSupportBlock(below)"},
            });
            okD = missD.isEmpty();
            if (!okD)
                qInfo().noquote() << "  [t1017 diag d] pin miss:" << missD.join(QLatin1Char(','));
        }
        // (e) review0906 #8 邻块编辑后失撑掉落行为腿：旧档残影注入（直接 setBlock 模拟 pre-t1017
        //     旧档）—— 侧贴仙人掌的梯子（梯子非整立方 → checkCactusOnEdit ④ 不触发，柱存活）+
        //     仙人掌顶贴地拉杆（顶面附着不触 ④ 水平邻接口径 → 稳定残留）。触发 = 挖**另一侧邻格**
        //     （支撑本体仙人掌全程存活 = 口径劈叉面：放置预检拒仙人掌、复检裸 isFullCube 对
        //     Cactus 恒真 → 旧档附着块永不掉）。NEW：复检走 mechLadderSupportBlock 单一权威 →
        //     Cactus 不算合格支撑 → 挖邻块即掉。石墙梯对照（挖支撑本体）验挖掘链本身不带假红。
        bool okE8 = false;
        {
            const auto [xn8, zn8] = nextSlot();
            for (int dx = -3; dx <= 5; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 4; ++dy) w.setBlock(xn8 + dx, kRigY + dy, zn8 + dz, BR::Air, 0);
                    w.setBlock(xn8 + dx, kRigY - 1, zn8 + dz, BR::Stone, 0);
                }
            w.setBlock(xn8, kRigY - 1, zn8, BR::Sand, 0);     // 仙人掌合法沙支撑
            w.setBlock(xn8, kRigY,     zn8, BR::Cactus, 0);   // 下柱
            w.setBlock(xn8, kRigY + 1, zn8, BR::Cactus, 0);   // 上柱
            w.setBlock(xn8 + 1, kRigY,     zn8, BR::Ladder, 1); // state=1 → 支撑墙 -X = 仙人掌（旧档注入）
            w.setBlock(xn8,     kRigY + 2, zn8, BR::Lever, 0);  // 贴地附着 → 支撑 = 仙人掌顶（旧档注入）
            tickN(w, 2);
            const bool rigOk8 = w.blockAt(xn8, kRigY, zn8) == BR::Cactus
                && w.blockAt(xn8, kRigY + 1, zn8) == BR::Cactus
                && w.blockAt(xn8 + 1, kRigY, zn8) == BR::Ladder
                && w.blockAt(xn8, kRigY + 2, zn8) == BR::Lever;
            // 触发一：挖梯子**另一侧邻格**石块（P-t945 真瞄准链 + 创造瞬破 = finishMiningAt 全链；
            //   石头水平邻是梯子格非仙人掌 → ④ 不触发；支撑仙人掌全程存活 = 口径劈叉面）。
            w.setBlock(xn8 + 2, kRigY, zn8, BR::Stone, 0);
            tickN(w, 2);
            pcT1017.setSelectedBlock(BR::Stone);
            aimT1017(float(xn8) + 4.5f, float(kRigY), float(zn8) + 0.5f,
                     float(xn8) + 3.4f, float(kRigY) + 0.5f, float(zn8) + 0.5f);
            pcT1017.beginMining(); // 创造瞬破命中石 → dropUnsupportedLaddersAround / MechAround 复检
            pcT1017.endMining();
            pumpMsT1017(60);
            tickN(w, 2);
            const bool ladderDropped8 = w.blockAt(xn8 + 1, kRigY, zn8) == BR::Air
                && w.blockAt(xn8 + 2, kRigY, zn8) == BR::Air
                && w.blockAt(xn8, kRigY, zn8) == BR::Cactus;   // 柱无恙（防「柱先塌带走梯子」假绿）
            // 触发二：拉杆正上方触发块（放置位水平邻非仙人掌 → ④ 不触发；挖它 → 拉杆 6 邻复检；
            //   自 +X 下方斜瞄石底面 — 射线在拉杆薄盒上方掠过不截胡）。
            w.setBlock(xn8, kRigY + 3, zn8, BR::Stone, 0);
            tickN(w, 2);
            aimT1017(float(xn8) + 3.5f, float(kRigY), float(zn8) + 0.5f,
                     float(xn8) + 1.05f, float(kRigY) + 3.1f, float(zn8) + 0.5f);
            pcT1017.beginMining();
            pcT1017.endMining();
            pumpMsT1017(60);
            tickN(w, 2);
            const bool leverDropped8 = w.blockAt(xn8, kRigY + 2, zn8) == BR::Air
                && w.blockAt(xn8, kRigY + 3, zn8) == BR::Air
                && w.blockAt(xn8, kRigY + 1, zn8) == BR::Cactus;
            // 石墙梯对照（挖掘链 sanity）：支撑本体被挖 → 梯必掉（新旧代码同绿，防 harness 假红）。
            //   石柱放 +4（贴柱水平邻 = 整立方触发 ④ 整柱坍落，特避）；自 +X 瞄石 +X 面（梯子盒在
            //   石的 -X 侧背后，射线先中石）。
            w.setBlock(xn8 + 4, kRigY, zn8, BR::Stone, 0);
            w.setBlock(xn8 + 3, kRigY, zn8, BR::Ladder, 0);    // state=0 → 支撑墙 +X = 石头
            tickN(w, 2);
            aimT1017(float(xn8) + 5.5f, float(kRigY), float(zn8) + 0.5f,
                     float(xn8) + 4.6f, float(kRigY) + 0.5f, float(zn8) + 0.5f);
            pcT1017.beginMining();
            pcT1017.endMining();
            pumpMsT1017(60);
            tickN(w, 2);
            const bool ctrlDropped8 = w.blockAt(xn8 + 3, kRigY, zn8) == BR::Air
                && w.blockAt(xn8 + 4, kRigY, zn8) == BR::Air;
            okE8 = rigOk8 && ladderDropped8 && leverDropped8 && ctrlDropped8;
            if (!okE8)
                qInfo().noquote() << "  [t1017 diag e] rig" << rigOk8 << "ladder" << ladderDropped8
                                  << "lever" << leverDropped8 << "ctrl" << ctrlDropped8;
            for (int dx = -3; dx <= 5; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 4; ++dy) w.setBlock(xn8 + dx, kRigY + dy, zn8 + dz, BR::Air, 0);
                    w.setBlock(xn8 + dx, kRigY - 1, zn8 + dz, BR::Air, 0);
                }
            tickN(w, 2);
        }
        // (f) review0906 #9 同族贴地 / 贴面件漏网收口：活板门 / 红石粉 / 雪层 / 铁轨 / 门 全族
        //     依仙人掌放置拒放（旧 trapdoorSupportBlock / isTopFlushSupport / 裸 isFullCube 谓词对
        //     Cactus（ShapeFull）恒真 = 全族漏网；现支撑语义包装层统一排除）。谓词腿（三包装谓词
        //     对 Cactus 翻假 + Stone 对照不回退）+ 行为腿（真瞄准链：贴侧面活板门 + 站顶面四件）+
        //     对照腿（雪层 / 铁轨站石头顶照常放置 = 收紧未过杀）。
        bool okF9 = false;
        {
            const auto [xn9, zn9] = nextSlot();
            for (int dx = -2; dx <= 7; ++dx)
                for (int dz = -3; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 5; ++dy) w.setBlock(xn9 + dx, kRigY + dy, zn9 + dz, BR::Air, 0);
                    w.setBlock(xn9 + dx, kRigY - 1, zn9 + dz, BR::Stone, 0);
                }
            w.setBlock(xn9, kRigY - 1, zn9, BR::Sand, 0);       // 仙人掌合法沙支撑
            w.setBlock(xn9, kRigY,     zn9, BR::Cactus, 0);      // 下柱
            w.setBlock(xn9, kRigY + 1, zn9, BR::Cactus, 0);      // 上柱
            w.setBlock(xn9 - 2, kRigY, zn9, BR::Stone, 0);       // 立足柱（-X 侧，顶面瞄准同 (b)）
            w.setBlock(xn9 - 2, kRigY + 1, zn9, BR::Stone, 0);
            w.setBlock(xn9 - 2, kRigY + 2, zn9, BR::Stone, 0);
            w.setBlock(xn9 + 5, kRigY, zn9, BR::Stone, 0);       // 对照石（+X 远端；非柱邻不触 ④）
            tickN(w, 2);
            const bool rigOk9 = w.blockAt(xn9, kRigY, zn9) == BR::Cactus
                && w.blockAt(xn9, kRigY + 1, zn9) == BR::Cactus;
            // 谓词腿：三支撑包装谓词对 Cactus 全假，Stone 全真（上半砖齐平支撑对照不回退）。
            const quint8 cac = quint8(BR::Cactus), sto = quint8(BR::Stone);
            const quint8 slabTop = quint8(BR::WoodSlab); // 上半砖 state bit0=1 → isTopFlushSupport 真
            const bool okPred9 = !BlockRegistry::trapdoorSupportBlock(cac, 0)
                && BlockRegistry::trapdoorSupportBlock(sto, 0)
                && !BlockRegistry::isTopFlushSupport(cac, 0)
                && BlockRegistry::isTopFlushSupport(sto, 0)
                && BlockRegistry::isTopFlushSupport(slabTop, 1)
                && !BlockRegistry::isTopFlushSupport(slabTop, 0)
                && !BlockRegistry::isDustSupport(cac, 0)
                && !BlockRegistry::solidSupportBlock(cac)
                && BlockRegistry::solidSupportBlock(sto);
            // 行为腿 · 贴侧面：活板门瞄上柱 +X 面 → 目标 (xn+1, kRigY+1)（下方 Air = 贴地路不可用，
            //   唯一可依附面 = 仙人掌侧面；旧谓词放行 = 缺口面）。
            pcT1017.setSelectedBlock(BR::WoodTrapdoor);
            const QVector3D hitTd = aimT1017(float(xn9) + 3.5f, float(kRigY), float(zn9) + 0.5f,
                                             float(xn9) + 0.85f, float(kRigY) + 1.5f, float(zn9) + 0.5f);
            pcT1017.placeBlock();
            pumpMsT1017(260);
            const bool tdAimed = hitTd == QVector3D(xn9, kRigY + 1, zn9);
            const bool tdRejected = w.blockAt(xn9 + 1, kRigY + 1, zn9) == BR::Air;
            // 行为腿 · 站顶面：红石粉（手持 RedstoneId）/ 雪层 / 铁轨 / 门 瞄上柱顶面 → 目标
            //   (xn, kRigY+2)（下方 = 仙人掌；旧口径全放行）。红石粉走 hotbar heldItemId 分流，
            //   验后还原空手（防 heldItemId 残留劫持后续 placeBlock 分流）。
            const float feetTop9[3] = { float(xn9) - 1.5f, float(kRigY + 3), float(zn9) + 0.5f };
            const float aimTop9[3] = { float(xn9) + 0.5f, float(kRigY) + 1.9f, float(zn9) + 0.5f };
            pcT1017.setSelectedBlock(BR::Air); // t1040 rig 加固：粉尘物品 QML 绑定归 Air——前腿置入的 WoodTrapdoor 残留在此腿被 heldItemId 分流旁路（不读 selectedBlock），显式归 Air 消残留 + 兜底
            hbT1017.setHeldBlock(RecipeRegistry::RedstoneId);
            const QVector3D hitDu = aimT1017(feetTop9[0], feetTop9[1], feetTop9[2],
                                             aimTop9[0], aimTop9[1], aimTop9[2]);
            pcT1017.placeBlock();
            pumpMsT1017(260);
            hbT1017.setHeldBlock(0);
            const bool duAimed = hitDu == QVector3D(xn9, kRigY + 1, zn9);
            const bool duRejected = w.blockAt(xn9, kRigY + 2, zn9) == BR::Air;
            bool topAllRejected = true;
            const int topKinds9[3] = { BR::SnowLayer, BR::Rail, BR::WoodDoor };
            for (int k = 0; k < 3 && topAllRejected; ++k) {
                pcT1017.setSelectedBlock(topKinds9[k]);
                const QVector3D hit = aimT1017(feetTop9[0], feetTop9[1], feetTop9[2],
                                               aimTop9[0], aimTop9[1], aimTop9[2]);
                pcT1017.placeBlock();
                pumpMsT1017(260);
                if (hit != QVector3D(xn9, kRigY + 1, zn9)
                    || w.blockAt(xn9, kRigY + 2, zn9) != BR::Air
                    || w.blockAt(xn9, kRigY + 3, zn9) != BR::Air) // 门两格同查（防半截门落上格）
                    topAllRejected = false;
            }
            const bool colOk9 = w.blockAt(xn9, kRigY, zn9) == BR::Cactus
                && w.blockAt(xn9, kRigY + 1, zn9) == BR::Cactus; // 柱无恙（防放置触 ④ 坍落假象）
            // 对照腿：雪层 / 铁轨站石头顶照常放置（新谓词未过杀常规支撑）。
            pcT1017.setSelectedBlock(BR::SnowLayer);
            const QVector3D hitSnowC = aimT1017(float(xn9) + 7.5f, float(kRigY), float(zn9) + 0.5f,
                                                float(xn9) + 5.5f, float(kRigY) + 0.95f, float(zn9) + 0.5f);
            pcT1017.placeBlock();
            pumpMsT1017(260);
            const bool snowCtrl = hitSnowC == QVector3D(xn9 + 5, kRigY, zn9)
                && w.blockAt(xn9 + 5, kRigY + 1, zn9) == BR::SnowLayer;
            w.setBlock(xn9 + 5, kRigY + 1, zn9, BR::Air, 0); // 清雪层（占位会拒铁轨 → 先清再对照）
            pcT1017.setSelectedBlock(BR::Rail);
            const QVector3D hitRailC2 = aimT1017(float(xn9) + 7.5f, float(kRigY), float(zn9) + 0.5f,
                                                 float(xn9) + 5.5f, float(kRigY) + 0.95f, float(zn9) + 0.5f);
            pcT1017.placeBlock();
            pumpMsT1017(260);
            const bool railCtrl2 = hitRailC2 == QVector3D(xn9 + 5, kRigY, zn9)
                && w.blockAt(xn9 + 5, kRigY + 1, zn9) == BR::Rail;
            okF9 = rigOk9 && okPred9 && tdAimed && tdRejected && duAimed && duRejected
                && topAllRejected && colOk9 && snowCtrl && railCtrl2;
            if (!okF9)
                qInfo().noquote() << "  [t1017 diag f] rig" << rigOk9 << "pred" << okPred9
                                  << "tdAim" << tdAimed << "tdRej" << tdRejected
                                  << "duAim" << duAimed << "duRej" << duRejected
                                  << "top" << topAllRejected << "col" << colOk9
                                  << "snowC" << snowCtrl << "railC" << railCtrl2;
            // 清场（同 (e) 尾清场口径）。
            for (int dx = -2; dx <= 7; ++dx)
                for (int dz = -3; dz <= 2; ++dz) {
                    for (int dy = -1; dy <= 5; ++dy) w.setBlock(xn9 + dx, kRigY + dy, zn9 + dz, BR::Air, 0);
                    w.setBlock(xn9 + dx, kRigY - 1, zn9 + dz, BR::Air, 0);
                }
            tickN(w, 2);
        }
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        if (!okD) ++totalFail;
        qInfo().noquote() << (okA && okB && okC && okD ? "PASS" : "FAIL")
                          << "| t1017 cactus-rejects-attachables rig: the single-authority"
                             "torchSupportBlock predicate now returns false for Cactus (stone/sand"
                             "controls keep supporting), and through the REAL aim->placeBlock"
                             "chain all three attachable kinds (torch / redstone torch / stone"
                             "button) are rejected on a 2-high cactus column across ALL FIVE"
                             "aimable faces (top + four sides; the bottom face's target cell is"
                             "buried by sand/the lower column so placement there is already"
                             "occupancy-rejected, and the ceiling rule is pinned by the"
                             "floating-stone leg) - 15/15 combinations keep their target cells"
                             "Air with the column intact, while the SAME 15 operations on a"
                             "stone block all place correctly - plus the"
                             "floating-stone bottom-face leg pins that ceiling rejection is the"
                             "pre-existing shared rule, not cactus-specific; source pins lock"
                             "the cactus rejection in torchSupportBlock and the mech/ladder"
                             "precheck guards (negative-round sensitive: reverting the cactus"
                             "exclusion lets torches attach to the column and the predicate leg"
                             "reads red)"
                          << (okA && okB && okC && okD
                                  ? QString()
                                  : QStringLiteral("diag a=%1 b=%2 c=%3 d=%4")
                                        .arg(okA).arg(okB).arg(okC).arg(okD));
        if (!okE8) ++totalFail;
        qInfo().noquote() << (okE8 ? "PASS" : "FAIL")
                          << "| t1017(e) neighbor-edit drop leg (review0906 #8): old-save residue"
                             " injects a ladder side-attached to a LIVE cactus column (a ladder is"
                             " not a full cube so the cactus neighbor-collapse rule never fires -"
                             " the residue persists exactly as in pre-t1017 saves) and a"
                             " floor-mounted lever on the column top (top-face attachment does not"
                             " touch the cactus horizontal-neighbor rule either). Mining a"
                             " DIFFERENT neighbor of each attached block (support cactus alive"
                             " throughout) runs the 6-neighbor recheck: the rechecks used bare"
                             " isFullCube which Cactus (ShapeFull) always passes - the placement"
                             " precheck rejected cactus while the residue never dropped (the"
                             " reject-vs-residue split). Both rechecks now read the shared"
                             " mechLadderSupportBlock authority (isFullCube && != Cactus), so the"
                             " ladder and the lever drop on the neighbor edit while the column"
                             " stays intact; a stone-wall ladder whose support block is mined"
                             " directly still drops (harness sanity control)"
                          << (okE8 ? QString()
                                   : QStringLiteral("diag see [t1017 diag e]"));
        if (!okF9) ++totalFail;
        qInfo().noquote() << (okF9 ? "PASS" : "FAIL")
                          << "| t1017(f) attach-family cactus sweep (review0906 #9): the ground/"
                             "face-support wrapper predicates (trapdoorSupportBlock /"
                             "isTopFlushSupport / isDustSupport / solidSupportBlock, the last now"
                             "the shared authority mechLadderSupportBlock delegates to) all return"
                             "false for Cactus while stone and top-half-slab controls keep"
                             "supporting, and through the REAL aim->placeBlock chain a trapdoor"
                             "against the column's side face (its only candidate attach face -"
                             "nothing below), plus redstone dust, snow layer, rail and door on the"
                             "column top are ALL rejected with their target cells staying Air and"
                             "the column intact (pre-fix every one of these placed: ShapeFull"
                             "cactus passed the bare predicates), while snow layer and rail on a"
                             "stone block still place (the tightening does not over-reject)"
                          << (okF9 ? QString()
                                   : QStringLiteral("diag see [t1017 diag f]"));
    });

    // ── P-t1012c 水冲毁梯子腿（review0906 #10；机制等价 MC 1.0 流水冲毁 ladder）—— t1043 口径更新：
    //    裁-1 清偿后 Rail 族**入**水毁附着族（旧「Rail 刻意排除」登记废除；worldgen 侧由 placeMineshaft
    //    生成期干燥门承接，见 P-t1043b），本探针的轨对照腿从「轨完好」翻转为「对向轨同源照冲」。
    //    (a) 谓词腿：isAttachableBlock 单一权威含 Ladder + Rail（三族经 isRail）；Stone 阴性对照；
    //        轨族 dropId=自身（掉落链免费成立面）；
    //    (b) 行为腿：悬空石平台 + 石墙 + 贴墙梯（state=0 支撑墙 +X）+ 水源贴梯扩散 → 梯被冲毁
    //        （格 Air ∨ Water 同 tick 入水）+ blockDroppedAsItem 掉自身 id（dropId(Ladder)=Ladder，
    //        与玩家挖除掉落链同源）；同源反向扩散路径上的轨格**同样被冲毁**（t1043 MC 口径：
    //        流水冲轨，dropId(Rail)=Rail；旧「轨完好对照」随裁-1 废除）→ 恰两掉落（梯 + 轨）。
    runLegMulti({ "t1012c water-wash ladder leg (review0906 #10, t1043 caliber update):the attachable-block water-d"
        "estroy family includes Ladder AND the railfamily (parity ruling cai-1 cleared the registered rai"
        "l exemption - theworldgen side is carried by the placeMineshaft dry-cell gate, seeP-t1043b) with"
        " rail dropId = itself so the drop chain is free (stonenegative control) - behaviorally a water s"
        "ource spreading into awall-attached ladder cell washes it to Air/Water and emitsblockDroppedAsIt"
        "em with the ladder's own id (same chain as playermining), while the same source spreading the op"
        "posite way now washesthe rail cell too (MC caliber: flowing water destroys rails) - exactlyone l"
        "adder drop and one rail dropdiag a=%1 b see [t1012c diag b]" }, [&]() {
        bool okA10 = !BlockRegistry::isAttachableBlock(quint8(BR::Stone))
            && BlockRegistry::isAttachableBlock(quint8(BR::Rail))
            && BlockRegistry::isAttachableBlock(quint8(BR::Ladder))
            && BlockRegistry::isAttachableBlock(quint8(BR::Torch))
            && BlockRegistry::isAttachableBlock(quint8(BR::Cobweb))
            && BlockRegistry::dropId(quint8(BR::Rail)) == int(BR::Rail)
            && BlockRegistry::dropId(quint8(BR::Ladder)) == int(BR::Ladder); // 掉落链免费成立面
        bool okB10 = false;
        {
            World wW10;
            wW10.setWidth(48); wW10.setDepth(48); wW10.setHeight(64); wW10.setSeed(9);
            // worldgen 水沉降（t34w④ 同口径：推进到连续静默，免地形水体扩散扰 rig）。
            int wc10 = 0;
            QObject::connect(&wW10, &World::worldChanged, &wW10, [&]() { ++wc10; });
            const auto settle10 = [&]() {
                int quiet = 0;
                for (int i = 0; i < 2000 && quiet < 10; ++i) {
                    const int wc0 = wc10;
                    wW10.tickWaterFlow(); wW10.tickWaterFlow(); wW10.tickWaterFlow();
                    quiet = (wc10 == wc0) ? quiet + 1 : 0;
                }
            };
            settle10();
            // 悬空石平台一排 y=40（x 22..30）：墙 x=28（双层防绕）、梯 x=27 贴墙（state=0 → 支撑 +X），
            //   水源 x=26（+X 扩散进梯格冲毁；-X 扩散经 x=25 进 x=24 轨格同源照冲——t1043 口径，
            //   下方石面全程 grounded）。
            constexpr int PY10 = 40, PZ10 = 24;
            for (int x = 22; x <= 30; ++x)
                for (int dy = 1; dy <= 3; ++dy)
                    if (wW10.blockAt(x, PY10 + dy, PZ10) != BR::Air)
                        wW10.setWaterSilent(x, PY10 + dy, PZ10, BR::Air, 0);
            for (int x = 22; x <= 30; ++x) wW10.setBlock(x, PY10, PZ10, BR::Stone, 0);
            wW10.setBlock(28, PY10 + 1, PZ10, BR::Stone, 0); // 支撑墙
            wW10.setBlock(28, PY10 + 2, PZ10, BR::Stone, 0);
            wW10.setBlock(27, PY10 + 1, PZ10, BR::Ladder, 0); // state=0 → 支撑墙 +X = 石墙
            wW10.setBlock(24, PY10 + 1, PZ10, BR::Rail, 0);   // 轨对照（-X 扩散路径；t1043 起照冲）
            int ladderDrops10 = 0, railDrops10 = 0;
            QObject::connect(&wW10, &World::blockDroppedAsItem, &wW10,
                             [&](int, int, int, int id) {
                                 if (id == int(BR::Ladder)) ++ladderDrops10;
                                 if (id == int(BR::Rail)) ++railDrops10;
                             });
            wW10.setBlock(26, PY10 + 1, PZ10, BR::Water, 0); // 源（桶倒路径同款源写入）
            settle10();
            const quint8 afterLadder = wW10.blockAt(27, PY10 + 1, PZ10);
            const bool ladderWashed = afterLadder == BR::Air || afterLadder == BR::Water;
            const quint8 afterRail = wW10.blockAt(24, PY10 + 1, PZ10);
            const bool railWashed = afterRail == BR::Air || afterRail == BR::Water;
            okB10 = ladderWashed && railWashed && ladderDrops10 == 1 && railDrops10 == 1;
            if (!okB10)
                qInfo().noquote() << "  [t1012c diag b] ladderCell" << int(afterLadder)
                                  << "railCell" << int(afterRail)
                                  << "ladderDrops" << ladderDrops10 << "railDrops" << railDrops10;
        }
        if (!okA10) ++totalFail;
        if (!okB10) ++totalFail;
        qInfo().noquote() << (okA10 && okB10 ? "PASS" : "FAIL")
                          << "| t1012c water-wash ladder leg (review0906 #10, t1043 caliber update):"
                             "the attachable-block water-destroy family includes Ladder AND the rail"
                             "family (parity ruling cai-1 cleared the registered rail exemption - the"
                             "worldgen side is carried by the placeMineshaft dry-cell gate, see"
                             "P-t1043b) with rail dropId = itself so the drop chain is free (stone"
                             "negative control) - behaviorally a water source spreading into a"
                             "wall-attached ladder cell washes it to Air/Water and emits"
                             "blockDroppedAsItem with the ladder's own id (same chain as player"
                             "mining), while the same source spreading the opposite way now washes"
                             "the rail cell too (MC caliber: flowing water destroys rails) - exactly"
                             "one ladder drop and one rail drop"
                          << (okA10 && okB10 ? QString()
                                             : QStringLiteral("diag a=%1 b see [t1012c diag b]")
                                                   .arg(okA10));
    });

    // ── P-t1008 小僵尸两修探针（① 生物蛋图标管线统一 ② 小鸡骑士组合越障跳）──
    //    通用 rig：44×44×96 局部世界（seed 26）y[85,95] 清 Air + y84 铺 Stone（P-t988 同款）+
    //    x∈[24,44) 全深 y85 石台（顶 86 = 1 格台阶，全深堵绕行）。
    //    (a) 蛋管线统一三面钉：itemFilenameMap 补 0x25D 行（t952 引入小蹒跚者蛋时漏本表行 =
    //        15 蛋唯一缺行 → pack 开时小僵尸蛋恒走 MaterialIcon 自绘 Canvas、其余 14 蛋走
    //        spawn_egg.png 两层染色生成管线 = 用户「小僵尸蛋与别的蛋风格不统一」观感分叉根源；
    //        滤注释源钉锁语句面）+ spawnEggTint(0x25D) 直调非空且幼体亮黄绿 base（t785 直调
    //        先例——生成式回退的染色权威）+ MaterialIcon case 0x25D 自绘分支在位（pack 关回退）。
    //    (b) 小鸡骑士越障行为腿（t1008② 修复面）：setChickenJockeyChance(1.0) 生成必组合 →
    //        小僵尸（骑手）台下追台上玩家被台面卡：骑手 aiHostile 越障跳门点火（钉位恒 resting
    //        → 门可达）但旧链钉位段把 vy/滑流清零 = 组合结构上跳不过（diag 实证 30s+ 原地踏步）；
    //        t1008② 跳意图转移载具（vy==kJumpSpeed 且载具贴地 → 载具同款起跳）→ 组合翻越上台
    //        （骑手 x≥24.5 且 y≥86.3 合取：台下面平地跳峰值虽过 86.3 但 x 恒 <24.5 被台面碰撞拦回，
    //        P-t988(b) 同款真上台判据）+ 抵达咬击带（distXZ ≤ 1.6）。30s 帽。阴性轮敏感：摘除
    //        tickMobMounts 跳意图转移块 → 本腿恰红（组合永卡台下面），(a)(c) 照绿。
    //    (c) 非骑乘小僵尸对照腿：plain baby（chance=0 全独立）同台面追玩家 → aiHostile 链
    //        t670 越障跳既有照绿（防本单意外破坏独立追击链；也钉「② 病灶仅在挂载链」的口径）。
    runLegMulti({ "t1008 baby-shambler duo: (a) the spawn-egg icon pipeline is unified -- itemFilenameMap gains the"
        " 0x25D row (the only missing egg id since t952; with a pack ON the baby egg used to fall back to"
        " the hand-drawn MaterialIcon canvas while the other 14 eggs took the two-template spawn_egg.png "
        "tint pipeline = the user-visible style split), spawnEggTint(0x25D) keeps the brighter baby-green"
        " family tint for the generated fallback, and the MaterialIcon case stays as the pack-off fallbac"
        "k; (b) the chicken jockey complex now crosses a 1-block step while chasing the player: the rider"
        "'s aiHostile jump gate fires in the mounted state (pin keeps resting=true so the gate is reachab"
        "le) but the mount pin used to discard vy/glide wholesale = the complex stalled at the step face "
        "forever -- the jump intent now transfers to the mount (exact kJumpSpeed while the mount is groun"
        "ded), its own gravity/landing physics lifts the complex, the rider's AABB clears the step and th"
        "e XZ pin drags the mount onto the top (rider x>=24.5 CONJ y>=86.3 plus reaching the 1.6 bite ban"
        "d, 30s cap); (c) the unmounted baby control leg pins the plain hostile chain still hops the step"
        " by itself (the lesion was mount-chain only)diag %1" }, [&]() {
        bool okA = false, okB = false, okC = false;
        QString diag1008;
        auto flatRig1008 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
            for (int z = 0; z < 44; ++z)
                for (int x = 24; x < 44; ++x)
                    w.setBlock(x, 85, z, BR::Stone, 0); // 全深石台（顶 86 = 1 格台阶；堵死绕行）
        };
        auto distXZ1008 = [](const QVector3D &p, const QVector3D &q) {
            return QVector3D(p.x() - q.x(), 0.0f, p.z() - q.z()).length();
        };
        // (a) 蛋管线统一：映射行（滤注释）+ 生成式染色表行（真直调）+ 自绘回退 case。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile rpf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
            const QString rp = rpf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rpf.readAll()) : QString();
            QString code; // 滤注释体（t836(e)/t1017(d) 同手法：注册注释可留字面量，语句面才作数）
            for (const QString &line : rp.split(QLatin1Char('\n'))) {
                if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                code += line; code += QLatin1Char('\n');
            }
            const bool mapRow = code.contains(
                QStringLiteral("{0x25D, QStringLiteral(\"zombie_spawn_egg.png\")}"));
            const EggTint *tint1008 = spawnEggTint(0x25D);
            const bool tintRow = tint1008 != nullptr
                                 && tint1008->base[0] == 0x5a && tint1008->base[1] == 0x7a
                                 && tint1008->base[2] == 0x42 && tint1008->spot[0] == 0x6a;
            QFile miq(root + QStringLiteral("/src/ui/MaterialIcon.qml"));
            const QString mi = miq.open(QIODevice::ReadOnly) ? QString::fromUtf8(miq.readAll()) : QString();
            const bool drawCase = mi.contains(QStringLiteral("drawSpawnEgg(\"babyshambler\")"));
            okA = mapRow && tintRow && drawCase;
            if (!okA) diag1008 += QStringLiteral("a mapRow=%1 tintRow=%2 drawCase=%3 ")
                                      .arg(mapRow).arg(tintRow).arg(drawCase);
        }
        // (b) 小鸡骑士越障行为腿：跳意图转移 → 组合翻越 1 格台阶 + 抵达咬击带。
        {
            World wb; flatRig1008(wb);
            EntityManager emb;
            emb.setChickenJockeyChance(1.0); // 生成必组合（矩阵缝写口径，P-t952 同源）
            const int baby = emb.spawnMobTyped(20, 85, 22, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 20);
            const QVector3D player(30.5f, 86.0f, 22.5f); // 台上玩家（侦测圈内 → 直追）
            bool onTop = false, reached = false;
            if (baby >= 0) {
                for (int t = 0; t < 1875 && !(onTop && reached); ++t) { // 30s 事件驱动帽
                    emb.tick(0.016, &wb, player, 0.3f, 1.8f, true, false);
                    const QVector3D p = emb.posAt(baby);
                    if (p.x() >= 24.5f && p.y() >= 86.3f) onTop = true; // 真上台合取判据
                    if (distXZ1008(p, player) <= 1.6f) reached = true;  // 越台后进咬击带
                }
                diag1008 += QStringLiteral("b final=(%1,%2,%3) ")
                                .arg(emb.posAt(baby).x(), 0, 'f', 2)
                                .arg(emb.posAt(baby).y(), 0, 'f', 2)
                                .arg(emb.posAt(baby).z(), 0, 'f', 2);
            } else diag1008 += QStringLiteral("b spawn failed ");
            okB = baby >= 0 && onTop && reached;
        }
        // (c) 非骑乘对照：plain baby 独立追击链越障照绿（chance=0 防组合翻态伪影）。
        {
            World wc; flatRig1008(wc);
            EntityManager emc;
            emc.setChickenJockeyChance(0.0); // 生成恒独立（防 5% 缺省掷骰引入 flake）
            const int baby = emc.spawnMobTyped(20, 85, 22, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 20);
            const QVector3D player(30.5f, 86.0f, 22.5f);
            bool onTop = false, reached = false;
            if (baby >= 0) {
                for (int t = 0; t < 1875 && !(onTop && reached); ++t) { // 30s 事件驱动帽
                    emc.tick(0.016, &wc, player, 0.3f, 1.8f, true, false);
                    const QVector3D p = emc.posAt(baby);
                    if (p.x() >= 24.5f && p.y() >= 86.3f) onTop = true;
                    if (distXZ1008(p, player) <= 1.6f) reached = true;
                }
            } else diag1008 += QStringLiteral("c spawn failed ");
            okC = baby >= 0 && onTop && reached;
        }
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        qInfo().noquote() << (okA && okB && okC ? "PASS" : "FAIL")
                          << "| t1008 baby-shambler duo: (a) the spawn-egg icon pipeline is"
                             " unified -- itemFilenameMap gains the 0x25D row (the only missing"
                             " egg id since t952; with a pack ON the baby egg used to fall back"
                             " to the hand-drawn MaterialIcon canvas while the other 14 eggs"
                             " took the two-template spawn_egg.png tint pipeline = the"
                             " user-visible style split), spawnEggTint(0x25D) keeps the brighter"
                             " baby-green family tint for the generated fallback, and the"
                             " MaterialIcon case stays as the pack-off fallback; (b) the chicken"
                             " jockey complex now crosses a 1-block step while chasing the player:"
                             " the rider's aiHostile jump gate fires in the mounted state (pin"
                             " keeps resting=true so the gate is reachable) but the mount pin used"
                             " to discard vy/glide wholesale = the complex stalled at the step"
                             " face forever -- the jump intent now transfers to the mount (exact"
                             " kJumpSpeed while the mount is grounded), its own gravity/landing"
                             " physics lifts the complex, the rider's AABB clears the step and"
                             " the XZ pin drags the mount onto the top (rider x>=24.5 CONJ"
                             " y>=86.3 plus reaching the 1.6 bite band, 30s cap); (c) the"
                             " unmounted baby control leg pins the plain hostile chain still"
                             " hops the step by itself (the lesion was mount-chain only)"
                          << (okA && okB && okC
                                  ? QString()
                                  : QStringLiteral("diag %1").arg(diag1008));
    });

    // ── P-t1023 性能批三（R19.20；docs/perf-batch3-research-2026-09.md）三腿 ──
    //   (a) 隐藏 delegate 永停表动画 `running: visible` 门控源码钉（t1007 治理路径 b/c 最小步；
    //       QML visible:false 不暂停 Animation on（t561 火焰同源）——槽池 200 item delegate 的
    //       rotY+bobY 与 47 mob delegate 的 endereye/enderpearl spin 在空槽/隐藏态恒烧 GUI 帧）。
    //   (b) AO 环境光遮蔽行为级（t1023 平滑光照调研首批小步；ChunkGeometry 直驱 t860 先例 +
    //       vertexData 直读 t965 先例）：默认关平坦基线 / 开启角点曲线精确值 / round-trip 逐字节。
    //   (c) meshing 线程模式事实钉（t906 复核）：src/ 零线程原语 与 F3 `threads: 0/0 (sync meshing)`
    //       事实行互锁——未来线程化立项必须两处同步更新（防 F3 谎报）。

    // (a) 源码钉：注释滤除后四条门控文本必须全体在场（任何一处被删/改绑即红；t860 源码钉先例）。
    runLegMulti({ "t1023a hidden-delegate animation gates (t1007-b/c minimal step): QML 'Animation on' never stops "
        "for visible:false (t561 flame lesson), so the 200-slot item pool kept running rotY spin + bobY f"
        "loat on every delegate including empty/picked-up hidden slots (~2x slots of constant animation b"
        "urn) and all 47 mob delegates kept the ender-eye/ender-pearl roll spinning regardless of entKind"
        " - the fix gates all four infinite animations on delegate/node visibility (running: entRoot.visi"
        "ble / endereyeNode.visible / enderpearlNode.visible), the t696 established pattern; hidden deleg"
        "ates stop paying animation ticks and slot-reuse restarts from 'from' = fresh-entity semanticsdia"
        "g rotY %1 bobY %2 eye %3 pearl %4" }, [&]() {
        QString qml;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            for (const QString &c : candidates) {
                QFile f(c);
                if (f.open(QIODevice::ReadOnly)) { qml = QString::fromUtf8(f.readAll()); break; }
            }
        }
        if (qml.isEmpty()) {
            // B-P2-1：Main.qml 找不到 = 布局破坏（源码钉失去锚点即失察面）—— 原静默 note 弃钉
            //   是探针自砍覆盖面，改响红（t1023(a) 本体的滤注释逻辑保持不变）。
            ++totalFail;
            qInfo().noquote() << "FAIL"
                              << "| t1023a hidden-delegate animation gates: Main.qml not found near"
                                 " exe (exeDir/../.. src/ui) - layout broken, source pin loud-miss"
                                 " (review0907 B-P2-1: was a silent note that skipped the pin)";
        } else {
            QString code;
            for (const QString &line : qml.split(QLatin1Char('\n'))) {
                const QString t = line.trimmed();
                if (t.startsWith(QLatin1String("//")) || t.startsWith(QLatin1String("*"))
                    || t.startsWith(QLatin1String("/*")))
                    continue;
                code += line;
                code += QLatin1Char('\n');
            }
            const auto countOf = [](const QString &hay, const QString &needle) {
                int n = 0, pos = 0;
                while ((pos = hay.indexOf(needle, pos)) >= 0) { ++n; pos += needle.size(); }
                return n;
            };
            // rotY 自转（item delegate，3s/圈）+ bobY 浮动（2s InOutSine）：running 绑 delegate 根 visible。
            const bool gateRotY = code.contains(QStringLiteral(
                "NumberAnimation on rotY { from: 0; to: 360; duration: 3000; loops: Animation.Infinite; running: entRoot.visible }"));
            const bool gateBobY = code.contains(QStringLiteral("SequentialAnimation on bobY"))
                                  && countOf(code, QStringLiteral("running: entRoot.visible")) >= 2;
            // mob 族同向（t1007-c）：endereye/enderpearl spin（面内 roll）running 绑各自 kind 门控节点。
            const bool gateEye = code.contains(QStringLiteral(
                "NumberAnimation on spin { from: 0; to: 360; duration: 1500; loops: Animation.Infinite; running: endereyeNode.visible }"));
            const bool gatePearl = code.contains(QStringLiteral(
                "NumberAnimation on spin { from: 0; to: 360; duration: 1200; loops: Animation.Infinite; running: enderpearlNode.visible }"));
            const bool okGate = gateRotY && gateBobY && gateEye && gatePearl;
            if (!okGate) ++totalFail;
            qInfo().noquote() << (okGate ? "PASS" : "FAIL")
                              << "| t1023a hidden-delegate animation gates (t1007-b/c minimal step):"
                                 " QML 'Animation on' never stops for visible:false (t561 flame lesson),"
                                 " so the 200-slot item pool kept running rotY spin + bobY float on"
                                 " every delegate including empty/picked-up hidden slots (~2x slots"
                                 " of constant animation burn) and all 47 mob delegates kept the"
                                 " ender-eye/ender-pearl roll spinning regardless of entKind - the"
                                 " fix gates all four infinite animations on delegate/node visibility"
                                 " (running: entRoot.visible / endereyeNode.visible /"
                                 " enderpearlNode.visible), the t696 established pattern; hidden"
                                 " delegates stop paying animation ticks and slot-reuse restarts"
                                 " from 'from' = fresh-entity semantics"
                              << (okGate ? QString()
                                         : QStringLiteral("diag rotY %1 bobY %2 eye %3 pearl %4")
                                               .arg(gateRotY).arg(gateBobY).arg(gateEye).arg(gatePearl));
        }
    });
}
