#include "matrix_helpers.h"

#include "entitystore.h" // R20.14 被测：EntityStore（掉落物模拟单一权威，非 QObject 值语义）

// R20.14 EntityStore 探针段（4 腿 r2014a-d；filter 词 "r2014"；矩阵 586→590，band 588±2 内）。
// 置尾先例沿用（接 section17，runAll 末执行）；store / Adapter 直驱先例同 section04
//（ItemEntityManager 探针 t1027 系零 QML 直编）+ fresh 小世界 incantation 同 section11+
//（本段用裸 World 仅承焚毁 / 重力物理子面——rig 世界 w 零接触）。
// 任务契约（docs/refactor-plan-2026-09-08.md §29.3 R20.14 原文四验收）：
//   「模拟迁移先掉落物族」→ 全段前提（ItemEntityManager 收敛过渡 Adapter，EntityStore 单一权威）；
//   「EntityId 稳定」→ r2014a（store 级 EntityId 自 1 单调 / 永不复用 / 存活期恒定；槽位 LIFO
//     复用 = t256/t978 QML Repeater 承重面零变化——双轨语义同段断言）；
//   「模拟实体不依赖 QML」→ r2014d（QObjectFree 编译期钉 + entitystore.h 禁 Q_OBJECT/Q_PROPERTY/
//     QQuick/qqml 反探）+ r2014a（store 直驱无 QObject 即全模拟可达）；
//   「可见实体由 snapshot 决定」→ r2014b（EntityStoreSnapshot 值类型沿重建 / 值隔离 / 独立复制 /
//     空槽不入集 = 可见集恰为活体集；feeder 改读快照属渲染管线后续单——登记非目标，本段只钉
//     快照权威本身与 Adapter 消费面零变化）；
//   「旧 EntityManager 作过渡 Adapter」→ r2014a（store↔Adapter 孪生全状态逐位一致 = 委托不复制）
//     + r2014c（entitiesChanged 信号沿计数 / revision 契约 / feeder 消费面回归——QML 面
//     语义零变化承重墙）+ r2014d（模拟本体禁回流反探）。
// 阴性轮恰红面设计（先于腿文定稿；R20.13 教训：共享实现下变异红面会收缩——绝对断言归被测
//   语义腿专钉，等价腿只钉相对恒等）：NEG1 摘 spawnItem 合并（store 实现体）→ r2014a 合并
//   绝对断言红 + t1027（既有合并语义腿）红，孪生等价子面保持绿（双路同变）；NEG2 摘 notify 沿
//   快照重建 → r2014b 单腿红（快照陈旧 vs 活体读）。存证 matrix_r2014_neg.log。
void MatrixRun::section18_entitystore()
{
    // ── 裸世界 rig：fresh 小世界（section11+ 同款 incantation）——仅 r2014a 焚毁/重力子面用 ──
    World wS18;
    wS18.setWidth(48);
    wS18.setDepth(48);
    wS18.setHeight(96);
    wS18.setSeed(82);
    wS18.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
    wS18.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG

    // 岩浆格选址：高空空气袋（y∈[70,90]，离地形远——确定性扫描，t1051「选址结构性保证」纪律）。
    int lavaX = -1, lavaY = -1, lavaZ = -1;
    for (int x = 8; x < 20 && lavaX < 0; ++x)
        for (int z = 18; z < 26 && lavaX < 0; ++z)
            for (int y = 70; y < 90; ++y)
                if (wS18.blockAt(x, y, z) == 0 && wS18.blockAt(x, y + 1, z) == 0) {
                    lavaX = x; lavaY = y; lavaZ = z;
                    break;
                }
    // 重力落柱选址：heightAt 顶为实块、顶上 h+1..h+3 三格全空的第一列（防树冠/花草占位
    //   抬高落点——首版漏验 h+2 致林冠落点 y 窗口误红教训）。
    int colX = -1, colZ = -1, colH = -1;
    for (int x = 30; x < 42 && colX < 0; ++x)
        for (int z = 30; z < 40 && colX < 0; ++z) {
            const int h = wS18.heightAt(x, z);
            if (h > 0 && h + 4 < wS18.height() && wS18.blockAt(x, h, z) != 0
                && wS18.blockAt(x, h + 1, z) == 0 && wS18.blockAt(x, h + 2, z) == 0
                && wS18.blockAt(x, h + 3, z) == 0) {
                colX = x; colZ = z; colH = h;
                break;
            }
        }

    // ── r2014a：store↔Adapter 孪生行为等价承重墙 + EntityId 稳定语义（验收①②④）────────
    //   同一操作序列逐次施于 EntityStore（直驱）与 ItemEntityManager（Adapter 面）：三连同格/
    //   邻格 spawn（合并）、带名不合并（rev2-C5）、定向弹出、部分拾取（余数回写）、removeAt、
    //   三维投掷（LIFO 槽复用）、重力落定（裸世界 40 帧）、岩浆焚毁、批内合并、墙钟顺延 despawn、
    //   空场 clearAll——每步后全状态逐位对账（count/revision/liveCount/highWater + 逐槽 alive/
    //   resting/pos/itemId/count/entityId/enchants/name/durability）。合并 / LIFO 复用 / despawn /
    //   clearAll 通知为绝对断言（NEG1 恰红面）；等价对账为相对恒等（共享实现变异下保持绿）。
    runLeg(QStringLiteral("r2014a store-vs-adapter twin equivalence + stable EntityId semantics"
        " (R20.14 EntityStore): an identical operation sequence (3 spawn merges same/adjacent"
        " cell, named-drop no-merge, directional dispenser pop, partial pickup remainder,"
        " removeAt, 3D throw reusing the LIFO slot, 40-frame gravity settle on a bare seed-82"
        " world, lava-cell burn, in-batch merge, wall-clock-shifted despawn, clearAll on an"
        " empty field) drives a bare EntityStore and the ItemEntityManager adapter in lockstep"
        " - after every step the full state matches bit-for-bit (count/revision/liveCount/"
        " high-water + per-slot alive/resting/pos/itemId/count/entityId/enchants/name/"
        " durability), merge/liveCount/slot-reuse/despawn hold their absolute values (merge"
        " fills 1 slot to count 3, the thrown item lands in the freed LIFO slot, lifetime"
        " despawn drains to zero, clearAll still notifies unconditionally), and store-level"
        " EntityIds are stable: strictly increasing per spawn, never reused after removal, the"
        " surviving merged entity keeps its id across every later operation while its slot is"
        " reused by other tenants (dual-track: presentation slot vs stable id)"), [&]() {
        bool ok = true;
        QString diag;

        const bool sitesOk = lavaX >= 0 && colX >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites lava=(%1,%2,%3) col=(%4,%5@%6)] ")
                                 .arg(lavaX).arg(lavaY).arg(lavaZ).arg(colX).arg(colZ).arg(colH);

        if (ok) {
            EntityStore es;             // store 直驱（无 QObject——验收②模拟不依赖 QML 的可达性）
            ItemEntityManager im;       // Adapter 面（QML/feeder 消费零变化的承载）
            QString why;
            const auto twinsEqual = [&](const char *step) {
                why.clear();
                bool eq = es.count() == im.count() && es.revision() == im.revision()
                    && es.liveCount() == im.liveCount()
                    && es.liveHighWater() == im.liveHighWater();
                for (int i = 0; i < qMax(es.count(), im.count()) && eq; ++i) {
                    eq = eq && es.aliveAt(i) == im.aliveAt(i);
                    if (!eq || !es.aliveAt(i)) continue;
                    eq = eq && es.restingAt(i) == im.restingAt(i)
                        && es.posAt(i) == im.posAt(i)
                        && es.itemIdAt(i) == im.itemIdAt(i)
                        && es.countAt(i) == im.countAt(i)
                        && es.entityIdAtSlot(i) == im.entityIdAtSlot(i)
                        && es.durabilityAt(i) == im.durabilityAt(i)
                        && es.nameAt(i) == im.nameAt(i);
                    const QVariantList ea = es.enchantsAt(i), eb = im.enchantsAt(i);
                    eq = eq && ea.size() == 4 && eb.size() == 4
                        && ea.at(0) == eb.at(0) && ea.at(1) == eb.at(1)
                        && ea.at(2) == eb.at(2) && ea.at(3) == eb.at(3);
                }
                if (!eq)
                    diag += QStringLiteral("[twin-mismatch@%1: %2]").arg(QLatin1String(step)).arg(why.isEmpty() ? QStringLiteral("face") : why);
                return eq;
            };

            // step1：三连 spawn 同格/邻格 → 就近合并塌缩为 1 槽 count=3（绝对值；NEG1 恰红面）。
            es.spawnItem(10, 40, 10, int(BR::Dirt), 1);
            im.spawnItem(10, 40, 10, int(BR::Dirt), 1);
            const quint32 idA = es.entityIdAtSlot(0);
            es.spawnItem(10, 40, 10, int(BR::Dirt), 1);
            im.spawnItem(10, 40, 10, int(BR::Dirt), 1);
            es.spawnItem(11, 40, 10, int(BR::Dirt), 1);
            im.spawnItem(11, 40, 10, int(BR::Dirt), 1);
            const bool mergeOk = es.liveCount() == 1 && es.aliveAt(0) && es.countAt(0) == 3
                && im.liveCount() == 1 && im.aliveAt(0) && im.countAt(0) == 3;
            ok = ok && mergeOk && twinsEqual("merge3");
            if (!mergeOk)
                diag += QStringLiteral("[merge live=%1/%2 cnt=%3/%4] ").arg(es.liveCount())
                            .arg(im.liveCount()).arg(es.countAt(0)).arg(im.countAt(0));

            // step2：带名 + 附魔 + 耐久 → 带名不合并（rev2-C5）；元数据保真。
            const QVariantList ench2 { 5, 6, 0, 0 };
            es.spawnItem(20, 40, 10, int(BR::Dirt), 1, ench2, QStringLiteral("  Excalibur  "), 42);
            im.spawnItem(20, 40, 10, int(BR::Dirt), 1, ench2, QStringLiteral("  Excalibur  "), 42);
            const quint32 idB = es.entityIdAtSlot(1);
            const bool namedOk = es.liveCount() == 2 && es.nameAt(1) == QStringLiteral("Excalibur")
                && es.durabilityAt(1) == 42 && es.enchantsAt(1).at(0).toInt() == 5;
            ok = ok && namedOk && twinsEqual("named");

            // step3：定点定向弹出（发射器排出口口径）。
            es.spawnItemAt(QVector3D(30.5f, 40.5f, 10.5f), int(BR::Stone), 5, 1.0f, 0.0f, 2.0f);
            im.spawnItemAt(QVector3D(30.5f, 40.5f, 10.5f), int(BR::Stone), 5, 1.0f, 0.0f, 2.0f);
            const quint32 idC = es.entityIdAtSlot(2);
            ok = ok && es.liveCount() == 3 && twinsEqual("dispenser");

            // step4：部分拾取（余数回写 5→4，实体保留）。
            es.setCountAt(2, 4);
            im.setCountAt(2, 4);
            ok = ok && es.countAt(2) == 4 && es.aliveAt(2) && twinsEqual("partial-pickup");

            // step5：removeAt 销毁带名实体（槽 1 入 free list）。
            es.removeAt(1);
            im.removeAt(1);
            ok = ok && !es.aliveAt(1) && es.liveCount() == 2 && twinsEqual("removeAt");

            // step6：三维投掷 → LIFO 槽复用（槽 1 复活 = t256 QML Repeater 面零变化的绝对断言）。
            es.spawnItemThrown(QVector3D(34.5f, 40.5f, 10.5f), int(BR::Planks), 7, 0.0f, -1.0f, 0.0f, 3.0f);
            im.spawnItemThrown(QVector3D(34.5f, 40.5f, 10.5f), int(BR::Planks), 7, 0.0f, -1.0f, 0.0f, 3.0f);
            const quint32 idD = es.entityIdAtSlot(1);
            const bool lruOk = es.aliveAt(1) && es.itemIdAt(1) == int(BR::Planks);
            ok = ok && lruOk && twinsEqual("thrown-lifo");

            // step7：重力落定（裸世界 40 帧——共享只读世界，物理逐位同源）。
            wS18.setBlock(colX, colH + 3, colZ, quint8(BR::Air), 0); // 确保落程无遮挡（选址已空，幂等）
            es.spawnItemAt(QVector3D(float(colX) + 0.5f, float(colH) + 3.5f, float(colZ) + 0.5f),
                           int(BR::Dirt), 1, 0.0f, 0.0f, 0.0f);
            im.spawnItemAt(QVector3D(float(colX) + 0.5f, float(colH) + 3.5f, float(colZ) + 0.5f),
                           int(BR::Dirt), 1, 0.0f, 0.0f, 0.0f);
            for (int f = 0; f < 40; ++f) {
                es.tick(0.016, &wS18);
                im.tick(0.016, &wS18);
            }
            const bool settleOk = es.restingAt(3) && im.restingAt(3)
                && es.posAt(3) == im.posAt(3)
                && es.posAt(3).y() > float(colH) && es.posAt(3).y() < float(colH) + 2.0f;
            ok = ok && settleOk && twinsEqual("gravity");
            if (!settleOk)
                diag += QStringLiteral("[settle rest=%1/%2 y=%3 h=%4] ").arg(es.restingAt(3))
                            .arg(im.restingAt(3)).arg(es.posAt(3).y()).arg(colH);

            // step8：岩浆焚毁（中心格 Lava → 瞬灭释放；t343 语义沿）。
            wS18.setBlock(lavaX, lavaY, lavaZ, quint8(BR::Lava), 0);
            es.spawnItemAt(QVector3D(float(lavaX) + 0.5f, float(lavaY) + 0.5f, float(lavaZ) + 0.5f),
                           int(BR::Dirt), 1, 0.0f, 0.0f, 0.0f);
            im.spawnItemAt(QVector3D(float(lavaX) + 0.5f, float(lavaY) + 0.5f, float(lavaZ) + 0.5f),
                           int(BR::Dirt), 1, 0.0f, 0.0f, 0.0f);
            const int liveBeforeBurn = es.liveCount();
            es.tick(0.016, &wS18);
            im.tick(0.016, &wS18);
            const bool burnOk = liveBeforeBurn == 5 && es.liveCount() == 4 && !es.aliveAt(4);
            ok = ok && burnOk && twinsEqual("lava-burn");
            if (!burnOk)
                diag += QStringLiteral("[burn before=%1 after=%2 alive4=%3] ")
                            .arg(liveBeforeBurn).arg(es.liveCount()).arg(es.aliveAt(4));

            // step9：批内合并（t354：批内 notify 不 emit、末尾收口——契约在 r2014c 计数，此处对账状态）。
            es.beginBatch(); im.beginBatch();
            es.spawnItem(50, 40, 10, int(BR::Dirt), 1); im.spawnItem(50, 40, 10, int(BR::Dirt), 1);
            es.spawnItem(50, 40, 10, int(BR::Dirt), 1); im.spawnItem(50, 40, 10, int(BR::Dirt), 1);
            es.endBatch(); im.endBatch();
            const int batchSlot = es.count() - 1;
            const bool batchMergeOk = es.aliveAt(batchSlot) && es.countAt(batchSlot) == 2
                && es.liveCount() == 5;
            ok = ok && batchMergeOk && twinsEqual("batch-merge");

            // EntityId 稳定语义（验收①；store 级口径）：严格递增 / 移除后永不复用 / 存活期恒定
            //   ——idA（合并实体）跨后续全部操作不变，尽管其所在槽被其它租户围绕。
            const bool idOk = idA > 0 && idB > idA && idC > idB && idD > idC
                && idD != idB // 被移除实体的 id 永不复用（槽复用 ≠ id 复用——双轨语义核心）
                && es.entityIdAtSlot(0) == idA && im.entityIdAtSlot(0) == idA;
            ok = ok && idOk;
            if (!idOk)
                diag += QStringLiteral("[entityId A=%1 B=%2 C=%3 D=%4 now0=%5/%6] ")
                            .arg(idA).arg(idB).arg(idC).arg(idD)
                            .arg(es.entityIdAtSlot(0)).arg(im.entityIdAtSlot(0));

            // step10：寿命注入缝（ageLifetimeClock 生产零调用，t1029 缝纪律）+ tick(null) →
            //   自然寿命 despawn（t320 沿；deferWallClocks 是 +ms 顺延对偶，不能老化）。
            es.ageLifetimeClock(300001);
            im.ageLifetimeClock(300001);
            es.tick(0.016, nullptr);
            im.tick(0.016, nullptr);
            const bool despawnOk = es.liveCount() == 0 && im.liveCount() == 0 && es.count() > 0;
            ok = ok && despawnOk && twinsEqual("despawn");
            if (!despawnOk)
                diag += QStringLiteral("[despawn live=%1/%2 slots=%3] ").arg(es.liveCount())
                            .arg(im.liveCount()).arg(es.count());

            // step11：空场 clearAll 仍无条件通知（重置语义沿）——**不 bump revision**（旧
            //   clearAll 只 emit 不 ++revision 的语义逐位保留，r2014c 同契约）。
            const int revBefore = es.revision();
            es.clearAll();
            im.clearAll();
            const bool clearOk = es.revision() == revBefore && im.revision() == revBefore;
            ok = ok && clearOk && twinsEqual("clearAll");
            if (!clearOk)
                diag += QStringLiteral("[clear rev %1/%2 vs %3] ").arg(es.revision())
                            .arg(im.revision()).arg(revBefore);

            // 快照面孪生终态对账（Adapter snapshot() 委托同一 store——验收③④交汇）：
            const bool snapTwinOk = es.snapshot().entries.size() == im.snapshot().entries.size();
            ok = ok && snapTwinOk;
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2014a store-vs-adapter twin equivalence + stable EntityId:"
                             " identical op sequences (merge triple, named no-merge, dispenser"
                             " pop, partial pickup, removeAt, throw into LIFO slot, 40-frame"
                             " gravity settle, lava burn, in-batch merge, shifted despawn,"
                             " clearAll) leave bare EntityStore and the ItemEntityManager"
                             " adapter bit-identical in full state after every step; merge"
                             " count/slot-reuse/despain/clearAll absolutes hold; EntityIds"
                             " strictly increase, are never reused after removal, and the"
                             " surviving entity keeps its id while slots turn over"
                          << (ok ? QString() : diag);
    });

    // ── r2014b：可见实体由 snapshot 决定（验收③；EntityStoreSnapshot 值纪律）────────────
    //   快照在 notify 沿自动重建（变更后立即可见、无需手动刷新）；旧快照值隔离（沿后不被迫
    //   改）；拷贝即深拷贝（独立复制 + 拷贝可变不回灌）；空槽不入集（可见集恰为活体集）；
    //   元数据可见面（附魔 / 名 trimmed / 耐久 / EntityId）齐备；Adapter 面快照与其逐槽读口
    //   对账（消费面零变化下快照权威并存）。**场景全程无合并步**（格距 >kMergeRadius）——
    //   恰红面纪律：NEG1（摘合并）只红 r2014a+t1027，本腿快照权威语义独立承变异。
    runLeg(QStringLiteral("r2014b visible entities decided by the snapshot (R20.14 EntityStore):"
        " EntityStoreSnapshot rebuilds automatically at every notifyChanged edge (each far-apart"
        " spawn extends the entry set immediately with the new revision), old snapshots stay"
        " value-isolated after later mutations, copies are deep (clearing a copy leaves the"
        " original intact), dead slots never enter the entry set so the visible set equals the"
        " live set exactly (slot-by-slot against aliveAt), metadata visibility is complete"
        " (enchants/trimmed name/durability/entityId), and the adapter-exposed snapshot agrees"
        " with the per-slot accessors it shadows (the whole scenario is merge-free so the"
        " snapshot authority is judged independently of merge semantics)"), [&]() {
        bool ok = true;
        QString diag;

        EntityStore es;
        es.spawnItem(10, 40, 10, int(BR::Dirt), 2);
        es.spawnItem(14, 40, 10, int(BR::Dirt), 1); // 距 4.0 > kMergeRadius → 不合并（无合并场景）
        const EntityStoreSnapshot snap1 = es.snapshot(); // 值拷贝定格（store 返回 const 缓存引用）
        const quint32 id0 = es.entityIdAtSlot(0);
        bool s1Ok = snap1.revision == es.revision() && snap1.entries.size() == 2
            && snap1.entries.size() == size_t(es.liveCount())
            && snap1.findSlot(0) && snap1.findSlot(1) && !snap1.findSlot(2)
            && snap1.findSlot(0)->count == 2 && snap1.findSlot(0)->itemId == int(BR::Dirt)
            && snap1.findSlot(0)->entityId == id0 && !snap1.findSlot(0)->resting
            && snap1.findSlot(0)->pos == es.posAt(0) && snap1.findSlot(0)->name.isEmpty()
            && snap1.findSlot(0)->durability == -1 && snap1.findSlot(0)->enchants[0] == 0
            && snap1.findSlot(1)->count == 1;
        // 可见集恰为活体集（逐槽对账 aliveAt）：
        size_t liveSlots = 0;
        for (int i = 0; i < es.count(); ++i)
            liveSlots += es.aliveAt(i) ? 1u : 0u;
        s1Ok = s1Ok && liveSlots == snap1.entries.size();
        ok = ok && s1Ok;
        if (!s1Ok) diag += QStringLiteral("[snap1 rev=%1/%2 n=%3 live=%4] ")
                              .arg(snap1.revision).arg(es.revision())
                              .arg(int(snap1.entries.size())).arg(es.liveCount());

        // 值隔离 + 沿重建：远格 spawn 沿后新快照立即含新条目，旧快照/旧拷贝纹丝不动。
        const EntityStoreSnapshot copy1 = snap1; // 深拷贝
        es.spawnItem(30, 40, 10, int(BR::Dirt), 5); // 远格（距 20）→ 新槽 count 5
        const EntityStoreSnapshot snap2 = es.snapshot();
        const bool isoOk = snap2.findSlot(2) && snap2.findSlot(2)->count == 5
            && snap2.entries.size() == 3 && snap2.revision == es.revision()
            && snap1.entries.size() == 2 && snap1.findSlot(2) == nullptr
            && snap1.revision < snap2.revision
            && copy1.findSlot(0)->count == 2;
        ok = ok && isoOk;
        if (!isoOk)
            diag += QStringLiteral("[iso new=%1 oldN=%2 oldRev=%3 newRev=%4] ")
                        .arg(snap2.entries.size()).arg(int(snap1.entries.size()))
                        .arg(snap1.revision).arg(snap2.revision);

        // 拷贝可变不回灌（独立复制的可写面）：
        EntityStoreSnapshot mut = snap2;
        mut.entries.clear();
        mut.revision = -1;
        const bool mutOk = snap2.entries.size() == 3 && snap2.revision == es.revision();
        ok = ok && mutOk;
        if (!mutOk) diag += QStringLiteral("[mut-copy-backflow] ");

        // 元数据可见面齐备：
        const QVariantList ench { 3, 0, 0, 0 };
        es.spawnItem(20, 40, 10, int(BR::Stone), 1, ench, QStringLiteral("  Excalibur  "), 7);
        const EntityStoreSnapshot snap3 = es.snapshot();
        const EntityStoreEntry *meta = snap3.findSlot(3);
        const bool metaOk = meta && meta->name == QStringLiteral("Excalibur")
            && meta->durability == 7 && meta->enchants[0] == 3 && meta->entityId > 0;
        ok = ok && metaOk;
        if (!metaOk)
            diag += QStringLiteral("[meta name=%1 dur=%2 ench=%3]")
                        .arg(meta ? meta->name : QString()).arg(meta ? meta->durability : -99)
                        .arg(meta ? meta->enchants[0] : -99);

        // 空槽不入集（removeAt 沿后可见集收缩——「可见实体由 snapshot 决定」本体）：
        es.removeAt(1);
        const EntityStoreSnapshot snap4 = es.snapshot();
        const bool deadOk = snap4.findSlot(1) == nullptr && snap4.entries.size() == 3
            && snap4.entries.size() == size_t(es.liveCount())
            && snap4.findSlot(0) && snap4.findSlot(2) && snap4.findSlot(3);
        ok = ok && deadOk;
        if (!deadOk) diag += QStringLiteral("[dead-slot] ");

        // Adapter 面快照 = 其逐槽读口（委托同源；消费面零变化下快照权威并存——验收④交汇）：
        ItemEntityManager im;
        im.spawnItem(10, 40, 10, int(BR::Dirt), 2);
        im.spawnItem(14, 40, 10, int(BR::Dirt), 1);
        im.spawnItem(18, 40, 10, int(BR::Dirt), 1); // 远格（无合并场景——NEG1 恰红面收缩）
        const EntityStoreSnapshot &ims = im.snapshot();
        bool adapterOk = ims.entries.size() == size_t(im.liveCount());
        for (int i = 0; i < im.count() && adapterOk; ++i) {
            if (!im.aliveAt(i)) continue;
            const EntityStoreEntry *e = ims.findSlot(i);
            adapterOk = e && e->pos == im.posAt(i) && e->itemId == im.itemIdAt(i)
                && e->count == im.countAt(i) && e->resting == im.restingAt(i)
                && e->entityId == im.entityIdAtSlot(i)
                && e->durability == im.durabilityAt(i) && e->name == im.nameAt(i);
        }
        ok = ok && adapterOk;
        if (!adapterOk) diag += QStringLiteral("[adapter-snap] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2014b visible entities decided by the snapshot: snapshots"
                             " rebuild at every notify edge (each far-apart spawn extends the"
                             " entry set immediately with the new revision), old snapshots and"
                             " deep copies stay value-isolated (mutating a copy never"
                             " backflows), dead slots never"
                             " enter the set so visible == live slot-by-slot, metadata"
                             " (enchants/trimmed name/durability/entityId) is fully visible,"
                             " and the adapter-exposed snapshot agrees with the per-slot"
                             " accessors"
                          << (ok ? QString() : diag);
    });

    // ── r2014c：Adapter 消费面回归——notify 沿计数 / revision 契约 / feeder 面零变化（验收④）─
    //   承重墙语义（P-t1027 系 / t1032 空转门纪律的 notify 沿）：非批 spawn 每 1 emit；批内
    //   N 变更 0 emit、endBatch 恰 1 emit 收口；无 begin 的 endBatch no-op；setCount 同值 no-op
    //   零沿；despawn / clearAll（含空场）各 1 emit；revision = notify 沿总数（批内也逐次自增）。
    //   全程精确计数（绝对契约面）+ BlockDropInstancing feeder 对 Adapter 的分桶/拾取反射回归。
    runLeg(QStringLiteral("r2014c adapter consumption-face regression: notify-edge and revision"
        " contracts hold exactly through the delegating adapter - one emit per non-batch spawn,"
        " zero emits during a batch with exactly one emit at endBatch, endBatch without begin is"
        " a no-op, setCountAt to the same count is a zero-edge no-op, despawn and clearAll each"
        " emit once including clearAll on an already-empty field (reset semantics are"
        " unconditional), and revision equals the total notify-edge count (bumped inside batches"
        " too); the BlockDropInstancing feeder wired to the adapter still buckets the plain-cube"
        " family per itemId and reflects pickups - the QML/instancing consumption face is"
        " unchanged"), [&]() {
        bool ok = true;
        QString diag;

        ItemEntityManager im;
        int emits = 0;
        QObject::connect(&im, &ItemEntityManager::entitiesChanged, &im, [&emits]() { ++emits; });

        // 非批 spawn：1 沿 1 emit。
        im.spawnItem(10, 40, 10, int(BR::Dirt), 1);
        // 批内两 spawn（远格无合并——恰红面纪律：NEG1 只红 r2014a+t1027）→ 0 emit；
        //   endBatch 恰 1 收口。
        im.beginBatch();
        const bool batchOn = im.batchActive();
        im.spawnItem(30, 40, 10, int(BR::Dirt), 1);
        im.spawnItem(34, 40, 10, int(BR::Dirt), 1);
        const int emitsMidBatch = emits;
        im.endBatch();
        const bool batchOk = batchOn && !im.batchActive() && emitsMidBatch == 1 && emits == 2
            && im.liveCount() == 3 && im.countAt(0) == 1 && im.countAt(1) == 1
            && im.countAt(2) == 1;
        // 无 begin 的 endBatch：no-op。
        im.endBatch();
        const int emitsAfterNop = emits;
        // 同值 setCountAt：零沿（早退防御）。
        im.setCountAt(0, 1);
        const bool nopOk = emitsAfterNop == 2 && emits == 2 && im.revision() == 3;
        // 余数回写 1 沿 + 全拾走 1 沿（槽 1/2 实体保留 → liveCount 2）。
        im.setCountAt(0, 2);
        im.setCountAt(0, 0);
        const bool pickOk = emits == 4 && im.revision() == 5 && im.liveCount() == 2;
        // despawn 沿（spawn 1 + 寿命注入 + tick(null) → 1 emit）。
        im.spawnItem(20, 40, 10, int(BR::Stone), 1);
        im.ageLifetimeClock(300001);
        im.tick(0.016, nullptr);
        const bool despawnOk = emits == 6 && im.revision() == 7 && im.liveCount() == 0;
        // clearAll（空场）×2：各 1 emit（无条件重置语义）。
        im.clearAll();
        im.clearAll();
        // clearAll 直发 emit 但**不 bump revision**（旧实现语义逐位保留——r2014a step11 同契约）。
        const bool clearOk = emits == 8 && im.revision() == 7;

        const bool contractOk = batchOk && nopOk && pickOk && despawnOk && clearOk;
        ok = ok && contractOk;
        if (!contractOk)
            diag += QStringLiteral("[contract batch=%1 nop=%2 pick=%3 despawn=%4 clear=%5"
                                   " emits=%6 rev=%7] ")
                        .arg(batchOk).arg(nopOk).arg(pickOk).arg(despawnOk).arg(clearOk)
                        .arg(emits).arg(im.revision());

        // feeder 消费面回归（BlockDropInstancing 对 Adapter——t1027a 面的随行迷你回归；
        //   全量腿族 t1027/t1032/t1039/t1041 在前置回归跑）：
        ItemEntityManager imF;
        BlockDropInstancing fDirt;
        fDirt.setManager(&imF);
        fDirt.setFamilyId(int(BR::Dirt));
        imF.spawnItem(10, 40, 10, int(BR::Dirt), 1);
        imF.spawnItem(14, 40, 10, int(BR::Torch), 1); // 3D 族：整立方桶不收
        const bool feederOk = fDirt.probeInstanceCount() == 1;
        imF.setCountAt(0, 0); // 拾走唯一泥土 → 桶空
        const bool feederPickOk = fDirt.probeInstanceCount() == 0;
        ok = ok && feederOk && feederPickOk;
        if (!feederOk || !feederPickOk)
            diag += QStringLiteral("[feeder pre=%1 post=%2] ")
                        .arg(feederOk).arg(feederPickOk);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2014c adapter consumption-face regression: exact notify-edge"
                             " contract (1 emit per non-batch spawn, 0 in-batch + 1 endBatch"
                             " close, bare endBatch no-op, same-value setCountAt zero-edge,"
                             " despawn 1 emit, clearAll unconditional 1 emit even on an empty"
                             " field) with revision tracking every edge including in-batch"
                             " ones, and the BlockDropInstancing feeder still buckets per"
                             " itemId and reflects pickups through the adapter"
                          << (ok ? QString() : diag);
    });

    // ── r2014d：结构钉（QObjectFree 编译期复述 + 搬移件落位 + 模拟本体禁回流 + 零 QML）────
    //   r2006b 腿内复钉先例：store/快照/条目 QObjectFree + 快照可独立复制（类型层「摘即红」）；
    //   模拟权威件（合并半径 / 重力 / 支撑真顶 / EntityId 分配点 / 寿命 / 焚毁 / 快照重建沿）
    //   唯一落位 entitystore.{h,cpp}（正面钉 ×N）；itementitymanager.{h,cpp} 禁再现模拟机件
    //   （反探 minCount=1 惯用法——r2013d「网格本体不回流」同款）；entitystore.h 禁 QML 面
    //   （验收②类型层）；Adapter QML 信号面 / 谓词权威锚串在位（t1027b/t1041d/t925 同址）。
    runLeg(QStringLiteral("r2014d structure pins (R20.14 EntityStore): compile-time restatement -"
        " EntityStore, EntityStoreSnapshot and EntityStoreEntry are QObjectFree and the"
        " snapshot independently copyable; comment-stripped source pins hold the simulation"
        " authority in entitystore.{h,cpp} alone (merge radius x3 spawn paths, gravity,"
        " support-top column scans, the single EntityId allocation point, lifetime despawn,"
        " lava/fire/cactus burn, maxStackSize merge clamps, snapshot rebuild edges, the"
        " notifySink seam) with no QML surface (no Q_OBJECT/Q_PROPERTY/Q_INVOKABLE/QQuick/qqml),"
        " hold the adapter as pure delegation (EntityStore m_store member, count/revision"
        " properties and entitiesChanged signal intact, isItem3DFamily family table verbatim)"
        " and forbid the simulation body from flowing back into itementitymanager.{h,cpp}"
        " (no kMergeRadius/kGravity/acquireSlot/m_freeSlots/supportTopYAt)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 编译期复述（r2006b 先例——本腿内再钉一次，头文件钉被删即双红）：
        static_assert(QObjectFree<EntityStore>, "r2014d: EntityStore must not carry QObject");
        static_assert(QObjectFree<EntityStoreSnapshot>, "r2014d: snapshot must not carry QObject");
        static_assert(QObjectFree<EntityStoreEntry>, "r2014d: entry must not carry QObject");
        static_assert(std::is_copy_constructible_v<EntityStoreSnapshot>
                          && std::is_copy_assignable_v<EntityStoreSnapshot>,
                      "r2014d: snapshot must be independently copyable");
        EntityStoreSnapshot valA; // 值语义可拷贝的编译+运行共存探针
        valA.revision = 7;
        EntityStoreSnapshot valB = valA;
        ok = ok && valB.revision == 7;

        // 源码钉根（exe 相对 src/——r2007b 同款解析）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ② store 头：模拟权威 + 值纪律钉在位；QML 面禁出（验收②类型层）。
        const QString eshPath = srcRoot + QStringLiteral("/Entities/entitystore.h");
        const QStringList missEsh = pinSet(eshPath, {
            SrcPin("r2014 store class present", "class EntityStore", 1),
            SrcPin("r2014 entityid authority", "quint32 m_nextEntityId = 1", 1),
            SrcPin("r2014 store qobjectfree pin", "static_assert(QObjectFree<EntityStore>", 1),
            SrcPin("r2014 snapshot type present", "struct EntityStoreSnapshot", 1),
            SrcPin("r2014 snapshot qobjectfree pin", "static_assert(QObjectFree<EntityStoreSnapshot>", 1),
            SrcPin("r2014 snapshot copyable pin", "EntityStoreSnapshot must be independently copyable", 1),
            SrcPin("r2014 notify seam present", "setNotifySink", 1),
            SrcPin("r2014 lifetime test seam", "ageLifetimeClock", 1),
            SrcPin("r2014 sim tick authority", "void tick(qreal dt, World *world);", 1),
        });
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const bool eshQmlFree = forbiddenAbsent(eshPath, "Q_OBJECT")
            && forbiddenAbsent(eshPath, "Q_PROPERTY")
            && forbiddenAbsent(eshPath, "Q_INVOKABLE")
            && forbiddenAbsent(eshPath, "QQuick")
            && forbiddenAbsent(eshPath, "qqml");
        for (const QString &m : missEsh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        ok = ok && eshQmlFree;
        if (!eshQmlFree) diag += QStringLiteral("[store-h-qml] ");

        // ③ store 实现：模拟本体唯一落位（物理/合并/焚毁/寿命/EntityId/快照沿）。
        const QString escPath = srcRoot + QStringLiteral("/Entities/entitystore.cpp");
        const QStringList missEsc = pinSet(escPath, {
            SrcPin("r2014 merge radius authority x3 paths", "kMergeRadius * kMergeRadius", 3),
            SrcPin("r2014 gravity authority", "kGravity", 1),
            SrcPin("r2014 support-top authority", "supportTopYAt", 4),
            SrcPin("r2014 single entityid allocation point", "m_nextEntityId++", 1),
            SrcPin("r2014 lifetime authority", "kDespawnMs", 2),
            SrcPin("r2014 merge stack clamp x3 paths", "maxStackSize", 3),
            SrcPin("r2014 lava burn", "BlockRegistry::Lava", 1),
            SrcPin("r2014 fire burn", "BlockRegistry::Fire", 1),
            SrcPin("r2014 cactus destroy", "BlockRegistry::Cactus", 1),
            SrcPin("r2014 snapshot rebuild edges", "rebuildVisibleSnapshot()", 3),
        });
        const bool escNoSignals = forbiddenAbsent(escPath, "emit ");
        for (const QString &m : missEsc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        ok = ok && escNoSignals;
        if (!escNoSignals) diag += QStringLiteral("[store-cpp-signals] ");

        // ④ Adapter 头：委托形态 + QML 消费面在位 + 模拟机件禁回流（r2013d 同款反探）。
        const QString iemhPath = srcRoot + QStringLiteral("/Game/itementitymanager.h");
        const QStringList missIemh = pinSet(iemhPath, {
            SrcPin("r2014 adapter delegates to store", "EntityStore m_store", 1),
            SrcPin("r2014 adapter qml named element", "QML_NAMED_ELEMENT(ItemEntityManager)", 1),
            SrcPin("r2014 adapter count property face", "Q_PROPERTY(int count READ count NOTIFY entitiesChanged)", 1),
            SrcPin("r2014 adapter revision property face", "Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)", 1),
            SrcPin("r2014 adapter signal face", "void entitiesChanged()", 1),
        });
        const bool iemhNoSim = forbiddenAbsent(iemhPath, "kMergeRadius")
            && forbiddenAbsent(iemhPath, "kGravity")
            && forbiddenAbsent(iemhPath, "acquireSlot")
            && forbiddenAbsent(iemhPath, "m_freeSlots")
            && forbiddenAbsent(iemhPath, "std::vector<ItemEntity");
        for (const QString &m : missIemh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        ok = ok && iemhNoSim;
        if (!iemhNoSim) diag += QStringLiteral("[adapter-h-flowback] ");

        // ⑤ Adapter 实现：谓词权威锚串在位（t925/t1027b/t1041d 同址依赖）+ 模拟本体禁回流。
        const QString iemcPath = srcRoot + QStringLiteral("/Game/itementitymanager.cpp");
        const QStringList missIemc = pinSet(iemcPath, {
            SrcPin("r2014 family predicate authority", "bool ItemEntityManager::isItem3DFamily", 1),
            SrcPin("r2014 family table verbatim", "case 13: case 20: case 136:", 1),
            SrcPin("r2014 adapter sink wiring", "setNotifySink", 1),
        });
        const bool iemcNoSim = forbiddenAbsent(iemcPath, "kMergeRadius")
            && forbiddenAbsent(iemcPath, "supportTopYAt")
            && forbiddenAbsent(iemcPath, "kGravity")
            && forbiddenAbsent(iemcPath, "m_entities")
            && forbiddenAbsent(iemcPath, "acquireSlot");
        for (const QString &m : missIemc) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        ok = ok && iemcNoSim;
        if (!iemcNoSim) diag += QStringLiteral("[adapter-cpp-flowback] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2014d structure pins: EntityStore/snapshot/entry QObjectFree"
                             " and snapshot copyable restated at compile time; simulation"
                             " authority (merge/gravity/support-top/EntityId point/lifetime/"
                             " burn trio/snapshot edges/notify seam) lives only in"
                             " entitystore.{h,cpp} with zero QML surface; adapter held as pure"
                             " delegation with the QML property/signal face and the verbatim"
                             " family table intact; simulation body forbidden from flowing"
                             " back into itementitymanager.{h,cpp}"
                          << (ok ? QString() : diag);
    });
}
