#include "chunkstore.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
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
// 附加表：per-chunk 一行；generation = 落盘时盖的**世界保存代次**（十进制文本，与 world_meta
// 同风格；W5 契约修订立证见 chunkstore.h 头注——无 stream_worlds 行时读 0 → 盖 1，与 W3
// 原版累计首写语义的全部已观测值逐位同值）；blob 三段与 worldstore chunks 表列同构收窄到
// 单 chunk。
static const char *const kTable = "chunk_edits";
// §29.5-W5 流式世界元数据表（D2/D3 域；一库一行，world 列 = 显式世界标识）。
static const char *const kStreamTable = "stream_worlds";
// 建代次锚的只读消费源：save_coord 的完整代次键（SaveCoordinator r2015 台账权威——**只读
// SELECT**，表/键缺席 = 0；台账唯一写点仍是 SaveCoordinator，本类绝不写它）。
static const char *const kCoordTable = "save_coord";
static const char *const kCoordKeyComplete = "complete_generation";

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

// §29.5-W5：流式世界元数据表幂等建表（同门纯加表；两表同连接同创建拍）。
bool ensureStreamTable(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS %1 (world TEXT PRIMARY KEY, streaming INTEGER NOT NULL,"
            " core_w INTEGER NOT NULL, core_d INTEGER NOT NULL, base_gen TEXT NOT NULL,"
            " save_gen TEXT NOT NULL)")
            .arg(QLatin1String(kStreamTable))))
        return true;
    qWarning() << "ChunkStore: ensure stream table failed:" << q.lastError().text();
    return false;
}

// §29.5-W5：stream_worlds 保存代次只读（行缺席 = 0——无元数据库上落盘盖 1，契约见
// chunkstore.h 代次修订段）。只读 SELECT，失败 = 0（诚实降级，读缝同门不抛不堵）。
qint64 readStreamSaveGen(QSqlDatabase &db, const QString &worldId)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT save_gen FROM %1 WHERE world = ?")
                  .arg(QLatin1String(kStreamTable)));
    q.addBindValue(worldId);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return 0;
}
} // namespace

Result<void> ChunkStore::persistChunk(int cx, int cz, const Chunk &chunk)
{
    if (!isBound())
        return Result<void>::fail(kErrChunkStoreNotBound, "chunk store not bound");
    const size_t n = chunk.voxelCount();
    // 三数组深拷贝（QByteArray 构造即拷贝——落盘时刻定格，此后活体再变与本次落盘无关）。
    const QByteArray voxels(reinterpret_cast<const char *>(chunk.voxelData()), int(n));
    const QByteArray states(reinterpret_cast<const char *>(chunk.stateData()), int(n));
    const QByteArray light(reinterpret_cast<const char *>(chunk.lightData()), int(n));

    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen() && ensureTable(db) && ensureStreamTable(db)) {
        // 代次盖写（§29.5-W5 契约，头注立证）：读 stream_worlds.save_gen，盖 stamp = save_gen
        // + 1（驱逐写 = 在途待保存代次；保存时冲洗写 = 本次保存代次——两条写路径同一刻度，
        // 读档 overlay 仲裁「行代次 vs 世界存代次」可比性的承重前提）。单语句 upsert 事务原子
        // ——「没写进 = 旧行原样保留在旧代次」（头注 marker 等价论证）。
        const qint64 stamp = readStreamSaveGen(db, m_worldId) + 1;
        QSqlQuery u(db);
        u.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO %1 (cx, cz, generation, voxels, states, light) VALUES"
            " (?, ?, ?, ?, ?, ?)")
            .arg(QLatin1String(kTable)));
        u.addBindValue(cx);
        u.addBindValue(cz);
        u.addBindValue(QString::number(stamp));
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

// ── §29.5-W5 流式世界元数据 + 行全量读回（契约与仲裁语义 = chunkstore.h 头注 W5 段）──────

// save_coord 完整代次只读消费（建代次锚的「世界存代次」半边）：表/键缺席（旧档无台账）=
// 0。台账唯一写点仍是 SaveCoordinator（r2015）——本类只 SELECT，绝不写他域表。
static qint64 readCoordCompleteGeneration(const QString &dbPath)
{
    if (dbPath.isEmpty() || !QFileInfo::exists(dbPath))
        return 0; // 无库 = 无台账 = Fresh（r2015 recover 同门判据）
    QSqlDatabase db = openStoreConnection(dbPath);
    qint64 gen = 0;
    if (db.isOpen()) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT value FROM %1 WHERE key = ?")
                      .arg(QLatin1String(kCoordTable)));
        q.addBindValue(QLatin1String(kCoordKeyComplete));
        if (q.exec() && q.next())
            gen = q.value(0).toLongLong();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return gen;
}

bool ChunkStore::readStreamWorldMeta(StreamWorldMeta &out) const
{
    if (!isBound())
        return false;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen()) {
        // 只读；表缺席（旧档/未登记）→ SELECT 失败 → false（fixed 世界 = 无标志无活动）。
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT streaming, core_w, core_d, base_gen, save_gen FROM %1 WHERE world = ?")
                      .arg(QLatin1String(kStreamTable)));
        q.addBindValue(m_worldId);
        if (q.exec() && q.next()) {
            out.streaming = q.value(0).toInt() != 0;
            out.coreW = q.value(1).toInt();
            out.coreD = q.value(2).toInt();
            out.baseGen = q.value(3).toLongLong();
            out.saveGen = q.value(4).toLongLong();
            ok = true;
        }
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return ok;
}

bool ChunkStore::writeStreamWorldMeta(const StreamWorldMeta &m)
{
    if (!isBound())
        return false;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen() && ensureStreamTable(db)) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO %1 (world, streaming, core_w, core_d, base_gen, save_gen)"
            " VALUES (?, ?, ?, ?, ?, ?)")
            .arg(QLatin1String(kStreamTable)));
        q.addBindValue(m_worldId);
        q.addBindValue(m.streaming ? 1 : 0);
        q.addBindValue(m.coreW);
        q.addBindValue(m.coreD);
        q.addBindValue(QString::number(m.baseGen));
        q.addBindValue(QString::number(m.saveGen));
        ok = q.exec();
        if (!ok)
            qWarning() << "ChunkStore: stream meta upsert failed:" << q.lastError().text();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return ok;
}

bool ChunkStore::markStreamingWorld(int coreW, int coreD)
{
    if (!isBound())
        return false;
    // 代次锚（头注「代次刻度」段）：base = max(世界存代次只读消费, 既有 save_gen)——再转换
    // 时老流式时代行全部 ≤ 新锚（等号行按「同代 = 表胜」让行越界判据语义本位）；save = base
    // （下一次流式保存 = base + 1，行盖写即新于锚）。SQL 失败面 = 旧行原样（marker 同门）。
    StreamWorldMeta prior;
    const bool had = readStreamWorldMeta(prior);
    StreamWorldMeta next;
    next.streaming = true;
    next.coreW = coreW;
    next.coreD = coreD;
    next.baseGen = qMax(readCoordCompleteGeneration(m_dbPath), had ? prior.saveGen : qint64(0));
    next.saveGen = next.baseGen;
    return writeStreamWorldMeta(next);
}

bool ChunkStore::clearStreamingWorldFlag()
{
    if (!isBound())
        return false;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    bool ok = false;
    if (db.isOpen()) {
        // 仅清标志位（行保留 = 境外数据不可达但不清除，§29.5.3 口径）；无行 = UPDATE 0 行
        // → false（fixed 世界无标志可翻）。
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE %1 SET streaming = 0 WHERE world = ?")
                      .arg(QLatin1String(kStreamTable)));
        q.addBindValue(m_worldId);
        ok = q.exec() && q.numRowsAffected() > 0;
        if (!ok)
            qWarning() << "ChunkStore: clear streaming flag failed:" << q.lastError().text();
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return ok;
}

qint64 ChunkStore::advanceStreamSaveGeneration()
{
    if (!isBound())
        return 0;
    StreamWorldMeta meta;
    if (!readStreamWorldMeta(meta))
        return 0; // 未登记流式世界：无代次可推进（调用方按失败上报，不谎报）
    QSqlDatabase db = openStoreConnection(m_dbPath);
    qint64 next = 0;
    bool ok = false;
    if (db.isOpen() && ensureStreamTable(db)) {
        // max(save, base) + 1：单调不重置不复用（r2015 代次口径同门——失败/中断消耗号段）。
        next = qMax(meta.saveGen, meta.baseGen) + 1;
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE %1 SET save_gen = ? WHERE world = ?")
                      .arg(QLatin1String(kStreamTable)));
        q.addBindValue(QString::number(next));
        q.addBindValue(m_worldId);
        ok = q.exec() && q.numRowsAffected() > 0;
        if (!ok) {
            qWarning() << "ChunkStore: advance save generation failed:" << q.lastError().text();
            next = 0; // 写不进 = 未推进（调用方按失败上报；marker 同门——旧行原样保留）
        }
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return next;
}

int ChunkStore::loadAllRows(QVector<ChunkStoreBlob> &out) const
{
    if (!isBound())
        return 0;
    QSqlDatabase db = openStoreConnection(m_dbPath);
    int n = -1;
    if (db.isOpen()) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT cx, cz, generation, voxels, states, light FROM %1")
                       .arg(QLatin1String(kTable)))) {
            n = 0;
            while (q.next()) {
                ChunkStoreBlob b;
                b.cx = q.value(0).toInt();
                b.cz = q.value(1).toInt();
                b.generation = q.value(2).toLongLong();
                b.voxels = q.value(3).toByteArray();
                b.states = q.value(4).toByteArray();
                b.light = q.value(5).toByteArray();
                // 尺寸自洽守卫（loadChunk 同门）：三段不等长 = 病行跳过（诚实降级，不堵整列）。
                if (b.voxels.isEmpty() || b.voxels.size() != b.states.size()
                    || b.voxels.size() != b.light.size())
                    continue;
                out.append(b);
                ++n;
            }
        } else {
            // 表缺席（旧档）= 空表合法态 → 0 行（Fresh 语义）；真 SQL 病才 -1。
            n = 0;
        }
    }
    if (QSqlDatabase::contains(kChunkStoreConn))
        QSqlDatabase::removeDatabase(kChunkStoreConn);
    return n;
}
