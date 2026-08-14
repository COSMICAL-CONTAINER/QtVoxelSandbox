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

WorldStore::WorldStore(QObject *parent) : QObject(parent) {}

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
    // user_version 读（PRAGMA 返回单行单列）。
    q.exec(QStringLiteral("PRAGMA user_version"));
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
    // progress 表（progress 新系统）：玩家进度（统计 + 成就）单行表，key 固定 'main'，data 存 PlayerProgress::toVariant()
    //   的 JSON。IF NOT EXISTS 幂等补建（schema 版本不 bump，同 chests/furnaces，纯加表对老库向前兼容）。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS progress ("
            "  key TEXT PRIMARY KEY,"
            "  data TEXT NOT NULL)"))) {
        qCCritical(lcSave) << "create progress failed:" << q.lastError().text();
        return false;
    }
    // 写 user_version（新库 0→kSchemaVersion；旧库同版本幂等；无 harm）。
    q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
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

bool WorldStore::saveAll(const QString &name, const QVariantList &chests, const QVariantList &furnaces, const QVariantList &dispensers)
{
    if (!m_open || !m_world) {
        qCWarning(lcSave) << "saveAll: no open db or world";
        return false;
    }
    const ChunkManager &cm = m_world->chunks();
    QSqlDatabase db = QSqlDatabase::database(kConn);
    if (!db.transaction()) {
        qCCritical(lcSave) << "saveAll: begin transaction failed:" << db.lastError().text();
        return false;
    }
    // 清空旧 chunks（upsert 全量重写最简；25 chunk 量级全删全插 < 1ms，无需增量）。
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM chunks"));

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
                qCCritical(lcSave) << "saveAll: chunk insert failed at" << cx << cz << ":" << iq.lastError().text();
                db.rollback();
                return false;
            }
            ++saved;
        }
    }
    // 刷 meta：seed（terrain 确定性 + 旧版可重生兜底）/ dims / name / playedAt（= 本次保存时刻）。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery mq(db);
    mq.prepare(QStringLiteral("INSERT OR REPLACE INTO world_meta (key, value) VALUES (?, ?)"));
    const QList<QPair<QString, QString>> metas = {
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
    for (const auto &kv : metas) {
        mq.addBindValue(kv.first);
        mq.addBindValue(kv.second);
        if (!mq.exec()) {
            qCCritical(lcSave) << "saveAll: meta update failed:" << mq.lastError().text();
            db.rollback();
            return false;
        }
    }
    // t188 箱子内容同事务落盘（chests 表 DELETE 全量 + INSERT；与 chunks / meta 原子提交）。
    if (!writeChests(chests)) {
        db.rollback();
        return false;
    }
    // t177 二轮复盘 熔炉内容同事务落盘（furnaces 表 DELETE 全量 + INSERT；与 chunks / meta / chests 原子提交）。
    if (!writeFurnaces(furnaces)) {
        db.rollback();
        return false;
    }
    // t542 发射器内容同事务落盘（dispensers 表 DELETE 全量 + INSERT；与 chunks / meta / chests / furnaces 原子提交）。
    if (!writeDispensers(dispensers)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        qCCritical(lcSave) << "saveAll: commit failed:" << db.lastError().text();
        db.rollback();
        return false;
    }
    qCInfo(lcSave) << "saved" << saved << "chunks for world" << m_openFile;
    return true;
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
    // QVariantMap → JSON 文本（QJsonDocument::fromVariant 处理嵌套 QVariantList<QVariantMap> 等）。
    const QJsonDocument doc = QJsonDocument::fromVariant(data);
    const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    QSqlQuery q(QSqlDatabase::database(kConn));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO player_state (id, data) VALUES (0, ?)"));
    q.addBindValue(json);
    if (!q.exec()) {
        qCCritical(lcSave) << "savePlayerData: insert failed:" << q.lastError().text();
        return false;
    }
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
    q.exec(QStringLiteral("SELECT COUNT(*) FROM player_state WHERE id = 0"));
    return q.next() && q.value(0).toInt() > 0;
}

bool WorldStore::hasChunks() const
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(kConn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM chunks"));
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
    QSqlDatabase db = QSqlDatabase::database(kConn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO progress (key, data) VALUES ('main', ?)"));
    const QJsonDocument doc = QJsonDocument::fromVariant(progress);
    q.addBindValue(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        qCCritical(lcSave) << "saveProgress failed:" << q.lastError().text();
        return false;
    }
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
