#include "savebridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>

// ── t1057 SaveCoordinator 生产接线桥实现（语义与选型立证见 savebridge.h 类头注）──────────────

SaveBridge *SaveBridge::instance()
{
    static SaveBridge inst; // 进程全局唯一（无状态桥——钩子缝除外，生产恒空）
    return &inst;
}

// saves/ 目录三级解析（worldstore::savesDir 镜像——登记的镜像面；与 StreamingBridge::savesDir
// 同款同源：exeDir/../saves → AppLocalData → exe 同级兜底）。
QString SaveBridge::savesDir()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/../saves")).absolutePath(),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
             + QStringLiteral("/saves")).absolutePath()
    };
    for (const QString &dir : candidates) {
        if (!dir.isEmpty() && QDir().mkpath(dir))
            return dir;
    }
    return exeDir + QStringLiteral("/saves"); // 兜底（mkpath 在 exe 同级仍可能成功）
}

QString SaveBridge::resolveSavePath(const QString &file)
{
    // 绝对路径直用（矩阵临时库先例——openWorld 绝对路径同门）；相对名落 saves/（镜像解析）。
    return QDir(savesDir()).absoluteFilePath(file);
}

bool SaveBridge::saveViaCoordinator(WorldStore *store, const QString &worldFile,
                                    const QString &worldName, const QVariantList &chests,
                                    const QVariantList &furnaces,
                                    const QVariantList &dispensers, const QVariantMap &worldTime,
                                    const QVariantMap &bedSpawn, const QVariantMap &playerData,
                                    const QVariantMap &progress, const QVariantList &hoppers,
                                    const QVariantList &brewingStands, const QVariantList &signs,
                                    const QVariantMap &mapDataset, const QVariantList &entities)
{
    if (!store || !store->isOpen() || worldFile.isEmpty()) {
        qWarning() << "SaveBridge::saveViaCoordinator: store not open / no world file - refusing";
        return false; // 诚实失败（caller 重试 + toast 兜底——t974 完成门同门）
    }
    // t1070 件三：**长活 coordinator**（savebridge.h m_coord 注——r2015 复用前提兑现 + 陈旧
    //   冻结点防面论证；旧逐保存栈上实例 = Review_2026-09-18 #1 指认面，随本单退役）。bind
    //   每保存重绑（store/路径逐调用传入语义不变）；钩子逐保存转发（r2031d 钉文本原样）。
    //   只读恢复面（recoveryInfo）仍栈上局部实例——recover 不触冻结缓冲，零复用诉求。
    SaveCoordinator &coord = m_coord;
    coord.bind(store, resolveSavePath(worldFile));
    coord.setFaultHook(m_faultHook); // 生产 = 恒空钩 = 无注入形态；矩阵腿经 C++ 面挂
    coord.setFlushHook(m_flushHook); // t1129：流式冲洗逐保存转发（生产 = StreamingBridge 登记；
                                     //   矩阵腿覆写同门——缺省空 = fixed 形态零动作）
    coord.setFlushCommitHook(m_flushCommitHook);
    // 载荷逐参透传（SaveRequest 字段序 = 本签名形参序——头注选型立证）；三写本体在
    // coordinator 内照旧调 WorldStore 现有 Q_INVOKABLE（worldstore 冻结域零改动）。
    SaveRequest req;
    req.name = worldName;
    req.chests = chests;
    req.furnaces = furnaces;
    req.dispensers = dispensers;
    req.hoppers = hoppers; // t1080 漏斗内容透传（SaveRequest 字段序 = 签名形参序同构）
    req.brewingStands = brewingStands; // t1113 补正：酿造内容透传（t1097 链缺口——下游第 8 参此前无载荷源）
    req.signs = signs;                 // t1113 牌子文本透传（载荷字段序 = 签名形参序同构）
    req.mapDataset = mapDataset;       // t1132 地图数据集透传（MapStore::exportVariant 产物——
                                       //   探索面与地形同一存档点；缺省空 = 表清空[会话无数据集]）
    req.entities = entities;           // t1133 生物持久化透传（EntityManager::exportPersistedEntities
                                       //   产物——存活生物全档快照，死亡不入档；缺省空 = 表清空）
    req.worldTime = worldTime;
    req.bedSpawn = bedSpawn;
    req.playerData = playerData;
    req.progress = progress;
    const SaveReceipt r = coord.saveAll(req);
    if (!r.ok()) {
        qWarning() << "SaveBridge::saveViaCoordinator: coordinated save failed (gen" << r.generation
                   << "code" << r.error.code << "-"
                   << (r.error.message ? r.error.message : "") << ")";
        return false; // 败 = 标记留档（gen>complete → 下次读档 Interrupted）+ 返回 false，
                      //   与旧 runExitSave「三写任一败即假」返回语义逐位同
    }
    return true; // 全成 + complete 戳盖讫（成功唯一权威——r2015 验收③）
}

QString SaveBridge::recoveryState(const QString &worldFile) const
{
    const SaveGenerationInfo info = recoveryInfo(worldFile);
    switch (info.state) {
    case SaveRecoveryState::Clean:
        return QStringLiteral("clean");
    case SaveRecoveryState::Interrupted:
        return QStringLiteral("interrupted");
    case SaveRecoveryState::OpenError:
        return QStringLiteral("open-error"); // #5②：读不了 ≠ fresh，不谎报
    case SaveRecoveryState::Fresh:
        break;
    }
    return QStringLiteral("fresh");
}

SaveGenerationInfo SaveBridge::recoveryInfo(const QString &worldFile) const
{
    // 只读台账面——recover 不触下游 store（bind 空指针即可；r2015 recover 只用 dbPath）。
    SaveCoordinator coord;
    coord.bind(nullptr, resolveSavePath(worldFile));
    return coord.recover();
}

// ── t1064 探锁退避（语义契约见 savebridge.h；探针选型立证录此处）──────────────────────────
// 探针三选型对比（为何是「独立连接 BEGIN EXCLUSIVE」而非登记原文示例的 saveProgress）：
//   ✗ saveProgress 单行 upsert（review0901 #36 原文示例面）：走 store 计数面 → 探锁成功也
//     ++m_saveOkCount，把 t974「成功 = +3」观测口径破坏成 +4；要避免就得动 worldstore 加
//     不计数探针（worldstore 冻结域，r2031d 禁触反探钉死）——两头堵。
//   ✗ 只读 SELECT 探针：不取写锁，测不出外部 EXCLUSIVE 持锁（savecoordinator #5② 注原话：
//     「SQLite open 是惰性的，锁占常在读面才炸」——只读恰是测不出的那一面）。
//   ✓ 独立连接 BEGIN EXCLUSIVE + ROLLBACK：BEGIN EXCLUSIVE 正是外部锁下整条保存链的第一失败
//     面（marker-first 台账写同型失败；r2015c/r2027d/r2031c 真锁注入先例全是这一手）——探它
//     的失败 = 重试同败的充分预测；ROLLBACK 立即放手 = 探针零数据写（无行无表）零计数面；
//     BUSY_TIMEOUT 显式归零 = 探针即败即返（不向退出路径引入隐藏等待）。
int SaveBridge::exitSaveRetryBackoff(WorldStore *store, const QString &worldFile)
{
    // 无库可探 = 无锁面 → 零退避（caller 立即重试，与旧 0ms 行为同门——绝不给无锁失败白添延迟）。
    if (!store || worldFile.isEmpty())
        return 0;
    const QString dbPath = resolveSavePath(worldFile);
    if (!QFileInfo::exists(dbPath))
        return 0;
    // 独立命名连接（开-用-关；savecoordinator 台账连接同款形态，与 worldstore / 台账连接互不占用）。
    static const char *const kProbeConn = "voxelsandbox_savebridge_probe";
    if (QSqlDatabase::contains(kProbeConn))
        QSqlDatabase::removeDatabase(kProbeConn);
    bool locked = true; // 保守缺省：探不了（open 拒/病）= 当作锁在持 → 退避一档（误判方向安全）
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kProbeConn);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0")); // 探针即败即返（无隐藏等待）
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("BEGIN EXCLUSIVE"))) {
                locked = false; // 拿到写锁 = 锁已释放 → 零退避
                QSqlQuery release(db);
                release.exec(QStringLiteral("ROLLBACK")); // 立即放手（探针零痕迹：无行无表无计数）
            }
        }
    }
    QSqlDatabase::removeDatabase(kProbeConn);
    if (!locked)
        return 0;
    qWarning() << "SaveBridge::exitSaveRetryBackoff: save db locked by an external process -"
               << "backing off" << kExitSaveBackoffMs << "ms before the single retry";
    QElapsedTimer t;
    t.start();
    QThread::msleep(kExitSaveBackoffMs); // 同步一档（关窗/退出路径本就同步阻塞等写完，t974 门同序）
    return int(t.elapsed());
}
