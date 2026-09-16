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
// ── 表内容与代次语义（SaveCoordinator 口径沿用，头注释立证）────────────────────────────
//   行 = (cx, cz, generation, voxels, states, light)：blob = 驱逐时刻该 chunk 三数组的原样
//   字节（体素 id + state + 光场，布局同 Chunk 数组——worldstore chunks 表行同构收窄到单
//   chunk；**本表自洽**，读回由 World::restoreChunkFromBlob 直接物化）。generation = 该
//   chunk 的**累计落盘代次**（upsert 前读旧行 +1；首次 = 1，单调不重置——SaveCoordinator
//   「代次单调递增、中断不重置不复用」口径的 per-chunk 收窄版）。marker-first 在单行原子
//   upsert 上的等价形态：SQLite 事务原子性 = 「没写进 = 旧行原样保留在旧代次」，失败面上
//   旧行仍是权威（「没盖戳 = 没成功」同义——事务本身就是 marker，无需独立在途标记行）。
//
// ── 持久化时机（设计依据 = 任务书 + §29.5.3 选型 1 (a)）────────────────────────────────
//   驱逐候选 dirty 时**即时落盘**（persistFn 缝实现，生产绑定点 = GameSession 流式会话）——
//   不走整世界 saveAll（fixed 世界的整存 blob 是全量栅格，per-chunk 表与它正交并存；跨会话
//   读档时的 blob×附加表 overlay 合并 = W5/D3 域，登记非目标——本单只做会话内驱逐+重载闭环）。
//
// ── 分层 / QML / 线程面 ────────────────────────────────────────────────────────────────
//   World 层（与 worldstore / savecoordinator 同域；依赖 Chunk + Core result + Qt Sql）。
//   非 QObject、无 Q_INVOKABLE / Q_PROPERTY（存档域不进 QML，R20 主线不变量）；零线程原语
//   （t1023c 白名单纪律；调用方 = 主线程驱逐缝 / 收割拍）。只增不改（INSERT OR REPLACE 是
//   同键内容推进，不删行不改写他行——兼容性底线同 SaveCoordinator「只增不改」）。

#include <QByteArray>
#include <QString>

#include <QtGlobal> // qint64

#include "chunk.h"   // Chunk 三数组读面（persist 载荷采集；布局权威）
#include "result.h"  // Result<void> / Error（失败面可见——驱逐中止语义的返回域）

// 错误码分域（result.h 百位段约定；2xx 段续号——201..206 = 执行/存档协调既有占用，跳到 210 段）。
constexpr int kErrChunkStoreNotBound = 210; // 未 bind（无库路径）——驱逐 persist 缝 fail-safe 面
constexpr int kErrChunkStoreSql      = 211; // 建表 / upsert / 查询 SQL 失败（含外部 EXCLUSIVE 锁）
constexpr int kErrChunkStoreSize     = 212; // blob 尺寸与目标 chunk 容量不符（dims 病 / 损坏防御）

// ── ChunkBlob：单 chunk 附加表行读回（值类型；generation = 累计落盘代次）────────────────
struct ChunkStoreBlob
{
    int cx = 0;
    int cz = 0;
    qint64 generation = 0;
    QByteArray voxels;
    QByteArray states;
    QByteArray light;
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

private:
    QString m_dbPath; // 存档库路径（独立连接开-用-关；不持连接状态）
};

#endif // CHUNKSTORE_H
