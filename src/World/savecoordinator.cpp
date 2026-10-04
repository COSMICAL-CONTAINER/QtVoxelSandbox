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
//   - WorldStore：持久化面 = 其 Q_INVOKABLE（saveAll/savePlayerData/saveProgress——裸调用
//     caller 形态原样）+ t1129 原子多面保存域原语（beginAtomicSave/write*Part/
//     stampLedgerKeyInTxn/commitAtomicSave/rollbackAtomicSave/runInSaveTransaction——表名键名
//     由本域注入 = 单一权威不离开本文件；isOpen/world/setWorld）。
//   - t1057 #5②（review0916 #5）：recover() 的 OpenError 可区分错误态——SQLite open 惰性，锁占
//     常在读面才炸，故「open 失败」与「SELECT busy」两面目同归 OpenError，与「表真缺席=旧档
//     Fresh」用 sqlite_master 只读探针辨析（实现见 recover() 内注）；saveAll 对 prior==OpenError
//     拒存（禁不可知台账上的代次重编）。
//   - t1066 保存链连接面 busy 等待归零：台账连接两设置点（recover 读面 + coordUpsert 写面）显式
//     归零 busy 等待（连接盘点表/自锁竞态面论证 = worldstore.cpp 同门头注）。Qt QSQLITE 默认
//     busy timeout 秒级——外部锁全程持有时保存链**首试**在本类 recover() 的台账 SELECT 上阻塞
//     秒级才失败（t1064 真锁腿 wall≈29s 实测），t1064 退避只约束重试间隔不约束首试阻塞。归零后
//     首试在第一条撞锁语句即败，锁占三面目判读（#5②）语义零变——Unreadable/OpenError 派生原样
//     只是不再先等秒级；失败收敛交 t1064 探锁退避（首试瞬时败 → 探锁 → 150ms 一档 → 恰一次
//     重试）。自锁竞态面：台账连接开-用-关于协调层五段流程内、全程 GUI 线程串行（本文件头注
//     登记原样），内部并发零窗口。

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
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（台账读面）
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
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // t1066：锁下即败即返（台账写面）
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
        ++m_bufferRebuilds; // t1070 件三诊断面：重建恰落此分支（同 dims 连续保存复用 = 不进此分支）
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

// ── saveAll：marker-first + 单事务原子统一保存协议（t1129 SAVE-01 修复本体；步号见头注）─────
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

    // 步④ 冻结缓冲回放 + 下游临时改绑（WorldStore 从 m_world 读 → 缓冲世界承载冻结点；
    //   收尾恢复原绑。回放失败在此早退——尚未改绑，无需恢复）。
    if (!ensureBuffer(snap) || !applySnapshotToBuffer(snap)) {
        r.error = Error{ kErrSaveSnapshotApply, "frozen snapshot apply failed" };
        return r;
    }
    m_store->setWorld(m_buffer);

    // 步⑤ 单事务开启（t1129 本体）：四面 + complete 戳全部收进下游 kConn 同一事务——任一后续
    //   失败 / 注入 → rollback = 零部分写（四面恰为上一完整代次），提交成功 = 四面恰为本代次。
    const bool playerNeeded = !req.playerData.isEmpty();
    const bool progressNeeded = !req.progress.isEmpty();
    if (!m_store->beginAtomicSave()) {
        m_store->setWorld(live);
        r.error = Error{ kErrSaveStoreRejected, "atomic save begin failed" };
        return r;
    }
    // 步⑤b 流式冲洗（事务内；钩空 = fixed 形态零动作）：驻留编辑 chunk_edits + save_gen 推进
    //   随保存事务同生共死（回滚面下行不残留 = 流式半边的「恰 A」保证）。
    if (m_flushHook && !m_store->runInSaveTransaction(m_flushHook)) {
        m_store->rollbackAtomicSave();
        m_store->setWorld(live);
        r.error = Error{ kErrSaveStoreRejected, "streaming flush failed (in-transaction)" };
        return r;
    }
    // 步⑥ world 段（表写体 = 旧 saveAll 的表写段原样；失败 → rollback 零部分写）。
    if (!m_store->writeWorldPart(req.name, req.chests, req.furnaces, req.dispensers,
                                 req.worldTime, req.bedSpawn, req.hoppers,
                                 req.brewingStands, req.signs, req.mapDataset)) {
        m_store->rollbackAtomicSave();
        m_store->setWorld(live);
        r.error = Error{ kErrSaveStoreRejected, "downstream world part failed" };
        return r;
    }
    // 步⑥b 实体段（t1133 ENTITY-01）：生物持久化表随统一保存链同事务落盘（world 段旁新部件写
    //   ——步⑥ 调用点形保留【t1132 源钉原样幸存】，本部件与之共用 kConn 事务，任一失败回滚 =
    //   零部分写）。写序与容器表同门 = **无条件 DELETE+INSERT**（空载荷 = 表清空——「死亡后
    //   保存不复活」硬语义：全灭会话的空快照必须把上一档的旧生物清掉，禁按空跳过；协调层矩阵
    //   腿的空载荷在无行表上 DELETE = no-op，既有腿零扰动）。
    if (!m_store->writeEntitiesPart(req.entities)) {
        m_store->rollbackAtomicSave();
        m_store->setWorld(live);
        r.error = Error{ kErrSaveStoreRejected, "downstream entities part failed" };
        return r;
    }
    // 步⑦ player 段（短路保留：world 段失败不至此；注入 = 回滚——【t1129】不再是「world 已落、
    //   player 未落」的混合写，而是零部分写）。
    if (playerNeeded) {
        if (injected(SaveFaultStage::Player)) {
            m_store->rollbackAtomicSave(); // t1129 NEG-2 摘面行：摘本行 = Player 段故障不再回滚
            m_store->setWorld(live);
            r.error = Error{ kErrSaveFaultInjected, "fault injected at player stage" };
            return r;
        }
        if (!m_store->writePlayerPart(req.playerData)) {
            m_store->rollbackAtomicSave();
            m_store->setWorld(live);
            r.error = Error{ kErrSaveStoreRejected, "downstream player part failed" };
            return r;
        }
    }
    // 步⑧ progress 段（同短路链）+ complete 戳入事务（【t1129】戳与数据同生共死——「数据已写、
    //   戳未盖」的假 Interrupted 窗消灭）。
    if (progressNeeded) {
        if (injected(SaveFaultStage::Progress)) {
            m_store->rollbackAtomicSave();
            m_store->setWorld(live);
            r.error = Error{ kErrSaveFaultInjected, "fault injected at progress stage" };
            return r;
        }
        if (!m_store->writeProgressPart(req.progress)) {
            m_store->rollbackAtomicSave();
            m_store->setWorld(live);
            r.error = Error{ kErrSaveStoreRejected, "downstream progress part failed" };
            return r;
        }
    }
    if (!m_store->stampLedgerKeyInTxn(kCoordTable, kCoordKeyComplete, newGen)) {
        m_store->rollbackAtomicSave();
        m_store->setWorld(live);
        r.error = Error{ kErrSaveCoordSql, "complete stamp failed (lock/disk?)" };
        return r;
    }
    // 步⑨ Finalize 段注入（【t1129】新语义：注入 = 提交 abort——崩在收尾的剩余形态，零部分
    //   写；旧「三部分已写仍不报成功」的面升级为「零部分写且不报成功」）。
    if (injected(SaveFaultStage::Finalize)) {
        m_store->rollbackAtomicSave();
        m_store->setWorld(live);
        r.error = Error{ kErrSaveFaultInjected, "fault injected at finalize stage" };
        return r;
    }
    // 步⑩ 提交（段=1 口径计数在提交成功尾补齐——t974 面逐位同旧；提交失败 = 方法内已回滚）。
    const int partsWritten = 1 + (playerNeeded ? 1 : 0) + (progressNeeded ? 1 : 0);
    if (!m_store->commitAtomicSave(partsWritten)) {
        m_store->setWorld(live);
        r.error = Error{ kErrSaveStoreRejected, "atomic save commit failed" };
        return r;
    }
    // 步⑪ 提交成功尾：流式账面收口（内存侧清未落盘账——库零触碰；回滚路径绝不至此）。
    if (m_flushCommitHook)
        m_flushCommitHook();
    // 步⑫ 恢复下游原绑（恒达——此后下游对调用方恢复保存前的观察面）。
    m_store->setWorld(live);

    // ok()：本代次完整收尾（四面 + complete_generation 同一事务落地；三部分共享同一戳）。
    r.worldSaved = true;
    r.playerSaved = playerNeeded;
    r.progressSaved = progressNeeded;
    return r;
}
