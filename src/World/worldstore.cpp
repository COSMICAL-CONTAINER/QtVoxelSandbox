#include "worldstore.h"

#include "chunk.h"
#include "chunkmanager.h"
#include "world.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <QVector>
#include <algorithm> // std::sort（迁移步按 targetVersion 排序）
#include <array>     // std::array（id remap 查表）
#include <cstring>   // std::memcpy（chunk blob 回填）
#include <memory>    // std::shared_ptr（id remap 查表捕获进 lambda）

// 单独日志分类（PLAN §2-F 模块化日志）：vo.save 存档读写可观测（建库 / 版本不符 / chunk blob 计数）。
Q_LOGGING_CATEGORY(lcSave, "vo.save")

// 命名 QSqlDatabase 连接（避免占用默认连接，便于多库切换 / 临时扫描连接隔离）。
static const char *const kConn = "voxelsandbox_worldstore";

// ── t1066 保存链连接面 busy 等待归零（全部 QSQLITE 连接设置点盘点 + 选型立证）──────────────
// 病灶：Qt QSQLITE 驱动默认 busy timeout 为秒级——外部锁（杀软/索引器/同步盘）全程持有时，
//   保存链**首试**在第一条撞锁语句上阻塞秒级才失败（t1064 真锁腿 wall≈29s 实测；首试第一撞锁
//   点 = 协调层台账 SELECT，随后事务写面在本文件 kConn 上同型阻塞）；t1064 的 150ms 探锁退避
//   只约束重试间隔、不约束首试阻塞 = 锁下退出仍卡秒级。
// 修法：各连接设置点显式归零 busy 等待（锁下即败即返），锁下失败语义交由 t1064 探锁退避收敛
//   （首试瞬时败 → 探锁 → 150ms 一档 → 恰一次重试）；无锁路径逐位不变（busy 等待只在撞锁时
//   生效——零竞争面上本选项零观感，r2040a 常态墙承重）。
// 连接盘点表（src/ 全部 QSQLITE 连接设置点，禁漏一处；r2040d 逐连接放置钉 = 逐行扫本 API 面）：
//   ① kConn/createWorld ② kConn/openWorld（保存链事务写面）
//   ③ kScanConn/worldList ④ kRenameConn/renameWorld（列表/重命名非保存链：归零后锁下从
//     「秒级等待后失败」变「瞬时失败」——列表跳该文件仍带 qWarning、重命名仍诚实 false，语义
//     原样只是更快）
//   ⑤⑥ savecoordinator.cpp 台账连接 recover/coordUpsert（台账读面 = 保存链首试第一撞锁点 +
//     台账写面 marker-first 闸）
//   ⑦ chunkstore.cpp openStoreConnection（附加表读写唯一设置点）
//   ⑧ savebridge.cpp 探锁连接 = t1064 已显式归零（不动；r2038d 源钉在案）。
// 自锁竞态面论证（0 值的代价 = 瞬时锁竞争也即败，须证进程内零并发窗口）：src/ 全部 SQLite
//   访问都在 GUI 线程串行——本文件连接同线程开-用-关，saveAll 单事务自持自放；保存时冲洗
//   （附加表写；Main.qml runExitSave 先冲洗后三写，两段零时间重叠）与本文件事务不交错；台账
//   连接在协调层五段流程内开-用-关；worldgen/mesh worker 线程不触任何 Sql 面 → 内部锁竞争
//   零窗口，0 值不引入自败面。
// worldstore 零 diff 审计面登记例外：本单改的只有**连接设置**（每设置点一行 setConnectOptions
//   + 本注释块），协议/数据/schema/返回语义/t974 计数口径零触碰——审计例外由主控关单时登记。

WorldStore::WorldStore(QObject *parent) : QObject(parent) {}

// t974 写完成计数统一收口（契约见 worldstore.h saveOkCount Q_PROPERTY 注释）：只在「持久化调用
//   已成功落盘」的成功尾调用 —— saveAll 在 commit 成功后、savePlayerData / saveProgress 在 exec
//   成功后。失败（事务回滚 / exec 失败）绝不调用，使计数与 false 返回值同源互证。
void WorldStore::noteSaveOk()
{
    ++m_saveOkCount;
    emit saveOkCountChanged();
}

WorldStore::~WorldStore()
{
    // 析构关连接（Qt Sql 连接需显式 removeDatabase 释放文件句柄； QFile 删除等在连接关闭后才能生效）。
    if (QSqlDatabase::contains(kConn))
        QSqlDatabase::removeDatabase(kConn);
}

void WorldStore::setWorld(World *w)
{
    if (m_world == w) return;
    m_world = w;
    emit worldChanged();
}

// saves/ 目录解析（仿 main.cpp resolveLogFilePath）：
//   1) <exeDir>/../saves → 开发期（exe 在 <工程根>/build/ → <工程根>/saves，开发者一眼能找到）。
//   2) AppLocalDataLocation/saves → 部署期（exe 装在 Program Files 等无写权限处）。
//   mkpath 既是「确保目录存在」也是「写权限探针」：不可写 → 跳到下一个候选。都失败 → 兜底回 exe 同级。
QString WorldStore::savesDir() const
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/../saves")).absolutePath(),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/saves")).absolutePath()
    };
    for (const QString &dir : candidates) {
        if (!dir.isEmpty() && QDir().mkpath(dir))
            return dir;
    }
    return exeDir + QStringLiteral("/saves"); // 兜底（mkpath 在 exe 同级仍可能成功）
}

QString WorldStore::dbPath(const QString &file) const
{
    return QDir(savesDir()).absoluteFilePath(file);
}

QString WorldStore::sanitizeName(const QString &name)
{
    // 保留字母数字 / 中文 / -_，其余替为 _；空 → "world"。防止路径穿越（../）与非法文件名字符。
    QString s = name.trimmed();
    s.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z\\u4e00-\\u9fff\\-_]")), QStringLiteral("_"));
    if (s.isEmpty()) s = QStringLiteral("world");
    return s;
}

// 在当前连接上初始化 schema（IF NOT EXISTS）+ 校验 / 写 user_version。
//   新库（user_version=0）→ 建表 + 写 kSchemaVersion。
//   已有库 user_version==kSchemaVersion → ok（建表幂等补缺）。
//   user_version > kSchemaVersion（高版本程序写的库）→ 拒绝（返回 false；caller qWarning，PLAN §2-E）。
bool WorldStore::initSchema()
{
    QSqlQuery q(QSqlDatabase::database(kConn));
    // user_version 读（PRAGMA 返回单行单列）。[t1139 F02] exec 返回值补核：读失败按开库病拒绝
    //   （旧面 = 失败被吞、version 恒 0 → 高版本库被当新库误写 schema 的降级面）。
    if (!q.exec(QStringLiteral("PRAGMA user_version"))) {
        qCCritical(lcSave) << "initSchema: user_version read failed:" << q.lastError().text();
        return false;
    }
    int version = 0;
    if (q.next()) version = q.value(0).toInt();
    if (version > kSchemaVersion) {
        qCCritical(lcSave) << "save file user_version" << version << "newer than supported"
                           << kSchemaVersion << "-> refuse to open (manual migration needed)";
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS world_meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL)"))) {
        qCCritical(lcSave) << "create world_meta failed:" << q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chunks ("
            "  cx INTEGER NOT NULL,"
            "  cz INTEGER NOT NULL,"
            "  voxels BLOB NOT NULL,"
            "  states BLOB NOT NULL,"
            "  light BLOB NOT NULL,"
            "  PRIMARY KEY (cx, cz))"))) {
        qCCritical(lcSave) << "create chunks failed:" << q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS player_state ("
            "  id INTEGER PRIMARY KEY DEFAULT 0,"
            "  data TEXT NOT NULL)"))) {
        qCCritical(lcSave) << "create player_state failed:" << q.lastError().text();
        return false;
    }
    // t188 箱子内容表：每只箱子（按方块世界坐标键控）一行，slots 序列化为 JSON 文本（同 player_state 自描述
    //   模式）。纯加表 —— 旧库（v1）IF NOT EXISTS 幂等补建，无数据迁移负担。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chests ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create chests failed:" << q.lastError().text();
        return false;
    }
    // t177 二轮复盘 熔炉内容表：同 chests 模式 —— 每只熔炉（按方块世界坐标键控）一行，data 列存整个
    //   {slots, burn, smelt} 的 JSON 文本（同 player_state 自描述）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，
    //   无数据迁移负担；schema 版本不 bump（IF NOT EXISTS 纯加表对老库向前兼容，见 worldstore.h kSchemaVersion 注释）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS furnaces ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create furnaces failed:" << q.lastError().text();
        return false;
    }
    // t542 发射器内容表：同 chests / furnaces 模式 —— 每只发射器（按方块世界坐标键控）一行，data 列存整个
    //   {slots} 的 JSON 文本（同 player_state / chests 自描述）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，
    //   无数据迁移负担；schema 版本不 bump（IF NOT EXISTS 纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS dispensers ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create dispensers failed:" << q.lastError().text();
        return false;
    }
    // t1080 漏斗内容表：同 chests / furnaces / dispensers 模式 —— 每只漏斗（按方块世界坐标键控）一行，
    //   data 列存整个 {slots} 的 JSON 文本（自描述）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，无迁移负担；
    //   schema 版本不 bump（纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hoppers ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create hoppers failed:" << q.lastError().text();
        return false;
    }
    // t1097 酿造内容表：同 hoppers 模式 —— 每台酿造台（按方块世界坐标键控）一行，data 列存
    //   {slots, progress, fuelOps} 的 JSON 文本（自描述）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，
    //   无迁移负担；schema 版本不 bump（纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS brewing ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create brewing failed:" << q.lastError().text();
        return false;
    }
    // t1113 牌子文本表：同 brewing 模式 —— 每块牌子（按方块世界坐标键控）一行，data 列存
    //   {lines:[4 行文本]} 的 JSON 文本（自描述；首个方块附挂文本面）。纯加表 —— 旧库 IF NOT EXISTS
    //   幂等补建，无迁移负担；schema 版本不 bump（纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sign_texts ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  data TEXT NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create sign_texts table failed:" << q.lastError().text();
        return false;
    }
    // t1132 地图数据集表：单行表（id=0，每库一份 = 共享口径单份数据集——era per-map 独立持久化的
    //   工程偏离如实登记，见 mapstore.h 存档面裁定）。pixels BLOB 存全幅 ARGB32 字节（自描述尺寸三键
    //   width/depth/revision 随行）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，无迁移负担；schema 版本
    //   不 bump（纯加表对老库向前兼容，同 sign_texts 门）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS map_dataset ("
            "  id INTEGER PRIMARY KEY CHECK (id = 0),"
            "  width INTEGER NOT NULL,"
            "  depth INTEGER NOT NULL,"
            "  revision INTEGER NOT NULL,"
            "  pixels BLOB NOT NULL)"))) {
        qCCritical(lcSave) << "create map_dataset table failed:" << q.lastError().text();
        return false;
    }
    // progress 表（progress 新系统）：玩家进度（统计 + 成就）单行表，key 固定 'main'，data 存 PlayerProgress::toVariant()
    //   的 JSON。IF NOT EXISTS 幂等补建（schema 版本不 bump，同 chests/furnaces，纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS progress ("
            "  key TEXT PRIMARY KEY,"
            "  data TEXT NOT NULL)"))) {
        qCCritical(lcSave) << "create progress failed:" << q.lastError().text();
        return false;
    }
    // t1133 生物持久化表（ENTITY-01）：每只存活生物一行（id = 行序主键，非 EntityId——mob 族无
    //   EntityId，槽下标会话语义不入档）。持久字段列集 = EntityManager 头注字段清单（种类/位置/
    //   血量/幼体与成长/驯服/归属[单机=驯服旗]/坐下/猫变体/羊毛/剪毛/史莱姆档/猪鞍）；AI 态零列
    //   （临时面不入档）。纯加表 —— 旧库 IF NOT EXISTS 幂等补建，无迁移负担；schema 版本不 bump
    //   （纯加表对老库向前兼容，同 sign_texts/map_dataset 门）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS entities ("
            "  id INTEGER PRIMARY KEY,"
            "  kind INTEGER NOT NULL,"
            "  type INTEGER NOT NULL,"
            "  x REAL NOT NULL,"
            "  y REAL NOT NULL,"
            "  z REAL NOT NULL,"
            "  color TEXT NOT NULL,"
            "  mh INTEGER NOT NULL,"
            "  hp INTEGER NOT NULL,"
            "  baby INTEGER NOT NULL,"
            "  grow REAL NOT NULL,"
            "  wt INTEGER NOT NULL,"
            "  ws INTEGER NOT NULL,"
            "  ot INTEGER NOT NULL,"
            "  os INTEGER NOT NULL,"
            "  ov INTEGER NOT NULL,"
            "  sw INTEGER NOT NULL,"
            "  swd INTEGER NOT NULL,"
            "  sh INTEGER NOT NULL,"
            "  ss INTEGER NOT NULL,"
            "  sd INTEGER NOT NULL)"))) {
        qCCritical(lcSave) << "create entities table failed:" << q.lastError().text();
        return false;
    }
    // t1137 活塞两拍动画侧表（PISTON-ANIM）：每个在册占位格一行（坐标主键——动画项全档快照语义，
    //   载荷 = World::exportPistonAnims 产物形）。持久字段 = era TilePiston NBT 五键的整数拍映射
    //   （storedId/storedState/facing/extending + beats=两拍剩余；era progress float 渲染半拍与
    //   headFlag 渲染位不入档——近似度登记，t1137_jar_piston_persist_bud.txt 定谳六）。纯加表 ——
    //   旧库 IF NOT EXISTS 幂等补建，无迁移负担；schema 版本不 bump（纯加表对老库向前兼容，
    //   同 entities/sign_texts 门）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS piston_anims ("
            "  x INTEGER NOT NULL,"
            "  y INTEGER NOT NULL,"
            "  z INTEGER NOT NULL,"
            "  id INTEGER NOT NULL,"
            "  st INTEGER NOT NULL,"
            "  fc INTEGER NOT NULL,"
            "  ex INTEGER NOT NULL,"
            "  beats INTEGER NOT NULL,"
            "  PRIMARY KEY (x, y, z))"))) {
        qCCritical(lcSave) << "create piston_anims table failed:" << q.lastError().text();
        return false;
    }
    // 写 user_version（新库 0→kSchemaVersion；旧库同版本幂等；无 harm）。[t1139 F02] 返回值补核
    //   （审查点名：版本戳写失败若被吞 = 升级错误处理风险面）。
    if (!q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        qCCritical(lcSave) << "initSchema: user_version write failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QVariantList WorldStore::worldList() const
{
    QVariantList out;
    const QDir dir(savesDir());
    if (!dir.exists()) return out;
    // 用独立扫描连接避免与主连接冲突（worldList 可能在主连接已打开时被调 —— 切世界前看列表）。
    static const char *const kScanConn = "voxelsandbox_worldstore_scan";
    const QStringList files = dir.entryList({QStringLiteral("*.sqlite")}, QDir::Files, QDir::Time);
    for (const QString &file : files) {
        if (QSqlDatabase::contains(kScanConn))
            QSqlDatabase::removeDatabase(kScanConn);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kScanConn);
            db.setDatabaseName(dir.absoluteFilePath(file));
            db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（盘点表见文件头）
            if (!db.open()) {
                qCWarning(lcSave) << "worldList: cannot open" << file << ":" << db.lastError().text();
                continue;
            }
            QSqlQuery q(db);
            // 只读 meta（不调 initSchema —— 列表不应改库）；版本不符 → 跳过该库但仍列入（灰显交 UI 判）。
            QVariantMap meta;
            if (q.exec(QStringLiteral("SELECT key, value FROM world_meta"))) {
                while (q.next()) meta.insert(q.value(0).toString(), q.value(1).toString());
            }
            QVariantMap item;
            item.insert(QStringLiteral("file"), file);
            item.insert(QStringLiteral("name"), meta.value(QStringLiteral("name"), file));
            item.insert(QStringLiteral("seed"), meta.value(QStringLiteral("seed"), QStringLiteral("0")).toInt());
            item.insert(QStringLiteral("width"), meta.value(QStringLiteral("width"), QStringLiteral("80")).toInt());
            item.insert(QStringLiteral("height"), meta.value(QStringLiteral("height"), QStringLiteral("64")).toInt());
            item.insert(QStringLiteral("depth"), meta.value(QStringLiteral("depth"), QStringLiteral("80")).toInt());
            item.insert(QStringLiteral("playedAt"), meta.value(QStringLiteral("playedAt"), QStringLiteral("0")).toLongLong());
            out.append(item);
        }
        if (QSqlDatabase::contains(kScanConn))
            QSqlDatabase::removeDatabase(kScanConn);
    }
    return out;
}

QString WorldStore::createWorld(const QString &name, int seed)
{
    const QString dir = savesDir();
    QDir().mkpath(dir);
    // 文件名 = 净化名 + .sqlite；重名 → 追加 _2/_3 ... 直至无碰撞。
    QString base = sanitizeName(name);
    QString file = base + QStringLiteral(".sqlite");
    for (int n = 2; QFile::exists(dbPath(file)); ++n)
        file = base + QStringLiteral("_%1.sqlite").arg(n);

    if (QSqlDatabase::contains(kConn)) QSqlDatabase::removeDatabase(kConn);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConn);
        db.setDatabaseName(dbPath(file));
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（盘点表①②）
        if (!db.open()) {
            qCCritical(lcSave) << "createWorld: cannot open" << file << ":" << db.lastError().text();
            return QString();
        }
        if (!initSchema()) {
            qCCritical(lcSave) << "createWorld: schema init failed for" << file;
            return QString();
        }
        // meta：name（原始名）/ seed / dims（固定 80×80×64）/ created / played（0 = 未游玩）。
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO world_meta (key, value) VALUES (?, ?)"));
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); // 见下方 include
        const QList<QPair<QString, QString>> metas = {
            {QStringLiteral("name"), name},
            {QStringLiteral("seed"), QString::number(seed)},
            {QStringLiteral("width"), QStringLiteral("80")},
            {QStringLiteral("height"), QStringLiteral("64")},
            {QStringLiteral("depth"), QStringLiteral("80")},
            {QStringLiteral("created"), QString::number(now)},
            {QStringLiteral("playedAt"), QStringLiteral("0")},
            // t382：新世界出生即当前 world_version（无需迁移；openWorld 的 migrateWorldData 据此 no-op）。
            {QStringLiteral("world_version"), QString::number(kWorldVersion)}
        };
        for (const auto &kv : metas) {
            q.addBindValue(kv.first);
            q.addBindValue(kv.second);
            if (!q.exec()) {
                qCCritical(lcSave) << "createWorld: meta insert failed:" << q.lastError().text();
                return QString();
            }
        }
    }
    m_open = true;
    m_openFile = file;
    qCInfo(lcSave) << "created world" << file << "seed" << seed;
    return file;
}

bool WorldStore::deleteWorld(const QString &file)
{
    const QString path = dbPath(file);
    // 必须先关连接（若删的是当前打开库）→ 否则 Windows 文件锁致删除失败。
    if (m_open && m_openFile == file) closeWorld();
    if (!QFile::exists(path)) {
        qCWarning(lcSave) << "deleteWorld: not found" << path;
        return false;
    }
    // t191：配套删截图封面 PNG（与 .sqlite 并排的 sidecar）。文件锁在 .sqlite 上，PNG 可直接删；
    //   不存在 / 删失败不阻断删世界（cover 是附属，主库删除仍进行）。
    deleteCover(file);
    if (!QFile::remove(path)) {
        qCWarning(lcSave) << "deleteWorld: remove failed" << path;
        return false;
    }
    qCInfo(lcSave) << "deleted world" << file;
    return true;
}

// t192 重命名世界：只改 world_meta 的 name（.sqlite 文件名不动，文件名是内部唯一键 —— 改文件名会引入路径
//   穿越 / 跨文件系统重命名复杂度且无用户可见收益）。用独立连接（kRenameConn，仿 worldList 的 kScanConn）：
//   renameWorld 在世界列表 UI 触发、通常当前无库打开，独立连接避免与主连接耦合，也覆盖「重命名当前打开库」
//   的边角情形（SQLite 多连接并发，主连接此刻无 in-flight 事务）。失败 → false + qWarning（§2-E）。
bool WorldStore::renameWorld(const QString &file, const QString &newName)
{
    const QString path = dbPath(file);
    if (!QFile::exists(path)) {
        qCWarning(lcSave) << "renameWorld: not found" << path;
        return false;
    }
    // 空白名 → 回退默认（与 createWorld 同语义），免世界列表出现无名条目。
    const QString name = newName.trimmed().isEmpty() ? QStringLiteral("新世界") : newName.trimmed();

    static const char *const kRenameConn = "voxelsandbox_worldstore_rename";
    if (QSqlDatabase::contains(kRenameConn))
        QSqlDatabase::removeDatabase(kRenameConn);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kRenameConn);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（盘点表③④）
        if (!db.open()) {
            qCWarning(lcSave) << "renameWorld: cannot open" << file << ":" << db.lastError().text();
        } else {
            // UPDATE 既有 'name' 行（createWorld 总会写 name，故行必存在）；语义「重命名=改既有名」。
            QSqlQuery q(db);
            q.prepare(QStringLiteral("UPDATE world_meta SET value = ? WHERE key = 'name'"));
            q.addBindValue(name);
            if (!q.exec()) {
                qCWarning(lcSave) << "renameWorld: update failed:" << q.lastError().text();
            } else {
                ok = true;
                qCInfo(lcSave) << "renamed world" << file << "->" << name;
            }
        }
    }
    if (QSqlDatabase::contains(kRenameConn))
        QSqlDatabase::removeDatabase(kRenameConn);
    return ok;
}

// t191 封面 PNG 路径：与 .sqlite 同名并排（saves/<completeBaseName>.png）。file 含 .sqlite 后缀；
//   completeBaseName 去「最后一个」扩展名（"a.sqlite"→"a"、"a.b.sqlite"→"a.b"），与 dbPath 同 savesDir。
QString WorldStore::coverPath(const QString &file) const
{
    const QString base = QFileInfo(file).completeBaseName();
    return QDir(savesDir()).absoluteFilePath(base + QStringLiteral(".png"));
}

// t191 把 grabToImage 拿到的 QImage 存为封面 PNG。image 来自 QML 的 grabResult.image（QVariant 包 QImage）。
//   null 图 / 写盘失败 → false + qWarning（caller 不阻塞退出，§2-E 降级为「无封面」灰块）。
bool WorldStore::saveCover(const QString &file, const QVariant &image)
{
    const QImage img = qvariant_cast<QImage>(image);
    if (img.isNull()) {
        qCWarning(lcSave) << "saveCover: null image for" << file;
        return false;
    }
    const QString path = coverPath(file);
    // QImage::save 据扩展名选格式（.png → PNG）；写盘失败（目录不可写 / 磁盘满）→ false。
    if (!img.save(path, "PNG")) {
        qCWarning(lcSave) << "saveCover: QImage::save failed:" << path;
        return false;
    }
    qCInfo(lcSave) << "saved cover for" << file << "->" << path;
    return true;
}

// t191 删封面 PNG（deleteWorld 内部调，也作 Q_INVOKABLE 供外部按需清理）。不存在视为成功（幂等）。
bool WorldStore::deleteCover(const QString &file)
{
    const QString path = coverPath(file);
    if (!QFile::exists(path)) return true;
    if (!QFile::remove(path)) {
        qCWarning(lcSave) << "deleteCover: remove failed:" << path;
        return false;
    }
    return true;
}

bool WorldStore::openWorld(const QString &file)
{
    closeWorld();
    if (QSqlDatabase::contains(kConn)) QSqlDatabase::removeDatabase(kConn);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConn);
        db.setDatabaseName(dbPath(file));
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（盘点表①②）
        if (!db.open()) {
            qCCritical(lcSave) << "openWorld: cannot open" << file << ":" << db.lastError().text();
            return false;
        }
        if (!initSchema()) {
            qCCritical(lcSave) << "openWorld: schema init / version check failed for" << file;
            QSqlDatabase::removeDatabase(kConn);
            return false;
        }
        // t382：把存档数据迁移到当前 world_version（旧存档 block-id 重排等）。失败 → 拒绝打开（§2-E）。
        //   新世界（createWorld 已写 world_version=kWorldVersion）→ migrateWorldData 内 no-op。
        if (!migrateWorldData()) {
            qCCritical(lcSave) << "openWorld: data migration failed for" << file;
            QSqlDatabase::removeDatabase(kConn);
            return false;
        }
    }
    m_open = true;
    m_openFile = file;
    qCInfo(lcSave) << "opened world" << file;
    return true;
}

void WorldStore::closeWorld()
{
    if (QSqlDatabase::contains(kConn))
        QSqlDatabase::removeDatabase(kConn);
    m_open = false;
    m_openFile.clear();
}

bool WorldStore::saveAll(const QString &name, const QVariantList &chests, const QVariantList &furnaces, const QVariantList &dispensers,
                         const QVariantMap &worldTime, const QVariantMap &bedSpawn, const QVariantList &hoppers,
                         const QVariantList &brewingStands, const QVariantList &signs, const QVariantMap &mapDataset)
{
    if (!m_open || !m_world) {
        qCWarning(lcSave) << "saveAll: no open db or world";
        return false;
    }
    // t1129 事务粒度原语化：裸 saveAll 面行为逐字节原样（自有事务 + 成功 +1 计数——r2015a 两域
    //   正交钉 / 旧档 Fresh 回归腿的承重面），表写体与原子多面保存域（writeWorldPart）共用同一段
    //   实现，杜绝第二份表写逻辑。
    if (!beginAtomicSave())
        return false;
    if (!writeWorldPart(name, chests, furnaces, dispensers, worldTime, bedSpawn, hoppers,
                        brewingStands, signs, mapDataset)) {
        rollbackAtomicSave();
        return false;
    }
    if (!commitAtomicSave(1))
        return false;
    return true;
}

// ── t1129 SAVE-01 原子多面保存域实现（契约见 worldstore.h 同名段）────────────────────────────

bool WorldStore::beginAtomicSave()
{
    if (!m_open) {
        qCWarning(lcSave) << "beginAtomicSave: no open db";
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(kConn);
    if (!db.transaction()) {
        qCCritical(lcSave) << "beginAtomicSave: begin transaction failed:" << db.lastError().text();
        return false;
    }
    m_lastWorldChunkCount = 0; // 提交日志面归位（本次事务的 chunk 数由 world 段回填）
    return true;
}

bool WorldStore::writeWorldPart(const QString &name, const QVariantList &chests, const QVariantList &furnaces,
                                const QVariantList &dispensers, const QVariantMap &worldTime,
                                const QVariantMap &bedSpawn, const QVariantList &hoppers,
                                const QVariantList &brewingStands, const QVariantList &signs,
                                const QVariantMap &mapDataset)
{
    if (!m_open || !m_world) {
        qCWarning(lcSave) << "writeWorldPart: no open db or world";
        return false;
    }
    const ChunkManager &cm = m_world->chunks();
    QSqlDatabase db = QSqlDatabase::database(kConn);
    // 清空旧 chunks（upsert 全量重写最简；25 chunk 量级全删全插 < 1ms，无需增量）。
    // [t1139 F02] 返回值检查补核：具名查询 + exec 失败 qCCritical + 短路 false —— 由调用域现有
    //   rollbackAtomicSave 回滚兜底（SQLite ABORT 语义：语句级失败回滚本语句、事务保持活跃——
    //   旧面 = 匿名临时查询丢弃返回值，空快照 / 键不重叠场景语句失败后事务继续提交 → 旧 chunks
    //   静默残留）。此处失败绝不走「继续 INSERT」路径（与 chests/hoppers 等容器表 DELETE 同门）。
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM chunks"))) {
        qCCritical(lcSave) << "writeWorldPart: chunks delete failed:" << del.lastError().text();
        return false;
    }

    QSqlQuery iq(db);
    iq.prepare(QStringLiteral(
        "INSERT INTO chunks (cx, cz, voxels, states, light) VALUES (?, ?, ?, ?, ?)"));
    int saved = 0;
    for (int cz = 0; cz < cm.chunksZ(); ++cz) {
        for (int cx = 0; cx < cm.chunksX(); ++cx) {
            const Chunk *c = cm.chunk(cx, cz);
            if (!c) continue;
            const size_t n = c->voxelCount();
            // QByteArray::fromRawData 不拷贝（仅读视图，addBindValue 会拷贝进 SQL 引擎，安全）。
            iq.addBindValue(cx);
            iq.addBindValue(cz);
            iq.addBindValue(QByteArray::fromRawData(reinterpret_cast<const char *>(c->voxelData()), int(n)));
            iq.addBindValue(QByteArray::fromRawData(reinterpret_cast<const char *>(c->stateData()), int(n)));
            iq.addBindValue(QByteArray::fromRawData(reinterpret_cast<const char *>(c->lightData()), int(n)));
            if (!iq.exec()) {
                qCCritical(lcSave) << "writeWorldPart: chunk insert failed at" << cx << cz << ":" << iq.lastError().text();
                return false;
            }
            ++saved;
        }
    }
    m_lastWorldChunkCount = saved; // 提交日志面（事务内暂存，commitAtomicSave 时如实打印）
    // 刷 meta：seed（terrain 确定性 + 旧版可重生兜底）/ dims / name / playedAt（= 本次保存时刻）。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery mq(db);
    mq.prepare(QStringLiteral("INSERT OR REPLACE INTO world_meta (key, value) VALUES (?, ?)"));
    QList<QPair<QString, QString>> metas = {
        {QStringLiteral("name"), name},
        {QStringLiteral("seed"), QString::number(m_world->seed())},
        {QStringLiteral("width"), QString::number(cm.width())},
        {QStringLiteral("height"), QString::number(cm.height())},
        {QStringLiteral("depth"), QString::number(cm.depth())},
        {QStringLiteral("playedAt"), QString::number(now)},
        // t382：每次保存都刷 world_version（确保存档始终标记为当前数据版本）+ chunk_count（完整性核查）。
        {QStringLiteral("world_version"), QString::number(kWorldVersion)},
        {QStringLiteral("chunk_count"), QString::number(saved)}
    };
    // t1016 世界时钟快照（caller 传非空 map 才写；同事务原子 —— 时间与地形同一存档点，杜绝「半新」
    //   存档）。phase 用 'g'/9 位有效数字：float 短往返表示（读回 toFloat 逐位还原）；day qint64 直接
    //   十进制；weather 枚举 int；t1046 weather_timer_ms = 当前态剩余毫秒 int（parity 台账低-5：
    //   等价 MC level.dat RainTime/ThunderTime 精确续跑——态唯一 + 单计时器，单键即可精确恢复剩余窗）。
    //   缺键跳过该键（不写半截快照）。
    if (!worldTime.isEmpty()) {
        if (worldTime.contains(QStringLiteral("phase")))
            metas.append({QStringLiteral("clock_phase"),
                          QString::number(worldTime.value(QStringLiteral("phase")).toFloat(), 'g', 9)});
        if (worldTime.contains(QStringLiteral("day")))
            metas.append({QStringLiteral("clock_day"),
                          QString::number(worldTime.value(QStringLiteral("day")).toLongLong())});
        if (worldTime.contains(QStringLiteral("weather")))
            metas.append({QStringLiteral("weather"),
                          QString::number(worldTime.value(QStringLiteral("weather")).toInt())});
        if (worldTime.contains(QStringLiteral("weatherTimerMs")))
            metas.append({QStringLiteral("weather_timer_ms"),
                          QString::number(worldTime.value(QStringLiteral("weatherTimerMs")).toLongLong())});
    }
    // t1024 床位重生锚（caller 传非空 map 才写；四键与 chunks / meta 同事务原子）。valid → 四键全写
    //   （bed_x 'g'9 float 短往返，同 clock_phase 口径）；!valid → 只写 bed_valid=0（显式失效位，
    //   挖锚床后退出存档 = 下次进世界不回填床位）。空 map（老探针 / 不感知床锚的 caller）→ 不写不删。
    if (!bedSpawn.isEmpty()) {
        if (bedSpawn.value(QStringLiteral("valid")).toBool()) {
            metas.append({QStringLiteral("bed_x"),
                          QString::number(bedSpawn.value(QStringLiteral("x")).toFloat(), 'g', 9)});
            metas.append({QStringLiteral("bed_y"),
                          QString::number(bedSpawn.value(QStringLiteral("y")).toFloat(), 'g', 9)});
            metas.append({QStringLiteral("bed_z"),
                          QString::number(bedSpawn.value(QStringLiteral("z")).toFloat(), 'g', 9)});
            metas.append({QStringLiteral("bed_valid"), QStringLiteral("1")});
        } else {
            metas.append({QStringLiteral("bed_valid"), QStringLiteral("0")});
        }
    }
    for (const auto &kv : metas) {
        mq.addBindValue(kv.first);
        mq.addBindValue(kv.second);
        if (!mq.exec()) {
            qCCritical(lcSave) << "writeWorldPart: meta update failed:" << mq.lastError().text();
            return false;
        }
    }
    // t188 箱子内容同事务落盘（chests 表 DELETE 全量 + INSERT；与 chunks / meta 原子提交）。
    if (!writeChests(chests))
        return false;
    // t177 二轮复盘 熔炉内容同事务落盘（furnaces 表 DELETE 全量 + INSERT；与 chunks / meta / chests 原子提交）。
    if (!writeFurnaces(furnaces))
        return false;
    // t542 发射器内容同事务落盘（dispensers 表 DELETE 全量 + INSERT；与 chunks / meta / chests / furnaces 原子提交）。
    if (!writeDispensers(dispensers))
        return false;
    // t1080 漏斗内容同事务落盘（hoppers 表 DELETE 全量 + INSERT；与 chunks / meta / 前述容器表原子提交）。
    if (!writeHoppers(hoppers))
        return false;
    // t1097 酿造内容同事务落盘（brewing 表 DELETE 全量 + INSERT；与 chunks / meta / 前述容器表原子提交）。
    if (!writeBrewing(brewingStands))
        return false;
    // t1113 牌子文本同事务落盘（sign_texts 表 DELETE 全量 + INSERT；与 chunks / meta / 前述容器表原子提交）。
    if (!writeSigns(signs))
        return false;
    // t1132 地图数据集同事务落盘（map_dataset 表 DELETE 全量 + present 才 INSERT 单行；与 chunks /
    //   meta / 前述容器表原子提交——探索面与地形同一存档点）。
    if (!writeMapDataset(mapDataset))
        return false;
    return true;
}

bool WorldStore::writePlayerPart(const QVariantMap &data)
{
    if (!m_open) {
        qCWarning(lcSave) << "writePlayerPart: no open db";
        return false;
    }
    // QVariantMap → JSON 文本（QJsonDocument::fromVariant 处理嵌套 QVariantList<QVariantMap> 等）。
    const QJsonDocument doc = QJsonDocument::fromVariant(data);
    const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    QSqlQuery q(QSqlDatabase::database(kConn));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO player_state (id, data) VALUES (0, ?)"));
    q.addBindValue(json);
    if (!q.exec()) {
        qCCritical(lcSave) << "writePlayerPart: insert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool WorldStore::writeProgressPart(const QVariantMap &progress)
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(kConn));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO progress (key, data) VALUES ('main', ?)"));
    const QJsonDocument doc = QJsonDocument::fromVariant(progress);
    q.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        qCCritical(lcSave) << "writeProgressPart failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool WorldStore::stampLedgerKeyInTxn(const char *table, const char *key, qint64 value)
{
    if (!m_open) return false;
    // 表名/键名由调用域注入（单一权威留在调用域——本域源文零台账域字面，r2015d 盲区钉幸存）。
    //   表若缺席（异常序：本面只在协调层 marker 落地后可达）→ IF NOT EXISTS 幂等补建，事务内
    //   DDL 合法（SQLite DDL 可回滚）。
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (key TEXT PRIMARY KEY, value TEXT NOT NULL)")
                    .arg(QLatin1String(table)))) {
        qCCritical(lcSave) << "stampLedgerKeyInTxn: table ensure failed:" << q.lastError().text();
        return false;
    }
    QSqlQuery u(QSqlDatabase::database(kConn));
    u.prepare(QStringLiteral("INSERT OR REPLACE INTO %1 (key, value) VALUES (?, ?)")
                  .arg(QLatin1String(table)));
    u.addBindValue(QLatin1String(key));
    u.addBindValue(QString::number(value));
    if (!u.exec()) {
        qCCritical(lcSave) << "stampLedgerKeyInTxn: upsert failed:" << u.lastError().text();
        return false;
    }
    return true;
}

bool WorldStore::commitAtomicSave(int partsWritten)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    if (!db.commit()) {
        qCCritical(lcSave) << "commitAtomicSave: commit failed:" << db.lastError().text();
        db.rollback();
        return false; // 零计数零部分写（t974 口径：失败绝不计数）
    }
    // t974 段=1 口径逐位同旧：按本次事务实际写段数补计数（每段一次 noteSaveOk = 一次 emit）。
    for (int i = 0; i < partsWritten; ++i)
        noteSaveOk();
    qCInfo(lcSave) << "atomic save committed (" << partsWritten << "part(s),"
                   << m_lastWorldChunkCount << "chunks) for world" << m_openFile;
    return true;
}

void WorldStore::rollbackAtomicSave()
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    if (db.rollback())
        qCInfo(lcSave) << "atomic save rolled back for world" << m_openFile;
    else
        qCCritical(lcSave) << "atomic save rollback failed:" << db.lastError().text();
}

bool WorldStore::runInSaveTransaction(const std::function<bool(QSqlDatabase &)> &work)
{
    if (!m_open) return false;
    if (!work) return true; // 空缝 = 零动作放行（fixed 世界生产形态）
    QSqlDatabase db = QSqlDatabase::database(kConn);
    return work(db);
}

QVariantMap WorldStore::loadMeta() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT key, value FROM world_meta"))) return out;
    while (q.next()) out.insert(q.value(0).toString(), q.value(1).toString());
    return out;
}

// t1016 读世界时钟快照（头注释见 .h）。逐键缺省：clock_phase→0.0（新世界默认相位）、clock_day→0、
//   weather→0（Clear 晴天）—— 旧存档缺字段拿默认值恢复，加载端行为 = 新世界首帧，不炸不跳。
//   phase 以 toFloat 还原（写侧 'g'/9 位有效数字为 float 短往返表示，逐位还原）；day toLongLong。
//   review0906 #14：hasWeather = 存档是否**真带** weather 键（缺键默认 0 与「真存过 Clear」不可区分
//   = enterWorld 无条件 setWeatherState 把 resetWeather 的首场晴偏短窗（20/45s）重抽为常规窗
//   45/120s 的根因）。消费端（Main.qml enterWorld）仅 hasWeather 才恢复天气态；缺键走 resetWeather
//   原窗（初始短窗口径恢复）。t1046 hasWeatherTimer / weatherTimerMs = 剩余时长键（world_meta
//   weather_timer_ms，毫秒 int）—— 缺键（旧档）→ hasWeatherTimer=false，恢复端走 setWeatherState
//   的随机重抽窗；真带 → setWeatherRemainingSec 精确续跑剩余窗（MC RainTime/ThunderTime 口径）。
QVariantMap WorldStore::loadWorldTime() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT key, value FROM world_meta"))) return out;
    QVariantMap meta;
    while (q.next()) meta.insert(q.value(0).toString(), q.value(1).toString());
    out.insert(QStringLiteral("phase"),
               meta.contains(QStringLiteral("clock_phase"))
                   ? QVariant(meta.value(QStringLiteral("clock_phase")).toString().toFloat())
                   : QVariant(0.0f));
    out.insert(QStringLiteral("day"),
               meta.contains(QStringLiteral("clock_day"))
                   ? QVariant(qlonglong(meta.value(QStringLiteral("clock_day")).toLongLong()))
                   : QVariant(qlonglong(0)));
    out.insert(QStringLiteral("hasWeather"),
               QVariant(meta.contains(QStringLiteral("weather"))));
    out.insert(QStringLiteral("weather"),
               meta.contains(QStringLiteral("weather"))
                   ? QVariant(meta.value(QStringLiteral("weather")).toInt())
                   : QVariant(0));
    out.insert(QStringLiteral("hasWeatherTimer"),
               QVariant(meta.contains(QStringLiteral("weather_timer_ms"))));
    out.insert(QStringLiteral("weatherTimerMs"),
               meta.contains(QStringLiteral("weather_timer_ms"))
                   ? QVariant(qlonglong(meta.value(QStringLiteral("weather_timer_ms")).toLongLong()))
                   : QVariant(qlonglong(0)));
    return out;
}

// t1024 读床位重生锚（头注释见 .h）。逐键缺省：bed_valid 缺或 0 / 坐标缺 → hasBed=false（coords 0）
//   —— 旧存档（t1024 前）无床键 = 「从未睡过床」，回世界出生点重生（t388 起既有语义）。valid=1 但
//   坐标键缺（异常半写；事务原子下不应出现）→ 防御性按无床处理。消费端仅 hasBed 才回填。
QVariantMap WorldStore::loadBedSpawn() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT key, value FROM world_meta"))) return out;
    QVariantMap meta;
    while (q.next()) meta.insert(q.value(0).toString(), q.value(1).toString());
    const bool hasCoords = meta.contains(QStringLiteral("bed_x"))
        && meta.contains(QStringLiteral("bed_y"))
        && meta.contains(QStringLiteral("bed_z"));
    const bool valid = meta.contains(QStringLiteral("bed_valid"))
        && meta.value(QStringLiteral("bed_valid")).toInt() == 1 && hasCoords;
    out.insert(QStringLiteral("hasBed"), QVariant(valid));
    out.insert(QStringLiteral("x"), QVariant(hasCoords
        ? double(meta.value(QStringLiteral("bed_x")).toString().toFloat()) : double(0)));
    out.insert(QStringLiteral("y"), QVariant(hasCoords
        ? double(meta.value(QStringLiteral("bed_y")).toString().toFloat()) : double(0)));
    out.insert(QStringLiteral("z"), QVariant(hasCoords
        ? double(meta.value(QStringLiteral("bed_z")).toString().toFloat()) : double(0)));
    return out;
}

int WorldStore::loadChunks()
{
    if (!m_open || !m_world) {
        qCWarning(lcSave) << "loadChunks: no open db or world";
        return -1;
    }
    // chunk() 是 const 方法但返回可变 Chunk*（unique_ptr pointee 非常）→ 经 const chunks() 链即可写回。
    const ChunkManager &cm = m_world->chunks();
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT cx, cz, voxels, states, light FROM chunks"))) {
        qCCritical(lcSave) << "loadChunks: select failed:" << q.lastError().text();
        return -1;
    }
    int loaded = 0;
    while (q.next()) {
        const int cx = q.value(0).toInt();
        const int cz = q.value(1).toInt();
        Chunk *c = cm.chunk(cx, cz);
        if (!c) continue; // 尺寸不符（存档 dims ≠ 当前世界）→ 跳过
        const QByteArray voxels = q.value(2).toByteArray();
        const QByteArray states = q.value(3).toByteArray();
        const QByteArray light = q.value(4).toByteArray();
        const size_t n = c->voxelCount();
        // 尺寸校验（dim 变更 / 损坏 → 跳过该 chunk，不写入半截数据）。
        if (size_t(voxels.size()) != n || size_t(states.size()) != n || size_t(light.size()) != n) {
            qCWarning(lcSave) << "loadChunks: size mismatch at" << cx << cz
                              << "expected" << qint64(n) << "got" << voxels.size() << states.size() << light.size();
            continue;
        }
        std::memcpy(c->voxelDataMut(), voxels.constData(), n);
        std::memcpy(c->stateDataMut(), states.constData(), n);
        std::memcpy(c->lightDataMut(), light.constData(), n);
        ++loaded;
    }
    // t382 完整性核查：实际加载 chunk 数应与存档 chunk_count 一致。不一致 = 有 chunk 因尺寸不符 / 损坏被
    //   跳过（上方 size mismatch continue）→ 告警但不阻断加载（已加载的部分仍可用，§2-E 降级运行）。
    {
        QSqlQuery cq(QSqlDatabase::database(kConn));
        if (cq.exec(QStringLiteral("SELECT value FROM world_meta WHERE key='chunk_count'")) && cq.next()) {
            const int stored = cq.value(0).toString().toInt();
            if (stored != loaded)
                qCWarning(lcSave) << "loadChunks: chunk_count mismatch — stored" << stored
                                  << "loaded" << loaded
                                  << "(some chunks skipped: size/dim mismatch or corruption)";
        }
    }
    qCInfo(lcSave) << "loaded" << loaded << "chunks for world" << m_openFile;
    return loaded;
}

bool WorldStore::savePlayerData(const QVariantMap &data)
{
    if (!m_open) {
        qCWarning(lcSave) << "savePlayerData: no open db";
        return false;
    }
    // t1129：写体与原子保存域共享（writePlayerPart 无计数）；裸面保持「exec 成功即 +1」口径。
    if (!writePlayerPart(data))
        return false;
    noteSaveOk();   // t974：exec 成功 = 玩家态已落盘（调用返回即写完成，同步无 deferred）
    return true;
}

QVariantMap WorldStore::loadPlayerData() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT data FROM player_state WHERE id = 0"))) return out;
    if (!q.next()) return out; // 无玩家态（首次进入）→ 空 Map（caller 用默认出生态）
    const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
    return doc.toVariant().toMap();
}

bool WorldStore::hasPlayerData() const
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(kConn));
    // [t1139 F02] 顺查补核：探针失败按「无数据」降级语义不变（返回 false 同旧），仅补 qCCritical
    //   留痕（旧面 = 丢弃 exec 返回值静默走 false）。
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM player_state WHERE id = 0"))) {
        qCCritical(lcSave) << "hasPlayerData: probe failed:" << q.lastError().text();
        return false;
    }
    return q.next() && q.value(0).toInt() > 0;
}

bool WorldStore::hasChunks() const
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(kConn));
    // [t1139 F02] 同上（hasPlayerData 同门）。
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM chunks"))) {
        qCCritical(lcSave) << "hasChunks: probe failed:" << q.lastError().text();
        return false;
    }
    return q.next() && q.value(0).toInt() > 0;
}

// t188 箱子落盘：DELETE 全量 + INSERT 每只箱子（坐标列 + slots JSON 文本）。调用方（saveAll）已开事务，
//   本方法不 BEGIN/COMMIT（同事务原子）。chests 形状 = ChestStore::allChests() 产物：每项
//   {x,y,z,slots:[{id,count}×27]}。坐标缺 / 非法 → 跳过该箱（不写残条目）。
bool WorldStore::writeChests(const QVariantList &chests)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM chests"))) {
        qCCritical(lcSave) << "saveAll: chests delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO chests (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : chests) {
        const QVariantMap cm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = cm.value(QStringLiteral("x")).toInt(&okx);
        const int y = cm.value(QStringLiteral("y")).toInt(&oky);
        const int z = cm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue; // 缺坐标 → 跳过（不写残条目）
        // slots 序列化为 JSON 文本（同 player_state 自描述、跨版本可读）。
        const QJsonDocument doc = QJsonDocument::fromVariant(cm.value(QStringLiteral("slots")));
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: chest insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t188 读 chests 表为 QVariantList（形状同 writeChests 入参）。未打开 → 空列表。caller（Main.qml.enterWorld）
//   转交 chestStore.loadAll 整体替换内存（清旧世界残留 + 填本世界箱子）。
QVariantList WorldStore::loadChests() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM chests"))) {
        qCWarning(lcSave) << "loadChests: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        QVariantMap cm;
        cm.insert(QStringLiteral("x"), q.value(0).toInt());
        cm.insert(QStringLiteral("y"), q.value(1).toInt());
        cm.insert(QStringLiteral("z"), q.value(2).toInt());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        cm.insert(QStringLiteral("slots"), doc.toVariant());
        out.append(cm);
    }
    return out;
}

// t1080 漏斗落盘：DELETE 全量 + INSERT 每只漏斗（坐标列 + slots JSON 文本）。调用方（saveAll）已开事务，
//   本方法不 BEGIN/COMMIT（同事务原子）。hoppers 形状 = HopperStore::allHoppers() 产物：每项
//   {x,y,z,slots:[{id,count}×5]}。坐标缺 / 非法 → 跳过该漏斗（不写残条目）。
bool WorldStore::writeHoppers(const QVariantList &hoppers)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM hoppers"))) {
        qCCritical(lcSave) << "saveAll: hoppers delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO hoppers (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : hoppers) {
        const QVariantMap hm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = hm.value(QStringLiteral("x")).toInt(&okx);
        const int y = hm.value(QStringLiteral("y")).toInt(&oky);
        const int z = hm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue; // 缺坐标 → 跳过（不写残条目）
        const QJsonDocument doc = QJsonDocument::fromVariant(hm.value(QStringLiteral("slots")));
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: hopper insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t1080 读 hoppers 表为 QVariantList（形状同 writeHoppers 入参）。未打开 → 空列表。caller
//   （Main.qml.enterWorld）转交 hopperStore.loadAll 整体替换内存（清旧世界残留 + 填本世界漏斗）。
QVariantList WorldStore::loadHoppers() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM hoppers"))) {
        qCWarning(lcSave) << "loadHoppers: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        QVariantMap hm;
        hm.insert(QStringLiteral("x"), q.value(0).toInt());
        hm.insert(QStringLiteral("y"), q.value(1).toInt());
        hm.insert(QStringLiteral("z"), q.value(2).toInt());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        hm.insert(QStringLiteral("slots"), doc.toVariant());
        out.append(hm);
    }
    return out;
}

// t1097 酿造落盘：DELETE 全量 + INSERT 每台酿造台（坐标列 + data JSON 文本）。调用方（saveAll）已开事务，
//   本方法不 BEGIN/COMMIT（同事务原子）。brewing 形状 = BrewingStore::allBrewingStands() 产物：每项
//   {x,y,z,slots:[{id,count}×5],progress,fuelOps}。坐标缺 / 非法 → 跳过该台（不写残条目）。
bool WorldStore::writeBrewing(const QVariantList &stands)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM brewing"))) {
        qCCritical(lcSave) << "saveAll: brewing delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO brewing (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : stands) {
        const QVariantMap hm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = hm.value(QStringLiteral("x")).toInt(&okx);
        const int y = hm.value(QStringLiteral("y")).toInt(&oky);
        const int z = hm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        QVariantMap data;
        data.insert(QStringLiteral("slots"), hm.value(QStringLiteral("slots")));
        data.insert(QStringLiteral("progress"), hm.value(QStringLiteral("progress")));
        data.insert(QStringLiteral("fuelOps"), hm.value(QStringLiteral("fuelOps")));
        const QJsonDocument doc = QJsonDocument::fromVariant(data);
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: brewing insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t1097 读 brewing 表为 QVariantList（形状同 writeBrewing 入参 = allBrewingStands 产物形）。未打开 →
//   空列表。caller（Main.qml.enterWorld）转交 brewingStore.loadAll 整体替换内存（清旧世界残留 + 填本世界）。
QVariantList WorldStore::loadBrewingStands() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM brewing"))) {
        qCWarning(lcSave) << "loadBrewingStands: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        QVariantMap hm;
        hm.insert(QStringLiteral("x"), q.value(0).toInt());
        hm.insert(QStringLiteral("y"), q.value(1).toInt());
        hm.insert(QStringLiteral("z"), q.value(2).toInt());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        const QVariantMap data = doc.toVariant().toMap();
        hm.insert(QStringLiteral("slots"), data.value(QStringLiteral("slots")));
        hm.insert(QStringLiteral("progress"), data.value(QStringLiteral("progress")));
        hm.insert(QStringLiteral("fuelOps"), data.value(QStringLiteral("fuelOps")));
        out.append(hm);
    }
    return out;
}

// t1113 牌子文本落盘：DELETE 全量 + INSERT 每块牌子（坐标列 + lines JSON 文本）。调用方（saveAll）已开
//   事务，本方法不 BEGIN/COMMIT（同事务原子）。signs 形状 = SignStore::allSigns() 产物：每项
//   {x,y,z,lines:[l0..l3]}。坐标缺 / 非法 → 跳过该牌（不写残条目）。
bool WorldStore::writeSigns(const QVariantList &signs)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM sign_texts"))) {
        qCCritical(lcSave) << "saveAll: sign_texts delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO sign_texts (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : signs) {
        const QVariantMap sm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = sm.value(QStringLiteral("x")).toInt(&okx);
        const int y = sm.value(QStringLiteral("y")).toInt(&oky);
        const int z = sm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        const QJsonDocument doc = QJsonDocument::fromVariant(sm.value(QStringLiteral("lines")));
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: sign_texts insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t1113 读 sign_texts 表为 QVariantList（形状同 writeSigns 入参 = allSigns 产物形）。未打开 → 空列表。
//   caller（Main.qml.enterWorld）转交 signStore.loadAll 整体替换内存（清旧世界残留 + 填本世界牌子）。
QVariantList WorldStore::loadSigns() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM sign_texts"))) {
        qCWarning(lcSave) << "loadSigns: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        QVariantMap sm;
        sm.insert(QStringLiteral("x"), q.value(0).toInt());
        sm.insert(QStringLiteral("y"), q.value(1).toInt());
        sm.insert(QStringLiteral("z"), q.value(2).toInt());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        sm.insert(QStringLiteral("lines"), doc.toVariant());
        out.append(sm);
    }
    return out;
}

// t1132 地图数据集落盘：DELETE 全量 + present 才 INSERT 单行（id=0）。调用方（saveAll /
//   writeWorldPart）已开事务，本方法不 BEGIN/COMMIT（同事务原子）。dataset 形状 = MapStore::
//   exportVariant() 产物：{present:bool, width, depth, revision, pixels:QByteArray}——尺寸三键 + 全幅
//   ARGB32 像素 BLOB（自描述；空 map = 表清空，与容器表 DELETE 口径一致）。
bool WorldStore::writeMapDataset(const QVariantMap &dataset)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM map_dataset"))) {
        qCCritical(lcSave) << "writeWorldPart: map_dataset delete failed:" << del.lastError().text();
        return false;
    }
    if (dataset.isEmpty() || !dataset.value(QStringLiteral("present")).toBool())
        return true; // 空 map / 缺 present = 会话无数据集 → 表保持空（清空语义）
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO map_dataset (id, width, depth, revision, pixels)"
                              " VALUES (0, ?, ?, ?, ?)"));
    iq.addBindValue(dataset.value(QStringLiteral("width")).toInt());
    iq.addBindValue(dataset.value(QStringLiteral("depth")).toInt());
    iq.addBindValue(dataset.value(QStringLiteral("revision")).toInt());
    iq.addBindValue(dataset.value(QStringLiteral("pixels")).toByteArray());
    if (!iq.exec()) {
        qCCritical(lcSave) << "writeWorldPart: map_dataset insert failed:" << iq.lastError().text();
        return false;
    }
    return true;
}

// t1132 读 map_dataset 表为 QVariantMap（形状同 writeMapDataset 入参 = MapStore::exportVariant
//   产物形）。未打开 / 无行 → 空 map（caller loadVariant 空行降级——t1114 会话口径）。
QVariantMap WorldStore::loadMapDataset() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT width, depth, revision, pixels FROM map_dataset WHERE id = 0"))) {
        qCWarning(lcSave) << "loadMapDataset: select failed:" << q.lastError().text();
        return out;
    }
    if (!q.next())
        return out; // 无行（新世界 / 旧档未存过图）→ 空 map
    out.insert(QStringLiteral("present"), true);
    out.insert(QStringLiteral("width"), q.value(0).toInt());
    out.insert(QStringLiteral("depth"), q.value(1).toInt());
    out.insert(QStringLiteral("revision"), q.value(2).toInt());
    out.insert(QStringLiteral("pixels"), q.value(3).toByteArray());
    return out;
}

// t1133 生物持久化表落盘：DELETE 全量 + INSERT 每行（裸列直存，无 JSON 包——行字段即存档面）。
//   调用方（SaveCoordinator 步⑥b 部件写）已开事务，本方法不 BEGIN/COMMIT（与 world 段同事务
//   原子——t1129 单事务域同门）。entities 形状 = EntityManager::exportPersistedEntities 产物。
//   kind/type 键缺 / 坐标键缺 → 跳过该行（不写残条目，同 writeChests 缺坐标门）；其余列值语义
//   门归 Entities 层恢复面（本类只存取）。
bool WorldStore::writeEntitiesPart(const QVariantList &entities)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM entities"))) {
        qCCritical(lcSave) << "writeEntitiesPart: entities delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral(
        "INSERT INTO entities (id, kind, type, x, y, z, color, mh, hp, baby, grow,"
        " wt, ws, ot, os, ov, sw, swd, sh, ss, sd)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    int ordinal = 0;
    for (const QVariant &v : entities) {
        const QVariantMap em = v.toMap();
        bool okKind = false, okType = false, okX = false, okY = false, okZ = false;
        const int kind = em.value(QStringLiteral("kind")).toInt(&okKind);
        const int type = em.value(QStringLiteral("type")).toInt(&okType);
        const double x = em.value(QStringLiteral("x")).toDouble(&okX);
        const double y = em.value(QStringLiteral("y")).toDouble(&okY);
        const double z = em.value(QStringLiteral("z")).toDouble(&okZ);
        if (!okKind || !okType || !okX || !okY || !okZ)
            continue; // 键缺 → 跳过（不写残条目）
        iq.addBindValue(++ordinal); // 行序主键（id 连续重编 = 快照全量重写语义，非稳定 id）
        iq.addBindValue(kind);
        iq.addBindValue(type);
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(em.value(QStringLiteral("color")).toString());
        iq.addBindValue(em.value(QStringLiteral("mh")).toInt());
        iq.addBindValue(em.value(QStringLiteral("hp")).toInt());
        iq.addBindValue(em.value(QStringLiteral("baby")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("grow")).toFloat());
        iq.addBindValue(em.value(QStringLiteral("wt")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("ws")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("ot")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("os")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("ov")).toInt());
        iq.addBindValue(em.value(QStringLiteral("sw")).toInt());
        iq.addBindValue(em.value(QStringLiteral("swd")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("sh")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("ss")).toInt());
        iq.addBindValue(em.value(QStringLiteral("sd")).toBool() ? 1 : 0);
        if (!iq.exec()) {
            qCCritical(lcSave) << "writeEntitiesPart: entity insert failed at row" << ordinal
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t1133 读 entities 表为 QVariantList（形状同 writeEntitiesPart 入参 = exportPersistedEntities
//   产物形）。未打开 → 空列表；SELECT 失败（旧档表缺席 = 首要形态 / 库病）→ qCWarning + 空列表
//   （loadChests 同门——旧档无表读入不丢不崩硬门）。行语义门（kind/type 越界 / NaN / 死亡）归
//   Entities 层 restorePersistedEntities。
QVariantList WorldStore::loadEntities() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral(
            "SELECT kind, type, x, y, z, color, mh, hp, baby, grow,"
            " wt, ws, ot, os, ov, sw, swd, sh, ss, sd FROM entities"))) {
        qCWarning(lcSave) << "loadEntities: select failed (legacy save without table?):"
                          << q.lastError().text();
        return out; // 旧档无表 → 空列表（恢复面零注入 = 自然入口生成照旧）
    }
    while (q.next()) {
        QVariantMap em;
        em.insert(QStringLiteral("kind"), q.value(0).toInt());
        em.insert(QStringLiteral("type"), q.value(1).toInt());
        em.insert(QStringLiteral("x"), q.value(2).toDouble());
        em.insert(QStringLiteral("y"), q.value(3).toDouble());
        em.insert(QStringLiteral("z"), q.value(4).toDouble());
        em.insert(QStringLiteral("color"), q.value(5).toString());
        em.insert(QStringLiteral("mh"), q.value(6).toInt());
        em.insert(QStringLiteral("hp"), q.value(7).toInt());
        em.insert(QStringLiteral("baby"), q.value(8).toInt() != 0);
        em.insert(QStringLiteral("grow"), q.value(9).toFloat());
        em.insert(QStringLiteral("wt"), q.value(10).toInt() != 0);
        em.insert(QStringLiteral("ws"), q.value(11).toInt() != 0);
        em.insert(QStringLiteral("ot"), q.value(12).toInt() != 0);
        em.insert(QStringLiteral("os"), q.value(13).toInt() != 0);
        em.insert(QStringLiteral("ov"), q.value(14).toInt());
        em.insert(QStringLiteral("sw"), q.value(15).toInt());
        em.insert(QStringLiteral("swd"), q.value(16).toInt() != 0);
        em.insert(QStringLiteral("sh"), q.value(17).toInt() != 0);
        em.insert(QStringLiteral("ss"), q.value(18).toInt());
        em.insert(QStringLiteral("sd"), q.value(19).toInt() != 0);
        out.append(em);
    }
    return out;
}

// t1137 活塞动画段落盘：DELETE 全量 + INSERT 每个在册占位格（坐标列 + 五字段整数拍列）。调用方
//   （协调层步⑥c）已开事务，本方法不 BEGIN/COMMIT（同事务原子）。anims 形状 = World::
//   exportPistonAnims() 产物：每项 {x,y,z,id,st,fc,ex,beats}。坐标缺 / 非法 → 跳过该行（不写残条目）。
bool WorldStore::writePistonAnimsPart(const QVariantList &anims)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM piston_anims"))) {
        qCCritical(lcSave) << "writePistonAnimsPart: piston_anims delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral(
        "INSERT INTO piston_anims (x, y, z, id, st, fc, ex, beats)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const QVariant &v : anims) {
        const QVariantMap em = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = em.value(QStringLiteral("x")).toInt(&okx);
        const int y = em.value(QStringLiteral("y")).toInt(&oky);
        const int z = em.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz)
            continue; // 缺坐标 → 跳过（不写残条目）
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(em.value(QStringLiteral("id")).toInt());
        iq.addBindValue(em.value(QStringLiteral("st")).toInt());
        iq.addBindValue(em.value(QStringLiteral("fc")).toInt());
        iq.addBindValue(em.value(QStringLiteral("ex")).toBool() ? 1 : 0);
        iq.addBindValue(em.value(QStringLiteral("beats")).toInt());
        if (!iq.exec()) {
            qCCritical(lcSave) << "writePistonAnimsPart: anim insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t1137 读 piston_anims 表为 QVariantList（形状同 writePistonAnimsPart 入参 = World::
//   exportPistonAnims 产物形）。未打开 → 空列表；SELECT 失败（旧档表缺席 = 首要形态 / 库病）→
//   qCWarning + 空列表（loadEntities 同门——旧档无表读入不丢不崩硬门；旧档 t1136 及以前零 164
//   放置面 → 空表即正确恢复面）。行语义门（坐标域 / id 域）归 World 层 restorePistonAnims。
QVariantList WorldStore::loadPistonAnims() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, id, st, fc, ex, beats FROM piston_anims"))) {
        qCWarning(lcSave) << "loadPistonAnims: select failed (legacy save without table?):"
                          << q.lastError().text();
        return out; // 旧档无表 → 空列表（恢复面零注入）
    }
    while (q.next()) {
        QVariantMap em;
        em.insert(QStringLiteral("x"), q.value(0).toInt());
        em.insert(QStringLiteral("y"), q.value(1).toInt());
        em.insert(QStringLiteral("z"), q.value(2).toInt());
        em.insert(QStringLiteral("id"), q.value(3).toInt());
        em.insert(QStringLiteral("st"), q.value(4).toInt());
        em.insert(QStringLiteral("fc"), q.value(5).toInt());
        em.insert(QStringLiteral("ex"), q.value(6).toInt() != 0);
        em.insert(QStringLiteral("beats"), q.value(7).toInt());
        out.append(em);
    }
    return out;
}

// t177 二轮复盘 熔炉落盘：DELETE 全量 + INSERT 每只熔炉（坐标列 + data JSON 文本）。调用方（saveAll）已开
//   事务，本方法不 BEGIN/COMMIT（同事务原子）。furnaces 形状 = FurnaceStore::allFurnaces() 产物：每项
//   {x,y,z,slots:[{id,count}×3], burn, smelt}。整个 QVariantMap（含 slots + burn + smelt）序列化为 JSON 文本
//   存 data 列（同 chests 自描述、跨版本可读）。坐标缺 / 非法 → 跳过该熔炉（不写残条目）。
bool WorldStore::writeFurnaces(const QVariantList &furnaces)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM furnaces"))) {
        qCCritical(lcSave) << "saveAll: furnaces delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO furnaces (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : furnaces) {
        const QVariantMap fm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = fm.value(QStringLiteral("x")).toInt(&okx);
        const int y = fm.value(QStringLiteral("y")).toInt(&oky);
        const int z = fm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue; // 缺坐标 → 跳过（不写残条目）
        // 整个熔炉条目（slots + burn + smelt）序列化为 JSON 文本（同 chests / player_state 自描述、跨版本可读）。
        const QJsonDocument doc = QJsonDocument::fromVariant(fm);
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: furnace insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t177 二轮复盘 读 furnaces 表为 QVariantList（形状同 writeFurnaces 入参）。未打开 → 空列表。caller
//   （Main.qml.enterWorld）转交 furnaceStore.loadAll 整体替换内存（清旧世界残留 + 填本世界熔炉）。
QVariantList WorldStore::loadFurnaces() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM furnaces"))) {
        qCWarning(lcSave) << "loadFurnaces: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        QVariantMap fm = doc.toVariant().toMap();
        // data 列存的是整个 {x,y,z,slots,burn,smelt} → JSON；坐标用表的列（权威），JSON 内坐标仅冗余。
        fm.insert(QStringLiteral("x"), q.value(0).toInt());
        fm.insert(QStringLiteral("y"), q.value(1).toInt());
        fm.insert(QStringLiteral("z"), q.value(2).toInt());
        out.append(fm);
    }
    return out;
}

// t542 发射器落盘：DELETE 全量 + INSERT 每只发射器（坐标列 + data JSON 文本）。调用方（saveAll）已开事务，
//   本方法不 BEGIN/COMMIT（同事务原子）。dispensers 形状 = DispenserStore::allDispensers() 产物：每项
//   {x,y,z,slots:[{id,count}×9]}。整个 QVariantMap（含 slots）序列化为 JSON 文本存 data 列（同 chests /
//   furnaces 自描述、跨版本可读）。坐标缺 / 非法 → 跳过该发射器（不写残条目）。
bool WorldStore::writeDispensers(const QVariantList &dispensers)
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM dispensers"))) {
        qCCritical(lcSave) << "saveAll: dispensers delete failed:" << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO dispensers (x, y, z, data) VALUES (?, ?, ?, ?)"));
    for (const QVariant &v : dispensers) {
        const QVariantMap dm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = dm.value(QStringLiteral("x")).toInt(&okx);
        const int y = dm.value(QStringLiteral("y")).toInt(&oky);
        const int z = dm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue; // 缺坐标 → 跳过（不写残条目）
        // 整个发射器条目（slots）序列化为 JSON 文本（同 chests / furnaces / player_state 自描述、跨版本可读）。
        const QJsonDocument doc = QJsonDocument::fromVariant(dm);
        iq.addBindValue(x);
        iq.addBindValue(y);
        iq.addBindValue(z);
        iq.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        if (!iq.exec()) {
            qCCritical(lcSave) << "saveAll: dispenser insert failed at" << x << y << z
                               << ":" << iq.lastError().text();
            return false;
        }
    }
    return true;
}

// t542 读 dispensers 表为 QVariantList（形状同 writeDispensers 入参）。未打开 → 空列表。caller
//   （Main.qml.enterWorld）转交 dispenserStore.loadAll 整体替换内存（清旧世界残留 + 填本世界发射器）。
QVariantList WorldStore::loadDispensers() const
{
    QVariantList out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT x, y, z, data FROM dispensers"))) {
        qCWarning(lcSave) << "loadDispensers: select failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        QVariantMap dm = doc.toVariant().toMap();
        // data 列存的是整个 {x,y,z,slots} → JSON；坐标用表的列（权威），JSON 内坐标仅冗余。
        dm.insert(QStringLiteral("x"), q.value(0).toInt());
        dm.insert(QStringLiteral("y"), q.value(1).toInt());
        dm.insert(QStringLiteral("z"), q.value(2).toInt());
        out.append(dm);
    }
    return out;
}
//   REPLACE INTO（单行 upsert，无坐标主键）。空 map → 写空 JSON（加载端 loadVariant 兜底重置默认）。
//   独立事务（caller Main.qml.saveAndExitToWorldList 内调用，与 saveAll 分离；progress 更新频次低，单独写无妨）。
bool WorldStore::saveProgress(const QVariantMap &progress)
{
    if (!m_open) return false;
    // t1129：写体与原子保存域共享（writeProgressPart 无计数）；裸面口径逐位同旧。
    if (!writeProgressPart(progress))
        return false;
    noteSaveOk();   // t974：exec 成功 = 进度已落盘（同步 upsert，无 deferred）
    return true;
}

// progress 读单行 key='main' → QVariantMap（同 toVariant 形状）。未打开 / 无行 → 空 map（caller 兜底重置默认）。
QVariantMap WorldStore::loadProgress() const
{
    QVariantMap out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConn));
    if (!q.exec(QStringLiteral("SELECT data FROM progress WHERE key='main'"))) {
        qCWarning(lcSave) << "loadProgress: select failed:" << q.lastError().text();
        return out;
    }
    if (q.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
        out = doc.toVariant().toMap();
    }
    return out;
}

// t382 迁移注册表（单一权威）。当前仅一条 0→1 步：t348 用映射层（BlockRegistry::mcBlockId）而非重排
//   引擎 id，故存档 chunk 字节序未变 → 空 remap 表（identity）。此步存在以打通「读 chunk → remap voxels
//   → 写回 → 刷 world_version」全链路（对 t382 前无 world_version 键的老存档执行一次），并为将来真正的
//   block-id 重排提供现成插入点：追加 Migration{N+1, makeIdRemap({{oldId,newId},...})} + bump kWorldVersion。
const QList<WorldStore::Migration> &WorldStore::migrations()
{
    static const QList<Migration> list = {
        Migration{1, makeIdRemap({})}, // 0→1：identity（t348 未重排引擎 id）
    };
    return list;
}

// 构造 block-id 重排迁移：建 256 字节查表（默认 identity，remap 覆盖指定项），返回遍历 voxels 改写的
//   BlobMigrator。查表用 shared_ptr 捕获进 lambda（一次建表、多次 chunk 复用，O(1)/字节）。空 remap =
//   identity（字节逐个映射回自身，等效 no-op 但走完整 remap 路径，验证机制可用）。
WorldStore::BlobMigrator WorldStore::makeIdRemap(const QHash<quint8, quint8> &remap)
{
    auto table = std::make_shared<std::array<quint8, 256>>();
    for (int i = 0; i < 256; ++i)
        (*table)[i] = quint8(i); // 默认 identity
    for (auto it = remap.constBegin(); it != remap.constEnd(); ++it)
        (*table)[quint8(it.key())] = quint8(it.value()); // 覆盖需重排的 id
    return [table](QByteArray &voxels, QByteArray & /*states*/, QByteArray & /*light*/) {
        char *p = voxels.data();
        const qsizetype n = voxels.size();
        for (qsizetype i = 0; i < n; ++i)
            p[i] = char((*table)[quint8(p[i])]);
    };
}

// t382 把存档数据迁移到 kWorldVersion。读 world_meta 'world_version'（缺键→0 = t382 前老存档），若低于
//   当前则按 migrations() 取覆盖步、按 targetVersion 升序逐 chunk 应用（事务原子：UPDATE 全部 chunk +
//   刷版本一次 COMMIT）。已是当前版本 → no-op。返回是否成功（SQL 失败 → rollback + false）。
bool WorldStore::migrateWorldData()
{
    QSqlDatabase db = QSqlDatabase::database(kConn);
    int stored = 0;
    {
        QSqlQuery rq(db);
        if (rq.exec(QStringLiteral("SELECT value FROM world_meta WHERE key='world_version'")) && rq.next())
            stored = rq.value(0).toString().toInt(); // 缺键（t382 前存档）→ 保持 0
    }
    if (stored >= kWorldVersion)
        return true; // 已是当前版本（含新世界 createWorld 已写 kWorldVersion）

    // 收集 targetVersion ∈ (stored, kWorldVersion] 的迁移步，按 targetVersion 升序应用。
    const QList<Migration> &ms = migrations();
    QList<const Migration *> todo;
    todo.reserve(ms.size());
    for (const Migration &m : ms)
        if (m.targetVersion > stored && m.targetVersion <= kWorldVersion)
            todo.append(&m);
    std::sort(todo.begin(), todo.end(),
              [](const Migration *a, const Migration *b) { return a->targetVersion < b->targetVersion; });

    qCInfo(lcSave) << "migrating world data world_version" << stored << "->" << kWorldVersion
                   << "across" << todo.size() << "step(s) for" << m_openFile;

    if (!db.transaction()) {
        qCCritical(lcSave) << "migrate: begin transaction failed:" << db.lastError().text();
        return false;
    }
    // ORDER BY cz, cx 与 saveAll 的 chunk 遍历序（cz 外 / cx 内）一致，便于一致性与日志可读。
    QSqlQuery sel(db);
    if (!sel.exec(QStringLiteral("SELECT cx, cz, voxels, states, light FROM chunks ORDER BY cz, cx"))) {
        qCCritical(lcSave) << "migrate: select chunks failed:" << sel.lastError().text();
        db.rollback();
        return false;
    }
    QSqlQuery up(db);
    up.prepare(QStringLiteral("UPDATE chunks SET voxels=?, states=?, light=? WHERE cx=? AND cz=?"));
    int migrated = 0;
    while (sel.next()) {
        const int cx = sel.value(0).toInt();
        const int cz = sel.value(1).toInt();
        QByteArray voxels = sel.value(2).toByteArray();
        QByteArray states = sel.value(3).toByteArray();
        QByteArray light = sel.value(4).toByteArray();
        for (const Migration *m : todo)
            m->apply(voxels, states, light);
        up.addBindValue(voxels);
        up.addBindValue(states);
        up.addBindValue(light);
        up.addBindValue(cx);
        up.addBindValue(cz);
        if (!up.exec()) {
            qCCritical(lcSave) << "migrate: update chunk failed at" << cx << cz << ":" << up.lastError().text();
            db.rollback();
            return false;
        }
        ++migrated;
    }
    // 刷 world_version（标志本库数据已推进到当前版本，后续 openWorld 跳过迁移）。
    QSqlQuery mq(db);
    mq.prepare(QStringLiteral("INSERT OR REPLACE INTO world_meta (key, value) VALUES ('world_version', ?)"));
    mq.addBindValue(QString::number(kWorldVersion));
    if (!mq.exec()) {
        qCCritical(lcSave) << "migrate: stamp world_version failed:" << mq.lastError().text();
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        qCCritical(lcSave) << "migrate: commit failed:" << db.lastError().text();
        db.rollback();
        return false;
    }
    qCInfo(lcSave) << "migration complete:" << migrated << "chunk(s) advanced to world_version" << kWorldVersion;
    return true;
}
