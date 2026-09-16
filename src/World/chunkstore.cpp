#include "chunkstore.h"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

// §29.5-W3 ChunkStore 实现（契约与 additive 兼容性论证见 chunkstore.h 头注，此处不赘）。
// 实现要点登记（savecoordinator.cpp 同门）：
//   - 独立命名连接（与 worldstore kConn / savecoordinator kCoordConn 互不占用），每次操作
//     开-用-关，无长活连接状态；同连接名复用前先 removeDatabase 防残留句柄。
//   - chunk_edits 表 IF NOT EXISTS 幂等建表（纯加表；零 bump——头注④）。
//   - 读路径失败一律降级 miss/false（不抛不堵；写路径失败 Result fail 上抛给驱逐缝）。

// 附加表连接名（独立于 worldstore 的 "voxelsandbox_worldstore" 与 savecoordinator 的
// "voxelsandbox_savecoordinator"——三域三连接互不占用）。
static const char *const kChunkStoreConn = "voxelsandbox_chunkstore";
// 附加表：per-chunk 一行；generation = 该 chunk 累计落盘代次（十进制文本，与 world_meta
// 同风格；blob 三段与 worldstore chunks 表列同构收窄到单 chunk）。
static const char *const kTable = "chunk_edits";

namespace {
// 开-用-关的连接就绪帮手：removeDatabase 防残留 + addDatabase + open。返回可写连接
//（!isOpen 时调用方按 SQL 失败面处理）。
QSqlDatabase openStoreConnection(const QString &dbPath)
{
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kChunkStoreConn);
    db.setDatabaseName(dbPath);
    db.open();
    return db;
}

// 幂等建表（纯加表零 bump）。对外部 EXCLUSIVE 锁：CREATE 本身是一次写 → 失败即挡（marker-first
// 第一闸的同构——写不进 = 什么都不写）。
bool ensureTable(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS %1 (cx INTEGER NOT NULL, cz INTEGER NOT NULL,"
            " generation TEXT NOT NULL, voxels BLOB NOT NULL, states BLOB NOT NULL,"
            " light BLOB NOT NULL, PRIMARY KEY (cx, cz))")
            .arg(QLatin1String(kTable))))
        return true;
    qWarning() << "ChunkStore: ensure table failed:" << q.lastError().text();
    return false;
}
} // namespace

Result<void> ChunkStore::persistChunk(int cx, int cz, const Chunk &chunk)
{
    if (!isBound())
        return Result<void>::fail(kErrChunkStoreNotBound, "chunk store not bound");
    const size_t n = chunk.voxelCount();
    // 三数组深拷贝（QByteArray 构造即拷贝——驱逐时刻定格，此后活体再变与本次落盘无关）。
    const QByteArray voxels(reinterpret_cast<const char *>(chunk.voxelData()), int(n));
    const QByteArray states(reinterpret_cast<const char *>(chunk.stateData()), int(n));
    const QByteArray light(reinterpret_cast<const char *>(chunk.lightData()), int(n));

    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen() && ensureTable(db)) {
        // 代次推进（SaveCoordinator 口径 per-chunk 收窄）：旧行 generation + 1，首行 1；
        // 单语句 upsert 事务原子——「没写进 = 旧行原样保留在旧代次」（头注 marker 等价论证）。
        QSqlQuery u(db);
        u.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO %1 (cx, cz, generation, voxels, states, light) VALUES"
            " (?, ?, COALESCE((SELECT generation FROM %1 WHERE cx = ? AND cz = ?) + 1, 1), ?, ?, ?)")
            .arg(QLatin1String(kTable)));
        u.addBindValue(cx);
        u.addBindValue(cz);
        u.addBindValue(cx);
        u.addBindValue(cz);
        u.addBindValue(voxels);
        u.addBindValue(states);
        u.addBindValue(light);
        ok = u.exec();
        if (!ok)
            qWarning() << "ChunkStore: persist failed for" << cx << cz << ":"
                       << u.lastError().text();
    } else if (!db.isOpen()) {
        qWarning() << "ChunkStore: open failed for persist:" << db.lastError().text();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    if (!ok)
        return Result<void>::fail(kErrChunkStoreSql, "chunk_edits upsert failed (lock/disk?)");
    return Result<void>::ok();
}

bool ChunkStore::hasChunk(int cx, int cz) const
{
    if (!isBound())
        return false;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool hit = false;
    if (db.isOpen()) {
        // 只读存在性查询；表缺席（旧档）→ SELECT 失败 → miss（Fresh 语义，不建表不写任何东西）。
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE cx = ? AND cz = ?")
                      .arg(QLatin1String(kTable)));
        q.addBindValue(cx);
        q.addBindValue(cz);
        hit = q.exec() && q.next();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return hit;
}

bool ChunkStore::loadChunk(int cx, int cz, ChunkStoreBlob &out) const
{
    if (!isBound())
        return false;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen()) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT generation, voxels, states, light FROM %1 WHERE cx = ? AND cz = ?")
            .arg(QLatin1String(kTable)));
        q.addBindValue(cx);
        q.addBindValue(cz);
        if (q.exec() && q.next()) {
            out.cx = cx;
            out.cz = cz;
            out.generation = q.value(0).toLongLong();
            out.voxels = q.value(1).toByteArray();
            out.states = q.value(2).toByteArray();
            out.light = q.value(3).toByteArray();
            ok = !out.voxels.isEmpty() && out.voxels.size() == out.states.size()
                && out.voxels.size() == out.light.size();
        }
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return ok;
}

int ChunkStore::chunkCount() const
{
    if (!isBound())
        return 0;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    int n = 0;
    if (db.isOpen()) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(kTable)))
            && q.next())
            n = q.value(0).toInt();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return n;
}
