#include "matrix_helpers.h"

#include "savecoordinator.h" // 被测面：统一保存协议（本段判别柱走真协调层链——r2109 段同门）

#include <limits>

// 复核残面快修批探针段（4 腿；filter 词 "r2111"；矩阵 990→994）。置尾先例沿用（接 section103，
//   runAll 末执行，rig 世界零接触——三行为腿全 store 级直驱 + 真临时库，零墙钟依赖全逻辑驱动）。
// 交付三面（复核报告残面 M1/M2/T1 三节的本单收口批）：
//   「数据集溢出矩阵柱」→ r2111a（数据集字节算式除法形式预算检查收口：双轴 INT_MAX / 数据集
//     边长恰 2³¹ 的 64 位回绕带[×4 恰回绕负值——旧形先乘后比把回绕负值当「低于预算」放行的
//     本柱判别面] / 单轴 INT_MAX / 32768 级 / 零 / 负 全部有界拒绝零建库零版本动（直建库与
//     装载两公开面各扫一遍）+ 无效 QVariant / 短 BLOB 有界拒绝 + 正常大图可载）；
//   「装载信号序柱」→ r2111b（先验账后建库：像素 BLOB 账平核对全过才建库——账不平从「建库 +
//     两信号补救面」收口为「零建库零半态面」，账平路径与建库本体变更信号语义零迁移；信号计数
//     逐面钉）；
//   「真不重叠判别柱」→ r2111c（T1 收口：A 存后独立测试连接摘全部 B 键行、留恰一条非中心旧行
//     (7,7)，再注 DELETE ABORT 存 B——B 键集与残行真不重叠 = 旧行为分叉点翻红面[新守卫
//     return false 短路在 INSERT 之前；摘守卫行 = 错误继续 + INSERT 全成 + 完成代推进 + B 面
//     全表覆盖——旧新两版无法同二进制并跑，判别论证以腿注/段注申报，NEG 链外论证面]；新实现
//     = receipt 失败 + 完成世代冻结 + Interrupted + 关库重开全表逐字段仍 A[chunks/player/
//     chests/map/entities/progress/meta 名十二面]，解除故障重试恰一次 = 全表逐字段翻 B 且与
//     B 原语存档逐行恒等）；
//   「结构钉族柱」→ r2111d（本单三面非摘面行钉族 + 装载守卫族波及钉逐字幸存复核 + 哨兵钉
//     166/213 不动 + CMake 段行 + 源树 filter 词元零命中 + QML 玩法路径零迁移负面门复钉）。
// 恰红面设计（先于腿文；双手工 Edit 摘单行承重语句，编译绿）：
//   NEG-1 = 摘 mapstore.cpp 数据集字节算式越界哨兵 return 行（if (!bounded) 空壳行与有界乘法
//     行原样——摘后越界域直落有界乘法裸式，数据集边长恰 2³¹ 带的 ×4 回绕负值被当「低于预算」
//     放行：直建库面在图翻真 + 字节数下界钳 0 = 零分配零崩的净红面）→ 恰红 = {r2111a 直建库
//     2³¹ 回绕带子断言}（实测量为准；装载面同入参经账平先拒面幸存——登记的豁免面）。
//     r2111b/c/d 豁免（信号序 / 判别 / 钉族零触达本行）。
//   NEG-2 = 摘 mapstore.cpp 装载账平先拒行（单行完整语句 if (pixels.size() != want)
//     { clearAll(); return; }——摘后截断载荷直落建库半态发布 + 短缓冲回填）→ 恰红 = {r2111b
//     信号序柱[截断两子面信号计数翻 2 + 在图翻真], r2111a 短 BLOB 子断言[清态面同源翻红],
//     r2109b 态四[截断态清态断言——同源承重面同色红]}（实测量为准，三腿登记预期红集）。
//     r2111c/d 豁免。
//   判别柱的旧实现红面 = 结构性不可同跑（旧新两版二进制互斥）——以段注 + 腿注申报判别论证，
//     不占本单双 NEG 名额（neg 链只收单行可摘面）。
// 词元纪律：腿名 / diag 零跨任务 filter 词元（本段注释中的族引用不进腿名）。
namespace {

// 临时库路径（r2109 段同门：QDir::temp() + pid + 腿标，测试自清理；绝对路径直用）。
inline QString r2111TempDb(const char *tag)
{
    return QDir::temp().absoluteFilePath(QStringLiteral("voxel_resid_%1_%2.sqlite")
                                             .arg(QLatin1String(tag))
                                             .arg(QCoreApplication::applicationPid()));
}

// 独立连接 open-用-关 执行单条 SQL（触发器注入 / 剪枝两用——纯测试侧注入零生产缝）。
inline bool r2111RawExec(const QString &db, const QString &sql)
{
    const QString conn = QStringLiteral("resid_raw_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    bool ok = false;
    {
        QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        p.setDatabaseName(db);
        if (p.open()) {
            QSqlQuery q(p);
            ok = q.exec(sql);
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// DELETE 故障注放（BEFORE DELETE RAISE(ABORT)——r2109 注入形同门）。
inline bool r2111InjectDeleteFault(const QString &db, bool on)
{
    return on ? r2111RawExec(db, QStringLiteral(
                                   "CREATE TRIGGER resid_no_del BEFORE DELETE ON chunks BEGIN"
                                   " SELECT RAISE(ABORT, 'injected'); END;"))
              : r2111RawExec(db, QStringLiteral("DROP TRIGGER IF EXISTS resid_no_del"));
}

// chunks 表 raw 直读（独立连接；按 cx,cz 定序——逐行比对确定性序）。
struct R2111ChunkRow
{
    int cx = 0;
    int cz = 0;
    QByteArray voxels;
};

inline QList<R2111ChunkRow> r2111ReadChunks(const QString &db)
{
    QList<R2111ChunkRow> out;
    const QString conn = QStringLiteral("resid_chunks_%1").arg(QCoreApplication::applicationPid());
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

inline bool r2111SameRows(const QList<R2111ChunkRow> &a, const QList<R2111ChunkRow> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].voxels != b[i].voxels)
            return false;
    return true;
}

// 全表快照（关库重开核全表判据——chunks 之外复核 player/容器/map/entities/progress/meta 名
//   十二面；复核原文「不只比较 chunks」的直答面）。valid = 全部探针查询皆成功。
struct R2111TableSnapshot
{
    bool valid = false;
    int playerRows = 0;
    QByteArray playerData; // player_state id=0 行 data 原文（JSON 文本）
    int chestRows = 0;
    QByteArray chestData; // chests 恰一行 data 原文（A/B 内容判别面）
    int furnaceRows = 0;
    int dispenserRows = 0;
    int hopperRows = 0;
    int brewingRows = 0;
    int signRows = 0;
    int mapRows = 0;
    int mapWidth = 0;
    int mapDepth = 0;
    int mapRevision = 0;
    qsizetype mapPixels = 0;
    int entityRows = 0;
    int pistonAnimRows = 0;
    int progressRows = 0;
    QByteArray progressData; // progress key='main' 行 data 原文
    QString metaName;        // world_meta 'name'（完成标记面 = 台账 complete，另由 recover 读）
    QList<R2111ChunkRow> chunks;

    bool sameAs(const R2111TableSnapshot &o) const
    {
        return valid && o.valid && playerRows == o.playerRows && playerData == o.playerData
            && chestRows == o.chestRows && chestData == o.chestData
            && furnaceRows == o.furnaceRows && dispenserRows == o.dispenserRows
            && hopperRows == o.hopperRows && brewingRows == o.brewingRows
            && signRows == o.signRows && mapRows == o.mapRows && mapWidth == o.mapWidth
            && mapDepth == o.mapDepth && mapRevision == o.mapRevision && mapPixels == o.mapPixels
            && entityRows == o.entityRows && pistonAnimRows == o.pistonAnimRows
            && progressRows == o.progressRows && progressData == o.progressData
            && metaName == o.metaName && r2111SameRows(chunks, o.chunks);
    }
    // 失配面 diag（逐字段短名，落 FAIL 语境可读）。
    QString diffAgainst(const R2111TableSnapshot &o) const
    {
        QString d;
        if (playerRows != o.playerRows) d += QStringLiteral("playerRows ");
        if (playerData != o.playerData) d += QStringLiteral("playerData ");
        if (chestRows != o.chestRows) d += QStringLiteral("chestRows ");
        if (chestData != o.chestData) d += QStringLiteral("chestData ");
        if (furnaceRows != o.furnaceRows) d += QStringLiteral("furn ");
        if (dispenserRows != o.dispenserRows) d += QStringLiteral("disp ");
        if (hopperRows != o.hopperRows) d += QStringLiteral("hopper ");
        if (brewingRows != o.brewingRows) d += QStringLiteral("brew ");
        if (signRows != o.signRows) d += QStringLiteral("sign ");
        if (mapRows != o.mapRows) d += QStringLiteral("mapRows ");
        if (mapWidth != o.mapWidth) d += QStringLiteral("mapW ");
        if (mapDepth != o.mapDepth) d += QStringLiteral("mapD ");
        if (mapRevision != o.mapRevision) d += QStringLiteral("mapRev ");
        if (mapPixels != o.mapPixels) d += QStringLiteral("mapPx ");
        if (entityRows != o.entityRows) d += QStringLiteral("ent ");
        if (pistonAnimRows != o.pistonAnimRows) d += QStringLiteral("panim ");
        if (progressRows != o.progressRows) d += QStringLiteral("progRows ");
        if (progressData != o.progressData) d += QStringLiteral("progData ");
        if (metaName != o.metaName) d += QStringLiteral("metaName ");
        if (!r2111SameRows(chunks, o.chunks)) d += QStringLiteral("chunks ");
        return d;
    }
};

inline R2111TableSnapshot r2111ReadSnapshot(const QString &db)
{
    R2111TableSnapshot s;
    const QString conn = QStringLiteral("resid_snap_%1").arg(QCoreApplication::applicationPid());
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    {
        QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        p.setDatabaseName(db);
        if (p.open()) {
            bool ok = true;
            const auto count = [&](const char *table, int *out) {
                if (!ok)
                    return;
                QSqlQuery c(p);
                if (!c.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table)))) {
                    ok = false;
                    return;
                }
                *out = c.next() ? c.value(0).toInt() : 0;
            };
            const auto text = [&](const QString &sql, QByteArray *out) {
                if (!ok)
                    return;
                QSqlQuery c(p);
                if (!c.exec(sql)) {
                    ok = false;
                    return;
                }
                *out = c.next() ? c.value(0).toString().toUtf8() : QByteArray();
            };
            count("player_state", &s.playerRows);
            text(QStringLiteral("SELECT data FROM player_state WHERE id = 0"), &s.playerData);
            count("chests", &s.chestRows);
            text(QStringLiteral("SELECT data FROM chests LIMIT 1"), &s.chestData);
            count("furnaces", &s.furnaceRows);
            count("dispensers", &s.dispenserRows);
            count("hoppers", &s.hopperRows);
            count("brewing", &s.brewingRows);
            count("sign_texts", &s.signRows);
            count("map_dataset", &s.mapRows);
            count("entities", &s.entityRows);
            count("piston_anims", &s.pistonAnimRows);
            count("progress", &s.progressRows);
            text(QStringLiteral("SELECT data FROM progress WHERE key = 'main'"), &s.progressData);
            {
                QSqlQuery c(p);
                ok = ok && c.exec(QStringLiteral("SELECT value FROM world_meta WHERE key = 'name'"))
                    && c.next();
                if (ok)
                    s.metaName = c.value(0).toString();
            }
            s.chunks = r2111ReadChunks(db);
            s.valid = ok;
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return s;
}

// 原语存档（同世界 fresh 库零故障完整保存一次 → chunks 行集）——「重试恰一次」的逐行判据
//   （r2109 段同门形：真协调层链）。
inline QList<R2111ChunkRow> r2111PristineSave(World &w, const char *tag)
{
    const QString db = r2111TempDb(tag);
    QFile::remove(db);
    WorldStore store;
    SaveCoordinator coord;
    store.openWorld(db);
    store.setWorld(&w);
    coord.bind(&store, db);
    SaveRequest req;
    req.name = QStringLiteral("r2111pristine");
    coord.saveAll(req);
    store.closeWorld();
    const QList<R2111ChunkRow> rows = r2111ReadChunks(db);
    QFile::remove(db);
    return rows;
}

// sparse 世界定版（构造缝窄域形：核心域尺寸 + 出生预生成半径可调——判别柱用「A 大域 / B 小域」
//   构造真不重叠：冻结缓冲 = fixed 全网格，保存键集 = 缓冲全网格，故 B 键集有界确定性只能靠
//   **核心域尺寸收窄**达成——B 32×32 = 2×2 chunk 网格，与 A 保留的远端 (7,7) 行零交集）。
inline void r2111SparseTight(World &w, int seed, int core, int radius)
{
    w.reinitializeAsSparse(World::SparseWorldParams{ seed, core, core, 96, radius });
}

inline void r2111Mark(World &w, int lx, int lz)
{
    w.setBlock(lx, 79, lz, quint8(BR::Stone), 0);
}

} // namespace

void MatrixRun::section104_review_residual_t1141()
{
    // ── r2111a:数据集溢出矩阵柱（除法形式预算检查——全部越界形有界拒绝零建库，回绕带判别面，
    //    正常大图可载）────────────────────────────────────────────────────────────────────
    //   直建库面（initialize 公开面）与装载面（loadVariant 公开面）各扫一遍同矩阵；有界拒绝
    //   面零建库零信号零版本动（fresh 实例 revision 恒 0 = 零副作用充分形）。
    runLeg("r2111a dataset overflow matrix column (the dataset byte authority answers the"
        " division form budget check so double int max axes the exact power of two dataset"
        " edge wrap band a single int max axis the 32768 level zero and negative dimensions"
        " are all bounded rejected with no library built no signal and no revision movement"
        " through both the direct initialize face and the load variant face, an invalid"
        " variant width and a short blob are bounded rejected, and a normal large canvas"
        " still loads with painted columns readable)", [&]() {
        bool ok = true;
        QString diag;
        const int kIntMax = std::numeric_limits<int>::max();
        MapStore probe;
        // 直建库面：双轴 INT_MAX / 2³¹ 回绕带【NEG-1 敏感】/ 单轴 INT_MAX / 32768 级 / 零 / 负。
        probe.initialize(kIntMax, kIntMax);
        const bool i1 = !probe.hasMap();
        probe.initialize(kIntMax - 255, kIntMax - 255); // 数据集边长恰 2³¹：×4 恰 64 位回绕带
        const bool i2 = !probe.hasMap();
        probe.initialize(kIntMax, 1);
        const bool i3 = !probe.hasMap();
        probe.initialize(32768, 32768);
        const bool i4 = !probe.hasMap();
        probe.initialize(0, 48);
        const bool i5 = !probe.hasMap();
        probe.initialize(-5, 48);
        const bool i6 = !probe.hasMap();
        const bool revStill = probe.revision() == 0; // 有界拒绝零建库零信号零版本动
        ok = ok && i1 && i2 && i3 && i4 && i5 && i6 && revStill;
        if (!(i1 && i2 && i3 && i4 && i5 && i6 && revStill))
            diag += QStringLiteral("[init i1=%1 i2=%2 i3=%3 i4=%4 i5=%5 i6=%6 rev=%7]")
                        .arg(i1)
                        .arg(i2)
                        .arg(i3)
                        .arg(i4)
                        .arg(i5)
                        .arg(i6)
                        .arg(revStill);
        // 装载面矩阵（rowA = 有效 48² 基准行；逐病键复制改写）。
        const quint32 c1 = 0xFF4e8f3c;
        const int B = MapStore::kDomainMargin;
        QVariantMap rowA;
        rowA.insert(QStringLiteral("present"), true);
        rowA.insert(QStringLiteral("width"), 48);
        rowA.insert(QStringLiteral("depth"), 48);
        rowA.insert(QStringLiteral("revision"), 7);
        {
            MapStore src;
            src.initialize(48, 48);
            src.writeColumn(10, 10, c1);
            src.commitColumns();
            rowA.insert(QStringLiteral("pixels"),
                        src.exportVariant().value(QStringLiteral("pixels")).toByteArray());
        }
        const auto loadCleared = [&](const QVariantMap &row) {
            probe.loadVariant(row);
            return !probe.hasMap();
        };
        QVariantMap row = rowA;
        row.insert(QStringLiteral("width"), kIntMax);
        row.insert(QStringLiteral("depth"), kIntMax);
        const bool v1 = loadCleared(row); // 双轴 INT_MAX
        row = rowA;
        row.insert(QStringLiteral("width"), kIntMax - 255);
        row.insert(QStringLiteral("depth"), kIntMax - 255);
        const bool v2 = loadCleared(row); // 2³¹ 回绕带（装载面经账平先拒——NEG-1 登记豁免形）
        row = rowA;
        row.insert(QStringLiteral("width"), kIntMax);
        const bool v3 = loadCleared(row); // 单轴 INT_MAX
        row = rowA;
        row.insert(QStringLiteral("width"), 32768);
        row.insert(QStringLiteral("depth"), 32768);
        const bool v4 = loadCleared(row); // 32768 级
        row = rowA;
        row.insert(QStringLiteral("width"), QStringLiteral("x"));
        const bool v5 = loadCleared(row); // 无效 QVariant
        row = rowA;
        row.insert(QStringLiteral("width"), -3);
        row.insert(QStringLiteral("depth"), -4);
        const bool v6 = loadCleared(row); // 负
        row = rowA;
        row.insert(QStringLiteral("width"), 0);
        const bool v7 = loadCleared(row); // 零
        {
            QVariantMap rowT = rowA;
            const QByteArray px = rowA.value(QStringLiteral("pixels")).toByteArray();
            rowT.insert(QStringLiteral("pixels"), px.left(px.size() - 4));
            probe.loadVariant(rowT); // 短 BLOB（账平先拒面——信号计数判据归信号序柱）
            const bool v8 = !probe.hasMap();
            ok = ok && v8;
            if (!v8)
                diag += QStringLiteral("[trunc hasMap=%1]").arg(probe.hasMap());
        }
        // 正常大图可载（1024² → 数据集 1152² = 5.3MB，填充列可读）。
        {
            QVariantMap rowOk = rowA;
            rowOk.insert(QStringLiteral("width"), 1024);
            rowOk.insert(QStringLiteral("depth"), 1024);
            const QByteArray big = QByteArray((1024 + 2 * B) * (1024 + 2 * B) * 4, char(0x33));
            rowOk.insert(QStringLiteral("pixels"), big);
            probe.loadVariant(rowOk);
            const bool bigOk = probe.hasMap() && probe.mapWidth() == 1024 + 2 * B
                && probe.columnColor(0, 0) == 0x33333333u;
            ok = ok && bigOk;
            if (!bigOk)
                diag += QStringLiteral("[big has=%1 w=%2 c=%3]")
                            .arg(probe.hasMap())
                            .arg(probe.mapWidth() == 1024 + 2 * B)
                            .arg(probe.columnColor(0, 0) == 0x33333333u);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2111a dataset overflow matrix column (the dataset byte authority answers the"
               " division form budget check so double int max axes the exact power of two"
               " dataset edge wrap band a single int max axis the 32768 level zero and negative"
               " dimensions are all bounded rejected with no library built no signal and no"
               " revision movement through both the direct initialize face and the load variant"
               " face, an invalid variant width and a short blob are bounded rejected, and a"
               " normal large canvas still loads with painted columns readable)"
            << (ok ? QString() : diag);
    });

    // ── r2111b:装载信号序柱（先验账后建库——账不平零建库零半态信号；账平面与建库本体信号
    //    语义零迁移）───────────────────────────────────────────────────────────────────────
    //   信号计数判据（同实例计数器）：有效装载 = 恰 2（建库 1 + 回填 1——公开面不变）/ 载后
    //   截断 = 恰 1【NEG-2 敏感——旧序 = 建库半态 1 + 补救 1 = 2】/ 载后坏键 = 恰 1（坏键降级
    //   面本单零迁移）/ 载后空行 = 恰 1；fresh 实例全病形 = 恰 0（含 2³¹ 回绕带交叉面）。
    runLeg("r2111b load signal order column (the loader verifies the pixel blob accounting"
        " before any library is built so an accounting mismatch on a fresh store publishes"
        " zero signals and on a loaded store exactly the degradation signal with no half"
        " built intermediate state, while a valid load keeps its two signal public face of"
        " build plus backfill and the bad key and empty row degradation faces keep their"
        " prior single signal semantics)", [&]() {
        bool ok = true;
        QString diag;
        const quint32 c1 = 0xFF4e8f3c;
        const int kIntMax = std::numeric_limits<int>::max();
        QVariantMap rowA;
        rowA.insert(QStringLiteral("present"), true);
        rowA.insert(QStringLiteral("width"), 48);
        rowA.insert(QStringLiteral("depth"), 48);
        rowA.insert(QStringLiteral("revision"), 7);
        {
            MapStore src;
            src.initialize(48, 48);
            src.writeColumn(10, 10, c1);
            src.commitColumns();
            rowA.insert(QStringLiteral("pixels"),
                        src.exportVariant().value(QStringLiteral("pixels")).toByteArray());
        }
        QVariantMap rowTrunc = rowA;
        {
            const QByteArray px = rowA.value(QStringLiteral("pixels")).toByteArray();
            rowTrunc.insert(QStringLiteral("pixels"), px.left(px.size() - 4));
        }
        QVariantMap rowBadKey = rowA;
        rowBadKey.insert(QStringLiteral("width"), 0);
        QVariantMap rowWrap = rowA;
        rowWrap.insert(QStringLiteral("width"), kIntMax - 255);
        rowWrap.insert(QStringLiteral("depth"), kIntMax - 255);

        MapStore ms;
        int sigs = 0;
        const QMetaObject::Connection conn =
            QObject::connect(&ms, &MapStore::mapChanged, &ms, [&sigs]() { ++sigs; });
        // 面1：fresh + 有效装载 = 恰 2（建库 1 + 回填 1——公开面零迁移）。
        sigs = 0;
        ms.loadVariant(rowA);
        const bool f1 = sigs == 2 && ms.hasMap() && ms.columnColor(10, 10) == c1
            && ms.revision() == 7;
        ok = ok && f1;
        if (!f1)
            diag += QStringLiteral("[f1 sigs=%1 has=%2]").arg(sigs).arg(ms.hasMap());
        // 面2：载后 + 有效重载 = 恰 2。
        sigs = 0;
        ms.loadVariant(rowA);
        const bool f2 = sigs == 2 && ms.hasMap();
        ok = ok && f2;
        if (!f2) diag += QStringLiteral("[f2 sigs=%1]").arg(sigs);
        // 面3：载后 + 截断 = 恰 1（仅降级信号——先验账后建库【NEG-2 敏感：旧序 = 2】）。
        sigs = 0;
        ms.loadVariant(rowTrunc);
        const bool f3 = sigs == 1 && !ms.hasMap()
            && ms.columnColor(10, 10) == MapStore::kUnexploredColor;
        ok = ok && f3;
        if (!f3)
            diag += QStringLiteral("[f3 sigs=%1 has=%2]").arg(sigs).arg(ms.hasMap());
        // 面4：载后 + 坏键 = 恰 1（坏键降级面语义零迁移；先复载 A 恢复在图态）。
        ms.loadVariant(rowA);
        sigs = 0;
        ms.loadVariant(rowBadKey);
        const bool f4 = sigs == 1 && !ms.hasMap();
        ok = ok && f4;
        if (!f4)
            diag += QStringLiteral("[f4 sigs=%1 has=%2]").arg(sigs).arg(ms.hasMap());
        // 面5：载后 + 空行 = 恰 1（先复载 A 恢复在图态）。
        ms.loadVariant(rowA);
        sigs = 0;
        ms.loadVariant(QVariantMap());
        const bool f5 = sigs == 1 && !ms.hasMap();
        ok = ok && f5;
        if (!f5)
            diag += QStringLiteral("[f5 sigs=%1 has=%2]").arg(sigs).arg(ms.hasMap());
        QObject::disconnect(conn);
        // fresh 实例零信号面（病形全谱——零建库零半态）。
        MapStore ms2;
        int sigs2 = 0;
        const QMetaObject::Connection conn2 =
            QObject::connect(&ms2, &MapStore::mapChanged, &ms2, [&sigs2]() { ++sigs2; });
        ms2.loadVariant(rowTrunc);
        const bool z1 = sigs2 == 0 && !ms2.hasMap() && ms2.revision() == 0; // 【NEG-2 敏感】
        ms2.loadVariant(rowBadKey);
        const bool z2 = sigs2 == 0 && !ms2.hasMap();
        ms2.loadVariant(QVariantMap());
        const bool z3 = sigs2 == 0 && !ms2.hasMap();
        ms2.loadVariant(rowWrap);
        const bool z4 = sigs2 == 0 && !ms2.hasMap(); // 2³¹ 回绕带 × 先验账交叉面
        QObject::disconnect(conn2);
        ok = ok && z1 && z2 && z3 && z4;
        if (!(z1 && z2 && z3 && z4))
            diag += QStringLiteral("[z z1=%1 z2=%2 z3=%3 z4=%4 sigs=%5]")
                        .arg(z1)
                        .arg(z2)
                        .arg(z3)
                        .arg(z4)
                        .arg(sigs2);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2111b load signal order column (the loader verifies the pixel blob"
               " accounting before any library is built so an accounting mismatch on a fresh"
               " store publishes zero signals and on a loaded store exactly the degradation"
               " signal with no half built intermediate state, while a valid load keeps its"
               " two signal public face of build plus backfill and the bad key and empty row"
               " degradation faces keep their prior single signal semantics)"
            << (ok ? QString() : diag);
    });

    // ── r2111c:真不重叠判别柱（T1 收口——DELETE 故障测试的真判别场景 + 关库重开核全表）──────
    //   链形：B 原语存档（fresh 库零故障）→ A 存（带 player/progress/chest/map/entity 全载荷
    //   → 完成代 1）→ 独立测试连接摘全部 B 键行（留恰一条非中心旧行 (0,0)——真不重叠前置面）
    //   → 注 DELETE ABORT → 存 B（B 仅出生域键，与 (0,0) 零交集）→ receipt 失败 + 完成代冻结 +
    //   Interrupted + 全表逐字段仍剪枝态（=A）→ 关库重开复核全表 → 解除故障重试存 B = 成功 +
    //   完成代 3（失败尝试耗 2 号段——单调不重用面）+ 全表逐字段翻 B + chunks 与 B 原语存档
    //   逐行恒等（恰一次：非中心旧行同被清除）。
    //   判别论证（旧实现翻红面申报——旧新两版无法同二进制并跑）：守卫 return false 短路在
    //   INSERT 之前 = 行为分叉点。摘 return false 后本腿链形翻红：DELETE 语句失败被吞 → 继续
    //   INSERT（B 的 2×2 网格 4 键与残行 (7,7) 零冲突全数成功）→ 容器/地图/实体/玩家/进度段全部照写
    //   （B 面）→ 提交 → receipt 翻真 + 完成代推进 → 全表恰 A 断言 / 关库重开恰 A 断言 /
    //   重试恰一次面三面同红。键重叠旧腿（前置批三场景）因 INSERT 撞残行自动回滚不可判别——
    //   本腿 = 审查原文点名的真不重叠补验面。
    runLeg("r2111c true non overlap discrimination column (world A saves with full player"
        " progress chest map and entity payloads then an independent test connection removes"
        " every chunk row the next world would rewrite leaving exactly one far non center"
        " old row, the delete abort trigger is injected and saving world B whose key set"
        " never overlaps the kept row answers a failed receipt with the complete generation"
        " frozen the ledger interrupted and a close reopen full table reread matching the"
        " kept state field by field across chunks player chests map entities progress and"
        " the meta name, and lifting the fault retries world B once into success with the"
        " completion generation advanced past the consumed attempt and every table field"
        " flipped to world B with chunk rows identical to a pristine save)", [&]() {
        bool ok = true;
        QString diag;
        const QString db = r2111TempDb("t1");
        QFile::remove(db);
        // B 原语存档（判据基准：32×32 核心域 = 2×2 chunk 网格——B 键集 ⊆ (0..1)²，真不含 A 保留
        //   的远端 (7,7) 行——非重叠前置面；冻结缓冲键集 = 缓冲全网格的引擎结构面由小域收窄）。
        World wB;
        r2111SparseTight(wB, 92, 32, 0);
        const auto rowsPB = r2111PristineSave(wB, "t1p");
        bool noOverlap = !rowsPB.isEmpty();
        for (const auto &r : rowsPB)
            if (r.cx == 7 && r.cz == 7)
                noOverlap = false;
        // A 世界：大域出生预生成 + 远端 (7,7) 单 chunk 确定性标记列（160×160 网格内 (7,7) 在格）。
        World wA;
        r2111SparseTight(wA, 91, 160, 2);
        wA.loadChunkAt(7, 7);
        r2111Mark(wA, 118, 121);
        // A 全载荷请求（player/progress/chest/map/entity 五面非平凡内容——判别面非空转）。
        SaveRequest reqA;
        reqA.name = QStringLiteral("r2111keep");
        reqA.playerData.insert(QStringLiteral("hp"), 17);
        reqA.progress.insert(QStringLiteral("score"), 42);
        QVariantMap chestA;
        chestA.insert(QStringLiteral("x"), 1);
        chestA.insert(QStringLiteral("y"), 80);
        chestA.insert(QStringLiteral("z"), 2);
        QVariantList slotsA;
        {
            QVariantMap st;
            st.insert(QStringLiteral("id"), 4);
            st.insert(QStringLiteral("count"), 5);
            slotsA.append(st);
        }
        chestA.insert(QStringLiteral("slots"), slotsA);
        reqA.chests.append(chestA);
        {
            MapStore mapA;
            mapA.initialize(32, 32);
            mapA.writeColumn(3, 3, 0xFF4e8f3c);
            mapA.commitColumns();
            reqA.mapDataset = mapA.exportVariant();
        }
        {
            QVariantMap ent;
            ent.insert(QStringLiteral("kind"), 1);
            ent.insert(QStringLiteral("type"), 0);
            ent.insert(QStringLiteral("x"), 1.0);
            ent.insert(QStringLiteral("y"), 80.0);
            ent.insert(QStringLiteral("z"), 2.0);
            ent.insert(QStringLiteral("color"), QStringLiteral("plain"));
            ent.insert(QStringLiteral("hp"), 7);
            reqA.entities.append(ent);
        }
        WorldStore store;
        SaveCoordinator coord;
        const bool openA = store.openWorld(db);
        store.setWorld(&wA);
        coord.bind(&store, db);
        const SaveReceipt rsa = coord.saveAll(reqA);
        const SaveGenerationInfo genA = coord.recover();
        const auto rowsA = r2111ReadChunks(db);
        QByteArray rowAfar;
        for (const auto &r : rowsA)
            if (r.cx == 7 && r.cz == 7)
                rowAfar = r.voxels;
        // 独立测试连接剪枝：摘其余全部行，留恰一条远端非中心旧行 (7,7)（真不重叠构造）。
        const bool pruned = r2111RawExec(
            db, QStringLiteral("DELETE FROM chunks WHERE NOT (cx = 7 AND cz = 7)"));
        const auto kept = r2111ReadChunks(db);
        const bool keptFace = pruned && kept.size() == 1 && kept[0].cx == 7 && kept[0].cz == 7
            && !rowAfar.isEmpty() && kept[0].voxels == rowAfar;
        const auto snapKept = r2111ReadSnapshot(db);
        // 注 DELETE ABORT → 存 B（B 键集 ⊆ (0..1)²——与 (7,7) 真不重叠）。
        const bool faultOn = r2111InjectDeleteFault(db, true);
        store.setWorld(&wB);
        SaveRequest reqB;
        reqB.name = QStringLiteral("r2111swap");
        reqB.playerData.insert(QStringLiteral("hp"), 5);
        reqB.progress.insert(QStringLiteral("score"), 7);
        QVariantMap chestB;
        chestB.insert(QStringLiteral("x"), 9);
        chestB.insert(QStringLiteral("y"), 80);
        chestB.insert(QStringLiteral("z"), 9);
        QVariantList slotsB;
        {
            QVariantMap st;
            st.insert(QStringLiteral("id"), 8);
            st.insert(QStringLiteral("count"), 2);
            slotsB.append(st);
        }
        chestB.insert(QStringLiteral("slots"), slotsB);
        reqB.chests.append(chestB);
        reqB.mapDataset = QVariantMap(); // B 空地图载荷 = 表清空语义（旧实现会摘 A 图行的面）
        reqB.entities = QVariantList();  // B 空实体载荷 = 表清空语义
        const SaveReceipt rsb = coord.saveAll(reqB);
        const SaveGenerationInfo genB = coord.recover();
        const auto snapB = r2111ReadSnapshot(db);
        const bool rolled = !rsb.ok() && genB.completeGeneration == 1 && genB.generation == 2
            && genB.state == SaveRecoveryState::Interrupted && snapB.sameAs(snapKept);
        // 关库重开核全表（复核原文：不只 chunks——十二面快照重读）。
        store.closeWorld();
        const bool open2 = store.openWorld(db);
        store.setWorld(&wB);
        SaveCoordinator coord2;
        coord2.bind(&store, db);
        const SaveGenerationInfo genB2 = coord2.recover();
        const auto snapB2 = r2111ReadSnapshot(db);
        const bool reopened = open2 && snapB2.sameAs(snapKept) && genB2.completeGeneration == 1
            && genB2.generation == 2 && genB2.state == SaveRecoveryState::Interrupted;
        // 解除故障 → 重试存 B = 恰一次（全表翻 B + chunks 与 B 原语存档逐行恒等）。
        const bool faultOff = r2111InjectDeleteFault(db, false);
        const SaveReceipt rsc = coord2.saveAll(reqB);
        const SaveGenerationInfo genC = coord2.recover();
        const auto rowsC = r2111ReadChunks(db);
        const auto snapC = r2111ReadSnapshot(db);
        const bool retryOnce = rsc.ok() && genC.completeGeneration == 3 && genC.generation == 3
            && genC.state == SaveRecoveryState::Clean && r2111SameRows(rowsC, rowsPB)
            && snapC.valid && snapC.metaName == QStringLiteral("r2111swap")
            && snapC.playerRows == 1 && snapC.playerData != snapKept.playerData
            && snapC.chestRows == 1 && snapC.chestData != snapKept.chestData
            && snapC.mapRows == 0 && snapC.entityRows == 0 && snapC.progressRows == 1
            && snapC.progressData != snapKept.progressData;
        ok = openA && rsa.ok() && !rowsA.isEmpty() && genA.completeGeneration == 1 && noOverlap
            && keptFace && faultOn && rolled && reopened && faultOff && retryOnce;
        if (!(openA && rsa.ok() && genA.completeGeneration == 1))
            diag += QStringLiteral("[a open=%1 save=%2 gen=%3 ec=%4 em=%5 npb=%6]")
                        .arg(openA)
                        .arg(rsa.ok())
                        .arg(genA.completeGeneration)
                        .arg(rsa.error.code)
                        .arg(QLatin1String(rsa.error.message ? rsa.error.message : ""))
                        .arg(rowsPB.size());
        if (!(noOverlap && keptFace))
            diag += QStringLiteral("[kept overlap=%1 pruned=%2 n=%3 far=%4]")
                        .arg(noOverlap)
                        .arg(pruned)
                        .arg(kept.size())
                        .arg(keptFace);
        if (!rolled)
            diag += QStringLiteral("[rolled ok=%1 cg=%2 g=%3 st=%4 diff=%5]")
                        .arg(rsb.ok())
                        .arg(genB.completeGeneration)
                        .arg(genB.generation)
                        .arg(int(genB.state))
                        .arg(snapB.diffAgainst(snapKept));
        if (!reopened)
            diag += QStringLiteral("[reopen open=%1 diff=%2 cg=%3 g=%4 st=%5]")
                        .arg(open2)
                        .arg(snapB2.diffAgainst(snapKept))
                        .arg(genB2.completeGeneration)
                        .arg(genB2.generation)
                        .arg(int(genB2.state));
        if (!retryOnce)
            diag += QStringLiteral("[retry ok=%1 cg=%2 g=%3 rows=%4 meta=%5]")
                        .arg(rsc.ok())
                        .arg(genC.completeGeneration)
                        .arg(genC.generation)
                        .arg(r2111SameRows(rowsC, rowsPB))
                        .arg(snapC.metaName);
        store.closeWorld();
        QFile::remove(db);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2111c true non overlap discrimination column (world A saves with full player"
               " progress chest map and entity payloads then an independent test connection"
               " removes every chunk row the next world would rewrite leaving exactly one far"
               " non center old row, the delete abort trigger is injected and saving world B"
               " whose key set never overlaps the kept row answers a failed receipt with the"
               " complete generation frozen the ledger interrupted and a close reopen full"
               " table reread matching the kept state field by field across chunks player"
               " chests map entities progress and the meta name, and lifting the fault retries"
               " world B once into success with the completion generation advanced past the"
               " consumed attempt and every table field flipped to world B with chunk rows"
               " identical to a pristine save)"
            << (ok ? QString() : diag);
    });

    // ── r2111d:结构钉族柱（本单三面非摘面行钉 + 装载守卫族波及钉逐字幸存复核 + 哨兵/CMake/
    //    词元/QML 门）─────────────────────────────────────────────────────────────────────
    //   NEG-1 摘面行（越界哨兵 return）与 NEG-2 摘面行（账平先拒行）豁免不钉；其余本单新形行
    //   与前序波及钉（装载守卫三行——本单改造零位移逐字幸存）全钉。
    runLeg("r2111d structure pin family column (the map store byte authority carries the"
        " non positive sentinel line the division form bounded verdict line and the bounded"
        " multiply line with the initialize derivation pulling the byte count and the cell"
        " count from the verified buffer, the load guard row the initialize bound guard and"
        " the helper head survive verbatim as the load order pin on the build call, the"
        " world store chunks delete guard family stays, the count and atlas sentinels stay"
        " unmoved, the cmake section row is on file, the source tree stays free of this"
        " task's filter token, and the gameplay path literals stay out of the qml)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = QDir(QCoreApplication::applicationDirPath()
                                    + QStringLiteral("/.."))
                                   .absoluteFilePath(QStringLiteral("src"));
        // 本单新形行（非摘面——NEG 双面摘除后全数幸存）。
        const QStringList missMsC = pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
            SrcPin("non positive sentinel",
                   "if (worldWidth <= 0 || worldDepth <= 0)", 1),
            SrcPin("division form verdict",
                   "const bool bounded = w <= qsizetype(std::numeric_limits<int>::max())", 1),
            SrcPin("division form budget clause", "&& w <= kCellBudget / d;", 1),
            SrcPin("bounded multiply", "return w * d * 4;", 1),
            SrcPin("init derivation bytes",
                   "m_pixels.resize(qBound<qsizetype>(0, datasetBytesFor(width, depth), kMaxDatasetBytes));", 1),
            SrcPin("init derivation cells", "const qsizetype cells = m_pixels.size() / 4;", 1),
            // 装载序钉（建库调用行幸存 = 先验账后建库的秩序面；NEG-2 摘的账平行本体豁免）。
            SrcPin("load build call", "initialize(w, d);", 1)});
        ok = ok && missMsC.isEmpty();
        if (!missMsC.isEmpty())
            diag += QStringLiteral("[msC %1]").arg(missMsC.join(QLatin1Char(',')));
        // 前序波及钉逐字幸存复核（装载守卫族三行——本单改造零位移；r2109 段钉面前移零需要）。
        const QStringList missMsP = pinSet(srcDir + QStringLiteral("/Game/mapstore.cpp"), {
            SrcPin("bytes helper head",
                   "qsizetype MapStore::datasetBytesFor(int worldWidth, int worldDepth)", 1),
            SrcPin("init bound guard",
                   "if (datasetBytesFor(width, depth) > kMaxDatasetBytes)", 1),
            SrcPin("load guard row",
                   "if (!okW || !okD || !okR || w <= 0 || d <= 0 || want > kMaxDatasetBytes)",
                   1)});
        ok = ok && missMsP.isEmpty();
        if (!missMsP.isEmpty())
            diag += QStringLiteral("[msP %1]").arg(missMsP.join(QLatin1Char(',')));
        // 判别柱分叉点钉（DELETE 守卫族幸存——判别论证的短路行面）。
        const QStringList missWs = pinSet(srcDir + QStringLiteral("/World/worldstore.cpp"), {
            SrcPin("chunks delete guard",
                   "if (!del.exec(QStringLiteral(\"DELETE FROM chunks\")))", 1),
            SrcPin("chunks delete log", "writeWorldPart: chunks delete failed:", 1)});
        ok = ok && missWs.isEmpty();
        if (!missWs.isEmpty())
            diag += QStringLiteral("[ws %1]").arg(missWs.join(QLatin1Char(',')));
        // 哨兵不动钉（本单零新方块零新表零 schema：Count 166 / Atlas 213 不动）。
        const QStringList missBr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("count sentinel", "Count           = 166,", 1),
            SrcPin("atlas sentinel", "static constexpr int AtlasTileCount = 213;", 1)});
        ok = ok && missBr.isEmpty();
        if (!missBr.isEmpty())
            diag += QStringLiteral("[br %1]").arg(missBr.join(QLatin1Char(',')));
        // CMake 段行。
        const QStringList missCm = pinSet(
            QCoreApplication::applicationDirPath() + QStringLiteral("/../CMakeLists.txt"),
            {
                SrcPin("cmake section row",
                       "tools/matrix/section104_review_residual_t1141.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
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
                if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("r2111"))) {
                    leaked = true;
                    break;
                }
            }
            ok = ok && !leaked;
            if (leaked) diag += QStringLiteral("[token]");
        }
        // QML 玩法路径零迁移负面门（r2109d 同门复钉）。
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
            << "| r2111d structure pin family column (the map store byte authority carries the"
               " non positive sentinel line the division form bounded verdict line and the"
               " bounded multiply line with the initialize derivation pulling the byte count"
               " and the cell count from the verified buffer, the load guard row the"
               " initialize bound guard and the helper head survive verbatim as the load order"
               " pin on the build call, the world store chunks delete guard family stays, the"
               " count and atlas sentinels stay unmoved, the cmake section row is on file, the"
               " source tree stays free of this task's filter token, and the gameplay path"
               " literals stay out of the qml)"
            << (ok ? QString() : diag);
    });
}
