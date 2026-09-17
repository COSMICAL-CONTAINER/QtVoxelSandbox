#include "savebridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

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
                                    const QVariantMap &progress)
{
    if (!store || !store->isOpen() || worldFile.isEmpty()) {
        qWarning() << "SaveBridge::saveViaCoordinator: store not open / no world file - refusing";
        return false; // 诚实失败（caller 重试 + toast 兜底——t974 完成门同门）
    }
    // 逐保存栈上 coordinator：开-用-关（r2015 连接经济学原样——无长活连接状态）。
    SaveCoordinator coord;
    coord.bind(store, resolveSavePath(worldFile));
    coord.setFaultHook(m_faultHook); // 生产 = 恒空钩 = 无注入形态；矩阵腿经 C++ 面挂
    // 载荷逐参透传（SaveRequest 字段序 = 本签名形参序——头注选型立证）；三写本体在
    // coordinator 内照旧调 WorldStore 现有 Q_INVOKABLE（worldstore 冻结域零改动）。
    SaveRequest req;
    req.name = worldName;
    req.chests = chests;
    req.furnaces = furnaces;
    req.dispensers = dispensers;
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
