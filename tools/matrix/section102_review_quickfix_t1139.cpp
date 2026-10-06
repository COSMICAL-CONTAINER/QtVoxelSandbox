#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：统一保存协议（t1129 段同门——r2109a 走真协调层链）

#include <QElapsedTimer>
#include <QEventLoop>

// t1139 第三轮外审快修批探针段（4 腿 r2109a-d；filter 词 "r2109"；矩阵 981→985）。置尾先例
// 沿用（接 section101，runAll 末执行）；fresh 小世界 + 临时 SQLite 库（pid 键名，测试自清理）
// + 真链 pc rig。交付三面（审查交接单编号 A04 / A06 / A07 溯源）：
//   「DELETE 故障柱」→ r2109a（真实临时库触发器注入 DELETE RAISE(ABORT,'injected')——纯测试
//     侧注入零生产缝；三场景 = 空载荷 / 键不重叠 / 键重叠，各验 receipt 失败 + 完成世代不前进
//     + 关库重开全表恰 A；解除故障重试 B = 与同世界原语存档逐行恒等（恰一次）+ 完成世代前进
//     ——三场景 receipt 失败面与旧实现同色的结构性根因 = sparse 中心格恒物化（A/B 键集恒共
//     含中心键 → DELETE 中止后 INSERT 恒撞 A 残行冲突自动回滚，审查原文「新旧主键重叠时
//     INSERT 通常冲突并触发回滚」判语 engine 侧坐实）；本柱钉修后行为面（receipt 失败 + A
//     完整 + 解除重试恰一次），DELETE 守卫行本体由 r2109d 钉）；
//   「地图五态柱」→ r2109b（同实例有效 A→坏尺寸→坏 revision→截断载荷→空 C 五态逐级零 A 残留
//     ——F06 坏键不再裸 return 保前世界内容；R01 数据集字节预算上界有界拒绝；存退重开隔离）；
//   「装瓶矩阵柱」→ r2109c（锅舀水与水源装瓶两面 × 数量 1/2/64 × 满包 / 合并槽——失败零消耗
//     零挥臂零水位动；成功一进一出；牛奶桶面审查明示不扩不在此柱）；
//   「结构钉族柱」→ r2109d（worldstore 五处补核行 + mapstore 上界算式行 + 装瓶守卫行 + 哨兵钉
//     166/213 不动 + NEG 双摘面行豁免不钉 + CMake 段行 + 源树 filter 词元零命中）。
// 恰红面设计（先于腿文；双手工 Edit 摘单行承重语句，编译绿）：
//   NEG-1 = 摘 playercontroller.cpp 水源装瓶早退守卫的 `return;` 单行（守卫行与消耗行原样
//     ——摘后 `if (!canFitStack)` 反转 = 满包反消耗 / 可容反拒）→ 恰红 = {r2109c}（水源面
//     满包两 case 瓶数变动面红 + 合并 case 反拒面红；锅面 case 幸存）。
//     【结构定谳留痕】DELETE 行的原 NEG 面（摘 worldstore 守卫内 return false）经实测不可
//     观察：见 r2109a 处结构性根因；DELETE 守卫降级为纵深防御面（r2109d 钉守卫行 + r2109a
//     钉三场景修后行为面）。r2109a/b/d 幸存（a/b 零涉装瓶链，d 钉行全数原样）。
//   NEG-2 = 摘 mapstore.cpp loadVariant 坏键分支 `clearAll();` 单行（守卫行与 return 原样）→
//     恰红 = {r2109b}（坏尺寸 / 坏 revision 两态保留 A 残留断言红；截断态与空态走另一 clearAll
//     行 = 幸存；R01 越界态走同一被摘行 = 红面一并承担——实测定谳：早退 return 直保 A（不走
//     账不平面），stB/stB2/r1 三子面全红）。r2109a/c/d 幸存。
// 词元纪律：腿名 / diag 零跨任务 filter 词元（本段注释中的族引用不进腿名）。
namespace {

// 临时库路径（section92/95 同门：QDir::temp() + pid + 腿标，测试自清理；绝对路径直用）。
inline QString r2109TempDb(const char *tag)
{
    return QDir::temp().absoluteFilePath(QStringLiteral("voxel_t1139_%1_%2.sqlite")
                                             .arg(QLatin1String(tag))
                                             .arg(QCoreApplication::applicationPid()));
}

// DELETE 故障注放（纯测试侧注入：触发器活在临时测试库 schema 内，零生产缝——审查 A04 验收
// 原文建议形）。on=true 置故障（BEFORE DELETE RAISE(ABORT)），on=false 解除。独立连接开-用-关。
inline bool r2109InjectDeleteFault(const QString &db, bool on)
{
    const QString conn = QStringLiteral("t1139_fault_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    bool ok = false;
    {
        QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        p.setDatabaseName(db);
        if (p.open()) {
            QSqlQuery q(p);
            ok = on ? q.exec(QStringLiteral(
                                  "CREATE TRIGGER t1139_no_del BEFORE DELETE ON chunks BEGIN"
                                  " SELECT RAISE(ABORT, 'injected'); END;"))
                    : q.exec(QStringLiteral("DROP TRIGGER IF EXISTS t1139_no_del"));
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// chunks 表 raw 直读（独立临时连接；按 cx,cz 定序——A/B 内容逐行比对的确定性序）。
struct R2109ChunkRow
{
    int cx = 0;
    int cz = 0;
    QByteArray voxels;
};

inline QList<R2109ChunkRow> r2109ReadChunks(const QString &db)
{
    QList<R2109ChunkRow> out;
    const QString conn = QStringLiteral("t1139_raw_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    {
        QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        p.setDatabaseName(db);
        if (p.open()) {
            QSqlQuery q(p);
            if (q.exec(QStringLiteral("SELECT cx, cz, voxels FROM chunks ORDER BY cx, cz"))) {
                while (q.next())
                    out.append({ q.value(0).toInt(), q.value(1).toInt(),
                                 q.value(2).toByteArray() });
            }
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
}

// 行序逐行恒等（行数 + 键 + voxels 三面全比；序由读侧 ORDER BY 定死）。
inline bool r2109SameRows(const QList<R2109ChunkRow> &a, const QList<R2109ChunkRow> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].voxels != b[i].voxels)
            return false;
    return true;
}

// 原语存档（同世界 fresh 库零故障完整保存一次 → chunks 行集）——「重试恰一次」的逐行判据：
// 带故障史的库重试结果必须与同世界原语存档逐行恒等（多一行 / 少一行 / 残留旧行即不等）。
inline QList<R2109ChunkRow> r2109PristineSave(World &w, const char *tag)
{
    const QString db = r2109TempDb(tag);
    QFile::remove(db);
    WorldStore store;
    SaveCoordinator coord;
    store.openWorld(db);
    store.setWorld(&w);
    coord.bind(&store, db);
    SaveRequest req;
    req.name = QStringLiteral("r2109pristine");
    coord.saveAll(req);
    store.closeWorld();
    const QList<R2109ChunkRow> rows = r2109ReadChunks(db);
    QFile::remove(db);
    return rows;
}

// 事件泵（section95 同门：processEvents 墙钟泵；放置 CD 200ms 越窗用 260ms）。
inline void r2109Pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// sparse 世界定版（section95 同门 brace 构造缝口径：零预生成核心域）+ 单 chunk 物化 +
// 确定性标记列（显式 y=79 置石——与自然地形无关的逐字节可比分面）。
inline void r2109Sparse(World &w, int seed)
{
    w.setWidth(160);
    w.setDepth(160);
    w.setHeight(96);
    w.setSeed(seed);
}

inline void r2109Mark(World &w, int lx, int lz)
{
    w.setBlock(lx, 79, lz, quint8(BR::Stone), 0);
}

inline void r2109Load(World &w, int cx, int cz)
{
    w.loadChunkAt(cx, cz);
}

} // namespace

void MatrixRun::section102_review_quickfix_t1139()
{
    // ── r2109a:DELETE 故障柱（真实临时库触发器注入——三场景 receipt 失败 + 完成世代冻结 +
    //    关库重开全表恰 A；解除故障重试 B 与原语存档逐行恒等 + 完成世代前进）─────────────────
    //   链形（每场景独立临时库）：存 A（真协调层 → complete=1）→ 注入 DELETE 故障 → 存 B →
    //   receipt 失败 + 世代冻结 + raw 重读恰 A → 解除故障 → 重试存 B → 成功 + 世代前进收口 +
    //   raw 重读与 B 原语存档逐行恒等（恰一次）。
    runLeg("r2109a delete fault column (a real temporary database carries a before delete"
        " trigger that aborts so the unified save through the real coordinator fails at the"
        " chunks delete with the receipt failing and the complete generation frozen and the"
        " store rows answering exactly the previous world for the empty payload the disjoint"
        " keys and the overlapping keys scenarios, and after the fault is lifted the retry"
        " answers success with the generation advanced and rows identical to a pristine save"
        " of the same world)", [&]() {
        bool ok = true;
        QString diag;
        // 场景一：空载荷 B（B 世界仅中心格自物化——引擎结构面，见段头定谳留痕）。
        {
            const QString db = r2109TempDb("s1");
            QFile::remove(db);
            World wA;
            r2109Sparse(wA, 91);
            World wB;
            r2109Sparse(wB, 92);
            r2109Load(wA, 2, 2);
            r2109Mark(wA, 37, 40);
            r2109Mark(wA, 44, 33);
            const auto rowsP = r2109PristineSave(wB, "s1p");
            WorldStore store;
            SaveCoordinator coord;
            const bool openA = store.openWorld(db);
            store.setWorld(&wA);
            coord.bind(&store, db);
            SaveRequest ra;
            ra.name = QStringLiteral("r2109s1");
            const SaveReceipt rsa = coord.saveAll(ra);
            const auto rowsA = r2109ReadChunks(db);
            const SaveGenerationInfo genA = coord.recover();
            const bool faultOn = r2109InjectDeleteFault(db, true);
            store.setWorld(&wB);
            SaveRequest rb;
            rb.name = QStringLiteral("r2109s1b");
            const SaveReceipt rsb = coord.saveAll(rb);
            const SaveGenerationInfo genB = coord.recover();
            const auto rowsB = r2109ReadChunks(db);
            const bool faultOff = r2109InjectDeleteFault(db, false);
            SaveRequest rc;
            rc.name = QStringLiteral("r2109s1c");
            const SaveReceipt rsc = coord.saveAll(rc);
            const SaveGenerationInfo genC = coord.recover();
            const auto rowsC = r2109ReadChunks(db);
            const bool retryOnce = rsc.ok() && r2109SameRows(rowsC, rowsP)
                && genC.completeGeneration > genA.completeGeneration
                && genC.completeGeneration == genC.generation;
            const bool s1 = openA && rsa.ok() && !rowsA.isEmpty()
                && genA.completeGeneration == 1 && faultOn && !rsb.ok()
                && genB.completeGeneration == 1 && r2109SameRows(rowsB, rowsA) && faultOff
                && retryOnce;
            store.closeWorld();
            QFile::remove(db);
            ok = ok && s1;
            if (!s1)
                diag += QStringLiteral("[s1 open=%1 a=%2 n=%3 frz=%4 same=%5 once=%6]")
                            .arg(openA)
                            .arg(rsa.ok())
                            .arg(rowsA.size())
                            .arg(genB.completeGeneration)
                            .arg(r2109SameRows(rowsB, rowsA))
                            .arg(retryOnce);
        }
        // 场景二：键不重叠 B（A 物化 (2,2) 邻域，B 物化 (7,7) 邻域——交集仅中心格键）。
        {
            const QString db = r2109TempDb("s2");
            QFile::remove(db);
            World wA;
            r2109Sparse(wA, 93);
            World wB;
            r2109Sparse(wB, 94);
            r2109Load(wA, 2, 2);
            r2109Mark(wA, 37, 40);
            r2109Load(wB, 7, 7);
            r2109Mark(wB, 118, 121);
            const auto rowsP = r2109PristineSave(wB, "s2p");
            WorldStore store;
            SaveCoordinator coord;
            const bool openA = store.openWorld(db);
            store.setWorld(&wA);
            coord.bind(&store, db);
            SaveRequest ra;
            ra.name = QStringLiteral("r2109s2");
            const SaveReceipt rsa = coord.saveAll(ra);
            const auto rowsA = r2109ReadChunks(db);
            const SaveGenerationInfo genA = coord.recover();
            const bool faultOn = r2109InjectDeleteFault(db, true);
            store.setWorld(&wB);
            SaveRequest rb;
            rb.name = QStringLiteral("r2109s2b");
            const SaveReceipt rsb = coord.saveAll(rb);
            const SaveGenerationInfo genB = coord.recover();
            const auto rowsB = r2109ReadChunks(db);
            const bool faultOff = r2109InjectDeleteFault(db, false);
            SaveRequest rc;
            rc.name = QStringLiteral("r2109s2c");
            const SaveReceipt rsc = coord.saveAll(rc);
            const SaveGenerationInfo genC = coord.recover();
            const auto rowsC = r2109ReadChunks(db);
            const bool retryOnce = rsc.ok() && r2109SameRows(rowsC, rowsP)
                && genC.completeGeneration > genA.completeGeneration
                && genC.completeGeneration == genC.generation;
            const bool s2 = openA && rsa.ok() && !rowsA.isEmpty()
                && genA.completeGeneration == 1 && faultOn && !rsb.ok()
                && genB.completeGeneration == 1 && r2109SameRows(rowsB, rowsA) && faultOff
                && retryOnce;
            store.closeWorld();
            QFile::remove(db);
            ok = ok && s2;
            if (!s2)
                diag += QStringLiteral("[s2 open=%1 a=%2 bfail=%3 same=%4 once=%5]")
                            .arg(openA)
                            .arg(rsa.ok())
                            .arg(!rsb.ok())
                            .arg(r2109SameRows(rowsB, rowsA))
                            .arg(retryOnce);
        }
        // 场景三：键重叠 B（A 物化 (0,0)+(1,1)，B 只物化 (0,0)——B 键 ⊂ A 键；旧实现 INSERT 冲突
        //   也回滚（审查原文如实承认），本场景钉定值面：receipt 失败 + 全表恰 A + 重试恰 B 且
        //   A 的 (1,1) 残行被成功 DELETE 清除 = 恰一次）。
        {
            const QString db = r2109TempDb("s3");
            QFile::remove(db);
            World wA;
            r2109Sparse(wA, 95);
            World wB;
            r2109Sparse(wB, 96);
            r2109Load(wA, 2, 2);
            r2109Mark(wA, 37, 40);
            r2109Load(wA, 5, 5);
            r2109Mark(wA, 85, 88);
            r2109Load(wB, 5, 5);
            r2109Mark(wB, 91, 82);
            const auto rowsP = r2109PristineSave(wB, "s3p");
            WorldStore store;
            SaveCoordinator coord;
            const bool openA = store.openWorld(db);
            store.setWorld(&wA);
            coord.bind(&store, db);
            SaveRequest ra;
            ra.name = QStringLiteral("r2109s3");
            const SaveReceipt rsa = coord.saveAll(ra);
            const auto rowsA = r2109ReadChunks(db);
            const bool faultOn = r2109InjectDeleteFault(db, true);
            store.setWorld(&wB);
            SaveRequest rb;
            rb.name = QStringLiteral("r2109s3b");
            const SaveReceipt rsb = coord.saveAll(rb);
            const auto rowsB = r2109ReadChunks(db);
            const bool faultOff = r2109InjectDeleteFault(db, false);
            SaveRequest rc;
            rc.name = QStringLiteral("r2109s3c");
            const SaveReceipt rsc = coord.saveAll(rc);
            const auto rowsC = r2109ReadChunks(db);
            // 恰一次判据 = 重试行集与同世界原语存档逐行恒等（含 A 独有 (1,1) 残行必不在集内
            // ——原语存档不含它）。键集断言按邻域物化实证放宽（loadChunkAt 3×3 裁剪物化）。
            const bool retryOnce = rsc.ok() && r2109SameRows(rowsC, rowsP);
            const bool s3 = openA && rsa.ok() && !rowsA.isEmpty() && faultOn && !rsb.ok()
                && r2109SameRows(rowsB, rowsA) && faultOff && retryOnce;
            store.closeWorld();
            QFile::remove(db);
            ok = ok && s3;
            if (!s3)
                diag += QStringLiteral("[s3 open=%1 a=%2 bfail=%3 same=%4 once=%5]")
                            .arg(openA)
                            .arg(rsa.ok())
                            .arg(!rsb.ok())
                            .arg(r2109SameRows(rowsB, rowsA))
                            .arg(retryOnce);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2109a delete fault column (a real temporary database carries a before delete"
               " trigger that aborts so the unified save through the real coordinator fails at"
               " the chunks delete with the receipt failing and the complete generation frozen"
               " and the store rows answering exactly the previous world for the empty payload"
               " the disjoint keys and the overlapping keys scenarios, and after the fault is"
               " lifted the retry answers success with the generation advanced and rows"
               " identical to a pristine save of the same world)"
            << (ok ? QString() : diag);
    });

    // ── r2109b:地图五态柱（F06 坏键降级 + R01 上界有界拒绝 + 存退重开隔离）────────────────────
    //   同实例五态逐级：有效 A（正负象限双标记列）→ 坏尺寸 B（NEG-2 敏感）→ 坏 revision（NEG-2
    //   敏感）→ 截断载荷（账不平面——另一 clearAll 行幸存）→ 空行 C——每态断言零 A 残留。R01 面：
    //   超预算 w/d（30000 与 INT_MAX 半值）有界拒绝进清态（禁溢出分配）+ 真实大图 2000² 可载。
    //   存退重开隔离：A 存退复原 → 坏键清态后空集落盘 → 重开无行。
    runLeg("r2109b map five state column (the same long lived store instance loads a valid"
        " world with painted columns in both quadrants then answers a bad width row a bad"
        " revision row a truncated payload and an empty row each degrading to the cleared"
        " state with zero pixels left from the previous world, over budget dimensions are"
        " bounded rejected into the cleared state without any overflow allocation while a"
        " real large canvas still loads, and a save close reopen cycle isolates the worlds so"
        " a cleared store persists empty)", [&]() {
        bool ok = true;
        QString diag;
        const quint32 c1 = 0xFF4e8f3c, c2 = 0xFF3b6cb0;
        const int B = MapStore::kDomainMargin;
        QVariantMap rowA;
        rowA.insert(QStringLiteral("present"), true);
        rowA.insert(QStringLiteral("width"), 48);
        rowA.insert(QStringLiteral("depth"), 48);
        rowA.insert(QStringLiteral("revision"), 7);
        {
            MapStore probe;
            probe.initialize(48, 48);
            probe.writeColumn(10, 10, c1);  // 核心域正象限列
            probe.writeColumn(-60, 20, c2); // 前沿带负列（带内）
            probe.commitColumns();
            rowA.insert(QStringLiteral("pixels"),
                        probe.exportVariant().value(QStringLiteral("pixels")).toByteArray());
        }
        MapStore ms;
        // 态一：有效 A 完整复原（含负象限列 + revision 复原）。
        ms.loadVariant(rowA);
        const bool stA = ms.hasMap() && ms.mapWidth() == 48 + 2 * B
            && ms.columnColor(10, 10) == c1 && ms.columnColor(-60, 20) == c2;
        ok = ok && stA;
        if (!stA)
            diag += QStringLiteral("[stA has=%1 w=%2 c1=%3 c2=%4]")
                        .arg(ms.hasMap())
                        .arg(ms.mapWidth() == 48 + 2 * B)
                        .arg(ms.columnColor(10, 10) == c1)
                        .arg(ms.columnColor(-60, 20) == c2);
        // 态二：坏尺寸 B（width=0）→ 清态零残留【NEG-2 敏感】。
        {
            QVariantMap rowB = rowA;
            rowB.insert(QStringLiteral("width"), 0);
            ms.loadVariant(rowB);
            const bool stB = !ms.hasMap()
                && ms.columnColor(10, 10) == MapStore::kUnexploredColor
                && ms.columnColor(-60, 20) == MapStore::kUnexploredColor;
            ok = ok && stB;
            if (!stB)
                diag += QStringLiteral("[stB has=%1 c=%2]")
                            .arg(ms.hasMap())
                            .arg(ms.columnColor(10, 10) == MapStore::kUnexploredColor);
        }
        // 态三：坏 revision（非整数键）→ 清态零残留【NEG-2 敏感】。先复载 A 再坏键。
        ms.loadVariant(rowA);
        {
            QVariantMap rowB2 = rowA;
            rowB2.insert(QStringLiteral("revision"), QStringLiteral("x"));
            ms.loadVariant(rowB2);
            const bool stB2 = !ms.hasMap()
                && ms.columnColor(10, 10) == MapStore::kUnexploredColor;
            ok = ok && stB2;
            if (!stB2)
                diag += QStringLiteral("[stB2 has=%1]").arg(ms.hasMap());
        }
        // 态四：截断载荷（账不平面——既有降级行保持）→ 清态。
        ms.loadVariant(rowA);
        {
            QVariantMap rowB3 = rowA;
            const QByteArray px = rowA.value(QStringLiteral("pixels")).toByteArray();
            rowB3.insert(QStringLiteral("pixels"), px.left(px.size() - 4));
            ms.loadVariant(rowB3);
            const bool stB3 = !ms.hasMap()
                && ms.columnColor(10, 10) == MapStore::kUnexploredColor;
            ok = ok && stB3;
            if (!stB3)
                diag += QStringLiteral("[stB3 has=%1]").arg(ms.hasMap());
        }
        // 态五：空行 C → 清态。
        ms.loadVariant(QVariantMap());
        const bool stC = !ms.hasMap();
        ok = ok && stC;
        if (!stC) diag += QStringLiteral("[stC]");
        // R01 面：超预算有界拒绝（30000² 与 INT_MAX 半值 → 禁溢出分配直落清态）+ 真实大图可载。
        {
            QVariantMap rowR = rowA;
            rowR.insert(QStringLiteral("width"), 30000);
            rowR.insert(QStringLiteral("depth"), 30000);
            ms.loadVariant(rowR);
            const bool r1a = !ms.hasMap();
            QVariantMap rowR2 = rowA;
            rowR2.insert(QStringLiteral("width"), int(0x40000000));
            rowR2.insert(QStringLiteral("depth"), 20000);
            ms.loadVariant(rowR2);
            const bool r1b = !ms.hasMap();
            QVariantMap rowOk = rowA;
            rowOk.insert(QStringLiteral("width"), 2000);
            rowOk.insert(QStringLiteral("depth"), 2000);
            const QByteArray big = QByteArray((2000 + 2 * B) * (2000 + 2 * B) * 4, char(0x33));
            rowOk.insert(QStringLiteral("pixels"), big);
            ms.loadVariant(rowOk);
            const bool r1ok = ms.hasMap() && ms.mapWidth() == 2000 + 2 * B;
            ok = ok && r1a && r1b && r1ok;
            if (!(r1a && r1b && r1ok))
                diag += QStringLiteral("[r1 over=%1 over2=%2 ok=%3 has=%4 w=%5 px=%6]")
                            .arg(r1a)
                            .arg(r1b)
                            .arg(r1ok)
                            .arg(ms.hasMap())
                            .arg(ms.mapWidth())
                            .arg(big.size());
        }
        ms.loadVariant(rowA); // 复载 A 备存退
        // 存退重开隔离：A 落盘 → 重开复原；坏键清态后空集落盘 → 重开无行（坏世界不得带走 A 探索）。
        {
            const QString db = r2109TempDb("map");
            QFile::remove(db);
            World w;
            r2109Sparse(w, 82);
            WorldStore store;
            const int savedRev = ms.revision();
            const bool open1 = store.openWorld(db);
            store.setWorld(&w);
            const bool save1 = store.saveAll(QStringLiteral("r2109map"), {}, {}, {}, {}, {}, {},
                                             {}, {}, ms.exportVariant());
            store.closeWorld();
            const bool open2 = store.openWorld(db);
            store.setWorld(&w);
            MapStore ms2;
            ms2.loadVariant(store.loadMapDataset());
            const bool back = open1 && save1 && open2 && ms2.hasMap()
                && ms2.columnColor(10, 10) == c1 && ms2.columnColor(-60, 20) == c2
                && ms2.revision() == savedRev;
            {
                QVariantMap rowBad = ms2.exportVariant();
                rowBad.insert(QStringLiteral("revision"), QStringLiteral("x"));
                ms2.loadVariant(rowBad);
                const bool cleared = !ms2.hasMap();
                const bool save2 = store.saveAll(QStringLiteral("r2109map"), {}, {}, {}, {}, {},
                                                 {}, {}, {}, ms2.exportVariant());
                store.closeWorld();
                const bool open3 = store.openWorld(db);
                store.setWorld(&w);
                const QVariantMap rowRe = store.loadMapDataset();
                MapStore ms3;
                ms3.loadVariant(rowRe);
                const bool emptyKept =
                    open3 && save2 && cleared
                    && !rowRe.value(QStringLiteral("present")).toBool() && !ms3.hasMap();
                ok = ok && back && emptyKept;
                if (!(back && emptyKept))
                    diag += QStringLiteral("[persist back=%1 clear=%2 save=%3 row=%4 has=%5]")
                                .arg(back)
                                .arg(cleared)
                                .arg(save2)
                                .arg(rowRe.value(QStringLiteral("present")).toBool())
                                .arg(ms3.hasMap());
            }
            store.closeWorld();
            QFile::remove(db);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2109b map five state column (the same long lived store instance loads a"
               " valid world with painted columns in both quadrants then answers a bad width"
               " row a bad revision row a truncated payload and an empty row each degrading to"
               " the cleared state with zero pixels left from the previous world, over budget"
               " dimensions are bounded rejected into the cleared state without any overflow"
               " allocation while a real large canvas still loads, and a save close reopen"
               " cycle isolates the worlds so a cleared store persists empty)"
            << (ok ? QString() : diag);
    });

    // ── r2109c:装瓶矩阵柱（F07 两面 × 数量 × 满包/合并——失败零消耗零挥臂；成功一进一出）─────
    //   锅舀水面：满包 2 瓶拒（瓶水位零动零挥臂）/ 满包 1 瓶腾手成（一进一出 + 降一级）/ 满包
    //   2 瓶 + 合并槽 63 水瓶成（合并 + 降级 + 手留恰 1 空瓶）/ 满包 64 瓶拒（水位保持）。
    //   水源面：瓶不带走水（水源格恒水）+ 同 3 形态。牛奶桶面审查明示不扩（空桶堆叠 1 消耗必
    //   腾槽 = 原子面天然成立，勿扩面）。
    runLeg("r2109c bottle capacity matrix column (a glass bottle right clicked on a full"
        " cauldron or a water source answers zero consumption with the bottle count the water"
        " level and the source cell all untouched and no swing when no slot can take the water"
        " bottle, and answers exactly one bottle consumed and one water bottle granted when"
        " the freed hand or a merge slot can take it, across bottle counts one two and a full"
        " stack with a full pack and with a merge slot, and the water source block stays"
        " water)", [&]() {
        bool ok = true;
        QString diag;
        World w;
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);
        w.setWeatherRemainingSec(3600.0f);
        // 地形带：地板 y=79 + 上空 80..84 清空；锅 (24,80,24)；水源 (30,80,30)（底 y=79 石）。
        for (int x = 16; x <= 34; ++x)
            for (int z = 20; z <= 34; ++z) {
                w.setBlock(x, 79, z, quint8(BR::Stone), 0);
                for (int y = 80; y <= 84; ++y)
                    w.setBlock(x, y, z, quint8(BR::Air), 0);
            }
        const int cxB = 24, cyB = 80, czB = 24; // 锅落格
        const int cxW = 30, cyW = 80, czW = 30; // 水源落格
        w.setBlock(cxB, cyB, czB, BR::Cauldron, 3);
        w.setBlock(cxW, cyW, czW, BR::Water, 0);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        QQuickWindow probeWin;
        pc.setParentItem(probeWin.contentItem());
        pc.grab();
        pc.setSelectedBlock(int(BR::Air));
        r2109Pump(300); // 越过 placeBlock 入口 CD 纪元（m_lastPlaceMs 初值窗——防 CD 拒代拒面假绿）
        // 瞄准（section83 同式：眼位 = 脚 +1.62；目标 = 命中格中心）。
        const auto aim = [&](float fx, float fz, float tx, float ty, float tz, int mode) {
            const float ex = fx, ey = 80.0f + 1.62f, ez = fz;
            const float dx = tx - ex, dy = ty - ey, dz = tz - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pc.release();
            pc.grab();
            pc.loadSavedState(fx, 80.0f, fz, yaw, pitch, mode);
            r2109Pump(17);
            pc.tick();
        };
        const auto aimCauldron = [&](int mode) {
            aim(20.5f, 24.5f, float(cxB) + 0.5f, float(cyB) + 0.5f, float(czB) + 0.5f, mode);
        };
        const auto aimWater = [&](int mode) {
            aim(26.5f, 30.5f, float(cxW) + 0.5f, float(cyW) + 0.5f, float(czW) + 0.5f, mode);
        };
        // 槽位快照（9 槽 id+count 全比——零消耗判据的充分形）。
        QVector<QPair<int, int>> snapBefore;
        const auto snap = [&]() {
            snapBefore.clear();
            for (int i = 0; i < 9; ++i)
                snapBefore.append({ hb.blockIdAt(i), hb.countAt(i) });
        };
        const auto snapSame = [&]() {
            for (int i = 0; i < 9; ++i)
                if (hb.blockIdAt(i) != snapBefore[i].first
                    || hb.countAt(i) != snapBefore[i].second)
                    return false;
            return true;
        };
        // 背包构筑：slot0 = 手持瓶 N；其余槽按形填充（full = 石 64；merge = 水瓶 63）。
        const auto buildPack = [&](int bottles, bool mergeSlot, bool fullPack) {
            hb.setStack(0, int(RecipeRegistry::GlassBottleId), bottles, 0);
            for (int i = 1; i <= 8; ++i) {
                if (mergeSlot && i == 1)
                    hb.setStack(i, int(RecipeRegistry::WaterBottleId), 63, 0);
                else if (fullPack)
                    hb.setStack(i, int(BR::Stone), 64, 0);
                else
                    hb.setStack(i, 0, 0);
            }
        };
        int swings = 0;
        const QMetaObject::Connection swingConn = QObject::connect(
            &pc, &PlayerController::swingArm, &pc, [&]() { ++swings; });
        const auto levelAt = [&]() {
            return w.stateAt(cxB, cyB, czB) & quint8(BR::CauldronStateLevelMask);
        };
        const auto waterOk = [&]() { return w.blockAt(cxW, cyW, czW) == BR::Water; };
        // 锅舀水面 case1：满包 2 瓶 → 拒（零消耗零挥臂零水位动）。
        buildPack(2, false, true);
        aimCauldron(2 /* Survival */);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c1 = snapSame() && levelAt() == 3 && swings == 0;
        ok = ok && c1;
        if (!c1)
            diag += QStringLiteral("[c1 same=%1 lvl=%2 sw=%3]")
                        .arg(snapSame())
                        .arg(levelAt())
                        .arg(swings);
        // 锅舀水面 case2：满包 1 瓶（腾手）→ 成（slot0=水瓶 1 + 水位 3→2 + 一次挥臂）。
        buildPack(1, false, true);
        aimCauldron(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c2 = hb.blockIdAt(0) == int(RecipeRegistry::WaterBottleId)
            && hb.countAt(0) == 1 && levelAt() == 2 && swings == 1;
        ok = ok && c2;
        if (!c2)
            diag += QStringLiteral("[c2 id=%1 n=%2 lvl=%3 sw=%4]")
                        .arg(hb.blockIdAt(0))
                        .arg(hb.countAt(0))
                        .arg(levelAt())
                        .arg(swings);
        // 锅舀水面 case3：满包 2 瓶 + 合并槽 63 水瓶 → 成（slot0 恰 1 空瓶 + slot1→64 + 2→1）。
        buildPack(2, true, true);
        aimCauldron(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c3 = hb.blockIdAt(0) == int(RecipeRegistry::GlassBottleId)
            && hb.countAt(0) == 1 && hb.blockIdAt(1) == int(RecipeRegistry::WaterBottleId)
            && hb.countAt(1) == 64 && levelAt() == 1 && swings == 1;
        ok = ok && c3;
        if (!c3)
            diag += QStringLiteral("[c3 id=%1 n=%2 m=%3 mn=%4 lvl=%5 sw=%6]")
                        .arg(hb.blockIdAt(0))
                        .arg(hb.countAt(0))
                        .arg(hb.blockIdAt(1))
                        .arg(hb.countAt(1))
                        .arg(levelAt())
                        .arg(swings);
        // 锅舀水面 case4：满包 64 瓶 → 拒（零消耗；水位保持 1）。
        buildPack(64, false, true);
        aimCauldron(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c4 = snapSame() && levelAt() == 1 && swings == 0;
        ok = ok && c4;
        if (!c4)
            diag += QStringLiteral("[c4 same=%1 lvl=%2 sw=%3]")
                        .arg(snapSame())
                        .arg(levelAt())
                        .arg(swings);
        // 水源面 case5：满包 2 瓶 → 拒（零消耗 + 水源格恒水）。
        buildPack(2, false, true);
        aimWater(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c5 = snapSame() && waterOk() && swings == 0;
        ok = ok && c5;
        if (!c5)
            diag += QStringLiteral("[c5 same=%1 water=%2 sw=%3]")
                        .arg(snapSame())
                        .arg(waterOk())
                        .arg(swings);
        // 水源面 case6：满包 1 瓶（腾手）→ 成（一进一出 + 水源格恒水——瓶不带走水）。
        buildPack(1, false, true);
        aimWater(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c6 = hb.blockIdAt(0) == int(RecipeRegistry::WaterBottleId)
            && hb.countAt(0) == 1 && waterOk() && swings == 1;
        ok = ok && c6;
        if (!c6)
            diag += QStringLiteral("[c6 id=%1 n=%2 water=%3 sw=%4]")
                        .arg(hb.blockIdAt(0))
                        .arg(hb.countAt(0))
                        .arg(waterOk())
                        .arg(swings);
        // 水源面 case7：满包 2 瓶 + 合并槽 63 水瓶 → 成（合并 + 水源恒水）。
        buildPack(2, true, true);
        aimWater(2);
        snap();
        swings = 0;
        r2109Pump(260);
        pc.placeBlock();
        r2109Pump(60);
        const bool c7 = hb.blockIdAt(0) == int(RecipeRegistry::GlassBottleId)
            && hb.countAt(0) == 1 && hb.countAt(1) == 64 && waterOk() && swings == 1;
        ok = ok && c7;
        if (!c7)
            diag += QStringLiteral("[c7 id=%1 n=%2 mn=%3 water=%4 sw=%5]")
                        .arg(hb.blockIdAt(0))
                        .arg(hb.countAt(0))
                        .arg(hb.countAt(1))
                        .arg(waterOk())
                        .arg(swings);
        QObject::disconnect(swingConn);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2109c bottle capacity matrix column (a glass bottle right clicked on a full"
               " cauldron or a water source answers zero consumption with the bottle count the"
               " water level and the source cell all untouched and no swing when no slot can"
               " take the water bottle, and answers exactly one bottle consumed and one water"
               " bottle granted when the freed hand or a merge slot can take it, across bottle"
               " counts one two and a full stack with a full pack and with a merge slot, and"
               " the water source block stays water)"
            << (ok ? QString() : diag);
    });

    // ── r2109d:结构钉族柱（NEG 双摘面行豁免不钉；哨兵不动；filter 词元零命中）───────────────
    //   worldstore 五处补核行（DELETE 守卫 + 留痕 + PRAGMA 读/写守卫 + 探针双留痕）+ piston_anims
    //   行序保全复核（t1137 钉族原样——本单零触碰）+ mapstore 上界算式族（预算常量 + 助手头 +
    //   init 守卫 + loadVariant 守卫行——NEG-2 摘的 clearAll 行豁免不钉）+ 装瓶双守卫行 + 哨兵钉
    //   + CMake 段行 + 源树词元零命中 + QML 玩法路径零迁移负面门复钉。
    runLeg("r2109d structure pin family column (the world store carries the named chunks"
        " delete guard with the failure log and the pragma read and write guards and the two"
        " probe guards and the piston anims table rows untouched, the map store carries the"
        " dataset byte budget constant the helper head the initialize bound guard and the"
        " load variant guard row with the neg face clear row exempt, the controller carries"
        " both capacity guard rows, the cmake section row is on file, the count and atlas"
        " sentinels stay unmoved, the source tree stays free of the cross task filter token,"
        " and the gameplay path literals stay out of the qml)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = QDir(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/.."))
                                   .absoluteFilePath(QStringLiteral("src"));
        const QStringList missWs = pinSet(srcDir + QStringLiteral("/World/worldstore.cpp"), {
            SrcPin("chunks delete guard",
                   "if (!del.exec(QStringLiteral(\"DELETE FROM chunks\")))", 1),
            SrcPin("chunks delete log", "writeWorldPart: chunks delete failed:", 1),
            SrcPin("pragma read guard",
                   "if (!q.exec(QStringLiteral(\"PRAGMA user_version\")))", 1),
            SrcPin("pragma write guard",
                   "if (!q.exec(QStringLiteral(\"PRAGMA user_version = %1\").arg(kSchemaVersion)))",
                   1),
            SrcPin("probe guard pair", "probe failed:", 2),
            SrcPin("piston anims row", "INSERT INTO piston_anims", 1)});
        ok = ok && missWs.isEmpty();
        if (!missWs.isEmpty())
            diag += QStringLiteral("[ws %1]").arg(missWs.join(QLatin1Char(',')));
        const QStringList missMsH = pinSet(srcDir + QStringLiteral("/Game/mapstore.h"), {
            SrcPin("budget constant",
                   "static constexpr qsizetype kMaxDatasetBytes = qsizetype(128) * 1024 * 1024;",
                   1)});
        ok = ok && missMsH.isEmpty();
        if (!missMsH.isEmpty())
            diag += QStringLiteral("[msH %1]").arg(missMsH.join(QLatin1Char(',')));
        const QStringList missMsC = pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
            SrcPin("bytes helper head",
                   "qsizetype MapStore::datasetBytesFor(int worldWidth, int worldDepth)", 1),
            SrcPin("init bound guard",
                   "if (datasetBytesFor(width, depth) > kMaxDatasetBytes)", 1),
            SrcPin("load guard row",
                   "if (!okW || !okD || !okR || w <= 0 || d <= 0 || want > kMaxDatasetBytes)",
                   1)});
        ok = ok && missMsC.isEmpty();
        if (!missMsC.isEmpty())
            diag += QStringLiteral("[msC %1]").arg(missMsC.join(QLatin1Char(',')));
        const QStringList missPc = pinSet(srcDir + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("capacity guard pair",
                   "m_hotbar->canFitStack(int(RecipeRegistry::WaterBottleId), 1, freesHand)",
                   2),
            SrcPin("water frees hand row",
                   "const bool freesHand = m_hotbar->countAt(m_hotbar->selectedSlot()) == 1;",
                   1)});
        ok = ok && missPc.isEmpty();
        if (!missPc.isEmpty())
            diag += QStringLiteral("[pc %1]").arg(missPc.join(QLatin1Char(',')));
        const QStringList missCm = pinSet(
            QCoreApplication::applicationDirPath() + QStringLiteral("/../CMakeLists.txt"),
            {
                SrcPin("cmake section row",
                       "tools/matrix/section102_review_quickfix_t1139.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // 哨兵不动钉（本单零新方块零新表：Count 166 / Atlas 213 不动——t1138 lawful 前移后现值复钉）。
        const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("count sentinel", "Count           = 166,", 1),
            SrcPin("atlas sentinel", "static constexpr int AtlasTileCount = 213;", 1)});
        ok = ok && missBr.isEmpty();
        if (!missBr.isEmpty())
            diag += QStringLiteral("[br %1]").arg(missBr.join(QLatin1Char(',')));
        // 源树 filter 词元零命中（filter 词只落矩阵域）。
        {
            bool leaked = false;
            QDirIterator it(srcDir,
                            { QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                              QStringLiteral("*.qml") },
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QFile f(it.next());
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("r2109"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // QML 玩法路径零迁移负面门（r2090d/r2105d/r2106e/r2107e/r2108e 同门复钉）。
        {
            QFile qf(srcDir + QStringLiteral("/ui/Main.qml"));
            const QString qml = qf.open(QIODevice::ReadOnly)
                ? QString::fromUtf8(qf.readAll())
                : QStringLiteral("<unreadable>");
            const bool qmlGate = !qml.contains(QStringLiteral("GameSession"))
                && !qml.contains(QStringLiteral("MeshWorker"))
                && !qml.contains(QStringLiteral("ChunkStreamDriver"));
            ok = ok && qmlGate;
            if (!qmlGate) diag += QStringLiteral("[qmlGate]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2109d structure pin family column (the world store carries the named chunks"
               " delete guard with the failure log and the pragma read and write guards and"
               " the two probe guards and the piston anims table rows untouched, the map store"
               " carries the dataset byte budget constant the helper head the initialize bound"
               " guard and the load variant guard row with the neg face clear row exempt, the"
               " controller carries both capacity guard rows, the cmake section row is on"
               " file, the count and atlas sentinels stay unmoved, the source tree stays free"
               " of the cross task filter token, and the gameplay path literals stay out of"
               " the qml)"
            << (ok ? QString() : diag);
    });
}
