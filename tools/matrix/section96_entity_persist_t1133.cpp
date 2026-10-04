#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：t1133 实体段随统一保存链同事务落盘（SaveRequest.entities 载荷）

#include <limits>

// t1133 实体持久化批探针段（5 腿；filter 词 r2103；矩阵 953→958）。置尾先例沿用（接 section95，
//   runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 / 真临时库 / 纯源钉腿）。
//
// ── era 定谳与范围裁定（交接单 ENTITY-01；留痕三面 = 源码注[entitymanager.h 持久化段] + 本段
//    腿文 + 提交注）────────────────────────────────────────────────────────────────────
//   era 真值（jar 反汇编/存档格式面）：1.0 实体随 chunk 存储持久化（per-chunk "Entities" NBT 列表，
//   region 文件内）；Wolf = "Owner"（归属字符串）+ "Sitting"（byte，**NBT 持久字段非 AI 态**——
//   台账:34「重进丢失」行即本单清偿面）+ "Angry"；Sheep = "Color" + "Shear"；幼体 = "Age" 负值；
//   Slime = "Size"；猪鞍 = "Saddle"；Health/Hurt/Death 常规字段。死亡实体不入档（era 同）。
//   范围裁定：**生物族（含驯服/归属/坐下）= 本单交付面**；掉落物（EntityStore，含 EntityId 单调
//   游标）与载具（矿车/船/箱车 t936 族）持久化 = 评估面过大（ItemEntity 元数据链 / 载具-内容-键
//   三方对账链），**候选池登记下轮排**（源注锚 + 报告陈述）。归属字段 era=Owner 字符串 vs 工程
//   单机无玩家身份面 → 驯服旗即归属面（如实登记简化，见 entitymanager.h 段注）。
//   自然生成去重口径：恢复数 > 0 → 入口初生两路径（固定三只+散布 / 村民桥）跳过（era：群体随
//   chunk 首生一次，有档重进 = 实体数据回放）；黑夜自然刷怪 / 刷怪笼不抑制（运行期生成域正交）。
//
// ── NEG 面与豁免设计（恰红归因先于腿文；摘调用点形编译绿；NEG 靶行豁免不钉 = t1132 同门）──────
//   NEG-1 = 摘 savecoordinator.cpp 步⑥b 实体段调用行（m_store->writeEntitiesPart(req.entities)
//     单语句行删除，编译仍绿）→ 恰红 = {r2103c, r2103d}（两腿同乘统一保存链实体段——c 往返柱
//     存档丢实体 FAIL；d 新码收敛面同样经协调层存 2 行 → 收敛读回空 FAIL。实测恰红核对在案；
//     r2103a/b 纯 EntityManager 面 = 幸存）。
//   NEG-2 = 摘 Main.qml 恢复注入行（const restoredEntityCount = entityManager.
//     restorePersistedEntities(persistedEntityRows) 单语句行删除，QML 无编译门）→ 恰红 = {r2103e}
//     （接线钉缺行 FAIL；r2103a-d 行为腿零源钉面 = 幸存）。
//   双摘面在本段 d 腿豁免不钉（r2102「NEG 双摘面豁免不钉」同门——钉面与 NEG 靶面分离保恰红）。
namespace {

// 源钉根路径（section77 同门：applicationDirPath/../src）。
inline QString srcRootForEntityPersistPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含（注释体锚——pinSet 剥注释会失配，section72..77 同款）。
inline bool rawContainsEntityPersist(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 临时库路径（section92 tempDb 同门：QDir::temp() + pid + 腿标，测试自清理）。
inline QString entityPersistTempDb(const char *tag)
{
    return QDir::temp().absoluteFilePath(QStringLiteral("vo_t1133_ent_%1_%2.sqlite")
                                             .arg(QLatin1String(tag))
                                             .arg(QCoreApplication::applicationPid()));
}

// 造一枚合法持久化行（防御腿/容量腿的载荷源——与 exportPersistedEntities 产物同形）。
inline QVariantMap makeEntityRow(int type, float x, float y, float z, int hp)
{
    QVariantMap row;
    row.insert(QStringLiteral("kind"), 0); // EntityManager::Kind::Mob
    row.insert(QStringLiteral("type"), type);
    row.insert(QStringLiteral("x"), x);
    row.insert(QStringLiteral("y"), y);
    row.insert(QStringLiteral("z"), z);
    row.insert(QStringLiteral("color"), QStringLiteral("#ff5555"));
    row.insert(QStringLiteral("mh"), 10);
    row.insert(QStringLiteral("hp"), hp);
    row.insert(QStringLiteral("baby"), false);
    row.insert(QStringLiteral("grow"), 0.0f);
    row.insert(QStringLiteral("wt"), false);
    row.insert(QStringLiteral("ws"), false);
    row.insert(QStringLiteral("ot"), false);
    row.insert(QStringLiteral("os"), false);
    row.insert(QStringLiteral("ov"), 0);
    row.insert(QStringLiteral("sw"), 0);
    row.insert(QStringLiteral("swd"), false);
    row.insert(QStringLiteral("sh"), false);
    row.insert(QStringLiteral("ss"), 1);
    row.insert(QStringLiteral("sd"), false);
    return row;
}

} // namespace

void MatrixRun::section96_entity_persist_t1133()
{
    // ── r2103a:导出门与行形柱（导出域 = 活体 Mob 全档快照）────────────────────────────────
    //   死亡不入档（致死伤害后 dead 门生效）+ 非 Mob kind 不入档（FallingBlock/Arrow 会话瞬态）
    //   + 行形（持久字段键集齐 + AI 态零键）+ 活体字段逐位进载荷（驯服/坐下/血量/羊毛/鞍/档）。
    runLeg("r2103a export gate and row shape column (the entity exporter answers one row per"
        " living mob and none for dead ones or falling blocks or arrows, every row carries the"
        " persisted field set and no ai timer keys, and the live values ride the rows verbatim"
        " for the tamed and sitting wolf and the damaged pig and the sheared dyed sheep and the"
        " saddled pig and the sized slime)",
        [&]() {
        bool ok = true;
        QString diag;
        EntityManager em;
        const int pig = em.spawnMobTyped(4, 41, 4, EntityManager::MobPig, QString(), 10);
        const int wolf = em.spawnMobTyped(6, 41, 4, EntityManager::MobWolf, QString(), 10);
        const int sheep = em.spawnMobTyped(8, 41, 4, EntityManager::MobSheep, QString(), 10);
        const int saddlePigSlot = em.spawnMobTyped(10, 41, 4, EntityManager::MobPig, QString(), 10);
        const int cat = em.spawnMobTyped(12, 41, 4, EntityManager::MobOcelot, QString(), 10);
        const int villager = em.spawnMobTyped(14, 41, 4, EntityManager::MobVillager, QString(), 10);
        const int slime = em.spawnSlime(16, 41, 4, 4);
        em.spawnFallingBlock(4, 45, 4, int(BR::Sand)); // 非 Mob kind → 不入档
        em.spawnArrow(QVector3D(4.5f, 41.5f, 6.5f), QVector3D(1, 0, 0)); // 投射物 → 不入档
        const bool rigOk = pig >= 0 && wolf >= 0 && sheep >= 0 && saddlePigSlot >= 0
            && cat >= 0 && villager >= 0 && slime >= 0;
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig]");
        // 驯服 + 坐下（缝接管必成）+ 染色 + 剪毛 + 鞍 + 半血。（染色在剪毛前——dyeSheep 染即长毛
        //   清 sheared，先剪后染会把剪毛面洗掉；持久面想要 sh+swd+sw 三值同真 = 染后剪。）
        em.setTameRollOverride(0);
        const bool tamed = em.tameWolf(wolf) && em.wolfTamedAt(wolf);
        em.toggleWolfSit(wolf);
        const bool dyed = em.dyeSheep(sheep, 6);
        em.shearSheep(sheep);
        em.saddlePig(saddlePigSlot);
        em.damageEntity(pig, 4); // 10 → 6（半血持久面）
        ok = ok && tamed && dyed && em.wolfSittingAt(wolf) && em.shearedAt(sheep)
            && em.saddledAt(saddlePigSlot) && em.healthAt(pig) == 6;
        if (!(tamed && dyed)) diag += QStringLiteral("[setup]");
        // 致死一只（死亡动画窗内 dead=true 槽仍活）→ 导出排除（死亡不入档 era 同门）。
        const int goat = em.spawnMobTyped(18, 41, 4, EntityManager::MobCow, QString(), 10);
        em.damageEntity(goat, 999);
        const QVariantList rows = em.exportPersistedEntities();
        const bool gateOk = em.aliveAt(goat) && em.deadAt(goat)
            && rows.size() == 7; // 7 活体 Mob（山羊 dead 排除；FallingBlock/Arrow 非 Mob 排除）
        ok = ok && gateOk;
        if (!gateOk)
            diag += QStringLiteral("[gate alive=%1 dead=%2 rows=%3]")
                        .arg(em.aliveAt(goat)).arg(em.deadAt(goat)).arg(rows.size());
        // 行形：持久键集齐 + AI 态零键（临时面不入档——交接单持久 vs 临时分界）。
        bool shapeOk = true;
        const QStringList mustKeys = { QStringLiteral("kind"), QStringLiteral("type"),
            QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"),
            QStringLiteral("color"), QStringLiteral("mh"), QStringLiteral("hp"),
            QStringLiteral("baby"), QStringLiteral("grow"), QStringLiteral("wt"),
            QStringLiteral("ws"), QStringLiteral("ot"), QStringLiteral("os"),
            QStringLiteral("ov"), QStringLiteral("sw"), QStringLiteral("swd"),
            QStringLiteral("sh"), QStringLiteral("ss"), QStringLiteral("sd") };
        const QStringList banKeys = { QStringLiteral("wanderTimer"), QStringLiteral("chaseTimer"),
            QStringLiteral("panicTimer"), QStringLiteral("fuseTimer"), QStringLiteral("hurtFlash"),
            QStringLiteral("enrageTimer") };
        for (const QVariant &v : rows) {
            const QVariantMap rm = v.toMap();
            for (const QString &k : mustKeys)
                if (!rm.contains(k)) shapeOk = false;
            for (const QString &k : banKeys)
                if (rm.contains(k)) shapeOk = false;
        }
        ok = ok && shapeOk;
        if (!shapeOk) diag += QStringLiteral("[shape]");
        // 活体值逐位进载荷（首行序 = 槽序；按 type 检索行断言）。
        auto rowByType = [&rows](int type) -> QVariantMap {
            for (const QVariant &v : rows) {
                const QVariantMap rm = v.toMap();
                if (rm.value(QStringLiteral("type")).toInt() == type)
                    return rm;
            }
            return QVariantMap();
        };
        const QVariantMap rw = rowByType(EntityManager::MobWolf);
        const QVariantMap rp = rowByType(EntityManager::MobPig);
        const QVariantMap rs = rowByType(EntityManager::MobSheep);
        const QVariantMap rsl = rowByType(EntityManager::MobSlime);
        bool anySaddledPig = false;
        for (const QVariant &v : rows) {
            const QVariantMap rm = v.toMap();
            if (rm.value(QStringLiteral("type")).toInt() == EntityManager::MobPig
                && rm.value(QStringLiteral("sd")).toBool())
                anySaddledPig = true;
        }
        const bool v1 = rw.value(QStringLiteral("wt")).toBool()
            && rw.value(QStringLiteral("ws")).toBool();
        const bool v2 = rp.value(QStringLiteral("hp")).toInt() == 6
            && rp.value(QStringLiteral("kind")).toInt() == 0;
        const bool v3 = rs.value(QStringLiteral("sh")).toBool() && rs.value(QStringLiteral("swd")).toBool()
            && rs.value(QStringLiteral("sw")).toInt() == 6;
        const bool v4 = rsl.value(QStringLiteral("ss")).toInt() == 4 && anySaddledPig
            && rowByType(EntityManager::MobVillager).value(QStringLiteral("sd")).toInt() == 0;
        const bool valOk = v1 && v2 && v3 && v4;
        ok = ok && valOk;
        if (!valOk)
            diag += QStringLiteral("[val wt=%1 ws=%2 hp=%3 sh=%4 swd=%5 sw=%6 ss=%7 sad=%8]")
                        .arg(rw.value(QStringLiteral("wt")).toBool())
                        .arg(rw.value(QStringLiteral("ws")).toBool())
                        .arg(rp.value(QStringLiteral("hp")).toInt())
                        .arg(rs.value(QStringLiteral("sh")).toBool())
                        .arg(rs.value(QStringLiteral("swd")).toBool())
                        .arg(rs.value(QStringLiteral("sw")).toInt())
                        .arg(rsl.value(QStringLiteral("ss")).toInt())
                        .arg(anySaddledPig);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2103a export gate and row shape column (the entity exporter answers one row"
               " per living mob and none for dead ones or falling blocks or arrows, every row"
               " carries the persisted field set and no ai timer keys, and the live values ride"
               " the rows verbatim for the tamed and sitting wolf and the damaged pig and the"
               " sheared dyed sheep and the saddled pig and the sized slime)"
            << (ok ? QString() : diag);
    });

    // ── r2103b:恢复恒等 + 防御门 + 容量柱（EntityManager 直驱）────────────────────────────
    //   行 → fresh 管理器恢复逐字段恒等（驯服/坐下/血量/变体/毛色/档/鞍/位置全精度）+ 防御门
    //   （空行 / kind 越界 / type 越界 / NaN 坐标 / hp<=0 各自跳过不崩）+ kCap 容量（满槽即停，
    //   剩余行不恢复，返回值如实计数）。
    runLeg("r2103b restore identity and defensive gate column (a fresh manager replays persisted"
        " rows into per field identity for the tamed sitting wolf and the half health pig and"
        " the variant cat and the wool sheep and the sized slime at full precision positions,"
        " skips empty rows and out of range kinds and out of range types and nan coordinates"
        " and non positive health without crashing, and stops at the slot cap leaving the"
        " remainder unrestored with an honest return count)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 源管理器导出 → 恢复恒等。
        EntityManager src;
        src.setTameRollOverride(0);
        const int sw = src.spawnMobTyped(4, 41, 4, EntityManager::MobWolf, QString(), 10);
        const int sp = src.spawnMobTyped(6, 41, 4, EntityManager::MobPig, QString(), 10);
        const int sc = src.spawnMobTyped(8, 41, 4, EntityManager::MobOcelot, QString(), 10);
        const int ss = src.spawnMobTyped(10, 41, 4, EntityManager::MobSheep, QString(), 10);
        const int ssl = src.spawnSlime(12, 41, 4, 2);
        const bool wolfTamed = sw >= 0 && src.tameWolf(sw);
        // 豹猫驯服无缝接管（tameOcelot 直掷 RNG，m_tameRollOverride 不覆盖）→ 有界重试
        //   （每次 1/3 成，64 试失败率 ~1e-12，低于既有 flake 名单门槛）；变体随机 0..2，
        //   恢复恒等按「记录源值 → 比对恢复值」断言。
        bool catTamed = false;
        for (int attempt = 0; attempt < 64 && !catTamed; ++attempt)
            catTamed = sc >= 0 && src.tameOcelot(sc);
        const bool rigOk = sw >= 0 && sp >= 0 && sc >= 0 && ss >= 0 && ssl >= 0
            && wolfTamed && catTamed;
        src.toggleWolfSit(sw);
        src.damageEntity(sp, 1); // 10 → 9（轻伤持久面）
        src.dyeSheep(ss, 9);
        src.shearSheep(ss);
        ok = ok && rigOk;
        if (!rigOk) diag += QStringLiteral("[rig]");
        src.saddlePig(sp);
        const QVector3D wolfPos = src.posAt(sw);
        const int catVariant = src.ocelotVariantAt(sc);
        const QVariantList rows = src.exportPersistedEntities();
        EntityManager dst;
        const int n = dst.restorePersistedEntities(rows);
        const bool idOk = n == rows.size() && rows.size() == 5;
        // 按 type 检索恢复体断言恒等（恢复序 = 行序 → 槽 0..4）。
        bool fieldOk = true;
        for (int i = 0; i < n; ++i) {
            const QVariantMap rm = rows.at(i).toMap();
            const int type = rm.value(QStringLiteral("type")).toInt();
            if (dst.mobTypeAt(i) != type || dst.kindAt(i) != 0) fieldOk = false;
            const QVector3D p = dst.posAt(i);
            if (std::abs(p.x() - float(rm.value(QStringLiteral("x")).toDouble())) > 1e-4f
                || std::abs(p.y() - float(rm.value(QStringLiteral("y")).toDouble())) > 1e-4f
                || std::abs(p.z() - float(rm.value(QStringLiteral("z")).toDouble())) > 1e-4f)
                fieldOk = false;
            if (dst.healthAt(i) != rm.value(QStringLiteral("hp")).toInt()
                || dst.maxHealthAt(i) != rm.value(QStringLiteral("mh")).toInt())
                fieldOk = false;
        }
        int wolfSlot = -1, catSlot = -1, sheepSlot = -1, slimeSlot = -1, pigSlot = -1;
        for (int i = 0; i < dst.count(); ++i) {
            if (!dst.aliveAt(i)) continue;
            const int t = dst.mobTypeAt(i);
            if (t == EntityManager::MobWolf) wolfSlot = i;
            else if (t == EntityManager::MobOcelot) catSlot = i;
            else if (t == EntityManager::MobSheep) sheepSlot = i;
            else if (t == EntityManager::MobSlime) slimeSlot = i;
            else if (t == EntityManager::MobPig) pigSlot = i;
        }
        const bool persOk = wolfSlot >= 0 && dst.wolfTamedAt(wolfSlot) && dst.wolfSittingAt(wolfSlot)
            && std::abs(dst.posAt(wolfSlot).x() - wolfPos.x()) < 1e-4f
            && catSlot >= 0 && dst.ocelotTamedAt(catSlot)
            && dst.ocelotVariantAt(catSlot) == catVariant
            && sheepSlot >= 0 && dst.shearedAt(sheepSlot) && dst.sheepWoolAt(sheepSlot) == 9
            && slimeSlot >= 0 && dst.slimeSizeAt(slimeSlot) == 2
            && pigSlot >= 0 && dst.saddledAt(pigSlot) && dst.healthAt(pigSlot) == 9;
        ok = ok && idOk && fieldOk && persOk;
        if (!(idOk && fieldOk && persOk))
            diag += QStringLiteral("[ident n=%1 field=%2 pers=%3]").arg(n).arg(fieldOk).arg(persOk);
        // (2) 防御门：空行 / kind 越界 / type 越界 / NaN 坐标 / hp<=0 各自跳过（恰 2 合法行恢复）。
        EntityManager guard;
        QVariantList bad;
        bad.append(QVariantMap());                                           // 空行
        QVariantMap badKind = makeEntityRow(1, 1, 41, 1, 10);
        badKind.insert(QStringLiteral("kind"), 7);                           // kind 越界（Arrow）
        bad.append(badKind);
        bad.append(makeEntityRow(99, 1, 41, 1, 10));                         // type 越界
        QVariantMap nanRow = makeEntityRow(1, 0, 41, 0, 10);
        nanRow.insert(QStringLiteral("x"), double(std::numeric_limits<float>::quiet_NaN()));
        bad.append(nanRow);                                                  // NaN 坐标
        bad.append(makeEntityRow(1, 1, 41, 1, 0));                           // hp<=0（死亡面）
        bad.append(makeEntityRow(1, 2, 41, 2, 10));                          // 合法
        bad.append(makeEntityRow(2, 3, 41, 3, 8));                           // 合法
        const int gn = guard.restorePersistedEntities(bad);
        int guardLive = 0;
        for (int i = 0; i < guard.count(); ++i)
            if (guard.aliveAt(i)) ++guardLive;
        const bool defOk = gn == 2 && guardLive == 2
            && guard.mobTypeAt(0) == 1 && guard.mobTypeAt(1) == 2;
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[defend n=%1 live=%2]").arg(gn).arg(guardLive);
        // (3) 容量：满槽前恢复即停（kCap=64；63 预填 + 3 行 → 恰 1 恢复，剩余不进）。
        EntityManager cap;
        for (int i = 0; i < 63; ++i)
            cap.spawnMobTyped(20 + (i % 8), 41, 20 + (i % 8), EntityManager::MobPig, QString(), 10);
        QVariantList trio;
        trio.append(makeEntityRow(1, 30, 41, 30, 10));
        trio.append(makeEntityRow(1, 31, 41, 31, 10));
        trio.append(makeEntityRow(2, 32, 41, 32, 10));
        const int cn = cap.restorePersistedEntities(trio);
        const bool capOk = cn == 1 && cap.liveCount() == 64;
        ok = ok && capOk;
        if (!capOk) diag += QStringLiteral("[cap n=%1 live=%2]").arg(cn).arg(cap.liveCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2103b restore identity and defensive gate column (a fresh manager replays"
               " persisted rows into per field identity for the tamed sitting wolf and the half"
               " health pig and the variant cat and the wool sheep and the sized slime at full"
               " precision positions, skips empty rows and out of range kinds and out of range"
               " types and nan coordinates and non positive health without crashing, and stops"
               " at the slot cap leaving the remainder unrestored with an honest return count)"
            << (ok ? QString() : diag);
    });

    // ── r2103c:统一保存链真链往返柱（NEG-1 敏感腿 = coordinator 步⑥b 实体段调用行）──────────
    //   繁殖缝产幼崽（t1025 缝同门）+ 驯狼坐下 + 半血羊 → export → SaveCoordinator 统一保存（真
    //   临时库）→ 关库重开 → loadEntities → fresh 管理器恢复：仍驯仍坐 / 幼体与成长保持 / 血量
    //   保持 + 幂等二次存。
    runLeg("r2103c unified save chain round trip column (a bred baby sheep with its grow clock"
        " and a tamed sitting wolf and a damaged sheep ride the coordinator save onto a real"
        " temp database inside the world transaction, a fresh store and manager after reopen"
        " restore still tamed and still sitting and still a baby with its grow clock and the"
        " damaged health, and a second save of the same snapshot stays idempotent)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString db = entityPersistTempDb("rt");
        QFile::remove(db);
        // 繁殖 rig（t1025 同门：石板地板 + 工作体积净空——未 regenerate 的 World 带 fBm 地形，
        // 不清空则 spawn 即嵌入 + 窒息扣血 = 幼崽腿全红根因）。
        World w;
        w.setWidth(32);
        w.setDepth(32);
        w.setHeight(24);
        w.setSeed(1029);
        for (int x = 8; x <= 24; ++x)
            for (int z = 8; z <= 24; ++z)
                w.setBlock(x, 10, z, BR::Stone, 0);
        for (int x = 8; x <= 24; ++x)
            for (int z = 8; z <= 24; ++z)
                for (int y = 11; y <= 16; ++y)
                    w.setBlock(x, y, z, BR::Air, 0);
        EntityManager src;
        src.setTameRollOverride(0);
        src.setWanderFrozen(true); // 确定性缝：走查 RNG 不进配对窗（t1029 先例）
        const int sa = src.spawnMobTyped(14, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int sb = src.spawnMobTyped(15, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int wolf = src.spawnMobTyped(16, 11, 15, EntityManager::MobWolf, QString(), 10);
        const bool fed = sa >= 0 && sb >= 0 && wolf >= 0
            && src.enterLoveMode(sa) && src.enterLoveMode(sb);
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        for (int t = 0; t < 24; ++t)
            src.tick(0.016f, &w, farL, 0.3f, 1.8f, false);
        int baby = -1;
        for (int i = 0; i < src.count(); ++i)
            if (src.aliveAt(i) && src.isBabyAt(i) && src.mobTypeAt(i) == EntityManager::MobSheep)
                baby = i;
        const bool tamed = src.tameWolf(wolf);
        src.toggleWolfSit(wolf);
        src.damageEntity(sa, 4); // 半血成体羊
        const QVariantList rows = src.exportPersistedEntities();
        const bool rigOk = fed && baby >= 0 && tamed && src.wolfSittingAt(wolf)
            && rows.size() == 4; // 成体×2 + 幼崽 + 驯狼
        ok = ok && rigOk;
        if (!rigOk)
            diag += QStringLiteral("[rig fed=%1 baby=%2 tamed=%3 rows=%4]")
                        .arg(fed).arg(baby).arg(tamed).arg(rows.size());
        const float babyGrow = baby >= 0 ? src.growTimerAt(baby) : -1.0f;
        // 统一保存链（真协调层 + 真临时库；SaveRequest.entities 载荷 = 生产 runExitSave 同形）。
        WorldStore store;
        const bool open1 = store.openWorld(db);
        store.setWorld(&w);
        SaveCoordinator coord;
        SaveRequest req;
        req.name = QStringLiteral("entworld");
        req.entities = rows;
        coord.bind(&store, db);
        const SaveReceipt r1 = coord.saveAll(req);
        store.closeWorld();
        // 关库重开（fresh store）→ 读面 → fresh 管理器恢复。
        const bool open2 = store.openWorld(db);
        store.setWorld(&w);
        const QVariantList back = store.loadEntities();
        EntityManager dst;
        const int n = dst.restorePersistedEntities(back);
        int dWolf = -1, dBaby = -1, dSheepA = -1;
        for (int i = 0; i < dst.count(); ++i) {
            if (!dst.aliveAt(i)) continue;
            const int t = dst.mobTypeAt(i);
            if (t == EntityManager::MobWolf) dWolf = i;
            else if (t == EntityManager::MobSheep) {
                if (dst.isBabyAt(i)) dBaby = i;
                else if (dSheepA < 0) dSheepA = i; // 首只成体 = 行序首（= 被扣血的 sa），防后只覆盖
            }
        }
        const bool rtOk = open1 && open2 && r1.ok()
            && back.size() == rows.size() && n == 4
            && dWolf >= 0 && dst.wolfTamedAt(dWolf) && dst.wolfSittingAt(dWolf)
            && dBaby >= 0 && dst.growTimerAt(dBaby) > 1199.0f
            && std::abs(dst.growTimerAt(dBaby) - babyGrow) < 1.0f
            && dSheepA >= 0 && dst.healthAt(dSheepA) == 6;
        ok = ok && rtOk;
        if (!rtOk)
            diag += QStringLiteral("[rt open=%1/%2 ok=%3 back=%4 n=%5 wolf=%6 baby=%7 grow=%8]")
                        .arg(open1).arg(open2).arg(r1.ok()).arg(back.size()).arg(n)
                        .arg(dWolf).arg(dBaby).arg(dBaby >= 0 ? dst.growTimerAt(dBaby) : -1.0f);
        // 幂等二次存（同快照再存 → 再开读回逐位同——t2102c 同门口径）。
        SaveRequest req2;
        req2.name = QStringLiteral("entworld");
        req2.entities = rows;
        coord.bind(&store, db);
        const SaveReceipt r2 = coord.saveAll(req2);
        store.closeWorld();
        const bool open3 = store.openWorld(db);
        store.setWorld(&w);
        const QVariantList back2 = store.loadEntities();
        store.closeWorld();
        const bool idemOk = r2.ok() && open3 && back2.size() == rows.size();
        ok = ok && idemOk;
        if (!idemOk) diag += QStringLiteral("[idem]");
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2103c unified save chain round trip column (a bred baby sheep with its grow"
               " clock and a tamed sitting wolf and a damaged sheep ride the coordinator save"
               " onto a real temp database inside the world transaction, a fresh store and"
               " manager after reopen restore still tamed and still sitting and still a baby"
               " with its grow clock and the damaged health, and a second save of the same"
               " snapshot stays idempotent)"
            << (ok ? QString() : diag);
    });

    // ── r2103d:旧档兼容 + 隔离 + 死亡不复活柱（真临时库三面）──────────────────────────────
    //   手工最小库无 entities 表（r2102c(3) 同门）→ 幂等补建 → 空集读入不丢不崩 + 恢复零注入 →
    //   新码收敛存取；跨世界隔离（第二库无行）；死亡后保存不复活（致死快照恰 N-1 行）。
    runLeg("r2103d legacy compatibility and isolation and death face column (a hand built legacy"
        " library without the entities table opens through the additive schema and reads an"
        " empty set without loss or crash and converges when the new code saves onto it, a"
        " second world database answers no rows, and saving after a death keeps the dead mob"
        " out of the snapshot so no reload resurrects it)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 旧档兼容：手工最小库（user_version 0 + 仅 world_meta）→ 新码开库幂等补表 → 空读入。
        const QString db = entityPersistTempDb("legacy");
        QFile::remove(db);
        const QString conn = QStringLiteral("r2103d_%1").arg(QCoreApplication::applicationPid());
        bool built = false;
        {
            if (QSqlDatabase::contains(conn))
                QSqlDatabase::removeDatabase(conn);
            QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            p.setDatabaseName(db);
            if (p.open()) {
                QSqlQuery q(p);
                built = q.exec(QStringLiteral(
                    "CREATE TABLE world_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))
                    && q.exec(QStringLiteral(
                        "INSERT INTO world_meta (key, value) VALUES ('name', 'legacy-ent')"));
            }
        }
        QSqlDatabase::removeDatabase(conn);
        World w;
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(84);
        WorldStore store;
        const bool open1 = store.openWorld(db);
        store.setWorld(&w);
        const QVariantList empty = store.loadEntities();
        EntityManager m0;
        const int r0 = m0.restorePersistedEntities(empty); // 空集恢复零注入（自然初生面照旧）
        ok = ok && built && open1 && empty.isEmpty() && r0 == 0;
        if (!(built && open1 && empty.isEmpty() && r0 == 0))
            diag += QStringLiteral("[legacy built=%1 open=%2 rows=%3 r=%4]")
                        .arg(built).arg(open1).arg(empty.size()).arg(r0);
        // 新码收敛：协调层存 2 行 → 再开 → 读回复原。
        QVariantList duo;
        duo.append(makeEntityRow(1, 5, 41, 5, 10));
        duo.append(makeEntityRow(3, 7, 41, 7, 9));
        SaveCoordinator coord;
        SaveRequest req;
        req.name = QStringLiteral("legacy-ent");
        req.entities = duo;
        coord.bind(&store, db);
        const SaveReceipt rs = coord.saveAll(req);
        store.closeWorld();
        const bool open2 = store.openWorld(db);
        store.setWorld(&w);
        const QVariantList back = store.loadEntities();
        store.closeWorld();
        ok = ok && rs.ok() && open2 && back.size() == 2
            && back.at(0).toMap().value(QStringLiteral("type")).toInt() == 1
            && back.at(1).toMap().value(QStringLiteral("hp")).toInt() == 9;
        if (!(rs.ok() && open2 && back.size() == 2))
            diag += QStringLiteral("[converge ok=%1 open=%2 rows=%3]")
                        .arg(rs.ok()).arg(open2).arg(back.size());
        QFile::remove(db);
        // (2) 跨世界隔离（第二库无行 → 空读入）。
        {
            const QString db2 = entityPersistTempDb("iso");
            QFile::remove(db2);
            WorldStore store2;
            const bool openB = store2.openWorld(db2);
            const QVariantList none = store2.loadEntities();
            const bool isoOk = openB && none.isEmpty();
            ok = ok && isoOk;
            if (!isoOk)
                diag += QStringLiteral("[iso open=%1 rows=%2]").arg(openB).arg(none.size());
            store2.closeWorld();
            QFile::remove(db2);
        }
        // (3) 死亡不复活：两只活体致死一只 → 快照恰 1 行 → 恢复恰 1 只（死者不在档）。
        EntityManager src;
        const int live = src.spawnMobTyped(4, 41, 4, EntityManager::MobPig, QString(), 10);
        const int doomed = src.spawnMobTyped(6, 41, 4, EntityManager::MobCow, QString(), 10);
        src.damageEntity(doomed, 999);
        const QVariantList rows = src.exportPersistedEntities();
        const bool deathGate = src.aliveAt(doomed) && rows.size() == 1
            && rows.at(0).toMap().value(QStringLiteral("type")).toInt() == EntityManager::MobPig;
        const QString db3 = entityPersistTempDb("death");
        QFile::remove(db3);
        WorldStore store3;
        const bool open3 = store3.openWorld(db3);
        store3.setWorld(&w);
        SaveCoordinator coord3;
        SaveRequest req3;
        req3.name = QStringLiteral("deathface");
        req3.entities = rows;
        coord3.bind(&store3, db3);
        const SaveReceipt r3 = coord3.saveAll(req3);
        store3.closeWorld();
        const bool open4 = store3.openWorld(db3);
        store3.setWorld(&w);
        const QVariantList back3 = store3.loadEntities();
        EntityManager dst;
        const int n3 = dst.restorePersistedEntities(back3);
        store3.closeWorld();
        QFile::remove(db3);
        Q_UNUSED(live);
        const bool deathOk = deathGate && open3 && open4 && r3.ok()
            && back3.size() == 1 && n3 == 1 && dst.mobTypeAt(0) == EntityManager::MobPig;
        ok = ok && deathOk;
        if (!deathOk)
            diag += QStringLiteral("[death gate=%1 open=%2/%3 ok=%4 rows=%5 n=%6]")
                        .arg(deathGate).arg(open3).arg(open4).arg(r3.ok())
                        .arg(back3.size()).arg(n3);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2103d legacy compatibility and isolation and death face column (a hand built"
               " legacy library without the entities table opens through the additive schema and"
               " reads an empty set without loss or crash and converges when the new code saves"
               " onto it, a second world database answers no rows, and saving after a death"
               " keeps the dead mob out of the snapshot so no reload resurrects it)"
            << (ok ? QString() : diag);
    });

    // ── r2103e:接线钉 + 结构钉族柱（NEG 双摘面豁免不钉——r2102 同门）────────────────────────
    //   Main.qml 恢复/门/保存三行 + worldstore 表/写体/读面 + SaveRequest 载荷字段 + 桥透传 +
    //   CMake 段行 + EntityManager 双声明 + era 锚注（raw 含）+ 跨任务词元零命中 + QML 玩法路径
    //   零迁移负面门复钉。
    runLeg("r2103e wiring and structure pin family column (the qml entry loads the entity rows"
        " and injects the restore call and gates the natural entry spawns behind a zero"
        " restore count and the exit save passes the exported snapshot as the fifteenth save"
        " argument, the world store carries the table and the writer body and the loader face,"
        " the request payload and the bridge pass the entity list through, the manager faces"
        " and the era adjudication anchors and the candidate pool note are on file, the cmake"
        " section row is registered, the filter token stays out of the source tree, and the"
        " gameplay path literals stay out of the qml)",
        [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForEntityPersistPins();
        // (E1) Main.qml 四行（NEG-2 摘恢复行 → 本腿恰红；恢复行在钉 = 豁免逻辑反转说明：
        //   NEG-2 靶 = 恢复注入行，本腿钉它 = NEG-2 的红面载体——t1132 的豁免纪律针对
        //   「NEG 摘 C++ 行为行」场景，QML 行无行为腿可红，红面即本钉腿本身）。
        const QStringList missQml = pinSet(srcDir + QStringLiteral("/ui/Main.qml"), {
            SrcPin("entry load row", "worldStore.loadEntities()", 1),
            SrcPin("entry restore row",
                   "entityManager.restorePersistedEntities(persistedEntityRows)", 1),
            SrcPin("entry natural gate row", "if (restoredEntityCount === 0) {", 1),
            SrcPin("exit save row", "entityManager.exportPersistedEntities())", 1)});
        ok = ok && missQml.isEmpty();
        if (!missQml.isEmpty())
            diag += QStringLiteral("[qml %1]").arg(missQml.join(QLatin1Char(',')));
        // (E2) worldstore 三面 + SaveRequest 载荷 + 桥透传（NEG-1 靶 = coordinator 调用行，豁免不钉）。
        const QStringList missWsH = pinSet(srcDir + QStringLiteral("/World/worldstore.h"), {
            SrcPin("loader decl", "Q_INVOKABLE QVariantList loadEntities() const;", 1),
            SrcPin("writer decl", "bool writeEntitiesPart(const QVariantList &entities);", 1)});
        ok = ok && missWsH.isEmpty();
        if (!missWsH.isEmpty())
            diag += QStringLiteral("[wsH %1]").arg(missWsH.join(QLatin1Char(',')));
        const QStringList missWsC = pinSet(srcDir + QStringLiteral("/World/worldstore.cpp"), {
            SrcPin("table row", "CREATE TABLE IF NOT EXISTS entities (", 1),
            SrcPin("writer head", "bool WorldStore::writeEntitiesPart(const QVariantList &entities)", 1),
            SrcPin("loader head", "QVariantList WorldStore::loadEntities() const", 1)});
        ok = ok && missWsC.isEmpty();
        if (!missWsC.isEmpty())
            diag += QStringLiteral("[wsC %1]").arg(missWsC.join(QLatin1Char(',')));
        const QStringList missScH = pinSet(srcDir + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("payload field", "QVariantList entities;", 1)});
        ok = ok && missScH.isEmpty();
        if (!missScH.isEmpty())
            diag += QStringLiteral("[scH %1]").arg(missScH.join(QLatin1Char(',')));
        const QStringList missSb = pinSet(srcDir + QStringLiteral("/Game/savebridge.cpp"), {
            SrcPin("bridge pass row", "req.entities = entities;", 1)});
        ok = ok && missSb.isEmpty();
        if (!missSb.isEmpty())
            diag += QStringLiteral("[sb %1]").arg(missSb.join(QLatin1Char(',')));
        const QStringList missCm = pinSet(QCoreApplication::applicationDirPath()
                                              + QStringLiteral("/../CMakeLists.txt"),
                                          {
                                              SrcPin("cmake section row",
                                                     "tools/matrix/section96_entity_persist_t1133.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // (E3) EntityManager 双声明 + era 锚注（raw 含——注释体锚）+ 候选池登记注。
        const QStringList missEmH = pinSet(srcDir + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("export decl", "Q_INVOKABLE QVariantList exportPersistedEntities() const;", 1),
            SrcPin("restore decl", "Q_INVOKABLE int restorePersistedEntities(const QVariantList &rows);", 1)});
        ok = ok && missEmH.isEmpty();
        if (!missEmH.isEmpty())
            diag += QStringLiteral("[emH %1]").arg(missEmH.join(QLatin1Char(',')));
        const bool anchorOk = rawContainsEntityPersist(
                                  srcDir + QStringLiteral("/Entities/entitymanager.h"),
                                  QStringLiteral("ENTITY-01"))
            && rawContainsEntityPersist(srcDir + QStringLiteral("/Entities/entitymanager.h"),
                                        QStringLiteral("Sitting"))
            && rawContainsEntityPersist(srcDir + QStringLiteral("/Entities/entitymanager.h"),
                                        QStringLiteral("候选池面"))
            && rawContainsEntityPersist(srcDir + QStringLiteral("/Entities/entitymanager.cpp"),
                                        QStringLiteral("死亡不入档"));
        ok = ok && anchorOk;
        if (!anchorOk) diag += QStringLiteral("[anchor]");
        // (E4) 跨任务词元零命中（filter 词只落矩阵域——src/ 全树零 r2103）。
        {
            bool leaked = false;
            QDirIterator it(srcRootForEntityPersistPins(), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (rawContainsEntityPersist(it.next(), QStringLiteral("r2103"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // (E5) QML 玩法路径零迁移负面门（r2090d/r2102e 同门复钉——Main.qml 零新桥字面）。
        {
            const QString qml = srcDir + QStringLiteral("/ui/Main.qml");
            const bool qmlGate = !rawContainsEntityPersist(qml, QStringLiteral("GameSession"))
                && !rawContainsEntityPersist(qml, QStringLiteral("MeshWorker"))
                && !rawContainsEntityPersist(qml, QStringLiteral("setChunkLifecycle"))
                && !rawContainsEntityPersist(qml, QStringLiteral("ChunkEvictor"))
                && !rawContainsEntityPersist(qml, QStringLiteral("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2103e wiring and structure pin family column (the qml entry loads the entity"
               " rows and injects the restore call and gates the natural entry spawns behind a"
               " zero restore count and the exit save passes the exported snapshot as the"
               " fifteenth save argument, the world store carries the table and the writer body"
               " and the loader face, the request payload and the bridge pass the entity list"
               " through, the manager faces and the era adjudication anchors and the candidate"
               " pool note are on file, the cmake section row is registered, the filter token"
               " stays out of the source tree, and the gameplay path literals stay out of the"
               " qml)"
            << (ok ? QString() : diag);
    });
}
