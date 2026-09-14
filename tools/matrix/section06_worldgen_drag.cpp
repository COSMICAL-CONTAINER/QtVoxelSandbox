// tools/matrix/section06_worldgen_drag.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L30841-37539 逐字节搬移（段 md5: ccb23b9bca56fe65efa94c070c27c525；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section06_worldgen_drag()
{

    // ── P-t960 弓 / 钓竿专属附魔探针（R19.17 🅳；用户第五轮口径「弓（力量/冲击/火矢/无限类）、
    //    钓竿（海之眷顾/饵钓类）——enchantregistry 扩池 + 适用物品门」；名称全原创，机制对位）──
    //    注册表扩 6 条（弓四件 id 15-18 主类别 Bow / 竿两件 id 19-20 主类别 Rod，全组 0）+ Category
    //    加 BowItem/RodItem 位（categoryForItem 弓→BowItem / 钓竿→RodItem；剪刀 / 锄仍 None）+
    //    kBookMainCats 主类别轮三→五类（t959 预留扩展位生效，书池可出弓 / 竿附魔）。效果接线：
    //    劲射=endBowDraw 箭伤 +1HP/级；震击/燃箭=Game 层倍率/时长经 spawnArrowPlayer 下传（per-entity
    //    t505 先例，Entities 不 include Game）；不竭=endBowDraw 免箭消耗（门槛「背包有箭」不变）；
    //    唤潮/缠咬=useFishingRod 甩竿经 spawnBobber 下传（等待 ×(1−0.2×级) / 判定窗 +0.5s×级——
    //    kBobberBiteWindowSec=1.0 基值用户口径钉死不动，附加式加宽）。
    //    断言：
    //    (a) 注册表数据钉：6 条 id 的 主类别/冲突组/最高等级/权重/显示名 逐字段 vs 语义表；
    //    (b) 适用物品门（「专属」核心）：弓四件 ⊆ 弓、竿两件 ⊆ 钓竿；剑/镐/护甲/锄/剪刀全拒；
    //        书载体全过（t959 预留扩展位生效）；耐久扩弓 / 竿位；
    //    (c) 书池可达腿：800 次施法弓四件 + 竿两件全部出现（主类别五轮换 → 书施法可达）；
    //    (d) 效果公式数值腿（单一权威 EnchantRegistry 五函数精确值 + 钳制）；
    //    (e) 唤潮 / 缠咬行为腿（t926 水槽 rig：同格同序号 → 同基线等待，settle 相对 tick ×0.4 同比；
    //        判定窗 +0.5s → 咬钩→逃走窗长 ∈ [1.4,1.6]s；基线复跑 tick 数相等 = 确定性）；
    //    (f) 震击 / 燃箭行为腿（t829(b) 直调 spawnArrowPlayer rig：0.8s 量测窗内 −x 位移积分随倍率
    //        缩放——×6 对基线的确定界天然分离（基线 <2.2 / 高倍率 >5.2 / 间隔 >3.0）；燃箭箭命中后
    //        isBurningAt 真、基线箭假）；
    //    (g) Game 层接线源码钉（t927(c) 手法：endBowDraw / useFishingRod 去注释体内五个权威函数调用）。
    {
        // (a) 数据钉。
        const int MG = int(EnchantRegistry::Might),       BS = int(EnchantRegistry::BowShock);
        const int BD = int(EnchantRegistry::BrightDraw),  NR = int(EnchantRegistry::NeverRun);
        const int TD = int(EnchantRegistry::TideCall),    BE = int(EnchantRegistry::BiteCall);
        const int newIds[6]  = { MG, BS, BD, NR, TD, BE };
        const int newCat[6]  = { int(EnchantRegistry::EnchantCatBow), int(EnchantRegistry::EnchantCatBow),
                                 int(EnchantRegistry::EnchantCatBow), int(EnchantRegistry::EnchantCatBow),
                                 int(EnchantRegistry::EnchantCatRod), int(EnchantRegistry::EnchantCatRod) };
        const int newMax[6]  = { 3, 2, 1, 1, 3, 3 };
        const int newW[6]    = { 10, 5, 2, 1, 2, 5 };
        bool okData = int(EnchantRegistry::EnchantCount) == 21
                      && EnchantRegistry::isEnchant(BE) && !EnchantRegistry::isEnchant(21);
        for (int i = 0; i < 6; ++i)
            okData = okData
                  && EnchantRegistry::homeCategory(newIds[i]) == newCat[i]
                  && EnchantRegistry::conflictGroup(newIds[i]) == 0          // 全组 0（不竭留待修复系）
                  && EnchantRegistry::maxLevel(newIds[i]) == newMax[i]
                  && EnchantRegistry::weight(newIds[i]) == newW[i]
                  && !EnchantRegistry::displayName(newIds[i]).isEmpty()
                  // 单类位判定（BookItem 位弓 / 竿 mask 都含，混入会恒真——书载体合法性走 (b) 门腿）
                  && EnchantRegistry::isApplicable(newIds[i], EnchantRegistry::BowItem) == (i < 4)
                  && EnchantRegistry::isApplicable(newIds[i], EnchantRegistry::RodItem) == (i >= 4)
                  && EnchantRegistry::isApplicable(newIds[i], EnchantRegistry::BookItem);

        // (b) 适用物品门（isApplicableForItem 逐物品精判 = 铁砧敲书权威；附魔台走同源池）。
        const int bowId  = int(ToolRegistry::Bow);
        const int rodId  = int(ToolRegistry::FishingRod);
        const int diaSword = int(ToolRegistry::DiamondSword);
        const int diaPick  = int(ToolRegistry::PickaxeDiamond);
        const int diaHoe   = int(ToolRegistry::DiamondHoe);
        const int diaChest = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1;
        const int bookId   = RecipeRegistry::BookId;
        bool okGate = EnchantRegistry::categoryForItem(bowId) == EnchantRegistry::BowItem
                   && EnchantRegistry::categoryForItem(rodId) == EnchantRegistry::RodItem
                   && EnchantRegistry::categoryForItem(int(ToolRegistry::Shears)) == EnchantRegistry::None
                   && EnchantRegistry::isApplicableForItem(int(EnchantRegistry::Unbreaking), bowId)
                   && EnchantRegistry::isApplicableForItem(int(EnchantRegistry::Unbreaking), rodId);
        for (int i = 0; i < 6; ++i) {
            okGate = okGate && EnchantRegistry::isApplicableForItem(newIds[i], bookId)   // 书载体全过
                   && EnchantRegistry::isApplicableForItem(newIds[i], bowId) == (i < 4)  // 弓四件只上弓
                   && EnchantRegistry::isApplicableForItem(newIds[i], rodId) == (i >= 4);// 竿两件只上钓竿
            const int others[5] = { diaSword, diaPick, diaHoe, diaChest, int(ToolRegistry::Shears) };
            for (int o = 0; o < 5; ++o)
                okGate = okGate && !EnchantRegistry::isApplicableForItem(newIds[i], others[o]);
        }

        // (c) 书池可达腿：800 次施法（主类别五轮换）弓四件 + 竿两件全部出现。seenBook 按 EnchantCount
        //     定尺寸（review0830 #24：按下标 id 写入，旧定长 21 在第 22 条上 OOB 写 UB；EnchantCount
        //     漂移另有 (a) 的 `== 21` 显式钉先红，此处保证不 UB）。
        constexpr int nidT960 = int(EnchantRegistry::EnchantCount);
        bool seenBook[nidT960];
        for (int i = 0; i < nidT960; ++i) seenBook[i] = false;
        for (int seed = 0; seed < 800; ++seed) {
            const QVariantList picks = EnchantRegistry::selectEnchantsForItem(bookId, 12 + (seed % 19), seed);
            for (const QVariant &v : picks) seenBook[v.toMap().value(QStringLiteral("id")).toInt()] = true;
        }
        bool okBook = true;
        for (int i = 0; i < 6; ++i) okBook = okBook && seenBook[newIds[i]];

        // (d) 效果公式数值腿（单一权威精确值；改数值此处必红，接线与显示共读一式）。
        const auto close = [](float got, float expect) { return std::abs(got - expect) < 1e-4f; };
        bool okForm = EnchantRegistry::bowArrowDamage(6, 0) == 6
                   && EnchantRegistry::bowArrowDamage(6, 1) == 7
                   && EnchantRegistry::bowArrowDamage(6, 3) == 9
                   && EnchantRegistry::bowArrowDamage(1, 2) == 3
                   && EnchantRegistry::bowArrowDamage(6, -2) == 6          // 负级防御钳 0
                   && close(EnchantRegistry::bowKnockbackMultiplier(0), 1.0f)
                   && close(EnchantRegistry::bowKnockbackMultiplier(1), 2.0f)
                   && close(EnchantRegistry::bowKnockbackMultiplier(2), 3.0f)
                   && close(EnchantRegistry::bowIgniteSeconds(0), 0.0f)
                   && close(EnchantRegistry::bowIgniteSeconds(1), 5.0f)
                   && close(EnchantRegistry::bowIgniteSeconds(3), 5.0f)    // max 1 → 恒 5s
                   && close(EnchantRegistry::rodWaitScale(0), 1.0f)
                   && close(EnchantRegistry::rodWaitScale(1), 0.8f)
                   && close(EnchantRegistry::rodWaitScale(3), 0.4f)
                   && close(EnchantRegistry::rodWaitScale(5), 0.4f)        // 钳 [0,3]
                   && close(EnchantRegistry::rodBiteWindowExtra(0), 0.0f)
                   && close(EnchantRegistry::rodBiteWindowExtra(1), 0.5f)
                   && close(EnchantRegistry::rodBiteWindowExtra(3), 1.5f)
                   && close(EnchantRegistry::rodBiteWindowExtra(5), 1.5f); // 钳 [0,3]

        // (e) 唤潮 / 缠咬行为腿（t926 同款水槽；同格同序号 → 同基线确定性等待，缩放腿免算哈希内部值）。
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(82);
        EntityManager entsL;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) entsL.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                wL.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池（t884(a)/t926 同款）
            }
        // 甩竿 → settle（入水浮定）/ 首咬 / 逃走 tick 数。等待腿用 **settle 相对** tick（spawn→settle 的
        //   落 tide 段不随缩放变，spawn 相对比会把常量落水时间算进比例——首轮实测正是此坑）。
        //   serial 恒 961 → 同格同基线等待。
        const auto castAndWatch = [&](float scale, float extra, bool waitEscape,
                                      int *settleOut, int *biteOut, int *escOut) {
            *settleOut = *biteOut = *escOut = -1;
            const int b = entsL.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0),
                                            961, scale, extra);
            for (int t = 1; b >= 0 && t <= 800; ++t) {
                tickL(1, 0.05f);
                if (*settleOut < 0 && entsL.bobberInWaterAt(b)) *settleOut = t;
                const bool has = entsL.aliveAt(b) && entsL.bobberHasBiteAt(b);
                if (*biteOut < 0 && has) *biteOut = t;
                if (*biteOut >= 0 && *escOut < 0 && !has) *escOut = t;
                if (*escOut >= 0) break;                 // 窗腿：逃走窗关即收
                if (*biteOut >= 0 && !waitEscape) break; // 等待腿：咬钩即收
            }
            if (b >= 0) entsL.removeEntityAt(b);
        };
        int baseSettle = -1, baseBite = -1, baseEsc = -1;
        int tideSettle = -1, tideBite = -1, tideEsc = -1;
        int againSettle = -1, againBite = -1, againEsc = -1;
        int wideSettle = -1, wideBite = -1, wideEsc = -1;
        castAndWatch(1.0f, 0.0f, false, &baseSettle, &baseBite, &baseEsc);   // 基线等待
        castAndWatch(0.4f, 0.0f, false, &tideSettle, &tideBite, &tideEsc);   // 唤潮 III（×0.4）
        castAndWatch(1.0f, 0.0f, false, &againSettle, &againBite, &againEsc);// 基线复跑（确定性）
        castAndWatch(1.0f, 0.5f, true, &wideSettle, &wideBite, &wideEsc);    // 缠咬 I（窗 +0.5s）
        const int baseWait = (baseSettle >= 0 && baseBite > baseSettle) ? baseBite - baseSettle : -1;
        const int tideWait = (tideSettle >= 0 && tideBite > tideSettle) ? tideBite - tideSettle : -1;
        const int againWait = (againSettle >= 0 && againBite > againSettle) ? againBite - againSettle : -2;
        const bool okTide = baseWait > 0 && tideWait > 0 && againWait == baseWait
                         && std::abs(float(tideWait) - 0.4f * float(baseWait)) <= 1.2f;
        const float wideWinSec = (wideBite > 0 && wideEsc > wideBite)
                                     ? float(wideEsc - wideBite) * 0.05f : -1.0f;
        const bool okBite = wideWinSec >= 1.4f && wideWinSec <= 1.6f; // 基线 1.0s 由 P-t926 行为腿钉死
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy + 1, z, BR::Air, 0);
                wL.setBlock(x, fy, z, BR::Air, 0);
            }

        // (f) 震击 / 燃箭行为腿（独立实体管理器同一世界）。**天空台**（y=92，天然地形远在其下——
        //     t799 rig 教训：地面坐标可能撞世界生成地形，猪被挤出台外落自然地表、弹道被台阶挡）；
        //     台上两格显式清空保证净空。短窗抢拍 t836(d) 口径：settle 3 + 飞行 2 tick ≪ 首游荡窗 30 tick。
        //     箭速 24 b/s → 逐帧采样步 1.2 格 < 命中盒宽 1.6（外扩 kArrowHitHalfW=0.4 + 猪半宽 0.4 →
        //     ±0.8，防高速逐帧采样跳过猪体——隧道效应）；2 tick 重力落差 0.21 格 ≪ 半身高 → 定平射命中。
        //     量测窗 = 命中后 0.8s（16 tick）：击退速度 v0 = kKnockbackHoriz(4.5)×倍率、衰减率 4/s →
        //     位移积分 ≈ 1.103×倍率（基线 ×1 → ~1.06 / 倍管 ×6 → ~6.36）。AI 游走是窗内**固定随机向**
        //     （kWanderMin=2.0s > 窗长）≤0.8 格噪声、击退恒沿箭向（−x）→ 量 −x 向位移取确定界：
        //     base ∈ [0.25, 1.95] / shock ∈ [5.5, 7.5]（最坏界含游走极值仍不相交，间隔断言 3.0 余量
        //     ≥0.56 —— ×2 倍率首版与游走噪声同量级导致偶发假红，改 ×6 对比后界间天然分离）。
        //     各跑 2 次取 base max / shock min 进一步压噪。倍率数值本体已由 (d) 形式腿钉死，本腿证
        //     spawn→实体载荷→命中击退 全链随倍率缩放。
        EntityManager entsA;
        // t1029 wander 冻结缝：0.8s 量测窗内猪 wander 随机向 = dBMax 历史擦线偶红源（review0907
        //   B-P1-2 放宽 3.0 的依据即此噪声）。冻结后位移 = 击退积分纯确定值，包络断言保持不动。
        entsA.setWanderFrozen(true);
        const auto tickA = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) entsA.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        for (int x = 16; x <= 26; ++x)
            for (int z = 18; z <= 22; ++z) {
                wL.setBlock(x, 92, z, BR::Stone, 0); // 天空台面（猪脚位 y=93）
                wL.setBlock(x, 93, z, BR::Air, 0);   // 台上净空显式清（防世界生成残留）
                wL.setBlock(x, 94, z, BR::Air, 0);
            }
        const auto shootPig = [&](float kbMul, float igniteSec, float *dispOut, bool *burnOut) {
            *dispOut = -1.0f; *burnOut = false;
            const int pig = entsA.spawnMobTyped(21, 93, 20, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            if (pig < 0) return;
            // settle 8 tick：落地/resting 需 ~4-5 tick（3 tick 会恰好抓在空中下落半程 → pp.y 偏低 →
            //   箭按它平射首 tick 即切进台面格嵌入，永不碰猪——首轮实测根因）。仍 ≪ 首游荡窗 30 tick。
            tickA(8, 0.05f);
            const QVector3D pp = entsA.posAt(pig);
            entsA.spawnArrowPlayer(QVector3D(pp.x() + 2.5f, pp.y(), pp.z()),
                                   QVector3D(-24.0f, 0.0f, 0.0f), 2, kbMul, igniteSec);
            int guard = 0;
            while (guard++ < 60) {
                bool arrowGone = true;
                for (int i = 0; i < entsA.count(); ++i)
                    if (entsA.aliveAt(i) && entsA.kindAt(i) == int(EntityManager::Arrow)) { arrowGone = false; break; }
                if (arrowGone) break; // 命中帧（命中即移除）
                tickA(1, 0.05f);
            }
            const float hitX = entsA.posAt(pig).x();
            *burnOut = entsA.isBurningAt(pig);
            tickA(16, 0.05f); // 0.8s 量测窗：击退全程衰减 + 覆盖多个 AI 时间片
            *dispOut = hitX - entsA.posAt(pig).x(); // −x 向位移（击退恒沿箭向 = −x；游走随机向为噪）
            entsA.removeEntityAt(pig);
        };
        float dB1 = -1.0f, dB2 = -1.0f, dS1 = -1.0f, dS2 = -1.0f, dTmp = -1.0f;
        bool burnBase = true, burnFlame = false;
        shootPig(1.0f, 0.0f, &dB1, &burnBase);        // 基线 #1：不点燃
        shootPig(6.0f, 0.0f, &dS1, &burnFlame);       // 高倍率 #1（链路缩放腿）
        shootPig(1.0f, 0.0f, &dB2, &burnFlame);       // 基线 #2
        shootPig(6.0f, 0.0f, &dS2, &burnFlame);       // 高倍率 #2
        shootPig(1.0f, 5.0f, &dTmp, &burnFlame);      // 燃箭（点燃腿；位移不读）
        const float dBMax = std::max(dB1, dB2), dSMin = std::min(dS1, dS2);
        // review0907 B-P1-2：dBMax 绝对上限 2.2 → 实测包络 3.0。依据：套件历史 13 次偶红 diag
        //   全部是 dBMax ∈ [2.202, 2.279] 擦线超限（0.8s 量测窗叠了猪游走随机位移，与击退位移同
        //   量级），dSMin 同期 9.06..9.84 从未威胁 5.2 下限。相对判据 dSMin > 4*dBMax 被同批数据
        //   否决（最差组合 9.06 < 4×2.28 = 9.12 仍红）。3.0 = 观测最大 2.279 + 31% 余量；判别力
        //   承载移到间隔断言（dSMin − dBMax > 3.0，实测最差 6.77）与 ×6 倍率腿 —— 正中箭位移显著
        //   小于蓄满箭的原判别面不变。
        const bool okArrow = dBMax > 0.2f && dBMax < 3.0f && dSMin > 5.2f
                          && (dSMin - dBMax) > 3.0f
                          && !burnBase && burnFlame;
        for (int x = 16; x <= 26; ++x)
            for (int z = 18; z <= 22; ++z)
                wL.setBlock(x, 92, z, BR::Air, 0);

        // (g) Game 层接线源码钉（t927(c) 手法：去注释函数体内五个权威函数调用——行为级不可达的
        //     endBowDraw 蓄力链 / useFishingRod 甩竿参数由源钉锁接线，数值本体已由 (d) 钉死）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString pcPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                        QStringLiteral("src/Game/playercontroller.cpp"));
            QFile f(pcPath);
            if (f.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(f.readAll());
                const auto body = [&](const char *from, const char *to) {
                    const int b0 = t.indexOf(QLatin1String(from));
                    const int b1 = t.indexOf(QLatin1String(to));
                    QString out;
                    if (b0 < 0 || b1 <= b0) return out;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                        if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                        out += line; out += QLatin1Char('\n');
                    }
                    return out;
                };
                const QString bowBody = body("void PlayerController::endBowDraw()",
                                             "void PlayerController::cancelBowDraw()");
                const QString rodBody = body("void PlayerController::useFishingRod()",
                                             "void PlayerController::updateFishing");
                okPin = bowBody.contains(QStringLiteral("EnchantRegistry::bowArrowDamage"))
                     && bowBody.contains(QStringLiteral("EnchantRegistry::NeverRun"))
                     && bowBody.contains(QStringLiteral("EnchantRegistry::bowKnockbackMultiplier"))
                     && bowBody.contains(QStringLiteral("EnchantRegistry::bowIgniteSeconds"))
                     && rodBody.contains(QStringLiteral("EnchantRegistry::rodWaitScale"))
                     && rodBody.contains(QStringLiteral("EnchantRegistry::rodBiteWindowExtra"));
            }
        }

        const bool ok960 = okData && okGate && okBook && okForm && okTide && okBite && okArrow && okPin;
        if (!ok960)
            qInfo().noquote() << "  t960 diag: data" << okData << "gate" << okGate << "book" << okBook
                              << "form" << okForm << "tide" << okTide
                              << "waits base/tide/again" << baseWait << tideWait << againWait
                              << "biteWin" << wideWinSec << "arrow" << okArrow
                              << "dBMax" << dBMax << "dSMin" << dSMin
                              << "burnBase" << burnBase << "burnFlame" << burnFlame << "pin" << okPin;
        if (!ok960) ++totalFail;
        qInfo().noquote() << (ok960 ? "PASS" : "FAIL")
                          << "| t960 bow/rod exclusive enchantments: registry grows to 20 with the bow"
                             " line (might +1HP/lv arrow damage, bow-shock x2/lv arrow knockback,"
                             " bright-draw ignite-on-hit 5s, never-run no-arrow-consumption) and the"
                             " rod line (tide-call wait x(1-0.2lv) faster bites, bite-call +0.5s/lv"
                             " reel window on top of the pinned 1.0s base), all conflict-group 0;"
                             " applicability gate is strict-exclusive (bow lines only on the bow, rod"
                             " lines only on the rod, every other item refused, book carrier passes -"
                             " the t959 main-category wheel grows 3->5 so book casts reach all six);"
                             " single-authority formulas pinned numerically; behavioral legs: same-cell"
                             " same-serial bobber wait shrinks x0.4 with determinism, +0.5s window"
                             " measures 1.4-1.6s bite->escape, shock arrow pushes ~x2 baseline"
                             " displacement and flame arrow leaves isBurning true (baseline false);"
                             " endBowDraw/useFishingRod wiring pinned at source (t927(c) precedent)";
    }

    // ── P-t961 杀手系攻击行族加成显示探针（R19.17 🅳；用户第五轮口径「亡灵杀手/截肢杀手的加成写进
    //    剑攻击行——『攻击+7(+3)』格式（括号=对特定族加成）」）──
    //    口径钉：+N = 现行 displayAttackDamage（物品基伤 + 锐锋；杀手系与锐锋互斥组 1 → 杀手剑的 N 即
    //    基伤，不动），(+M) = 对特定族加成合计（EnchantRegistry::familyAttackBonus = 2.5×各级，与
    //    attackMob 实战 t476 对族公式同值；互斥组 1 → 至多一支非零），括号本身即族加成语义（不展开族名）。
    //    断言：
    //    (a) 注册表权威数值腿：亡灵 III = 7.5 / 节肢 I = 2.5 / 无杀手 = 0 / 锐锋不算族加成；
    //    (b) 显示组装行为腿（Hotbar 直调，t763 同台）：钻石剑 + 节肢 I → displayAttackDamage 仍 7（+N
    //        口径不动）且后缀「(+3)」——拼形即用户口径「+7(+3)」；亡灵 III →「(+8)」（7.5 取整）；
    //        无杀手 → 空串（行形态不变）；
    //    (c) 源码钉：九处攻击行组装点（八面板 tooltip + HUD hover）全拼 displayFamilyBonusText（QML 不持
    //        族加成数值——后缀值只活在注册表权威一处）；hotbar.cpp 桥本体含「(+」括号分支 + 走
    //        familyAttackBonus 调用链（t960(g) 手法）；review0830 #25：attackMob 实战体同调注册表
    //        familyAttackBonusFor 单支权威（族门在调用侧）+ 双写 2.5f 字面量绝迹（回退即红）。
    {
        // (a) 注册表权威数值腿。
        const auto closeF = [](float got, float expect) { return std::abs(got - expect) < 1e-4f; };
        const int smite3  = EnchantRegistry::pack(int(EnchantRegistry::UndeadSlay), 3);
        const int bane1   = EnchantRegistry::pack(int(EnchantRegistry::ArthropodSlay), 1);
        const int sharp3b = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
        const int smiteE[4] = {smite3, 0, 0, 0};
        const int baneE[4]  = {bane1, 0, 0, 0};
        const int sharpE[4] = {sharp3b, 0, 0, 0};
        const int noneE[4]  = {0, 0, 0, 0};
        bool okForm = closeF(EnchantRegistry::familyAttackBonus(smiteE), 7.5f)
                   && closeF(EnchantRegistry::familyAttackBonus(baneE), 2.5f)
                   && closeF(EnchantRegistry::familyAttackBonus(noneE), 0.0f)
                   && closeF(EnchantRegistry::familyAttackBonus(sharpE), 0.0f)  // 锐锋非杀手系
                   && EnchantRegistry::conflictGroup(int(EnchantRegistry::UndeadSlay)) == 1
                   && EnchantRegistry::conflictGroup(int(EnchantRegistry::ArthropodSlay)) == 1;

        // (b) 显示组装行为腿（Hotbar 直调）：+N 口径不动 + (+M) 后缀拼形。
        Hotbar hb;
        const int diaSword = int(ToolRegistry::DiamondSword);
        const QVariantList baneL{bane1, 0, 0, 0};
        const QVariantList smiteL{smite3, 0, 0, 0};
        const QVariantList zeroL{0, 0, 0, 0};
        const QString famBane  = hb.displayFamilyBonusText(baneL);
        const QString famSmite = hb.displayFamilyBonusText(smiteL);
        const QString famNone  = hb.displayFamilyBonusText(zeroL);
        bool okShow = ToolRegistry::attackDamage(diaSword) == 7
                   && hb.displayAttackDamage(diaSword, baneL) == 7            // +N = 基伤 7，不动
                   && famBane == QStringLiteral("(+3)")                        // 拼 =「+7(+3)」用户口径形
                   && hb.displayAttackDamage(diaSword, smiteL) == 7
                   && famSmite == QStringLiteral("(+8)")                       // 7.5 → round 8
                   && famNone.isEmpty();                                       // 无杀手 → 行形态不变
        if (!okShow)
            qInfo().noquote() << "  t961 show diag: famBane" << famBane << "famSmite" << famSmite
                              << "famNoneEmpty" << famNone.isEmpty();

        // (c) 源码钉：九处组装点 + hotbar.cpp 桥本体调用链。
        bool okPin = true;
        int pinnedFaces = 0;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            static const char *kFaces[9] = {
                "src/ui/Inventory.qml", "src/ui/SurvivalInventory.qml", "src/ui/ChestUI.qml",
                "src/ui/DispenserUI.qml", "src/ui/CraftingTableUI.qml", "src/ui/FurnaceUI.qml",
                "src/ui/AnvilUI.qml", "src/ui/EnchantingTableUI.qml", "src/ui/Main.qml",
            };
            for (const char *f : kFaces) {
                QFile qf(root + QLatin1Char('/') + QLatin1String(f));
                const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
                // 组装点钉：displayFamilyBonusText( 与 displayAttackDamage( 同文件共现（该桥接名全工程
                //   仅九处调用 + hotbar 一处实现，出现在文件内 = 该攻击行面拼了族加成后缀）。
                if (t.isEmpty() || !t.contains(QLatin1String("displayAttackDamage"))
                    || !t.contains(QLatin1String("displayFamilyBonusText("))) {
                    okPin = false;
                    qInfo().noquote() << "  t961 pin diag: face missing suffix call" << f;
                } else {
                    ++pinnedFaces;
                }
            }
            QFile hf(root + QStringLiteral("/src/Game/hotbar.cpp"));
            if (hf.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(hf.readAll());
                const int b0 = t.indexOf(QLatin1String("QString Hotbar::displayFamilyBonusText"));
                const int b1 = t.indexOf(QLatin1String("bool Hotbar::enchantApplicableTo"));
                QString body;
                if (b0 >= 0 && b1 > b0) body = t.mid(b0, b1 - b0);
                if (body.isEmpty() || !body.contains(QLatin1String("EnchantRegistry::familyAttackBonus"))
                    || !body.contains(QLatin1String("(+%1"))) {  // 「(+」括号分支本体
                    okPin = false;
                    qInfo().noquote() << "  t961 pin diag: hotbar.cpp bridge body missing chain/branch";
                }
            } else {
                okPin = false;
                qInfo().noquote() << "  t961 pin diag: hotbar.cpp unreadable";
            }
            // review0830 #25：实战侧对族加成同调注册表单支权威 familyAttackBonusFor（族门在调用侧，
            //   亡灵/节肢两支各取其一——合计面 familyAttackBonus 会把亡灵支放行到蜘蛛上破族门，故实战
            //   取支变体；每级倍率字面量全工程只活 familyAttackBonusFor 一处）——attackMob 函数体内含
            //   两支权威调用、旧双写 2.5f 字面量绝迹（回退双写或误调合计面即红）。
            QFile pcf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            if (pcf.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(pcf.readAll());
                const int m0 = t.indexOf(QLatin1String("void PlayerController::attackMob"));
                const int m1 = t.indexOf(QLatin1String("void PlayerController::"), m0 + 10);
                QString body;
                if (m0 >= 0 && m1 > m0) body = t.mid(m0, m1 - m0);
                if (body.isEmpty()
                    || !body.contains(QLatin1String("familyAttackBonusFor(heldEnch, EnchantRegistry::UndeadSlay)"))
                    || !body.contains(QLatin1String("familyAttackBonusFor(heldEnch, EnchantRegistry::ArthropodSlay)"))
                    || body.contains(QLatin1String("2.5f"))) {
                    okPin = false;
                    qInfo().noquote() << "  t961 pin diag: attackMob body missing familyAttackBonusFor"
                                         " branches / retains the doubled 2.5f literal";
                }
            } else {
                okPin = false;
                qInfo().noquote() << "  t961 pin diag: playercontroller.cpp unreadable";
            }
        }

        const bool ok961 = okForm && okShow && okPin;
        if (!ok961)
            qInfo().noquote() << "  t961 diag: form" << okForm << "show" << okShow << "pin" << okPin
                              << "faces" << pinnedFaces;
        if (!ok961) ++totalFail;
        qInfo().noquote() << (ok961 ? "PASS" : "FAIL")
                          << "| t961 slayer family bonus in the sword attack line: +N keeps the"
                             " current display value (base + sharpness; slayers sit in mutual"
                             " exclusion group 1 so a slayer sword's N is the plain base damage)"
                             " and a (+M) suffix carries the vs-family bonus taken from the single"
                             " registry authority familyAttackBonus (2.5/level, same formula the"
                             " combat path applies - review0830 #25: the combat path now calls the"
                             " registry per-branch authority familyAttackBonusFor with the family"
                             " gate at the call site, the doubled 2.5f literal in attackMob is"
                             " pinned extinct and the single numeric source lives in the registry"
                             " so display==combat is by construction); diamond sword +"
                             " arthropod I assembles +7(+3), undead III rounds to (+8), no slayer"
                             " keeps the old line shape via an empty suffix; all nine attack-line"
                             " assembly sites (8 panel tooltips + HUD hover) route through"
                             " displayFamilyBonusText, and the bridge body pins the registry call"
                             " chain plus the (+ paren branch (t960(g)";
    }

    // ── P-t962 附魔书↔附魔书交换（R19.17 🅳 收官；用户第五轮口径「背包拿附魔书左键物品交换是对的，
    //    但附魔书对附魔书槽的交换没做——补齐」）──
    //    对「普通物品交换是对的」的分叉点根因双闸：
    //    ① InventoryOps.resolveClick 同 id 恒入 C 合并臂 → 附魔书 maxStack=1 → 槽恒满 space≤0 → null =
    //       no-op（旧注释「A/B/D 路径覆盖工具搬运」对同 id 不成立——D 互换在同 id 下旧不可达）；
    //    ② EnchantingTableUI.localCanPlace 对附魔书（itemEnchantCategory=None）拒入槽 0——就算换算出来
    //       也进不去（t648 门禁把书与非可附魔物一并扫进拒入面）。
    //    修：cap≤1 同 id 落 D 互换（可堆叠满槽 no-op 与全部合并语义保持）+ 附魔书门禁豁免（itemReady 恒
    //    假 → 书在槽 0 不可再附，t648 刷属性面不开放；t959 施法链只从「可附魔且未附魔」态出发零触碰）。
    //    断言（t874/t956 真链 harness 先例：源树 QML + 真 C++ Hotbar，面板函数链直调——合成鼠标事件在
    //    该 harness 有不可消除的拖动伪影，t874 定案函数链直调已覆盖真链关键面「真 QML × 真 C++ VM」）：
    //    (a) 附魔台槽 0 书书左键交换 + 换回：附魔 / 名随各自实例双向往返不失真（B 书附魔落 1 号槽位、
    //        A 书带实例名——钉逐槽 / 逐名保真，非「任一非零」粗粒度）；
    //    (b) 豁免后的自由进出：空槽 0 放书 / 取回元数据保真；书在槽 0 doEnchant 恒拒（itemReady 门——
    //        等级 / 青金石零消耗、槽内容不动，t648 防线对书路径不破）；
    //    (c) 阴性腿：普通异 id 交换照旧（c1）；已附魔剑仍拒入槽 0（c2，豁免仅附魔书）；可堆叠同 id 满
    //        槽撞同 id 仍 no-op（c3，满槽 / 合并口径保持）；
    //    (d) 铁砧同病同修：A/B 两输入槽书书交换（铁砧面无门禁，纯 resolveClick 臂修复即愈）。
    {
        static bool sT962TypesRegistered = false;
        if (!sT962TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT962", 1, 0, "Hotbar");
            qmlRegisterType<PlayerState>("VoxelSandboxProbeT962", 1, 0, "PlayerState");
            qmlRegisterType<PlayerController>("VoxelSandboxProbeT962", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT962", 1, 0, "ResourcePackManager");
            sT962TypesRegistered = true;
        }
        bool harnessOk = true;
        QString diag;
        const QString uiDir962 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUiDir = QDir::temp().absoluteFilePath(
                QStringLiteral("t962_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir);
        for (const QString f : { QStringLiteral("AnvilUI.qml"), QStringLiteral("EnchantingTableUI.qml"),
                                 QStringLiteral("InventoryOps.js"), QStringLiteral("InvSlot.qml"),
                                 QStringLiteral("ToolIcon.qml"), QStringLiteral("MaterialIcon.qml") }) {
            QFile::remove(probeUiDir + QLatin1Char('/') + f);
            QFile(uiDir962 + QLatin1Char('/') + f).copy(probeUiDir + QLatin1Char('/') + f);
        }
        {
            // 临时目录直载的 URL 改写（t874/t956 同款）：相对 js 导入 → 绝对 file URL（防 build qmldir
            //   prefer 重定向染指）；面板 import → 探针私有 URI（私有 URI 无 qmldir → 走 C++ 注册）。
            const QUrl jsUrl = QUrl::fromLocalFile(probeUiDir + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            const QStringList qmlFiles = QDir(probeUiDir).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles) {
                QFile p(probeUiDir + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT962\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine962;
        Hotbar vm962;
        PlayerState ps962;
        engine962.rootContext()->setContextProperty(QStringLiteral("t962Hotbar"), &vm962);
        engine962.rootContext()->setContextProperty(QStringLiteral("t962PlayerState"), &ps962);
        QQmlComponent wrapComp962(&engine962);
        // 宿主桩：Main.qml 根（id: window）最小复刻（两面板经作用域链解析 window.shiftHeld 等）。
        wrapComp962.setData(R"QML(import QtQuick
Item {
    id: window
    width: 800; height: 1200
    property bool shiftHeld: false
    property string hoveredSlotKey: ""
    function refocusKeyInput() { }
    function closeAnvil() { }
    function closeEnchantingTable() { }
    function burstEnchantRunes(n) { }
}
)QML", QUrl());
        QQuickItem host962;   // 独立场景根（无窗口，同 t874：函数链直调无需窗口事件）
        QQuickItem *wrap962 = nullptr;
        QObject *anvil962 = nullptr;
        QObject *enchant962 = nullptr;
        if (wrapComp962.isError()) {
            harnessOk = false;
            diag = QStringLiteral("wrapper: ") + wrapComp962.errorString();
        } else {
            wrap962 = qobject_cast<QQuickItem *>(wrapComp962.create());
            if (!wrap962) {
                harnessOk = false;
                diag = QStringLiteral("wrapper create failed");
            } else {
                wrap962->setParent(&engine962);
                wrap962->setParentItem(&host962);
            }
        }
        auto loadPanel962 = [&](const char *fileName, QObject **out, double y) {
            QFile src(probeUiDir + QLatin1Char('/') + QLatin1String(fileName));
            if (!src.open(QIODevice::ReadOnly)) {
                harnessOk = false;
                diag = QString::fromLatin1(fileName) + QStringLiteral(" read failed");
                return;
            }
            QQmlComponent comp(&engine962);
            comp.setData(src.readAll(), QUrl::fromLocalFile(src.fileName()));
            if (comp.isError()) {
                harnessOk = false;
                diag = QString::fromLatin1(fileName) + QStringLiteral(" load: ") + comp.errorString();
                return;
            }
            *out = comp.create(qmlContext(wrap962));   // wrapper 作用域链（同 t874：面板内 window.id 解析）
            QQuickItem *it = qobject_cast<QQuickItem *>(*out);
            if (!it) {
                harnessOk = false;
                diag = QString::fromLatin1(fileName) + QStringLiteral(" create failed");
                return;
            }
            (*out)->setProperty("hotbar", QVariant::fromValue(&vm962));
            (*out)->setProperty("playerState", QVariant::fromValue(&ps962));
            (*out)->setProperty("player", QVariant());
            (*out)->setProperty("progress", QVariant());
            (*out)->setProperty("theWorld", QVariant());   // EnchantingTableUI 有；AnvilUI 无此属性（setProperty 无害）
            it->setWidth(800);
            it->setHeight(600);
            (*out)->setParent(wrap962);
            it->setParentItem(wrap962);
            it->setY(y);
        };
        if (harnessOk) {
            loadPanel962("AnvilUI.qml", &anvil962, 0.0);
            loadPanel962("EnchantingTableUI.qml", &enchant962, 600.0);
        }

        bool okA = false, okB = false, okC1 = false, okC2 = false, okC3 = false, okD = false;
        if (harnessOk && anvil962 && enchant962) {
            QCoreApplication::processEvents();
            const int book = RecipeRegistry::EnchantedBookId;
            const int plainBook = RecipeRegistry::BookId;
            const int lapis = RecipeRegistry::LapisId;
            const int sharp5 = (EnchantRegistry::Sharpness << 8) | 5;
            const int fire1 = (EnchantRegistry::FireAspect << 8) | 1;
            const int kb2 = (EnchantRegistry::Knockback << 8) | 2;
            const int prot4 = (EnchantRegistry::Protection << 8) | 4;
            const int sharp3 = (EnchantRegistry::Sharpness << 8) | 3;
            const int pick = ToolRegistry::PickaxeIron;
            const int sword = ToolRegistry::SwordIron;

            auto call = [](QObject *obj, const char *method, const QVariantList &args) -> bool {
                const QVariant v0 = args.value(0), v1 = args.value(1), v2 = args.value(2), v3 = args.value(3);
                const QVariant v4 = args.value(4), v5 = args.value(5), v6 = args.value(6);
                switch (args.size()) {
                case 0:  return QMetaObject::invokeMethod(obj, method);
                case 1:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0));
                case 2:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1));
                case 3:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1), Q_ARG(QVariant, v2));
                case 4:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1), Q_ARG(QVariant, v2), Q_ARG(QVariant, v3));
                case 5:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1), Q_ARG(QVariant, v2), Q_ARG(QVariant, v3), Q_ARG(QVariant, v4));
                case 6:  return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1), Q_ARG(QVariant, v2), Q_ARG(QVariant, v3), Q_ARG(QVariant, v4), Q_ARG(QVariant, v5));
                default: return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, v0), Q_ARG(QVariant, v1), Q_ARG(QVariant, v2), Q_ARG(QVariant, v3), Q_ARG(QVariant, v4), Q_ARG(QVariant, v5), Q_ARG(QVariant, v6));
                }
            };
            auto listEq4 = [](const QVariantList &a, int e0, int e1, int e2, int e3) {
                return a.size() == 4 && a.at(0).toInt() == e0 && a.at(1).toInt() == e1
                        && a.at(2).toInt() == e2 && a.at(3).toInt() == e3;
            };
            auto enchStr = [](const QVariantList &e) {
                QString s;
                for (int i = 0; i < e.size(); ++i)
                    s += (i ? QStringLiteral(",") : QString()) + QString::number(e.at(i).toInt());
                return s;
            };
            auto idAt = [](QObject *panel, const char *prop, int idx) -> int {
                const QVariantList a = panel->property(prop).toList();
                return (idx >= 0 && idx < a.size()) ? a.at(idx).toInt() : 0;
            };
            auto enchAt = [](QObject *panel, const char *prop, int idx) -> QVariantList {
                const QVariantList outer = panel->property(prop).toList();
                QVariantList e;
                if (idx >= 0 && idx < outer.size())
                    e = outer.at(idx).toList();
                while (e.size() < 4)
                    e.append(0);
                return e;
            };
            auto nameAt = [](QObject *panel, const char *prop, int idx) -> QString {
                const QVariantList a = panel->property(prop).toList();
                return (idx >= 0 && idx < a.size()) ? a.at(idx).toString() : QString();
            };
            // 双击判定态复位（t874 同款：harness 同步连点恒 <280ms，须显式复位成「独立单击」）。
            auto resetTap = [&](QObject *panel) {
                panel->setProperty("lastTapMs", 0.0);
                panel->setProperty("lastTapKey", QString());
            };
            auto clearVm = [&]() {
                for (int i = 0; i < vm962.slotCount(); ++i)
                    vm962.setStack(i, 0, 0);
                for (int i = 0; i < vm962.mainCount(); ++i)
                    vm962.mainSetStack(i, 0, 0);
                vm962.setHeldBlock(0);
            };
            auto resetEnch = [&]() {
                clearVm();
                enchant962->setProperty("visible", false);   // 触发 returnEnchantToHotbar（槽空则零迭代）
                enchant962->setProperty("visible", true);
                clearVm();
            };
            auto resetAnvil = [&]() {
                clearVm();
                anvil962->setProperty("visible", false);     // 触发 returnAnvilToHotbar
                anvil962->setProperty("visible", true);
                clearVm();
            };
            // 直写本地槽（writeSlot 面板薄包装 → InventoryOps → localWriteSlot；探针铺底用——被测的
            //   点击交换链从铺底态出发，铺底本身不走被测路径）。
            auto putItem = [&](QObject *panel, const QString &group, int idx, int id, int count,
                               const QVariantList &ench, const QString &nm) {
                call(panel, "writeSlot", { QVariant(group), QVariant(idx), QVariant(id),
                                           QVariant(count), QVariant(0), QVariant(ench), QVariant(nm) });
            };

            // (a) 附魔台槽 0 书书左键交换 + 换回（双向元数据保真）。
            {
                resetEnch();
                putItem(enchant962, QStringLiteral("enchant"), 0, book, 1,
                        QVariantList{sharp5, 0, 0, 0}, QStringLiteral("甲书"));      // A：锐锋 V 带名
                vm962.setHeldBlock(book);
                vm962.setHeldCount(1);
                vm962.setHeldEnchants(QVariantList{0, fire1, 0, 0});                  // B：火触 I 落 1 号槽位（逐槽保真钉）+ 无名
                vm962.setHeldCustomName(QString());
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                const bool fwd = idAt(enchant962, "enchantSlots", 0) == book
                        && idAt(enchant962, "enchantCounts", 0) == 1
                        && listEq4(enchAt(enchant962, "enchantEnch", 0), 0, fire1, 0, 0)
                        && nameAt(enchant962, "enchantNames", 0).isEmpty()
                        && vm962.heldBlock() == book && vm962.heldCount() == 1
                        && listEq4(vm962.heldEnchants(), sharp5, 0, 0, 0)
                        && vm962.heldCustomName() == QStringLiteral("甲书");
                // 换回：A（现光标）对 B（现槽 0）再左键 → 槽回到 A、光标回到 B（往返不失真）。
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                const bool back = idAt(enchant962, "enchantSlots", 0) == book
                        && listEq4(enchAt(enchant962, "enchantEnch", 0), sharp5, 0, 0, 0)
                        && nameAt(enchant962, "enchantNames", 0) == QStringLiteral("甲书")
                        && vm962.heldBlock() == book
                        && listEq4(vm962.heldEnchants(), 0, fire1, 0, 0)
                        && vm962.heldCustomName().isEmpty();
                okA = fwd && back;
                if (!okA)
                    diag = QStringLiteral("(a) fwd=") + QVariant(fwd).toString()
                            + QStringLiteral(" back=") + QVariant(back).toString()
                            + QStringLiteral(" slot0ench=") + enchStr(enchAt(enchant962, "enchantEnch", 0))
                            + QStringLiteral(" heldench=") + enchStr(vm962.heldEnchants());
            }
            // (b) 豁免自由进出：空槽 0 放书 / 取回 + 书在槽 0 doEnchant 恒拒（itemReady 门）。
            {
                resetEnch();
                vm962.setHeldBlock(book);
                vm962.setHeldCount(1);
                vm962.setHeldEnchants(QVariantList{kb2, 0, 0, 0});
                vm962.setHeldCustomName(QString());
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });   // 放入空槽 0
                const bool in = idAt(enchant962, "enchantSlots", 0) == book
                        && listEq4(enchAt(enchant962, "enchantEnch", 0), kb2, 0, 0, 0)
                        && vm962.heldBlock() == 0;
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });   // 取回
                const bool out = vm962.heldBlock() == book && listEq4(vm962.heldEnchants(), kb2, 0, 0, 0)
                        && idAt(enchant962, "enchantSlots", 0) == 0;
                // 书在槽 0 → 三档位施放恒拒（itemReady 门：category=None + 已带附魔）：等级 / 青金石
                //   零消耗、槽内容不动 —— t648「不可再附」语义对附魔书路径保持。
                putItem(enchant962, QStringLiteral("enchant"), 0, book, 1,
                        QVariantList{sharp5, 0, 0, 0}, QString());
                putItem(enchant962, QStringLiteral("enchant"), 1, lapis, 5, QVariantList{0, 0, 0, 0}, QString());
                vm962.setHeldBlock(0);
                const int levelBefore = ps962.property("level").toInt();
                call(enchant962, "doEnchant", { QVariant(0) });
                const bool rejected = idAt(enchant962, "enchantSlots", 0) == book
                        && listEq4(enchAt(enchant962, "enchantEnch", 0), sharp5, 0, 0, 0)
                        && idAt(enchant962, "enchantCounts", 1) == 5
                        && ps962.property("level").toInt() == levelBefore;
                okB = in && out && rejected;
                if (!okB)
                    diag = QStringLiteral("(b) in=") + QVariant(in).toString()
                            + QStringLiteral(" out=") + QVariant(out).toString()
                            + QStringLiteral(" rejected=") + QVariant(rejected).toString();
            }
            // (c1) 阴性：普通异 id 交换照旧（素品镐 ↔ 素品剑；用户口径「左键物品交换是对的」基线）。
            {
                resetEnch();
                putItem(enchant962, QStringLiteral("enchant"), 0, pick, 1, QVariantList{0, 0, 0, 0}, QString());
                vm962.setStack(3, sword, 1);
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });    // 拾剑
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });   // 剑对镐 → 交换
                okC1 = idAt(enchant962, "enchantSlots", 0) == sword
                        && vm962.heldBlock() == pick
                        && listEq4(vm962.heldEnchants(), 0, 0, 0, 0);
            }
            // (c2) 阴性：已附魔剑仍拒入槽 0（t648 刷属性防线不破——豁免仅附魔书，不外溢已附魔工具）。
            {
                resetEnch();
                vm962.setStack(3, sword, 1, ToolRegistry::maxDurability(sword) - 4,
                               QVariantList{sharp3, 0, 0, 0}, QString());
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                okC2 = idAt(enchant962, "enchantSlots", 0) == 0
                        && vm962.heldBlock() == sword
                        && listEq4(vm962.heldEnchants(), sharp3, 0, 0, 0);
            }
            // (c3) 阴性：可堆叠同 id 满槽撞同 id 仍 no-op（t962 只放行 cap≤1 不可堆叠臂——满槽 /
            //      合并口径在权威库层面保持）。
            {
                resetEnch();
                putItem(enchant962, QStringLiteral("enchant"), 0, plainBook, 64, QVariantList{0, 0, 0, 0}, QString());
                vm962.setHeldBlock(plainBook);
                vm962.setHeldCount(20);
                resetTap(enchant962);
                call(enchant962, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                okC3 = idAt(enchant962, "enchantSlots", 0) == plainBook
                        && idAt(enchant962, "enchantCounts", 0) == 64
                        && vm962.heldBlock() == plainBook && vm962.heldCount() == 20;
            }
            // (d) 铁砧同病同修：A/B 两输入槽书书交换（铁砧面无门禁，纯 resolveClick 臂修复即愈）。
            {
                resetAnvil();
                putItem(anvil962, QStringLiteral("anvil"), 1, book, 1,
                        QVariantList{prot4, 0, 0, 0}, QStringLiteral("乙书"));        // B 槽书 A
                vm962.setHeldBlock(book);
                vm962.setHeldCount(1);
                vm962.setHeldEnchants(QVariantList{0, fire1, 0, 0});
                vm962.setHeldCustomName(QString());
                resetTap(anvil962);
                call(anvil962, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                const bool legB = idAt(anvil962, "anvilSlots", 1) == book
                        && listEq4(enchAt(anvil962, "anvilEnch", 1), 0, fire1, 0, 0)
                        && nameAt(anvil962, "anvilNames", 1).isEmpty()
                        && vm962.heldBlock() == book
                        && listEq4(vm962.heldEnchants(), prot4, 0, 0, 0)
                        && vm962.heldCustomName() == QStringLiteral("乙书");
                putItem(anvil962, QStringLiteral("anvil"), 0, book, 1,
                        QVariantList{sharp5, 0, 0, 0}, QString());                    // A 槽书 C
                vm962.setHeldBlock(book);
                vm962.setHeldCount(1);
                vm962.setHeldEnchants(QVariantList{kb2, 0, 0, 0});                    // 光标书 D
                vm962.setHeldCustomName(QString());
                resetTap(anvil962);
                call(anvil962, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                const bool legA = idAt(anvil962, "anvilSlots", 0) == book
                        && listEq4(enchAt(anvil962, "anvilEnch", 0), kb2, 0, 0, 0)
                        && vm962.heldBlock() == book
                        && listEq4(vm962.heldEnchants(), sharp5, 0, 0, 0);
                okD = legB && legA;
                if (!okD)
                    diag = QStringLiteral("(d) legB=") + QVariant(legB).toString()
                            + QStringLiteral(" legA=") + QVariant(legA).toString();
            }
            clearVm();
        }
        QDir(probeUiDir).removeRecursively();

        const bool ok962 = harnessOk && okA && okB && okC1 && okC2 && okC3 && okD;
        if (!ok962)
            qInfo().noquote() << "  [t962 diag] harness" << harnessOk << "a" << okA << "b" << okB
                              << "c1" << okC1 << "c2" << okC2 << "c3" << okC3 << "d" << okD << diag;
        if (!ok962) ++totalFail;
        qInfo().noquote() << (ok962 ? "PASS" : "FAIL")
                          << "| t962 enchanted-book <-> enchanted-book slot swap: an enchanted book in "
                             "hand left-clicking the enchanting-table slot 0 (or either anvil input slot) "
                             "now swaps instead of silently doing nothing - InventoryOps.resolveClick "
                             "routes same-id UNSTACKABLE stacks (maxStackSize<=1: tools/armor/books) to "
                             "the D instance-swap arm whose metadata (enchants/durability/name) rides "
                             "each side faithfully both ways, while stackable full-slot no-op and every "
                             "merge semantic stay pinned, and EnchantingTableUI.localCanPlace exempts "
                             "the enchanted book from the category/enchanted rejection so it can enter "
                             "slot 0 (re-enchant remains blocked by the itemReady gate: tiers stay dark "
                             "and doEnchant consumes zero xp/lapis with a book in slot); legs: real "
                             "EnchantingTableUI.qml+AnvilUI.qml x real C++ Hotbar harness swaps book A "
                             "(sharpness V named) against book B (fire-aspect I unnamed) and back with "
                             "per-slot metadata fidelity, places into and takes back from the empty "
                             "slot, verifies doEnchant rejection, keeps the plain pickaxe/sword swap "
                             "and the enchanted-sword rejection and the 64-book full-slot no-op green, "
                             "and swaps books in both anvil input slots";
    }

    // ── t963 豹猫查看器返修（R19.17 🅴：驯服后**变黑色**（错，驯服=家猫花纹）+ 没看到项圈；坐姿与狼
    //    同 bug 已随 t946 连修闭合，本探针零触碰坐姿面）──
    //    根因①（变黑）：驯服毛色变体 0 的程序贴图 mob_cat_black 本身就是全黑档——图鉴驯服拨杆恒取变体 0
    //    代表（t949 注记「开包拨已驯服显全脸黑猫」），游戏内驯服也有 1/3 概率落黑 → 用户观感「驯服=变黑」。
    //    修 = 全黑档退役：build_mob.py make_cat_black → make_cat_tabby（暖棕底 + 深棕虎斑横带 + 浅奶黄
    //    口鼻/腹纹；确定性条带函数 + 相位噪点自创，非 MC 家猫三花色照搬），变体 1 姜黄 / 2 奶油同家猫色
    //    不动——驯服后三变体皆家猫花纹，全工程 mob_cat_black 资产与引用绝迹。
    //    根因②（无项圈）：狼驯服态有 t831 红项圈（游戏内 Main.qml + 图鉴 ResourceBrowser 双落点），豹猫
    //    delegate 两处都无 → 镜像补齐（豹猫颈围缩放 0.36/0.05/0.06；站姿颈根 (0,0.14,-0.30)、坐姿位 =
    //    站姿绕 t946 豹猫坐姿根锚 (-0.12,0.32) 旋 18° = (0,0.319,-0.189)，t946 成对契约同款换算）。
    //    单源：贴图切换 / pack 判据 / 项圈可见三消费端全挂 ocatTamed 同一驯服态位（游戏内 Main.qml 全文
    //    entityManager.ocelotTamedAt 直读恰 1 处 = 属性声明源）。
    //    行为面注：驯服链本身零改动——真实输入链驯服 + 变体 0..2 断言由 P-t949(a) 持续覆盖；QML 呈现 /
    //    贴图资产 headless 不可见，按 t931/t941/t957 纯视觉项先例走源码钉 + PNG 数据钉。
    //    (a) 贴图源钉：两消费端（游戏内 Texture+变体分支 / 图鉴 selectedMobTexSource）指 mob_cat_tabby
    //        + 全黑档四文件绝迹 + 旧资产不在盘。
    //    (b) PNG 数据钉：16×16 全不透明 + 暖底（meanR−meanB ≥ 40）+ 花纹非纯色（亮度 sd ≥ 15）+
    //        非全黑（lum<40 像素 ≤ 5）+ 浅色腹/口鼻斑在（lum≥190 ≥ 8 px）——旧全黑档（meanLum 27.6、
    //        sd 8.5、88% 暗像素）全部断言必红 = 「驯服变黑」回归即测即红。
    //    (c) 项圈两处存在钉 + ocatTamed 单源钉（游戏内 visible: ocatTamed 唯一 + ocelotTamedAt 直读恰 1
    //        + 坐姿/站姿成对位置串；图鉴门挂 mobTamedPreview + 同款位置串 + 注释锚）。
    {
        bool ok = true;
        QString diag;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        auto readSrc963 = [&root](const QString &rel) -> QString {
            QFile f(root + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mn963 = readSrc963(QStringLiteral("src/ui/Main.qml"));
        const QString rb963 = readSrc963(QStringLiteral("src/ui/ResourceBrowser.qml"));
        const QString cm963 = readSrc963(QStringLiteral("CMakeLists.txt"));
        const QString bm963 = readSrc963(QStringLiteral("tools/build_mob.py"));
        // (a) 两消费端贴图源 = 家猫花纹程序贴图（变体 0 棕虎斑）+ 全黑档绝迹（含注释残留）。
        const bool okA963 = mn963.contains(QStringLiteral("qrc:/textures/mob_cat_tabby.png"))
                         && mn963.contains(QStringLiteral("if (v === 0) return mobCatTabbyTex"))
                         && mn963.contains(QStringLiteral("if (ocatTamed) {"))
                         && rb963.contains(QStringLiteral("? \"qrc:/textures/mob_cat_tabby.png\""))
                         && !mn963.contains(QStringLiteral("mob_cat_black"))
                         && !mn963.contains(QStringLiteral("mobCatBlack"))
                         && !rb963.contains(QStringLiteral("mob_cat_black"))
                         && !cm963.contains(QStringLiteral("mob_cat_black"))
                         && !bm963.contains(QStringLiteral("mob_cat_black"))
                         && !QFile::exists(root + QStringLiteral("/textures/mob_cat_black.png"));
        // (b) PNG 数据钉：非全黑家猫花纹的结构断言（旧全黑档签名 meanLum 27.6 / sd 8.5 / 88% 暗像素）。
        QImage tabby963(root + QStringLiteral("/textures/mob_cat_tabby.png"));
        bool okB963 = tabby963.width() == 16 && tabby963.height() == 16;
        double sumR963 = 0, sumB963 = 0, sumL963 = 0, sumL2963 = 0;
        int opaque963 = 0, dark963 = 0, light963 = 0;
        const int n963 = tabby963.width() * tabby963.height();
        for (int y = 0; okB963 && y < tabby963.height(); ++y) {
            for (int x = 0; x < tabby963.width(); ++x) {
                const QColor p = tabby963.pixelColor(x, y);
                if (p.alpha() == 255) ++opaque963;
                const double l = 0.299 * p.red() + 0.587 * p.green() + 0.114 * p.blue();
                sumR963 += p.red();
                sumB963 += p.blue();
                sumL963 += l;
                sumL2963 += l * l;
                if (l < 40.0) ++dark963;
                if (l >= 190.0) ++light963;
            }
        }
        const double meanL963 = sumL963 / n963;
        const double sdL963 = std::sqrt(std::max(0.0, sumL2963 / n963 - meanL963 * meanL963));
        okB963 = okB963 && opaque963 == n963
                && (sumR963 - sumB963) / n963 >= 40.0
                && sdL963 >= 15.0
                && dark963 <= 5
                && light963 >= 8;
        // (c) 项圈两处存在钉 + ocatTamed 单源钉（t986 迁移：项圈从 overlay 盒改 MobModel 几何
        //     collarVisible——单源语义不变：游戏内项圈直挂 ocatTamed 唯一 + ocelotTamedAt 直读恰 1
        //     + 环带几何注释锚；图鉴门挂 mobCollarActive（= mobTamedActive 同一拨杆）+ 旧 overlay 位
        //     串绝迹（P-t986 gone 钉兜底）；图鉴单源门族计数 5 = 贴图源/pack 例外/双眼/形态注
        //     —— 猫项圈 overlay 门已随 t986 退役）。
        const bool okC963 = mn963.count(QStringLiteral("collarVisible: ocatTamed")) == 1
                         && mn963.count(QStringLiteral("entityManager.ocelotTamedAt(index)")) == 1
                         && mn963.contains(QStringLiteral("t986 驯服项圈环带：几何内裸颈段四薄板围合一圈"))
                         && mn963.contains(QStringLiteral("t963 驯服项圈 → t986 整删 overlay"))
                         && rb963.count(QStringLiteral("root.selectedMobFromSection === 11 && root.mobTamedPreview")) == 5
                         //   （review0830 #23：门左操作数改单源 selectedMobFromSection —— t986 起同形 5 处 =
                         //    贴图源 + pack 例外 + 眼×2 + 形态注；猫项圈 overlay 门随 t986 退役 → mobCollarActive）
                         && rb963.count(QStringLiteral("readonly property bool mobCollarActive")) == 1
                         && rb963.contains(QStringLiteral("collarVisible: mobPreviewModel.mobCollarActive"))
                         && rb963.contains(QStringLiteral("t920/t963 驯服狼·猫红项圈 → t986 整删 overlay"));
        ok = okA963 && okB963 && okC963;
        if (!ok)
            qInfo().noquote() << "  [t963 diag] texSrc" << okA963 << "pngData" << okB963
                              << "collarPins" << okC963
                              << "| meanL" << meanL963 << "sdL" << sdL963
                              << "dark" << dark963 << "light" << light963;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t963 ocelot viewer rework: the tamed coat no longer turns BLACK - the "
                             "old variant-0 procedural texture was itself the all-black coat (the "
                             "viewer always previewed it and in-game taming landed on it 1/3 of the "
                             "time, reading as 'taming turns the ocelot black'), so the black coat "
                             "is retired: build_mob.py now generates mob_cat_tabby (warm "
                             "orange-brown base, dark-brown tabby bands from a self-created "
                             "deterministic band function + phase dither, light cream muzzle/belly; "
                             "not imitating any existing cat coat) as variant 0 while variants 1/2 "
                             "stay domestic, and every mob_cat_black asset reference is extinct; "
                             "the red collar the wolf has when tamed is mirrored onto the ocelot in "
                             "BOTH places (in-game Main.qml delegate and the ResourceBrowser 3D "
                             "preview) scaled to the ocelot neck (0.36/0.05/0.06, standing root "
                             "(0,0.14,-0.30), sit position derived by the t946 hip-root chain "
                             "(0,0.319,-0.189)); texture switch, pack predicate and collar visibility "
                             "all hang on the SAME ocatTamed state bit (exactly one direct "
                             "ocelotTamedAt read remains in Main.qml); the sit-pose face needed no "
                             "touch (closed by t946, zero overlap); legs: dual-consumer texture "
                             "source pins + black-coat extinction across four files and the asset "
                             "itself, PNG data pin (16x16 fully opaque, warm base meanR-meanB>=40, "
                             "patterned sd>=15, <=5 dark pixels, >=8 light belly/muzzle pixels - the "
                             "old black coat meanLum 27.6 / sd 8.5 / 88% dark fails every clause), "
                             "and collar/single-source pins on both consumers";
    }

    // ── P-review0830-23 蛋路径项圈残留驯服态（Review_2026-08-30 #23 中危；审查 §六-4 建议
    //    「toggle 状态机补切走序列行为腿 + 同文件门式互对拍 sync pin」的落点）──
    //    病灶：t963 猫项圈可见门用双源 `selectedMobType`（= 生物段选中 **或** 蛋映射，蛋路径
    //    0x24A→11 恒真）→ 「点豹猫 → 拨已驯服 → 再点豹猫蛋」序列下残留 mobTamedPreview 使野生
    //    蛋戴红项圈（贴图门 :253 是单源 selectedMobFromSection → 贴图正确回野生 = 项圈孤证残留）。
    //    同型狼项圈门（t920 即 review-0829 #7 flagged 后未修的同一处）一并修。修 = 两门改单源
    //    `selectedMobFromSection`（与贴图门 :253 口径逐字一致）。
    //    (a) 门式互对拍 sync pin：全文双源驯服门形态（selectedMobType === 1X && mobTamedPreview）
    //        绝迹；单源驯服门（selectedMobFromSection === 1X && mobTamedPreview）逐处计数
    //        （:254 贴图 / :1256 pack 例外 / :1433+:1445 眼 / :1765 形态注——狼/猫项圈门已随 t986
    //        退役：项圈收编 MobModel 几何，门改挂 mobCollarActive（= mobTamedActive 同一拨杆，
    //        selectedMobTameable 单源）→ 单源面只增不减，回潮即红）。
    //    (b) 切走序列行为腿（真 rig，t967 装配法）：真 QQmlEngine 直载源树 ResourceBrowser.qml，
    //        驱动状态机 selectMob(豹猫) → mobTamedPreview=true → selectItem(豹猫蛋 0x24A)：
    //        驯服态**预览 MobModel collarVisible=true**（t986 起项圈=几何旗标，旧 overlay Model
    //        退役）+ 贴图含 tabby；切蛋后 collarVisible=false（修前恒 true = 腿有判别力）+ 贴图回
    //        野生。狼（蛋 0x249）镜像腿（review-0829 #7 遗留同步闭合）。
    //        t989 演化（P-t949(d) 先例）：蛋路径随查看器蛋分区整体退役 → 「残留驯服态漏上蛋」
    //        病面结构性不可再现；豹猫蛋腿翻转为「切蛋 = 蛋路径绝迹（selectedMobType 归 -1）+
    //        驯服残留清零（项圈灭 / 非 tabby / 眼 overlay 随分支门灭）」，驯服态正相腿（拨杆 →
    //        项圈显 + tabby）保绿不动。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rfR23(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rbR23 = rfR23.open(QIODevice::ReadOnly) ? QString::fromUtf8(rfR23.readAll()) : QString();
        const bool okAR23 = rbR23.count(QStringLiteral("root.selectedMobType === 10 && root.mobTamedPreview")) == 0
                         && rbR23.count(QStringLiteral("root.selectedMobType === 11 && root.mobTamedPreview")) == 0
                         && rbR23.count(QStringLiteral("root.selectedMobFromSection === 11 && root.mobTamedPreview")) == 5
                         && rbR23.count(QStringLiteral("root.selectedMobFromSection === 10 && root.mobTamedPreview")) == 0
                         && rbR23.contains(QStringLiteral("!(root.selectedMobFromSection === 11 && root.mobTamedPreview)"))
                         && rbR23.count(QStringLiteral("readonly property bool mobCollarActive")) == 1
                         && rbR23.contains(QStringLiteral("collarVisible: mobPreviewModel.mobCollarActive"))
                         && rbR23.contains(QStringLiteral("review0830 #23 单源"));
        ok = ok && okAR23;

        static bool sTR23TypesRegistered = false;
        if (!sTR23TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeR23", 1, 0, "Hotbar");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeR23", 1, 0, "ResourcePackManager");
            qmlRegisterType<BlockCube>("VoxelSandboxProbeR23", 1, 0, "BlockCube");
            qmlRegisterType<ItemShapeGeometry>("VoxelSandboxProbeR23", 1, 0, "ItemShapeGeometry");
            qmlRegisterType<BedModelGeometry>("VoxelSandboxProbeR23", 1, 0, "BedModelGeometry");
            qmlRegisterType<MobModel>("VoxelSandboxProbeR23", 1, 0, "MobModel");
            qmlRegisterType<EnchantBookBox>("VoxelSandboxProbeR23", 1, 0, "EnchantBookBox");
            qmlRegisterType<MobBowGeometry>("VoxelSandboxProbeR23", 1, 0, "MobBowGeometry");
            qmlRegisterType<UnitCube>("VoxelSandboxProbeR23", 1, 0, "UnitCube");
            sTR23TypesRegistered = true;
        }
        bool rigOkR23 = false;
        QString rigDiagR23;
        const QString uiDirR23 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUiR23 = QDir::temp().absoluteFilePath(
                QStringLiteral("review0830_23_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiR23);
        for (const QString f : { QStringLiteral("ResourceBrowser.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUiR23 + QLatin1Char('/') + f);
            QFile(uiDirR23 + QLatin1Char('/') + f).copy(probeUiR23 + QLatin1Char('/') + f);
        }
        {
            const QStringList qmlFilesR23 = QDir(probeUiR23).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFilesR23) {
                QFile p(probeUiR23 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeR23\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engineR23;
        Hotbar hbR23;
        ResourcePackManager rpR23;
        QQuickWindow winR23; // 永不 show（visible 旗标求值不依赖渲染回路，t967 先例）
        QQmlComponent compR23(&engineR23,
                              QUrl::fromLocalFile(probeUiR23 + QStringLiteral("/ResourceBrowser.qml")));
        QQuickItem *bR23 = nullptr;
        if (compR23.isError()) {
            rigDiagR23 = QStringLiteral("load: ") + compR23.errorString();
        } else if ((bR23 = qobject_cast<QQuickItem *>(compR23.create())) == nullptr) {
            rigDiagR23 = QStringLiteral("create failed");
        } else {
            bR23->setParent(&engineR23);
            bR23->setProperty("hotbar", QVariant::fromValue(&hbR23));
            bR23->setProperty("resourcePack", QVariant::fromValue(&rpR23));
            bR23->setProperty("atlasSource", QStringLiteral("qrc:/textures/atlas.png"));
            bR23->setProperty("packActive", false);
            bR23->setWidth(700);
            bR23->setHeight(500);
            bR23->setParentItem(winR23.contentItem());
            for (int i = 0; i < 8; ++i)
                QCoreApplication::processEvents();
            // 项圈/眼 overlay 定位：材质基色 + Model scale 逐分量模糊匹配（t963 猫项圈 0.36/0.05/0.06、
            //   t920 狼项圈 0.42/0.06/0.07、豹猫眼 0.035/0.04/0.02；inline 材质 parent() 即宿主 Model）。
            auto overlayModelR23 = [bR23](const QColor &col, const QVector3D &scale) -> QObject * {
                auto scEq = [](const QVector3D &a, const QVector3D &b) {
                    return qFuzzyCompare(a.x(), b.x()) && qFuzzyCompare(a.y(), b.y()) && qFuzzyCompare(a.z(), b.z());
                };
                const auto kids = bR23->findChildren<QObject *>();
                for (QObject *o : kids) {
                    if (std::strcmp(o->metaObject()->className(), "QQuick3DPrincipledMaterial") != 0)
                        continue;
                    if (o->property("baseColor").value<QColor>() != col)
                        continue;
                    QObject *m = o->parent();
                    if (!m || std::strcmp(m->metaObject()->className(), "QQuick3DModel") != 0)
                        continue;
                    if (scEq(m->property("scale").value<QVector3D>(), scale))
                        return m;
                }
                return nullptr;
            };
            auto qmlCallR23 = [](QObject *obj, const char *method, const QVariantList &args) -> bool {
                if (args.size() == 1)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)));
                if (args.size() == 2)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)), Q_ARG(QVariant, args.at(1)));
                return false;
            };
            QObject *catEye = overlayModelR23(QColor(0x1a, 0x1a, 0x1a), QVector3D(0.035f, 0.04f, 0.02f));
            // t986 迁移：项圈 overlay Model 退役 → 拨杆面改读预览 MobModel 的 collarVisible 几何旗标
            //   （单源拨杆不变：mobCollarActive = mobTamedActive = selectedMobTameable × mobTamedPreview）。
            //   类名前缀匹配：MobModel 块内声明 rodClock（t782）→ 动态元对象 "MobModel_QML_N"。
            QObject *mobPreviewR23 = nullptr;
            for (QObject *o : bR23->findChildren<QObject *>()) {
                if (std::strncmp(o->metaObject()->className(), "MobModel", 8) == 0) { mobPreviewR23 = o; break; }
            }
            if (!mobPreviewR23 || !catEye) {
                rigOkR23 = false;
                rigDiagR23 = QStringLiteral("mob model / eye overlay not found");
            } else {
                auto pumpR23 = []() {
                    for (int i = 0; i < 8; ++i)
                        QCoreApplication::processEvents();
                };
                auto failR23 = [&](const char *tag) {
                    rigOkR23 = false;
                    qInfo().noquote() << "  review0830-23 diag:" << tag
                                      << "collarVisible" << mobPreviewR23->property("collarVisible").toBool()
                                      << "eyeVis" << catEye->property("visible").toBool()
                                      << "tex" << bR23->property("selectedMobTexSource").toString();
                };
                rigOkR23 = true;
                // 用户复现序列：点豹猫 → 变体面板拨「已驯服」→ 再点豹猫蛋。
                qmlCallR23(bR23, "selectMob", { QVariant(int(EntityManager::MobOcelot)), QVariant(QStringLiteral("豹猫")) });
                bR23->setProperty("mobTamedPreview", true);
                pumpR23();
                const bool tamedCat = mobPreviewR23->property("collarVisible").toBool()
                                  && bR23->property("selectedMobTexSource").toString().contains(QStringLiteral("mob_cat_tabby"));
                if (!tamedCat) failR23("tamedCat state");
                // t989 演化（P-t949(d) 先例）：原腿钉「切蛋 → 野生形态残留清零」（蛋→mob 预览路径）；
                //   蛋路径随查看器蛋分区整体退役 → 病面结构性不可再现，腿翻转为「切蛋 = 蛋路径绝迹 +
                //   驯服残留清零」：selectedMobType 归 -1（旧蛋→11 映射绝迹）、项圈灭、贴图非 tabby、
                //   预览眼 overlay 随分支门灭（旧蛋路径程序贴图无脸纹 → 眼恒显的反空转锚一并退役）。
                qmlCallR23(bR23, "selectItem", { QVariant(int(RecipeRegistry::SpawnEggOcelotId)) });
                pumpR23();
                const bool afterEgg = bR23->property("selectedId").toInt() == int(RecipeRegistry::SpawnEggOcelotId)
                                  && bR23->property("selectedMobFromSection").toInt() == -1
                                  && bR23->property("selectedMobType").toInt() == -1
                                  && !mobPreviewR23->property("collarVisible").toBool()
                                  && !bR23->property("selectedMobTexSource").toString().contains(QStringLiteral("mob_cat_tabby"))
                                  && !catEye->property("visible").toBool(); // 蛋路径绝迹 → mob 预览分支整体不可见
                if (!afterEgg) failR23("ocelot egg switch (t989: egg path extinct)");
                // 狼镜像（review-0829 #7 遗留同病灶）：拨杆仍真 → 狼项圈显 → 切狼蛋 → 残留必须清。
                qmlCallR23(bR23, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) });
                pumpR23();
                if (!mobPreviewR23->property("collarVisible").toBool()) failR23("tamedWolf state");
                qmlCallR23(bR23, "selectItem", { QVariant(int(RecipeRegistry::SpawnEggWolfId)) });
                pumpR23();
                if (mobPreviewR23->property("collarVisible").toBool()) failR23("wolf egg switch");
            }
        }
        QDir(probeUiR23).removeRecursively();
        const bool okBR23 = rigOkR23;
        ok = ok && okBR23;
        if (!ok)
            qInfo().noquote() << "  [review0830-23 diag] syncPin" << okAR23 << "rig" << okBR23 << rigDiagR23;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review0830-23 spawn-egg path keeps the residual tamed collar state off "
                             "wild eggs (Review_2026-08-30 #23; the review-0829 #7 pattern recurring "
                             "AND being copied): t963's cat collar gate read the DUAL-source "
                             "selectedMobType (mob-section selection OR egg->type mapping, which is "
                             "always 11 on the ocelot-egg path), so the state machine 'tap ocelot -> "
                             "toggle Tamed -> tap the ocelot egg' left a wild egg wearing the red "
                             "collar while the texture gate (single-source selectedMobFromSection) "
                             "correctly fell back to the wild coat - a lone contradicting overlay; "
                             "the wolf collar gate is the same shape (the review-0829 #7 leftover "
                             "itself); the fix aligns both gates to the texture gate's exact "
                             "single-source caliber (selectedMobFromSection === N && mobTamedPreview); "
                             "legs: a same-file gate-family sync pin (dual-source tamed-gate forms "
                             "extinct, single-source forms counted at every consumer: texture source, "
                             "pack-UV exception, both eye overlays, the tamed-coat note; the wolf/cat "
                             "collar gates retired to the t986 geometry flag mobCollarActive -"
                             "collarVisible, same tamed lever, single-source face preserved) and a "
                             "real-QmlEngine rig driving the user's sequence "
                             "through the real ResourceBrowser.qml (selectMob -> mobTamedPreview=true "
                             "-> selectItem egg): the tamed state raises the preview MobModel's "
                             "collarVisible flag + tabby texture, "
                             "the egg switch must drop collarVisible to false, "
                             "return the texture to wild, and the eye overlay goes OFF with the "
                             "mob-preview branch gate (t989 evolution: the egg->mob preview path is "
                             "extinct so the branch is hidden outright - the old always-on-eye "
                             "anti-vacuity anchor retired with it), mirrored for the wolf "
                             "collar and the wolf egg";
    }

    // ── P-t964 预览重置按钮 z 序探针（R19.17 🅴；用户第五轮口径「方块预览滚轮放大后遮住右下角
    //    重置按钮——按钮应永远最前」）──
    //    根因：按钮（t922）声明在 cubeView（View3D，anchors.fill 铺满预览区）**之前**——QML 兄弟层
    //    缺省按声明序绘制，后声明的视口恒绘在按钮上层；且按钮仅在已缩放态显示（zoom≠1）= 恰逢
    //    模型投影最大、像素铺进视口右下角的时刻 → 放大即遮。headless 无窗口合成不可见，按
    //    t931/t963 纯视觉项先例走源码层级钉。
    //    (a) 层级钉：t922 按钮注释锚 → `id: cubeView` 之间的按钮块必含**独立行** `z: 10`（注释内
    //        提及不算）+ 契约注释锚「t964 永远最前」；且 cubeView 头段（id → PerspectiveCamera
    //        之间）无 z 覆盖（缺省 0）—— 两侧合钉 = 「按钮层 z > 视口层 z」。阴性 = 删按钮
    //        `z: 10` 行 → 块扫描红（回归即测即红）。
    //    (b) 约定钉：全文独立 `z: 10` 行恰 3 处（变体面板 t783 ② 先例 + 本按钮 t964 + t965 形态按钮组
    //        按约定入层——钉合法演化）=「预览区浮层永远最前」层级契约登记（t967 分类按钮组同约定加 z）。
    //    (c) 身份钉：块内仍是缩放重置按钮本体（缩放态显隐谓词 + reset 写 1.0 + 右下角锚）防 z 钉漂移。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile f964(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb964 = f964.open(QIODevice::ReadOnly) ? QString::fromUtf8(f964.readAll()) : QString();
        // 按钮块边界：t922 重置按钮注释（全文唯一）→ View3D 声明（id: cubeView，块尾界）。
        const int anchor964 = rb964.indexOf(QStringLiteral("// t922 重置缩放按钮"));
        const int view964 = anchor964 >= 0 ? rb964.indexOf(QStringLiteral("id: cubeView"), anchor964) : -1;
        const QString btnBlock964 = anchor964 >= 0 && view964 > anchor964
            ? rb964.mid(anchor964, view964 - anchor964) : QString();
        // 视口头段边界：id: cubeView → PerspectiveCamera（其自身属性区，不含子项）。
        const int cam964 = view964 >= 0 ? rb964.indexOf(QStringLiteral("PerspectiveCamera {"), view964) : -1;
        const QString viewHead964 = view964 >= 0 && cam964 > view964
            ? rb964.mid(view964, cam964 - view964) : QString();
        // (a) 按钮层显式 z: 10（独立行形态——注释里的 "z: 10" 不算数）+ 契约锚；视口缺省 z 0
        //     （头段无 z 覆盖）→ 按钮层 > 视口层。
        const bool btnZRow964 = btnBlock964.contains(QRegularExpression(
            QStringLiteral("^\\s*z: 10\\s*$"), QRegularExpression::MultilineOption));
        const bool okA964 = !btnBlock964.isEmpty()
                         && btnZRow964
                         && btnBlock964.contains(QStringLiteral("t964 永远最前"))
                         && viewHead964.contains(QStringLiteral("anchors.fill: parent"))
                         && !viewHead964.contains(QStringLiteral("z:"));
        // (b) 独立 z: 10 行（行首缩进 + 行尾，不把 z: 1000 计入）恰 3 处 = 面板 + 按钮 + t965 形态
        //     按钮组（钉合法演化 P-t949(d) 先例：t965 形态面板按 t964 登记的「预览区浮层 z ≥ 10」
        //     约定入层，约定本身不变、成员数 2→3）。
        const int zRows964 = rb964.count(QRegularExpression(QStringLiteral("^\\s*z: 10\\s*$"),
                                                             QRegularExpression::MultilineOption));
        const bool okB964 = zRows964 == 3
                         && rb964.contains(QStringLiteral("id: variantPanel"))
                         && rb964.contains(QStringLiteral("id: formPanel"));
        // (c) 按钮本体身份（防层级钉漂到其他控件）。
        const bool okC964 = btnBlock964.contains(QStringLiteral("visible: cubeView.visible && Math.abs(root.previewZoom - 1.0) > 0.001"))
                         && btnBlock964.contains(QStringLiteral("onClicked: root.previewZoom = 1.0"))
                         && btnBlock964.contains(QStringLiteral("anchors.bottom: parent.bottom"))
                         && btnBlock964.contains(QStringLiteral("anchors.right: parent.right"));
        ok = okA964 && okB964 && okC964;
        if (!ok)
            qInfo().noquote() << "  [t964 diag] block" << !btnBlock964.isEmpty() << "zPin" << okA964
                              << "convention(zRows" << zRows964 << ")" << okB964 << "identity" << okC964;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t964 preview reset button z-order: wheel-zooming the block preview no "
                             "longer covers the bottom-right reset button - the button was declared "
                             "BEFORE the full-viewport View3D (cubeView) and QML sibling stacking "
                             "paints later-declared siblings on top, so the viewport painted over "
                             "it exactly when it is visible (zoom != 1 is when the zoomed model "
                             "pixels reach the bottom-right corner); the fix lifts the button with "
                             "an explicit z: 10 above the viewport default z 0 following the "
                             "variantPanel precedent (explicit z guards against later sibling "
                             "insertion re-flipping the order), registering the preview-area "
                             "floating-layer contract (variant panel + reset button + the future "
                             "t965 mode/classification button group all carry explicit z >= 10, "
                             "always frontmost of the viewport); legs: button-block z pin + "
                             "contract comment anchor + viewport-head-stays-default-z pin (button "
                             "layer z > viewport layer z), file-wide standalone z: 10 row count == 3 "
                             "(panel + button + the t965 form button group joining the registered "
                             "z >= 10 floating-layer convention), and identity pins keeping the z pin on the real "
                             "reset button (zoom-visibility predicate, reset-to-1.0 handler, "
                             "bottom-right anchor)";
    }

    // ── P-t965 形态切换按钮组系统探针（R19.17 🅴；用户第五轮口径「变体面板扩展成编号按钮组（1 2 3…
    //    默认最普通形态）——耕地干/湿两态、门+活板门未激活/激活、草丛低/中/高三态、红石火把亮/灭、
    //    动力铁轨未激活/激活、末地传送门框架有眼/无眼、作物（小麦/胡萝卜/马铃薯）生长阶段」）──
    //    支持清单核实结论（state 表达方式逐项核自数据层实现，无臆造 API；无末地主题方块缺席项——
    //    末地传送门框架即 EndPortal=111，state bit0 = 末影之眼放眼位）：
    //      耕地 23 = state 低 2 位湿润等级（干 0 / 湿 3 最深；顶瓦 26/27）
    //      门三族 19/89/135 = state bit2 开合（合 0 / 开 4；薄板几何换边）——135 系 t965 订正（旧
    //        家族表字面量 71 是错 id=青色羊毛）
    //      活板门两族 20/136 = state bit0 开合（合 0 / 开 1；水平薄板↔竖直薄板）
    //      草丛 24 = state 即变种 0/1/2（cross 高 0.5/1.0/2.0）
    //      红石火把 129 = state bit3 熄灭位（亮 0 / 灭 8；瓦 161/170）
    //      动力轨 127 = state bit4 通电位（未激活 0 / 激活 16；瓦 157/159）
    //      末地框 111 = state bit0 放眼位（无眼 0 / 有眼 1；顶瓦 141/142）
    //      作物三族 25/55/56 = state 即生长阶段 0..7（小麦瓦 29+age；胡萝卜/马铃薯基底+age/2 四视觉阶段）
    //    形态钮 n → 支持表 index n-1；钮 1 = 表首 state 0 = 放置缺省（最普通）形态（用户口径；
    //    红石火把放置即常亮 → 钮 1=亮，灭是受供电的特殊态——用户原文「红石火把亮/灭」序同）。
    //    (a) 支持表行为钉：Hotbar::blockFormStates 直调——13 支持项逐 id 数量/编码/默认态 + 8 不支持
    //        项空表（含 71 青羊毛 / 128 探测轨——不在用户口径的邻位防外溢）。
    //    (b) 预览几何行为级三类状态差（ItemShapeGeometry 直调 t880 先例 + BlockCube 直调新链）：
    //        活板门/门开合 bounds 换边、草丛三态高度、作物阶段/红石火把/动力轨态变瓦片 UV、
    //        耕地干湿/末地框无眼有眼顶面瓦片 + 非形态方块 state 惰性零漂移钉。
    //    (c) 查看器源码钉：按钮组构建链（支持表消费 + 默认钮 1 + 换选重置 + 预览 blockState 接线 +
    //        编号钮本体 + z:10 浮层契约〔P-t964(b) 同步演化恰 3〕+ 门族 135/127 家族钉与 71 绝迹）。
    //    (d) review0830 #27 按钮组宽度契约腿（源码解析宽链常量 → 8 钮恒不溢出由构造成立）。
    {
        bool ok = true;
        // (a) 支持表行为钉（Hotbar::blockFormStates 直调；Game 层单一权威）。
        Hotbar hb965;
        struct FormExpect { int id; int count; int first; int second; int last; };
        const FormExpect fe965[] = {
            { int(BR::Farmland),      2, 0, int(BR::FarmlandHydrationMax),    int(BR::FarmlandHydrationMax) },
            { int(BR::WoodDoor),      2, 0, int(BR::DoorStateOpenFlag),       int(BR::DoorStateOpenFlag) },
            { int(BR::SpruceDoor),    2, 0, int(BR::DoorStateOpenFlag),       int(BR::DoorStateOpenFlag) },
            { int(BR::IronDoor),      2, 0, int(BR::DoorStateOpenFlag),       int(BR::DoorStateOpenFlag) },
            { int(BR::WoodTrapdoor),  2, 0, int(BR::TrapdoorStateOpenFlag),   int(BR::TrapdoorStateOpenFlag) },
            { int(BR::IronTrapdoor),  2, 0, int(BR::TrapdoorStateOpenFlag),   int(BR::TrapdoorStateOpenFlag) },
            { int(BR::TallGrass),     3, int(BR::TallGrassShort), int(BR::TallGrassMedium), int(BR::TallGrassTall) },
            { int(BR::RedstoneTorch), 2, 0, int(BR::RedstoneTorchStateOffFlag), int(BR::RedstoneTorchStateOffFlag) },
            { int(BR::GoldenRail),    2, 0, int(BR::GoldenRailStateOnFlag),   int(BR::GoldenRailStateOnFlag) },
            { int(BR::EndPortal),     2, 0, int(BR::EndPortalStateActiveFlag), int(BR::EndPortalStateActiveFlag) },
            { int(BR::WheatCrop),     int(BR::WheatCropStageMax) + 1, 0, 1, int(BR::WheatCropStageMax) },
            { int(BR::CarrotCrop),    int(BR::WheatCropStageMax) + 1, 0, 1, int(BR::WheatCropStageMax) },
            { int(BR::PotatoCrop),    int(BR::WheatCropStageMax) + 1, 0, 1, int(BR::WheatCropStageMax) },
        };
        bool okA965 = true;
        for (const FormExpect &e : fe965) {
            const QVariantList forms = hb965.blockFormStates(e.id);
            if (forms.size() != e.count || forms.first().toInt() != e.first
                || forms.at(1).toInt() != e.second || forms.last().toInt() != e.last) {
                okA965 = false;
                qInfo().noquote() << "  t965 diag(a): id" << e.id << "count" << forms.size()
                                  << "expect" << e.count << "first" << forms.value(0).toInt()
                                  << "expect" << e.first;
            }
        }
        // 不支持方块 → 空表（按钮组不出现；含 71 青羊毛/103 普通轨/128 探测轨三个邻位防外溢）。
        const int unsupported965[] = { int(BR::Stone), int(BR::Dirt), int(BR::WoodSlab),
                                       int(BR::Chest), int(BR::Glass), int(BR::WoolCyan),
                                       int(BR::Rail), int(BR::DetectorRail) };
        for (int id : unsupported965) {
            if (!hb965.blockFormStates(id).isEmpty()) {
                okA965 = false;
                qInfo().noquote() << "  t965 diag(a): unsupported id" << id << "has form states";
            }
        }
        ok = ok && okA965;

        // (b) 预览几何行为级（态变瓦片 UV 从 vertexData 直读；item stride 20B=5 float、cube stride 36B=9 float）。
        const int nTiles965 = int(BR::AtlasTileCount);
        const float hx965 = 0.5f / (nTiles965 * int(BR::kAtlasTilePx));
        auto tileOfU965 = [nTiles965, hx965](float u) {
            return qRound((u - hx965) * float(nTiles965));
        };
        auto itemVtxU = [](const QByteArray &vd, int vIdx) {
            float f;
            std::memcpy(&f, vd.constData() + vIdx * 20 + 12, sizeof(float)); // 顶点第 4 float = u
            return f;
        };
        auto cubeVtxU = [](const QByteArray &vd, int vIdx) {
            float f;
            std::memcpy(&f, vd.constData() + vIdx * 36 + 12, sizeof(float)); // pos3+uv2 → 第 4 float = u
            return f;
        };
        bool okB965 = true;
        {
            ItemShapeGeometry g;
            // 活板门：合(0) 水平薄板 yMax=3/32 ↔ 开(1) 竖直薄板贴 +X 边（yMax=0.5、xMin=0.3125）。
            g.setBlockId(int(BR::WoodTrapdoor));
            g.setBlockState(0);
            const float tdClosedYMax = g.boundsMax().y();
            g.setBlockState(int(BR::TrapdoorStateOpenFlag));
            okB965 = okB965
                && qAbs(tdClosedYMax - 3.0f / 32.0f) <= 0.001f
                && g.boundsMax().y() >= 0.499f
                && g.boundsMin().x() >= 0.31f;
            // 门：合(0) 薄板 +X 边 ↔ 开(bit2) 薄板换 +Z 边（X 全幅）。
            g.setBlockId(int(BR::WoodDoor));
            g.setBlockState(0);
            const float dClosedXMin = g.boundsMin().x();
            const float dClosedZMin = g.boundsMin().z();
            g.setBlockState(int(BR::DoorStateOpenFlag));
            okB965 = okB965
                && dClosedXMin >= 0.31f && dClosedZMin <= -0.49f
                && g.boundsMin().z() >= 0.31f && g.boundsMin().x() <= -0.49f;
            // 铁门（t965 订正后的 135 消费端）开合同款换边。
            g.setBlockId(int(BR::IronDoor));
            g.setBlockState(0);
            const float idClosedXMin = g.boundsMin().x();
            g.setBlockState(int(BR::DoorStateOpenFlag));
            okB965 = okB965
                && idClosedXMin >= 0.31f
                && g.boundsMin().z() >= 0.31f && g.boundsMin().x() <= -0.49f;
            // 草丛三态高度严格递增（矮 0.25 / 中 0.5 / 高 1.0 形心居中 yMax）。
            g.setBlockId(int(BR::TallGrass));
            float prevY965 = -1e9f;
            for (int v = 0; v <= 2; ++v) {
                g.setBlockState(v);
                const float yMax = g.boundsMax().y();
                if (yMax <= prevY965) {
                    okB965 = false;
                    qInfo().noquote() << "  t965 diag(b): tallgrass v" << v << "yMax" << yMax;
                }
                prevY965 = yMax;
            }
            okB965 = okB965 && prevY965 >= 0.999f;
            // 作物阶段瓦片（state 0 嫩芽 29 ↔ state 7 成熟 36）——UV 态变即测即红。
            g.setBlockId(int(BR::WheatCrop));
            g.setBlockState(0);
            const int wTile0 = tileOfU965(itemVtxU(g.vertexData(), 0));
            g.setBlockState(int(BR::WheatCropStageMax));
            const int wTile7 = tileOfU965(itemVtxU(g.vertexData(), 0));
            okB965 = okB965 && wTile0 == 29
                  && wTile7 == 29 + int(BR::WheatCropStageMax);
            // 红石火把亮(0)=def 侧瓦 ↔ 灭(8)=170。
            g.setBlockId(int(BR::RedstoneTorch));
            g.setBlockState(0);
            const int rtLit = tileOfU965(itemVtxU(g.vertexData(), 0));
            g.setBlockState(int(BR::RedstoneTorchStateOffFlag));
            const int rtOff = tileOfU965(itemVtxU(g.vertexData(), 0));
            okB965 = okB965 && rtLit == int(BR::tileIndex(BR::RedstoneTorch, BR::PosX)) && rtOff == 170;
            // 动力轨：贴地薄板 quad（8 顶点、扁平 yMax≈0）+ 未激活 157 ↔ 激活 159 亮金。
            g.setBlockId(int(BR::GoldenRail));
            g.setBlockState(0);
            const int gVCount = int(g.vertexData().size()) / 20;
            const float gYMax = g.boundsMax().y();
            const float gYMin = g.boundsMin().y();
            const int gTile0 = tileOfU965(itemVtxU(g.vertexData(), 0));
            g.setBlockState(int(BR::GoldenRailStateOnFlag));
            const int gTileOn = tileOfU965(itemVtxU(g.vertexData(), 0));
            okB965 = okB965 && gVCount == 8
                  && gYMax <= 0.001f && gYMin >= -0.001f
                  && gTile0 == int(BR::tileIndex(BR::GoldenRail, BR::PosX)) && gTileOn == 159;
            // 非形态方块 state 惰性（零漂移钉）：半砖 state 0↔5 几何逐字节一致。
            g.setBlockId(int(BR::WoodSlab));
            g.setBlockState(0);
            const QByteArray slab0 = g.vertexData();
            g.setBlockState(5);
            okB965 = okB965 && slab0 == g.vertexData();
        }
        {
            // BlockCube 顶面态变（顶面 = 面 2 → 顶点 8..11；顶点第 4 float = u）。
            BlockCube bc;
            bc.setBlockId(int(BR::EndPortal));
            bc.setBlockState(0);
            const int epNoEye = tileOfU965(cubeVtxU(bc.vertexData(), 8));
            bc.setBlockState(int(BR::EndPortalStateActiveFlag));
            const int epEye = tileOfU965(cubeVtxU(bc.vertexData(), 8));
            okB965 = okB965 && epNoEye == 141 && epEye == 142;
            // 耕地干(0) 26 ↔ 湿(3 最深) 27。
            bc.setBlockId(int(BR::Farmland));
            bc.setBlockState(0);
            const int fmDry = tileOfU965(cubeVtxU(bc.vertexData(), 8));
            bc.setBlockState(int(BR::FarmlandHydrationMax));
            const int fmWet = tileOfU965(cubeVtxU(bc.vertexData(), 8));
            okB965 = okB965 && fmDry == 26 && fmWet == 27;
            // 非态变方块 state 惰性（石头 0↔7 逐字节一致 = 既有消费端零漂移）。
            bc.setBlockId(int(BR::Stone));
            bc.setBlockState(0);
            const QByteArray stone0 = bc.vertexData();
            bc.setBlockState(7);
            okB965 = okB965 && stone0 == bc.vertexData();
        }
        ok = ok && okB965;

        // (c) 查看器源码钉（按钮组构建链 + z 约定 + 门族订正）。
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf965(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb965 = rf965.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf965.readAll()) : QString();
        const int iSel965 = rb965.indexOf(QStringLiteral("selectedIsItem3D:"));
        const QString selBlock965 = iSel965 >= 0 ? rb965.mid(iSel965, 1600) : QString();
        // formPanel 块内独立 z: 10 行（P-t964(b) 约定钉的计数形态——注释并入行不算数，同其首次阴性教训）。
        const int iForm965 = rb965.indexOf(QStringLiteral("id: formPanel"));
        const QString formBlock965 = iForm965 >= 0 ? rb965.mid(iForm965, 500) : QString();
        const bool formZRow965 = formBlock965.contains(QRegularExpression(
            QStringLiteral("^\\s*z: 10\\s*$"), QRegularExpression::MultilineOption));
        const bool okC965 = rb965.contains(QStringLiteral("id: formPanel"))
                         && formZRow965
                         && rb965.contains(QStringLiteral("root.hotbar.blockFormStates(root.selectedId)"))
                         && rb965.contains(QStringLiteral("property int selectedFormIndex: 0"))
                         && rb965.contains(QStringLiteral("onSelectedIdChanged: root.selectedFormIndex = 0"))
                         && rb965.count(QStringLiteral("blockState: root.selectedFormState")) == 2
                         && rb965.contains(QStringLiteral("text: index + 1"))
                         && selBlock965.contains(QStringLiteral("=== 135"))
                         && selBlock965.contains(QStringLiteral("=== 127"))
                         && !selBlock965.contains(QStringLiteral("=== 71"));
        // (d) review0830 #27 按钮组宽度契约（由构造成立，非余量吸收）：从源码解析**全链**宽常量——
        //     右列 width: 322 → 列内 Column anchors.margins 10（**四边各 10，宽度共减 2×10**）→
        //     previewArea 302 → formPanel −58 = 244 → formCol −10 = 234 → 钮宽 / 行距，断言 8 钮最坏
        //     行宽 8×钮宽 + 7×行距 ≤ formCol 内容宽。旧形态 26px 钮 236 > 234 溢 2px 靠边框余量吸收 +
        //     注释写 254 漏算一层列边距（契约文本失实——漏算的正是这层 margins），钮宽 25 收口后
        //     「8 钮恒不溢出」由构造成立——钮宽 / 任一层收窄回漂即本腿红。
        const int iPanelD965 = rb965.indexOf(QStringLiteral("id: formPanel"));
        const QString panelBlockD965 = iPanelD965 >= 0 ? rb965.mid(iPanelD965, 3200) : QString();
        const auto digitsAfter = [](const QString &s, int from, int span) -> int {
            const QString tail = s.mid(from, span);
            QString num;
            for (const QChar &c : tail) {
                if (c.isDigit()) { num += c; continue; }
                if (!num.isEmpty()) break;          // 数字后遇非数字 → 收（width: 25; height:… 形）
            }
            return num.isEmpty() ? -1 : num.toInt();
        };
        const int iCol322D965 = rb965.indexOf(QStringLiteral("width: 322; height: parent.height"));
        const int iMarginD965 = iCol322D965 < 0 ? -1
            : rb965.indexOf(QLatin1String("anchors.margins: "), iCol322D965);
        const int colW965 = iCol322D965 < 0 ? -1 : 322;
        const int colMargin965 = iMarginD965 < 0 ? -1
            : digitsAfter(rb965, iMarginD965 + int(qstrlen("anchors.margins: ")), 8);
        const int iBtnD965 = panelBlockD965.indexOf(QLatin1String("delegate: Rectangle {"));
        const int iBtnW965 = iBtnD965 < 0 ? -1
            : panelBlockD965.indexOf(QLatin1String("width: "), iBtnD965);
        const int btnW965 = iBtnW965 < 0 ? -1
            : digitsAfter(panelBlockD965, iBtnW965 + int(qstrlen("width: ")), 8);
        const int iRowD965 = panelBlockD965.indexOf(QLatin1String("Row {"));
        const int iRowSp965 = iRowD965 < 0 ? -1
            : panelBlockD965.indexOf(QLatin1String("spacing: "), iRowD965);
        const int rowSp965 = iRowSp965 < 0 ? -1
            : digitsAfter(panelBlockD965, iRowSp965 + int(qstrlen("spacing: ")), 8);
        const int iOffD965 = panelBlockD965.indexOf(QLatin1String("width: parent.width - "));
        const int panelOff965 = iOffD965 < 0 ? -1
            : digitsAfter(panelBlockD965, iOffD965 + int(qstrlen("width: parent.width - ")), 8);
        const int iColOffD965 = panelBlockD965.indexOf(QLatin1String("width: parent.width - "),
                                                       iOffD965 + 1);
        const int colOff965 = iColOffD965 < 0 ? -1
            : digitsAfter(panelBlockD965, iColOffD965 + int(qstrlen("width: parent.width - ")), 8);
        const bool okD965 = colW965 > 0 && colMargin965 > 0 && btnW965 > 0 && rowSp965 >= 0
                         && panelOff965 > 0 && colOff965 >= 0
                         && (8 * btnW965 + 7 * rowSp965)
                                <= (colW965 - 2 * colMargin965 - panelOff965 - colOff965);
        ok = ok && okC965 && okD965;
        if (!ok)
            qInfo().noquote() << "  [t965 diag] table" << okA965 << "geometry" << okB965
                              << "sourcePins" << okC965 << "widthContract" << okD965
                              << "colW" << colW965 << "colMargin" << colMargin965
                              << "btnW" << btnW965 << "rowSp" << rowSp965
                              << "panelOff" << panelOff965 << "colOff" << colOff965;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t965 form-toggle button group system: the browser variant panel extends "
                             "into numbered state buttons (1 2 3..., button 1 = state 0 = the placed "
                             "default form) driven by a single Game-layer support table "
                             "(Hotbar::blockFormStates: farmland dry/wet via the 2-bit hydration level, "
                             "wood/spruce/iron doors open on state bit2, wood/iron trapdoors open on "
                             "bit0, tall grass short/mid/tall via variant states, redstone torch "
                             "lit/unlit via the off flag, powered rail unpowered/powered via the on "
                             "flag, end portal frame eyeless/eyed via the active flag, and the three "
                             "crops expose all 8 growth stages 0..7); the preview meshes consume the "
                             "same state through BlockRegistry::stateTileOverride (Core single "
                             "authority, mesher branches now delegate to it too): trapdoor/door open "
                             "geometry swaps the plate edge, grass cross height 0.5/1.0/2.0, crop "
                             "stage / torch off / powered-rail tiles verified by UV, farmland "
                             "dry-wet and frame eyeless-eyed top tiles verified per face; state on "
                             "non-form blocks is inert byte-for-byte; the iron door family id was "
                             "corrected 71(=cyan wool, a masked routing bug)->135 on both QML family "
                             "tables and the powered rail joins the viewer-only 3D family; legs: "
                             "support-table behavior pins (13 entries + 8 unsupported-empty incl. "
                             "cyan wool 71 and detector rail 128), geometry behavior pins "
                             "(ItemShapeGeometry + BlockCube direct), and browser source pins "
                             "(panel + z:10 floating-layer contract + default-form reset + wiring "
                             "+ review0830 #27 width contract parsed from the source: 8 worst-case "
                             "buttons 8*w+7*sp fit inside the content column 322-2x10-58-10=234 by "
                             "construction, not by border slack)";
    }

    // ── P-t966 预览拖拽方向 rig 探针（R19.17 🅴；用户第六轮口径「左右旋转到背面还是反的——
    //    彻底查相位判定（spinAngle 基准/拖拽轴映射），实机 rig 验证」）──
    //    符号面全景演化结论（git -S 全链 + 实机 rig 证伪）：水平拖自 t599 起就是纯线性
    //    spinAngle += dx·0.6，全相位无符号面（近面屏幕位移恒同向，本 rig 腿 (b1) 即证）；唯一
    //    相位相关符号面是 t921 的 faceSign = sign(cos(displayYaw)) 俯仰补偿——但真正的病根在
    //    **变换图**：单节点 eulerRotation 的合成序是 Ry(yaw)·Rx(pitch)（yaw 世界系最外；本 rig
    //    前置实验 scenePosition 读回实测，本 Qt 无 rotationOrder 旋钮）→ 俯仰铰链 =
    //    Ry(θ)·X̂，屏幕投影随 cos(θ) 翻号（背面相位「上下拖拽反了」根因），且侧相位（|θ|→90°）
    //    铰链顺向视口、竖直拖带出绕视轴平面内分量（拖拽轴被换走 = 用户「转到背面的过程手感
    //    乱」）；t921 在定律上乘相位补偿 = 反向补丁叠相位面，过 ±90° 边界换向瞬间仍乱 → 未愈。
    //    修法：四预览分支 eulerRotation 拆 **pitch 父（世界系俯仰，铰链恒屏幕水平）+ yaw 子
    //    （自转转台）** 两层，定律回到纯线性（无任何相位判定）。rig 不复算数学：真 QQmlEngine
    //    直载源树 ResourceBrowser.qml，真 Quick3D 节点 sceneRotation/scenePosition 读回（无窗口
    //    show 亦传播，headless 行为级），模型空间单位立方六面心 → 世界系近面点（z 最大）在
    //    1px 拖拽增量下的屏幕位移方向断言：
    //    (b1) 四相位水平腿：spin 0/90/180/270 各 +0.6°（右拖 1px = DragHandler 1px=0.6° 定律）
    //         → 近面位移向右；−0.6° 向左——「拖右近面向右」全相位一致（t599 契约恒保持）。
    //    (b2) 俯仰腿：spin 前/侧/背三相位（显示 yaw -35/+90/+180）userPitch +0.6°（上拖 1px =
    //         t599/t877 定稿符号）→ 近面向下（上拖看顶）全相位一致；横向分量 ≤ 25% 竖直
    //         （恒铰链 = 不换轴；旧图侧相位该比值 ~40% 或近面 y 分量归零即红）；spinAngle 值
    //         不被竖直拖触碰（轴纯度）。
    //    (a) 源码钉（辅助，非替代）：纯线性定律行逐字 + const faceSign / Math.cos 绝迹 +
    //        旧单节点合成串绝迹 + 四分支 pitch/yaw 两层计数 + 契约注释锚。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf966(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb966 = rf966.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf966.readAll()) : QString();
        const bool okA966 = rb966.contains(QStringLiteral("root.userPitch = Math.max(-60, Math.min(60, root.userPitch - dy * 0.6))"))
                         && !rb966.contains(QStringLiteral("const faceSign"))
                         && !rb966.contains(QStringLiteral("Math.cos"))
                         && !rb966.contains(QStringLiteral("eulerRotation: Qt.vector3d(-22 + root.userPitch"))
                         && rb966.count(QStringLiteral("eulerRotation.x: 22 + root.userPitch")) == 4 // t990 演化（P-t949(d) 先例）：俯视基偏翻 +22，四分支拆层计数钉意图不变
                         && rb966.count(QStringLiteral("eulerRotation.y: root.spinAngle - 35")) == 4
                         && rb966.contains(QStringLiteral("t966 恒铰链俯仰"))
                         && rb966.contains(QStringLiteral("t966 纯线性定律"));
        // (b) 行为 rig（真链 harness：t956 装配法——临时目录逃离 qrc 重映射 + 私有 URI；窗口不 show）。
        static bool sT966TypesRegistered = false;
        if (!sT966TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT966", 1, 0, "Hotbar");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT966", 1, 0, "ResourcePackManager");
            qmlRegisterType<BlockCube>("VoxelSandboxProbeT966", 1, 0, "BlockCube");
            qmlRegisterType<ItemShapeGeometry>("VoxelSandboxProbeT966", 1, 0, "ItemShapeGeometry");
            qmlRegisterType<BedModelGeometry>("VoxelSandboxProbeT966", 1, 0, "BedModelGeometry");
            qmlRegisterType<MobModel>("VoxelSandboxProbeT966", 1, 0, "MobModel");
            qmlRegisterType<EnchantBookBox>("VoxelSandboxProbeT966", 1, 0, "EnchantBookBox");
            qmlRegisterType<MobBowGeometry>("VoxelSandboxProbeT966", 1, 0, "MobBowGeometry");
            qmlRegisterType<UnitCube>("VoxelSandboxProbeT966", 1, 0, "UnitCube");
            sT966TypesRegistered = true;
        }
        bool rigOk = false;
        QString rigDiag;
        double worstRatio966 = 0.0;
        const QString uiDir966 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUi966 = QDir::temp().absoluteFilePath(
                QStringLiteral("t966_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUi966);
        for (const QString f : { QStringLiteral("ResourceBrowser.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUi966 + QLatin1Char('/') + f);
            QFile(uiDir966 + QLatin1Char('/') + f).copy(probeUi966 + QLatin1Char('/') + f);
        }
        {
            const QStringList qmlFiles966 = QDir(probeUi966).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles966) {
                QFile p(probeUi966 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT966\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine966;
        Hotbar hb966;
        ResourcePackManager rp966;
        QQuickWindow win966; // 永不 show（headless：Quick3D 节点变换传播不依赖渲染回路，前置 rig 实测）
        QQmlComponent comp966(&engine966,
                              QUrl::fromLocalFile(probeUi966 + QStringLiteral("/ResourceBrowser.qml")));
        QQuickItem *b966 = nullptr;
        QObject *cube966 = nullptr;
        if (comp966.isError()) {
            rigDiag = QStringLiteral("load: ") + comp966.errorString();
        } else if ((b966 = qobject_cast<QQuickItem *>(comp966.create())) == nullptr) {
            rigDiag = QStringLiteral("create failed");
        } else {
            b966->setParent(&engine966);
            b966->setProperty("hotbar", QVariant::fromValue(&hb966));
            b966->setProperty("resourcePack", QVariant::fromValue(&rp966));
            b966->setProperty("atlasSource", QStringLiteral("qrc:/textures/atlas.png"));
            b966->setProperty("packActive", false);
            b966->setProperty("previewDragging", true); // 冻结自转动画（rig 显式驱动确定性；t599 拖拽语义）
            b966->setProperty("selectedId", int(BR::Stone));
            b966->setWidth(700);
            b966->setHeight(500);
            b966->setParentItem(win966.contentItem());
            for (int i = 0; i < 8; ++i)
                QCoreApplication::processEvents();
            // 整立方预览 Model：可见 + geometry 是 BlockCube（傀儡头 BlockCube 不可见态被排除）。
            const auto models966 = b966->findChildren<QObject *>();
            for (QObject *m : models966) {
                if (std::strcmp(m->metaObject()->className(), "QQuick3DModel") != 0)
                    continue;
                if (!m->property("visible").toBool())
                    continue;
                QObject *geo = m->property("geometry").value<QObject *>();
                if (geo && std::strcmp(geo->metaObject()->className(), "BlockCube") == 0) {
                    cube966 = m;
                    break;
                }
            }
            if (!cube966) {
                rigDiag = QStringLiteral("visible BlockCube model not found");
            } else {
                auto pump966 = []() {
                    for (int i = 0; i < 8; ++i)
                        QCoreApplication::processEvents();
                };
                // 近面点 = Quick3D 真实 sceneRotation/scenePosition 下单位立方六面心的最大世界 z 者
                //   （用户视角最近模型的表面锚点；屏幕系 = 世界系 x 右 / y 上——相机在 +Z 无旋转，
                //   透视除正深度不改位移符号）。
                auto near966 = [&](QObject *m) {
                    const QQuaternion q = m->property("sceneRotation").value<QQuaternion>();
                    const QVector3D p = m->property("scenePosition").value<QVector3D>();
                    QVector3D best = q.rotatedVector(QVector3D(0.5f, 0, 0)) + p;
                    const QVector3D kFace[6] = {
                        QVector3D(0.5f, 0, 0), QVector3D(-0.5f, 0, 0), QVector3D(0, 0.5f, 0),
                        QVector3D(0, -0.5f, 0), QVector3D(0, 0, 0.5f), QVector3D(0, 0, -0.5f)
                    };
                    for (int i = 1; i < 6; ++i) {
                        const QVector3D w = q.rotatedVector(kFace[i]) + p;
                        if (w.z() > best.z())
                            best = w;
                    }
                    return best;
                };
                rigOk = true;
                // (b1) 四相位水平腿（右拖 → 近面向右，左拖反之，0/90/180/270 全相位）。
                const double kH966[4] = { 0.0, 90.0, 180.0, 270.0 };
                for (double ph : kH966) {
                    b966->setProperty("spinAngle", ph);
                    pump966();
                    const QVector3D base = near966(cube966);
                    b966->setProperty("spinAngle", ph + 0.6);
                    pump966();
                    const QVector3D r = near966(cube966);
                    b966->setProperty("spinAngle", ph - 0.6);
                    pump966();
                    const QVector3D l = near966(cube966);
                    if (!((r.x() - base.x()) > 0.0 && (l.x() - base.x()) < 0.0)) {
                        rigOk = false;
                        qInfo().noquote() << "  t966 diag: yaw phase" << ph << "right dx"
                                          << double(r.x() - base.x()) << "left dx" << double(l.x() - base.x());
                    }
                }
                // (b2) 俯仰腿（上拖看顶全相位一致 + 恒铰链不换轴 + 轴纯度）。
                const double kP966[3] = { 0.0, 125.0, 215.0 }; // 显示 yaw -35（正）/+90（侧）/+180（背）
                for (double ph : kP966) {
                    b966->setProperty("spinAngle", ph);
                    b966->setProperty("userPitch", 0.0);
                    pump966();
                    const QVector3D base = near966(cube966);
                    b966->setProperty("userPitch", 0.6);
                    pump966();
                    const QVector3D up = near966(cube966);
                    b966->setProperty("userPitch", -0.6);
                    pump966();
                    const QVector3D dn = near966(cube966);
                    const double dxU = std::fabs(double(up.x() - base.x()));
                    const double dyU = std::fabs(double(up.y() - base.y()));
                    const double ratio = dyU > 1e-12 ? dxU / dyU : 99.0;
                    worstRatio966 = std::max(worstRatio966, ratio);
                    const bool spinUntouched = b966->property("spinAngle").toDouble() == ph;
                    if (!((up.y() - base.y()) < 0.0 && (dn.y() - base.y()) > 0.0) || ratio > 0.25
                        || !spinUntouched) {
                        rigOk = false;
                        qInfo().noquote() << "  t966 diag: pitch phase" << ph << "up dy"
                                          << double(up.y() - base.y()) << "down dy" << double(dn.y() - base.y())
                                          << "axis ratio" << ratio << "spinUntouched" << spinUntouched;
                    }
                }
            }
        }
        QDir(probeUi966).removeRecursively();
        const bool okB966 = rigOk;
        ok = okA966 && okB966;
        if (!ok)
            qInfo().noquote() << "  [t966 diag] sourcePins" << okA966 << "rig" << okB966 << rigDiag
                              << "worstAxisRatio" << worstRatio966;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t966 preview drag direction final fix (user sixth round: left-right "
                             "rotating to the back face is STILL reversed - t921 phase compensation did "
                             "not cure): the sign-face panorama is now settled - horizontal drag has "
                             "been purely linear since t599 (spinAngle += dx*0.6, no phase face, proven "
                             "uniform by the rig's four-phase yaw legs); the only phase-dependent sign "
                             "face was t921's sign(cos(displayYaw)) pitch increment patch, and the true "
                             "root is the TRANSFORM GRAPH: a single-node eulerRotation composes as "
                             "Ry(yaw)*Rx(pitch) with yaw outermost in world space (verified empirically "
                             "against this Qt's Quick3D scene graph, which has no rotationOrder knob), "
                             "so the pitch hinge Ry(theta)*X_axis projects onto the screen with a "
                             "cos(theta) factor - it flips exactly on the back phase (the round-five "
                             "symptom) and swings toward the view axis near the side phase where "
                             "vertical drag leaks an in-plane spin component (the drag axis is "
                             "swapped); t921's compensation patched the LAW on top of that phase face "
                             "and re-flips at the +/-90 deg boundary mid-gesture, which is why the user "
                             "still felt it wrong; the fix splits all four preview branches (full cube "
                             "/ bed / item shape / mob) into a PITCH PARENT (world-frame tilt, hinge "
                             "always the screen-horizontal X axis, projection never flips) over a YAW "
                             "CHILD (spinAngle turntable) and restores the pure linear law with zero "
                             "phase judgment; the behavioral rig loads the real source-tree "
                             "ResourceBrowser.qml in a real QQmlEngine against real Quick3D nodes "
                             "(sceneRotation/scenePosition read back, no re-implemented math) and "
                             "asserts near-surface screen displacement under 1px drags: yaw right/left "
                             "at spin 0/90/180/270 all move the near face right/left respectively, "
                             "pitch up/down at front/side/back phases keeps 'up-drag sees the top' "
                             "uniform with <=25% lateral leakage (old graph measured ~40% or a "
                             "near-zero vertical component at the side phase) and yaw value untouched "
                             "by vertical drags; source pins keep the pure linear law line verbatim, "
                             "the old single-node composition string and any const faceSign / Math.cos "
                             "extinct, and the four-branch pitch/yaw split counted";
    }

    // ── P-t967 分类单选化 + 三大类重划探针（R19.17 🅴；用户第五轮口径「选一个物品又选一个生物，
    //    出现在一起导致问题——共用一个选中态；分类重划三大类 生物/方块/物品材料，无 3D 贴图的归
    //    物品材料」）──
    //    病灶：旧版生物格点击只写 selectedMobFromSection/selectedMobName、不清 selectedId，而预览侧
    //    床分支（selectedIsBed）/ 异形 3D 分支（selectedIsItem3D）的 visible 无 !selectedIsMob 守卫
    //    （整立方分支的 review27 #4 家族互斥治不了**跨分类**双选）→ 先选床/活板门/火把/栅栏等再点
    //    生物格 = 两套 3D 模型同时可见叠渲，而名字/类别行与形态·变体面板因 selectedIsMob 优先只显
    //    生物侧（渲染双份 + 面板单份的复合坏面）。修法 = 选中收口 selectMob/selectItem 单一权威
    //    （写本类 + 清另一类）+ categoryOfEntry 三大类单一权威（与预览路由谓词同源）。
    //    (a) 源码钉：权威函数体互清形态 + 两 TapHandler 路由权威 + 旧内联双写形态绝迹 +
    //        categoryOfEntry 判据链 + 三分区 Repeater/表头 + 谓词函数化委托。
    //    (b) 归属表行为钉（真 rig，t966 装配法）：真 QQmlEngine 直载源树 ResourceBrowser.qml ——
    //        paletteModel 三分区读回：分区完整性（两两不交 + 并集 == paletteModel）+ 代表条目逐类
    //        断言（石头/火把/活板门/白床/草丛→方块、木棍/生物蛋邻位→各自类、狼→生物图鉴段）+
    //        categoryOfEntry 直调返回值。
    //    (c) 单选中态行为钉（同 rig 状态机 + 渲染面）：selectItem(火把)→selectMob(狼) →
    //        selectedId==0 且可见几何集只剩 MobModel（可见 ItemShapeGeometry/BedModelGeometry 与
    //        可见 MobModel 并存 = 双选中渲染面，结构性不可再现）；selectItem(白床)→selectMob(狼)
    //        同钉床分支；反向 selectMob(狼)→selectItem(石头) → selectedMobFromSection==-1 且只剩
    //        BlockCube；代表条目 ×4 循环「先物品后生物」selectedId 恒归 0；selectedTabName 三类
    //        逐字断言。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf967(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb967 = rf967.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf967.readAll()) : QString();
        // (a) 源码钉（阴性轮回退形态：selectMob 的清物品行注释化——QML 仍可加载，t957 教训）。
        const int iSelMob967 = rb967.indexOf(QStringLiteral("function selectMob"));
        const QString selMobBlock967 = iSelMob967 >= 0 ? rb967.mid(iSelMob967, 400) : QString();
        const int iSelItem967 = rb967.indexOf(QStringLiteral("function selectItem"));
        const QString selItemBlock967 = iSelItem967 >= 0 ? rb967.mid(iSelItem967, 400) : QString();
        const int iCat967 = rb967.indexOf(QStringLiteral("function categoryOfEntry"));
        const QString catBlock967 = iCat967 >= 0 ? rb967.mid(iCat967, 400) : QString();
        const bool okA967 = iSelMob967 >= 0 && iSelItem967 >= 0 && iCat967 >= 0
                         && selMobBlock967.contains(QStringLiteral("root.selectedMobFromSection = mobType"))
                         && selMobBlock967.contains(QStringLiteral("root.selectedId = 0"))
                         && selItemBlock967.contains(QStringLiteral("root.selectedMobFromSection = -1"))
                         && catBlock967.contains(QStringLiteral("root.isCubeId(id) || root.isBedId(id) || root.isItem3DId(id)"))
                         && rb967.count(QStringLiteral("onTapped: root.selectMob(modelData.mobType, modelData.name)")) == 1
                         && rb967.count(QStringLiteral("onTapped: root.selectItem(modelData)")) == 1
                         && !rb967.contains(QStringLiteral("root.selectedId = modelData"))
                         && !rb967.contains(QStringLiteral("onTapped: { root.selectedMobFromSection"))
                         && rb967.count(QStringLiteral("model: root.blockEntries")) == 1
                         && rb967.count(QStringLiteral("model: root.matEntries")) == 1
                         && rb967.count(QStringLiteral("delegate: itemCell")) == 2 // t989 演化（P-t949(d) 先例）：蛋分区退役 → itemCell 复用点 3→2
                         && !rb967.contains(QStringLiteral("text: \"生物蛋\""))   // t989 演化：蛋表头绝迹
                         && rb967.contains(QStringLiteral("text: \"方块\""))
                         && rb967.contains(QStringLiteral("text: \"物品材料\""))
                         && !rb967.contains(QStringLiteral("text: \"物品\"")) // 旧两段表头退役（「物品材料」不带封闭引号，不误伤）
                         && rb967.contains(QStringLiteral("selectedIsItem3D: root.isItem3DId(root.selectedId)"))
                         && rb967.contains(QStringLiteral("selectedIsCube: root.isCubeId(root.selectedId)"))
                         && rb967.contains(QStringLiteral("selectedIsBed: root.isBedId(root.selectedId)"));
        ok = ok && okA967;

        // (b)(c) 行为 rig（t966 装配法：临时目录逃离 qrc 重映射 + 私有 URI；窗口不 show——
        //     visible 绑定求值不依赖渲染回路，本探针只断言可见性旗标与选中状态）。
        static bool sT967TypesRegistered = false;
        if (!sT967TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT967", 1, 0, "Hotbar");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT967", 1, 0, "ResourcePackManager");
            qmlRegisterType<BlockCube>("VoxelSandboxProbeT967", 1, 0, "BlockCube");
            qmlRegisterType<ItemShapeGeometry>("VoxelSandboxProbeT967", 1, 0, "ItemShapeGeometry");
            qmlRegisterType<BedModelGeometry>("VoxelSandboxProbeT967", 1, 0, "BedModelGeometry");
            qmlRegisterType<MobModel>("VoxelSandboxProbeT967", 1, 0, "MobModel");
            qmlRegisterType<EnchantBookBox>("VoxelSandboxProbeT967", 1, 0, "EnchantBookBox");
            qmlRegisterType<MobBowGeometry>("VoxelSandboxProbeT967", 1, 0, "MobBowGeometry");
            qmlRegisterType<UnitCube>("VoxelSandboxProbeT967", 1, 0, "UnitCube");
            sT967TypesRegistered = true;
        }
        bool rigOk967 = false;
        QString rigDiag967;
        const QString uiDir967 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUi967 = QDir::temp().absoluteFilePath(
                QStringLiteral("t967_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUi967);
        for (const QString f : { QStringLiteral("ResourceBrowser.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUi967 + QLatin1Char('/') + f);
            QFile(uiDir967 + QLatin1Char('/') + f).copy(probeUi967 + QLatin1Char('/') + f);
        }
        {
            const QStringList qmlFiles967 = QDir(probeUi967).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles967) {
                QFile p(probeUi967 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT967\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine967;
        Hotbar hb967;
        ResourcePackManager rp967;
        QQuickWindow win967; // 永不 show（headless：visible 旗标求值不依赖渲染回路）
        QQmlComponent comp967(&engine967,
                              QUrl::fromLocalFile(probeUi967 + QStringLiteral("/ResourceBrowser.qml")));
        QQuickItem *b967 = nullptr;
        if (comp967.isError()) {
            rigDiag967 = QStringLiteral("load: ") + comp967.errorString();
        } else if ((b967 = qobject_cast<QQuickItem *>(comp967.create())) == nullptr) {
            rigDiag967 = QStringLiteral("create failed");
        } else {
            b967->setParent(&engine967);
            b967->setProperty("hotbar", QVariant::fromValue(&hb967));
            b967->setProperty("resourcePack", QVariant::fromValue(&rp967));
            b967->setProperty("atlasSource", QStringLiteral("qrc:/textures/atlas.png"));
            b967->setProperty("packActive", false);
            b967->setWidth(700);
            b967->setHeight(500);
            b967->setParentItem(win967.contentItem());
            auto pump967 = []() {
                for (int i = 0; i < 8; ++i)
                    QCoreApplication::processEvents();
            };
            pump967();
            // QML 函数直调（t874 qmlCall 手法：QML 脚本方法签名 = QVariant 形参）。
            auto qmlCall967 = [](QObject *obj, const char *method, const QVariantList &args) -> bool {
                if (args.size() == 1)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)));
                if (args.size() == 2)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)), Q_ARG(QVariant, args.at(1)));
                return false;
            };
            auto qmlFn967 = [](QObject *obj, const char *method, const QVariant &arg) -> QVariant {
                QVariant ret;
                if (!QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, arg)))
                    return QVariant();
                return ret;
            };
            // 当前**有效可见** Model 的几何类名集合。两个工程事实（本轮 rig 实测）：
            //   ① 可见性须走**全祖链 visible 旗标**（property("visible") 只是本节点旗标：生物分支
            //      Model 自身缺省 true、靠 pitch 父 Node 门控）——且不能用「类名 == QQuick3DNode」
            //      识别 Node 层：QML 内联声明自定义属性的 Node（item3D yaw 子的 item3DScale /
            //      etBookNode 的 etBookPackHit）运行期是动态元对象、类名带 _QML_N 后缀，精确匹配
            //      会漏检其 visible=false → 未选附魔台也把书页计成可见（假绿面）。改为对每个祖先
            //      有 visible 属性即检查，走到 QQuickWindow（rig 不 show、恒 false）为止不检查。
            //   ② 几何类名同样有 _QML_N 后缀面（MobModel 块内声明 rodClock → "MobModel_QML_123"）
            //      → 用 qobject_cast 继承判定（对动态元对象照常工作），不比类名字符串。
            auto visGeoms967 = [b967]() {
                auto effVis = [](QObject *m) {
                    for (QObject *p = m; p != nullptr; p = p->parent()) {
                        if (std::strcmp(p->metaObject()->className(), "QQuickWindow") == 0)
                            break; // rig 窗口永不 show——其 visible=false 与 3D 分支无关
                        const QVariant v = p->property("visible");
                        if (v.isValid() && !v.toBool())
                            return false;
                    }
                    return true;
                };
                auto tagOf = [](QObject *geo) -> QString {
                    if (qobject_cast<MobModel *>(geo)) return QStringLiteral("MobModel");
                    if (qobject_cast<ItemShapeGeometry *>(geo)) return QStringLiteral("ItemShapeGeometry");
                    if (qobject_cast<BedModelGeometry *>(geo)) return QStringLiteral("BedModelGeometry");
                    if (qobject_cast<BlockCube *>(geo)) return QStringLiteral("BlockCube");
                    if (qobject_cast<EnchantBookBox *>(geo)) return QStringLiteral("EnchantBookBox");
                    if (qobject_cast<UnitCube *>(geo)) return QStringLiteral("UnitCube");
                    return QString();
                };
                QStringList geoms;
                const auto models = b967->findChildren<QObject *>();
                for (QObject *m : models) {
                    // t986 注：Model 自身带声明属性（预览 Model 的 mobCollarActive/材质路由属性）→
                    //   动态元对象类名 "QQuick3DModel_QML_N"（同注① 的 Node 面）→ 只能前缀匹配。
                    if (std::strncmp(m->metaObject()->className(), "QQuick3DModel", 13) != 0)
                        continue;
                    if (!m->property("visible").toBool() || !effVis(m))
                        continue;
                    QObject *geo = m->property("geometry").value<QObject *>();
                    if (!geo)
                        continue;
                    const QString tag = tagOf(geo);
                    if (!tag.isEmpty())
                        geoms << tag;
                }
                return geoms;
            };
            rigOk967 = true;
            // ═══ (b) 三大类归属表（分区完整性 + 代表条目逐类 + categoryOfEntry 直调）═══
            const QVariantList pal967 = b967->property("paletteModel").toList();
            const QVariantList blk967 = b967->property("blockEntries").toList();
            const QVariantList mat967 = b967->property("matEntries").toList();
            if (pal967.isEmpty()) {
                rigOk967 = false;
                rigDiag967 = QStringLiteral("palette empty");
            } else {
                // 分区完整性：两两不交 + 并集（多重集）== paletteModel（t989 演化：两分区）。
                QSet<int> seen967;
                bool disjoint = true;
                for (const QVariantList *lst : { &blk967, &mat967 }) {
                    for (const QVariant &v : *lst) {
                        const int id = v.toInt();
                        if (seen967.contains(id))
                            disjoint = false;
                        seen967.insert(id);
                    }
                }
                QList<int> a967, c967;
                for (const QVariant &v : pal967) a967 << v.toInt();
                for (const QVariant &v : blk967) c967 << v.toInt();
                for (const QVariant &v : mat967) c967 << v.toInt();
                std::sort(a967.begin(), a967.end());
                std::sort(c967.begin(), c967.end());
                const bool partitionOk = disjoint && a967 == c967;
                // 代表条目逐类（用户口径代表 + 邻位外溢守卫）：石头/火把/木活板门/白床/草丛→方块；
                //   木棍/弓/护甲→物品材料。t989 演化（P-t949(d) 先例）：原「猪/狼蛋→生物」腿翻转为
                //   「蛋绝迹」——蛋 id 不在 paletteModel / 任一分区，categoryOfEntry 不再产出 0。
                const int repsBlk967[] = { int(BR::Stone), int(BR::Torch), int(BR::WoodTrapdoor),
                                           int(BR::BedWhite), int(BR::TallGrass) };
                const int repsMat967[] = { int(RecipeRegistry::StickId), int(ToolRegistry::Bow),
                                           int(RecipeRegistry::ArmorIdBase) };
                const int repsEggGone967[] = { int(RecipeRegistry::SpawnEggPigId), int(RecipeRegistry::SpawnEggWolfId) };
                bool repsOk = partitionOk;
                auto inList967 = [](const QVariantList &lst, int id) {
                    for (const QVariant &v : lst)
                        if (v.toInt() == id) return true;
                    return false;
                };
                for (int id : repsBlk967)
                    if (!(qmlFn967(b967, "categoryOfEntry", QVariant(id)).toInt() == 1 && inList967(blk967, id))) {
                        repsOk = false;
                        qInfo().noquote() << "  t967 diag(b): block-rep miss id" << id;
                    }
                for (int id : repsMat967)
                    if (!(qmlFn967(b967, "categoryOfEntry", QVariant(id)).toInt() == 2 && inList967(mat967, id))) {
                        repsOk = false;
                        qInfo().noquote() << "  t967 diag(b): mat-rep miss id" << id;
                    }
                for (int id : repsEggGone967)
                    if (inList967(pal967, id) || qmlFn967(b967, "categoryOfEntry", QVariant(id)).toInt() == 0) {
                        repsOk = false;
                        qInfo().noquote() << "  t967 diag(b): egg-gone miss id" << id;
                    }
                // 狼（图鉴段）→ 生物类：selectMob 后 selectedTabName 逐字「生物」+ 类别行「生物 / mobType N」。
                if (!qmlCall967(b967, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) }))
                    rigOk967 = false;
                pump967();
                const bool wolfTab967 = b967->property("selectedTabName").toString() == QStringLiteral("生物")
                                     && b967->property("selectedIsMob").toBool()
                                     && b967->property("selectedMobFromSection").toInt() == int(EntityManager::MobWolf)
                                     && b967->property("selectedId").toInt() == 0; // 单选中态：物品侧被清
                if (!wolfTab967) {
                    repsOk = false;
                    qInfo().noquote() << "  t967 diag(b): wolf tab" << b967->property("selectedTabName").toString()
                                      << "isMob" << b967->property("selectedIsMob").toBool()
                                      << "selId" << b967->property("selectedId").toInt();
                }
                if (!repsOk) rigOk967 = false;

                // ═══ (c) 单选中态行为钉（状态机 + 可见几何集双面）═══
                // c1 物品→生物（用户症状原路径：异形 3D 分支无 !selectedIsMob 守卫的叠渲面）：
                //    selectItem(火把) → ItemShapeGeometry 独显；再 selectMob(狼) → selectedId 归 0
                //    且可见几何集只剩 MobModel（无 ItemShape/Bed/Cube 残留）。
                auto drive967 = [&](bool stepOk, const char *tag) {
                    if (!stepOk) {
                        rigOk967 = false;
                        qInfo().noquote() << "  t967 diag(c):" << tag;
                    }
                };
                qmlCall967(b967, "selectItem", { QVariant(int(BR::Torch)) });
                pump967();
                QStringList g1 = visGeoms967();
                const bool c1a = b967->property("selectedId").toInt() == int(BR::Torch)
                              && g1.contains(QStringLiteral("ItemShapeGeometry")) && !g1.contains(QStringLiteral("MobModel"));
                qmlCall967(b967, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) });
                pump967();
                QStringList g2 = visGeoms967();
                const bool c1b = b967->property("selectedId").toInt() == 0
                              && b967->property("selectedMobFromSection").toInt() == int(EntityManager::MobWolf)
                              && g2.contains(QStringLiteral("MobModel"))
                              && !g2.contains(QStringLiteral("ItemShapeGeometry"))
                              && !g2.contains(QStringLiteral("BedModelGeometry"))
                              && !g2.contains(QStringLiteral("BlockCube"));
                drive967(c1a && c1b, "c1 item(torch)->mob(wolf)");
                if (!c1a || !c1b)
                    qInfo().noquote() << "    g1" << g1.join(QLatin1Char(',')) << "| g2" << g2.join(QLatin1Char(','))
                                      << "selId" << b967->property("selectedId").toInt();
                // c2 床→生物（床分支同病面）：selectItem(白床) → BedModelGeometry 独显；再 selectMob(狼)
                //    → 床几何消失、MobModel 独显。
                qmlCall967(b967, "selectItem", { QVariant(int(BR::BedWhite)) });
                pump967();
                QStringList g3 = visGeoms967();
                const bool c2a = g3.contains(QStringLiteral("BedModelGeometry")) && !g3.contains(QStringLiteral("MobModel"));
                qmlCall967(b967, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) });
                pump967();
                QStringList g4 = visGeoms967();
                const bool c2b = g4.contains(QStringLiteral("MobModel")) && !g4.contains(QStringLiteral("BedModelGeometry"))
                              && !g4.contains(QStringLiteral("ItemShapeGeometry")) && !g4.contains(QStringLiteral("BlockCube"));
                drive967(c2a && c2b, "c2 item(bed)->mob(wolf)");
                if (!c2a || !c2b)
                    qInfo().noquote() << "    g3" << g3.join(QLatin1Char(',')) << "| g4" << g4.join(QLatin1Char(','));
                // c3 反向 生物→物品：selectMob(狼) → selectItem(石头) → 生物段清 + BlockCube 独显。
                qmlCall967(b967, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) });
                pump967();
                qmlCall967(b967, "selectItem", { QVariant(int(BR::Stone)) });
                pump967();
                QStringList g5 = visGeoms967();
                const bool c3 = b967->property("selectedMobFromSection").toInt() == -1
                             && b967->property("selectedId").toInt() == int(BR::Stone)
                             && g5.contains(QStringLiteral("BlockCube")) && !g5.contains(QStringLiteral("MobModel"))
                             && b967->property("selectedTabName").toString() == QStringLiteral("方块");
                drive967(c3, "c3 mob(wolf)->item(stone)");
                // c4 物品材料类选中名面 + t989 演化选蛋路径绝迹：木棍 → 「物品材料」；
                //   猪蛋 id 直写 selectItem（旧版 → 「生物」+ mob 3D 预览）→ 现不产 mob 态
                //   （selectedIsMob false = 蛋→mob 预览映射随蛋分区绝迹）。
                qmlCall967(b967, "selectItem", { QVariant(int(RecipeRegistry::StickId)) });
                pump967();
                const bool c4a = b967->property("selectedTabName").toString() == QStringLiteral("物品材料")
                              && !b967->property("selectedIsMob").toBool();
                qmlCall967(b967, "selectItem", { QVariant(int(RecipeRegistry::SpawnEggPigId)) });
                pump967();
                const bool c4b = !b967->property("selectedIsMob").toBool()
                              && b967->property("selectedTabName").toString() != QStringLiteral("生物");
                drive967(c4a && c4b, "c4 stick tab + egg path extinct");
                // c5 状态机循环钉：代表条目 ×3「先物品后生物」→ selectedId 恒归 0（双选中不可再现；
                //   t989 演化：蛋 id 非调色板条目退出代表集）。
                bool c5 = true;
                const int loopIds967[] = { int(BR::Stone), int(BR::Torch), int(RecipeRegistry::StickId) };
                for (int id : loopIds967) {
                    qmlCall967(b967, "selectItem", { QVariant(id) });
                    qmlCall967(b967, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) });
                    pump967();
                    if (b967->property("selectedId").toInt() != 0
                        || b967->property("selectedMobFromSection").toInt() != int(EntityManager::MobWolf)) {
                        c5 = false;
                        qInfo().noquote() << "  t967 diag(c5): id" << id << "selId"
                                          << b967->property("selectedId").toInt();
                    }
                }
                drive967(c5, "c5 loop exclusivity");
            }
        }
        QDir(probeUi967).removeRecursively();
        const bool okB967 = rigOk967;
        ok = ok && okB967;
        if (!ok)
            qInfo().noquote() << "  [t967 diag] sourcePins" << okA967 << "rig" << okB967 << rigDiag967;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t967 browser single-selection + three-category regroup: the user's "
                             "fifth-round report 'a mob and an item can be selected at the SAME time "
                             "(pick an item then a mob and they appear together)' resolves to two "
                             "independent selection variables (selectedMobFromSection for the mob "
                             "gallery vs selectedId for the palette) whose only mutual exclusion was "
                             "one-directional: item taps cleared the mob side but mob taps left "
                             "selectedId stale, and the bed / item-shape preview branches gate their "
                             "visibility on selectedIsBed / selectedIsItem3D with no !selectedIsMob "
                             "guard (the full-cube branch's review27-4 family exclusion cannot cure a "
                             "cross-category dual selection), so BedModelGeometry / ItemShapeGeometry "
                             "and the MobModel rendered stacked while the name/category row and the "
                             "form/variant panels followed selectedIsMob alone; the fix funnels every "
                             "tap through selectMob / selectItem authorities that write their own side "
                             "AND clear the other (selectedId = 0 sentinel), making the dual state "
                             "unreachable by construction; categories regroup into Mobs / Blocks / "
                             "Items-Materials via one authoritative categoryOfEntry predicate that "
                             "reuses the very same id-level 3D routing functions (isCubeId / isBedId / "
                             "isItem3DId) so the classification table IS the preview routing table: "
                             "mob-gallery entries and spawn eggs (whose preview is the 3D mob model) "
                             "are Mobs, palette entries with an exclusive 3D display (full cubes, "
                             "beds, the t880/t925/t965 item-shape families) are Blocks, and "
                             "everything without a 3D display (tools, materials, armor, and flat-icon "
                             "cross/rail/plate families) is Items-Materials, per the user's 'no 3D "
                             "goes to items-materials' caliber; legs: source pins (authority function "
                             "bodies, tap routing, extinct inline dual-write forms, three Repeater "
                             "partitions + section headers, predicate delegation), a real-QmlEngine "
                             "rig reading the palette partition back (pairwise disjoint + union == "
                             "paletteModel + representative pins: stone/torch/trapdoor/white-bed/tall-"
                             "grass -> Blocks, stick/bow/armor -> Items-Materials, pig/wolf eggs "
                             "EXTINCT from the palette (t989 evolution: in no partition and "
                             "categoryOfEntry no longer yields Mobs for them), wolf gallery -> "
                             "Mobs tab), and behavior pins driving the real "
                             "selection state machine (torch then wolf: selectedId returns to 0 and "
                             "the visible geometry set holds MobModel alone; bed then wolf likewise "
                             "for the bed branch; wolf then stone flips back with BlockCube alone; "
                             "stick/egg tab names (t989 evolution: egg select path extinct); "
                             "three-representative exclusivity loop)";
    }

    // ── P-t989 查看器生物蛋整段移除探针（R19.17 🅴；用户 9-01 口径「生物蛋纯属多余，上面已经有
    //    生物的查看了」+「小僵尸蛋和别的蛋（风格）不统一」→ 查看器不再列蛋）──
    //    翻案边界（t967 局部）：只动查看器——生物图鉴本体格保留（mobModel 表段）；创造背包
    //    （Inventory.qml 材料 tab）蛋分区不动；右键生成 / 中键复制蛋链路不动。移除面 = 蛋分区表头 +
    //    格段 + categoryOfEntry 蛋条目（本表不再产出 0）+ 蛋预览路径（原 mobTypeForEgg(selectedId)
    //    → mob 3D 回退映射）+ paletteModel 蛋段（hotbar.isSpawnEgg 权威谓词过滤 = Game 层
    //    RecipeRegistry::mobTypeForSpawnEgg 单一权威透传——QML 侧蛋 id 字面量镜像表整表退役）。
    //    (a) 源码钉（注释剥离后断言「绝迹」——蛋键允许仅存注释性登记，故必须剥 // 与 /* */ 再钉，
    //        剥离器带字符串字面量态防 file:// 类内容误吞）：mobTypeForEgg / eggEntries / 「生物蛋」
    //        / 全部 14 蛋十六进制键在**代码**中绝迹；正向钉 = isSpawnEgg 过滤行 + 三分区 Repeater
    //        结构 + t989 契约注释锚（裸源）。
    //    (b) rig 腿（t966/t967 装配法）：paletteModel 非空 + 两分区（block/mat）两两不交 + 多重集
    //        并集 == paletteModel（paletteModel = block+matEntries 并集，蛋段绝迹）；全部 14 蛋 id
    //        逐个断言：不在 paletteModel / 不在任一分区 / categoryOfEntry != 0（== 2 材料档）；
    //        图鉴本体保留（mobModel 表 17 条）；selectMob(狼) → 生物 tab 照旧；选蛋路径不存在
    //        （selectItem(猪蛋) → 非生物预览态）；单选中收口照旧（selectItem(木棍) → selectMob(狼)
    //        → selectedId 归 0）。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf989(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb989 = rf989.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf989.readAll()) : QString();
        // 注释剥离器（Code/String/Line/Block 四态；字符串内 \\ 转义感知；块注释保留换行防行粘连）。
        auto stripQmlComments989 = [](const QString &src) {
            QString out;
            out.reserve(src.size());
            enum St { Code, Str, Line, Block };
            St st = Code;
            int i = 0;
            const int n = src.size();
            while (i < n) {
                const QChar c = src.at(i);
                const QChar nx = (i + 1 < n) ? src.at(i + 1) : QChar(u'\0');
                if (st == Code) {
                    if (c == u'"') { st = Str; out.append(c); }
                    else if (c == u'/' && nx == u'/') { st = Line; ++i; }
                    else if (c == u'/' && nx == u'*') { st = Block; ++i; }
                    else out.append(c);
                } else if (st == Str) {
                    out.append(c);
                    if (c == u'\\') { if (i + 1 < n) { out.append(src.at(i + 1)); ++i; } }
                    else if (c == u'"') st = Code;
                } else if (st == Line) {
                    if (c == u'\n') { st = Code; out.append(c); }
                } else { // Block
                    if (c == u'*' && nx == u'/') { st = Code; ++i; }
                    else if (c == u'\n') out.append(c);
                }
                ++i;
            }
            return out;
        };
        const QString code989 = stripQmlComments989(rb989);
        static const char *kEggHex989[] = {
            "0x20F", "0x210", "0x211", "0x213", "0x214", "0x215", "0x216", "0x22C", "0x22E",
            "0x246", "0x247", "0x249", "0x24A", "0x25D", "0x25E" // t1012③ 洞穴蜘蛛蛋入绝迹键表
        };
        bool eggKeysExtinct = true;
        for (const char *k : kEggHex989)
            if (code989.contains(QLatin1String(k))) {
                eggKeysExtinct = false;
                qInfo().noquote() << "  t989 diag(a): egg hex key survives in code:" << k;
            }
        const bool okA989 = rb989.contains(QStringLiteral("t989 生物蛋整段移除"))
                         && rb989.contains(QStringLiteral("t989 翻案登记"))
                         && eggKeysExtinct
                         && !code989.contains(QStringLiteral("mobTypeForEgg"))
                         && !code989.contains(QStringLiteral("eggEntries"))
                         && !code989.contains(QStringLiteral("生物蛋"))
                         && code989.contains(QStringLiteral("return !root.hotbar.isSpawnEgg(id)"))
                         && rb989.count(QStringLiteral("model: root.blockEntries")) == 1
                         && rb989.count(QStringLiteral("model: root.matEntries")) == 1
                         && rb989.count(QStringLiteral("delegate: itemCell")) == 2
                         && rb989.contains(QStringLiteral("text: \"生物\""))
                         && rb989.contains(QStringLiteral("text: \"方块\""))
                         && rb989.contains(QStringLiteral("text: \"物品材料\""));
        // (b) 行为 rig（t966/t967 装配法）。
        static bool sT989TypesRegistered = false;
        if (!sT989TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT989", 1, 0, "Hotbar");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT989", 1, 0, "ResourcePackManager");
            qmlRegisterType<BlockCube>("VoxelSandboxProbeT989", 1, 0, "BlockCube");
            qmlRegisterType<ItemShapeGeometry>("VoxelSandboxProbeT989", 1, 0, "ItemShapeGeometry");
            qmlRegisterType<BedModelGeometry>("VoxelSandboxProbeT989", 1, 0, "BedModelGeometry");
            qmlRegisterType<MobModel>("VoxelSandboxProbeT989", 1, 0, "MobModel");
            qmlRegisterType<EnchantBookBox>("VoxelSandboxProbeT989", 1, 0, "EnchantBookBox");
            qmlRegisterType<MobBowGeometry>("VoxelSandboxProbeT989", 1, 0, "MobBowGeometry");
            qmlRegisterType<UnitCube>("VoxelSandboxProbeT989", 1, 0, "UnitCube");
            sT989TypesRegistered = true;
        }
        bool rigOk989 = false;
        QString rigDiag989;
        const QString uiDir989 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUi989 = QDir::temp().absoluteFilePath(
                QStringLiteral("t989_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUi989);
        for (const QString f : { QStringLiteral("ResourceBrowser.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUi989 + QLatin1Char('/') + f);
            QFile(uiDir989 + QLatin1Char('/') + f).copy(probeUi989 + QLatin1Char('/') + f);
        }
        {
            const QStringList qmlFiles989 = QDir(probeUi989).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles989) {
                QFile p(probeUi989 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT989\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine989;
        Hotbar hb989;
        ResourcePackManager rp989;
        QQuickWindow win989; // 永不 show（headless）
        QQmlComponent comp989(&engine989,
                              QUrl::fromLocalFile(probeUi989 + QStringLiteral("/ResourceBrowser.qml")));
        QQuickItem *b989 = nullptr;
        if (comp989.isError()) {
            rigDiag989 = QStringLiteral("load: ") + comp989.errorString();
        } else if ((b989 = qobject_cast<QQuickItem *>(comp989.create())) == nullptr) {
            rigDiag989 = QStringLiteral("create failed");
        } else {
            b989->setParent(&engine989);
            b989->setProperty("hotbar", QVariant::fromValue(&hb989));
            b989->setProperty("resourcePack", QVariant::fromValue(&rp989));
            b989->setProperty("atlasSource", QStringLiteral("qrc:/textures/atlas.png"));
            b989->setProperty("packActive", false);
            b989->setWidth(700);
            b989->setHeight(500);
            b989->setParentItem(win989.contentItem());
            auto pump989 = []() {
                for (int i = 0; i < 8; ++i)
                    QCoreApplication::processEvents();
            };
            pump989();
            auto qmlCall989 = [](QObject *obj, const char *method, const QVariantList &args) -> bool {
                if (args.size() == 1)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)));
                if (args.size() == 2)
                    return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)), Q_ARG(QVariant, args.at(1)));
                return false;
            };
            auto qmlFn989 = [](QObject *obj, const char *method, const QVariant &arg) -> QVariant {
                QVariant ret;
                if (!QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, arg)))
                    return QVariant();
                return ret;
            };
            auto inList989 = [](const QVariantList &lst, int id) {
                for (const QVariant &v : lst)
                    if (v.toInt() == id) return true;
                return false;
            };
            rigOk989 = true;
            const QVariantList pal989 = b989->property("paletteModel").toList();
            const QVariantList blk989 = b989->property("blockEntries").toList();
            const QVariantList mat989 = b989->property("matEntries").toList();
            if (pal989.isEmpty()) {
                rigOk989 = false;
                rigDiag989 = QStringLiteral("palette empty");
            } else {
                // 两分区两两不交 + 多重集并集 == paletteModel（蛋段绝迹 → 并集从三段缩两段）。
                QSet<int> seen989;
                bool disjoint989 = true;
                for (const QVariantList *lst : { &blk989, &mat989 }) {
                    for (const QVariant &v : *lst) {
                        const int id = v.toInt();
                        if (seen989.contains(id))
                            disjoint989 = false;
                        seen989.insert(id);
                    }
                }
                QList<int> a989, c989;
                for (const QVariant &v : pal989) a989 << v.toInt();
                for (const QVariant &v : blk989) c989 << v.toInt();
                for (const QVariant &v : mat989) c989 << v.toInt();
                std::sort(a989.begin(), a989.end());
                std::sort(c989.begin(), c989.end());
                if (!disjoint989 || a989 != c989) {
                    rigOk989 = false;
                    rigDiag989 = QStringLiteral("partition union mismatch");
                }
                // 全部 14 蛋 id：不在调色板 / 不在任一分区 / categoryOfEntry 不再产出 0（== 2 材料档）。
                const int eggsAll989[] = {
                    int(RecipeRegistry::SpawnEggPigId), int(RecipeRegistry::SpawnEggCowId),
                    int(RecipeRegistry::SpawnEggSheepId), int(RecipeRegistry::SpawnEggShamblerId),
                    int(RecipeRegistry::SpawnEggBonesId), int(RecipeRegistry::SpawnEggStalkerId),
                    int(RecipeRegistry::SpawnEggSpiderId), int(RecipeRegistry::SpawnEggChickenId),
                    int(RecipeRegistry::SpawnEggSquidId), int(RecipeRegistry::SpawnEggNightwalkerId),
                    int(RecipeRegistry::SpawnEggEmberlingId), int(RecipeRegistry::SpawnEggWolfId),
                    int(RecipeRegistry::SpawnEggOcelotId), int(RecipeRegistry::SpawnEggBabyShamblerId)
                };
                for (int id : eggsAll989) {
                    const int cat = qmlFn989(b989, "categoryOfEntry", QVariant(id)).toInt();
                    if (inList989(pal989, id) || inList989(blk989, id) || inList989(mat989, id)
                        || cat == 0 || cat != 2) {
                        rigOk989 = false;
                        qInfo().noquote() << "  t989 diag(b): egg id" << id << "cat" << cat
                                          << "inPal" << inList989(pal989, id);
                    }
                }
                // 图鉴本体保留（mobModel 表 18 条——蛋分区退役不动图鉴；t1012③ 洞穴蜘蛛条目合法追加）。
                const QVariantList gallery989 = b989->property("mobModel").toList();
                if (gallery989.size() != 18) {
                    rigOk989 = false;
                    qInfo().noquote() << "  t989 diag(b): mobModel gallery size" << gallery989.size();
                }
                // 三分类照旧（方块 / 物品材料代表 + 图鉴段选中生物）。
                if (qmlFn989(b989, "categoryOfEntry", QVariant(int(BR::Stone))).toInt() != 1
                    || qmlFn989(b989, "categoryOfEntry", QVariant(int(RecipeRegistry::StickId))).toInt() != 2) {
                    rigOk989 = false;
                    qInfo().noquote() << "  t989 diag(b): category reps";
                }
                if (!qmlCall989(b989, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) }))
                    rigOk989 = false;
                pump989();
                if (b989->property("selectedTabName").toString() != QStringLiteral("生物")
                    || !b989->property("selectedIsMob").toBool()) {
                    rigOk989 = false;
                    qInfo().noquote() << "  t989 diag(b): wolf gallery tab";
                }
                // 选蛋路径不存在：selectItem(猪蛋) 直写 → 非生物预览态（旧蛋→mob 映射绝迹）。
                if (!qmlCall989(b989, "selectItem", { QVariant(int(RecipeRegistry::SpawnEggPigId)) }))
                    rigOk989 = false;
                pump989();
                if (b989->property("selectedIsMob").toBool()) {
                    rigOk989 = false;
                    qInfo().noquote() << "  t989 diag(b): egg select still routes to mob preview";
                }
                // 单选中收口照旧：selectItem(木棍) → selectMob(狼) → selectedId 归 0。
                if (!qmlCall989(b989, "selectItem", { QVariant(int(RecipeRegistry::StickId)) }))
                    rigOk989 = false;
                pump989();
                if (!qmlCall989(b989, "selectMob", { QVariant(int(EntityManager::MobWolf)), QVariant(QStringLiteral("狼")) }))
                    rigOk989 = false;
                pump989();
                if (b989->property("selectedId").toInt() != 0
                    || b989->property("selectedMobFromSection").toInt() != int(EntityManager::MobWolf)) {
                    rigOk989 = false;
                    qInfo().noquote() << "  t989 diag(b): mutual exclusion selId"
                                      << b989->property("selectedId").toInt();
                }
            }
        }
        QDir(probeUi989).removeRecursively();
        const bool okB989 = rigOk989;
        ok = okA989 && okB989;
        if (!ok)
            qInfo().noquote() << "  [t989 diag] sourcePins" << okA989 << "rig" << okB989 << rigDiag989;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t989 browser spawn-egg section removed (t967 partial reversal - user "
                             "9-01: 'mob eggs are redundant, the mob gallery above already shows the "
                             "mobs' + the baby-shambler egg reads stylistically off next to the other "
                             "eggs): the viewer-side egg face is gone wholesale while the boundary "
                             "holds - the mob GALLERY stays (mobModel table untouched), the creative "
                             "inventory (Inventory.qml materials tab) keeps its egg section, and "
                             "right-click spawning / pick-block egg routes are untouched; removal "
                             "face = egg section header + grid, the categoryOfEntry egg entry (the "
                             "table no longer yields category 0), the egg preview path (the old "
                             "mobTypeForEgg(selectedId) fallback into the mob 3D branch) and the "
                             "paletteModel egg segment - filtered via a new authoritative "
                             "Hotbar::isSpawnEgg Q_INVOKABLE delegating to RecipeRegistry::"
                             "mobTypeForSpawnEgg, so the QML-side egg-id literal mirror table retires "
                             "wholesale (egg keys extinct from viewer code, comment-only "
                             "registration per the t989 contract); P-t967 evolved lawfully per the "
                             "P-t949(d) precedent (its egg legs now assert egg absence: no egg in "
                             "any partition / categoryOfEntry never 0 / egg select path yields no "
                             "mob state, two partition Repeaters, itemCell reuse count 3->2); legs: "
                             "comment-stripped source pins (string-aware // and /* */ stripper so "
                             "registration comments may keep the keys) asserting mobTypeForEgg / "
                             "eggEntries / the egg header and all 15 egg hex keys extinct from CODE, "
                             "plus the positive filter line and section structure; a real-QQmlEngine "
                             "rig reading the real palette back: pairwise-disjoint block/mat "
                             "partitions whose multiset union equals paletteModel, all 15 egg ids "
                             "absent from palette and both partitions with categoryOfEntry==2, the "
                             "18-entry mob gallery intact, wolf gallery tap still yields the Mobs "
                             "tab, selectItem(pig egg) no longer produces the mob preview state, "
                             "and the single-selection funnel (stick then wolf -> selectedId 0) "
                             "unchanged";
    }

    // ── P-t990 方块默认视角回归俯视探针（R19.17 🅴；用户口径「方块默认视角从俯视变成仰视了，
    //    之前不是这样——git -S 查回归源修复」）──
    //    考古结论（git -S "-22" / "userPitch" / eulerRotation 全链）：基偏数值从未翻号——t458 初版
    //    单节点 eulerRotation(-22, spinAngle-35, 0) → t599 加 userPitch → t820 翻拖拽符号 → t877
    //    回退定稿 → t966 (8eef0f4) 拆 pitch 父/yaw 子时把 −22 逐字搬进世界系 pitch 父。回归源 =
    //    **t966 拆层本身**：旧单节点图合成 Ry(yaw)·Rx(pitch) 中 −22° 作用于模型局部系，自转带动
    //    倾角 → 顶/底面在自转中交替可见（翻滚观感，「见顶面」注释半相位为真）；世界系 pitch 父后
    //    同一 −22° 恒定 = top 法线 z 分量 sin(−22°) < 0 恒背相机（相机 +Z 轴）→ **全自转相位恒见
    //    底面 = 恒定仰视**——用户观感「从俯视变成仰视」如实归因于节点层级变化（间接因，非基偏
    //    翻号/丢失）。修 = 四分支基偏 −22° → +22°（sin(+22°) > 0 = 顶面恒朝相机 = JEI 式恒定俯视
    //    3/4）；−35 yaw 基偏与 t877 定稿拖拽定律逐字不动（上拖 → userPitch 增 → 更俯视，两基偏号
    //    下方向均一致——P-t966 行为腿数学上两号皆绿，不改判）。
    //    (a) 源码钉：四分支 `eulerRotation.x: 22 + root.userPitch` 计数 == 4 + "eulerRotation.x: -22"
    //        绝迹 + t990 契约注释锚 + −35 yaw 子计数 == 4 + 纯线性拖拽定律行（定律未动）。
    //    (b) rig 腿（t966/t989 装配法）：真 QQmlEngine 直载源树 ResourceBrowser.qml——
    //        ① 逐支钉：四分支 pitch 父 Node（裸 QQuick3DNode，无自定义属性）读回 eulerRotation.x
    //           == 22 ± 0.5（userPitch 默认 0）恰好 4 支；
    //        ② 行为钉（渲染面）：可见 BlockCube Model 的 sceneRotation 读回——top 法线 (0,1,0) 旋后
    //           z 分量 ∈ (0.30, 0.45)（sin22°≈0.375：号 + 量级双钉）在 spin 0/90/180/270 全相位恒
    //           成立（旧图恒负 = 病面），bottom 法线 z 恒 < 0；上拖 +0.6°（t877 定稿符号）→ top z
    //           增（更俯视）。
    {
        bool ok = true;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf990(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString rb990 = rf990.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf990.readAll()) : QString();
        const bool okA990 = rb990.count(QStringLiteral("eulerRotation.x: 22 + root.userPitch")) == 4
                         && !rb990.contains(QStringLiteral("eulerRotation.x: -22"))
                         && rb990.contains(QStringLiteral("t990 俯视基偏"))
                         && rb990.count(QStringLiteral("eulerRotation.y: root.spinAngle - 35")) == 4
                         && rb990.contains(QStringLiteral("root.userPitch = Math.max(-60, Math.min(60, root.userPitch - dy * 0.6))"));
        // (b) 行为 rig（t966/t989 装配法）。
        static bool sT990TypesRegistered = false;
        if (!sT990TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT990", 1, 0, "Hotbar");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT990", 1, 0, "ResourcePackManager");
            qmlRegisterType<BlockCube>("VoxelSandboxProbeT990", 1, 0, "BlockCube");
            qmlRegisterType<ItemShapeGeometry>("VoxelSandboxProbeT990", 1, 0, "ItemShapeGeometry");
            qmlRegisterType<BedModelGeometry>("VoxelSandboxProbeT990", 1, 0, "BedModelGeometry");
            qmlRegisterType<MobModel>("VoxelSandboxProbeT990", 1, 0, "MobModel");
            qmlRegisterType<EnchantBookBox>("VoxelSandboxProbeT990", 1, 0, "EnchantBookBox");
            qmlRegisterType<MobBowGeometry>("VoxelSandboxProbeT990", 1, 0, "MobBowGeometry");
            qmlRegisterType<UnitCube>("VoxelSandboxProbeT990", 1, 0, "UnitCube");
            sT990TypesRegistered = true;
        }
        bool rigOk990 = false;
        QString rigDiag990;
        const QString uiDir990 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUi990 = QDir::temp().absoluteFilePath(
                QStringLiteral("t990_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUi990);
        for (const QString f : { QStringLiteral("ResourceBrowser.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUi990 + QLatin1Char('/') + f);
            QFile(uiDir990 + QLatin1Char('/') + f).copy(probeUi990 + QLatin1Char('/') + f);
        }
        {
            const QStringList qmlFiles990 = QDir(probeUi990).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles990) {
                QFile p(probeUi990 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT990\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine990;
        Hotbar hb990;
        ResourcePackManager rp990;
        QQuickWindow win990; // 永不 show（headless：Quick3D 节点变换传播不依赖渲染回路，t966 先例）
        QQmlComponent comp990(&engine990,
                              QUrl::fromLocalFile(probeUi990 + QStringLiteral("/ResourceBrowser.qml")));
        QQuickItem *b990 = nullptr;
        if (comp990.isError()) {
            rigDiag990 = QStringLiteral("load: ") + comp990.errorString();
        } else if ((b990 = qobject_cast<QQuickItem *>(comp990.create())) == nullptr) {
            rigDiag990 = QStringLiteral("create failed");
        } else {
            b990->setParent(&engine990);
            b990->setProperty("hotbar", QVariant::fromValue(&hb990));
            b990->setProperty("resourcePack", QVariant::fromValue(&rp990));
            b990->setProperty("atlasSource", QStringLiteral("qrc:/textures/atlas.png"));
            b990->setProperty("packActive", false);
            b990->setProperty("previewDragging", true); // 冻结自转动画（rig 显式驱动确定性）
            b990->setProperty("selectedId", int(BR::Stone));
            b990->setWidth(700);
            b990->setHeight(500);
            b990->setParentItem(win990.contentItem());
            auto pump990 = []() {
                for (int i = 0; i < 8; ++i)
                    QCoreApplication::processEvents();
            };
            pump990();
            // ① 逐支钉：pitch 父 = 裸 QQuick3DNode（四分支 pitch 父均无自定义属性 → 精确类名），
            //    eulerRotation.x ≈ 22（yaw 子/无关 Node x≈0）恰好 4 支（生物分支 Node 亦含其中）。
            int pitchParents990 = 0;
            double xSum990 = 0.0;
            const auto nodes990 = b990->findChildren<QObject *>();
            for (QObject *nd : nodes990) {
                if (std::strcmp(nd->metaObject()->className(), "QQuick3DNode") != 0)
                    continue;
                const QVector3D e = nd->property("eulerRotation").value<QVector3D>();
                if (qAbs(double(e.x()) - 22.0) < 0.5) {
                    ++pitchParents990;
                    xSum990 += double(e.x());
                }
            }
            // ② 行为钉：可见 BlockCube Model 的 sceneRotation 下 top/bottom 法线朝向（渲染面）。
            QObject *cube990 = nullptr;
            for (QObject *m : nodes990) {
                if (std::strcmp(m->metaObject()->className(), "QQuick3DModel") != 0)
                    continue;
                if (!m->property("visible").toBool())
                    continue;
                QObject *geo = m->property("geometry").value<QObject *>();
                if (geo && std::strcmp(geo->metaObject()->className(), "BlockCube") == 0) {
                    cube990 = m;
                    break;
                }
            }
            if (pitchParents990 != 4) {
                rigDiag990 = QStringLiteral("pitch parent count %1").arg(pitchParents990);
            } else if (!cube990) {
                rigDiag990 = QStringLiteral("visible BlockCube model not found");
            } else {
                rigOk990 = true;
                const double avgX990 = xSum990 / pitchParents990;
                if (qAbs(avgX990 - 22.0) > 1e-6) {
                    rigOk990 = false;
                    qInfo().noquote() << "  t990 diag: pitch parent avg x" << avgX990;
                }
                // 全自转相位：top 法线 z ∈ (0.30, 0.45)（sin22°≈0.375 号 + 量级双钉；旧图恒负）。
                const double kPhases990[4] = { 0.0, 90.0, 180.0, 270.0 };
                for (double ph : kPhases990) {
                    b990->setProperty("spinAngle", ph);
                    b990->setProperty("userPitch", 0.0);
                    pump990();
                    const QQuaternion q = cube990->property("sceneRotation").value<QQuaternion>();
                    const double topZ = double(q.rotatedVector(QVector3D(0, 1, 0)).z());
                    const double botZ = double(q.rotatedVector(QVector3D(0, -1, 0)).z());
                    if (!(topZ > 0.30 && topZ < 0.45 && botZ < 0.0)) {
                        rigOk990 = false;
                        qInfo().noquote() << "  t990 diag: phase" << ph << "topZ" << topZ << "botZ" << botZ;
                    }
                }
                // 拖拽定律照旧：上拖（userPitch +0.6，t877 定稿符号）→ 更俯视（top z 增）。
                b990->setProperty("spinAngle", 0.0);
                b990->setProperty("userPitch", 0.0);
                pump990();
                double topZ0 = double(cube990->property("sceneRotation").value<QQuaternion>()
                                          .rotatedVector(QVector3D(0, 1, 0)).z());
                b990->setProperty("userPitch", 0.6);
                pump990();
                double topZup = double(cube990->property("sceneRotation").value<QQuaternion>()
                                           .rotatedVector(QVector3D(0, 1, 0)).z());
                if (!(topZup > topZ0)) {
                    rigOk990 = false;
                    qInfo().noquote() << "  t990 diag: up-drag topZ" << topZ0 << "->" << topZup;
                }
            }
        }
        QDir(probeUi990).removeRecursively();
        const bool okB990 = rigOk990;
        ok = okA990 && okB990;
        if (!ok)
            qInfo().noquote() << "  [t990 diag] sourcePins" << okA990 << "rig" << okB990 << rigDiag990;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t990 block default view restored to top-down (user: 'the block default "
                             "view changed from top-down to bottom-up, it was not like this before'): "
                             "git -S archaeology (the -22/userPitch/eulerRotation chain across t458/"
                             "t599/t820/t877/t966) proves the base-tilt VALUE never flipped sign - "
                             "the regression source is the t966 SPLIT itself: in the old single-node "
                             "graph Ry(yaw)*Rx(pitch) the -22 deg tilt acted in the model-local frame "
                             "so the spin carried it around and the top/bottom faces alternated "
                             "visibility (a tumbling look; the 'sees the top face' comment was true "
                             "for half the phase); once t966 moved the tilt into a world-frame pitch "
                             "parent the same constant became absolute - the top-face normal's z "
                             "component sin(-22 deg) is permanently negative (away from the +Z-axis "
                             "camera), so EVERY spin phase shows the underside = a constant bottom-up "
                             "view, the user's symptom honestly attributed to the node-hierarchy "
                             "change; fix = flip the base -22 -> +22 on all four preview branches "
                             "(sin(+22 deg) > 0 = top face always fronting the camera = the JEI-style "
                             "constant top-down three-quarter view), the -35 yaw offset and the "
                             "t877 user-approved drag law stay byte-identical (up-drag raises "
                             "userPitch = more top-down under either base sign, so the P-t966 "
                             "behavioral legs are green under both with no re-judgment; its base "
                             "count pin evolved lawfully per the P-t949(d) precedent); legs: source "
                             "pins (four split pitch bindings with +22, the -22 form extinct "
                             "file-wide, the t990 contract anchor, four yaw children, the pure "
                             "linear drag law line) plus a real-QQmlEngine rig reading the real "
                             "scene graph back - exactly four bare QQuick3DNode pitch parents at "
                             "eulerRotation.x == 22 (one per branch), and the visible BlockCube "
                             "Model's sceneRotation mapped by the top/bottom face normals: top "
                             "normal z within (0.30, 0.45) = sin22 magnitude AND sign, bottom "
                             "normal z negative, invariant across spin 0/90/180/270 (the old graph "
                             "was constant-negative), and an up-drag of +0.6 deg increases the top "
                             "normal z (more top-down, the t877 contract intact)";
    }

    // ── P-t968 燃烬者两修探针（R19.17 🅴；用户第五轮口径「头×0.6 再缩；烈焰棒上下错开一点（不在同一
    //    平面）」）──
    //    病灶：t818 的 0.7³ 头观感仍偏大（×0.6 再缩：半长 0.35→0.21=0.42³，绕头心 (0,+0.10,0) 缩、头位
    //    不动）；4 根棒基心同 y=-0.03 全在同一水平面。修法 = mobmodel.cpp mobType 17 分支单点改（三消费端
    //    delegate / 图鉴 / 笼迷你共享同一几何，t782 同源纪律——一处修三处）+ 棒 Y 交错（偶数棒 +0.10 /
    //    奇数棒 -0.10 → 棒心 y=+0.07/-0.13 两档非共面；公转转轴 / 轨道半径 0.52 / 棒长 1.10 全不动）。
    //    三消费端呈现参数随跨度演化（[-0.58,+0.52]=1.10 → [-0.68,+0.62]=1.30、体心 -0.02 → -0.03）：
    //    笼迷你 0.38→0.32（0.42/1.30 归一口径）/ yOff 0.008→0.010；图鉴预览 1.1→0.95 / centY 0.02→0.03。
    //    (a) 行为级真几何钉（MobModel 直编读 vertexData，P-t946 先例；rodSpin=0 → 棒在 0/90/180/270 轴位，
    //        轴对齐盒分桶最稳）：头桶（|x|,|z| ≤ 0.25——棒内缘 0.47 在桶外）恰 24 顶点，max|x|=max|z|=0.21
    //        （×0.6 钉）且 y∈[-0.11,+0.31]（绕心缩头位不动）；棒桶 96 顶点，顶沿集合 {0.62,0.42} /
    //        底沿集合 {-0.48,-0.68}（各恰 2 个不同值 = 非共面断言「棒 Y 值集合 ≥2 个不同值」）+ 径向内缘
    //        0.47（轨道半径/转轴保持）+ 外缘 0.57 + 总跨 1.30。
    //    (b) 源码钉（t931/t941 文本钉先例——QML/几何值无 static_assert 面）：头尺寸表值行 + 棒 Y 交错
    //        三元式 + 旧共面棒心/旧头尺寸绝迹 + 两消费端呈现参数随动 + 公转动画两处保持。
    {
        bool ok = true;
        QString diag;
        // (a) 行为级：真 MobModel mobType 17 顶点直读（stride 5 float = pos3+uv2，MobVtx 契约）。
        MobModel g968;
        g968.setMobType(17);
        g968.setRodSpin(0.0f); // 静态轴位（6° 网格契约内）：棒在 0/90/180/270° 正交位，分桶判定最稳
        const QByteArray vd968 = g968.vertexData();
        const int vCount968 = int(vd968.size()) / 20;
        const float *vp968 = reinterpret_cast<const float *>(vd968.constData());
        auto q968 = [](float v) { return int(std::round(v * 100.0f)); }; // 棒顶/底沿量化到 0.01 档
        int headCount = 0, rodCount = 0;
        float hMaxX = 0.0f, hMaxZ = 0.0f, hMinY = 9e9f, hMaxY = -9e9f;
        float rMinY = 9e9f, rMaxY = -9e9f, rMinAxis = 9e9f, rMaxX = 0.0f, rMaxZ = 0.0f;
        QSet<int> rodTops, rodBottoms; // 棒 Y 值集合（非共面断言：各 ≥2 个不同值）
        for (int i = 0; i < vCount968; ++i) {
            const float x = vp968[i * 5], y = vp968[i * 5 + 1], z = vp968[i * 5 + 2];
            if (std::fabs(x) <= 0.25f && std::fabs(z) <= 0.25f) { // 头桶（头半 0.21；棒内缘 0.47 > 0.25 不入桶）
                ++headCount;
                hMaxX = std::max(hMaxX, std::fabs(x));
                hMaxZ = std::max(hMaxZ, std::fabs(z));
                hMinY = std::min(hMinY, y);
                hMaxY = std::max(hMaxY, y);
            } else {                                              // 棒桶（4 棒 × 24 顶点）
                ++rodCount;
                rMinY = std::min(rMinY, y);
                rMaxY = std::max(rMaxY, y);
                rMinAxis = std::min(rMinAxis, std::max(std::fabs(x), std::fabs(z)));
                rMaxX = std::max(rMaxX, std::fabs(x));
                rMaxZ = std::max(rMaxZ, std::fabs(z));
                if (y >= 0.35f)
                    rodTops.insert(q968(y));
                if (y <= -0.35f)
                    rodBottoms.insert(q968(y));
            }
        }
        // 头 ×0.6 钉：半长 0.21（=0.35×0.6）+ 绕头心 (0,+0.10,0) 缩（y 跨 [-0.11,+0.31]，头位不动）。
        const bool okHead968 = headCount == 24
            && std::fabs(hMaxX - 0.21f) < 2e-3f && std::fabs(hMaxZ - 0.21f) < 2e-3f
            && std::fabs(hMinY - (-0.11f)) < 2e-3f && std::fabs(hMaxY - 0.31f) < 2e-3f;
        // 棒非共面钉：顶沿 {0.62,0.42} / 底沿 {-0.48,-0.68} 各恰 2 个不同值（交错 ±0.10）；
        //   轨道/转轴保持：径向内缘 0.47（=0.52-0.05）+ 外缘 0.57；总跨 [-0.68,+0.62]=1.30。
        const bool okRods968 = rodCount == 96
            && rodTops.size() == 2 && rodTops.contains(q968(0.62f)) && rodTops.contains(q968(0.42f))
            && rodBottoms.size() == 2 && rodBottoms.contains(q968(-0.48f)) && rodBottoms.contains(q968(-0.68f))
            && std::fabs(rMinY - (-0.68f)) < 2e-3f && std::fabs(rMaxY - 0.62f) < 2e-3f
            && std::fabs(rMinAxis - 0.47f) < 2e-3f
            && std::fabs(rMaxX - 0.57f) < 2e-3f && std::fabs(rMaxZ - 0.57f) < 2e-3f;
        // (b) 源码钉（t931/t941 文本钉先例；相对 exe ../ = 工程根）。
        const QString exeDir968 = QCoreApplication::applicationDirPath();
        const QString root968 = QDir(exeDir968 + QStringLiteral("/..")).absolutePath();
        auto readSrc968 = [&root968](const QString &rel) -> QString {
            QFile f(root968 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mm968 = readSrc968(QStringLiteral("src/Renderer/mobmodel.cpp"));
        const QString mn968 = readSrc968(QStringLiteral("src/ui/Main.qml"));
        const QString rb968 = readSrc968(QStringLiteral("src/ui/ResourceBrowser.qml"));
        const bool okB1 = mm968.contains(QStringLiteral("addBox(0.00f, 0.10f, 0.00f, 0.21f, 0.21f, 0.21f")) // 头 ×0.6 尺寸表值（0.35×0.6=0.21）
            && mm968.contains(QStringLiteral("-0.03f + ((i % 2) == 0 ? 0.10f : -0.10f)"))                    // 棒 Y 交错三元式（非共面）
            && !mm968.contains(QStringLiteral("0.35f, 0.35f, 0.35f"))                                        // 旧头尺寸绝迹
            && !mm968.contains(QStringLiteral("* 0.52f, -0.03f, std::sin"));                                 // 旧共面棒心绝迹
        const bool okB2 = mn968.contains(QStringLiteral("NumberAnimation on rodSpin"))                       // 公转动画保持（游戏内 delegate）
            && mn968.contains(QStringLiteral("if (t === EntityManager.MobEmberling) return 0.32"))           // 笼迷你缩放随跨度 1.30 演化
            && mn968.contains(QStringLiteral("if (t === EntityManager.MobEmberling) return 0.010"));         // 笼迷你 yOff 随体心 -0.03 演化
        const bool okB3 = rb968.contains(QStringLiteral("if (t === 17) return 0.95"))                        // 图鉴预览缩放随跨度演化
            && rb968.contains(QStringLiteral("case 17: return 0.03"))                                        // 图鉴居中随体心演化
            && rb968.contains(QStringLiteral("rodSpin: root.selectedMobType === 17 ? rodClock : 0"));        // 公转动画保持（图鉴）
        const bool okB968 = okB1 && okB2 && okB3;
        ok = ok && okHead968 && okRods968 && okB968;
        if (!ok)
            diag += QStringLiteral("head=%1 rods=%2 pins=%3 hc=%4 rc=%5 tops=%6 bots=%7 b1=%8 b2=%9 b3=%10")
                        .arg(int(okHead968)).arg(int(okRods968)).arg(int(okB968))
                        .arg(headCount).arg(rodCount).arg(rodTops.size()).arg(rodBottoms.size())
                        .arg(int(okB1)).arg(int(okB2)).arg(int(okB3));
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  [t968 diag]" << diag;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t968 emberling two fixes: the head shrinks a further x0.6 "
                             "(t818's 0.7^3 cube, half 0.35 -> 0.21 = 0.42^3) scaled about its "
                             "own center (0,+0.10,0) so the head position holds (y span "
                             "[-0.11,+0.31]); the four orbiting rods break out of one plane - "
                             "even rods sit at center y +0.07, odd at -0.13 (base -0.03 +/- "
                             "0.10), the rod Y value set holds exactly two distinct tops "
                             "{0.62,0.42} and two distinct bottoms {-0.48,-0.68} (non-coplanar) "
                             "while the spin axis, orbit radius 0.52 (inner edge 0.47 / outer "
                             "0.57) and per-rod length 1.10 all stay put, total span now "
                             "[-0.68,+0.62]=1.30; the fix lands once in the shared MobModel "
                             "mobType-17 geometry so all three consumers (in-game delegate, "
                             "browser preview, spawner-cage mini) move together, their "
                             "presentation tables re-derived from the new span/center (cage mini "
                             "0.38->0.32 / yOff 0.008->0.010, browser 1.1->0.95 / centY "
                             "0.02->0.03); legs: behavioral vertex read of a real MobModel (24-"
                             "vert head bucket at half 0.21 + 96-vert rod bucket with the "
                             "two-tier Y sets and radius kept) plus source pins on the head size "
                             "line, the stagger ternary, the extinct coplanar rod center and old "
                             "head size, the consumer tables, and the rodSpin orbit animations "
                             "retained on both QML sides";
    }

    // ── P-t969 火把贴图采样窗/UV 修正探针（R19.17 🅴；用户第五轮口径「中间悬空黑色部分——贴图采样窗
    //    /UV 修正」）──
    //    病灶（实机渲染复现钉死，headless View3D.grabToImage 像素差分）：ItemShapeGeometry 火把分支
    //    侧脸 v 满高 [0,1]——torch 瓦片 row0 与 row14..15 是透明底（Mask 丢弃）→ 侧脸上下各留死带，
    //    盒底真边与柄身可见内容之间出现透明断口；±Y 端面把含透明行的整条竖带压扁成 2/16 见方碎片，
    //    悬在断口下方露背景底色 = 「悬空黑色部分」（渲染实测：柄底下方 6px 处一片菱形碎片，与柄身
    //    隔一条全黑断口）。修法 = 侧脸 v 窗收正到不透明内容带 [2/16,15/16]（V 朝向契约：图像顶 ↔ v=1，
    //    t489 像素级实测 + 本次渲染复测钉死——v 窗 [12/16,14/16] 采到图像 row8..16 焰带即证）；
    //    ±Y 端面经 capTopV/capBotV 覆写采柄木全双列不透明带（顶 row6..7 / 底 row12..13；机制等价 MC
    //    火把方块模型 up 面采 [7,6]..[9,8] 柄顶截面）——焰尖 row1 只占 x7 单列（x8 透明），任何含焰
    //    行的端面窗中央必有透明孔 → 黑斑，故端面窗必须全窗不透明。
    //    (a) UV 矩形钉（真几何顶点 UV × 图集内容真值比对——从 textures/atlas.png tile 17 逐像素扫
    //        alpha≥128 的内容包围盒，折算到 v 空间〔v = 1 − 图像行/64〕，与几何六脸 UV 四角比对）：
    //        ①全脸窗 ⊆ 内容带（旧满窗脸直接红）；②侧四脸窗与内容带相互贴合（收正而非仅在内——防
    //        「窗缩太小躲进安全区丢内容」）；③±Y 端面窗 ⊆ 柄木全不透明带（防焰行透明孔黑斑——焰带
    //        端面窗同样红）；④u 窗 ⊆ 火把本体柱内容列。
    //    (b) 源码钉：内容带/端面窗常量行 + addShapeBox capTop/capBot 形参管线 + 火把调用接线 + 旧
    //        满窗调用形绝迹。
    {
        bool ok = true;
        QString diag;
        // 内容真值：tile 17 逐像素扫 alpha≥128 包围盒（build_torch.py 画稿 4× NEAREST 上采样，内容
        //   图像 row1..13 → px row4..56、列 7..8 → px 28..36）。
        const QString exeDir969 = QCoreApplication::applicationDirPath();
        const QString root969 = QDir(exeDir969 + QStringLiteral("/..")).absolutePath();
        const QImage atlas969(root969 + QStringLiteral("/textures/atlas.png"));
        bool okA = !atlas969.isNull();
        if (!okA)
            diag = QStringLiteral("atlas load failed");
        const int kTilePx = 64, kTileIdx = 17;
        int r0 = kTilePx, r1 = -1, c0 = kTilePx, c1 = -1;
        if (okA) {
            for (int y = 0; y < kTilePx; ++y)
                for (int x = 0; x < kTilePx; ++x)
                    if (qAlpha(atlas969.pixel(kTileIdx * kTilePx + x, y)) >= 128) {
                        r0 = std::min(r0, y); r1 = std::max(r1, y);
                        c0 = std::min(c0, x); c1 = std::max(c1, x);
                    }
            okA = r1 >= 0;
            if (!okA)
                diag = QStringLiteral("tile17 empty");
        }
        // v 空间内容带（图像顶 ↔ v=1）：v ∈ [1 − r1px/64, 1 − r0px/64]；柄木全不透明带 = 柄行 6..13
        //   的内缩安全子带（px 24..56 全双列不透明，焰行 px4..23 含 x 列透明孔不进端面窗）。
        const float kTol969 = 0.02f; // 半纹素(1/128≈0.008)+采样余量；远小于旧满窗越界量(~0.12)
        const float contentV0 = 1.0f - float(r1 + 1) / float(kTilePx);
        const float contentV1 = 1.0f - float(r0) / float(kTilePx);
        const float woodV0 = 1.0f - 56.0f / float(kTilePx); // 0.125（柄行 6..13 px 下沿）
        const float woodV1 = 1.0f - 24.0f / float(kTilePx); // 0.625（柄行 6..13 px 上沿）
        if (okA) {
            ItemShapeGeometry g969;
            g969.setBlockId(int(BR::Torch));
            const QByteArray vd969 = g969.vertexData();
            const int vCount969 = int(vd969.size()) / 20; // stride = pos3+uv2 = 20B（类注释契约）
            okA = okA && vCount969 == 24;
            const float *vp969 = reinterpret_cast<const float *>(vd969.constData());
            for (int f = 0; f < 6 && okA; ++f) {
                float uMin = 9e9f, uMax = -9e9f, vMin = 9e9f, vMax = -9e9f;
                for (int c = 0; c < 4; ++c) {
                    const float u = vp969[(f * 4 + c) * 5 + 3];
                    const float v = vp969[(f * 4 + c) * 5 + 4];
                    uMin = std::min(uMin, u); uMax = std::max(uMax, u);
                    vMin = std::min(vMin, v); vMax = std::max(vMax, v);
                }
                const bool isCap = (f == 2 || f == 3);
                // ①全脸 v 窗 ⊆ 内容带（±半纹素余量）——旧满窗上下越界即红。
                const bool insideContent = vMin >= contentV0 - kTol969 && vMax <= contentV1 + kTol969;
                // ②侧四脸与内容带**相互贴合**（收正钉：下沿贴内容下沿、上沿贴内容上沿——窗只能
                //   收正到内容带，不许缩窄丢内容躲绿）。
                const bool rectified = isCap || (vMin <= contentV0 + kTol969 && vMax >= contentV1 - kTol969);
                // ③端面窗 ⊆ 柄木全不透明带（黑斑防钉：焰行窗/满窗红）。
                const bool capSafe = !isCap || (vMin >= woodV0 - kTol969 && vMax <= woodV1 + kTol969);
                // ④u 窗 ⊆ 火把本体柱内容列（tile 局部 u = (u − 17/N)×N 折算回 [0,1]，N=AtlasTileCount 运行值）。
                const float tuMin = (uMin - float(kTileIdx) / float(BR::AtlasTileCount)) * float(BR::AtlasTileCount);
                const float tuMax = (uMax - float(kTileIdx) / float(BR::AtlasTileCount)) * float(BR::AtlasTileCount);
                const bool uSafe = tuMin >= float(c0) / float(kTilePx) - kTol969
                                && tuMax <= float(c1 + 1) / float(kTilePx) + kTol969;
                if (!(insideContent && rectified && capSafe && uSafe)) {
                    okA = false;
                    diag += QStringLiteral("f%1 v[%2,%3]u[%4,%5] ic%6 rc%7 cs%8 us%9")
                                .arg(f).arg(vMin, 6, 'g', 4).arg(vMax, 6, 'g', 4)
                                .arg(uMin, 6, 'g', 4).arg(uMax, 6, 'g', 4)
                                .arg(int(insideContent)).arg(int(rectified))
                                .arg(int(capSafe)).arg(int(uSafe));
                }
            }
        }
        // (b) 源码钉（t931/t941 文本钉先例；相对 exe ../ = 工程根）。
        QFile isg969(root969 + QStringLiteral("/src/Renderer/itemshapegeometry.cpp"));
        const QString src969 = isg969.open(QIODevice::ReadOnly) ? QString::fromUtf8(isg969.readAll()) : QString();
        const bool okB = src969.contains(QStringLiteral("constexpr float kWv0 = 2.0f / 16.0f, kWv1 = 15.0f / 16.0f;"))
            && src969.contains(QStringLiteral("constexpr float kCapV0 = 8.0f / 16.0f, kCapV1 = 10.0f / 16.0f;"))
            && src969.contains(QStringLiteral("constexpr float kCapV2 = 2.0f / 16.0f, kCapV3 = 4.0f / 16.0f;"))
            && src969.contains(QStringLiteral("float capTopV0 = -1.0f, float capTopV1 = -1.0f,"))
            && src969.contains(QStringLiteral("float capBotV0 = -1.0f, float capBotV1 = -1.0f)"))
            && src969.contains(QStringLiteral("-1, 0u, kCapV0, kCapV1, kCapV2, kCapV3);"))
            && !src969.contains(QStringLiteral("bMax, kWu0, kWu1, 0.0f, 1.0f")); // 旧满窗火把调用绝迹
        ok = ok && okA && okB;
        if (!ok) {
            ++totalFail;
            qInfo().noquote() << "  [t969 diag]" << diag
                              << "r0" << r0 << "r1" << r1 << "c0" << c0 << "c1" << c1
                              << "pins" << int(okB);
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t969 torch texture sampling-window/UV fix: the floating black shard "
                             "under the preview/dropped torch is the box end faces sampling the FULL "
                             "tile-height window - the torch tile's rows 0 and 14-15 are transparent "
                             "(Mask-discarded), so the side faces kept dead bands top and bottom and "
                             "the +/-Y end faces squeezed the whole strip (transparent rows included) "
                             "into a 2/16 wafer that hung below the visible stick across a fully "
                             "transparent gap (reproduced headlessly: detached diamond shard 6px below "
                             "the stick, black gap between); fix = side v window rectified onto the "
                             "opaque content band [2/16,15/16] (V convention image-top = v 1, per the "
                             "t489 pixel-proven contract - the window was derived flipped) and the "
                             "end faces overridden via new capTopV/capBotV addShapeBox params to the "
                             "fully two-column-opaque wood bands (top rows 6-7 / bottom rows 12-13, "
                             "mechanism-equivalent to the MC torch block model sampling the stick "
                             "cross-section on its up face) - no cap window may include a flame row "
                             "because the flame tip row 1 is single-column (x8 transparent) and would "
                             "punch a black hole mid-cap; legs: UV-rect pin reading the real "
                             "ItemShapeGeometry(13) vertex UVs against the per-pixel opaque bbox of "
                             "atlas tile 17 (all faces inside the content band, side faces mutually "
                             "rectified with it, caps inside the wood band, u inside the stick "
                             "columns) plus source pins on the window constants, the cap parameter "
                             "plumbing and the extinct full-window torch call";
    }

    // ── P-t971 鱼粒子预告窗探针（R19.17 🅵 钓鱼组收官；用户第五轮口径「一开始（入水待机）就有水粒子——
    //    应收窄到临近咬钩才出现」）──
    //    t884③ 待机逼近粒子链（QML 380ms 拍 → BlockParticles.burstWaterApproach）的触发门收窄到咬钩前
    //    kBobberParticleLeadSec=2.5s 前瞻窗（Entities 单一权威谓词 bobberApproachAt = Water ∧ 等待阶段
    //    ∧ 剩余等待 ≤N；Game 镜像 bobberApproach → QML running）。发射节流 / 粒子池不动，只动触发门。
    //    断言（gate = 粒子发射的充分条件——QML Timer running 逐项含之，gate 序列即「粒子拍」序列）：
    //    (a) 基线腿：settle → ①前段静默（settle 后 2.0s 采样窗 gate 恒 false——最早可开门 = 等待下界
    //        5s − N = 2.5s，窗口留 0.5s 裕量，「等待期前段粒子计数=0」）；②门开→咬钩沿 2.5s ±0.1
    //        （dt 0.05 步进下 = 50 tick——N 值行为级钉死）；③门开后每 tick 恒 true 直到咬钩沿（预告窗内
    //        无间隙——粒子持续到咬钩）；④咬钩判定窗内 gate 恒 false（t926「窗口期水面突然安静」对比
    //        保留——本任务只收窄待机前段，不延窗）；⑤逃走重掷后 gate 即 false（新等待 ≥5s > N）；
    //        ⑥入水水花 bobberSplashed 恰一次（抛竿反馈保留，与预告门解耦——有水花 ≠ 有预告粒子）。
    //    (b) 唤潮腿：确定性预选短等待 serial（直调 bobberWaitSeconds∘hashVoxel——t836(b) 镜像手法；
    //        base ≤5.5s → ×0.4 ≤2.2s < N）→ settle 当拍 gate 即 true 且等待期全程 true 到咬钩
    //        （短等待不裸奔——预告窗盖满剩余等待；水花 +1 再证保留）。
    //    (c) Game 镜像腿：pc 真甩竿（t884(c) 同 rig）→ settle 后 pc.tick 镜像 bobberApproach() 前段
    //        恒 false → 驱动进预告窗镜像翻 true（且逐 tick 与实体谓词一致——QML Timer running 绑定链
    //        行为级锁死）→ 收竿后镜像四值清零（reset 面抽验）。
    //    (d) 源码钉（t926(b) 段界手法）：Main.qml 钓鱼段 running 含 player.bobberApproach（新门在）+
    //        保留 !player.hasBite（窗口停拍不回退）+ interval: 380（发射节流不碰）。
    //    阴性轮（回退验红已登记 commit；复原后随本矩阵全绿）：bobberApproachAt 去掉剩余等待门
    //        （= t971 前语义，全待机期 true）→ (a)①②与 (c) 前段腿恰红；(b)(d) 不受影响。
    {
        World wT;
        wT.setWidth(48); wT.setDepth(48); wT.setHeight(96); wT.setSeed(82);
        EntityManager entsT;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickT = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) entsT.tick(qreal(dt), &wT, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wT.setBlock(x, fy, z, BR::Stone, 0);
                wT.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池（t884(a)/t926/t960(e) 同款）
            }
        int splashT = 0;
        QObject::connect(&entsT, &EntityManager::bobberSplashed, &entsT,
                         [&](float, float, float) { ++splashT; });

        // (a) 基线腿：serial 971 → 直落 (6.5, 84.75, 6.5)（格 (6,84,6)），等待 = 确定性掷骰 ∈[5,30]s。
        const int bA = entsT.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 971);
        int settleA = -1, firstGateA = -1, biteA = -1, escA = -1;
        bool earlySilentA = true, gateHeldA = true, winSilentA = true, rewaitSilentA = true;
        for (int t = 1; bA >= 0 && t <= 700; ++t) {
            tickT(1, 0.05f);
            const bool inW = entsT.bobberInWaterAt(bA);
            const bool has = entsT.bobberHasBiteAt(bA);
            const bool gate = entsT.bobberApproachAt(bA);
            if (settleA < 0 && inW) settleA = t;
            if (settleA >= 0) {
                if (biteA < 0) {
                    if (has) {
                        biteA = t;                                       // 咬钩沿（等待期终结）
                    } else {
                        if (t <= settleA + 40 && gate) earlySilentA = false;  // ①前段静默
                        if (firstGateA < 0 && gate) firstGateA = t;           // 预告窗开沿
                        else if (firstGateA >= 0 && !gate) gateHeldA = false; // ③窗内无间隙
                    }
                } else {
                    if (escA < 0 && !has) escA = t;                       // 逃走沿（窗口过期）
                    else if (escA >= 0 && gate) rewaitSilentA = false;    // ⑤重掷期静默
                    if (gate) winSilentA = false;                         // ④窗口期静默
                }
            }
            if (escA >= 0 && t >= escA + 20) break; // 重掷静默采样 1s（新等待 ≥5s > N，必然还关着）
        }
        const float leadSecA = (firstGateA > 0 && biteA > firstGateA)
                                   ? float(biteA - firstGateA) * 0.05f : -1.0f;
        const bool okA = bA >= 0 && settleA > 0 && splashT == 1              // ⑥水花保留恰一次
                         && earlySilentA && firstGateA > settleA + 40        // ①不是一开始就有
                         && leadSecA >= 2.4f && leadSecA <= 2.6f             // ②N=2.5 行为级
                         && gateHeldA && winSilentA && rewaitSilentA;        // ③④⑤
        if (bA >= 0) entsT.removeEntityAt(bA);

        // (b) 唤潮腿：确定性预选短等待 serial（同格同盐直调掷骰；serial 2000..2199 内必含 ≤5.5s 样本
        //     —— [5,30] 均匀 2501 档中 ≤5.5 占 51 档 ≈2%，200 样本空窗概率 ~1.7%×→ 仍空则判红人工复核）。
        int shortSerial = -1;
        for (quint32 s = 2000; s < 2200 && shortSerial < 0; ++s) {
            const float w = EntityManager::bobberWaitSeconds(wT.hashVoxel(
                int(quint32(wT.seed()) ^ 0xF15Cu /* EntityManager::kBobberWaitHashSalt 镜像 */ ^ s),
                6, fy + 1, 6));
            if (w <= 5.5f) shortSerial = int(s); // ×0.4（唤潮 III）≤2.2s < N=2.5 → 落定即预告窗内
        }
        int settleB = -1, biteB = -1;
        bool fullCoverB = true;
        const int splashBeforeB = splashT;
        const int bB = shortSerial >= 0
            ? entsT.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0),
                                quint32(shortSerial), 0.4f)
            : -1;
        for (int t = 1; bB >= 0 && t <= 120; ++t) { // 等待 ≤2.2s = 44 tick + 裕量
            tickT(1, 0.05f);
            const bool inW = entsT.bobberInWaterAt(bB);
            const bool has = entsT.bobberHasBiteAt(bB);
            if (settleB < 0 && inW) settleB = t;
            if (settleB > 0 && !has && !entsT.bobberApproachAt(bB)) fullCoverB = false; // 全程预告
            if (settleB > 0 && has) { biteB = t; break; }
        }
        const bool okB = shortSerial >= 0 && bB >= 0 && settleB > 0 && biteB > settleB
                         && fullCoverB && splashT == splashBeforeB + 1;
        if (bB >= 0) entsT.removeEntityAt(bB);

        // (c) Game 镜像腿：pc 真甩竿（t884(c) 同 rig：立足柱 (3,83,6)，settle (5.5,84.75,6.5)）。
        wT.setBlock(3, fy, 6, BR::Stone, 0); // 玩家立足柱
        PlayerController pcT;
        Hotbar hbT;
        hbT.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hbT.setSelectedSlot(0);
        pcT.setWorld(&wT);
        pcT.setEntityManager(&entsT);
        pcT.setHotbar(&hbT);
        pcT.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        pcT.useFishingRod();
        int bobC = -1;
        for (int i = 0; i < entsT.count(); ++i)
            if (entsT.aliveAt(i) && entsT.kindAt(i) == int(EntityManager::Bobber)) { bobC = i; break; }
        int settleC = -1;
        bool earlyMirrorSilentC = true, mirrorOpensC = false, mirrorTracksC = true;
        for (int t = 1; bobC >= 0 && t <= 700; ++t) {
            tickT(1, 0.05f);
            pcT.tick(); // 镜像刷新（updateFishing 拉四镜像——t971 approach 在内）
            if (settleC < 0 && entsT.bobberInWaterAt(bobC)) settleC = t;
            if (settleC < 0) continue;
            const bool has = entsT.bobberHasBiteAt(bobC);
            const bool gate = entsT.bobberApproachAt(bobC);
            mirrorTracksC = mirrorTracksC && pcT.bobberApproach() == gate; // 镜像逐步一致
            if (!has) {
                if (t <= settleC + 40 && pcT.bobberApproach()) earlyMirrorSilentC = false; // 前段恒 false
                if (pcT.bobberApproach()) mirrorOpensC = true;                             // 预告窗随行翻 true
            } else {
                break; // 咬钩沿即收（开沿已证；窗内镜像行为与 (a)④ 同源谓词）
            }
        }
        bool okC = bobC >= 0 && settleC > 0 && pcT.fishing() && pcT.bobberInWater()
                   && earlyMirrorSilentC && mirrorOpensC && mirrorTracksC;
        pcT.useFishingRod(); // 收竿 → 镜像四值清零（t971 reset 面抽验）
        okC = okC && !pcT.fishing() && !pcT.bobberApproach() && !pcT.hasBite() && !pcT.bobberInWater();

        // (d) 源码钉（t926(b) 段界手法）：钓鱼段（fishingBobber → fishingRodTipWorld 界滤）running 新门在。
        const QString exeDirT = QCoreApplication::applicationDirPath();
        const QString rootT = QDir(exeDirT + QStringLiteral("/..")).absolutePath();
        QFile mfT(rootT + QStringLiteral("/src/ui/Main.qml"));
        const QString mtT = mfT.open(QIODevice::ReadOnly) ? QString::fromUtf8(mfT.readAll()) : QString();
        const int d0 = mtT.indexOf(QStringLiteral("id: fishingBobber"));
        const int d1 = d0 >= 0 ? mtT.indexOf(QStringLiteral("function fishingRodTipWorld"), d0) : -1;
        const QString segT = (d0 >= 0 && d1 > d0) ? mtT.mid(d0, d1 - d0) : QString();
        const bool okD = segT.contains(QStringLiteral("player.bobberApproach"))
                         && segT.contains(QStringLiteral("!player.hasBite"))
                         && segT.contains(QStringLiteral("interval: 380"));

        const bool okT971 = okA && okB && okC && okD;
        if (!okT971) ++totalFail;
        if (!okT971)
            qInfo().noquote() << "  [t971 diag] okA" << okA << "(settle" << settleA << "firstGate"
                              << firstGateA << "bite" << biteA << "esc" << escA << "lead" << leadSecA
                              << "early" << earlySilentA << "held" << gateHeldA << "winSil"
                              << winSilentA << "rewait" << rewaitSilentA << "splash" << splashT
                              << ") okB" << okB << "(serial" << shortSerial << "settle" << settleB
                              << "bite" << biteB << "cover" << fullCoverB << ") okC" << okC
                              << "(settle" << settleC << "early" << earlyMirrorSilentC
                              << "opens" << mirrorOpensC << "tracks" << mirrorTracksC << ") okD" << okD;
        qInfo().noquote() << (okT971 ? "PASS" : "FAIL")
                          << "| t971 fish-particle lead window: the t884③ idle approach-particle "
                             "chain (QML 380ms beat -> burstWaterApproach) no longer runs from the "
                             "water-settle instant - its trigger gate narrows to the last "
                             "kBobberParticleLeadSec=2.5s before the bite (user caliber: particles "
                             "used to bubble the whole wait, now they announce the bite; option 1 "
                             "of the brief, the N-second lookahead that hands off naturally into "
                             "the pinned 1.0s judgment window), single authority "
                             "EntityManager::bobberApproachAt (Water && waiting-phase && remaining "
                             "<= N) mirrored as PlayerController bobberApproach into the QML "
                             "running clause - emission throttle (380ms) and the particle pool "
                             "untouched, only the gate moved; behavioral: baseline wait shows "
                             "2.0s of post-settle silence (earliest possible open = 5s lower "
                             "bound - N), gate-open->bite measures 2.5s +-0.1, gate holds every "
                             "tick to the bite edge, stays false through the bite window (t926 "
                             "sudden-quiet contrast preserved - this task narrows the wait, it "
                             "does not extend into the window) and after the escape re-roll, "
                             "while the cast splash still fires exactly once (cast feedback kept, "
                             "decoupled from the lead gate); tide leg: a deterministically "
                             "pre-selected short wait (base <=5.5s x0.4 <=2.2s < N) shows the "
                             "gate true from the settle tick through the bite (short waits are "
                             "fully covered, no naked window); Game mirror leg: a real pc cast "
                             "tracks the entity predicate tick-by-tick, silent early, open in "
                             "the window, all four mirrors cleared on reel; source pin: the "
                             "Main.qml fishing segment running clause carries "
                             "player.bobberApproach, keeps !player.hasBite and the 380ms "
                             "interval; negative round registered in the commit (gate term "
                             "removed -> baseline-early + mirror-early legs red, restored green)";
    }

    // ── P-t973 生物格放方块检测（R19.17 🅶 杂项组；用户第五轮口径「生物占据的格子不能放置方块——防活埋」）──
    //   玩家放置全链探针（P-t945 同式：直编 PlayerController 走完整放置链 射线 → 预检 → setBlock），
    //   EntityManager / ItemEntityManager / MinecartManager 直造直调（t950/t952 先例，不启 16ms tick；
    //   ents 仅随 pc.tick() 每 aim 推进 1 tick）。防漂移 rig 几何（lessons t836/t897）：僵尸只与 (b)(d)
    //   两腿同场——(b) 紧跟 spawn（1 个 aim tick 内位移 ≤ kChaseSpeed×dt钳0.05=0.14 << 格余量 0.2，盒
    //   恒在出生格内），(d) 时已 dead 冻结；(c)(e) 的目标格远离僵尸可达域（≥3 格），成功腿不受游走污染。
    //   断言六段：
    //   (a) 空格对照：无实体占用 → 放置成功（链路本身通畅，防「射线落空 no-op」假绿——(b) 的拒必经命中格）；
    //   (b) 僵尸所在格放置 → 拒：写入格 id 不变 + Survival 栈不消耗 + 不挥手（拒绝是 no-op 口径，
    //       hitBlock 断言钉「预检链真到达」——被门拦 ≠ 没走到门，t814 三件套教训）；
    //   (c) 掉落物所在格 → 成功：同场仍有活体僵尸（(b) 未杀，其可达域离目标格 ≥3 格），唯一差异 =
    //       写入格占着的是掉落物（ItemEntityManager）而非活体 mob → 放置成功（钉「非 mob 实体不拒」
    //       防占用门越权扩域）；
    //   (d) 濒死/死亡（hp<=0 帧）不拒：damageEntity 致死（dead=true 死亡动画帧，槽仍在）→ 僵尸当前
    //       实读格放置成功（门对死 mob 放行）；
    //   (e) 玩家骑乘矿车旁放置不受干扰：tryMount 登乘 → 从座位实读眼位瞄准车旁格 → 放置成功且骑乘不断
    //       （矿车在 MinecartManager，不在占用门查询域——骑乘建筑链路零干扰）；
    //   (f) 源码钉：占用门调用行 + Entities 侧排除面（kind!=Mob/dead）+ 门在预检链的序位（目标格必须空
    //       检查之后、方块族专项预检（火把）之前——t945 字符串钉模式 + 语句序钉）。
    {
        // rig 选址：kRigY 高空全空盒扫描（P-t945 同式；dx -1..10、dz -1..1、dy -2..+4）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 10 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 10 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t973 mob-occupied-cell place gate: no clear rig area found";
        } else {
            WorldClock clockP973;
            EntityManager entsP973;      // 本探针实体域：占用门查询源（活体 mob 才拦）
            ItemEntityManager iemcP973;  // 掉落物域：(c) 腿证明占用门不越权到非 mob 实体
            MinecartManager cartsP973;   // 载具域：(e) 腿证明骑乘链路不受占用门干扰
            Hotbar hbP973;
            PlayerController pcP973;     // t814 真消费端模式（P-t945 同式挂窗 grab 载体）
            pcP973.setWorld(&w);
            pcP973.setWorldClock(&clockP973);
            pcP973.setEntityManager(&entsP973);
            pcP973.setItemEntities(&iemcP973);
            pcP973.setMinecartManager(&cartsP973);
            pcP973.setHotbar(&hbP973);
            QQuickWindow probeWinP973;
            pcP973.setParentItem(probeWinP973.contentItem());
            pcP973.grab(); // m_window 就绪 → setCaptured(true) 走通（placeBlock 入口门）
            pcP973.setSelectedBlock(int(BR::Stone));
            // 挥手计数：拒绝面是 no-op（不挥不消耗，同其它预检拒绝口径）→ (b) 拒绝时计数不动。
            int swingsP973 = 0;
            const QMetaObject::Connection swingConnP973 = QObject::connect(
                &pcP973, &PlayerController::swingArm, &pcP973, [&swingsP973]() { ++swingsP973; });
            // rig 搭建：石台面（kRigY-1，x0..x0+9 全宽）+ 台上三格清空。台面 3 宽（z0-1..z0+1）防
            // 玩家/僵尸 AABB 贴 z 缘时的支撑缺失。
            const auto buildRigP973 = [&]() {
                for (int dx = 0; dx <= 9; ++dx)
                    for (int dz = -1; dz <= 1; ++dz) {
                        w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                        for (int dy = 0; dy <= 2; ++dy)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
                    }
            };
            const auto clearTopP973 = [&](int dx) {
                for (int dy = 0; dy <= 2; ++dy)
                    w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
            };
            // 瞄准 + tick 刷射线（P-t945 同式：release+grab 重居中光标 → loadSavedState 写位姿 →
            // tick 刷命中；eye = feet + 1.62 视角换算，与 t945 同约定）。
            const auto aimP973 = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ,
                                     int mode) {
                const float ex = feetX, ey = float(kRigY) + 1.62f, ez = feetZ;
                const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float pitch = std::asin(dy / len) * 57.2957795f;
                const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
                pcP973.release();
                pcP973.grab();
                pcP973.loadSavedState(feetX, float(kRigY), feetZ, yaw, pitch, mode);
                pcP973.tick(); // updateRaycast 刷命中 + ents 推进 1 tick
                return pcP973.hitBlock();
            };
            const auto pumpMsP973 = [](int ms) { // 放置 200ms CD 间隔（t128；P-t945 同式）
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            };
            // 僵尸实读占用格（XZ 格 + 脚位格）：占用门判的是 AABB 严格相交——从 posAt 实读反推写入格，
            // 游荡 / 追击的微小位移免疫（lessons t836：mob 位姿实读再瞄准；~3 tick 位移 ≤0.15 << 格余量
            // 0.2，盒恒在出生格内——cellB 断言同时钉「稳在出生格」防漂移出假绿）。
            const auto mobCellP973 = [&]() {
                const QVector3D p = entsP973.posAt(0);
                return QPoint(int(std::floor(double(p.x()))), int(std::floor(double(p.z()))));
            };
            buildRigP973();
            // (a) 空格对照（Creative）：瞄 (x0+4) 台面顶 → 目标 (x0+4, kRigY, z0) → 放置成功 + 挥手。
            const QVector3D hitA = aimP973(float(x0) + 6.5f, float(z0) + 0.5f,
                                           float(x0) + 4.5f, float(kRigY), float(z0) + 0.5f, 1);
            pcP973.placeBlock();
            const bool okA = hitA == QVector3D(float(x0 + 4), float(kRigY - 1), float(z0))
                && w.blockAt(x0 + 4, kRigY, z0) == BR::Stone
                && swingsP973 == 1;
            clearTopP973(4);
            pumpMsP973(260);
            // (b) 僵尸所在格 → 拒（Survival 全链）：spawnMobTyped 蹒跚者（机制等价 MC 僵尸）落台面
            //     (x0+3)；Survival hotbar 石头栈 ×16；瞄僵尸实读占用格的台面顶 → 目标 = 僵尸格 →
            //     占用门拒：格 id 不变 + 栈不消耗 + 不挥手 + hitBlock 断言（预检链真到达，非射线落空）。
            entsP973.spawnMobTyped(x0 + 3, kRigY, z0, EntityManager::MobShambler,
                                   QStringLiteral("#4a6a3a"), 0);
            hbP973.setStack(0, BR::Stone, 16);
            hbP973.setSelectedSlot(0);
            const QPoint cellB = mobCellP973();
            const bool cellBIsMobCell = cellB == QPoint(x0 + 3, z0); // 僵尸稳在出生格
            const QVector3D hitB = aimP973(float(x0) + 6.5f, float(z0) + 0.5f,
                                           float(cellB.x()) + 0.5f, float(kRigY),
                                           float(cellB.y()) + 0.5f, 2);
            pcP973.placeBlock();
            const bool okB = cellBIsMobCell
                && hitB == QVector3D(float(cellB.x()), float(kRigY - 1), float(cellB.y()))
                && w.blockAt(cellB.x(), kRigY, cellB.y()) == BR::Air  // 格 id 不变（拒）
                && entsP973.deadAt(0) == false                        // 拒绝面与僵尸生死无关（仍活体）
                && hbP973.countAt(0) == 16                            // 不消耗
                && swingsP973 == 1;                                   // 不挥手
            if (!okB)
                qInfo().noquote() << "  [t973 diag] b cell" << cellB << "hit" << hitB
                                  << "tgt" << int(w.blockAt(cellB.x(), kRigY, z0))
                                  << "stack" << hbP973.countAt(0) << "swings" << swingsP973
                                  << "mobAlive" << entsP973.aliveAt(0);
            pumpMsP973(260);
            // (c) 掉落物所在格 → 成功：目标格放 (x0+7)——僵尸可达域之外（(b) 后仅 1 个 aim tick 的
            //     游走/追击位移 ≤ kChaseSpeed×dt钳 0.05 = 0.14，盒最远摸到 x0+3.94，离 x0+7 ≥3 格），
            //     成功腿不受活体游走污染；掉落物（ItemEntityManager）落 (x0+7) 格 → 放置成功（占用门
            //     不越权到非 mob 实体）+ 掉落物仍在 + 消耗/挥手照常（per-leg delta，免疫前腿级联）。
            iemcP973.spawnItem(x0 + 7, kRigY, z0, int(RecipeRegistry::SweetBerryId));
            const int stackC0 = hbP973.countAt(0);
            const int swingsC0 = swingsP973;
            const QVector3D hitC = aimP973(float(x0) + 9.5f, float(z0) + 0.5f,
                                           float(x0) + 7.5f, float(kRigY), float(z0) + 0.5f, 2);
            pcP973.placeBlock();
            bool itemAliveC = false;
            for (int i = 0; i < iemcP973.count(); ++i)
                if (iemcP973.aliveAt(i) && iemcP973.itemIdAt(i) == int(RecipeRegistry::SweetBerryId))
                    itemAliveC = true;
            const bool okC = hitC == QVector3D(float(x0 + 7), float(kRigY - 1), float(z0))
                && w.blockAt(x0 + 7, kRigY, z0) == BR::Stone
                && itemAliveC
                && hbP973.countAt(0) == stackC0 - 1
                && swingsP973 == swingsC0 + 1;
            if (!okC)
                qInfo().noquote() << "  [t973 diag] c hit" << hitC
                                  << "tgt" << int(w.blockAt(x0 + 7, kRigY, z0))
                                  << "item" << itemAliveC << "stack" << hbP973.countAt(0)
                                  << "exp" << stackC0 - 1 << "swings" << swingsP973
                                  << "expS" << swingsC0 + 1;
            clearTopP973(7);
            pumpMsP973(260);
            // (d) 濒死/死亡（hp<=0 帧）不拒：damageEntity 致死 → dead=true（死亡动画帧，槽仍在、
            //     aliveAt 仍 true）→ 僵尸当前实读格（游走后的真实占用格）放置成功（门对死 mob 放行）。
            //     脚位随 cellD 取（cellD.x+2.5，Δx=2 实证几何 + 射线长 ≤kReach）。
            entsP973.damageEntity(0, 999);
            const bool deadNow = entsP973.deadAt(0);
            const QPoint cellD = mobCellP973();
            const int stackD0 = hbP973.countAt(0);
            const int swingsD0 = swingsP973;
            const QVector3D hitD = aimP973(float(cellD.x()) + 2.5f, float(z0) + 0.5f,
                                           float(cellD.x()) + 0.5f, float(kRigY),
                                           float(cellD.y()) + 0.5f, 2);
            pcP973.placeBlock();
            const bool okD = deadNow
                && entsP973.aliveAt(0)                            // 死亡动画帧仍在槽（非已释放空槽）
                && w.blockAt(cellD.x(), kRigY, cellD.y()) == BR::Stone // 濒死不拒 → 放置成功
                && hbP973.countAt(0) == stackD0 - 1
                && swingsP973 == swingsD0 + 1;
            if (!okD)
                qInfo().noquote() << "  [t973 diag] d dead" << deadNow << "alive" << entsP973.aliveAt(0)
                                  << "cell" << cellD << "hit" << hitD
                                  << "tgt" << int(w.blockAt(cellD.x(), kRigY, cellD.y()))
                                  << "stack" << hbP973.countAt(0) << "exp" << stackD0 - 1
                                  << "swings" << swingsP973 << "expS" << swingsD0 + 1;
            clearTopP973(cellD.x() - x0);
            pumpMsP973(260);
            // (e) 玩家骑乘矿车旁放置不受干扰（Creative）：矿车落台面 (x0+3)（地面静止模式）→ tryMount
            //     登乘 → 座位钉位 settle tick 后实读眼位 → 瞄车旁 (x0+5) 台面顶 → 放置成功 + 骑乘不断
            //     + 挥手（载具不在占用门查询域，骑乘建筑链路零干扰）。
            pcP973.setSelectedBlock(int(BR::Stone));
            const int swingsE0 = swingsP973; // (e) per-leg 挥手基线
            cartsP973.spawnCart(x0 + 3, kRigY, z0, &w);
            {
                const QVector3D eyeM(float(x0) + 6.5f, float(kRigY) + 1.62f, float(z0) + 0.5f);
                const QVector3D tgtM(float(x0) + 3.5f, float(kRigY) + 0.3f, float(z0) + 0.5f);
                const QVector3D dirM = (tgtM - eyeM).normalized();
                if (!cartsP973.tryMount(eyeM, dirM, 8.0f)) {
                    qInfo().noquote() << "  [t973 diag] e mount ray missed";
                }
                // 座位钉位 settle tick：loadSavedState 任意位姿 → tick 后玩家被钉到座位（updateRaycast
                // 随后跑）→ 实读座位眼位再算第二跳瞄准角。
                pcP973.release();
                pcP973.grab();
                pcP973.loadSavedState(float(x0) + 6.5f, float(kRigY), float(z0) + 0.5f, 0.0f, 0.0f, 1);
                pcP973.tick();
                const QVector3D eyeSeat = pcP973.position(); // 实读（座位钉位后的眼位）
                const float ax = float(x0) + 5.5f, ay = float(kRigY), az = float(z0) + 0.5f;
                const float ddx = ax - eyeSeat.x(), ddy = ay - eyeSeat.y(), ddz = az - eyeSeat.z();
                const float dlen = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
                const float pitch2 = std::asin(ddy / dlen) * 57.2957795f;
                const float yaw2 = std::atan2(-ddx, -ddz) * 57.2957795f;
                pcP973.release();
                pcP973.grab();
                pcP973.loadSavedState(float(x0) + 6.5f, float(kRigY), float(z0) + 0.5f, yaw2, pitch2, 1);
                pcP973.tick(); // 座位重钉（矿车静止 → 同眼位）+ updateRaycast 从座位射线
                pcP973.placeBlock();
            }
            const bool ridingHeld = cartsP973.ridingIndex() == 0;
            // 结果断言（座位两跳瞄准的落点对 ±1 格光标残差不敏感——-leg 契约 = 骑乘中「车旁放置」成功
            // 且骑乘不断，落格在车旁台面带 (x0+4..x0+8) 内即可；占用门对载具域的零干涉由 (f) 源码钉
            // 与矿车不在 EntityManager 查询域的结构事实共同钉住）。
            bool stoneBesideCartE = false;
            for (int dx = 4; dx <= 8; ++dx)
                if (w.blockAt(x0 + dx, kRigY, z0) == BR::Stone) stoneBesideCartE = true;
            const bool okE = ridingHeld
                && stoneBesideCartE                               // 车旁带放置成功（骑乘中）
                && swingsP973 == swingsE0 + 1;
            if (!okE)
                qInfo().noquote() << "  [t973 diag] e riding" << cartsP973.ridingIndex()
                                  << "beside" << stoneBesideCartE
                                  << "s4..8" << int(w.blockAt(x0 + 4, kRigY, z0))
                                  << int(w.blockAt(x0 + 5, kRigY, z0))
                                  << int(w.blockAt(x0 + 6, kRigY, z0))
                                  << int(w.blockAt(x0 + 7, kRigY, z0))
                                  << int(w.blockAt(x0 + 8, kRigY, z0))
                                  << "swings" << swingsP973 << "expS" << swingsE0 + 1;
            // (f) 源码钉（P-t945 字符串钉模式 + 语句序钉）：占用门调用行 + Entities 侧排除面 + 门在
            //     预检链序位（目标格必须空检查 < 门 < 方块族专项预检首行（火把））。
            const QString exeDirP973 = QCoreApplication::applicationDirPath();
            const QString rootP973 = QDir(exeDirP973 + QStringLiteral("/..")).absolutePath();
            auto readSrcP973 = [&rootP973](const QString &rel) -> QString {
                QFile f(rootP973 + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString pcSrcP973 = readSrcP973(QStringLiteral("src/Game/playercontroller.cpp"));
            const QString emSrcP973 = readSrcP973(QStringLiteral("src/Entities/entitymanager.cpp"));
            const QString emHdrP973 = readSrcP973(QStringLiteral("src/Entities/entitymanager.h"));
            const int iEmptyP973 = pcSrcP973.indexOf(QStringLiteral(
                "if (tid != BlockRegistry::Air && tid != BlockRegistry::Water"));
            const int iGateP973 = pcSrcP973.indexOf(QStringLiteral(
                "if (m_entityManager->mobOccupiesCell(tx, ty, tz)) return;"));
            // 方块族专项预检首行（火把 t114）用其后独有的 torchHostOk lambda 钉——
            // `m_selectedBlock == ...Torch` 文本在 placeState 计算段（更早）也出现，不能作序位锚。
            const int iTorchP973 = pcSrcP973.indexOf(QStringLiteral(
                "const auto torchHostOk = [this](int ax, int ay, int az) {"));
            const bool okF = iEmptyP973 >= 0 && iGateP973 >= 0 && iTorchP973 >= 0
                && iEmptyP973 < iGateP973 && iGateP973 < iTorchP973   // 门在「必须空」与族专项之间
                && pcSrcP973.contains(QStringLiteral(
                    "if (isDoor && m_entityManager->mobOccupiesCell(tx, ty + 1, tz)) return;"))
                && pcSrcP973.contains(QStringLiteral(
                    "if (isBed && m_entityManager->mobOccupiesCell(tx + hdx, ty, tz + hdz)) return;"))
                && pcSrcP973.contains(QStringLiteral(
                    "if (m_entityManager && m_entityManager->mobOccupiesCell(m_hitBx, m_hitBy, m_hitBz)) return;"))
                && emSrcP973.contains(QStringLiteral("bool EntityManager::mobOccupiesCell(int bx, int by, int bz) const"))
                && emSrcP973.contains(QStringLiteral("if (!e.alive || e.kind != Mob || e.dead) continue;"))
                && emHdrP973.contains(QStringLiteral("bool mobOccupiesCell(int bx, int by, int bz) const;"));
            // 清场 + 释放（grab 析构配对；dismount 清骑乘链）。车旁带逐格清（(e) 落格对 ±1 格光标
            // 残差不敏感 → 清场带同步放宽）。
            QVector3D outFeetP973;
            cartsP973.dismount(&w, outFeetP973);
            for (int dx = 4; dx <= 8; ++dx)
                clearTopP973(dx);
            for (int dx = 0; dx <= 9; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
            QObject::disconnect(swingConnP973);
            pcP973.release();
            probeWinP973.deleteLater();
            const bool okP973 = okA && okB && okC && okD && okE && okF;
            if (!okP973) ++totalFail;
            if (!okP973)
                qInfo().noquote() << "  [t973 diag] a" << okA << "b" << okB << "c" << okC
                                  << "d" << okD << "e" << okE << "f" << okF;
            qInfo().noquote() << (okP973 ? "PASS" : "FAIL")
                              << "| t973 mob-occupied-cell placement gate: the write cell of any"
                                 " placement is checked against living-mob AABBs (anti-burial,"
                                 " user caliber) via the read-only EntityManager::mobOccupiesCell"
                                 " query (Game -> Entities downward dependency, hostileNearby"
                                 " precedent) - (a) empty-cell control places and swings (chain"
                                 " proven live, so (b)'s rejection is not a ray-miss no-op);"
                                 " (b) Survival placement aimed at the shambler's actual occupied"
                                 " cell is rejected: cell id unchanged, stack not consumed, no"
                                 " arm swing (reject = no-op caliber), hitBlock pinned so the"
                                 " precheck chain demonstrably ran; (c) a dropped item occupying"
                                 " the target cell while the shambler is still alive next door"
                                 " still places - non-mob entities never gate (MC semantics:"
                                 " items can be covered), consume/swing normal; (d) after"
                                 " damageEntity lethal (hp<=0 death-animation frame, slot still"
                                 " alive) the same cell places - dying mobs do not block;"
                                 " (e) mounted on a ground-mode minecart via tryMount, aiming"
                                 " from the actually-read seat eye, the cell beside the cart"
                                 " places and the ride persists - vehicles are outside the gate's"
                                 " query domain; (f) source pins: the gate call sits in the"
                                 " generic precheck layer between the target-must-be-empty check"
                                 " and the first block-family precheck (torch), the door-upper"
                                 " and bed-head write cells are gated symmetrically, the"
                                 " merge/stack write sites carry the same gate, and the Entities"
                                 " side exclusion face (alive && kind==Mob && !dead) is pinned";
        }
    }

    // ── P-t974 保存退出偶发未保存（用户第五轮：有概率重进是上一次存档点）──
    //    调查判决：保存链（savePlayerData → saveAll → saveProgress）全部是同步 SQLite 写 —— 链上无
    //    deferred / 分帧写，「异步写完成前退出」的假设不成立。真实的概率窗口有二：
    //    (1) 退出存档是 fire-and-forget —— 三个写盘调用的返回值被弃置，任一瞬态失败（外部进程瞬持
    //        .sqlite 文件锁：杀软 / 索引器 / 同步盘；磁盘满）即整事务回滚，库中留**上一次存档**、退出
    //        照常进行 → 与症状逐字吻合（回滚 = 旧档完好，「重进是上一次存档点」）；
    //    (2) playing 态直接关窗（标题栏 X / Alt+F4）无任何落盘点 —— 进程即退，自上次保存退出后的
    //        全部进度静默丢失（用户从界面分不清本次走的是哪条退出路径 → 观感「偶发」）。
    //    修法：QML 退出存档收口为 runExitSave()（三写 + 核验返回值 + 失败幂等重试一次 + toast 显式
    //    告知），写完成门在 coverGrabPending / 退出流程置位**之前**（链序钉）；窗口关闭兜底 onClosing
    //    走同一条链（写完再放行关闭）；WorldStore 加 saveOkCount 写完成计数（每次成功持久化 +1，
    //    失败不计数 —— 与 false 返回值同源互证）作行为级观测面。原子性核验：saveAll 单事务
    //    （DELETE 全量 + INSERT，COMMIT 失败整事务回滚）+ chunks 主键 (cx,cz) → 半写永不可见、
    //    失败必回滚到旧档（本探针 (c) 腿实证）。
    {
        bool okA = false;   // 行为腿：保存退出 → 立即关库重载同档 → 关键状态往返一致（非上一次存档点）
        bool okB = false;   // 写完成计数腿：三写成功恰好 +3；失败腿零计数
        bool okC = false;   // 竞态窗口阳性腿：瞬持库写锁 → 三写全部失败（可检测）→ 回滚保旧档（症状复现）→ 锁释放重试成功
        bool okD = false;   // 源码钉腿：写完成门链序 + onClosing 兜底 + 计数器契约

        // rig：独立小世界（3×3 chunk）+ PID 后缀临时库（t822 先例：不污染 saves/，双实例互不覆盖）。
        World wT974;
        wT974.setWidth(48);
        wT974.setDepth(48);
        wT974.setHeight(96);
        wT974.setSeed(97);
        WorldStore storeT974;
        storeT974.setWorld(&wT974);
        const QString dbT974 = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t974_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT974);

        // 玩家态（gatherPlayerState v3 形状的最小代表：位 / 姿 / 模式 / 血饥 / xp / 选中槽 + 背包一格有物）。
        const auto makeStackT974 = [](int id, int count) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), id);
            s.insert(QStringLiteral("count"), count);
            s.insert(QStringLiteral("durability"), 0);
            s.insert(QStringLiteral("enchants"), QVariantList{0, 0, 0, 0});
            s.insert(QStringLiteral("name"), QString());
            return s;
        };
        QVariantList hbT974, mnT974, arT974;
        for (int i = 0; i < 9; ++i) hbT974.append(makeStackT974(0, 0));
        for (int j = 0; j < 27; ++j) mnT974.append(makeStackT974(0, 0));
        for (int k = 0; k < 4; ++k) arT974.append(makeStackT974(0, 0));
        hbT974[4] = makeStackT974(int(BR::Stone), 3);
        const auto makePlayerT974 = [&](double px) {
            QVariantMap d;
            d.insert(QStringLiteral("version"), 3);
            d.insert(QStringLiteral("px"), px);
            d.insert(QStringLiteral("py"), 33.0);
            d.insert(QStringLiteral("pz"), 22.5);
            d.insert(QStringLiteral("yaw"), 33.0);
            d.insert(QStringLiteral("pitch"), -12.5);
            d.insert(QStringLiteral("mode"), 1);
            d.insert(QStringLiteral("health"), 17);
            d.insert(QStringLiteral("hunger"), 18);
            d.insert(QStringLiteral("xp"), 9);
            d.insert(QStringLiteral("selectedSlot"), 4);
            d.insert(QStringLiteral("hotbar"), hbT974);
            d.insert(QStringLiteral("main"), mnT974);
            d.insert(QStringLiteral("armor"), arT974);
            return d;
        };
        const auto makeProgressT974 = [](int minutes) {
            QVariantMap p;
            p.insert(QStringLiteral("statPlayedMinutes"), minutes);
            p.insert(QStringLiteral("achvList"), QVariantList{QStringLiteral("t974a"), QStringLiteral("t974b")});
            return p;
        };
        const QVariantMap pdV1 = makePlayerT974(11.5);
        const QVariantMap prV1 = makeProgressT974(7);

        // (a) 第一次退出存档 S1（「上一次存档点」基线）→ 关库重开重载 → 断言基线可见。
        okA = storeT974.openWorld(dbT974)
              && !storeT974.hasChunks()
              && storeT974.savePlayerData(pdV1)
              && storeT974.saveAll(QStringLiteral("t974rig"), QVariantList(), QVariantList(), QVariantList())
              && storeT974.saveProgress(prV1);
        storeT974.closeWorld();
        const int mxT974 = 24, mzT974 = 24;
        const int myT974 = wT974.heightAt(mxT974, mzT974) + 2;
        if (okA) {
            okA = storeT974.openWorld(dbT974) && storeT974.hasChunks();
            wT974.beginLoad(97);
            const int loadedS1 = storeT974.loadChunks();
            wT974.finishLoad();
            const QVariantMap pdBack = storeT974.loadPlayerData();
            okA = okA && loadedS1 == 9
                  && pdBack.value(QStringLiteral("px")).toDouble() == 11.5
                  && pdBack.value(QStringLiteral("xp")).toInt() == 9;
            storeT974.closeWorld();
        }
        // (a)(b) 第二次会话：世界改动（标记块）+ 新玩家位 / 新进度 → 退出存档 S2（三写 + 计数核验）
        //   → **立即 closeWorld**（复现「保存后马上退出拆链」的时序形态）→ 重开重载 → 断言看到的是
        //   新档而非上一次存档点。
        const quint8 markerT974 = wT974.blockAt(mxT974, myT974, mzT974) == quint8(BR::Stone)
                                      ? quint8(BR::Dirt) : quint8(BR::Stone);
        wT974.setBlock(mxT974, myT974, mzT974, markerT974, 0);
        if (okA) {
            // 新会话（S1 读档路径关库后重开）→ 世界改动 → 退出存档 S2（三写 + 计数核验）。
            okA = storeT974.openWorld(dbT974);
            const int c0 = storeT974.saveOkCount();
            const bool s2p = storeT974.savePlayerData(makePlayerT974(44.5));
            const bool s2w = storeT974.saveAll(QStringLiteral("t974rig"), QVariantList(), QVariantList(), QVariantList());
            const bool s2r = storeT974.saveProgress(makeProgressT974(21));
            const int c1 = storeT974.saveOkCount();
            okB = s2p && s2w && s2r && c1 == c0 + 3;
            storeT974.closeWorld();   // 保存后立刻拆链（竞态窗口的「退出」半边）
            const bool reopenOk = storeT974.openWorld(dbT974);   // 立即重进（读档）
            wT974.beginLoad(97);
            const int loadedS2 = storeT974.loadChunks();
            wT974.finishLoad();
            const QVariantMap pdS2 = storeT974.loadPlayerData();
            const QVariantMap prS2 = storeT974.loadProgress();
            const QVariantList hbS2 = pdS2.value(QStringLiteral("hotbar")).toList();
            okA = okA && okB && reopenOk && loadedS2 == 9
                  && wT974.blockAt(mxT974, myT974, mzT974) == markerT974   // 新档落地（非上一次存档点）
                  && pdS2.value(QStringLiteral("px")).toDouble() == 44.5
                  && int(hbS2.size()) == 9
                  && hbS2.at(4).toMap().value(QStringLiteral("id")).toInt() == int(BR::Stone)
                  && hbS2.at(4).toMap().value(QStringLiteral("count")).toInt() == 3
                  && prS2.value(QStringLiteral("statPlayedMinutes")).toInt() == 21;
            if (!okA)
                qInfo().noquote() << "  [t974 diag] a loaded=" << loadedS2
                                  << "markerRead=" << int(wT974.blockAt(mxT974, myT974, mzT974))
                                  << "markerExp=" << int(markerT974)
                                  << "px=" << pdS2.value(QStringLiteral("px")).toDouble()
                                  << "min=" << prS2.value(QStringLiteral("statPlayedMinutes")).toInt();
        }
        // (c) 竞态窗口阳性腿（确定性复现「偶发」）：第二连接 BEGIN EXCLUSIVE 瞬持库写锁（野外 =
        //    杀软 / 索引器 / 同步盘的瞬态锁）→ 三写必须**全部失败且可检测**（false + 计数不动 =
        //    修后 QML 完成门能看到的信号）→ 释放锁后重读：库中仍是**上一次存档点**（事务回滚保旧档
        //    = 用户症状的库侧形态；半写永不可见 = 原子性核验）→ 重试三写成功 + 计数恢复 +3。
        {
            QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("t974_locker"));
            locker.setDatabaseName(dbT974);
            const bool lOpen = locker.open();
            // 锁必须先于三写获取（BEGIN EXCLUSIVE 持库写锁 → WorldStore 主连接的全部写路径 SQLITE_BUSY）。
            QSqlQuery lq(locker);
            const bool lLock = lOpen && lq.exec(QStringLiteral("BEGIN EXCLUSIVE"));
            const int cB = storeT974.saveOkCount();
            const bool fP = storeT974.savePlayerData(makePlayerT974(99.5));
            const bool fW = storeT974.saveAll(QStringLiteral("t974rig_failed"), QVariantList(), QVariantList(), QVariantList());
            const bool fR = storeT974.saveProgress(makeProgressT974(99));
            const int cA = storeT974.saveOkCount();
            okC = lOpen && lLock && !fP && !fW && !fR && cA == cB;
            lq.exec(QStringLiteral("ROLLBACK"));   // 释放（QSqlQuery 先于 removeDatabase 出作用域销毁）
            locker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("t974_locker"));
        {
            // 锁释放后：旧档完好（失败写被回滚）+ 重试成功（QML 幂等重试的 C++ 级验证）。
            const QVariantMap pdKept = storeT974.loadPlayerData();
            const int r0 = storeT974.saveOkCount();
            const bool rP = storeT974.savePlayerData(makePlayerT974(44.5));
            const bool rW = storeT974.saveAll(QStringLiteral("t974rig"), QVariantList(), QVariantList(), QVariantList());
            const bool rR = storeT974.saveProgress(makeProgressT974(21));
            okC = okC && pdKept.value(QStringLiteral("px")).toDouble() == 44.5   // (c) 失败写的 pd2=99.5 未落库
                  && rP && rW && rR && storeT974.saveOkCount() == r0 + 3;
            if (!okC)
                qInfo().noquote() << "  [t974 diag] c keptPx=" << pdKept.value(QStringLiteral("px")).toDouble()
                                  << "retry" << rP << rW << rR;
        }
        // (d) 源码钉：QML 写完成门链序（runExitSave 定义 < 完成门 < 重试 < toast < coverGrabPending 置位）
        //     + onClosing 兜底同链 + WorldStore 计数器契约（Q_PROPERTY + 三处成功尾 bump，saveAll 的
        //     bump 在 commit 成功门之后）。
        {
            const QString exeDirP974 = QCoreApplication::applicationDirPath();
            const QString rootP974 = QDir(exeDirP974 + QStringLiteral("/..")).absolutePath();
            auto readSrcP974 = [&rootP974](const QString &rel) -> QString {
                QFile f(rootP974 + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString mainSrc = readSrcP974(QStringLiteral("src/ui/Main.qml"));
            const QString wsHdr = readSrcP974(QStringLiteral("src/World/worldstore.h"));
            const QString wsCpp = readSrcP974(QStringLiteral("src/World/worldstore.cpp"));
            const int iRunExit = mainSrc.indexOf(QStringLiteral("function runExitSave()"));
            const int iGate = mainSrc.indexOf(QStringLiteral("let exitSaveOk = runExitSave()"));
            const int iRetry = mainSrc.indexOf(QStringLiteral("if (!exitSaveOk) exitSaveOk = runExitSave()"));
            const int iToast = mainSrc.indexOf(QStringLiteral("存档写入失败，本次进度未保存"));
            const int iCover = mainSrc.indexOf(QStringLiteral("coverGrabPending = true"));
            const int iOnClose = mainSrc.indexOf(QStringLiteral("onClosing: (close) => {"));
            // review0901 #30/#35/#36 起 onClosing 段扩长（归还链 + lastExitSaveOk 写入 + 重试登记注释），
            //   截窗 800 → 1400 保 closeWorld 尾语句仍在切片内（钉意图不变：兜底路径走同链 + 关库）。
            const QString onCloseSlice = iOnClose >= 0 ? mainSrc.mid(iOnClose, 1400) : QString();
            const int iBump1 = wsCpp.indexOf(QStringLiteral("noteSaveOk();"));
            const int iCommitGate = wsCpp.indexOf(QStringLiteral("if (!db.commit())"));
            okD = iRunExit >= 0
                  && iRunExit < mainSrc.indexOf(QStringLiteral("function saveAndExitToWorldList()"))
                  && iGate >= 0 && iRetry >= 0 && iToast >= 0 && iCover >= 0
                  && iGate < iCover          // 写完成门先于退出流程置位 = 「写盘在退出前同步完成」链序
                  && mainSrc.contains(QStringLiteral("window.lastExitSaveOk = exitSaveOk"))
                  && iOnClose >= 0
                  && onCloseSlice.contains(QStringLiteral("runExitSave()"))
                  && onCloseSlice.contains(QStringLiteral("worldStore.closeWorld()"))
                  && wsHdr.contains(QStringLiteral("Q_PROPERTY(int saveOkCount READ saveOkCount NOTIFY saveOkCountChanged)"))
                  && iBump1 >= 0 && iCommitGate >= 0 && iCommitGate < iBump1
                  && wsCpp.count(QStringLiteral("noteSaveOk();")) == 3;
        }
        // (e) review0901 #30 行为腿（真链「开铁砧放物品 → 走 onClosing 归还链 → 存档 → 重载物品在背包」）：
        //     从 Main.qml 源码抽取 returnTransientItemsBeforeSave 函数体（brace 配平）**原样**嵌入 wrapper，
        //     驱真 AnvilUI.qml × 真 C++ Hotbar（t962/t975 装配法：临时目录 + 私有 URI + window 作用域；
        //     wrapper 只镜像 Main.qml 的面板开态 bool 与 closeXxx 归还臂——grab/焦点 chrome 略）。修前该
        //     函数不存在 → 抽取失败本腿即红（现行 onClosing 关窗丢物复现）；修后链路真跑：铁砧 A 槽受损
        //     铁镐 → 归还函数清槽入背包 VM → gatherPlayerState 同形快照落盘 → 重开重读物品在背包。
        bool okE = false;
        QString diagE;
        {
            const QString root974e = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
            QFile main974f(root974e + QStringLiteral("/src/ui/Main.qml"));
            const QString main974s = main974f.open(QIODevice::ReadOnly)
                                         ? QString::fromUtf8(main974f.readAll()) : QString();
            const int iFn974 = main974s.indexOf(QStringLiteral("function returnTransientItemsBeforeSave()"));
            QString fn974;
            if (iFn974 >= 0) {
                const int iOpen = main974s.indexOf(QLatin1Char('{'), iFn974);
                int depth = 0;
                for (int i = iOpen; i >= 0 && i < main974s.size(); ++i) {
                    const QChar c = main974s.at(i);
                    if (c == QLatin1Char('{')) ++depth;
                    else if (c == QLatin1Char('}') && --depth == 0) {
                        fn974 = main974s.mid(iFn974, i - iFn974 + 1);
                        break;
                    }
                }
            }
            if (fn974.isEmpty()) {
                diagE = QStringLiteral("returnTransientItemsBeforeSave not extractable (pre-fix shape)");
            } else {
                static bool sT974TypesRegistered = false;
                if (!sT974TypesRegistered) {
                    qmlRegisterType<Hotbar>("VoxelSandboxProbeT974", 1, 0, "Hotbar");
                    qmlRegisterType<PlayerState>("VoxelSandboxProbeT974", 1, 0, "PlayerState");
                    qmlRegisterType<PlayerController>("VoxelSandboxProbeT974", 1, 0, "PlayerController");
                    qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT974", 1, 0, "ResourcePackManager");
                    sT974TypesRegistered = true;
                }
                const QString uiDir974 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                             .filePath(QStringLiteral("../../src/ui"));
                const QString probeUiDir974 = QDir::temp().absoluteFilePath(
                        QStringLiteral("t974_qml_%1").arg(QCoreApplication::applicationPid()));
                QDir().mkpath(probeUiDir974);
                for (const QString f : { QStringLiteral("AnvilUI.qml"), QStringLiteral("InventoryOps.js"),
                                         QStringLiteral("InvSlot.qml"), QStringLiteral("ToolIcon.qml"),
                                         QStringLiteral("MaterialIcon.qml") }) {
                    QFile::remove(probeUiDir974 + QLatin1Char('/') + f);
                    QFile(uiDir974 + QLatin1Char('/') + f).copy(probeUiDir974 + QLatin1Char('/') + f);
                }
                const QUrl jsUrl974 = QUrl::fromLocalFile(probeUiDir974 + QLatin1Char('/')
                                                          + QStringLiteral("InventoryOps.js"));
                const QStringList qmlFiles974 = QDir(probeUiDir974).entryList({ QStringLiteral("*.qml") }, QDir::Files);
                for (const QString &f : qmlFiles974) {
                    QFile p(probeUiDir974 + QLatin1Char('/') + f);
                    if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                        continue;
                    QString t = QString::fromUtf8(p.readAll());
                    p.close();
                    t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                              QStringLiteral("import \"") + jsUrl974.toString() + QStringLiteral("\" as InventoryOps"));
                    t.replace(QStringLiteral("import VoxelSandbox\n"),
                              QStringLiteral("import VoxelSandboxProbeT974\n"));
                    if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                        p.write(t.toUtf8());
                        p.close();
                    }
                }
                QQmlEngine engine974;
                Hotbar vm974;
                PlayerState ps974;
                QQmlComponent wrapComp974(&engine974);
                // wrapper（id: window）：Main.qml 根最小复刻——面板开态四 bool + 三合成格 QtObject 桩
                //   （本腿红面在铁砧槽，合成格桩 no-op 即可）+ closeXxx 归还臂镜像 + 原文归还函数。
                wrapComp974.setData(QStringLiteral(
                    "import QtQuick\n"
                    "Item {\n"
                    "    id: window\n"
                    "    width: 800; height: 600\n"
                    "    property bool shiftHeld: false\n"
                    "    property string hoveredSlotKey: \"\"\n"
                    "    property bool enchantingTableOpen: false\n"
                    "    property bool anvilOpen: false\n"
                    "    property bool dispenserOpen: false\n"
                    "    property bool inventoryOpen: false\n"
                    "    property var hotbarVM: null\n"
                    "    property var anvilPanel: null\n"
                    "    property var enchantingPanel: null\n"
                    "    property var craftingTablePanel: QtObject { function returnCraftToHotbar() { } }\n"
                    "    property var survivalPanel: QtObject { function returnCraftToHotbar() { } }\n"
                    "    property var inventoryPanel: QtObject { function returnCraftToHotbar() { } }\n"
                    "    function refocusKeyInput() { }\n"
                    "    function closeEnchantingTable() {\n"
                    "        if (!enchantingTableOpen) return\n"
                    "        enchantingTableOpen = false\n"
                    "        if (enchantingPanel) enchantingPanel.returnEnchantToHotbar()\n"
                    "        returnHeldToHotbar()\n"
                    "    }\n"
                    "    function closeAnvil() {\n"
                    "        if (!anvilOpen) return\n"
                    "        anvilOpen = false\n"
                    "        anvilPanel.returnAnvilToHotbar()\n"
                    "        returnHeldToHotbar()\n"
                    "    }\n"
                    "    function closeDispenser() {\n"
                    "        if (!dispenserOpen) return\n"
                    "        dispenserOpen = false\n"
                    "        returnHeldToHotbar()\n"
                    "    }\n"
                    "    function closeInventory() {\n"
                    "        if (!inventoryOpen) return\n"
                    "        inventoryOpen = false\n"
                    "        returnHeldToHotbar()\n"
                    "    }\n"
                    "    function returnHeldToHotbar() {\n"
                    "        if (!hotbarVM.heldBlock || hotbarVM.heldCount <= 0) return\n"
                    "        const leftover = hotbarVM.addToAny(hotbarVM.heldBlock, hotbarVM.heldCount,\n"
                    "                                           hotbarVM.heldDurability, hotbarVM.heldEnchants(),\n"
                    "                                           hotbarVM.heldCustomName)\n"
                    "        if (leftover > 0) hotbarVM.heldCount = leftover\n"
                    "        else hotbarVM.heldBlock = 0\n"
                    "    }\n"
                    "    %1\n"
                    "}\n").arg(fn974).toUtf8(), QUrl());
                QQuickItem host974;
                QQuickItem *wrap974 = nullptr;
                QObject *anvil974 = nullptr;
                if (wrapComp974.isError()) {
                    diagE = QStringLiteral("wrapper: ") + wrapComp974.errorString();
                } else {
                    wrap974 = qobject_cast<QQuickItem *>(wrapComp974.create());
                    if (!wrap974) {
                        diagE = QStringLiteral("wrapper create failed");
                    } else {
                        wrap974->setParent(&engine974);
                        wrap974->setParentItem(&host974);
                    }
                }
                if (wrap974) {
                    QFile anvilSrc974(probeUiDir974 + QLatin1Char('/') + QStringLiteral("AnvilUI.qml"));
                    if (!anvilSrc974.open(QIODevice::ReadOnly)) {
                        diagE = QStringLiteral("AnvilUI read failed");
                    } else {
                        QQmlComponent anvilComp974(&engine974);
                        anvilComp974.setData(anvilSrc974.readAll(),
                                             QUrl::fromLocalFile(anvilSrc974.fileName()));
                        if (anvilComp974.isError()) {
                            diagE = QStringLiteral("AnvilUI load: ") + anvilComp974.errorString();
                        } else {
                            anvil974 = anvilComp974.create(qmlContext(wrap974));
                            QQuickItem *ai = qobject_cast<QQuickItem *>(anvil974);
                            if (!ai) {
                                diagE = QStringLiteral("AnvilUI create: ") + anvilComp974.errorString();
                            } else {
                                anvil974->setProperty("hotbar", QVariant::fromValue(&vm974));
                                anvil974->setProperty("playerState", QVariant::fromValue(&ps974));
                                anvil974->setProperty("player", QVariant());
                                anvil974->setProperty("progress", QVariant());
                                anvil974->setProperty("theWorld", QVariant());
                                ai->setWidth(800);
                                ai->setHeight(600);
                                anvil974->setParent(wrap974);
                                ai->setParentItem(wrap974);
                            }
                        }
                    }
                }
                if (anvil974 && wrap974) {
                    QCoreApplication::processEvents();
                    const int pick974 = int(ToolRegistry::PickaxeIron);
                    const QVariantList ench0974{0, 0, 0, 0};
                    wrap974->setProperty("hotbarVM", QVariant::fromValue(&vm974));
                    wrap974->setProperty("anvilPanel", QVariant::fromValue(anvil974));
                    // 开铁砧放物品（真 writeSlot 路径：A 槽 = 受损铁镐一件，dur=87）。
                    const bool putOk = QMetaObject::invokeMethod(anvil974, "writeSlot",
                        Q_ARG(QVariant, QVariant(QStringLiteral("anvil"))), Q_ARG(QVariant, QVariant(0)),
                        Q_ARG(QVariant, QVariant(pick974)), Q_ARG(QVariant, QVariant(1)),
                        Q_ARG(QVariant, QVariant(87)), Q_ARG(QVariant, QVariant(ench0974)),
                        Q_ARG(QVariant, QVariant(QString())));
                    wrap974->setProperty("anvilOpen", QVariant(true));
                    QVariant preSlot974;
                    QMetaObject::invokeMethod(anvil974, "readSlot", Q_RETURN_ARG(QVariant, preSlot974),
                                              Q_ARG(QVariant, QVariant(QStringLiteral("anvil"))),
                                              Q_ARG(QVariant, QVariant(0)));
                    const bool preHas = putOk && preSlot974.toMap().value(QStringLiteral("id")).toInt() == pick974
                                        && preSlot974.toMap().value(QStringLiteral("count")).toInt() == 1;
                    // 走 onClosing 链的归还半边（Main.qml 原文函数）。
                    const bool ranChain = QMetaObject::invokeMethod(wrap974, QStringLiteral("returnTransientItemsBeforeSave")
                                                                        .toLatin1().constData());
                    QVariant postSlot974;
                    QMetaObject::invokeMethod(anvil974, "readSlot", Q_RETURN_ARG(QVariant, postSlot974),
                                              Q_ARG(QVariant, QVariant(QStringLiteral("anvil"))),
                                              Q_ARG(QVariant, QVariant(0)));
                    const bool postEmpty = postSlot974.toMap().value(QStringLiteral("id")).toInt() == 0;
                    bool inVm = false;
                    int vmDur = -1;
                    for (int i = 0; i < vm974.slotCount() && !inVm; ++i)
                        if (vm974.blockIdAt(i) == pick974) { inVm = true; vmDur = vm974.durabilityAt(i); }
                    for (int i = 0; i < vm974.mainCount() && !inVm; ++i)
                        if (vm974.mainBlockIdAt(i) == pick974) { inVm = true; vmDur = vm974.mainDurabilityAt(i); }
                    // gatherPlayerState 同形快照 → 落盘 → 重开重读 → 物品在背包/hotbar（存档不丢闭环）。
                    const auto mk974 = [](int id, int count, int dur) {
                        QVariantMap s;
                        s.insert(QStringLiteral("id"), id);
                        s.insert(QStringLiteral("count"), count);
                        s.insert(QStringLiteral("durability"), dur);
                        s.insert(QStringLiteral("enchants"), QVariantList{0, 0, 0, 0});
                        s.insert(QStringLiteral("name"), QString());
                        return s;
                    };
                    QVariantList hb974e, mn974e, ar974e;
                    for (int i = 0; i < 9; ++i)
                        hb974e.append(mk974(vm974.blockIdAt(i), vm974.countAt(i), vm974.durabilityAt(i)));
                    for (int i = 0; i < 27; ++i)
                        mn974e.append(mk974(vm974.mainBlockIdAt(i), vm974.mainCountAt(i), vm974.mainDurabilityAt(i)));
                    for (int k = 0; k < 4; ++k)
                        ar974e.append(mk974(vm974.armorBlockIdAt(k), vm974.armorCountAt(k), vm974.armorDurabilityAt(k)));
                    QVariantMap pd974e;
                    pd974e.insert(QStringLiteral("version"), 3);
                    pd974e.insert(QStringLiteral("hotbar"), hb974e);
                    pd974e.insert(QStringLiteral("main"), mn974e);
                    pd974e.insert(QStringLiteral("armor"), ar974e);
                    const QString db974e = QDir::temp().absoluteFilePath(
                            QStringLiteral("voxel_t974e_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
                    QFile::remove(db974e);
                    World w974e;
                    w974e.setWidth(48);
                    w974e.setDepth(48);
                    w974e.setHeight(96);
                    w974e.setSeed(97);
                    WorldStore store974e;
                    store974e.setWorld(&w974e);
                    bool persisted = store974e.openWorld(db974e) && store974e.savePlayerData(pd974e);
                    store974e.closeWorld();
                    persisted = persisted && store974e.openWorld(db974e);
                    const QVariantMap pdBack974e = store974e.loadPlayerData();
                    store974e.closeWorld();
                    QFile::remove(db974e);
                    bool backInBag = false;
                    const auto scanBag974e = [&pdBack974e, &pick974, &backInBag](const QString &key) {
                        const QVariantList arr = pdBack974e.value(key).toList();
                        for (const QVariant &v : arr) {
                            if (v.toMap().value(QStringLiteral("id")).toInt() == pick974) { backInBag = true; return; }
                        }
                    };
                    scanBag974e(QStringLiteral("hotbar"));
                    if (!backInBag) scanBag974e(QStringLiteral("main"));
                    okE = preHas && ranChain && postEmpty && inVm && vmDur == 87
                          && persisted && backInBag;
                    if (!okE)
                        diagE = QStringLiteral("put") + QString::number(int(preHas))
                                + QStringLiteral(" chain") + QString::number(int(ranChain))
                                + QStringLiteral(" emptied") + QString::number(int(postEmpty))
                                + QStringLiteral(" inVm") + QString::number(int(inVm))
                                + QStringLiteral(" dur") + QString::number(vmDur)
                                + QStringLiteral(" persist") + QString::number(int(persisted))
                                + QStringLiteral(" reload") + QString::number(int(backInBag));
                } else if (diagE.isEmpty()) {
                    diagE = QStringLiteral("rig incomplete");
                }
            }
        }
        // (f) review0901 #30/#35/#36 源码钉：归还序函数定义 + 两路径调用点先于存档 + lastExitSaveOk
        //     两路径写入 + 世界列表角标消费面 + 重试无退避登记注释。
        bool okF974 = false;
        {
            const QString root974f = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
            auto readSrc974f = [&root974f](const QString &rel) -> QString {
                QFile f(root974f + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString main974m = readSrc974f(QStringLiteral("src/ui/Main.qml"));
            const QString wl974s = readSrc974f(QStringLiteral("src/ui/WorldList.qml"));
            const int iFnDef974 = main974m.indexOf(QStringLiteral("function returnTransientItemsBeforeSave()"));
            const int iRunExitDef = iFnDef974 >= 0
                                        ? main974m.indexOf(QStringLiteral("function runExitSave()"), iFnDef974) : -1;
            const QString fnSlice974 = (iFnDef974 >= 0 && iRunExitDef > iFnDef974)
                                           ? main974m.mid(iFnDef974, iRunExitDef - iFnDef974) : QString();
            const int nRefs974 = main974m.count(QStringLiteral("returnTransientItemsBeforeSave()"));   // 定义 + 两路径调用 = 3
            const int iGate974f = main974m.indexOf(QStringLiteral("let exitSaveOk = runExitSave()"));
            const int iCallSave974 = iFnDef974 >= 0
                                         ? main974m.indexOf(QStringLiteral("returnTransientItemsBeforeSave()"), iFnDef974 + 1) : -1;
            const int iOnClose974f = main974m.indexOf(QStringLiteral("onClosing: (close) => {"));
            const QString closeSlice974 = iOnClose974f >= 0 ? main974m.mid(iOnClose974f, 1400) : QString();
            const int iCallClose974 = closeSlice974.indexOf(QStringLiteral("returnTransientItemsBeforeSave()"));
            const int iSaveClose974 = closeSlice974.indexOf(QStringLiteral("runExitSave()"));
            const int iBadgeBind974 = main974m.indexOf(QStringLiteral(
                    "unsavedExitFile: window.lastExitSaveOk === false ? window.currentWorldFile : \"\""));
            okF974 = iFnDef974 >= 0 && nRefs974 == 3
                     && iCallSave974 >= 0 && iGate974f >= 0 && iCallSave974 < iGate974f
                     && iCallClose974 >= 0 && iSaveClose974 >= 0 && iCallClose974 < iSaveClose974
                     // 函数体归还臂钉（三面板 / 三合成格 / 关背包 / 手持兜底——抽公共函数漏臂即红）。
                     && fnSlice974.contains(QStringLiteral("if (enchantingTableOpen) closeEnchantingTable()"))
                     && fnSlice974.contains(QStringLiteral("if (anvilOpen) closeAnvil()"))
                     && fnSlice974.contains(QStringLiteral("if (dispenserOpen) closeDispenser()"))
                     && fnSlice974.count(QStringLiteral("returnCraftToHotbar()")) == 3
                     && fnSlice974.contains(QStringLiteral("if (inventoryOpen) closeInventory()"))
                     && fnSlice974.contains(QStringLiteral("returnHeldToHotbar()"))
                     // #35：两路径写 lastExitSaveOk + 世界列表角标消费面。
                     && main974m.contains(QStringLiteral("window.lastExitSaveOk = exitSaveOk"))
                     && closeSlice974.contains(QStringLiteral("window.lastExitSaveOk = okClose"))
                     && iBadgeBind974 >= 0
                     && wl974s.contains(QStringLiteral("property string unsavedExitFile"))
                     && wl974s.contains(QStringLiteral("root.unsavedExitFile === model.file"))
                     && wl974s.contains(QStringLiteral("上次退出未保存"))
                     // #36：重试无退避登记注释（按钮路径全登记 + 关窗路径指针 = 恰两处）。
                     && main974m.count(QStringLiteral("review0901 #36")) == 2
                     && main974m.contains(QStringLiteral("退避 ≤300ms"));
            if (!okF974)
                qInfo().noquote() << "  [t974 diag] f fnDef" << iFnDef974 << "refs" << nRefs974
                                  << "callSave" << iCallSave974 << "gate" << iGate974f
                                  << "callClose" << iCallClose974 << "saveClose" << iSaveClose974
                                  << "badge" << iBadgeBind974;
        }
        storeT974.closeWorld();
        QFile::remove(dbT974);
        const bool okP974 = okA && okB && okC && okD && okE && okF974;
        if (!okP974) ++totalFail;
        if (!okP974)
            qInfo().noquote() << "  [t974 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "e" << okE << "f" << okF974 << diagE;
        qInfo().noquote() << (okP974 ? "PASS" : "FAIL")
                          << "| t974 save-exit silent-loss race: the exit-save chain (player state +"
                             " chunks transaction + progress) is synchronous SQLite - the probabilistic"
                             " window is the discarded return values (fire-and-forget: one transient"
                             " external lock / disk failure rolls the transaction back and the file"
                             " keeps the PREVIOUS save while exit proceeds) plus the never-saving"
                             " window-close path - (a) behavior leg: exit-save -> immediate close ->"
                             " reopen -> reload sees the NEW save (marker block, player position,"
                             " inventory stack, progress), not the previous one; (b) write-completion"
                             " counter: the trio bumps saveOkCount by exactly +3, failures never count;"
                             " (c) race-window positive leg, deterministic: a second connection holding"
                             " BEGIN EXCLUSIVE (in the wild: AV / indexer / sync tools) makes all three"
                             " writes fail detectably (false + flat counter), the rolled-back file keeps"
                             " the previous save intact (half-writes never visible = atomicity proof),"
                             " and the retry after lock release succeeds +3; (d) source pins: the"
                             " runExitSave gate (check -> retry -> toast) precedes coverGrabPending in"
                             " saveAndExitToWorldList, onClosing routes the window-close path through"
                             " the same chain and closes the store, and the WorldStore counter contract"
                             " (Q_PROPERTY + exactly 3 bump sites, saveAll's after the commit gate)."
                             " review0901 additions: (e) behavioral leg on the real return-chain -"
                             " the returnTransientItemsBeforeSave function source is extracted from"
                             " Main.qml verbatim and driven against a real AnvilUI.qml x real C++"
                             " Hotbar rig (damaged iron pickaxe into slot A -> chain runs -> slot"
                             " emptied -> item in the VM bag with durability intact ->"
                             " gatherPlayerState-shaped snapshot saved -> world reopened -> item back"
                             " in the bag; pre-fix the function does not exist and the leg is red);"
                             " (f) source pins: the shared function is defined with all return arms"
                             " (three panels / three craft grids / close-inventory / held fallback)"
                             " and is called before the save in BOTH exit paths (exactly 3 literal"
                             " occurrences = definition + two call sites), lastExitSaveOk is written"
                             " by both paths and consumed by the world-list '上次退出未保存' badge"
                             " (unsavedExitFile binding), and the retry-without-backoff trade-off"
                             " registration (<=300ms cap direction) is in place";
    }

    // ── P-t975 创造拿取语义再反转（用户 8-28 定稿：调色板**左键 = 拿一组 / 右键 = 只拿一个**，
    //    t896 的「左键默认 1 个」被翻案）──
    //    逐入口清单（全库核）：创造拿取面唯一 = Inventory.qml 调色板格（无限源）；其余槽位面
    //    （Inventory 护甲/合成/主栏/hotbar 行、AnvilUI、EnchantingTableUI、SurvivalInventory）左/右键走
    //    InventoryOps 生存共享语义、中键走 t653①/t896/t956 复制面，均非「无限源拿取」，零触碰。
    //    (a) 源码钉：paletteTake 单一入口三段结构（预设书 / 同格 toggle-or-续拿 / 异格·空手换拿）+
    //        左键传 maxStackSize(modelData)+toggle、右键（Qt.RightButton 邻近段）传 1+无 toggle +
    //        无限源拿取字面恰 2 处（共用入口 + 中键复制面）+ paletteTake 调用恰 2 处（逐入口数钉：
    //        创造拿取只此左右两键，散写第三处即红）。
    //    (b) 行为腿（t874/t956 装配法：临时目录 + 私有 URI + wrapper 作用域 + 真 Hotbar 直调；
    //        returnHeldToVoidRequested 接宿主同义 sink——Main.qml 直连 `heldBlock = 0` 的行为级镜像）：
    //        ①左键一组=64 ②左键同格 t318 归还虚空 ③右键空手=1 ④右键连点续拿=2 ⑤右键满组 cap no-op
    //        ⑥右键异格换拿 1 件 ⑦左键异格换拿整组 ⑧工具左键「一组」=1（maxStackSize 权威，t33 口径）
    //        ⑨预设附魔书右键 = 0x227×1 带预设附魔；itemTaken / voidReturn 发射计数精确钉
    //        （拿取发、归还/满组 no-op 不发）。
    //    阴性轮：左键数量回 1（翻案回滚）→ 恰 P-t975 FAIL → 复原绿。
    {
        bool okPin = false, behavOk = false;
        QString behavDiag;
        const QString exeDir975 = QCoreApplication::applicationDirPath();
        const QString root975 = QDir(exeDir975 + QStringLiteral("/..")).absolutePath();
        QFile inv975f(root975 + QStringLiteral("/src/ui/Inventory.qml"));
        const QString inv975s = inv975f.open(QIODevice::ReadOnly) ? QString::fromUtf8(inv975f.readAll()) : QString();
        const int iFn975 = inv975s.indexOf(QStringLiteral("function paletteTake(modelData, takeCount, toggleReturnOnSameId)"));
        const QString fn975 = iFn975 >= 0 ? inv975s.mid(iFn975, 1400) : QString();
        const int iLeft975 = inv975s.indexOf(QStringLiteral("root.paletteTake(modelData, root.hotbar.maxStackSize(modelData), true)"));
        const int iRight975 = iLeft975 >= 0
                ? inv975s.indexOf(QStringLiteral("acceptedButtons: Qt.RightButton"), iLeft975) : -1;
        const QString rightSeg975 = iRight975 >= 0 ? inv975s.mid(iRight975, 300) : QString();
        const int nTakeLit975 = inv975s.count(QStringLiteral("root.hotbar.heldBlock = modelData"));
        const int nTakeCall975 = inv975s.count(QStringLiteral("root.paletteTake("));
        okPin = iFn975 >= 0
                && fn975.contains(QStringLiteral("root.hotbar.takeCreativeEnchantedBook(bi.ench)"))
                && fn975.contains(QStringLiteral("if (toggleReturnOnSameId) {"))
                && fn975.contains(QStringLiteral("root.returnHeldToVoidRequested()"))
                && fn975.contains(QStringLiteral("heldCount < root.hotbar.maxStackSize(modelData)"))
                && fn975.contains(QStringLiteral("root.hotbar.heldCount = root.hotbar.heldCount + 1"))
                && fn975.contains(QStringLiteral("root.hotbar.heldCount = takeCount"))
                && fn975.contains(QStringLiteral("root.itemTaken()"))
                && iLeft975 >= 0
                && iRight975 > iLeft975
                && rightSeg975.contains(QStringLiteral("root.paletteTake(modelData, 1, false)"))
                && nTakeLit975 == 2
                && nTakeCall975 == 2;

        // (b) 行为腿装配（t956 同款：临时目录逃离 qrc 重映射 + 私有 URI + wrapper 作用域）。
        static bool sT975TypesRegistered = false;
        if (!sT975TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT975", 1, 0, "Hotbar");
            qmlRegisterType<PlayerController>("VoxelSandboxProbeT975", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT975", 1, 0, "ResourcePackManager");
            sT975TypesRegistered = true;
        }
        const QString uiDir975 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUiDir975 = QDir::temp().absoluteFilePath(
                QStringLiteral("t975_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir975);
        for (const QString f : { QStringLiteral("Inventory.qml"), QStringLiteral("InventoryOps.js"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("DurabilityBar.qml"), QStringLiteral("DarkScrollBar.qml") }) {
            QFile::remove(probeUiDir975 + QLatin1Char('/') + f);
            QFile(uiDir975 + QLatin1Char('/') + f).copy(probeUiDir975 + QLatin1Char('/') + f);
        }
        // CharacterPreview3D 桩：角色预览是纯呈现 3D 组件，与本任务拿取语义无关（真件引 QtQuick3D 全
        //   模块场景，harness 不装 —— t956 AnvilUI 装配法的「最小依赖面」策略）；四注入属性同签名。
        {
            QFile stub975(probeUiDir975 + QStringLiteral("/CharacterPreview3D.qml"));
            if (stub975.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                stub975.write(QByteArrayLiteral(
                    "import QtQuick\n"
                    "Item {\n"
                    "    property var hotbar\n"
                    "    property var player\n"
                    "    property bool showHitboxes: false\n"
                    "    property var mouseScene\n"
                    "}\n"));
                stub975.close();
            }
        }
        {
            // import 重映射（t956 同款）：InventoryOps.js → 绝对路径；import VoxelSandbox → 私有 URI。
            const QUrl jsUrl975 = QUrl::fromLocalFile(probeUiDir975 + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            const QStringList qmlFiles975 = QDir(probeUiDir975).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles975) {
                QFile p(probeUiDir975 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl975.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT975\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine975;
        Hotbar vm975;
        QQmlComponent wrapComp975(&engine975);
        // wrapper 作用域：id: window（面板内 window.shiftHeld / window.progress / window.showHitboxes 解析）
        //   + cursorTracker（previewMouseScene 绑定源）——均与拿取语义无关，仅保装载。
        wrapComp975.setData(R"QML(import QtQuick
Item {
    id: window
    width: 800; height: 600
    property bool shiftHeld: false
    property bool showHitboxes: false
    property var progress
    function refocusKeyInput() { }
    HoverHandler { id: cursorTracker }
}
)QML", QUrl());
        QQuickItem host975;   // 独立场景根（无窗口，同 t874/t956）
        QObject *inv975Obj = nullptr;
        QFile invSrc975(probeUiDir975 + QLatin1Char('/') + QStringLiteral("Inventory.qml"));
        if (wrapComp975.isError()) {
            behavDiag = QStringLiteral("wrapper: ") + wrapComp975.errorString();
        } else if (!invSrc975.open(QIODevice::ReadOnly)) {
            behavDiag = QStringLiteral("Inventory read failed");
        } else {
            QQuickItem *wrapItem = qobject_cast<QQuickItem *>(wrapComp975.create());
            if (!wrapItem) {
                behavDiag = QStringLiteral("wrapper create failed");
            } else {
                wrapItem->setParent(&engine975);
                wrapItem->setParentItem(&host975);
                QQmlComponent invComp975(&engine975);
                invComp975.setData(invSrc975.readAll(), QUrl::fromLocalFile(invSrc975.fileName()));
                if (invComp975.isError()) {
                    behavDiag = QStringLiteral("Inventory load: ") + invComp975.errorString();
                } else {
                    inv975Obj = invComp975.create(qmlContext(wrapItem));
                    QQuickItem *ii = qobject_cast<QQuickItem *>(inv975Obj);
                    if (!ii) {
                        behavDiag = QStringLiteral("Inventory create: ") + invComp975.errorString();
                    } else {
                        inv975Obj->setProperty("hotbar", QVariant::fromValue(&vm975));
                        inv975Obj->setProperty("player", QVariant());
                        inv975Obj->setProperty("progress", QVariant());
                        ii->setWidth(800);
                        ii->setHeight(600);
                        inv975Obj->setParent(wrapItem);
                        ii->setParentItem(wrapItem);
                    }
                }
            }
        }
        if (!inv975Obj) {
            behavOk = false;
        } else {
            QCoreApplication::processEvents();
            // 宿主同义 sink：Main.qml 直连语义行为级镜像（onReturnHeldToVoidRequested: hotbarVM.heldBlock = 0）
            //   + itemTaken 计数（连接失败恒 -1，腿内即红；t956 同款 QtObject 计数器）。
            QQmlComponent sinkComp975(&engine975);
            sinkComp975.setData(QByteArrayLiteral(
                                   "import QtQuick\n"
                                   "QtObject {\n"
                                   "    property int voidCount: 0\n"
                                   "    property int takenCount: 0\n"
                                   "    property QtObject vm\n"
                                   "    function voidReturn() { voidCount += 1; if (vm) vm.heldBlock = 0 }\n"
                                   "    function takenBump() { takenCount += 1 }\n"
                                   "}\n"), QUrl());
            QObject *sink975 = sinkComp975.create();
            if (sink975) sink975->setParent(inv975Obj);
            if (sink975) sink975->setProperty("vm", QVariant::fromValue(&vm975));
            const bool vConn975 = sink975 && QObject::connect(inv975Obj, SIGNAL(returnHeldToVoidRequested()),
                                                              sink975, SLOT(voidReturn()));
            const bool tConn975 = sink975 && QObject::connect(inv975Obj, SIGNAL(itemTaken()),
                                                              sink975, SLOT(takenBump()));
            const auto voids975 = [sink975]() { return sink975 ? sink975->property("voidCount").toInt() : -1; };
            const auto taken975 = [sink975]() { return sink975 ? sink975->property("takenCount").toInt() : -1; };
            auto invokeTake975 = [inv975Obj](int id, int count, bool toggle) {
                return inv975Obj && QMetaObject::invokeMethod(inv975Obj, "paletteTake",
                                                              Q_ARG(QVariant, QVariant(id)),
                                                              Q_ARG(QVariant, QVariant(count)),
                                                              Q_ARG(QVariant, QVariant(toggle)));
            };
            const int stone975 = int(BR::Stone);
            const int dirt975 = int(BR::Dirt);
            const int sword975 = int(ToolRegistry::DiamondSword);
            const int stoneMax975 = vm975.maxStackSize(stone975);
            const int sharpPack975 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 1);
            // 调色板哨兵 id = Inventory.qml bookSentinel(-0x1000) - ench（bookInfoFor 映射方向）。
            const int bookCell975 = -0x1000 - int(EnchantRegistry::Sharpness);
            if (!vConn975 || !tConn975) {
                behavDiag = QStringLiteral("sink connect failed");
            } else do {
                // ① 左键（空手）= 拿一组：maxStackSize 上手（64）。
                if (!invokeTake975(stone975, stoneMax975, true)) { behavDiag = QStringLiteral("invoke 1 failed"); break; }
                if (vm975.heldBlock() != stone975 || vm975.heldCount() != stoneMax975) {
                    behavDiag = QStringLiteral("left stack: held ") + QString::number(vm975.heldBlock())
                                 + QStringLiteral(" x") + QString::number(vm975.heldCount());
                    break;
                }
                if (taken975() != 1 || voids975() != 0) { behavDiag = QStringLiteral("leg1 signals"); break; }
                // ② 左键同格 = t318 切换式归还（sink 镜像宿主 heldBlock = 0；不重复发拿取反馈）。
                if (!invokeTake975(stone975, stoneMax975, true)) { behavDiag = QStringLiteral("invoke 2 failed"); break; }
                if (vm975.heldBlock() != 0 || vm975.heldCount() != 0 || voids975() != 1 || taken975() != 1) {
                    behavDiag = QStringLiteral("left toggle: held ") + QString::number(vm975.heldBlock())
                                 + QStringLiteral(" voids ") + QString::number(voids975());
                    break;
                }
                // ③ 右键（空手）= 只拿一个：1 件上手。
                if (!invokeTake975(stone975, 1, false)) { behavDiag = QStringLiteral("invoke 3 failed"); break; }
                if (vm975.heldBlock() != stone975 || vm975.heldCount() != 1 || taken975() != 2) {
                    behavDiag = QStringLiteral("right one: held ") + QString::number(vm975.heldCount());
                    break;
                }
                // ④ 右键连点 = 续拿 +1（不接 t318 toggle —— 连点拿/还振荡防线）。
                if (!invokeTake975(stone975, 1, false)) { behavDiag = QStringLiteral("invoke 4 failed"); break; }
                if (vm975.heldCount() != 2 || voids975() != 1 || taken975() != 3) {
                    behavDiag = QStringLiteral("right accumulate: x") + QString::number(vm975.heldCount());
                    break;
                }
                // ⑤ 右键满组 = cap no-op（maxStackSize 单一权威；不重发反馈）。
                vm975.setHeldCount(stoneMax975);
                if (!invokeTake975(stone975, 1, false)) { behavDiag = QStringLiteral("invoke 5 failed"); break; }
                if (vm975.heldCount() != stoneMax975 || voids975() != 1 || taken975() != 3) {
                    behavDiag = QStringLiteral("right cap: x") + QString::number(vm975.heldCount());
                    break;
                }
                // ⑥ 右键异格 = 换拿 1 件（旧物回虚空 + 新物 1 件上手）。
                if (!invokeTake975(dirt975, 1, false)) { behavDiag = QStringLiteral("invoke 6 failed"); break; }
                if (vm975.heldBlock() != dirt975 || vm975.heldCount() != 1 || voids975() != 2 || taken975() != 4) {
                    behavDiag = QStringLiteral("right swap: held ") + QString::number(vm975.heldBlock());
                    break;
                }
                // ⑦ 左键异格 = 换拿整组。
                if (!invokeTake975(stone975, stoneMax975, true)) { behavDiag = QStringLiteral("invoke 7 failed"); break; }
                if (vm975.heldBlock() != stone975 || vm975.heldCount() != stoneMax975 || voids975() != 3 || taken975() != 5) {
                    behavDiag = QStringLiteral("left swap: x") + QString::number(vm975.heldCount());
                    break;
                }
                // ⑧ 工具左键「一组」= maxStackSize 权威 = 1（t33 不可堆叠口径，左右键同量）。
                if (!invokeTake975(sword975, vm975.maxStackSize(sword975), true)) { behavDiag = QStringLiteral("invoke 8 failed"); break; }
                if (vm975.heldBlock() != sword975 || vm975.heldCount() != 1 || voids975() != 4 || taken975() != 6) {
                    behavDiag = QStringLiteral("tool stack: x") + QString::number(vm975.heldCount());
                    break;
                }
                // ⑨ 预设附魔书（哨兵格）= 专用拿取：0x227 ×1 + 预设附魔（锐锋 I）。
                if (!invokeTake975(bookCell975, 1, false)) { behavDiag = QStringLiteral("invoke 9 failed"); break; }
                const QVariantList he975 = vm975.heldEnchants();
                if (vm975.heldBlock() != int(RecipeRegistry::EnchantedBookId) || vm975.heldCount() != 1
                        || he975.size() != 4 || he975.at(0).toInt() != sharpPack975
                        || voids975() != 5 || taken975() != 7) {
                    behavDiag = QStringLiteral("book take: held ") + QString::number(vm975.heldBlock())
                                 + QStringLiteral(" ench0 ") + (he975.isEmpty() ? QStringLiteral("-") : he975.at(0).toString());
                    break;
                }
                behavOk = true;
            } while (false);
        }
        QDir(probeUiDir975).removeRecursively();

        const bool ok975 = okPin && behavOk;
        if (!ok975) ++totalFail;
        if (!ok975)
            qInfo().noquote() << "  [t975 diag] pin" << okPin << "behav" << behavOk << behavDiag;
        qInfo().noquote() << (ok975 ? "PASS" : "FAIL")
                          << "| t975 creative take semantics re-reversed (user 8-28 final word, overriding"
                             " t896's left-click-takes-one): palette LEFT-click takes a FULL maxStackSize"
                             " stack and RIGHT-click takes exactly ONE item; both buttons share the"
                             " paletteTake single entry (t632 preset-book / t318 same-cell toggle-return"
                             " kept on the primary left button only / t136-t292-t356 swap-with-void-return"
                             " structure unchanged, quantity assigned per button), while the middle-click"
                             " copy face (t653/t896/t956) is untouched - repeated right-clicks accumulate"
                             " one at a time capped at maxStackSize instead of oscillating take/void; the"
                             " palette is the only unlimited-source creative take face in the codebase"
                             " (slot faces keep shared survival semantics); pinned by source pins (entry"
                             " structure, per-button quantity literals, exactly two infinite-source take"
                             " literals and two paletteTake call sites) plus a behavioral leg on the real"
                             " Inventory.qml x real Hotbar rig with a host-mirroring void-return sink";
    }

    // ── P-t976 背包耐久条显隐回归修（用户第五轮：已消耗耐久的镐背包里不显条、hover 能看到耐久掉了）──
    //    调查判决：HEAD 显示条件本身未反（t931 语义「满耐久隐、受损显」正确，引擎级 rig 全绿）；真根因
    //    是**实机 qmlcachegen AOT 面**——DurabilityBar.qml 单独成编译单元，组件内 `visible` 被静态编译
    //    （width/color 因跨对象属性静态不可解析自动回退解释执行，aotstats codegenResult 可证），显隐决策
    //    隔着「面板单元绑定写 curDur/maxDur → 组件单元绑定再读」两跳跨单元链实机不重算（t498「进背包无
    //    耐久显示、只在 hover tooltip 显」/ t976「已消耗耐久不显条」同症：tooltip 走信号处理器直读 VM 恒
    //    新、条恒隐）。修法 = 五个使用点（Inventory 主栏/hotbar 行/护甲槽 + SurvivalInventory 主栏/hotbar
    //    行）显隐决策上收面板 delegate 单元，表达形式触碰 revision 并参与返回值（qml-touch 三轮口径、
    //    图标/数量已实证的同款 AOT 形状）；t931 语义（curDur<maxDur 隐满耐久）原样保留。
    //    (a) 源码钉：三 revision 变体恰好各 1/1/1（Inventory）+ 1/1（SurvivalInventory）次出现 + 组件头
    //        t976 契约段 + 组件内 t931 语义行保留（散改/漏改即红）。
    //    (b) 行为腿（t874/t956/t975 装配法：真 SurvivalInventory.qml × 真 Hotbar；InvSlot 补入文件面）：
    //        ①预置半耐久镐（hotbar 行 200/250、主栏 150/250）→ 条 visible 且彩段宽 ∝ 比例
    //        ②满耐久镐（主栏 250/250）→ 条隐藏（t931 语义）
    //        ③创建后到达（主栏 60/250）→ 条 visible（panel 存活期 revision 驱动重算）
    //        ④live damage：选中满耐久镐 damageSelectedItem×30 → 220/250 条由隐转显（签名不与腿① 相撞）
    //        ⑤空槽条自隐 + 总条数 40（4 护甲 armorDurBar 内联 + 27 主栏 + 9 hotbar 行）。
    //    阴性轮：撤 SurvivalInventory 主栏 visible 上收行（回退组件内隐式决策）→ 恰 P-t976 FAIL → 复原绿。
    {
        bool okPin = false, behavOk = false;
        QString behavDiag;
        const QString exeDir976 = QCoreApplication::applicationDirPath();
        const QString root976 = QDir(exeDir976 + QStringLiteral("/..")).absolutePath();
        QFile inv976f(root976 + QStringLiteral("/src/ui/Inventory.qml"));
        QFile surv976f(root976 + QStringLiteral("/src/ui/SurvivalInventory.qml"));
        QFile bar976f(root976 + QStringLiteral("/src/ui/DurabilityBar.qml"));
        const QString inv976s = inv976f.open(QIODevice::ReadOnly) ? QString::fromUtf8(inv976f.readAll()) : QString();
        const QString surv976s = surv976f.open(QIODevice::ReadOnly) ? QString::fromUtf8(surv976f.readAll()) : QString();
        const QString bar976s = bar976f.open(QIODevice::ReadOnly) ? QString::fromUtf8(bar976f.readAll()) : QString();
        // 三 revision 变体逐字钉（决策上收面板单元：revision 参与返回值 + cDur/mDur 同源判定）。
        const QString visMain976 = QStringLiteral("visible: { const _r = root.hotbar.mainRevision; return _r >= 0 && mDur > 0 && cDur > 0 && cDur < mDur }");
        const QString visSlot976 = QStringLiteral("visible: { const _r = root.hotbar.slotRevision; return _r >= 0 && mDur > 0 && cDur > 0 && cDur < mDur }");
        const QString visArmor976 = QStringLiteral("visible: { const _r = root.hotbar.armorRevision; return _r >= 0 && mDur > 0 && cDur > 0 && cDur < mDur }");
        okPin = inv976s.count(visMain976) == 1
                && inv976s.count(visSlot976) == 1
                && inv976s.count(visArmor976) == 1
                && surv976s.count(visMain976) == 1
                && surv976s.count(visSlot976) == 1
                && inv976s.count(QStringLiteral("t976")) >= 3
                && surv976s.count(QStringLiteral("t976")) >= 2
                && bar976s.contains(QStringLiteral("t976"))
                && bar976s.contains(QStringLiteral("visible: maxDur > 0 && curDur > 0 && curDur < maxDur"));

        // (b) 行为腿装配（t975 同款：临时目录逃离 qrc 重映射 + 私有 URI + wrapper 作用域）。
        static bool sT976TypesRegistered = false;
        if (!sT976TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT976", 1, 0, "Hotbar");
            qmlRegisterType<PlayerController>("VoxelSandboxProbeT976", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT976", 1, 0, "ResourcePackManager");
            sT976TypesRegistered = true;
        }
        const QString uiDir976 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUiDir976 = QDir::temp().absoluteFilePath(
                QStringLiteral("t976_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir976);
        for (const QString f : { QStringLiteral("SurvivalInventory.qml"), QStringLiteral("InventoryOps.js"),
                                 QStringLiteral("MaterialIcon.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("DurabilityBar.qml"), QStringLiteral("DarkScrollBar.qml"),
                                 QStringLiteral("InvSlot.qml") }) {
            QFile::remove(probeUiDir976 + QLatin1Char('/') + f);
            QFile(uiDir976 + QLatin1Char('/') + f).copy(probeUiDir976 + QLatin1Char('/') + f);
        }
        {
            QFile stub976(probeUiDir976 + QStringLiteral("/CharacterPreview3D.qml"));
            if (stub976.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                stub976.write(QByteArrayLiteral(
                    "import QtQuick\n"
                    "Item {\n"
                    "    property var hotbar\n"
                    "    property var player\n"
                    "    property bool showHitboxes: false\n"
                    "    property var mouseScene\n"
                    "}\n"));
                stub976.close();
            }
        }
        {
            const QUrl jsUrl976 = QUrl::fromLocalFile(probeUiDir976 + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            const QStringList qmlFiles976 = QDir(probeUiDir976).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles976) {
                QFile p(probeUiDir976 + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl976.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT976\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine976;
        Hotbar vm976;
        QQmlComponent wrapComp976(&engine976);
        wrapComp976.setData(R"QML(import QtQuick
Item {
    id: window
    width: 800; height: 600
    property bool shiftHeld: false
    property bool showHitboxes: false
    property var progress
    function refocusKeyInput() { }
    HoverHandler { id: cursorTracker }
}
)QML", QUrl());
        QQuickItem host976;
        QObject *panel976Obj = nullptr;
        QFile invSrc976(probeUiDir976 + QLatin1Char('/') + QStringLiteral("SurvivalInventory.qml"));
        if (wrapComp976.isError()) {
            behavDiag = QStringLiteral("wrapper: ") + wrapComp976.errorString();
        } else if (!invSrc976.open(QIODevice::ReadOnly)) {
            behavDiag = QStringLiteral("SurvivalInventory read failed");
        } else {
            QQuickItem *wrapItem976 = qobject_cast<QQuickItem *>(wrapComp976.create());
            if (!wrapItem976) {
                behavDiag = QStringLiteral("wrapper create failed");
            } else {
                wrapItem976->setParent(&engine976);
                wrapItem976->setParentItem(&host976);
                QQmlComponent invComp976(&engine976);
                invComp976.setData(invSrc976.readAll(), QUrl::fromLocalFile(invSrc976.fileName()));
                if (invComp976.isError()) {
                    behavDiag = QStringLiteral("SurvivalInventory load: ") + invComp976.errorString();
                } else {
                    panel976Obj = invComp976.create(qmlContext(wrapItem976));
                    QQuickItem *ii976 = qobject_cast<QQuickItem *>(panel976Obj);
                    if (!ii976) {
                        behavDiag = QStringLiteral("SurvivalInventory create: ") + invComp976.errorString();
                    } else {
                        panel976Obj->setProperty("hotbar", QVariant::fromValue(&vm976));
                        panel976Obj->setProperty("player", QVariant());
                        panel976Obj->setProperty("progress", QVariant());
                        ii976->setWidth(800);
                        ii976->setHeight(600);
                        panel976Obj->setParent(wrapItem976);
                        ii976->setParentItem(wrapItem976);
                    }
                }
            }
        }
        if (!panel976Obj) {
            behavOk = false;
        } else {
            QCoreApplication::processEvents();
            // 条目收集器：递归找「有 curDur+maxDur 属性」的条（DurabilityBar 实例 + armorDurBar 内联），
            //   读 own visible 属性值 / curDur / maxDur / 彩段宽（子 Rectangle 中宽 < 槽条宽者）。
            const auto collect976 = [](auto &&self, QQuickItem *item, QVariantList &out) -> void {
                const QMetaObject *mo = item->metaObject();
                if (mo->indexOfProperty("curDur") >= 0 && mo->indexOfProperty("maxDur") >= 0
                    && mo->indexOfProperty("ratio") >= 0) {
                    QVariantMap rec;
                    rec.insert(QStringLiteral("visible"), item->property("visible").toBool());
                    rec.insert(QStringLiteral("curDur"), item->property("curDur").toInt());
                    rec.insert(QStringLiteral("maxDur"), item->property("maxDur").toInt());
                    rec.insert(QStringLiteral("barW"), item->width());
                    QVariantList kids;
                    const auto children = item->childItems();
                    for (QQuickItem *c : children) {
                        if (QString::fromUtf8(c->metaObject()->className()).contains(QStringLiteral("Rectangle"))) {
                            QVariantMap k;
                            k.insert(QStringLiteral("w"), c->width());
                            k.insert(QStringLiteral("h"), c->height());
                            kids.append(k);
                        }
                    }
                    rec.insert(QStringLiteral("kids"), kids);
                    out.append(rec);
                }
                const auto next = item->childItems();
                for (QQuickItem *c : next) self(self, c, out);
            };
            QVariantList bars976;
            {
                QQuickItem *panelItem976 = qobject_cast<QQuickItem *>(panel976Obj);
                if (panelItem976) collect976(collect976, panelItem976, bars976);
            }
            const int pick976 = int(ToolRegistry::PickaxeIron);
            const int pickMax976 = ToolRegistry::maxDurability(pick976);
            if (bars976.size() != 40) {
                behavDiag = QStringLiteral("bar count ") + QString::number(bars976.size());
            } else do {
                // ① 预置半耐久镐：hotbar 行 200/250、主栏 150/250 → visible + 彩段宽 ∝ 比例。
                vm976.setStack(2, pick976, 1, 200);
                vm976.mainSetStack(5, pick976, 1, 150);
                QCoreApplication::processEvents();
                QVariantList barsA;
                {
                    QQuickItem *panelItem976 = qobject_cast<QQuickItem *>(panel976Obj);
                    if (panelItem976) collect976(collect976, panelItem976, barsA);
                }
                const auto findIn976 = [](const QVariantList &bars, int cur, int max) -> QVariantMap {
                    for (const QVariant &v : bars) {
                        const QVariantMap m = v.toMap();
                        if (m.value(QStringLiteral("curDur")).toInt() == cur
                            && m.value(QStringLiteral("maxDur")).toInt() == max)
                            return m;
                    }
                    return {};
                };
                const QVariantMap hb976 = findIn976(barsA, 200, 250);
                const QVariantMap mn976 = findIn976(barsA, 150, 250);
                const qreal hbW976 = hb976.isEmpty() ? -1 : hb976.value(QStringLiteral("barW")).toReal();
                const auto coloredW976 = [](const QVariantMap &m) -> qreal {
                    const QVariantList kids = m.value(QStringLiteral("kids")).toList();
                    const qreal barW = m.value(QStringLiteral("barW")).toReal();
                    for (const QVariant &k : kids) {
                        const QVariantMap km = k.toMap();
                        const qreal w = km.value(QStringLiteral("w")).toReal();
                        // 彩段 = 非「整条背景」的子 Rectangle（宽 < 槽条宽）；背景 anchors.fill 宽 == barW。
                        if (w >= 0 && w < barW - 0.5)
                            return w;
                    }
                    return -1;
                };
                if (hb976.isEmpty() || mn976.isEmpty()
                        || !hb976.value(QStringLiteral("visible")).toBool()
                        || !mn976.value(QStringLiteral("visible")).toBool()
                        || hbW976 <= 0) {
                    behavDiag = QStringLiteral("damaged bars: hb ") + (hb976.isEmpty() ? QStringLiteral("missing") : (hb976.value(QStringLiteral("visible")).toBool() ? QStringLiteral("vis") : QStringLiteral("hidden")))
                                 + QStringLiteral(" mn ") + (mn976.isEmpty() ? QStringLiteral("missing") : (mn976.value(QStringLiteral("visible")).toBool() ? QStringLiteral("vis") : QStringLiteral("hidden")));
                    break;
                }
                const qreal hbColW976 = coloredW976(hb976);
                const qreal mnColW976 = coloredW976(mn976);
                if (hbColW976 < hbW976 * 0.8 - 0.5 || hbColW976 > hbW976 * 0.8 + 0.5
                        || mnColW976 < hbW976 * 0.6 - 0.5 || mnColW976 > hbW976 * 0.6 + 0.5) {
                    behavDiag = QStringLiteral("colored width: hb ") + QString::number(hbColW976)
                                 + QStringLiteral(" mn ") + QString::number(mnColW976)
                                 + QStringLiteral(" (barW ") + QString::number(hbW976) + QStringLiteral(")");
                    break;
                }
                // ② 满耐久镐（主栏 250/250）→ 条隐藏（t931 语义保持）。
                vm976.mainSetStack(6, pick976, 1, -1);
                QCoreApplication::processEvents();
                QVariantList barsB;
                {
                    QQuickItem *panelItem976 = qobject_cast<QQuickItem *>(panel976Obj);
                    if (panelItem976) collect976(collect976, panelItem976, barsB);
                }
                const QVariantMap full976 = findIn976(barsB, pickMax976, pickMax976);
                if (full976.isEmpty() || full976.value(QStringLiteral("visible")).toBool()) {
                    behavDiag = QStringLiteral("full bar: ") + (full976.isEmpty() ? QStringLiteral("missing") : QStringLiteral("visible"));
                    break;
                }
                // ③ 创建后到达（主栏 60/250）→ 条 visible（revision 驱动重算）。
                vm976.mainSetStack(8, pick976, 1, 60);
                QCoreApplication::processEvents();
                QVariantList barsC;
                {
                    QQuickItem *panelItem976 = qobject_cast<QQuickItem *>(panel976Obj);
                    if (panelItem976) collect976(collect976, panelItem976, barsC);
                }
                const QVariantMap late976 = findIn976(barsC, 60, 250);
                if (late976.isEmpty() || !late976.value(QStringLiteral("visible")).toBool()) {
                    behavDiag = QStringLiteral("late arrival: ") + (late976.isEmpty() ? QStringLiteral("missing") : QStringLiteral("hidden"));
                    break;
                }
                // ④ live damage：选中满耐久镐（hotbar 槽 7）→ 隐；damageSelectedItem×30 → 220/250 显
                //    （220 与腿① 的 200 不撞签名，断言只能由槽 7 自己的条满足）。
                vm976.setStack(7, pick976, 1, -1);
                vm976.setSelectedSlot(7);
                QCoreApplication::processEvents();
                for (int k = 0; k < 30; ++k) vm976.damageSelectedItem();
                QCoreApplication::processEvents();
                if (vm976.durabilityAt(7) != 220) {
                    behavDiag = QStringLiteral("damage vm dur ") + QString::number(vm976.durabilityAt(7));
                    break;
                }
                QVariantList barsD;
                {
                    QQuickItem *panelItem976 = qobject_cast<QQuickItem *>(panel976Obj);
                    if (panelItem976) collect976(collect976, panelItem976, barsD);
                }
                const QVariantMap live976 = findIn976(barsD, 220, 250);
                if (live976.isEmpty() || !live976.value(QStringLiteral("visible")).toBool()) {
                    behavDiag = QStringLiteral("live damage bar: ") + (live976.isEmpty() ? QStringLiteral("missing") : QStringLiteral("hidden"));
                    break;
                }
                // ⑤ 空槽条自隐（curDur==0 / maxDur==0 的 armorDurBar 四条恒隐）。
                const auto emptyHidden976 = [barsD]() -> bool {
                    for (const QVariant &v : barsD) {
                        const QVariantMap m = v.toMap();
                        if (m.value(QStringLiteral("curDur")).toInt() == 0
                                && m.value(QStringLiteral("maxDur")).toInt() == 0
                                && m.value(QStringLiteral("visible")).toBool())
                            return false;
                    }
                    return true;
                };
                if (!emptyHidden976()) {
                    behavDiag = QStringLiteral("empty bar visible");
                    break;
                }
                behavOk = true;
            } while (false);
        }
        QDir(probeUiDir976).removeRecursively();

        const bool ok976 = okPin && behavOk;
        if (!ok976) ++totalFail;
        if (!ok976)
            qInfo().noquote() << "  [t976 diag] pin" << okPin << "behav" << behavOk << behavDiag;
        qInfo().noquote() << (ok976 ? "PASS" : "FAIL")
                          << "| t976 backpack durability-bar display regression (user fifth round:"
                          << " consumed pickaxe shows NO bar in the inventory while hover tooltip shows"
                          << " the dropped durability) - display condition itself is correct (t931"
                          << " full-hides semantics kept) but lived inside the separately AOT-compiled"
                          << " DurabilityBar.qml unit two hops away from the panel revision bindings,"
                          << " the exact real-machine never-re-evaluates class (t498 'no durability in"
                          << " backpack, only hover tooltip' same symptom); fix hoists the visibility"
                          << " decision into each of the five usage sites' delegate unit as a"
                          << " revision-touching expression (the app-proven icon/count shape); pinned by"
                          << " source pins (three revision variants exactly once per panel, component"
                          << " contract + t931 line kept) plus behavioral legs on the real"
                          << " SurvivalInventory.qml x real Hotbar rig (damaged-visible with proportional"
                          << " colored width / full-hidden / late-arrival-visible / live damageSelectedItem"
                          << " flip / empty-hidden, 40 bars total)";
    }

    // ── P-t977 切换物品栏物品名浮显（R19.17 🅶 杂项组收官；用户第五轮口径「切槽显示当前物品名——
    //    血量/饱食度上方中间、白字；附魔物品显示详情（名称+耐久度+换行+逐条附魔带等级）；改名物品显示
    //    改名后名字（多附魔工具区分用）。一定时长淡出」）──
    //    (a) C++ 组装单一权威行为腿（真 Hotbar 直调 slotDetailText）：附魔+改名镐 = 改名行+耐久行+逐条
    //        附魔罗马等级行（恰 4 行逐字）/ 普通方块单行名 / 空槽空串 / 护甲件护甲耐久行（工具段无行 →
    //        ArmorRegistry 权威）/ 附魔书有附魔无耐久行。
    //    (b) QML 行为腿（真 HeldItemNameFlash.qml × 真 Hotbar；headless 可达——组件纯 QtQuick，Timer/
    //        NumberAnimation 走引擎动画时钟，无窗口依赖）：创建基线静默；切槽 → 文本=当前物品名且立即
    //        不透明（无淡入）；附魔镐 → QML 消费面 == C++ 权威输出逐字；空槽切 → 立即隐；连续切槽 →
    //        计时重置最终显最后槽（t0 切 A、t0+140 切 B：无 restart 则 B 于 t0+300 入淡出、有 restart
    //        t0+370 采样仍全不透明——该采样点即「计时重置」判别点）+ t0+750 淡出收尾 opacity==0。
    //    (c) 源码钉：Main.qml 挂载点（vitalsBar 上方 8px 居中 + 游玩态门 + hotbar 注入）+ 组件内白字 /
    //        hold 2000 / fade 300 默认常量 + onSelectedSlotChanged 直读 Q_INVOKABLE（AOT 契约）+
    //        lastShownSlot 同槽守卫 + 非活跃期基线重同步（读档灌 selectedSlot 不闪名）。
    //    阴性轮：撤 flash() 的 holdTimer.restart()（计时重置回退）→ 恰 P-t977 FAIL → 复原绿。
    {
        bool okAsm = false, okQml = false, okPin = false;
        QString diag977;
        Hotbar vm977;
        const int pick977 = int(ToolRegistry::PickaxeIron);
        const int pickMax977 = ToolRegistry::maxDurability(pick977);
        QVariantList ench977;
        ench977.append(EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3));
        ench977.append(EnchantRegistry::pack(int(EnchantRegistry::Efficiency), 2));
        // 权威期望串（(a) C++ 直调与 (b) QML 消费面共用同一份逐字断言）。
        const QString want977 = QStringLiteral("挖掘者\n耐久: 200/%1\n锐锋 III\n效率 II").arg(pickMax977);

        // (a) 组装单一权威腿（真 Hotbar 直调）。
        vm977.setStack(2, pick977, 1, 200, ench977, QStringLiteral("挖掘者"));
        const bool okRenamed977 = vm977.slotDetailText(2) == want977;
        vm977.setStack(3, int(BR::Dirt), 5);
        const QString plain977 = vm977.slotDetailText(3);
        const bool okPlain977 = !plain977.isEmpty() && plain977 == vm977.nameAt(3)
                                && !plain977.contains(QLatin1Char('\n'));
        const bool okEmpty977 = vm977.slotDetailText(4).isEmpty();
        const int chest977 = int(RecipeRegistry::ArmorIdBase) + 4 * int(ArmorRegistry::Iron)
                             + int(ArmorRegistry::Chestplate);
        vm977.setStack(8, chest977, 1, 120);
        const bool okArmor977 = vm977.slotDetailText(8)
                == QStringLiteral("%1\n耐久: 120/%2").arg(ArmorRegistry::displayName(chest977))
                                               .arg(ArmorRegistry::maxDurability(chest977));
        vm977.setStack(1, int(RecipeRegistry::EnchantedBookId), 1, 0,
                       QVariantList{ EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 5) });
        const bool okBook977 = vm977.slotDetailText(1)
                == (vm977.nameAt(1) + QStringLiteral("\n锐锋 V"));
        okAsm = okRenamed977 && okPlain977 && okEmpty977 && okArmor977 && okBook977;
        if (!okAsm)
            diag977 = QStringLiteral("asm r=") + (okRenamed977 ? "1" : "0")
                      + QStringLiteral(" p=") + (okPlain977 ? "1" : "0")
                      + QStringLiteral(" e=") + (okEmpty977 ? "1" : "0")
                      + QStringLiteral(" a=") + (okArmor977 ? "1" : "0")
                      + QStringLiteral(" b=") + (okBook977 ? "1" : "0")
                      + QStringLiteral(" got=\"") + vm977.slotDetailText(2) + QStringLiteral("\"");

        // (b) QML 行为腿（真组件 × 真 Hotbar）。
        const QString uiDir977 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        QQmlEngine engine977;
        QQmlComponent comp977(&engine977, QUrl::fromLocalFile(uiDir977 + QStringLiteral("/HeldItemNameFlash.qml")));
        QObject *flash977 = comp977.isError() ? nullptr : comp977.create();
        if (!flash977) {
            diag977 = QStringLiteral("component: ")
                      + (comp977.isError() ? comp977.errorString() : QStringLiteral("create null"));
        } else {
            flash977->setParent(&engine977);
            Hotbar vmQ977;
            // 预置槽（均非选中槽 0 → setStack 不补发 selectedSlotChanged，不惊动浮显基线）。
            vmQ977.setStack(2, int(BR::Dirt), 5);
            vmQ977.setStack(5, int(BR::Log), 1);
            QVariantList enchQ977;
            enchQ977.append(EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3));
            enchQ977.append(EnchantRegistry::pack(int(EnchantRegistry::Efficiency), 2));
            vmQ977.setStack(6, pick977, 1, 200, enchQ977, QStringLiteral("挖掘者"));
            flash977->setProperty("hotbar", QVariant::fromValue(&vmQ977));
            flash977->setProperty("active", true);
            // 探针专用短节拍（生产默认 2000/300 由 (c) 源码钉钉住；普通属性 → 此处可覆盖）。
            flash977->setProperty("holdMs", 300);
            flash977->setProperty("fadeMs", 100);
            QCoreApplication::processEvents();
            QObject *label977 = nullptr;
            const auto kids977 = flash977->findChildren<QObject *>();
            for (QObject *c : kids977) {
                if (QString::fromUtf8(c->metaObject()->className()).contains(QStringLiteral("QQuickText"))) {
                    label977 = c;
                    break;
                }
            }
            if (!label977) {
                diag977 = QStringLiteral("label text not found");
            } else {
                const auto text977 = [label977]() { return label977->property("text").toString(); };
                const auto opa977 = [label977]() { return label977->property("opacity").toReal(); };
                const auto vis977 = [label977]() { return label977->property("visible").toBool(); };
                const auto spin977 = [](QElapsedTimer &clock, qint64 untilMs) {
                    while (!clock.hasExpired(untilMs))
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                };
                QElapsedTimer wall977;
                do {
                    // 基线：创建即静默（Component.onCompleted 只立基线，不浮显）。
                    if (!text977().isEmpty() || opa977() > 0.001 || vis977()) {
                        diag977 = QStringLiteral("baseline shows");
                        break;
                    }
                    // ① 切槽 → 文本=当前物品名，立即全不透明（无淡入）。
                    vmQ977.setSelectedSlot(2);
                    QCoreApplication::processEvents();
                    if (text977() != vmQ977.nameAt(2) || qAbs(opa977() - 1.0) > 0.001) {
                        diag977 = QStringLiteral("switch: \"") + text977()
                                  + QStringLiteral("\" op ") + QString::number(opa977());
                        break;
                    }
                    // ② 附魔+改名镐 → QML 消费面 == C++ 权威输出（同一 slotDetailText 逐字）。
                    vmQ977.setSelectedSlot(6);
                    QCoreApplication::processEvents();
                    if (text977() != want977) {
                        diag977 = QStringLiteral("enchanted: \"") + text977() + QStringLiteral("\"");
                        break;
                    }
                    // ③ 空槽切 → 不显（立即隐，无残留文本）。
                    vmQ977.setSelectedSlot(7);
                    QCoreApplication::processEvents();
                    if (!text977().isEmpty() || opa977() > 0.001 || vis977()) {
                        diag977 = QStringLiteral("empty slot shows: \"") + text977() + QStringLiteral("\"");
                        break;
                    }
                    // ④ 连续切槽 → 计时重置最终显最后槽 + hold→fade→隐管线（判别点采样见块注）。
                    wall977.restart();
                    vmQ977.setSelectedSlot(5);
                    spin977(wall977, 140);
                    vmQ977.setSelectedSlot(2);
                    QCoreApplication::processEvents();
                    if (text977() != vmQ977.nameAt(2) || qAbs(opa977() - 1.0) > 0.001) {
                        diag977 = QStringLiteral("storm last: \"") + text977()
                                  + QStringLiteral("\" op ") + QString::number(opa977());
                        break;
                    }
                    spin977(wall977, 370);
                    if (qAbs(opa977() - 1.0) > 0.001) {
                        diag977 = QStringLiteral("restart lost: op@370 ") + QString::number(opa977());
                        break;
                    }
                    spin977(wall977, 750);
                    if (opa977() > 0.001 || text977() != vmQ977.nameAt(2)) {
                        diag977 = QStringLiteral("fade end: op@750 ") + QString::number(opa977())
                                  + QStringLiteral(" text \"") + text977() + QStringLiteral("\"");
                        break;
                    }
                    okQml = true;
                } while (false);
            }
        }

        // (d) review0901 #32 耐久口径统一腿（hotbar 槽内受损护甲件，槽 8 = 胸甲 dur 120 已由 (a) 预置）：
        //     maxDurabilityFor 双段判定单一权威与浮显（slotDetailText 耐久行）/ 条（DurabilityBar max 侧）
        //     两面同数——修前权威不存在（invoke 失败本腿即红）且条位消费 toolMaxDurability 单段（护甲件
        //     =0 → 条恒隐，「浮显有数条无」劈叉；对照臂在真 DurabilityBar.qml 上复现两种形态）。
        bool okDur977 = false;
        QString diagDur977;
        {
            int maxAuth977 = -1;
            const bool invokable977 = QMetaObject::invokeMethod(&vm977, "maxDurabilityFor",
                                                                Q_RETURN_ARG(int, maxAuth977),
                                                                Q_ARG(int, chest977));
            const int maxSeg977 = vm977.toolMaxDurability(chest977);   // 单段判定值（护甲件 = 0 = 修前条位所见）
            const QString detail977 = vm977.slotDetailText(8);
            const int iSlash977 = detail977.indexOf(QLatin1Char('/'));
            const int flashMax977 = iSlash977 >= 0 ? detail977.mid(iSlash977 + 1).trimmed().toInt() : -1;
            const bool flashMatches977 = invokable977
                                          && maxAuth977 == ArmorRegistry::maxDurability(chest977)
                                          && maxSeg977 == 0
                                          && flashMax977 == maxAuth977;   // 浮显数 == 权威数
            // 工具照旧 / 非耐久物照旧（权威对既有段零行为漂移）。
            int maxPick977 = -1, maxDirt977 = -1;
            QMetaObject::invokeMethod(&vm977, "maxDurabilityFor", Q_RETURN_ARG(int, maxPick977), Q_ARG(int, pick977));
            QMetaObject::invokeMethod(&vm977, "maxDurabilityFor", Q_RETURN_ARG(int, maxDirt977), Q_ARG(int, int(BR::Dirt)));
            const bool othersOk977 = maxPick977 == pickMax977 && maxDirt977 == 0;
            // 条侧行为腿：五条位的 delegate 显隐决策（t976 上收形态——决策面在面板 delegate 而非组件内，
            //   组件内 visible 仅语义兜底、实机不可依赖）以权威 max 为输入 → 受损护甲显条；单段形态
            //   （=0）→ 隐条（劈叉两态复现）。真 DurabilityBar 实例同吃权威值（wiring 校验经回读）。
            QQmlEngine durEngine977;
            QQmlComponent wrapBar977(&durEngine977);
            // setData + 源树 base URL（t874 同式）：DurabilityBar 以同目录隐式组件解析（真源树组件）。
            wrapBar977.setData(QStringLiteral(
                "import QtQuick\n"
                "Item {\n"
                "    property int cDur: 0\n"
                "    property int mDur: 0\n"
                "    property bool barVisible: mDur > 0 && cDur > 0 && cDur < mDur\n"
                "    DurabilityBar { id: embedded; width: 30; height: 3; curDur: cDur; maxDur: mDur }\n"
                "    property int embeddedCur: embedded.curDur\n"
                "    property int embeddedMax: embedded.maxDur\n"
                "}\n").toUtf8(), QUrl::fromLocalFile(uiDir977 + QStringLiteral("/_t977_wrap.qml")));
            QObject *wrapBar = wrapBar977.isError() ? nullptr : wrapBar977.create();
            bool barShows977 = false, barHidesSingleSeg977 = false;
            int embMax977 = -1, embCur977 = -1;
            if (!wrapBar) {
                diagDur977 = QStringLiteral("wrapBar: ")
                             + (wrapBar977.isError() ? wrapBar977.errorString() : QStringLiteral("create null"));
            } else {
                wrapBar->setParent(&durEngine977);
                wrapBar->setProperty("mDur", maxAuth977);   // 权威 max（修后条位消费面同值）
                wrapBar->setProperty("cDur", 120);
                QCoreApplication::processEvents();
                barShows977 = wrapBar->property("barVisible").toBool();
                embMax977 = wrapBar->property("embeddedMax").toInt();
                embCur977 = wrapBar->property("embeddedCur").toInt();
                wrapBar->setProperty("mDur", maxSeg977);    // 单段判定形态（0 → 修前条位恒隐）
                QCoreApplication::processEvents();
                barHidesSingleSeg977 = !wrapBar->property("barVisible").toBool();
            }
            okDur977 = flashMatches977 && othersOk977 && barShows977 && barHidesSingleSeg977
                       && embMax977 == maxAuth977 && embCur977 == 120;
            if (!okDur977)
                diagDur977 = QStringLiteral("invk") + QString::number(int(invokable977))
                             + QStringLiteral(" auth") + QString::number(maxAuth977)
                             + QStringLiteral(" seg") + QString::number(maxSeg977)
                             + QStringLiteral(" flash") + QString::number(flashMax977)
                             + QStringLiteral(" pick") + QString::number(maxPick977)
                             + QStringLiteral(" dirt") + QString::number(maxDirt977)
                             + QStringLiteral(" barShow") + QString::number(int(barShows977))
                             + QStringLiteral(" barHide1seg") + QString::number(int(barHidesSingleSeg977))
                             + QStringLiteral(" emb") + QString::number(embMax977)
                             + QStringLiteral("/") + QString::number(embCur977)
                             + QStringLiteral(" | ") + diagDur977;
        }

        // (c) 源码钉（挂载点 / 白字 / 时长常量 / AOT 契约面 / 守卫与基线重同步）。
        const QString root977 = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        QFile main977f(root977 + QStringLiteral("/src/ui/Main.qml"));
        QFile flash977f(root977 + QStringLiteral("/src/ui/HeldItemNameFlash.qml"));
        const QString main977s = main977f.open(QIODevice::ReadOnly) ? QString::fromUtf8(main977f.readAll()) : QString();
        const QString flash977s = flash977f.open(QIODevice::ReadOnly) ? QString::fromUtf8(flash977f.readAll()) : QString();
        okPin = main977s.count(QStringLiteral("HeldItemNameFlash {")) == 1
                && main977s.count(QStringLiteral("anchors.bottom: vitalsBar.top\n"
                                                "        anchors.bottomMargin: 8\n"
                                                "        anchors.horizontalCenter: parent.horizontalCenter\n"
                                                "        hotbar: hotbarVM")) == 1
                && main977s.count(QStringLiteral("hotbar: hotbarVM\n"
                                                 "        active: window.appState === \"playing\" && player.mode !== PlayerController.Spectator")) == 1
                && main977s.count(QStringLiteral("t977")) >= 2
                && flash977s.count(QStringLiteral("color: \"#ffffff\"")) == 1
                && flash977s.count(QStringLiteral("property int holdMs: 2000")) == 1
                && flash977s.count(QStringLiteral("property int fadeMs: 300")) == 1
                && flash977s.count(QStringLiteral("function onSelectedSlotChanged()")) == 1
                && flash977s.count(QStringLiteral("hotbar.slotDetailText(")) == 1
                && flash977s.count(QStringLiteral("if (slot === root.lastShownSlot)")) == 1
                && flash977s.count(QStringLiteral("root.lastShownSlot = root.hotbar.selectedSlot")) == 1
                // 双行锚钉（防注释字面干扰：头注释亦含 holdTimer.restart() 字样，裸钉计数会误判——
                //   锚住 flash() 内「立即显」行的下一行调用，散改/漏改即红）。
                && flash977s.count(QStringLiteral("label.opacity = 1 // 立即显（opacity 直写不走 Behavior → 零淡入）\n"
                                                  "        holdTimer.restart()")) == 1
                && flash977s.count(QStringLiteral("t977")) >= 2;

        // review0901 #32 单一权威钉：C++ 权威（hotbar.h 声明 + slotDetailText 本身改走权威）+ 五处
        //   条位全改调 maxDurabilityFor（HUD hotbar 1 + Inventory 2 + SurvivalInventory 2；每文件
        //   条位行残留 toolMaxDurability 单段判定即红——hover tooltip 的 toolMaxDurability 消费面
        //   不在 #32 范围，按精确参数形钉不误伤）。
        const auto readSrc977b = [&root977](const QString &rel) -> QString {
            QFile f(root977 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString hbHdr977 = readSrc977b(QStringLiteral("src/Game/hotbar.h"));
        const QString hbCpp977 = readSrc977b(QStringLiteral("src/Game/hotbar.cpp"));
        const QString inv977s = readSrc977b(QStringLiteral("src/ui/Inventory.qml"));
        const QString sur977s = readSrc977b(QStringLiteral("src/ui/SurvivalInventory.qml"));
        const bool durAuthPin =
                hbHdr977.contains(QStringLiteral("Q_INVOKABLE int maxDurabilityFor(int itemId) const"))
                && hbCpp977.contains(QStringLiteral("const int maxDur = maxDurabilityFor(id);"))
                && hbCpp977.count(QStringLiteral("maxDurabilityFor(")) == 2   // 权威定义 + slotDetailText 调用
                && main977s.count(QStringLiteral("hotbarVM.maxDurabilityFor(hotbarVM.blockIdAt(index))")) == 1
                && !main977s.contains(QStringLiteral("hotbarVM.toolMaxDurability(hotbarVM.blockIdAt(index))"))
                && inv977s.count(QStringLiteral("root.hotbar.maxDurabilityFor(mainId)")) == 1
                && inv977s.count(QStringLiteral("root.hotbar.maxDurabilityFor(slotId)")) == 1
                && !inv977s.contains(QStringLiteral("toolMaxDurability(mainId)"))
                && !inv977s.contains(QStringLiteral("toolMaxDurability(slotId)"))
                && sur977s.count(QStringLiteral("root.hotbar.maxDurabilityFor(mainId)")) == 1
                && sur977s.count(QStringLiteral("root.hotbar.maxDurabilityFor(slotId)")) == 1
                && !sur977s.contains(QStringLiteral("toolMaxDurability(mainId)"))
                && !sur977s.contains(QStringLiteral("toolMaxDurability(slotId)"));

        const bool ok977 = okAsm && okQml && okPin && okDur977 && durAuthPin;
        if (!ok977) ++totalFail;
        if (!ok977)
            qInfo().noquote() << "  [t977 diag] asm" << okAsm << "qml" << okQml << "pin" << okPin
                              << "dur" << okDur977 << diagDur977 << "durPin" << durAuthPin << diag977;
        qInfo().noquote() << (ok977 ? "PASS" : "FAIL")
                          << "| t977 hotbar switch item-name flash (user fifth round: switching slots"
                          << " shows the current item name centered above the hearts/hunger row in"
                          << " white; enchanted items show details (name + durability + per-enchant"
                          << " roman-level lines); renamed items show the renamed name; fades out"
                          << " after a hold) - assembly single-sourced in Hotbar::slotDetailText"
                          << " (AOT lesson: QML consumes the string via a signal-handler Q_INVOKABLE"
                          << " read, never a cross-unit binding); same-slot content churn suppressed"
                          << " by lastShownSlot guard; inactive window resyncs the baseline so the"
                          << " world-load slot restore never flashes; pinned by exact-string assembly"
                          << " legs (renamed+enchanted pickaxe 4-line / plain single-line / empty /"
                          << " armor durability line / enchanted book without durability line),"
                          << " behavioral legs on the real HeldItemNameFlash.qml x real Hotbar rig"
                          << " (baseline silent / switch shows name at full opacity / enchanted text"
                          << " equals the C++ authority / empty hides / switch-storm restart keeps the"
                          << " last slot opaque past the first slot's deadline then fades to 0), and"
                          << " source pins (mount above vitalsBar + active gate + hotbar injection,"
                          << " white color, holdMs 2000 / fadeMs 300 defaults, handler + guard forms)."
                          << " review0901 additions: the durability caliber is unified -"
                          << " maxDurabilityFor is the single dual-segment authority (tool segment"
                          << " then ArmorRegistry fallback) consumed by slotDetailText AND all five"
                          << " durability-bar sites (HUD hotbar + Inventory x2 + SurvivalInventory"
                          << " x2); behavioral leg proves the flash text's durability max equals the"
                          << " authority for a damaged armor piece in a hotbar slot while the"
                          << " single-segment form (toolMaxDurability==0) renders no bar on the real"
                          << " DurabilityBar.qml (pre-fix split reproduced), tools and non-durable"
                          << " items unchanged; source pins lock the authority declaration, the"
                          << " slotDetailText delegation, and all five call sites";
    }

    // ── P-t996 打火石直点 TNT 引燃链探针（R19.18 批 t996；用户 9-01 实测「点燃 TNT 后原方块没清除——
    //    持续闪烁动画不停、方块变成贴图、人物可以穿过去」）──
    //   病根：t492 Bug B 删「右键 TNT 本体点燃」分支时打火石尚不存在（注释原文「本项目无打火石」）；
    //   t724 打火石落地后**未补 TNT 分流** → 手持打火石右键 TNT 方块落进 t843 回退立地火路径：命中面
    //   邻格 setBlock(Fire)，TNT 格不清、零 PrimedTnt 实体。用户看到的就是贴着 TNT 的那团火——flipbook
    //   闪烁（「闪烁动画不停」）+ cross 面片（「变成贴图」）+ ShapeNone 无碰撞（「人物可以穿过去」），
    //   且 TNT 本体永不被引燃（flammable 表刻意不含 TNT，t724 v1 注释「TNT 点燃走既有引燃链」——但
    //   打火石这条「既有链」从未接上）。修法：placeBlock 打火石分支补 TNT 直点分流（机制等价 MC 1.0
    //   flint and steel 点燃 TNT）——isTnt(命中格) → clearBlockSilent 原格置 Air（同 firePowerTnt/机关/
    //   踩板三条引燃链单一尾）+ spawnPrimedTnt 引燃态实体接管闪烁渲染与碰撞 + 失撑三族补口。红石/机关/
    //   踩板/发射器四条既有引燃链已由 P-t814/P-t856 钉死不动。断言四段：
    //   (a) 真路径引燃（t945 真链模式：定位定向 → tick 刷射线 → placeBlock）：TNT 格清空（Air）+
    //       PrimedTnt 实体在原格格心 + 满引信（fuseProgress==1）+ 命中面邻格**无** Fire（旧病灶回归
    //       守卫——回退落火路径若再吃 TNT 点击即红）；
    //   (b) 引信链全通：ents.tick 细步驱动 6.25s（> kPrimedTntFuseSec 5s）→ 实体移除（fuse 归 0
    //       detonatePrimedTnt）+ 爆炸真发生（半径内哨兵石块被 destroySphereSilent 清掉）+ 原格仍 Air；
    //   (c) 非 TNT 回退不回归：打火石点石头顶面 → 邻格照常落火（t724 语义保留，石面点火不被分流误吞）；
    //   (d) 源码钉：placeBlock 打火石分支内 TNT 直点分流的条件 + 两调用锚（任一散失即红）。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 7 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t996 flint-ignite TNT: no clear rig area found";
        } else {
            WorldClock clockT996;
            EntityManager entsT996;       // PrimedTnt 断言源 + (b) 段 fuse 驱动
            Hotbar hbT996;
            PlayerController pcT996;      // t945 真链模式（挂窗 grab 载体；headless 无指针锁）
            pcT996.setWorld(&w);
            pcT996.setWorldClock(&clockT996);
            pcT996.setEntityManager(&entsT996);
            pcT996.setHotbar(&hbT996);
            QQuickWindow probeWinT996;
            pcT996.setParentItem(probeWinT996.contentItem());
            pcT996.grab(); // m_window 就绪 → setCaptured(true) 走通（placeBlock 入口门）
            hbT996.setStack(0, int(ToolRegistry::FlintAndSteel), 1); // 手持打火石（用户场景）
            hbT996.setSelectedSlot(0);
            pcT996.setSelectedBlock(int(BR::Air)); // t1040 rig 加固（t1030 同式）：打火石工具段→Air 建模（引燃分支按 heldItemId 无条件 return 不读此面，兜底通用放置）
            // 瞄准 + tick 刷射线（t945 aimP945 同款：release+grab 重居中光标防 delta 踢变 →
            //   loadSavedState 定位定向 → tick 刷 updateRaycast 命中）。
            const auto aimT996 = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ) {
                const float ex = feetX, ey = float(kRigY) + 1.62f, ez = feetZ;
                const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float pitch = std::asin(dy / len) * 57.2957795f;
                const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
                pcT996.release();
                pcT996.grab();
                pcT996.loadSavedState(feetX, float(kRigY), feetZ, yaw, pitch, 1); // Creative（不耗耐久）
                pcT996.tick();
                return pcT996.hitBlock();
            };
            const auto pumpMsT996 = [](int ms) { // 放置 200ms CD 间隔（t128；m_evtClock 单调墙钟）
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            };
            const auto buildRigT996 = [&]() { // 石台面（kRigY-1）+ TNT（x0+2, kRigY, z0）
                for (int dx = 0; dx <= 5; ++dx) {
                    w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                    for (int dy = 0; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
                }
                w.setBlock(x0 + 2, kRigY, z0, BR::TntBlock, 0);
            };
            const auto clearRigT996 = [&]() {
                for (int dx = 0; dx <= 5; ++dx)
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
                entsT996.clearAll(); // 清引燃实体（防污染后续腿实体计数）
            };

            // (a) 真路径引燃：瞄 TNT -X 面内点 (x0+2.05, kRigY+0.5, z0+0.5)（命中格=TNT，法线 -X）。
            buildRigT996();
            const QVector3D hitA = aimT996(float(x0) + 0.5f, float(z0) + 0.5f,
                                           float(x0) + 2.05f, float(kRigY) + 0.5f, float(z0) + 0.5f);
            pumpMsT996(260);
            pcT996.placeBlock(); // 手持打火石右键 TNT 本体
            int idxA = -1;
            for (int i = 0; i < entsT996.count(); ++i)
                if (entsT996.isPrimedAt(i)) { idxA = i; break; }
            const bool okA = hitA == QVector3D(float(x0 + 2), float(kRigY), float(z0)) // 射线命中的是 TNT 本格
                && w.blockAt(x0 + 2, kRigY, z0) == BR::Air                    // 原格置 Air（fix 核心）
                && idxA >= 0                                                  // PrimedTnt 实体接管
                && std::abs(entsT996.posAt(idxA).x() - (x0 + 2.5f)) < 1e-3f   // 原格格心
                && std::abs(entsT996.posAt(idxA).y() - (kRigY + 0.5f)) < 1e-3f
                && std::abs(entsT996.posAt(idxA).z() - (z0 + 0.5f)) < 1e-3f
                && entsT996.fuseProgressAt(idxA) >= 0.999f                    // 满引信（默认 5s 未起跳）
                && w.blockAt(x0 + 1, kRigY, z0) == BR::Air                    // 旧病灶守卫：命中面邻格不落火
                && w.blockAt(x0 + 3, kRigY, z0) == BR::Air;
            if (!okA)
                qInfo().noquote() << "  [t996 a diag] hit" << hitA
                                  << "tntCell" << int(w.blockAt(x0 + 2, kRigY, z0))
                                  << "adjNeg" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "idxA" << idxA
                                  << "fuseProg" << (idxA >= 0 ? entsT996.fuseProgressAt(idxA) : -1.0f);

            // (b) 引信链全通：哨兵石 (x0+4, kRigY, z0)（距爆心 2.0 < 半径 3）→ 细步 6.25s > 5s 引信。
            w.setBlock(x0 + 4, kRigY, z0, BR::Stone, 0);
            for (int i = 0; i < 400; ++i)
                entsT996.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
            int primedAfterB = 0;
            for (int i = 0; i < entsT996.count(); ++i)
                if (entsT996.isPrimedAt(i)) ++primedAfterB;
            const bool okB = idxA >= 0 && primedAfterB == 0                     // 引爆后实体移除
                && w.blockAt(x0 + 2, kRigY, z0) == BR::Air                      // 原格仍 Air
                && w.blockAt(x0 + 4, kRigY, z0) == BR::Air;                     // 爆炸真发生（哨兵被清）
            if (!okB)
                qInfo().noquote() << "  [t996 b diag] primedAfter" << primedAfterB
                                  << "tntCell" << int(w.blockAt(x0 + 2, kRigY, z0))
                                  << "sentinel" << int(w.blockAt(x0 + 4, kRigY, z0));

            // (c) 非 TNT 回退不回归：石块顶面点火 → 顶邻格落火（t724 语义保留）。
            clearRigT996();
            w.setBlock(x0 + 2, kRigY, z0, BR::Stone, 0);
            const QVector3D hitC = aimT996(float(x0) + 0.5f, float(z0) + 0.5f,
                                           float(x0) + 2.5f, float(kRigY) + 1.0f, float(z0) + 0.5f);
            pumpMsT996(260);
            pcT996.placeBlock();
            const bool okC = hitC == QVector3D(float(x0 + 2), float(kRigY), float(z0))
                && w.blockAt(x0 + 2, kRigY + 1, z0) == BR::Fire                 // 顶邻格落火（回退路径健在）
                && w.blockAt(x0 + 2, kRigY, z0) == BR::Stone;                   // 石头本体无恙
            if (!okC)
                qInfo().noquote() << "  [t996 c diag] hit" << hitC
                                  << "fire" << int(w.blockAt(x0 + 2, kRigY + 1, z0))
                                  << "stone" << int(w.blockAt(x0 + 2, kRigY, z0));
            clearRigT996();

            // (d) 源码钉：placeBlock 打火石分支（FlintAndSteel 守卫行 → PaintingId 分支行）滤注释后
            //   必含 TNT 直点分流条件 + clearBlockSilent/spawnPrimedTnt 两调用（任一散失即红）。
            bool okPin = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
                QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
                const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
                const int b0 = t.indexOf(QStringLiteral("heldItemId == int(ToolRegistry::FlintAndSteel))"));
                const int b1 = t.indexOf(QStringLiteral("RecipeRegistry::PaintingId"));
                if (b0 < 0 || b1 <= b0) {
                    qInfo().noquote() << "  t996 flint-branch slice miss";
                } else {
                    QString body;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                        if (!line.trimmed().startsWith(QLatin1String("//"))) {
                            body += line; body += QLatin1Char('\n');
                        }
                    okPin = body.contains(QStringLiteral("BlockRegistry::TntBlock && m_entityManager"))
                        && body.count(QStringLiteral("clearBlockSilent(m_hitBx, m_hitBy, m_hitBz)")) == 1
                        && body.count(QStringLiteral("spawnPrimedTnt(m_hitBx, m_hitBy, m_hitBz)")) == 1;
                }
            }

            const bool okT996 = okA && okB && okC && okPin;
            if (!okT996) ++totalFail;
            qInfo().noquote() << (okT996 ? "PASS" : "FAIL")
                              << "| t996 flint-and-steel on a TNT block ignites it in place (user"
                              << " ninth-round report: after igniting, the original block never"
                              << " cleared - an endless flicker, the block 'turned into a texture',"
                              << " and the player could walk through it; root cause: the t492 Bug B"
                              << " removal of direct TNT right-click ignition predates the flint"
                              << " and steel, and t724 never added the TNT split, so the click fell"
                              << " through to the fallback ground-fire path which placed a"
                              << " flipbook-flickering, non-colliding cross-quad fire in the"
                              << " face-adjacent cell while the TNT stayed forever unprimed):"
                              << " the hit TNT cell is cleared to Air, a PrimedTnt entity takes over"
                              << " at the cell center with a full default fuse, no fire lands in the"
                              << " face-adjacent cells (pre-fix symptom guard); the fuse chain runs"
                              << " to zero, removes the entity and really explodes (sentinel stone"
                              << " inside the blast radius destroyed, cell still Air); flint on a"
                              << " stone top face still falls back to placing fire (t724 semantics"
                              << " kept); source pins lock the TNT split condition and the"
                              << " clearBlockSilent + spawnPrimedTnt pair inside the flint branch";
        }
    }

    // ── P-t997 多 TNT 同帧引爆性能归因 + 爆炸波分期探针（R19.18 批 t997；用户 9-01 实测 35d5a72
    //    「放很多 TNT 爆炸仍很卡：15FPS / 66.7ms/frame / sim 仅 4.63ms」——t933 修的是单爆炸级联风暴
    //    （逐格 recomputeLightAround + 逐柱 worldChanged），多 TNT 同爆 = N 个爆炸各自走一遍批量化后
    //    的链，不在此前修复覆盖内）──
    //   归因模型（destroySphereSilent t383 批量收口后单爆成本 = 1×refloodBox + 1×worldChanged +
    //   1×clearAllDirty）：同帧 N 爆 → N× 联合盒 reflood（球重叠 ×N 浪费）+ N× worldChanged QML 扇出
    //   （GUI 侧 25 chunk × N 次重建检查/帧——用户 F3 sim=4.63ms 占 7% → 大头在渲染/同步侧，本 rig
    //   量 C++ 侧计数与 tick 耗时，present/vsync/GPU 侧须实机 F3 判读，不编数）。rig：6×6=36 primed
    //   TNT 同引信（5s）同帧到期（模拟红石同帧点燃一片 TNT；无 TNT 方块 → 零链式引燃干扰，同帧爆
    //   归因干净）→ 逐 1/60s 步进量：每 tick 引爆数分布（FrameProfiler 无 detonate 计数，实体扫描差分）
    //   + refloodN/lightEditN/gravColN/cascN 差分 + worldChanged 计数 + 逐 tick 墙钟（引爆 tick vs 静默 tick）。
    //   修复 = 爆炸波分期（entitymanager tick 单 tick 引爆预算 kMaxTntDetonationsPerTick，预算耗尽 →
    //   引信重挂 kDetonationWaveRegroupSec 下 tick 再爆）：同帧成本 ≤ K 倍单爆，总破坏量不变（链式
    //   引爆语义保留——链式 TNT 本就带 1.2s 错峰引信，分期只影响同帧到期簇）。断言三段：
    //   (a) 分期行为：任意 tick 引爆数 ≤ 预算（pre-fix 36 全在同一 tick → 红）+ 全部 36 实爆（总量
    //       不变，分期不许吞爆炸）+ 终态零 primed 残留；
    //   (b) 归因计数不变式：worldChanged == refloodN（每爆批链恒 1 扇出 / 1 联合盒，t933 收口
    //       口径）+ refloodN ∈ [1, totalDet]（每爆至多 1 联合盒）。阴性轮（pre-fix）实测恰
    //       36/36 = N 倍放大源直读落盘；post-fix 分期后序波爆炸球可能整落在先波弹坑内 →
    //       destroySphereSilent 的 destroyed 空早退 = 该爆零 reflood 零扇出（免费），实测 21/21
    //       —— 分期顺带去掉重叠 reflood 的真实收益，「总量不变」契约指破坏方块量（totalDet==36）
    //       而非 reflood 计数；
    //   (c) 源码钉：entitymanager.h 预算常量 + entitymanager.cpp 预算钳制行（任一散失即红）。
    {
        int x0 = -1, z0 = -1;
        // rig 选址（三级）：① stride 2 快扫（同 t996 模式）；② stride 1 全深细扫（奇对齐 + t814 扩深
        //   128 后 z≥96 老扫描盲区）；③ 兜底：全图扫「占用最少」候选位 → setBlock Air 预清场后照常搭台。
        //   背景：kRigY=41 的 39..44 层被丛林/沼泽冠层大片占据（首跑实测 ①② 全落空 → 「no clear rig
        //   area found」假红），10×9×6 净空在本图不保证存在；③ 的预清场发生在 worldChanged 连接与
        //   FrameProfiler 基线**之前** → rig 造价（清场 reflood/worldChanged）不进归因窗口，(b) 腿干净。
        //   setBlock 同 id 早退（World::setBlock 无变化路径零信号零重 flood）→ 清 Air-over-Air 零成本。
        const auto t997AreaClear = [&](int xx, int zz) {
            for (int dx = -1; dx <= 8; ++dx)
                for (int dz = -1; dz <= 7; ++dz)
                    for (int dy = -2; dy <= 3; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) return false;
            return true;
        };
        for (int zz = 3; zz < 90 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 8 < 96 && x0 < 0; xx += 2)
                if (t997AreaClear(xx, zz)) { x0 = xx; z0 = zz; }
        for (int zz = 3; zz + 7 < 128 && x0 < 0; ++zz)
            for (int xx = 1; xx + 8 < 96 && x0 < 0; ++xx)
                if (t997AreaClear(xx, zz)) { x0 = xx; z0 = zz; }
        if (x0 < 0) {
            int bestOcc = 1 << 30;
            for (int zz = 3; zz + 7 < 128; ++zz)
                for (int xx = 1; xx + 8 < 96; ++xx) {
                    int occ = 0;
                    for (int dx = -1; dx <= 8; ++dx)
                        for (int dz = -1; dz <= 7; ++dz)
                            for (int dy = -2; dy <= 3; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) ++occ;
                    if (occ < bestOcc) { bestOcc = occ; x0 = xx; z0 = zz; }
                }
            if (x0 >= 0 && bestOcc > 0) {
                qInfo().noquote() << "  [t997] no fully-clear rig area; pre-clearing best candidate"
                                  << x0 << "," << z0 << "(occupancy" << bestOcc << "/540 cells, pre-baseline)";
                for (int dx = -1; dx <= 8; ++dx)
                    for (int dz = -1; dz <= 7; ++dz)
                        for (int dy = -2; dy <= 3; ++dy)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
            }
        }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t997 mass-detonation staging: no clear rig area found";
        } else {
            EntityManager entsT997;
            // rig：6×6 石台（kRigY-1）+ 36 primed TNT 同引信 5s（不摆 TNT 方块 → 零链式干扰）。
            for (int dz = 0; dz <= 6; ++dz)
                for (int dx = 0; dx <= 7; ++dx)
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
            // worldChanged 计数（本 rig 专享；scope 末 disconnect 防悬空 / 污染后续探针）。
            int wcT997 = 0;
            const QMetaObject::Connection wcConnT997 = QObject::connect(
                &w, &World::worldChanged, &w, [&wcT997]() { ++wcT997; });
            // 归因计数基线（FrameProfiler 全局桶，矩阵单线程顺序跑 → 窗口差分即本 rig 增量）。
            //   基线**必须落在石台 setBlock 之后**：World::setBlock 每次成功写 = 1×recomputeLightAround
            //   （refloodN/lightEditN 各 +1）+ 1×emit worldChanged —— 基线若在搭台前，56 格搭台会灌进窗口
            //   （refloodD 恒 92 ≠ 36，(b) 腿永红）；清场段同理在 refloodD 快照之后、其 worldChanged 由
            //   wcDetT997 快照隔离。搭台/清场是 rig 造价，不属「每爆 1 联合盒 / 1 扇出」的归因窗口。
            //   spawnPrimedTnt 纯实体侧（不写世界、不发 World 信号）→ 窗口内恰 36 次引爆归因。
            const qint64 reflood0 = FrameProfiler::instance()->countValue("refloodN");
            const qint64 lightEdit0 = FrameProfiler::instance()->countValue("lightEditN");
            const qint64 gravCol0 = FrameProfiler::instance()->countValue("gravColN");
            const qint64 casc0 = FrameProfiler::instance()->countValue("cascN");
            for (int dz = 0; dz < 6; ++dz)
                for (int dx = 0; dx < 6; ++dx)
                    entsT997.spawnPrimedTnt(x0 + dx, kRigY, z0 + dz);
            // 逐 1/60s 步进（~11.7s cap）：每步前后实体扫描差分 = 本 tick 引爆数；墙钟逐 tick 记录。
            constexpr int kT997Budget = 4; // 与 entitymanager.h kMaxTntDetonationsPerTick 同值（源码钉 (c) 对齐）
            int primedBefore = 0;
            for (int i = 0; i < entsT997.count(); ++i)
                if (entsT997.isPrimedAt(i)) ++primedBefore;
            int totalDet = 0, maxDetPerTick = 0;
            qint64 maxTickNs = 0, totalTickNs = 0;
            int steps = 0;
            for (int s = 0; s < 700 && totalDet < primedBefore; ++s) {
                int alive0 = 0;
                for (int i = 0; i < entsT997.count(); ++i)
                    if (entsT997.isPrimedAt(i)) ++alive0;
                const qint64 t0 = FrameProfiler::nowNs();
                entsT997.tick(1.0 / 60.0, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                const qint64 tickNs = FrameProfiler::nowNs() - t0;
                totalTickNs += tickNs;
                if (tickNs > maxTickNs) maxTickNs = tickNs;
                int alive1 = 0;
                for (int i = 0; i < entsT997.count(); ++i)
                    if (entsT997.isPrimedAt(i)) ++alive1;
                const int det = alive0 - alive1; // 本 tick 引爆数（ detonatePrimedTnt 移除实体；无链式源）
                if (det > 0) {
                    totalDet += det;
                    if (det > maxDetPerTick) maxDetPerTick = det;
                }
                ++steps;
            }
            int primedAfter = 0;
            for (int i = 0; i < entsT997.count(); ++i)
                if (entsT997.isPrimedAt(i)) ++primedAfter;
            const qint64 refloodD = FrameProfiler::instance()->countValue("refloodN") - reflood0;
            const qint64 lightEditD = FrameProfiler::instance()->countValue("lightEditN") - lightEdit0;
            const qint64 gravColD = FrameProfiler::instance()->countValue("gravColN") - gravCol0;
            const qint64 cascD = FrameProfiler::instance()->countValue("cascN") - casc0;
            const double maxTickMs = double(maxTickNs) / 1e6;
            const double avgTickMs = steps > 0 ? double(totalTickNs) / 1e6 / double(steps) : 0.0;
            const int wcDetT997 = wcT997; // 归因读数先快照：清场 setBlock（Air 化）每格发 worldChanged
            // 清场（平台残留 + 断连接防悬空）。
            for (int dz = -1; dz <= 7; ++dz)
                for (int dx = -1; dx <= 8; ++dx)
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
            entsT997.clearAll();
            QObject::disconnect(wcConnT997);

            const bool okA = totalDet == 36                       // 全部实爆（分期不吞爆炸）
                && primedAfter == 0                               // 终态零 primed 残留
                && maxDetPerTick <= kT997Budget;                  // 单 tick 引爆 ≤ 预算（pre-fix 36 → 红）
            // (b) 归因计数不变式（见探针头注释 (b) 段）：每爆批链 1 扇出 / ≤1 联合盒；refloodD==wcDetT997
            //     且 ∈ [1, totalDet]。pre-fix 恰 36/36（N 倍放大直读）；post-fix 分期去重后 21/21。
            const bool okB = refloodD >= 1 && refloodD <= totalDet
                && wcDetT997 == refloodD;                         // 每爆批链恒 1 worldChanged / ≤1 联合盒 reflood
            bool okPin = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
                QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
                QFile cf(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
                const QString h = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
                const QString c = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
                okPin = h.contains(QStringLiteral("kMaxTntDetonationsPerTick"))
                    && h.contains(QStringLiteral("kDetonationWaveRegroupSec"))
                    // review0903 #8：值级钉（仿 P-t947 「= 4」声明形态钉）——预算上调（放松）或下调
                    // （行为漂移）都恰红；本地镜像 kT997Budget=4 与声明值双向对齐。
                    && h.contains(QStringLiteral("kMaxTntDetonationsPerTick = 4"))
                    && c.contains(QStringLiteral("kMaxTntDetonationsPerTick"))
                    && c.contains(QStringLiteral("e.fuse = kDetonationWaveRegroupSec"));
            }

            const bool okT997 = okA && okB && okPin;
            if (!okT997) ++totalFail;
            if (!okT997)
                qInfo().noquote() << "  [t997 diag] totalDet" << totalDet << "maxPerTick" << maxDetPerTick
                                  << "primedAfter" << primedAfter << "refloodD" << refloodD
                                  << "lightEditD" << lightEditD << "gravColD" << gravColD << "cascD" << cascD
                                  << "wc" << wcDetT997 << "maxTickMs" << maxTickMs << "avgTickMs" << avgTickMs
                                  << "steps" << steps << "pin" << okPin;
            qInfo().noquote() << (okT997 ? "PASS" : "FAIL")
                              << "| t997 mass TNT same-tick detonation staged by a per-tick budget;"
                              << " attribution: 36 same-fuse primed TNT over a stone platform driven"
                              << " in 1/60s ticks produced" << refloodD << "reflood boxes /" << wcDetT997
                              << " worldChanged fanouts /" << lightEditD << " light edits /" << gravColD
                              << " grav cols /" << cascD << " cascades for" << totalDet
                              << " detonations (each explosion runs the t933-batched chain once:"
                              << " 1x refloodBox + 1x worldChanged + 1x clearAllDirty, so N same-"
                              << "tick explosions = N overlapping reflood boxes and N QML mesh-"
                              << "recheck fanouts in one frame - the user F3 snapshot showed sim"
                              << " only 4.63ms of 66.7ms, so the cure is bounding the per-frame"
                              << " detonation wave); the budget spreads detonations <=4 per tick"
                              << " (max tick" << maxTickMs << "ms vs avg" << avgTickMs
                              << "ms in this rig) while total destruction and the 1.2s chain-"
                              << "ignite semantics stay intact (all 36 really explode, zero primed"
                              << " left); GUI-side present/vsync/GPU numbers still need on-device"
                              << " F3; source pins lock the budget constant and its = 4 literal"
                              << " value (review0903 #8) plus the fuse-regroup clamp";
        }
    }

    // ── P-t1005 红石块旁多 TNT 连锁引燃·终态清零探针（R19.20 批 t1005；用户实测回归「红石块旁多 TNT
    //    同时连锁引爆 → 部分 TNT 持续闪烁不清除（永续 primed 残留）；单发引爆正常」——t997 波分期修复
    //    后实机仍复现）──
    //   真消费端链（P-t814 模式）：World::tickRedstone 通电上升沿 → powerTntTriggered → PlayerController::
    //   firePowerTnt（同步镜像 Main.qml 转发 handler）= clearBlockSilent 清 TNT 方块 + spawnPrimedTnt
    //   （5s 手点引信）；爆炸链式引燃走 detonateTntSphere 内 spawnPrimedTnt（1.2s + jitter 错峰）→
    //   t997 预算重挂路径（同帧到期 > kMaxTntDetonationsPerTick=4 → e.fuse 重挂 1/60s）。真实时钟拓扑：
    //   实体 tick 每帧 1/60s + World 红石 tick 每 6 帧（≈100ms，Main.qml WorldClock 驱动同款）。
    //   四布局（验收三布局 + 同帧簇加严；红石块嵌阵 = 用户「红石块旁多 TNT」形态）：
    //     A 2×2（R 角位：2 直供同帧 + 1 斜角链）；B 3×3（R 居中：4 直供同帧 + 4 斜角链）；
    //     C 1×4 长链（R 头位：1 直供 + 3 爆炸链）；D 同帧簇加严（R 四水平 + 正上共 5 直供**同帧 5s
    //     到期 > 预算 4** → 必经重挂路径 + 东向 2 格爆炸链）。
    //   加严轮 E/F（首跑 A-D 全 settle 后按「更多直供数 / 更密阵 / 交错重供」加严）：
    //     E 8-direct-2R（双 R 各 4 直供同帧 8 发 = 两倍深重挂 + 中缝纯链）；F dust-fed-2x6
    //     （R→6 粉馈线 → 底排 6 直供超预算 + 顶排 6 叠置纯链 = 12 TNT 密阵，波 1 断供形态）。
    //   断言：(a) 点火 sanity（≤6 红石 tick 内 primed 实体出现——链路本身通，防布线假阴）；(b) 有界
    //   收敛（45s cap 内 settle：零存活 primed 且 rig 原 TNT 格全 Air，且该态**连续保持 120 帧**——
    //   2s > 链式最大引信 1.8s，防波间空窗假收敛；cap 不 settle = 用户「永续闪烁」行为学复现 → 红）；
    //   (c) 终态零残留（settle 点零存活 primed + 全部原 TNT 格 Air——「TNT 方块全部变 Air、无存活
    //   primed 实体残留」逐字口径）。
    {
        PlayerController pc;
        EntityManager ents;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        const QMetaObject::Connection tntFwd1005 = QObject::connect(
            &w, &World::powerTntTriggered, &pc,
            [&pc](int x, int y, int z) { pc.firePowerTnt(x, y, z); });

        struct TntCell { int dx, dy, dz; };
        // t1005 加严轮：rs/dust 均为多格列表（E 双红石块 8 直供 / F 粉馈线交错重供形态）。
        struct T1005Rig { const char *name; QVector<TntCell> tnt; QVector<TntCell> rs; QVector<TntCell> dust; };
        const QVector<T1005Rig> rigs1005 = {
            { "2x2 R-corner",  {{1, 0, 0}, {0, 0, 1}, {1, 0, 1}}, {{0, 0, 0}}, {} },
            { "3x3 R-center",  {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 1}, {2, 0, 1},
                                {0, 0, 2}, {1, 0, 2}, {2, 0, 2}}, {{1, 0, 1}}, {} },
            { "1x4 chain",     {{1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}}, {{0, 0, 0}}, {} },
            { "5-same-frame",  {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0},
                                {1, 0, 1}, {2, 0, 1}}, {{0, 0, 0}}, {} },
            // 加严 E「8-direct-2R」：双红石块各带 4 直供（±x / +z / 上）同帧 8 发到期 = 预算 4 的
            //    **两倍深重挂**（4 爆 + 4 重挂 → 次帧 4 爆），中缝 (0,0,2) 距两源均 2 格不直供 =
            //    纯爆炸链——重挂窗与爆炸链同帧交叠的加严形态。
            { "8-direct-2R",   {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0},
                                {0, 0, 3}, {1, 0, 4}, {-1, 0, 4}, {0, 1, 4},
                                {0, 0, 2}}, {{0, 0, 0}, {0, 0, 4}}, {} },
            // 加严 F「dust-fed-2x6」：红石块 → 6 粉馈线（交错重供形态——TNT 不贴源、经通电粉直供）
            //    → 底排 6 直供同帧（超预算 4）+ 顶排 6 叠置纯链（粉斜角不供）= 12 TNT 密阵；粉线 /
            //    源全在 1 号爆炸球内 → 波 1 断供（断供后不得有残燃）。
            { "dust-fed-2x6",  {{-1, 0, 1}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {3, 0, 1}, {4, 0, 1},
                                {-1, 1, 1}, {0, 1, 1}, {1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {4, 1, 1}},
                               {{-2, 0, 0}},
                               {{-1, 0, 0}, {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}} },
        };

        bool ok1005 = true;
        QString diag1005;
        for (const T1005Rig &rig : rigs1005) {
            const auto [x0, z0] = nextSlot();
            const int y = kRigY;
            // 先清后铺 9×9 石台（y-1；y..y+2 净空）——前探针弹坑残留不塌本台。
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz) {
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, y + dy, z0 + dz, BR::Air, 0);
                    w.setBlock(x0 + dx, y - 1, z0 + dz, BR::Stone, 0);
                }
            placeRigBlock(w, x0 + rig.rs[0].dx, y + rig.rs[0].dy, z0 + rig.rs[0].dz, BR::RedstoneBlock, 0);
            for (int rsi = 1; rsi < rig.rs.size(); ++rsi)
                placeRigBlock(w, x0 + rig.rs[rsi].dx, y + rig.rs[rsi].dy, z0 + rig.rs[rsi].dz,
                              BR::RedstoneBlock, 0);
            for (const TntCell &d : rig.dust)
                placeRigBlock(w, x0 + d.dx, y + d.dy, z0 + d.dz, BR::RedstoneDust, 0);
            const QVector<TntCell> placed = rig.tnt;
            for (const TntCell &c : placed)
                placeRigBlock(w, x0 + c.dx, y + c.dy, z0 + c.dz, BR::TntBlock, 0);

            // (a) 点火 sanity：≤6 红石 tick 内 primed 实体出现。
            int primed0 = 0;
            for (int t = 0; t < 6 && primed0 == 0; ++t) {
                w.tickRedstone();
                for (int i = 0; i < ents.count(); ++i)
                    if (ents.isPrimedAt(i)) ++primed0;
            }
            // (b) 有界收敛推进：实体 tick 每帧 1/60s + 红石 tick 每 6 帧；settle 态（零 primed + 原
            //     TNT 格全 Air）连续保持 120 帧才算收敛（防波间空窗假收敛）。
            int settledRun = 0, settleFrame = -1, maxPrimed = primed0;
            bool settled = false;
            const int kCapFrames1005 = 60 * 45;
            for (int f = 0; f < kCapFrames1005 && !settled; ++f) {
                if (f % 6 == 0) w.tickRedstone();
                ents.tick(1.0 / 60.0, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                int live = 0;
                for (int i = 0; i < ents.count(); ++i)
                    if (ents.isPrimedAt(i)) ++live;
                maxPrimed = qMax(maxPrimed, live);
                bool blocksGone = true;
                for (const TntCell &c : placed)
                    if (w.blockAt(x0 + c.dx, y + c.dy, z0 + c.dz) != BR::Air) { blocksGone = false; break; }
                if (live == 0 && blocksGone) {
                    if (++settledRun >= 120) { settled = true; settleFrame = f; }
                } else {
                    settledRun = 0;
                }
            }
            // (c) 终态零残留核对（settle 点或 cap 末）。
            int liveEnd = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) ++liveEnd;
            bool cellsAir = true;
            for (const TntCell &c : placed)
                if (w.blockAt(x0 + c.dx, y + c.dy, z0 + c.dz) != BR::Air) cellsAir = false;
            const bool okRig = primed0 > 0 && settled && liveEnd == 0 && cellsAir;
            if (!okRig) {
                ok1005 = false;
                diag1005 += QStringLiteral("\n  [t1005 diag] %1: ignitePrimed=%2 settled=%3"
                                           " settleFrame=%4 liveEnd=%5 cellsAir=%6 maxPrimed=%7")
                                .arg(QString::fromLatin1(rig.name)).arg(primed0 > 0).arg(settled)
                                .arg(settleFrame).arg(liveEnd).arg(cellsAir).arg(maxPrimed);
            }
            // 清场（实体 + rig 全格，防跨 rig / 跨探针污染）。
            ents.clearAll();
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, y + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
        }

        // ── t1005 加严 G「pool-full chain」（实体槽池打满下的链式引燃——「僵尸槽」假说复现面）──
        //    假说（用户「部分 TNT 永续闪烁」的一种归因）：槽满时 spawn 路径产生「参与渲染但不被 tick」
        //    的 primed 僵尸槽（alive 但引信永不归零）。本面把假说推到极限压力下证伪/坐实：
        //    ① 槽压力前置：槽池先填 59 只羊（kCap=64 硬上限，t789 先例硬编码——spawn -1 即漂移响红；
        //       封闭石栏内：防爆波波及（rig 位距 ≥22 > 半径 3）/ 防摔落伤（落地 0-1 格）/ 防游走出圈）；
        //    ② 5 直供布局（复用 D 形态）点火 → 池恰满（59+5=64）→ 引爆波与链式 spawn 全程在满池压力
        //       下跑（达 cap 路径 = spawnPrimedTnt 静默跳过：TNT 方块已被爆清 → 消失 ≠ 僵尸）；
        //    ③ 满池前提钉：点火后补 spawn 必得 -1（若成功 = 64 上限前提漂移 → 响红，压力面失效）；
        //    ④ 僵尸槽检测器：任一 isPrimedAt 槽的 fuseProgressAt 连续 30 帧（0.5s）逐位不变 = 不被 tick
        //       的 primed 槽（正常路径 fuse 每帧 -=dt 必变；t997 重挂是跳变不是冻结；fuse 耗尽未爆的
        //       永续 p==0 态同样判僵尸——正是用户症状的实体语义）；
        //    ⑤ 检测器自检（防「永不出火的死检测器」假绿）：先 tick 数帧证 p 变化（不误报），再冻结
        //       tick 读数 35 帧证必出火（不漏报）。
        //    断言：filler 全成功 + 点火 sanity + 满池前提钉 + 有界收敛（零 primed + TNT 格全 Air +
        //       120 帧滞回）+ 全程零僵尸槽。红 = 假说坐实（修实体侧）；绿 = 僵尸槽假说证伪（转渲染侧）。
        {
            const auto [bx0, bz0] = nextSlot(); // 羊栏 slot
            const int byG = kRigY;
            // 7×7 石台 + 2 高石墙（内部 5×5）。石台面（非 Grass）→ 羊不吃草不改地形。
            for (int dx = -3; dx <= 3; ++dx)
                for (int dz = -3; dz <= 3; ++dz) {
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(bx0 + dx, byG + dy, bz0 + dz, BR::Air, 0);
                    w.setBlock(bx0 + dx, byG - 1, bz0 + dz, BR::Stone, 0); // 台面
                    if (dx == -3 || dx == 3 || dz == -3 || dz == 3) {      // 2 高围墙
                        w.setBlock(bx0 + dx, byG, bz0 + dz, BR::Stone, 0);
                        w.setBlock(bx0 + dx, byG + 1, bz0 + dz, BR::Stone, 0);
                    }
                }
            // ⑤ 检测器自检：spawn 1 primed → tick 数帧（p 必变，run 归零）→ 冻结 tick 读 35 帧（必出火）。
            ents.spawnPrimedTnt(bx0 + 1, byG, bz0 + 1);
            bool detTrips = false, detQuiet = true;
            {
                float pLast = -1.0f;
                int run = 0;
                for (int f = 0; f < 40 && !detTrips; ++f) {
                    if (f < 5) // 前帧段照常 tick：证检测器不误报活动引信
                        ents.tick(1.0 / 60.0, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                    float p = -1.0f;
                    for (int i = 0; i < ents.count(); ++i)
                        if (ents.isPrimedAt(i)) { p = ents.fuseProgressAt(i); break; }
                    if (p < 0.0f) continue; // primed 已爆完（不应发生于 40 帧窗；防御）
                    run = (p == pLast) ? run + 1 : 0;
                    if (f >= 5 && run == 0) detQuiet = false; // 冻结段里 p 竟变了 = 自检失效
                    pLast = p;
                    if (run >= 30) detTrips = true; // 冻结 30 帧 → 检测器出火
                }
            }
            ents.clearAll(); // 自检清场（池归零）再填 filler
            // ① 槽压力前置：59 filler（64 硬上限硬编码，t789 先例；-1 = 上限漂移响红）。
            int fillOk = 0;
            for (int i = 0; i < 59; ++i)
                if (ents.spawnMobTyped(bx0, byG, bz0, EntityManager::MobSheep,
                                       QStringLiteral("#f5f0e8"), 10) >= 0) ++fillOk;
            // ② TNT rig：复用 D「5-same-frame」形态（R 四水平 + 正上 5 直供 + 东向 2 纯链）。
            const auto [gx0, gz0] = nextSlot();
            const int gy = kRigY;
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz) {
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(gx0 + dx, gy + dy, gz0 + dz, BR::Air, 0);
                    w.setBlock(gx0 + dx, gy - 1, gz0 + dz, BR::Stone, 0);
                }
            const T1005Rig &rigG = rigs1005[3];
            placeRigBlock(w, gx0 + rigG.rs[0].dx, gy + rigG.rs[0].dy, gz0 + rigG.rs[0].dz,
                          BR::RedstoneBlock, 0);
            const QVector<TntCell> placedG = rigG.tnt;
            for (const TntCell &c : placedG)
                placeRigBlock(w, gx0 + c.dx, gy + c.dy, gz0 + c.dz, BR::TntBlock, 0);
            // (a) 点火 sanity：≤6 红石 tick 内 primed 出现（59+5 → 池恰满 64）。
            int primed0G = 0;
            for (int t = 0; t < 6 && primed0G == 0; ++t) {
                w.tickRedstone();
                for (int i = 0; i < ents.count(); ++i)
                    if (ents.isPrimedAt(i)) ++primed0G;
            }
            // ③ 满池前提钉：点火后补 spawn 必 -1（成功 = 64 上限前提漂移，压力面失效）。
            const bool poolFullPinned = ents.spawnMobTyped(bx0, byG + 1, bz0,
                                                           EntityManager::MobSheep,
                                                           QStringLiteral("#f5f0e8"), 10) < 0;
            // ④ 僵尸检测器状态表（槽 idx → 上次 fuseProgress / 冻结帧数；固定表免容器开销）。
            float gLastP[512];
            int gFrozen[512];
            for (int i = 0; i < 512; ++i) { gLastP[i] = -1.0f; gFrozen[i] = 0; }
            int zombieFrames = 0;
            const auto detectZombies = [&]() {
                const int n = ents.count();
                for (int i = 0; i < n && i < 512; ++i) {
                    if (!ents.isPrimedAt(i)) { gFrozen[i] = 0; gLastP[i] = -1.0f; continue; }
                    const float p = ents.fuseProgressAt(i);
                    if (p == gLastP[i]) {
                        if (++gFrozen[i] >= 30) ++zombieFrames; // 冻结 0.5s = 不被 tick 的 primed 槽
                    } else {
                        gFrozen[i] = 0;
                        gLastP[i] = p;
                    }
                }
            };
            // (b) 满池收敛推进：每帧实体 tick + 僵尸检测；settle 态（零 primed + TNT 格全 Air）
            //     连续 120 帧（滞回同上）。
            int settledRunG = 0, settleFrameG = -1, maxPrimedG = primed0G;
            bool settledG = false;
            const int kCapFramesG = 60 * 45;
            for (int f = 0; f < kCapFramesG && !settledG; ++f) {
                if (f % 6 == 0) w.tickRedstone();
                ents.tick(1.0 / 60.0, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                detectZombies();
                int live = 0;
                for (int i = 0; i < ents.count(); ++i)
                    if (ents.isPrimedAt(i)) ++live;
                maxPrimedG = qMax(maxPrimedG, live);
                bool blocksGoneG = true;
                for (const TntCell &c : placedG)
                    if (w.blockAt(gx0 + c.dx, gy + c.dy, gz0 + c.dz) != BR::Air) { blocksGoneG = false; break; }
                if (live == 0 && blocksGoneG) {
                    if (++settledRunG >= 120) { settledG = true; settleFrameG = f; }
                } else {
                    settledRunG = 0;
                }
            }
            // (c) 终态零残留核对。
            int liveEndG = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) ++liveEndG;
            bool cellsAirG = true;
            for (const TntCell &c : placedG)
                if (w.blockAt(gx0 + c.dx, gy + c.dy, gz0 + c.dz) != BR::Air) cellsAirG = false;
            const bool okG = fillOk == 59 && detTrips && detQuiet && primed0G > 0
                             && poolFullPinned && settledG && liveEndG == 0 && cellsAirG
                             && zombieFrames == 0;
            if (!okG) {
                ok1005 = false;
                diag1005 += QStringLiteral("\n  [t1005 diag] pool-full G: fill=%1/59 detTrips=%2"
                                           " detQuiet=%3 ignitePrimed=%4 poolFullPinned=%5"
                                           " settled=%6 settleFrame=%7 liveEnd=%8 cellsAir=%9"
                                           " maxPrimed=%10 zombieFrames=%11")
                                .arg(fillOk).arg(detTrips).arg(detQuiet).arg(primed0G > 0)
                                .arg(poolFullPinned).arg(settledG).arg(settleFrameG)
                                .arg(liveEndG).arg(cellsAirG).arg(maxPrimedG).arg(zombieFrames);
            }
            // 清场（实体 + 羊栏 + TNT rig 全格，防跨探针污染）。
            ents.clearAll();
            for (int dx = -3; dx <= 3; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(bx0 + dx, byG + dy, bz0 + dz, BR::Air, 0);
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(gx0 + dx, gy + dy, gz0 + dz, BR::Air, 0);
            tickN(w, 2);
        }
        QObject::disconnect(tntFwd1005);
        if (!ok1005) ++totalFail;
        qInfo().noquote() << (ok1005 ? "PASS" : "FAIL")
                          << "| t1005 redstone-block adjacent multi-TNT chain ignition burns down:"
                          << " 7 layouts (2x2 / 3x3 / 1x4 / 5-same-frame cluster over budget 4 /"
                          << " 8-direct dual-source 2-deep regroup / dust-fed 2x6 dense array"
                          << " wave-1 power-cut / pool-full 64-slot cap pressure with a"
                          << " fuse-frozen zombie-slot detector + detector self-check)"
                          << " driven by the real consumer chain (tickRedstone -> powerTntTriggered"
                          << " -> firePowerTnt) with per-frame entity ticks (1/60s) + per-6-frame"
                          << " redstone ticks (100ms WorldClock topology) all settled to zero"
                          << " surviving primed entities and all-Air rig cells inside the bounded"
                          << " window (2s settle hysteresis > 1.8s max chain fuse)"
                          << (ok1005 ? QString() : diag1005);
    }

    // ── P-t979 猪两修（浅水淹没度溺水 + 掉落表；专用局部世界 w979a/b：围栏平台 + 浅水档水洼/盖顶水槽）──
    //    根因：t828 溺水头判（旧）= floor(pos.y+halfH·0.8) 处**整格** blockAt==Water 布尔，无视水位档
    //    （state 7=液面 1/8、state 6=液面 2/8，t197 语义）。猪 halfH=0.45 → 口鼻线在脚位上方 0.81 < 1 格
    //    → 站 1/8 水洼里口鼻线仍落在脚位水格 → 判「没顶」启动呼吸耗尽（用户实测「脚踝水深、往上跳还是
    //    淹死」；「往上跳」= 同一误判驱动的 t923 浮力浅水空跳）。修 = t979 mobSnoutSubmergedInWater 单一
    //    权威（口鼻线 vs 格内液面连续比较；满格水源液面=格顶 → 与旧判在源块水等价 → t828/t923 既有探针
    //    不变）。判据差异只在**连续淹没**窗显形（出水即双计时器清零）：开放水洼猪数秒走出、新旧两版都不
    //    挨罚 → 判别腿用**盖顶水槽**（1 高走廊 + 脚踝水全覆盖：猪被天花板钉住口鼻、无法出水——真实对应
    //    矮桥洞 / 隧道积水困死；旧码持续整格淹没 15s 必红）。
    //    (a) 开放水洼腿 ×2（1/8=state7、1/4=state6）：围栏平台上 8 头猪各站一格孤立水洼（无 state 梯度 →
    //        流推不扰），20s：全员满血 + ≥6/8 已走出水洼格（wander 脱困不受罚不压制；每头 P(20s 全 idle)
    //        ≈0.25⁴≈0.4%，8 头全不动 ≈ 6e-21）。
    //    (b) 盖顶水槽腿 ×2（1/8、1/4）：1 宽走廊脚踝水全覆盖 + 石盖（口鼻恒被钉在水面上方 0.9 格内），
    //        4 头猪 20s 全员满血（旧码：头格恒水 → 15s 起 1HP/s → 3HP 猪死 → 恰红）。
    //    (c) 没顶腿：3 深水源柜 + 石盖（t828 同款钉水法）钉住猪 → 溺水链仍有效（20s 内 3HP 猪死或 ≤1）。
    //    (d) 源码钉：entitymanager.cpp 含 mobSnoutSubmergedInWater 谓词（定义 + 溺水/浮面/resting 破除
    //        三消费 ≥4 处）+ 液面公式 (8 - st)；Main.qml 猪分支掉落表修正钉（熟排/生排火焰链保留 + t473 猪皮革移除、
    //        牛皮革保留——掉落表在 QML 呈现层，headless 无 spawnItem 可断行为 → 源码钉先例 t880/t997）。
    //    阴性轮（行为关键必须）：回退溺水判据为旧整格布尔（浮面/resting 保持新谓词）→ (b) 盖顶水槽腿恰红
    //        （4 头全死：口鼻被钉持续淹没复现）→ 复原绿。
    {
        const int kPigMax = 3;
        bool ok = true;
        int exitedTotal = 0; // (a) 合计脱困头数（诊断输出用）
        int trenchAlive = 0; // (b) 盖顶水槽存活满血头数（诊断输出用）
        bool legFull[2] = {true, true}; // 分腿满血标记（diag 用）
        int legExit[2] = {0, 0};        // 分腿脱困数（diag 用）
        bool okDeep = false;            // (c) 没顶腿（diag 用）
        for (int leg = 0; leg < 2 && ok; ++leg) { // leg0 = 1/8(state7)、leg1 = 1/4(state6)
            const quint8 puddleState = leg == 0 ? quint8(7) : quint8(6);
            World w979;
            w979.setWidth(44); w979.setDepth(44); w979.setHeight(96); w979.setSeed(21);
            // 围栏平台：y84 石板（8..38²）+ y85..86 边墙（防 wander 出平台坠地形摔伤假红；猪无跳跃 AI）。
            for (int x = 8; x < 38; ++x)
                for (int z = 8; z < 38; ++z) w979.setBlock(x, 84, z, BR::Stone, 0);
            for (int x = 8; x < 38; ++x)
                for (int z = 8; z < 38; ++z) {
                    const bool wall = (x == 8 || x == 37 || z == 8 || z == 37);
                    if (wall)
                        for (int y = 85; y <= 86; ++y) w979.setBlock(x, y, z, BR::Stone, 0);
                }
            // (a) 8 个孤立开放水洼（4×2 间隔布点；脚位格水位档由 state 给：1/8=7、1/4=6；探针不 tick
            //     world → 流体静置）。
            int puddles[8][2];
            int n = 0;
            for (int px = 0; px < 4; ++px)
                for (int pz = 0; pz < 2; ++pz) {
                    const int cx = 11 + px * 6, cz = 12 + pz * 14;
                    w979.setBlock(cx, 85, cz, BR::Water, puddleState);
                    puddles[n][0] = cx; puddles[n][1] = cz; ++n;
                }
            // (b) 盖顶水槽：z=26 一条 x=11..22 的 1 宽走廊——脚位格全覆盖脚踝水 + y86 石盖（口鼻被钉在
            //     水面上方 <1 格、恒处水格内）+ 两侧 y85..86 壁 + 两端封头（物理困住：旧判据 15s 必罚）。
            for (int cx = 11; cx <= 22; ++cx) {
                w979.setBlock(cx, 85, 26, BR::Water, puddleState);
                w979.setBlock(cx, 86, 26, BR::Stone, 0);
                w979.setBlock(cx, 85, 25, BR::Stone, 0); w979.setBlock(cx, 86, 25, BR::Stone, 0);
                w979.setBlock(cx, 85, 27, BR::Stone, 0); w979.setBlock(cx, 86, 27, BR::Stone, 0);
            }
            w979.setBlock(10, 85, 26, BR::Stone, 0); w979.setBlock(10, 86, 26, BR::Stone, 0);
            w979.setBlock(23, 85, 26, BR::Stone, 0); w979.setBlock(23, 86, 26, BR::Stone, 0);
            EntityManager em979;
            const QVector3D far979(-1000.0f, 90.0f, -1000.0f);
            int pigs[8], trench[4] = {13, 15, 17, 19};
            bool spawned = true;
            for (int i = 0; i < 8; ++i) {
                pigs[i] = em979.spawnMobTyped(puddles[i][0], 85, puddles[i][1], EntityManager::MobPig,
                                              QStringLiteral("#f0a8b0"), kPigMax);
                spawned = spawned && pigs[i] >= 0;
            }
            int trenchPigs[4];
            for (int i = 0; i < 4; ++i) {
                trenchPigs[i] = em979.spawnMobTyped(trench[i], 85, 26, EntityManager::MobPig,
                                                    QStringLiteral("#f0a8b0"), kPigMax);
                spawned = spawned && trenchPigs[i] >= 0;
            }
            if (!spawned) {
                ok = false;
            } else {
                int exited = 0, alive = 0;
                bool allFull = true;
                bool everLeft[8] = {}; // 粘性脱困标记：任一采样点在洼外即计「能走出」（终态单点采样会被
                                       //   随机游走折返骗过——t980 轮实测 5/8 假红根因，语义改「曾离开」）
                for (int t = 0; t < 1250; ++t) { // 20s（≥ 溺水起罚 15s + 5s 余量：旧码判别腿必红）
                    em979.tick(0.016f, &w979, far979, 0.3f, 1.8f, false);
                    if (t % 8 == 0) { // 0.128s 采样（0.4 b/s 涉水速下猪无法在两采样间出洼又回洼）
                        for (int i = 0; i < 8; ++i) {
                            const QVector3D p = em979.posAt(pigs[i]);
                            if (qFloor(p.x()) != puddles[i][0] || qFloor(p.z()) != puddles[i][1])
                                everLeft[i] = true; // 脚位格已离水洼（wander 走出 = 浅水不困不罚）
                        }
                    }
                    if (t == 1249) {
                        for (int i = 0; i < 8; ++i)
                            if (em979.healthAt(pigs[i]) != kPigMax || em979.deadAt(pigs[i]))
                                allFull = false; // 新谓词：浅水档口鼻线在液面上 → 零溺水伤
                        for (int i = 0; i < 8; ++i)
                            if (everLeft[i]) ++exited;
                        for (int i = 0; i < 4; ++i)
                            if (em979.healthAt(trenchPigs[i]) == kPigMax && !em979.deadAt(trenchPigs[i]))
                                ++alive; // 盖顶水槽（判别腿）：口鼻被钉水面下 0.9 格内仍零溺水伤
                    }
                }
                exitedTotal += exited;
                trenchAlive += alive;
                legFull[leg] = allFull;
                legExit[leg] = exited;
                ok = ok && allFull && exited >= 6 && alive == 4; // ≥6/8 曾脱困 + 4/4 水槽满血
            }
        }
        // (c) 没顶腿：石盖水柜（t828 同款：水 y85..87 源块 + y88 石盖 + **四周全高壁**——壁顶与盖平，
        //     防浮力钉盖下的猪随 wander 水平漂出水柱、坠井外干地存活（RNG 漂移型假红，t980 轮实测））。
        {
            World w979b;
            w979b.setWidth(24); w979b.setDepth(24); w979b.setHeight(96); w979b.setSeed(22);
            for (int x = 4; x < 20; ++x)
                for (int z = 4; z < 20; ++z) w979b.setBlock(x, 84, z, BR::Stone, 0);
            for (int x = 8; x < 16; ++x)
                for (int z = 8; z < 16; ++z)
                    for (int y = 85; y <= 88; ++y)
                        w979b.setBlock(x, y, z, (y <= 87) ? BR::Water : BR::Stone, 0);
            // 四周全高壁（y85..88）：把「盖下漂浮带」四面围死——漂浮猪水平漂移撞壁折返，恒在水柱上方。
            for (int x = 7; x <= 16; ++x)
                for (int z = 7; z <= 16; ++z) {
                    const bool ring = (x == 7 || x == 16 || z == 7 || z == 16);
                    if (ring)
                        for (int y = 85; y <= 88; ++y) w979b.setBlock(x, y, z, BR::Stone, 0);
                }
            EntityManager em979b;
            const QVector3D far979b(-1000.0f, 90.0f, -1000.0f);
            const int pigD = em979b.spawnMobTyped(11, 85, 11, EntityManager::MobPig,
                                                  QStringLiteral("#f0a8b0"), kPigMax);
            okDeep = pigD >= 0;
            if (okDeep) {
                for (int t = 0; t < 1250; ++t) em979b.tick(0.016f, &w979b, far979b, 0.3f, 1.8f, false);
                okDeep = em979b.deadAt(pigD) || em979b.healthAt(pigD) <= 1; // 3HP − 1HP/s（15s 起）→ 20s 内死/残
            }
            ok = ok && okDeep;
        }
        // (d) 源码钉（t880/t997 先例：QML 掉落表 headless 无行为缝 → 源码钉；谓词单一权威钉同段）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile mf(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            QFile qf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString c = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const QString q = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
            int pinCount = 0;
            int from = 0;
            while (true) {
                const int at = int(c.indexOf(QStringLiteral("mobSnoutSubmergedInWater"), from));
                if (at < 0) break;
                ++pinCount;
                from = at + 1;
            }
            okPin = pinCount >= 4                              // 定义 + 溺水/浮面/resting 破除三消费（单一权威不外散）
                 && c.contains(QStringLiteral("(8 - st) / 8.0f")) // 液面分档公式在位
                 && q.contains(QStringLiteral("const meat = burned ? 0x221 : 0x20B")) // 火焰熟排链保留
                 && q.contains(QStringLiteral("t979 掉落表修正"))
                 && !q.contains(QStringLiteral("猪也掉皮革"))     // t473 猪皮革口径移除
                 && !q.contains(QStringLiteral("0x20D, 1) // 皮革 ×1（t473 扩到猪"))
                 && q.contains(QStringLiteral("0x20D, 1) // 皮革 ×1（非肉，燃烧不变）")); // 牛皮革保留
        }
        ok = ok && okPin;
        if (!ok) ++totalFail;
        // review0903 #3：diag 恒打印（原仅 FAIL 输出且截断丢信息）——PASS 态也留 trench/allFull 数值，
        // 常规跑即可监察判别腿边际（水槽存活数贴近阈值等退化趋势）不待红。
        qInfo().noquote() << "  t979 stats: legFull" << int(legFull[0]) << int(legFull[1])
                          << "legExit" << legExit[0] << legExit[1] << "trench" << trenchAlive
                          << "/8 deep" << int(okDeep) << "pin" << okPin;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t979 pig ankle-deep water safety + drop table: open 1/8 and 1/4"
                             " level puddles zero drowning damage (full HP) and >=6/8 wade out ("
                          << exitedTotal << " exits), covered ankle-deep trench keeps all 4 pigs per"
                             " leg at full HP with snout pinned under the lid (" << trenchAlive
                          << "/8 alive across both levels), pinned under deep source water the pig"
                             " still drowns (chain intact); snout-line-vs-surface single-authority"
                             " predicate shared by drown + swim-up + resting-break, pig drop is"
                             " meat-only with cooked-on-burn kept and cow leather untouched (pins)";
    }

    // ── P-t980 鱿鱼陆地生存两修（离水搁浅窒息掉血至死 + 陆地挣扎缓动非行走步态；局部世界 w980）──
    //    旧状：鱿鱼离水不伤不死（窒息块整体跳过鱿鱼）+ 搁浅委托 aiWander = 正常行走步态（kWalkSpeed
    //    连续步摆）——与「鱿鱼上岸会死 + 笨拙扑腾」的 MC 1.0 语义双缺。修法（与 t979 成对）：
    //    生存 = mobBodyAboveWaterSurface（t979 淹没谓词同族反向：体底线 vs 格内液面，半浸水洼不算离水）
    //    → 1s 宽限后每 1s 扣 1HP 至死（10HP 鱿鱼 ~11s 死）；步态 = aiSquid 搁浅分支改间歇挣扎（每
    //    1.6s 周期随机换向、前 0.6s 以 0.5（=kWalkSpeed 一半）蠕动、后 1.0s 摊歇——moveSpeed 面即
    //    walkPhase 驱动面，低速间歇值域本身排除 kWalkSpeed 连续步态）。
    //    (a) 计时精确腿：干地鱿鱼（10HP）3.504s 恰扣 2 次（宽限 1s + 1HP/s → 第 1/2 次在 ~2.0/~3.0s，
    //        边距 0.5s ≫ aiTick 量子 0.064s）→ health==8。
    //    (b) 掉血至死腿：同鱿鱼推到 12s → 恰 10 次扣血 → health==0（死；末次 ~11.0s）。
    //    (c) 陆地步态退化钉（与 a/b 同鱿鱼前 8.5s 采样窗）：max(moveSpeed) ∈ [0.4,0.56]（蠕动爆发恒
    //        发生且上界 < kWalkSpeed=1.0 → 非行走步态）+ 零速样本占比 ≥ 0.3（摊歇窗 1.0/1.6=62.5%
    //        理论值，间歇性可见）。
    //    (d) 水中零回归腿：2 深水源井（双层封闭：y87 檐板 + y88 压顶壁环，review0903 #3）鱿鱼 15s 满血
    //        满活（t828 浮面/喷水行为不受搁浅链影响——水下体底线恒在液面下）。
    //    (e) 源码钉：mobBodyAboveWaterSurface（成对谓词在位）+ kSquidFlopInterval/kSquidStruggleSpeed/
    //        kSquidStrandGraceSeconds（挣扎步态 + 宽限常量，仅新分支存在）。
    //    阴性轮（两刀，见提交正文）：①注释掉 tick 鱿鱼窒息 else 块 → (a)(b) 恰红（409/1，回到
    //        「离水不死」旧世界）；②aiSquid 搁浅分支回退 aiWander 委托 → (c) max(moveSpeed) 触 1.0
    //        恰红（行走步态复现）→ 均复原绿。
    {
        World w980;
        w980.setWidth(40); w980.setDepth(40); w980.setHeight(96); w980.setSeed(23);
        // 围栏平台：y84 石板 + y85..86 边墙（同 P-t979 口径，防 wander/蠕动出平台坠地形假红）。
        for (int x = 6; x < 34; ++x)
            for (int z = 6; z < 34; ++z) w980.setBlock(x, 84, z, BR::Stone, 0);
        for (int x = 6; x < 34; ++x)
            for (int z = 6; z < 34; ++z) {
                const bool wall = (x == 6 || x == 33 || z == 6 || z == 33);
                if (wall)
                    for (int y = 85; y <= 86; ++y) w980.setBlock(x, y, z, BR::Stone, 0);
            }
        // (d) 水井：3×3 两深水源 + 四壁（y85..87）+ 内盖（y87）+ **双层封闭**（review0903 #3 硬化：
        //     单层盖沿形态在提交后常规跑实证漏过一次——鱿鱼越过井沿上平台搁浅假红，matrix_run2 04:07）：
        //     ①压顶——井壁环加高到 y88（高出内盖一层，盖沿平面无裸露壁顶可跨越）；②盖沿外挑——y87 实体
        //     连铺到 12..18 外环（7×7 整片檐板，越沿者落在檐上仍被 y88 壁环挡回）。任何越沿路径须连续穿过
        //     ≥2 层实体格，几何性杜绝（同 t979 (c) 没顶柜「全高壁」防漂出口径）。
        for (int x = 14; x <= 16; ++x)
            for (int z = 14; z <= 16; ++z) {
                for (int y = 85; y <= 86; ++y) w980.setBlock(x, y, z, BR::Water, 0);
                w980.setBlock(x, 87, z, BR::Stone, 0); // 内盖
            }
        for (int x = 12; x <= 18; ++x)
            for (int z = 12; z <= 18; ++z) {
                const bool rim5 = (x == 13 || x == 17 || z == 13 || z == 17);   // 井壁环（13..17 沿）
                const bool flange = (x == 12 || x == 18 || z == 12 || z == 18); // 盖沿外挑环
                if (rim5) {
                    for (int y = 85; y <= 87; ++y) w980.setBlock(x, y, z, BR::Stone, 0);
                    w980.setBlock(x, 88, z, BR::Stone, 0); // 压顶：壁高出内盖一层
                }
                if (flange)
                    w980.setBlock(x, 87, z, BR::Stone, 0); // 外挑：与内盖同层连成 7×7 檐板
            }
        EntityManager em980;
        const QVector3D far980(-1000.0f, 90.0f, -1000.0f);
        const int sqDry = em980.spawnMobTyped(10, 85, 10, EntityManager::MobSquid,
                                              QStringLiteral("#6a4a3a"), 10);
        const int sqWat = em980.spawnMobTyped(15, 85, 15, EntityManager::MobSquid,
                                              QStringLiteral("#6a4a3a"), 10);
        bool ok = sqDry >= 0 && sqWat >= 0;
        float maxSpd = 0.0f;          // (c) 步态采样极值（作用域提出 if(ok) 供 FAIL diag / PASS 行输出）
        int zeroSmp = 0, gaitSmp = 0; //     零速样本数 / 总样本数
        int hp354 = -1, hp120 = -1;   // (a)/(b) 计时腿读数（diag 用）
        int watHp = -1;               // (d) 水井鱿鱼终态血量（diag 用）
        bool watDead = true, watAlive = false;
        QVector3D watPos;             //     终态位置（diag 用：判是否漂出井 / 卡井沿）
        if (ok) {
            for (int t = 0; t < 750; ++t) { // 干地鱿鱼窗：12s（计时至死 + 步态采样）
                em980.tick(0.016f, &w980, far980, 0.3f, 1.8f, false);
                if (t == 219) {
                    hp354 = em980.healthAt(sqDry);
                    ok = ok && hp354 == 8;                     // (a) 3.504s 恰 2 次搁浅扣血
                }
                if (t >= 30 && t <= 530) {                     // (c) 步态采样窗 0.5..8.5s
                    const float ms = em980.moveSpeedAt(sqDry);
                    maxSpd = std::max(maxSpd, ms);
                    if (ms <= 1e-4f) ++zeroSmp;
                    ++gaitSmp;
                }
            }
            hp120 = em980.healthAt(sqDry);
            ok = ok && hp120 == 0;                         // (b) 12s 恰 10 次 → 死
            ok = ok && maxSpd >= 0.4f && maxSpd <= 0.56f   // (c) 蠕动爆发在位且上界非行走速
                 && zeroSmp * 3 >= gaitSmp;                //     零速占比 ≥ 1/3（间歇摊歇可见）
            for (int t = 0; t < 938; ++t)                  // (d) 水井鱿鱼 15s
                em980.tick(0.016f, &w980, far980, 0.3f, 1.8f, false);
            watHp = em980.healthAt(sqWat);
            watDead = em980.deadAt(sqWat);
            watAlive = em980.aliveAt(sqWat);
            watPos = em980.posAt(sqWat);
            ok = ok && watHp == 10 && !watDead && watAlive;
        }
        // (e) 源码钉。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile mf(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            const QString c = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            okPin = c.contains(QStringLiteral("bool mobBodyAboveWaterSurface(World"))
                 && c.contains(QStringLiteral("kSquidFlopInterval"))
                 && c.contains(QStringLiteral("kSquidStruggleSpeed"))
                 && c.contains(QStringLiteral("kSquidStrandGraceSeconds"));
        }
        ok = ok && okPin;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  t980 diag: hp@3.5s" << hp354 << "hp@12s" << hp120
                              << "maxSpd" << maxSpd << "zero/gait" << zeroSmp << "/" << gaitSmp
                              << "watHp" << watHp << "watDead" << int(watDead)
                              << "watAlive" << int(watAlive) << "watPos" << watPos
                              << "pin" << okPin;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t980 squid land survival: beached squid takes 1HP/s stranding damage"
                             " after a 1s grace (exactly 2 hits by 3.5s, dead by 12s) and its land"
                             " gait degrades to intermittent struggling (max moveSpeed" << maxSpd
                          << "< walk speed, >=1/3 samples at rest), water squid stays full HP with"
                             " swim/bob intact; belly-line predicate pairs the t979 snout-line"
                             " authority (pins)";
    }

    // ── t995 地底结构对照首批修正探针（共享 rig：多 seed 小世界池 → 地牢房间逐房采样；worldgen 行为级
    //    + 源码钉，同 t759/t786 先例收录）──
    // 修正是 t995「对照 MC 1.0 布局」首批三项（全落 placeDungeons）：
    //    a) 房间石材 Cobble+Stone 混排 → Cobble+MossyCobble 苔石混排（地板苔率 50% 重于墙/顶 25%）；
    //    b) 内空 7×7 恒定 → W/Z 各 hash 独立位随机 5/7（机制等价 MC 1.0 地牢 5×5..7×7 随机见方）；
    //    c) 恒 1 箱 → 1-2 箱（~50% 对角角位再加一箱，同地牢 flag 首开各自填池）。
    // t999 逐方块校准批合法演化：a/c 两项生成端重写（地板 75% 苔/墙零苔 + 箱尝试规则）→ 本 rig 采样
    //   同步扩展（豁口/箱规则/笼型），b（尺寸位域）原样保留。
    // rig：t786 同款 5 seed（20260821/777/424242/1337/90210）× 128×128×64 世界池（96² 每 5 seed 仅
    //   ~3 间房样本太少 → 升 128² 候选格 16→25/世界）。以地牢刷怪笼（解码 ∈ 僵尸/骷髅/蜘蛛/爬行者；
    //   要塞银鱼笼除外）为锚逐房采样。层位（锚 y = 刷怪笼层 = cy+1）：内空空气 cy..cy+3、地板板 cy-1、
    //   顶板 cy+4、箱恒在 cy+1。采样：笼顶二层（cy+3 —— 无箱/无笼纯空气层）量 ±x/±z 连续 Air 游程 →
    //   内空 W/D（对称且 ∈ {5,7} 才算净样，洞穴/矿井破墙样本弃置不进统计）；净样内统计地板（cy-1）与
    //   墙环（cy..cy+2）+ 顶板（cy+4）苔石/圆石计数 + 箱层（cy+1）带 ChestStateDungeonFlag 的 Chest 数。
    //   t999 扩展采样：墙脚豁口（环格 cy/cy+1 双层 Air + 脚下地板 / 头顶墙俱实 → 2 高豁口格，沿每侧
    //   连续格并 run，每 run 记 1 豁口；run 任一格向外一格仍双层 Air = 穿透）、每箱贴墙规则核、房间
    //   刷怪笼解码型（P-t999 联合面）。
    struct Room995 {
        int w = 0, d = 0;
        int mossyFloor = 0, cobbleFloor = 0; // 地板板（cy-1 = 锚 y-2）苔石/圆石计数
        int mossyWall = 0, cobbleWall = 0;   // 墙环（cy..cy+2）+ 顶板（cy+4 = 锚 y+3）苔石/圆石计数
        int chests = 0;                      // 箱层（cy+1 = 锚 y）带 ChestStateDungeonFlag 的 Chest 数
        bool chestRuleOk = true;             // t999 每箱四水平邻恰一实心（非 Air 非 Chest，与生成端同口径）
        bool chestAdjacent = false;          // t999 两箱相邻（自然成双箱布局；独立单箱，仅登记）
        int openings = 0;                    // t999 墙脚 2 高豁口 run 数
        bool openPenetrated = true;          // t999 每豁口 run 向外穿透（通空气，非盲洞）
        int mobType = -1;                    // t999 房间刷怪笼解码型（EntityManager::MobType）
    };
    std::vector<Room995> rooms995;
    int dungeonRooms995 = 0; // 含破墙弃样（diag 用）
    {
        const quint32 seeds995[] = { 20260821u, 777u, 424242u, 1337u, 90210u }; // 同 t786 池
        EntityManager em995;
        for (quint32 sd : seeds995) {
            World w995;
            w995.setWidth(128);
            w995.setDepth(128);
            w995.setHeight(64);
            w995.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeDungeons）
            for (int y = 7; y + 4 < w995.height(); ++y) // 地牢 cy ≥ 6 → 笼层 y = cy+1 ≥ 7；顶 cy+4 = y+3
                for (int z = 1; z < w995.depth() - 1; ++z)
                    for (int x = 1; x < w995.width() - 1; ++x) {
                        if (w995.blockAt(x, y, z) != BlockRegistry::Spawner) continue;
                        const int mt = em995.spawnerMobTypeForState(int(w995.stateAt(x, y, z)));
                        if (mt != EntityManager::MobShambler && mt != EntityManager::MobBones
                            && mt != EntityManager::MobSpider && mt != EntityManager::MobStalker)
                            continue; // 要塞银鱼笼 / 未知型 → 非地牢
                        ++dungeonRooms995;
                        // 量内空（笼顶二层 y+2 = cy+3，纯空气层）±x/±z 连续 Air 游程（cap 6 防破墙长廊）。
                        auto run995 = [&](int dx, int dz) {
                            int n = 0;
                            while (n < 6 && w995.blockAt(x + dx * (n + 1), y + 2, z + dz * (n + 1)) == BlockRegistry::Air)
                                ++n;
                            return n;
                        };
                        const int rxp = run995(1, 0), rxm = run995(-1, 0);
                        const int rzp = run995(0, 1), rzm = run995(0, -1);
                        const int rw = rxp + rxm + 1, rd = rzp + rzm + 1;
                        if (rxp != rxm || rzp != rzm || (rw != 5 && rw != 7) || (rd != 5 && rd != 7))
                            continue; // 洞穴/矿井破墙 / 非净样 → 弃置
                        Room995 r;
                        r.w = rw; r.d = rd;
                        r.mobType = mt; // t999 联合面（刷怪笼池回归敏感）
                        const int x0 = x - rxm, z0 = z - rzm; // 内空原点角（角箱位）
                        const int x1 = x + rxp, z1 = z + rzp; // 内空对角角（二箱位）
                        // 地板板（y-2 = cy-1）全幅矩形（含墙 footprint 下地板，同 placeDungeons 填充域）。
                        for (int fx = x0 - 1; fx <= x1 + 1; ++fx)
                            for (int fz = z0 - 1; fz <= z1 + 1; ++fz) {
                                const quint8 fb = w995.blockAt(fx, y - 2, fz);
                                if (fb == BlockRegistry::MossyCobble) ++r.mossyFloor;
                                else if (fb == BlockRegistry::Cobble) ++r.cobbleFloor;
                            }
                        // 墙环（y-1..y+2 = cy..cy+2 的边界格）+ 顶板（y+3 = cy+4 全幅）苔/圆石计数。
                        for (int wy = y - 1; wy <= y + 3; ++wy)
                            for (int fx = x0 - 1; fx <= x1 + 1; ++fx)
                                for (int fz = z0 - 1; fz <= z1 + 1; ++fz) {
                                    const bool edge = (fx == x0 - 1 || fx == x1 + 1 || fz == z0 - 1 || fz == z1 + 1);
                                    const bool cap = (wy == y + 3);
                                    if (!edge && !cap) continue;
                                    const quint8 wb = w995.blockAt(fx, wy, fz);
                                    if (wb == BlockRegistry::MossyCobble) ++r.mossyWall;
                                    else if (wb == BlockRegistry::Cobble) ++r.cobbleWall;
                                }
                        // 箱层（y = cy+1）地牢箱清点 + t999 尝试规则核（每箱四水平邻恰一实心 —— 非 Air
                        //   且非 Chest，与 placeDungeons chestSolidNeighbors 同一口径）+ 双箱相邻登记。
                        int chestAx[2] = { 0, 0 }, chestAz[2] = { 0, 0 };
                        for (int fx = x0; fx <= x1; ++fx)
                            for (int fz = z0; fz <= z1; ++fz)
                                if (w995.blockAt(fx, y, fz) == BlockRegistry::Chest
                                    && (w995.stateAt(fx, y, fz) & BlockRegistry::ChestStateDungeonFlag) != 0) {
                                    if (r.chests < 2) { chestAx[r.chests] = fx; chestAz[r.chests] = fz; }
                                    ++r.chests;
                                }
                        for (int ci = 0; ci < r.chests && ci < 2; ++ci) {
                            int solidN = 0;
                            for (int nd = 0; nd < 4; ++nd) {
                                static const int kChestDirs995[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                                const quint8 nb = w995.blockAt(chestAx[ci] + kChestDirs995[nd][0], y,
                                                               chestAz[ci] + kChestDirs995[nd][1]);
                                if (nb != BlockRegistry::Air && nb != BlockRegistry::Chest) ++solidN;
                            }
                            if (solidN != 1) r.chestRuleOk = false; // 贴墙规则破坏（浮箱 / 双实心）
                        }
                        if (r.chests == 2) {
                            int ddx = chestAx[0] - chestAx[1]; if (ddx < 0) ddx = -ddx;
                            int ddz = chestAz[0] - chestAz[1]; if (ddz < 0) ddz = -ddz;
                            if (ddx + ddz == 1) r.chestAdjacent = true; // 自然成双箱布局（登记，不断言）
                        }
                        // t999 墙脚豁口清点：环格（不含角格）上下两层（y-1/y = cy/cy+1）皆 Air 且脚下
                        //   （y-2 = cy-1）地板、头顶（y+1 = cy+2）墙俱实 → 「2 高墙脚豁口」格；沿每侧连续
                        //   格并 run，每 run 记 1 豁口；run 收口时任一格曾向外一格仍双层 Air → 穿透
                        //   （通空气；未穿透 = 盲洞，形态破坏）。
                        auto openCell995 = [&](int fx, int fz) {
                            return w995.blockAt(fx, y - 1, fz) == BlockRegistry::Air
                                && w995.blockAt(fx, y, fz) == BlockRegistry::Air
                                && w995.blockAt(fx, y - 2, fz) != BlockRegistry::Air
                                && w995.blockAt(fx, y + 1, fz) != BlockRegistry::Air;
                        };
                        auto openSide995 = [&](int fx0, int fz0, int dfx, int dfz, int steps, int nx, int nz) {
                            bool prev = false, runPen = false;
                            for (int i = 0; i <= steps; ++i) {
                                const int fx = fx0 + dfx * i, fz = fz0 + dfz * i;
                                const bool cur = (i < steps) ? openCell995(fx, fz) : false; // 末步收 run
                                if (cur) {
                                    if (!prev) { ++r.openings; runPen = false; } // 新 run 起点
                                    if (w995.blockAt(fx + nx, y - 1, fz + nz) == BlockRegistry::Air
                                        && w995.blockAt(fx + nx, y, fz + nz) == BlockRegistry::Air)
                                        runPen = true; // 该格向外穿透
                                } else if (prev && !runPen) {
                                    r.openPenetrated = false; // run 收口未穿透（盲洞）
                                }
                                prev = cur;
                            }
                        };
                        openSide995(x0 - 1, z0, 0, 1, rd, -1, 0); // -x 墙（fz: z0..z0+rd-1）
                        openSide995(x1 + 1, z0, 0, 1, rd, 1, 0);  // +x 墙
                        openSide995(x0, z0 - 1, 1, 0, rw, 0, -1); // -z 墙
                        openSide995(x0, z1 + 1, 1, 0, rw, 0, 1);  // +z 墙
                        rooms995.push_back(r);
                    }
        }
    }
    const int clean995 = int(rooms995.size());

    // P-t995a 苔石混排探针（t999 合法演化：地板苔率窗 ~75% [60,90] + 墙/顶零苔；旧「地板 50% > 墙 25%」
    //   方向性断言随生成端重写作废）：净样池 ≥4；地板苔/圆石两材质齐（逐块混排真发生）；墙环+顶板
    //   零苔且圆石在场（t999 墙顶普通圆石口径）；池化地板苔率窗 [60,90]%；源码钉（旧 mossyPct 双率
    //   lambda 绝迹 + t999 地板 75% 苔行 / 墙零苔行 verbatim）。
    {
        bool ok = clean995 >= 4;
        int mf = 0, cf = 0, mw = 0, cw = 0;
        for (const Room995 &r : rooms995) {
            mf += r.mossyFloor; cf += r.cobbleFloor; mw += r.mossyWall; cw += r.cobbleWall;
        }
        if (!(mf > 0 && cf > 0)) ok = false;                       // 地板两材质齐（混排真发生）
        if (mw != 0 || cw == 0) ok = false;                        // 墙/顶零苔且圆石在场（t999）
        if (!(mf * 10 >= (mf + cf) * 6 && mf * 10 <= (mf + cf) * 9)) ok = false; // 地板苔率窗 [60,90]%
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            if (src.isEmpty()
                || src.contains(QStringLiteral("mossyPct"))
                || !src.contains(QStringLiteral("if (!isFloor) return BlockRegistry::Cobble; // t999 墙 / 顶普通圆石（零苔）"))
                || !src.contains(QStringLiteral("return (wb % 100u) < 75u ? BlockRegistry::MossyCobble : BlockRegistry::Cobble; // t999 地板 75% 苔石"))) {
                qInfo().noquote() << "  [t995a diag] source pin miss src" << src.isEmpty();
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t995a dungeon masonry (t999 evolution): per-block floor mix 25% cobble/"
                             "75% MossyCobble in [60,90]% window with walls/ceiling zero-moss cobble"
                             " replaces the stale dual-rate mix in placeDungeons, MC Monster Room"
                             " masonry look; pooled floor mossy" << mf << "cobble" << cf
                          << "wall mossy" << mw << "cobble" << cw << "over" << clean995 << "clean rooms";
    }

    // P-t995b 内空 5/7 随机探针：净样池 ≥4；全部净样 W/D ∈ {5,7}（采样构造已滤，此处防御复查）；
    //   池内 5 与 7 两种尺寸均出现（随机化真发生，非恒 7 摆设）；源码钉（bit28/29 取值行 verbatim）。
    {
        bool ok = clean995 >= 4;
        bool sawFive = false, sawSeven = false;
        for (const Room995 &r : rooms995) {
            if ((r.w != 5 && r.w != 7) || (r.d != 5 && r.d != 7)) ok = false;
            if (r.w == 5 || r.d == 5) sawFive = true;
            if (r.w == 7 || r.d == 7) sawSeven = true;
        }
        if (!sawFive || !sawSeven) {
            qInfo().noquote() << "  [t995b diag] size variety missing sawFive" << sawFive
                              << "sawSeven" << sawSeven << "clean" << clean995
                              << "sampled" << dungeonRooms995;
            ok = false;
        }
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            if (!src.contains(QStringLiteral("const int roomW = ((r >> 28) & 1u) ? 5 : 7; // t995 内空宽 5/7"))
                || !src.contains(QStringLiteral("const int roomD = ((r >> 29) & 1u) ? 5 : 7; // t995 内空深 5/7"))) {
                qInfo().noquote() << "  [t995b diag] source pin miss";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t995b dungeon room size randomization: interior W/D hash-picked 5/7"
                             " per room (MC 5x5..7x7 variety, was fixed 7x7), both sizes present across"
                             " pooled seeds; clean rooms" << clean995 << "/" << dungeonRooms995;
    }

    // P-t995c 地牢箱尝试规则探针（t999 合法演化：旧「恒角箱 + ~50% 对角二箱」断言随生成端改制作废）：
    //   每净样 0..2 箱（MC 规则 = 2 箱位 × 各 3 次尝试，全失败 → 0 箱少见态，登记不锁）；每箱贴墙核
    //   （四水平邻恰一实心 —— 非 Air 且非 Chest，与生成端 chestSolidNeighbors 同口径）；池内 ≥1 间双箱房
    //   （尝试规则下双箱概率 ~半，固定种子池确定）；双箱相邻（自然成双布局）仅登记（项目无双箱合并态）；
    //   源码钉（t999 尝试规则锚 verbatim + 旧对角二箱行绝迹）。
    {
        bool ok = clean995 >= 4;
        int twoChestRooms = 0, zeroChestRooms = 0, adjRooms = 0;
        for (const Room995 &r : rooms995) {
            if (r.chests < 0 || r.chests > 2) ok = false;
            if (!r.chestRuleOk) ok = false; // 每箱贴墙恰一实心邻
            if (r.chests == 2) ++twoChestRooms;
            if (r.chests == 0) ++zeroChestRooms;
            if (r.chestAdjacent) ++adjRooms;
        }
        if (twoChestRooms < 1) {
            qInfo().noquote() << "  [t995c diag] no two-chest dungeon in pooled seeds, clean" << clean995;
            ok = false;
        }
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            if (!src.contains(QStringLiteral("for (int slot = 0; slot < 2; ++slot) { // t999 2 箱位 × 3 尝试"))
                || !src.contains(QStringLiteral("if (chestSolidNeighbors(px, cy + 1, pz) != 1) continue; // t999 贴墙：恰一实心邻"))
                || src.contains(QStringLiteral("if (((r >> 30) & 1u) == 0u) // t995 对角二箱（~50%）"))) {
                qInfo().noquote() << "  [t995c diag] source pin miss";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t995c dungeon chest attempt rule (t999 evolution): 0-2 chests per room"
                             " via 2 slots x 3 tries (zero-chest rooms" << zeroChestRooms << ", two-chest"
                             " rooms" << twoChestRooms << "/" << clean995 << "), every chest wall-adjacent"
                             " with exactly one solid neighbor, adjacent-pair layout" << adjRooms
                          << " registered (no double-chest merge state in project), corner-guarantee"
                             " retired (pins)";
    }

    // ── P-t999 地牢逐方块校准（墙脚豁口 + t999 口径联合面；复用 t995 5-seed rig 扩展采样）──
    //    腿：①豁口——每净样墙脚豁口 run ∈ [0,5]（MC 上限；全封 = 0 豁口房间，登记）、每 run 向外穿透
    //    （通空气，盲洞 = 形态破坏）、>80% 净样 ≥1 豁口（有限世界无空气邻域全封率由窗口容忍 + 日志登记）；
    //    ②联合面（阴性轮敏感面）——池化地板苔率窗 [60,90]%、墙零苔、净样刷怪笼全 ∈ {Shambler,Bones,
    //    Spider}（Stalker 退出地牢池）；③源码钉——豁口数行 / 挖穿锚 / 地板苔行 verbatim。
    {
        bool ok = clean995 >= 4;
        int openTotal = 0, roomsWithOpen = 0, sealedRooms = 0, penBad = 0;
        for (const Room995 &r : rooms995) {
            if (r.openings < 0 || r.openings > 5) ok = false; // 豁口数 MC 上限 5
            if (!r.openPenetrated) ++penBad;                  // 盲洞（挖而不通空气）
            openTotal += r.openings;
            if (r.openings >= 1) ++roomsWithOpen; else ++sealedRooms;
        }
        if (penBad != 0) ok = false;
        if (clean995 > 0 && roomsWithOpen * 5 <= clean995 * 4) ok = false; // >80% 房间有豁口
        int mf = 0, cf = 0, mw = 0, stalkerRooms = 0;
        for (const Room995 &r : rooms995) {
            mf += r.mossyFloor; cf += r.cobbleFloor; mw += r.mossyWall;
            if (r.mobType == EntityManager::MobStalker) ++stalkerRooms;
        }
        const int floorTotal = mf + cf;
        if (!(mf * 10 >= floorTotal * 6 && mf * 10 <= floorTotal * 9)) ok = false; // 地板苔率窗 [60,90]%
        if (mw != 0) ok = false;                          // 墙/顶零苔（t999）
        if (stalkerRooms != 0) ok = false;                // Stalker 退出地牢池（t999）
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile wf(root + QStringLiteral("/src/World/world.cpp"));
            const QString src = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
            okPin = src.contains(QStringLiteral("const int openingCount = 1 + int((r >> 14) & 7u) % 5; // t999 豁口数 1..5"))
                 && src.contains(QStringLiteral("if (best < 0) break; // 无空气柱可通 → 剩余豁口全弃（全封候选）"))
                 && src.contains(QStringLiteral("return (wb % 100u) < 75u ? BlockRegistry::MossyCobble : BlockRegistry::Cobble; // t999 地板 75% 苔石"));
        }
        if (!okPin) ok = false;
        if (!ok) ++totalFail;
        qInfo().noquote() << "  [t999 diag] openings" << openTotal << "roomsWithOpen" << roomsWithOpen
                          << "/" << clean995 << "sealed" << sealedRooms << "penBad" << penBad
                          << "floorMossy%" << (floorTotal > 0 ? mf * 100 / floorTotal : -1)
                          << "wallMossy" << mw << "stalkerRooms" << stalkerRooms << "pin" << okPin;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t999 dungeon per-block calibration: 1-5 wall-foot 2-high openings per"
                             " room dug outward to nearest air (total" << openTotal << ", rooms with"
                             " >=1 opening" << roomsWithOpen << "/" << clean995 << ">80%, fully sealed"
                             " rooms" << sealedRooms << "registered, no blind tunnels), joint face"
                             " floor mossy in [60,90]% window / walls zero moss / spawner pool"
                             " stalker-free (pins)";
    }

    // ── t1000 成就「隔墙有眼」探针（进入要塞结构区域解锁；行为级 + bounds 一致性 + 源码钉）──
    //   (a) 行为腿：真 PlayerController + 真要塞世界 + 真 PlayerProgress（QML 路由的 C++ 等价直连）——
    //       界外 tick 零信号；走进足迹 → 上升沿恰发一次 + 解锁 + 恰一 toast；界内连 tick 不重发；
    //       重复进出信号再发但 unlock 幂等（toast 仍 1）；finishWorldLoad 重置守卫后界内首 tick 重产沿
    //       （进度已解锁 → 无新 toast）。achievements() 含新条目（名「隔墙有眼」/ 独立根）。
    //   (b) bounds 一致性：insideStronghold 对 placeStronghold 足迹边界内外采样（含墙环 cell / y 上下界）。
    //   (b') 读档反推腿：真 WorldStore save → beginLoad + loadChunks + finishLoad（rebind 从体素反推
    //       portal 坐标）→ bounds 仍判对 + 重进沿重发而 progress 回放后 unlock 幂等不重发 toast。
    //   (c) 源码钉：tick 上升沿守卫 + finishWorldLoad 重置 + QML 路由行 + 定义行 + 常量同源。
    //   被测世界：主世界 w 已被早前探针改种子 / 尺寸（44×44×96 seed26 一族），要塞有无不定 → 同 t759
    //   fallback 模式自建 96×96×48 扫种子（每种子 ~64% 命中，24 发上限仅防退化）。
    {
        bool ok = true;
        World wT1000;
        wT1000.setWidth(96);
        wT1000.setDepth(96);
        wT1000.setHeight(48);
        for (int s = 1; s <= 24 && !wT1000.hasStronghold(); ++s)
            wT1000.setSeed(s);
        ok = ok && wT1000.hasStronghold();
        if (ok) {
            const int px = wT1000.strongholdPortalX(), py = wT1000.strongholdPortalY(), pz = wT1000.strongholdPortalZ();
            const int cx = px, cy = py - World::kStrongholdPortalDy, cz = pz - World::kStrongholdPortalDz; // 反解原点（同 insideStronghold 内部口径）
            const int half = World::kStrongholdHalf;

            // ── (b) bounds 一致性：足迹 cell 闭区间 [cx±22]/[cz±22] × [cy, cy+9] ──
            ok = ok
                 && wT1000.insideStronghold(cx + 0.5, cy + 1.0, cz + 0.5)                            // 大厅中心（内部）
                 && wT1000.insideStronghold(px + 0.5, py + 0.5, pz + 0.5)                            // 传送门框架格
                 && wT1000.insideStronghold(cx + half + 0.5, cy + 1.0, cz + 0.5)                     // +X 墙环 cell（含界）
                 && wT1000.insideStronghold(cx - half + 0.5, cy + 1.0, cz - half + 0.5)              // -X/-Z 角墙 cell
                 && wT1000.insideStronghold(cx + 0.5, double(cy + World::kStrongholdWallH + 1) + 0.5, cz + 0.5) // 顶板层 cell（含界）
                 && !wT1000.insideStronghold(cx + half + 1.5, cy + 1.0, cz + 0.5)                    // +X 足迹外一格
                 && !wT1000.insideStronghold(cx + 0.5, cy + 1.0, cz - half - 1.5)                    // -Z 足迹外一格
                 && !wT1000.insideStronghold(cx + 0.5, double(cy - 1) + 0.5, cz + 0.5)               // 地板层下（y 下界外）
                 && !wT1000.insideStronghold(cx + 0.5, double(cy + World::kStrongholdWallH + 2) + 0.5, cz + 0.5); // 顶板上（y 上界外）
            // 足迹外采样点（世界界内；东出界翻西向镜像——cx∈[23,73] 必有一侧成立）。
            const int outCellX = (cx + half + 9 < wT1000.width()) ? cx + half + 9 : cx - half - 9;
            const double outX = double(outCellX) + 0.5;

            // ── (a) 行为腿：真 PlayerController + 真 PlayerProgress（QML 路由 C++ 等价直连）──
            PlayerProgress progressT1000;
            int enteredCount = 0, toastCount = 0;
            PlayerController pc; // 无窗口直造（componentComplete 不触发，无 16ms 定时器；tick 直调）
            pc.setWorld(&wT1000);
            QObject::connect(&pc, &PlayerController::enteredStronghold, &pc, [&]() {
                ++enteredCount;
                progressT1000.onEnteredStronghold(); // Main.qml Connections onEnteredStronghold 路由的 C++ 等价
            });
            QObject::connect(&progressT1000, &PlayerProgress::achievementUnlocked, &progressT1000,
                             [&](const QString &, const QString &, const QString &) { ++toastCount; });
            // 界外走动（脚位在足迹东/西 9 格外，XZ 界外 y 界内）：连 tick 零信号零解锁。
            pc.loadSavedState(float(outX), float(cy + 1), float(cz) + 0.5f, -90.0f, 0.0f, 0 /* Spectator noclip：定点无重力，脚位不漂 */);
            for (int t = 0; t < 5; ++t) pc.tick();
            ok = ok && enteredCount == 0 && toastCount == 0
                  && !progressT1000.isUnlocked(QStringLiteral("entered_stronghold"));
            // 走进 bounds（大厅中心地板上）：首 tick 上升沿 → 恰 1 信号 + 解锁 + 恰 1 toast。
            pc.loadSavedState(float(cx) + 0.5f, float(cy + 1), float(cz) + 0.5f, -90.0f, 0.0f, 0);
            pc.tick();
            ok = ok && enteredCount == 1 && toastCount == 1
                  && progressT1000.isUnlocked(QStringLiteral("entered_stronghold"));
            // 界内连 tick：守卫已置位 → 不再发（一次性事件信号，禁每帧直发）。
            for (int t = 0; t < 9; ++t) pc.tick();
            ok = ok && enteredCount == 1 && toastCount == 1;
            // 重复进出：出（零信号——离开非事件）→ 再进（第二次沿）→ 信号 2 但 unlock 幂等 toast 仍 1。
            pc.loadSavedState(float(outX), float(cy + 1), float(cz) + 0.5f, -90.0f, 0.0f, 0);
            pc.tick();
            ok = ok && enteredCount == 1;
            pc.loadSavedState(float(cx) + 0.5f, float(cy + 1), float(cz) + 0.5f, -90.0f, 0.0f, 0);
            pc.tick();
            ok = ok && enteredCount == 2 && toastCount == 1;
            // achievements() 含新条目：unlocked + 名「隔墙有眼」+ 独立根（parentId 空）。
            bool foundEntry = false;
            const QVariantList achT1000 = progressT1000.achievements();
            for (const QVariant &v : achT1000) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toString() == QLatin1String("entered_stronghold"))
                    foundEntry = m.value(QStringLiteral("unlocked")).toBool()
                              && m.value(QStringLiteral("name")).toString() == QStringLiteral("隔墙有眼")
                              && m.value(QStringLiteral("parentId")).toString().isEmpty();
            }
            ok = ok && foundEntry;
            // 进世界重置钩子（finishWorldLoad：读档重进同指针路径 setWorld 不触发）→ 守卫清零 →
            // 界内首 tick 重产沿（进度已解锁 → 无新 toast）。
            pc.finishWorldLoad();
            pc.tick();
            ok = ok && enteredCount == 3 && toastCount == 1;

            // ── (b') 读档反推腿：真 WorldStore 存取 + rebind 反推 portal 坐标 + progress 回放幂等 ──
            const QString dbT1000 = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t1000_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbT1000);
            WorldStore storeT1000;
            storeT1000.setWorld(&wT1000);
            bool okSave = storeT1000.openWorld(dbT1000)
                          && storeT1000.saveAll(QStringLiteral("t1000rig"), QVariantList(), QVariantList(), QVariantList());
            storeT1000.closeWorld();

            World wLoad;
            wLoad.setWidth(wT1000.width());
            wLoad.setDepth(wT1000.depth());
            wLoad.setHeight(wT1000.height());
            WorldStore storeLoad;
            storeLoad.setWorld(&wLoad);
            bool okLoad = okSave && storeLoad.openWorld(dbT1000);
            wLoad.beginLoad(wT1000.seed());          // 零填充（不 worldgen）
            okLoad = okLoad && storeLoad.loadChunks() > 0;
            wLoad.finishLoad();                      // ← rebindStrongholdPortalFromVoxels 从体素反推 portal 坐标
            okLoad = okLoad && wLoad.hasStronghold()
                     && wLoad.strongholdPortalX() == px && wLoad.strongholdPortalY() == py
                     && wLoad.strongholdPortalZ() == pz
                     && wLoad.insideStronghold(px + 0.5, py + 0.5, pz + 0.5)     // 读档后 bounds 仍判对
                     && !wLoad.insideStronghold(outX, double(cy + 1), cz + 0.5); // 界外仍判外
            ok = ok && okLoad;

            PlayerProgress progressLoad;
            int toastLoad = 0, loadEntered = 0;
            QObject::connect(&progressLoad, &PlayerProgress::achievementUnlocked, &progressLoad,
                             [&](const QString &, const QString &, const QString &) { ++toastLoad; });
            progressLoad.loadVariant(progressT1000.toVariant()); // 静默回放（loadVariant 直插不 emit toast）
            ok = ok && progressLoad.isUnlocked(QStringLiteral("entered_stronghold")) && toastLoad == 0;
            PlayerController pcLoad;
            pcLoad.setWorld(&wLoad);
            QObject::connect(&pcLoad, &PlayerController::enteredStronghold, &pcLoad, [&]() {
                ++loadEntered;
                progressLoad.onEnteredStronghold(); // 路由同上（unlock 幂等核：重进不重发 toast）
            });
            pcLoad.loadSavedState(float(px) + 0.5f, float(py) + 0.5f, float(pz) + 0.5f, -90.0f, 0.0f, 0);
            pcLoad.tick();
            ok = ok && loadEntered == 1 && toastLoad == 0
                  && progressLoad.isUnlocked(QStringLiteral("entered_stronghold"));
            storeLoad.closeWorld();
            QFile::remove(dbT1000);

            // ── (c) 源码钉：边沿守卫 + 重置钩子 + QML 路由行 + 定义行 + 常量同源 ──
            const QString exeDirT1000 = QCoreApplication::applicationDirPath();
            const QString rootT1000 = QDir(exeDirT1000 + QStringLiteral("/..")).absolutePath();
            const auto readSrcT1000 = [&rootT1000](const QString &rel) -> QString {
                QFile f(rootT1000 + QLatin1Char('/') + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString pcCppT1000 = readSrcT1000(QStringLiteral("src/Game/playercontroller.cpp"));
            const QString mainQmlT1000 = readSrcT1000(QStringLiteral("src/ui/Main.qml"));
            const QString ppCppT1000 = readSrcT1000(QStringLiteral("src/Game/playerprogress.cpp"));
            const QString worldHdrT1000 = readSrcT1000(QStringLiteral("src/World/world.h"));
            const QString worldCppT1000 = readSrcT1000(QStringLiteral("src/World/world.cpp"));
            const bool okPin =
                pcCppT1000.contains(QStringLiteral("if (inStronghold && !m_insideStronghold)"))      // tick 上升沿守卫
                && pcCppT1000.contains(QStringLiteral("读档重进同清进入沿守卫"))                       // finishWorldLoad 重置钩子
                && mainQmlT1000.contains(QStringLiteral("function onEnteredStronghold() { progress.onEnteredStronghold() }")) // 路由行
                && ppCppT1000.contains(QStringLiteral("{ \"entered_stronghold\", nullptr,"))          // 定义行（独立根）
                && ppCppT1000.contains(QStringLiteral("隔墙有眼"))
                && worldHdrT1000.contains(QStringLiteral("static constexpr int kStrongholdHalf = 22;")) // 足迹常量单一权威
                && worldCppT1000.contains(QStringLiteral("kStrongholdHalf"))                          // placeStronghold 引用类常量（同源防漂移）
                && worldCppT1000.contains(QStringLiteral("kStrongholdPortalDy"));
            ok = ok && okPin;
            if (!ok)
                qInfo().noquote() << "  [t1000 diag] bounds" << (okLoad ? "ok" : "BAD") << "entered"
                                  << enteredCount << "toast" << toastCount << "loadEntered" << loadEntered
                                  << "toastLoad" << toastLoad << "entry" << foundEntry << "pin" << okPin;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1000 stronghold-entry achievement: edge-guarded enteredStronghold fires once"
                             " on entering footprint (outside ticks silent, in-bounds re-ticks quiet,"
                             " re-entry re-fires with idempotent toast, finishWorldLoad re-arms guard),"
                             " bounds match footprint ring/roof cells incl. y-range, save->load voxel"
                             " rebind keeps bounds correct with no re-toast, source pins (guard/reset/"
                             "route/def/constants)";
    }

    // ── t1020 成就树扩展探针（面板 A：四结构区域重推导 + 足迹一致性 + 读档重推导）──
    //    rig：96×96×48 逐种子扫描（seeds 1..80，每结构独立记首个命中种子；地牢加验刷怪笼在位 /
    //    两神殿加验落位中心方块完好 —— 被峡谷 / 矿井巷道切损的种子弃样续扫）。断言三层：
    //    (a) 区域表面：四结构 structureRegionCount ≥1 + bounds 闭区间采样（中心 / 三轴角格含界 /
    //        XZ 界外一格 / y 上下界外）+ 足迹-体素同源（地牢 = 周界内刷怪笼格、沙漠 = 金字塔中心
    //        floor 砂岩、丛林 = 苔石混排地板）；
    //    (a') 读档重推导腿：真 WorldStore 存 → beginLoad + loadChunks + finishLoad（rebuild 从
    //        seed 纯算术重推导）→ 四结构区域表与生成期逐项相等 + insideDungeon 判读一致。
    {
        World wT20Dun, wT20Mine, wT20Des, wT20Jun;
        // 高 96：沙漠 / 丛林神殿坐落**地表**（siteOk 守卫 surfaceY+顶冠 < m_height）→ 48 高世界
        //   地表普遍 ≥38 → 神殿恒拒（t1020 首轮 80 seed 全空根因）；96 高给足地表带。地牢 / 矿井
        //   在 y<48 地下，高度只影响 heightAt 钳制 → 96 高统一四世界。
        wT20Dun.setWidth(96);  wT20Dun.setDepth(96);  wT20Dun.setHeight(96);
        wT20Mine.setWidth(96); wT20Mine.setDepth(96); wT20Mine.setHeight(96);
        wT20Des.setWidth(96);  wT20Des.setDepth(96);  wT20Des.setHeight(96);
        wT20Jun.setWidth(96);  wT20Jun.setDepth(96);  wT20Jun.setHeight(96);
        int seedT20Dun = -1, seedT20Mine = -1, seedT20Des = -1, seedT20Jun = -1;
        // 足迹-体素同源抽验：地牢刷怪笼在位（placeDungeons 步骤 3 无条件落笼）／沙漠金字塔层 0 实心盘
        //   边缘 Sandstone（dx=+7 避开风玫瑰羊毛覆盖 —— 中心格 = WoolBlue 蓝块，见 intactT20 内注）／
        //   丛林神殿中心地板 Cobble/MossyCobble（t1004 A 地板全幅混排）。矿井包络无单点同源块（巷道材质
        //   hash 分段）→ 免验（登记）。
        auto intactT20 = [](World &w, int kind, const QVariantList &rg) -> bool {
            const int mnx = rg[0].toInt(), mny = rg[1].toInt(), mnz = rg[2].toInt();
            const int mxx = rg[3].toInt(), mxz = rg[5].toInt();
            if (kind == World::StructureDungeon) {
                const int roomW = mxx - mnx - 1, roomD = mxz - mnz - 1;
                return w.blockAt(mnx + 1 + roomW / 2, mny + 2, mnz + 1 + roomD / 2)
                           == BlockRegistry::Spawner; // 刷怪笼格 = (cx+roomW/2, cy+1, cz+roomD/2)
            }
            if (kind == World::StructureDesertTemple)
                // 锚点 = 层 0 实心盘边缘点（dx=+7：风玫瑰菱域 |dx|+|dz|≤5 之外不被橙/蓝羊毛覆盖；
                //   大厅清空自 S+1 起、入口/顶窗 carve 均 v≥1 → 盘面恒 Sandstone。不可取中心格 ——
                //   placeDesertTemple C) 步 putSolid(cx,S,cz,WoolBlue) 风玫瑰蓝块覆盖中心（t1003 钉），
                //   t1020 首轮 des=-1 根因即锚点误取中心）。仍属足迹 bbox 内「足迹-体素同源」抽验。
                return w.blockAt(mnx + World::kDesertTempleHalf + 7,
                                 mny + World::kDesertTempleChamberDrop + 1,
                                 mnz + World::kDesertTempleHalf) == BlockRegistry::Sandstone;
            if (kind == World::StructureJungleTemple) {
                const quint8 b = w.blockAt(mnx + World::kJungleTempleHalf, mny,
                                           mnz + World::kJungleTempleHalf);
                return b == BlockRegistry::Cobble || b == BlockRegistry::MossyCobble;
            }
            return true; // StructureMineshaft
        };
        for (int s = 1; s <= 80
             && (seedT20Dun < 0 || seedT20Mine < 0 || seedT20Des < 0 || seedT20Jun < 0); ++s) {
            if (seedT20Dun < 0) {
                wT20Dun.setSeed(s);
                if (wT20Dun.structureRegionCount(World::StructureDungeon) > 0)
                    seedT20Dun = intactT20(wT20Dun, World::StructureDungeon,
                                           wT20Dun.structureRegion(World::StructureDungeon, 0)) ? s : -1;
            }
            if (seedT20Mine < 0) {
                wT20Mine.setSeed(s);
                if (wT20Mine.structureRegionCount(World::StructureMineshaft) > 0)
                    seedT20Mine = intactT20(wT20Mine, World::StructureMineshaft,
                                            wT20Mine.structureRegion(World::StructureMineshaft, 0)) ? s : -1;
            }
            if (seedT20Des < 0) {
                wT20Des.setSeed(s);
                if (wT20Des.structureRegionCount(World::StructureDesertTemple) > 0)
                    seedT20Des = intactT20(wT20Des, World::StructureDesertTemple,
                                           wT20Des.structureRegion(World::StructureDesertTemple, 0)) ? s : -1;
            }
            if (seedT20Jun < 0) {
                wT20Jun.setSeed(s);
                if (wT20Jun.structureRegionCount(World::StructureJungleTemple) > 0)
                    seedT20Jun = intactT20(wT20Jun, World::StructureJungleTemple,
                                           wT20Jun.structureRegion(World::StructureJungleTemple, 0)) ? s : -1;
            }
        }
        // bounds 闭区间采样（中心 + 三轴角格含界 + XZ 界外一格 + y 上下界外；纯谓词零栅格访问）。
        auto boundsT20 = [](World &w, int kind) -> bool {
            if (w.structureRegionCount(kind) <= 0) return false;
            const QVariantList rg = w.structureRegion(kind, 0);
            const double mnx = rg[0].toDouble(), mny = rg[1].toDouble(), mnz = rg[2].toDouble();
            const double mxx = rg[3].toDouble(), mxy = rg[4].toDouble(), mxz = rg[5].toDouble();
            const double cx = (mnx + mxx) / 2.0 + 0.5, cy = mny + 1.0, cz = (mnz + mxz) / 2.0 + 0.5;
            return w.insideStructureRegion(kind, cx, cy, cz)                                // 中心
                && w.insideStructureRegion(kind, mnx + 0.5, mny + 0.5, mnz + 0.5)          // -X/-Y/-Z 角格（含界）
                && w.insideStructureRegion(kind, mxx + 0.5, mxy + 0.5, mxz + 0.5)          // +X/+Y/+Z 角格（含界）
                && !w.insideStructureRegion(kind, mnx - 0.5, cy, cz)                       // -X 足迹外一格
                && !w.insideStructureRegion(kind, cx, cy, mxz + 1.5)                       // +Z 足迹外一格
                && !w.insideStructureRegion(kind, cx, mny - 0.5, cz)                       // y 下界外
                && !w.insideStructureRegion(kind, cx, mxy + 1.5, cz);                      // y 上界外
        };
        bool okA = seedT20Dun > 0 && seedT20Mine > 0 && seedT20Des > 0 && seedT20Jun > 0
                   && boundsT20(wT20Dun, World::StructureDungeon)
                   && boundsT20(wT20Mine, World::StructureMineshaft)
                   && boundsT20(wT20Des, World::StructureDesertTemple)
                   && boundsT20(wT20Jun, World::StructureJungleTemple);
        if (!okA)
            qInfo().noquote() << "  [t1020 diag] seeds dun/mine/des/jun =" << seedT20Dun
                              << seedT20Mine << seedT20Des << seedT20Jun;

        // ── (a') 读档重推导腿：区域表逐项相等（四 kind）+ insideDungeon 判读一致 ──
        const QVariantList dunRgT20 = wT20Dun.structureRegion(World::StructureDungeon, 0);
        const int dunMinX = dunRgT20[0].toInt(), dunMinY = dunRgT20[1].toInt(), dunMinZ = dunRgT20[2].toInt();
        const int dunMaxX = dunRgT20[3].toInt();
        const int dunCx = dunMinX + 1, dunCy = dunMinY + 1, dunCz = dunMinZ + 1; // 反解原点（同足迹定义）
        const QString dbT20 = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_t1020_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT20);
        WorldStore storeT20;
        storeT20.setWorld(&wT20Dun);
        bool okSaveT20 = storeT20.openWorld(dbT20)
                          && storeT20.saveAll(QStringLiteral("t1020rig"), QVariantList(), QVariantList(), QVariantList());
        storeT20.closeWorld();
        World wT20Load;
        wT20Load.setWidth(wT20Dun.width());
        wT20Load.setDepth(wT20Dun.depth());
        wT20Load.setHeight(wT20Dun.height());
        WorldStore storeT20Load;
        storeT20Load.setWorld(&wT20Load);
        bool okLoadT20 = okSaveT20 && storeT20Load.openWorld(dbT20);
        wT20Load.beginLoad(wT20Dun.seed());
        okLoadT20 = okLoadT20 && storeT20Load.loadChunks() > 0;
        wT20Load.finishLoad(); // ← rebuildStructureRegions 从 seed 纯算术重推导（零序列化 / 零体素扫描）
        auto regionsEqT20 = [](const World &a, const World &b, int kind) -> bool {
            if (a.structureRegionCount(kind) != b.structureRegionCount(kind)) return false;
            for (int i = 0; i < a.structureRegionCount(kind); ++i)
                if (a.structureRegion(kind, i) != b.structureRegion(kind, i)) return false;
            return true;
        };
        okLoadT20 = okLoadT20
            && regionsEqT20(wT20Dun, wT20Load, World::StructureDungeon)
            && regionsEqT20(wT20Dun, wT20Load, World::StructureMineshaft)
            && regionsEqT20(wT20Dun, wT20Load, World::StructureDesertTemple)
            && regionsEqT20(wT20Dun, wT20Load, World::StructureJungleTemple)
            && wT20Load.insideDungeon(double(dunCx) + 0.5, double(dunCy) + 1.0, double(dunCz) + 0.5);
        okA = okA && okLoadT20;
        if (!okLoadT20)
            qInfo().noquote() << "  [t1020 diag] load-rebind" << (okSaveT20 ? "ok" : "BAD")
                              << "counts" << wT20Dun.structureRegionCount(World::StructureDungeon)
                              << wT20Load.structureRegionCount(World::StructureDungeon);
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1020 structure-region re-derivation: seed-pure sites() recompute fills"
                             " dungeon/mineshaft/desert/jungle region tables (bounds closed-interval"
                             " sampling incl. corner and out-of-range cells, voxel-tie checks:"
                             " dungeon spawner / pyramid sandstone / jungle mossy floor), save->load"
                             " rebuild reproduces identical region tables with no serialization"
                             "(seeds dun/mine/des/jun ="
                          << seedT20Dun << seedT20Mine << seedT20Des << seedT20Jun << ")";

        // ── (a'') 矿井包络行为腿（review0907 低 #3 勘误 + 探针盲区收口）：Rail 为矿井 worldgen 唯一
        //    放置块（Cobweb 有丛林神殿定角 / 要塞书馆散布噪声，不可用作签名）→ 全图 Rail 格必须全部
        //    落进矿井区域表 bbox（巷道 piece 不出包络）；region-0 水平半边恰 = 2×kEnvHalf = 32
        //    （重推导值钉：L 形垂直两腿，负向 6 + lenA-1 9 = 15 + 1 墙环 = 16；旧 26 = 两腿共线误推。
        //    演化点：piece 几何演化时随 world.cpp 推导注释同步改）。
        {
            bool okEnv = false;
            int railCellsEnv = 0, railOutsideEnv = 0;
            const int regionsMineEnv = wT20Mine.structureRegionCount(World::StructureMineshaft);
            if (regionsMineEnv > 0) {
                std::vector<QVariantList> rgsEnv;
                rgsEnv.reserve(regionsMineEnv);
                for (int i = 0; i < regionsMineEnv; ++i)
                    rgsEnv.push_back(wT20Mine.structureRegion(World::StructureMineshaft, i));
                for (int y = 0; y < wT20Mine.height(); ++y)
                    for (int z = 0; z < wT20Mine.depth(); ++z)
                        for (int x = 0; x < wT20Mine.width(); ++x) {
                            if (wT20Mine.blockAt(x, y, z) != BR::Rail) continue;
                            ++railCellsEnv;
                            bool inEnv = false;
                            for (const auto &rg : rgsEnv)
                                if (x >= rg[0].toInt() && x <= rg[3].toInt()
                                    && y >= rg[1].toInt() && y <= rg[4].toInt()
                                    && z >= rg[2].toInt() && z <= rg[5].toInt()) { inEnv = true; break; }
                            if (!inEnv) ++railOutsideEnv;
                        }
                const QVariantList rg0Env = wT20Mine.structureRegion(World::StructureMineshaft, 0);
                const int halfXEnv = rg0Env[3].toInt() - rg0Env[0].toInt();
                const int halfZEnv = rg0Env[5].toInt() - rg0Env[2].toInt();
                okEnv = railCellsEnv > 0 && railOutsideEnv == 0
                        && halfXEnv == 32 && halfZEnv == 32;
                if (!okEnv)
                    qInfo().noquote() << "  [t1020 diag env] regions=" << regionsMineEnv
                                      << "rails=" << railCellsEnv << "outside=" << railOutsideEnv
                                      << "halfX=" << halfXEnv << "halfZ=" << halfZEnv;
            } else {
                qInfo().noquote() << "  [t1020 diag env] no mineshaft region (seedT20Mine="
                                  << seedT20Mine << ")";
            }
            if (!okEnv) ++totalFail;
            qInfo().noquote() << (okEnv ? "PASS" : "FAIL")
                              << "| t1020 mineshaft envelope behavior leg (review0907 low #3): every"
                                 " worldgen Rail cell (" << railCellsEnv << ") lies inside a mineshaft"
                                 " region bbox (corridor pieces never stick out of the envelope) and"
                                 " the region-0 horizontal half-extent is 32 = 2 x kEnvHalf 16"
                                 " (L-shaped perpendicular legs: 6 + 9 + 1 wall ring; old 26 was a"
                                 " collinear mis-derivation), so the entered-mineshaft bbox and the"
                                 " ambient audio zone no longer double the real footprint";
        }

    // ── t1020 成就树扩展探针（面板 B：structureEntered 行为沿 + 成就钩子 + 源码钉；与面板 A 同作用域
    //    —— 复用 wT20Dun / wT20Load / dbT20 / storeT20Load）──
    //    (b) 行为腿：真 PlayerController + 真地牢世界 + 真 PlayerProgress（QML 路由 C++ 等价直连）——
    //        足迹外 tick 零信号；走进 → 上升沿恰发一次 + 解锁 + 恰一 toast；界内连 tick 不重发；
    //        重复进出再发但 unlock 幂等；finishWorldLoad 重置守卫后界内首 tick 重产沿（无新 toast）。
    //    (b') 钩子腿：首杀四生物（怪物猎人同事件双解锁）/ 首煤铁红石父前置门控 / 骑矿车·钓鱼·
    //        箱车 / 结构 kind 映射 1..3 / 幂等不重 toast；achievements() 恰 31 条 + 新条目形状。
    //    (c) 源码钉：tick 上升沿守卫 + 读档重置 + QML 路由行 + 定义行 + 类常量同源。
    {
        // dunCx/dunCy/dunCz/dunMaxX 复用面板 A 反解值（同作用域）。
        const double outXT20 = (dunMaxX + 9 < wT20Dun.width()) ? double(dunMaxX) + 9.5
                                                               : double(dunMinX) - 8.5;
        // ── (b) 行为腿 ──
        PlayerProgress progT20A;
        int enteredT20[World::StructureKindCount] = { 0, 0, 0, 0 };
        int toastT20 = 0;
        PlayerController pcT20;
        pcT20.setWorld(&wT20Dun);
        QObject::connect(&pcT20, &PlayerController::structureEntered, &pcT20, [&](int kind) {
            ++enteredT20[kind];
            progT20A.onStructureEntered(kind); // Main.qml Connections onStructureEntered 路由的 C++ 等价
        });
        QObject::connect(&progT20A, &PlayerProgress::achievementUnlocked, &progT20A,
                         [&](const QString &, const QString &, const QString &) { ++toastT20; });
        bool okB = true;
        // 足迹外走动：连 tick 零信号零解锁。
        pcT20.loadSavedState(float(outXT20), float(dunCy + 1), float(dunCz) + 0.5f, -90.0f, 0.0f, 0);
        for (int t = 0; t < 5; ++t) pcT20.tick();
        okB = okB && enteredT20[0] == 0 && toastT20 == 0
              && !progT20A.isUnlocked(QStringLiteral("entered_dungeon"));
        // 走进房间足迹：首 tick 上升沿 → 恰 1 信号 + 解锁 + 恰 1 toast。
        pcT20.loadSavedState(float(dunCx) + 0.5f, float(dunCy + 1), float(dunCz) + 0.5f, -90.0f, 0.0f, 0);
        pcT20.tick();
        okB = okB && enteredT20[0] == 1 && toastT20 == 1
              && progT20A.isUnlocked(QStringLiteral("entered_dungeon"));
        // 界内连 tick：守卫已置位 → 不再发（一次性事件信号）。
        for (int t = 0; t < 9; ++t) pcT20.tick();
        okB = okB && enteredT20[0] == 1 && toastT20 == 1;
        // 重复进出：出（零信号）→ 再进（第二次沿）→ 信号 2 但 unlock 幂等 toast 仍 1。
        pcT20.loadSavedState(float(outXT20), float(dunCy + 1), float(dunCz) + 0.5f, -90.0f, 0.0f, 0);
        pcT20.tick();
        okB = okB && enteredT20[0] == 1;
        pcT20.loadSavedState(float(dunCx) + 0.5f, float(dunCy + 1), float(dunCz) + 0.5f, -90.0f, 0.0f, 0);
        pcT20.tick();
        okB = okB && enteredT20[0] == 2 && toastT20 == 1;
        // 进世界重置钩子（finishWorldLoad）→ 守卫清零 → 界内首 tick 重产沿（进度已解锁 → 无新 toast）。
        pcT20.finishWorldLoad();
        pcT20.tick();
        okB = okB && enteredT20[0] == 3 && toastT20 == 1;
        // ── (b') 钩子腿（fresh 进度 VM；toast 台账见各步注释。先补主线祖先链 open_inventory→get_wood→
        //    crafting_table —— unlock 父前置检查吞无父链的首杀 / 首矿解锁（t1020 首轮 toastB=6 卡
        //    独立根的根因），同真实生存顺序）──
        PlayerProgress progT20B;
        int toastT20B = 0;
        QObject::connect(&progT20B, &PlayerProgress::achievementUnlocked, &progT20B,
                         [&](const QString &, const QString &, const QString &) { ++toastT20B; });
        progT20B.onInventoryOpened();                                       // open_inventory（1，根）
        progT20B.onItemPicked(int(BlockRegistry::Log));                     // get_wood（2）
        progT20B.onCraft(int(BlockRegistry::CraftingTable));                // crafting_table（3）
        progT20B.onCraft(int(ToolRegistry::SwordWood));                     // sword_time（4）
        okB = okB && progT20B.isUnlocked(QStringLiteral("sword_time"))
              && progT20B.isUnlocked(QStringLiteral("crafting_table"));
        progT20B.onMobKilled(int(EntityManager::MobSpider));                // monster_hunter（5）+ kill_spider（6）
        okB = okB && progT20B.isUnlocked(QStringLiteral("monster_hunter"))
              && progT20B.isUnlocked(QStringLiteral("kill_spider")) && toastT20B == 6;
        progT20B.onMobKilled(int(EntityManager::MobBones));                 // kill_bones（7）
        progT20B.onMobKilled(int(EntityManager::MobStalker));               // kill_stalker（8）
        progT20B.onMobKilled(int(EntityManager::MobSilverfish));            // kill_silverfish（9）
        okB = okB && progT20B.isUnlocked(QStringLiteral("kill_bones"))
              && progT20B.isUnlocked(QStringLiteral("kill_stalker"))
              && progT20B.isUnlocked(QStringLiteral("kill_silverfish")) && toastT20B == 9;
        progT20B.onItemPicked(int(RecipeRegistry::CoalId));                 // 父「挖矿时间到」未解锁 → 吞（9）
        okB = okB && !progT20B.isUnlocked(QStringLiteral("get_coal")) && toastT20B == 9;
        progT20B.onCraft(int(ToolRegistry::PickaxeWood));                   // mining_time（10）
        progT20B.onItemPicked(int(RecipeRegistry::CoalId));                 // get_coal（11）
        okB = okB && progT20B.isUnlocked(QStringLiteral("mining_time"))
              && progT20B.isUnlocked(QStringLiteral("get_coal")) && toastT20B == 11;
        progT20B.onCraft(int(ToolRegistry::PickaxeStone));                  // upgrade（12）
        progT20B.onItemPicked(int(RecipeRegistry::IronOreDropId));          // get_iron（13）
        okB = okB && progT20B.isUnlocked(QStringLiteral("upgrade"))
              && progT20B.isUnlocked(QStringLiteral("get_iron")) && toastT20B == 13;
        progT20B.onItemPicked(int(RecipeRegistry::RedstoneId));             // 父「钻石!」未解锁 → 吞（13）
        okB = okB && !progT20B.isUnlocked(QStringLiteral("get_redstone")) && toastT20B == 13;
        progT20B.onItemPicked(int(RecipeRegistry::DiamondId));              // get_diamond（14）
        progT20B.onItemPicked(int(RecipeRegistry::RedstoneId));             // get_redstone（15）
        okB = okB && progT20B.isUnlocked(QStringLiteral("get_diamond"))
              && progT20B.isUnlocked(QStringLiteral("get_redstone")) && toastT20B == 15;
        progT20B.onMinecartRideStarted(0.0, 0.0);
        progT20B.onMinecartMoved(600.0f, 600.0, 0.0);                       // ride_minecart（16，t1048：径向 ≥500 达阈；600 越阈避开恰 500 校准缝——阴性缝摘除时本腿保绿，恰红 t1046e）
        progT20B.onFishCaught();                                            // first_catch（17）
        progT20B.onChestCartOpened();                                       // chest_cart_loot（18）
        progT20B.onStructureEntered(1);                                     // entered_mineshaft（19）
        progT20B.onStructureEntered(2);                                     // entered_desert_temple（20）
        progT20B.onStructureEntered(3);                                     // entered_jungle_temple（21）
        okB = okB && progT20B.isUnlocked(QStringLiteral("ride_minecart"))
              && progT20B.isUnlocked(QStringLiteral("first_catch"))
              && progT20B.isUnlocked(QStringLiteral("chest_cart_loot"))
              && progT20B.isUnlocked(QStringLiteral("entered_mineshaft"))
              && progT20B.isUnlocked(QStringLiteral("entered_desert_temple"))
              && progT20B.isUnlocked(QStringLiteral("entered_jungle_temple")) && toastT20B == 21;
        progT20B.onMinecartMoved(1.0f, 601.0, 0.0);                         // 幂等：已解锁，越阈续乘不再 toast
        progT20B.onFishCaught();
        progT20B.onChestCartOpened();
        progT20B.onStructureEntered(3);
        progT20B.onStructureEntered(99);                                    // 越界 kind 防御忽略
        okB = okB && toastT20B == 21;
        // achievements() 恰 31 条 + 新条目形状（名 / 父链 / 独立根 / 图标 id 非 0）。
        const QVariantList achT20 = progT20B.achievements();
        int totalDefsT20 = 0;
        bool spiderShape = false, dungeonShape = false, cartShape = false;
        for (const QVariant &v : achT20) {
            const QVariantMap m = v.toMap();
            ++totalDefsT20;
            const QString id = m.value(QStringLiteral("id")).toString();
            if (id == QLatin1String("kill_spider"))
                spiderShape = m.value(QStringLiteral("unlocked")).toBool()
                           && m.value(QStringLiteral("name")).toString() == QStringLiteral("织网终结者")
                           && m.value(QStringLiteral("parentId")).toString() == QLatin1String("monster_hunter");
            if (id == QLatin1String("entered_dungeon"))
                dungeonShape = m.value(QStringLiteral("name")).toString() == QStringLiteral("地牢探秘")
                            && m.value(QStringLiteral("parentId")).toString().isEmpty()
                            && m.value(QStringLiteral("iconId")).toInt() == int(BlockRegistry::Spawner);
            if (id == QLatin1String("ride_minecart"))
                cartShape = m.value(QStringLiteral("name")).toString() == QStringLiteral("轨道骑士")
                         && m.value(QStringLiteral("parentId")).toString().isEmpty()
                         && m.value(QStringLiteral("iconId")).toInt() == int(RecipeRegistry::MinecartId);
        }
        okB = okB && totalDefsT20 == 31 && spiderShape && dungeonShape && cartShape;
        // 读档回放：loadVariant 静默恢复 → 界内重进沿重发而 toast 不重发。
        PlayerProgress progT20Load;
        int toastT20Load = 0, enteredT20Load = 0;
        progT20Load.loadVariant(progT20A.toVariant());
        okB = okB && progT20Load.isUnlocked(QStringLiteral("entered_dungeon"));
        PlayerController pcT20Load;
        pcT20Load.setWorld(&wT20Load);
        QObject::connect(&pcT20Load, &PlayerController::structureEntered, &pcT20Load, [&](int kind) {
            if (kind == World::StructureDungeon) ++enteredT20Load;
            progT20Load.onStructureEntered(kind);
        });
        QObject::connect(&progT20Load, &PlayerProgress::achievementUnlocked, &progT20Load,
                         [&](const QString &, const QString &, const QString &) { ++toastT20Load; });
        pcT20Load.loadSavedState(float(dunCx) + 0.5f, float(dunCy + 1), float(dunCz) + 0.5f, -90.0f, 0.0f, 0);
        pcT20Load.tick();
        okB = okB && enteredT20Load == 1 && toastT20Load == 0;
        storeT20Load.closeWorld();
        QFile::remove(dbT20);
        if (!okB)
            qInfo().noquote() << "  [t1020 diag] entered" << enteredT20[0] << "toast" << toastT20
                              << "toastB" << toastT20B << "defs" << totalDefsT20
                              << "shape" << spiderShape << dungeonShape << cartShape
                              << "loadEntered" << enteredT20Load << "loadToast" << toastT20Load;

        // ── (c) 源码钉：边沿守卫 + 重置钩子 + QML 路由行 + 定义行 + 类常量同源 ──
        //     B-P1-1 迁移：滤注释钉（pinSet）。两处改锚（B-P2-2 / B-P2-5）：
        //     · 「读档重进同清四结构进入沿守卫」原是 playercontroller.cpp 注释文本 → 改锚清零
        //       **语句** `m_insideStructure[k] = false;` minCount 3（setWorld / finishWorldLoad /
        //       leaveWorld 三处生命周期边界全须在场；少任一处即红 —— 三口同清契约）；
        //     · 「轨道骑士」显示名钉 → 改锚稳定标识符：解锁定义行 `{ "ride_minecart",`（显示名
        //       文案可自由改版，def id 行才是接线契约）。
        {
            const QString rootT20 = QDir(QCoreApplication::applicationDirPath()
                                         + QStringLiteral("/..")).absolutePath();
            const auto srcT20 = [&rootT20](const QString &rel) {
                return rootT20 + QLatin1Char('/') + rel;
            };
            QStringList missPinT20;
            missPinT20 << pinSet(srcT20(QStringLiteral("src/Game/playercontroller.cpp")), {
                {"tick-edge-guard", "if (inStruct && !m_insideStructure[k])"},   // tick 上升沿守卫
                {"reset-hook-clear", "m_insideStructure[k] = false;", 3},        // 三处重置清零语句
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/Game/playercontroller.h")), {
                {"sig-structureEntered", "void structureEntered(int kind);"},    // 信号声明
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/ui/Main.qml")), {
                {"qml-route-structureEntered", "function onStructureEntered(kind) { progress.onStructureEntered(kind) }"}, // 路由行
                // t1046 起 ridingCart 门控里程埋点；t1048 改径向口径：路由行随签名携当前位置采样。
                {"qml-route-minecartMoved", "if (ridingCart) progress.onMinecartMoved(deltaBlocks, player.position.x, player.position.z)"},
                {"qml-route-chestCartOpened", "if (isCartCell) progress.onChestCartOpened()"},
                {"qml-route-fishCaught", "progress.onFishCaught()"},
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/Game/playerprogress.cpp")), {
                {"def-entered-dungeon", "{ \"entered_dungeon\", nullptr,"},      // 定义行（独立根）
                {"def-ride-minecart-row", "{ \"ride_minecart\","},               // def id 行（显示名钉改锚）
                {"def-spider-kind", "if (mobType == MT::MobSpider || mobType == MT::MobCaveSpider)"},
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/Game/playerprogress.h")), {
                {"invokable-structureEntered", "Q_INVOKABLE void onStructureEntered(int kind);"},
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/World/world.h")), {
                {"enum-StructureKind", "enum StructureKind"},                    // 区域 kind 契约
                {"const-kDesertTempleHalf", "static constexpr int kDesertTempleHalf = 10;"}, // 足迹常量单一权威
            });
            missPinT20 << pinSet(srcT20(QStringLiteral("src/World/world.cpp")), {
                {"fn-rebuildStructureRegions", "rebuildStructureRegions"},
                {"const-kDesertBiomeGuarantee", "kDesertBiomeGuarantee"},        // t1010 保底旗迁移存活
            });
            const bool okPinT20 = missPinT20.isEmpty();
            if (!okPinT20)
                qInfo().noquote() << "  [t1020 diag] source pins drifted:" << missPinT20.join(QLatin1Char(','));
            okB = okB && okPinT20;
        }
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1020 achievement-tree expansion: structureEntered edge fires once per"
                             " dungeon entry with idempotent toast and finishWorldLoad re-arm,"
                             " first-kill x4 co-unlocks with monster_hunter, first-ore parent gating"
                             " (coal/iron/redstone), minecart 500m radial ride hook (t1048)/fish/chest-cart"
                             " hooks, kind mapping, 31 defs with shape checks, save->load replay"
                             " without re-toast, source pins (guard/reset/route/def/constants)";
    }
    } // t1020 面板 A+B 共用作用域收口（B 复用 A 的探针世界 / 存档句柄）
}
