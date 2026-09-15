#ifndef SAVECOORDINATOR_H
#define SAVECOORDINATOR_H

// R20.15 SaveCoordinator（refactor-plan §29.3 R20.15「先保持 SQLiteAdapter，再统一 SaveGeneration
// 和恢复状态」）。存档协调层：在 **WorldStore（SQLiteAdapter）零改动** 的前提下，把一次保存统一成
// 「一个冻结点 + 一个代次 + 一份成败结论」，并给恢复路径一个可判读的完整/中断状态机。
//
// 五验收逐条落位（本头即契约）：
//   ①「保存不直接读取正在变化的 WorldState」→ 冻结快照协议：saveAll 对活体 World 的唯一读点 =
//      captureSnapshot（§步②）；此后持久化只消费 WorldSaveSnapshot 值——经内部冻结缓冲世界
//      （frozen buffer，见下）喂给下游 WorldStore::saveAll，活体此后再变与本次存档无关。行为级
//      可证：冻结与落盘之间改动活体（故障钩 World 段窗口），重读档 = 冻结点 ≠ 活体。
//   ②「玩家、世界和进度有统一 generation」→ 一次 saveAll 调用 = 一个代次 newGen，覆盖 world
//      （saveAll 事务）+ player（savePlayerData）+ progress（saveProgress）三部分；全部成功才在
//      save_coord 盖 complete 戳——三部分共享同一个 complete_generation。
//   ③「保存失败不会报告成功」→ SaveReceipt.ok() == !isError(error)；任一请求部分失败 / 故障注入
//      / complete 戳写失败 → receipt 一律失败。complete 戳是「成功」的唯一权威：没盖戳 = 没成功，
//      即使三部分数据实际都已落盘（Finalize 段注入 = 崩溃在收尾前，receipt 也不得报成功）。
//   ④「旧档仍可读取」→ 兼容性论证（双向）：协调层状态只落在**新增**的 save_coord 表（key/value
//      两行文本），worldstore.{h,cpp} 零改动——旧代码对未知表全盲（loadMeta 只读 world_meta，
//      其余读路径只碰自己的表）→ 旧程序读新档不受影响；新代码读旧档 = save_coord 不存在 →
//      recover() 报 Fresh（视为无代次台账的合法旧档），全部读路径照旧。user_version /
//      world_version / chunk blob 字节布局零变化——**无任何不可逆存档格式迁移**。
//   ⑤「故障注入测试可以模拟锁、磁盘错误和损坏 Chunk」→ 三条缝：锁 = 真实第二连接 BEGIN
//      EXCLUSIVE（t974 先例；marker-first 协议使其连台账都写不进 → 零部分尝试）；磁盘/崩溃 =
//      SaveFaultHook 测试缝按段注入（生产零挂载恒不触发；World 段兼作「冻结后改动活体」的
//      窗口）；损坏 Chunk = 直接改库中 blob（worldstore loadChunks 的尺寸校验 + chunk_count
//      对账是既有守卫，经协调层写入的档同样受其保护——降级一致语义不破）。
//
// 部分写防护协议（marker-first，崩溃窗口逐段枚举）：
//   步①BeginMark：先在 save_coord 落 generation=newGen（在途标记，独立连接独立事务）。此步失败
//      （如外部锁）→ 立即放弃，**一个部分都不写** → 台账无在途痕迹 = Clean@旧代次。
//   步②③冻结 + World 段注入窗：均未触库（活体只读一次）。注入 → 放弃（gen>complete = Interrupted，
//      保守分类：尝试未完成）。
//   步④-⑦ world/player/progress 逐部分写，**短 路**：前序失败后序不试（杜绝新 player 压旧 world
//      的混合写）。崩在中途 → 已写部分是新版、未写部分是旧版，gen>complete = Interrupted 如实
//      标记「部分写可能存在」，旧完整代次（complete_generation）照常可读。
//   步⑨Finalize：全部成功 → 盖 complete_generation=newGen（独立事务）。崩在其前 → Interrupted。
//   恢复语义（最小落地）：recover() 返回 {generation=最后尝试代次, completeGeneration=最后完整代次,
//   state}；state = Fresh（无台账 = 旧档/新库）/ Clean（相等 = 最后一次保存完整）/ Interrupted
//   （generation > completeGeneration = 最后一次保存未收尾）。Interrupted 的最小恢复动作 = 上报
//   调用方（数据仍可读——SQLite 事务保证每个部分自身原子；调用方按需重存收敛），协调层不回滚
//   不改写存档数据（只增不改 = 兼容性底线）。
//
// 代次语义：generation 单调递增（max(最后尝试, 最后完整)+1），中断/失败消耗号段但不重置不复用；
// complete_generation 只在全部请求部分成功后推进。台账唯一写点 = 本类（worldstore 对其全盲），
// WorldStore 自身的裸 saveAll 不产生也不推进协调层代次（两域正交，r2015a 行为级钉）。
//
// 值纪律 / 所有权边界（立此存照）：本类非 QObject（static_assert QObjectFree 钉）；SaveRequest /
// SaveReceipt / SaveGenerationInfo / WorldSaveSnapshot 全值类型。SaveCoordinator 持两枚**裸指针**
// 成员（result.h QObjectFree 边界登记：指针不构成子对象，评审把关）：m_store（下游 Adapter，
// **不拥有**，调用方保证先于本类析构）与 m_buffer（内部冻结缓冲 World，**拥有**，析构 delete，
// 故本类不可拷贝）。冻结缓冲的必要性：WorldStore::saveAll 从 m_world 读 chunk blob（现有接口
// 零改动），缓冲世界 = 把冻结快照「回放」成一个稳定 World 供其读取；缓冲按 dims 惰性重建并跨
// 保存复用（setWidth 系触 worldgen 属一次性建造成本，beginLoad 零填充无 worldgen——生产接线
// 若在意该成本属后续单优化面，本单零生产接线）。保存期间下游 WorldStore 被临时改绑 buffer、
// 收尾恢复原绑（setWorld 沿可见——本单测试外无观察者；QML 零迁移）。
//
// 分层（PLAN §2）：World 层（与 worldstore 同域；只依赖 World/Chunk/ChunkManager/WorldStore +
// Core result.h + Qt Sql）。零 QML 面（无 Q_OBJECT/Q_PROPERTY/Q_INVOKABLE/QML_NAMED_ELEMENT）。
// **零生产接线**（R20.11 GenerationJob 先例：组件 + 无头实证先行，Main.qml 保存流迁移 = 后续
// 单；故现行保存/读档行为零变化是结构性事实）。

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>
#include <type_traits>
#include <functional>

#include "result.h" // Error / isError / QObjectFree（值纪律编译期钉）

class World;
class WorldStore;

// ── 错误码：2xx 段 = 存档协调域（result.h「百位段 = 域」约定延伸；101 = 队列域已占）────────
constexpr int kErrSaveCoordNotBound = 201;  // 未 bind（无下游 Adapter / 无台账库路径）
constexpr int kErrSaveStoreNotOpen  = 202;  // 下游 WorldStore 未开库 / 未绑活体世界
constexpr int kErrSaveCoordSql      = 203;  // save_coord 台账读写失败（含外部 EXCLUSIVE 锁）
constexpr int kErrSaveSnapshotApply = 204;  // 冻结快照回放失败（缓冲 dims 冲突 / blob 尺寸错）
constexpr int kErrSaveStoreRejected = 205;  // 下游 WorldStore 拒绝（事务失败 / exec 失败）
constexpr int kErrSaveFaultInjected = 206;  // SaveFaultHook 注入触发（测试缝；生产零挂载）

// ── SaveRecoveryState：恢复状态机三态（最小落地；plan「恢复状态」原文域）────────────────
enum class SaveRecoveryState
{
    Fresh = 0,      // 无 save_coord 台账 = 旧档（协调层之前）或新库——视为合法干净态
    Clean,          // generation == completeGeneration = 最后一次保存完整收尾
    Interrupted,    // generation > completeGeneration = 最后一次保存未收尾（部分写可能存在）
};

// ── SaveChunkBlob：单 chunk 冻结三段（voxels/states/light，与 chunks 表行同构）────────────
struct SaveChunkBlob
{
    int cx = 0;
    int cz = 0;
    QByteArray voxels;
    QByteArray states;
    QByteArray light;
};

// ── WorldSaveSnapshot：冻结点全量（验收①本体）───────────────────────────────────────────
// 自持值类型（QByteArray 深拷贝、QList 值语义）——冻结后与活体世界完全解耦；可独立复制。
struct WorldSaveSnapshot
{
    int seed = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    QList<SaveChunkBlob> chunks;

    int chunkCount() const { return chunks.size(); }
    const SaveChunkBlob *findChunk(int cx, int cz) const
    {
        for (const SaveChunkBlob &b : chunks)
            if (b.cx == cx && b.cz == cz) return &b;
        return nullptr;
    }
};

// ── SaveGenerationInfo：recover() 读数（台账两键 + 派生态）────────────────────────────────
struct SaveGenerationInfo
{
    qint64 generation = 0;         // 最后一次**尝试**的代次（在途标记；0 = 无台账）
    qint64 completeGeneration = 0; // 最后一次**完整收尾**的代次（0 = 无完整保存）
    SaveRecoveryState state = SaveRecoveryState::Fresh;
};

// ── SaveReceipt：一次协调保存的统一成败结论（验收③）─────────────────────────────────────
// part 布尔 = 「该部分被请求且成功」；未请求（空 map）= false 但不计错。ok() = 无错误 = 本代次
// 已盖 complete 戳（三部分共享）。失败时 error.code 给出域内原因（2xx 段）。
struct SaveReceipt
{
    qint64 generation = 0;
    bool worldSaved = false;
    bool playerSaved = false;
    bool progressSaved = false;
    Error error{};

    bool ok() const { return !isError(error); }
};

// ── SaveFaultStage / SaveFaultHook：故障注入缝（验收⑤；测试专用，生产零挂载）────────────
// 返回 true = 在该段注入失败。段语义：BeginMark（台账标记前）/ World（冻结后、持久化前——
// 兼作「冻结后改动活体」的观察窗）/ Player / Progress（对应部分写前）/ Finalize（各部分已写、
// complete 戳未盖——崩溃在收尾前的形态）。
enum class SaveFaultStage
{
    None = 0,
    BeginMark,
    World,
    Player,
    Progress,
    Finalize,
};
using SaveFaultHook = std::function<bool(SaveFaultStage)>;

// ── SaveRequest：一次统一保存的请求载荷（全值；playerData/progress 空 map = 该部分不请求）──
// name/chests/furnaces/dispensers/worldTime/bedSpawn 六参形状与 WorldStore::saveAll 现有签名
// 逐一同构（下游原样转发）；playerData → savePlayerData、progress → saveProgress。
struct SaveRequest
{
    QString name;
    QVariantList chests;
    QVariantList furnaces;
    QVariantList dispensers;
    QVariantMap worldTime;
    QVariantMap bedSpawn;
    QVariantMap playerData;
    QVariantMap progress;
};

// ── SaveCoordinator：存档协调层（非 QObject；两枚裸指针成员见头注所有权边界）────────────
class SaveCoordinator
{
public:
    SaveCoordinator() = default;
    ~SaveCoordinator();
    SaveCoordinator(const SaveCoordinator &) = delete;            // 拥有 m_buffer → 不可拷贝
    SaveCoordinator &operator=(const SaveCoordinator &) = delete;

    // 绑定下游 SQLiteAdapter（WorldStore，现状接口零改动）+ 存档库路径（与 openWorld 同一路径；
    //   协调层用它开自己的独立连接读写 save_coord 台账，与 WorldStore 的连接互不占用）。
    void bind(WorldStore *store, const QString &dbFilePath);
    bool isBound() const { return m_store != nullptr && !m_dbPath.isEmpty(); }

    // 故障注入缝（验收⑤）。缺省无钩 = 生产形态，恒不注入。
    void setFaultHook(SaveFaultHook hook) { m_faultHook = std::move(hook); }

    // 恢复状态读数（只读；库/表不存在 → Fresh）。唯一权威判据 = save_coord 两键比对。
    SaveGenerationInfo recover() const;

    // 统一保存（验收①②③本体；协议见头注 marker-first 五段）。失败绝不报告成功。
    SaveReceipt saveAll(const SaveRequest &req);

private:
    // 冻结：对活体 World 的唯一读点（验收①）。逐 chunk 深拷贝三段 blob + seed/dims。
    static WorldSaveSnapshot captureSnapshot(const World &live);
    // 冻结缓冲就绪：dims 惰性重建 + beginLoad(seed) 零填充（无 worldgen；跨保存复用）。
    bool ensureBuffer(const WorldSaveSnapshot &snap);
    // 快照回放进冻结缓冲（逐 chunk memcpy 三段；尺寸不符 → false 防半写）。
    bool applySnapshotToBuffer(const WorldSaveSnapshot &snap);
    // save_coord 台账单键 upsert（独立连接、独立事务；CREATE TABLE IF NOT EXISTS 幂等建表；
    //   外部锁 / 磁盘错误 → false = marker-first 防线的第一闸）。台账唯一写点。
    bool coordUpsert(const char *key, qint64 value) const;

    World *m_buffer = nullptr;   // 内部冻结缓冲世界（拥有；dims 变化时惰性重建）
    WorldStore *m_store = nullptr; // 下游 SQLiteAdapter（不拥有；调用方保证存活期）
    QString m_dbPath;            // 存档库路径（与 WorldStore::openWorld 同一路径）
    SaveFaultHook m_faultHook;   // 故障注入缝（缺省空 = 生产形态）
};

// 值纪律编译期钉（R20.06 体系；r2014d/r2013d 腿内复述先例——头钉被删即双红）。
static_assert(QObjectFree<SaveCoordinator>, "SaveCoordinator must not carry QObject (R20.15)");
static_assert(QObjectFree<WorldSaveSnapshot>, "WorldSaveSnapshot must not carry QObject (R20.15)");
static_assert(QObjectFree<SaveRequest>, "SaveRequest must not carry QObject (R20.15)");
static_assert(QObjectFree<SaveReceipt>, "SaveReceipt must not carry QObject (R20.15)");
static_assert(QObjectFree<SaveGenerationInfo>, "SaveGenerationInfo must not carry QObject (R20.15)");
static_assert(std::is_copy_constructible_v<WorldSaveSnapshot>
                  && std::is_copy_assignable_v<WorldSaveSnapshot>,
              "WorldSaveSnapshot must be independently copyable (freeze value semantics)");

#endif // SAVECOORDINATOR_H
