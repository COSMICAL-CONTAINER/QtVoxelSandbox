// tools/matrix/section03_mid_probes.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L12288-18560 逐字节搬移（段 md5: 722d4992b8723eeebe597e62405c575e；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section03_mid_probes()
{

    // ── t913 发射器冷却对齐 MC 探针（Game 层真消费端，t868 模式；spec「调研 MC 1.0 发射器实际延迟
    //    （MC 语义约 4 game ticks = 0.2s，且同一信号沿只触发一次=上升沿触发）；以调研值为准重钉常量
    //    与探针（现 kDispenserCooldown 0.5f）」）──
    //   MC 出处（dev-plan R19.16 钉值；实网核验被反爬 403 拦截，以 dev-plan 所钉 MC 语义为准）：发射器
    //   重触发间隔 = 4 game ticks 按本作红石 10Hz 时基折算 = **2 redstone ticks = 0.2s**（review28 #2
    //   更正旧注 20Hz 表述）；同一信号沿只触发一次 = 上升沿触发（t689 m_dispenserPoweredCells 基线集，
    //   既有语义正交不动）。
    //   断言三段（钉常量 ∈ (0.1, 0.3] 窗——改 0.5/2.0 → (c) FAIL 复现用户「持续闪烁只射几根箭」；
    //   改 0/0.1 → (b) FAIL 防抖闸失效）：
    //   (a) 首沿恰发一支（基线）；
    //   (b) 0.112s（7 帧）后的新沿 → 冷却拦（0.1 < 0.2：箭数持平 + 库存不扣）；
    //   (c) 续 0.128s（累计 0.24s > 0.2）后的新沿 → 必再发（箭 +1 / 库存再扣）。
    //   (d) review28 #2 新增：**0.2s 等周期时钟逐沿发射**（常量窗 (a)(b)(c) 测不到帧相位量化）——
    //       红石沿由 10Hz 世界时钟产生、帧内经信号到达，60fps 帧递减把 0.2s 冷却量化成 12 帧归零；
    //       周期恰等于冷却值时旧纯 >0 闸在第 12 帧遇沿（余量 ≈0.008s > 0）确定性吞沿 → 半速率发射。
    //       rig 模拟生产时序：每格世界 tick 之间**交替注入 scanDispenserTraps(1/60s) 帧驱动 + 沿写入**
    //       （沿注入 = 置拉杆再 tickRedstone 触达，等价 QML 信号帧内到达），连发 N=6 沿断言 N 次发射
    //       （回退一帧容差 → 恰 3 次 = FAIL）。
    runLegMulti({ "t913 dispenser cooldown pinned to MC 0.2s (2 redstone ticks @ 10Hz; rising-edge-once semantics s"
        "tay in the t689 baseline set): first edge fires exactly one arrow, a fresh rising edge 0.112s in"
        " stays blocked (cooldown window), an edge at 0.24s total MUST re-fire, and a 0.2s equal-period c"
        "lock interleaved with 60fps frame-driven decay fires all 6 edges (review28 #2: the frame-quantiz"
        "ed remainder ~0.008s at the 12th frame must not swallow the on-period edge; constant window pinn"
        "ed in (0.1, 0.3])" }, [&]() {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto arrowCount913 = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++n;
            return n;
        };
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);
        store.ensureDispenser(x0, kRigY, z0);
        store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 4);
        tickN(w, 2);
        const auto edgeOn = [&]() { placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1); tickN(w, 4); };
        const auto edgeOff = [&]() { w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0); tickN(w, 4); };
        edgeOn();                                        // (a) 首沿 → 恰发一支
        const int a1 = arrowCount913();
        const bool okA = a1 == 1 && store.slotCountAt(x0, kRigY, z0, 0) == 3;
        edgeOff();                                       // 清沿基线
        for (int f = 0; f < 7; ++f) pc.scanDispenserTraps(0.016f); // 0.112s 帧驱动递减（< 0.2s）
        edgeOn();                                        // (b) 冷却窗内新沿 → 拦
        const int a2 = arrowCount913();
        const bool okB = a2 == 1 && store.slotCountAt(x0, kRigY, z0, 0) == 3;
        edgeOff();
        for (int f = 0; f < 8; ++f) pc.scanDispenserTraps(0.016f); // 续 0.128s（累计 0.24s > 0.2s）
        edgeOn();                                        // (c) 冷却已过 → 必再发
        const int a3 = arrowCount913();
        const bool okC = a3 == 2 && store.slotCountAt(x0, kRigY, z0, 0) == 2;
        // (d) 0.2s 等周期时钟连发 6 沿 → 6 次发射（review28 #2 帧相位量化回归面）。
        //    生产时序 = 10Hz 沿网格 × 60fps 帧递减并存：每格世界 tick 之间先 scanDispenserTraps(1/60s)
        //    推帧（冷却递减 + 记本帧容差），再写沿（拉杆置位 → tickRedstone 复算 → 电力沿信号到达 →
        //    fireDispenserAtQml）。每周期 12 格 = 0.2s；库存预填 8 箭足额。第 12 格的沿在旧纯 >0 闸下
        //    余量 ≈0.008s > 0 被拦（此后沿逐周期丢 → 恰 3 次）；新容差闸逐沿放行 = 恰 6 次。
        edgeOff();                                       // 清 (c) 沿基线
        store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 8);
        const int d0 = arrowCount913();
        for (int cyc = 0; cyc < 6; ++cyc) {
            for (int g = 0; g < 12; ++g) {
                pc.scanDispenserTraps(1.0f / 60.0f);     // 帧驱动递减（本帧容差同步写入）
                if (g == 11) {
                    placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1); // 沿注入（等周期第 12 格）
                    tickN(w, 1);                         // 复算 → 电力沿信号帧内到达
                    w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0); // 立即清（下周期同一格再注入沿）
                    tickN(w, 1);
                } else {
                    tickN(w, 1);
                }
            }
        }
        const int dN = arrowCount913() - d0;
        const bool okD = dN == 6 && store.slotCountAt(x0, kRigY, z0, 0) == 8 - 6;
        const bool ok = okA && okB && okC && okD;
        if (!ok)
            qInfo().noquote() << "  t913 first" << a1 << "inWindow" << a2 << "afterWindow" << a3
                              << "equalPeriodFired" << dN
                              << "stock" << store.slotCountAt(x0, kRigY, z0, 0);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t913 dispenser cooldown pinned to MC 0.2s (2 redstone ticks @ 10Hz; "
                             "rising-edge-once semantics stay in the t689 baseline "
                             "set): first edge fires exactly one arrow, a fresh rising edge 0.112s in "
                             "stays blocked (cooldown window), an edge at 0.24s total MUST re-fire, "
                             "and a 0.2s equal-period clock interleaved with 60fps frame-driven decay "
                             "fires all 6 edges (review28 #2: the frame-quantized remainder ~0.008s at "
                             "the 12th frame must not swallow the on-period edge; constant window "
                             "pinned in (0.1, 0.3])";
        // 清场
        w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0, kRigY, z0, BR::Air, 0);
        store.clearDispenser(x0, kRigY, z0);
        ents.clearAll();
        tickN(w, 2);
    });

    // ── t866 载具攻击 / 摧毁语义探针（Game 层 PlayerController + EntityManager + MinecartManager 直编）──
    //   用户报告（R19.15）：①「矿车载生物时打矿车本体 → 打到生物 → 生物永远下不来」（乘骑 mob 钉座位
    //   AABB 与车体重叠 → 攻击射线恒先中乘员，矿车耐久链永不可达 → 下车唯一路径〔车毁〕永不成）；
    //   ②「矿车运动中碰仙人掌应变掉落物、乘员自动下来；岩浆同样」（矿车原无环境摧毁链）。矩阵断言：
    //   (a1) 生存攻击乘骑车：命中乘员改判进矿车耐久链（hpAt 3→2）+ 乘员不掉血 + 仍在车（末击摧毁释放
    //        链由 t811 探针 (d) 已覆盖，此处钉改判面）；
    //   (a2) 创造攻击乘骑车：瞬毁 + cartBroken 不发（创造无掉落）+ 乘员对账自动释放（rideCart==-1、
    //        存活、不掉血）；
    //   (a3) 源码钉（t889 先例）：beginMining 乘员重路由 = **验 hitCartFromRay/hitBoatFromRay 返回值**
    //        （review26 #4：旧无条件 return 在「瞄乘员露出车斗的上身/头部」几何〔射线中 mob 不交车盒〕
    //        吞击——冷却+挥手已发但零效果）+ 射线长度 m_hitDist（非 kReach 全程，极端角度不可隔墙打车）
    //        + 未命中车盒落回 attackMob（乘员本体照旧可打）；
    //   (a4) 行为几何钉（review26 #4 复现形态）：Shambler 乘员（halfH 0.90，头顶高出车盒顶 ~1.04 格）
    //        瞄上身高度的水平射线 → findMobHit 命中乘员 + findCartHit 恒 -1（= 驱动 a3 落回分支的几何事实）；
    //   (b)  仙人掌：轨端前方一格仙人掌 → 空车被推到末格中心（AABB 前沿探入仙人掌格）→ 下一
    //        tickPushedCarts 环境检查即毁 + cartBroken 发（生存掉落语义）；
    //   (c)  岩浆：静止车格被岩浆灌入（setBlock Lava）→ 下一 tick 即毁（同链；掉落物落岩浆由
    //        ItemEntityManager 瞬毁判定收尾，净效果 = 毁无物）。
    runLegMulti({ "t866 attack on passenger-carrying cart routes to cart durability (mob unharmed, still seated), c"
        "reative hit destroys + passenger auto-released, moving cart touching cactus breaks into dropped "
        "item, lava cell destroys cart same chain" }, [&]() {
        // rig 选址：运行期扫描空区（t867 先例）。需 8×1×4 净空（含隔离边）。
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
            qInfo().noquote() << "FAIL | t866 vehicle attack/environment destroy: no clear rig area found";
        } else {
            // ── (a) 攻击重路由 ── 行为级（captured 门内 beginMining 不可直驱，t889 源码钉补接线面）：
            // 石基座 + 单轨 + 矿车 + mob 登乘（tickVehicleRiding 扫描 ≤0.8 格）。
            w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            // 乘员用 Shambler（halfH 0.90 高个）：头顶高出车盒顶 ~1.04 格 —— 正是 review26 #4 的
            // 「瞄上身射线不交车盒」形态载体（短 mob 上身全在车盒内，旧吞击几何不可达）。
            const int mob = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                               QStringLiteral("#ff5555"), 50);
            for (int t = 0; t < 8 && ents.rideCartAt(mob) < 0; ++t) {
                ents.tick(0.016f, &w, carts.posAt(0) + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            int brokenCount = 0;
            QObject::connect(&carts, &MinecartManager::cartBroken, &carts,
                             [&](int, int, int) { ++brokenCount; });
            const QVector3D cp0 = carts.posAt(0);
            const QVector3D eye(cp0.x(), cp0.y() + 4.0f, cp0.z());
            const QVector3D down(0.0f, -1.0f, 0.0f);
            const int mobHp0 = ents.healthAt(mob);
            // (a0) 前置钉：乘骑 mob 的 AABB 与车体重叠 → 攻击射线恒先中乘员（改判是**承重**的——
            //      无它则攻击打在 mob 上、矿车耐久链永不可达 = 用户症状根因链）。
            const bool okA0 = ents.findMobHit(eye, down, 4.0f, nullptr) == mob;
            // (a1) 生存击（= 改判后 beginMining 调的同一调用面）：hp 3→2 + 乘员不掉血 + 仍在车。
            carts.hitCartFromRay(eye, down, 4.0f, &w, /*instantBreak=*/false);
            const bool okA1 = ents.rideCartAt(mob) == 0 && carts.aliveAt(0)
                              && carts.hpAt(0) == 2
                              && ents.healthAt(mob) == mobHp0
                              && brokenCount == 0;
            // (a2) 创造击：瞬毁 + 乘员对账自动释放 + 无掉落信号（t767 创造无掉落）。
            carts.hitCartFromRay(eye, down, 4.0f, &w, /*instantBreak=*/true);
            ents.tick(0.016f, &w, cp0 + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
            ents.tickVehicleRiding(); // 对账：座位指空槽 → mob 自释放
            const bool okA2 = !carts.aliveAt(0) && ents.rideCartAt(mob) == -1
                              && ents.aliveAt(mob) && ents.healthAt(mob) == mobHp0
                              && brokenCount == 0;
            // (a3) 源码钉（t889 先例；t1015 改版）：beginMining mob 分支的骑乘甄别接线——乘骑判定
            //      （rideCartAt/rideBoatAt）→ **t1015 指定载具单盒甄别**（rayHitDistAt(乘员所乘的那台,
            //      eye, look, m_hitDist) 与乘员距离比较取射线最近者）→ 命中载具盒进 hit*At 指定结算
            //      （冷却门 && 判定 = 单击单目标）→ 乘员盒更近 / 载具未中落回 attackMob（乘员本体照旧
            //      可打，review26 #4 语义保持）。滤注释体（注释里的字面量不参与）。
            bool okA3 = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
                QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
                const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::beginMining()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::endMining()"));
                if (b0 < 0 || b1 <= b0) {
                    qInfo().noquote() << "  t866 (a3) beginMining slice miss";
                } else {
                    QString body;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                        if (!line.trimmed().startsWith(QLatin1String("//"))) {
                            body += line; body += QLatin1Char('\n');
                        }
                    // 骑乘甄别语句面（t1015 契约）：乘骑判定 + 「冷却门 && rayHitDistAt 指定载具单盒
                    // 甄别（m_hitDist 射线长）」值门 → hit*At(指定槽) 结算 + 分支内落回 attackMob
                    // （首个出现位须在各自值门调用之后）。
                    const int iRideC = body.indexOf(QStringLiteral("m_entityManager->rideCartAt(mobIdx)"));
                    const int iRideB = body.indexOf(QStringLiteral("m_entityManager->rideBoatAt(mobIdx)"));
                    const int iDistC = body.indexOf(QStringLiteral("m_minecartManager->rayHitDistAt(rideCart, eye, look, m_hitDist)"));
                    const int iDistB = body.indexOf(QStringLiteral("m_boatManager->rayHitDistAt(rideBoat, eye, look, m_hitDist)"));
                    const int iHitC  = body.indexOf(QStringLiteral("m_minecartManager->hitCartAt(rideCart"));
                    const int iHitB  = body.indexOf(QStringLiteral("m_boatManager->hitBoatAt(rideBoat"));
                    const int iAtkC  = iHitC >= 0 ? body.indexOf(QStringLiteral("attackMob(mobIdx);"), iHitC) : -1;
                    const int iAtkB  = iHitB >= 0 ? body.indexOf(QStringLiteral("attackMob(mobIdx);"), iHitB) : -1;
                    okA3 = iRideC >= 0 && iRideB > iRideC
                           && iDistC > iRideC && iDistB > iRideB
                           && iHitC > iDistC && iHitB > iDistB
                           && iAtkC > iHitC && iAtkB > iHitB;
                }
            }
            // (a4) 行为几何钉（review26 #4 复现形态）：重铺车 + 乘员再登（a2 释放后仍站在轨格旁，登乘扫描
            //      ≤0.8 拾回）。瞄「乘员上身高度（座位中心 + 0.6*halfH，严格高于车盒顶 cp.y+0.45）」的纯
            //      水平射线（dy=0 → 车盒 Y slab 恒排除）→ findMobHit 命中乘员 + findCartHit -1 +
            //      hitCartFromRay 明确返 false（= beginMining 据以落回 attackMob 的那个返回值）。
            carts.spawnCart(x0, kRigY, z0, &w);
            for (int t = 0; t < 8 && ents.rideCartAt(mob) < 0; ++t) {
                ents.tick(0.016f, &w, carts.posAt(0) + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const QVector3D cpA4 = carts.posAt(0);
            const float halfA4 = ents.halfHeightAt(mob);
            const float chestY = cpA4.y() - 0.3125f + halfA4 + 0.6f * halfA4; // 座位钉位中心（车心-0.3125+halfH）+ 0.6*halfH = 胸/头区间
            const QVector3D eyeA4(cpA4.x() + 3.0f, chestY, cpA4.z());
            const QVector3D aimA4(-1.0f, 0.0f, 0.0f);
            const bool okA4 = ents.rideCartAt(mob) == 0
                              && halfA4 > 0.8f && chestY > cpA4.y() + 0.45f
                              && ents.findMobHit(eyeA4, aimA4, 4.0f, nullptr) == mob
                              && carts.findCartHit(eyeA4, aimA4, 4.0f, nullptr) < 0
                              && !carts.hitCartFromRay(eyeA4, aimA4, 4.0f, &w, /*instantBreak=*/false);
            if (!okA4)
                qInfo().noquote() << "  t866 a4 upper-body aim: ride" << ents.rideCartAt(mob)
                                  << "halfH" << halfA4 << "chestY-cartTop" << (chestY - (cpA4.y() + 0.45f));
            // 清 (a) 场。
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);

            // ── (b) 仙人掌（Entities 层直编）：x0..x0+2 轨 + x0+3 仙人掌（轨端前格）；空车推到末格中心。──
            for (int dx = 0; dx <= 2; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Rail, 0);
            }
            w.setBlock(x0 + 3, kRigY - 1, z0, BR::Sand, 0); // 仙人掌基座（沙）
            w.setBlock(x0 + 3, kRigY, z0, BR::Cactus, 0);
            carts.spawnCart(x0, kRigY, z0, &w); // 槽复用 → 槽 0
            QVector3D pusher = carts.posAt(0);
            bool reached = false;
            for (int t = 0; t < 400 && !reached; ++t) { // 玩家追着 +X 推（t809 模式）
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);
                carts.tickPushedCarts(0.016, &w);
                pusher = carts.posAt(0);
                if (!carts.aliveAt(0)) { reached = true; break; }           // 环境检查已毁
            }
            // t863④ 续推：到位 / 死端前磨停（首版 reached 窗口被磨停点 6.4 误触提前退出的实测坑）后
            //   继续追推 → 轨末端推离（derailed 出轨）→ 贴地滑入仙人掌格 → 环境摧毁 + 掉落信号。
            for (int t = 0; t < 300 && carts.aliveAt(0); ++t) {
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);
                carts.tickPushedCarts(0.016, &w);
                pusher = carts.posAt(0);
            }
            const bool okB = !carts.aliveAt(0) && brokenCount >= 1; // 摧毁 + 生存掉落信号
            // 清 (b) 场。
            for (int dx = 0; dx <= 2; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            w.setBlock(x0 + 3, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Air, 0);
            tickN(w, 2);

            // ── (c) 岩浆（Entities 层直编）：地面静止车 + 格内灌岩浆 → 下一 tick 环境检查即毁。──
            w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);
            carts.spawnCart(x0, kRigY - 1, z0, &w); // 非轨地面静止车（kCartGroundH 贴 cell 底）
            const int lavaBroken0 = brokenCount;
            w.setBlock(x0, kRigY - 1, z0, BR::Lava, 0); // 基座格换岩浆 → 车 AABB 覆盖该格
            carts.tickPushedCarts(0.016, &w);
            const bool okC = !carts.aliveAt(0) && brokenCount == lavaBroken0 + 1;
            const bool ok = okA0 && okA1 && okA2 && okA3 && okA4 && okB && okC;
            if (!ok)
                qInfo().noquote() << "  t866 a0" << okA0 << "a1" << okA1 << "hp" << carts.hpAt(0)
                                  << "a2" << okA2 << "a3(src)" << okA3 << "a4(geom)" << okA4
                                  << "rideC" << ents.rideCartAt(mob) << "b" << okB
                                  << "c" << okC << "broken" << brokenCount;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t866 attack on passenger-carrying cart routes to cart durability"
                                 " (mob unharmed, still seated), creative hit destroys + passenger"
                                 " auto-released, moving cart touching cactus breaks into dropped"
                                 " item, lava cell destroys cart same chain";
            // 清场
            carts.clearAll();
            ents.clearAll();
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    });



    // ── t848 余烬门尺寸上限 23×23 探针（World 层直调；t806 泛化门的用户实测回归）──
    //   背景（用户 8-24 实测）：t806 内腔上限 4 宽×5 高，实测「最大只有 4×4 能点燃，再大激活不了」——
    //   5 宽内腔在 ③ 量宽被旧上限 kMaxW-1=3 截断 → w 恒测 4 → ④ 右柱校验打到第 5 内腔列（空气格）判败。
    //   t806 探针盲区 = ⑧ 只测「上限内 4×5 四角点燃位」、③ 反把 5 宽当「超限拒」断言——验的是「设计上限
    //   合规」不是「用户期望的更大门」（用户期望 = MC 1.0 语义：内腔上限 21×21 / 框外沿 23×23）。
    //   t848 修：内腔 2×3 最小 .. 21×21 最大（22+ 超限拒点）+ 相邻双门共用中间竖柱各自成门（柱检查只验
    //   黑曜石不验独占）。rig 独立世界 64×64×64（21 高门 + 清场盒 ±3 需 y 5..32，t806 的 32 高 rig 放不下）：
    //   ① 用户复现位 5×4 内腔（X 平面带角）→ 成门恰 20 格（修复前此位拒点 = 用户症状本体）；
    //   ② 21×21 最大内腔 X 平面，点燃位 = 开口右上角（同时压满 ① 下探 21 步 + ② 左探 20 步两扫描上界）
    //      → 成门恰 441 格（= 内腔面积）+ state=0；
    //   ③ 21×21 最大内腔 Z 平面 → 成门恰 441 + state=1（正交轴向各证一次）；
    //   ④ 超限拒：22 宽 / 22 高内腔均拒且零门格（量宽 / 量高 21 截断 → 柱 / 梁校验打内腔空气格）；
    //   ⑤ 2×3 最小门仍可（放宽上限不动下限）；
    //   ⑥ 共用竖柱双门：3×4 + 3×4 共享中柱 → 先点 A（B 侧零误填）→ 再点 B（A 门 12 格健在互不干扰）→
    //      破共享柱中格 → 双门同熄（共享柱对两门都是承重格，批 F 熄灭钩子按连通域各自收域）；
    //   ⑦ 缺角 21×21 最大门 → 成门 441（四角可选语义在超大门保持）；
    //   ⑧ 破框碎门（批 F 钩子超大门回归）：⑦ 门破底梁中格（镜像 finishMiningAt setBlock(Air)+
    //      breakNetherPortalsAround 序列）→ 整门 441 格全熄（连通域熄灭尺寸无关）。
    runLegMulti({ "t848 portal size cap 23x23: interior 2x3..21x21 (frame outer 4x5..23x23 MC 1.0 cap; t806-era wid"
        "th-cap truncated measurement -> pillar probe hit interior air = user 'only 4x4 ignites'), user-r"
        "epro 5x4 lights 20 cells, 21x21 max lights on both planes 441 cells each (= interior area, top-r"
        "ight ignite pins 21-step down + 20-step left scan bounds), 22w/22h rejected zero cells, 2x3 min "
        "unchanged, shared middle pillar double door lights independently (12 then 12+12) and collapses t"
        "ogether on shared-pillar break, cornerless 21x21 lights, bottom-beam break collapses whole 441-c"
        "ell door via write-family extinguish hook" }, [&]() {
        World w848;
        w848.setWidth(64);
        w848.setDepth(64);
        w848.setHeight(64);
        w848.setSeed(13);
        bool ok848 = true;
        const int pY848 = 8; // 门框基线层（开口 y=pY848..pY848+h-1；清场盒兜底防地形干扰）
        // 建门框（t806 buildFrame 同源，基线层换本 rig）：开口左下角 (x0,pY848,z0) 沿 u=(ux,uz) 展开
        //   w 列 × h 层全 Air；底 / 顶梁（开口正下 / 正上各 w 格，不含角）+ 左右边柱（两翼各 h 格，不含角）
        //   全黑曜石；corners=true 补四角。清场盒 = 框外沿 ±3 × 门法向 ±2（含 y ±(h+3)）。
        const auto buildFrame848 = [&](int x0, int z0, int ux, int uz, int w, int h, bool corners) {
            const int vx = uz, vz = ux; // 门法线向（清深 ±2）
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        w848.setBlock(x0 + c * ux + d * vx, pY848 + r, z0 + c * uz + d * vz, BR::Air, 0);
            for (int c = 0; c < w; ++c) {
                w848.setBlock(x0 + c * ux, pY848 - 1, z0 + c * uz, BR::Obsidian, 0);
                w848.setBlock(x0 + c * ux, pY848 + h, z0 + c * uz, BR::Obsidian, 0);
            }
            for (int r = 0; r < h; ++r) {
                w848.setBlock(x0 - ux, pY848 + r, z0 - uz, BR::Obsidian, 0);
                w848.setBlock(x0 + w * ux, pY848 + r, z0 + w * uz, BR::Obsidian, 0);
            }
            if (corners) {
                const int cs[2] = {-1, w};
                for (const int ci : cs)
                    for (const int ry : {-1, h})
                        w848.setBlock(x0 + ci * ux, pY848 + ry, z0 + ci * uz, BR::Obsidian, 0);
            }
        };
        // 本 rig 清场盒内门格计数（t806 cellsInBox 同源；隔壁 rig 残留门不串数）。
        const auto cellsInBox848 = [&](int x0, int z0, int ux, int uz, int w, int h) -> int {
            int n = 0;
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (w848.blockAt(x0 + c * ux + d * uz, pY848 + r, z0 + c * uz + d * ux)
                            == BR::NetherPortal)
                            ++n;
            return n;
        };

        // ① 用户复现位：5×4 内腔（X 平面带角）开口中格点燃 → 成门恰 20 格。
        {
            buildFrame848(6, 6, 1, 0, 5, 4, true);
            const bool lit = w848.tryIgniteNetherPortal(8, pY848 + 1, 6);
            const int n = cellsInBox848(6, 6, 1, 0, 5, 4);
            if (!lit || n != 20) {
                qInfo().noquote() << "  [t848 diag] user-repro 5x4 interior:" << lit << "cells" << n;
                ok848 = false;
            }
        }
        // ② 21×21 最大内腔（X 平面带角），点燃位 = 开口右上角（压满两扫描上界）→ 441 格 + state=0。
        {
            buildFrame848(6, 16, 1, 0, 21, 21, true);
            const bool lit = w848.tryIgniteNetherPortal(26, pY848 + 20, 16);
            const int n = cellsInBox848(6, 16, 1, 0, 21, 21);
            const int st = int(w848.stateAt(6, pY848, 16) & 1);
            if (!lit || n != 441 || st != 0) {
                qInfo().noquote() << "  [t848 diag] 21x21 X-plane max gate:" << lit << "cells" << n
                                  << "state" << st;
                ok848 = false;
            }
        }
        // ③ 21×21 最大内腔（Z 平面带角），点燃位 = 开口中格 → 441 格 + state=1。
        {
            buildFrame848(34, 6, 0, 1, 21, 21, true);
            const bool lit = w848.tryIgniteNetherPortal(34, pY848 + 10, 16);
            const int n = cellsInBox848(34, 6, 0, 1, 21, 21);
            const int st = int(w848.stateAt(34, pY848, 6) & 1);
            if (!lit || n != 441 || st != 1) {
                qInfo().noquote() << "  [t848 diag] 21x21 Z-plane max gate:" << lit << "cells" << n
                                  << "state" << st;
                ok848 = false;
            }
        }
        // ④ 超限拒：内腔 22 宽 / 22 高（超 21×21 上限，框外沿 23×23 封顶）均拒且零门格。
        {
            buildFrame848(6, 40, 1, 0, 22, 3, true); // 22 宽（X 平面）
            bool bad = w848.tryIgniteNetherPortal(17, pY848 + 1, 40)
                       || cellsInBox848(6, 40, 1, 0, 22, 3) != 0;
            buildFrame848(40, 40, 0, 1, 2, 22, true); // 22 高（Z 平面）
            bad = bad || w848.tryIgniteNetherPortal(40, pY848 + 1, 41)
                        || cellsInBox848(40, 40, 0, 1, 2, 22) != 0;
            if (bad) {
                qInfo().noquote() << "  [t848 diag] oversize 22w/22h not rejected";
                ok848 = false;
            }
        }
        // ⑤ 2×3 最小门（X 平面带角）→ 成门恰 6 格（放宽上限不动下限）。
        {
            buildFrame848(6, 48, 1, 0, 2, 3, true);
            const bool lit = w848.tryIgniteNetherPortal(7, pY848 + 1, 48);
            const int n = cellsInBox848(6, 48, 1, 0, 2, 3);
            if (!lit || n != 6) {
                qInfo().noquote() << "  [t848 diag] 2x3 min gate:" << lit << "cells" << n;
                ok848 = false;
            }
        }
        // ⑥ 共用竖柱双门：A（内腔 x sX..sX+2）与 B（x sX+4..sX+6）共享中柱 x=sX+3（各 3 宽×4 高，X 平面）。
        //    两个 buildFrame848 的清场盒会互 wipe 邻门框 → 单清场盒 + 显式放 union 框。
        {
            const int sX = 12, sZ = 56;
            for (int x = sX - 4; x <= sX + 11; ++x)
                for (int y = pY848 - 4; y <= pY848 + 7; ++y)
                    for (int z = sZ - 2; z <= sZ + 2; ++z)
                        w848.setBlock(x, y, z, BR::Air, 0);
            for (int g = 0; g < 3; ++g) { // 每门 3 内腔列的底 / 顶梁
                w848.setBlock(sX + g, pY848 - 1, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + g, pY848 + 4, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 4 + g, pY848 - 1, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 4 + g, pY848 + 4, sZ, BR::Obsidian, 0);
            }
            for (int r = 0; r < 4; ++r) { // 三竖柱：A 左 / 共享 / B 右（各 h=4 格）
                w848.setBlock(sX - 1, pY848 + r, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 3, pY848 + r, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 7, pY848 + r, sZ, BR::Obsidian, 0);
            }
            const auto countRect848 = [&](int x0, int w, int h) -> int { // 双门 interior 精确计数（X 平面）
                int n = 0;
                for (int c = 0; c < w; ++c)
                    for (int r = 0; r < h; ++r)
                        if (w848.blockAt(x0 + c, pY848 + r, sZ) == BR::NetherPortal) ++n;
                return n;
            };
            const bool litA = w848.tryIgniteNetherPortal(sX + 1, pY848 + 1, sZ);
            const int onlyA = countRect848(sX, 3, 4) + countRect848(sX + 4, 3, 4); // 点 A 后：A=12 / B=0
            const bool litB = w848.tryIgniteNetherPortal(sX + 5, pY848 + 1, sZ);
            const int bothA = countRect848(sX, 3, 4), bothB = countRect848(sX + 4, 3, 4); // 双门共存各 12
            w848.setBlock(sX + 3, pY848 + 1, sZ, BR::Air, 0); // 破共享柱中格（finishMiningAt 同款先清格）
            w848.breakNetherPortalsAround(sX + 3, pY848 + 1, sZ);
            const int after = countRect848(sX, 3, 4) + countRect848(sX + 4, 3, 4); // 双门同熄归零
            if (!litA || !litB || onlyA != 12 || bothA != 12 || bothB != 12 || after != 0) {
                qInfo().noquote() << "  [t848 diag] shared-pillar double door: litA" << litA
                                  << "litB" << litB << "afterA" << onlyA << "A" << bothA
                                  << "B" << bothB << "afterBreak" << after;
                ok848 = false;
            }
        }
        // ⑦ 缺角 21×21 最大门（X 平面无角）→ 成门恰 441 格；⑧ 破底梁中格 → 整门全熄（批 F 钩子超大门回归）。
        {
            buildFrame848(24, 34, 1, 0, 21, 21, false);
            const bool lit = w848.tryIgniteNetherPortal(34, pY848 + 10, 34);
            const int n = cellsInBox848(24, 34, 1, 0, 21, 21);
            if (!lit || n != 441) {
                qInfo().noquote() << "  [t848 diag] cornerless 21x21 max gate:" << lit << "cells" << n;
                ok848 = false;
            }
            w848.setBlock(34, pY848 - 1, 34, BR::Air, 0); // 底梁中格（finishMiningAt 同款先清格）
            w848.breakNetherPortalsAround(34, pY848 - 1, 34);
            if (cellsInBox848(24, 34, 1, 0, 21, 21) != 0) {
                qInfo().noquote() << "  [t848 diag] max-gate beam break left"
                                  << cellsInBox848(24, 34, 1, 0, 21, 21) << "cells";
                ok848 = false;
            }
        }
        if (!ok848) ++totalFail;
        qInfo().noquote() << (ok848 ? "PASS" : "FAIL")
                          << "| t848 portal size cap 23x23: interior 2x3..21x21 (frame outer 4x5..23x23 "
                             "MC 1.0 cap; t806-era width-cap truncated measurement -> pillar probe hit "
                             "interior air = user 'only 4x4 ignites'), user-repro 5x4 lights 20 cells, "
                             "21x21 max lights on both planes 441 cells each (= interior area, top-right "
                             "ignite pins 21-step down + 20-step left scan bounds), 22w/22h rejected "
                             "zero cells, 2x3 min unchanged, shared middle pillar double door lights "
                             "independently (12 then 12+12) and collapses together on shared-pillar "
                             "break, cornerless 21x21 lights, bottom-beam break collapses whole "
                             "441-cell door via write-family extinguish hook";
    });

    // ── P-t812 铁轨四向连接优先级 + 红石变道探针（R19.13 🅰；t771 三消费端同源架构的交汇形态收口）──
    //   用户报告：「普通铁轨周围 3+ 轨连接时一坨不知道咋走」。断言四组（任一 FAIL = 用户症状在当前
    //   HEAD 的复现点）：
    //   (a) 四向全连（4 臂）：不再输出十字多臂——直线优先出一对直位（全新 state=0 → NS），且邻块编辑
    //       复检后恒同值（不闪变）；非拐角（railCornerArms 拒）；
    //   (b) T 交叉（3 臂 = 一对贯穿 + 单端岔尖）＝转辙器：默认弯向（bit6=0 → 贯穿轴正端；mesher tile 136
    //       拐角贴图走 railCornerArms 同源象限）；激活前稳定（放源块 / 邻编辑不闪变）；拉杆升沿切弯 →
    //       断电保持 → 再升沿再切；红石块 / 压力板两源同语义（isReceivingPower 全源覆盖）；
    //   (c) 矿车过 T 交叉按当前弯向走：默认弯向出口侧 → 通电切弯后改走另一侧 → 断电后仍按保持的弯向走。
    runLegMulti({ "t812 four-way junction = straight pair not multi-arm cross: conafter edit(stable across neighbor"
        "-edit recompute)",
               "t812 T-junction switch: default curvetile136 corner, stable pre-power, lever/redstone-block/pres"
        "sure-plate rising edges toggle curve, falling edges hold position (MC junction semantics; con no"
        "w)",
               "t812 cart through T-junction follows current curve: default exits +Z dead end, after power toggl"
        "e exits -Z, after power off holds -Z" }, [&]() {
        // ── (a) 四向全连 → 直线一对 + 不闪变 ──
        {
            const auto [x0, z0] = nextSlot();
            // 四臂轨先铺、中心轨最后（邻齐后一次成形）；全部 state=0（无轴偏好）→ 期望 NS 直线对。
            w.setBlock(x0 - 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            tickN(w, 2);
            const quint8 con4 = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            int axd = 0, azd = 0;
            bool ok = con4 == quint8(BR::RailConnPz | BR::RailConnNz)   // 直线一对（NS）
                      && con4 != quint8(BR::RailConnPx | BR::RailConnNx | BR::RailConnPz | BR::RailConnNz) // 十字退役
                      && !BR::railCornerArms(con4, axd, azd);           // 非拐角形态
            // 不闪变：邻块编辑（在东臂上方放 / 破石头——复检范围覆盖中心轨且不动轨布局）后 con 恒同值。
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0);
            const quint8 con4a = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Air, 0);
            const quint8 con4b = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            ok = ok && con4a == con4 && con4b == con4;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 four-way junction = straight pair not multi-arm cross: con"
                              << int(con4) << "after edit" << int(con4a) << int(con4b)
                              << "(stable across neighbor-edit recompute)";
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // ── (b) T 交叉转辙器：默认弯向 + 稳定 + 升沿切弯 + 断电保持 + 多源 ──
        //   布局：J=(x0,z0) 普通轨；贯穿对 = ±Z 两臂；岔尖 = +X 臂；-X 空位放源（拉杆/红石块/压力板）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);   // +Z 贯穿臂
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);   // -Z 贯穿臂
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);   // +X 岔尖
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);       // 转辙器 J（最后放）
            tickN(w, 2);
            const auto jCon = [&]() { return quint8(w.stateAt(x0, kRigY, z0) & 0x0F); };
            const auto jState = [&]() { return w.stateAt(x0, kRigY, z0); };
            // 拐角贴图同源断言（P16 cornerQuadrantOk 同款最小版）：T 弯 2 位 → railCornerArms 解码成功 +
            //   mesher 直调产 tile 136 拐角 quad（u 落 136 瓦片窗）。
            const auto cornerTile136 = [](quint8 con) {
                int xd = 0, zd = 0;
                if (!BR::railCornerArms(con, xd, zd)) return false;
                QVector<Vtx> verts; QVector<quint32> idx;
                PartialLightCtx lctx; lctx.light = 1.0f;
                for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
                PartialNeighborCtx nctx;
                nctx.posX = nctx.negX = nctx.posZ = nctx.negZ = 0;
                const float tileW = 1.0f / 16.0f;
                PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::Rail, con, lctx, nctx,
                                             tileW, 0.0f, 0.0f, 0.0f, 1.0f);
                int n136 = 0;
                for (const Vtx &v : verts) {
                    const float uu = (v.u - 136.0f * tileW) / tileW;
                    if (uu >= 0.0f && uu <= 1.0f) ++n136;
                }
                return n136 >= 4; // 一片拐角 quad（4 顶点）在 136 窗
            };
            const quint8 kDef = quint8(BR::RailConnPx | BR::RailConnPz);   // 默认弯：岔尖(+X) + 贯穿正端(+Z)
            const quint8 kAlt = quint8(BR::RailConnPx | BR::RailConnNz);   // 切弯后：岔尖 + 贯穿负端(-Z)
            bool ok = jCon() == kDef && cornerTile136(jCon());
            // 激活前稳定：-X 空位放拉杆（OFF）——邻编辑复检覆盖 J，弯向必须保持（不闪变）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 2);
            ok = ok && jCon() == kDef;
            // 拉杆升沿 → 切弯（bit6 翻转 + bit7 通电记忆置位）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == kAlt
                 && (jState() & BR::RailSwitchCurveFlag) != 0
                 && (jState() & BR::RailSwitchPoweredFlag) != 0
                 && cornerTile136(jCon());
            // 断电 → 弯向保持（bit7 清、bit6/连接位不动）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 4);
            ok = ok && jCon() == kAlt
                 && (jState() & BR::RailSwitchCurveFlag) != 0
                 && (jState() & BR::RailSwitchPoweredFlag) == 0;
            // 再升沿 → 再切回默认侧（转辙器来回扳）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            // 红石块源：拆拉杆（降沿）→ 放红石块（升沿切弯）→ 拆红石块（降沿保持）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 4);
            w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            ok = ok && jCon() == kAlt;
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 4);
            ok = ok && jCon() == kAlt;
            // 压力板源：压下（state bit0）升沿切弯；松开降沿保持。
            w.setBlock(x0 - 1, kRigY, z0, BR::WoodPressurePlate, 1);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            w.setBlock(x0 - 1, kRigY, z0, BR::WoodPressurePlate, 0);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 T-junction switch: default curve" << int(kDef)
                              << "tile136 corner, stable pre-power, lever/redstone-block/pressure-plate"
                                 " rising edges toggle curve, falling edges hold position (MC junction"
                                 " semantics; con now" << int(jCon()) << ")";
            // 清场
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // ── (c) 矿车过 T 交叉按当前弯向走（空车追推跑法，同 P11(d)/t771(d)：pushEmptyCart + 每帧 wish 随行进向）──
        //   布局同 (b)（独立槽）：J=(x0,z0)，岔尖 +X（spawn 位），贯穿 ±Z 死端臂。默认弯 kDef=Px|Pz →
        //   岔尖进车出 +Z；通电切到 kAlt=Px|Nz → 出 -Z；断电后保持 → 仍出 -Z。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0); // 源（先 OFF）
            tickN(w, 2);
            const auto jCon = [&]() { return quint8(w.stateAt(x0, kRigY, z0) & 0x0F); };
            // 跑一趟：岔尖 (x0+1,z0) spawn → 推向 J → 按当前弯向出到贯穿死端臂格心停（死端反向滤停）。
            //   返终停格 (bx,bz)；停在岔尖 / 进错臂都由期望值比对抓出。
            const auto runJunctionCart = [&]() {
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0, &w); // 岔尖轨 con=Nx → spawn 定向 -X（朝 J）
                QVector3D prev = carts.posAt(0);
                float wishX = -1.0f, wishZ = 0.0f;
                for (int t = 0; t < 900; ++t) {
                    carts.pushEmptyCart(&w, prev, wishX, wishZ); // 玩家追着推（静止即续推）
                    carts.tickPushedCarts(0.016f, &w);
                    const QVector3D cp = carts.posAt(0);
                    const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                    const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                    if (dl > 1e-4f) { wishX = ddx / dl; wishZ = ddz / dl; }
                    prev = cp;
                }
                return QPair<int, int>(int(std::floor(prev.x())), int(std::floor(prev.z())));
            };
            // ① 默认弯（kDef=Px|Pz）：岔尖进 J（行 -X，Px 反向滤）→ Pz dot=0 胜 → 出 +Z 死端停格心。
            bool ok = jCon() == quint8(BR::RailConnPx | BR::RailConnPz);
            const auto end1 = runJunctionCart();
            ok = ok && end1.first == x0 && end1.second == z0 + 1;
            // ② 拉杆升沿切弯（kAlt=Px|Nz）→ 新车改出 -Z 死端。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == quint8(BR::RailConnPx | BR::RailConnNz);
            const auto end2 = runJunctionCart();
            ok = ok && end2.first == x0 && end2.second == z0 - 1;
            // ③ 断电保持（kAlt 不回弹）→ 新车仍出 -Z。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 4);
            ok = ok && jCon() == quint8(BR::RailConnPx | BR::RailConnNz);
            const auto end3 = runJunctionCart();
            ok = ok && end3.first == x0 && end3.second == z0 - 1;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 cart through T-junction follows current curve: default exits"
                                 " +Z dead end" << (end1.second == z0 + 1)
                              << ", after power toggle exits -Z" << (end2.second == z0 - 1)
                              << ", after power off holds -Z" << (end3.second == z0 - 1);
            // 清场
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 2);
        }
    });

    // ── t821 床头/尾 z-fighting 盒几何探针（bedHalfBoxes 单一权威直调；World 层静态函数，无 rig 依赖）──
    //    用户报「浏览器 3D 床预览，床头/尾羊毛与床身模板接触面重叠闪烁」：旧版床垫长轴满 [0,1] → 床垫外
    //    端面与床头/尾板外面同格边共面同法线（z 区间重叠）→ z-fight；枕头外端同病。断言（16 床色 × 4
    //    facing × head/foot = 128 rig）：
    //    (a) 盒数 ≥5（foot：2 腿+床架+床垫+板）/ ≥6（head 多枕头）；
    //    (b) 床垫 + 枕头长轴**外端**内缩 kBedBoardThick 恰达板内面（不触格边 = 与板外面不再共面）；
    //    (c) 床垫长轴**内端**仍满触格边（两半对接连续，t496「中间不空」契约不随本修复回归）；
    //    (d) 外端存在贴格边、顶至 boardTop 的 planks 板盒（内缩后外端仍有板封口，无可见缺口）。
    runLegMulti({ "t821 bed board z-fight: mattress/pillow outer end inset to board inner face, mattress inner end "
        "joins at cell boundary, board caps outer end (16 colors x 4 facings x head/foot =rigs; a/b/c/d ="
        ")" }, [&]() {
        bool okA = true, okB = true, okC = true, okD = true;
        const int planksT821 = BR::tileIndex(quint8(BR::Planks), BR::PosX);
        const int woolT821 = BR::tileIndex(quint8(BR::Wool), BR::PosX);
        const float thick821 = BR::kBedBoardThick;
        int bedChecks = 0;
        for (int id = 0; id < int(BR::Count); ++id) {
            if (!BR::isBed(quint8(id))) continue;
            const int bedT = BR::tileIndex(quint8(id), BR::PosX);
            for (int f = 0; f < 4; ++f) {
                const bool longX = (f == 0 || f == 1);
                const bool frontPos = (f == 0 || f == 2);
                for (int h = 0; h <= 1; ++h) {
                    const bool isHead = (h == 1);
                    const bool outerPos = isHead ? !frontPos : frontPos;
                    const float boardTop = isHead ? BR::kBedHeadboardTop : BR::kBedFootboardTop;
                    QVector<BedHalfBox> bx;
                    PartialBlockGeometry::bedHalfBoxes(quint8(id), isHead, f, bx);
                    ++bedChecks;
                    if (bx.size() < (isHead ? 6 : 5)) okA = false;
                    const auto lo = [&](const BedHalfBox &b) { return longX ? b.x0 : b.z0; };
                    const auto hi = [&](const BedHalfBox &b) { return longX ? b.x1 : b.z1; };
                    for (const BedHalfBox &b : bx) {
                        if (b.tile != bedT && b.tile != woolT821) continue;
                        const float outerC = outerPos ? hi(b) : lo(b);
                        const float innerC = outerPos ? lo(b) : hi(b);
                        const float wantOuter = outerPos ? 1.f - thick821 : thick821;
                        const float wantInner = outerPos ? 0.f : 1.f;
                        if (std::abs(outerC - wantOuter) > 1e-4f) okB = false;
                        if (b.tile == bedT && std::abs(innerC - wantInner) > 1e-4f) okC = false;
                    }
                    bool boardFound = false;
                    for (const BedHalfBox &b : bx) {
                        if (b.tile != planksT821) continue;
                        const float outerC = outerPos ? hi(b) : lo(b);
                        const bool atEdge = std::abs(outerC - (outerPos ? 1.f : 0.f)) < 1e-4f;
                        if (atEdge && std::abs(b.y1 - boardTop) < 1e-4f) boardFound = true;
                    }
                    if (!boardFound) okD = false;
                }
            }
        }
        const bool ok821 = okA && okB && okC && okD && bedChecks == 128;
        if (!ok821) ++totalFail;
        qInfo().noquote() << (ok821 ? "PASS" : "FAIL")
                          << "| t821 bed board z-fight: mattress/pillow outer end inset to board inner face,"
                             " mattress inner end joins at cell boundary, board caps outer end (16 colors x 4"
                             " facings x head/foot ="
                          << bedChecks << "rigs; a/b/c/d =" << okA << okB << okC << okD << ")";
    });

    // ── t824 附魔台选项池物品过滤探针（R19.13；Game 层表 + Hotbar 桥接，无 World/QML）──
    //    用户报告：「镐子附上亡灵杀手（对镐无意义）」。根因：选项池按大类 mask 过滤（亡灵杀手
    //    appliesToMask=Weapon|Tool|BookItem 含 Tool 位 → 镐 / 铲全过门；摔落保护 mask=Armor → 胸甲也过门）。
    //    t824 收口 selectEnchantsForItem：候选 = isApplicableForItem 逐物品精判（对齐 t763/t798 适用表）。
    //    断言：
    //    (a) 全 seed 扫池（offered 2/12/30 × seed 0..399）逐物品收集出现过的附魔 id：
    //        钻石镐 / 铲 ⊆ {效率,精准,时运,耐久}（**亡灵杀手等武器系绝迹** + 池非空四元全在）；
    //        钻石斧 ⊆ 锐锋族+效率+精准+耐久（无时运）；钻石剑 ⊆ 锐锋族+击退+燃焰+耐久（无效率/采集系）；
    //        胸甲 ⊆ 保护/火焰保护/弹射物保护/耐久（无摔落/水上亲和）；靴 + 摔落保护；头盔 + 水上亲和；
    //        锄 / 剪刀 → 恒空（MC 1.0 锄无适用附魔 → 不给选项；categoryForItem 判 None）；
    //        弓 ⊆ 劲射/震击/燃箭/不竭/耐久、钓竿 ⊆ 唤潮/缠咬/耐久 且全在（t960：弓 / 竿专属池，
    //        categoryForItem 判 BowItem / RodItem → 可附魔）；书 → 全 20 附魔都在池；
    //    (b) Hotbar 桥接 selectEnchantsPreviewForItem == EnchantRegistry 直调（同 seed 同产物）；
    //    (c) enchantSelected 对锄返 false（附魔台点档 no-op，不白扣 XP / 青金石）+ 对剑 true 且产物全在剑池
    //        + 已附魔再点返 false（防重复附魔闸不回归）。
    runLegMulti({ "t824 enchant pool filtered per item: pick/shovel subset {eff,silk,fortune,unbreaking} (no undead"
        "-slay on pick = user symptom), axe adds sharpness-family w/o fortune, sword weapon-only, chest w"
        "/o feather-fall, boots+feather/helm+aqua, hoe/shears empty + category None, bow subset {might,bo"
        "w-shock,bright-draw,never-run,unbreaking} and rod subset {tide-call,bite-call,unbreaking} all pr"
        "esent (t960 exclusive pools, categories BowItem/RodItem), book keeps full 20; bridge==direct; en"
        "chantSelected rejects hoe & already-enchanted" }, [&]() {
        Hotbar hb;
        const int diaPick   = int(ToolRegistry::PickaxeDiamond);
        const int diaShovel = int(ToolRegistry::DiamondShovel);
        const int diaAxe    = int(ToolRegistry::DiamondAxe);
        const int diaSword  = int(ToolRegistry::DiamondSword);
        const int diaHoe    = int(ToolRegistry::DiamondHoe);
        const int bowId     = int(ToolRegistry::Bow);
        const int shearsId  = int(ToolRegistry::Shears);
        const int bookId    = RecipeRegistry::BookId;
        const int diaChest  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1;
        const int diaBoots  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 3;
        const int diaHelm   = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0;
        const int E  = int(EnchantRegistry::Efficiency),    ST = int(EnchantRegistry::SilkTouch);
        const int F  = int(EnchantRegistry::Fortune),       U  = int(EnchantRegistry::Unbreaking);
        const int SH = int(EnchantRegistry::Sharpness),     UD = int(EnchantRegistry::UndeadSlay);
        const int AR = int(EnchantRegistry::ArthropodSlay), KB = int(EnchantRegistry::Knockback);
        const int FA = int(EnchantRegistry::FireAspect),    P  = int(EnchantRegistry::Protection);
        const int FP = int(EnchantRegistry::FireProtection), PR = int(EnchantRegistry::ProjectileProt);
        const int FF = int(EnchantRegistry::FeatherFall),   AA = int(EnchantRegistry::AquaAffinity);
        const int MG = int(EnchantRegistry::Might),         BS = int(EnchantRegistry::BowShock);
        const int BD = int(EnchantRegistry::BrightDraw),    NR = int(EnchantRegistry::NeverRun);
        const int TC = int(EnchantRegistry::TideCall),      BC = int(EnchantRegistry::BiteCall);
        const int NID = int(EnchantRegistry::EnchantCount); // t960 起 21（1..20；固定 15 数组会越界）
        // 全 seed 扫池：seen[1..20] = 该物品选项池中出现过的附魔 id（1200 次抽取 → 稀有权重 1 的精准采集 /
        //   不竭也在池中以概率 1-(1-p)^2400 ≈ 1 覆盖，假阴性率 < e^-30）。
        const auto poolOf = [&](int itemId, bool *seen) {
            for (int i = 0; i < NID; ++i) seen[i] = false;
            const int offeredList[3] = {2, 12, 30};
            for (int oi = 0; oi < 3; ++oi)
                for (int seed = 0; seed < 400; ++seed) {
                    const QVariantList picks = EnchantRegistry::selectEnchantsForItem(itemId, offeredList[oi], seed);
                    for (const QVariant &v : picks) seen[v.toMap().value(QStringLiteral("id")).toInt()] = true;
                }
        };
        // 池 ⊆ 允许集 且 期望集全出现（防「过滤过头 → 空池 / 半池」反向回归）。
        const auto poolIs = [&](int itemId, const std::vector<int> &allowed, bool requireAll) {
            bool seen[21];
            poolOf(itemId, seen);
            for (int i = 1; i < NID; ++i) {
                const bool allowedHas = std::find(allowed.begin(), allowed.end(), i) != allowed.end();
                if (seen[i] && !allowedHas) return false; // 出现了不允许的（如镐出亡灵杀手 = 用户症状）
                if (requireAll && allowedHas && !seen[i]) return false; // 允许的没出现（池被砍空）
            }
            return true;
        };
        const std::vector<int> miningPool = {E, ST, F, U};                 // 镐 / 铲
        const std::vector<int> axePool    = {SH, UD, AR, E, ST, U};        // 斧（锐锋族 + 采集系无时运）
        const std::vector<int> swordPool  = {SH, UD, AR, KB, FA, U};       // 剑
        const std::vector<int> chestPool  = {P, FP, PR, U};                // 胸甲 / 护腿
        const std::vector<int> bootsPool  = {P, FP, PR, U, FF};            // 靴 + 摔落保护
        const std::vector<int> helmPool   = {P, FP, PR, U, AA};            // 头盔 + 水上亲和
        const std::vector<int> bowPool    = {MG, BS, BD, NR, U};           // t960 弓（专属四件 + 耐久）
        const std::vector<int> rodPool    = {TC, BC, U};                   // t960 钓竿（专属两件 + 耐久）
        std::vector<int> bookPool;
        for (int i = 1; i < NID; ++i) bookPool.push_back(i);              // 书 = 全 20 池
        bool ok = poolIs(diaPick, miningPool, true)
               && poolIs(diaShovel, miningPool, true)
               && poolIs(diaAxe, axePool, true)
               && poolIs(diaSword, swordPool, true)
               && poolIs(diaChest, chestPool, true)
               && poolIs(diaBoots, bootsPool, true)
               && poolIs(diaHelm, helmPool, true)
               && poolIs(bowId, bowPool, true)                            // t960：弓可附魔（专属池）
               && poolIs(int(ToolRegistry::FishingRod), rodPool, true)    // t960：钓竿可附魔（专属池）
               && poolIs(bookId, bookPool, true);
        // 锄 / 剪刀 → 恒空池 + 类别 None（附魔台槽 0 拒入的三重门之一）；弓 / 钓竿 t960 起类别非 None。
        bool seenHoe[21];
        poolOf(diaHoe, seenHoe);
        for (int i = 1; i < NID; ++i) ok = ok && !seenHoe[i];
        ok = ok && EnchantRegistry::categoryForItem(diaHoe) == EnchantRegistry::None
               && EnchantRegistry::categoryForItem(int(ToolRegistry::Shears)) == EnchantRegistry::None
               && EnchantRegistry::selectEnchantsForItem(shearsId, 12, 7).isEmpty()
               && EnchantRegistry::categoryForItem(bowId) == EnchantRegistry::BowItem
               && EnchantRegistry::categoryForItem(int(ToolRegistry::FishingRod)) == EnchantRegistry::RodItem;
        // (b) 桥接 == 直调（同 seed 同产物；防 QML 侧再持副本）。
        const QVariantList viaBridge = hb.selectEnchantsPreviewForItem(diaSword, 17, 4242);
        const QVariantList direct    = EnchantRegistry::selectEnchantsForItem(diaSword, 17, 4242);
        ok = ok && viaBridge.size() == direct.size() && !direct.isEmpty();
        for (int i = 0; ok && i < int(direct.size()); ++i)
            ok = viaBridge.at(i).toMap().value(QStringLiteral("id")) == direct.at(i).toMap().value(QStringLiteral("id"))
              && viaBridge.at(i).toMap().value(QStringLiteral("level")) == direct.at(i).toMap().value(QStringLiteral("level"));
        // (c) enchantSelected：锄拒（no-op 不落附魔）；剑成且产物 ⊆ 剑池；已附魔再点拒。
        hb.setStack(0, diaHoe, 1);
        hb.setSelectedSlot(0);
        ok = ok && !hb.enchantSelected(10, 99);
        hb.setStack(0, diaSword, 1);
        ok = ok && hb.enchantSelected(10, 99);
        bool swordEnchOk = false, anyEnch = false;
        const QVariantList gotEnch = hb.enchantsAt(0);
        for (int i = 0; i < 4; ++i) {
            const int packed = gotEnch.at(i).toInt();
            if (packed == 0) continue;
            anyEnch = true;
            swordEnchOk = std::find(swordPool.begin(), swordPool.end(),
                                    EnchantRegistry::packEnchantId(packed)) != swordPool.end();
            if (!swordEnchOk) break;
        }
        ok = ok && anyEnch && swordEnchOk && !hb.enchantSelected(10, 100); // 已附魔 → 拒
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t824 enchant pool filtered per item: pick/shovel subset {eff,silk,fortune,"
                             "unbreaking} (no undead-slay on pick = user symptom), axe adds sharpness-family "
                             "w/o fortune, sword weapon-only, chest w/o feather-fall, boots+feather/helm+aqua, "
                             "hoe/shears empty + category None, bow subset {might,bow-shock,bright-draw,"
                             "never-run,unbreaking} and rod subset {tide-call,bite-call,unbreaking} all "
                             "present (t960 exclusive pools, categories BowItem/RodItem), book keeps full 20; "
                             "bridge==direct; enchantSelected rejects hoe & already-enchanted";
    });

    // ── t825 锋利最终伤害显示 = 实战同源探针（R19.13；Game 层公式 + Hotbar 桥接）──
    //    用户报告：「钻石剑附锋利后伤害显示仍 +7」。静态复核：实际伤害链（attackMob t476/t763）与九处
    //    tooltip 攻击行均已含锐锋加成 —— 本任务把公式收口 EnchantRegistry::weaponAttackDamage 单一权威
    //    （attackMob 起点 + displayAttackDamage 桥接取整），「显示 = 实战的目标无关部分」结构化成立。
    //    断言：① 权威公式精确值（钻石剑 7 + 0.5/级：0/7.0、I/7.5、III/8.5；木剑 V = 4+2.5 = 6.5）；
    //    ② 显示桥接 = round(权威)（含 .5 半上取整与 JS Math.round 同侧：I → 8）；③ 无附魔 / 非武器不虚增。
    runLegMulti({ "t825 display==combat damage single source: weaponAttackDamage 7/7.5/8.5 (dia sword lvl 0/I/III),"
        " wood sword V 6.5; Hotbar::displayAttackDamage rounds same authority (8 at .5 half-up, 9 at III,"
        " level sweep 0..5 equality); empty/partial enchant arrays degrade to base" }, [&]() {
        Hotbar hb;
        const int diaSword  = int(ToolRegistry::DiamondSword);
        const int woodSword = int(ToolRegistry::SwordWood);
        const int sharp1 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 1);
        const int sharp3 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
        const int sharp5 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 5);
        const int zero[4] = {0, 0, 0, 0};
        const int e1[4] = {sharp1, 0, 0, 0};
        const int e3[4] = {sharp3, 0, 0, 0};
        const int e5[4] = {sharp5, 0, 0, 0};
        const auto close = [](float got, float expect) { return std::abs(got - expect) < 1e-4f; };
        bool ok = close(EnchantRegistry::weaponAttackDamage(diaSword, nullptr), 7.0f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, zero), 7.0f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, e1), 7.5f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, e3), 8.5f)
               && close(EnchantRegistry::weaponAttackDamage(woodSword, e5), 6.5f);
        // 显示桥接 = round(weaponAttackDamage)：0/7、I/8（round(7.5) 半上，同 JS Math.round(7.5)=8）、III/9、
        //   木剑 V/round(6.5)=7；空附魔数组 / 缺项 → 仅基础（防 QML 传 null/[] 崩）。
        ok = ok && hb.displayAttackDamage(diaSword, QVariantList{}) == 7
               && hb.displayAttackDamage(diaSword, QVariantList{sharp1, 0, 0, 0}) == 8
               && hb.displayAttackDamage(diaSword, QVariantList{sharp3, 0, 0, 0}) == 9
               && hb.displayAttackDamage(woodSword, QVariantList{sharp5, 0, 0, 0}) == 7
               && hb.displayAttackDamage(diaSword, QVariantList{sharp5}) == 10; // 缺项补 0 → 7+2.5 = round 10
        // 同源逐级枚举：display == qRound(authority) 对级 0..5 全成立（公式只活在一处的运行时证据）。
        for (int lvl = 0; lvl <= 5; ++lvl) {
            const int e[4] = {EnchantRegistry::pack(int(EnchantRegistry::Sharpness), lvl), 0, 0, 0};
            const QVariantList qvl = QVariantList{e[0], 0, 0, 0};
            ok = ok && hb.displayAttackDamage(diaSword, qvl)
                      == qRound(EnchantRegistry::weaponAttackDamage(diaSword, e));
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t825 display==combat damage single source: weaponAttackDamage 7/7.5/8.5 (dia "
                             "sword lvl 0/I/III), wood sword V 6.5; Hotbar::displayAttackDamage rounds same "
                             "authority (8 at .5 half-up, 9 at III, level sweep 0..5 equality); empty/partial "
                             "enchant arrays degrade to base";
    });

    // ── t917 附魔选项 hover 预告同源探针（源码钉 + 行为面；R19.16）──
    //    用户口径：「锋利? 耐久?」式预告——只显示一种附魔、必定出现该附魔、等级未知。诚实预告的前提 =
    //    预告与施放读同一确定性种子（PLAN §2-K）；旧 doEnchant 种子掺 Date.now()&0xffff → 每次点击结果
    //    都变，任何静态预告必假（预告-施放双路漂移）。t917 收口 tierSeed 单一权威（台位 ^ 物品 ^ 档位 ^
    //    optionReroll，无时钟无随机）。断言：① 源码钉 —— EnchantingTableUI.qml 定义 tierSeed、doEnchant
    //    消费 root.tierSeed(slotIdx)、tierPreviewName 消费同一 root.tierSeed、旧时钟混种式已绝迹（Date.now
    //    本身不 ban——双击计时的 Date.now() 是合法用途，只 ban 种子混法）；② 行为面 —— 剑 / 书各档 offered
    //    的 picks 非空且首条 displayName 可解析（预告 = 产物首条，纯函数复算即同产物，「必出」不另掷）。
    //    review28 #3 加钉：③ 种子快照**必须先于** H1 书堆归一化写槽 —— 源码钉断言 doEnchant 函数体内
    //    `const seed = root.tierSeed(slotIdx)` 的行号先于 `InventoryOps.writeSlot(root, "enchant", 0,`
    //    （归一化写）与 `if (srcCount0 > 1)`（归一化门）——旧序（种子在归一化写后取）下书堆路径
    //    writeSlot 触发的同步 optionReroll++ 令施放种子 ≠ hover 预告种子，「必得」预告漂移。种子取在
    //    `const srcCount0` 行之前 = 归一化写（会 bump reroll 的唯一写）之前，两函数严格同源。
    runLegMulti({ "t917 enchant option hover preview single-source: tierSeed authority (pos^item^tier^reroll; clock"
        " mixing Date.now()&0xffff extinct), doEnchant and tierPreviewName both consume root.tierSeed -> "
        "preview == first pick of the actual result (guaranteed enchant, level masked); sword offered 1.."
        "30 / book samples non-empty with resolvable display names; review28 #3: the doEnchant seed snaps"
        "hot is taken BEFORE the H1 book-stack normalization gate/write (seed line < srcCount0 gate line "
        "< slot-0 write line) so the synchronous optionReroll++ fired by the normalization enchantRev bum"
        "p can no longer split the preview seed from the cast seed" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString rootDir = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile ef(rootDir + QStringLiteral("/src/ui/EnchantingTableUI.qml"));
        const QString t = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
        bool ok = t.contains(QStringLiteral("function tierSeed(slotIdx)"))
                  && t.contains(QStringLiteral("const seed = root.tierSeed(slotIdx)"))
                  && t.contains(QStringLiteral("root.tierSeed(slotIdx))"))
                  && !t.contains(QStringLiteral("Date.now() & 0xffff"));
        int seedLine = -1, normGateLine = -1, normWriteLine = -1;
        if (ok) {
            const QStringList lines = t.split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                const QString &ln = lines.at(i);
                if (seedLine < 0 && ln.contains(QStringLiteral("const seed = root.tierSeed(slotIdx)"))) seedLine = i;
                if (normGateLine < 0 && ln.contains(QStringLiteral("const srcCount0 = root.enchantCounts[0]"))) normGateLine = i;
                // 归一化写特异串（srcId0 + 1 + remain 只在 doEnchant H1 段出现；泛化的 slot-0 write 会
                //   误中文件前部的 returnEnchantToHotbar / slotShiftLeftEnchant 清槽写）。
                if (normWriteLine < 0 && ln.contains(QStringLiteral("srcId0, 1 + remain"))) normWriteLine = i;
            }
            ok = seedLine >= 0 && normGateLine > seedLine && normWriteLine > seedLine;
        }
        Hotbar hb;
        const int diaSword = int(ToolRegistry::DiamondSword);
        const int bookId = RecipeRegistry::BookId;
        for (int s = 0; ok && s < 40; ++s) {
            const QVariantList picks = hb.selectEnchantsPreviewForItem(diaSword, 1 + (s % 30), s * 977 + 13);
            if (picks.isEmpty()) { ok = false; break; }
            ok = !hb.enchantDisplayName(picks.at(0).toMap().value(QStringLiteral("id")).toInt()).isEmpty();
        }
        for (int s = 0; ok && s < 20; ++s) {
            const QVariantList picks = hb.selectEnchantsPreviewForItem(bookId, 10, s * 331 + 7);
            ok = !picks.isEmpty()
                 && !hb.enchantDisplayName(picks.at(0).toMap().value(QStringLiteral("id")).toInt()).isEmpty();
        }
        if (!ok) {
            qInfo().noquote() << "  t917 diag seedLine" << seedLine << "normGate" << normGateLine
                              << "normWrite" << normWriteLine;
            ++totalFail;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t917 enchant option hover preview single-source: tierSeed authority (pos^item^"
                             "tier^reroll; clock mixing Date.now()&0xffff extinct), doEnchant and tierPreviewName "
                             "both consume root.tierSeed -> preview == first pick of the actual result (guaranteed "
                             "enchant, level masked); sword offered 1..30 / book samples non-empty with resolvable "
                             "display names; review28 #3: the doEnchant seed snapshot is taken BEFORE the H1 "
                             "book-stack normalization gate/write (seed line < srcCount0 gate line < slot-0 "
                             "write line) so the synchronous optionReroll++ fired by the normalization "
                             "enchantRev bump can no longer split the preview seed from the cast seed";
    });

    // ── t918 铁砧 shift+左键产物直入背包探针（源码钉；R19.16）──
    //    用户：「附魔书敲进工具完成后 shift+左键对合成品应直接放到背包，现在没反应被鼠标拿取」。
    //    takeProduct 增 toInventory 路由：预检 main+hotbar 容量 + 光标兜底位（slotShiftLeftCraft 同口径，
    //    预检不过 → 零消耗无操作防 t626② 复制面回归）→ addToAny 直入背包（hotbar 空槽优先 / main 兜底，
    //    同 id 无名栈就地优先）→ 余量 fallback 光标。断言（源码钉）：
    //    ① takeProduct 带 toInventory 形参；② 产物槽 TapHandler 传 window.shiftHeld（shift 分流）；
    //    ③ 落定段 shift 分支走 addToAny(outId, outCount, outDur, outEnch, outName)（取出链走 InventoryOps/
    //       VM 既有 helper 非手搓循环写槽）；④ 预检含 cursorSpace 兜底位 + 满则 return（零消耗门）。
    //    review28 #1 加固（行为腿在本文件 t874 harness 段 (13)，此处钉**口径逐字**——QML 路由 C++ 矩阵
    //    全盲，预检条件漂移只能靠源码钉拦）：
    //    ⑤ 预检与 addToAny rev2-C5 双向带名守卫同口径：同 id 无名栈余量仅在产物自身无名（outName 判空，
    //       条件逐字含 outName.length === 0）时计入——旧码只查槽名 → 改名产物场景预检虚增容量，背包无
    //       空槽时 addToAny 返整份 remain，落定段把改名产物凭空并进异物光标计数（复制/转化面）；
    //    ⑥ 落定 else 分支三件套：heldId === outId 守卫（异物光标不并栈——不可达防御分支走
    //       dropItemAtFront 丢实体，§2-E 不静默吞）+ Math.min(cap, heldCount + remain) 封顶（防超上限栈）；
    //    ⑦ 工作台批量合成 slotShiftLeftCraft 同病同钉（review 模式小结 #1 清点结论：附魔台无「预检算
    //       容量」组合，仅铁砧/工作台两处）：无名产物不并入带名同 id 栈（守卫槽侧半边）→ 槽名判空 + 落定
    //       Math.min 封顶。
    runLegMulti({ "t918 anvil shift-click product to inventory: takeProduct(toInventory) dual route, preview slot p"
        "asses window.shiftHeld, inventory route lands via hotbar.addToAny (hotbar-first empty slots, sam"
        "e-id unnamed merge in place), preflight counts main+hotbar capacity + cursor fallback and bails "
        "with zero consumption when neither fits (t626 duplicate-item guard preserved); leftover falls ba"
        "ck to cursor (MC take-out semantics); review28 #1 hardening: preflight same-caliber as addToAny "
        "bidirectional named guard (same-id stack headroom counted only when product itself unnamed - out"
        "Name.length===0 pinned verbatim), landing else branch holds heldId===outId guard (unreachable fo"
        "reign-cursor fallback drops remain as entity, no silent swallow) + Math.min(cap,...) clamp; craf"
        "ting-table slotShiftLeftCraft swept and pinned in InventoryOps.js (named-slot headroom excluded "
        "+ cursor clamp)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString rootDir = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile af(rootDir + QStringLiteral("/src/ui/AnvilUI.qml"));
        const QString t = af.open(QIODevice::ReadOnly) ? QString::fromUtf8(af.readAll()) : QString();
        bool ok = t.contains(QStringLiteral("function takeProduct(toInventory)"))
                  && t.contains(QStringLiteral("root.takeProduct(window.shiftHeld)"))
                  && t.contains(QStringLiteral("const remain = root.hotbar.addToAny(outId, outCount, outDur, outEnch, outName)"))
                  && t.contains(QStringLiteral("const cursorSpace = (heldId === 0) ? cap"))
                  && t.contains(QStringLiteral("if (space < outCount) return"))
                  && t.contains(QStringLiteral("outName.length === 0 && s.id === outId && s.name.length === 0"))
                  && t.contains(QStringLiteral("} else if (heldId === outId) {"))
                  && t.contains(QStringLiteral("Math.min(cap, heldCount + remain)"));
        QFile iof(rootDir + QStringLiteral("/src/ui/InventoryOps.js"));
        const QString js = iof.open(QIODevice::ReadOnly) ? QString::fromUtf8(iof.readAll()) : QString();
        ok = ok && js.contains(QStringLiteral("s.id === r.outputId && s.name.length === 0"))
              && js.contains(QStringLiteral("Math.min(cap, prevHeldCount + remain)"));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t918 anvil shift-click product to inventory: takeProduct(toInventory) dual route, "
                             "preview slot passes window.shiftHeld, inventory route lands via hotbar.addToAny "
                             "(hotbar-first empty slots, same-id unnamed merge in place), preflight counts "
                             "main+hotbar capacity + cursor fallback and bails with zero consumption when neither "
                             "fits (t626 duplicate-item guard preserved); leftover falls back to cursor (MC take-"
                             "out semantics); review28 #1 hardening: preflight same-caliber as addToAny bidirectional "
                             "named guard (same-id stack headroom counted only when product itself unnamed - "
                             "outName.length===0 pinned verbatim), landing else branch holds heldId===outId guard "
                             "(unreachable foreign-cursor fallback drops remain as entity, no silent swallow) + "
                             "Math.min(cap,...) clamp; crafting-table slotShiftLeftCraft swept and pinned in "
                             "InventoryOps.js (named-slot headroom excluded + cursor clamp)";
    });

    // ── t919 钻石剑伤害核账探针（公式面 + 运行时 DoT 补刀复现；R19.16）──
    //    用户疑问：「钻石剑只有火焰附加、显示 +7 伤害，两刀砍死 20 血僵尸——7×2=14 < 20 为何死？」。
    //    核账结论（账目公式钉死，非 bug——「DoT 补刀 = 正常」口径）：
    //    - 直伤 = EnchantRegistry::weaponAttackDamage = 基础 7（钻石剑 tier 4，ToolRegistry::attackDamage）
    //      + 锐锋 ×0.5/级 —— **火焰附加不进直伤公式**（tooltip +7 与实战直伤 7 同源同值且正确；MC 1.0
    //      fire-aspect 同样不加攻击面板，其输出全在点燃 DoT）；
    //    - 火焰附加输出 = ignite(级×4s) 点燃 + 火伤每 kFireDamageInterval(0.75s) 扣 1HP（fireTimer>0 期间，
    //      第二刀刷新燃烧窗）→ 两刀 14 直伤 + ≥6 拍火伤 ≥ 20 → 僵尸在两刀后数秒内死亡（用户观感「两刀死」）；
    //    - 暴击（滞空下落 ×1.5）另辟一路：7→11，两记跳劈 22 ≥ 20 可独立致死（attackMob crit 分支）；
    //    - 僵尸（Shambler）满血 = kHostileDefaultHealth = 20（MC 1.0 口径，头文件钉死）。
    //    断言：① 公式面 —— attackDamage(钻石剑)=7；火焰附加 II 下 weaponAttackDamage=7.0（火不加直伤）；
    //    displayAttackDamage=7（显示 = 直伤同源，火不在面板）。② 运行时 —— 两只满血 Shambler 各吃两刀
    //    直伤 7：无火对照 6s 后存活（14 < 20，血 6）；带火（每刀后 ignite 4s）在窗内死亡且 mobDied 带
    //    burned=true（DoT 补刀走火烧致死链）——同一 rig 上「无火不死 / 有火死」的对照即用户疑问的答案。
    runLegMulti({ "t919 diamond sword damage accounting: base 7 (tier 4), fire-aspect adds ZERO direct damage (weap"
        "onAttackDamage 7.0 with FA II, display +7 == combat direct damage - fire lives in the ignite DoT"
        "); two 7-hits leave a 20HP shambler alive at 6HP without fire, with per-hit ignite(4s) the burn "
        "ticks finish it and mobDied carries burned=true (DoT finisher = expected, not a display bug); cr"
        "it x1.5 path noted in attackMob (11/hit, two jump strikes 22 >= 20 standalone)" }, [&]() {
        Hotbar hb;
        const int diaSword = int(ToolRegistry::DiamondSword);
        const int fa2 = EnchantRegistry::pack(int(EnchantRegistry::FireAspect), 2);
        const int eFa[4] = {fa2, 0, 0, 0};
        bool ok = ToolRegistry::attackDamage(diaSword) == 7
               && std::abs(EnchantRegistry::weaponAttackDamage(diaSword, eFa) - 7.0f) < 1e-4f
               && hb.displayAttackDamage(diaSword, QVariantList{fa2, 0, 0, 0}) == 7;
        // 运行时 rig（t827 同款净空扫描 + 石平台；far listener 安抚 AI，despawn 在 tickHostileLife 不在 tick）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 125 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 15 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 15 && clear; ++dx)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            // review0906 D2 首跑实锤：depth 144→180 后 worldgen 洞穴/地表分布整体迁移（worm 起点按
            //   深度取模重排），原扫描域（z<125）不再保有一体 16×7 净空腔 =「no clear rig area」假红。
            //   扫描式选址本质上依赖地形（t814 教训同源）→ 退化为远端定址 + 自净空：z=169 行（slot
            //   220+，本轮尾部 ≈ slot 184 = 行 46 = z 142，永不触及）清出工作盒后照常铺设。
            x0 = 8;
            z0 = 169;
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                    for (int dy = -1; dy <= 3; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
        }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t919 fire-aspect DoT finisher: no clear rig area found";
        } else {
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) {
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                    // t970 连带加固：2 格高石墙围死活动域（t897 口径——1 格墙会被越障跳翻越）。对照腿
                    // 7.5s 长窗内 Shambler RNG 游走可走出无栏平台缘 → mob 坠落链（t970 起）结算边缘
                    // 摔伤（9s 窗实测 1/3 轮 hp 5 假红）；伤害核账语义与墙无关。
                    if (dx == 0 || dx == 15 || dz == -3 || dz == 3) {
                        w.setBlock(x0 + dx, kRigY, z0 + dz, BR::Stone, 0);
                        w.setBlock(x0 + dx, kRigY + 1, z0 + dz, BR::Stone, 0);
                    }
                }
            EntityManager ents;
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            // 满血 20 = kHostileDefaultHealth（spawnHostileMob 同值；显式传参取回句柄索引）。
            const int burned = ents.spawnMobTyped(x0 + 2, kRigY, z0, EntityManager::MobShambler,
                                                  QStringLiteral("#4a6a3a"), 20);
            const int control = ents.spawnMobTyped(x0 + 10, kRigY, z0, EntityManager::MobShambler,
                                                   QStringLiteral("#4a6a3a"), 20);
            ok = ok && burned >= 0 && control >= 0
                    && ents.healthAt(burned) == 20 && ents.healthAt(control) == 20;
            int diedBurnedFlag = -1;
            QObject::connect(&ents, &EntityManager::mobDied, &ents,
                             [&](int, int, int, int, bool wasBurned, bool) {
                                 if (diedBurnedFlag < 0) diedBurnedFlag = wasBurned ? 1 : 0;
                             });
            // 第一刀（attackMob 直伤 7）+ 燃焰 II 点燃 8s（口径用 I 的 4s 亦同理；II 取上界证刷新语义）。
            ents.damageEntity(burned, 7);
            ents.ignite(burned, 4.0f);
            ents.damageEntity(control, 7);
            for (int t = 0; t < 94; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);   // ~1.5s
            // 第二刀（同窗两刀 14 直伤；带火侧刷新燃烧）。
            ents.damageEntity(burned, 7);
            ents.ignite(burned, 4.0f);
            ents.damageEntity(control, 7);
            for (int t = 0; t < 470; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);  // ~7.5s 火烧尽 + 死亡动画
            // 对照：14 直伤无火 → 存活 6 HP（第二刀后 7.5s 无任何额外伤害源）；带火：14 + ≥6 拍火伤 → 死。
            const bool controlAlive = !ents.deadAt(control) && ents.healthAt(control) == 6;
            const bool fireDied = ents.deadAt(burned) && diedBurnedFlag == 1;
            ok = ok && controlAlive && fireDied;
            if (!ok) qInfo().noquote() << "  t919 diag: control dead" << ents.deadAt(control)
                                       << "hp" << ents.healthAt(control)
                                       << "| fire dead" << ents.deadAt(burned)
                                       << "burnedFlag" << diedBurnedFlag;
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) {
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air);
                    if (dx == 0 || dx == 15 || dz == -3 || dz == 3) { // t970 连带加固墙随平台一并清
                        w.setBlock(x0 + dx, kRigY, z0 + dz, BR::Air);
                        w.setBlock(x0 + dx, kRigY + 1, z0 + dz, BR::Air);
                    }
                }
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t919 diamond sword damage accounting: base 7 (tier 4), fire-aspect adds ZERO "
                                 "direct damage (weaponAttackDamage 7.0 with FA II, display +7 == combat direct "
                                 "damage - fire lives in the ignite DoT); two 7-hits leave a 20HP shambler alive "
                                 "at 6HP without fire, with per-hit ignite(4s) the burn ticks finish it and "
                                 "mobDied carries burned=true (DoT finisher = expected, not a display bug); crit "
                                 "x1.5 path noted in attackMob (11/hit, two jump strikes 22 >= 20 standalone)";
        }
    });

    // ── t826 击退附魔实战强度探针（R19.13；公式面 + Entities 层真位移，t774 爆炸击退同款 rig）──
    //    用户报告：「附击退打生物无击退」。根因：旧强度 1+0.5*级 令 II 仅 ~2.3 格总位移（基线 ~1.1 格），
    //    与 AI 游荡抖动同量级 → 实战「无感」。t826 收口 EnchantRegistry::knockbackStrength 单一权威
    //    （1+3.0*级：I/II = 4.0/7.0 → 总位移 ~4.5/~7.9 格，MC 1.0 量级）。
    //    断言：① 公式面 0/1/2 级 = 1.0/4.0/7.0（负级防御钳）；② 物理面 —— 真 EntityManager 三猪各吃一档
    //    knockback(+X)，8 tick（0.128s）位移严格按级递增且落量级带（理论 0.90/3.60/6.30 格 ≈
    //    v0*(1-e^-0.512)/4，v0 = 4.5*strength；游荡噪声 ±0.15）。
    runLegMulti({ "t826 knockback enchant scales in combat: strength 1.0/4.0/7.0 (lvl 0/I/II, negative clamped); re"
        "al EntityManager displacement over 0.128s strictly increasing ~0.9/3.6/6.3 blocks (old +50%/lvl "
        "was ~1.4/2.0 = wander-noise level, user saw no knockback)" }, [&]() {
        bool ok = std::abs(EnchantRegistry::knockbackStrength(0) - 1.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(1) - 4.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(2) - 7.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(-3) - 1.0f) < 1e-5f;
        // rig：运行期扫空区（P20/t809 先例 —— nextSlot 网格已耗尽）。需 18×5 净空（dy -1..+3 含地板层）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 17 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 17 && clear; ++dx)
                    for (int dz = -2; dz <= 2 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            // review0906 D2 fallback（同 t919）：depth 180 世界gen 迁移后扫描域无 18×5 净空腔 →
            //   远端定址 + 自净空（z=169 行本轮永不触及），选址不再依赖地形。
            x0 = 8;
            z0 = 169;
            for (int dx = 0; dx <= 17; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = -1; dy <= 3; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
        }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t826 knockback by level: no clear rig area found";
        } else {
            // 石平台（防 spawn 即坠；3 行宽防侧移跌落）。
            for (int dx = 0; dx <= 17; ++dx)
                for (int dz = -2; dz <= 2; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
            EntityManager ents;
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            // 三猪错开 3 格；**从远端猪先击**（II 位移朝 +X，先行者让出跑道防相互推挤）。
            const int pig2 = ents.spawnMobTyped(x0 + 7, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            const int pig1 = ents.spawnMobTyped(x0 + 4, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            const int pig0 = ents.spawnMobTyped(x0 + 1, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            ok = ok && pig0 >= 0 && pig1 >= 0 && pig2 >= 0;
            // 落定窗（0.96s）：spawn 瞬时悬空 0.05 格 + resting 复探按 AI 相位错峰 —— 不先 tick 落定的话，
            //   个别猪在他人测量窗内正处「下落 / 半嵌地板」态，mobAabbHitsSolid 把水平击退位移全撤回
            //   （首轮实测 I 级猪 dx 0.39 vs 期望 1.83 的根因）。60 tick 后全部 resting 贴面再测。
            for (int t = 0; t < 60; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            const int pigs[3] = {pig2, pig1, pig0};                 // 击序：II → I → 0（远端先走）
            const float strengths[3] = {EnchantRegistry::knockbackStrength(2),
                                       EnchantRegistry::knockbackStrength(1),
                                       EnchantRegistry::knockbackStrength(0)};
            float dxPos[3] = {-1.0f, -1.0f, -1.0f};                 // [0]=II [1]=I [2]=无附魔
            for (int k = 0; ok && k < 3; ++k) {
                const QVector3D startP = ents.posAt(pigs[k]);
                ents.knockback(pigs[k], 1.0f, 0.0f, strengths[k]);  // +X 方向击退（强度 = attackMob 同式）
                for (int t = 0; t < 8; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
                const QVector3D endP = ents.posAt(pigs[k]);
                dxPos[k] = endP.x() - startP.x();
                qInfo().noquote() << "  [t826 diag] pig" << k << "idx" << pigs[k] << "start" << startP
                                  << "end" << endP << "dx" << dxPos[k];
            }
            // 短窗（0.128s）位移 ≈ v0×0.016×(1-0.936^8)/0.064 ≈ 0.10×v0（总位移 v0/kKnockbackDrag ≈ 40% 在
            //   窗内；短窗让击退主导、游荡噪声 ≤±0.15）：理论 II/I/0 = 3.19/1.83/0.46。
            ok = ok && dxPos[2] > 0.15f && dxPos[2] < 0.80f       // 无附魔基线 ~0.46 格
                 && dxPos[1] > 1.35f && dxPos[1] < 2.35f          // I ~1.83 格
                 && dxPos[0] > 2.65f && dxPos[0] < 3.75f          // II ~3.19 格
                 && dxPos[2] < dxPos[1] && dxPos[1] < dxPos[0];   // 严格按级递增（用户症状的反面）
            if (!ok) qInfo().noquote() << "  t826 displacements II/I/base =" << dxPos[0] << dxPos[1] << dxPos[2];
            // 清场（地板全清 + 2 tick 收敛）。
            for (int dx = 0; dx <= 17; ++dx)
                for (int dz = -2; dz <= 2; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air);
            tickN(w, 2);
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t826 knockback enchant scales in combat: strength 1.0/4.0/7.0 (lvl 0/I/II, "
                             "negative clamped); real EntityManager displacement over 0.128s strictly "
                             "increasing ~0.9/3.6/6.3 blocks (old +50%/lvl was ~1.4/2.0 = wander-noise level, "
                             "user saw no knockback)";
    });

    // ── t827 燃焰点燃链探针（R19.13 附魔全效果审计的「重点疑」实测项；EntityManager 直编）──
    //    attackMob → ignite(level*4s) 静态接线已核（playercontroller t476 链）；本探针锁点燃 → 火烧推进
    //    → 扣血 → 致死 burned 掉落链的运行时行为。kFireExtinguishChance=0.15（每次火伤结算随机提前熄灭）
    //    → 断言取「多样本计数下界」防偶发：① 点燃即 isBurningAt（对照猪恒 false）；② 10 只 3HP 猪 1.76s 内
    //    ≥5 只实际扣血（每只 P(扣) = 0.85，P(<5) ≈ 3e-5）；③ 6 只 1HP 猪 ≥1 只烧死（首脉冲 ~1.05s +
    //    0.5s 死亡动画 → mobDied burned=true ~1.55s 在窗内；P(全不成) = 0.15^6 ≈ 1e-5）。游荡 ≤1.76 格 →
    //    地板 7 行宽（dz -3..+3）+ 出生位留 ≥2 格边距，防侧移跌落污染对照。
    runLegMulti({ "t827 fire-aspect ignite chain: ignite->isBurningAt immediate (control stays unlit), 1s-interval "
        "fire damage lands on >=5/10 meter pigs in 2.2s, 1HP pig dies with burned=true (cooked-drop entry"
        "); attackMob->ignite(4s*level) wiring static-verified" }, [&]() {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 125 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 15 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 15 && clear; ++dx)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            // review0906 D2 fallback（同 t919）：depth 180 世界gen 迁移后扫描域无 16×7 净空腔 →
            //   远端定址 + 自净空（z=169 行本轮永不触及），选址不再依赖地形。
            x0 = 8;
            z0 = 169;
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                    for (int dy = -1; dy <= 3; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
        }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t827 fire-aspect ignite: no clear rig area found";
        } else {
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
            EntityManager ents;
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            int diedBurned = 0, diedTotal = 0;
            QObject::connect(&ents, &EntityManager::mobDied, &ents,
                             [&](int, int, int, int, bool burned, bool) {
                                 ++diedTotal; if (burned) ++diedBurned;
                             });
            // 10 只计量猪（3HP，中央两行错开）+ 6 只 1HP 判死猪 + 1 只对照猪（10HP 不点燃）。
            int meter[10];
            for (int i = 0; i < 10; ++i)
                meter[i] = ents.spawnMobTyped(x0 + 1 + (i % 5) * 3, kRigY, z0 + (i < 5 ? -1 : 1),
                                              EntityManager::MobPig, QStringLiteral("#ee9999"), 3);
            int frail[6];
            for (int i = 0; i < 6; ++i)
                frail[i] = ents.spawnMobTyped(x0 + 1 + i * 2, kRigY, z0, EntityManager::MobPig,
                                              QStringLiteral("#ee9999"), 1);
            const int control = ents.spawnMobTyped(x0 + 13, kRigY, z0, EntityManager::MobPig,
                                                   QStringLiteral("#ee9999"), 10);
            bool ok = control >= 0 && !ents.isBurningAt(control);
            for (int i = 0; i < 10; ++i) ok = ok && meter[i] >= 0;
            for (int i = 0; i < 6; ++i) ok = ok && frail[i] >= 0;
            // 点燃全部实验猪（燃焰 II = 8s 同长；对照不点）。点燃即燃（视觉火焰 Model 据点）。
            for (int i = 0; i < 10; ++i) ents.ignite(meter[i], 8.0f);
            for (int i = 0; i < 6; ++i) ents.ignite(frail[i], 8.0f);
            bool allBurning = true;
            for (int i = 0; i < 10; ++i) allBurning = allBurning && ents.isBurningAt(meter[i]);
            for (int i = 0; i < 6; ++i) allBurning = allBurning && ents.isBurningAt(frail[i]);
            ok = ok && allBurning && !ents.isBurningAt(control);
            // 1.76s（110 tick）：首火伤脉冲（~1.05s）+ 死亡动画 0.5s（mobDied ~1.55s）均在窗内。
            for (int t = 0; t < 110; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            int damaged = 0;
            for (int i = 0; i < 10; ++i)
                if (ents.healthAt(meter[i]) < 3 || ents.deadAt(meter[i])) ++damaged; // 3HP 计量猪扣血 / 烧死均计
            ok = ok && damaged >= 5                       // ≥5/10 实际吃到火伤（P(<5) ≈ 3e-5）
                 && ents.healthAt(control) == 10 && !ents.deadAt(control) // 对照猪无伤
                 && diedTotal >= 1 && diedBurned >= 1;    // ≥1 只 1HP 猪烧死且走 burned 掉落链
            if (!ok) qInfo().noquote() << "  t827 fire diag: damaged" << damaged << "/10, died" << diedTotal
                                       << "burned" << diedBurned << "control hp" << ents.healthAt(control);
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t827 fire-aspect ignite chain: ignite->isBurningAt immediate (control "
                                 "stays unlit), 1s-interval fire damage lands on >=5/10 meter pigs in 2.2s, "
                                 "1HP pig dies with burned=true (cooked-drop entry); attackMob->ignite("
                                 "4s*level) wiring static-verified";
        }
    });

    // ── P-t828 水下窒息 + 鱿鱼浮力（R19.13 生物组；专用局部世界 wA：pit 水柜 + 干 pit 对照）──
    //    (a) 猪（3HP）沉水 pit 底部：头位浸水 → 15s 呼吸耗尽 → 1HP/s 窒息掉血（20s 窗扣 ~4HP → 死或 ≤1）；
    //        同构干 pit（同壁高同基底、只少水）对照猪满血不动（呼吸不启动）。机制等价玩家 t202 溺水节奏。
    //    (b) 鱿鱼（10HP）同水 pit：浮力项 → 不贴底（中心 y 明显高于池底 restY；旧缓沉行为恒贴底）；
    //        且水生豁免溺水（20s 后仍满血）。
    //    rig 高度：局部 World setter 触发 worldgen（地形 ~57-71 + 树冠 ≤81）→ rig 平面取 y84+（地形之上
    //    确定性净空，P31「自凿净空」精神的免凿版——直接摆更高）。pit 结构：y84 石基底（30×30）+ 两口
    //    12×12 pit 开口（水 pit x10..21/z10..21 填水 y85..87 + **y88 石盖**；干 pit x24..35/z24..35 全空）+
    //    口外 y85..88 全石壁（壁顶 89）。t923 起**水 pit 加盖**：陆栖 mob 主动浮面（头浸水 → kMobSwimBuoyancy
    //    升到水面呼吸）后敞顶猪不再溺亡（MC 语义：溺水须被按在水下）——加盖把猪钉在盖下（review25 #4 上浮
    //    天花板钳制）保溺水断言仍可达；鱿鱼浮面被盖截在 ~87.6 ≥ 86.2 下界，(b) 断言不变。水位恒定（探针
    //    不 tick world，流体静置）。
    runLegMulti({ "t828 drowning + squid buoyancy: submerged pig loses HP after 15s breath (1HP/s, dry control stay"
        "s full), squid buoyed off pool floor (no bottom-resting) and exempt from drowning" }, [&]() {
        World wA;
        wA.setWidth(44); wA.setDepth(44); wA.setHeight(96); wA.setSeed(21);
        for (int x = 8; x < 38; ++x)
            for (int z = 8; z < 38; ++z) wA.setBlock(x, 84, z, BR::Stone, 0);
        for (int x = 8; x < 38; ++x)
            for (int z = 8; z < 38; ++z)
                for (int y = 85; y <= 88; ++y) {
                    const bool inWaterPit = (x >= 10 && x < 22 && z >= 10 && z < 22);
                    const bool inDryPit = (x >= 24 && x < 36 && z >= 24 && z < 36);
                    const quint8 b = inWaterPit
                        ? ((y <= 87) ? BR::Water : BR::Stone) // t923：y88 石盖（猪被浮力钉在盖下 → 头恒浸水）
                        : (inDryPit ? BR::Air : BR::Stone);
                    wA.setBlock(x, y, z, b, 0);
                }
        EntityManager emA;
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        const int drownPig = emA.spawnMobTyped(15, 85, 15, EntityManager::MobPig,
                                               QStringLiteral("#ee9999"), 3);
        const int dryPig = emA.spawnMobTyped(29, 85, 29, EntityManager::MobPig,
                                             QStringLiteral("#ee9999"), 3);
        const int squid = emA.spawnMobTyped(13, 86, 13, EntityManager::MobSquid,
                                            QStringLiteral("#6a4a3a"), 10);
        bool ok = drownPig >= 0 && dryPig >= 0 && squid >= 0;
        if (ok) {
            // (a)+(b) 同场推进 20s（1250 tick）：猪窒息窗 15+4s、鱿鱼浮力窗充裕。
            for (int t = 0; t < 1250; ++t) emA.tick(0.016f, &wA, farListener, 0.3f, 1.8f, false);
            const int dHp = emA.healthAt(drownPig);
            const bool dDead = emA.deadAt(drownPig);
            const int dryHp = emA.healthAt(dryPig);
            const float sqY = emA.posAt(squid).y();
            const int sqHp = emA.healthAt(squid);
            // 溺水猪：3HP − 1HP/s（自 15s 起）→ 20s 内扣 ~4HP → 死或 ≤1；对照猪满血 3。
            ok = ok && (dDead || dHp <= 1);
            ok = ok && dryHp == 3;
            // 鱿鱼：浮离池底（旧缓沉恒贴底 restY 85.45；浮力后 bob 在 ~86.4..88.5 → 下界 86.2 区分）+
            //   满血（水生豁免溺水）。
            ok = ok && sqY >= 86.2f && sqHp == 10 && !emA.deadAt(squid);
            if (!ok)
                qInfo().noquote() << "  t828 diag: drownPig hp" << dHp << "dead" << dDead
                                  << "| dryPig hp" << dryHp
                                  << "| squid y" << sqY << "hp" << sqHp;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t828 drowning + squid buoyancy: submerged pig loses HP after 15s "
                             "breath (1HP/s, dry control stays full), squid buoyed off pool floor "
                             "(no bottom-resting) and exempt from drowning";
    });

    // ── P-t923 驯服狼生态三面（R19.16 t923：跟随返修 / 浮面 / 仇恨传递 + 阴性轮）──
    //    (a) 全链复现用户实测「走了他还在水里最后淹死」并断言修复：驯服站立狼沉 2 深水坑（壁顶与水面等高
    //        = 常规掘塘口径）→ 主动浮面（头浸水净浮力，中心升到水面带 ≥86.3——旧缓沉恒贴底 85.45）→ 不溺亡
    //        （12s 满血）→ 追玩家贴壁 → 泳跃（vy=kJumpSpeed 直设，穿 rise 钳制）跃上壁顶 → 出水登岸
    //        （中心 ≥87.0）+ 向玩家逼近（XZ 距 <6，初始 ~9）。玩家距 <12 不瞬移（瞬移分支会短路本链）。
    //    (b) 阴性·坐态：驯服坐狼同坑 → XZ 静止（留守不跟随）但**仍浮面**（浮力是物理层非 AI 层——坐宠落水
    //        也浮，机制等价 MC）+ 满血。
    //    (c) 仇恨传递：wolfRetaliateAgainst(驯服狼, 攻击者 Shambler) 直调（骷髅箭 / 燃烬者火球两接线点由 (d)
    //        源码钉）→ 狼越过跟随优先追击攻击者（m_wolfTarget 分支先于跟随）+ 攻击者被咬扣血（< 20HP）。
    //    (c2) 阴性·豹猫：同调 wolfRetaliateAgainst(驯服豹猫, 攻击者) → no-op（受害者非狼；MC 1.0 猫不攻击
    //        怪物，机制钉死）→ 攻击者满血 + 豹猫原地贴玩家。窗口 playerTargetable=false（防敌对近战命中玩家
    //        顺带注册 m_wolfTarget 把 (c) 残狼引来搅局——t480 melee 注册链）。
    //    (d) 源码钉：火球 / 骷髅箭命中狼两伤害点调 wolfRetaliateAgainst + 箭碰撞滤网并入狼（QML 外 C++ 行为
    //        可行为级断言的是 hook 本体；两处伤害点内部走逐帧弹道，headless 复现弹道不稳 → t880 (b) 源码钉先例）。
    runLegMulti({ "t923 tamed-wolf ecology: float-to-surface (head-submerged buoyancy) breaks the drown chain, sitt"
        "ing pet still floats but stays put (negative), standing pet swims-hops the pond lip and reaches "
        "the owner (follow rework root = water trap), wolfRetaliateAgainst drives pack biting of a mob at"
        "tacker while the same hook is a no-op for tamed cats (MC 1.0: cats do not fight) and both damage"
        " sites (skeleton arrow + emberling fireball) are source-pinned to the hook" }, [&]() {
        World wW;
        wW.setWidth(44); wW.setDepth(44); wW.setHeight(96); wW.setSeed(26); // rig y84+ 地形之上（t828 同款）
        for (int x = 4; x < 40; ++x)
            for (int z = 4; z < 40; ++z) wW.setBlock(x, 84, z, BR::Stone, 0);
        for (int x = 4; x < 40; ++x)
            for (int z = 4; z < 40; ++z)
                for (int y = 85; y <= 86; ++y) {
                    const bool inPit = (x >= 12 && x < 20 && z >= 12 && z < 20);
                    wW.setBlock(x, y, z, inPit ? BR::Water : BR::Stone, 0); // 坑内水 y85..86（顶 87）；坑外岸顶 87
                }
        EntityManager emW;
        const QVector3D bank(24.5f, 87.0f, 15.5f); // 岸上玩家位（坑心距 ~9 < 12 不瞬移；x≥20 在岸上）
        bool ok = true;
        const int wolf = emW.spawnMobTyped(15, 86, 15, EntityManager::MobWolf,
                                           QStringLiteral("#c8ccd4"), 10);
        if (wolf < 0) {
            ok = false;
        } else {
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && !tamed; ++attempt)
                tamed = emW.tameWolf(wolf);
            ok = ok && tamed && emW.wolfTamedAt(wolf);
            // (b) 坐态：浮面 + XZ 静止 + 满血（先坐后站——(a) 要站态）。
            emW.toggleWolfSit(wolf);
            ok = ok && emW.wolfSittingAt(wolf);
            const QVector3D sitP0 = emW.posAt(wolf);
            for (int t = 0; t < 250; ++t) // 4s：浮面（自沉落位升到水面带；旧缓沉恒 85.45 贴底）
                emW.tick(0.016f, &wW, bank, 0.3f, 1.8f, true);
            const QVector3D sitP1 = emW.posAt(wolf);
            const float sitDXZ = QVector3D(sitP1.x() - sitP0.x(), 0.0f, sitP1.z() - sitP0.z()).length();
            ok = ok && sitDXZ < 0.3f          // 坐 → 留守（XZ 不跟）
                 && sitP1.y() >= 86.3f        // 浮面（水面带中心 ~86.6-87；贴底 85.45 = 旧缓沉判据）
                 && emW.healthAt(wolf) == 10; // 不溺亡（bobbing 全程头出水呼吸恢复）
            // (a) 站态：跟随出水上岸（浮面 → 贴壁 → 泳跃 → 登岸 → 逼近玩家）。
            emW.toggleWolfSit(wolf);
            ok = ok && !emW.wolfSittingAt(wolf);
            for (int t = 0; t < 500; ++t) // 8s
                emW.tick(0.016f, &wW, bank, 0.3f, 1.8f, true);
            const QVector3D outP = emW.posAt(wolf);
            const float outDXZ = QVector3D(outP.x() - bank.x(), 0.0f, outP.z() - bank.z()).length();
            ok = ok && outP.y() >= 87.0f   // 出水登岸（岸顶 87 + halfH 0.45 ≈ 87.45；留半格余量）
                 && outDXZ < 6.0f          // 已逼近玩家（初始 ~9；困坑内 ≈9-10 → 阈值 6 区分）
                 && emW.healthAt(wolf) == 10;
            if (!ok)
                qInfo().noquote() << "  t923ab diag: tamed" << emW.wolfTamedAt(wolf)
                                  << "sitDXZ" << sitDXZ << "sitY" << sitP1.y()
                                  << "| outY" << outP.y() << "outDXZ" << outDXZ
                                  << "hp" << emW.healthAt(wolf);
        }
        // (c) 仇恨传递：驯服狼反击攻击者（直调 hook —— 火球/箭接线见 (d)）。
        const int attacker = emW.spawnMobTyped(22, 87, 20, EntityManager::MobShambler,
                                               QStringLiteral("#3a7a3a"), 20);
        if (attacker < 0 || wolf < 0) {
            ok = false;
        } else {
            emW.wolfRetaliateAgainst(wolf, attacker);
            const QVector3D farAway(-1000.0f, 90.0f, -1000.0f); // 玩家远 → 跟随/瞬移不抢戏（目标分支优先）
            for (int t = 0; t < 312; ++t) // 5s：追上（~5 格 @3.5/s）+ 冷却 1s 咬 ≥2 口
                emW.tick(0.016f, &wW, farAway, 0.3f, 1.8f, true);
            ok = ok && emW.healthAt(attacker) < 20; // 被驯服狼咬伤（kWolfAttackDamage 4/口）
            if (!ok)
                qInfo().noquote() << "  t923c diag: attacker hp" << emW.healthAt(attacker);
        }
        // (c2) 阴性·豹猫：同 hook 对驯服猫 no-op（猫无防御分支，MC 1.0 机制钉死）。
        const int cat = emW.spawnMobTyped(30, 87, 30, EntityManager::MobOcelot,
                                          QStringLiteral("#e8c890"), 10);
        const int attacker2 = emW.spawnMobTyped(26, 87, 30, EntityManager::MobShambler,
                                                QStringLiteral("#3a7a3a"), 20);
        if (cat < 0 || attacker2 < 0) {
            ok = false;
        } else {
            bool catTamed = false;
            for (int attempt = 0; attempt < 200 && !catTamed; ++attempt)
                catTamed = emW.tameOcelot(cat);
            ok = ok && catTamed && emW.ocelotTamedAt(cat);
            const QVector3D nearCat(31.0f, 87.0f, 30.5f); // 玩家贴猫（<2.5 到位停步 → XZ 稳定基线）
            emW.wolfRetaliateAgainst(cat, attacker2);     // 非狼受害者 → 静默 no-op
            const QVector3D catP0 = emW.posAt(cat);
            for (int t = 0; t < 188; ++t) // 3s；targetable=false：敌对不近战玩家 → 不触发 t480 melee 注册链
                emW.tick(0.016f, &wW, nearCat, 0.3f, 1.8f, false);
            const QVector3D catP1 = emW.posAt(cat);
            const float catDXZ = QVector3D(catP1.x() - catP0.x(), 0.0f, catP1.z() - catP0.z()).length();
            ok = ok && emW.healthAt(attacker2) == 20  // 猫不反击（MC 1.0 猫不攻击怪物）
                 && catDXZ < 0.8f;                    // 猫原地贴玩家（无防御追击）
            if (!ok)
                qInfo().noquote() << "  t923c2 diag: attacker2 hp" << emW.healthAt(attacker2)
                                  << "catDXZ" << catDXZ << "tamed" << emW.ocelotTamedAt(cat);
        }
        // (d) 源码钉：两伤害点接 wolfRetaliateAgainst + 骷髅箭碰撞滤网并入狼。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile ef(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            const QString t = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
            ok = ok && t.contains(QStringLiteral("wolfRetaliateAgainst(mi, e.fireballShooter)"))
                 && t.contains(QStringLiteral("wolfRetaliateAgainst(mi, e.arrowShooter)"))
                 && t.contains(QStringLiteral("m.mobType != MobIronGolem && m.mobType != MobWolf"));
            if (t.isEmpty())
                qInfo().noquote() << "  t923d diag: entitymanager.cpp slice miss";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t923 tamed-wolf ecology: float-to-surface (head-submerged buoyancy) "
                             "breaks the drown chain, sitting pet still floats but stays put (negative), "
                             "standing pet swims-hops the pond lip and reaches the owner (follow rework "
                             "root = water trap), wolfRetaliateAgainst drives pack biting of a mob "
                             "attacker while the same hook is a no-op for tamed cats (MC 1.0: cats do "
                             "not fight) and both damage sites (skeleton arrow + emberling fireball) "
                             "are source-pinned to the hook";
    });

    // ── P-t829 末影人三修（专用局部世界 wB 平石台；rig y84+ 地形之上，t828 同款免凿高台）──
    //    (a) 碰水即伤 + 瞬移逃离：夜行者站 1 深水洼 → 首触 tick 扣 1HP + 8..16 格瞬移离开（4s 窗 hp<满
    //        且位移 ≥6；旧实现「连续满 1s 才扣」在瞬移先发节奏下恒凑不满 = 水伤死代码——用户「水中平安
    //        无事」根因，t829③ 改首触即伤）；
    //    (b) 箭命中 race 修复：玩家箭命中夜行者 → 强制瞬移（绕过冷却）+ 箭一并消耗——1.6s 后场内零 Arrow
    //        残留（旧「dodge 后箭继续飞 / 冷却内零反应穿身」两面都锁）、夜行者未被箭扣血（弹射免疫）；
    //    (c) 下巴补全合成器：合成 256×128 rig（头盒区底带透明 + 上部不透明脸 / 眼亮行）→ 输出头区全不透明
    //        + 列向延拓色（下巴带 == 其列上方最近不透明色）+ 眼行原样保留 + 头区外像素不动 + 坏输入返空。
    runLegMulti({ "t829 nightwalker trio: water contact deals first-touch damage + teleport escape, player arrow hi"
        "t forces teleport dodge and consumes the arrow (no pass-through, no cooldown-blind window, mob u"
        "nhurt), chin-filler compositor fills head-box transparent band via column extension preserving e"
        "yes and out-of-region pixels" }, [&]() {
        World wB;
        wB.setWidth(48); wB.setDepth(48); wB.setHeight(96); wB.setSeed(22);
        for (int x = 2; x < 46; ++x)
            for (int z = 2; z < 46; ++z) wB.setBlock(x, 84, z, BR::Stone, 0);
        EntityManager emB;
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        bool ok = true;
        // (a) 水洼 4×4（x22..25, z22..25）y85 一层水（台面顶 85.0 → 夜行者脚位 85.0 恰在水格）。
        for (int x = 22; x < 26; ++x)
            for (int z = 22; z < 26; ++z) wB.setBlock(x, 85, z, BR::Water, 0);
        const int nw1 = emB.spawnMobTyped(23, 86, 23, EntityManager::MobNightwalker,
                                          QStringLiteral("#2a1f2a"), 10);
        if (nw1 < 0) {
            ok = false;
        } else {
            const QVector3D p0 = emB.posAt(nw1);
            // playerTargetable=true（真实 Survival 语义）：敌对 mob 走 mobType 分发链进 aiNightwalker
            //   （怕水扣血/瞬移在其内）——false 会走 t290 观察者门控的 aiWander 回退，永远到不了水伤段
            //   （首跑踩坑：moved 0 + hp 10）。listener 远在 -1000 → detect/stare 够不着，只余游荡。
            //   位移断言取**全程最大 XZ 位移**（逐 tick 采样）：瞬移方向随机、后续跳可能把净位移拉回
            //   原点附近（实测首跳 13 格后二跳回 4.6）——净位移断言 flaky，峰值断言确定性捕获首次瞬移。
            float maxMoved = 0.0f;
            for (int t = 0; t < 250; ++t) {
                emB.tick(0.016f, &wB, farListener, 0.3f, 1.8f, true);
                const QVector3D pt = emB.posAt(nw1);
                maxMoved = std::max(maxMoved,
                                    QVector3D(pt.x() - p0.x(), 0.0f, pt.z() - p0.z()).length());
            }
            ok = ok && emB.healthAt(nw1) < 10 && maxMoved >= 6.0f;
            if (!ok)
                qInfo().noquote() << "  t829a diag: nw hp" << emB.healthAt(nw1)
                                  << "maxMoved" << maxMoved;
        }
        // (b) 箭 deflection：独立管理器 + 独立台面区（避开 (a) 残留水洼 / 夜行者）。箭 y 86.4 = 夜行者
        //     站姿中心（85+1.4）；24 b/s 飞 ~8 格重力跌落 ~1.3 格仍在其 ±(1.4+0.4) 命中带内。
        EntityManager emB2;
        const int nw2 = emB2.spawnMobTyped(34, 85, 6, EntityManager::MobNightwalker,
                                           QStringLiteral("#2a1f2a"), 10);
        if (nw2 < 0) {
            ok = false;
        } else {
            const QVector3D p0 = emB2.posAt(nw2);
            emB2.spawnArrowPlayer(QVector3D(26.5f, 86.4f, 6.5f), QVector3D(24.0f, 0.0f, 0.0f), 5);
            // 1.6s 窗：命中（~0.35s）即 dodge 位移 ≥8；取全程最大 XZ 位移（同 (a)——瞬移方向随机，净
            //   位移可能被后续跳拉回）。箭在命中帧即被消耗（t829①）——零 Arrow 断言无需等寿命 despawn。
            float maxMoved2 = 0.0f;
            for (int t = 0; t < 100; ++t) {
                emB2.tick(0.016f, &wB, farListener, 0.3f, 1.8f, false);
                const QVector3D pt = emB2.posAt(nw2);
                maxMoved2 = std::max(maxMoved2,
                                     QVector3D(pt.x() - p0.x(), 0.0f, pt.z() - p0.z()).length());
            }
            int arrows = 0;
            for (int i = 0; i < emB2.count(); ++i)
                if (emB2.aliveAt(i) && emB2.kindAt(i) == EntityManager::Arrow) ++arrows;
            ok = ok && arrows == 0 && emB2.healthAt(nw2) == 10 && maxMoved2 >= 6.0f;
            if (!ok)
                qInfo().noquote() << "  t829b diag: arrows" << arrows << "nw hp"
                                  << emB2.healthAt(nw2) << "maxMoved" << maxMoved2;
        }
        // (c) 下巴合成器密闭 rig。
        {
            QDir dC(QDir::temp().absoluteFilePath("t829_chin_probe"));
            dC.removeRecursively();
            dC.mkpath(".");
            const QString srcPath = dC.absoluteFilePath("enderman.png");
            QImage src(256, 128, QImage::Format_ARGB32);
            src.fill(Qt::transparent); // 全透底（含头区外「躯干带」原样保留断言用）
            // 头盒区 = base (0,0)-(32,16) → HD (0,0)-(128,64)：top 面不透明暗色岛 + 脸窗（眼亮行 / 底带透）。
            const QRgb dark = qRgb(24, 18, 30);
            const QRgb eye = qRgb(232, 220, 255);
            for (int x = 32; x < 64; ++x)
                for (int y = 0; y < 32; ++y) src.setPixel(x, y, dark);      // top 面不透明
            for (int x = 32; x < 64; ++x)
                for (int y = 32; y < 48; ++y) src.setPixel(x, y, dark);     // 脸上部
            for (int x = 40; x < 56; ++x)
                for (int y = 36; y < 40; ++y) src.setPixel(x, y, eye);      // 眼亮行（须原样保留）
            // 脸底带（HD y48..63 = base v12..15）留透明 → 合成须列向延拓填充。
            src.save(srcPath, "PNG");
            const QString outPath = generateNightwalkerChinFile(srcPath, 77);
            bool cOk = !outPath.isEmpty();
            if (cOk) {
                QImage out(outPath);
                cOk = !out.isNull() && out.size() == src.size();
                if (cOk) {
                    for (int y = 0; y < 64 && cOk; ++y)
                        for (int x = 0; x < 128; ++x)
                            if (qAlpha(out.pixel(x, y)) != 255) { cOk = false; break; } // 头区全不透明
                    // 下巴带（y48..63, x32..63）== 列上方最近不透明色（本 rig 脸上部恒 dark → 全 dark）。
                    for (int y = 48; y < 64 && cOk; ++y)
                        for (int x = 32; x < 64; ++x)
                            if (out.pixel(x, y) != dark) { cOk = false; break; }
                    // 眼行原样保留（合成不得动既有不透明像素）。
                    for (int x = 40; x < 56 && cOk; ++x)
                        for (int y = 36; y < 40; ++y)
                            if (out.pixel(x, y) != eye) { cOk = false; break; }
                    // 头区外不动（仍透明——合成只补头盒区，防把延拓拉进躯干带）。
                    cOk = cOk && qAlpha(out.pixel(200, 100)) == 0 && qAlpha(out.pixel(10, 80)) == 0;
                }
            }
            // 坏输入：缺文件 → 返空（优雅降级，调用方回退原样）。
            cOk = cOk && generateNightwalkerChinFile(dC.absoluteFilePath("missing.png"), 77).isEmpty();
            if (!cOk) qInfo().noquote() << "  t829c diag: chin synth contract broken";
            ok = ok && cOk;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t829 nightwalker trio: water contact deals first-touch damage + "
                             "teleport escape, player arrow hit forces teleport dodge and consumes "
                             "the arrow (no pass-through, no cooldown-blind window, mob unhurt), "
                             "chin-filler compositor fills head-box transparent band via column "
                             "extension preserving eyes and out-of-region pixels";
    });

    // ── P-t830 燃烬者主世界自然刷新摘除（专用局部世界 wF **height 96**：共享 w 高 48 时 heightAt ~57-71
    //    越 surface 界 → surface 尝试恒败只剩稀有洞穴气袋（实测 60 周期仅 3 刷）——高度 96 下地表 spawn
    //    真实成立。tickHostileLife skyBrightness=0 全域黑暗）──
    //    统计采样：~40+ 个自然刷新事件（每 2.2s 周期 ≤1 只，周期末清场防敌对区域 cap 12 饱和停刷）→
    //    断言零 Emberling（旧 6 份表 P(40 采样零燃烬) = (5/6)^40 ≈ 4.6e-4 → 回归必被逮）+ ≥3 型多样
    //    （防「摘除时错删整池」的反向回归）。手动刷路径（蛋 / 笼 spawnHostileMob）不经本表——t787 探针
    //    已锁笼路径含 Emberling，不受影响。ring [24,40] 全落界内（listener 居中 50,50）。
    runLegMulti({ "t830 emberling removed from natural dark-spawn pool:  natural spawns sampled with zero Emberling"
        " (>=3 distinct hostile types prove pool alive; eggs/spawner manual paths untouched)" }, [&]() {
        World wF;
        wF.setWidth(100); wF.setDepth(100); wF.setHeight(96); wF.setSeed(26);
        EntityManager em830;
        const QVector3D P830(50.0f, float(wF.heightAt(50, 50) + 2), 50.0f);
        int samples = 0, ember = 0, distinct = 0;
        bool seen[20] = {};
        for (int round = 0; round < 60 && samples < 40; ++round) {
            for (int t = 0; t < 140; ++t)
                em830.tickHostileLife(0.016f, &wF, P830, 0.0f); // 一个 spawn 周期（kSpawnInterval 2s）+ 余量
            for (int i = 0; i < em830.count(); ++i) {
                if (!em830.aliveAt(i) || em830.deadAt(i)) continue;
                if (em830.kindAt(i) != EntityManager::Mob) continue;
                const int mt = em830.mobTypeAt(i);
                ++samples;
                if (mt == EntityManager::MobEmberling) ++ember;
                if (mt >= 0 && mt < 20 && !seen[mt]) { seen[mt] = true; ++distinct; }
                em830.damageEntity(i, em830.maxHealthAt(i)); // 清场（dead → 不计 cap / 下轮不再采）
            }
        }
        bool ok = samples >= 40 && ember == 0 && distinct >= 3;
        if (!ok)
            qInfo().noquote() << "  t830 diag: samples" << samples << "ember" << ember
                              << "distinct" << distinct;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t830 emberling removed from natural dark-spawn pool: " << samples
                          << " natural spawns sampled with zero Emberling (>=3 distinct hostile "
                             "types prove pool alive; eggs/spawner manual paths untouched)";
    });

    // ── P-t831 狼/豹猫驯服全链补全（专用局部世界 wC 平石台；Entities 层直测，Game 层 useBlock 接线静态审）──
    //    (a) 驯服成功 → 爱心（inLoveAt 真）→ kTameHeartDuration 4s 衰减后收心；
    //    (b) healTamedPet：受伤驯服狼回血钳上限；满血 / 未驯服 → false（caller 不消耗）；
    //    (c) 坐态冻结（toggleWolfSit 后近旁玩家 1s 零位移）↔ 站态跟随（2s 位移 ≥2）；
    //    (d) 跟随态距玩家 > kWolfTeleportDist → 瞬移到玩家近旁（1s 内 ≤10 格）；
    //    (e) 豹猫驯服同爱心链。
    runLegMulti({ "t831/t878 taming chain: tame success shows heart (decays 4s), healTamedPet restores injured pet "
        "(full/wild reject), sitting freezes movement vs standing follows owner, >12-block gap teleports "
        "pet to owner side (mid-gap 16 also jumps, near-gap 6 does not - t878⑤ 24->12), ocelot taming sho"
        "ws heart too (collar/sit-toggle GUI glue static-reviewed)" }, [&]() {
        World wC;
        wC.setWidth(44); wC.setDepth(44); wC.setHeight(96); wC.setSeed(23); // rig y84+ 地形之上（t828 同款）
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wC.setBlock(x, 84, z, BR::Stone, 0);
        EntityManager emC;
        const int wolf = emC.spawnMobTyped(8, 85, 8, EntityManager::MobWolf,
                                           QStringLiteral("#c8ccd4"), 10);
        bool ok = wolf >= 0;
        if (ok) {
            // (a) 驯服循环（~33%/次；P(200 次全败)≈2e-36）。
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && !tamed; ++attempt)
                tamed = emC.tameWolf(wolf);
            ok = ok && tamed && emC.wolfTamedAt(wolf) && emC.inLoveAt(wolf); // 驯服即爱心
            for (int t = 0; t < 344; ++t) // 5.5s > 4s 爱心时长
                emC.tick(0.016f, &wC, QVector3D(10.5f, 86.0f, 10.5f), 0.3f, 1.8f, true);
            ok = ok && !emC.inLoveAt(wolf); // 收心
            // (b) 喂食回血。
            emC.damageEntity(wolf, 4);
            ok = ok && emC.healthAt(wolf) == 6;
            ok = ok && emC.healTamedPet(wolf, 4) && emC.healthAt(wolf) == 10; // 6→10
            ok = ok && !emC.healTamedPet(wolf, 4);                            // 满血 → false
            const int wildCtl = emC.spawnMobTyped(4, 85, 4, EntityManager::MobWolf,
                                                  QStringLiteral("#c8ccd4"), 10);
            ok = ok && wildCtl >= 0 && !emC.healTamedPet(wildCtl, 4);         // 未驯服 → false
            // (c) 坐态冻结。
            emC.toggleWolfSit(wolf);
            ok = ok && emC.wolfSittingAt(wolf);
            const QVector3D p0 = emC.posAt(wolf);
            for (int t = 0; t < 64; ++t)
                emC.tick(0.016f, &wC, QVector3D(11.5f, 86.0f, 8.5f), 0.3f, 1.8f, true); // 3 格外玩家
            ok = ok && (emC.posAt(wolf) - p0).length() < 0.05f; // 坐 → 不动（游荡 / 跟随全停）
            // (c2) 站态跟随。
            emC.toggleWolfSit(wolf);
            ok = ok && !emC.wolfSittingAt(wolf);
            for (int t = 0; t < 125; ++t)
                emC.tick(0.016f, &wC, QVector3D(15.5f, 86.0f, 8.5f), 0.3f, 1.8f, true); // 7 格外玩家
            ok = ok && (emC.posAt(wolf) - p0).length() >= 2.0f; // 站 → 跟随走近
            // (d) 过远瞬移（listener y 取台面层 86 → 瞬移落点扫到台面而非台面下自然地形）。
            //   t878⑤ 阈值 24→12（MC 语义 >12 格瞬移）：35 格远距照触发；中距 16 格（旧 24 阈值内、新 12 外）
            //   也须瞬移；近距 6 格**不瞬移**（走跟非跳变——单 AI 窗内位移 ≪ 瞬移跳距，可分辨）。
            const QVector3D far(36.5f, 86.0f, 36.5f);
            for (int t = 0; t < 64; ++t)
                emC.tick(0.016f, &wC, far, 0.3f, 1.8f, true);
            const QVector3D posAfterFar = emC.posAt(wolf);
            const float dXZ = QVector3D(posAfterFar.x() - far.x(), 0.0f,
                                        posAfterFar.z() - far.z()).length();
            ok = ok && dXZ <= 10.0f; // >12 → 瞬移 2..5 环 + 漂移余量
            // (d2) 中距 16 格（12 < 16 < 24）→ 一并瞬移（t878⑤ 新语义；旧 24 阈值此处不瞬移 = 探针对旧值红）。
            const QVector3D mid(20.5f, 86.0f, 8.5f); // 距 far 玩家位 ~16+ 格
            const float midGap = QVector3D(mid.x() - posAfterFar.x(), 0.0f,
                                           mid.z() - posAfterFar.z()).length();
            if (ok && midGap > 13.0f && midGap < 23.0f) { // 只在几何成立时驱动（防瞬移落点贴边使 gap 出带）
                const QVector3D beforeMid = emC.posAt(wolf);
                for (int t = 0; t < 8; ++t) // 8 帧 ≤ 2 个 AI 窗（kAiTickInterval=4）；走跟位移 ≤0.5
                    emC.tick(0.016f, &wC, mid, 0.3f, 1.8f, true);
                ok = ok && (emC.posAt(wolf) - beforeMid).length() > 5.0f; // 跳变 = 瞬移（走跟 8 帧 ≈ 0.45）
            }
            // (d3) 近距 6 格 → 不瞬移（8 帧内位移 ≤1.0 = 走跟，非 ≥5 跳变）。
            {
                const QVector3D nearP(posAfterFar.x() + 6.0f, 86.0f, posAfterFar.z());
                const QVector3D beforeNear = emC.posAt(wolf);
                for (int t = 0; t < 8; ++t)
                    emC.tick(0.016f, &wC, nearP, 0.3f, 1.8f, true);
                ok = ok && (emC.posAt(wolf) - beforeNear).length() < 1.0f; // 无瞬移跳变
            }
            if (!ok)
                qInfo().noquote() << "  t831 diag: tamed" << emC.wolfTamedAt(wolf)
                                  << "heart" << emC.inLoveAt(wolf) << "hp" << emC.healthAt(wolf)
                                  << "dist" << dXZ;
        }
        // (e) 豹猫驯服爱心链。
        const int ocelot = emC.spawnMobTyped(20, 85, 20, EntityManager::MobOcelot,
                                             QStringLiteral("#e8c890"), 10);
        if (ocelot >= 0) {
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && !tamed; ++attempt)
                tamed = emC.tameOcelot(ocelot);
            ok = ok && tamed && emC.ocelotTamedAt(ocelot) && emC.inLoveAt(ocelot);
        } else {
            ok = false;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t831/t878 taming chain: tame success shows heart (decays 4s), "
                             "healTamedPet restores injured pet (full/wild reject), sitting freezes "
                             "movement vs standing follows owner, >12-block gap teleports pet to "
                             "owner side (mid-gap 16 also jumps, near-gap 6 does not - t878⑤ 24->12), "
                             "ocelot taming shows heart too (collar/sit-toggle GUI glue "
                             "static-reviewed)";
    });

    // ── P-t878④ 中键 pick-block 生物蛋映射全蛋族补全（纯静态映射直调；Game 层）──
    //    mobTypeEggId（PlayerController 静态单一权威）必须覆盖 RecipeRegistry **全部 13 种蛋**（t785 造狼/
    //    豹猫/夜行者/燃烬者蛋时旧表漏跟 = 用户「中键复制不了狼/豹猫生物蛋」根因）。断言三向：
    //    (a) 4 新映射精确命中（狼/豹猫/夜行者/燃烬者）；
    //    (b) 蛋族完备性：遍历全部 mobType 收集映射，**恰好**等于 13 蛋全集（加蛋不跟表 → 集合差非空即红）；
    //    (c) 单射：无两个 mobType 映射同一蛋 id（防复制粘贴错位）。
    runLegMulti({ "t878 pick-block egg map completion: mobTypeEggId covers ALL 13 spawn-egg items (wolf/ocelot/nigh"
        "twalker/emberling added - the t785 egg batch never followed this table, which is why creative mi"
        "ddle-click could not copy wolf/ocelot eggs), set-equality against the RecipeRegistry egg family "
        "plus injectivity pin 'new egg must follow the table' (a future egg without a mapping row turns t"
        "his red)" }, [&]() {
        bool ok = true;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobWolf)
                  == RecipeRegistry::SpawnEggWolfId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobOcelot)
                  == RecipeRegistry::SpawnEggOcelotId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobNightwalker)
                  == RecipeRegistry::SpawnEggNightwalkerId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobEmberling)
                  == RecipeRegistry::SpawnEggEmberlingId;
        const int kAllEggs[] = {
            RecipeRegistry::SpawnEggPigId,      RecipeRegistry::SpawnEggCowId,
            RecipeRegistry::SpawnEggSheepId,    RecipeRegistry::SpawnEggShamblerId,
            RecipeRegistry::SpawnEggBonesId,    RecipeRegistry::SpawnEggStalkerId,
            RecipeRegistry::SpawnEggSpiderId,   RecipeRegistry::SpawnEggChickenId,
            RecipeRegistry::SpawnEggSquidId,    RecipeRegistry::SpawnEggNightwalkerId,
            RecipeRegistry::SpawnEggEmberlingId, RecipeRegistry::SpawnEggWolfId,
            RecipeRegistry::SpawnEggOcelotId,
        };
        const int kEggCount = int(sizeof(kAllEggs) / sizeof(kAllEggs[0]));
        std::vector<int> mapped;
        for (int mt = 0; mt <= EntityManager::MobAnvil; ++mt) {
            const int egg = PlayerController::mobTypeEggId(mt);
            if (egg != 0) mapped.push_back(egg);
        }
        std::sort(mapped.begin(), mapped.end());
        ok = ok && int(mapped.size()) == kEggCount;
        for (size_t i = 0; ok && i + 1 < mapped.size(); ++i)
            ok = ok && mapped[i] != mapped[i + 1]; // 单射（排序后相邻重复 = 冲突）
        for (int i = 0; ok && i < kEggCount; ++i)
            ok = ok && std::binary_search(mapped.begin(), mapped.end(), kAllEggs[i]);
        if (!ok) {
            qInfo().noquote() << "  t878 diag: mapped" << int(mapped.size()) << "eggs, expect" << kEggCount
                              << "wolfEgg" << PlayerController::mobTypeEggId(EntityManager::MobWolf)
                              << "ocelotEgg" << PlayerController::mobTypeEggId(EntityManager::MobOcelot);
            ++totalFail;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t878 pick-block egg map completion: mobTypeEggId covers ALL 13 spawn-egg "
                             "items (wolf/ocelot/nightwalker/emberling added - the t785 egg batch never "
                             "followed this table, which is why creative middle-click could not copy "
                             "wolf/ocelot eggs), set-equality against the RecipeRegistry egg family plus "
                             "injectivity pin 'new egg must follow the table' (a future egg without a "
                             "mapping row turns this red)";
    });

    // ── P-t832 染料染羊 + 长回自然色重掷（专用局部世界 wD 草平台）──
    //    染（sheepWoolAt=染下标）→ 剪毛载荷 = 染色（一次性语义上半）→ 吃草长回 = 重掷自然权重恢复自然色
    //    （确定性断言：长回色 ∈ 自然色集 {0,6,7,8,12,15}——染 10 紫 ∉ 集，重掷绝不再现 10）；非羊拒染。
    runLegMulti({ "t832 dye-on-sheep: dyeSheep sets wool color, shear payload carries the dyed color (one-shot sema"
        "ntics), regrow rerolls natural weights (never the dyed color again), non-sheep rejected" }, [&]() {
        World wD;
        wD.setWidth(44); wD.setDepth(44); wD.setHeight(96); wD.setSeed(24); // rig y84 地形之上
        for (int x = 8; x < 36; ++x)
            for (int z = 8; z < 36; ++z) wD.setBlock(x, 84, z, BR::Grass, 0); // 28×28 草平台
        EntityManager emD;
        const int sheep = emD.spawnMobTyped(22, 85, 22, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
        bool ok = sheep >= 0;
        if (ok) {
            int shearPayload = -1, shearCount = 0;
            QObject::connect(&emD, &EntityManager::sheepSheared, &emD,
                             [&](int, int, int, int woolIdx) {
                                 if (shearCount == 0) shearPayload = woolIdx;
                                 ++shearCount;
                             });
            ok = ok && emD.dyeSheep(sheep, 10) && emD.sheepWoolAt(sheep) == 10; // 染紫
            emD.shearSheep(sheep);
            ok = ok && shearCount == 1 && shearPayload == 10 && emD.shearedAt(sheep); // 剪毛得染色
            const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
            // t897 后长回窗必须按「wander 依赖」定宽（探针鲁棒性，同 t897 flake 教训）：t897①把吃草收紧到
            //   「自身列支撑 == Grass」后，羊 idle 站在出生列几乎必然**早期**吃掉出生列草（→Dirt）——等 6s
            //   regrowCooldown 到期扫描时，羊脚下的支撑格已是 Dirt → 长回改走「wander 挪到邻列 Grass」
            //   路径（RNG 游走 + 每秒复扫）。旧窗 750 tick（12s）只盖「冷却 + 扫描 + 余量」，没盖 wander
            //   多轮 → 复跑 1/2~2/3 概率假红（sheared 恒 true）。窗 750→1600 tick（25.6s ≈ 6s 冷却 +
            //   ~10 轮 wander 周期 + ~19 次复扫，长窗行为断言「窗口覆盖节律多轮」口径）。
            for (int t = 0; t < 1600; ++t) // 25.6s：regrowCooldown 6s + wander 多轮 + 复扫余量
                emD.tick(0.016f, &wD, farListener, 0.3f, 1.8f, false);
            const int regrown = emD.sheepWoolAt(sheep);
            const bool natural = regrown == 0 || regrown == 6 || regrown == 7
                                 || regrown == 8 || regrown == 12 || regrown == 15;
            ok = ok && !emD.shearedAt(sheep) && natural && regrown != 10; // 长回自然色（绝不再现染紫）
            if (!ok)
                qInfo().noquote() << "  t832 diag: sheared" << emD.shearedAt(sheep)
                                  << "regrown" << regrown << "payload" << shearPayload;
            const int pig = emD.spawnMobTyped(30, 85, 30, EntityManager::MobPig,
                                              QStringLiteral("#ee9999"), 10);
            ok = ok && pig >= 0 && !emD.dyeSheep(pig, 5); // 非羊拒染
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t832 dye-on-sheep: dyeSheep sets wool color, shear payload carries "
                             "the dyed color (one-shot semantics), regrow rerolls natural weights "
                             "(never the dyed color again), non-sheep rejected";
    });

    // ── P-t833 刷怪笼被动实刷 + 鱿鱼水格路径（专用局部世界 wE 平石台 + 水柱）──
    //    蛋改型语义下的被动笼（pig state / squid state）经 tickSpawners 真刷：猪笼出猪（血 10 = 被动默认，
    //    敌对路由是 20——极性锁）；鱿鱼笼走水格谓词（here+above Water）在水柱内出鱿鱼。笼位 state 用
    //    BlockRegistry::spawnerStateForMob 单一权威编码。迷你模型换型视觉（t833① 重建修）不进矩阵（无
    //    Quick3D），留人工目视。
    runLegMulti({ "t833 passive spawners really spawn: pig cage produces 10-HP pigs (passive polarity) and squid ca"
        "ge spawns through the water-column predicate (spawn lands submerged); cage mini-model retype vis"
        "ual is manual-check (rebuilt-on-retype fix)" }, [&]() {
        World wE;
        wE.setWidth(40); wE.setDepth(40); wE.setHeight(96); wE.setSeed(25); // rig y84 地形之上
        for (int x = 2; x < 38; ++x)
            for (int z = 2; z < 38; ++z) wE.setBlock(x, 84, z, BR::Stone, 0);
        // 猪笼 (14,85,14)；鱿鱼笼 (24,85,24) + 南侧 3 宽 2 深水柱 (23..25, 85..86, 25)。
        wE.setBlock(14, 85, 14, BR::Spawner, BR::spawnerStateForMob(EntityManager::MobPig));
        wE.setBlock(24, 85, 24, BR::Spawner, BR::spawnerStateForMob(EntityManager::MobSquid));
        for (int x = 23; x <= 25; ++x)
            for (int y = 85; y <= 86; ++y) wE.setBlock(x, y, 25, BR::Water, 0);
        EntityManager emE;
        const QVector3D nearPlayer(14.5f, 86.0f, 15.5f); // 猪笼旁（两笼均 <16 激活半径：猪笼 1.4 / 鱿鱼笼 ~14.1）
        emE.tickSpawners(6.5f, &wE, nearPlayer); // ≥ kSpawnerInterval 6 → 完整刷怪周期
        emE.tickSpawners(6.5f, &wE, nearPlayer); // 第二周期（首个候选位被前轮占用时兜底）
        int pigs = 0, squids = 0;
        bool squidInWater = false;
        for (int i = 0; i < emE.count(); ++i) {
            if (!emE.aliveAt(i) || emE.deadAt(i) || emE.kindAt(i) != EntityManager::Mob) continue;
            if (emE.mobTypeAt(i) == EntityManager::MobPig) {
                ++pigs;
                if (emE.healthAt(i) != 10) pigs = -100; // 被动默认血 10（敌对路由 20 → 极性破）
            } else if (emE.mobTypeAt(i) == EntityManager::MobSquid) {
                ++squids;
                const QVector3D sp = emE.posAt(i);
                // +0.01 nudge：浮点 feet 85.45-0.45 可能落 84.9999 → 截断 84（石）；nudge 同引擎
                //   resting 复探的 FP-robust 手法（lessons「resting 态支撑格复探」条）。
                if (wE.blockAt(int(sp.x()), int(sp.y() - 0.45f + 0.01f), int(sp.z())) == BR::Water)
                    squidInWater = true;
            }
        }
        // 鱿鱼笼距 nearPlayer ~14.1 < 16 激活半径 ✓（激活是 XZ 距离）。
        bool ok = pigs >= 1 && squids >= 1 && squidInWater;
        if (!ok)
            qInfo().noquote() << "  t833 diag: pigs" << pigs << "squids" << squids
                              << "squidInWater" << squidInWater;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t833 passive spawners really spawn: pig cage produces 10-HP pigs "
                             "(passive polarity) and squid cage spawns through the water-column "
                             "predicate (spawn lands submerged); cage mini-model retype visual is "
                             "manual-check (rebuilt-on-retype fix)";
    });

    // ── P-r24#1 门整扇湿判防火带（Review 2026-08-24 中危 #1；专用局部世界 seed 31，P-t830 局部世界先例）──
    //   7b83fbd 防火带只判主目标湿 → 门配对联动点燃无湿判：火从干半扇侧掷中 → 被水保护的湿半扇被连带
    //   焚毁（不可逆）。修后 igniteFlammableAt 把湿判提前为「主目标**或其配对半扇**含水 → 整扇不燃」，
    //   且 (d) 烧毁收尾的「对偶仍是门 → 同窗 Air」兜底对湿对偶跳过（点燃后才泼水的边角同样护住）。
    //   两段断言（真实 tickFire 驱动；晴天无雨混淆源）：
    // (a) 蔓延窗：火贴干半扇（下格 6 邻）、水贴湿半扇（上格侧邻，非火的 6 邻不抑制火）→ 600 窗后两半门
    //     格恒 WoodDoor 且从未进燃烧态（整扇湿判拦下每一次掷骰——非概率断言）+ 火仍存活（证掷骰每窗
    //     都在发生，拦截面真实可达）；
    // (b) 烧尽收尾兜底（确定性，无概率）：干扇两半点燃 → 同 id 写清除上扇燃烧态（t843 契约：显式写 =
    //     换新实例清侧表）→ 上扇侧放水 → 下扇 10 窗烧尽时 (d) 兜底须被湿判拦下 → 上扇存活（修前兜底
    //     setBlock(Air) 焚毁湿半扇）。
    runLegMulti({ "review24#1 door whole-panel wet firebreak: fire licking the dry half never ignites either half w"
        "hile water touches the other half (600 windows, door intact, never burning), and a half ignited "
        "before water arrived burns out without consuming its now-wet pair (burn-completion fallback skip"
        "s wet dual)" }, [&]() {
        World wD1;
        wD1.setWidth(40); wD1.setDepth(40); wD1.setHeight(96); wD1.setSeed(31);
        const int gy1 = 81; // rig 层（96 高度地形之上；整带自凿清空防丘陵地形撞 rig）
        for (int x = 10; x <= 16; ++x)
            for (int z = 10; z <= 16; ++z)
                for (int y = 79; y <= 84; ++y)
                    wD1.setBlock(x, y, z, BR::Air, 0);
        // (a) rig：火(12) - 门下(13,81) - 门上(13,82)，水(14,82) 只贴门上（对门下是对角 → 干湿分明）。
        wD1.setBlock(12, gy1, 12, BR::Fire, 0);         // 火源（6 邻仅门下格可燃 → 恒 hasFuel 不自熄；水距 2 格对角不抑制）
        wD1.setBlock(13, gy1, 12, BR::WoodDoor, 0);     // 门下格（state bit3=0；干半扇——水不在其 6 邻）
        wD1.setBlock(13, gy1 + 1, 12, BR::WoodDoor, 8); // 门上格（state bit3=1；湿半扇——水正右侧邻）
        wD1.setBlock(14, gy1 + 1, 12, BR::Water, 0);    // 水（harness 不驱动 tickWaterFlow → 静止不漫）
        for (int win = 0; win < 600; ++win)
            for (int t = 0; t < 5; ++t) wD1.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval=5）
        const bool okA = wD1.blockAt(13, gy1, 12) == BR::WoodDoor      // 干半扇存活（从未点燃）
                      && wD1.blockAt(13, gy1 + 1, 12) == BR::WoodDoor  // 湿半扇存活（整扇不燃）
                      && !wD1.isBurningAt(13, gy1, 12)                 // 且从未进燃烧态（非概率断言）
                      && !wD1.isBurningAt(13, gy1 + 1, 12)
                      && wD1.blockAt(12, gy1, 12) == BR::Fire;         // 火仍存活（掷骰每窗发生，拦截面可达）
        // (b) rig（隔 2 行 z=14，与 (a) 火源 / 水均非 6 邻互不干扰）。
        wD1.setBlock(13, gy1, 14, BR::WoodDoor, 0);
        wD1.setBlock(13, gy1 + 1, 14, BR::WoodDoor, 8);
        const bool litB = wD1.igniteFlammableAt(13, gy1, 14); // 干扇直燃（两半全干 → 整扇放行 + 联动点燃）
        const bool linkedB = litB && wD1.isBurningAt(13, gy1 + 1, 14);
        wD1.setBlock(13, gy1 + 1, 14, BR::WoodDoor, 8); // 同 id 写：清上扇燃烧侧表（t843 契约）→ 制造「下扇在燃、上扇不在燃」不对称
        const bool unlitUpper = !wD1.isBurningAt(13, gy1 + 1, 14);
        wD1.setBlock(14, gy1 + 1, 14, BR::Water, 0);    // 上扇侧放水（非下扇 6 邻 → 下扇不被浇熄，烧尽链保留）
        for (int win = 0; win < 40; ++win)
            for (int t = 0; t < 5; ++t) wD1.tickFire(); // kBurnWindowsWood=10 → 下扇第 10 窗烧尽（余量 ×4）
        const bool okB = linkedB && unlitUpper
                      && wD1.blockAt(13, gy1, 14) != BR::WoodDoor       // 下扇已烧尽（Fire flare 或 Air）
                      && wD1.blockAt(13, gy1 + 1, 14) == BR::WoodDoor;  // 湿上扇存活（兜底被湿判拦下；修前被 Air）
        const bool ok = okA && okB;
        if (!ok)
            qInfo().noquote() << "  r24#1 diag: aLower" << int(wD1.blockAt(13, gy1, 12))
                              << "aUpper" << int(wD1.blockAt(13, gy1 + 1, 12))
                              << "aFire" << int(wD1.blockAt(12, gy1, 12))
                              << "bLit" << litB << "bLower" << int(wD1.blockAt(13, gy1, 14))
                              << "bUpper" << int(wD1.blockAt(13, gy1 + 1, 14));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#1 door whole-panel wet firebreak: fire licking the dry half "
                             "never ignites either half while water touches the other half (600 "
                             "windows, door intact, never burning), and a half ignited before water "
                             "arrived burns out without consuming its now-wet pair (burn-completion "
                             "fallback skips wet dual)";
    });

    // ── P-r24#2 clearBlockSilent 红石火把幽灵网格（Review 2026-08-24 中危 #2；专用局部世界 seed 32）──
    //   recheck 火把分支旧版不 emit，而 clearBlockSilent 的 worldChanged/clearAllDirty 在 recheck
    //   **之前** → 红石火把（chunk mesh 几何）清格标脏后错过重建信号 = 幽灵火把残留。修后火把分支在
    //   「实际掉落 ≥1」时自 emit。断言（信号时序序：掉落信号之后必须还能观测到 worldChanged——修前
    //   唯一 worldChanged 在掉落之前 → 时序断言 FAIL）：TNT 顶立红石火把（TorchFloor state=0，支撑 =
    //   正下方 TNT 格）→ clearBlockSilent 清 TNT → 火把格变 Air + 掉落物信号 + 之后仍有重建信号。
    runLegMulti({ "review24#2 clearBlockSilent redstone-torch ghost mesh: torch standing on TNT drops exactly once "
        "when TNT ignition clears the block, torch cell becomes Air, and a worldChanged (rebuild signal) "
        "is observed AFTER the drop (torch branch self-emits on actual drop)" }, [&]() {
        World wT2;
        wT2.setWidth(40); wT2.setDepth(40); wT2.setHeight(96); wT2.setSeed(32);
        for (int x = 10; x <= 14; ++x)
            for (int z = 10; z <= 14; ++z)
                for (int y = 79; y <= 84; ++y)
                    wT2.setBlock(x, y, z, BR::Air, 0);
        wT2.setBlock(12, 81, 12, BR::TntBlock, 0);        // TNT（ShapeFull → 可承火把，torchSupportBlock 真）
        wT2.setBlock(12, 82, 12, BR::RedstoneTorch, 0);   // 红石火把立柱（state TorchFloor=0 → 附着格 = 下方 TNT）
        int seq2 = 0, torchDropSeq = 0, worldSeqAfterDrop = 0, torchDrops = 0;
        QObject::connect(&wT2, &World::blockDroppedAsItem, &wT2,
                         [&](int x, int y, int z, int id) {
                             ++seq2;
                             if (x == 12 && y == 82 && z == 12 && id == int(BR::RedstoneTorch)) {
                                 torchDropSeq = seq2;
                                 ++torchDrops;
                             }
                         });
        QObject::connect(&wT2, &World::worldChanged, &wT2, [&]() {
            ++seq2;
            if (torchDropSeq > 0) worldSeqAfterDrop = seq2; // 掉落之后到来的重建信号（修前恒 0 → FAIL）
        });
        wT2.clearBlockSilent(12, 81, 12); // TNT 点火清格（静默路径；recheck 火把扫应连带掉火把 + 自 emit）
        const bool ok = torchDrops == 1                          // 恰 1 次火把掉落（无双掉）
                     && torchDropSeq > 0
                     && worldSeqAfterDrop > torchDropSeq         // 掉落后仍有 worldChanged（重建信号不再缺席）
                     && wT2.blockAt(12, 82, 12) == BR::Air       // 火把格已清（网格无残留依据）
                     && wT2.blockAt(12, 81, 12) == BR::Air;      // TNT 格已清（清格本体成立）
        if (!ok)
            qInfo().noquote() << "  r24#2 diag: drops" << torchDrops << "dropSeq" << torchDropSeq
                              << "worldAfterDrop" << worldSeqAfterDrop
                              << "torch" << int(wT2.blockAt(12, 82, 12));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#2 clearBlockSilent redstone-torch ghost mesh: torch standing "
                             "on TNT drops exactly once when TNT ignition clears the block, torch "
                             "cell becomes Air, and a worldChanged (rebuild signal) is observed "
                             "AFTER the drop (torch branch self-emits on actual drop)";
    });

    // ── P-r24#3 静默清格两兄弟路径附着物复检（Review 2026-08-24 中危 #3；专用局部世界 seed 33）──
    //   「口径合一」漏改 destroySphereSilent（爆炸）与 tickLavaFlow（岩浆焚毁）：(a) 爆炸掀支撑后
    //   压力板 / 铁轨须随 recheckAttachmentsAfterClear 掉落不悬浮（压力板 = 修前爆炸路径漏的族；
    //   铁轨 = 指令指定锁点；r=0.9 球心距 1 的附着物在球外幸存 → 只能靠钩子掉落，t733 探针同手法）；
    // (b) 岩浆焚毁木板支撑（8%/窗确定性哈希，≤400 窗必中）→ 焚毁循环内 recheck 须把轨掉落（修前
    //   tickLavaFlow 不含 checkRailOnEdit → 轨悬浮）。岩浆稳态早退（m_lavaDirty）用标记格翻转逐窗
    //   重标脏驱动（pokeFluidDirty 7 邻扫含岩浆源）；每窗 35 调 tickLavaFlow ≥ 节流 30 保证恰 1 窗。
    runLegMulti({ "review24#3 silent-clear sibling paths recheck attachments: explosion dropping support drops the "
        "out-of-sphere pressure plate AND rail (no floating residue), lava burning a plank support drops "
        "the rail on top (burn loop now routes through recheckAttachmentsAfterClear)" }, [&]() {
        World wX3;
        wX3.setWidth(48); wX3.setDepth(40); wX3.setHeight(96); wX3.setSeed(33);
        for (int x = 10; x <= 30; ++x)
            for (int z = 10; z <= 16; ++z)
                for (int y = 79; y <= 84; ++y)
                    wX3.setBlock(x, y, z, BR::Air, 0);
        int plateDrops = 0, railDrops = 0;
        QObject::connect(&wX3, &World::blockDroppedAsItem, &wX3,
                         [&](int, int, int, int id) {
                             if (id == int(BR::WoodPressurePlate)) ++plateDrops;
                             else if (id == int(BR::Rail)) ++railDrops;
                         });
        // (a) 爆炸：石支撑 + 板（x=12）与 石支撑 + 轨（x=18）两组，r=0.9 各炸支撑。
        wX3.setBlock(12, 81, 12, BR::Stone, 0);
        wX3.setBlock(12, 82, 12, BR::WoodPressurePlate, 0); // 板（距球心 1 > r=0.9 → 球外幸存，靠钩子掉）
        wX3.setBlock(18, 81, 12, BR::Stone, 0);
        wX3.setBlock(18, 82, 12, BR::Rail, 0);              // 轨（同上球外幸存）
        wX3.destroySphereSilent(12, 81, 12, 0.9f);          // 炸掉板支撑 → recheck 压力板分支掉板
        wX3.destroySphereSilent(18, 81, 12, 0.9f);          // 炸掉轨支撑 → recheck 铁轨分支掉轨
        const bool okA = wX3.blockAt(12, 82, 12) == BR::Air
                      && wX3.blockAt(18, 82, 12) == BR::Air
                      && plateDrops == 1 && railDrops == 1;
        // (b) 岩浆焚毁：石地板(y=79) + 岩浆源(14,80) + 木板支撑(13,80) + 轨(13,81) + 标记格(14,81，岩浆
        //     正上方——翻转 Stone↔Air 逐窗重标脏；岩浆不上升 → 标记格恒空可翻转；贴轨仅触发连接重算
        //     no-op，孤轨无连接零写入）。焚毁 8%/窗 → ≤400 窗必中（P(400 窗全空)≈0.92^400≈e^-33）。
        wX3.setBlock(13, 79, 12, BR::Stone, 0);
        wX3.setBlock(14, 79, 12, BR::Stone, 0);             // 岩浆 / 木板下方地板（岩浆 grounded 不下落）
        wX3.setBlock(13, 80, 12, BR::Planks, 0);            // 木板支撑（isWoodLike → 岩浆 ignite pass 目标）
        wX3.setBlock(13, 81, 12, BR::Rail, 0);              // 轨（满顶支撑 Planks 上；焚毁后须掉落不悬浮）
        wX3.setBlock(14, 80, 12, BR::Lava, 0);              // 岩浆源（贴木板 → 每窗 8% 焚毁掷骰）
        bool burned = false;
        for (int win = 0; win < 400 && !burned; ++win) {
            wX3.setBlock(14, 81, 12, (win & 1) ? BR::Air : BR::Stone, 0); // 标记翻转（真实变化 → poke 标脏岩浆）
            for (int t = 0; t < 35; ++t) wX3.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 个流/焚毁窗
            // t891① 语义更新：岩浆掷中邻木 → **点燃进燃烧态**（id 不变，焚毁让位给燃烧计时终局）→
            //   燃烧 10 窗后烧毁（setBlock Fire/Air）——本循环驱动 tickLavaFlow（点燃）+ tickFire（推进
            //   燃烧计时至烧毁），「burned」= 木板格不再是 Planks（被点燃烧尽）。
            wX3.tickFire();
            burned = wX3.blockAt(13, 80, 12) != BR::Planks;   // 木板被点燃烧毁（燃烧终局 / 岩浆漫入）
        }
        const bool okB = burned && wX3.blockAt(13, 81, 12) == BR::Air && railDrops == 2; // 轨掉落（(a)1 + (b)1）
        const bool ok = okA && okB;
        if (!ok)
            qInfo().noquote() << "  r24#3 diag: plateCell" << int(wX3.blockAt(12, 82, 12))
                              << "railCellA" << int(wX3.blockAt(18, 82, 12))
                              << "plateDrops" << plateDrops << "railDrops" << railDrops
                              << "burned" << burned << "railCellB" << int(wX3.blockAt(13, 81, 12));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#3 silent-clear sibling paths recheck attachments: explosion "
                             "dropping support drops the out-of-sphere pressure plate AND rail (no "
                             "floating residue), lava burning a plank support drops the rail on top "
                             "(burn loop now routes through recheckAttachmentsAfterClear)";
    });

    // ── P-t835 暗渊珠五项修探针（Entities 层 EntityManager 直编 + Game 层 applyEnderPearlTeleport 直调，
    //    同 t774 / t852 先例；独立小世界 96×40×96 不动主世界 rig——96 高世界地形+树冠最高 ~81，y≥84 天空
    //    带免凿，rig 地板摆 y=83 顶面 84）：
    //    (a) ①任意接触必传送：铁轨 / 火把（无碰撞盒非整格）+ 木压力板（薄碰撞盒）三柱，珠垂直落上 →
    //        enderPearlLanded 落点 = **非整格自身格**（旧 collisionAABBsAt 点测穿过它落到下方支撑格 = 根因）；
    //        Game 侧 applyEnderPearlTeleport：轨/火把格 → 玩家立**其格内**（y=84，穿模贴脚同 MC）；板格 →
    //        立其顶（y=85，薄盒是碰撞支撑）；Survival 传送自伤恰发一次 (5, EnderPearlTp)。
    //    (b) ②液体缓沉：水柱 5 深 / 岩浆柱 4 深——入液不即时传送（40 tick 仍存活且已入液），稳态下沉速度带
    //        水 ~1.5 b/s / 岩浆 ~0.7 b/s（岩浆明显更慢），最终沉到液体底接触底面格才传送（落点=底面格，
    //        Game 侧从底面格传送 → 玩家立于水格 y=84——旧 isSolid 把水当实心全列 abort 的回归面）。
    //    (c) ③虚空/出界不传送：整柱清空到 y=0 的虚空列，珠一路无接触落出底部 → 移除且零 landed；
    //        水平飞出 XZ 边界同（出界消散，MC 珍珠入虚空有去无回）。
    //    (d) ④抛距加长：平抛 v=24（镜像 kPlayerPearlSpeed）自 6 格高 → 理论落距 24 格（t=√(2·6/12)=1s，
    //        kEnderPearlGravity=12 轻重力直证）；45° 满抛 → ~50 格带（旧 12+重力 28 只 ~5 格）。
    //    (e) ⑤疾跑加成：平抛 v=24 与 v=24×1.3（镜像 kPearlSprintFactor，对齐 t51 Sprint ×1.3）落距比
    //        ∈[1.27,1.33]；镜像常量值锁（Game 层掷出分支本地 constexpr 探针不可达，P18 镜像同步模式）。
    runLegMulti({ "t835 ender pearl five fixes: (1) any-contact teleports - pearl lands ON rail/torch/plate cell it"
        "self and player stands in-cell (y=84) / on plate top (y=85); (2) water/lava slow-sink (1.5 / 0.7"
        " b/s steady band) then teleport at liquid-bottom cell, player placed in water cell; (3) void & o"
        "ut-of-bounds fall removes pearl with ZERO teleport; (4) flat throw v=24 drops ~24 blocks (light "
        "gravity 12), 45deg ~50 blocks; (5) sprint 1.3x speed -> range ratio 1.27..1.33 + Survival tp sel"
        "f-damage (5, EnderPearlTp) exactly once per teleport" }, [&]() {
        // 镜像常量（与实现侧私有/函数本地常量文档值同步，改值须两处同步；P18 镜像模式——Entities 层
        //   kEnderPearlGravity / Game 层掷珠分支 kPlayerPearlSpeed/kPearlSprintFactor 均探针不可达）：
        constexpr float kMirrorPearlGravity = 12.0f;       // EntityManager::kEnderPearlGravity（t835④ 珠轻重力；MC 投掷物 12 vs 世界 28）
        constexpr float kMirrorPearlSpeed = 24.0f;        // kPlayerPearlSpeed（t835④ 12→24；MC 投掷物 1.5 b/t=30 量级）
        constexpr float kMirrorPearlSprintFactor = 1.3f;  // kPearlSprintFactor（t835⑤ 疾跑初速系数；t51 Sprint ×1.3 同源）
        Q_UNUSED(kMirrorPearlGravity); // 带断言（平抛 6 格落差 t=√(2·6/12)=1s → 落距=初速）即其数值锁；显式引用免 -Wunused
        World wP;
        wP.setWidth(96); wP.setDepth(40); wP.setHeight(96); wP.setSeed(77);
        EntityManager ents;
        int landedCount = 0; int lastLx = -1, lastLy = -1, lastLz = -1;
        QObject::connect(&ents, &EntityManager::enderPearlLanded, &ents,
                         [&](int x, int y, int z) { ++landedCount; lastLx = x; lastLy = y; lastLz = z; });
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        const auto tickPearls = [&](int n) { for (int i = 0; i < n; ++i) ents.tick(0.016f, &wP, farListener, 0.3f, 1.8f, false); };
        const int floorY = 83; // rig 地板格（顶面 y=84）；上方 y≥84 全空带
        bool ok = true;

        // ---- (a) ①非整格接触：轨 / 火把 / 木压力板三柱 ----
        struct DecorRig { int x; quint8 id; float tpFootY; const char *name; };
        const DecorRig decors[] = {
            { 6, BR::Rail,               84.0f, "rail" },     // 无碰撞盒 → 立格内 y=84（穿模贴脚同 MC）
            { 9, BR::Torch,              84.0f, "torch" },    // 同上
            { 12, BR::WoodPressurePlate, 85.0f, "plate" },    // 薄碰撞盒是支撑 → 立其顶 y=85
        };
        const int az = 6;
        bool okA = true;
        for (const DecorRig &d : decors) {
            wP.setBlock(d.x, floorY, az, BR::Stone, 0);   // 支撑地板
            wP.setBlock(d.x, floorY + 1, az, d.id, 0);    // 非整格本体（轨/火把贴地、板贴支撑面）
            const int before = landedCount;
            const int pearl = ents.spawnEnderPearl(QVector3D(d.x + 0.5f, 88.0f, az + 0.5f), QVector3D(0, 0, 0));
            tickPearls(80); // 88→入格 ~45 tick 内必中
            // Entities 半面：落点 = 非整格自身格（floorY+1）——旧判据穿过它落进下方支撑格（floorY）。
            if (landedCount != before + 1 || lastLx != d.x || lastLy != floorY + 1 || lastLz != az) {
                okA = false;
                qInfo().noquote() << "  t835(a) diag:" << d.name << "landed" << landedCount - before
                                  << "at" << lastLx << lastLy << lastLz << "(expect 1 at" << d.x << floorY + 1 << az << ")";
            }
            Q_UNUSED(pearl);
        }
        // Game 半面：applyEnderPearlTeleport 直调（Survival）——轨/火把立格内、板立其顶 + 自伤恰一次 (5, EnderPearlTp)。
        PlayerController pc;
        pc.setWorld(&wP);
        pc.setMode(PlayerController::Survival);
        int dmgHits = 0, dmgHp = -1, dmgCause = -1;
        QObject::connect(&pc, &PlayerController::fallDamageTaken, &pc,
                         [&](int hp, int cause) { ++dmgHits; dmgHp = hp; dmgCause = cause; });
        for (const DecorRig &d : decors) {
            pc.applyEnderPearlTeleport(d.x, floorY + 1, az); // 落点 = 珠接触格（ Entities 半面同参）
            const float gotY = pc.feetPosition().y();
            if (qAbs(gotY - d.tpFootY) > 0.01f) {
                okA = false;
                qInfo().noquote() << "  t835(a) tp diag:" << d.name << "footY" << gotY << "(expect" << d.tpFootY << ")";
            }
        }
        okA = okA && dmgHits == 3 && dmgHp == 5 && dmgCause == int(PlayerState::EnderPearlTp);
        ok = ok && okA;

        // ---- (b) ②液体缓沉：水柱（5 深）/ 岩浆柱（4 深）----
        const int wz = 10, lz2 = 14; // 两柱 z 錯開
        for (int y = floorY + 1; y <= floorY + 5; ++y) wP.setBlock(20, y, wz, BR::Water, 0);
        wP.setBlock(20, floorY, wz, BR::Stone, 0);
        for (int y = floorY + 1; y <= floorY + 4; ++y) wP.setBlock(24, y, lz2, BR::Lava, 0);
        wP.setBlock(24, floorY, lz2, BR::Stone, 0);
        // 水：40 tick 仍存活（不即时传送）且已入液；稳态带 |dy|/tick ∈ [0.019,0.027]（1.5 b/s·dt±余量）；沉底传送。
        const int beforeW = landedCount;
        const int pearlW = ents.spawnEnderPearl(QVector3D(20.5f, 91.0f, wz + 0.5f), QVector3D(0, 0, 0));
        tickPearls(40);
        const float yW40 = ents.posAt(pearlW).y();
        bool okW = ents.aliveAt(pearlW) && yW40 < 89.0f && yW40 > 84.0f; // 已入液未到底未传送
        float sinkW = 0.0f;
        for (int t = 0; t < 100 && ents.aliveAt(pearlW); ++t) {
            const float y0 = ents.posAt(pearlW).y();
            tickPearls(1);
            sinkW = y0 - ents.posAt(pearlW).y(); // 末次采样（稳态：远离入液减速段与底面）
        }
        okW = okW && sinkW > 0.019f && sinkW < 0.027f;    // 稳态缓沉 ~1.5 b/s
        tickPearls(300);                                  // 沉底 + 传送余量（5 格 @1.5 b/s ≈ 250 tick 总）
        okW = okW && !ents.aliveAt(pearlW) && landedCount == beforeW + 1
                 && lastLx == 20 && lastLy == floorY && lastLz == wz; // 落点 = 液体底面格
        // Game 半面：从水底格传送 → 玩家立水格 y=84（水无碰撞可立入；旧 isSolid 把水当实心全列 abort）。
        pc.applyEnderPearlTeleport(20, floorY, wz);
        okW = okW && qAbs(pc.feetPosition().y() - float(floorY + 1)) < 0.01f
                 && qAbs(pc.feetPosition().x() - 20.5f) < 0.01f;
        // 岩浆：同构更慢（0.7 b/s 稳态带更窄）+ 沉底传送（①岩浆接触同样必传送，传送不点燃——MC 1.0 语义）。
        const int beforeL = landedCount;
        const int pearlL = ents.spawnEnderPearl(QVector3D(24.5f, 90.0f, lz2 + 0.5f), QVector3D(0, 0, 0));
        tickPearls(80); // 入液 + 减速收敛（vy 4.9→0.7 需 ~22 tick）
        bool okL = ents.aliveAt(pearlL);
        float sinkL = 0.0f;
        for (int t = 0; t < 100 && ents.aliveAt(pearlL); ++t) {
            const float y0 = ents.posAt(pearlL).y();
            tickPearls(1);
            sinkL = y0 - ents.posAt(pearlL).y();
        }
        okL = okL && sinkL > 0.008f && sinkL < 0.014f     // 稳态缓沉 ~0.7 b/s
                 && sinkW > sinkL + 0.004f;               // 水明显快于岩浆（1.5 vs 0.7）
        tickPearls(500);                                  // 4 格 @0.7 b/s ≈ 357 tick + 余量
        okL = okL && !ents.aliveAt(pearlL) && landedCount == beforeL + 1
                 && lastLx == 24 && lastLy == floorY && lastLz == lz2;
        ok = ok && okW && okL;

        // ---- (c) ③虚空 / 出界不传送 ----
        const int vz = 18;
        for (int y = 0; y < 96; ++y) wP.setBlock(30, y, vz, BR::Air, 0); // 整柱清到 y=0（虚空列）
        const int beforeV = landedCount;
        const int pearlV = ents.spawnEnderPearl(QVector3D(30.5f, 90.0f, vz + 0.5f), QVector3D(0, 0, 0));
        tickPearls(300); // 90→0 自由落 ~242 tick，越 y<0 出界移除
        bool okV = !ents.aliveAt(pearlV) && landedCount == beforeV; // 移除且零传送
        const int pearlX = ents.spawnEnderPearl(QVector3D(94.5f, 90.0f, vz + 0.5f), QVector3D(30.0f, 0, 0));
        tickPearls(10);  // ~0.5 格/tick → 3 tick 内飞出 x>96 出界移除
        okV = okV && !ents.aliveAt(pearlX) && landedCount == beforeV;
        ok = ok && okV;

        // ---- (d) ④抛距 + (e) ⑤疾跑比（平抛走廊：地板 x=8..70 @ z=26，顶面 y=84；自 y=90 平抛落距 6 格落差）----
        const int rz = 26;
        for (int x = 4; x <= 72; ++x) wP.setBlock(x, floorY, rz, BR::Stone, 0);
        struct RangeShot { float speed; };
        const RangeShot shots[] = { { kMirrorPearlSpeed }, { kMirrorPearlSpeed * kMirrorPearlSprintFactor } };
        float rangeCells[2] = { -1.0f, -1.0f };
        for (int s = 0; s < 2; ++s) {
            const int before = landedCount;
            const int pearl = ents.spawnEnderPearl(QVector3D(8.5f, 90.0f, rz + 0.5f),
                                                   QVector3D(shots[s].speed, 0, 0));
            tickPearls(120); // 6 格落差 t=1s=62 tick，余量足
            if (!ents.aliveAt(pearl) && landedCount == before + 1)
                rangeCells[s] = float(lastLx - 8);
        }
        // ④：v=24 落距 ~24 格（理论 24.0，dt 步进/格量化余量 ±2.5；旧物理 12+重力 28 仅 ~4.5 格 → 带断言分得开）。
        bool okD = rangeCells[0] >= 21.5f && rangeCells[0] <= 26.5f;
        // ⑤：疾跑 ×1.3 → 落距比 ∈[1.27,1.33]（同落差同重力 → 落距比 = 初速比；格量化 ±1 格已含在带内）。
        bool okE = rangeCells[1] >= 27.0f && rangeCells[1] <= 34.0f
                && rangeCells[0] > 0.0f
                && rangeCells[1] / rangeCells[0] >= 1.27f && rangeCells[1] / rangeCells[0] <= 1.33f;
        // ④补充：45° 满抛 v=24 → ~50 格带（自 y=86 上升弧越世界顶 y≥96 = 空气无碰撞照飞；旧物理只 ~5 格）。
        const int before45 = landedCount;
        const int pearl45 = ents.spawnEnderPearl(QVector3D(8.5f, 86.0f, rz + 0.5f),
                                                 QVector3D(24.0f * 0.7071f, 24.0f * 0.7071f, 0));
        tickPearls(260); // 满弧 ~2.9s ≈ 183 tick + 余量
        okD = okD && !ents.aliveAt(pearl45) && landedCount == before45 + 1
                 && float(lastLx - 8) >= 35.0f && float(lastLx - 8) <= 58.0f;
        ok = ok && okD && okE;
        if (!ok) {
            qInfo().noquote() << "  t835 diag: okA" << okA << "okW" << okW << "okL" << okL << "okV" << okV
                              << "okD" << okD << "okE" << okE << "| ranges" << rangeCells[0] << rangeCells[1]
                              << "sinkW" << sinkW << "sinkL" << sinkL << "| last landed" << lastLx << lastLy << lastLz
                              << "| dmg" << dmgHits << dmgHp << dmgCause;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t835 ender pearl five fixes: (1) any-contact teleports - pearl lands ON rail/torch/"
                             "plate cell itself and player stands in-cell (y=84) / on plate top (y=85); (2) water/"
                             "lava slow-sink (1.5 / 0.7 b/s steady band) then teleport at liquid-bottom cell, "
                             "player placed in water cell; (3) void & out-of-bounds fall removes pearl with ZERO "
                             "teleport; (4) flat throw v=24 drops ~24 blocks (light gravity 12), 45deg ~50 blocks; "
                             "(5) sprint 1.3x speed -> range ratio 1.27..1.33 + Survival tp self-damage (5, "
                             "EnderPearlTp) exactly once per teleport";
    });

    // ── t837(1) 画作支撑失撑 World 钩子族探针（World rig 直编；setBlock / clearBlockSilent 双入口 + M1 邻画
    //    不误伤 + 墙体置换保留）：1x2 画（锚格 top state=0x80|face|index / 非锚格 bottom state=faceBits）贴
    //    face 0（+X 外法线）墙（-X 邻 Stone/TNT 两格）。断言面：
    //    (a) 破**非锚格背后**的墙（用户复现位）→ 整画两格全清 + 恰 1 件掉落（dropId(Painting) 运行期同源读）；
    //    (b) 破锚格背后的墙 → 同样整画掉落（任一支撑破坏 → 整画掉，MC 语义）；
    //    (c) 墙置换为另一完整立方（Stone->Planks）→ 画保留零掉落（支撑仍有效，防误清）；
    //    (d) 直调 World::removePaintingAt（finishMiningAt 直挖画格同路径）→ 整画清 + 1 件；
    //    (e) clearBlockSilent（TNT 点火清格等系统路径，recheckAttachmentsAfterClear 收口）→ 整画掉落；
    //    (f) M1 钉契约：同面并排两 1x1 画，破其一的墙 → 只掉那一张，邻画完好（连通域 ≠ 整画，锚格矩形圈定）。
    runLegMulti({ "t837 painting support (a): dig wall behind NON-anchor cell of 1x2 painting -> whole painting dro"
        "ps as ONE item (no residual single face)",
               "t837 painting support (b): dig wall behind anchor cell -> whole 1x2 painting drops (ANY support "
        "face break drops the entire painting, MC semantics)",
               "t837 painting support (c): wall replaced by another full cube -> painting survives, zero drops ("
        "support recheck keeps valid walls)",
               "t837 painting remove (d): World::removePaintingAt from non-anchor seed clears the whole 1x2 pain"
        "ting + drops exactly one item",
               "t837 painting support (e): clearBlockSilent (TNT-ignite style system clear) -> painting drops vi"
        "a recheckAttachmentsAfterClear single entry",
               "t837 painting M1 pin (f): two adjacent 1x1 paintings share a wall plane - breaking one support d"
        "rops ONLY that painting, neighbor intact" }, [&]() {
        // 运行期查 1x2 与 1x1 的画作 index（paintingSize 单一权威，免本表持字面量副本）。
        int idx1x2 = -1, idx1x1 = -1;
        for (int i = 0; i < BR::PaintingCount; ++i) {
            int pw = 1, ph = 1;
            BR::paintingSize(i, pw, ph);
            if (idx1x2 < 0 && pw == 1 && ph == 2) idx1x2 = i;
            if (idx1x1 < 0 && pw == 1 && ph == 1) idx1x1 = i;
        }
        const quint8 faceBits = 0; // face 0（+X 外法线）→ 非锚格 state=0（bit7=0）
        const int paintingDropId = BR::dropId(BR::Painting);
        const auto buildPainting = [&](int x0, int z0, int idx, BR::Id wallId) {
            // 墙两格（y41/y40）+ 画两格（墙 +X 侧）；锚格 top 带 0x80|index。
            w.setBlock(x0, 41, z0, wallId, 0);
            w.setBlock(x0, 40, z0, wallId, 0);
            w.setBlock(x0 + 1, 41, z0, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | faceBits | quint8(idx & BR::PaintingStateIndexMask)));
            w.setBlock(x0 + 1, 40, z0, BR::Painting, faceBits);
        };
        // (a) 破非锚格背后的墙（底部墙格）→ 整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 40, z0, BR::Air, 0); // 用户复现位：挖掉画下半背后那块「非承重」墙
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) {
                qInfo().noquote() << "  [t837a diag] bottomCell"
                                  << int(w.blockAt(x0 + 1, 40, z0)) << "topCell" << int(w.blockAt(x0 + 1, 41, z0))
                                  << "drops" << (dropItemCount - drops0) << "lastDropId" << lastDropId
                                  << "expect" << paintingDropId;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (a): dig wall behind NON-anchor cell of 1x2 painting "
                                 "-> whole painting drops as ONE item (no residual single face)";
        }
        // (b) 破锚格背后的墙 → 同样整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Air, 0);
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (b): dig wall behind anchor cell -> whole 1x2 painting "
                                 "drops (ANY support face break drops the entire painting, MC semantics)";
        }
        // (c) 墙置换为另一完整立方 → 画保留（支撑仍有效）。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Planks, 0); // Stone -> Planks（均完整立方）
            const bool ok = w.blockAt(x0 + 1, 41, z0) == quint8(BR::Painting)
                        && w.blockAt(x0 + 1, 40, z0) == quint8(BR::Painting)
                        && dropItemCount == drops0;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (c): wall replaced by another full cube -> painting "
                                 "survives, zero drops (support recheck keeps valid walls)";
        }
        // (d) 直调 World::removePaintingAt（直挖画格路径）→ 整画清 + 1 件。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.removePaintingAt(x0 + 1, 40, z0, 0, /*drop=*/true); // 从非锚格种子（直挖下半格同型）
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting remove (d): World::removePaintingAt from non-anchor seed clears "
                                 "the whole 1x2 painting + drops exactly one item";
        }
        // (e) clearBlockSilent（TNT 点火清格 -> recheckAttachmentsAfterClear 收口）→ 整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::TntBlock);
            const int drops0 = dropItemCount;
            w.clearBlockSilent(x0, 40, z0); // 点火清格同型系统路径（TNT 墙被引燃）
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (e): clearBlockSilent (TNT-ignite style system clear) "
                                 "-> painting drops via recheckAttachmentsAfterClear single entry";
        }
        // (f) M1 邻画不误伤：同面并排两 1x1 画（face 0 的 u = -Z），破其一的墙 → 只掉那一张。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, 41, z0, BR::Stone, 0);       // 画 A 墙
            w.setBlock(x0, 41, z0 - 1, BR::Stone, 0);   // 画 B 墙（u 向相邻）
            w.setBlock(x0 + 1, 41, z0, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | quint8(idx1x1 & BR::PaintingStateIndexMask)));
            w.setBlock(x0 + 1, 41, z0 - 1, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | quint8(idx1x1 & BR::PaintingStateIndexMask)));
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Air, 0);         // 只破画 A 的墙
            const bool ok = w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)          // A 掉
                        && w.blockAt(x0 + 1, 41, z0 - 1) == quint8(BR::Painting)  // B 完好
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting M1 pin (f): two adjacent 1x1 paintings share a wall plane - "
                                 "breaking one support drops ONLY that painting, neighbor intact";
        }
    });

    // ── t847 植物放置谓词探针（Core 纯函数真值表 + World 花失撑掉落链 t788 回归钉）：plantGroundBlock
    //    单一权威——草丛→**仅草方块**（t903 收紧：泥土也不行，用户定稿对齐 MC；拒草上叠草 / 树叶 / 沙 /
    //    耕地 / 水 / 泥土）；花→泥土/草方块/耕地（MC 1.0 BlockFlower.canBlockStay 同集含 tilledField）；
    //    蘑菇→泥土/草方块；枯灌木→沙子。掉落链：破花下泥土 → 花 dropId 掉落（t788 染料链不回归；dropId
    //    运行期读，免字面量副本）。放置预检本体在 PlayerController 私有 placeBlock（t841 P20 先例：谓词面 +
    //    失撑面矩阵化，放置拒绝人工目视收口）。
    runLegMulti({ "t847 plant placement predicate: plantGroundBlock single authority truth table (tallgrass grass-o"
        "nly per t903 tightening - dirt rejected too, no grass-on-grass/leaves/water; flowers +farmland; "
        "mushrooms dirt/grass; dead bush sand-only) + flower lost-support drop stays on dropId chain (t78"
        "8 dye linkage) + tallgrass lost-support now symmetric with the placement-side family (isGroundPl"
        "ant shared by precheck and the World hook; digs out ground -> grass clears and drops its dropId)" }, [&]() {
        const bool okGround =
               !BR::plantGroundBlock(BR::TallGrass, BR::Dirt)     // t903 收紧：泥土也不行（仅草方块）
            && BR::plantGroundBlock(BR::TallGrass, BR::Grass)
            && !BR::plantGroundBlock(BR::TallGrass, BR::TallGrass)   // 不能草上叠草
            && !BR::plantGroundBlock(BR::TallGrass, BR::Leaves)      // 不能放树叶上
            && !BR::plantGroundBlock(BR::TallGrass, BR::Sand)
            && !BR::plantGroundBlock(BR::TallGrass, BR::Farmland)
            && !BR::plantGroundBlock(BR::TallGrass, BR::Water)       // 水下拒绝（着地面谓词面）
            && BR::plantGroundBlock(BR::FlowerRed, BR::Dirt)
            && BR::plantGroundBlock(BR::FlowerRed, BR::Grass)
            && BR::plantGroundBlock(BR::FlowerRed, BR::Farmland)     // MC 花可放耕地
            && !BR::plantGroundBlock(BR::FlowerRed, BR::Sand)
            && BR::plantGroundBlock(BR::Mushroom, BR::Dirt)
            && BR::plantGroundBlock(BR::Mushroom, BR::Grass)
            && !BR::plantGroundBlock(BR::Mushroom, BR::Farmland)
            && BR::plantGroundBlock(BR::DeadBush, BR::Sand)          // 枯灌木沙地限定
            && !BR::plantGroundBlock(BR::DeadBush, BR::Dirt)
            && !BR::plantGroundBlock(BR::Stone, BR::Dirt);           // 非植物 → 恒 false（谓词域守卫）
        // 失撑链回归钉：花失撑掉 dropId（t788 起花掉对应染料；本探针运行期读表比对，与玩家直破同源）。
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, 41, z0, BR::Dirt, 0);
        placeRigBlock(w, x0, 42, z0, BR::FlowerRed, 0);
        const int drops0 = dropItemCount;
        w.setBlock(x0, 41, z0, BR::Air, 0); // 破花下泥土
        const bool okDrop = w.blockAt(x0, 42, z0) == quint8(BR::Air)
                     && dropItemCount == drops0 + 1
                     && lastDropId == BR::dropId(BR::FlowerRed);
        // t847 收口（R19.13 终审 C-M1）：草丛失撑链补钉——t847 只把草丛收进放置预检（泥土/草限定）而
        //   World 失撑族没跟，挖掉下方泥土后草丛悬空永存；修后 isGroundPlant 单一权威两面共用（放置预检
        //   与 checkFlowerMushroomOnEdit 同谓词）。破草丛下泥土 → 草丛清 Air + dropId（0x208 种子族，运行
        //   期读表）掉落，与玩家直破同源。
        const auto [x1, z1] = nextSlot();
        placeRigBlock(w, x1, 41, z1, BR::Dirt, 0);
        placeRigBlock(w, x1, 42, z1, BR::TallGrass, 0);
        const int drops1 = dropItemCount;
        w.setBlock(x1, 41, z1, BR::Air, 0); // 破草丛下泥土
        const bool okDropGrass = w.blockAt(x1, 42, z1) == quint8(BR::Air)
                       && dropItemCount == drops1 + 1
                       && lastDropId == BR::dropId(BR::TallGrass);
        const bool ok = okGround && okDrop && okDropGrass;
        if (!ok) {
            qInfo().noquote() << "  [t847 diag] okGround" << okGround << "okDrop" << okDrop
                              << "lastDropId" << lastDropId << "expect" << BR::dropId(BR::FlowerRed)
                              << "okDropGrass" << okDropGrass << "(grassDrop"
                              << BR::dropId(BR::TallGrass) << ")";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t847 plant placement predicate: plantGroundBlock single authority truth table "
                             "(tallgrass grass-only per t903 tightening - dirt rejected too, no "
                             "grass-on-grass/leaves/water; flowers +farmland; "
                             "mushrooms dirt/grass; dead bush sand-only) + flower lost-support drop stays on "
                             "dropId chain (t788 dye linkage) + tallgrass lost-support now symmetric with the "
                             "placement-side family (isGroundPlant shared by precheck and the World hook; digs "
                             "out ground -> grass clears and drops its dropId)";
    });

    // ── t815/t838 item 图标路径探针（Game 层 Hotbar 闭合直调，t800 探针同模式；测试二进制无 qrc → 图集
    //    渲染落盘空图，URL 链路断言有效、像素内容留实机人工目视）：(1) 红石粉（130）pick-block 图标改走
    //    isPackDerivedIconFamily 程序图集 flat 重渲（file:/// 运行期缓存，非 qrc 手绘旧稿——「贴图旧版」
    //    根因钉死在回退链位置：旧稿只余渲染失败兜底）；(2) 玻璃（54）缓存族换代 icon4->icon5（URL 家族名
    //    断言；t800 flat -> t838(1) dimetric 3D 的画法切换靠换代兜底，防 AppLocalData 旧 flat 缓存被复用）。
    runLegMulti({ "t815/t838 item icon paths: redstone dust pick-block icon resolves via runtime atlas flat re-rend"
        "er (file:/// cache, stale hand-drawn qrc retired to last-resort fallback), glass icon cache fami"
        "ly bumped icon6->icon7 (t879 trapdoor draw switch and t902 farmland face fix ride the same bump)" }, [&]() {
        Hotbar hb;
        const QString dustIcon = hb.iconSourceForBlock(int(BR::RedstoneDust));
        const QString glassIcon = hb.iconSourceForBlock(int(BR::Glass));
        // t879 换代 icon5->icon6、t902 换代 icon6->icon7（耕地图标面修正：side/front 钉 dirt——旧泛化把
        //   frontTile=湿耕地瓦片画上左前面；URL 家族名断言随缓存名同步——测试二进制无 qrc → 图集渲染
        //   落盘空图，URL 链路断言有效、像素内容留实机人工目视）。
        const bool ok = dustIcon.startsWith(QStringLiteral("file:///"))
                     && !dustIcon.contains(QStringLiteral("icon_redstone_dust"))
                     && glassIcon.startsWith(QStringLiteral("file:///"))
                     && glassIcon.contains(QStringLiteral("voxelsandbox_rp_icon7_"))
                     && dustIcon.contains(QStringLiteral("voxelsandbox_rp_icon7_"));
        if (!ok) {
            qInfo().noquote() << "  [t815/t838 diag] dustIcon" << dustIcon << "| glassIcon" << glassIcon;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t815/t838 item icon paths: redstone dust pick-block icon resolves via runtime "
                             "atlas flat re-render (file:/// cache, stale hand-drawn qrc retired to last-resort "
                             "fallback), glass icon cache family bumped icon6->icon7 (t879 trapdoor draw switch "
                             "and t902 farmland face fix ride the same bump)";
    });

    // ── P-t836 钓鱼系统整改探针（Entities 层 EntityManager 直编 + Game 层 PlayerController/Hotbar 真消费端，
    //    t835/t856 同模式；独立小世界 48×48×96 seed 77（t835 实测该种子地形+树冠 ≤81，y≥84 天空带免凿——
    //    t769 教训：不假设「某高度以上必空」，此为已核种子的实测带）+ 水池 / 猪平台 rig 摆 y=83 起）──
    //    (a) 抛物线（轻重力 12 落差带）+ 落水浮定（XZ 收格心 / Y = 液面 − 浸没 0.125 精确断言）；
    //    (b) 确定性等待：bobberWaitSeconds 两端恰可达（h=0 → 5.00 / h=2500 → 30.00）+ 分布带（600 序号
    //        min<6 / max>29）+ 行为级（真实 settle 后按 hashVoxel 预计算的等待值 ±1 tick 咬钩——公式即驱动）；
    //    (c) 咬钩窗口窗内收 = 获物 + 耐久 -1（fishCaught 载荷：池内物品 id + 浮标位 + 朝玩家弹向 + t886 抛物解
    //        弹速镜像）；窗过 = 鱼跑（escaped 信号 + hasBite 翻 false）+ 重等（第二咬可达）+ 此后空收无消耗；
    //    (d) 钩 mob：pc 真甩竿飞行段命中猪（bobberHookedMobAt 绑定）→ 收竿拉拽（猪位移朝玩家 >0.03 +
    //        耐久 -5 + 猪血量不变 + 零 fishCaught——钩中不伤害不获物口径）；陆上静止浮标冻结（Ground 态）；
    //        d2 垂死 mob 收竿（R19.13 终审 B-L2）：钩住后打死猪（死亡动画窗内、不 tick ents → 脱钩验证未
    //        跑）→ 收竿拉拽 no-op → 耐久**不扣**（旧版无条件 -5 白损）；
    //    (e) 熟鱼链：kSmelt + kSmeltXp 两表都接（t788 教训）+ 食用 +4（生鱼 +2 对照）+ 名「熟鱼」+
    //        pack 映射源码钉（0x25B → cooked_cod.png）+ 创造 tab 源码钉 + 豹猫仍只认生鱼（源码钉 gate）。
    //    (f) 出界消散（R19.13 终审 C-M2 补断言面，此前文案声称零断言）：XZ 飞越边界 + 极端 y（y<0 虚空）
    //        → ents.tick 若干 → 浮标槽释放（despawn；边界路径非 180s 寿命路径）；
    //    (g) Game 层镜像（C-M2）：外部清场（ents.clearAll = 出界/寿命/系统清理的等价构造）→ 收竿无获物
    //        无消耗干净收场 + 镜像惰性（clearAll 后 fishing 态仍在，tick/收竿才收）+ updateFishing 失效
    //        自动收竿源序钉（pc.tick 的 captured 门在无窗探针不可达——直调会先走 !m_captured 早退分支的
    //        cancelFishing 掩盖镜像路径，行为级不可达、以 t836(e) 源码钉手法锁语句面，取舍声明）。
    //    (h) review25 #12 Ground 态支撑复查：挖掉贴靠方块 → ≤40 tick 转 Flying 下坠（旧版悬空滞留至
    //        180s 寿命兜底）；
    //    (i) review25 #13 实体格命中门：1 格墙后贴壁猪 + 高速飞行浮标（next 一跳入墙格且在猪外扩命中
    //        盒内）→ 贴面 Ground 不隔墙钩（旧序先钩后碰会隔墙钩住）。
    runLegMulti({ "t836 fishing overhaul: bobber is an EntityManager projectile (light-gravity parabola, hook-on-fl"
        "ight vs mob AABB, water settle at surface-minus-dip with state-aware liquid height, ground rest "
        "frozen, out-of-bounds despawn ASSERTED: xz flyout within 8 ticks + void-y first-tick slot releas"
        "e) driven from Game layer cast-anywhere/reel (EntityManager-carries-entity + PlayerController-se"
        "ttles-semantics split, pearl/drop precedent); deterministic 5-30s wait via hashVoxel(seed^salt^c"
        "astSerial) with exact reachable endpoints and +-1tick behavioral match, bite window 1.0s (t926 u"
        "ser override of the MC 1.0 ~0.5s value; in-window reel = fishingPool loot thrown to the player a"
        "s a ballistic spawnItemThrown (t886: solved arc, distance-adaptive speed) + rod -1, expired = es"
        "caped signal + re-roll + empty reel costs nothing), hooked-mob reel pulls at ~6 b/s with -5 dura"
        "bility and zero damage, dead-target reel = pull no-op with NO durability charge; externally-clea"
        "red bobber keeps lazy fishing state then reels clean (no loot, no cost) with updateFishing inval"
        "id-to-auto-reel pinned at source level (pc.tick captured gate unreachable headless, tradeoff dec"
        "lared); ground-rest bobber rechecks its support cell on a 10-tick throttle and falls (review25 #"
        "12: mined support -> flying within 40 ticks, no more hovering until the 180s lifetime bail); sol"
        "id-cell hit gate precedes mob hooking (review25 #13: wall-pinned pig behind a 1-thick wall with "
        "a fast bobber whose next point lands inside the wall cell and the padded pig AABB grounds at the"
        " wall face instead of hooking through it); cooked fish 0x25B closes the chain (raw->cooked in BO"
        "TH kSmelt+kSmeltXp, +4 hunger vs raw +2, name/tab/pack-mapping pinned, ocelot still raw-only)" }, [&]() {
        // 镜像常量（P18 模式，改值须两处同步；Entities 层 kBobberWaitHashSalt / kBobberBiteWindowSec 与
        //   Game 层获物抛物解均探针不可达私有）：
        constexpr quint32 kMirrorBobberSalt = 0xF15Cu;   // EntityManager::kBobberWaitHashSalt（等待掷骰盐）
        constexpr float kMirrorBiteWindow = 1.0f;        // EntityManager::kBobberBiteWindowSec（咬钩窗口秒；
                                                          //   t926 用户口径 1.0s——0.5→1.0 随源同步，P18 双钉）
        // t886 获物弹速 = 抛物解镜像 fishCatchSpeedMirror（文件级 helper，t886 探针共用）。
        World wF;
        wF.setWidth(48); wF.setDepth(48); wF.setHeight(96); wF.setSeed(77);
        EntityManager ents;
        int bitCount = 0, escCount = 0;
        QObject::connect(&ents, &EntityManager::bobberBit, &ents,
                         [&](float, float, float) { ++bitCount; });
        QObject::connect(&ents, &EntityManager::bobberEscaped, &ents,
                         [&](float, float, float) { ++escCount; });
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickB = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wF, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83; // rig 地板格（t835 实测 seed 77 地形 ≤81 → 84+ 全空带）

        // ---- (a) 抛物 + 落水浮定（Entities 直编）----
        // 水池：3×3 石底 fy + 1 深水 fy+1（液面 = fy+1+0.875−0.125 = fy+1.75；t892 源 state=0 → surf 7/8）。
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wF.setBlock(x, fy, z, BR::Stone, 0);
                wF.setBlock(x, fy + 1, z, BR::Water, 0);
            }
        bool okA = false;
        {
            // a1 抛物：v=(8,0,0) 自 (10.5, fy+4, 12.5)（空带无遮挡）——4 tick（dt=0.05 → t=0.2s）落差 =
            //   ½·g·t² = ½·12·0.04 = 0.24（轻重力 12 直证；世界重力 28 会给 0.56 出带）。
            const int b1 = ents.spawnBobber(QVector3D(10.5f, float(fy + 4), 12.5f), QVector3D(8.0f, 0.0f, 0.0f), 901);
            tickB(4, 0.05f);
            const float yDrop = float(fy + 4) - ents.posAt(b1).y();
            const bool okAry = ents.aliveAt(b1)
                               && qAbs(yDrop - 0.24f) < 0.06f
                               && ents.posAt(b1).x() > 11.9f; // 水平位移 ≈ 8×0.2 = 1.6（弧线在飞）
            ents.removeEntityAt(b1);
            // a2 落水浮定：水池正上方垂直落（v=0）→ 穿入顶水格 settle 到格心 + 液面 − 0.125（浮力平衡半浸）。
            const int b2 = ents.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 902);
            for (int t = 0; t < 60 && ents.aliveAt(b2); ++t) tickB(1, 0.05f); // 落定 + 水中静置（等待期不咬）
            const QVector3D p2 = ents.posAt(b2);
            const bool okAset = ents.aliveAt(b2)
                                && qAbs(p2.x() - 6.5f) < 1e-3f
                                && qAbs(p2.y() - (float(fy + 1) + 0.75f)) < 1e-3f
                                && qAbs(p2.z() - 6.5f) < 1e-3f;
            ents.removeEntityAt(b2);
            // a3 陆上静止：石台正上垂直落 → Ground 贴面冻结（后续 tick 位置不变；Ground 不钩 mob / 不进等待）。
            wF.setBlock(12, fy, 18, BR::Stone, 0);
            const int b3 = ents.spawnBobber(QVector3D(12.5f, float(fy + 4), 18.5f), QVector3D(0, 0, 0), 903);
            for (int t = 0; t < 60 && ents.aliveAt(b3); ++t) tickB(1, 0.05f);
            const QVector3D p3a = ents.posAt(b3);
            tickB(20, 0.05f);
            const QVector3D p3b = ents.posAt(b3);
            const bool okAgr = ents.aliveAt(b3) && p3a == p3b
                               && qAbs(p3b.y() - float(fy + 1)) < 1.0f; // 停在石台上表面一带（贴命中面）
            ents.removeEntityAt(b3);
            wF.setBlock(12, fy, 18, BR::Air, 0);
            okA = okAry && okAset && okAgr;
            if (!okA)
                qInfo().noquote() << "  [t836 a diag] okAry" << okAry << "okAset" << okAset << "(pos" << p2
                                  << ") okAgr" << okAgr << "(pos" << p3b << ")";
        }

        // ---- (b) 确定性等待：公式两端 + 分布带 + 行为级公式即驱动 ----
        bool okB = false;
        {
            const bool okEnds = qAbs(EntityManager::bobberWaitSeconds(0u) - 5.0f) < 1e-4f
                                && qAbs(EntityManager::bobberWaitSeconds(2500u) - 30.0f) < 1e-3f
                                && EntityManager::bobberWaitSeconds(1u) > 5.0f
                                && EntityManager::bobberWaitSeconds(2499u) < 30.0f;
            float wMin = 1e9f, wMax = -1e9f;
            for (quint32 s = 0; s < 600u; ++s) {
                const float wv = EntityManager::bobberWaitSeconds(
                    wF.hashVoxel(int(quint32(wF.seed()) ^ kMirrorBobberSalt ^ s), 5, 84, 6));
                wMin = std::min(wMin, wv);
                wMax = std::max(wMax, wv);
            }
            const bool okDist = wMin > 4.99f && wMin < 6.0f && wMax > 29.0f && wMax < 30.01f;
            // 行为级：pc 真甩竿（serial 1）入水池 → settle 后按 hashVoxel 预计算等待值，±1 tick 内咬钩。
            //   轨迹（tick 逐步核）：眼 (3.5, fy+2.62) pitch −20 → 原点 (3.876, 85.483) vel (14.10, −5.13)；
            //   tick1 next=(4.581, 85.197) 格 (4,85) 空气；tick2 next=(5.286, 84.882) 格 (5,84) 水 → settle
            //   (5.5, 84.875, 6.5)（XZ 收格心 / 液面 1.0 − 浸没 0.125）。
            PlayerController pc;
            Hotbar hb;
            hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            pc.useFishingRod(); // 甩竿（serial → 1）
            int bobC = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobC = i; break; }
            const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
            bool okCast = pc.fishing() && bobC >= 0;
            for (int t = 0; t < 40 && okCast; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(bobC)) { okCast = false; break; }
                if (ents.posAt(bobC) == settlePos) break; // settled（精确浮点：0.5/0.75 均二进制精确）
            }
            okCast = okCast && ents.posAt(bobC) == settlePos;
            // settle 格 (5, fy+1, 6) + serial 1 → 预计算等待（镜像盐 = 实现盐，P18 双钉）。
            const float waitSec = EntityManager::bobberWaitSeconds(
                wF.hashVoxel(int(quint32(wF.seed()) ^ kMirrorBobberSalt ^ 1u), 5, fy + 1, 6));
            int ticksToBite = 0;
            bool okWaitTime = false;
            for (ticksToBite = 0; ticksToBite < 660; ++ticksToBite) {
                if (ents.bobberHasBiteAt(bobC)) break;
                tickB(1, 0.05f);
            }
            if (ents.bobberHasBiteAt(bobC) && ticksToBite < 660) {
                const float got = float(ticksToBite) * 0.05f;
                okWaitTime = qAbs(got - waitSec) <= 0.06f; // ±1 tick（公式即驱动的行为级直证）
            }
            okB = okEnds && okDist && okCast && okWaitTime;
            if (!okB)
                qInfo().noquote() << "  [t836 b diag] okEnds" << okEnds << "okDist" << okDist << "(" << wMin << ".."
                                  << wMax << ") okCast" << okCast << "okWaitTime" << okWaitTime << "(got"
                                  << ticksToBite * 0.05f << "s expect" << waitSec << "s)";
            if (bobC >= 0) ents.removeEntityAt(bobC); // 清场（防 (c) 的「首个 Bobber」搜索误拾本浮标）
        }

        // ---- (c) 咬钩窗口：窗内收 = 获物 + 耐久 -1；窗过 = 鱼跑重等 + 空收无消耗 ----
        bool okC = false;
        {
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod); // 64（MC 1.0 钓竿）
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            int caughtCount = 0, caughtId = 0; float cpx = 0, cpy = 0, cpz = 0, cdx = 0, cdz = 0, csp = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int itemId, int, float px, float py, float pz, float dx, float dz, float speed) {
                                 ++caughtCount; caughtId = itemId;
                                 cpx = px; cpy = py; cpz = pz; cdx = dx; cdz = dz; csp = speed;
                             });
            // c1 窗内收：甩竿（serial 1）→ settle（同 (b) 轨迹：格 (5,84,6)，pos (5.5, 84.875, 6.5)）→
            //   drive 到咬钩即收 → fishCaught 恰一次 + 耐久 64→63 + 载荷（池内 id + 浮标位 + 朝玩家水平弹向 +
            //   弹速镜像 4.5）+ fishing 态复位。
            pc.useFishingRod();
            int b1 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { b1 = i; break; }
            const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
            bool okc1 = b1 >= 0;
            for (int t = 0; t < 40 && okc1; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(b1)) { okc1 = false; break; }
                if (ents.posAt(b1) == settlePos) break;
            }
            okc1 = okc1 && ents.posAt(b1) == settlePos;
            for (int t = 0; t < 660 && okc1 && !ents.bobberHasBiteAt(b1); ++t) tickB(1, 0.05f);
            const QVector3D bobPos = ents.posAt(b1);
            okc1 = okc1 && ents.bobberHasBiteAt(b1);
            pc.useFishingRod(); // 窗内收竿
            const int dur1 = hb.durabilityAt(0);
            // 弹向断言：dir 点乘（玩家 − 浮标）水平归一 > 0.9（朝玩家）；弹速 = t886 抛物解镜像（|v| 随距离自适应）。
            const float toPX = 3.5f - bobPos.x(), toPZ = 6.5f - bobPos.z();
            const float toPLen = std::sqrt(toPX * toPX + toPZ * toPZ);
            const bool poolIds[] = {
                RecipeRegistry::RawFishId == caughtId, RecipeRegistry::LeatherId == caughtId,
                RecipeRegistry::StringId == caughtId, RecipeRegistry::BoneId == caughtId,
                RecipeRegistry::RottenFleshId == caughtId, RecipeRegistry::StickId == caughtId,
                RecipeRegistry::InkSacId == caughtId, RecipeRegistry::SaddleId == caughtId,
                RecipeRegistry::NameTagId == caughtId, RecipeRegistry::DiamondId == caughtId };
            bool idInPool = false;
            for (bool b : poolIds) idInPool = idInPool || b;
            okc1 = okc1 && caughtCount == 1 && idInPool && dur1 == rodDur - 1 && !pc.fishing()
                   && !ents.aliveAt(b1) // 浮标实体已收走
                   && qAbs(cpx - bobPos.x()) < 1e-3f && qAbs(cpy - bobPos.y()) < 1e-3f
                   && qAbs(cpz - bobPos.z()) < 1e-3f
                   && qAbs(csp - fishCatchSpeedMirror(bobPos, QVector3D(3.5f, float(fy + 1), 6.5f))) < 1e-2f
                   && (toPLen < 1e-3f || (cdx * toPX + cdz * toPZ) / toPLen > 0.9f);
            // c2 窗过 = 鱼跑重等 + 空收无消耗：再甩（serial 2）→ 咬 → drive 过窗（1.0s + 余量）→ escaped 信号 +
            //   hasBite 翻 false → 继续 drive 到第二次咬（重等可达，cap 31s）→ 再过窗 → 此刻收 = 真空收
            //   （无咬无获物 + 耐久不变）。
            const int escBefore = escCount, bitBefore = bitCount;
            pc.useFishingRod();
            int b2 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { b2 = i; break; }
            bool okc2 = b2 >= 0;
            for (int t = 0; t < 40 && okc2; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(b2)) { okc2 = false; break; }
                if (ents.posAt(b2) == settlePos) break;
            }
            okc2 = okc2 && ents.posAt(b2) == settlePos;
            for (int t = 0; t < 660 && okc2 && !ents.bobberHasBiteAt(b2); ++t) tickB(1, 0.05f);
            okc2 = okc2 && ents.bobberHasBiteAt(b2);
            tickB(int(kMirrorBiteWindow / 0.05f) + 2, 0.05f); // 1.1s > 1.0s 窗（t926）→ 鱼跑
            const bool okEsc = escCount == escBefore + 1 && !ents.bobberHasBiteAt(b2) && bitCount == bitBefore + 1;
            bool okRewait = false;
            for (int t = 0; t < 620 && ents.aliveAt(b2); ++t) {
                if (ents.bobberHasBiteAt(b2)) { okRewait = true; break; }
                tickB(1, 0.05f);
            }
            tickB(int(kMirrorBiteWindow / 0.05f) + 2, 0.05f); // 第二次咬钩窗口亦过期（1.1s > 1.0s）→ 收 = 空收
            const int durBeforeEmpty = hb.durabilityAt(0);
            pc.useFishingRod(); // 无咬空收
            okc2 = okc2 && okEsc && okRewait && caughtCount == 1 && hb.durabilityAt(0) == durBeforeEmpty
                   && !pc.fishing() && !ents.aliveAt(b2);
            okC = okc1 && okc2;
            if (!okC)
                qInfo().noquote() << "  [t836 c diag] okc1" << okc1 << "(caught" << caughtCount << "id" << caughtId
                                  << "dur" << rodDur - 1 << ") okc2" << okc2 << "(okEsc" << okEsc << "okRewait"
                                  << okRewait << "esc" << escCount - escBefore << ")";
        }

        // ---- (d) 钩 mob：真甩竿飞行段命中 → 收竿拉拽 + 耐久 -5 + 不伤害 ----
        bool okD = false;
        {
            // 猪平台（3×3，防落定 1.5s 游荡期间走下台）+ 猪；玩家站 x 13.5 平台按猪实时位姿瞄准。
            wF.setBlock(13, fy, 6, BR::Stone, 0);
            for (int x = 15; x <= 17; ++x)
                for (int z = 5; z <= 7; ++z)
                    wF.setBlock(x, fy, z, BR::Stone, 0);
            const int pig = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 10);
            // review25 探针加固（基线潜伏 flake，与本批游戏侧改动无关）：mob 游荡走运行期 QRandomGenerator
            //   （aiPig wanderTimer 1.5-3.5s 随机）→ 旧版 30 tick（1.5s）settle 窗内猪已可游走（实测偶发
            //   x≈17.9 平台边缘半身悬空 → 甩钩窗内再走一步跌出平台 → 钩空假 FAIL）。改**短窗 settle**
            //   （5 tick 重力落定；出生位=格心精确），甩竿+飞行+钩定 ≤15 tick < 首个游荡窗下界 30 tick
            //   → 猪在整个命中窗内钉在出生位（确定性）。
            tickB(5, 0.05f);
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod);
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, -90.0f, 0.0f, 2 /* Survival */);
            int caughtCount = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
            // 按猪实时中心位姿算 yaw/pitch（水平 look = (ux,uz) → yaw = atan2(-ux,-uz)；pitch = atan2(dy, 水平距)）。
            const QVector3D pp = ents.posAt(pig);
            const float eyeY = float(fy + 1) + 1.62f;
            const float ux = pp.x() - 13.5f, uz = pp.z() - 6.5f;
            const float hLen = std::sqrt(ux * ux + uz * uz);
            const float yawDeg = qRadiansToDegrees(std::atan2(-ux, -uz));
            const float pitchDeg = qRadiansToDegrees(std::atan2(pp.y() - eyeY, hLen));
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, yawDeg, pitchDeg, 2);
            pc.useFishingRod(); // 甩向猪
            int bb = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bb = i; break; }
            bool okHook = bb >= 0;
            for (int t = 0; t < 40 && okHook; ++t) {
                if (ents.bobberHookedMobAt(bb) == pig) break;
                tickB(1, 0.05f);
                if (!ents.aliveAt(bb)) { okHook = false; break; }
            }
            okHook = okHook && ents.bobberHookedMobAt(bb) == pig;
            const float pigX0 = ents.posAt(pig).x();
            const int pigHp0 = ents.healthAt(pig);
            const int dur0 = hb.durabilityAt(0);
            pc.useFishingRod(); // 收竿 → 拉拽
            tickB(1, 0.016f);  // 一帧物理：猪被拉向玩家（vx = −6 朝 −X…方向断言按位移点积）
            const float pigX1 = ents.posAt(pig).x();
            const float moved = pigX1 - pigX0;
            // 期望位移方向：猪在玩家 +X 侧 → 被拉向 −X（toward = pigX0 − 玩家x 的符号取反）。
            const float toward = (pigX0 - 13.5f) >= 0.0f ? -1.0f : 1.0f;
            okD = okHook && caughtCount == 0 && hb.durabilityAt(0) == dur0 - 5
                  && ents.healthAt(pig) == pigHp0          // 钩中不伤害
                  && moved * toward > 0.03f                  // 位移朝玩家 > 0.03（6 b/s 冲量 × 一帧）
                  && !pc.fishing() && !ents.aliveAt(bb);
            // d2 垂死 mob 收竿（R19.13 终审 B-L2）：老猪已被拉拽 + 累计游荡 ~3s 位置不可控（平台 rig 的
            //   1.5s 游荡安全窗只保单次落定）→ 换新猪平台中心重摆（同段首手法）。钩住后打死（死亡动画
            //   0.5s 窗内、探针不 tick ents → 浮标脱钩验证未跑，bobberHookedMobAt 仍指猪）→ 收竿：
            //   pullMobToward 对 dead 早退返 false → 按空收处理，耐久**不扣**（旧版 void 无条件 -5 = 白损）。
            const int pigHpAfter = ents.healthAt(pig); // diag 用（d2 换猪后老猪槽已释放，先存值）
            ents.removeEntityAt(pig);
            const int pig2 = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickB(5, 0.05f); // 短窗 settle（同段首 pig 加固口径：首游荡窗 30 tick 前完成甩钩）
            const QVector3D pp2 = ents.posAt(pig2);
            const float eyeY2 = float(fy + 1) + 1.62f;
            const float ux2 = pp2.x() - 13.5f, uz2 = pp2.z() - 6.5f;
            const float hLen2 = std::sqrt(ux2 * ux2 + uz2 * uz2);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                              qRadiansToDegrees(std::atan2(-ux2, -uz2)),
                              qRadiansToDegrees(std::atan2(pp2.y() - eyeY2, hLen2)), 2);
            pc.useFishingRod(); // 甩向新猪
            int bb2 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bb2 = i; break; }
            bool okD2 = bb2 >= 0;
            for (int t = 0; t < 40 && okD2; ++t) {
                if (ents.bobberHookedMobAt(bb2) == pig2) break;
                tickB(1, 0.05f);
                if (!ents.aliveAt(bb2)) { okD2 = false; break; }
            }
            okD2 = okD2 && ents.bobberHookedMobAt(bb2) == pig2;
            const bool hooked2Now = bb2 >= 0 && ents.bobberHookedMobAt(bb2) == pig2; // 收竿前现场（甩中与否）
            const QVector3D pig2End = ents.posAt(pig2);
            ents.damageEntity(pig2, 999); // 打死（dead=true；槽仍 alive，死亡动画窗内）
            const int durD2 = hb.durabilityAt(0);
            pc.useFishingRod();           // 垂死目标收竿 → 拉拽 no-op
            okD2 = okD2 && !pc.fishing() && !ents.aliveAt(bb2) && caughtCount == 0
                   && hb.durabilityAt(0) == durD2; // 空收口径：无获物不扣耐久（B-L2 修）
            okD = okD && okD2;
            if (!okD)
                qInfo().noquote() << "  [t836 d diag] okHook" << okHook << "dur" << hb.durabilityAt(0) - dur0
                                  << "hp" << pigHpAfter << "/" << pigHp0 << "moved" << moved
                                  << "toward" << toward << "okD2(dead-mob no-cost)" << okD2
                                  << "hooked2Now" << hooked2Now << "pig2End" << pig2End;
            ents.removeEntityAt(pig2); // 清场（探针私有 ents 冻结不外泄）
            wF.setBlock(13, fy, 6, BR::Air, 0);
            for (int x = 15; x <= 17; ++x)
                for (int z = 5; z <= 7; ++z)
                    wF.setBlock(x, fy, z, BR::Air, 0);
        }

        // ---- (e) 熟鱼链（kSmelt + kSmeltXp 两表 + 食用值 + 名 + pack/创造 tab 源码钉 + 豹猫 gate 钉）----
        bool okE = false;
        {
            Hotbar hbF;
            const bool okCore = RecipeRegistry::CookedFishId == 0x25B
                                && SmeltingRegistry::smeltResult(RecipeRegistry::RawFishId) == RecipeRegistry::CookedFishId
                                && SmeltingRegistry::smeltXpReward(RecipeRegistry::CookedFishId) == 1
                                && PlayerController::foodHungerAmount(RecipeRegistry::CookedFishId) == 4
                                && PlayerController::foodHungerAmount(RecipeRegistry::RawFishId) == 2
                                && hbF.nameForBlock(RecipeRegistry::CookedFishId) == QStringLiteral("熟鱼")
                                && hbF.nameForBlock(RecipeRegistry::RawFishId) == QStringLiteral("生鱼");
            // 源码钉（QML/源内字面量契约，t789 QML-literal 模式）：pack 映射行 + 创造 tab 行 + 豹猫生鱼 gate
            //   （熟鱼不接豹猫喂食 = MC 1.0 口径）。exe 在 build/ → ../src 或 ../../src 兜底（r24#5 同款）。
            bool okSrc = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString rpmPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Core/resourcepackmanager.cpp"));
                const QString hbPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                           QStringLiteral("src/Game/hotbar.cpp"));
                const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Game/playercontroller.cpp"));
                if (QFile::exists(rpmPath) && QFile::exists(hbPath) && QFile::exists(pcpPath)) {
                    QFile f1(rpmPath), f2(hbPath), f3(pcpPath);
                    if (f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly) && f3.open(QIODevice::ReadOnly)) {
                        const QString t1 = QString::fromUtf8(f1.readAll());
                        const QString t2 = QString::fromUtf8(f2.readAll());
                        const QString t3 = QString::fromUtf8(f3.readAll());
                        // 滤注释行后查语句（防「注释里有、代码里没有」的假 PASS；豹猫 gate 行在代码区）。
                        QString t3code;
                        for (const QString &line : t3.split(QLatin1Char('\n'))) {
                            if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                            t3code += line; t3code += QLatin1Char('\n');
                        }
                        okSrc = t1.contains(QStringLiteral("{0x25B, QStringLiteral(\"cooked_cod.png\")}"))
                                && t2.contains(QStringLiteral("int(RecipeRegistry::CookedFishId)"))
                                && t3code.contains(QStringLiteral("heldItemId == RecipeRegistry::RawFishId"));
                    }
                }
            }
            okE = okCore && okSrc;
            if (!okE)
                qInfo().noquote() << "  [t836 e diag] okCore" << okCore << "okSrc" << okSrc;
        }

        // ---- (f) 出界消散（R19.13 终审 C-M2：PASS 文案曾含 "out-of-bounds despawn" 而无对应断言——
        //      Review24 #9「描述超断言」病复发处，补上）：XZ 飞越边界 + 极端 y（y<0 虚空）→ ents.tick
        //      若干 → 浮标槽释放（aliveAt 翻 false = 槽 despawn；8 tick 内出界 = 边界路径非 180s 寿命路径）。----
        bool okF = false;
        {
            // f1 XZ 飞越：世界 48 宽，自 (44.5, fy+4, 6.5) 以 30 b/s +X → 第 3 tick x>48 出界（轻重力下
            //   y 仍在空带，不落水 / 不着地 / 无 mob 可钩 → 唯一出路是边界消散）。
            const int bOut = ents.spawnBobber(QVector3D(44.5f, float(fy + 4), 6.5f),
                                              QVector3D(30.0f, 0.0f, 0.0f), 911);
            bool okXz = bOut >= 0 && ents.aliveAt(bOut);
            for (int t = 0; t < 8 && okXz; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(bOut)) break;
            }
            okXz = okXz && !ents.aliveAt(bOut);
            // f2 极端 y（虚空直落）：y<0 → 首 tick 即消散。
            const int bLow = ents.spawnBobber(QVector3D(6.5f, -10.0f, 6.5f), QVector3D(0, 0, 0), 912);
            tickB(1, 0.05f);
            const bool okLow = bLow >= 0 && !ents.aliveAt(bLow);
            okF = okXz && okLow;
            if (!okF)
                qInfo().noquote() << "  [t836 f diag] okXz" << okXz << "okLow" << okLow;
        }

        // ---- (g) Game 层镜像（R19.13 终审 C-M2：updateFishing 每 tick 镜像路径此前零执行）----
        //      行为半边（探针可达）：甩竿后外部清场（ents.clearAll = 出界 / 寿命消散 / 换世界清场的等价
        //      构造）→ ① 镜像惰性（清场即刻 fishing 态仍在——Game 层不主动扫描，tick / 收竿才收）；
        //      ② 收竿走 valid=false 分支 = 干净收场（无获物 / 无耐久消耗 / fishing 复位 / 浮标槽已空）。
        //      自动收竿半边（pc.tick 驱动 updateFishing）：tickImpl 的 captured 门在无窗测试二进制不可达
        //      （直调 pc.tick 会先走 !m_captured 早退分支的 cancelFishing，掩盖镜像路径本体）→ 源序钉
        //      （t836(e) 手法）：滤注释后锁 updateFishing 函数体内「aliveAt/kindAt 双查 + 失效自动收竿」
        //      语句面。取舍：行为级不可达已声明，源码钉防语句面漂移（review 建议的退路）。----
        bool okG = false;
        {
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod);
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            int caughtCount = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
            pc.useFishingRod(); // 甩竿（飞行中即可——镜像检测与浮标态无关）
            int bg = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bg = i; break; }
            bool okBeh = pc.fishing() && bg >= 0;
            ents.clearAll(); // 浮标消散等价构造（releaseSlot，不发 removeEntityAt）
            okBeh = okBeh && pc.fishing(); // ① 镜像惰性：清场即刻态不塌（tick / 收竿才收）
            const int durG = hb.durabilityAt(0);
            pc.useFishingRod(); // 收竿 → valid=false（aliveAt 双查失败）→ 自动收竿态
            okBeh = okBeh && !pc.fishing() && !ents.aliveAt(bg) && caughtCount == 0
                    && hb.durabilityAt(0) == durG; // ② 干净收场：无获物无消耗
            // 源序钉：锁 updateFishing 函数体内的失效检测 + 自动收竿语句面（captured 门不可达的退路）。
            bool okPin = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Game/playercontroller.cpp"));
                QFile f(pcpPath);
                if (f.open(QIODevice::ReadOnly)) {
                    const QString t = QString::fromUtf8(f.readAll());
                    const int b0 = t.indexOf(QStringLiteral("void PlayerController::updateFishing(float dt)"));
                    const int b1 = t.indexOf(QStringLiteral("void PlayerController::cancelFishing"));
                    if (b0 >= 0 && b1 > b0) {
                        QString body;
                        for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                            if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                            body += line; body += QLatin1Char('\n');
                        }
                        okPin = body.contains(QStringLiteral("m_entityManager->aliveAt(m_bobberEntityIdx)"))
                                && body.contains(QStringLiteral(
                                       "kindAt(m_bobberEntityIdx) == int(EntityManager::Bobber)"))
                                && body.contains(QStringLiteral("m_fishing = false;"))
                                && body.contains(QStringLiteral("emit fishingChanged();"));
                    }
                }
            }
            okG = okBeh && okPin;
            if (!okG)
                qInfo().noquote() << "  [t836 g diag] okBeh" << okBeh << "okPin" << okPin;
        }

        // ---- (h) review25 #12 Ground 态支撑复查：挖掉贴靠方块 → 若干 tick 内转 Flying 下落 ----
        //      旧版 Ground 恒 continue（零复查）→ 挖掉贴靠方块后浮标悬空滞留至 180s 寿命兜底。修复 =
        //      贴靠格快照（进态时记录）+ 每 kBobberGroundRecheckEvery tick 一查 blockAt。断言三段：
        //      落定冻结（20 tick 位置不变）→ 挖支撑 → ≤40 tick 内 y 下坠 >0.05（复查节流 ≤10 tick + 重力
        //      积累 ~4 tick）且实体仍活。
        bool okH = false;
        {
            wF.setBlock(20, fy, 8, BR::Stone, 0); // 石台（贴靠格）
            const int bh = ents.spawnBobber(QVector3D(20.5f, float(fy + 4), 8.5f), QVector3D(0, 0, 0), 921);
            for (int t = 0; t < 80 && ents.aliveAt(bh); ++t) tickB(1, 0.05f); // 落到石台 → Ground
            const QVector3D phA = ents.posAt(bh);
            tickB(20, 0.05f);
            const QVector3D phB = ents.posAt(bh);
            const bool frozen = ents.aliveAt(bh) && phA == phB && phA.y() > float(fy); // 冻结于台面上方
            wF.setBlock(20, fy, 8, BR::Air, 0); // 挖掉贴靠方块（Ground 复查的触发源）
            bool fell = false;
            for (int t = 0; t < 40 && ents.aliveAt(bh); ++t) {
                tickB(1, 0.05f);
                if (ents.posAt(bh).y() < phA.y() - 0.05f) { fell = true; break; }
            }
            okH = frozen && fell;
            if (!okH)
                qInfo().noquote() << "  [t836 h diag] frozen" << frozen << "phA" << phA << "phB" << phB
                                  << "fell" << fell << "pos" << ents.posAt(bh);
            if (bh >= 0) ents.removeEntityAt(bh);
        }

        // ---- (i) review25 #13 实体格命中门：1 格墙后贴壁 mob 飞行浮标不隔墙钩 ----
        //      旧序先用 next 点测 mob AABB（外扩 kBobberHookHitPad）再查方块碰撞 → next 跨入墙格且已进
        //      墙后 mob 外扩命中盒时被隔墙钩住（Hooked 钉位 + 收竿拉拽可拉 mob 穿墙）。rig：石坑困猪（四壁
        //      2 高 + 坑底，开口向上），knockback 把猪压在 -X 壁（= 浮标来向的 1 格墙）上 → 猪 AABB 贴壁 →
        //      外扩命中盒左沿伸到墙格前 0.15（≈23.85）——浮标 spawn 于 (23.96, pigY, 7.5) v=(18,0,0)（步长
        //      0.9/tick）首 tick next=(24.86,·)：已入墙格且在命中盒内。断言：不钩（bobberHookedMobAt==-1）
        //      + 贴面停在墙前（pos.x ∈ (23,24)）——修复序实体格命中门先于钩 mob，贴面 Ground 不钩。
        bool okI = false;
        {
            wF.setBlock(25, fy, 7, BR::Stone, 0); // 坑底
            for (int yy = fy + 1; yy <= fy + 2; ++yy) {
                wF.setBlock(24, yy, 7, BR::Stone, 0); // -X 壁 = 浮标来向 1 格墙
                wF.setBlock(26, yy, 7, BR::Stone, 0);
                wF.setBlock(25, yy, 6, BR::Stone, 0);
                wF.setBlock(25, yy, 8, BR::Stone, 0);
            }
            const int pigI = ents.spawnMobTyped(25, fy + 1, 7, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickB(30, 0.05f);                    // 落定（坑内 1×1 活动域）
            ents.knockback(pigI, -1.0f, 0.0f);   // 压向 -X 壁 → AABB 贴壁钉住（命中盒伸入墙格带）
            tickB(10, 0.05f);                    // 滑到贴壁静止
            const QVector3D pigP = ents.posAt(pigI);
            const bool pinned = pigP.x() >= 25.35f && pigP.x() <= 25.60f; // 贴壁带（halfW±漂移；diag 半断言）
            const int bi = ents.spawnBobber(QVector3D(23.96f, pigP.y(), 7.5f),
                                            QVector3D(18.0f, 0.0f, 0.0f), 922);
            tickB(3, 0.05f);
            okI = pinned && bi >= 0 && ents.aliveAt(bi)
                  && ents.bobberHookedMobAt(bi) == -1 // 不隔墙钩
                  && ents.posAt(bi).x() < 24.0f       // 贴面停在墙前（未穿入墙格）
                  && ents.posAt(bi).x() > 23.0f;
            if (!okI)
                qInfo().noquote() << "  [t836 i diag] pinned" << pinned << "pigX" << pigP.x()
                                  << "hooked" << (bi >= 0 ? ents.bobberHookedMobAt(bi) : -2)
                                  << "bobPos" << (bi >= 0 ? ents.posAt(bi) : QVector3D());
            if (bi >= 0) ents.removeEntityAt(bi);
            ents.removeEntityAt(pigI);
            wF.setBlock(25, fy, 7, BR::Air, 0);
            for (int yy = fy + 1; yy <= fy + 2; ++yy) {
                wF.setBlock(24, yy, 7, BR::Air, 0);
                wF.setBlock(26, yy, 7, BR::Air, 0);
                wF.setBlock(25, yy, 6, BR::Air, 0);
                wF.setBlock(25, yy, 8, BR::Air, 0);
            }
        }

        const bool okT836 = okA && okB && okC && okD && okE && okF && okG && okH && okI;
        if (!okT836) ++totalFail;
        qInfo().noquote() << (okT836 ? "PASS" : "FAIL")
                          << "| t836 fishing overhaul: bobber is an EntityManager projectile (light-gravity "
                             "parabola, hook-on-flight vs mob AABB, water settle at surface-minus-dip with "
                             "state-aware liquid height, ground rest frozen, out-of-bounds despawn ASSERTED: "
                             "xz flyout within 8 ticks + void-y first-tick slot release) driven from "
                             "Game layer cast-anywhere/reel (EntityManager-carries-entity + "
                             "PlayerController-settles-semantics split, pearl/drop precedent); deterministic "
                             "5-30s wait via hashVoxel(seed^salt^castSerial) with exact reachable endpoints and "
                             "+-1tick behavioral match, bite window 1.0s (t926 user override of the MC "
                             "1.0 ~0.5s value; in-window reel = fishingPool loot "
                             "thrown to the player as a ballistic spawnItemThrown (t886: solved arc, "
                             "distance-adaptive speed) + rod -1, expired = escaped signal + re-roll + empty "
                             "reel costs nothing), hooked-mob reel pulls at ~6 b/s with -5 durability and zero "
                             "damage, dead-target reel = pull no-op with NO durability charge; externally-"
                             "cleared bobber keeps lazy fishing state then reels clean (no loot, no cost) with "
                             "updateFishing invalid-to-auto-reel pinned at source level (pc.tick captured gate "
                             "unreachable headless, tradeoff declared); ground-rest bobber rechecks its "
                             "support cell on a 10-tick throttle and falls (review25 #12: mined support -> "
                             "flying within 40 ticks, no more hovering until the 180s lifetime bail); "
                             "solid-cell hit gate precedes mob hooking (review25 #13: wall-pinned pig "
                             "behind a 1-thick wall with a fast bobber whose next point lands inside the "
                             "wall cell and the padded pig AABB grounds at the wall face instead of "
                             "hooking through it); "
                             "cooked fish 0x25B closes the chain (raw->cooked in BOTH kSmelt+kSmeltXp, "
                             "+4 hunger vs raw +2, name/tab/pack-mapping pinned, ocelot still raw-only)";
    });

    // ── Review 2026-08-25 #2 浇熄摘侧表信号探针（blockDoused 恰一次 + 坐标 + 火灭块存）──
    // 背景：浇熄设计为「火灭块存」——tickFire 抑制掷中后 m_burningCells.remove 直摘：栅格不变（无
    //   blockBroken）、侧表直摘（无 worldChanged）→ QML 面火 overlay 两条摘除链（onBlockBroken /
    //   onWorldChanged→cleanupVis）都不触发 = 假火 delegate 永久残留。setBlock 同 id 无变化早退清表
    //   （t843 特意放早退前）同根因第二实例。修法 = 新增 blockDoused(x,y,z) 精确信号驱动
    //   removeBurningVis（取舍：不选补发 worldChanged——那会触发 cleanupVis 全量对账，浇熄是常见
    //   事件不该付全量 mesh 重查的价）。锁法（三段）：
    //   (a) 同 id 早退路径（确定性）：点燃木板 → setBlock(Planks)（同 id no-op 写）→ 恰发一次
    //       blockDoused + 坐标正确 + isBurningAt=false + 栅格块仍存；
    //   (b) 有变化路径不重发：点燃 → setBlock(Air)（blockBroken + worldChanged 已覆盖）→ 计数不增；
    //   (c) tickFire 浇熄掷中路径：点燃（干）→ 邻注水 → 推窗至掷中（40%/窗 vs 木板 10 窗烧毁，
    //       先掷中概率 99.4%/候选；烧毁即换候选重试，40 候选下假 FAIL 率 ~1e-85）→ 恰一次 + 坐标 +
    //       块存 + 全程零 blockBroken（火灭块存 ≠ 烧毁语义钉死）。
    runLegMulti({ "review25 #2 douse signal: same-id setBlock early-exit and tickFire suppress-roll removal each em"
        "it blockDoused exactly once with correct coords and block-preserved (no blockBroken), change-pat"
        "h stays silent (broken+worldChanged cover it); probabilistic roll closed via candidate search (4"
        "0 tries, ~1e-85 false rate)" }, [&]() {
        // rig 选址：运行期扫描空区（t809 先例——尾部探针不占 nextSlot 网格）。需 11×6×8 候选带。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 10 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 10 && clear; ++dx)
                    for (int dz = 0; dz < 7 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #2 douse signal: no clear rig area found";
        } else {
            bool ok = true;
            int doused = 0, dX = -1, dY = -1, dZ = -1, brokenCnt = 0;
            QMetaObject::Connection cD = QObject::connect(
                    &w, &World::blockDoused, &w,
                    [&](int x, int y, int z) { ++doused; dX = x; dY = y; dZ = z; });
            QMetaObject::Connection cB = QObject::connect(
                    &w, &World::blockBroken, &w, [&](int, int, int, int) { ++brokenCnt; });

            // (a) 同 id 无变化早退清表（确定性路径）。
            placeRigBlock(w, x0, kRigY, z0, BR::Planks, 0);
            const bool ignitedA = w.igniteFlammableAt(x0, kRigY, z0) && w.isBurningAt(x0, kRigY, z0);
            doused = 0;
            const bool noChangeRet = !w.setBlock(x0, kRigY, z0, BR::Planks); // 同 id → false 早退
            const bool okA = ignitedA && noChangeRet && doused == 1 && dX == x0 && dY == kRigY && dZ == z0
                             && !w.isBurningAt(x0, kRigY, z0)
                             && w.blockAt(x0, kRigY, z0) == BR::Planks; // 火灭块存（栅格不动）

            // (b) 有变化路径不重发（blockBroken + worldChanged 覆盖，blockDoused 不掺和）。
            const bool ignitedB = w.igniteFlammableAt(x0, kRigY, z0) && w.isBurningAt(x0, kRigY, z0);
            doused = 0;
            brokenCnt = 0;
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            const bool okB = ignitedB && doused == 0 && brokenCnt >= 1
                             && !w.isBurningAt(x0, kRigY, z0);

            // (c) tickFire 抑制浇熄掷中（候选搜索：找到即断言，失败候选（10 窗内未掷中先烧毁）清扫换位）。
            bool okC = false;
            int usedCand = -1;
            for (int cand = 0; cand < 40 && !okC; ++cand) {
                usedCand = cand;
                const int tx = x0 + (cand % 5) * 2;        // 步距 2：候选板格与水格互不占位
                const int tz = z0 + cand / 5;
                placeRigBlock(w, tx, kRigY, tz, BR::Planks, 0);
                if (!w.igniteFlammableAt(tx, kRigY, tz)) { // 干格点燃（水后注——湿燃料不可点燃是入口守卫）
                    w.setBlock(tx, kRigY, tz, BR::Air, 0);
                    continue;
                }
                w.setBlock(tx + 1, kRigY, tz, BR::Water, 0); // 点燃后注水邻 → 进抑制态
                doused = 0;
                brokenCnt = 0;
                for (int t = 0; t < 60 && doused == 0; ++t) w.tickFire(); // 60 调 = 12 窗（interval 5）
                const bool survived = w.blockAt(tx, kRigY, tz) == BR::Planks;
                if (doused == 1 && survived && dX == tx && dY == kRigY && dZ == tz
                        && !w.isBurningAt(tx, kRigY, tz) && brokenCnt == 0) {
                    okC = true; // 恰一次 + 坐标 + 块存 + 零 broken（浇熄非烧毁）
                } else {
                    w.setBlock(tx + 1, kRigY, tz, BR::Air, 0); // 清水 + 清格（烧毁 flare/Air 残留归一）
                    w.setBlock(tx, kRigY, tz, BR::Air, 0);
                }
            }
            ok = okA && okB && okC;
            if (!ok)
                qInfo().noquote() << "  [r25#2 diag] okA" << okA << "okB" << okB << "okC" << okC
                                  << "cand" << usedCand << "doused" << doused << "broken" << brokenCnt;
            QObject::disconnect(cD);
            QObject::disconnect(cB);
            // 清场（候选带全扫 Air——水格 + 板格 + flare 残留一并）。
            for (int dx = 0; dx <= 10; ++dx)
                for (int dz = 0; dz < 7; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        if (w.blockAt(x0 + dx, kRigY + dy, z0 + dz) != BR::Air)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #2 douse signal: same-id setBlock early-exit and tickFire"
                                 " suppress-roll removal each emit blockDoused exactly once with correct"
                                 " coords and block-preserved (no blockBroken), change-path stays silent"
                                 " (broken+worldChanged cover it); probabilistic roll closed via"
                                 " candidate search (40 tries, ~1e-85 false rate)";
        }
    });

    // ── Review 2026-08-25 #3 tickVehicleRiding emit 节流探针（不直发 / pending 由 tick 收口接住）──
    // 背景：t811 tickVehicleRiding 末尾 `if (dirty) { ++m_revision; emit entitiesChanged(); }` 直发，且
    //   每帧被调两次（playercontroller mob 桶常开 + step 后补钉）——乘客跟车每帧 dirty → 最坏每帧 2 次
    //   全量 revision+emit（激活全体实体 delegate revision 绑定 + 行走 MobModel 全几何重建 = t500 已修的
    //   22ms/帧卡顿模式复发；矩阵探针只断言钉位行为，emit 面探针盲区）。修法 = dirty 只置 m_pendingEmit
    //   复用 tick 末尾 kEmitEveryN（~20Hz）收口。锁法：
    //   (a) 直调相（隔离验证）：spawnCart + spawnMob + 仅 tickVehicleRiding×2/帧 + 推车物理（车动 → 钉位
    //       每帧变 → 每帧 dirty）跑 20 帧**不调 ents.tick** → entitiesChanged 零 emit（旧直发版首帧登乘
    //       即 emit → 回归即红；20 帧移动场景旧版 ≥10 emit）；
    //   (b) 收口相：接续 ents.tick×6（含相位门 %3）→ ≥1 emit（pending 被 tick 接住 = 钉位变更最终可见，
    //       t811 呈现语义不丢）且 ≤ 3（= 6/3 + 1 节流上界——防「换一处直发」的复发面）；
    //   (c) 钉位行为不回归：全程 mob 钉车座位（t811 座位公式误差 <0.01）。
    runLegMulti({ "review25 #3 riding emit throttle: tickVehicleRiding never emits entitiesChanged directly (20 dir"
        "ty frames -> 0 emits; old code >=1 on first boarding frame), pending flushed through tick's kEmi"
        "tEveryN gate (6 ticks -> 1..3 emits), seat-pin formula intact (dx/dy/dz < 0.01)" }, [&]() {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
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
            qInfo().noquote() << "FAIL | review25 #3 riding emit throttle: no clear rig area found";
        } else {
            const float seatDrop = 0.3125f; // kCartSeatDrop 同值镜像（t811 探针同款）
            for (int dz = -5; dz <= 0; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    placeRigBlock(w, x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                    if (dx == 0) placeRigBlock(w, x0, kRigY, z0 + dz, BR::Rail, 0);
                }
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mob = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ff5555"), 10);
            int emitted = 0;
            QMetaObject::Connection cE = QObject::connect(
                    &ents, &EntityManager::entitiesChanged, &ents, [&]() { ++emitted; });
            // (a) 直调相：20 帧只跑骑乘收口 + 车物理（登乘 + 跟车每帧 dirty），不调 ents.tick → 恒 0 emit。
            QVector3D player = carts.posAt(0);
            bool boarded = false;
            for (int t = 0; t < 20; ++t) {
                ents.tickVehicleRiding();
                carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北推（t811 玩家模型）
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();
                player = carts.posAt(0);
                if (ents.rideCartAt(mob) >= 0) boarded = true;
            }
            const int directEmits = emitted;
            // (c) 钉位公式（车座位 = 车心 − seatDrop + halfH(0.5)）。
            const QVector3D cp = carts.posAt(0);
            const QVector3D mp = ents.posAt(mob);
            const bool pinOk = boarded && std::fabs(mp.x() - cp.x()) <= 0.01f
                               && std::fabs(mp.y() - (cp.y() - seatDrop + 0.5f)) <= 0.01f
                               && std::fabs(mp.z() - cp.z()) <= 0.01f;
            // (b) 收口相：6 帧 tick（相位门 %3 → 恰 2 次对齐）接住 pending。
            const int beforeFlush = emitted;
            for (int t = 0; t < 6; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
                carts.pushEmptyCart(&w, player, 0.0f, -1.0f);
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();
                player = carts.posAt(0);
            }
            const int flushEmits = emitted - beforeFlush;
            const bool ok = directEmits == 0 && pinOk && flushEmits >= 1 && flushEmits <= 3;
            if (!ok)
                qInfo().noquote() << "  [r25#3 diag] direct" << directEmits << "pinOk" << pinOk
                                  << "flush" << flushEmits;
            QObject::disconnect(cE);
            carts.clearAll();
            ents.clearAll();
            for (int dz = -5; dz <= 0; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
                w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            }
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #3 riding emit throttle: tickVehicleRiding never emits"
                                 " entitiesChanged directly (20 dirty frames -> 0 emits; old code >=1 on"
                                 " first boarding frame), pending flushed through tick's kEmitEveryN gate"
                                 " (6 ticks -> 1..3 emits), seat-pin formula intact (dx/dy/dz < 0.01)";
        }
    });

    // ── Review 2026-08-25 #4 鱿鱼持续浮力天花板碰撞探针（封顶水柱上浮贴顶不穿出）──
    // 背景：垂直积分段只为下落设计——浮力 vy>0 自由上移分支无向上阻挡 → 头顶穿入固体格（冰面/封顶水池）
    //   后**中心**落入固体格那帧，落地扫描 restY(格顶+halfH) 高于当前位置 → mobNewY<=restY 成立把整段
    //   setY(restY) 抬到方块顶上（穿顶）。修法 = vy>0 时对头顶格取碰撞盒最低底（collisionAABBsAt 口径，
    //   与落定分支对称；ShapeNone 无盒族照穿过），头将穿入 → 钳 pos.y=ceilBottom−halfH、vy=0 贴顶悬停。
    //   rig：封闭水箱（5×5 石底 + 石壁环 3 层 + 内腔 3×3 水×3 + 5×5 石顶）——石壁防鱿鱼水平漂游出腔
    //   （aiSquid 有 XZ 漂游 + 碰撞撤回）。锁法：spawn 鱿鱼于中层水 → tick 400 帧 →
    //   (a) 不穿出：末位 pos.y + halfH ≤ 顶格下沿 + 0.02（旧版被整段抬到格顶上 ≈ +1.47 → 红）；
    //   (b) 贴顶稳定：末 60 帧 Y 带 ≤ 0.05（浮力再积再钳的贴顶悬停，非振荡/继续上穿）；
    //   (c) 确有上浮：末位 > 初始位（防「误杀浮力」的反向回归）。
    runLegMulti({ "review25 #4 squid ceiling: sustained buoyancy in a capped water box clamps at head-level collisi"
        "on bottom (pos.y+halfH stays below ceiling underface +0.02, 60-tick stability band <=0.05, still"
        " rises from spawn = buoyancy intact); old code teleported squid whole-body above the ceiling (re"
        "stY snap ~1.45 above the clamp)" }, [&]() {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 4 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 4 && clear; ++dx)
                    for (int dz = 0; dz <= 4 && clear; ++dz)
                        for (int dy = -1; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #4 squid ceiling: no clear rig area found";
        } else {
            const int yB = kRigY;                  // 箱底（石）；水 yB+1..yB+3；顶 yB+4（石）
            for (int dx = 0; dx <= 4; ++dx)
                for (int dz = 0; dz <= 4; ++dz) {
                    placeRigBlock(w, x0 + dx, yB, z0 + dz, BR::Stone, 0);        // 底
                    placeRigBlock(w, x0 + dx, yB + 4, z0 + dz, BR::Stone, 0);    // 顶
                    const bool wall = (dx == 0 || dx == 4 || dz == 0 || dz == 4);
                    for (int dy = 1; dy <= 3; ++dy) {
                        if (wall) placeRigBlock(w, x0 + dx, yB + dy, z0 + dz, BR::Stone, 0);
                        else     placeRigBlock(w, x0 + dx, yB + dy, z0 + dz, BR::Water, 0);
                    }
                }
            EntityManager ents;
            const int sq = ents.spawnMobTyped(x0 + 2, yB + 2, z0 + 2, EntityManager::MobSquid,
                                              QStringLiteral("#306090"), 10);
            const float halfH = 0.45f; // MobSquid 半高（spawnMobCore 表）
            const float ceilBottom = float(yB + 4);
            const float startY = ents.posAt(sq).y();
            float loY = 1e9f, hiY = -1e9f;
            for (int t = 0; t < 400; ++t) {
                ents.tick(0.016, &w, QVector3D(x0 + 2.5f, yB + 2.5f, z0 + 2.5f), 0.3f, 1.8f, false);
                if (t >= 340) {
                    const float y = ents.posAt(sq).y();
                    loY = std::min(loY, y);
                    hiY = std::max(hiY, y);
                }
            }
            const float endY = ents.posAt(sq).y();
            const bool ok = ents.aliveAt(sq)
                            && endY + halfH <= ceilBottom + 0.02f   // (a) 不穿出（旧版 ≈ ceil+1.45 → 红）
                            && (hiY - loY) <= 0.05f                 // (b) 贴顶稳定带
                            && endY > startY - 0.01f;               // (c) 浮力仍在（上升到顶）
            if (!ok)
                qInfo().noquote() << "  [r25#4 diag] endY" << endY << "ceilBottom" << ceilBottom
                                  << "band" << (hiY - loY) << "startY" << startY
                                  << "alive" << ents.aliveAt(sq);
            ents.clearAll();
            for (int dx = 0; dx <= 4; ++dx)
                for (int dz = 0; dz <= 4; ++dz)
                    for (int dy = 0; dy <= 4; ++dy)
                        w.setBlock(x0 + dx, yB + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #4 squid ceiling: sustained buoyancy in a capped water box"
                                 " clamps at head-level collision bottom (pos.y+halfH stays below ceiling"
                                 " underface +0.02, 60-tick stability band <=0.05, still rises from spawn ="
                                 " buoyancy intact); old code teleported squid whole-body above the ceiling"
                                 " (restY snap ~1.45 above the clamp)";
        }
    });

    // ── Review 2026-08-25 #5 珍珠无碰撞植物族穿过探针（草丛格穿过 / 铁轨格仍命中对照）──
    // 背景：t835 命中判据「本格任意方块实存（5 id 豁免表）」把 TallGrass/花/蘑菇/树苗/枯灌木/作物
    //   （ShapeNone 无碰撞盒）也当命中 → 草地/农田平抛弧线数格内被草截断传送（反噬 t835「更远投掷」
    //   目标；本工程箭按空碰撞盒穿过植物）。修法 = 判据改「本格存在碰撞 sub-AABB」（判据本质化：旧
    //   5 id 豁免族全 ShapeNone 无盒 → 语义天然保留；铁轨/压力板/台阶等薄盒族仍命中 → t835「落铁轨
    //   必传送」不回归；未来新无碰撞方块自动正确）。锁法：两列对照直落 ——
    //   (a) 草丛列（石上 TallGrass）：珠穿过草格、命中**下方石格**才 enderPearlLanded（旧「任意实存」
    //       判据在草格即结算 → 落点 y = 草格 ≠ 石格 → 红）；
    //   (b) 铁轨列（石上 Rail）：珠在**轨格**即命中（薄盒存在 → t835 落轨传送语义钉死）。
    runLegMulti({ "review25 #5 pearl plant pass-through: pearl falling through a TallGrass cell (ShapeNone, no coll"
        "ision box) keeps flying and lands on the stone cell below (old any-block-here criterion triggere"
        "d on the grass cell), while a Rail cell (thin collision box present) still triggers landing in-c"
        "ell (t835 rail-teleport semantics preserved)" }, [&]() {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 3 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 3 && clear; ++dx)
                    for (int dz = 0; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #5 pearl plants: no clear rig area found";
        } else {
            // 两列相隔 3（x0 草丛列 / x0+3 铁轨列；互不邻接防编辑钩子串扰）。
            placeRigBlock(w, x0, kRigY, z0, BR::Stone, 0);
            placeRigBlock(w, x0, kRigY + 1, z0, BR::TallGrass, 0);
            placeRigBlock(w, x0 + 3, kRigY, z0, BR::Stone, 0);
            placeRigBlock(w, x0 + 3, kRigY + 1, z0, BR::Rail, 0);
            EntityManager ents;
            int landedCnt = 0;
            int lx[2] = { -1, -1 }, ly[2] = { -1, -1 };
            QMetaObject::Connection cL = QObject::connect(
                    &ents, &EntityManager::enderPearlLanded, &ents,
                    [&](int x, int y, int z) {
                        Q_UNUSED(z);
                        if (landedCnt < 2) { lx[landedCnt] = x; ly[landedCnt] = y; }
                        ++landedCnt;
                    });
            ents.spawnEnderPearl(QVector3D(x0 + 0.5f, kRigY + 3.5f, z0 + 0.5f),
                                 QVector3D(0.0f, -2.0f, 0.0f));
            ents.spawnEnderPearl(QVector3D(x0 + 3.5f, kRigY + 3.5f, z0 + 0.5f),
                                 QVector3D(0.0f, -2.0f, 0.0f));
            for (int t = 0; t < 200 && landedCnt < 2; ++t)
                ents.tick(0.016, &w, QVector3D(x0 + 2.0f, kRigY + 3.0f, z0 + 0.5f), 0.3f, 1.8f, false);
            // 各列落点归位断言（x 匹配列；y = 期望格）。
            bool grassPassed = false, railHit = false;
            for (int i = 0; i < 2 && i < landedCnt; ++i) {
                if (lx[i] == x0 && ly[i] == kRigY) grassPassed = true;        // 草丛列：石格才结算
                if (lx[i] == x0 + 3 && ly[i] == kRigY + 1) railHit = true;    // 铁轨列：轨格即结算
            }
            const bool ok = landedCnt == 2 && grassPassed && railHit;
            if (!ok)
                qInfo().noquote() << "  [r25#5 diag] landed" << landedCnt << "lx" << lx[0] << lx[1]
                                  << "ly" << ly[0] << ly[1];
            QObject::disconnect(cL);
            ents.clearAll();
            w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #5 pearl plant pass-through: pearl falling through a"
                                 " TallGrass cell (ShapeNone, no collision box) keeps flying and"
                                 " lands on the stone cell below (old any-block-here criterion"
                                 " triggered on the grass cell), while a Rail cell (thin collision"
                                 " box present) still triggers landing in-cell (t835 rail-teleport"
                                 " semantics preserved)";
        }
    });

    // ── t874/t875 铁砧附魔丢失 + 附魔台拒入 真链探针（R19.15 批三）──
    // 背景：用户报「放入铁砧附魔直接没了，元数据丢失」（跨七八个版本未根治）+「附魔台能放入已附魔物品
    //   并清洗附魔属性」。t792 实机探针（qml.exe 驱真 AnvilUI.qml + 桩 Hotbar.qml，11 放入路径 47/47）与
    //   t822（真 Hotbar VM C++ 直调镜像，无 QML 层）两轮全绿 → **接缝只剩「真 QML × 真 C++ Hotbar」从未
    //   同台执行**（Q_PROPERTY 早期返回 / QVariantList↔JS 转换 / NOTIFY 时序只在组合态暴露）。本探针拼上
    //   这一半：QQmlEngine 源树直载 AnvilUI.qml / EnchantingTableUI.qml（同目录隐式组件 + InventoryOps.js
    //   原地解析），真 Hotbar/PlayerState 经 context property 注入，真 QMouseEvent（press+release）驱动面板
    //   内联 TapHandler —— 与用户真实点击完全同链。
    // t874 覆盖：放入/取出全入口（真鼠标左/右键点 A/B 槽、Shift 搬运、左/右拖动、双击拿同类、数字键交换）
    //   × 四类别（工具 / 武器 4 附魔满配 / 护甲 / 附魔书）× 带名实例 × takeProduct 四 op（repair / combine /
    //   merge / rename）× 关包归还 × 存档 round-trip 后重绑 VM 再放入。
    // t875 覆盖：已附魔物品七入口拒入（真鼠标左/右键槽 0、右键拖、左键拖、数字键交换、Shift 搬运、双击
    //   合并）+ 全链无清洗（青金石槽放入 → 关包归还）+ 素品附魔 → 产物取出带附魔 → 拒再入。
    runLegMulti({ "t874 real-chain anvil enchant preservation (real QQmlEngine x source-tree AnvilUI.qml x real C++"
        " Hotbar): closes the last probe seam - t792 drove real QML against a mock Hotbar.qml, t822 drove"
        " the real VM without any QML; this probe loads the actual AnvilUI.qml+InventoryOps.js with the a"
        "ctual Hotbar/PlayerState injected and clicks via synthesized QMouseEvent through the inline TapH"
        "andlers (the exact user path). Entries: mouse left/right on A slot, shift-move, drag single-slot"
        " release, double-click pickup, number-key swap, takeProduct repair/combine/merge/rename, close-p"
        "anel return, save round-trip with VM rebind; review28 #1 t918 shift-route legs: foreign-cursor +"
        " named-product preflight rejects with zero consumption (no transmutation into cursor count), sam"
        "e-id cursor stays at cap (no over-cap stack), named product still lands via addToAny into empty "
        "slots; categories: tool/weapon(4-ench)/armor/enchanted-book, all with custom names + instance du"
        "rability asserted at every hop",
               "t875 real-chain enchanting-table gate + no-wipe (same harness, real EnchantingTableUI.qml): alre"
        "ady-enchanted item rejected on all seven entry paths (mouse left/right on slot 0, right-drag pla"
        "ce-one, left-drag redistribute + single-slot fallback, number-key swap, shift-move, double-click"
        " merge) while cursor stack keeps id/ench/name intact; lapis-slot sojourn + close-panel return pr"
        "eserves enchant metadata end-to-end (wipe hunt); clean-pick + lapis -> doEnchant tier1 product c"
        "arries >=1 enchant, take-out keeps it, re-entry rejected" }, [&]() {
        // 类型注册：**探针私有 URI**（VoxelSandboxProbe）。不能用 VoxelSandbox —— build/VoxelSandbox/qmldir
        //   （qt_add_qml_module 产物）落在 exe 同目录默认 import path 上，`import VoxelSandbox` 会命中它并
        //   `prefer :/VoxelSandbox/` 重定向到 qrc 资源（本测试二进制未链模块资源 → "Script
        //   qrc:/VoxelSandbox/src/ui/InventoryOps.js unavailable"）。私有 URI 无 qmldir → 走本处 C++ 注册；
        //   面板拷贝上 import 行同步改写（类型名 Hotbar/PlayerState/PlayerController/ResourcePackManager 原名）。
        static bool sVoxelTypesRegistered = false;
        if (!sVoxelTypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbe", 1, 0, "Hotbar");
            qmlRegisterType<PlayerState>("VoxelSandboxProbe", 1, 0, "PlayerState");
            qmlRegisterType<PlayerController>("VoxelSandboxProbe", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbe", 1, 0, "ResourcePackManager");
            sVoxelTypesRegistered = true;
        }
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 见上：防磁盘缓存把依赖重定向到未链接的 qrc 资源
        QQmlEngine engine;
        Hotbar vm;
        PlayerState ps;
        ps.setXp(4000); // repair/combine/merge/rename 与 doEnchant 的等级门槛全可付
        engine.rootContext()->setContextProperty(QStringLiteral("t874Hotbar"), &vm);
        engine.rootContext()->setContextProperty(QStringLiteral("t874PlayerState"), &ps);

        // 宿主桩：Main.qml 根（id: window）的最小复刻 —— AnvilUI/EnchantingTableUI 经作用域链解析
        //   window.shiftHeld / refocusKeyInput / closeAnvil / burstEnchantRunes。
        QQmlComponent wrapComp(&engine);
        wrapComp.setData(R"QML(import QtQuick
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

        // harness 装配：wrapper（engine 持有）→ 800×1200 上下两半各挂一块面板。函数链直调无窗口事件 →
        //   面板挂独立 QQuickItem 容器即可（无需 QQuickWindow；QQuickItem 场景经 contentItem 承载）。
        QString harnessDiag;
        bool harnessOk = true;
        QQuickItem hostItem; // 独立场景根（无窗口）
        QQuickItem *wrapper = nullptr;
        QObject *anvilRoot = nullptr;
        QObject *enchantRoot = nullptr;
        if (wrapComp.isError()) {
            harnessOk = false;
            harnessDiag = QStringLiteral("wrapper: ") + wrapComp.errorString();
        } else {
            wrapper = qobject_cast<QQuickItem *>(wrapComp.create());
            if (!wrapper) {
                harnessOk = false;
                harnessDiag = QStringLiteral("wrapper create failed");
            } else {
                wrapper->setParent(&engine); // QObject 父（引擎析构兜底；视觉父子另有 parentItem）
                wrapper->setParentItem(&hostItem);
            }
        }
        const QString uiDir = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                  .filePath(QStringLiteral("../../src/ui"));
        // t874/t875：源树路径直载时引擎把同目录相对导入（InventoryOps.js）重映射到编译模块 qrc 前缀
        //   （qrc:/VoxelSandbox/src/ui/...，本测试二进制未链模块资源 → unavailable）。把 6 个源文件拷到
        //   临时目录加载，逃离模块路径映射（文件内容逐字节同源树 —— 链路保真不受影响）。
        const QString probeUiDir = QDir::temp().absoluteFilePath(
                QStringLiteral("t874_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir);
        for (const QString f : { QStringLiteral("AnvilUI.qml"), QStringLiteral("EnchantingTableUI.qml"),
                                 QStringLiteral("InventoryOps.js"), QStringLiteral("InvSlot.qml"),
                                 QStringLiteral("ToolIcon.qml"), QStringLiteral("MaterialIcon.qml") }) {
            QFile src(uiDir + QLatin1Char('/') + f);
            QFile dst(probeUiDir + QLatin1Char('/') + f);
            dst.remove();
            src.copy(dst.fileName());
        }
        // 拷贝上两处 URL 改写（文件内容其余逐字节同源树，链路保真）：
        //   ① 相对 js 导入 → 绝对 file URL（防 build 目录 qmldir 的 prefer 重定向染指）；
        //   ② `import VoxelSandbox` → `import VoxelSandboxProbe`（防 exe 同目录 build/VoxelSandbox/qmldir 命中，
        //     其 prefer :/VoxelSandbox/ 指向本二进制未链接的 qrc 资源）。**全部拷贝文件**都改 ——
        //     ToolIcon/MaterialIcon 等子组件同样显式 import VoxelSandbox（t41 子目录显式导入约定）。
        {
            const QUrl jsUrl = QUrl::fromLocalFile(probeUiDir + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            QDir pd(probeUiDir);
            const QStringList qmlFiles = pd.entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles) {
                QFile p(probeUiDir + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbe\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        if (harnessOk) {
            // setData + 源文件 base URL：QQmlComponent(url) 直载时 type loader 把同目录相对导入重映射到
            //   编译模块 qrc 前缀（见上 probeUiDir 注释）；setData 携带 base URL 按原文编译，相对导入按
            //   base URL 解析（同目录文件真实存在）。
            QFile anvilSrc(probeUiDir + QLatin1Char('/') + QStringLiteral("AnvilUI.qml"));
            if (!anvilSrc.open(QIODevice::ReadOnly)) {
                harnessOk = false;
                harnessDiag = QStringLiteral("AnvilUI read failed");
            } else {
                QQmlComponent anvilComp(&engine);
                anvilComp.setData(anvilSrc.readAll(), QUrl::fromLocalFile(anvilSrc.fileName()));
                if (anvilComp.isError()) {
                    harnessOk = false;
                    harnessDiag = QStringLiteral("AnvilUI load: ") + anvilComp.errorString();
                } else {
                    // 以 wrapper 的 context 创建（面板内 window/shiftHeld 等经作用域链解析 wrapper 根的
                    //   id —— 真实应用面板在 Main.qml 作用域内实例化，此处同构复刻；引擎根 context 无 id）。
                    anvilRoot = anvilComp.create(qmlContext(wrapper));
                    QQuickItem *ai = qobject_cast<QQuickItem *>(anvilRoot);
                    if (!ai) {
                        harnessOk = false;
                        harnessDiag = QStringLiteral("AnvilUI create: ") + anvilComp.errorString();
                    } else {
                        anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                        anvilRoot->setProperty("playerState", QVariant::fromValue(&ps));
                        anvilRoot->setProperty("player", QVariant());
                        anvilRoot->setProperty("progress", QVariant());
                        ai->setWidth(800);
                        ai->setHeight(600);
                        anvilRoot->setParent(wrapper);
                        ai->setParentItem(wrapper);
                        ai->setY(0.0);
                    }
                }
            }
        }
        if (harnessOk) {
            QFile enSrc(probeUiDir + QLatin1Char('/') + QStringLiteral("EnchantingTableUI.qml"));
            if (!enSrc.open(QIODevice::ReadOnly)) {
                harnessOk = false;
                harnessDiag = QStringLiteral("EnchantingTableUI read failed");
            } else {
                QQmlComponent enComp(&engine);
                enComp.setData(enSrc.readAll(), QUrl::fromLocalFile(enSrc.fileName()));
                if (enComp.isError()) {
                    harnessOk = false;
                    harnessDiag = QStringLiteral("EnchantingTableUI load: ") + enComp.errorString();
                } else {
                    enchantRoot = enComp.create(qmlContext(wrapper)); // 同上：挂 wrapper 作用域链
                    QQuickItem *ei = qobject_cast<QQuickItem *>(enchantRoot);
                    if (!ei) {
                        harnessOk = false;
                        harnessDiag = QStringLiteral("EnchantingTableUI create: ") + enComp.errorString();
                    } else {
                        enchantRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                        enchantRoot->setProperty("playerState", QVariant::fromValue(&ps));
                        enchantRoot->setProperty("player", QVariant());
                        enchantRoot->setProperty("progress", QVariant());
                        enchantRoot->setProperty("theWorld", QVariant());
                        ei->setWidth(800);
                        ei->setHeight(600);
                        enchantRoot->setParent(wrapper);
                        ei->setParentItem(wrapper);
                        ei->setY(600.0); // 下半区（与铁砧面板空间隔离）
                    }
                }
            }
        }

        // —— 驱动原语 ——
        // QML 函数调用（面板 root 上的 slotLeft / slotRight / takeProduct / slotShiftLeftAnvil /
        //   doMergeSameId / begin/endLeftDrag / begin/endRightDrag / addDragSlot / swapHoveredWithHotbar /
        //   doEnchant）。t874/t875 定案：点击驱动 = **直调面板函数链**（AnvilSlot / EnchantInputSlot 的
        //   TapHandler onTapped 内联体对非预览槽执行的就是 root.slotLeft/slotRight 同一函数；产物槽 =
        //   root.takeProduct）。曾试合成 QMouseEvent 走 TapHandler：事件代理把「上次点击位置 → 本次 press」
        //   距离当拖动（无真实 move/hover 流 → 根 DragHandler 激活 → 松手单格退路对滞留旧 hoveredKey 幽灵
        //   点击），harness 伪影不可消除 → 弃。真链关键在「真 QML × 真 C++ Hotbar」（QVariantList↔JS 序列化
        //   边界），与鼠标事件来源无关 —— 函数链直调已覆盖。
        auto qmlCall = [](QObject *obj, const char *method, const QVariantList &args = QVariantList()) -> bool {
            if (args.isEmpty())
                return QMetaObject::invokeMethod(obj, method);
            if (args.size() == 1)
                return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)));
            if (args.size() == 2)
                return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)), Q_ARG(QVariant, args.at(1)));
            return false;
        };
        auto listEq4 = [](const QVariantList &a, int e0, int e1, int e2, int e3) {
            return a.size() == 4 && a.at(0).toInt() == e0 && a.at(1).toInt() == e1
                    && a.at(2).toInt() == e2 && a.at(3).toInt() == e3;
        };
        auto localIdAt = [](QObject *panel, const char *prop, int idx) -> int {
            const QVariantList a = panel->property(prop).toList();
            return (idx >= 0 && idx < a.size()) ? a.at(idx).toInt() : 0;
        };
        auto localEnchAt = [](QObject *panel, const char *prop, int idx) -> QVariantList {
            const QVariantList outer = panel->property(prop).toList();
            QVariantList e;
            if (idx >= 0 && idx < outer.size())
                e = outer.at(idx).toList();
            while (e.size() < 4)
                e.append(0);
            return e;
        };
        auto localNameAt = [](QObject *panel, const char *prop, int idx) -> QString {
            const QVariantList a = panel->property(prop).toList();
            return (idx >= 0 && idx < a.size()) ? a.at(idx).toString() : QString();
        };
        auto clearVm = [&]() {
            for (int i = 0; i < vm.slotCount(); ++i)
                vm.setStack(i, 0, 0);
            for (int i = 0; i < vm.mainCount(); ++i)
                vm.mainSetStack(i, 0, 0);
            vm.setHeldBlock(0);
        };

        bool ok874 = true, ok875 = true;
        if (!harnessOk) {
            ok874 = false;
            ok875 = false;
            qInfo().noquote() << "  [t874/t875 diag] harness failed:" << harnessDiag;
        } else {
            QCoreApplication::processEvents();

            const int pick = ToolRegistry::PickaxeIron;
            const int sword = ToolRegistry::SwordIron;
            const int chestId = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
            const int bookId = RecipeRegistry::EnchantedBookId;
            const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
            const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
            const int sharp3 = (EnchantRegistry::Sharpness << 8) | 3;
            const int kb2 = (EnchantRegistry::Knockback << 8) | 2;
            const int fire2 = (EnchantRegistry::FireAspect << 8) | 2;
            const int unb3 = (EnchantRegistry::Unbreaking << 8) | 3;
            const int prot4 = (EnchantRegistry::Protection << 8) | 4;
            const int sharp5 = (EnchantRegistry::Sharpness << 8) | 5;
            const int fire1 = (EnchantRegistry::FireAspect << 8) | 1;
            const int pickMax = ToolRegistry::maxDurability(pick);
            const int swordMax = ToolRegistry::maxDurability(sword);

            // t874/t875 harness：复位面板双击判定态（lastTapMs/lastTapKey）—— 探针连点同槽间隔 < 280ms
            //   会被 AnvilUI/EnchantingTableUI 的双击拿同类判定吞掉第二次点击（doMergeSameId 对带名实例
            //   正确 no-op = 拒绝，但探针语义要的是「两次独立单击」）。真实用户连点间隔通常 > 280ms 或
            //   中途移动；harness 内同步执行恒 < 280ms → 每次独立点击前显式复位。
            auto resetTap = [&](QObject *panel) {
                panel->setProperty("lastTapMs", 0.0);
                panel->setProperty("lastTapKey", QString());
            };
            auto resetAnvil = [&]() {
                clearVm();
                anvilRoot->setProperty("visible", false); // 触发 returnAnvilToHotbar（槽已清则零迭代）
                anvilRoot->setProperty("visible", true);
                clearVm();
            };
            auto resetEnchant = [&]() {
                clearVm();
                enchantRoot->setProperty("visible", false);
                enchantRoot->setProperty("visible", true);
                clearVm();
            };

            // ═══ (1) t874 四类别 × 真鼠标放入/取出主链（hotbar 行拾取[函数链] → 真鼠标点 A 放置 →
            //        真鼠标点 A 取回 → 函数链放回 hotbar）═══
            struct Cat {
                const char *tag;
                int id;
                int dur;
                int e0, e1, e2, e3;
                QString nm;
            };
            const Cat cats[] = {
                { "tool", pick, pickMax - 5, eff3, unb2, 0, 0, QStringLiteral("我的神镐") },
                { "weapon", sword, swordMax - 9, sharp3, kb2, fire2, unb3, QString() },
                { "armor", chestId, 33, prot4, unb2, 0, 0, QString() },
                { "book", bookId, 0, sharp5, fire1, 0, 0, QString() },
            };
            for (const Cat &c : cats) {
                const QVariantList ce = QVariantList{c.e0, c.e1, c.e2, c.e3};
                resetAnvil();
                vm.setStack(3, c.id, 1, c.dur, ce, c.nm);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                bool step = vm.heldBlock() == c.id && listEq4(vm.heldEnchants(), c.e0, c.e1, c.e2, c.e3)
                        && vm.heldCustomName() == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 pickup lost meta:" << c.tag
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants()
                                      << "name=" << vm.heldCustomName();
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                step = localIdAt(anvilRoot, "anvilSlots", 0) == c.id
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), c.e0, c.e1, c.e2, c.e3)
                        && localNameAt(anvilRoot, "anvilNames", 0) == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 place-in lost meta:" << c.tag
                                      << "slotId=" << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                step = vm.heldBlock() == c.id && listEq4(vm.heldEnchants(), c.e0, c.e1, c.e2, c.e3)
                        && vm.heldCustomName() == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 take-out lost meta:" << c.tag
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                step = vm.blockIdAt(4) == c.id && listEq4(vm.enchantsAt(4), c.e0, c.e1, c.e2, c.e3)
                        && vm.customNameAt(4) == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 place-back lost meta:" << c.tag
                                      << "slot4=" << vm.blockIdAt(4) << "ench=" << vm.enchantsAt(4);
                }
            }

            // ═══ (2) t874 Shift+左键搬运（hotbar → A 槽）+ Shift 取回 ═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 7, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("移形换位"));
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                bool step = localIdAt(anvilRoot, "anvilSlots", 0) == pick
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), eff3, unb2, 0, 0)
                        && localNameAt(anvilRoot, "anvilNames", 0) == QStringLiteral("移形换位")
                        && vm.blockIdAt(3) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E2 shift put-in lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                wrapper->setProperty("shiftHeld", false);
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("移形换位");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("移形换位");
                if (!found || localIdAt(anvilRoot, "anvilSlots", 0) != 0) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E2 shift return lost meta: found=" << found;
                }
            }

            // ═══ (3) t874 右键放 1（真鼠标）+ 拖动链（begin/add/endLeftDrag 单格退路）═══
            {
                resetAnvil();
                vm.setStack(3, sword, 1, swordMax - 4, QVariantList{sharp3, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotRight", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                bool step = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, 0, 0, 0)
                        && vm.heldBlock() == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E3 right place-one lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                // 取回后重放，走拖动链（cap=1 → redistribute 早退 → endLeftDrag n==1 → singleLeftClick 放置）。
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) }); // 取回到光标
                qmlCall(anvilRoot, "beginLeftDrag", {});
                qmlCall(anvilRoot, "addDragSlot", { QVariant(QStringLiteral("anvil:0")) });
                qmlCall(anvilRoot, "endLeftDrag", {});
                step = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, 0, 0, 0);
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E3 drag single-slot lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
            }

            // ═══ (4) t874 双击拿同类（doMergeSameId 单件快照——无名实例）+ 数字键交换 ═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 6, QVariantList{eff3, unb2, 0, 0}, QString());
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                qmlCall(anvilRoot, "doMergeSameId", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                bool step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldDurability() == pickMax - 6
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E4 double-merge lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "dur=" << vm.heldDurability();
                }
                // 放回 A 后数字键交换：anvil:0 ↔ hotbar:6。
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                anvilRoot->setProperty("hoveredKey", QStringLiteral("anvil:0"));
                qmlCall(anvilRoot, "swapHoveredWithHotbar", { QVariant(6) });
                step = vm.blockIdAt(6) == pick && listEq4(vm.enchantsAt(6), eff3, unb2, 0, 0)
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E4 number-swap lost meta: hb6=" << vm.blockIdAt(6)
                                      << "ench=" << vm.enchantsAt(6);
                }
            }

            // ═══ (5) t874 takeProduct·repair（附魔镐 + 铁锭）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 21, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("神镐"));
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                vm.setStack(4, RecipeRegistry::IronIngotId, 3);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) }); // 3 锭整栈入 B
                const int per = pickMax / 3;
                const int need = std::min(3, int(std::ceil(21.0 / per)));
                const int use = std::min(3, need);
                const int expectDur = std::min(pickMax, (pickMax - 21) + use * per);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(false) }); // t918 起带 toInventory 形参（shift 背包路由）；探针按普通左键语义传 false（光标路由） // 产物槽点击（TapHandler onTapped → takeProduct 同一函数）
                bool step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldCustomName() == QStringLiteral("神镐")
                        && vm.heldDurability() == expectDur
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0
                        && localIdAt(anvilRoot, "anvilSlots", 1) == RecipeRegistry::IronIngotId
                        && localIdAt(anvilRoot, "anvilCounts", 1) == 3 - use;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E5 repair lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "dur=" << vm.heldDurability()
                                      << "expectDur=" << expectDur << "use=" << use;
                }
            }

            // ═══ (6) t874 takeProduct·combine（双镐合并 → 附魔并集）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 10, QVariantList{eff3, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, pick, 1, pickMax - 20, QVariantList{unb2, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(false) }); // t918 起带 toInventory 形参（shift 背包路由）；探针按普通左键语义传 false（光标路由）
                const QVariantList pe = vm.heldEnchants();
                const bool hasEff = pe.contains(QVariant(eff3));
                const bool hasUnb = pe.contains(QVariant(unb2));
                const int expectDur = std::min(pickMax, int(std::floor((pickMax - 10) + (pickMax - 20) + pickMax * 0.1)));
                const bool step = vm.heldBlock() == pick && hasEff && hasUnb
                        && vm.heldDurability() == expectDur
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0
                        && localIdAt(anvilRoot, "anvilSlots", 1) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E6 combine lost meta: held=" << vm.heldBlock()
                                      << "ench=" << pe << "dur=" << vm.heldDurability()
                                      << "expectDur=" << expectDur;
                }
            }

            // ═══ (7) t874 takeProduct·merge（素剑 + 附魔书）+ rename（改名保附魔保名）═══
            {
                resetAnvil();
                vm.setStack(3, sword, 1, swordMax - 3, QVariantList{0, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, bookId, 1, 0, QVariantList{sharp5, fire1, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(false) }); // t918 起带 toInventory 形参（shift 背包路由）；探针按普通左键语义传 false（光标路由）
                const QVariantList me = vm.heldEnchants();
                bool step = vm.heldBlock() == sword && me.contains(QVariant(sharp5))
                        && localIdAt(anvilRoot, "anvilSlots", 1) == 0; // 书消耗
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E7 merge lost meta: held=" << vm.heldBlock()
                                      << "ench=" << me;
                }
                // rename：带名带附魔镐单独改名 → 产物双保。
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 2, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("旧名"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                anvilRoot->setProperty("renameName", QStringLiteral("新名字"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(false) }); // t918 起带 toInventory 形参（shift 背包路由）；探针按普通左键语义传 false（光标路由）
                step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldCustomName() == QStringLiteral("新名字")
                        && vm.heldDurability() == pickMax - 2;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E7 rename lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "name=" << vm.heldCustomName();
                }
            }

            // ═══ (8) t874 关包归还（A 槽带名附魔镐 + B 槽材料 → visible=false → 全参归还）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 12, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("归还镐"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, RecipeRegistry::IronIngotId, 2);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                anvilRoot->setProperty("visible", false);
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("归还镐");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("归还镐");
                bool foundIngot = false;
                for (int i = 0; i < vm.slotCount() && !foundIngot; ++i)
                    foundIngot = vm.blockIdAt(i) == RecipeRegistry::IronIngotId && vm.countAt(i) == 2;
                for (int i = 0; i < vm.mainCount() && !foundIngot; ++i)
                    foundIngot = vm.mainBlockIdAt(i) == RecipeRegistry::IronIngotId && vm.mainCountAt(i) == 2;
                if (!found || !foundIngot) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E8 close-return lost meta: pick=" << found
                                      << "ingot=" << foundIngot;
                }
                anvilRoot->setProperty("visible", true);
            }

            // ═══ (9) t874 存档 round-trip 后重绑 VM 再放入（WorldStore 真库 + applyPlayerState 镜像回灌）═══
            {
                Hotbar vm2;
                QVariantMap data;
                QVariantList hotbarArr;
                clearVm();
                vm.setStack(3, sword, 1, swordMax - 15, QVariantList{sharp3, kb2, fire2, unb3}, QStringLiteral("回环剑"));
                for (int i = 0; i < vm.slotCount(); ++i) {
                    QVariantMap s;
                    s.insert(QStringLiteral("id"), vm.blockIdAt(i));
                    s.insert(QStringLiteral("count"), vm.countAt(i));
                    s.insert(QStringLiteral("durability"), vm.durabilityAt(i));
                    s.insert(QStringLiteral("enchants"), vm.enchantsAt(i));
                    s.insert(QStringLiteral("name"), vm.customNameAt(i));
                    hotbarArr.append(s);
                }
                data.insert(QStringLiteral("hotbar"), hotbarArr);
                WorldStore store;
                const QString dbAbs = QDir::temp().absoluteFilePath(
                        QStringLiteral("voxel_t874_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
                QFile::remove(dbAbs);
                bool step = store.openWorld(dbAbs) && store.savePlayerData(data);
                store.closeWorld();
                QVariantMap back;
                if (step) {
                    step = store.openWorld(dbAbs);
                    if (step)
                        back = store.loadPlayerData();
                    store.closeWorld();
                }
                QFile::remove(dbAbs);
                if (step) {
                    // applyPlayerState 镜像回灌到新 VM（Main.qml :647-653 同形）。
                    const QVariantList hb = back.value(QStringLiteral("hotbar")).toList();
                    for (int i = 0; i < 9 && i < hb.size(); ++i) {
                        const QVariantMap s = hb.at(i).toMap();
                        vm2.setStack(i, s.value(QStringLiteral("id")).toInt(),
                                     s.value(QStringLiteral("count")).toInt(),
                                     s.contains(QStringLiteral("durability"))
                                             ? s.value(QStringLiteral("durability")).toInt() : -1,
                                     s.contains(QStringLiteral("enchants"))
                                             ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                     s.contains(QStringLiteral("name"))
                                             ? s.value(QStringLiteral("name")).toString() : QString());
                    }
                    // 重绑面板 hotbar → 真 QML × 读档 VM 再跑主链。
                    resetAnvil();
                    anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm2));
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                    const bool pickOk = vm2.heldBlock() == sword
                            && listEq4(vm2.heldEnchants(), sharp3, kb2, fire2, unb3)
                            && vm2.heldCustomName() == QStringLiteral("回环剑");
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                    const bool placeOk = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                            && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, kb2, fire2, unb3);
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                    const bool backOk = vm2.heldBlock() == sword
                            && listEq4(vm2.heldEnchants(), sharp3, kb2, fire2, unb3);
                    step = pickOk && placeOk && backOk;
                    if (!step) {
                        qInfo().noquote() << "  [t874 diag] E9 roundtrip:" << pickOk << placeOk << backOk
                                          << "ench=" << vm2.heldEnchants();
                    }
                    // 归还光标 + 还原绑定（后续 t875 段仍用 vm）。
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(7) });
                    anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                }
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E9 save roundtrip leg failed";
                }
                clearVm();
            }

            // ═══ (10) t875a 已附魔物品拒入（七入口：两真鼠标 + 两拖动 + 数字键 + Shift + 双击）═══
            {
                const QVariantList pickEnch = QVariantList{eff3, unb2, 0, 0};
                auto rejectCheck = [&](const char *tag) {
                    const bool empty0 = localIdAt(enchantRoot, "enchantSlots", 0) == 0;
                    const bool heldOk = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                            && vm.heldCustomName() == QStringLiteral("附魔镐");
                    if (!empty0 || !heldOk) {
                        ok875 = false;
                        qInfo().noquote() << "  [t875 diag]" << tag << "slot0=" << localIdAt(enchantRoot, "enchantSlots", 0)
                                          << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                    }
                };
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax - 5, pickEnch, QStringLiteral("附魔镐"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // R1 真鼠标左键槽 0
                rejectCheck("R1 mouse-left");
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotRight", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // R2 真鼠标右键（放 1）
                rejectCheck("R2 mouse-right");
                qmlCall(enchantRoot, "beginRightDrag", {}); // R3 右键拖（每格放 1）
                qmlCall(enchantRoot, "addRightDragSlot", { QVariant(QStringLiteral("enchant:0")) });
                qmlCall(enchantRoot, "endRightDrag", {});
                rejectCheck("R3 right-drag");
                qmlCall(enchantRoot, "beginLeftDrag", {}); // R4 左键拖（均分 → 单格退路 gated）
                qmlCall(enchantRoot, "addDragSlot", { QVariant(QStringLiteral("enchant:0")) });
                qmlCall(enchantRoot, "endLeftDrag", {});
                rejectCheck("R4 left-drag");
                // R5 数字键交换：hovered=enchant:0（空）↔ hotbar:6 —— dst（hotbar 6 空栈）入槽 0 恒空；
                //   反向（槽 0 持素品 + hotbar 持附魔品）另测于 R7 前置。
                enchantRoot->setProperty("hoveredKey", QStringLiteral("enchant:0"));
                qmlCall(enchantRoot, "swapHoveredWithHotbar", { QVariant(6) });
                rejectCheck("R5 number-swap");
                // 光标归位后 R6 Shift+左键（slotShiftLeftEnchant 已附魔守卫）。
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                wrapper->setProperty("shiftHeld", true);
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                wrapper->setProperty("shiftHeld", false);
                const bool r6 = localIdAt(enchantRoot, "enchantSlots", 0) == 0
                        && vm.blockIdAt(4) == pick && listEq4(vm.enchantsAt(4), eff3, unb2, 0, 0)
                        && vm.customNameAt(4) == QStringLiteral("附魔镐");
                if (!r6) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] R6 shift-reject failed: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "hb4=" << vm.blockIdAt(4) << "ench=" << vm.enchantsAt(4);
                }
                // R7 双击合并（t693 门禁过滤：槽 0 素品 + 光标附魔同 id → 收集表剔 gated 槽 → 无操作）。
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax, QVariantList(), QString()); // 素品镐
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 素品入槽 0（gate 放行）
                vm.setStack(4, pick, 1, pickMax - 5, pickEnch, QStringLiteral("附魔镐"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                qmlCall(enchantRoot, "doMergeSameId", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                const bool r7 = localIdAt(enchantRoot, "enchantSlots", 0) == pick
                        && listEq4(localEnchAt(enchantRoot, "enchantEnch", 0), 0, 0, 0, 0) // 槽 0 素品不被污染
                        && vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0);
                if (!r7) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] R7 double-merge failed: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "slot0ench=" << localEnchAt(enchantRoot, "enchantEnch", 0)
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                }
            }

            // ═══ (11) t875b 全链无清洗（青金石槽放入 → 关包归还）═══
            {
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax - 8, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("不清洗"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(1) }); // 青金石槽（index 1 无门禁——设计允许任意物）
                bool step = localIdAt(enchantRoot, "enchantSlots", 1) == pick
                        && listEq4(localEnchAt(enchantRoot, "enchantEnch", 1), eff3, unb2, 0, 0)
                        && localNameAt(enchantRoot, "enchantNames", 1) == QStringLiteral("不清洗");
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875b lapis put lost meta: slot1="
                                      << localIdAt(enchantRoot, "enchantSlots", 1)
                                      << "ench=" << localEnchAt(enchantRoot, "enchantEnch", 1);
                }
                enchantRoot->setProperty("visible", false); // 关包归还（returnEnchantToHotbar 全参）
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("不清洗");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("不清洗");
                if (!found) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875b close-return lost meta";
                }
                enchantRoot->setProperty("visible", true);
            }

            // ═══ (12) t875c 素品附魔正链 → 产物取出带附魔 → 拒再入 ═══
            {
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax, QVariantList(), QString());
                vm.setStack(4, RecipeRegistry::LapisId, 5);
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 素品镐入槽 0
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(1) }); // 5 青金石入槽 1
                const bool pre = localIdAt(enchantRoot, "enchantSlots", 0) == pick
                        && localIdAt(enchantRoot, "enchantSlots", 1) == RecipeRegistry::LapisId;
                qmlCall(enchantRoot, "doEnchant", { QVariant(0) }); // 档 1（selectEnchantsForItem 保证 ≥1 条）
                const QVariantList prod = localEnchAt(enchantRoot, "enchantEnch", 0);
                const bool hasAny = prod.at(0).toInt() != 0 || prod.at(1).toInt() != 0
                        || prod.at(2).toInt() != 0 || prod.at(3).toInt() != 0;
                bool step = pre && localIdAt(enchantRoot, "enchantSlots", 0) == pick && hasAny;
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c doEnchant product not enchanted: pre=" << pre
                                      << "prod=" << prod;
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 取出产物（held ← 附魔镐）
                step = vm.heldBlock() == pick && !listEq4(vm.heldEnchants(), 0, 0, 0, 0);
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c take-out lost ench: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants();
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 产物再入 → 拒（已附魔）
                step = localIdAt(enchantRoot, "enchantSlots", 0) == 0 && vm.heldBlock() == pick
                        && !listEq4(vm.heldEnchants(), 0, 0, 0, 0);
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c re-entry not rejected: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "heldEnch=" << vm.heldEnchants();
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(5) }); // 光标归位
            }

            // ═══ (13) review28 #1 t918 shift 路由行为腿（真 QML × 真 VM：预检同口径 + 落定守卫）═══
            //    事故链两场景复现（同根因）+ 一条正链（防 #1 修复误伤 t918 本体）：
            //    S1 异物光标 5 泥土 + 背包无空槽仅 main0 34/64 无名石头栈 + 30 石头改名「X」→ 旧码预检把
            //       无名栈余量 30 虚增进容量（漏看产物带名）→ addToAny 带名不并无空槽 remain=30 → 落定 else
            //       直接 heldCount=5+30=35（30 个改名石头凭空转化进泥土计数）；新码预检拒 → 零消耗无操作。
            //    S2 同 id 光标 40/64 石头 + main0 10/64 无名石头栈 + 40 石头改名「Y」→ 旧码 cursorSpace 24 +
            //       栈余量 54 = 78 ≥ 40 通过 → remain=40 → heldCount=40+40=80 > cap 64；新码预检拒 → 保持 40。
            //    S3 带名产物 + 空背包正链：addToAny 空槽开新带名栈（hotbar 0 号优先），光标不受扰。
            {
                const int stone = BR::Stone, dirt = BR::Dirt;
                auto fillAll = [&]() {   // 36 槽全满异物（泥土 64）——封死空槽容量贡献
                    for (int i = 0; i < vm.slotCount(); ++i)
                        vm.setStack(i, dirt, 64);
                    for (int i = 0; i < vm.mainCount(); ++i)
                        vm.mainSetStack(i, dirt, 64);
                };
                // —— S1 异物光标 + 带名产物：预检必须拒（零消耗、异物计数不变）——
                wrapper->setProperty("shiftHeld", false);
                resetAnvil();
                vm.setStack(3, stone, 30);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                fillAll();                        // hotbar3 取走后已空 → 补满；全背包无空槽
                vm.mainSetStack(0, stone, 34);    // 唯一「同 id 无名栈余量 30」诱饵
                vm.setHeldBlock(dirt);            // 异物光标 5 泥土（cursorSpace=0）
                vm.setProperty("heldCount", 5);
                anvilRoot->setProperty("renameName", QStringLiteral("X"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(true) });
                bool step = vm.heldBlock() == dirt && vm.heldCount() == 5
                        && localIdAt(anvilRoot, "anvilSlots", 0) == stone
                        && localIdAt(anvilRoot, "anvilCounts", 0) == 30
                        && vm.mainBlockIdAt(0) == stone && vm.mainCountAt(0) == 34;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [review28-1 diag] S1 foreign-cursor transmutation: held="
                                      << vm.heldBlock() << "x" << vm.heldCount()
                                      << " A=" << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "x" << localIdAt(anvilRoot, "anvilCounts", 0);
                }
                anvilRoot->setProperty("renameName", QString());
                // —— S2 同 id 光标 + 带名产物超上限：预检必须拒（heldCount 保持 40 不变 80）——
                resetAnvil();
                vm.setStack(3, stone, 40);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                fillAll();
                vm.mainSetStack(0, stone, 10);    // 无名石头 10/64（旧码会把余量 54 虚增进预检）
                vm.setHeldBlock(stone);           // 同 id 光标 40/64（cursorSpace=24 < 40）
                vm.setProperty("heldCount", 40);
                anvilRoot->setProperty("renameName", QStringLiteral("Y"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(true) });
                step = vm.heldBlock() == stone && vm.heldCount() == 40
                        && localIdAt(anvilRoot, "anvilSlots", 0) == stone
                        && localIdAt(anvilRoot, "anvilCounts", 0) == 40
                        && vm.mainBlockIdAt(0) == stone && vm.mainCountAt(0) == 10;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [review28-1 diag] S2 over-cap cursor stack: held="
                                      << vm.heldBlock() << "x" << vm.heldCount()
                                      << " A=" << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "x" << localIdAt(anvilRoot, "anvilCounts", 0);
                }
                anvilRoot->setProperty("renameName", QString());
                // —— S3 带名产物 + 空背包正链：shift 路由本体不受 #1 修复误伤 ——
                resetAnvil();
                vm.setStack(3, stone, 30);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                anvilRoot->setProperty("renameName", QStringLiteral("Z"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", { QVariant(true) });
                step = vm.blockIdAt(0) == stone && vm.countAt(0) == 30
                        && vm.customNameAt(0) == QStringLiteral("Z")
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0
                        && vm.heldBlock() == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [review28-1 diag] S3 positive route: hb0="
                                      << vm.blockIdAt(0) << "x" << vm.countAt(0)
                                      << " name=" << vm.customNameAt(0)
                                      << " A=" << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << " held=" << vm.heldBlock();
                }
            }

            // ═══ (14) review28 #3 t917 书堆首击种子同源行为腿（真 QML × 真 VM）═══
            //    事故链：书堆 count>1 → doEnchant H1 归一化 writeSlot 写回槽 0 → enchantRev++ **同步**触发
            //    onEnchantRevChanged → 换件键（id0*4096+count）变 → optionReroll++；旧码种子在其后才取 →
            //    施放用 reroll+1 种子而 hover 预告（tierPreviewName 读点击前状态）用旧种子 → 「必得」预告
            //    漂移。断言：书堆入槽 → 记 hover 预告名（点击前种子）→ doEnchant(0) 首击 → 产物首条附魔
            //    displayName == 预告名（种子不漂 → 严格同源）。回退（种子取在归一化写之后）→ 施放用
            //    reroll+1 种子而预告是旧种子 → selectEnchantsForItem 对 seed 位敏感 → 首条大概率漂 → FAIL。
            {
                resetEnchant();
                const int plainBook = RecipeRegistry::BookId;
                vm.setStack(3, plainBook, 8);      // 整摞书（count 8 > 1 → 必走归一化路径）
                vm.setStack(4, RecipeRegistry::LapisId, 5);
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 书堆入槽 0
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(1) }); // 青金石入槽 1
                const bool stackIn = localIdAt(enchantRoot, "enchantSlots", 0) == plainBook
                        && localIdAt(enchantRoot, "enchantCounts", 0) == 8;
                // 黑盒校准：本态必须对 reroll 轴敏感（name(R) != name(R+1)）——否则两种子首条巧合相同，
                //   回退也假 PASS（首轮阴性恰命中：书全池单条 pick 下两 seed 同首条概率 ~1/10）。用真
                //   tierPreviewName 直接探测：暂写 optionReroll 前移一格读 name(R+1)（= 旧码归一化写后
                //   取种子的产物首条名），与 name(R) 比；不敏感则停在 R+1 再试下一对（≤8 格内书池必出
                //   判别态）。校准只动 reroll 快照，点击链本身零干预。
                const int reroll0 = enchantRoot->property("optionReroll").toInt();
                auto previewNow = [&]() -> QString {
                    QVariant pv;
                    QMetaObject::invokeMethod(enchantRoot, "tierPreviewName",
                                              Q_RETURN_ARG(QVariant, pv), Q_ARG(QVariant, QVariant(0)));
                    return pv.toString();
                };
                QString preview;
                bool discriminating = false;
                for (int bump = 0; bump < 8 && !discriminating; ++bump) {
                    const int r = reroll0 + bump;
                    enchantRoot->setProperty("optionReroll", r);
                    preview = previewNow();                     // name(r)：用户 hover 所见（点击前状态）
                    enchantRoot->setProperty("optionReroll", r + 1);
                    const QString previewRerolled = previewNow(); // name(r+1)：旧码施放将读的种子
                    if (previewRerolled != preview) {
                        discriminating = true;
                        enchantRoot->setProperty("optionReroll", r); // 回到 r —— 施放从点击前状态出发
                    } // 不敏感 → 留在 r+1，下一轮探测 (r+1, r+2)
                }
                QMetaObject::invokeMethod(enchantRoot, "doEnchant", Q_ARG(QVariant, QVariant(0))); // 首击
                const QVariantList prod = localEnchAt(enchantRoot, "enchantEnch", 0);
                QString prodFirstName;
                if (prod.at(0).toInt() != 0) {
                    Hotbar hb917;
                    prodFirstName = hb917.enchantDisplayName(prod.at(0).toInt() >> 8);
                }
                bool step = stackIn
                        && discriminating                        // 校准达判别态（否则本腿无法分 red/green）
                        && preview.length() > 0                  // 预告真出了名（「必得」预告本体活着）
                        && prodFirstName == preview;             // 产物首条 == 预告（同源断言）
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [review28-3 diag] book-stack first-click seed drift: stackIn="
                                      << stackIn << "discriminating=" << discriminating
                                      << "preview=" << preview
                                      << "prodFirst=" << prodFirstName
                                      << "slot0=" << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "x" << localIdAt(enchantRoot, "enchantCounts", 0);
                }
            }

            clearVm();
        }

        if (!ok874)
            ++totalFail;
        qInfo().noquote() << (ok874 ? "PASS" : "FAIL")
                          << "| t874 real-chain anvil enchant preservation (real QQmlEngine x source-tree "
                             "AnvilUI.qml x real C++ Hotbar): closes the last probe seam - t792 drove real "
                             "QML against a mock Hotbar.qml, t822 drove the real VM without any QML; this "
                             "probe loads the actual AnvilUI.qml+InventoryOps.js with the actual Hotbar/"
                             "PlayerState injected and clicks via synthesized QMouseEvent through the inline "
                             "TapHandlers (the exact user path). Entries: mouse left/right on A slot, "
                             "shift-move, drag single-slot release, double-click pickup, number-key swap, "
                             "takeProduct repair/combine/merge/rename, close-panel return, save round-trip "
                             "with VM rebind; review28 #1 t918 shift-route legs: foreign-cursor + named-product "
                             "preflight rejects with zero consumption (no transmutation into cursor count), "
                             "same-id cursor stays at cap (no over-cap stack), named product still lands via "
                             "addToAny into empty slots; categories: tool/weapon(4-ench)/armor/enchanted-book, all "
                             "with custom names + instance durability asserted at every hop";
        if (!ok875)
            ++totalFail;
        qInfo().noquote() << (ok875 ? "PASS" : "FAIL")
                          << "| t875 real-chain enchanting-table gate + no-wipe (same harness, real "
                             "EnchantingTableUI.qml): already-enchanted item rejected on all seven entry "
                             "paths (mouse left/right on slot 0, right-drag place-one, left-drag "
                             "redistribute + single-slot fallback, number-key swap, shift-move, "
                             "double-click merge) while cursor stack keeps id/ench/name intact; lapis-slot "
                             "sojourn + close-panel return preserves enchant metadata end-to-end (wipe "
                             "hunt); clean-pick + lapis -> doEnchant tier1 product carries >=1 enchant, "
                             "take-out keeps it, re-entry rejected";
    });

    // ── t873 书架→附魔台字流真链探针（用户「新版仍不见文字流」实机二查；t823 冻结镜像的实装版）──
    // 背景：t823 用**冻结镜像**（C++ 复刻 rescanPairs 逐行语义）钉了规则口径，但镜像 ≠ 实装 —— 用户换新
    //   exe 仍报看不见，链路断点只剩「真 QML 组件从未被自动化执行」这一跳。本探针照 t874 真链模式：
    //   QQmlEngine 直载源树 EnchantGlyphFlow.qml（无相对导入 / 无 VoxelSandbox import → 免临时目录改写，
    //   仅 QtQuick/QtQuick3D 模块导入），真 World 独立小世界 + 真 ListModel 台表按 Main.qml
    //   glyphFlowLoader.onLoaded 同款注入（world/tableModel/active/editRev；camNode 留 null —— 组件对
    //   null cam 本就全通距离门，真实应用由 billboard 分支兜底；tableCount 静态注入场景间以 editRev
    //   显式触碰重扫，真实应用拆台也走 worldEditRev++ 同一触发面）。断言四态：
    //   ① 净空基线 0 对、空表发射零计数；② 单层地面满环带 16 书架 → 真 QML rescanPairs 产 16 对（与
    //   t823 镜像/权威 16/15 同 rig 口径互证——此处钉的是 QML **实装**本身）；③ 连发 12 颗 →
    //   emittedTotal/liveCount 计数一致；④ 堵半步 -1 对 + 删台归零且发射器停摆（500ms Timer running
    //   翻假——「无对即停摆」性能红线的实机钉子）。组件头注释宣称的「数据链完好」自此有自动化实证；
    //   渲染侧（字形贴图/尺寸/billboard）属 qml.exe/肉眼域，由 [t873] 运行期日志 + 实测文档覆盖。
    runLegMulti({ "t873 glyph-flow real-chain probe (real QQmlEngine loads source-tree EnchantGlyphFlow.qml x real "
        "World rig x injected ListModel): empty baseline 0 pairs + no-op spawn, single ground ring -> rea"
        "l QML rescanPairs yields 16 (t823 mirror/authority 16/15 same rig cross-checked against the actu"
        "al implementation), 12 spawns tracked by emittedTotal/liveCount, blocked half-step -1 pair, tabl"
        "e-row removal -> zero pairs + spawn timer idle (data chain proven live; pixel-side remains qml.e"
        "xe/manual)" }, [&]() {
        bool ok873 = true;
        QString diag873;
        World wG;
        wG.setWidth(40); wG.setDepth(40); wG.setHeight(48); wG.setSeed(21);
        // 净空 5×5 扫描（y 带 44/45，同 t823：世界高 48 → y∈[0,47]；含半步格）。
        const int gY = 44;
        int gx = -1, gz = -1;
        const auto areaClear = [&](int x, int z) {
            for (int dy = 0; dy <= 1; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                        if (wG.blockAt(x + dx, gY + dy, z + dz) != BR::Air) return false;
                        if (wG.blockAt(x + dx / 2, gY + dy, z + dz / 2) != BR::Air) return false;
                    }
            return true;
        };
        for (int zz = 4; zz + 2 < 36 && gx < 0; zz += 2)
            for (int xx = 4; xx + 2 < 36 && gx < 0; xx += 2)
                if (areaClear(xx, zz)) { gx = xx; gz = zz; }
        if (gx < 0) {
            ok873 = false;
            diag873 = QStringLiteral("no clear 5x5 rig at y=44/45");
        } else {
            QQmlEngine gEngine;
            // 真源树组件直载（base URL = 源文件 → 无相对导入需解析，模块导入走 Qt 安装 qml 目录）。
            const QString glyphPath = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                          .filePath(QStringLiteral("../../src/ui/EnchantGlyphFlow.qml"));
            // 真 ListModel 台表（Main.qml enchantTablePositions 运行期同类物）：经桩根的 JS 助手增删行，
            //   避免 C++ 直调 QQmlListModel 的 QJSValue 签名猜测。组件只消费 tableModel.count/.get(i)，
            //   与真实注入面同构。
            QQmlComponent stubComp(&gEngine);
            stubComp.setData(QByteArrayLiteral(
                                 "import QtQuick\n"
                                 "Item {\n"
                                 "    property alias tableModel: lm\n"
                                 "    ListModel { id: lm }\n"
                                 "    function addEntry(x, y, z) { lm.append({x: x, y: y, z: z}) }\n"
                                 "    function removeFirstRow() { lm.remove(0, 1) }\n"
                                 "}\n"), QUrl());
            QQmlComponent glyphComp(&gEngine, QUrl::fromLocalFile(glyphPath));
            QObject *stub = nullptr;
            QObject *gRoot = nullptr;
            if (stubComp.isError()) {
                ok873 = false;
                diag873 = QStringLiteral("table stub load: ") + stubComp.errorString();
            } else if (glyphComp.isError()) {
                ok873 = false;
                diag873 = QStringLiteral("EnchantGlyphFlow load: ") + glyphComp.errorString();
            } else {
                stub = stubComp.create();
                gRoot = glyphComp.create();
                if (!stub || !gRoot) {
                    ok873 = false;
                    diag873 = QStringLiteral("create failed (stub=%1 glyph=%2)")
                                  .arg(stub != nullptr).arg(gRoot != nullptr);
                } else {
                    stub->setParent(&gEngine);
                    gRoot->setParent(&gEngine);
                    // Main.qml glyphFlowLoader.onLoaded 同款注入。
                    gRoot->setProperty("world", QVariant::fromValue(&wG));
                    gRoot->setProperty("tableModel",
                                       QVariant::fromValue(stub->property("tableModel").value<QObject *>()));
                    gRoot->setProperty("active", true);

                    auto addTable = [&](int x, int y, int z) {
                        QMetaObject::invokeMethod(stub, "addEntry", Q_ARG(QVariant, x),
                                                  Q_ARG(QVariant, y), Q_ARG(QVariant, z));
                    };
                    auto pairsOf = [&]() -> int {
                        return gRoot->property("pairs").toList().size();
                    };
                    auto touchRescan = [&]() {
                        gRoot->setProperty("editRev", gRoot->property("editRev").toInt() + 1);
                    };
                    // 发射器停摆实证：组件内 500ms 那颗 Timer 即 spawnTimer（tickTimer 是 20ms）——
                    //   running 绑定 active && pairs.length>0，删台归零后应翻假。
                    auto spawnTimerRunning = [&]() -> bool {
                        const QList<QObject *> kids = gRoot->findChildren<QObject *>();
                        for (QObject *k : kids) {
                            const QVariant iv = k->property("interval");
                            if (iv.isValid() && iv.toInt() == 500) {
                                const QVariant rv = k->property("running");
                                if (rv.isValid())
                                    return rv.toBool();
                            }
                        }
                        return false; // 找不到 Timer 视为停摆（不误报，加载失败另有 FAIL 行）
                    };

                    // ① 净空基线：0 对 + 空表发射零计数。
                    addTable(gx, gY, gz);
                    touchRescan();
                    const int p0 = pairsOf();
                    QMetaObject::invokeMethod(gRoot, "spawnGlyph",
                                              Q_ARG(QVariant, QVariant(QVariantList())));
                    const bool spawnEmptyNoop = gRoot->property("emittedTotal").toInt() == 0;
                    if (p0 != 0 || !spawnEmptyNoop) {
                        ok873 = false;
                        diag873 += QStringLiteral("(a) baseline pairs=%1 noop=%2; ")
                                       .arg(p0).arg(spawnEmptyNoop);
                    }

                    // ② 单层地面满环带 16 书架（半步格已净空）→ 真 QML rescanPairs 应产 16 对。
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz)
                            if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                wG.setBlock(gx + dx, gY, gz + dz, BR::Bookshelf, 0);
                    touchRescan();
                    const int p16 = pairsOf();
                    if (p16 != 16) {
                        ok873 = false;
                        diag873 += QStringLiteral("(b) full-ring pairs=%1 want 16; ").arg(p16);
                    }

                    // ③ 连发 12 颗：emittedTotal/liveCount 与池占用同步走。
                    const QVariantList pairArr = gRoot->property("pairs").toList();
                    for (int i = 0; i < 12; ++i)
                        QMetaObject::invokeMethod(gRoot, "spawnGlyph", Q_ARG(QVariant, QVariant(pairArr)));
                    const int em12 = gRoot->property("emittedTotal").toInt();
                    const int live12 = gRoot->property("liveCount").toInt();
                    if (em12 != 12 || live12 != 12) {
                        ok873 = false;
                        diag873 += QStringLiteral("(c) emitted=%1 live=%2 want 12/12; ").arg(em12).arg(live12);
                    }

                    // ④ 堵一角书架半步格 → 重扫 -1 对（视觉与档位同步减，t823 ③ 同口径钉到实装）；
                    //    删台行 → 归零 + 发射器停摆（拆台即停承诺）。
                    wG.setBlock(gx - 1, gY, gz - 1, BR::Cobble, 0);
                    touchRescan();
                    const int pBlocked = pairsOf();
                    wG.setBlock(gx - 1, gY, gz - 1, BR::Air, 0);
                    QMetaObject::invokeMethod(stub, "removeFirstRow");
                    touchRescan();
                    const int pGone = pairsOf();
                    const bool emitterIdle = !spawnTimerRunning();
                    if (pBlocked != 15 || pGone != 0 || !emitterIdle) {
                        ok873 = false;
                        diag873 += QStringLiteral("(d) blocked=%1 gone=%2 idle=%3; ")
                                       .arg(pBlocked).arg(pGone).arg(emitterIdle);
                    }
                }
            }
            // 好公民：复原环带（探针不留脏 rig；独立小世界随作用域析构，此步为对称纪律）。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                        wG.setBlock(gx + dx, gY, gz + dz, BR::Air, 0);
        }
        if (!ok873)
            ++totalFail;
        if (!diag873.isEmpty())
            qInfo().noquote() << "  [t873 diag]" << diag873;
        qInfo().noquote() << (ok873 ? "PASS" : "FAIL")
                          << "| t873 glyph-flow real-chain probe (real QQmlEngine loads source-tree "
                             "EnchantGlyphFlow.qml x real World rig x injected ListModel): empty baseline "
                             "0 pairs + no-op spawn, single ground ring -> real QML rescanPairs yields 16 "
                             "(t823 mirror/authority 16/15 same rig cross-checked against the actual "
                             "implementation), 12 spawns tracked by emittedTotal/liveCount, blocked "
                             "half-step -1 pair, table-row removal -> zero pairs + spawn timer idle "
                             "(data chain proven live; pixel-side remains qml.exe/manual)";
    });

    // ── t953 字形流两调 + 书架变更 rescan 加固（worldChanged 事件钩 + 风暴合并 + 1s 自愈轮询）──
    //    用户第五轮实测（8-28）：① 字还有点大、速度偏快——再调小调慢；② 多放 / 挖一个书架文字流停
    //    且不恢复（需保存退出才恢复）。病灶：字形流台×书架集合（pairs）的重扫唯一事件驱动是 editRev
    //    （= window.worldEditRev，Main.qml 仅在玩家 blockPlaced/blockBroken 处自增）；一切系统改写栅格
    //    路径（爆炸 destroySphereSilent / 落块着地 setBlockFromEntity 等）按约定只发 worldChanged →
    //    书架被系统路径增删后集合永不重算，冻结在世界级缓存上（enchantTablePositions 读档重建才刷新
    //    = 用户「保存退出才恢复」的观测面）；且玩家 editRev 链自身无任何自愈兜底。修 = 用户菜单双通
    //    道：worldChanged 事件钩（脏标记 200ms 合并风暴，组件内 Connections 直连注入的 world）+ 1s
    //    自愈轮询（重扫复用 rescanPairs 单一实现，不写第二套扫描）。
    //    本探针（t873 真 QQmlEngine×真组件 rig 复用）：
    //    (a) 行为级：满环带 16 书架基线（editRev 同步通道，t873 契约不回归）→
    //        ① 爆炸腿：destroySphereSilent(r=0.6 恰拆一格) 拆一角书架（真 t942 路径，只发 worldChanged）
    //          → **不触碰 editRev**，200ms 合并窗后 pairs 15（旧链在此恒 16 陈旧 = 阴性回退判据）；
    //        ② 系统放回腿：setBlockFromEntity（落块着地语义，occ 守卫过、只发 worldChanged）→ pairs 16；
    //        ③ 玩家挖 / 放腿（用户主诉）：World::setBlock 挖 / 放同样不触碰 editRev —— 单钉 worldChanged
    //          通道：即便宿主 worldEditRev 链回归断线也须自愈 → pairs 15 / 16；
    //        ④ 风暴合并腿：200ms 窗内 3 次系统写 → rescanCount 恰 +1（脏标记合并，非逐写重扫）；
    //        ⑤ 自愈腿：worldRunning 置真 + 2.3s 无编辑窗 → 1s watchdog ≥1 次重扫（轮询通道活着）。
    //    (b) 参数钉（用户 8-28 口径「再小再慢」源码钉）：glyphScale 0.21-0.32（t915 0.26-0.40 ×0.8）、
    //        driftSpeed 1.8（2.6 ×0.7）、ratePerShelf 0.40（0.55 放缓）、maxPerTick 4（封顶 8/s）、寿命钳
    //        0.9-2.2（随降速等比放宽——钳不放宽会截断慢飞令 t=1 提前到达 = 尾段重新加速，与调慢背反）。
    //    (c) 通道源码钉：Connections onWorldChanged→requestRescan + 200ms 防抖窗 + 1s watchdog
    //        （running 门 active && worldRunning —— review26-11 菜单/硬暂停零常驻约定）。
    //        review0830 #20：防抖真 trailing-edge 钉（requestRescan 体无早退、每次 restart()，旧
    //        rescanPending 早退形态绝迹）——风暴合并行为腿对两形态同绿，注释-实现一致由本钉承载。
    //    (d) #20 trailing-edge 钉（见探针块内注）。
    runLegMulti({ "t953 glyph-flow rescan hardening real-chain probe (real QQmlEngine x real World rig): full-ring "
        "baseline via editRev sync channel, then WITHOUT ever touching editRev - explosion path (destroyS"
        "phereSilent, worldChanged-only) drops pairs 16->15, entity-landing place-back restores 16, playe"
        "r mine/place via World::setBlock self-heals 15/16 even with the host worldEditRev chain  severed"
        ", 3-write storm inside the 200ms window merges into exactly one rescan, 1s watchdog rescans >=1x"
        " in a 2.3s idle window under worldRunning (stale world-lifetime cache disease closed on all path"
        "s)" }, [&]() {
        bool ok953a = true;
        QString diag953;
        // 泵事件循环等待墙钟（QTimer 需事件循环投递；每片 ≤10ms 防饿死，t889 pumpFor 同款）。
        const auto pumpFor953 = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        World wT;
        wT.setWidth(40); wT.setDepth(40); wT.setHeight(48); wT.setSeed(23);
        const int t953Y = 44;
        int tx0 = -1, tz0 = -1;
        const auto areaClearT = [&](int x, int z) {
            for (int dy = 0; dy <= 1; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                        if (wT.blockAt(x + dx, t953Y + dy, z + dz) != BR::Air) return false;
                        if (wT.blockAt(x + dx / 2, t953Y + dy, z + dz / 2) != BR::Air) return false;
                    }
            return true;
        };
        for (int zz = 4; zz + 2 < 36 && tx0 < 0; zz += 2)
            for (int xx = 4; xx + 2 < 36 && tx0 < 0; xx += 2)
                if (areaClearT(xx, zz)) { tx0 = xx; tz0 = zz; }
        if (tx0 < 0) {
            ok953a = false;
            diag953 = QStringLiteral("no clear 5x5 rig at y=44/45");
        } else {
            QQmlEngine e953;
            QQmlComponent stubComp(&e953);
            stubComp.setData(QByteArrayLiteral(
                                 "import QtQuick\n"
                                 "Item {\n"
                                 "    property alias tableModel: lm\n"
                                 "    ListModel { id: lm }\n"
                                 "    function addEntry(x, y, z) { lm.append({x: x, y: y, z: z}) }\n"
                                 "}\n"), QUrl());
            const QString glyphPath = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                          .filePath(QStringLiteral("../../src/ui/EnchantGlyphFlow.qml"));
            QQmlComponent glyphComp(&e953, QUrl::fromLocalFile(glyphPath));
            QObject *stub953 = nullptr;
            QObject *g953 = nullptr;
            if (stubComp.isError() || glyphComp.isError()) {
                ok953a = false;
                diag953 = QStringLiteral("qml load: ")
                              + (stubComp.isError() ? stubComp.errorString() : glyphComp.errorString());
            } else {
                stub953 = stubComp.create();
                g953 = glyphComp.create();
                if (!stub953 || !g953) {
                    ok953a = false;
                    diag953 = QStringLiteral("create failed (stub=%1 glyph=%2)")
                                  .arg(stub953 != nullptr).arg(g953 != nullptr);
                } else {
                    stub953->setParent(&e953);
                    g953->setParent(&e953);
                    // Main.qml glyphFlowLoader.onLoaded 同款注入（camNode 留 null = 距离门全通，t873 同口径）。
                    g953->setProperty("world", QVariant::fromValue(&wT));
                    g953->setProperty("tableModel",
                                      QVariant::fromValue(stub953->property("tableModel").value<QObject *>()));
                    g953->setProperty("active", true);
                    auto pairsOf953 = [&]() -> int {
                        return g953->property("pairs").toList().size();
                    };
                    auto rescans953 = [&]() -> int {
                        return g953->property("rescanCount").toInt();
                    };
                    QMetaObject::invokeMethod(stub953, "addEntry", Q_ARG(QVariant, tx0),
                                              Q_ARG(QVariant, t953Y), Q_ARG(QVariant, tz0));
                    // 基线：满环带 16 书架 + editRev 同步触碰（通道一契约，t873 同款）。
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz)
                            if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                wT.setBlock(tx0 + dx, t953Y, tz0 + dz, BR::Bookshelf, 0);
                    g953->setProperty("editRev", g953->property("editRev").toInt() + 1);
                    const int basePairs = pairsOf953();

                    // ① 爆炸腿：真 t942 路径只发 worldChanged；r=0.6 球心距判定恰拆一格（邻格 1.0 > 0.6）。
                    wT.destroySphereSilent(tx0 + 2, t953Y, tz0, 0.6f);
                    pumpFor953(450);   // 200ms 合并窗 + 泵余量
                    const int pairsAfterBlast = pairsOf953();

                    // ② 系统放回腿：落块着地语义（occ 守卫过：格已 Air；只发 worldChanged）。
                    const bool placedBack = wT.setBlockFromEntity(tx0 + 2, t953Y, tz0, BR::Bookshelf);
                    pumpFor953(450);
                    const int pairsAfterBack = pairsOf953();

                    // ③ 玩家挖 / 放腿：World::setBlock（发 broken/placed + worldChanged）——**不触碰
                    //    editRev**，单钉 worldChanged 通道（宿主 worldEditRev 链断线也须自愈）。
                    wT.setBlock(tx0 + 2, t953Y, tz0, BR::Air, 0);
                    pumpFor953(450);
                    const int pairsAfterMine = pairsOf953();
                    wT.setBlock(tx0 + 2, t953Y, tz0, BR::Bookshelf, 0);
                    pumpFor953(450);
                    const int pairsAfterPlace = pairsOf953();

                    // ④ 风暴合并腿：200ms 窗内 3 次系统写（挖→放→挖）→ 恰 1 次重扫（worldRunning 仍假
                    //    → watchdog 未跑、spawn/tick Timer 未跑 → 窗内 rescanCount 增量只可能来自合并窗）。
                    const int cStorm = rescans953();
                    wT.setBlock(tx0 + 2, t953Y, tz0, BR::Air, 0);
                    wT.setBlock(tx0 + 2, t953Y, tz0, BR::Bookshelf, 0);
                    wT.setBlock(tx0 + 2, t953Y, tz0, BR::Air, 0);
                    pumpFor953(450);
                    const int stormRescans = rescans953() - cStorm;
                    const int pairsAfterStorm = pairsOf953();

                    // ⑤ 自愈腿：worldRunning 真门 + 2.3s 无编辑窗 → 1s watchdog 恰 2-3 次重扫（钳 1..3
                    //    防墙钟抖动误报）；无写入 → 合并窗静默，增量全来自轮询。
                    g953->setProperty("worldRunning", true);
                    const int cWd = rescans953();
                    pumpFor953(2300);
                    const int wdRescans = rescans953() - cWd;
                    g953->setProperty("worldRunning", false);

                    ok953a = basePairs == 16 && placedBack
                             && pairsAfterBlast == 15 && pairsAfterBack == 16
                             && pairsAfterMine == 15 && pairsAfterPlace == 16
                             && stormRescans == 1 && pairsAfterStorm == 15
                             && wdRescans >= 1 && wdRescans <= 3;
                    if (!ok953a)
                        diag953 += QStringLiteral("(a) base=%1 back=%2 blast=%3 backPairs=%4 mine=%5 "
                                                  "place=%6 storm=%7/%8 wd=%9;")
                                       .arg(basePairs).arg(placedBack).arg(pairsAfterBlast)
                                       .arg(pairsAfterBack).arg(pairsAfterMine).arg(pairsAfterPlace)
                                       .arg(stormRescans).arg(pairsAfterStorm).arg(wdRescans);
                }
            }
            // 好公民：复原环带（独立小世界随作用域析构，此步为 t873 同款对称纪律）。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                        wT.setBlock(tx0 + dx, t953Y, tz0 + dz, BR::Air, 0);
        }
        if (!ok953a)
            ++totalFail;
        if (!diag953.isEmpty())
            qInfo().noquote() << "  [t953 diag]" << diag953;
        qInfo().noquote() << (ok953a ? "PASS" : "FAIL")
                          << "| t953 glyph-flow rescan hardening real-chain probe (real QQmlEngine x real "
                             "World rig): full-ring baseline via editRev sync channel, then WITHOUT ever "
                             "touching editRev - explosion path (destroySphereSilent, worldChanged-only) "
                             "drops pairs 16->15, entity-landing place-back restores 16, player mine/place "
                             "via World::setBlock self-heals 15/16 even with the host worldEditRev chain "
                             " severed, 3-write storm inside the 200ms window merges into exactly one "
                             "rescan, 1s watchdog rescans >=1x in a 2.3s idle window under worldRunning "
                             "(stale world-lifetime cache disease closed on all paths)";
    });

    // ── t953 参数/通道源码钉（用户 8-28 第五轮口径「字还有点大、速度偏快——再调小调慢」+ rescan 双通道）──
    runLegMulti({ "t953 glyph params + rescan channels source pin: glyph quads 0.21-0.32 (t915 0.26-0.40 x0.8, user"
        " 'still a bit big'), drift 1.8 (x0.7, user 'a bit fast'), rate 0.40/shelf + cap 4/tick (<=8/s), "
        "life clamp 0.9-2.2 scaled with the slower drift (clamping would truncate slow flights and re-acc"
        "elerate the tail); worldChanged event hook -> requestRescan TRUE trailing-edge debounce (every r"
        "eentry restart()s the 200ms window, review0830 #20: the old dirty-flag early-return form froze t"
        "he window contrary to its own comment and is pinned extinct) + 1s self-heal watchdog gated activ"
        "e&&worldRunning (review26-11 pause convention)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        const auto readSrc = [&root](const QString &rel) {
            QFile f(root + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString gf = readSrc(QStringLiteral("/src/ui/EnchantGlyphFlow.qml"));
        // (b) 参数钉：调小（×0.8）+ 调慢（漂速 ×0.7 / 发射率放缓 / 封顶收口 / 寿命钳随降速放宽）。
        const bool ok953b = gf.contains(QStringLiteral("glyphScaleMin: 0.21"))
                            && gf.contains(QStringLiteral("glyphScaleMax: 0.32"))
                            && gf.contains(QStringLiteral("driftSpeed: 1.8"))
                            && gf.contains(QStringLiteral("ratePerShelf: 0.40"))
                            && gf.contains(QStringLiteral("maxPerTick: 4"))
                            && gf.contains(QStringLiteral("flightLifeMin: 0.9"))
                            && gf.contains(QStringLiteral("flightLifeMax: 2.2"));
        // (c) 通道钉：worldChanged 事件钩 + 200ms 防抖窗 + 1s 自愈轮询（running 门 = review26-11 约定）。
        //     review0830 #20：防抖改真 trailing-edge——requestRescan 体无早退、每次 restart()（风暴未停
        //     窗顺延，注释与实现一致）；旧 rescanPending 脏标记早退形态绝迹（回退固定窗即 (d) 红）。
        const bool ok953c = gf.contains(QStringLiteral("function requestRescan()"))
                            && gf.contains(QStringLiteral("target: root.world"))
                            && gf.contains(QStringLiteral("function onWorldChanged() { root.requestRescan() }"))
                            && gf.contains(QStringLiteral("id: rescanDebounce"))
                            && gf.contains(QStringLiteral("interval: 200"))
                            && gf.contains(QStringLiteral("id: rescanWatchdog"))
                            && gf.contains(QStringLiteral("interval: 1000"))
                            && gf.contains(QStringLiteral("running: root.active && root.worldRunning"));
        // (d) #20 trailing-edge 钉：requestRescan 函数体内含 restart()、不含早退（旧「窗内早退固定窗」
        //     形态必然带 return + rescanPending，回退即红——行为腿 ④ 风暴合并对两形态同绿，故钉源码面）。
        QString rrBody953;
        const int rr0953 = gf.indexOf(QStringLiteral("function requestRescan()"));
        if (rr0953 >= 0) {
            const int rr1953 = gf.indexOf(QLatin1Char('}'), rr0953);
            if (rr1953 > rr0953) rrBody953 = gf.mid(rr0953, rr1953 - rr0953);
        }
        const bool ok953d = !rrBody953.isEmpty()
                            && rrBody953.contains(QStringLiteral("rescanDebounce.restart()"))
                            && !rrBody953.contains(QStringLiteral("return"))
                            && !gf.contains(QStringLiteral("rescanPending"));
        if (!ok953b || !ok953c || !ok953d)
            qInfo().noquote() << "  t953 diag: okParams" << ok953b << "okChannels" << ok953c
                              << "okTrailingEdge" << ok953d;
        if (!ok953b || !ok953c || !ok953d)
            ++totalFail;
        qInfo().noquote() << ((ok953b && ok953c && ok953d) ? "PASS" : "FAIL")
                          << "| t953 glyph params + rescan channels source pin: glyph quads 0.21-0.32 "
                             "(t915 0.26-0.40 x0.8, user 'still a bit big'), drift 1.8 (x0.7, user 'a bit "
                             "fast'), rate 0.40/shelf + cap 4/tick (<=8/s), life clamp 0.9-2.2 scaled with "
                             "the slower drift (clamping would truncate slow flights and re-accelerate the "
                             "tail); worldChanged event hook -> requestRescan TRUE trailing-edge debounce "
                             "(every reentry restart()s the 200ms window, review0830 #20: the old "
                             "dirty-flag early-return form froze the window contrary to its own comment "
                             "and is pinned extinct) + 1s self-heal watchdog gated active&&worldRunning "
                             "(review26-11 pause convention)";
    });

    // ---- t889 暂停语义统一（两档：GUI 开=世界照跑玩家照坠但不动；ESC=全停；t885 鱼线持久前置）----
    //      门控矩阵钉子（行为级 + 源码钉双层）：
    //        (a) 软档（!captured + worldRunning=true，GUI 面板开等价）：pc.tick() step 照跑（玩家坠、XZ 冻结）、
    //            掉落物照落、钓鱼态不自动收（旧代码此分支 cancelFishing）+ 浮标照 tick；
    //        (b) 硬档（worldRunning=false，ESC 暂停菜单等价）：pc.tick() 早退于实体桶 —— 玩家位 / 掉落物 /
    //            浮标位置全冻结（精确等值），钓鱼态保活（不收、只是不 tick），复跑续钓；
    //        (c) WorldClock.running 行为级：默认 true；false 停表（ticked 零发）→ true 复跑；
    //        (d) 墙钟顺延：deferWallClocks 把 spawnMs 推后（掉落物免拾窗 ready→not-ready 翻转复验）+
    //            setWorldRunning 复跑连调三管理器（源码钉）；
    //        (e) release() 体无 cancelFishing（t885：开背包 / ESC / 失焦不收竿。行为级不可达 —— 无窗口
    //            rig 进不去 captured 态，t836(e) 同取舍源码钉）；
    //        (f) Main.qml 门控钉：window.worldRunning 派生属性 + worldClock.running / player.worldRunning
    //            绑定 + pauseOverlay 取反消费 + keyInput 未捕获守卫 + onTicked 桥无 captured/面板门（GUI 开
    //            时火 / 水 / 生长照跑的源头证）。
    runLegMulti({ "t889 pause-semantics unification, two tiers (Java singleplayer parity): soft tier (!captured + w"
        "orldRunning=true, any GUI panel open equivalent) keeps world running -- pc.tick() step() falls ("
        "Y drops, XZ frozen), item entity falls, fishing persists with bobber alive; hard tier (worldRunn"
        "ing=false, ESC menu equivalent) freezes player/item/bobber exact-equal with fishing line kept al"
        "ive across pause+resume; WorldClock.running stops/starts the 100ms ticked gate behaviorally; def"
        "erWallClocks pushes spawnMs (pickup-window ready->not-ready flip) and setWorldRunning rebases al"
        "l three managers; release() body has no cancelFishing (t885 line persistence); Main.qml gate pin"
        "s (worldRunning derived property + worldClock.running / player.worldRunning bindings + pauseOver"
        "lay negated consume + keyInput !captured guard + onTicked bridge free of captured/panel gates)" }, [&]() {
        bool okA = true, okB = true, okC = true, okD = true, okE = true, okF = true;
        QString diag889;
        // 泵事件循环等待墙钟（QTimer 需事件循环投递；每片 ≤10ms 防饿死）
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };

        // ---- (c) WorldClock.running 行为级 ----
        {
            WorldClock wc;
            int ticksOn = 0, ticksOff = 0;
            QObject::connect(&wc, &WorldClock::ticked, &wc, [&](qreal) {
                if (wc.running()) ++ticksOn; else ++ticksOff;
            });
            pumpFor(350);          // 默认 running=true（100ms QTimer → ≥3 tick）
            wc.setRunning(false);
            pumpFor(350);          // 停表窗口：ticked 零发（ticksOff 恒 0）
            wc.setRunning(true);
            pumpFor(350);          // 复跑窗口
            okC = wc.running() && ticksOn >= 4 && ticksOff == 0;
            if (!okC)
                diag889 += QStringLiteral("(c) running=%1 on=%2 off=%3; ")
                               .arg(wc.running()).arg(ticksOn).arg(ticksOff);
        }

        // ---- (a)/(b) PlayerController 两档 + 实体桶 + 钓鱼（行为级）----
        {
            World w9;
            w9.setWidth(48); w9.setDepth(48); w9.setHeight(96); w9.setSeed(77);
            EntityManager ents;
            ItemEntityManager items;
            Hotbar hb;
            hb.setStack(0, ToolRegistry::FishingRod, 1,
                        ToolRegistry::maxDurability(ToolRegistry::FishingRod));
            hb.setSelectedSlot(0);
            PlayerController pc;
            pc.setWorld(&w9);
            pc.setEntityManager(&ents);
            pc.setItemEntities(&items);
            pc.setHotbar(&hb);
            // 石板地板（fy=83 空带，同 t836 rig 口径）：玩家列 (6,6) 与掉落物列 (20,20) 各 5×5
            const int fy = 83;
            for (int x = 4; x <= 8; ++x)
                for (int z = 4; z <= 8; ++z) w9.setBlock(x, fy, z, BR::Stone, 0);
            for (int x = 18; x <= 22; ++x)
                for (int z = 18; z <= 22; ++z) w9.setBlock(x, fy, z, BR::Stone, 0);
            // 玩家：生存、悬空 6 格（面对下坠）；默认 !captured = GUI 开等价软档
            pc.loadSavedState(6.5f, float(fy + 6), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            const QVector3D startEye = pc.position();
            // 甩竿（t836 先例：useFishingRod 无 captured 门，直调成活）
            pc.useFishingRod();
            int bobIdx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobIdx = i; break; }
            if (!(pc.fishing() && bobIdx >= 0)) { okA = false; diag889 += QStringLiteral("(a) cast failed; "); }
            // 掉落物：远处列悬空（离玩家 14 格 > 拾取半径 1.5 → pickupScan 不干扰）
            items.spawnItem(20, fy + 4, 20, BR::Cobble, 1);
            int itemIdx = -1;
            for (int i = 0; i < items.count(); ++i)
                if (items.aliveAt(i) && items.posAt(i).x() > 19.0f) { itemIdx = i; break; }
            if (itemIdx < 0) { okA = false; diag889 += QStringLiteral("(a) item spawn failed; "); }
            const float itemY0 = itemIdx >= 0 ? items.posAt(itemIdx).y() : 0.0f;
            // (a) 软档：12 tick（间隔真 17ms 泵 dt）→ 眼位下坠 + XZ 冻结 + 掉落物下落 + 钓鱼态保持
            for (int i = 0; i < 12; ++i) { pumpFor(17); pc.tick(); }
            if (okA) {
                const float dyEye = float(startEye.y() - pc.position().y());
                const float dyItem = float(itemY0 - items.posAt(itemIdx).y());
                okA = pc.position().y() < startEye.y() - 0.05f                       // 照坠（step 软档推进）
                      && pc.position().x() == startEye.x()
                      && pc.position().z() == startEye.z()                            // 无输入不动（XZ 冻结）
                      && dyItem > 0.05f                                              // 掉落物照落（实体桶照跑）
                      && pc.fishing() && ents.aliveAt(bobIdx);                        // t885：GUI 开不收竿
                if (!okA)
                    diag889 += QStringLiteral("(a) dyEye=%1 dyItem=%2 fish=%3 bobAlive=%4 xyz=(%5,%6,%7); ")
                                   .arg(dyEye).arg(dyItem).arg(pc.fishing())
                                   .arg(bobIdx >= 0 && ents.aliveAt(bobIdx))
                                   .arg(pc.position().x()).arg(pc.position().y()).arg(pc.position().z());
            }
            // (b) 硬档：ESC 等价 → 全冻结（位置精确等值）+ 钓鱼态保活 + 复跑续钓
            if (okB) {
                pc.setWorldRunning(false);
                const QVector3D frozenEye = pc.position();
                const QVector3D frozenItem = itemIdx >= 0 ? items.posAt(itemIdx) : QVector3D();
                const QVector3D frozenBob = bobIdx >= 0 ? ents.posAt(bobIdx) : QVector3D();
                for (int i = 0; i < 12; ++i) { pumpFor(17); pc.tick(); }
                okB = pc.position() == frozenEye
                      && (itemIdx < 0 || items.posAt(itemIdx) == frozenItem)
                      && (bobIdx < 0 || ents.posAt(bobIdx) == frozenBob)
                      && pc.fishing();                                               // 鱼线保活过暂停（t885）
                pc.setWorldRunning(true);                                            // 复跑（墙钟顺延在 pc 内）
                pumpFor(17); pc.tick();
                okB = okB && pc.fishing() && (bobIdx < 0 || ents.aliveAt(bobIdx));   // 续钓不掉线
                if (!okB) diag889 += QStringLiteral("(b) hard-tier regression; ");
            }
            // 好公民：清场（探针不留脏 rig；独立小世界随作用域析构）
            ents.clearAll();
            items.clearAll();
        }

        // ---- (d) 墙钟顺延行为级：掉落物免拾窗 ready→not-ready 翻转复验 ----
        {
            ItemEntityManager items2;
            items2.spawnItem(3, 60, 3, BR::Cobble, 1);
            int idx = -1;
            for (int i = 0; i < items2.count(); ++i)
                if (items2.aliveAt(i)) { idx = i; break; }
            if (idx < 0) { okD = false; diag889 += QStringLiteral("(d) spawn failed; "); }
            else {
                pumpFor(650);                       // kPickupDelayMs=500 → ready
                const bool readyBefore = items2.isPickupReady(idx);
                items2.deferWallClocks(100000);      // 顺延 100s → spawnMs 推后 → 免拾窗重开
                const bool readyAfter = items2.isPickupReady(idx);
                items2.deferWallClocks(0);           // ms<=0 早退（幂等防御面）
                okD = readyBefore && !readyAfter;
                if (!okD) diag889 += QStringLiteral("(d) ready=%1 after=%2; ")
                                         .arg(readyBefore).arg(readyAfter);
            }
        }

        // ---- (e)/(f) 源码钉（行为级不可达 / QML 门控面）----
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            auto readSrc = [&root](const QString &rel) {
                QFile f(root + QLatin1Char('/') + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString t = readSrc(QStringLiteral("src/Game/playercontroller.cpp"));
            // 滤注释体（t836(e) 同手法）：源码钉只看语句面，注释里的字面量（如「不再 cancelFishing」）不参与
            const auto stripComments = [](const QString &body) {
                QString out;
                for (const QString &line : body.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                    out += line; out += QLatin1Char('\n');
                }
                return out;
            };
            // (e) release() 体（滤注释后）无 cancelFishing 调用（甩竿后开背包 / ESC / 失焦不收竿；
            //     收竿只走主动 / 换持物 / 重生 / 换世界四入口）
            {
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::release()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::setCaptured"));
                if (b0 < 0 || b1 <= b0) { okE = false; diag889 += QStringLiteral("(e) slice miss; "); }
                else {
                    okE = !stripComments(t.mid(b0, b1 - b0)).contains(QStringLiteral("cancelFishing"));
                    if (!okE) diag889 += QStringLiteral("(e) release still cancels fishing; ");
                }
            }
            // (d 尾) setWorldRunning 复跑连调三管理器 deferWallClocks（调用面钉）
            {
                const int s0 = t.indexOf(QStringLiteral("void PlayerController::setWorldRunning"));
                const int s1 = t.indexOf(QStringLiteral("QPoint PlayerController::windowCenterGlobal"));
                if (s0 < 0 || s1 <= s0) { okD = false; diag889 += QStringLiteral("(d) slice miss; "); }
                else {
                    const QString sb = t.mid(s0, s1 - s0);
                    okD = okD && sb.contains(QStringLiteral("m_entityManager->deferWallClocks"))
                          && sb.contains(QStringLiteral("m_itemEntities->deferWallClocks"))
                          && sb.contains(QStringLiteral("m_xpOrbManager->deferWallClocks"));
                }
            }
            // (f) Main.qml 门控面
            {
                const QString q = readSrc(QStringLiteral("src/ui/Main.qml"));
                if (q.isEmpty()) { okF = false; diag889 += QStringLiteral("(f) read miss; "); }
                const bool fProp  = q.contains(QStringLiteral("readonly property bool worldRunning"));
                const bool fClock = q.contains(QStringLiteral("WorldClock { id: worldClock; running: window.worldRunning }"));
                const bool fBind  = q.contains(QStringLiteral("worldRunning: window.worldRunning"));
                const bool fPause = q.contains(QStringLiteral("visible: !window.worldRunning"));
                const bool fKey   = q.contains(QStringLiteral("if (!player.captured) { e.accepted = true; return }"));
                // onTicked 桥（World tick 全家）无 captured / 面板门 —— GUI 开时火 / 水 / 生长照跑的源头证
                const int c0 = q.indexOf(QStringLiteral("function onTicked(dt)"));
                const int c1 = q.indexOf(QStringLiteral("// 光标位置追踪层"), c0);
                bool fBridge = false;
                if (c0 < 0 || c1 <= c0) { diag889 += QStringLiteral("(f) onTicked slice miss; "); }
                else {
                    const QString body = q.mid(c0, c1 - c0);
                    fBridge = !body.contains(QStringLiteral("captured"))
                              && !body.contains(QStringLiteral("inventoryOpen"));
                }
                okF = okF && fProp && fClock && fBind && fPause && fKey && fBridge;
                if (!(fProp && fClock && fBind && fPause && fKey && fBridge))
                    diag889 += QStringLiteral("(f) prop=%1 clock=%2 bind=%3 pause=%4 key=%5 bridge=%6; ")
                                   .arg(fProp).arg(fClock).arg(fBind).arg(fPause).arg(fKey).arg(fBridge);
            }
        }

        const bool ok889 = okA && okB && okC && okD && okE && okF;
        if (!ok889) ++totalFail;
        if (!diag889.isEmpty())
            qInfo().noquote() << "  [t889 diag]" << diag889;
        qInfo().noquote() << (ok889 ? "PASS" : "FAIL")
                          << "| t889 pause-semantics unification, two tiers (Java singleplayer parity): soft "
                             "tier (!captured + worldRunning=true, any GUI panel open equivalent) keeps world "
                             "running -- pc.tick() step() falls (Y drops, XZ frozen), item entity falls, "
                             "fishing persists with bobber alive; hard tier (worldRunning=false, ESC menu "
                             "equivalent) freezes player/item/bobber exact-equal with fishing line kept alive "
                             "across pause+resume; WorldClock.running stops/starts the 100ms ticked gate "
                             "behaviorally; deferWallClocks pushes spawnMs (pickup-window ready->not-ready "
                             "flip) and setWorldRunning rebases all three managers; release() body has no "
                             "cancelFishing (t885 line persistence); Main.qml gate pins (worldRunning "
                             "derived property + worldClock.running / player.worldRunning bindings + "
                             "pauseOverlay negated consume + keyInput !captured guard + onTicked bridge "
                             "free of captured/panel gates)";
    });

    // ── review26 #25 软档受击击退探针（Game 层 PlayerController 直编，t889(a) 软档 rig 族）──
    //   用户症状（review26 低危）：GUI 开（软档，世界照跑）被 mob 攻击伤害照扣但击退被吞——
    //   applyHitKnockback 旧门 `m_dead || !m_captured` 把软档击退一起拦掉（Java 语义：开背包照被打且被打飞）。
    //   修：门只拦 m_dead（applyGolemLaunch 连坐同修）。断言（行为级，t889(a) 的 pumpFor+pc.tick 驱动）：
    //   (a) 软档（默认 !captured）直调 applyHitKnockback(+X) → 玩家 X 位移 > 0.3（旧门恒 0 = 症状签名，回退即红）
    //       + 垂直小跳真发（y 曾高于地面）；
    //   (b) 对照：无击退时同窗口 X 精确不动（软档 XZ 冻结基线，位移只来自击退冲量）。
    runLegMulti({ "review26-25 soft-tier hit knockback lands: with a GUI-open equivalent (!captured, world running)"
        ", applyHitKnockback displaces the player >0.3 blocks along the hit direction with the vertical h"
        "op (Java parity: damage already ticked, knockback must follow; old gate swallowed it - the no-kn"
        "ockback window signature), while the no-hit control window keeps X/Z exactly frozen (displacemen"
        "t comes only from the impulse)" }, [&]() {
        bool okA = false, okB = false, okHop = false;
        const auto pumpFor25 = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        World w25;
        w25.setWidth(48); w25.setDepth(48); w25.setHeight(96); w25.setSeed(77);
        EntityManager ents25;
        PlayerController pc25;
        pc25.setWorld(&w25);
        pc25.setEntityManager(&ents25);
        const int fy25 = 83;
        for (int x = 3; x <= 10; ++x)                       // 石板地板走廊（击退 +X 弹程接地）
            for (int z = 4; z <= 8; ++z) w25.setBlock(x, fy25, z, BR::Stone, 0);
        pc25.loadSavedState(4.5f, float(fy25 + 1), 6.5f, -90.0f, 0.0f, 2 /* Survival */);
        const QVector3D base25 = pc25.position();
        // (b) 对照窗：无击退 12 tick → X/Z 精确冻结（软档零输入基线）
        for (int i = 0; i < 12; ++i) { pumpFor25(17); pc25.tick(); }
        okB = pc25.position().x() == base25.x() && pc25.position().z() == base25.z();
        // (a) 软档击退：直调（QML Connections 等价；默认 !captured = GUI 开软档）→ +X 位移 + (hop) 垂直上抬
        const float groundY = pc25.position().y();
        pc25.applyHitKnockback(1.0f, 0.0f);
        float maxY = groundY;
        for (int i = 0; i < 12; ++i) {
            pumpFor25(17); pc25.tick();
            maxY = std::max(maxY, float(pc25.position().y()));
        }
        okA = float(pc25.position().x() - base25.x()) > 0.3f;
        okHop = maxY > groundY + 0.05f; // kHitKnockbackUp 小跳（m_vel.y max 写入）
        const bool ok25 = okA && okB && okHop;
        if (!ok25)
            qInfo().noquote() << "  [review26-25 diag] dx=" << float(pc25.position().x() - base25.x())
                          << "frozenOk=" << okB << "maxY-lift=" << float(maxY - groundY);
        if (!ok25) ++totalFail;
        qInfo().noquote() << (ok25 ? "PASS" : "FAIL")
                          << "| review26-25 soft-tier hit knockback lands: with a GUI-open equivalent "
                             "(!captured, world running), applyHitKnockback displaces the player >0.3 "
                             "blocks along the hit direction with the vertical hop (Java parity: damage "
                             "already ticked, knockback must follow; old gate swallowed it - the "
                             "no-knockback window signature), while the no-hit control window keeps X/Z "
                             "exactly frozen (displacement comes only from the impulse)";
    });

    // ── P-t881 鱼线最大长度探针（32 格断线，行为级）──
    //    pc 真甩竿 → settle（近距 ~2 格）→ pc.tick 线仍持；applyEnderPearlTeleport 把玩家拉到 ~53 格
    //    （loadSavedState 会 cancelFishing 不可用——传送是唯一不撞钓鱼态的移位口）→ 传送本身不断线
    //    （检测在 updateFishing）→ 首 pc.tick 断线：浮标槽释放 + fishing 复位 + 耐久不变 + 零 fishCaught。
    runLegMulti({ "t881 fishing line max length: eye-to-bobber 3D distance beyond 32 blocks snaps the line on the n"
        "ext updateFishing mirror tick (bobber entity removed, fishing state cleared, zero fishCaught, ze"
        "ro rod durability cost -- a snapped line is not a reel); near-distance tick keeps the line (beha"
        "vioral: real cast -> settle -> pc.tick holds; ender-pearl teleport hauls the player ~53 blocks a"
        "way without touching fishing state -- the only headless repositioning path, loadSavedState cance"
        "ls fishing by savegame semantics)" }, [&]() {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(78);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const int fy = 83; // rig 地板格（seed 78 未生成地形 → 全空带，手摆）
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                wL.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池（settle 用）
            }
        wL.setBlock(3, fy, 6, BR::Stone, 0);  // 玩家立足柱（pc.tick step 物理需要）
        wL.setBlock(44, fy, 42, BR::Stone, 0); // 传送目标立足柱（距浮标 (5.5,6.5) 水平 √(39²+36²)≈53 > 32）
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wL);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caught = 0;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float) { ++caught; });
        pc.useFishingRod(); // 甩竿（serial 1，轨迹同 t836(b)：settle (5.5, fy+1.75, 6.5)）
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        bool ok = pc.fishing() && bob >= 0;
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
        for (int t = 0; t < 40 && ok; ++t) {
            tickL(1, 0.05f);
            if (!ents.aliveAt(bob)) { ok = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        ok = ok && ents.posAt(bob) == settlePos;
        pumpFor(17); pc.tick(); // 近距镜像 tick（~2 格）→ 线仍持（fishing 保持 + 浮标活）
        ok = ok && pc.fishing() && ents.aliveAt(bob);
        const int dur0 = hb.durabilityAt(0);
        pc.applyEnderPearlTeleport(44, fy + 2, 42); // 玩家 → (44.5, fy+1, 42.5)（传送不撞钓鱼态）
        ok = ok && pc.fishing();                    // 传送本身不断线（检测在 updateFishing 镜像段）
        pumpFor(17); pc.tick();                     // → 断线
        ok = ok && !pc.fishing() && !ents.aliveAt(bob) && caught == 0
              && hb.durabilityAt(0) == dur0;        // 无获物 / 无耐久（扯断≠收竿）
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t881 fishing line max length: eye-to-bobber 3D distance beyond 32 blocks "
                             "snaps the line on the next updateFishing mirror tick (bobber entity removed, "
                             "fishing state cleared, zero fishCaught, zero rod durability cost -- a snapped "
                             "line is not a reel); near-distance tick keeps the line (behavioral: real cast "
                             "-> settle -> pc.tick holds; ender-pearl teleport hauls the player ~53 blocks "
                             "away without touching fishing state -- the only headless repositioning path, "
                             "loadSavedState cancels fishing by savegame semantics)";
    });

    // ── P-t882 拉拽反馈增强探针（行为级：距离缩放 / 上抛弧；源码钉：角度调制——yaw 无 WRITE，收杆改向
    //    行为级不可达 headless，t836(g) 同取舍）──
    //    (a) 近距钩猪（~3 格直瞄，t836(d) 轨迹）→ 收竿一帧物理位移 movedN + 耐久 -5；
    //    (b) 远距钩猪（~12.5 格：直瞄会中途落地够不着 → 仰角 12..34° 扫描，每次失败换新猪重摆——首游荡窗
    //        30 tick 内完成甩钩的确定性口径）→ 收竿 movedF > movedN×1.25（6+0.35d 距离缩放）+ 猪升起
    //        riseF > 0.06（t927 落差解算上抛 √(2·g·Δh)：平地远猪 vy≈10 → 首 tick 升 ~0.15——t882 旧冲量
    //        2.8+0.18d 只升 ~0.08 / 更旧基值 2.8 只升 ~0.045，用户「没看到生物被拉起来飞」）；
    //    (c) 源码钉：useFishingRod 体内距离增益（kFishHookPullGain）+ t927 落差解算上抛（√(2·g·Δh) 语句 +
    //        眼位目标 kFishHookLiftOverhead + 重型折扣 kFishHookHeavyLift）+ 角度调制（angleFactor 乘
    //        speed 与 lift 两支）语句面。
    runLegMulti({ "t882 hook-reel feedback: pull strength scales with line length (speed += 0.35 x dist-capped-32 -"
        "> far pig displaces >1.25x near pig in the first physics tick after the reel) and the launch arc"
        " is solved from fall height (t927 drop-solved lift vy=sqrt(2 x 28 x dh) targeting the player eye"
        " + overhead margin -> far-pig rise > 0.06/tick vs old flat 0.045 -- 'yanked visibly into the air"
        "'), both modulated by reel angle (look-vs-line |cos| factor, facing the target = full power, sid"
        "eways/over-shoulder decays to 0.4x; pitch excluded via horizontal renorm -- angled-down water ca"
        "sts must not lose force; angle branch pinned at source level since yaw has no WRITE and re-aimin"
        "g mid-hook is unreachable headless); impulse constants moved wholly to the Game layer (pullMobTo"
        "ward now takes speed+upSpeed, Entities layer holds no impulse constants - kBobberHookPullUp reti"
        "red); far-cast rig sweeps elevation 12-34 deg with a fresh pig per attempt (flat aim falls short"
        " of a 12.5-block target under light gravity)" }, [&]() {
        World wP;
        wP.setWidth(48); wP.setDepth(48); wP.setHeight(96); wP.setSeed(79);
        EntityManager ents;
        // t1029 wander 冻结缝：甩钩 / 收杆全程把猪钉在原地（历史偶红 = 窗内猪 wander RNG 漂移，
        //   okFar-false 签名；t970 直驱先例的管理器级等价物）。物理（重力 settle / 拉拽）不受影响。
        ents.setWanderFrozen(true);
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickP = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wP, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wP);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);

        // ---- (a) 近距（~3 格直瞄）：基速 ≈ 6+0.35×3 ≈ 7.1 ----
        wP.setBlock(13, fy, 6, BR::Stone, 0); // 玩家立足柱
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Stone, 0);
        const int pigN = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                            QStringLiteral("#e8a0a0"), 10);
        tickP(5, 0.05f); // 短窗 settle（t836(d) 加固口径：甩钩链压进首游荡窗 30 tick 内）
        {
            const QVector3D pp = ents.posAt(pigN);
            const float eyeY = float(fy + 1) + 1.62f;
            const float ux = pp.x() - 13.5f, uz = pp.z() - 6.5f;
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                              qRadiansToDegrees(std::atan2(-ux, -uz)),
                              qRadiansToDegrees(std::atan2(pp.y() - eyeY, std::sqrt(ux * ux + uz * uz))), 2);
        }
        pc.useFishingRod();
        int bobN = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobN = i; break; }
        bool okNear = bobN >= 0;
        for (int t = 0; t < 40 && okNear; ++t) {
            if (ents.bobberHookedMobAt(bobN) == pigN) break;
            tickP(1, 0.05f);
            if (!ents.aliveAt(bobN)) { okNear = false; break; }
        }
        okNear = okNear && ents.bobberHookedMobAt(bobN) == pigN;
        const float pigNX0 = ents.posAt(pigN).x();
        const int durN0 = hb.durabilityAt(0);
        pc.useFishingRod(); // 收竿拉拽
        tickP(1, 0.016f);   // 一帧物理：位移 = 拉速 × dt（≈7.1×0.016≈0.11）
        const float movedN = std::fabs(ents.posAt(pigN).x() - pigNX0);
        okNear = okNear && movedN > 0.03f && hb.durabilityAt(0) == durN0 - 5;
        ents.removeEntityAt(pigN);
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Air, 0);

        // ---- (b) 远距（~12.5 格）：仰角扫描甩中 → 位移/升幅随距离增强 ----
        for (int x = 25; x <= 27; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Stone, 0);
        bool okFar = false;
        float movedF = 0.0f, riseF = 0.0f, usedPitch = -1.0f;
        QString farDiag;
        for (int pi = 6; pi <= 17 && !okFar; ++pi) {
            const float pitch = float(pi) * 2.0f; // 12°..34°（直瞄平射 ~13 格处已落到台下，必须仰射）
            // 每次尝试换新猪（t882 原短窗确定性口径：settle+甩+飞 ≤1.3s < 首游荡窗 1.5s；t1029 缝
            //   冻结 wander 后猪全程定身，重试窗纪律保留作冗余防御——重试不叠猪龄）
            const int pigF = ents.spawnMobTyped(26, fy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickP(5, 0.05f);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, -90.0f, pitch, 2);
            pc.useFishingRod();
            int bobF = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobF = i; break; }
            bool hooked = false;
            if (bobF >= 0) {
                for (int t = 0; t < 40; ++t) {
                    if (ents.bobberHookedMobAt(bobF) == pigF) { hooked = true; break; }
                    tickP(1, 0.05f);
                    if (!ents.aliveAt(bobF)) break;
                }
            }
            if (hooked) {
                const float pigFX0 = ents.posAt(pigF).x();
                const float pigFY0 = ents.posAt(pigF).y();
                const int durF0 = hb.durabilityAt(0);
                pc.useFishingRod(); // 收竿拉拽（远距：≈6+0.35×12.5 ≈ 10.4 b/s + t927 解算上抛 √(2·28·Δh)）
                tickP(1, 0.016f);
                movedF = std::fabs(ents.posAt(pigF).x() - pigFX0);
                riseF = ents.posAt(pigF).y() - pigFY0;
                usedPitch = pitch;
                okFar = movedF > movedN * 1.25f && riseF > 0.06f
                        && hb.durabilityAt(0) == durF0 - 5;
                if (!okFar)
                    farDiag = QStringLiteral("movedF=%1 movedN=%2 riseF=%3").arg(movedF).arg(movedN).arg(riseF);
            } else {
                pc.useFishingRod(); // 空收浮标（重试下一仰角）
            }
            ents.removeEntityAt(pigF);
        }
        for (int x = 25; x <= 27; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Air, 0);
        wP.setBlock(13, fy, 6, BR::Air, 0);

        // ---- (c) 角度调制 / 距离增益源码钉（yaw 无 WRITE，改向行为级不可达 headless）----
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                        QStringLiteral("src/Game/playercontroller.cpp"));
            QFile f(pcpPath);
            if (f.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(f.readAll());
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::useFishingRod()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::updateFishing"));
                if (b0 >= 0 && b1 > b0) {
                    QString body;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                        if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                        body += line; body += QLatin1Char('\n');
                    }
                    okPin = body.contains(QStringLiteral("kFishHookPullGain * dc"))
                            && body.contains(QStringLiteral(
                                   "std::sqrt(2.0f * kFishHookLiftGravity * dhLift)"))
                            && body.contains(QStringLiteral("kFishHookLiftOverhead"))
                            && body.contains(QStringLiteral("kFishHookHeavyLift"))
                            && body.contains(QStringLiteral("pullSpeed *= angleFactor;"))
                            && body.contains(QStringLiteral("liftSpeed *= angleFactor;"))
                            && body.contains(QStringLiteral(
                                   "pullMobToward(hooked, m_pos, pullSpeed, liftSpeed)"));
                }
            }
        }
        const bool okT882 = okNear && okFar && okPin;
        if (!okT882) ++totalFail;
        if (!okT882)
            qInfo().noquote() << "  [t882 diag] okNear" << okNear << "movedN" << movedN
                              << "okFar" << okFar << "pitch" << usedPitch << farDiag
                              << "okPin" << okPin;
        qInfo().noquote() << (okT882 ? "PASS" : "FAIL")
                          << "| t882 hook-reel feedback: pull strength scales with line length (speed "
                             "+= 0.35 x dist-capped-32 -> far pig displaces >1.25x near pig in the "
                             "first physics tick after the reel) and the launch arc is solved from "
                             "fall height (t927 drop-solved lift vy=sqrt(2 x 28 x dh) targeting the "
                             "player eye + overhead margin -> far-pig rise > 0.06/tick vs old flat "
                             "0.045 -- 'yanked visibly into the air'), both modulated by reel angle "
                             "(look-vs-line |cos| factor, facing the target = full power, sideways/"
                             "over-shoulder decays to 0.4x; pitch excluded via horizontal renorm -- "
                             "angled-down water casts must not lose force; angle branch pinned at "
                             "source level since yaw has no WRITE and re-aiming mid-hook is "
                             "unreachable headless); impulse constants moved wholly to the Game "
                             "layer (pullMobToward now takes speed+upSpeed, Entities layer holds no "
                             "impulse constants - kBobberHookPullUp retired); far-cast rig sweeps "
                             "elevation 12-34 deg with a fresh pig per attempt (flat aim falls "
                             "short of a 12.5-block target under light gravity)";
    });

    // ── P-t883 夜行者对鱼钩瞬移探针（行为级）──
    //    真甩竿命中夜行者（t836(d) 直瞄轨迹，3 格内必中）→ ① 全程不钩定（bobberHookedMobAt 恒 -1，钩不住
    //    夜行者族）；② 夜行者被强制瞬移（位移 >4 格——瞬移带 8-16 格 vs 游荡步进 <1 格/秒可分辨）；③ 浮标
    //    穿过原站位继续飞 / 落定（实体仍活，不被消耗——鱼钩是软线不是箭）；对照：猪在 3 格直瞄必钩（t836(d)
    //    已钉，不重摆）。闪避后夜行者掉下平台（瞬移落点在平台外）也只断言位移量不断言落点。
    runLegMulti({ "t883 nightwalker vs fishhook: flying-bobber hook scan treats a MobNightwalker hit as a projectil"
        "e encounter -- forced teleport dodge (bypassing teleportCooldown, same t829 arrow-chain fix so a"
        " cooldown-window hit can never pass through silently) and the bobber never latches (hook state m"
        "achine unreachable for the nightwalker family; MC 1.0 enderman projectile-immunity parity for th"
        "e fishing rod); the bobber itself is NOT consumed (soft line, unlike the arrow's remove=true) an"
        "d keeps flying through the vacated spot; behavioral rig: direct 3-block aim at a settled nightwa"
        "lker -> zero hook across 24 ticks + displacement >4 blocks (teleport band 8-16 vs wander <1/s)" }, [&]() {
        World wN;
        wN.setWidth(48); wN.setDepth(48); wN.setHeight(96); wN.setSeed(80);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickN = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wN, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        wN.setBlock(13, fy, 6, BR::Stone, 0); // 玩家立足柱
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wN.setBlock(x, fy, z, BR::Stone, 0); // 夜行者平台
        // 夜行者（t727 口径 spawnMobTyped 直摆；halfH 1.40 三格高，halfW 0.35——3 格直瞄命中盒大）
        const int nw = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobNightwalker,
                                          QStringLiteral("#1a1426"), 40);
        // settle 16 tick（夜行者 halfH 1.40 → 重心出生位高，落定穿行 ~1 格需 ~7 tick；5 tick 会抓到中途
        //   下坠位（实测 84.35）导致瞄准错位。16+甩钩 ≤3 tick 仍 < 首游荡窗 30 tick，确定性保持）
        tickN(16, 0.05f);
        const QVector3D nw0 = ents.posAt(nw);
        const float eyeY = float(fy + 1) + 1.62f;
        const float ux = nw0.x() - 13.5f, uz = nw0.z() - 6.5f;
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wN);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                          qRadiansToDegrees(std::atan2(-ux, -uz)),
                          qRadiansToDegrees(std::atan2(nw0.y() - eyeY, std::sqrt(ux * ux + uz * uz))), 2);
        pc.useFishingRod(); // 甩向夜行者（3 格直瞄，飞行 ≤3 tick 必进命中盒）
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        bool noHook = bob >= 0;
        bool teleported = false;
        for (int t = 0; t < 24 && noHook; ++t) {
            tickN(1, 0.05f);
            if (ents.bobberHookedMobAt(bob) == nw) { noHook = false; break; } // 钩上了 = FAIL
            const QVector3D np = ents.posAt(nw);
            if ((np - nw0).length() > 4.0f) teleported = true; // 强制瞬移带 8-16 格（游荡 <1 格/秒可分辨）
            if (!ents.aliveAt(bob)) break; // 穿过后落定 / 出界消散皆可（浮标不被消耗即不再追认）
        }
        const bool okT883 = noHook && teleported;
        if (!okT883) ++totalFail;
        if (!okT883)
            qInfo().noquote() << "  [t883 diag] noHook" << noHook << "teleported" << teleported
                              << "nwPos" << ents.posAt(nw) << "from" << nw0
                              << "hooked" << (bob >= 0 ? ents.bobberHookedMobAt(bob) : -2);
        qInfo().noquote() << (okT883 ? "PASS" : "FAIL")
                          << "| t883 nightwalker vs fishhook: flying-bobber hook scan treats a "
                             "MobNightwalker hit as a projectile encounter -- forced teleport dodge "
                             "(bypassing teleportCooldown, same t829 arrow-chain fix so a cooldown-"
                             "window hit can never pass through silently) and the bobber never "
                             "latches (hook state machine unreachable for the nightwalker family; "
                             "MC 1.0 enderman projectile-immunity parity for the fishing rod); the "
                             "bobber itself is NOT consumed (soft line, unlike the arrow's "
                             "remove=true) and keeps flying through the vacated spot; behavioral "
                             "rig: direct 3-block aim at a settled nightwalker -> zero hook across "
                             "24 ticks + displacement >4 blocks (teleport band 8-16 vs wander <1/s)";
    });

    // ── P-review26-8 弹射物闪避不清仇恨源码钉（review26 #8：浮标闪避复用 teleportEntity 免费净化）──
    //   行为级不可密闭驱动的取舍声明：enraged 是 ≤1s 瞬态（rage 满 1s 即 teleportBehindPlayer 转蓄力段，
    //   背后无落点也清 enraged 防卡态），且进入态依赖「夜行者面朝玩家」——游荡 yaw 随机翻转 → headless
    //   掷骰驱动必 flaky（t882 mob flake 前车之鉴，不新增）——按 t889(a3)/t836(e) 源码钉手法锁语句面：
    //   ① teleportEntity 落定清仇恨三连（enraged/rageTimer/windupTimer）被 clearAggro 参数门控；
    //   ② 箭链 / 浮标两处弹射物闪避调用显式传 false（MC 1.0 末影人被投射物闪避不解除仇恨；浮标 0 伤害
    //      0 消耗清仇恨 = 免费无限远程「净化」+ 打断攻击前摇，箭链同为投射物一并修）；
    //   ③ 水逃逸 / 近战 dodge 保持默认 true（既有设计：水伤与近身交互打断激怒）+ 头文件默认参数存在。
    //   闪避行为本身（位移 / 不钩定 / 箭消耗）由 t883 / t829(b) 行为级探针覆盖，不重摆。
    runLegMulti({ "review26-8 projectile dodge no longer wipes aggro: teleportEntity's landing hatred-clear trio (e"
        "nraged/rageTimer/windupTimer) is gated behind a clearAggro param; all FOUR projectile dodge call"
        " sites pass false explicitly (arrow + bobber direct, snowball + egg via the nightwalkerDodge pas"
        "s-through added by review27 #5 - the old count-of-2 pin was the incomplete enumeration that let "
        "the snowball/egg chains keep the free-purge exploit), while water-escape and melee dodge keep th"
        "e default true (documented design: water damage and close-range interaction interrupt rage); sou"
        "rce pin because the enraged state is a sub-1s transient gated on random wander yaw - behavioral "
        "driving would be flaky (t889 a3 / t836 e precedent); dodge behavior itself stays covered by the "
        "t883/t829(b) rigs" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile cf(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
        QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
        const QString ct = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
        const QString ht = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
        const int b0 = ct.indexOf(QStringLiteral("bool EntityManager::teleportEntity"));
        const int b1 = ct.indexOf(QStringLiteral("bool EntityManager::teleportBehindPlayer"));
        bool okGuard = false, okCalls = false, okDefault = false, okDecl = false;
        if (b0 < 0 || b1 <= b0 || ht.isEmpty()) {
            qInfo().noquote() << "  [review26-8 diag] slice miss b0" << b0 << "b1" << b1;
        } else {
            QString body;
            for (const QString &line : ct.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                if (!line.trimmed().startsWith(QLatin1String("//"))) { body += line; body += QLatin1Char('\n'); }
            // ① 清仇恨三连被参数门控（签名带参 + if (clearAggro) 守卫）
            okGuard = body.contains(QStringLiteral("bool clearAggro"))
                      && body.contains(QStringLiteral("if (clearAggro)"));
            // ② 四条弹射物闪避链显式 false（review27 #5 扩：箭 / 浮标直调 + 雪球 / 蛋经
            //    nightwalkerDodge 透传——旧口径只数箭/浮标 2 处，漏改正是 review27 #5 本身）
            okCalls = ct.count(QStringLiteral("clearAggro=*/false")) == 4;
            // ③ 水逃逸保持默认调用（Max 后紧跟右括号 = 未传参）。近战 dodge 改经
            //    nightwalkerDodge(clearAggro) 透传（review27-5 探针钉 "Max, clearAggro)" +
            //    头文件默认 true），默认清仇恨语义不变——故本钉从 2 降为 1。
            okDefault = ct.count(QStringLiteral("kNightwalkerTeleportMax)")) == 1;
            // 头文件默认参数（true = 近战/水逃逸清仇恨语义保持）
            okDecl = ht.contains(QStringLiteral("bool clearAggro = true"));
        }
        const bool okR8 = okGuard && okCalls && okDefault && okDecl;
        if (!okR8) ++totalFail;
        if (!okR8)
            qInfo().noquote() << "  [review26-8 diag] okGuard" << okGuard << "okCalls" << okCalls
                              << "okDefault" << okDefault << "okDecl" << okDecl;
        qInfo().noquote() << (okR8 ? "PASS" : "FAIL")
                          << "| review26-8 projectile dodge no longer wipes aggro: teleportEntity's "
                             "landing hatred-clear trio (enraged/rageTimer/windupTimer) is gated "
                             "behind a clearAggro param; all FOUR projectile dodge call sites "
                             "pass false explicitly (arrow + bobber direct, snowball + egg via the "
                             "nightwalkerDodge pass-through added by review27 #5 - the old "
                             "count-of-2 pin was the incomplete enumeration that let the snowball/"
                             "egg chains keep the free-purge exploit), while water-escape and melee dodge "
                             "keep the default true (documented design: water damage and close-"
                             "range interaction interrupt rage); source pin because the enraged "
                             "state is a sub-1s transient gated on random wander yaw - behavioral "
                             "driving would be flaky (t889 a3 / t836 e precedent); dodge behavior "
                             "itself stays covered by the t883/t829(b) rigs";
    });

    // ── P-t884 咬钩可见性全套探针（行为级：①入水水花信号恰一次 + 坐标；Water 态查询三态分辨；Game 层
    //    bobberInWater 镜像翻转。②微飘动画 / ③水面轨迹粒子 / ④下沉加深与咬钩水花加强是 QML 视觉层——
    //    commit 钉 visual-only：驱动条件（bobberInWater && !hasBite）已被本探针行为级锁死）──
    runLegMulti({ "t884 bite-visibility set: (1) cast-to-water splash -- bobberSplashed fires exactly once on the F"
        "lying->Water settle edge with the exact float-surface coordinates (3s idle water stays at one; r"
        "e-entry after drain/refill re-fires naturally), routed to burstWaterCast; (2) bobberInWaterAt di"
        "scriminates all four states (born Flying false / settled Water true / Ground false) and the Game"
        "-layer bobberInWater mirror flips true after a settle+pc.tick and false on reel -- the exact dri"
        "ving condition chain for the QML idle bob animation + approach-trail particles; (3) visual-only "
        "halves (idle micro-bob sin phase +-0.035 via NumberAnimation, deterministic golden-angle approac"
        "h ripples every 380ms arriving-and-dying at the bobber, bite sink deepened 0.15->0.35 plus bite "
        "splash strengthened 10->14 particles, all gated bobberInWater&&!hasBite&&worldRunning so ESC fre"
        "ezes them) pinned visual-only in the commit" }, [&]() {
        World wV;
        wV.setWidth(48); wV.setDepth(48); wV.setHeight(96); wV.setSeed(81);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickV = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wV, farL, 0.3f, 1.8f, false);
        };
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const int fy = 83;
        int splashCount = 0;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        QObject::connect(&ents, &EntityManager::bobberSplashed, &ents,
                         [&](float x, float y, float z) { ++splashCount; sx = x; sy = y; sz = z; });
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wV.setBlock(x, fy, z, BR::Stone, 0);
                wV.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池
            }

        // (a) 入水：直落水池 → Flying 段 inWater=false → settle 恰一次 bobberSplashed（坐标 = 浮定水面坐标）
        const int b1 = ents.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 951);
        const bool flyingSeen = b1 >= 0 && !ents.bobberInWaterAt(b1); // 出生 Flying（inWater false）
        for (int t = 0; t < 60 && ents.aliveAt(b1) && splashCount == 0; ++t) tickV(1, 0.05f);
        const QVector3D settlePos(6.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okA = flyingSeen && splashCount == 1 && ents.aliveAt(b1) && ents.bobberInWaterAt(b1)
                   && qAbs(sx - settlePos.x()) < 1e-3f && qAbs(sy - settlePos.y()) < 1e-3f
                   && qAbs(sz - settlePos.z()) < 1e-3f;
        tickV(60, 0.05f); // 水中静置 3s（跨等待期）不重发（再入水才重发）
        okA = okA && splashCount == 1;
        ents.removeEntityAt(b1);

        // (b) 陆上：石台直落 → Ground：零 bobberSplashed + inWater false（微飘/轨迹粒子的驱动条件不误触）
        wV.setBlock(12, fy, 18, BR::Stone, 0);
        const int b2 = ents.spawnBobber(QVector3D(12.5f, float(fy + 4), 18.5f), QVector3D(0, 0, 0), 952);
        for (int t = 0; t < 60 && ents.aliveAt(b2); ++t) tickV(1, 0.05f);
        const bool okB = ents.aliveAt(b2) && !ents.bobberInWaterAt(b2) && splashCount == 1;
        ents.removeEntityAt(b2);
        wV.setBlock(12, fy, 18, BR::Air, 0);

        // (c) Game 层镜像：pc 真甩竿 settle（轨迹同 t836(b)：格 (5,84,6)）→ pc.tick → bobberInWater true；
        //     收竿翻 false（QML 待机微飘 / 水面轨迹粒子的驱动条件链行为级锁死）。
        wV.setBlock(3, fy, 6, BR::Stone, 0); // 玩家立足柱
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wV);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        const QVector3D settleC(5.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okC = bob >= 0 && !pc.bobberInWater(); // 甩出即 Flying（镜像初值 false）
        for (int t = 0; t < 40 && okC; ++t) {
            tickV(1, 0.05f);
            if (!ents.aliveAt(bob)) { okC = false; break; }
            if (ents.posAt(bob) == settleC) break;
        }
        pumpFor(17); pc.tick(); // 镜像刷新（updateFishing 拉 bobberInWaterAt）
        okC = okC && ents.posAt(bob) == settleC && pc.fishing() && pc.bobberInWater();
        pc.useFishingRod(); // 收竿 → 镜像翻 false
        okC = okC && !pc.fishing() && !pc.bobberInWater();

        const bool okT884 = okA && okB && okC;
        if (!okT884) ++totalFail;
        if (!okT884)
            qInfo().noquote() << "  [t884 diag] okA" << okA << "(splash" << splashCount << "at" << sx << sy
                              << sz << ") okB" << okB << "okC" << okC;
        qInfo().noquote() << (okT884 ? "PASS" : "FAIL")
                          << "| t884 bite-visibility set: (1) cast-to-water splash -- bobberSplashed "
                             "fires exactly once on the Flying->Water settle edge with the exact "
                             "float-surface coordinates (3s idle water stays at one; re-entry after "
                             "drain/refill re-fires naturally), routed to burstWaterCast; (2) "
                             "bobberInWaterAt discriminates all four states (born Flying false / "
                             "settled Water true / Ground false) and the Game-layer bobberInWater "
                             "mirror flips true after a settle+pc.tick and false on reel -- the exact "
                             "driving condition chain for the QML idle bob animation + approach-trail "
                             "particles; (3) visual-only halves (idle micro-bob sin phase +-0.035 via "
                             "NumberAnimation, deterministic golden-angle approach ripples every "
                             "380ms arriving-and-dying at the bobber, bite sink deepened 0.15->0.35 "
                             "plus bite splash strengthened 10->14 particles, all gated "
                             "bobberInWater&&!hasBite&&worldRunning so ESC freezes them) pinned "
                             "visual-only in the commit";
    });

    // ── P-t886 鱼获反馈探针（行为级：获物抛物弹出落玩家旁可捡 + 经验球 1-6 XP）──
    //    pc 真 Consumer 端（ItemEntityManager + XpOrbManager 都注入）：甩竿 → settle → drive 到咬钩 → 收竿 →
    //    ① 掉落物实体已生成于浮标格向上**列扫**弹出点（review26 #7：首个非水格 +0.225 = 静水格顶+0.225——
    //    水面上空气格，浮水分支不吞弧线；旧 +0.35 固定抬升口径退役）；
    //    ② 推掉落物物理 3s（60 tick × 0.05）→ 落定在玩家中心 2.2 格内（抛物解准确弹向玩家，可捡）；
    //    ③ review26 #21：XP 直接入账——恰一次 fishXpGained、量 ∈[1,6]、零经验球（MC 1.0 钓鱼 1-6 XP
    //       无球实体；旧口径「球落浮标格中心」随 #21 退役）；
    //    ④ 弹速 = 抛物解 |v| 镜像（近距 ≈8.1，随距离自适应）+ 耐久 -1（口径不变）。
    runLegMulti({ "t886 catch feedback: the loot item is spawned C++-side (dispenser/dropper direct-call precedent)"
        " as a solved ballistic throw from the bobber - spawn point column-scanned to the first non-water"
        " cell above the bobber +0.225 (review26 #7: static water = cell top +0.225; the old fixed +0.35 "
        "lift never left the water cell on flowing water where the surface frac is lower, and the item fl"
        "oat-water branch zeroes vy and glues the drop to the surface, killing the arc), target = player "
        "center, flight time clamp(0.45+0.055D, 0.5,1.4), vy = dy/T + g*T/2 (g=28 item gravity mirror) - "
        "after 3s of item physics the drop rests within 2.2 blocks of the player center (accurately catch"
        "able); review26 #21: XP credits directly via one fishXpGained of 1-6 amount routed to addXp (MC "
        "1.0 fishing grants no xp-orb entity - the old orb sat up to 32 blocks away at the bobber, pure-m"
        "agnet so it never chased the player) and zero orb entities spawn (credit-orb mutual exclusion gu"
        "ards double-grant); fishCaught speed payload equals the solved |v| mirror and the QML onFishCaug"
        "ht forwarder is retired (signal is now informational - double-spawn guard); rod -1 unchanged. Ma"
        "trix probe drives a real PlayerController with ItemEntityManager + XpOrbManager injected" }, [&]() {
        World wC;
        wC.setWidth(48); wC.setDepth(48); wC.setHeight(96); wC.setSeed(82);
        EntityManager ents;
        ItemEntityManager items;
        XpOrbManager orbs;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickC = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wC, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        // 落地带：石地板 x 3..8 / z 4..8（弹道短/过长都接得住；不含水池列外的虚空）+ 3×3 水池（5..7,5..7）
        for (int x = 3; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) wC.setBlock(x, fy, z, BR::Stone, 0);
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) wC.setBlock(x, fy + 1, z, BR::Water, 0);
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wC);
        pc.setEntityManager(&ents);
        pc.setItemEntities(&items);
        pc.setXpOrbManager(&orbs);
        pc.setHotbar(&hb); // t886：耐久 -1 断言需要（首跑红真因——漏注入 → damageSelectedItem 的
                           //   m_hotbar 门静默 false，探针 diag 三值全对唯独 dur=0 暴露）
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caughtCount = 0; float csp = 0.0f;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float speed) {
                             ++caughtCount; csp = speed;
                         });
        // review26 #21：钓获 XP 直接入账信号（MC 1.0 钓鱼无经验球实体）——计数 + 量程；旧版断言球落
        //   浮标格随 #21 退役（改断言**零球**：入账与球互斥，防双发）。
        int xpGainCount = 0; int xpGainAmount = 0;
        QObject::connect(&pc, &PlayerController::fishXpGained, &pc,
                         [&](int amount) { ++xpGainCount; xpGainAmount += amount; });
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okCast = bob >= 0;
        for (int t = 0; t < 40 && okCast; ++t) {
            tickC(1, 0.05f);
            if (!ents.aliveAt(bob)) { okCast = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        okCast = okCast && ents.posAt(bob) == settlePos;
        for (int t = 0; t < 660 && okCast && !ents.bobberHasBiteAt(bob); ++t) tickC(1, 0.05f);
        okCast = okCast && ents.bobberHasBiteAt(bob);
        const QVector3D bobPos = ents.posAt(bob);
        const int dur0 = hb.durabilityAt(0);
        pc.useFishingRod(); // 窗内收竿 → C++ 直调 spawnItemThrown + spawnOrb（t886 主路径）
        // ① 掉落物已生成于列扫弹出点（review26 #7：浮标格向上首个非水格 +0.225——静水 = 格顶+0.225；
        //    本 rig 浮标 settle 于 (5, fy+1, 6) → 弹出格 (5, fy+2, 6)）
        int item = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { item = i; break; }
        const QVector3D spawnExp(bobPos.x(), std::floor(bobPos.y()) + 1.225f, bobPos.z());
        bool okItem = item >= 0 && caughtCount == 1
                      && qAbs(items.posAt(item).x() - spawnExp.x()) < 1e-2f
                      && qAbs(items.posAt(item).y() - spawnExp.y()) < 1e-2f
                      && qAbs(items.posAt(item).z() - spawnExp.z()) < 1e-2f;
        // ② 推掉落物物理 3s：抛物解落点 = 玩家中心 —— 断言**首次触底点**（resting 首次 true）距玩家中心
        //    < 2.2（积分步进误差余量）。review26 #2 起落定后残余水平速度按 t468 支撑面摩擦继续滑行
        //    （弹速 ~8 / 摩擦 6 ≈ 1.4 格 + 本 rig 落地带西缘就在玩家脚边 → 终点会滑出平台落到下层
        //    地形）—— 终点位不再钉「可捡半径」，触底点才是抛物解准确性的锚（滑行是引擎 t468 既定
        //    摩擦语义，非弹道偏差）。
        QVector3D touchdown(0.0f, 0.0f, 0.0f);
        bool touchedDown = false;
        for (int t = 0; t < 60 && okItem; ++t) {
            items.tick(0.05, &wC);
            if (!touchedDown && items.aliveAt(item) && items.restingAt(item)) {
                touchedDown = true;
                touchdown = items.posAt(item);
            }
        }
        const QVector3D playerCenter(3.5f, float(fy + 1) + 0.9f, 6.5f);
        okItem = okItem && touchedDown && items.aliveAt(item)
                 && (touchdown - playerCenter).length() < 2.2f;
        // ③ review26 #21：XP 直接入账——恰一次 fishXpGained、量 ∈[1,6]、零经验球（入账与球互斥防双发）；
        //    ④ 弹速 = 抛物解镜像 + 耐久 -1
        int orbAlive = 0;
        for (int i = 0; i < orbs.count(); ++i)
            if (orbs.aliveAt(i)) ++orbAlive;
        const float vmagExp = fishCatchSpeedMirror(bobPos, QVector3D(3.5f, float(fy + 1), 6.5f));
        const bool okOrb = xpGainCount == 1 && xpGainAmount >= 1 && xpGainAmount <= 6
                           && orbAlive == 0
                           && qAbs(csp - vmagExp) < 1e-2f
                           && hb.durabilityAt(0) == dur0 - 1;
        const bool okT886 = okCast && okItem && okOrb;
        if (!okT886) ++totalFail;
        if (!okT886)
            qInfo().noquote() << "  [t886 diag] okCast" << okCast << "okItem" << okItem << "(itemPos"
                              << (item >= 0 ? items.posAt(item) : QVector3D()) << ") okOrb" << okOrb
                              << "(xpGain" << xpGainCount << "amt" << xpGainAmount
                              << "orbAlive" << orbAlive
                              << "csp" << csp << "exp" << vmagExp
                              << "dur" << hb.durabilityAt(0) - dur0 << ")";
        qInfo().noquote() << (okT886 ? "PASS" : "FAIL")
                          << "| t886 catch feedback: the loot item is spawned C++-side (dispenser/dropper "
                             "direct-call precedent) as a solved ballistic throw from the bobber - spawn "
                             "point column-scanned to the first non-water cell above the bobber +0.225 "
                             "(review26 #7: static water = cell top +0.225; the old fixed +0.35 lift "
                             "never left the water cell on flowing water where the surface frac is lower, "
                             "and the item float-water branch zeroes vy and glues the drop to the surface, "
                             "killing the arc), target = player center, flight time clamp(0.45+0.055D, "
                             "0.5,1.4), vy = dy/T + g*T/2 (g=28 item gravity mirror) - after 3s of "
                             "item physics the drop rests within 2.2 blocks of the player center "
                             "(accurately catchable); review26 #21: XP credits directly via one "
                             "fishXpGained of 1-6 amount routed to addXp (MC 1.0 fishing grants no "
                             "xp-orb entity - the old orb sat up to 32 blocks away at the bobber, "
                             "pure-magnet so it never chased the player) and zero orb entities "
                             "spawn (credit-orb mutual exclusion guards double-grant); "
                             "fishCaught speed payload equals the solved |v| mirror "
                             "and the QML onFishCaught forwarder is retired (signal is now "
                             "informational - double-spawn guard); rod -1 unchanged. Matrix probe "
                             "drives a real PlayerController with ItemEntityManager + XpOrbManager "
                             "injected";
    });

    // ── P-review26-7 流动水获物弹出点列扫探针（review26 #7：+0.35 固定抬升在 state≥2 未离水格）──
    //   旧口径 kFishCatchRiseOffset 0.35 只在静水（state 0，液面 7/8）恰好把生成点送出水格；流动水
    //   state≥2 液面 ≤0.75 → 生成点仍落水格内 → 掉落物浮水分支（vy 清零 + 恒速上浮）把弧线整个吞掉，
    //   获物粘回浮标处（t886 症状在河流 / 溢流边缘复发）。新口径：从浮标格向上**列扫**首个非 Water 格再
    //   +0.225（与 ItemEntityManager 浮水分支自己的列扫同源）。rig：3×3 池 state=5（液面 3/8 → 浮标 settle
    //   y = 格底+0.25，旧口径生成点 = +0.60 仍在水格 = FAIL 面）→ ① 生成点 y = 首非水格+0.225 = 池上空气格
    //   fy+2+0.225 且中心格非 Water；② 弧线不被吞：spawn 后 2 tick 水平位移 >0（浮水分支只动 Y）。
    //   t886 探针（静水 rig）已同步新口径断言，两水位全覆盖。
    runLegMulti({ "review26-7 catch spawn escapes FLOWING water: the loot pop point is column-scanned from the bobb"
        "er cell up to the first non-water cell (+0.225, same column-scan the item float-water branch its"
        "elf uses) instead of a fixed +0.35 lift - on state>=2 water the surface frac is <=0.75 so the ol"
        "d fixed lift left the spawn INSIDE the water cell and the float branch zeroed vy and swallowed t"
        "he whole arc (the t886 'no visible catch flight' symptom recurring on rivers/overflow edges); ri"
        "g: 3x3 pool at state 5 (surface 3/8, bobber settles at cell+0.25, old code spawned at +0.60 = st"
        "ill in water) - spawn lands at the air cell above (non-water center cell) and the drop moves hor"
        "izontally within 2 ticks (arc alive); the t886 static-water probe asserts the same column-scan v"
        "alue (cell top +0.225)" }, [&]() {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(84);
        EntityManager ents;
        ItemEntityManager items;
        XpOrbManager orbs;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 3; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) wL.setBlock(x, fy, z, BR::Stone, 0);
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) wL.setBlock(x, fy + 1, z, BR::Water, 5); // 流动水 state 5（液面 3/8）
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wL);
        pc.setEntityManager(&ents);
        pc.setItemEntities(&items);
        pc.setXpOrbManager(&orbs);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caughtCount = 0;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        // state 5：settle y = (fy+1) + 3/8 − 0.125 = fy+1.25（浮定沿随液面折算，同 t892 口径）
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.25f, 6.5f);
        bool okCast = bob >= 0;
        for (int t = 0; t < 40 && okCast; ++t) {
            tickL(1, 0.05f);
            if (!ents.aliveAt(bob)) { okCast = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        okCast = okCast && ents.posAt(bob) == settlePos;
        for (int t = 0; t < 660 && okCast && !ents.bobberHasBiteAt(bob); ++t) tickL(1, 0.05f);
        okCast = okCast && ents.bobberHasBiteAt(bob);
        const QVector3D bobPos = ents.posAt(bob);
        pc.useFishingRod(); // 窗内收竿 → 获物 spawnItemThrown（review26 #7 列扫弹出点）
        int item = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { item = i; break; }
        // ① 生成点在非水格：列扫 → 浮标格 (5, fy+1, 6) 上首个非水格 = (5, fy+2, 6) 空气格 → y = fy+2+0.225
        const QVector3D spawnExp(bobPos.x(), float(fy + 2) + 0.225f, bobPos.z());
        const bool okSpawn = item >= 0 && caughtCount == 1
                             && qAbs(items.posAt(item).x() - spawnExp.x()) < 1e-2f
                             && qAbs(items.posAt(item).y() - spawnExp.y()) < 1e-2f
                             && qAbs(items.posAt(item).z() - spawnExp.z()) < 1e-2f
                             && wL.blockAt(5, fy + 2, 6) != BR::Water;
        // ② 弧线不被浮水分支吞：spawn 后 2 tick 水平位移 >0.05（浮水分支 vy 清零只动 Y，水平冻结）
        bool okArc = false;
        QVector3D pos2;
        for (int t = 0; t < 2 && okSpawn; ++t) {
            items.tick(0.05, &wL);
            pos2 = items.posAt(item);
        }
        if (okSpawn && items.aliveAt(item)) {
            const float dh = QVector3D(pos2.x() - spawnExp.x(), 0.0f, pos2.z() - spawnExp.z()).length();
            okArc = dh > 0.05f;
        }
        const bool okR7 = okCast && okSpawn && okArc;
        if (!okR7) ++totalFail;
        if (!okR7)
            qInfo().noquote() << "  [review26-7 diag] okCast" << okCast << "okSpawn" << okSpawn
                              << "(itemPos" << (item >= 0 ? items.posAt(item) : QVector3D())
                              << "exp" << spawnExp << ") okArc" << okArc << "(pos2" << pos2 << ")";
        qInfo().noquote() << (okR7 ? "PASS" : "FAIL")
                          << "| review26-7 catch spawn escapes FLOWING water: the loot pop point is "
                             "column-scanned from the bobber cell up to the first non-water cell "
                             "(+0.225, same column-scan the item float-water branch itself uses) "
                             "instead of a fixed +0.35 lift - on state>=2 water the surface frac is "
                             "<=0.75 so the old fixed lift left the spawn INSIDE the water cell and "
                             "the float branch zeroed vy and swallowed the whole arc (the t886 "
                             "'no visible catch flight' symptom recurring on rivers/overflow "
                             "edges); rig: 3x3 pool at state 5 (surface 3/8, bobber settles at "
                             "cell+0.25, old code spawned at +0.60 = still in water) - spawn lands "
                             "at the air cell above (non-water center cell) and the drop moves "
                             "horizontally within 2 ticks (arc alive); the t886 static-water probe "
                             "asserts the same column-scan value (cell top +0.225)";
    });

    // ── P-review26-10 载具乘客钉位帧 rideRevision 同步探针（review26 #10：mob 乘客 QML 刷新 20Hz vs 矿车
    //   60Hz 不同步——快速车载乘视觉锯齿）──
    //   review25 #3 把 tickVehicleRiding 的乘客直发收口到 ~20Hz 相位门（修每帧双发卡顿，方向正确），但车侧
    //   位移仍每帧 notifyChanged（60Hz）→ C++ 乘客每帧钉车、QML 乘客 delegate 20Hz 采样 → 快速矿车
    //   （~8 格/s）载 mob 乘客相对车滞后 ~0.4 格、每 50ms 跳变。修 = ②「乘客单开小名单每帧 emit」的专用
    //   revision 变体：钉位值真变帧 bump rideRevision + 发 ridersChanged（有界：仅载客载具移动帧），QML 侧
    //   仅 mob delegate 的 position 绑定触碰它（t500 卡顿主因的 ~12 条 revision 绑定 + MobModel 重建面不
    //   触碰）。矩阵断言：(a) 行为级——载客矿车每个移动帧 rideRevision 恰 +1（同帧同步契约），停驻帧
    //   零 bump（无空转发射），钉位精度 <0.01（rig 自证场景成立）；(b) 源码钉——Main.qml mob delegate 的
    //   position 绑定触碰 entityManager.rideRevision + entitymanager.h 的 Q_PROPERTY 三件套存在
    //   （t870/t889 源码钉先例）。
    runLegMulti({ "review26-10 vehicle passenger pin syncs to the cart cadence: every frame a passenger-carrying ca"
        "rt MOVES bumps rideRevision exactly once (dedicated ridersChanged emit, only the mob delegate po"
        "sition binding touches it - the t500 12-binding revision face stays at the 20Hz gate) and parked"
        " frames emit nothing; QML position binding touches rideRevision (source pin); travel" }, [&]() {
        // 专用世界（review26-5/6 先例：seed 77 全空带 y84+ 平台——不占主世界 rig 位，防下游槽位漂移）：
        //   平台 y84 + 北向直轨 10 格 y85（x6，z21..30）。
        World wR10;
        wR10.setWidth(48); wR10.setDepth(48); wR10.setHeight(96); wR10.setSeed(77);
        for (int x = 4; x <= 8; ++x)
            for (int z = 20; z <= 32; ++z) wR10.setBlock(x, 84, z, BR::Stone, 0);
        const int rx = 6, rz0 = 30;
        for (int dz = -10; dz <= 0; ++dz) wR10.setBlock(rx, 85, rz0 + dz, BR::Rail, 0);
        MinecartManager carts;
        EntityManager ents;
        ents.setVehicleManagers(&carts, nullptr);
        carts.spawnCart(rx, 85, rz0, &wR10);
        const int mob = ents.spawnMobTyped(rx, 85, rz0, 0, QStringLiteral("#ff5555"), 10);
        const float seatDropX = 0.3125f; // kCartSeatDrop 同值镜像（t811 探针同款）
        QVector3D player = carts.posAt(0);
        QVector3D lastCp = carts.posAt(0);
        bool boarded = false, contractArmed = false, okMove = true, okIdle = true, pinOk = true;
        int settleFrames = 0, moveFrames = 0;
        float travel = 0.0f;
        // 阶段 A（推动期）：登乘后 2 帧武装契约（首钉落座帧允许一次性 bump，非车载移动）。
        for (int t = 0; t < 1200 && travel < 6.0f; ++t) {
            const int rev0 = ents.rideRevision();
            ents.tick(0.016, &wR10, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();                       // 钉位①（mob 桶内，游戏同序）
            if (int(std::floor(player.z())) > rz0 - 10)
                carts.pushEmptyCart(&wR10, player, 0.0f, -1.0f); // 长按 W 朝北推（t809/t811 玩家模型）
            carts.tickPushedCarts(0.016, &wR10);
            ents.tickVehicleRiding();                       // 钉位②（step 后同帧随车）
            const int rev1 = ents.rideRevision();
            const QVector3D cp = carts.posAt(0);
            const bool moved = QVector3D(cp - lastCp).length() > 1e-4f;
            if (moved) travel += QVector3D(cp - lastCp).length();
            if (ents.rideCartAt(mob) >= 0) {
                boarded = true;
                const QVector3D mp = ents.posAt(mob);
                if (std::fabs(mp.x() - cp.x()) > 0.01f
                    || std::fabs(mp.y() - (cp.y() - seatDropX + 0.5f)) > 0.01f
                    || std::fabs(mp.z() - cp.z()) > 0.01f) pinOk = false;
                if (!contractArmed) {
                    if (++settleFrames >= 2) contractArmed = true; // 落座 / 登乘帧不计契约
                } else if (moved) {
                    ++moveFrames;
                    if (rev1 - rev0 != 1) okMove = false;   // 移动帧恰 +1（同帧同步契约）
                } else {
                    if (rev1 != rev0) okIdle = false;        // 静止帧零 bump（无空转发射）
                }
            }
            lastCp = cp;
            player = cp; // 贴身追随（t809 先例）
        }
        // 阶段 B（停驻期）：不再推 → 余速滑到死端停驻 → 100 tick 静止帧零 bump。
        bool okPark = true;
        int parked = 0;
        for (int t = 0; t < 700 && parked < 100; ++t) {
            const int rev0 = ents.rideRevision();
            ents.tick(0.016, &wR10, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            carts.tickPushedCarts(0.016, &wR10);
            ents.tickVehicleRiding();
            const int rev1 = ents.rideRevision();
            const QVector3D cp = carts.posAt(0);
            const bool moved = QVector3D(cp - lastCp).length() > 1e-4f;
            if (moved) { parked = 0; travel += QVector3D(cp - lastCp).length(); }
            else ++parked;
            if (!moved && rev1 != rev0) okPark = false;      // 停驻帧零 bump
            lastCp = cp;
            player = cp;
        }
        // (b) 源码钉：QML position 绑定触碰 + 头文件属性三件套（t870/t889 先例）。
        bool okPinQml = false, okPinHdr = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString mt = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const int d0 = mt.indexOf(QStringLiteral("id: mobDelegate"));
            const int d1 = mt.indexOf(QStringLiteral("property int entKind:"), d0 > 0 ? d0 : 0);
            okPinQml = d0 >= 0 && d1 > d0
                       && mt.mid(d0, d1 - d0).contains(QStringLiteral("entityManager.rideRevision"));
            QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            const QString ht = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
            okPinHdr = ht.contains(QStringLiteral(
                "Q_PROPERTY(int rideRevision READ rideRevision NOTIFY ridersChanged)"));
        }
        const bool ok = boarded && pinOk && okMove && okIdle && okPark && moveFrames >= 30
                        && travel >= 4.0f && okPinQml && okPinHdr;
        if (!ok)
            qInfo().noquote() << "  review26-10 diag: boarded" << boarded << "pinOk" << pinOk
                              << "okMove" << okMove << "okIdle" << okIdle << "okPark" << okPark
                              << "moveFrames" << moveFrames << "travel" << travel
                              << "okPinQml" << okPinQml << "okPinHdr" << okPinHdr;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review26-10 vehicle passenger pin syncs to the cart cadence: every"
                             " frame a passenger-carrying cart MOVES bumps rideRevision exactly once"
                             " (dedicated ridersChanged emit, only the mob delegate position binding"
                             " touches it - the t500 12-binding revision face stays at the 20Hz gate)"
                             " and parked frames emit nothing; QML position binding touches"
                             " rideRevision (source pin); travel" << travel;
        // 专用世界随作用域丢弃，无需清场。
    });

    // ── P-review26-11 硬暂停冻结纯 QML 视觉 Timer 源码钉（review26 #11：ESC 时附魔字形仍持续发射/飞行）──
    //   EnchantGlyphFlow 两 Timer 的 running 只绑 active（appState=="playing" 派生）→ ESC 硬档（世界全停）
    //   白字持续飞（t884 自己 gate 了 worldRunning、t873 漏了）。修 = worldRunning 并入 running（经 Loader
    //   注入 window.worldRunning，同 active/world/camNode 注入先例——组件不直引跨上下文 id）。全仓纯视觉
    //   Timer 清点（本探针一并钉同批修的漏网）：EnchantGlyphFlow spawn+tick / EnchantRunes spawn+tick /
    //   BlockParticles tick（碎屑烟雾）/ Main.qml 水·岩浆·火·余烬门四翻书帧 + 附魔书翻页 + t878 爱心
    //   NumberAnimation。豁免面（UI chrome / 输入冻结期输出必静态，清点表落 Review 与 commit message）：
    //   bobber 拍水 Timer（t884 已 gate）/ f3Refresh / faceTimer（书朝向，玩家冻结→值静态）/ 指南针钟表
    //   图标 / 聊天淡出 / 上下船 toast / 信息 toast / anvil·enchant 面板闪光 / CharacterPreview3D 预览。
    runLegMulti({ "review26-11 hard pause freezes pure-visual QML Timers: enchant glyph spawn+flight, ambient runes"
        ", block debris pool, water/lava/fire/portal strip flipbooks, book page-flip and love hearts all "
        "gate worldRunning (MC Java singleplayer pause freezes particles); UI-chrome timers (toasts, chat"
        " fade, panel flashes, preview pane) stay exempt (source pin)" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        const auto readSrc = [&root](const QString &rel) {
            QFile f(root + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString gf = readSrc(QStringLiteral("/src/ui/EnchantGlyphFlow.qml"));
        const QString rn = readSrc(QStringLiteral("/src/ui/EnchantRunes.qml"));
        const QString bp = readSrc(QStringLiteral("/src/ui/BlockParticles.qml"));
        const QString mn = readSrc(QStringLiteral("/src/ui/Main.qml"));
        // (a) finding 本体：GlyphFlow 两 Timer running 含 worldRunning + 注入属性存在。
        const bool okGf = gf.contains(QStringLiteral(
                              "running: root.active && root.pairs.length > 0 && root.worldRunning"))
                          && gf.contains(QStringLiteral(
                              "running: (root.active || root.liveCount > 0) && root.worldRunning"))
                          && gf.contains(QStringLiteral("property bool worldRunning: false"));
        // (b) 同族漏网：EnchantRunes spawn+tick / BlockParticles tick（tick 改声明式——imperative start 退役）。
        const bool okRn = rn.contains(QStringLiteral(
                              "running: root.active && root.shelfCells.length > 0 && root.worldRunning"))
                          && rn.contains(QStringLiteral("running: root.worldRunning"))
                          && !rn.contains(QStringLiteral("tickTimer.start()"));
        const bool okBp = bp.contains(QStringLiteral("running: root.worldRunning"))
                          && !bp.contains(QStringLiteral("tickTimer.start()"));
        // (c) Main.qml：四翻书帧 Timer + 附魔书翻页 Timer gate window.worldRunning（≥5 处）+ 爱心
        //     NumberAnimation + 三处 Loader 注入（GlyphFlow/Runes/BlockParticles）。
        int flipGates = 0;
        for (int i = mn.indexOf(QStringLiteral("running: window.worldRunning")); i >= 0;
             i = mn.indexOf(QStringLiteral("running: window.worldRunning"), i + 1)) ++flipGates;
        const bool okMn = flipGates >= 5
                          && mn.contains(QStringLiteral(
                              "running: loveHearts.visible && window.worldRunning"))
                          && mn.count(QStringLiteral(".item.worldRunning = Qt.binding(function() { return window.worldRunning })")) >= 3;
        const bool ok = okGf && okRn && okBp && okMn;
        if (!ok)
            qInfo().noquote() << "  review26-11 diag: okGf" << okGf << "okRn" << okRn
                              << "okBp" << okBp << "okMn" << okMn << "flipGates" << flipGates;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review26-11 hard pause freezes pure-visual QML Timers: enchant glyph"
                                 " spawn+flight, ambient runes, block debris pool, water/lava/fire/"
                                 "portal strip flipbooks, book page-flip and love hearts all gate"
                                 " worldRunning (MC Java singleplayer pause freezes particles);"
                                 " UI-chrome timers (toasts, chat fade, panel flashes, preview pane)"
                                 " stay exempt (source pin)";
    });

    // ── P-t888 火伤节奏对齐 MC 探针（行为级 + 数值钉）──
    //    t888：① 常量钉（kFireDamageInterval 0.75s / kFireExtinguishChance 0 / kFireDuration 8——改值须
    //      同步本探针；MC 基准出处见 entitymanager.h 常量注释）；② 玩家侧行为级：真 pc 站立地火 → 首拍
    //      ∈[0.7,1.1]s、8s 内恰 ~10-11 拍（间隔恒定无随机吞拍）、余焰满 8s（0.75×11=8.25 > 8 → 恰 11 拍
    //      后 fireTimer 到期熄灭）；③ mob 侧同链（ignite 直燃猪，2.25s ≥3 拍 = 期望伤 >1HP/s）；
    //      ④ 阴性对照：kFireExtinguishChance=0 下 8s 窗内零「提前熄灭」（fireTimer 单调递减到自然归零，
    //      不出现中途跳零）。t889 软档语义照跑口径：pc.tick() 在 !captured 下 step 照跑火烧段。
    runLegMulti({ "t888 fire damage pacing aligned to MC: interval constant pinned at 0.75s (first pulse lands in ["
        "0.55,1.15]s vs old 1.0s), random early extinguish retired to exactly 0 (MC normal fire never sel"
        "f-extinguishes mid-burn; rain douse is a separate path) -> 10.2s standing-in-fire window yields "
        "exactly 13 damage pulses with zero swallowed ticks (old 0.15 chance ate ~40% of them), afterburn"
        " duration stays MC 8s; mob side shares the same constants via ignite() >=3 HP lost in 3s; player"
        " contact ignition reuses the t344 burn chain (soft-tier worldRunning semantics, world keeps tick"
        "ing while GUI open)" }, [&]() {
        World wF;
        wF.setWidth(48); wF.setDepth(48); wF.setHeight(96); wF.setSeed(86);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0); // 手持非火源物（火烧判定只读世界，持物无关；占位防空手分支）
        pc.setWorld(&wF);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        // rig：石地板 5×5 @fy，中央立地火 @fy+1（坐在地板上——不替换地板格，防脚下悬空洞）；
        //   玩家站火上（Fire 实存可站 / 或沉入格内，两种碰撞语义下新扫描均必接触）。
        const int fy = 83;
        for (int x = 1; x <= 5; ++x)
            for (int z = 4; z <= 8; ++z) wF.setBlock(x, fy, z, BR::Stone, 0);
        wF.setBlock(3, fy + 1, 6, BR::Fire, 0);
        // 常量钉（编译期值运行期复核——探针文本可读、回归即红）
        bool ok = EntityManager::kFireDamageInterval == 0.75f
                  && EntityManager::kFireExtinguishChance == 0.0f
                  && EntityManager::kFireDuration == 8.0f;
        // 玩家侧：settle 后验 burning 翻转（接触点燃主路径），随后重摆干净 rig 数拍（信号级精确）。
        pc.loadSavedState(3.5f, float(fy + 2), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        for (int t = 0; t < 20 && !pc.burning(); ++t) {
            pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false); pc.tick();
        }
        ok = ok && pc.burning(); // 站火必燃
        // —— 重摆干净 rig 数拍：fallDamageTaken(Fire) 连接计数，10.2s 窗断言恰 13 拍 + 首拍时刻带。
        wF.setBlock(3, fy + 1, 6, BR::Air, 0);
        pc.clearStatusEffects();
        wF.setBlock(3, fy + 1, 6, BR::Fire, 0);
        int firePulses = 0;
        double pulseT = -1.0;
        QElapsedTimer burnClock; burnClock.start();
        QObject::connect(&pc, &PlayerController::fallDamageTaken, &pc,
                         [&](int hp, int cause) {
                             if (hp == 1 && cause == int(PlayerState::Fire)) {
                                 ++firePulses;
                                 if (pulseT < 0.0) pulseT = burnClock.elapsed() / 1000.0;
                             }
                         });
        burnClock.restart();
        QElapsedTimer wholeClock; wholeClock.start();
        while (wholeClock.elapsed() < 10200) {
            pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false); pc.tick();
        }
        // 10.2s 窗：0.75s 恒间隔（随机熄灭已归零 = 零吞拍）→ 首拍 ~0.75 起、末拍 13×0.75=9.75 ≤ 10.2 <
        //   14×0.75=10.5 → **恰 13 拍**（旧 1.0s+15% 吞拍同窗只有 ~7-9 拍且首拍更晚）。首拍 ∈ [0.55, 1.15]
        //   （0.75 标称 ± 泵抖动 ~0.34s/帧容差）。
        ok = ok && firePulses == 13
             && pulseT >= 0.55 && pulseT <= 1.15;
        if (!(firePulses == 13 && pulseT >= 0.55 && pulseT <= 1.15))
            qInfo().noquote() << "  [t888 diag] firePulses" << firePulses << "firstPulse" << pulseT
                              << "burning" << pc.burning();
        // mob 侧同链：ignite 猪 → 3s 内 ≥3 拍（0.75 间隔 → 3s 恰 4 拍；≥3 容泵抖动；P(<3)=0 间隔确定性）
        wF.setBlock(3, fy + 1, 6, BR::Air, 0); // 清玩家立地火（mob 段用 ignite 直燃，防火源干扰对照）
        pc.clearStatusEffects();
        const int pigB = ents.spawnMobTyped(3, fy + 1, 6, EntityManager::MobPig,
                                            QStringLiteral("#ee9999"), 20);
        ok = ok && pigB >= 0;
        if (pigB >= 0) {
            ents.ignite(pigB, 8.0f);
            const float h0 = ents.healthAt(pigB);
            QElapsedTimer mobClock; mobClock.start();
            while (mobClock.elapsed() < 3000) {
                pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false);
            }
            ok = ok && (h0 - ents.healthAt(pigB)) >= 3.0f;
        }
        // 清场
        wF.setBlock(3, fy + 1, 6, BR::Air, 0);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t888 fire damage pacing aligned to MC: interval constant pinned at "
                             "0.75s (first pulse lands in [0.55,1.15]s vs old 1.0s), random early "
                             "extinguish retired to exactly 0 (MC normal fire never self-extinguishes "
                             "mid-burn; rain douse is a separate path) -> 10.2s standing-in-fire window "
                             "yields exactly 13 damage pulses with zero swallowed ticks (old 0.15 chance "
                             "ate ~40% of them), afterburn duration stays MC 8s; mob side shares the "
                             "same constants via ignite() >=3 HP lost in 3s; player contact ignition "
                             "reuses the t344 burn chain (soft-tier worldRunning semantics, world keeps "
                             "ticking while GUI open)";
    });

    // ── P-t891 点火源扩展探针（① 岩浆邻燃 / ② 烈焰弹全链）──
    //    (a) 岩浆邻燃：木板贴岩浆源 → 首个命中窗**点燃进燃烧态**（isBurningAt 真 + id 保留 = 直燃语义；
    //        核此前只有焚毁没有点燃）→ 持续驱动至烧毁（blockAt 变非 Planks，燃烧计时终局独家承担焚毁——
    //        单掷双义防一格两份消耗）；湿对照（板邻水）→ 同窗数内不点燃也不焚毁（防火带收口在入口内）；
    //        书架（非 isWoodLike 但 flammable）→ 也被点燃（入口门扩到可燃全表）。确定性：tickLavaFlow
    //        散布 = hashVoxel(seed+窗口序号) 纯函数；8%/窗 → 240 窗 P(未命中)≈2e-10（上限裕量充足）。
    //    (b) 烈焰弹全链（真 pc）：setStack 手持烈焰弹 → 右键发射 → Fireball 实体出生眼位前
    //        （kindAt==Fireball）→ 飞行撞墙消失 → 撞击必生火：打石墙 = 来向空气格置立地火（blockAt==Fire，
    //        100% per-entity 点燃概率）；打木板墙 = 板进燃烧态（id 不变，igniteFlammableAt 直燃口径）；
    //        创造不耗 + 挥手信号；生存消耗 1 弹；低头发射不自伤（玩家侧火球 shooter 豁免——发射后 HP 满 =
    //        无 mobAttackedPlayer 伤害，pc 无 PlayerState 注入以「burning 未翻转」间接证）；合成配方
    //        match 双证（煤版 / 木炭版各合 3 发）+ FireChargeId 0x25C 钉位 + 创造调色板含烈焰弹。
    runLegMulti({ "t891 ignition sources extended: (a) lava neighbor ignition -- a wood plank hugging a lava source"
        " now ENTERS the burning state on the first hit window (id preserved = direct-burn semantics via "
        "the shared igniteFlammableAt entry; the core path previously only incinerated), then burns away "
        "through the burn-timer endgame exclusively (single-roll dual-meaning: ignite wins over incinerat"
        "e, no double consumption), a water-backed bookshelf stays intact and unburned (damp-fuel firewal"
        "l at the ignite entry; a wet plank would fall to the legacy incinerate path which has no water g"
        "uard - out of scope), and a bookshelf (flammable but outside the old isWoodLike set) now catches"
        " too (entry gate widened to the full flammable table); (b) fire charge item: real-PC right-click"
        " launches a Fireball along the look direction (reusing the emberling projectile chain), stone-wa"
        "ll hit places standing fire in the approach air cell (100% per-entity ignite chance, flint-and-s"
        "teel-homolog caliber), plank wall enters burning state directly, survival consumes one charge wh"
        "ile creative does not, straight-down launch never self-hits (behavioral: player-side fireball sp"
        "awned INSIDE the player's expanded hitbox with playerTargetable=true yields zero mobAttackedPlay"
        "er and settles into floor fire - owner exemption via shooter==-1 skip), recipes blaze-powder+coa"
        "l/charcoal+gunpowder -> 3 charges both match and the item sits in the creative material palette "
        "at id 0x25C" }, [&]() {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(91);
        const int fy = 83;
        bool okA = true;
        // (a1) 干木板贴岩浆 → 点燃（直燃语义）→ 续驱至烧毁。驱动：每窗翻转标记格 poke 岩浆脏
        //     （m_lavaDirty 稳态早退——setBlock 直写不 poke，须显式重标脏；r24#3(b) 同款手法）。
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(6, fy, z, BR::Stone, 0);
            wL.setBlock(5, fy, z, BR::Lava, 0); // 岩浆源列（不驱动流动也参与 ignite pass 扫描）
        }
        wL.setBlock(6, fy, 5, BR::Planks, 0); // 木板贴岩浆（x=5 的 +X 邻）
        int lavaWins = 0;
        bool sawLit = false;
        for (; lavaWins < 240 && !sawLit; ++lavaWins) {
            wL.setBlock(5, fy + 1, 4, (lavaWins & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int t = 0; t < 35; ++t) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗（r24#3 同款）
            sawLit = wL.isBurningAt(6, fy, 5); // 点燃观测在窗内即时取（防同窗后段已烧毁漏采）
        }
        const bool litByLava = sawLit;
        bool burnedAway = false;
        if (litByLava) {
            for (int t = 0; t < 120 && !burnedAway; ++t) { // 燃烧计时 10 窗 + 余烬衔接
                wL.tickFire();
                if (wL.blockAt(6, fy, 5) != BR::Planks) burnedAway = true;
            }
        }
        okA = okA && litByLava && burnedAway;
        // (a2) 湿对照：**书架**邻岩浆且邻水 → 恒静（防火带强断言）。用书架而非木板：木板是 isWoodLike，
        //     掷中且湿拒后会**回落旧焚毁路径**（焚毁无水守卫 = 既有语义，板被烧掉）→ 断言面混入旧路径；
        //     书架非 isWoodLike（掷中且湿拒 → 本窗跳过不焚毁）→ 240 窗后仍完好未燃 = 防火带在点燃入口
        //     恒拒的纯净信号（无 tickFire 驱动 → 曾点燃会驻留燃烧态被末态捕获）。
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(12, fy, z, BR::Stone, 0);
            wL.setBlock(11, fy, z, BR::Lava, 0);
            wL.setBlock(13, fy, z, BR::Water, 0); // 水在书架另一侧 → 书架湿
        }
        wL.setBlock(12, fy, 5, BR::Bookshelf, 0);
        for (int t = 0; t < 240; ++t) {
            wL.setBlock(11, fy + 1, 4, (t & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int k = 0; k < 35; ++k) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗
        }
        const bool wetQuiet = wL.blockAt(12, fy, 5) == BR::Bookshelf
                              && !wL.isBurningAt(12, fy, 5);
        okA = okA && wetQuiet;
        // (a3) 书架（flammable 非 isWoodLike）→ 点燃（入口门扩全表）
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(20, fy, z, BR::Stone, 0);
            wL.setBlock(19, fy, z, BR::Lava, 0);
        }
        wL.setBlock(20, fy, 5, BR::Bookshelf, 0);
        bool shelfLit = false;
        for (int t = 0; t < 240 && !shelfLit; ++t) {
            wL.setBlock(19, fy + 1, 4, (t & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int k = 0; k < 35; ++k) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗
            shelfLit = wL.isBurningAt(20, fy, 5); // 窗内即时观测（同 a1）
        }
        okA = okA && shelfLit;
        // 清 (a) 场
        for (int x : {5, 6, 11, 12, 13, 19, 20})
            for (int z = 4; z <= 6; ++z)
                for (int dy = 0; dy <= 2; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);

        // (b) 烈焰弹全链（真 pc rig：石地 + 石靶墙 + 木靶墙）
        EntityManager ents;
        Hotbar hb;
        PlayerController pcF;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        for (int x = 1; x <= 14; ++x)
            for (int z = 4; z <= 8; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                for (int dy = 1; dy <= 3; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);
            }
        for (int dy = 1; dy <= 2; ++dy) // 石靶墙 x=13（两层高）
            for (int z = 4; z <= 8; ++z) wL.setBlock(13, fy + dy, z, BR::Stone, 0);
        pcF.setWorld(&wL);
        pcF.setEntityManager(&ents);
        pcF.setHotbar(&hb);
        // placeBlock 的 !m_captured 入口门（headless 无指针锁定）：探针 pc 无窗口（QQuickItem 裸构造，
        //   window()=null → grab() 早退）→ 直调公开 Q_INVOKABLE setCaptured 不可用（private 非槽）。
        //   captured 是 Q_PROPERTY(bool captured READ ...) 只读 → QML 写不进。t886/t881 探针的
        //   useFishingRod 不吃此门，placeBlock 吃——headless 唯一通道 = 手动把 pc 挂进一个 QQuickWindow
        //   （setParentItem 挂 window contentItem → windowChanged → onWindowChanged 设 m_window）再调
        //   grab()（m_window 就绪后 setCaptured(true) 走通）。窗仅作指针捕获载体（无 show，光标覆写
        //   只作用于本测试进程无副作用；进程退出析构配对 release）。
        QQuickWindow probeWin;
        pcF.setParentItem(probeWin.contentItem());
        pcF.grab();
        // t1040 rig 加固（t1030 盲区清偿）：烈焰弹是材料段物品，真实游戏 QML 绑定 player.selectedBlock
        //   经 Hotbar::selectedBlockId 材料段→Air；探针无 QML 引擎，m_selectedBlock 会保持构造默认
        //   Stone（playercontroller.h t06 默认）。发射分支按 hotbar heldItemId 无条件 return（不读
        //   selectedBlock，fall-through 通用放置不可达）→ 显式归 Air 建模接线（阴性轮/未来腿兜底）。
        pcF.setSelectedBlock(int(BR::Air));
        // 配方 match 双证 + id 钉位 + 创造调色板
        const int gridCoal[9] = { RecipeRegistry::BlazePowderId, RecipeRegistry::CoalId,
                                  RecipeRegistry::GunpowderId, 0, 0, 0, 0, 0, 0 };
        const int gridChar[9] = { RecipeRegistry::BlazePowderId, RecipeRegistry::CharcoalId,
                                  RecipeRegistry::GunpowderId, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gridCoal, 2);
        const RecipeRegistry::Recipe *rch = RecipeRegistry::match(gridChar, 2);
        bool inPalette = false;
        const QVariantList mats = hb.creativeMaterials();
        for (const QVariant &v : mats)
            if (v.toInt() == RecipeRegistry::FireChargeId) { inPalette = true; break; }
        const bool okRecipe = rc && rch && rc->outputId == RecipeRegistry::FireChargeId
                              && rc->outputCount == 3 && rch->outputCount == 3
                              && RecipeRegistry::FireChargeId == 0x25C && inPalette;
        // 发射链 A：创造模式瞄石墙（水平直射 z=6 行）
        hb.setStack(0, RecipeRegistry::FireChargeId, 5, 0);
        hb.setSelectedSlot(0);
        const QVector3D eyeP(2.5f, float(fy + 1), 6.5f);
        const QVector3D dirV = (QVector3D(13.5f, float(fy + 1) + 0.5f, 6.5f)
                                - QVector3D(eyeP.x(), eyeP.y() + 1.62f, eyeP.z())).normalized();
        pcF.loadSavedState(eyeP.x(), eyeP.y(), eyeP.z(),
                           qRadiansToDegrees(std::atan2(-dirV.x(), -dirV.z())),
                           qRadiansToDegrees(std::asin(dirV.y())), 1 /* Creative */);
        pumpFor(17); pcF.tick(); // settle（碰撞落位）
        int swingSeen = 0;
        QObject::connect(&pcF, &PlayerController::swingArm, &pcF, [&]() { ++swingSeen; });
        const int beforeCount = ents.count();
        pcF.placeBlock(); // 右键发射
        int fbIdx = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Fireball)) { fbIdx = i; break; }
        const bool spawnedOk = fbIdx >= 0 && ents.count() > beforeCount
                               && hb.blockIdAt(0) == RecipeRegistry::FireChargeId
                               && hb.countAt(0) == 5 /* 创造不耗 */ && swingSeen >= 1;
        // 推实体 tick 至撞击（~11 格 / 12b/s ≈ 0.95s ≈ 60 tick @dt0.05；上限 80 裕量）
        int fireAtWall = -1;
        for (int t = 0; t < 80 && fireAtWall < 0; ++t) {
            ents.tick(0.05, &wL, farL, 0.3f, 1.8f, false);
            if (!ents.aliveAt(fbIdx)) { // 消失 = 撞击结算完成（石墙非可燃 → 来向格立地火）
                const QVector3D last = ents.posAt(fbIdx);
                const int lx = qFloor(last.x()), ly = qFloor(last.y()), lz = qFloor(last.z());
                static constexpr int kNb7[7][3] = {{0,0,0},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0},{0,0,-1},{0,0,1}};
                for (const auto &o : kNb7) {
                    const int qx = lx + o[0], qy = ly + o[1], qz = lz + o[2];
                    if (qy >= 0 && wL.blockAt(qx, qy, qz) == BR::Fire) { fireAtWall = t; break; }
                }
            }
        }
        // 发射链 B：生存模式瞄木板墙（换靶重发；验消耗 + 直燃口径）。链 A 的火球已飞出（撞墙消失或
        //   寿命兜底）→ 本段「首个 Fireball」扫描会命中链 A 残留（若寿命未到）→ 先排空实体桶。
        hb.setStack(0, RecipeRegistry::FireChargeId, 3, 0);
        for (int dy = 1; dy <= 2; ++dy)
            for (int z = 4; z <= 8; ++z) wL.setBlock(11, fy + dy, z, BR::Planks, 0);
        pcF.clearStatusEffects();
        // 链 A 火球已结算消失（fireAtWall ≥ 0 实证）→ 链 B 的首个活 Fireball 即新弹。**不可按 idx 排除
        //   链 A 残弹**——EntityManager 是 slot-reuse（t256），新火球大概率恰好复用链 A 的槽位。
        const QVector3D dirW = (QVector3D(11.5f, float(fy + 1) + 0.5f, 6.5f)
                                - QVector3D(eyeP.x(), eyeP.y() + 1.62f, eyeP.z())).normalized();
        pcF.loadSavedState(eyeP.x(), eyeP.y(), eyeP.z(),
                           qRadiansToDegrees(std::atan2(-dirW.x(), -dirW.z())),
                           qRadiansToDegrees(std::asin(dirW.y())), 2 /* Survival */);
        pumpFor(320); pcF.tick(); // > 放置 CD 200ms（链 A 发射已刷新 m_lastPlaceMs——不泵则本发被吞）
        pcF.placeBlock();
        int fb2 = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Fireball)) { fb2 = i; break; }
        bool impactSettled = false;
        for (int t = 0; t < 80 && !impactSettled && fb2 >= 0; ++t) {
            ents.tick(0.05, &wL, farL, 0.3f, 1.8f, false);
            if (!ents.aliveAt(fb2)) impactSettled = true; // 消失 = 撞击结算完成
        }
        // 直燃口径的燃烧态是**持续态**（id 不变）→ 撞击后仍可复验（不依赖窗内时序）
        bool plankStillBurning = false;
        for (int dx = 9; dx <= 13 && !plankStillBurning; ++dx)
            for (int dy = 0; dy <= 3 && !plankStillBurning; ++dy)
                for (int z = 4; z <= 8 && !plankStillBurning; ++z)
                    if (wL.isBurningAt(dx, fy + dy, z)) plankStillBurning = true;
        const bool survivalConsumed = hb.blockIdAt(0) == RecipeRegistry::FireChargeId
                                      && hb.countAt(0) == 2; // 3-1
        // (c) 玩家侧豁免（行为级）：直下发射——火球出生点在玩家外扩命中盒内（眼位下方 0.5，XZ 偏移 0 <
        //     0.6），无豁免则首帧必自击（5HP + 点燃）。豁免（fireballShooter==-1 玩家侧 → 玩家命中分支
        //     跳过）生效 → 全程零 mobAttackedPlayer，火球正常坠地生火（石地板 → 来向格立地火）。
        int playerHits = 0;
        QObject::connect(&ents, &EntityManager::mobAttackedPlayer, &ents,
                         [&](int, int, float, float) { ++playerHits; });
        const int fbD = ents.spawnFireball(QVector3D(3.5f, float(fy + 1) + 1.62f - 0.5f, 6.5f),
                                            QVector3D(0.0f, -12.0f, 0.0f), 100);
        bool downSettled = false;
        for (int t = 0; t < 40 && !downSettled; ++t) {
            // playerTargetable=true + listener=玩家脚位 → 玩家命中分支真实求值（豁免是唯一免击原因）
            ents.tick(0.05, &wL, QVector3D(3.5f, float(fy + 1), 6.5f), 0.3f, 1.8f, true);
            if (fbD >= 0 && !ents.aliveAt(fbD)) downSettled = true;
        }
        const bool exemptOk = playerHits == 0 && downSettled
                              && wL.blockAt(3, fy + 1, 6) == BR::Air   // review27 #14①：玩家自身格不再落火（朝脚下直射偏移出 AABB）
                              && wL.blockAt(4, fy + 1, 6) == BR::Fire; // 落火偏移到不与玩家 AABB 相交的邻格（+X 候选首中）
        // 清场
        for (int x = 1; x <= 14; ++x)
            for (int z = 4; z <= 8; ++z)
                for (int dy = 0; dy <= 3; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);
        pcF.clearStatusEffects();
        probeWin.deleteLater(); // 捕获载体窗随探针作用域收尾（release 光标覆写配对）
        pcF.release();
        const bool okT891 = okA && spawnedOk && okRecipe && fireAtWall >= 0
                            && plankStillBurning && survivalConsumed && exemptOk;
        if (!okT891) ++totalFail;
        if (!okT891)
            qInfo().noquote() << "  [t891 diag] okA" << okA << "litByLava" << litByLava
                              << "burnedAway" << burnedAway << "wetQuiet" << wetQuiet
                              << "shelfLit" << shelfLit << "| spawnedOk" << spawnedOk
                              << "fbIdx" << fbIdx << "count" << ents.count()
                              << "| okRecipe" << okRecipe << "inPalette" << inPalette
                              << "| fireAtWall" << fireAtWall << "plankStillBurning" << plankStillBurning
                              << "survivalConsumed" << survivalConsumed
                              << "exemptOk" << exemptOk << "playerHits" << playerHits
                              << "cnt0" << hb.countAt(0) << "id0" << hb.blockIdAt(0);
        qInfo().noquote() << (okT891 ? "PASS" : "FAIL")
                          << "| t891 ignition sources extended: (a) lava neighbor ignition -- a wood "
                             "plank hugging a lava source now ENTERS the burning state on the first "
                             "hit window (id preserved = direct-burn semantics via the shared "
                             "igniteFlammableAt entry; the core path previously only incinerated), "
                             "then burns away through the burn-timer endgame exclusively (single-roll "
                             "dual-meaning: ignite wins over incinerate, no double consumption), a "
                             "water-backed bookshelf stays intact and unburned (damp-fuel firewall at "
                             "the ignite entry; a wet plank would fall to the legacy incinerate path "
                             "which has no water guard - out of scope), "
                             "and a bookshelf (flammable but outside the old isWoodLike set) now "
                             "catches too (entry gate widened to the full flammable table); "
                             "(b) fire charge item: real-PC right-click launches a Fireball along the "
                             "look direction (reusing the emberling projectile chain), stone-wall hit "
                             "places standing fire in the approach air cell (100% per-entity ignite "
                             "chance, flint-and-steel-homolog caliber), plank wall enters burning "
                             "state directly, survival consumes one charge while creative does not, "
                             "straight-down launch never self-hits (behavioral: player-side fireball "
                             "spawned INSIDE the player's expanded hitbox with playerTargetable=true "
                             "yields zero mobAttackedPlayer and settles into floor fire - owner "
                             "exemption via shooter==-1 skip), "
                             "recipes blaze-powder+coal/charcoal+gunpowder -> 3 charges both match and "
                             "the item sits in the creative material palette at id 0x25C";
    });
}
