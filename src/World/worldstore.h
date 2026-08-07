#ifndef WORLDSTORE_H
#define WORLDSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QVariantList>
#include <functional>

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
//   - 箱子内容（t188）：saveAll(name, chests) 把 ChestStore 的箱子（坐标键控的 27 槽物品）落盘到 chests
//     表（与 chunks / meta 同一事务，原子写）；loadChests() 读回。cheststore 在 Game 层 → WorldStore **不**
//     include 它（向上违铁律），箱子数据经 Q_INVOKABLE 裸 QVariantList 边界传入 / 取出（同 player_state
//     模式：每项 {x,y,z,slots:[{id,count}×27]}，WorldStore 不解析 Game 层语义，只存 / 取）。
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
    //   t191：配套删除截图封面 PNG（与 .sqlite 同名并排的 sidecar），免孤儿文件残留。
    Q_INVOKABLE bool deleteWorld(const QString &file);
    // t192 重命名世界（文件名相对 saves/）：只改 world_meta 的 name（显示名），**.sqlite 文件名不动** ——
    //   文件名是内部唯一键（worldList/openWorld/saveAll 全用它路由），改文件名会引入路径穿越 / 跨文件系统
    //   重命名等复杂度且无用户可见收益（用户只见 name）。用独立连接 UPDATE（与 worldList 同模式：不调
    //   initSchema、不依赖主连接是否打开，renameWorld 通常在世界列表 UI 触发、此时无库打开）。
    //   失败（文件不存在 / 打不开 / SQL 失败）→ false + qWarning（§2-E）；newName 空白 → 回退 "新世界"
    //   （与 createWorld 同语义，免世界列表出现无名条目）。
    Q_INVOKABLE bool renameWorld(const QString &file, const QString &newName);

    // t191 截图封面：世界存档的缩略图 PNG（与 .sqlite 同名并排放：saves/<base>.png）。
    //   - coverPath(file)：返回封面 PNG 绝对路径（供 WorldList delegate 拼 file:/// URL 加载缩略图）。
    //   - saveCover(file, image)：把 grabToImage 拿到的 QImage 存为 PNG（QML 传 grabResult.image）。
    //     image 为 null / 写盘失败 → false + qWarning（不阻塞退出，§2-E）。
    //   - deleteCover(file)：删 PNG（deleteWorld 内部已调，此 Q_INVOKABLE 供外部按需清理）。
    //   file 均为相对 saves/ 的存档文件名（含 .sqlite）；封面 base 名取 QFileInfo::completeBaseName。
    Q_INVOKABLE QString coverPath(const QString &file) const;
    Q_INVOKABLE bool saveCover(const QString &file, const QVariant &image);
    Q_INVOKABLE bool deleteCover(const QString &file);
    // 打开已有世界（文件名相对 saves/）→ 后续 saveAll/loadChunks/loadMeta/savePlayerData/loadPlayerData
    //   作用于该库。版本校验 / schema 初始化在此做；返回是否成功打开（版本不符 / 损坏 → false）。
    Q_INVOKABLE bool openWorld(const QString &file);
    // 关闭当前库（释放连接）。save&exit 后 / 切世界前调，避免连接残留。
    Q_INVOKABLE void closeWorld();
    // 当前是否有库打开。
    Q_INVOKABLE bool isOpen() const { return m_open; }

    // 保存当前 World 的全部 chunk blob + 刷 meta（name/seed/dims/playedAt）。须先 openWorld。
    //   事务性写入（一次 COMMIT）：chunks + meta + chests 同事务原子落盘（t188）。chests 为
    //   ChestStore::allChests() 产物（每项 {x,y,z,slots:[{id,count}×27]}；空列表 → 清空 chests 表）。
    //   返回是否成功（无 world / 未打开 / SQL 失败 → false + qWarning）。
    Q_INVOKABLE bool saveAll(const QString &name, const QVariantList &chests = {});
    // 读当前库的 chests 表为 QVariantList（同 saveAll 的 chests 形状）。未打开 → 空列表。
    //   caller（Main.qml.enterWorld）转交 chestStore.loadAll 整体替换内存（清旧世界残留 + 填本世界箱子）。
    Q_INVOKABLE QVariantList loadChests() const;
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

    // PRAGMA user_version；schema 变更时 +1 并写迁移。v2（t188）= 新增 chests 表（纯加表，旧库 IF NOT EXISTS 幂等补建）。
    static constexpr int kSchemaVersion = 2;
    // t382 world_version：**数据语义**版本（方块 id 映射演进），存 world_meta 'world_version' 键。
    //   与 PRAGMA user_version（SQL schema 版本）刻意分离 —— schema 版本管表结构（加表 / 加列），
    //   world_version 管「chunk blob 里那个字节现在指代哪个方块」（block-id 重排）。改方块 id 语义时 bump
    //   本常量 + 在 migrations() 注册一条 remap 步，旧存档 openWorld 时自动迁移（不破老存档）。
    //   t348 用映射层（BlockRegistry::mcBlockId）而非重排引擎 id → 存档字节序未变 → kWorldVersion=1，
    //   migrations() 的 0→1 步为 identity（空 remap 表）。缺键（t382 前存档）→ 视为 0。
    static constexpr int kWorldVersion = 1;
    // 把箱子 QVariantList 落盘进 chests 表（DELETE 全量 + INSERT；用 kConn 连接，caller 已开事务）。
    //   失败 → false（caller rollback）。
    bool writeChests(const QVariantList &chests);

    // ── t382 迁移注册表（world_version → kWorldVersion 的数据迁移；详见类头注释 + migrations()）──
    // 单条迁移：把存档数据从 (targetVersion-1) 推进到 targetVersion。apply 对一个 chunk 的三段 blob
    //   就地改写 —— block-id 重排只动 voxels（states/light 传入以备将来 state-bit 重排，当前迁移不改）。
    using BlobMigrator = std::function<void(QByteArray &voxels, QByteArray &states, QByteArray &light)>;
    struct Migration {
        int targetVersion;   // 本步把数据推进到的目标 world_version
        BlobMigrator apply;  // 对单 chunk 三段 blob 的就地变换
    };
    // 迁移注册表（单一权威）：按 targetVersion 升序的迁移列表。openWorld 时据存档 world_version 取
    //   (stored, kWorldVersion] 内的步按序应用。新增 block-id 重排：追加
    //   Migration{N+1, makeIdRemap({{oldId,newId},...})} 并同步 bump kWorldVersion=N+1。
    static const QList<Migration> &migrations();
    // 构造一条 block-id 重排迁移：remap 给出「旧 id → 新 id」的非 identity 映射（未列出 = 保持不变）。
    //   返回的 BlobMigrator 一次性建 256 字节查表、遍历 voxels 每字节 O(1) 改写（states/light 不动）。
    //   空 remap = identity（用于「打通链路但无需重排」的迁移步，如 t382 的 0→1）。
    static BlobMigrator makeIdRemap(const QHash<quint8, quint8> &remap);
    // 把当前库的 chunk blob 从存档 world_version 推进到 kWorldVersion：读 world_meta 'world_version'
    //   （缺键→0），若 < kWorldVersion 则逐 chunk 行应用 migrations()、写回、刷版本（事务原子）。
    //   已是当前版本 → no-op 返 true。openWorld 在 initSchema 后调一次。失败 → false（caller 拒绝打开）。
    bool migrateWorldData();
};

#endif // WORLDSTORE_H
