#ifndef CHUNKSTORE_H
#define CHUNKSTORE_H

// ── §29.5-W3 驱逐 + Edits-on-evict 落盘 / ChunkStore（refactor-plan §29.5.2 W3 原文：
//   「driver evictor 注入 → ChunkEvictor 生产缝实装（dirtyQuery = World 脏面、persistFn =
//   §29.5.3 选型、lifecycleTransition = ChunkManager 真转移）」+ §29.5.3 选型 1（D5 执行）：
//   「(a) 新增 per-chunk 附加表（save_coord 同款 additive 路线，零格式迁移零 bump，hard-gate
//   安全）」——本头即该附加表的载体）。
//
// ── additive 兼容性论证（r2015 save_coord 先例逐条对照，头注释立证）──────────────────────
//   · 表 additive：状态只落在**新增**的 chunk_edits 表（per-chunk 一行，键 = (cx,cz) 主键），
//     worldstore.{h,cpp} **一行禁触**（本类自带独立命名连接，仿 savecoordinator.cpp
//     kCoordConn 先例——开-用-关，无长活连接状态；与 WorldStore 的 kConn 互不占用）。
//   · 零 bump：CREATE TABLE IF NOT EXISTS 幂等建表（旧库首次被本类触碰时补建），user_version /
//     world_version / 任何既有表零变化——**无任何不可逆存档格式迁移**（数据安全硬门自查通过）。
//   · 旧码读新档 = 未知表全盲（loadMeta 只读 world_meta、loadChunks 只读 chunks 表——
//     worldstore 读路径不枚举未知表）；新码读旧档 = chunk_edits 不存在 → 幂等建表为空表 →
//     hasChunk 恒 false = Fresh 语义（未命中走现行生成路径，r2019 Load 语义不变）。
//
// ── 表内容与代次语义（W3 原版 + W5 契约修订留痕，头注释立证）────────────────────────────
//   行 = (cx, cz, generation, voxels, states, light)：blob = 驱逐/冲洗时刻该 chunk 三数组的
//   原样字节（体素 id + state + 光场，布局同 Chunk 数组——worldstore chunks 表行同构收窄到
//   单 chunk；**本表自洽**，读回由 World::restoreChunkFromBlob 直接物化）。
//   generation 契约 **§29.5-W5 修订（纠偏留痕非破坏）**：W3 原版 = per-chunk 累计落盘代次
//   （upsert 前读旧行 +1）；W5 起改为**世界保存代次刻度**——写入时读 stream_worlds.save_gen
//   并盖 stamp = save_gen + 1（驱逐在途写与保存冲洗写同式；冲洗流程在落盘后把 save_gen 推
//   进到恰为该 stamp——见 GameSession::flushResidentEditsForSave 头注拍序论证）。修订依据：
//   读档 overlay 仲裁要求「chunk_edits 行代次」与「世界存代次」**同刻度可比**（per-chunk
//   累计从 1 数起与世界保存次数从 1 数起不可互比，r2025 前的会话内闭环无此需求）。**观测值
//   兼容性**：无 stream_worlds 行的库上 save_gen 读 0 → stamp 恒 1——与 W3 原版「首写 = 1、
//   单次写后不重写」的全部已观测值逐位同值（W3 腿族的代次断言 [恒 1] 零扰动，实证 = r2025
//   全族回归绿）。marker-first 等价形态不变：单行 upsert 事务原子 = 「没写进 = 旧行原样保留
//   在旧代次」（事务本身就是 marker）。
//
// ── 持久化时机（W3 设计依据 + W5 增补，任务书 + §29.5.3 选型 1 (a)）──────────────────────
//   驱逐候选 dirty 时**即时落盘**（persistFn 缝实现，生产绑定点 = GameSession 流式会话）——
//   不走整世界 saveAll（fixed 世界的整存 blob 是全量栅格，per-chunk 表与它正交并存）。W5 起
//   增补第二条写路径：**保存时冲洗（flush-all）**——流式世界的持久化形态 = 玩家/进度 blob
//   （现行走既有链）+ chunk_edits 全量（已生成未编辑的地形块不存，读档按 seed 确定性重生成
//   ——W1b parity 是正确性根基）；跨会话 blob×附加表 overlay 合并见下方 W5 段。

// ── §29.5-W5 流式世界元数据（D2 后端）+ 读档 overlay 仲裁（D3 语义）──────────────────────
//  计划原文（refactor-plan §29.5.2 W5 + §29.5.3 选型 2）：世界 meta 加 streaming 标志
//  （additive 零迁移）；D3 = opt-in 转换（blob 区成为核心区，境外按需生成 + 经 W3 落盘），
//  转换可逆（标志翻回 = fixed 语义，境外数据不可达但不清除）。本头 = 该域的载体（worldstore
//  一行禁触的 additive 路线延续）：
//
//  · stream_worlds 表（世界标识/streaming 标志/core_w/core_d/建代次/保存代次）：一库一行
//    （一 .sqlite = 一世界，world 列作显式标识与跨检）；core_w/core_d = 核心域方块尺寸
//    （出生区界——原生的流式世界 = 出生参考域；D3 转换世界 = 被转换 fixed 世界的 dims）。
//    世界行本身经现有 Q_INVOKABLE 面创建（生产 = createWorld / openWorld；本类零涉），
//    streaming 标志只住本表——**fixed 世界零标志零活动**（构造门在 GameSession：fixed 会话
//    连 ChunkStore 都不构造，W3 零活动墙同门延续）。
//  · 代次刻度（仲裁的承重前提，头注释立证）：base_gen（建代次）= 标志置位时刻锚定的
//    「世界存代次」——读 save_coord 的 complete_generation（SaveCoordinator r2015 权威的
//    只读消费；无台账 = 0）与既有 save_gen 取 max（再转换 = 老流式时代全部让位新锚——
//    翻回期若经 fixed 链重存，blob 已被重写而 fixed 生产保存不推进任何本域可见代次，保守
//    取「新锚压老行」；该不可判定面的登记 = 保存流统一归 W5b+ 后收敛）。save_gen（保存
//    代次）= 每次流式保存推进（advanceStreamSaveGeneration：max(save_gen, base_gen)+1）。
//  · 读档 overlay 仲裁（逐 chunk 键，GameSession::loadStreamingWorld 消费）：
//      - chunk_edits 行存在且（无 blob 在场，或 行代次 ≥ base_gen）→ 行胜（新者胜；同代 =
//        表胜——行字节按行代次盖写，等号即「行不旧于锚」语义本位）；
//      - 行代次 < base_gen → blob 胜（老流式时代行，已被再转换锚让位）；
//      - 无行 → blob 行存在则 blob 物化（核心区），否则 seed 确定性重生成（W2 生成路径）。
//    流式世界读档形态 = sparse 构造（W1）+ 出生半径预生成 + blob 物化（经现有只读
//    Q_INVOKABLE 面 hasChunks/loadChunks）+ 行全量回灌（跳 population——W3 语义）。
//  · 转换/逆翻：markStreamingWorld（D2 创建与 D3 转换同一落点——置位 + core dims + 代次锚）
//    / clearStreamingWorldFlag（仅翻标志，行保留 = 境外数据不清除）；两操作纯元数据，零
//    blob 搬迁零 chunk_edits 触碰（§29.5.3「零数据搬迁」原文）。
//
// ── 分层 / QML / 线程面 ────────────────────────────────────────────────────────────────
//   World 层（与 worldstore / savecoordinator 同域；依赖 Chunk + Core result + Qt Sql）。
//   非 QObject、无 Q_INVOKABLE / Q_PROPERTY（存档域不进 QML，R20 主线不变量）；零线程原语
//   （t1023c 白名单纪律；调用方 = 主线程驱逐缝 / 收割拍）。只增不改（INSERT OR REPLACE 是
//   同键内容推进，不删行不改写他行——兼容性底线同 SaveCoordinator「只增不改」）。

#include <QByteArray>
#include <QString>
#include <QVector>

#include <QtGlobal> // qint64

#include "chunk.h"   // Chunk 三数组读面（persist 载荷采集；布局权威）
#include "result.h"  // Result<void> / Error（失败面可见——驱逐中止语义的返回域）

// 错误码分域（result.h 百位段约定；2xx 段续号——201..206 = 执行/存档协调既有占用，跳到 210 段）。
constexpr int kErrChunkStoreNotBound = 210; // 未 bind（无库路径）——驱逐 persist 缝 fail-safe 面
constexpr int kErrChunkStoreSql      = 211; // 建表 / upsert / 查询 SQL 失败（含外部 EXCLUSIVE 锁）
constexpr int kErrChunkStoreSize     = 212; // blob 尺寸与目标 chunk 容量不符（dims 病 / 损坏防御）

// ── ChunkBlob：单 chunk 附加表行读回（值类型；generation = 落盘时盖的世界保存代次）────────
struct ChunkStoreBlob
{
    int cx = 0;
    int cz = 0;
    qint64 generation = 0;
    QByteArray voxels;
    QByteArray states;
    QByteArray light;
};

// ── StreamWorldMeta：stream_worlds 行读回/写入（值类型；§29.5-W5 D2/D3 元数据域）──────────
//   streaming = 流式标志（D2；fixed 世界无行 = 零标志）；coreW/coreD = 核心域方块尺寸
//   （出生区界 / D3 = 被转换世界 dims）；baseGen = 建代次（标志置位时刻锚定的世界存代次）；
//   saveGen = 流式保存代次（每次流式保存推进，单调不重置）。QML 零暴露（存档域不进 QML）。
struct StreamWorldMeta
{
    bool streaming = false;
    int coreW = 0;
    int coreD = 0;
    qint64 baseGen = 0;
    qint64 saveGen = 0;
};

// ── ChunkStore：per-chunk 编辑附加表（非 QObject 值语义组件；调用者唯一访问面）──────────
class ChunkStore
{
public:
    ChunkStore() = default;

    // 绑定存档库路径（与 WorldStore::openWorld / SaveCoordinator::bind 同一路径语义——生产 =
    //   W5 存档入口接线；矩阵 = fresh 临时库。未 bind = 写缝恒失败（驱逐中止 = 宁驻留不误删，
    //   P3 fail-safe 承接）、读缝恒 miss（全 Generate 路径）。
    void bind(const QString &dbFilePath) { m_dbPath = dbFilePath; }
    bool isBound() const { return !m_dbPath.isEmpty(); }

    // 世界标识（stream_worlds 主键；一库一世界的显式标识列——生产 = 存档文件名，矩阵 = 腿标）。
    //   缺省空串 = 匿名键（单行现实下自洽；readXxx 语义不变）。
    void setStreamWorldId(const QString &worldId) { m_worldId = worldId; }
    QString streamWorldId() const { return m_worldId; }

    // 落盘（驱逐 persist 缝的生产执行体）：chunk 三数组深拷贝入 BLOB + 代次推进（旧行 +1 /
    //   首行 1）。单行 upsert 事务原子（头注「持久化时机」节）。失败 = Result fail（code 见
    //   上错误码段），旧行原样——调用方（ChunkEvictor 顺序铁律）据此中止驱逐。
    Result<void> persistChunk(int cx, int cz, const Chunk &chunk);

    // 存在性查询（driver savedContentQuery 的生产绑定点 = Load kind 选择权威）：有行 = 该
    //   chunk 有已落盘内容 → 重载走 blob 物化。查询失败（锁 / 病）按 miss 处理（降级生成路径，
    //   诚实降级与 worldstore loadChunks 尺寸跳过同门；不抛不堵）。
    bool hasChunk(int cx, int cz) const;

    // 行读回（重载执行体取数）：命中 true + blob 填充；未命中 / 读败 / 尺寸字段缺失 → false。
    bool loadChunk(int cx, int cz, ChunkStoreBlob &out) const;

    // 诊断面（矩阵断言用）：表内行数（会话内驱逐落盘累计；clean 候选不落盘 = 不增行）。
    int chunkCount() const;

    // ── §29.5-W5 流式世界元数据 + 行全量读回（D2/D3 域；语义见类头 W5 段）──────────────────
    // 行存在性读（isStreamingWorld 类消费面）：无行 → false（fixed 世界 = 无标志无活动）。
    bool readStreamWorldMeta(StreamWorldMeta &out) const;
    // 行全量写（upsert；生产消费面 = markStreamingWorld / advanceStreamSaveGeneration 内部，
    //   裸露形态供矩阵腿构造仲裁相位[代次锚移位]——生产零直调）。
    bool writeStreamWorldMeta(const StreamWorldMeta &m);
    // D2 创建 + D3 转换（同一落点）：置位 + core dims + 代次锚（base = max(save_coord 完整
    //   代次只读消费, 既有 save_gen)；save = base）。SQL 失败 → false（不动旧行）。
    bool markStreamingWorld(int coreW, int coreD);
    // D3 逆翻（标志翻回 = fixed 语义）：仅清标志位，行保留（境外数据不可达但不清除——
    //   §29.5.3 既有口径）。无行 / SQL 失败 → false。
    bool clearStreamingWorldFlag();
    // 流式保存代次推进（保存时冲洗的第一拍）：save = max(save, base) + 1 单语句原子 upsert
    //   （失败面 = 旧行原样，marker 同门）；返回新代次（无行 / SQL 失败 → 0 = 调用方按失败
    //   处理，冲洗据此上报不谎报）。
    qint64 advanceStreamSaveGeneration();
    // 行全量读回（读档 overlay 的行回灌面）：成功返回行数；SQL 失败 → -1（调用方诚实降级
    //   + 告警，hasChunk 单行读同门）。
    int loadAllRows(QVector<ChunkStoreBlob> &out) const;

private:
    QString m_dbPath; // 存档库路径（独立连接开-用-关；不持连接状态）
    QString m_worldId; // stream_worlds 世界标识（setStreamWorldId；缺省空串）
};

#endif // CHUNKSTORE_H
