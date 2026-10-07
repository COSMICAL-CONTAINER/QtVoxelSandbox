#include "entitystagestore.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

// t1142 EntityStageStore 实现（契约与行形状论证 = entitystagestore.h 类头，此处不赘）。
// 实现要点登记（chunkstore.cpp 同门）：独立命名连接开-用-关 + busy timeout 归零（t1066）+
// t1098 句柄收内层作用域（摘名时零活引用）；读路径失败一律降级空/缺（不抛不堵），写路径
// 失败 bool false 上抛给驱逐缝/保存事务；幂等建表纯追加零 bump。

// 附加域连接名（独立于 worldstore / savecoordinator / chunkstore 三域——四域四连接互不占用）。
static const char *const kEntityStageConn = "voxelsandbox_entitystagestore";
// 会话暂存表（键 = (kind, eid) 复合主键——建表注全论证；cx/cz = 派生所有权列；data = 序列化
// 行 JSON。头注初版「eid 唯一」为复合键修正前残留，t1142 验收勘正）。
static const char *const kStageTable = "entity_staging";
// 掉落物已提交快照表（oid = 行序主键——快照全量重写语义，非稳定 id；eid 列仅暂存域对账位）。
static const char *const kItemTable = "item_entities";

namespace {
// 开-用-关的连接就绪帮手（chunkstore openStoreConnection 同门）。
QSqlDatabase openStageConnection(const QString &dbPath)
{
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kEntityStageConn);
    db.setDatabaseName(dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返
    db.open();
    return db;
}

// 幂等建表（纯追加零 bump；对外部 EXCLUSIVE 键 CREATE 即写 → 失败即挡，marker-first 同构）。
//   暂存键 = (kind, eid) 复合主键——eid 稳定身份**按族分配**（mob / 掉落物两族各自独立游标
//   [EntityStore R20.14 先例 + EntityManager t1142 同门]，跨族数值必然交叠（两族都从 1 数起
//   ），单列 eid 键会让后写族覆盖先写族（资产丢失面）——族前缀进键 = 全局唯一。
bool ensureStageTables(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS %1 (kind INTEGER NOT NULL, eid TEXT NOT NULL,"
            " cx INTEGER NOT NULL, cz INTEGER NOT NULL, data TEXT NOT NULL,"
            " PRIMARY KEY (kind, eid))")
            .arg(QLatin1String(kStageTable))))
        return false;
    QSqlQuery q2(db);
    if (!q2.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS %1 (oid INTEGER PRIMARY KEY AUTOINCREMENT,"
            " eid TEXT NOT NULL, data TEXT NOT NULL)")
            .arg(QLatin1String(kItemTable))))
        return false;
    return true;
}

// 行 → JSON 文本（QVariantMap 原样；序列化层键集权威——本类零解析）。
QString rowToJson(const QVariantMap &row)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(row)).toJson());
}

// JSON 文本 → 行（QVariantMap；损坏载荷 → 空 map——恢复侧 kind 门/防御门拒收，不半恢复）。
QVariantMap jsonToRow(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
}
} // namespace

bool EntityStageStore::stageEntity(int cx, int cz, const QVariantMap &row)
{
    if (!isBound())
        return false;
    // eid 键缺 = 序列化层契约破坏 → 拒收（不写无主行——恰一次语义的键完整性前提）。
    const QString eid = row.value(QStringLiteral("eid")).toString();
    if (eid.isEmpty())
        return false;
    bool ok = false;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen() && ensureStageTables(db)) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO %1 (kind, eid, cx, cz, data) VALUES (?, ?, ?, ?, ?)")
                .arg(QLatin1String(kStageTable)));
            q.addBindValue(row.value(QStringLiteral("kind")).toInt());
            q.addBindValue(eid);
            q.addBindValue(cx);
            q.addBindValue(cz);
            q.addBindValue(rowToJson(row));
            ok = q.exec();
            if (!ok)
                qWarning() << "EntityStageStore: stage upsert failed:" << q.lastError().text();
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return ok;
}

int EntityStageStore::stagedCount() const
{
    if (!isBound())
        return 0;
    int n = 0;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(kStageTable)))
                && q.next())
                n = q.value(0).toInt();
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return n;
}

QVariantList EntityStageStore::stagedRows() const
{
    QVariantList out;
    if (!isBound())
        return out;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT eid, data FROM %1 ORDER BY eid")
                          .arg(QLatin1String(kStageTable)))) {
                while (q.next()) {
                    QVariantMap row = jsonToRow(q.value(1).toString());
                    if (row.isEmpty())
                        continue;
                    row.insert(QStringLiteral("eid"), q.value(0).toString());
                    out.append(row);
                }
            }
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return out;
}

QVariantList EntityStageStore::takeChunk(int cx, int cz)
{
    QVariantList out;
    if (!isBound())
        return out;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen()) {
            // take = SELECT + DELETE 同拍（恰一次语义本体）；表缺席（旧档）→ SELECT 失败 =
            // 空（不建表不写——读面零触碰，hasChunk 同门）。
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT eid, data FROM %1 WHERE cx = ? AND cz = ? ORDER BY eid")
                .arg(QLatin1String(kStageTable)));
            q.addBindValue(cx);
            q.addBindValue(cz);
            if (q.exec()) {
                while (q.next()) {
                    QVariantMap row = jsonToRow(q.value(1).toString());
                    if (row.isEmpty())
                        continue; // 损坏载荷：恢复侧拒收（行仍会被下方 DELETE 消费 = 不复活）
                    row.insert(QStringLiteral("eid"), q.value(0).toString());
                    out.append(row);
                }
                QSqlQuery d(db);
                d.prepare(QStringLiteral("DELETE FROM %1 WHERE cx = ? AND cz = ?")
                              .arg(QLatin1String(kStageTable)));
                d.addBindValue(cx);
                d.addBindValue(cz);
                if (!d.exec())
                    qWarning() << "EntityStageStore: take delete failed:"
                               << d.lastError().text();
            }
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return out;
}

bool EntityStageStore::clearStaging()
{
    if (!isBound())
        return false;
    bool ok = false;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen() && ensureStageTables(db)) {
            QSqlQuery d(db);
            ok = d.exec(QStringLiteral("DELETE FROM %1").arg(QLatin1String(kStageTable)));
            if (!ok)
                qWarning() << "EntityStageStore: staging clear failed:" << d.lastError().text();
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return ok;
}

bool EntityStageStore::commitItemSnapshotOn(QSqlDatabase &db, const QVariantList &rows)
{
    if (!isBound() || !db.isOpen())
        return false;
    if (!ensureStageTables(db))
        return false; // 建表本身是写：失败即挡（marker-first 同构；调用方回滚整事务）
    // 无条件 DELETE 全量 + INSERT（worldstore 容器表写序同门——空载荷 = 表清空，快照语义）。
    QSqlQuery del(db);
    if (!del.exec(QStringLiteral("DELETE FROM %1").arg(QLatin1String(kItemTable)))) {
        qWarning() << "EntityStageStore: item snapshot delete failed:"
                   << del.lastError().text();
        return false;
    }
    QSqlQuery iq(db);
    iq.prepare(QStringLiteral("INSERT INTO %1 (eid, data) VALUES (?, ?)")
                   .arg(QLatin1String(kItemTable)));
    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        if (row.isEmpty())
            continue; // 损坏行不写残条目（writeEntitiesPart 键缺跳过同门）
        iq.addBindValue(row.value(QStringLiteral("eid")).toString());
        iq.addBindValue(rowToJson(row));
        if (!iq.exec()) {
            qWarning() << "EntityStageStore: item snapshot insert failed:"
                       << iq.lastError().text();
            return false;
        }
    }
    return true;
}

QVariantList EntityStageStore::loadItemSnapshot() const
{
    QVariantList out;
    if (!isBound())
        return out;
    {
        QSqlDatabase db = openStageConnection(m_dbPath);
        if (db.isOpen()) {
            // 表缺席（旧档）→ SELECT 失败 = 空列表不崩（loadEntities 同门硬门）。
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT data FROM %1 ORDER BY oid")
                          .arg(QLatin1String(kItemTable)))) {
                while (q.next()) {
                    QVariantMap row = jsonToRow(q.value(0).toString());
                    if (!row.isEmpty())
                        out.append(row);
                }
            }
        }
    }
    if (QSqlDatabase::contains(kEntityStageConn))
        QSqlDatabase::removeDatabase(kEntityStageConn);
    return out;
}
