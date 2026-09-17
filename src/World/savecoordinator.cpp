#include "savecoordinator.h"

#include "chunk.h"
#include "chunkmanager.h"
#include "world.h"
#include "worldstore.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <cstring> // std::memcpy（冻结快照回放）

// R20.15 SaveCoordinator 实现（契约与协议论证见 savecoordinator.h 头注，此处不赘）。
// 实现要点登记：
//   - save_coord 台账用**独立命名连接**（与 WorldStore 的 kConn 互不占用；仿 worldstore 的
//     scan/rename 独立连接先例），每次操作开-用-关，无长活连接状态。
//   - 台账表 IF NOT EXISTS 幂等（纯加表；旧库首次被协调层触碰时补建，不 bump 任何版本——
//     兼容性论证见头注④）。
//   - WorldStore 零改动：持久化面只走其现有 Q_INVOKABLE（saveAll/savePlayerData/saveProgress/
//     isOpen/world/setWorld）。
//   - t1057 #5②（review0916 #5）：recover() 的 OpenError 可区分错误态——SQLite open 惰性，锁占
//     常在读面才炸，故「open 失败」与「SELECT busy」两面目同归 OpenError，与「表真缺席=旧档
//     Fresh」用 sqlite_master 只读探针辨析（实现见 recover() 内注）；saveAll 对 prior==OpenError
//     拒存（禁不可知台账上的代次重编）。

// 协调层台账连接名（独立于 worldstore 的 "voxelsandbox_worldstore"）。
static const char *const kCoordConn = "voxelsandbox_savecoordinator";
// 台账表与两键（value 一律十进制文本，与 world_meta 同风格）。
static const char *const kCoordTable = "save_coord";
static const char *const kCoordKeyGeneration = "generation";          // 最后尝试代次（在途标记）
static const char *const kCoordKeyComplete = "complete_generation";   // 最后完整代次（成功戳）

SaveCoordinator::~SaveCoordinator()
{
    delete m_buffer; // 拥有的冻结缓冲（World 析构关自身；无 Qt Sql 连接残留）
    m_buffer = nullptr;
}

void SaveCoordinator::bind(WorldStore *store, const QString &dbFilePath)
{
    m_store = store;
    m_dbPath = dbFilePath;
}

// ── recover()：台账只读 + 状态派生（Fresh/Clean/Interrupted/OpenError 判据见头注）──────────
SaveGenerationInfo SaveCoordinator::recover() const
{
    SaveGenerationInfo info; // 缺省 Fresh 0/0
    if (m_dbPath.isEmpty() || !QFileInfo::exists(m_dbPath))
        return info; // 无库 = 无台账 = Fresh（新库/已删档）
    // 台账读数结局（#5② 三面目）：Ok = 两键已读；NoTable = 表真缺席（旧档合法形态 = Fresh）；
    //   Unreadable = 库打不开 / 读不了（锁占 / 病）——可区分错误态，绝不静默按 Fresh。
    enum class LedgerRead
    {
        Ok,
        NoTable,
        Unreadable
    };
    LedgerRead read = LedgerRead::Unreadable; // 缺省不可读（open 失败也不静默 = #5②）
    if (QSqlDatabase::contains(kCoordConn))
        QSqlDatabase::removeDatabase(kCoordConn);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kCoordConn);
        db.setDatabaseName(m_dbPath);
        if (db.open()) {
            // 只读，不建表（recover 对无台账旧档不写任何东西）。
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT key, value FROM %1").arg(QLatin1String(kCoordTable)))) {
                read = LedgerRead::Ok;
                while (q.next()) {
                    const QString k = q.value(0).toString();
                    const qint64 v = q.value(1).toLongLong();
                    if (k == QLatin1String(kCoordKeyGeneration))
                        info.generation = v;
                    else if (k == QLatin1String(kCoordKeyComplete))
                        info.completeGeneration = v;
                }
            } else {
                // SELECT 失败两面目辨析（#5②）：sqlite_master 只读探针——成功且无行 = save_coord
                //   表真缺席（旧档，Fresh 面保持 r2015 语义）；探针也失败（锁占时读 sqlite_master
                //   同样 busy）/ 有行但主 SELECT 失败 = 台账读不了 → 维持 Unreadable 不谎报。
                QSqlQuery probe(db);
                if (probe.exec(QStringLiteral(
                        "SELECT name FROM sqlite_master WHERE type='table' AND name='%1'")
                        .arg(QLatin1String(kCoordTable)))
                    && !probe.next())
                    read = LedgerRead::NoTable;
            }
        } else {
            qWarning() << "SaveCoordinator: ledger open failed:" << db.lastError().text();
        }
    }
    if (QSqlDatabase::contains(kCoordConn))
        QSqlDatabase::removeDatabase(kCoordConn);
    info.state = read == LedgerRead::Unreadable
                     ? SaveRecoveryState::OpenError // #5②：读不了 ≠ 无台账（Fresh 留给旧档/新库）
                     : (info.generation <= 0
                            ? SaveRecoveryState::Fresh
                            : (info.generation > info.completeGeneration ? SaveRecoveryState::Interrupted
                                                                         : SaveRecoveryState::Clean));
    return info;
}

// ── coordUpsert：台账唯一写点（独立连接独立事务；任何 SQL 失败 = false）───────────────────
bool SaveCoordinator::coordUpsert(const char *key, qint64 value) const
{
    if (m_dbPath.isEmpty())
        return false;
    if (QSqlDatabase::contains(kCoordConn))
        QSqlDatabase::removeDatabase(kCoordConn);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kCoordConn);
        db.setDatabaseName(m_dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            // IF NOT EXISTS 幂等建表（纯加表；对外部 EXCLUSIVE 锁这本身就是一次写 → 失败即挡，
            // marker-first 第一闸）。
            if (q.exec(QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS %1 (key TEXT PRIMARY KEY, value TEXT NOT NULL)")
                    .arg(QLatin1String(kCoordTable)))) {
                QSqlQuery u(db);
                u.prepare(QStringLiteral("INSERT OR REPLACE INTO %1 (key, value) VALUES (?, ?)")
                              .arg(QLatin1String(kCoordTable)));
                u.addBindValue(QLatin1String(key));
                u.addBindValue(QString::number(value));
                ok = u.exec();
                if (!ok)
                    qWarning() << "SaveCoordinator: coord upsert failed for" << key << ":"
                               << u.lastError().text();
            } else {
                qWarning() << "SaveCoordinator: coord table ensure failed:"
                           << q.lastError().text();
            }
        }
    }
    if (QSqlDatabase::contains(kCoordConn))
        QSqlDatabase::removeDatabase(kCoordConn);
    return ok;
}

// ── 冻结：对活体 World 的唯一读点（验收①本体；逐 chunk 三段深拷贝）────────────────────────
WorldSaveSnapshot SaveCoordinator::captureSnapshot(const World &live)
{
    WorldSaveSnapshot snap;
    snap.seed = live.seed();
    const ChunkManager &cm = live.chunks();
    snap.width = cm.width();
    snap.height = cm.height();
    snap.depth = cm.depth();
    for (int cz = 0; cz < cm.chunksZ(); ++cz) {
        for (int cx = 0; cx < cm.chunksX(); ++cx) {
            const Chunk *c = cm.chunk(cx, cz);
            if (!c) continue; // 与 WorldStore::saveAll 同口径（空槽跳过）
            SaveChunkBlob b;
            b.cx = cx;
            b.cz = cz;
            const size_t n = c->voxelCount();
            // QByteArray(ptr, int) 构造即深拷贝（冻结点与活体自此解耦）。
            b.voxels = QByteArray(reinterpret_cast<const char *>(c->voxelData()), int(n));
            b.states = QByteArray(reinterpret_cast<const char *>(c->stateData()), int(n));
            b.light = QByteArray(reinterpret_cast<const char *>(c->lightData()), int(n));
            snap.chunks.append(b);
        }
    }
    return snap;
}

// ── 冻结缓冲就绪：dims 惰性重建 + beginLoad 零填充（无 worldgen；跨保存复用）───────────────
bool SaveCoordinator::ensureBuffer(const WorldSaveSnapshot &snap)
{
    const bool dimsMatch = m_buffer
        && m_buffer->chunks().width() == snap.width
        && m_buffer->chunks().depth() == snap.depth
        && m_buffer->chunks().height() == snap.height;
    if (!dimsMatch) {
        delete m_buffer;
        m_buffer = new World();
        // 尺寸 setter 各触一次 worldgen（一次性建造成本；复用面见头注登记），beginLoad 随即
        // 零填充覆盖。
        m_buffer->setWidth(snap.width);
        m_buffer->setDepth(snap.depth);
        m_buffer->setHeight(snap.height);
    }
    m_buffer->beginLoad(snap.seed); // 零填充分区网格 + 换 seed（World::beginLoad 无 worldgen）
    return m_buffer != nullptr;
}

// ── 快照回放：逐 chunk 三段 memcpy（尺寸不符即拒 = 防半写回放）────────────────────────────
bool SaveCoordinator::applySnapshotToBuffer(const WorldSaveSnapshot &snap)
{
    if (!m_buffer || snap.chunks.isEmpty())
        return false;
    const ChunkManager &cm = m_buffer->chunks();
    for (const SaveChunkBlob &b : snap.chunks) {
        Chunk *c = cm.chunk(b.cx, b.cz);
        if (!c)
            return false;
        const size_t n = c->voxelCount();
        if (size_t(b.voxels.size()) != n || size_t(b.states.size()) != n
            || size_t(b.light.size()) != n)
            return false;
        std::memcpy(c->voxelDataMut(), b.voxels.constData(), n);
        std::memcpy(c->stateDataMut(), b.states.constData(), n);
        std::memcpy(c->lightDataMut(), b.light.constData(), n);
    }
    return true;
}

// ── saveAll：marker-first 统一保存协议（五段流程；逐段注释 = 协议步号）────────────────────
SaveReceipt SaveCoordinator::saveAll(const SaveRequest &req)
{
    SaveReceipt r;
    const auto injected = [this](SaveFaultStage st) { return m_faultHook && m_faultHook(st); };

    if (!isBound() || !m_store) {
        r.error = Error{ kErrSaveCoordNotBound, "save coordinator not bound" };
        return r;
    }
    World *live = m_store->world();
    if (!live || !m_store->isOpen()) {
        r.error = Error{ kErrSaveStoreNotOpen, "downstream store not open / no live world" };
        return r;
    }

    // 步① marker-first：先落在途标记（gen 单调：max(最后尝试, 最后完整)+1，中断不重置不复用）。
    //   本步失败（外部锁 / 磁盘错误）→ 立即放弃，零部分尝试（Clean@旧代次不变）。
    if (injected(SaveFaultStage::BeginMark)) {
        r.error = Error{ kErrSaveFaultInjected, "fault injected at begin-mark" };
        return r;
    }
    const SaveGenerationInfo prior = recover();
    if (prior.state == SaveRecoveryState::OpenError) {
        // t1057 #5② 生产化：台账打不开 → 代次历史不可知。禁在不可知台账上从 0 重编 newGen
        // （那会把「最后尝试如 5」抹成 1 = 台账历史失真，review0916 #5 指认面）——按 marker-first
        // 第一闸同语义放弃：零部分尝试、receipt 失败（域内同码 kErrSaveCoordSql），调用方重试
        // 语义（t974 完成门）与锁失败路径共用。
        r.error = Error{ kErrSaveCoordSql, "generation ledger unreadable - refusing to renumber" };
        return r;
    }
    const qint64 newGen = qMax(prior.generation, prior.completeGeneration) + 1;
    if (!coordUpsert(kCoordKeyGeneration, newGen)) {
        r.error = Error{ kErrSaveCoordSql, "generation mark failed (lock/disk?)" };
        return r;
    }
    r.generation = newGen;

    // 步② 冻结：对活体的唯一读点（此后活体再变与本次存档无关——验收①）。
    const WorldSaveSnapshot snap = captureSnapshot(*live);

    // 步③ World 段注入窗：冻结后、持久化前（兼作「冻结后改动活体」的无头观察窗）。
    if (injected(SaveFaultStage::World)) {
        r.error = Error{ kErrSaveFaultInjected, "fault injected at world stage (post-freeze)" };
        return r; // gen 已标记、complete 未盖 → Interrupted（保守：尝试未完成）
    }

    // 步④ 冻结缓冲回放 + 下游临时改绑（WorldStore::saveAll 从 m_world 读 → 缓冲世界承载冻结点；
    //   收尾步⑧恢复原绑。回放失败在此早退——尚未改绑，无需恢复）。
    if (!ensureBuffer(snap) || !applySnapshotToBuffer(snap)) {
        r.error = Error{ kErrSaveSnapshotApply, "frozen snapshot apply failed" };
        return r;
    }
    m_store->setWorld(m_buffer);

    // 步⑤ world 部分（下游事务性写：chunks+meta+containers 原子）。
    r.worldSaved = m_store->saveAll(req.name, req.chests, req.furnaces, req.dispensers,
                                    req.worldTime, req.bedSpawn);

    // 步⑥ player 部分（短路：world 失败则不试——杜绝新版 player 压旧版 world 的混合写）。
    const bool playerNeeded = !req.playerData.isEmpty();
    bool playerFault = false;
    if (r.worldSaved && playerNeeded) {
        if (injected(SaveFaultStage::Player))
            playerFault = true;
        else
            r.playerSaved = m_store->savePlayerData(req.playerData);
    }

    // 步⑦ progress 部分（同短路链）。
    const bool progressNeeded = !req.progress.isEmpty();
    bool progressFault = false;
    if (r.worldSaved && !playerFault && (!playerNeeded || r.playerSaved) && progressNeeded) {
        if (injected(SaveFaultStage::Progress))
            progressFault = true;
        else
            r.progressSaved = m_store->saveProgress(req.progress);
    }

    // 步⑧ 恢复下游原绑（恒达——此后下游对调用方恢复保存前的观察面）。
    m_store->setWorld(live);

    // 步⑨ Finalize：全部请求部分成功（且无注入）才盖 complete 戳。崩溃/注入在其前 → gen>complete
    //   = Interrupted 如实标记；戳写失败同样按失败上报（验收③：没盖戳 = 没成功）。
    const bool partsOk = r.worldSaved && (!playerNeeded || r.playerSaved)
        && (!progressNeeded || r.progressSaved);
    const bool finalizeFault = injected(SaveFaultStage::Finalize);
    if (partsOk && !playerFault && !progressFault && !finalizeFault) {
        if (!coordUpsert(kCoordKeyComplete, newGen)) {
            r.error = Error{ kErrSaveCoordSql, "complete stamp failed (lock/disk?)" };
            return r;
        }
        return r; // ok()：本代次完整收尾（三部分共享同一 complete_generation）
    }
    if (r.error.code == 0) {
        if (!r.worldSaved)
            r.error = Error{ kErrSaveStoreRejected, "downstream world save failed" };
        else if (playerFault || progressFault || finalizeFault)
            r.error = Error{ kErrSaveFaultInjected, "fault injected mid-save" };
        else
            r.error = Error{ kErrSaveStoreRejected, "downstream part save failed" };
    }
    return r; // 失败：generation 已消耗、complete 未盖（Interrupted 由 recover() 判读）
}
