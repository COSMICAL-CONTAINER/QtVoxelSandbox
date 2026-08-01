#include "worldstore.h"

#include "chunk.h"
#include "chunkmanager.h"
#include "world.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <cstring> // std::memcpy（chunk blob 回填）

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
            {QStringLiteral("playedAt"), QStringLiteral("0")}
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
    if (!QFile::remove(path)) {
        qCWarning(lcSave) << "deleteWorld: remove failed" << path;
        return false;
    }
    qCInfo(lcSave) << "deleted world" << file;
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

bool WorldStore::saveAll(const QString &name)
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
        {QStringLiteral("playedAt"), QString::number(now)}
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
