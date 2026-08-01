#ifndef WORLDSTORE_H
#define WORLDSTORE_H

#include <QObject>
#include <QtQml/qqml.h>

#include "world.h" // Q_PROPERTY(World*) + chunks() 路由（World 层只读，同层 include 不破铁律）

// 世界存档（SQLite，PLAN §2-L + §1「SQLite（事务性）+ 自描述 chunk blob + 迁移注册表」；§2 分层：
// World 层）。每个世界 = saves/ 下一个 .sqlite 文件（机制等价 MC 单文件夹存档，但用单库事务性更强）。
//
// 职责（仅 World 层依赖：Qt Sql + Chunk/ChunkManager/World + Core；**不**依赖 Renderer/Physics/Game，
// 故玩家态 / 物品以**裸原语**经 QML 编排传入，WorldStore 不持 Game 层对象引用——保持依赖只向下）：
//   - 世界列表：扫描 saves/ 下 *.sqlite 读 meta（名 / 种子 / 尺寸 / 上次游玩）。
//   - 新建世界：createWorld(name, seed) → 建库 + 写 meta（worldgen 不在此做——玩世界时由 World 生成）。
//   - 删除世界：deleteWorld(file)。
//   - 打开世界：openWorld(file) → 后续 saveAll / loadChunks / loadMeta / savePlayerData / loadPlayerData
//     作用于此库。
//   - 全量保存：saveAll() → 把当前 World 的 25 chunk 序列化为 chunk blob（voxels+state+light 三段定长
//     BLOB）+ 刷 meta（seed/dims/name/played_at）。事务性写入（BEGIN/COMMIT）。
//   - 加载地形：loadChunks() → 把 chunk blob 回填进 World（World 须先 beginLoad 把网格零填充；本方法
//     写完后由 caller 调 World.finishLoad 触发重建）。
//   - 玩家态：savePlayerData(QVariantMap) / loadPlayerData() → 玩家位姿 / 模式 / 血饥 / 背包（hotbar 9 +
//     main 27）以 JSON 文本存 player_state 表（自描述、跨版本可读；裸原语由 QML 编排，WorldStore 不解析
//     Game 层语义，只存 / 取整块 JSON）。
//   - 迁移注册表：PRAGMA user_version 烘 kSchemaVersion；新建库写版本、打开库校验版本。版本高于本程序
//     支持 → 拒绝打开 + 大声告警（PLAN §2-E「存档损坏 → 停机」；用户应降版本 / 手动迁移）。
//
// 分层（PLAN §2）：World 层。chunks() 路由 + Chunk 原始字节访问（同层，voxelDataMut 等访问器见 chunk.h）。
// 不 include playercontroller/hotbar（Game 层，向上违铁律）—— 玩家态经 Q_INVOKABLE 裸 QVariantMap 边界
// 传入（同 chunkgeometry 不 include worldclock 的「裸 QVector3D 边界」先例）。
class WorldStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldStore)
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)

public:
    explicit WorldStore(QObject *parent = nullptr);
    ~WorldStore() override;

    World *world() const { return m_world; }
    void setWorld(World *w);

    // 世界列表（扫描 saves/ 下 *.sqlite，逐个读 meta）。返回 QVariantList<QVariantMap>，每项含：
    //   file（文件名，相对 saves/，作唯一标识）/ name / seed / width / height / depth / playedAt（ms）。
    //   读单个库失败（损坏 / 版本不符）→ 跳过该库 + qWarning（不整列失败，PLAN §2-E 保持运行）。
    Q_INVOKABLE QVariantList worldList() const;
    // 新建世界：建库 + 写 meta。返回文件名（相对 saves/，作后续 openWorld 入参）；失败返回空串。
    //   worldgen 不在此做 —— 创建只落 meta，玩世界时由 World 按 seed 生成（首次进入即新生成地形）。
    Q_INVOKABLE QString createWorld(const QString &name, int seed);
    // 删除世界（文件名相对 saves/）。失败（不存在 / 无权删）→ false + qWarning。
    Q_INVOKABLE bool deleteWorld(const QString &file);
    // 打开已有世界（文件名相对 saves/）→ 后续 saveAll/loadChunks/loadMeta/savePlayerData/loadPlayerData
    //   作用于该库。版本校验 / schema 初始化在此做；返回是否成功打开（版本不符 / 损坏 → false）。
    Q_INVOKABLE bool openWorld(const QString &file);
    // 关闭当前库（释放连接）。save&exit 后 / 切世界前调，避免连接残留。
    Q_INVOKABLE void closeWorld();
    // 当前是否有库打开。
    Q_INVOKABLE bool isOpen() const { return m_open; }

    // 保存当前 World 的全部 chunk blob + 刷 meta（name/seed/dims/playedAt）。须先 openWorld。
    //   事务性写入（一次 COMMIT）。返回是否成功（无 world / 未打开 / SQL 失败 → false + qWarning）。
    Q_INVOKABLE bool saveAll(const QString &name);
    // 读当前库的 meta（name/seed/width/height/depth/playedAt）。未打开 → 空 Map。
    Q_INVOKABLE QVariantMap loadMeta() const;
    // 把当前库的 chunk blob 回填进 World（World 须已 beginLoad 零填充网格）。返回读到的 chunk 数；
    //   失败 → -1。caller 随后调 World.finishLoad 触发重建。
    Q_INVOKABLE int loadChunks();

    // 玩家态（JSON 文本，自描述）：存 / 取整块。QML 编排把 player.pos/yaw/pitch/mode + playerState
    //   health/hunger + hotbar 9 + main 27 打包成 QVariantMap 传入；loadPlayerData 取出后 QML 拆包回填。
    Q_INVOKABLE bool savePlayerData(const QVariantMap &data);
    Q_INVOKABLE QVariantMap loadPlayerData() const;
    // 当前库是否存有玩家态（首次进入新世界时无 → 用默认出生态）。
    Q_INVOKABLE bool hasPlayerData() const;
    // 当前库是否存有 chunk blob（= 是否曾保存过地形）。WorldStore 据此分流：有 → 加载存档地形，
    //   无 → 新世界走 World.regenerate(seed)（deterministic worldgen）。无副作用（SELECT COUNT）。
    Q_INVOKABLE bool hasChunks() const;

signals:
    void worldChanged();

private:
    // saves 目录解析（仿 main.cpp resolveLogFilePath：<exeDir>/../saves 开发期 / AppLocalDataLocation 部署）。
    QString savesDir() const;
    // 库全路径（file 为相对 saves/ 的文件名）。
    QString dbPath(const QString &file) const;
    // 在当前连接上初始化 schema（CREATE TABLE IF NOT EXISTS）+ 写 user_version=kSchemaVersion。
    //   返回是否成功。版本高于 kSchemaVersion → false（caller qWarning）。
    bool initSchema();
    // 文件名净化：保留字母数字 / 中文 / -_，其余替为 _；空 → "world"。
    static QString sanitizeName(const QString &name);

    World *m_world = nullptr;
    bool m_open = false;       // 是否有库打开
    QString m_openFile;        // 当前打开库的相对文件名（saveAll 刷 meta 用）

    static constexpr int kSchemaVersion = 1; // PRAGMA user_version；schema 变更时 +1 并写迁移
};

#endif // WORLDSTORE_H
